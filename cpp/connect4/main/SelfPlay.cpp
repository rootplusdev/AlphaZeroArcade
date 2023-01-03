#include <chrono>
#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include <common/GameRunner.hpp>
#include <common/NNetPlayer.hpp>
#include <connect4/C4GameState.hpp>
#include <connect4/C4PerfectPlayer.hpp>
#include <connect4/C4Tensorizor.hpp>
#include <util/Config.hpp>
#include <util/StringUtil.hpp>

struct Args {
  std::string c4_solver_dir_str;
  int num_mcts_iters;
  int num_games;
  int parallelism_factor;
  bool verbose;
  bool perfect;
};

using Player = common::AbstractPlayer<c4::GameState>;
using C4NNetPlayer = common::NNetPlayer<c4::GameState, c4::Tensorizor>;
using Mcts = common::Mcts_<c4::GameState, c4::Tensorizor>;
using player_array_t = std::array<Player*, c4::kNumPlayers>;
using time_point_t = std::chrono::time_point<std::chrono::steady_clock>;
using duration_t = std::chrono::nanoseconds;

C4NNetPlayer* create_nnet_player(const Args& args) {
  C4NNetPlayer::Params params;
  params.num_mcts_iters = args.num_mcts_iters;
  params.temperature = 0;
  params.verbose = args.verbose;
  auto player = new C4NNetPlayer(params);
  player->set_name(util::create_string("MCTS-m%d", args.num_mcts_iters));
  return player;
}

c4::PerfectPlayer* create_perfect_player(const Args& args) {
  if (args.c4_solver_dir_str.empty()) {
    throw util::Exception("Must either pass -c or add an entry for \"c4.solver_dir\" to %s",
                          util::Config::instance()->config_path().c_str());
  }

  boost::filesystem::path c4_solver_dir(args.c4_solver_dir_str);
  c4::PerfectPlayer::Params params;
  params.c4_solver_dir = c4_solver_dir;
  return new c4::PerfectPlayer(params);
}

class SelfPlay {
public:
  SelfPlay(C4NNetPlayer* p1, Player* p2)
  : p1_(p1)
  , p2_(p2)
  {
    players_[c4::kRed] = p1;
    players_[c4::kYellow] = p2;
  }

  void run(int num_games, int parallelism_factor) {
    for (int i = 0; i < num_games; ++i) {
      common::GameRunner<c4::GameState> runner(players_);
      time_point_t t1 = std::chrono::steady_clock::now();
      auto outcome = runner.run();
      time_point_t t2 = std::chrono::steady_clock::now();
      if (outcome[c4::kRed] == 1) {
        win_++;
      } else if (outcome[c4::kYellow] == 1) {
        loss_++;
      } else {
        draw_++;
      }

      duration_t duration = t2 - t1;
      int64_t ns = duration.count();
      total_ns_ += ns;
      min_ns_ = std::min(min_ns_, ns);
      max_ns_ = std::max(max_ns_, ns);

      int cache_hits;
      int cache_misses;
      int cache_size;
      float hash_balance_factor;
      p1_->get_cache_stats(cache_hits, cache_misses, cache_size, hash_balance_factor);
      int cur_cache_hits = cache_hits - last_cache_hits_;
      int cur_cache_misses = cache_misses - last_cache_misses_;
      float cache_hit_rate = cur_cache_hits * 1.0 / std::max(1, cur_cache_hits + cur_cache_misses);
      last_cache_hits_ = cache_hits;
      last_cache_misses_ = cache_misses;
      int wasted_evals = cache_misses - cache_size;  // assumes cache large enough that no evictions
      double ms = ns * 1e-6;

      printf("W%d L%d D%d | cache:[%.2f%% %d %d %.3f] | %.3fms", win_, loss_, draw_, 100 * cache_hit_rate,
             wasted_evals, cache_size, hash_balance_factor, ms);
      std::cout << std::endl;
    }

    printf("Avg runtime: %.3fs\n", 1e-9 * total_ns_ / num_games);
    printf("Max runtime: %.3fs\n", 1e-9 * max_ns_);
    printf("Min runtime: %.3fs\n", 1e-9 * min_ns_);
  }

private:
  C4NNetPlayer* p1_;
  Player* p2_;
  player_array_t players_;

  int win_ = 0;
  int loss_ = 0;
  int draw_ = 0;

  int64_t total_ns_ = 0;
  int64_t min_ns_ = std::numeric_limits<int64_t>::max();
  int64_t max_ns_ = 0;

  int last_cache_hits_ = 0;
  int last_cache_misses_ = 0;
};

int main(int ac, char* av[]) {
  Args args;

  std::string default_c4_solver_dir_str = util::Config::instance()->get("c4.solver_dir", "");

  namespace po = boost::program_options;
  po::options_description desc("Pit Mcts as red against perfect as yellow");
  desc.add_options()("help,h", "help");

  Mcts::global_params_.dirichlet_mult = 0;
  Mcts::add_options(desc);

  desc.add_options()
      ("c4-solver-dir,d", po::value<std::string>(&args.c4_solver_dir_str)->default_value(default_c4_solver_dir_str), "base dir containing c4solver bin and 7x6 book")
      ("num-mcts-iters,m", po::value<int>(&args.num_mcts_iters)->default_value(100), "num mcts iterations to do per move")
      ("num-games,g", po::value<int>(&args.num_games)->default_value(100), "num games to simulate")
      ("parallelism-factor,P", po::value<int>(&args.parallelism_factor)->default_value(1), "num games to play in parallel")
      ("verbose,v", po::bool_switch(&args.verbose)->default_value(false), "verbose")
      ("perfect,p", po::bool_switch(&args.perfect)->default_value(false), "play against perfect")
      ;

  po::variables_map vm;
  po::store(po::command_line_parser(ac, av).options(desc).run(), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 0;
  }

  C4NNetPlayer* p1 = create_nnet_player(args);
  Player* p2 = args.perfect ? (Player*) create_perfect_player(args) : (Player*) create_nnet_player(args);

  SelfPlay self_play(p1, p2);
  self_play.run(args.num_games, args.parallelism_factor);

  delete p1;
  delete p2;
  return 0;
}
