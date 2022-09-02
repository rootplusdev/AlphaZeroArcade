#!/usr/bin/env python3
import argparse
import numpy as np
import os
import pickle
import random
import subprocess
from tqdm import tqdm

import game

def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", '--c4-solver-dir', required=True,
                        help="base directory containing c4solver bin and 7x6.book")
    parser.add_argument("-n", "--num-training-games", type=int, help="number of training games")
    parser.add_argument("-o", "--output-file", default="output.pkl")

    return parser.parse_args()


def main():
    args = get_args()

    c4_solver_dir = os.path.expanduser(args.c4_solver_dir)
    c4_solver_bin = os.path.join(c4_solver_dir, 'c4solver')
    c4_solver_book = os.path.join(c4_solver_dir, '7x6.book')
    assert os.path.isdir(c4_solver_dir)
    assert os.path.isfile(c4_solver_bin)
    assert os.path.isfile(c4_solver_book)
    c4_cmd = f"{c4_solver_bin} -b {c4_solver_book} -a"
    proc = subprocess.Popen(c4_cmd, shell=True, stdout=subprocess.PIPE, stdin=subprocess.PIPE,
                            stderr=subprocess.PIPE, encoding='utf-8')

    record_count = 0

    with open(args.output_file, 'wb') as output_file:
        full_output = []

        for _ in tqdm(range(args.num_training_games), desc="Writing training games"):
            g = game.Game()
            game_output = []
            move_history = ''
            while True:
                record_count += 1

                proc.stdin.write(move_history + '\n')
                proc.stdin.flush()
                stdout = proc.stdout.readline()

                move_scores = list(map(int, stdout.split()[-7:]))

                best_score = max(move_scores)
                best_move_arr = (np.array(move_scores) == best_score).astype(int)
                cur_player_value = np.sign(best_score) * .5 + .5
                other_player_value = 1 - cur_player_value

                gvec = g.vectorize()
                data = (gvec, cur_player_value, best_move_arr)
                game_output.append(data)

                #
                # value_vec = np.array([0.0, 0.0])
                # value_vec[g.current_player] = cur_player_value
                # value_vec[1 - g.current_player] = other_player_value
                #
                # output_vec = np.concatenate((gvec, best_move_arr, value_vec))
                # output_file.write(' '.join(map(lambda s: '%g' % s, output_vec)))
                # output_file.write('\n')

                moves = g.get_valid_moves()
                if not moves:
                    break
                move = random.choice(moves)
                result = g.apply_move(move, announce=False)
                if result is not None:
                    break

                move_history += str(move)

            full_output.append(game_output)
        pickle.dump(full_output, output_file)

    print('record: ', record_count)


if __name__ == '__main__':
    main()