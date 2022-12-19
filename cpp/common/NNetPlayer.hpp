#pragma once

#include <ostream>

#include <boost/filesystem.hpp>
#include <unsupported/Eigen/CXX11/Tensor>

#include <common/AbstractPlayer.hpp>
#include <common/BasicTypes.hpp>
#include <common/DerivedTypes.hpp>
#include <common/GameStateConcept.hpp>
#include <common/Mcts.hpp>
#include <common/MctsResults.hpp>
#include <common/TensorizorConcept.hpp>
#include <util/CppUtil.hpp>
#include <util/EigenTorch.hpp>

namespace common {

template<GameStateConcept GameState_, TensorizorConcept<GameState_> Tensorizor_>
class NNetPlayer : public AbstractPlayer<GameState_> {
public:
  using base_t = AbstractPlayer<GameState_>;
  using GameState = GameState_;
  using Tensorizor = Tensorizor_;

  struct Params {
    Params();

    boost::filesystem::path nnet_filename;
    boost::filesystem::path debug_filename;
    bool verbose = false;
    bool allow_eliminations = true;
    int num_search_threads = 1;
    int num_mcts_iters = 100;
    float temperature = 0;
  };

  using GameStateTypes = GameStateTypes_<GameState>;

  using Mcts = Mcts_<GameState, Tensorizor>;
  using MctsParams = typename Mcts::Params;
  using MctsSimParams = typename Mcts::SimParams;
  using MctsResults = MctsResults_<GameState>;

  using ActionMask = typename GameStateTypes::ActionMask;
  using GameOutcome = typename GameStateTypes::GameOutcome;
  using player_array_t = typename base_t::player_array_t;
  using ValueProbDistr = typename Mcts::ValueProbDistr;
  using LocalPolicyProbDistr = typename Mcts::LocalPolicyProbDistr;
  using GlobalPolicyProbDistr = typename GameStateTypes::GlobalPolicyProbDistr;

  NNetPlayer(const Params&);
  ~NNetPlayer();

  void start_game(const player_array_t& players, player_index_t seat_assignment) override;
  void receive_state_change(player_index_t, const GameState&, action_index_t, const GameOutcome&) override;
  action_index_t get_action(const GameState&, const ActionMask&) override;

private:
  struct VerboseInfo {
    ValueProbDistr mcts_value;
    LocalPolicyProbDistr mcts_policy;
    MctsResults mcts_results;

    bool initialized = false;
  };

  static MctsParams get_mcts_params(const Params& params);
  void verbose_dump() const;

  const Params params_;
  Tensorizor tensorizor_;

  Mcts mcts_;
  MctsSimParams sim_params_;
  const float inv_temperature_;
  player_index_t my_index_ = -1;
  VerboseInfo* verbose_info_ = nullptr;
};

}  // namespace common

#include <common/inl/NNetPlayer.inl>