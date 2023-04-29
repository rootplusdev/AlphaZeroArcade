#pragma once

#include <array>
#include <cstdint>

#include <torch/torch.h>

#include <common/AbstractSymmetryTransform.hpp>
#include <common/DerivedTypes.hpp>
#include <common/IdentityTransform.hpp>
#include <common/TensorizorConcept.hpp>
#include <connect4/Constants.hpp>
#include <connect4/GameState.hpp>
#include <util/CppUtil.hpp>

namespace c4 {

class Tensorizor {
public:
  static constexpr int kMaxNumSymmetries = 2;
  using Shape = util::int_sequence<kNumPlayers, kNumColumns, kNumRows>;
  using TensorizorTypes = common::TensorizorTypes<Tensorizor>;
  using SymmetryIndexSet = TensorizorTypes::SymmetryIndexSet;
  using InputEigenSlab = TensorizorTypes::InputSlab::EigenType;
  using SymmetryTransform = common::AbstractSymmetryTransform<GameState, Tensorizor>;
  using IdentityTransform = common::IdentityTransform<GameState, Tensorizor>;
  using transform_array_t = std::array<SymmetryTransform*, 2>;

  class ReflectionTransform : public SymmetryTransform {
  public:
    void transform_input(InputEigenSlab& input) override;
    void transform_policy(PolicyEigenSlab& policy) override;
  };

  void clear() {}
  void receive_state_change(const GameState& state, common::action_index_t action_index) {}
  void tensorize(InputEigenSlab& tensor, const GameState& state) const { state.tensorize(tensor); }

  SymmetryIndexSet get_symmetry_indices(const GameState&) const;
  SymmetryTransform* get_symmetry(common::symmetry_index_t index) const;

private:
  static transform_array_t transforms();

  static IdentityTransform identity_transform_;
  static ReflectionTransform reflection_transform_;
};

}  // namespace c4

static_assert(common::TensorizorConcept<c4::Tensorizor, c4::GameState>);

#include <connect4/inl/Tensorizor.inl>