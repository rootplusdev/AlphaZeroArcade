#pragma once

#include <common/DataExportingMctsPlayer.hpp>

#include <util/BitSet.hpp>

namespace common {

template<GameStateConcept GameState_, TensorizorConcept<GameState_> Tensorizor_>
template<typename... BaseArgs>
DataExportingMctsPlayer<GameState_, Tensorizor_>::DataExportingMctsPlayer(
    TrainingDataWriter* writer, BaseArgs&&... base_args)
: base_t(std::forward<BaseArgs>(base_args)...)
, writer_(writer) {}

template<GameStateConcept GameState_, TensorizorConcept<GameState_> Tensorizor_>
void DataExportingMctsPlayer<GameState_, Tensorizor_>::start_game(
    game_id_t game_id, const player_array_t& players, player_index_t seat_assignment)
{
  base_t::start_game(game_id, players, seat_assignment);
  game_data_ = writer_->get_data(game_id);
  seat_assignment_ = seat_assignment;
}

template<GameStateConcept GameState_, TensorizorConcept<GameState_> Tensorizor_>
void DataExportingMctsPlayer<GameState_, Tensorizor_>::receive_state_change(
    player_index_t p, const GameState& state, action_index_t action,
    const GameOutcome& outcome)
{
  base_t::receive_state_change(p, state, action, outcome);
  if (is_terminal_outcome(outcome)) {
    game_data_->record_for_all(outcome.reshaped(1, GameState::kNumPlayers));
    writer_->close(game_data_);
    game_data_ = nullptr;
  }
}

template<GameStateConcept GameState_, TensorizorConcept<GameState_> Tensorizor_>
action_index_t DataExportingMctsPlayer<GameState_, Tensorizor_>::get_action(
    const GameState& state, const ActionMask& mask)
{
  action_index_t action = base_t::get_action(state, mask);
  if (this->sim_type_ == base_t::kFull) {
    record_position(state);
  }
  return action;
}

template<GameStateConcept GameState_, TensorizorConcept<GameState_> Tensorizor_>
void DataExportingMctsPlayer<GameState_, Tensorizor_>::record_position(const GameState& state) {
  auto sym_indices = this->tensorizor_.get_symmetry_indices(state);
  for (symmetry_index_t sym_index : bitset_util::on_indices(sym_indices)) {
    auto slab = game_data_->get_next_slab();
    auto& input = slab.input;
    auto& policy = slab.policy;

    this->tensorizor_.tensorize(input, state);

    const GlobalPolicyCountDistr& counts = this->mcts_results_->counts;
    const auto& fcounts = counts.reshaped(1, GameState::kNumGlobalActions).template cast<float>();
    policy = fcounts / std::max(1.0f, (float)fcounts.sum());

    auto transform = this->tensorizor_.get_symmetry(sym_index);
    transform->transform_input(input);
    transform->transform_policy(policy);
  }
}

}  // namespace common
