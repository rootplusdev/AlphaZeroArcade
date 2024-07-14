#include <mcts/Node.hpp>

#include <util/CppUtil.hpp>
#include <util/LoggingUtil.hpp>

namespace mcts {

template <core::concepts::Game Game>
inline Node<Game>::stable_data_t::stable_data_t(const FullState& state,
                                                const ActionOutcome& outcome) {
  if (outcome.terminal) {
    V = outcome.terminal_value;
    num_valid_actions = 0;
    current_player = -1;
    terminal = true;
  } else {
    V.setZero();  // to be set lazily
    valid_action_mask = Game::Rules::get_legal_moves(state);
    num_valid_actions = valid_action_mask.count();
    current_player = Game::Rules::get_current_player(state);
    terminal = false;
  }
}

template <core::concepts::Game Game>
void Node<Game>::stats_t::init_q(const ValueArray& value) {
  RQ = value;
  VQ = value;
  for (int p = 0; p < kNumPlayers; ++p) {
    provably_winning[p] = value(p) == 1;
    provably_losing[p] = value(p) == 0;
  }
}

template <core::concepts::Game Game>
void Node<Game>::stats_t::init_q_and_real_increment(const ValueArray& value) {
  init_q(value);
  real_increment();
}

template <core::concepts::Game Game>
void Node<Game>::stats_t::init_q_and_increment_transfer(const ValueArray& value) {
  init_q(value);
  increment_transfer();
}

template <core::concepts::Game Game>
Node<Game>::LookupTable::Defragmenter::Defragmenter(LookupTable* table)
    : table_(table),
      node_bitset_(table->node_pool_.size()),
      edge_bitset_(table->edge_pool_.size()) {}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::Defragmenter::scan(node_pool_index_t n) {
  if (n < 0 || node_bitset_[n]) return;

  node_bitset_[n] = true;
  Node* node = &table_->node_pool_[n];
  if (!node->edges_initialized()) return;

  edge_pool_index_t first_edge_index = node->get_first_edge_index();
  int n_edges = node->stable_data().num_valid_actions;

  edge_bitset_.set(first_edge_index, n_edges, true);
  for (int e = 0; e < n_edges; ++e) {
    scan(node->get_edge(e)->child_index);
  }
}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::Defragmenter::prepare() {
  init_remapping(node_index_remappings_, node_bitset_);
  init_remapping(edge_index_remappings_, edge_bitset_);
}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::Defragmenter::remap(node_pool_index_t& n) {
  bitset_t processed_nodes(table_->node_pool_.size());
  remap_helper(n, processed_nodes);
  n = node_index_remappings_[n];
  util::debug_assert(processed_nodes == node_bitset_);
}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::Defragmenter::defrag() {
  table_->node_pool_.defragment(node_bitset_);
  table_->edge_pool_.defragment(edge_bitset_);

  for (auto it = table_->map_.begin(); it != table_->map_.end();) {
    if (!node_bitset_[it->second]) {
      it = table_->map_.erase(it);
    } else {
      it->second = node_index_remappings_[it->second];
      ++it;
    }
  }
}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::Defragmenter::remap_helper(node_pool_index_t n,
                                                         bitset_t& processed_nodes) {
  if (processed_nodes[n]) return;

  processed_nodes[n] = true;
  Node* node = &table_->node_pool_[n];
  if (!node->edges_initialized()) return;

  edge_pool_index_t first_edge_index = node->get_first_edge_index();
  int n_edges = node->stable_data().num_valid_actions;

  for (int e = 0; e < n_edges; ++e) {
    edge_t* edge = node->get_edge(e);
    if (edge->child_index < 0) continue;
    remap_helper(edge->child_index, processed_nodes);
    edge->child_index = node_index_remappings_[edge->child_index];
  }

  node->set_first_edge_index(edge_index_remappings_[first_edge_index]);
}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::Defragmenter::init_remapping(index_vec_t& remappings,
                                                           bitset_t& bitset) {
  remappings.resize(bitset.size());
  for (int i = 0; i < (int)bitset.size(); ++i) {
    remappings[i] = -1;
  }

  auto i = bitset.find_first();
  int k = 0;
  while (i != bitset_t::npos) {
    remappings[i] = k++;
    i = bitset.find_next(i);
  }
}

template <core::concepts::Game Game>
Node<Game>::LookupTable::LookupTable(bool multithreaded_mode)
    : mutex_pool_(multithreaded_mode ? kDefaultMutexPoolSize : 1),
      cv_pool_(multithreaded_mode ? kDefaultMutexPoolSize : 1) {}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::clear() {
  map_.clear();
  edge_pool_.clear();
  node_pool_.clear();
}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::defragment(node_pool_index_t& root_index) {
  Defragmenter defragmenter(this);
  defragmenter.scan(root_index);
  defragmenter.prepare();
  defragmenter.remap(root_index);
  defragmenter.defrag();
}

template <core::concepts::Game Game>
void Node<Game>::LookupTable::insert_node(const MCTSKey& key, node_pool_index_t value) {
  std::lock_guard lock(map_mutex_);
  map_[key] = value;
}

template <core::concepts::Game Game>
typename Node<Game>::node_pool_index_t Node<Game>::LookupTable::lookup_node(
    const MCTSKey& key) const {
  std::lock_guard lock(map_mutex_);
  auto it = map_.find(key);
  if (it == map_.end()) {
    return -1;
  }
  return it->second;
}

template <core::concepts::Game Game>
int Node<Game>::LookupTable::get_random_mutex_id() const {
  return util::Random::uniform_sample(0, (int)mutex_pool_.size());
}

template <core::concepts::Game Game>
Node<Game>::Node(LookupTable* table, const FullState& state, const ActionOutcome& outcome)
    : stable_data_(state, outcome), lookup_table_(table), mutex_id_(table->get_random_mutex_id()) {}

template <core::concepts::Game Game>
typename Node<Game>::PolicyTensor Node<Game>::get_counts(const ManagerParams& params) const {
  // This should only be called in contexts where the search-threads are inactive, so we do not need
  // to worry about thread-safety

  core::seat_index_t cp = stable_data().current_player;

  if (kEnableDebug) {
    std::cout << "get_counts()" << std::endl;
    std::cout << "  cp: " << int(cp) << std::endl;
  }

  PolicyTensor counts;
  counts.setZero();

  bool provably_winning = stats_.provably_winning[cp];
  bool provably_losing = stats_.provably_losing[cp];

  for (int i = 0; i < stable_data().num_valid_actions; i++) {
    const edge_t* edge = get_edge(i);
    core::action_t action = edge->action;
    const Node* child = get_child(edge);
    if (!child) continue;

    const auto& stats = child->stats();
    int count = stats.RN;

    int modified_count = count;
    const char* detail = "";
    if (params.avoid_proven_losers && !provably_losing && stats.provably_losing[cp]) {
      modified_count = 0;
      detail = " (losing)";
    } else if (params.exploit_proven_winners && provably_winning && !stats.provably_winning[cp]) {
      modified_count = 0;
      detail = " (?)";
    } else if (provably_winning) {
      detail = " (winning)";
    }

    if (kEnableDebug) {
      std::cout << "  " << Game::IO::action_to_str(action) << ": " << count;
      if (modified_count != count) {
        std::cout << " -> " << modified_count;
      }
      std::cout << detail << std::endl;
    }

    if (modified_count) {
      counts(action) = modified_count;
    }
  }

  return counts;
}

template <core::concepts::Game Game>
typename Node<Game>::ValueArray Node<Game>::make_virtual_loss() const {
  constexpr float x = 1.0 / (kNumPlayers - 1);
  ValueArray virtual_loss;
  virtual_loss.setZero();
  virtual_loss(stable_data().current_player) = x;
  return virtual_loss;
}

template <core::concepts::Game Game>
template <typename UpdateT>
void Node<Game>::update_stats(const UpdateT& update_instruction) {
  core::seat_index_t cp = stable_data().current_player;

  ValueArray RQ_sum;
  RQ_sum.setZero();
  int RN = 0;

  /*
   * provably winning/losing calculation
   *
   * TODO: generalize this by computing lower/upper utility in games with unbounded/non-zero-sum
   * utilities.
   */
  bool cp_has_winning_move = false;
  int num_children = 0;

  player_bitset_t all_provably_winning;
  player_bitset_t all_provably_losing;
  all_provably_winning.set();
  all_provably_losing.set();
  bool skipped = false;
  for (int i = 0; i < stable_data().num_valid_actions; i++) {
    const edge_t* edge = get_edge(i);
    const Node* child = get_child(edge);
    if (!child) {
      skipped = true;
      continue;
    }
    const auto& child_stats = child->stats();
    RN += edge->RN;
    RQ_sum += child_stats.RQ * edge->RN;

    cp_has_winning_move |= child_stats.provably_winning[cp];
    all_provably_winning &= child_stats.provably_winning;
    all_provably_losing &= child_stats.provably_losing;
    num_children++;
  }

  if (skipped) {
    all_provably_winning.set(false);
    all_provably_losing.set(false);
  }

  std::unique_lock lock(mutex());
  update_instruction(this);

  if (stats_.RN) {
    RQ_sum += stable_data_.V;
    RN++;
  }

  // incorporate bounds from children
  int num_valid_actions = stable_data_.num_valid_actions;
  if (num_valid_actions == 0) {
    // terminal state, provably_winning/losing are already set by instruction
  } else if (cp_has_winning_move) {
    stats_.provably_winning[cp] = true;
    stats_.provably_losing.set();
    stats_.provably_losing[cp] = false;
  } else if (num_children == num_valid_actions) {
    stats_.provably_winning = all_provably_winning;
    stats_.provably_losing = all_provably_losing;
  }

  stats_.RQ = RN ? (RQ_sum / RN) : RQ_sum;
  if (stats_.VN) {
    ValueArray VQ_sum = RQ_sum + make_virtual_loss() * stats_.VN;
    stats_.VQ = VQ_sum / (RN + stats_.VN);
  } else {
    stats_.VQ = stats_.RQ;
  }
}

// NOTE: this can be switched to use binary search if we'd like
template <core::concepts::Game Game>
typename Node<Game>::node_pool_index_t Node<Game>::lookup_child_by_action(
    core::action_t action) const {
  int i = 0;
  for (core::action_t a : bitset_util::on_indices(stable_data_.valid_action_mask)) {
    if (a == action) {
      return get_edge(i)->child_index;
    }
    ++i;
  }
  return -1;
}

template <core::concepts::Game Game>
void Node<Game>::initialize_edges(const FullState& state) {
  int n_edges = stable_data_.num_valid_actions;
  if (n_edges == 0) return;
  first_edge_index_ = lookup_table_->alloc_edges(n_edges);

  struct pair_t {
    auto operator<=>(const pair_t& other) const = default;
    BaseState child_state;
    int edge_index;
  };
  pair_t pairs[n_edges];

  int i = 0;
  for (core::action_t action : bitset_util::on_indices(stable_data_.valid_action_mask)) {
    edge_t* edge = get_edge(i);
    new (edge) edge_t();
    edge->action = action;

    FullState state_copy = state;
    Game::Rules::apply(state_copy, action);

    pair_t& pair = pairs[i];
    pair.child_state = state_copy;
    pair.edge_index = i;

    group::element_t canonical_sym = Game::Symmetries::get_canonical_symmetry(pair.child_state);
    Game::Symmetries::apply(pair.child_state, canonical_sym);

    i++;
  }

  std::sort(pairs, pairs + n_edges);

  int representative_edge_indices[n_edges];
  if (IS_MACRO_ENABLED(DEBUG_BUILD)) {
    std::fill(representative_edge_indices, representative_edge_indices + n_edges, -1);
  }

  int r = 0;
  for (i = 0; i < n_edges; ++i) {
    if (i == 0 || pairs[i].child_state != pairs[i - 1].child_state) {
      representative_edge_indices[r++] = pairs[i].edge_index;
    }
  }
  num_representative_actions_ = r;

  std::sort(representative_edge_indices, representative_edge_indices + r);

  int collapsed_index_lookup[n_edges];
  if (IS_MACRO_ENABLED(DEBUG_BUILD)) {
    std::fill(collapsed_index_lookup, collapsed_index_lookup + n_edges, -1);
  }

  for (i = 0; i < r; ++i) {
    util::debug_assert(representative_edge_indices[i] >= 0);
    collapsed_index_lookup[representative_edge_indices[i]] = i;
  }

  int cur_representative = 0;

  for (i = 0; i < n_edges; ++i) {
    int j = pairs[i].edge_index;
    if (i == 0 || pairs[i].child_state != pairs[i - 1].child_state) {
      cur_representative = j;
    }
    edge_t* edge = get_edge(j);
    edge->representative_edge_index = cur_representative;
    edge->collapsed_index = collapsed_index_lookup[cur_representative];
    util::debug_assert(edge->collapsed_index >= 0);
  }
}

template <core::concepts::Game Game>
template<typename PolicyTransformFunc>
void Node<Game>::load_eval(NNEvaluation* eval, PolicyTransformFunc f) {
  ValueArray V;
  LocalPolicyArray P_raw(stable_data_.num_valid_actions);

  if (eval == nullptr) {
    // treat this as uniform P and V
    V.setConstant(1.0 / kNumPlayers);
    P_raw.setConstant(1.0 / stable_data_.num_valid_actions);
  } else {
    V = eval->value_prob_distr();
    P_raw = eigen_util::softmax(eval->local_policy_logit_distr());
  }

  LocalPolicyArray P_adjusted(num_representative_actions_);
  P_adjusted.setZero();
  for (int i = 0; i < stable_data_.num_valid_actions; ++i) {
    edge_t* edge = get_edge(i);
    P_adjusted(edge->collapsed_index) += P_raw(i);
  }

  if (eval) f(P_adjusted);

  stable_data_.V = V;
  stats_.RQ = V;
  stats_.VQ = V;

  for (int i = 0; i < stable_data_.num_valid_actions; ++i) {
    edge_t* edge = get_edge(i);
    edge->raw_policy_prior = P_raw[i];
    if (edge->representative_edge_index == i) {
      edge->adjusted_policy_prior = P_adjusted[edge->collapsed_index];
    } else {
      edge->adjusted_policy_prior = 0;
    }
  }
}

template <core::concepts::Game Game>
typename Node<Game>::edge_t* Node<Game>::get_edge(int i) const {
  util::debug_assert(first_edge_index_ != -1);
  return lookup_table_->get_edge(first_edge_index_ + i);
}

template <core::concepts::Game Game>
Node<Game>* Node<Game>::get_child(const edge_t* edge) const {
  if (edge->child_index < 0) return nullptr;
  return lookup_table_->get_node(edge->child_index);
}

template <core::concepts::Game Game>
group::element_t Node<Game>::make_symmetry(const FullState& state, const ManagerParams& params) {
  group::element_t sym = 0;
  if (params.apply_random_symmetries) {
    sym = bitset_util::choose_random_on_index(Game::Symmetries::get_mask(state));
  }
  return sym;
}

}  // namespace mcts
