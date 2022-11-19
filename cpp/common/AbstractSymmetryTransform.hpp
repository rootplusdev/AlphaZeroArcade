#pragma once

#include <torch/torch.h>

namespace common {

/*
 * SymmetryTransform's provide a mechanism to effectively apply a symmetry transform to a GameState. In the game of go,
 * there are 8 transforms: 4 rotations and 4 reflections. See AlphaGo papers for more details.
 *
 * NOTE: some derived classes will invoke certain torch functions that are likely to result in dynamic memory
 * allocation (such as Tensor.flip()). In principle, by supplying "scratch" memory space, we should be able to avoid
 * dynamic memory allocation. This represents an inefficiency that deserves to be fixed. Doing so may require modifying
 * the interface of this class. I am holding off on doing this until I better understand the torch c++ API.
 */
class AbstractSymmetryTransform {
public:
  virtual ~AbstractSymmetryTransform() {}

  /*
   * Transforms a tensor generated by a Tensorizor in place.
   */
  virtual void transform_input(torch::Tensor input) = 0;

  /*
   * Transforms a policy tensor in place.
   */
  virtual void transform_policy(torch::Tensor input) = 0;
};

}  // namespace common