#pragma once

#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include <common/AbstractPlayer.hpp>
#include <common/BasicTypes.hpp>
#include <common/players/HumanTuiPlayerGenerator.hpp>
#include <connect4/GameState.hpp>
#include <connect4/players/HumanTuiPlayer.hpp>
#include <util/BoostUtil.hpp>

namespace c4 {

class HumanTuiPlayerGenerator : public common::HumanTuiPlayerGenerator<c4::GameState> {
public:
  struct Params {
    bool cheat_mode;

    auto make_options_description() {
      namespace po = boost::program_options;
      namespace po2 = boost_util::program_options;

      po2::options_description desc("c4::HumanTUIPlayer options");
      return desc
          .template add_option<"cheat-mode", 'C'>(po::bool_switch(&cheat_mode)->default_value(false),
                                                  "show winning moves")
          ;
    }
  };

  common::AbstractPlayer<c4::GameState>* generate(common::game_thread_id_t) override;
  void print_help(std::ostream& s) override { params_.make_options_description().print(s); }
  void parse_args(const std::vector<std::string>& args) override;

private:
  Params params_;
};

}  // namespace c4

#include <connect4/players/inl/HumanTuiPlayerGenerator.inl>
