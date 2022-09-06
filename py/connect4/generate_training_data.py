#!/usr/bin/env python3
import argparse
from typing import Tuple

import h5py
from mpi4py import MPI
import numpy as np
import os
import random
import subprocess
from tqdm import tqdm

import game
from game import Game, NUM_ROWS, NUM_COLUMNS


RANK = MPI.COMM_WORLD.Get_rank()
SIZE = MPI.COMM_WORLD.Get_size()


def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", '--c4-solver-dir', required=True,
                        help="base directory containing c4solver bin and 7x6.book")
    parser.add_argument("-n", "--num-training-games", type=int, help="number of training games")
    parser.add_argument("-g", "--games-dir", default="c4_games")
    parser.add_argument("-s", "--num-previous-states", type=int, default=3,
                        help='how many previous board states to use')

    return parser.parse_args()


def main():
    args = get_args()

    if not RANK:
        if os.path.exists(args.games_dir):
            os.system(f'rm -rf {args.games_dir}')
        os.makedirs(args.games_dir, exist_ok=True)

    MPI.COMM_WORLD.barrier()
    launch(args)


Shape = Tuple[int, ...]


def chunkify(shape: Shape, size=1024) -> Shape:
    values = list(shape)
    values[0] = min(values[0], size)
    return tuple(values)


def launch(args):
    verbose = not RANK
    num_previous_states = args.num_previous_states
    c4_solver_dir = os.path.expanduser(args.c4_solver_dir)
    c4_solver_bin = os.path.join(c4_solver_dir, 'c4solver')
    c4_solver_book = os.path.join(c4_solver_dir, '7x6.book')
    assert os.path.isdir(c4_solver_dir)
    assert os.path.isfile(c4_solver_bin)
    assert os.path.isfile(c4_solver_book)
    c4_cmd = f"{c4_solver_bin} -b {c4_solver_book} -a"
    proc = subprocess.Popen(c4_cmd, shell=True, stdout=subprocess.PIPE, stdin=subprocess.PIPE,
                            stderr=subprocess.PIPE, encoding='utf-8')

    output_filename = os.path.join(args.games_dir, f'{RANK}.h5')
    n = args.num_training_games
    start = RANK * n // SIZE
    end = (RANK+1) * n // SIZE
    num_my_games = end - start
    max_rows = num_my_games * NUM_ROWS * NUM_COLUMNS

    with h5py.File(output_filename, 'w') as output_file:
        input_shape = (max_rows, num_previous_states*2+3, NUM_COLUMNS, NUM_ROWS)
        value_output_shape = (max_rows, 1)
        policy_output_shape = (max_rows, NUM_COLUMNS)

        write_index = 0
        input_dataset = output_file.create_dataset('input', shape=input_shape, dtype=np.float32,
                                                   chunks=chunkify(input_shape))
        value_output_dataset = output_file.create_dataset('value_output', shape=value_output_shape, dtype=np.float32,
                                                          chunks=chunkify(value_output_shape))
        policy_output_dataset = output_file.create_dataset('policy_output', shape=policy_output_shape,
                                                           dtype=np.float32, chunks=chunkify(policy_output_shape))

        tqdm_range = tqdm(range(num_my_games), desc="Writing training games") if verbose else range(num_my_games)
        for _ in tqdm_range:
            g = game.Game()

            full_red_mask = np.zeros((num_previous_states + 1, NUM_COLUMNS, NUM_ROWS), dtype=bool)
            full_yellow_mask = np.zeros((num_previous_states + 1, NUM_COLUMNS, NUM_ROWS), dtype=bool)

            move_history = ''
            while True:
                proc.stdin.write(move_history + '\n')
                proc.stdin.flush()
                stdout = proc.stdout.readline()

                move_scores = list(map(int, stdout.split()[-NUM_COLUMNS:]))

                best_score = max(move_scores)
                best_move_arr = (np.array(move_scores) == best_score)
                cur_player_value = np.sign(best_score)

                shape1 = (1, NUM_COLUMNS, NUM_ROWS)

                cur_player = g.get_current_player()
                cur_player_mask = np.zeros(shape1, dtype=bool) + cur_player
                if cur_player == Game.RED:
                    yellow_mask = g.get_mask(Game.YELLOW)
                    full_yellow_mask = np.concatenate((yellow_mask.reshape(shape1), full_yellow_mask[:-1]))
                else:
                    red_mask = g.get_mask(Game.RED)
                    full_red_mask = np.concatenate((red_mask.reshape(shape1), full_red_mask[:-1]))

                input_matrix = np.concatenate((full_red_mask, full_yellow_mask, cur_player_mask))

                input_dataset[write_index] = input_matrix
                value_output_dataset[write_index] = cur_player_value
                policy_output_dataset[write_index] = best_move_arr
                write_index += 1

                moves = g.get_valid_moves()
                if not moves:
                    break
                move = random.choice(moves)
                result = g.apply_move(move, announce=False)
                if result is not None:
                    break

                move_history += str(move)

        input_dataset.resize(write_index, axis=0)
        value_output_dataset.resize(write_index, axis=0)
        policy_output_dataset.resize(write_index, axis=0)

    if verbose:
        generic_output_filename = os.path.join(args.games_dir, '*.h5')
        print('')
        print(f'Wrote to: {generic_output_filename}')


if __name__ == '__main__':
    main()