#pragma once

#include <common/AbstractSymmetryTransform.hpp>

namespace common {

template<typename GameState, typename Tensorizor>
class IdentityTransform : public AbstractSymmetryTransform<GameState, Tensorizor> {
public:
  using base_t = AbstractSymmetryTransform<GameState, Tensorizor>;
  using InputTensor = typename base_t::InputTensor;
  using PolicyVector = typename base_t::PolicyVector;

  void transform_input(InputTensor& input) override {}
  void transform_policy(PolicyVector& policy) override {}
};

}
