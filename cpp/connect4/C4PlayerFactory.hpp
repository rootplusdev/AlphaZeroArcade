#pragma once

#include <common/PlayerFactory.hpp>
#include <common/MctsPlayerGenerator.hpp>
#include <common/RandomPlayerGenerator.hpp>
#include <connect4/C4GameState.hpp>
#include <connect4/C4HumanTuiPlayerGenerator.hpp>
#include <connect4/C4PerfectPlayerGenerator.hpp>
#include <connect4/C4Tensorizor.hpp>

namespace c4 {

class PlayerFactory : public common::PlayerFactory<GameState> {
public:
  using base_t = common::PlayerFactory<GameState>;
  using player_generator_vec_t = base_t::player_generator_vec_t;

  PlayerFactory() : base_t(make_generators()) {}

private:
  static player_generator_vec_t make_generators() {
    return {
      new c4::HumanTuiPlayerGenerator(),
      new common::CompetitiveMctsPlayerGenerator<GameState, Tensorizor>(),
      new common::TrainingMctsPlayerGenerator<GameState, Tensorizor>(),
      new c4::PerfectPlayerGenerator(),
      new common::RandomPlayerGenerator<GameState>()
    };
  }
};

}  // namespace c4
