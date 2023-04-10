#include <array>
#include <iostream>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include <common/GameServer.hpp>
#include <connect4/C4GameState.hpp>
#include <connect4/C4PlayerFactory.hpp>
#include <connect4/C4Tensorizor.hpp>
#include <util/Exception.hpp>
#include <util/ParamDumper.hpp>

using GameState = c4::GameState;
using PlayerFactory = c4::PlayerFactory;
using Tensorizor = c4::Tensorizor;

using GameServer = common::GameServer<GameState>;
using Mcts = common::Mcts<GameState, Tensorizor>;
using Player = common::AbstractPlayer<GameState>;

namespace po = boost::program_options;
namespace po2 = boost_util::program_options;

struct Args {
  std::vector<std::string> player_strs;

  auto make_options_description() {
    po2::options_description desc("Connect4 options");

    return desc
        .template add_option<"player">(po::value<std::vector<std::string>>(&player_strs),
                                       "Space-delimited list of player options, wrapped in quotes, to be specified multiple times")
        ;
  }
};

GameServer::Params get_default_game_server_params() {
  GameServer::Params parallel_game_runner_params;
  parallel_game_runner_params.display_progress_bar = true;
  return parallel_game_runner_params;
}

int main(int ac, char* av[]) {
  try {
    namespace po = boost::program_options;
    namespace po2 = boost_util::program_options;

    Args args;
    GameServer::Params game_server_params = get_default_game_server_params();

    po2::options_description raw_desc("General options");
    auto desc = raw_desc.template add_option<"help", 'h'>("help")
        .add(args.make_options_description())
        .add(game_server_params.make_options_description());

    po::variables_map vm;
    po::store(po::command_line_parser(ac, av).options(desc).run(), vm);
    po::notify(vm);

    c4::PlayerFactory player_factory;
    if (vm.count("help")) {
      std::cout << desc << std::endl;
      player_factory.print_help(args.player_strs);
      return 0;
    }

    GameServer server(game_server_params);

    for (const auto &pgs: player_factory.parse(args.player_strs)) {
      server.register_player(pgs.seat, pgs.generator);
    }
    if (!server.ready_to_start()) {
      server.wait_for_remote_player_registrations();
    }
    server.run();

//  mcts_player_params.dump();
//  util::ParamDumper::add("MCTS search threads", "%d", mcts_params.num_search_threads);
//  util::ParamDumper::add("MCTS max batch size", "%d", mcts_params.batch_size_limit);
    util::ParamDumper::add("MCTS avg batch size", "%.2f", Mcts::global_avg_batch_size());
    util::ParamDumper::flush();
  } catch (const util::CleanException& e) {
    std::cerr << "Caught a CleanException. Details:" << std::endl;
    std::cerr << e.what() << std::endl;
    return 1;
  }

  return 0;
}
