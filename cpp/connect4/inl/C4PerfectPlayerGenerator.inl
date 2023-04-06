#include <connect4/C4PerfectPlayerGenerator.hpp>

#include <boost/program_options.hpp>

namespace c4 {

inline void PerfectPlayerGenerator::parse_args(const std::vector<std::string>& args) {
  namespace po = boost::program_options;
  po::variables_map vm;
  po::store(po::command_line_parser(args).options(params_.make_options_description()).run(), vm);
  po::notify(vm);
}

}  // namespace c4
