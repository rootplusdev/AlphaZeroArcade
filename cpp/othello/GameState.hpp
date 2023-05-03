#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <tuple>

#include <boost/functional/hash.hpp>
#include <torch/torch.h>

#include <common/AbstractPlayer.hpp>
#include <common/BasicTypes.hpp>
#include <common/DerivedTypes.hpp>
#include <common/GameStateConcept.hpp>
#include <common/MctsResults.hpp>
#include <common/SerializerTypes.hpp>
#include <common/serializers/DeterministicGameSerializer.hpp>
#include <othello/Constants.hpp>
#include <util/CppUtil.hpp>
#include <util/EigenUtil.hpp>

namespace othello { class GameState; }

template <>
struct std::hash<othello::GameState> {
  std::size_t operator()(const othello::GameState& state) const;
};

namespace othello {

/*
 * See <othello/Constants.hpp> for bitboard representation details.
 *
 * The algorithms for manipulating the board are lifted from:
 *
 * https://github.com/abulmo/edax-reversi
 */
class GameState {
public:
  static constexpr int kNumPlayers = othello::kNumPlayers;
  static constexpr int kNumGlobalActions = othello::kNumGlobalActions;
  static constexpr int kMaxNumLocalActions = othello::kNumGlobalActions;
  static constexpr int kTypicalNumMovesPerGame = othello::kTypicalNumMovesPerGame;

  using GameStateTypes = common::GameStateTypes<GameState>;
  using ActionMask = GameStateTypes::ActionMask;
  using player_name_array_t = GameStateTypes::player_name_array_t;
  using ValueProbDistr = GameStateTypes::ValueProbDistr;
  using MctsResults = common::MctsResults<GameState>;
  using LocalPolicyProbDistr = GameStateTypes::LocalPolicyProbDistr;
  using GameOutcome = GameStateTypes::GameOutcome;

  common::seat_index_t get_current_player() const { return cur_player_; }
  GameOutcome apply_move(common::action_index_t action);
  ActionMask get_valid_actions() const;

  template<eigen_util::FixedTensorConcept InputSlab> void tensorize(InputSlab&) const;
  void dump(common::action_index_t last_action=-1, const player_name_array_t* player_names=nullptr) const;
  bool operator==(const GameState& other) const = default;
  std::size_t hash() const;

  static void dump_mcts_output(const ValueProbDistr& mcts_value, const LocalPolicyProbDistr& mcts_policy,
                               const MctsResults& results);

private:
  auto to_tuple() const { return std::make_tuple(opponent_mask_, cur_player_mask_, cur_player_, pass_count_); }
  GameOutcome compute_outcome() const;  // assumes game has ended
  void row_dump(row_t row, column_t blink_column) const;
  static mask_t get_moves(mask_t P, mask_t O);
  static mask_t get_some_moves(mask_t P, mask_t mask, int dir);

  mask_t opponent_mask_ = kStartingWhiteMask;  // spaces occupied by either player
  mask_t cur_player_mask_ = kStartingBlackMask;  // spaces occupied by current player
  common::seat_index_t cur_player_ = kStartingColor;
  int8_t pass_count_ = 0;
};

static_assert(common::GameStateConcept<othello::GameState>);

extern uint64_t (*flip[kNumGlobalActions])(const uint64_t, const uint64_t);

using Player = common::AbstractPlayer<GameState>;

}  // namespace c4

namespace common {

// template specialization
template<> struct serializer<othello::GameState> {
  using type = DeterministicGameSerializer<othello::GameState>;
};

}  // namespace common

#include <othello/inl/GameState.inl>
