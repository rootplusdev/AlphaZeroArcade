#pragma once

#include <cstdint>
#include <iterator>
#include <mutex>

#include <core/DerivedTypes.hpp>
#include <core/GameStateConcept.hpp>
#include <core/TensorizorConcept.hpp>
#include <mcts/Constants.hpp>
#include <mcts/NNEvaluation.hpp>
#include <mcts/TypeDefs.hpp>
#include <util/AtomicSharedPtr.hpp>

namespace mcts {

/*
 * A Node consists of n=3 main groups of non-const member variables:
 *
 * children_data_: edges to children nodes, needed for tree traversal
 * evaluation_data_: policy/value vectors that come from neural net evaluation
 * stats_: values that get updated throughout MCTS via backpropagation
 *
 * During MCTS, multiple search threads will try to read and write these values. Thread-safety is achieved in a
 * high-performance manner through mutexes and condition variables.
 */
template<core::GameStateConcept GameState, core::TensorizorConcept<GameState> Tensorizor>
class Node {
public:
  using asptr = util::AtomicSharedPtr<Node>;

  using NNEvaluation = mcts::NNEvaluation<GameState>;
  using GameStateTypes = core::GameStateTypes<GameState>;

  static constexpr int kMaxNumLocalActions = GameState::kMaxNumLocalActions;
  static constexpr int kNumGlobalActions = GameStateTypes::kNumGlobalActions;
  static constexpr int kNumPlayers = GameState::kNumPlayers;
  static constexpr int kEdgeDataChunkSize = std::min(8, kMaxNumLocalActions);

  using ActionMask = typename GameStateTypes::ActionMask;
  using GameOutcome = typename GameStateTypes::GameOutcome;
  using LocalPolicyArray = typename GameStateTypes::LocalPolicyArray;
  using PolicyTensor = typename GameStateTypes::PolicyTensor;
  using ValueArray = typename GameStateTypes::ValueArray;
  using dtype = typename GameStateTypes::dtype;

  enum evaluation_state_t : int8_t {
    kUnset,
    kSet,
  };

  struct stable_data_t {
    stable_data_t(const Tensorizor&, const GameState&, const GameOutcome&);

    int num_valid_actions() const { return valid_action_mask.count(); }  // consider saving in member variable

    Tensorizor tensorizor;
    GameState state;
    GameOutcome outcome;
    ActionMask valid_action_mask;
    core::seat_index_t current_player;
    core::symmetry_index_t sym_index;
  };

  /*
   * Thread-safety policy: mutex on writes, not on reads. On reads, we simply do a copy of the entire struct, in
   * order to simplify the reasoning about race-conditions.
   *
   * Note that for the non-primitive members, the writes are not guaranteed to be atomic. A non-mutex-protected-read
   * may encounter partially-updated arrays when reading such members. Furthermore, there are no guarantees in this
   * implementation of the order of member-updates when writing, meaning that non-mutex-protected-reads might
   * encounter states where some of the members have been updated while other have not.
   *
   * Despite the above caveats, we can still read without a mutex, since all usages are ok with arbitrarily-partially
   * written data.
   */
  struct stats_t {
    stats_t();
    void add(const ValueArray& value);
    void add_virtual_loss(const ValueArray& loss);
    void correct_virtual_loss(const ValueArray& correction);
    ValueArray compute_clipped_update_value(const stats_t& edge_stats, float eps) const;

    ValueArray value_avg;
    int count = 0;
    int virtual_count = 0;  // only used for debugging
  };

  /*
   * An edge_data_t corresponds to an action that can be taken from this node. It is instantiated only when the
   * action is expanded. It stores both a (smart) pointer to the child node and the stats for the edge. Edge stats
   * are used to support MCTS mechanics.
   *
   * When instantiating an edge_data_t, we assign the members in the order child_, local_action_, action_. The
   * instantiated() check looks at the last of these (action_). This discipline ensures that lockfree usages work
   * properly. Write ordering is enforced via the volatile keyword.
   */
  struct edge_data_t {
    edge_data_t* instantiate(core::action_index_t a, core::local_action_index_t l, asptr c);
    bool instantiated() const { return action_ >= 0; }
    core::action_index_t action() const { return action_; }
    core::local_action_index_t local_action() const { return local_action_; }
    asptr child() const { return const_cast<asptr&>(child_); }
    stats_t& stats() { return stats_; }
    const stats_t& stats() const { return stats_; }

  private:
    volatile core::action_index_t action_ = -1;
    volatile core::local_action_index_t local_action_ = -1;
    volatile asptr child_;
    stats_t stats_;
  };

  /*
   * A chunk of edge_data_t's, together with a pointer to the next chunk. This chunking results in more efficient
   * memory access patterns. In particular, if a node is not expanded to more than kEdgeDataChunkSize children, then
   * we avoid dynamic memory allocation.
   */
  struct edge_data_chunk_t {
    ~edge_data_chunk_t() { delete next; }
    edge_data_t* find(core::local_action_index_t l);
    edge_data_t* insert(core::action_index_t a, core::local_action_index_t l, asptr child);

    edge_data_t data[kEdgeDataChunkSize];
    edge_data_chunk_t* next = nullptr;
  };

  /*
   * children_data_t maintains a logical map of action -> edge_data_t. It is implemented as a chunked linked list.
   * Compared to a more natural std::map representation, lookups are theoretically slower (O(N) vs O(log(N))). However,
   * the linked list representation allows us to avoid mutexes on reads, which is a performance win, since reads are
   * much more common than writes. We can avoid mutexes on reads because we only append to the linked list at the end,
   * and because our reads are ok with arbitrarily-partially written data.
   *
   * When writing, we need to grab children_mutex_.
   *
   * TODO: use a custom allocator for the linked list.
   */
  struct children_data_t {

    template<bool is_const>
    struct iterator_base_t {
      using chunk_t = std::conditional_t<is_const, const edge_data_chunk_t, edge_data_chunk_t>;

      iterator_base_t(chunk_t* chunk=nullptr, int index=0);

    protected:
      bool equals(const iterator_base_t& other) const { return chunk == other.chunk && index == other.index; }
      void increment();
      void nullify_if_at_end();

      chunk_t* chunk;
      int index;
    };

    struct iterator : public iterator_base_t<false> {
      using base_t = iterator_base_t<false>;

      using iterator_category = std::forward_iterator_tag;
      using difference_type   = std::ptrdiff_t;
      using value_type        = edge_data_t;
      using pointer           = value_type*;
      using reference         = value_type&;

      using base_t::base_t;
      iterator& operator++() { this->increment(); return *this; }
      iterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }
      bool operator==(const iterator& other) const { return this->equals(other); }
      bool operator!=(const iterator& other) const { return !(*this == other); }
      edge_data_t& operator*() const { return this->chunk->data[this->index]; }
      edge_data_t* operator->() const { return &this->chunk->data[this->index]; }
    };

    struct const_iterator : public iterator_base_t<true> {
      using base_t = iterator_base_t<true>;

      using iterator_category = std::forward_iterator_tag;
      using difference_type   = std::ptrdiff_t;
      using value_type        = edge_data_t;
      using pointer           = value_type*;
      using reference         = value_type&;

      using base_t::base_t;
      const_iterator& operator++() { this->increment(); return *this; }
      const_iterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }
      bool operator==(const const_iterator& other) const { return this->equals(other); }
      bool operator!=(const const_iterator& other) const { return !(*this == other); }
      const edge_data_t& operator*() const { return this->chunk->data[this->index]; }
      const edge_data_t* operator->() const { return &this->chunk->data[this->index]; }
    };

    static_assert(std::forward_iterator<iterator>);
    static_assert(std::forward_iterator<const_iterator>);

    ~children_data_t() { delete first_chunk_.next; }

    /*
     * Traverses the chunked linked list and attempts to find an edge_data_t corresponding to the given action. If
     * it finds it, then it returns a pointer to the edge_data_t. Otherwise, it returns nullptr.
     *
     * The expected usage is to call this first without grabbing children_mutex_, to optimize for the more common case
     * where the action has already been expanded. If the action has not been expanded, then we grab children_mutex_
     * and call insert(). Note that there is a race-condition possible; insert() deals with this possibility
     * appropriately.
     */
    edge_data_t* find(core::local_action_index_t l) { return first_chunk_.find(l); }

    /*
     * Inserts a new edge_data_t into the chunked linked list with the given action/Node, and returns a pointer to it.
     *
     * It is possible that an edge_data_t already exists for this action due to a race condition. In this case, returns
     * a pointer to the existing entry.
     */
    edge_data_t* insert(core::action_index_t a, core::local_action_index_t l, asptr child) {
      return first_chunk_.insert(a, l, child);
    }

    iterator begin() { return iterator(&first_chunk_, 0); }
    iterator end() { return iterator(nullptr, 0); }
    const_iterator begin() const { return const_iterator(&first_chunk_, 0); }
    const_iterator end() const { return const_iterator(nullptr, 0); }

  private:
    edge_data_chunk_t first_chunk_;
  };

  struct evaluation_data_t {
    NNEvaluation::asptr ptr;
    LocalPolicyArray local_policy_prob_distr;
    evaluation_state_t state = kUnset;
  };

  Node(const Tensorizor&, const GameState&, const GameOutcome&);

  void debug_dump() const;

  std::condition_variable& cv_evaluate() { return cv_evaluate_; }
  std::mutex& evaluation_data_mutex() const { return evaluation_data_mutex_; }

  PolicyTensor get_counts() const;
  void backprop(ValueArray& value, Node* parent=nullptr, edge_data_t* edge_data=nullptr);
  void backprop_with_virtual_undo(ValueArray& value, Node* parent=nullptr, edge_data_t* edge_data=nullptr);
  void virtual_backprop(Node* parent=nullptr, edge_data_t* edge_data=nullptr);

  ValueArray make_virtual_loss() const;

  asptr lookup_child_by_action(core::action_index_t action) const;

  const stable_data_t& stable_data() const { return stable_data_; }
  const children_data_t& children_data() const { return children_data_; }
  children_data_t& children_data() { return children_data_; }
  std::mutex& children_mutex() { return children_mutex_; }
  const stats_t& stats() const { return stats_; }
  const evaluation_data_t& evaluation_data() const { return evaluation_data_; }
  evaluation_data_t& evaluation_data() { return evaluation_data_; }

private:
  std::condition_variable cv_evaluate_;
  mutable std::mutex evaluation_data_mutex_;
  mutable std::mutex children_mutex_;
  mutable std::mutex stats_mutex_;
  const stable_data_t stable_data_;
  children_data_t children_data_;
  evaluation_data_t evaluation_data_;
  stats_t stats_;
};

}  // namespace mcts

#include <mcts/inl/Node.inl>
