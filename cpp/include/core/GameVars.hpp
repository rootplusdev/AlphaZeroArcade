#pragma once

#include <core/concepts/Game.hpp>
#include <util/Math.hpp>

namespace core {

template <concepts::Game Game>
struct GameVars {
  static math::var_bindings_map_t get_bindings() {
    math::var_bindings_map_t bindings;
    bindings["b"] = Game::Constants::kMaxBranchingFactor;
    return bindings;
  }
};

}  // namespace core
