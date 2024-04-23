from dataclasses import dataclass
import math

from games.game_spec import GameSpec, ReferencePlayerFamily
from net_modules import ModelConfig, ModuleSpec
from util.torch_util import Shape


BOARD_LENGTH = 3
NUM_ACTIONS = BOARD_LENGTH * BOARD_LENGTH
NUM_PLAYERS = 2
NUM_POSSIBLE_END_OF_GAME_SQUARE_STATES = NUM_PLAYERS + 1  # +1 for empty square


def b3_c8(input_shape: Shape):
    board_shape = input_shape[1:]
    board_size = math.prod(board_shape)
    policy_shape = (NUM_ACTIONS, )
    c_trunk = 8
    c_mid = 8
    c_policy_hidden = 2
    c_value_hidden = 1
    n_value_hidden = 16

    return ModelConfig(
        input_shape=input_shape,

        stem=ModuleSpec(type='ConvBlock', args=[input_shape[0], c_trunk]),

        blocks=[
            ModuleSpec(type='ResBlock', args=['block1', c_trunk, c_mid]),
            ModuleSpec(type='ResBlock', args=['block2', c_trunk, c_mid]),
            ModuleSpec(type='ResBlock', args=['block3', c_trunk, c_mid]),
            ],

        heads=[
            ModuleSpec(type='PolicyHead',
                       args=['policy', board_size, c_trunk, c_policy_hidden, policy_shape]),
            ModuleSpec(type='ValueHead',
                       args=['value', board_size, c_trunk, c_value_hidden, n_value_hidden,
                             NUM_PLAYERS]),
            ],

        loss_weights={
            'policy': 1.0,
            'value': 1.5,
            },
        )


@dataclass
class TicTacToeSpec(GameSpec):
    name = 'tictactoe'
    model_configs = {
        'default': b3_c8,
        'b3_c8': b3_c8,
    }
    reference_player_family = ReferencePlayerFamily('Perfect', '--strength', 0, 1)
    n_mcts_iters_for_ratings_matches = 1


TicTacToe = TicTacToeSpec()
