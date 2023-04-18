#pragma once

#include <array>
#include <chrono>
#include <map>
#include <vector>

#include <common/AbstractPlayer.hpp>
#include <common/AbstractPlayerGenerator.hpp>
#include <common/BasicTypes.hpp>
#include <common/DerivedTypes.hpp>
#include <common/GameStateConcept.hpp>
#include <common/RemotePlayerProxyGenerator.hpp>
#include <third_party/ProgressBar.hpp>

namespace common {

template<GameStateConcept GameState>
class GameServer {
public:
  static constexpr int kNumPlayers = GameState::kNumPlayers;

  using GameStateTypes = common::GameStateTypes<GameState>;
  using GameOutcome = typename GameStateTypes::GameOutcome;
  using ActionMask = typename GameStateTypes::ActionMask;
  using Player = AbstractPlayer<GameState>;
  using PlayerGenerator = AbstractPlayerGenerator<GameState>;
  using RemotePlayerProxyGenerator = common::RemotePlayerProxyGenerator<GameState>;
  using player_array_t = std::array<Player*, kNumPlayers>;
  using player_name_array_t = typename GameStateTypes::player_name_array_t;
  using results_map_t = std::map<float, int>;
  using results_array_t = std::array<results_map_t, kNumPlayers>;
  using time_point_t = std::chrono::time_point<std::chrono::steady_clock>;
  using duration_t = std::chrono::nanoseconds;

  /*
   * A player_instantiation_t is instantiated from a registration_t. See registration_t for more detail.
   */
  struct player_instantiation_t {
    Player* player = nullptr;
    seat_index_t seat = -1;  // -1 means random seat
    player_id_t player_id = -1;  // order in which player was registered
  };
  using player_instantiation_array_t = std::array<player_instantiation_t, kNumPlayers>;

  /*
   * A registration_t gives birth to a player_instantiation_t.
   *
   * The difference is that a registration_t has a player-*generating-function*, rather than a player.
   * This is needed because when multiple GameThread's are launched, each needs to instantiate its own Player.
   * This requires the GameServer API to demand passing in a player-*generator*, as opposed to a player, so that
   * each spawned GameThread can create its own player.
   */
  struct registration_t {
    PlayerGenerator* gen = nullptr;
    seat_index_t seat = -1;  // -1 means random seat
    player_id_t player_id = -1;  // order in which player was generated

    player_instantiation_t instantiate(game_thread_id_t id) const { return {gen->generate_with_name(id), seat, player_id}; }
  };
  using registration_vec_t = std::vector<registration_t>;

  struct Params {
    auto make_options_description();

    int num_games = 1000;  // if <=0, run indefinitely
    int parallelism = 100;  // number of games to run simultaneously
    int port = 0;
    bool display_progress_bar = false;
  };

protected:
  static std::string get_results_str(const results_map_t& map);

private:
  /*
   * Members of GameServer that need to be accessed by the individual GameThread's.
   */
  class SharedData {
  public:
    SharedData(const Params& params) : params_(params) {}
    ~SharedData();

    const Params& params() const { return params_; }
    void init_progress_bar();
    bool request_game(int num_games);  // returns false iff hit num_games limit
    void update(const GameOutcome& outcome, int64_t ns);
    auto get_results() const;
    void end_session();
    bool ready_to_start() const;
    int compute_parallelism_factor() const;
    int num_games_started() const { return num_games_started_; }
    void register_player(seat_index_t seat, PlayerGenerator* gen, bool implicit_remote=false);
    int num_registrations() const { return registrations_.size(); }
    player_instantiation_array_t generate_player_order(const player_instantiation_array_t& instantiations) const;
    registration_vec_t& registration_templates() { return registrations_; }

  private:
    const Params params_;

    registration_vec_t registrations_;

    mutable std::mutex mutex_;
    progressbar* bar_ = nullptr;
    int num_games_started_ = 0;

    results_array_t results_array_;
    int64_t total_ns_ = 0;
    int64_t min_ns_ = std::numeric_limits<int64_t>::max();
    int64_t max_ns_ = 0;
  };

  class GameThread {
  public:
    GameThread(SharedData& shared_data, game_thread_id_t);
    ~GameThread();

    void join() { if (thread_ && thread_->joinable()) thread_->join(); }
    void launch();

  private:
    void run();
    GameOutcome play_game(player_array_t&);

    SharedData& shared_data_;
    player_instantiation_array_t instantiations_;
    std::thread* thread_ = nullptr;
    game_thread_id_t id_;
  };

public:
  GameServer(const Params& params);

  /*
   * A negative seat implies a random seat. Otherwise, the player generated is assigned the specified seat.
   *
   * The player generator is assigned a unique player_id_t (0, 1, 2, ...), according to the order in which the
   * registrations are made. When aggregate game outcome stats are reported, they are aggregated by player_id_t.
   *
   * Takes ownership of the pointer.
   */
  void register_player(seat_index_t seat, PlayerGenerator* gen) { shared_data_.register_player(seat, gen); }

  /*
   * Blocks until all players have registered.
   */
  void wait_for_remote_player_registrations();

  const Params& params() const { return shared_data_.params(); }
  int get_port() const { return params().port; }
  int num_registered_players() const { return shared_data_.num_registrations(); }
  void run();

private:
  SharedData shared_data_;
};

}  // namespace common

#include <common/inl/GameServer.inl>
