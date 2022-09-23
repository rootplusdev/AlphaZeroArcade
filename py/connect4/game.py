import random
from typing import Optional, List

import numpy as np
from termcolor import colored


NUM_COLUMNS = 7
NUM_ROWS = 6
MAX_MOVES_PER_GAME = NUM_ROWS * NUM_COLUMNS
Color = int


COLORS = ['R', 'Y']
NUM_COLORS = len(COLORS)
CIRCLE_CHAR = chr(9679)
PRETTY_COLORS = [colored(CIRCLE_CHAR, c) for c in ('red', 'yellow')]


class Game:
    RED = 0  # first player
    YELLOW = 1

    def __init__(self, piece_mask=None, current_player=None):
        self.piece_mask = np.zeros((2, NUM_COLUMNS, NUM_ROWS), dtype=bool) if piece_mask is None else piece_mask
        self.current_player = Game.RED if current_player is None else current_player

    def clone(self) -> 'Game':
        return Game(np.copy(self.piece_mask), self.current_player)

    def get_num_moves_played(self) -> int:
        return sum(self.piece_mask)

    def get_valid_moves(self) -> List[int]:
        cur_heights = np.sum(np.sum(self.piece_mask, axis=0), axis=1)
        assert cur_heights.shape == (NUM_COLUMNS, )
        return [c+1 for c, h in enumerate(cur_heights) if h < NUM_ROWS]

    def get_current_player(self) -> Color:
        return self.current_player

    def get_mask(self, color: Color) -> np.ndarray:
        return self.piece_mask[color]

    def undo_move(self, column: int):
        """
        Assumes that self.apply_move(column) was just invoked, and undoes that call.
        """
        assert 1 <= column <= NUM_COLUMNS

        cur_height = np.sum(self.piece_mask[:, column-1])
        assert cur_height > 0
        self.piece_mask[:][column-1][cur_height-1] = 0
        self.current_player = 1 - self.current_player

    def apply_move(self, column: int, announce: bool = False) -> Optional[Color]:
        """
        column is 1-indexed

        Returns the winning color if the game has ended. Else returns None.
        """
        assert 1 <= column <= NUM_COLUMNS

        cur_height = np.sum(self.piece_mask[:, column-1])
        key = (self.current_player, column-1, cur_height)
        key2 = (1-self.current_player, column-1, cur_height)
        assert self.piece_mask[key] == 0
        assert self.piece_mask[key2] == 0
        self.piece_mask[key] = 1

        move_color = self.current_player
        self.current_player = 1 - self.current_player

        winner = self.compute_winner(column, cur_height, move_color)

        if announce and winner is not None:
            cur_color = 'R' if self.current_player == Game.RED else 'Y'
            print(f'** {cur_color} playing in column {column}')
            print(f'Winner! {cur_color}')
        return winner

    def compute_winner(self, column, cur_height, move_color):
        mask = self.piece_mask[move_color]
        dir_tuple_set = (
            ((-1, 0), (+1, 0)),  # horizontal
            ((0, -1),),  # vertical
            ((-1, -1), (+1, +1)),  # diagonal 1
            ((-1, +1), (+1, -1)),  # diagonal 2
        )
        for dir_tuples in dir_tuple_set:
            count = 1
            for (dc, dr) in dir_tuples:
                c = column - 1
                r = cur_height
                while count < 4:
                    c += dc
                    r += dr
                    if 0 <= c < NUM_COLUMNS:
                        if 0 <= r < NUM_ROWS:
                            if mask[(c, r)]:
                                count += 1
                                continue
                    break
                if count == 4:
                    return move_color
        return None

    def to_ascii_drawing(self, pretty_print=True, newline=True, add_legend=False, player_names=('1', '2'),
                         highlight_column=None) -> str:
        colors = PRETTY_COLORS if pretty_print else COLORS
        empty_color = ' ' if pretty_print else '.'
        char_matrix = [[empty_color for _ in range(NUM_COLUMNS)] for _ in range(NUM_ROWS)]
        blink_x = -1
        blink_y = -1
        for (c, color) in enumerate(colors):
            for x, y in zip(*np.where(self.piece_mask[c])):
                char_matrix[y][x] = color
                if x+1 == highlight_column:
                    blink_x = x
                    blink_y = max(y, blink_y)

        if blink_x > 0:
            char_matrix[blink_y][blink_x] = colored(char_matrix[blink_y][blink_x], attrs=['blink'])

        out_lines = list(reversed([''.join(char_list) for char_list in char_matrix]))
        if add_legend:
            out_lines.append('1234567')
            out_lines.append(f'{colors[0]}: {player_names[0]}')
            out_lines.append(f'{colors[1]}: {player_names[1]}')
        if newline:
            out_lines.append('')
        return '\n'.join(out_lines)

    def __hash__(self):
        return hash(tuple(self.piece_mask.data))

    def __eq__(self, other: 'Game'):
        return all(np.all(m1 == m2) for m1, m2 in (self.piece_mask, other.piece_mask))


if __name__ == '__main__':
    for _ in range(1000):
        g = Game()
        while True:
            moves = g.get_valid_moves()
            if not moves:
                # print('Game is drawn!')
                break
            result = g.apply_move(random.choice(moves), announce=False)
            # print(g.to_ascii_drawing())
            if result is not None:
                break

    print('Played all games!')
