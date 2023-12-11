#include <util/BoostUtil.hpp>
#include <util/Config.hpp>
#include <util/KeyValueDumper.hpp>
#include <util/Random.hpp>
#include <util/RepoUtil.hpp>
#include <util/ScreenUtil.hpp>
#include <util/ThreadSafePrinter.hpp>
#include <util/SocketUtil.hpp>

namespace boost_util {
namespace program_options {

bool Settings::help_full = false;

}  // namespace program_options
}  // namespace boost_util

namespace util {

Config* Config::instance_ = nullptr;
KeyValueDumper* KeyValueDumper::instance_ = nullptr;
Random* Random::instance_ = nullptr;
Repo* Repo::instance_ = nullptr;
ScreenClearer* ScreenClearer::instance_ = nullptr;
std::mutex ThreadSafePrinter::mutex_;

}  // namespace util

namespace io {

Socket::map_t Socket::map_;

}  // namespace io