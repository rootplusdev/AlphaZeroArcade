#include <util/CppUtil.hpp>

#include <chrono>

namespace util {

namespace detail {

/*
 * Adapted from: https://www.linkedin.com/pulse/generic-tuple-hashing-modern-c-alex-dathskovsky/
 */
inline auto lazyHasher=[](size_t& cur, auto&&...value){
  auto lazyCombiner = [&cur](auto&& val) {
    cur ^= std::hash<std::decay_t<decltype(val)>>{}(val) * 0x9e3779b9 + (cur << 6) + (cur >> 2);
  };
  (lazyCombiner(std::forward<decltype(value)>(value)), ...);
  return cur;
};

struct TupleHasher{
  template<typename...Ts>
  size_t operator()(const std::tuple<Ts...>& tp){
    size_t hs = 0;
    apply([&hs](auto&&...value){ hs = lazyHasher(hs, value...); }, tp);
    return hs;
  }
};

}  // namespace detail

template<typename TimePoint> int64_t ns_since_epoch(const TimePoint& t) {
  return std::chrono::time_point_cast<std::chrono::nanoseconds>(t).time_since_epoch().count();
}

template<typename A>
 constexpr auto to_std_array() {
   return std::array<A, 0>();
 }

 template<typename A, typename T, size_t N>
 constexpr auto to_std_array(const std::array<T, N>& a) {
   std::array<A, N> r;
   for (size_t i=0; i<N; ++i) {
     r[i] = a[i];
   }
   return r;
 }

 template<typename A, typename T>
 constexpr auto to_std_array(T t) {
   std::array<A, 1> r = {A(t)};
   return r;
 }

 template<typename A, typename T, typename... Ts>
 constexpr auto to_std_array(const T& t, const Ts&... ts) {
   auto a = to_std_array<A>(t);
   auto b = to_std_array<A>(ts...);
   std::array<A, array_size(a) + array_size(b)> r;
   std::copy(std::begin(a), std::end(a), std::begin(r));
   std::copy(std::begin(b), std::end(b), std::begin(r)+array_size(a));
   return r;
 }

template<typename T, typename U, size_t N> std::array<T, N> array_cast(const std::array<U, N>& arr) {
  std::array<T, N> out;
  for (size_t i = 0; i < N; ++i) {
    out[i] = arr[i];
  }
  return out;
}

template<typename... T> size_t tuple_hash(const std::tuple<T...>& tup) {
  return detail::TupleHasher{}(tup);
}

}  // namespace util
