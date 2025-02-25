#pragma once

#include <core/BasicTypes.hpp>
#include <util/CppUtil.hpp>
#include <util/FiniteGroups.hpp>

#include <concepts>

namespace core {
namespace concepts {

template <typename GS, typename T>
concept OperatesOn = requires(util::strict_type_match_t<T&> t, group::element_t sym) {
  { GS::apply(t, sym) };
};

/*
 * If FullState and BaseState are distinct types, one may optionally provide an addition static
 * function:
 *
 * void apply(FullState&, group::element_t);
 *
 * Specifying this allows for MCTS optimizations. Without specifying this, MCTS must resort to a
 * "double-pass" through the game-tree on each tree-traversal. We typically expect this overhead to
 * be small compared to the greater cost of neural network inference, but conceivably for some
 * games it could be wise to avoid this if possible.
 */
template <typename GS, typename GameTypes, typename BaseState>
concept GameSymmetries = requires(const BaseState& base_state) {
  { GS::get_mask(base_state) } -> std::same_as<typename GameTypes::SymmetryMask>;
  requires core::concepts::OperatesOn<GS, BaseState>;
  requires core::concepts::OperatesOn<GS, typename GameTypes::PolicyTensor>;
  requires core::concepts::OperatesOn<GS, core::action_t>;
  { GS::get_canonical_symmetry(base_state) } -> std::same_as<group::element_t>;
};

}  // namespace concepts
}  // namespace core
