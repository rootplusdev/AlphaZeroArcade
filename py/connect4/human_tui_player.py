import os
import sys
from io import StringIO
from typing import List, Optional

from interface import AbstractPlayer, PlayerIndex, ActionIndex, GameResult, ActionMask
from connect4.game_logic import C4GameState


class C4HumanTuiPlayer(AbstractPlayer):
    def __init__(self):
        super(C4HumanTuiPlayer, self).__init__('Human')
        self.player_names = ['?', '?']
        self.my_index = None
        self.last_action: Optional[ActionIndex] = None
        self.buf = StringIO()

    def start_game(self, players: List[AbstractPlayer], seat_assignment: PlayerIndex):
        self.player_names = [p.get_name() for p in players]
        self.my_index = seat_assignment
        sys.stdout = self.buf

    def sys_switch(self, state: C4GameState):
        sys.stdout = sys.__stdout__
        self.print_state(state)
        sys.stdout.write(self.buf.getvalue())

    def receive_state_change(self, p: PlayerIndex, state: C4GameState,
                             action_index: ActionIndex, result: GameResult):
        self.last_action = action_index
        if result is not None:
            self.sys_switch(state)

    def print_state(self, state: C4GameState):
        column = None if self.last_action is None else self.last_action + 1
        os.system('clear')
        print(state.to_ascii_drawing(add_legend=True, player_names=self.player_names, highlight_column=column))

    def get_action(self, state: C4GameState, valid_actions: ActionMask) -> ActionIndex:
        self.sys_switch(state)

        my_action = None
        while True:
            if my_action is not None:
                self.print_state(state)
                sys.stdout.write(self.buf.getvalue())
                print(f'Invalid input!')
            my_action = input('Enter move [1-7]: ')
            try:
                my_action = int(my_action) - 1
                assert valid_actions[my_action]
                break
            except:
                continue

        self.buf = StringIO()
        sys.stdout = self.buf
        return my_action
