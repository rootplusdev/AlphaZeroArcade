#pragma once

#include <array>
#include <string>

#include <common/GameResult.hpp>
#include <common/Types.hpp>
#include <util/BitSet.hpp>

namespace c4 {

using column_t = int8_t;
using row_t = int8_t;
using mask_t = uint64_t;
const int kNumColumns = 7;
const int kNumRows = 6;
const int kNumCells = kNumColumns * kNumRows;
const int kMaxMovesPerGame = kNumColumns * kNumRows;
const int kNumPlayers = 2;

const common::player_index_t kRed = 0;
const common::player_index_t kYellow = 1;

using ActionMask = util::BitSet<kNumColumns>;
using GameResult = common::GameResult<kNumPlayers>;
using player_name_array_t = std::array<std::string, kNumPlayers>;

}  //namespace c4