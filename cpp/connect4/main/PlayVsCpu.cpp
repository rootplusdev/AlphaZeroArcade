#include <iostream>

#include <boost/program_options.hpp>

#include <common/BasicTypes.hpp>
#include <common/GameRunner.hpp>
#include <common/HumanTuiPlayer.hpp>
#include <common/MctsPlayer.hpp>
#include <connect4/C4Constants.hpp>
#include <connect4/C4GameState.hpp>
#include <connect4/C4PerfectPlayer.hpp>
#include <connect4/C4Tensorizor.hpp>
#include <util/Config.hpp>
#include <util/Exception.hpp>
#include <util/Random.hpp>

struct Args {
  std::string my_starting_color;
  bool perfect;
  bool verbose;
};

using GameState = c4::GameState;
using Tensorizor = c4::Tensorizor;
using Mcts = common::Mcts<GameState, Tensorizor>;
using MctsPlayer = common::MctsPlayer<GameState, Tensorizor>;

common::player_index_t parse_color(const std::string& str) {
  if (str == "R") return c4::kRed;
  if (str == "Y") return c4::kYellow;
  if (str.empty()) return util::Random::uniform_sample(0, c4::kNumPlayers);
  throw util::Exception("Invalid --my-starting-color/-s value: \"%s\"", str.c_str());
}

int main(int ac, char* av[]) {
  namespace po = boost::program_options;
  Args args;

  std::string default_c4_solver_dir_str = util::Config::instance()->get("c4.solver_dir", "");

  po::options_description desc("Play vs MCTS as a human");
  desc.add_options()("help,h", "help");

  Mcts::add_options(desc);
  MctsPlayer::competitive_params.add_options(desc, true);
  c4::PerfectPlayParams::PerfectPlayParams::add_options(desc, true);

  desc.add_options()
      ("my-starting-color,s", po::value<std::string>(&args.my_starting_color),
          "human's starting color (R or Y). Default: random")
      ("perfect,p", po::bool_switch(&args.perfect)->default_value(false), "play against perfect player")
      ;

  po::variables_map vm;
  po::store(po::command_line_parser(ac, av).options(desc).run(), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 0;
  }

  using C4HumanTuiPlayer = common::HumanTuiPlayer<c4::GameState>;
  c4::Player* human = new C4HumanTuiPlayer();

  c4::Player* cpu;
  if (args.perfect) {
    cpu = new c4::PerfectPlayer();
  } else {
    MctsPlayer::Params cpu_params = MctsPlayer::competitive_params;
    cpu = new MctsPlayer(cpu_params);
  }

  common::player_index_t my_color = parse_color(args.my_starting_color);
  common::player_index_t cpu_color = 1 - my_color;

  using player_t = common::AbstractPlayer<c4::GameState>;
  using player_array_t = std::array<player_t*, c4::kNumPlayers>;

  player_array_t players;
  players[my_color] = human;
  players[cpu_color] = cpu;

  using GameRunner = common::GameRunner<c4::GameState>;
  GameRunner runner(players);
  auto outcome = runner.run(GameRunner::kFixedPlayerSeats);

  if (outcome[my_color] == 1) {
    std::cout << "Congratulations, you win!" << std::endl;
  } else if (outcome[cpu_color] == 1) {
    std::cout << "Sorry! You lose!" << std::endl;
  } else {
    std::cout << "The game has ended in a draw!" << std::endl;
  }

  delete cpu;
  delete human;

  return 0;
}
