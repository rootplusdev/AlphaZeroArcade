#pragma once

#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include <core/AbstractPlayer.hpp>
#include <core/BasicTypes.hpp>
#include <generic_players/HumanTuiPlayerGenerator.hpp>
#include <games/othello/Game.hpp>
#include <games/othello/players/HumanTuiPlayer.hpp>
#include <util/BoostUtil.hpp>

namespace othello {

class HumanTuiPlayerGenerator : public generic::HumanTuiPlayerGenerator<othello::GameState> {
 public:
  core::AbstractPlayer<othello::GameState>* generate(core::game_thread_id_t) override {
    return new HumanTuiPlayer();
  }
};

}  // namespace othello
