#pragma once

#include <core/PlayerFactory.hpp>
#include <core/players/MctsPlayerGenerator.hpp>
#include <core/players/RandomPlayerGenerator.hpp>
#include <core/players/RemotePlayerProxyGenerator.hpp>
#include <connect4/GameState.hpp>
#include <connect4/Tensorizor.hpp>
#include <connect4/players/HumanTuiPlayerGenerator.hpp>
#include <connect4/players/MctsPlayerGenerator.hpp>
#include <connect4/players/PerfectPlayerGenerator.hpp>

namespace c4 {

class PlayerFactory : public common::PlayerFactory<GameState> {
public:
  using base_t = common::PlayerFactory<GameState>;
  using player_generator_creator_vec_t = base_t::player_generator_creator_vec_t;

  PlayerFactory() : base_t(make_generators()) {}

private:
  static player_generator_creator_vec_t make_generators() {
    return {
      new common::PlayerGeneratorCreator<c4::HumanTuiPlayerGenerator>(),
      new common::PlayerGeneratorCreator<c4::CompetitiveMctsPlayerGenerator>(),
      new common::PlayerGeneratorCreator<c4::TrainingMctsPlayerGenerator>(),
      new common::PlayerGeneratorCreator<c4::PerfectPlayerGenerator>(),
      new common::PlayerGeneratorCreator<common::RandomPlayerGenerator<GameState>>(),
      new common::PlayerGeneratorCreator<common::RemotePlayerProxyGenerator<GameState>>()
    };
  }
};

}  // namespace c4
