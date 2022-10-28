#pragma once

#include <cstdint>

#include <torch/torch.h>

#include <connect4/C4GameLogic.hpp>
#include <connect4/Constants.hpp>

namespace c4 {

template<int8_t tNumPreviousStates>
class HistoryBuffer {
public:
  static const int kHistoryBufferLength = 1 + tNumPreviousStates + kNumCells / kNumPlayers;
  HistoryBuffer();

private:
  torch::Tensor full_mask_;
  common::player_index_t next_player_ = kRed;
  int8_t ref_indices_[kNumPlayers] = {tNumPreviousStates, tNumPreviousStates};
};

class ReflectionTransform {
public:
  void transform_input(torch::Tensor& input) const;
  void transform_policy(torch::Tensor& policy) const;
};

class Tensorizor {
public:
  static constexpr std::initializer_list<size_t> kShape = {2, kNumColumns, kNumRows};

  void clear();

  void receive_state_change(const GameState& state, common::action_index_t action_index);

  auto tensorize(const GameState& state);

  auto get_symmetries(const GameState& state);


private:

};

}  // namespace c4

#include <connect4/C4TensorizorINLINES.cpp>
