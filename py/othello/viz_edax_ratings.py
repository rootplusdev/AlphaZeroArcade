#!/usr/bin/env python3

"""
Use this script to visualize the progress of an othello alphazero run.

Usage:

cd py;

./alphazero/main_loop.py -g othello -t <TAG>

While the above is running, launch the grading daemon, preferably from a different machine:

./othello/assign_edax_ratings.py -t <TAG> -D

While the above is running, launch the visualizer:

./othello/viz_edax_ratings.py -t <TAG>
"""
import argparse
import json
import os
import pipes
import sqlite3
import sys

import numpy as np
from bokeh.layouts import column
from bokeh.models import ColumnDataSource, RangeSlider, CheckboxGroup
from bokeh.plotting import figure, curdoc
from natsort import natsorted

from config import Config


class Args:
    launch: bool
    alphazero_dir: str
    tag: str
    port: int

    @staticmethod
    def load(args):
        Args.launch = bool(args.launch)
        Args.alphazero_dir = args.alphazero_dir
        Args.tag = args.tag
        Args.port = args.port

        assert Args.tag, 'Required option: -t'


def load_args():
    parser = argparse.ArgumentParser()
    cfg = Config.instance()

    parser.add_argument('--launch', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('-t', '--tag', help='tag for this run (e.g. "v1")')
    cfg.add_parser_argument('alphazero_dir', parser, '-d', '--alphazero-dir', help='alphazero directory')
    parser.add_argument('-p', '--port', type=int, default=5006, help='bokeh port (default: %(default)s)')

    args = parser.parse_args()
    Args.load(args)


load_args()

if not Args.launch:
    script = os.path.abspath(__file__)
    args = ' '.join(map(pipes.quote, sys.argv[1:] + ['--launch']))
    cmd = f'bokeh serve --port {Args.port} --show {script} --args {args}'
    sys.exit(os.system(cmd))


base_dir = os.path.join(Args.alphazero_dir, 'othello', Args.tag)

metadata_filename = os.path.join(base_dir, 'metadata.json')
with open(metadata_filename, 'r') as f:
    metadata = json.load(f)

n_games = metadata['n_games']
mcts_iters = metadata['mcts_iters']

db_filename = os.path.join(base_dir, 'edax.db')
conn = sqlite3.connect(db_filename)
cursor = conn.cursor()
res = cursor.execute('SELECT mcts_gen, edax_rating FROM ratings WHERE mcts_iters = ? AND n_games >= ?',
                     (mcts_iters, n_games))

gen_rating_pairs = []
for mcts_gen, edax_rating in res.fetchall():
    gen_rating_pairs.append((mcts_gen, edax_rating))

conn.close()

gen_rating_pairs.sort()


class ProgressVisualizer:
    def __init__(self):
        self.x = np.array([g[0] for g in gen_rating_pairs])
        self.y = np.array([g[1] for g in gen_rating_pairs])

        self.source = ColumnDataSource()
        self.data = {'x': self.x, 'y': self.y}
        self.source.data = self.data

    def plot(self):
        source = self.source
        x = self.data['x']
        y = self.data['y']
        y_range = [0, max(y)+1]

        title = f'{Args.tag} Othello Alphazero Run (n_games={n_games}, mcts_iters={mcts_iters})'
        plot = figure(height=600, width=800, title=title, x_range=[x[0], x[-1]], y_range=y_range,
                      y_axis_label='Edax Rating', x_axis_label='Generation')  # , tools='wheel_zoom')
        plot.line('x', 'y', source=source, line_color='blue')

        inputs = column(plot)
        return inputs


viz = ProgressVisualizer()


curdoc().add_root(viz.plot())
