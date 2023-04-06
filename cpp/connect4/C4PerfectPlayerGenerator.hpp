#pragma once

#include <string>
#include <vector>

#include <common/AbstractPlayer.hpp>
#include <common/AbstractPlayerGenerator.hpp>
#include <connect4/C4PerfectPlayer.hpp>

namespace c4 {

class PerfectPlayerGenerator : public common::AbstractPlayerGenerator<c4::GameState> {
public:
  std::vector<std::string> get_types() const override { return {"Perfect"}; }
  std::string get_description() const override { return "Perfect player"; }
  common::AbstractPlayer<c4::GameState>* generate(void* play_address) override { return new PerfectPlayer(params_); }
  void print_help(std::ostream& s) override { params_.make_options_description().print(s); }
  void parse_args(const std::vector<std::string>& args);

private:
  PerfectPlayer::Params params_;
};

}  // namespace c4

#include <connect4/inl/C4PerfectPlayerGenerator.inl>
