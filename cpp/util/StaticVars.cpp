#include <util/Config.hpp>
#include <util/PrintUtil.hpp>
#include <util/Random.hpp>
#include <util/RepoUtil.hpp>

namespace util {
namespace detail {

_xprintf_helper _xprintf_helper::instance_;

}  // namespace detail

Config* Config::instance_ = nullptr;
Random* Random::instance_ = nullptr;
Repo* Repo::instance_ = nullptr;

}  // namespace util
