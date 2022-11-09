#pragma once

#include <array>

#include <common/Types.hpp>

namespace common {

template<GameStateConcept GameState>
class GameRunner {
public:
  using GameResult = GameResult<GameState::get_num_players()>;
  using Player = AbstractPlayer<GameState>;
  using player_array_t = std::array<Player*, GameState::get_num_players()>;

  template<typename T> GameRunner(T&& players) : players_(players) {}

  GameResult run();

private:
  player_array_t players_;
};

}  // namespace common

#include <common/GameRunnerINLINES.cpp>
