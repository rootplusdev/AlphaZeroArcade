#pragma once

#include <common/BasicTypes.hpp>
#include <common/DerivedTypes.hpp>
#include <common/GameStateConcept.hpp>
#include <common/MctsPlayer.hpp>
#include <common/TensorizorConcept.hpp>
#include <common/TrainingDataWriter.hpp>

namespace common {

template<GameStateConcept GameState_, TensorizorConcept<GameState_> Tensorizor_>
class DataExportingMctsPlayer : public MctsPlayer<GameState_, Tensorizor_> {
public:
  using GameState = GameState_;
  using Tensorizor = Tensorizor_;
  using GameStateTypes = common::GameStateTypes<GameState>;
  using ActionMask = typename GameStateTypes::ActionMask;
  using GameOutcome = typename GameStateTypes::GameOutcome;
  using GlobalPolicyCountDistr = typename GameStateTypes::GlobalPolicyCountDistr;
  using TrainingDataWriter = common::TrainingDataWriter<GameState, Tensorizor>;

  using base_t = MctsPlayer<GameState, Tensorizor>;
  using Params = base_t::Params;
  using Mcts = base_t::Mcts;
  using player_array_t = base_t::player_array_t;

  DataExportingMctsPlayer(TrainingDataWriter* writer, const Params& params, Mcts* mcts=nullptr);

  void start_game(const player_array_t& players, player_index_t seat_assignment) override;
  void receive_state_change(
      player_index_t p, const GameState& state, action_index_t action,
      const GameOutcome& outcome) override;

  action_index_t get_action(const GameState&, const ActionMask&) override;

protected:
  TrainingDataWriter* writer_;
  TrainingDataWriter::GameData* game_data_ = nullptr;
  player_index_t seat_assignment_;
};

}  // namespace common

#include <common/inl/DataExportingMctsPlayer.inl>
