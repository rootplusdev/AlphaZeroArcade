#pragma once

#include <common/GameStateConcept.hpp>
#include <common/serializers/GeneralSerializer.hpp>

namespace common {

/*
 * The DeterministicGameSerializer is identical to the GeneralSerializer, except that it assumes that the underlying
 * game is deterministic. This allows us to optimize the state-change serialization/deserialization methods - rather
 * than sending the entire GameState, we just send the action, and reconstruct the GameState on the other end.
 */
template <GameStateConcept GameState>
class DeterministicGameSerializer : public GeneralSerializer<GameState> {
public:
  using GameStateTypes = common::GameStateTypes<GameState>;
  using ActionMask = typename GameStateTypes::ActionMask;
  using GameOutcome = typename GameStateTypes::GameOutcome;

  size_t serialize_state_change(char* buf, size_t buf_size, const GameState& state, seat_index_t seat, action_index_t action) const override;
  void deserialize_state_change(const char* buf, GameState* state, seat_index_t* seat, action_index_t* action) const override;
};

}  // namespace common

#include <common/serializers/inl/DeterministicGameSerializer.inl>