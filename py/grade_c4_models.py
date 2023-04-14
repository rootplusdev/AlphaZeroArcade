#!/usr/bin/env python3

import argparse
import os
import time

from config import Config
from util import subprocess_util
from util.py_util import make_hidden_filename, timed_print
from util.repo_util import Repo


class Args:
    c4_base_dir_root: str
    tag: str
    n_games: int
    mcts_iters: int
    batch_size_limit: int
    parallelism_factor: int
    daemon_mode: bool

    @staticmethod
    def load(args):
        Args.c4_base_dir_root = args.c4_base_dir_root
        Args.tag = args.tag
        Args.n_games = args.n_games
        Args.mcts_iters = args.mcts_iters
        Args.batch_size_limit = args.batch_size_limit
        Args.parallelism_factor = args.parallelism_factor
        Args.daemon_mode = bool(args.daemon_mode)
        assert Args.tag, 'Required option: -t'


def load_args():
    parser = argparse.ArgumentParser()
    cfg = Config.instance()

    parser.add_argument('-t', '--tag', help='tag for this run (e.g. "v1")')
    cfg.add_parser_argument('c4.base_dir_root', parser, '-d', '--c4-base-dir-root',
                            help='base-dir-root for game/model files')
    parser.add_argument('-n', '--n-games', type=int, default=200,
                        help='number of games to play per generation (default: %(default)s))')
    parser.add_argument('-i', '--mcts-iters', type=int, default=300,
                        help='number of MCTS iterations per move (default: %(default)s)')
    parser.add_argument('-b', '--batch-size-limit', type=int, default=64,
                        help='batch size limit (default: %(default)s)')
    parser.add_argument('-p', '--parallelism-factor', type=int, default=50,
                        help='parallelism factor (default: %(default)s)')
    parser.add_argument('-D', '--daemon-mode', action='store_true', help='daemon mode (run forever)')

    args = parser.parse_args()
    Args.load(args)


class ModelGrader:
    def __init__(self):
        self.c4_base_dir = os.path.join(Args.c4_base_dir_root, Args.tag)
        assert os.path.isdir(self.c4_base_dir), self.c4_base_dir
        self.grading_logs_dir = os.path.join(self.c4_base_dir, 'grading-logs')
        os.makedirs(self.grading_logs_dir, exist_ok=True)
        self.next_gen = 1

    def get_model_filename(self, gen):
        return os.path.join(self.c4_base_dir, 'models', f'gen-{gen}.ptj')

    def get_log_filename(self, gen):
        return os.path.join(self.grading_logs_dir, f'gen-{gen}.log')

    def grade(self, gen):
        c4_bin = os.path.join(Repo.root(), 'target/Release/bin/c4')
        assert os.path.isfile(c4_bin)
        player_args = [
            '--type=MCTS-C',
            '--name=MCTS',
            '-i', Args.mcts_iters,
            '--batch-size-limit', Args.batch_size_limit,
            '--nnet-filename', self.get_model_filename(gen),
            '--no-forced-playouts',
            '--grade-moves',
        ]
        player2_args = [
            '--name=MCTS2',
            '--copy-from=MCTS',
        ]

        cmd = [
            c4_bin,
            '-G', Args.n_games,
            '-p', Args.parallelism_factor,
            '--player', '"%s"' % (' '.join(map(str, player_args))),
            '--player', '"%s"' % (' '.join(map(str, player2_args))),
                        ]
        cmd = ' '.join(map(str, cmd))
        log_filename = self.get_log_filename(gen)
        hidden_log_filename = make_hidden_filename(log_filename)
        cmd = f'{cmd} > {hidden_log_filename}'
        timed_print(f'Running: {cmd}')
        subprocess_util.run(cmd)
        os.rename(hidden_log_filename, log_filename)
        timed_print(f'Moved {hidden_log_filename} to {log_filename}')

    def run(self):
        os.system(f'rm -f {self.grading_logs_dir}/.*.log')  # clean up straggling tmp files
        while True:
            gen = self.next_gen
            model_filename = self.get_model_filename(gen)
            if not os.path.isfile(model_filename):
                break
            grading_log = self.get_log_filename(gen)
            if not os.path.isfile(grading_log):
                self.grade(gen)
            self.next_gen += 1


def main():
    load_args()
    grader = ModelGrader()

    if Args.daemon_mode:
        while True:
            grader.run()
            time.sleep(5)
    else:
        grader.run()


if __name__ == '__main__':
    main()
