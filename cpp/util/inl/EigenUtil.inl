#include <util/EigenUtil.hpp>

#include <array>
#include <cstdint>

namespace eigen_util {

template<typename Scalar>
template<typename Array, typename Urng, typename... DimTs>
Array UniformDirichletGen<Scalar>::generate(Urng&& urng, Scalar alpha, DimTs&&... dims) {
  static_assert(Array::MaxColsAtCompileTime > 0);
  static_assert(Array::MaxRowsAtCompileTime > 0);

  if (alpha != alpha_) {
    alpha_ = alpha;
    new(&gamma_) GammaGen(alpha);
  }

  Array out(dims...);
  for (int i = 0; i < out.size(); ++i) {
    out.data()[i] = gamma_.template generate<Eigen::Array<Scalar, 1, 1>>(1, 1, urng)(0);
  }
  out /= out.sum();
  return out;
}

template<FixedTensorConcept DstTensorT, typename SrcTensorT, bool Aligned>
void packed_fixed_tensor_cp(DstTensorT& dst, const SrcTensorT& src) {
  if (Aligned) {
    dst = src;
  } else {
    constexpr bool SrcDstTypeMatch = std::is_same_v<SrcTensorT, DstTensorT>;
    if (SrcDstTypeMatch) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
      memcpy(&dst, &src, packed_fixed_tensor_size_v<DstTensorT>);
#pragma GCC diagnostic pop
    } else {
      DstTensorT tmp = src;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
      memcpy(&dst, &tmp, packed_fixed_tensor_size_v<DstTensorT>);
#pragma GCC diagnostic pop
    }
  }
}

template<FixedTensorConcept TensorT> const auto& slice(const TensorT& tensor, int row) {
  using Scalar = typename TensorT::Scalar;
  using Shape = subshape_t<extract_shape_t<TensorT>>;
  constexpr int Options = TensorT::Options;
  using SliceType = Eigen::TensorFixedSize<Scalar, Shape, Options>;

  const Scalar* data = tensor.data();
  data += Shape::total_size * row;
  return *reinterpret_cast<const SliceType*>(data);
}

template<FixedTensorConcept TensorT> auto& slice(TensorT& tensor, int row) {
  using Scalar = typename TensorT::Scalar;
  using Shape = subshape_t<extract_shape_t<TensorT>>;
  constexpr int Options = TensorT::Options;
  using SliceType = Eigen::TensorFixedSize<Scalar, Shape, Options>;

  Scalar* data = tensor.data();
  data += Shape::total_size * row;
  return *reinterpret_cast<SliceType*>(data);
}

template<ShapeConcept Shape, typename TensorT> const auto& slice(const TensorT& tensor, int row) {
  using Scalar = typename TensorT::Scalar;
  constexpr int Options = TensorT::Options;
  using SliceType = Eigen::TensorFixedSize<Scalar, Shape, Options>;

  const Scalar* data = tensor.data();
  data += Shape::total_size * row;
  return *reinterpret_cast<const SliceType*>(data);
}

template<ShapeConcept Shape, typename TensorT> auto& slice(TensorT& tensor, int row) {
  using Scalar = typename TensorT::Scalar;
  constexpr int Options = TensorT::Options;
  using SliceType = Eigen::TensorFixedSize<Scalar, Shape, Options>;

  Scalar* data = tensor.data();
  data += Shape::total_size * row;
  return *reinterpret_cast<SliceType*>(data);
}

template<typename Array> auto softmax(const Array& array) {
  auto normalized_array = array - array.maxCoeff();
  auto z = normalized_array.exp();
  return z / z.sum();
}

template<FixedTensorConcept Tensor> auto reverse(const Tensor& tensor, int dim) {
  using Sizes = extract_shape_t<Tensor>;
  constexpr int N = Sizes::count;
  static_assert(N > 0);

  Eigen::array<bool, N> rev;
  rev.fill(false);
  rev[dim] = true;
  return tensor.reverse(rev);
}

template<ShapeConcept Shape>
auto fixed_bool_tensor_to_std_bitset(const Eigen::TensorFixedSize<bool, Shape, Eigen::RowMajor>& tensor) {
  constexpr int N = Shape::count;
  std::bitset<N> bitset;
  for (int i = 0; i < N; ++i) {
    bitset[i] = tensor.data()[i];
  }
  return bitset;
}

template<ShapeConcept Shape, size_t N>
auto std_bitset_to_fixed_bool_tensor(const std::bitset<N>& bitset) {
  Eigen::TensorFixedSize<bool, Shape, Eigen::RowMajor> tensor;
  for (int i = 0; i < N; ++i) {
    tensor.data()[i] = bitset[i];
  }
  return tensor;
}

template<typename Scalar, ShapeConcept Shape, int Options>
const auto& reinterpret_as_array(const Eigen::TensorFixedSize<Scalar, Shape, Options>& tensor) {
  constexpr int N = Shape::total_size;
  using ArrayT = Eigen::Array<Scalar, N, 1>;
  return reinterpret_cast<const ArrayT&>(tensor);
}

template<typename Scalar, ShapeConcept Shape, int Options>
auto& reinterpret_as_array(Eigen::TensorFixedSize<Scalar, Shape, Options>& tensor) {
  constexpr int N = Shape::total_size;
  using ArrayT = Eigen::Array<Scalar, N, 1>;
  return reinterpret_cast<ArrayT&>(tensor);
}

template<FixedTensorConcept TensorT, typename Scalar, int N>
const TensorT& reinterpret_as_tensor(const Eigen::Array<Scalar, N, 1>& array) {
  static_assert(std::is_same_v<typename TensorT::Scalar, Scalar>);
  static_assert(N == extract_shape_t<TensorT>::total_size);
  static_assert(TensorT::Options | Eigen::RowMajor);
  return reinterpret_cast<const TensorT&>(array);
}

template<FixedTensorConcept TensorT, typename Scalar, int N>
TensorT& reinterpret_as_tensor(Eigen::Array<Scalar, N, 1>& array) {
  static_assert(std::is_same_v<typename TensorT::Scalar, Scalar>);
  static_assert(N == extract_shape_t<TensorT>::total_size);
  static_assert(TensorT::Options | Eigen::RowMajor);
  return reinterpret_cast<TensorT&>(array);
}

template<FixedMatrixConcept MatrixT, typename Scalar, ShapeConcept Shape, int Options>
const MatrixT& reinterpret_as_matrix(const Eigen::TensorFixedSize<Scalar, Shape, Options>& tensor) {
  static_assert(std::is_same_v<typename MatrixT::Scalar, Scalar>);
  static_assert(MatrixT::RowsAtCompileTime * MatrixT::ColsAtCompileTime == Shape::total_size);
  static_assert((Options | Eigen::RowMajorBit) == (MatrixT::Flags & Eigen::RowMajorBit));
  return reinterpret_cast<const MatrixT&>(tensor);
}

template<FixedMatrixConcept MatrixT, typename Scalar, ShapeConcept Shape, int Options>
MatrixT& reinterpret_as_matrix(Eigen::TensorFixedSize<Scalar, Shape, Options>& tensor) {
  static_assert(std::is_same_v<typename MatrixT::Scalar, Scalar>);
  static_assert(MatrixT::RowsAtCompileTime * MatrixT::ColsAtCompileTime == Shape::total_size);
  static_assert((Options | Eigen::RowMajorBit) == (MatrixT::Flags & Eigen::RowMajorBit));
  return reinterpret_cast<MatrixT&>(tensor);
}

template<typename TensorT> auto sum(const TensorT& tensor) {
  using Scalar = typename TensorT::Scalar;
  Eigen::TensorFixedSize<Scalar, Eigen::Sizes<>> out = tensor.sum();
  return out;
}

template<typename TensorT> auto max(const TensorT& tensor) {
  using Scalar = typename TensorT::Scalar;
  Eigen::TensorFixedSize<Scalar, Eigen::Sizes<>> out = tensor.maximum();
  return out;
}

template<typename TensorT> auto min(const TensorT& tensor) {
  using Scalar = typename TensorT::Scalar;
  Eigen::TensorFixedSize<Scalar, Eigen::Sizes<>> out = tensor.minimum();
  return out;
}

template<typename Scalar, int N> void left_rotate(Eigen::Array<Scalar, N, 1>& array, int n) {
  Scalar* data = array.data();
  std::rotate(data, data + n, data + N);
}

template<typename Scalar, int N> void right_rotate(Eigen::Array<Scalar, N, 1>& array, int n) {
  Scalar* data = array.data();
  std::rotate(data, data + N - n, data + N);
}

}  // namespace eigen_util
