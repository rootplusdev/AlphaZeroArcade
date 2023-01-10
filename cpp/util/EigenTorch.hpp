#pragma once

#include <Eigen/Core>
#include <torch/torch.h>
#include <unsupported/Eigen/CXX11/Tensor>

#include <util/CppUtil.hpp>
#include <util/EigenUtil.hpp>

/*
 * Eigen and torch are two different third-party libraries that supply API's to interpret a float[] as a
 * mathematical tensor of floats. The two libraries have their pros and cons:
 *
 * Reasons to prefer Eigen:
 * - torch does not allow for specifying the datatype at compile time. This adds some overhead to all operations, as
 *   they must all branch at runtime based on datatype.
 * - torch does not allow for compile-time specifying of sizes. This adds overhead to operations. More significantly,
 *   it is very hard to control memory allocations, as simple operations like (tensor1 + tensor2) will incur a
 *   dynamic allocation as it constructs the output tensor.
 * - Eigen, by contrast, allows for specifying the datatype and sizes at compile-time. This results in bare-metal
 *   operations on the float[], which is inherently more efficient, avoids unnecessary dynamic memory allocations,
 *   and more easily facilitates additional compiler optimizations like SSE instructions.
 *
 * Reasons to prefer torch:
 * - torch::Module (the type for neural networks) uses torch::Tensor for inputs/outputs.
 *
 * Is it possible to get the best of both worlds? To have one underlying float[], with c++ union-ish mechanics to
 * interpret that float[] either as an Eigen object or as a torch::Tensor, depending on the contextual needs?
 *
 * The answer is YES, and this module provides classes to do exactly that.
 *
 * eigentorch::TensorFixedSize can be thought of as a c++ union of torch::Tensor and Eigen::TensorFixedSize.
 *
 * eigentorch::Tensor can be thought of as a c++ union of torch::Tensor and Eigen::Tensor.
 *
 * eigentorch::Array can be thought of as a c++ union of torch::Tensor and Eigen::Array.
 */
namespace eigentorch {

/*
 * Can be thought of as a c++ union of torch::Tensor and Eigen::TensorFixedSize.
 *
 * The torch and eigen tensors will share the same datatype, specified by Scalar_, and the same shape, specified by
 * Sizes_. The torch shape can be overridden by passing it in as a constructor argument.
 *
 * The recommendation is to do all operations and manipulations via asEigen(), and to only use asTorch() when
 * absolutely needed (i.e., when using as input for neural network evaluation input/output).
 *
 * WARNING: when mixing reading of t.asTorch() with writing of t.asEigen() or similar, I'm not sure if the compiler
 * will know not to reorder those operations.
 */
template <typename Scalar_, typename Sizes_, int Options_=Eigen::RowMajor>
class TensorFixedSize {
public:
  using Scalar = Scalar_;
  using Sizes = Sizes_;
  static constexpr int Options = Options_;
  using EigenType = Eigen::TensorFixedSize<Scalar, Sizes, Options>;
  template<typename Sizes> using EigenSlabType = Eigen::TensorFixedSize<Scalar, Sizes, Options>;
  using TorchType = torch::Tensor;

  TensorFixedSize();
  template<typename IntT, size_t N> TensorFixedSize(const std::array<IntT, N>& torch_shape);

  template<typename Sizes> const EigenSlabType<Sizes>& eigenSlab(int row) const;
  template<typename Sizes> EigenSlabType<Sizes>& eigenSlab(int row);

  EigenType& asEigen() { return eigen_tensor_; }
  const EigenType& asEigen() const { return eigen_tensor_; }

  TorchType& asTorch() { return torch_tensor_; }
  const TorchType& asTorch() const { return torch_tensor_; }

private:
  EigenType eigen_tensor_;
  TorchType torch_tensor_;
};

/*
 * Can be thought of as a c++ union of torch::Tensor and Eigen::Tensor.
 *
 * The torch and eigen tensors will share the same datatype, specified by Scalar_, and the same shape, passed in
 * by constructor argument. The torch shape can be overridden by passing it in as an additional constructor argument.
 *
 * The recommendation is to do all operations and manipulations via asEigen(), and to only use asTorch() when
 * absolutely needed (i.e., when using as input for neural network evaluation input/output).
 *
 * WARNING: when mixing reading of t.asTorch() with writing of t.asEigen() or similar, I'm not sure if the compiler
 * will know not to reorder those operations.
 */
template <typename Scalar_, int Rank_, int Options_=Eigen::RowMajor>
class Tensor {
public:
  using Scalar = Scalar_;
  static constexpr int Rank = Rank_;
  static constexpr int Options = Options_;
  using EigenType = Eigen::Tensor<Scalar, Rank, Options>;
  template<typename Sizes> using EigenSlabType = Eigen::TensorFixedSize<Scalar, Sizes, Options>;
  using TorchType = torch::Tensor;

  template<typename IntT, size_t N> Tensor(const std::array<IntT, N>& eigen_shape);

  template<typename IntT1, size_t N1, typename IntT2, size_t N2>
  Tensor(const std::array<IntT1, N1>& eigen_shape, const std::array<IntT2, N2>& torch_shape);

  template<typename Sizes> const EigenSlabType<Sizes>& eigenSlab(int row) const;
  template<typename Sizes> EigenSlabType<Sizes>& eigenSlab(int row);

  EigenType& asEigen() { return eigen_tensor_; }
  const EigenType& asEigen() const { return eigen_tensor_; }

  TorchType& asTorch() { return torch_tensor_; }
  const TorchType& asTorch() const { return torch_tensor_; }

private:
  EigenType eigen_tensor_;
  TorchType torch_tensor_;
};

/*
 * Can be thought of as a c++ union of torch::Tensor and Eigen::Array.
 *
 * The torch tensor and eigen array will share the same datatype, specified by Scalar_. By default, they will also
 * share the same 2-dimensional shape, specified by Rows_ and Cols_ (or by constructor args if either are Dynamic).
 * The torch shape can be overridden by passing it in as a constructor argument.
 *
 * The recommendation is to do all operations and manipulations via asEigen(), and to only use asTorch() when
 * absolutely needed (i.e., when using as input for neural network evaluation input/output).
 *
 * WARNING: when mixing reading of t.asTorch() with writing of t.asEigen() or similar, I'm not sure if the compiler
 * will know not to reorder those operations.
 */
template <typename Scalar_, int Rows_, int Cols_, int Options_=Eigen::RowMajor>
class Array {
public:
  using Scalar = Scalar_;
  static constexpr int Rows = Rows_;
  static constexpr int Cols = Cols_;
  static constexpr int Options = Options_;
  using EigenType = Eigen::Array<Scalar, Rows, Cols, Options>;
  using EigenSlabType = Eigen::Array<Scalar, 1, Cols, Options>;
  using TorchType = torch::Tensor;

  /*
   * The first two constructors have the torch Tensor exactly match the eigen Tensor in shape. The default constructor
   * is only appropriate in the case where Rows_ and Cols_ are fixed, while the second constructor is only
   * appropriate when at least one of the two is Dynamic.
   *
   * The third and fourth constructor are similar, but also allow you to explicitly specify the torch shape.
   */
  Array();
  Array(int rows, int cols);
  template<typename IntT, size_t N> Array(const std::array<IntT, N>& torch_shape);
  template<typename IntT, size_t N> Array(int eigen_rows, int eigen_cols, const std::array<IntT, N>& torch_shape);

  const EigenSlabType& eigenSlab(int row) const;
  EigenSlabType& eigenSlab(int row);

  EigenType& asEigen() { return eigen_matrix_; }
  const EigenType& asEigen() const { return eigen_matrix_; }

  TorchType& asTorch() { return torch_tensor_; }
  const TorchType& asTorch() const { return torch_tensor_; }

private:
  EigenType eigen_matrix_;
  TorchType torch_tensor_;
};

}  // namespace eigentorch

#include <util/inl/EigenTorch.inl>
