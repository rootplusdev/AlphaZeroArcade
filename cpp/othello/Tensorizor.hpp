#pragma once

#include <array>
#include <cstdint>

#include <torch/torch.h>

#include <common/AbstractSymmetryTransform.hpp>
#include <common/DerivedTypes.hpp>
#include <common/IdentityTransform.hpp>
#include <common/TensorizorConcept.hpp>
#include <othello/Constants.hpp>
#include <othello/GameState.hpp>
#include <util/CppUtil.hpp>
#include <util/EigenUtil.hpp>
#include <util/TorchUtil.hpp>

namespace othello {

/*
 * Note: all the transforms leave the center 4 squares of the board untouched. This is done because of the
 * (questionable?) decision to encode the "pass" move as the center D4 square.
 */
class Tensorizor {
public:
  static constexpr int kMaxNumSymmetries = 8;
  using InputShape = eigen_util::Shape<kNumPlayers, kBoardDimension, kBoardDimension>;
  using InputTensor = Eigen::TensorFixedSize<bool, InputShape, Eigen::RowMajor>;

  using GameStateTypes = common::GameStateTypes<GameState>;
  using TensorizorTypes = common::TensorizorTypes<Tensorizor>;
  using SymmetryIndexSet = TensorizorTypes::SymmetryIndexSet;
  using PolicyTensor = GameStateTypes::PolicyTensor;
  using SymmetryTransform = common::AbstractSymmetryTransform<GameState, Tensorizor>;
  using IdentityTransform = common::IdentityTransform<GameState, Tensorizor>;
  using transform_array_t = std::array<SymmetryTransform*, kMaxNumSymmetries>;

  using InputScalar = typename InputTensor::Scalar;
  using PolicyScalar = typename PolicyTensor::Scalar;

  template<typename Scalar>
  using MatrixT = Eigen::Matrix<Scalar, kBoardDimension, kBoardDimension, Eigen::RowMajor>;

  struct CenterFourSquares {
    bool starting_white1;
    bool starting_white2;
    bool starting_black1;
    bool starting_black2;
  };

  static CenterFourSquares get_center_four_squares(const PolicyTensor& policy);
  static void set_center_four_squares(PolicyTensor& policy, const CenterFourSquares& center_four_squares);

  static MatrixT<InputScalar>& slice_as_matrix(InputTensor& input, int row);
  static MatrixT<PolicyScalar>& as_matrix(PolicyTensor& policy);

  struct Rotation90Transform : public SymmetryTransform {
    void transform_input(InputTensor& input) override;
    void transform_policy(PolicyTensor& policy) override;
  };

  struct Rotation180Transform : public SymmetryTransform {
    void transform_input(InputTensor& input) override;
    void transform_policy(PolicyTensor& policy) override;
  };

  struct Rotation270Transform : public SymmetryTransform {
    void transform_input(InputTensor& input) override;
    void transform_policy(PolicyTensor& policy) override;
  };

  struct ReflectionOverHorizontalTransform : public SymmetryTransform {
    void transform_input(InputTensor& input) override;
    void transform_policy(PolicyTensor& policy) override;
  };

  struct ReflectionOverHorizontalWithRotation90Transform : public SymmetryTransform {
    void transform_input(InputTensor& input) override;
    void transform_policy(PolicyTensor& policy) override;
  };

  struct ReflectionOverHorizontalWithRotation180Transform : public SymmetryTransform {
    void transform_input(InputTensor& input) override;
    void transform_policy(PolicyTensor& policy) override;
  };

  struct ReflectionOverHorizontalWithRotation270Transform : public SymmetryTransform {
    void transform_input(InputTensor& input) override;
    void transform_policy(PolicyTensor& policy) override;
  };

  void clear() {}
  void receive_state_change(const GameState& state, common::action_index_t action_index) {}
  void tensorize(InputTensor& tensor, const GameState& state) const { state.tensorize(tensor); }

  SymmetryIndexSet get_symmetry_indices(const GameState&) const;
  SymmetryTransform* get_symmetry(common::symmetry_index_t index) const;

private:
  static transform_array_t transforms();

  struct transforms_struct_t {
    IdentityTransform identity_transform_;
    Rotation90Transform rotation90_transform_;
    Rotation180Transform rotation180_transform_;
    Rotation270Transform rotation270_transform_;
    ReflectionOverHorizontalTransform reflection_over_horizontal_transform_;
    ReflectionOverHorizontalWithRotation90Transform reflection_over_horizontal_with_rotation90_transform_;
    ReflectionOverHorizontalWithRotation180Transform reflection_over_horizontal_with_rotation180_transform_;
    ReflectionOverHorizontalWithRotation270Transform reflection_over_horizontal_with_rotation270_transform_;
  };

  static transforms_struct_t transforms_struct_;
};

}  // namespace othello

static_assert(common::TensorizorConcept<othello::Tensorizor, othello::GameState>);

#include <othello/inl/Tensorizor.inl>
