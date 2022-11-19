#pragma once

#include <Eigen/Core>

#include <common/GameResult.hpp>
#include <common/GameStateConcept.hpp>
#include <common/NeuralNet.hpp>
#include <common/TensorizorConcept.hpp>
#include <common/Types.hpp>
#include <util/BitSet.hpp>

namespace common {

template<GameStateConcept GameState, TensorizorConcept<GameState> Tensorizor>
class Mcts {
public:
  static constexpr int kNumPlayers = GameState::kNumPlayers;
  static constexpr int kNumGlobalActions = GameState::kNumGlobalActions;
  static constexpr int kMaxNumLocalActions = GameState::kMaxNumLocalActions;

  using GlobalPolicyCountDistr = Eigen::Vector<int, kNumGlobalActions>;
  using GlobalPolicyProbDistr = Eigen::Vector<float, kNumGlobalActions>;
  using ValueProbDistr = Eigen::Vector<float, kNumPlayers>;
  using Result = GameResult<kNumPlayers>;
  using ActionMask = util::BitSet<kNumGlobalActions>;
  using LocalPolicyLogitDistr = Eigen::Vector<float, kMaxNumLocalActions>;
  using LocalPolicyProbDistr = Eigen::Vector<float, kMaxNumLocalActions>;

  struct Params {
    int tree_size_limit;
    float root_softmax_temperature;
    float cPUCT = 1.1;
    float dirichlet_mult = 0.25;
    float dirichlet_alpha = 0.03;
    bool allow_eliminations = true;

    bool can_reuse_subtree() const { return dirichlet_mult == 0; }
  };

  struct Results {
    GlobalPolicyCountDistr counts;
    GlobalPolicyProbDistr policy_prior;
    ValueProbDistr win_rates;
    ValueProbDistr value_prior;
  };

private:

  class StateEvaluation {
  public:
    StateEvaluation(const NeuralNet& net, const Tensorizor& tensorizor, const GameState& state, const Result& result);
    bool is_terminal() const { return result_.is_terminal(); }

  private:
    player_index_t current_player_;
    Result result_;

    // Below members are only valid if !is_terminal()
    ActionMask valid_action_mask_;
    LocalPolicyLogitDistr local_policy_logit_distr_;
    ValueProbDistr value_prob_distr_;
  };

public:
  Mcts();
  void clear();
  void receive_state_change(player_index_t, const GameState&, action_index_t, const Result&);
};

}  // namespace common

#include <common/inl/Mcts.inl>