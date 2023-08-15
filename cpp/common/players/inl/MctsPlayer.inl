#include <common/players/MctsPlayer.hpp>

#include <unistd.h>

#include <mcts/SearchResultsDumper.hpp>
#include <util/BitSet.hpp>
#include <util/BoostUtil.hpp>
#include <util/CppUtil.hpp>
#include <util/Exception.hpp>
#include <util/Math.hpp>
#include <util/ParamDumper.hpp>
#include <util/Random.hpp>
#include <util/RepoUtil.hpp>
#include <util/ScreenUtil.hpp>
#include <util/StringUtil.hpp>
#include <util/TorchUtil.hpp>

namespace common {

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
MctsPlayer<GameState_, Tensorizor_>::Params::Params(mcts::Mode mode)
{
  if (mode == mcts::kCompetitive) {
    num_fast_iters = 1600;
    num_full_iters = 0;
    full_pct = 0.0;
    move_temperature_str = "0.5->0.2:2*sqrt(b)";
  } else if (mode == mcts::kTraining) {
    num_fast_iters = 100;
    num_full_iters = 600;
    full_pct = 0.25;
    move_temperature_str = "0.8->0.2:2*sqrt(b)";
  } else {
    throw util::Exception("Unknown mcts::Mode: %d", (int)mode);
  }
}


template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
void MctsPlayer<GameState_, Tensorizor_>::Params::dump() const {
  if (full_pct == 0) {
    util::ParamDumper::add("MctsPlayer num iters", "%d", num_fast_iters);
  } else {
    util::ParamDumper::add("MctsPlayer num fast iters", "%d", num_fast_iters);
    util::ParamDumper::add("MctsPlayer num full iters", "%d", num_full_iters);
    util::ParamDumper::add("MctsPlayer num fast iters", "%.8g", full_pct);
    util::ParamDumper::add("MctsPlayer move temperature", "%s", move_temperature_str.c_str());
  }
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
auto MctsPlayer<GameState_, Tensorizor_>::Params::make_options_description()
{
  namespace po = boost::program_options;
  namespace po2 = boost_util::program_options;

  po2::options_description desc("MctsPlayer options");

  return desc
      .template add_option<"num-fast-iters", 'i'>(po::value<int>(&num_fast_iters)->default_value(num_fast_iters),
          "num mcts iterations to do per fast move")
      .template add_option<"num-full-iters", 'I'>(po::value<int>(&num_full_iters)->default_value(num_full_iters),
          "num mcts iterations to do per full move")
      .template add_option<"full-pct", 'f'>(po2::float_value("%.2f", &full_pct, full_pct),
          "pct of moves that should be full")
      .template add_option<"move-temp", 't'>(po::value<std::string>(&move_temperature_str)->default_value(move_temperature_str),
          "temperature for move selection")
      .template add_option<"verbose", 'v'>(po::bool_switch(&verbose)->default_value(verbose),
          "mcts player verbose mode")
      ;
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline MctsPlayer<GameState_, Tensorizor_>::MctsPlayer(const Params& params, MctsManager* mcts_manager)
: params_(params)
, mcts_manager_(mcts_manager)
, search_params_{
        {params.num_fast_iters, true},  // kFast
        {params.num_full_iters},  // kFull
        {1, true}  // kRawPolicy
  }
, move_temperature_(math::ExponentialDecay::parse(params.move_temperature_str, GameStateTypes::get_var_bindings()))
, owns_manager_(mcts_manager==nullptr)
{
  if (params.verbose) {
    verbose_info_ = new VerboseInfo();
  }
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
template<typename... Ts>
MctsPlayer<GameState_, Tensorizor_>::MctsPlayer(const Params& params, Ts&&... mcts_params_args)
: MctsPlayer(params, new MctsManager(std::forward<Ts>(mcts_params_args)...))
{
  owns_manager_ = true;
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline MctsPlayer<GameState_, Tensorizor_>::~MctsPlayer() {
  if (verbose_info_) {
    delete verbose_info_;
  }
  if (owns_manager_) delete mcts_manager_;
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline void MctsPlayer<GameState_, Tensorizor_>::start_game()
{
  move_count_ = 0;
  move_temperature_.reset();
  tensorizor_.clear();
  if (owns_manager_) {
    mcts_manager_->start();
  }
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline void MctsPlayer<GameState_, Tensorizor_>::receive_state_change(
    core::seat_index_t seat, const GameState& state, core::action_t action)
{
  move_count_++;
  move_temperature_.step();
  tensorizor_.receive_state_change(state, action);
  if (owns_manager_) {
    mcts_manager_->receive_state_change(seat, state, action);
  }
  if (base_t::get_my_seat() == seat && params_.verbose) {
    if (facing_human_tui_player_) {
      util::ScreenClearer::clear_once();
    }
    verbose_dump();
    if (!facing_human_tui_player_) {
      state.dump(action, &this->get_player_names());
    }
  }
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline core::action_t MctsPlayer<GameState_, Tensorizor_>::get_action(
    const GameState& state, const ActionMask& valid_actions)
{
  SearchMode search_mode = choose_search_mode();
  const MctsSearchResults* mcts_results = mcts_search(state, search_mode);
  return get_action_helper(search_mode, mcts_results, valid_actions);
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline void MctsPlayer<GameState_, Tensorizor_>::get_cache_stats(
    int& hits, int& misses, int& size, float& hash_balance_factor) const
{
  mcts_manager_->get_cache_stats(hits, misses, size, hash_balance_factor);
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline const typename MctsPlayer<GameState_, Tensorizor_>::MctsSearchResults*
MctsPlayer<GameState_, Tensorizor_>::mcts_search(const GameState& state, SearchMode search_mode) const {
  return mcts_manager_->search(tensorizor_, state, search_params_[search_mode]);
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline typename MctsPlayer<GameState_, Tensorizor_>::SearchMode
MctsPlayer<GameState_, Tensorizor_>::choose_search_mode() const {
  bool use_raw_policy = move_count_ < params_.num_raw_policy_starting_moves;
  return use_raw_policy ? kRawPolicy : get_random_search_mode();
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline core::action_t MctsPlayer<GameState_, Tensorizor_>::get_action_helper(
    SearchMode search_mode, const MctsSearchResults* mcts_results, const ActionMask& valid_actions) const
{
  PolicyTensor policy_tensor;
  if (search_mode == kRawPolicy) {
    GameStateTypes::local_to_global(mcts_results->policy_prior, valid_actions, policy_tensor);
  } else {
    policy_tensor = mcts_results->counts;
  }

  PolicyArray& policy = eigen_util::reinterpret_as_array(policy_tensor);
  if (search_mode != kRawPolicy) {
    float temp = move_temperature_.value();
    if (temp != 0) {
      policy = policy.pow(1.0 / temp);
    } else {
      policy = (policy == policy.maxCoeff()).template cast<torch_util::dtype>();
    }
  }

  if (policy.sum() == 0) {
    // This happens if MCTS proves that the position is losing. In this case we just choose a random valid action.
    for (core::action_t action : bitset_util::on_indices(valid_actions)) {
      policy[action] = 1;
    }
  }

  if (verbose_info_) {
    policy /= policy.sum();
    GameStateTypes::global_to_local(policy_tensor, valid_actions, verbose_info_->action_policy);
    verbose_info_->mcts_results = *mcts_results;
    verbose_info_->initialized = true;
  }
  core::action_t action = util::Random::weighted_sample(policy.begin(), policy.end());
  assert(valid_actions[action]);
  return action;
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
MctsPlayer<GameState_, Tensorizor_>::SearchMode
MctsPlayer<GameState_, Tensorizor_>::get_random_search_mode() const {
  float r = util::Random::uniform_real<float>(0.0f, 1.0f);
  return r < params_.full_pct ? kFull : kFast;
}

template<core::GameStateConcept GameState_, core::TensorizorConcept<GameState_> Tensorizor_>
inline void MctsPlayer<GameState_, Tensorizor_>::verbose_dump() const {
  if (!verbose_info_->initialized) return;

  const auto& action_policy = verbose_info_->action_policy;
  const auto& mcts_results = verbose_info_->mcts_results;

  printf("CPU pos eval:\n");
  mcts::SearchResultsDumper<GameState>::dump(action_policy, mcts_results);
  std::cout << std::endl;
}

}  // namespace common
