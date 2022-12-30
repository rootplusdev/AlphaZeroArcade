#pragma once

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <Eigen/Core>
#include <EigenRand/EigenRand>

#include <common/AbstractSymmetryTransform.hpp>
#include <common/BasicTypes.hpp>
#include <common/DerivedTypes.hpp>
#include <common/GameStateConcept.hpp>
#include <common/MctsResults.hpp>
#include <common/NeuralNet.hpp>
#include <common/TensorizorConcept.hpp>
#include <util/AtomicSharedPtr.hpp>
#include <util/BitSet.hpp>
#include <util/CppUtil.hpp>
#include <util/EigenTorch.hpp>
#include <util/LRUCache.hpp>
#include <util/Profiler.hpp>

namespace common {

/*
 * TODO: move the various inner-classes of Mcts_ into separate files as standalone-classes.
 *
 * TODO: use CRTP for slightly more elegant inheritance mechanics.
 */
template<GameStateConcept GameState, TensorizorConcept<GameState> Tensorizor>
class Mcts_ {
private:
  class SearchThread;

public:
  /*
   * "Positions in the queue are evaluated by the neural network using a mini-batch size of 8"
   *
   * From page 26 of AlphaGoZero paper
   *
   * "https://discovery.ucl.ac.uk/id/eprint/10045895/1/agz_unformatted_nature.pdf
   */
  static constexpr int kDefaultBatchSize = 8;

  static constexpr bool kEnableProfiling = IS_MACRO_ASSIGNED_TO_1(PROFILE_MCTS);
  static constexpr bool kEnableVerboseProfiling = IS_MACRO_ASSIGNED_TO_1(PROFILE_MCTS_VERBOSE);

  static constexpr int kNumPlayers = GameState::kNumPlayers;
  static constexpr int kNumGlobalActions = GameState::kNumGlobalActions;
  static constexpr int kMaxNumLocalActions = GameState::kMaxNumLocalActions;

  using TensorizorTypes = TensorizorTypes_<Tensorizor>;
  using GameStateTypes = GameStateTypes_<GameState>;

  using MctsResults = MctsResults_<GameState>;
  using SymmetryTransform = AbstractSymmetryTransform<GameState, Tensorizor>;
  using ValueProbDistr = typename GameStateTypes::ValueProbDistr;
  using GameOutcome = typename GameStateTypes::GameOutcome;
  using ActionMask = util::BitSet<kNumGlobalActions>;
  using LocalPolicyProbDistr = typename GameStateTypes::LocalPolicyProbDistr;
  using GlobalPolicyCountDistr = typename GameStateTypes::GlobalPolicyCountDistr;

  using FullInputTensor = typename TensorizorTypes::DynamicInputTensor;
  using FullValueArray = typename GameStateTypes::template ValueArray<Eigen::Dynamic>;
  using FullPolicyArray = typename GameStateTypes::template PolicyArray<Eigen::Dynamic>;
  using ValueArray1D = typename GameStateTypes::ValueArray1D;
  using PolicyArray1D = typename GameStateTypes::PolicyArray1D;

  using time_point_t = std::chrono::time_point<std::chrono::steady_clock>;

  /*
   * Params pertains to a single Mcts instance.
   *
   * By contrast, SimParams pertains to each individual sim() call.
   */
  struct Params {
    std::string nnet_filename;
    int num_search_threads = kDefaultBatchSize;
    int batch_size_limit = kDefaultBatchSize;
    bool run_offline = false;
    int offline_tree_size_limit = 4096;
    int64_t nn_eval_timeout_ns = util::ms_to_ns(5);;
    size_t cache_size = 4096;

    float root_softmax_temperature = 1.03;
    float cPUCT = 1.1;
    float dirichlet_mult = 0.25;
    float dirichlet_alpha = 0.03;
    bool allow_eliminations = true;
#ifdef PROFILE_MCTS
    std::string profiling_dir;
#endif  // PROFILE_MCTS
  };

  static Params global_params_;
  static void add_options(boost::program_options::options_description& desc);

  /*
   * SimParams pertain to a single call to sim(). Even given a single Mcts instance, different sim() calls can have
   * different SimParams. For instance, for KataGo, there are "fast" searches and "full" searches, which differ
   * in their tree_size_limit and dirchlet settings.
   *
   * By contrast, Params pertains to a single Mcts instance.
   */
  struct SimParams {
    int tree_size_limit = 100;
    bool disable_noise = false;
  };

private:
  class NNEvaluation {
  public:
    NNEvaluation(const ValueArray1D& value, const PolicyArray1D& policy, const ActionMask& valid_actions, float inv_temp);
    const ValueProbDistr& value_prob_distr() const { return value_prob_distr_; }
    const LocalPolicyProbDistr& local_policy_prob_distr() const { return local_policy_prob_distr_; }

  protected:
    ValueProbDistr value_prob_distr_;
    LocalPolicyProbDistr local_policy_prob_distr_;
  };
  using NNEvaluation_sptr = std::shared_ptr<NNEvaluation>;
  using NNEvaluation_asptr = util::AtomicSharedPtr<NNEvaluation>;

  /*
   * A Node consists of 4 main groups of non-const member variables:
   *
   * LAZILY INITIALIZED DATA: state + action -> state', computed lazily since not immediately needed upon child expand
   * CHILDREN DATA: the addresses/number of children nodes, needed for tree traversal
   * NEURAL NETWORK EVALUATION: policy/value vectors that come from neural net evaluation
   * STATS: values that get updated throughout MCTS via backpropagation
   *
   * Of these, only STATS are continuously changing. The others are written only once. They are non-const in the
   * sense that they are lazily written, after-object-construction.
   *
   * During MCTS, multiple search threads will try to read and write these values. The MCTS literature is filled with
   * approaches on how to minimize thread contention, including various "lockfree" approaches that tolerate various
   * race conditions.
   *
   * See for example this 2009 paper: https://webdocs.cs.ualberta.ca/~mmueller/ps/enzenberger-mueller-acg12.pdf
   *
   * For now, I am achieving thread-safety by having three mutexes per-Node, one for each of the above three
   * categories. Once we have appropriate tooling to profile performance and detect bottlenecks, we can improve this
   * implementation.
   *
   * NAMING NOTE: Methods with a leading underscore are NOT thread-safe. Such methods are expected to be called in
   * a context that guarantees the appropriate level of thread-safety.
   */
  class Node {
  public:
    Node(Node* parent, action_index_t action);
    Node(const Tensorizor&, const GameState&, const GameOutcome&, bool disable_noise);
    Node(const Node& node, bool prune_parent=false);

    void debug_dump() const;

    /*
     * Releases the memory occupied by this and by all descendents, EXCEPT for the descendents of
     * protected_child (which is guaranteed to be an immediate child of this if non-null). Note that the memory of
     * protected_child itself IS released; only the *descendents* of protected_child are protected.
     *
     * In the current implementation, this works by calling delete and delete[] and by recursing down the tree.
     *
     * In future implementations, if we have object pools, this might work by releasing to an object pool.
     *
     * Also, in the future, we might have Monte Carlo *Graph* Search (MCGS) instead of MCTS. In this future, a given
     * Node might have multiple parents, so release() might decrement smart-pointer reference counts instead.
     */
    void _release(Node* protected_child=nullptr);

    /*
     * Set child->parent = this for all children of this.
     *
     * This is the only reason that stable_data_ is not const.
     */
    void _adopt_children();

    std::mutex& lazily_initialized_data_mutex() { return lazily_initialized_data_mutex_; }
    std::mutex& stats_mutex() { return stats_mutex_; }

    GlobalPolicyCountDistr get_effective_counts() const;
    bool expand_children(SearchThread*);  // returns false iff already has children
    void backprop(const ValueProbDistr& value);
    void backprop_with_virtual_undo(const ValueProbDistr& value);
    void virtual_backprop();
    void undo_virtual_backprop();
    void perform_eliminations(const ValueProbDistr& outcome);
    ValueArray1D make_virtual_loss() const;
    void _lazy_init();

    action_index_t action() const { return stable_data_.action_; }
    Node* parent() const { return stable_data_.parent_; }
    bool is_root() const { return !stable_data_.parent_; }
    bool disable_noise() const { return stable_data_.disable_noise_; }

    const Tensorizor& _tensorizor() const { return lazily_initialized_data_.union_.data_.tensorizor_; }
    const GameState& _state() const { return lazily_initialized_data_.union_.data_.state_; }
    const GameOutcome& _outcome() const { return lazily_initialized_data_.union_.data_.outcome_; }
    symmetry_index_t _sym_index() const { return lazily_initialized_data_.union_.data_.sym_index_; }
    player_index_t _current_player() const { return lazily_initialized_data_.union_.data_.current_player_; }
    const ActionMask& _valid_action_mask() const { return lazily_initialized_data_.union_.data_.valid_action_mask_; }
    bool _lazily_initialized() const { return lazily_initialized_data_.initialized_; }

    bool _has_children() const { return children_data_.num_children_; }
    int _num_children() const { return children_data_.num_children_; }
    Node* _get_child(int c) const { return children_data_.first_child_ + c; }
    Node* _find_child(action_index_t action) const;

    const auto& _value_avg() const { return stats_.value_avg_; }
    bool _eliminated() const { return stats_.eliminated_; }
    float _V_floor(player_index_t p) const { return stats_.V_floor_(p); }
    float _effective_value_avg(player_index_t p) const { return stats_.effective_value_avg_(p); }
    int _effective_count() const { return stats_.eliminated_ ? 0 : stats_.count_; }
    bool _has_certain_outcome() const { return stats_.V_floor_.sum() > 0; }  // won, lost, OR drawn positions
    bool _can_be_eliminated() const { return stats_.V_floor_.maxCoeff() == 1; }  // won/lost positions, not drawn ones

    NNEvaluation_sptr _evaluation() const { return evaluation_.load(); }
    void _set_evaluation(NNEvaluation_sptr eval) { evaluation_.store(eval); }

  private:
    float _get_max_V_floor_among_children(player_index_t p) const;
    float _get_min_V_floor_among_children(player_index_t p) const;

    struct stable_data_t {
      stable_data_t(Node* parent, action_index_t action, bool disable_noise);
      stable_data_t(const stable_data_t& data, bool prune_parent);

      Node* parent_;
      action_index_t action_;
      bool disable_noise_;
    };

    struct lazily_initialized_data_t {
      struct data_t {
        data_t(Node* parent, action_index_t action);
        data_t(const Tensorizor&, const GameState&, const GameOutcome&);

        Tensorizor tensorizor_;
        GameState state_;
        GameOutcome outcome_;
        ActionMask valid_action_mask_;
        player_index_t current_player_;
        symmetry_index_t sym_index_;
      };

      union union_t {
        union_t() : dummy_(false) {}
        union_t(const union_t& u) : data_(u.data_) {}
        union_t(Node* parent, action_index_t action) : data_(parent, action) {}
        union_t(const Tensorizor& tensorizor, const GameState& state, const GameOutcome& outcome)
            : data_(tensorizor, state, outcome) {}

        data_t data_;
        bool dummy_;
      };

      lazily_initialized_data_t() = default;
      lazily_initialized_data_t(Node* parent, action_index_t action)
        : union_(parent, action)
        , initialized_(true) {}
      lazily_initialized_data_t(const Tensorizor& tensorizor, const GameState& state, const GameOutcome& outcome)
        : union_(tensorizor, state, outcome)
        , initialized_(true) {}

      union_t union_;
      bool initialized_ = false;
    };

    struct children_data_t {
      Node* first_child_ = nullptr;
      int num_children_ = 0;
    };

    struct stats_t {
      stats_t();

      ValueArray1D value_avg_;
      ValueArray1D effective_value_avg_;
      ValueArray1D V_floor_;
      int count_ = 0;
      bool eliminated_ = false;
    };

    mutable std::mutex lazily_initialized_data_mutex_;
    mutable std::mutex children_data_mutex_;
    mutable std::mutex stats_mutex_;
    stable_data_t stable_data_;  // effectively const
    lazily_initialized_data_t lazily_initialized_data_;
    children_data_t children_data_;
    NNEvaluation_asptr evaluation_;
    stats_t stats_;
  };

  class SearchThread {
  public:
    SearchThread(Mcts_* mcts, int thread_id);
    ~SearchThread();
    void join();
    void kill();
    void launch(int tree_size_limit);
    int thread_id() const { return thread_id_; }

    enum region_t {
      kCheckVisitReady = 0,
      kAcquiringLazilyInitializedDataMutex = 1,
      kLazyInit = 2,
      kBackpropOutcome = 3,
      kPerformEliminations = 4,
      kMisc = 5,
      kCheckingCache = 6,
      kAcquiringBatchMutex = 7,
      kWaitingUntilBatchReservable = 8,
      kTensorizing = 9,
      kIncrementingCommitCount = 10,
      kWaitingForReservationProcessing = 11,
      kVirtualBackprop = 12,
      kWaitingForChildrenDataMutex = 13,
      kAllocationChildrenMemory = 14,
      kConstructingChildren = 15,
      kPUCT = 16,
      kWaitingForStatsMutex = 17,
      kBackpropEvaluation = 18,
      kNumRegions = 19
    };

    using profiler_t = util::Profiler<int(kNumRegions), kEnableVerboseProfiling>;

    void record_for_profiling(region_t region);
    void dump_profiling_stats();

#ifdef PROFILE_MCTS
    profiler_t* get_profiler() { return &profiler_; }
    FILE* get_profiling_file() const { return profiling_file_; }
    const char* get_profiler_name() const { return profiler_name_.c_str(); }
    void init_profiling(const char* filename, const char* name) {
      profiling_file_ = fopen(filename, "w");
      profiler_name_ = name;
      profiler_.skip_next_n_dumps(5);
    }
    void close_profiling_file() { fclose(profiling_file_); }

    profiler_t profiler_;
    std::string profiler_name_;
    FILE* profiling_file_ = nullptr;
    int profile_count_ = 0;
#else  // PROFILE_MCTS
    constexpr profiler_t* get_profiler() { return nullptr; }
    static FILE* get_profiling_file() { return nullptr; }
    const char* get_profiler_name() const { return nullptr; }
    void init_profiling(const char* filename, const char* name) {}
    void close_profiling_file() {}
#endif  // PROFILE_MCTS

  private:
    Mcts_* const mcts_;
    std::thread* thread_ = nullptr;
    const int thread_id_;
  };

  using search_thread_vec_t = std::vector<SearchThread*>;

  /*
   * The NNEvaluationService services multiple search threads, which may belong to multiple Mcts instances (if two
   * Mcts agents are playing against each other for instance).
   *
   * The main API is the evaluate() method. It tensorizes a game state, passes the tensor to a neural network, and
   * returns the output. Under its hood, it batches multiple evaluate() requests in order to maximize GPU throughput.
   * This batching is transparent to the caller, as the evaluate() method blocks internally until the batch is full
   * (or until a timeout is hit).
   *
   * Batching of N evaluations is accomplished by maintaining a length-N array of evaluation objects, and various
   * tensors (for nnet input and output) of shape (N, ...). Each evaluate() call gets assigned a particular index i
   * with 0 <= i < N, and writes to the i'th slot of these data structures. A separate evaluation thread issues the
   * nnet evaluation and writes to the i'th slot of the output data structures.
   *
   * The service has an LRU cache, which helps to avoid the costly GPU operations when possible.
   *
   * When the number of search threads is 1, we simply do everything in the main thread, mainly for easier debugging.
   * When we have more than 1 search thread, we encounter sensitive thread-safety considerations. Here is a detailed
   * description of how this implementation handles them.
   *
   * There are two mutexes:
   *
   * - cache_mutex_: prevents race-conditions on cache reads/writes - especially important because without locking,
   *                 cache eviction can lead to a key-value pair disappearing after checking for the key
   *
   * - batch_mutex_: prevents race-conditions on batch reads/writes
   *
   * There are three separate members that are used to synchronize batch writing, all protected by batch_mutex_:
   *
   * - batch_reserve_index_: keeps track of the next batch slot to write to. A search thread does a protected
   *                         read + increment of this member, and then with the mutex released, starts the work of
   *                         tensorizing the game state and writing to the appropriate slot.
   *
   * - batch_commit_count_: Once the search thread has finished tensorizing and writing to its slot, it increments
   *                        the batch_commit_count_. This allows the service thread to detect when it is safe to
   *                        evaluate the batch.
   *
   * - batch_unread_count_: After the service thread has written the N nnet outputs, the search threads can start
   *                        reading those outputs. Without care, a race condition could cause eager search threads to
   *                        overwrite that data before the prior threads fully read that data. This unread count
   *                        value helps to prevent this race condition.
   *
   * Note that batch_reserve_index_ and batch_commit_count_ could have been rolled into one single count. This would
   * have been simpler, but increased the duration for which batch_mutex_ is held (as the tensorizing would then
   * need to happen under the mutex). Separation allows tensorization to occur outside any mutex locks.
   *
   * Search threads will detect that the batch is fully saturated by checking batch_reserve_index_, and wait until it
   * is reset by the evaluation thread before proceeding with tensorization.
   *
   * The evaluation thread will detect that search threads are mid-writing by comparing batch_commit_count_ to
   * batch_reserve_index_. Only when they are equal will it proceed to query the GPU and write to the output slots.
   */
  class NNEvaluationService {
  public:
    /*
     * Constructs an evaluation thread and returns it.
     *
     * If another thread with the given nnet_filename has already been create()'d, then returns that. If that returned
     * thread does not match the thread parameters (batch_size, nn_eval_timeout_ns, cache_size), then raises an
     * exception.
     */
    static NNEvaluationService* create(const Mcts_* mcts);

    /*
     * Instantiates the thread_ member if not yet instantiated. This spawns a new thread.
     *
     * If the thread_ member is already instantiated, then this is a no-op.
     */
    void connect();

    void disconnect();

    /*
     * Called by search threads. Returns immediately if we get a cache-hit. Otherwise, blocks on the service thread.
     *
     * Note that historically, parallel MCTS did evaluations asynchronously. AlphaGo Zero was the first version that
     * switched to blocking evaluations.
     *
     * "Compared to the MCTS in AlphaGo Fan and AlphaGo Lee, the principal differences are...each search thread simply
     * waits for the neural network evaluation, rather than performing evaluation and backup asynchronously"
     *
     * - Mastering the Game of Go without Human Knowledge (page 27)
     *
     * https://discovery.ucl.ac.uk/id/eprint/10045895/1/agz_unformatted_nature.pdf
     */
    NNEvaluation_sptr evaluate(
        SearchThread* thread, const Tensorizor& tensorizor, const GameState& state, const ActionMask& valid_action_mask,
        symmetry_index_t sym_index, float inv_temp, bool single_threaded);

    void get_cache_stats(int& hits, int& misses, int& size, float& hash_balance_factor) const;

  private:
    NNEvaluationService(const boost::filesystem::path& net_filename, int batch_size_limit,
                        std::chrono::nanoseconds timeout_duration, size_t cache_size,
                        const boost::filesystem::path& profiling_dir);
    ~NNEvaluationService();

    void batch_evaluate();
    void loop();

    bool all_batch_reservations_committed() const { return batch_reserve_index_ == batch_commit_count_; }
    bool batch_reservations_full() const { return batch_reserve_index_ == batch_size_limit_; }
    bool batch_reservations_empty() const { return batch_reserve_index_ == 0; }
    bool batch_reservable() const { return batch_unread_count_ == 0 && batch_reserve_index_ < batch_size_limit_; }

    using instance_map_t = std::map<boost::filesystem::path, NNEvaluationService*>;
    using cache_key_t = StateEvaluationKey<GameState>;
    using cache_t = util::LRUCache<cache_key_t, NNEvaluation_asptr>;

    struct evaluation_data_t {
      NNEvaluation_asptr eval_ptr;

      cache_key_t cache_key;
      ActionMask valid_actions;
      SymmetryTransform* transform;
      float inv_temp;
    };

    enum region_t {
      kAcquiringBatchMutex = 0,
      kWaitingForFirstReservation = 1,
      kWaitingForLastReservation = 2,
      kWaitingForCommits = 3,
      kCopyingCpuToGpu = 4,
      kEvaluatingNeuralNet = 5,
      kCopyingToPool = 6,
      kAcquiringCacheMutex = 7,
      kFinishingUp = 8,
      kNumRegions = 9
    };

    using profiler_t = util::Profiler<int(kNumRegions), kEnableVerboseProfiling>;

    struct profiling_stats_t {
      time_point_t start_times[kNumRegions + 1];
      int batch_size;
    };

    void record_for_profiling(region_t region);
    void dump_profiling_stats();

#ifdef PROFILE_MCTS
    profiler_t* get_profiler() { return &profiler_; }
    FILE* get_profiling_file() const { return profiling_file_; }
    const char* get_profiler_name() const { return profiler_name_.c_str(); }
    void init_profiling(const char* filename, const char* name) {
      profiling_file_ = fopen(filename, "w");
      profiler_name_ = name;
      profiler_.skip_next_n_dumps(5);
    }
    void close_profiling_file() { fclose(profiling_file_); }

    profiler_t profiler_;
    std::string profiler_name_;
    FILE* profiling_file_ = nullptr;
    int profile_count_ = 0;
#else  // PROFILE_MCTS
    constexpr profiler_t* get_profiler() { return nullptr; }
    static FILE* get_profiling_file() { return nullptr; }
    const char* get_profiler_name() const { return nullptr; }
    void init_profiling(const char* filename, const char* name) {}
    void close_profiling_file() {}
#endif  // PROFILE_MCTS

    static instance_map_t instance_map_;

    std::thread* thread_ = nullptr;
    std::mutex cache_mutex_;
    std::mutex batch_mutex_;
    std::condition_variable cv_service_loop_;
    std::condition_variable cv_evaluate_;

    NeuralNet net_;
    FullPolicyArray policy_batch_;
    FullValueArray value_batch_;
    FullInputTensor input_batch_;
    evaluation_data_t* evaluation_data_batch_;

    common::NeuralNet::input_vec_t input_vec_;
    torch::Tensor torch_input_gpu_;
    cache_t cache_;

    const std::chrono::nanoseconds timeout_duration_;
    const int batch_size_limit_;

    time_point_t deadline_;
    int batch_reserve_index_ = 0;
    int batch_commit_count_ = 0;
    int batch_unread_count_ = 0;

    int num_connections_ = 0;

    int cache_hits_ = 0;
    int cache_misses_ = 0;
  };

public:
  /*
   * In multi-threaded mode, the search threads can continue running outside of the main sim() method. For example,
   * when playing against a human player, we can continue growing the MCTS tree while the human player thinks.
   */
  static constexpr int kDefaultMaxTreeSize =  4096;

  Mcts_(const Params& params);
  Mcts_() : Mcts_(global_params_) {}
  ~Mcts_();

  const Params& params() const { return params_; }
  void start();
  void clear();
  void receive_state_change(player_index_t, const GameState&, action_index_t, const GameOutcome&);
  const MctsResults* sim(const Tensorizor& tensorizor, const GameState& game_state, const SimParams& params);
  void visit(SearchThread* thread, Node*, int depth);

  int num_search_threads() const { return params_.num_search_threads; }

  void start_search_threads(int tree_size_limit);
  void wait_for_search_threads();
  void stop_search_threads();
  void run_search(SearchThread* thread, int tree_size_limit);
  void get_cache_stats(int& hits, int& misses, int& size, float& hash_balance_factor) const;

#ifdef PROFILE_MCTS
  boost::filesystem::path profiling_dir() const { return boost::filesystem::path(params_.profiling_dir); }
#else  // PROFILE_MCTS
  boost::filesystem::path profiling_dir() const { return {}; }
#endif  // PROFILE_MCTS

private:
  static void init_profiling_dir(const std::string& profiling_dir);
  bool check_visit_ready(SearchThread* thread, int tree_size_limit) const;

  eigen_util::UniformDirichletGen<float> dirichlet_gen_;
  Eigen::Rand::P8_mt19937_64 rng_;

  const Params params_;
  search_thread_vec_t search_threads_;
  NNEvaluationService* nn_eval_service_ = nullptr;

  Node* root_ = nullptr;
  MctsResults results_;

  std::mutex search_mutex_;
  std::condition_variable cv_search_;
  int num_active_search_threads_ = 0;
  bool search_active_ = false;
};

}  // namespace common

#include <common/inl/Mcts.inl>
