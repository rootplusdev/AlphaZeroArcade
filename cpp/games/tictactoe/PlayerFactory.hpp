#pragma once

#include <core/PlayerFactory.hpp>
#include <core/players/RemotePlayerProxyGenerator.hpp>
#include <games/generic/players/MctsPlayerGenerator.hpp>
#include <games/generic/players/RandomPlayerGenerator.hpp>
#include <games/tictactoe/GameState.hpp>
#include <games/tictactoe/Tensorizor.hpp>
#include <games/tictactoe/players/HumanTuiPlayerGenerator.hpp>
#include <games/tictactoe/players/PerfectPlayerGenerator.hpp>

namespace tictactoe {

class PlayerFactory : public core::PlayerFactory<GameState> {
 public:
  using base_t = core::PlayerFactory<GameState>;
  using player_subfactory_vec_t = base_t::player_subfactory_vec_t;

  PlayerFactory() : base_t(make_generators()) {}

 private:
  static player_subfactory_vec_t make_generators() {
    return {new core::PlayerSubfactory<tictactoe::HumanTuiPlayerGenerator>(),
            new core::PlayerSubfactory<tictactoe::PerfectPlayerGenerator>(),
            new core::PlayerSubfactory<
                generic::CompetitiveMctsPlayerGenerator<GameState, Tensorizor>>(),
            new core::PlayerSubfactory<
                generic::TrainingMctsPlayerGenerator<GameState, Tensorizor>>(),
            new core::PlayerSubfactory<generic::RandomPlayerGenerator<GameState>>(),
            new core::PlayerSubfactory<core::RemotePlayerProxyGenerator<GameState>>()};
  }
};

}  // namespace tictactoe
