#pragma once

#include <tuple>
#include <utility>

#include <Eigen/Core>
#include <unsupported/Eigen/CXX11/Tensor>

#include <common/BasicTypes.hpp>
#include <util/BitSet.hpp>
#include <util/CppUtil.hpp>
#include <util/EigenTorch.hpp>
#include <util/EigenUtil.hpp>

namespace common {

/*
 * Represents the result of a game, as a length-t array of non-negative floats, where t is the number of players in
 * the game.
 *
 * If the result represents a terminal game state, the array will have sum 1. Normally, one slot in the array,
 * corresponding to the winner, will equal 1, and the other slots will equal 0. In the even of a draw, the tied
 * players will typically each equal the same fractional value.
 *
 * If the game is not yet over, the result will have all zeros.
 */
template<int NumPlayers> using GameResult_ = Eigen::Vector<float, NumPlayers>;
template<int NumPlayers> bool is_terminal_result(const GameResult_<NumPlayers>& result) { return result.sum() > 0; }
template<int NumPlayers> auto make_non_terminal_result() { GameResult_<NumPlayers> r; r.setZero(); return r; }

template<typename GameState>
struct GameStateTypes_ {
  static constexpr int kNumPlayers = GameState::kNumPlayers;
  static constexpr int kNumGlobalActions = GameState::kNumGlobalActions;
  static constexpr int kMaxNumLocalActions = GameState::kMaxNumLocalActions;

  using GameResult = GameResult_<kNumPlayers>;

  template <int NumRows> using PolicyMatrix = eigentorch::Matrix<float, NumRows, kNumGlobalActions, Eigen::RowMajor>;
  template <int NumRows> using ValueMatrix = eigentorch::Matrix<float, NumRows, kNumPlayers, Eigen::RowMajor>;

  using PolicySlab = PolicyMatrix<1>;
  using ValueSlab = ValueMatrix<1>;

  using PolicyVector = Eigen::Vector<float, kNumGlobalActions>;
  using ValueVector = Eigen::Vector<float, kNumPlayers>;

  using ValueProbDistr = Eigen::Vector<float, kNumPlayers>;
  using LocalPolicyCountDistr = Eigen::Matrix<int, Eigen::Dynamic, 1, 0, kMaxNumLocalActions>;
  using LocalPolicyProbDistr = Eigen::Matrix<float, Eigen::Dynamic, 1, 0, kMaxNumLocalActions>;

  using GlobalPolicyCountDistr = Eigen::Vector<int, kNumGlobalActions>;
  using GlobalPolicyProbDistr = Eigen::Vector<float, kNumGlobalActions>;

  using ActionMask = util::BitSet<kNumGlobalActions>;
  using player_name_array_t = std::array<std::string, kNumPlayers>;
};

template<typename Tensorizor>
struct TensorizorTypes_ {
  using BaseShape = typename Tensorizor::Shape;
  using Shape = eigen_util::to_sizes_t<util::concat_int_sequence_t<util::int_sequence<1>, BaseShape>>;
  using InputTensor = eigentorch::TensorFixedSize<float, Shape>;
  using DynamicInputTensor = eigentorch::Tensor<float, BaseShape::size() + 1>;
};

template<typename GameState>
struct StateEvaluationKey {
  GameState state;
  float inv_temp;
  symmetry_index_t sym_index;

  bool operator==(const StateEvaluationKey& other) const {
    return state == other.state && inv_temp == other.inv_temp && sym_index == other.sym_index;
  }
};

}  // namespace common

template <typename GameState>
struct std::hash<common::StateEvaluationKey<GameState>> {
  std::size_t operator()(const common::StateEvaluationKey<GameState> ssi) const {
    return util::tuple_hash(std::make_tuple(ssi.state, ssi.inv_temp, ssi.sym_index));
  }
};
