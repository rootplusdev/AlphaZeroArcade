#pragma once

#include <core/AbstractPlayer.hpp>
#include <core/BasicTypes.hpp>
#include <core/GameStateConcept.hpp>
#include <util/EigenUtil.hpp>
#include <util/Random.hpp>

namespace common {

/*
 * RandomPlayer always chooses uniformly at random among the set of legal moves.
 */
template<core::GameStateConcept GameState>
class RandomPlayer : public core::AbstractPlayer<GameState> {
public:
  using base_t = core::AbstractPlayer<GameState>;
  using GameStateTypes = core::GameStateTypes<GameState>;
  using Action = typename GameStateTypes::Action;
  using ActionMask = typename GameStateTypes::ActionMask;
  using GameOutcome = typename GameStateTypes::GameOutcome;

  Action get_action(const GameState&, const ActionMask& mask) override {
    return eigen_util::sample(mask);
  }
};

}  // namespace common
