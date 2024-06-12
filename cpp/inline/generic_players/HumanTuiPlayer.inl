#include <generic_players/HumanTuiPlayer.hpp>

#include <cstdlib>
#include <iostream>

#include <util/ScreenUtil.hpp>

namespace generic {

template <core::concepts::Game Game>
inline void HumanTuiPlayer<Game>::start_game() {
  last_action_ = -1;
  std::cout << "Press any key to start game" << std::endl;
  std::string input;
  std::getline(std::cin, input);

  util::clearscreen();
}

template <core::concepts::Game Game>
inline void HumanTuiPlayer<Game>::receive_state_change(core::seat_index_t, const FullState&,
                                                       core::action_t action) {
  last_action_ = action;
}

template <core::concepts::Game Game>
core::ActionResponse HumanTuiPlayer<Game>::get_action_response(
    const FullState& state, const ActionMask& valid_actions) {
  util::ScreenClearer::clear_once();
  print_state(state, false);
  bool complain = false;
  core::action_t my_action = -1;
  while (true) {
    if (complain) {
      printf("Invalid input!\n");
    }
    complain = true;
    my_action = prompt_for_action(state, valid_actions);

    if (my_action < 0 || my_action >= Game::kNumActions || !valid_actions[my_action]) continue;
    break;
  }

  util::ScreenClearer::reset();
  return my_action;
}

template <core::concepts::Game Game>
inline void HumanTuiPlayer<Game>::end_game(const FullState& state, const ValueArray& outcome) {
  util::ScreenClearer::clear_once();
  print_state(state, true);

  auto seat = this->get_my_seat();
  if (outcome[seat] == 1) {
    std::cout << "Congratulations, you win!" << std::endl;
  } else if (outcome[1 - seat] == 1) {
    std::cout << "Sorry, you lose." << std::endl;
  } else {
    std::cout << "The game has ended in a draw." << std::endl;
  }
}

template <core::concepts::Game Game>
inline void HumanTuiPlayer<Game>::print_state(const FullState& state, bool terminal) {
  IO::print_snapshot(state.current(), last_action_, &this->get_player_names());
}

}  // namespace generic
