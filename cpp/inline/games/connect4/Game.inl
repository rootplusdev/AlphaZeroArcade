#include <games/connect4/Game.hpp>

#include <bit>
#include <iostream>

#include <boost/lexical_cast.hpp>

#include <util/AnsiCodes.hpp>
#include <util/BitSet.hpp>
#include <util/CppUtil.hpp>

namespace c4 {

inline core::seat_index_t Game::BaseState::get_current_player() const {
  return std::popcount(full_mask) % 2;
}

inline core::seat_index_t Game::BaseState::get_player_at(int row, int col) const {
  int cp = get_current_player();
  int index = _to_bit_index(row, col);
  bool occupied_by_cur_player = (mask_t(1) << index) & cur_player_mask;
  bool occupied_by_any_player = (mask_t(1) << index) & full_mask;
  return occupied_by_any_player ? (occupied_by_cur_player ? cp : (1 - cp)) : -1;
}

inline size_t Game::BaseState::hash() const {
  return boost::hash_range(&full_mask, &full_mask + 2);
}

inline void Game::Reflect::apply(BaseState& pos) {
  pos.full_mask = __builtin_bswap64(pos.full_mask) >> 8;
  pos.cur_player_mask = __builtin_bswap64(pos.cur_player_mask) >> 8;
}

inline void Game::Reflect::apply(PolicyTensor& t) {
  eigen_util::FTensor<PolicyShape> u = eigen_util::reverse(t, t.rank() - 1);
  t = u;
}

inline Game::ActionMask Game::Rules::legal_moves(const FullState& state) {
  const BaseState& base = state;
  mask_t bottomed_full_mask = base.full_mask + _full_bottom_mask();

  ActionMask mask;
  for (int col = 0; col < kNumColumns; ++col) {
    bool legal = bottomed_full_mask & _column_mask(col);
    mask[col] = legal;
  }
  return mask;
}

inline core::seat_index_t Game::Rules::current_player(const BaseState& base) {
  return base.get_current_player();
}

inline Game::SymmetryIndexSet Game::Rules::get_symmetry_indices(const FullState& state) {
  SymmetryIndexSet set;
  set.set();
  return set;
}

inline Game::InputTensor Game::InputTensorizor::tensorize(const BaseState* start,
                                                          const BaseState* cur) {
  core::seat_index_t cp = cur->get_current_player();
  InputTensor tensor;
  for (int row = 0; row < kNumRows; ++row) {
    for (int col = 0; col < kNumColumns; ++col) {
      core::seat_index_t p = cur->get_player_at(row, col);
      tensor(0, row, col) = (p == cp);
      tensor(1, row, col) = (p == 1 - cp);
    }
  }
  return tensor;
}

inline Game::TrainingTargetTensorizor::OwnershipTarget::Tensor
Game::TrainingTargetTensorizor::OwnershipTarget::tensorize(const GameLogView& view) {
  Tensor tensor;
  core::seat_index_t cp = view.cur_pos->get_current_player();
  for (int row = 0; row < kNumRows; ++row) {
    for (int col = 0; col < kNumColumns; ++col) {
      core::seat_index_t p = view.final_pos->get_player_at(row, col);
      int val = (p == -1) ? 0 : ((p == cp) ? 2 : 1);
      tensor(row, col) = val;
    }
  }
  return tensor;
}

inline constexpr int Game::_to_bit_index(row_t row, column_t col) { return 8 * col + row; }

inline constexpr mask_t Game::_column_mask(column_t col) { return 63UL << (8 * col); }

inline constexpr mask_t Game::_bottom_mask(column_t col) { return 1UL << (8 * col); }

inline constexpr mask_t Game::_full_bottom_mask() {
  mask_t mask = 0;
  for (int col = 0; col < kNumColumns; ++col) {
    mask |= _bottom_mask(col);
  }
  return mask;
}

}  // namespace c4
