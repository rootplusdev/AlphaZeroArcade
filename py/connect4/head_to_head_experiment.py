#!/usr/bin/env python3
"""
Pit two players against each other.
"""
import time
from collections import defaultdict
import os
import sys

sys.path.append(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from game_runner import GameRunner
from connect4.game_logic import C4GameState
from connect4.nnet_player import NNetPlayer, NNetPlayerParams
from connect4.perfect_player import PerfectPlayer, PerfectPlayerParams


def main():
    use_perfect = False
    num_games = 1
    num_mcts_iters = 100

    if use_perfect:
        cpu1 = PerfectPlayer(PerfectPlayerParams())
        cpu1.set_name('Perfect')
    else:
        params1 = NNetPlayerParams(num_mcts_iters=num_mcts_iters)
        cpu1 = NNetPlayer(params1)
        cpu1.set_name('MCTS1-' + str(params1.num_mcts_iters))

    params2 = NNetPlayerParams(num_mcts_iters=num_mcts_iters)
    cpu2 = NNetPlayer(params2)
    cpu2.set_name('MCTS2-' + str(params2.num_mcts_iters))

    stats = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))  # name -> color -> W/L/D -> count

    def stats_str(name, color):
        counts = stats[name][color]
        w = counts[1]
        l = counts[0]
        d = counts[0.5]
        return f'{w}W {l}L {d}D'

    results_str_dict = {
        0: 'Y wins',
        0.5: 'draw  ',
        1: 'R wins',
    }

    r = range(1, num_games, 2) if use_perfect else range(num_games)
    t1 = time.time()
    n_games_played = 0
    for n in r:
        n_games_played += 1
        players = [None, None]
        m = n % 2
        players[m] = cpu1
        players[1-m] = cpu2

        runner = GameRunner(C4GameState, players)
        result = runner.run()

        for c in (0, 1):
            stats[players[c].get_name()][c][result[c]] += 1

        result_str = results_str_dict[result[0]]
        cumulative_result_str = ' '.join([
            f'R:{players[0].get_name()}:[{stats_str(players[0].get_name(), 0)}]',
            f'Y:{players[1].get_name()}:[{stats_str(players[1].get_name(), 1)}]',
        ])
        print(f'R:{players[0].get_name()} Y:{players[1].get_name()} Res:{result_str} {cumulative_result_str}')

    t2 = time.time()
    elapsed = t2 - t1
    avg_runtime = elapsed / n_games_played
    print('Avg runtime: %.3fs/game' % avg_runtime)


if __name__ == '__main__':
    main()
