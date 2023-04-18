#include <common/GameServerProxy.hpp>

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <common/Constants.hpp>
#include <common/Packet.hpp>
#include <util/Exception.hpp>

namespace common {

template <GameStateConcept GameState>
auto GameServerProxy<GameState>::Params::make_options_description() {
  namespace po = boost::program_options;
  namespace po2 = boost_util::program_options;

  po2::options_description desc("Remote GameServer options");

  return desc
      .template add_option<"remote-server">(po::value<std::string>(&remote_server)->default_value(remote_server),
          "Remote server to connect players to")
      .template add_option<"remote-port">(po::value<int>(&remote_port),
          "Remote port to connect players to. If not specified, run server in-process")
      ;
}

template <GameStateConcept GameState>
GameServerProxy<GameState>::SharedData::SharedData(const Params& params)
: params_(params)
{
  util::clean_assert(params_.remote_port > 0, "Remote port must be specified");
  socket_ = io::Socket::create_client_socket(params_.remote_server, params_.remote_port);
  std::cout << "Connected to the server!" << std::endl;
}

template <GameStateConcept GameState>
GameServerProxy<GameState>::SharedData::~SharedData() {
  for (auto gen : player_generators_) {
    delete gen;
  }
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::SharedData::register_player(seat_index_t seat, PlayerGenerator* gen) {
  std::string name = gen->get_name();
  util::clean_assert(name.size() + 1 < kMaxNameLength, "Player name too long (\"%s\" size=%d)",
                     name.c_str(), (int) name.size());

  seat_generators_.emplace_back(seat, gen);
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::SharedData::init_socket() {
  int n = seat_generators_.size();
  for (int i = 0; i < n; ++i) {
    seat_generator_t& seat_generator = seat_generators_[i];
    seat_index_t seat = seat_generator.seat;
    PlayerGenerator* gen = seat_generator.gen;

    std::string name = gen->get_name();

    printf("Registering player \"%s\" at seat %d\n", name.c_str(), seat);
    std::cout.flush();

    Packet<Registration> send_packet;
    Registration& registration = send_packet.payload();
    registration.remaining_requests = n - i - 1;
    registration.max_simultaneous_games = gen->max_simultaneous_games();
    registration.requested_seat = seat;

    char* name_buf = registration.dynamic_size_section.player_name;
    size_t name_buf_size = sizeof(registration.dynamic_size_section.player_name);
    strncpy(name_buf, name.c_str(), name_buf_size);
    name_buf[name_buf_size - 1] = '\0';  // not needed because of clean_assert() above, but makes compiler happy
    send_packet.set_dynamic_section_size(name.size() + 1);  // + 1 for null-delimiter
    send_packet.send_to(socket_);

    Packet<RegistrationResponse> recv_packet;
    recv_packet.read_from(socket_);
    const RegistrationResponse& response = recv_packet.payload();
    player_id_t player_id = response.player_id;
    util::clean_assert(player_id >= 0 && player_id < kNumPlayers, "Invalid player_id: %d", (int)player_id);

    player_generators_[player_id] = gen;
    printf("Registered player \"%s\" at seat %d (player_id:%d)\n", name.c_str(), seat, (int)player_id);
    std::cout.flush();
  }
}

template<GameStateConcept GameState>
GameServerProxy<GameState>::PlayerThread::PlayerThread(
    SharedData& shared_data, Player* player, game_thread_id_t game_thread_id, player_id_t player_id)
: shared_data_(shared_data)
, player_(player)
, game_thread_id_(game_thread_id)
, player_id_(player_id)
{
  thread_ = new std::thread([&] { run(); });
}

template<GameStateConcept GameState>
GameServerProxy<GameState>::PlayerThread::~PlayerThread() {
  delete thread_;
  delete player_;
}

template<GameStateConcept GameState>
void GameServerProxy<GameState>::PlayerThread::handle_start_game(const StartGame& payload) {
  game_id_t game_id = payload.game_id;
  player_name_array_t player_names;
  seat_index_t seat_assignment = payload.seat_assignment;
  payload.parse_player_names(player_names);

  player_->init_game(game_id, player_names, seat_assignment);
}

template<GameStateConcept GameState>
void GameServerProxy<GameState>::PlayerThread::handle_state_change(const StateChange& payload) {
  const char* buf = payload.dynamic_size_section.buf;

  seat_index_t seat;
  action_index_t action;

  state_.deserialize_state_change(buf, &seat, &action);
  player_->receive_state_change(seat, state_, action);
}

template<GameStateConcept GameState>
void GameServerProxy<GameState>::PlayerThread::handle_action_prompt(const ActionPrompt& payload) {
  const char* buf = payload.dynamic_size_section.buf;

  state_.deserialize_action_prompt(buf, &valid_actions_);
  cv_.notify_one();

  std::unique_lock lock(mutex_);
  cv_.wait(lock);

  send_action_packet();
}

template<GameStateConcept GameState>
void GameServerProxy<GameState>::PlayerThread::handle_end_game(const EndGame& payload) {
  const char* buf = payload.dynamic_size_section.buf;

  GameOutcome outcome;

  state_.deserialize_game_end(buf, &outcome);
  player_->end_game(state_, outcome);
}

template<GameStateConcept GameState>
void GameServerProxy<GameState>::PlayerThread::send_action_packet() {
  Packet<Action> packet;
  Action& action = packet.payload();
  char* buf = action.dynamic_size_section.buf;
  action.game_thread_id = game_thread_id_;
  action.player_id = player_id_;
  packet.set_dynamic_section_size(state_.serialize_action(buf, sizeof(buf), action_));
  packet.send_to(shared_data_.socket());
}

template<GameStateConcept GameState>
void GameServerProxy<GameState>::PlayerThread::run()
{
  while (true) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock);

    action_ = player_->get_action(state_, valid_actions_);
    lock.unlock();
    cv_.notify_one();
  }
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::run()
{
  shared_data_.init_socket();
  init_player_threads();

  while (true) {
    GeneralPacket response_packet;
    response_packet.read_from(shared_data_.socket());

    auto type = response_packet.header().type;
    switch (type) {
      case PacketHeader::kStartGame:
        handle_start_game(response_packet);
        break;
      case PacketHeader::kStateChange:
        handle_state_change(response_packet);
        break;
      case PacketHeader::kActionPrompt:
        handle_action_prompt(response_packet);
        break;
      case PacketHeader::kEndGame:
        handle_end_game(response_packet);
        break;
      default:
        throw util::Exception("Unexpected packet type: %d", (int) type);
    }
  }
}

template <GameStateConcept GameState>
GameServerProxy<GameState>::~GameServerProxy() {
  for (auto& array : thread_vec_) {
    for (auto& thread : array) {
      delete thread;
    }
  }
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::init_player_threads()
{
  Packet<GameThreadInitialization> recv_packet;
  recv_packet.read_from(shared_data_.socket());
  int num_game_threads = recv_packet.payload().num_game_threads;

  for (game_thread_id_t g = 0; g < (game_thread_id_t)num_game_threads; ++g) {
    thread_array_t& array = thread_vec_.emplace_back();
    for (player_id_t p = 0; p < (player_id_t)kNumPlayers; ++p) {
      array[p] = nullptr;
      PlayerGenerator* gen = shared_data_.get_gen(p);
      if (gen) {
        Player* player = gen->generate(g);
        array[p] = new PlayerThread(shared_data_, player, g, p);
      }
    }
  }

  Packet<GameThreadInitializationResponse> send_packet;
  send_packet.send_to(shared_data_.socket());
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::handle_start_game(const GeneralPacket& packet) {
  const StartGame& payload = packet.payload_as<StartGame>();
  thread_vec_[payload.game_thread_id][payload.player_id]->handle_start_game(payload);
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::handle_state_change(const GeneralPacket& packet) {
  const StateChange& payload = packet.payload_as<StateChange>();
  thread_vec_[payload.game_thread_id][payload.player_id]->handle_state_change(payload);
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::handle_action_prompt(const GeneralPacket& packet) {
  const ActionPrompt& payload = packet.payload_as<ActionPrompt>();
  thread_vec_[payload.game_thread_id][payload.player_id]->handle_action_prompt(payload);
}

template <GameStateConcept GameState>
void GameServerProxy<GameState>::handle_end_game(const GeneralPacket& packet) {
  const EndGame& payload = packet.payload_as<EndGame>();
  thread_vec_[payload.game_thread_id][payload.player_id]->handle_end_game(payload);
}

}  // namespace common
