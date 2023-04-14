#include <common/PlayerFactory.hpp>

#include <algorithm>

#include <util/BoostUtil.hpp>
#include <util/Exception.hpp>
#include <util/StringUtil.hpp>

namespace common {

template<GameStateConcept GameState>
auto PlayerFactory<GameState>::Params::make_options_description() {
  namespace po = boost::program_options;
  namespace po2 = boost_util::program_options;

  po2::options_description desc("PlayerFactory options, for each instance of --player \"...\"");
  return desc
      .template add_option<"type">(po::value<std::string>(&type), "player type. Required")
      .template add_option<"name">(po::value<std::string>(&name), "Name. Required")
      .template add_option<"copy-from">(po::value<std::string>(&copy_from),
          "If specified, copy everything but --name and --seat from the --player with this name")
      .template add_option<"seat">(po::value<int>(&seat), "seat (0 or 1). Random if unspecified")
      ;
}

template<GameStateConcept GameState>
PlayerFactory<GameState>::PlayerFactory(const player_generator_creator_vec_t& creators)
: creators_(creators) {
  // validate that the generator types don't overlap
  std::set<std::string> types;
  for (auto* creator : creators_) {
    auto* generator = creator->create();
    for (const auto& type : generator->get_types()) {
      if (types.count(type)) {
        throw util::Exception("PlayerFactory: duplicate type: %s", type.c_str());
      }
      types.insert(type);
    }
    delete generator;
  }
}

template<GameStateConcept GameState>
typename PlayerFactory<GameState>::player_generator_seat_vec_t
PlayerFactory<GameState>::parse(const std::vector<std::string>& player_strs) {
  player_generator_seat_vec_t vec;

  for (const auto& player_str : player_strs) {
    std::vector<std::string> tokens = util::split(player_str);

    std::string name = boost_util::pop_option_value(tokens, "name");
    std::string seat_str = boost_util::pop_option_value(tokens, "seat");

    util::clean_assert(!name.empty(), "Missing --name in --player \"%s\"", player_str.c_str());

    int seat = -1;
    if (!seat_str.empty()) {
      seat = std::stoi(seat_str);
      util::clean_assert(seat < GameState::kNumPlayers, "Invalid seat (%d) in --player \"%s\"", seat, player_str.c_str());
    }

    player_generator_seat_t player_generator_seat;
    player_generator_seat.generator = parse_helper(player_str, name, tokens);
    player_generator_seat.seat = seat;

    vec.push_back(player_generator_seat);
  }

  return vec;
}

template<GameStateConcept GameState>
void PlayerFactory<GameState>::print_help(const std::vector<std::string>& player_strs)
{
  Params params;
  params.make_options_description().print(std::cout);
  std::cout << "  --... ...             type-specific args, dependent on --type" << std::endl << std::endl;

  std::cout << "For each player, you must pass something like:" << std::endl << std::endl;
  std::cout << "  --player \"--type=MCTS-C --name=CPU <type-specific options...>\"" << std::endl;
  std::cout << "  --player \"--type=TUI --name=Human --seat=1 <type-specific options...>\"" << std::endl << std::endl;
  std::cout << std::endl << std::endl;

  std::cout << "The set of legal --type values are:" << std::endl;

  player_generator_vec_t generators;
  for (auto* creator : creators_) {
    generators.push_back(creator->create());
  }
  for (auto* generator : generators) {
    std::cout << "  " << type_str(generator) << ": " << generator->get_description() << std::endl;
  }
  std::cout << std::endl;
  std::cout << "To see the options for a specific --type, pass -h --player \"--type=<type>\"" << std::endl;

  std::vector<bool> used_types(generators.size(), false);
  for (const std::string &s: player_strs) {
    std::vector<std::string> tokens = util::split(s);
    std::string type = boost_util::get_option_value(tokens, "type");
    for (int g = 0; g < (int)generators.size(); ++g) {
      if (matches(generators[g], type)) {
        used_types[g] = true;
        break;
      }
    }
  }

  for (int g = 0; g < (int)generators.size(); ++g) {
    if (!used_types[g]) continue;

    PlayerGenerator* generator = generators[g];

    std::ostringstream ss;
    generator->print_help(ss);
    std::string s = ss.str();
    if (!s.empty()) {
      std::cout << std::endl << "--type=" << type_str(generator) << " options:" << std::endl << std::endl;

      std::stringstream ss2(s);
      std::string line;
      while (std::getline(ss2, line, '\n')) {
        std::cout << "  " << line << std::endl;
      }
    }
  }

  for (auto* generator : generators) {
    delete generator;
  }
}

template<GameStateConcept GameState>
std::string PlayerFactory<GameState>::type_str(const PlayerGenerator* generator) {
  std::vector<std::string> types = generator->get_types();
  std::ostringstream ss;
  for (int k = 0; k < (int)types.size(); ++k) {
    if (k > 0) {
      ss << "/";
    }
    ss << types[k];
  }
  return ss.str();
}

template<GameStateConcept GameState>
bool PlayerFactory<GameState>::matches(const PlayerGenerator* generator, const std::string& type) {
  for (const auto& t : generator->get_types()) {
    if (t == type) {
      return true;
    }
  }
  return false;
}

template<GameStateConcept GameState>
typename PlayerFactory<GameState>::PlayerGenerator*
PlayerFactory<GameState>::parse_helper(
    const std::string& player_str, const std::string& name, const std::vector<std::string>& orig_tokens)
{
  std::vector<std::string> tokens = orig_tokens;

  std::string type = boost_util::pop_option_value(tokens, "type");
  std::string copy_from = boost_util::pop_option_value(tokens, "copy-from");

  if (!copy_from.empty()) {
    if (!type.empty()) {
      throw util::Exception("Invalid usage of --copy-from with --type in --player \"%s\"", player_str.c_str());
    }
    util::clean_assert(name_map_.count(copy_from), "Invalid --copy-from in --player \"%s\"", player_str.c_str());
    return parse_helper(player_str, name, name_map_.at(copy_from));
  }

  util::clean_assert(!type.empty(), "Must specify --type or --copy-from in --player \"%s\"", player_str.c_str());
  util::clean_assert(!name_map_.count(name), "Duplicate --name \"%s\"", name.c_str());
  name_map_[name] = orig_tokens;
  for (auto* creator : creators_) {
    auto* generator = creator->create();
    if (matches(generator, type)) {
      generator->set_name(name);
      generator->parse_args(tokens);
      return generator;
    }
    delete generator;
  }

  throw util::CleanException("Unknown type in --player \"%s\"", player_str.c_str());
}

}  // namespace common
