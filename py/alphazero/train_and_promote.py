#!/usr/bin/env python3

"""
TODO: make this game-agnostic. There is some hard-coded c4 stuff in here at present.
"""
import argparse
import os
import random
from typing import Dict, List

import torch
import torch.nn as nn
from torch import optim

from alphazero.manager import AlphaZeroManager, Generation
from alphazero.optimization_args import add_optimization_args, OptimizationArgs, ModelingArgs, GatingArgs
from config import Config
from connect4.tensorizor import C4Net
from neural_net import NeuralNet
from util import subprocess_util
from util.py_util import timed_print
from util.repo_util import Repo


class Args:
    c4_base_dir: str

    @staticmethod
    def load(args):
        Args.c4_base_dir = args.c4_base_dir
        assert Args.c4_base_dir, 'Required option: -d'


def load_args():
    parser = argparse.ArgumentParser()
    cfg = Config.instance()
    cfg.add_parser_argument('c4.base_dir', parser, '-d', '--c4-base-dir', help='base-dir for game/model files')
    parser.add_argument('-T', '--temperature', type=float, default=0.2, help='mcts player temperature')
    parser.add_argument('-i', '--mcts-iters', type=int, default=300, help='num mcts player iters')
    parser.add_argument('-g', '--num-games', type=int, default=200, help='num games')
    add_optimization_args(parser)

    args = parser.parse_args()
    Args.load(args)
    OptimizationArgs.load(args)


class SelfPlayGameMetadata:
    def __init__(self, filename: str):
        self.filename = filename
        info = os.path.split(filename)[1].split('.')[0].split('-')  # 1685860410604914-10.ptd
        self.timestamp = int(info[0])
        self.num_positions = int(info[1])


class GenerationMetadata:
    def __init__(self, full_gen_dir: str):
        self.game_metadata_list = []
        for filename in os.listdir(full_gen_dir):
            if filename.startswith('.'):
                continue
            full_filename = os.path.join(full_gen_dir, filename)
            game_metadata = SelfPlayGameMetadata(full_filename)
            self.game_metadata_list.append(game_metadata)

        self.game_metadata_list.sort(key=lambda g: -g.timestamp)  # sort reverse chronological order
        self.num_positions = sum(g.num_positions for g in self.game_metadata_list)


def compute_n_window(N_total: int) -> int:
    """
    From Appendix C of KataGo paper.

    https://arxiv.org/pdf/1902.10565.pdf
    """
    c = ModelingArgs.window_c
    alpha = ModelingArgs.window_alpha
    beta = ModelingArgs.window_beta
    return min(N_total, int(c * (1 + beta * ((N_total / c) ** alpha - 1) / alpha)))


class SelfPlayMetadata:
    def __init__(self, self_play_dir: str):
        self.self_play_dir = self_play_dir
        self.metadata: Dict[Generation, GenerationMetadata] = {}
        self.n_total_positions = 0
        for gen_dir in os.listdir(self_play_dir):
            assert gen_dir.startswith('gen'), gen_dir
            generation = int(gen_dir[3:])
            full_gen_dir = os.path.join(self_play_dir, gen_dir)
            metadata = GenerationMetadata(full_gen_dir)
            self.metadata[generation] = metadata
            self.n_total_positions += metadata.num_positions

    def get_window(self, n_window: int) -> List[SelfPlayGameMetadata]:
        window = []
        cumulative_n_positions = 0
        for generation in reversed(self.metadata.keys()):
            gen_metadata = self.metadata[generation]
            n = len(gen_metadata.game_metadata_list)
            i = 0
            while cumulative_n_positions < n_window and i < n:
                game_metadata = gen_metadata.game_metadata_list[i]
                cumulative_n_positions += game_metadata.num_positions
                i += 1
                window.append(game_metadata)
        return window


class DataLoader:
    def __init__(self, manager: AlphaZeroManager):
        self.manager = manager
        self.self_play_metadata = SelfPlayMetadata(manager.self_play_dir)
        self.n_total = self.self_play_metadata.n_total_positions
        self.n_window = compute_n_window(self.n_total)
        self.window = self.self_play_metadata.get_window(self.n_window)

        self._returned_snapshots = 0
        self._index = len(self.window)

    def __iter__(self):
        return self

    def __next__(self):
        if self._returned_snapshots == ModelingArgs.snapshot_steps:
            raise StopIteration

        self._returned_snapshots += 1
        minibatch: List[SelfPlayGameMetadata] = []
        n = 0
        while n < ModelingArgs.minibatch_size:
            n += self._add_to_minibatch(minibatch)

        input_data = []
        policy_data = []
        value_data = []

        for metadata in minibatch:
            data = torch.jit.load(metadata.filename).state_dict()
            input_data.append(data['input'])
            policy_data.append(data['policy'])
            value_data.append(data['value'])

        input_data = torch.concat(input_data)
        policy_data = torch.concat(policy_data)
        value_data = torch.concat(value_data)

        return (input_data, policy_data, value_data)

    def _add_to_minibatch(self, minibatch: List[SelfPlayGameMetadata]):
        if self._index == len(self.window):
            random.shuffle(self.window)
            self._index = 0

        game_metadata = self.window[self._index]
        minibatch.append(game_metadata)
        self._index += 1
        return game_metadata.num_positions


def get_num_correct_policy_predictions(policy_outputs, policy_labels):
    selected_moves = torch.argmax(policy_outputs, dim=1)
    correct_policy_preds = policy_labels.gather(1, selected_moves.view(-1, 1))
    return int(sum(correct_policy_preds))


def get_num_correct_value_predictions(value_outputs, value_labels):
    value_output_probs = value_outputs.softmax(dim=1)
    deltas = abs(value_output_probs - value_labels)
    return int(sum((deltas < 0.25).all(dim=1)))


class TrainingStats:
    def __init__(self):
        self.policy_accuracy_num = 0.0
        self.policy_loss_num = 0.0
        self.value_accuracy_num = 0.0
        self.value_loss_num = 0.0
        self.den = 0

    def update(self, policy_labels, policy_outputs, policy_loss, value_labels, value_outputs, value_loss):
        n = len(policy_labels)
        self.policy_loss_num += float(policy_loss.item()) * n
        self.value_loss_num += float(value_loss.item()) * n
        self.den += n
        self.policy_accuracy_num += get_num_correct_policy_predictions(policy_outputs, policy_labels)
        self.value_accuracy_num += get_num_correct_value_predictions(value_outputs, value_labels)

    def dump(self):
        policy_accuracy = self.policy_accuracy_num / self.den
        avg_policy_loss = self.policy_loss_num / self.den
        value_accuracy = self.value_accuracy_num / self.den
        avg_value_loss = self.value_loss_num / self.den

        print(f'Policy accuracy: %5.3f' % policy_accuracy)
        print(f'Policy loss:     %5.3f' % avg_policy_loss)
        print(f'Value accuracy:  %5.3f' % value_accuracy)
        print(f'Value loss:      %5.3f' % avg_value_loss)


def gating_test(candidate_filename, latest_filename):
    self_play_bin = os.path.join(Repo.root(), 'target/Release/bin/c4_competitive_self_play')
    args = [
        self_play_bin,
        '-G', GatingArgs.num_games,
        '-i', GatingArgs.mcts_iters,
        '-t', GatingArgs.temperature,
        '--nnet-filename', latest_filename,
        '--nnet-filename2', candidate_filename,
    ]
    cmd = ' '.join(map(str, args))
    timed_print(f'Running: {cmd}')
    proc = subprocess_util.Popen(cmd)
    stdout, stderr = proc.communicate()
    if proc.returncode:
        print(stderr)
        raise Exception()

    lines = [line for line in stdout.splitlines() if line.startswith('P1 ')]
    assert len(lines) == 1, stdout
    candidate_results_line = lines[0]  # P1 W136 L30 D34 [153]
    win_rate_token = candidate_results_line.split()[-1]
    assert win_rate_token.startswith('[') and win_rate_token.endswith(']'), candidate_results_line
    win_rate = float(win_rate_token[1:-1]) / GatingArgs.num_games
    promote = win_rate > GatingArgs.promotion_win_rate
    timed_print('Run complete.')
    print(f'Candidate win-rate: %.5f' % win_rate)
    print(f'Promotion win-rate: %.5f' % GatingArgs.promotion_win_rate)
    print(f'Promote: %s' % promote)
    return promote


def main():
    load_args()
    manager = AlphaZeroManager(Args.c4_base_dir)
    manager.load_generation()

    latest_model_filename = manager.get_model_filename(manager.generation)
    candidate_filename = manager.get_current_candidate_model_filename()
    checkpoint_filename = manager.get_current_checkpoint_filename()
    net = C4Net.load_checkpoint(checkpoint_filename)

    value_loss_lambda = ModelingArgs.value_loss_lambda
    learning_rate = ModelingArgs.learning_rate
    momentum = ModelingArgs.momentum
    weight_decay = ModelingArgs.weight_decay
    optimizer = optim.SGD(net.parameters(), lr=learning_rate, momentum=momentum, weight_decay=weight_decay)

    policy_criterion = nn.MultiLabelSoftMarginLoss()
    value_criterion = nn.CrossEntropyLoss()

    epoch = 0
    while True:
        loader = DataLoader(manager)
        epoch += 1
        print('******************************')
        timed_print(f'Epoch: {epoch}')
        timed_print(f'Sampling from the {loader.n_window} most recent positions among {loader.n_total} total positions')

        stats = TrainingStats()

        for i, data in enumerate(loader):
            inputs, value_labels, policy_labels = data
            inputs = inputs.to('cuda')
            value_labels = value_labels.to('cuda')
            policy_labels = policy_labels.to('cuda')

            optimizer.zero_grad()
            policy_outputs, value_outputs = net(inputs)
            policy_loss = policy_criterion(policy_outputs, policy_labels)
            value_loss = value_criterion(value_outputs, value_labels)
            loss = policy_loss + value_loss * value_loss_lambda

            stats.update(policy_labels, policy_outputs, policy_loss, value_labels, value_outputs, value_loss)

            loss.backward()
            optimizer.step()

        timed_print(f'Epoch {epoch} complete')
        stats.dump()

        net.save_checkpoint(checkpoint_filename)
        net.save_model(candidate_filename)
        if gating_test(candidate_filename, latest_model_filename):
            break


if __name__ == '__main__':
    main()
