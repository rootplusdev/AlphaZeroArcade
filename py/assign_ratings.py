#!/usr/bin/env python3

"""
Assigns skill ratings to a fixed set of agents produced by the alphazero main loop.

We use the basic Bradley-Terry model, without the generalized capabilities of first-player advantage or draws. This
model models agent i's skill as a parameter beta_i, and predicts the expected win-rate in a match between agent i
and agent j as:

P(i beats j) = e^beta_i / (e^beta_i + e^beta_j)

To estimate the beta_i's, we run a series of matches between the agents. Below is a description of the methodology.

Let N be the total number of agents in our system. As a baseline, we can use the agents produced by the alphazero main
loop, with each agent configured to use i=256 MCTS iterations. If we want, we can produce additional agents by
parameterizing those baseline agents differently (e.g., using a different number of MCTS iterations). We can also
add additional non-MCTS agents (Random, Perfect for c4, etc.).

We have some expectations on the relative skill level of these agents. For example, we expect the generation-k agent
to be inferior to the generation-(k+1) agent. If we choose to use different parameterizations of the same generation,
we have expectations amongst those. For example, all else equal, we expect that increasing the number of MCTS
iterations increases the skill level of the agent. We also expect that the random agent will be inferior to all other
agents, and that the perfect agent will be superior to all other agents. We can encode all these expectations into an
(n, n)-shaped boolean "Expected Relative Skill Matrix", E. The (i, j)-th entry of this matrix will be true if we
expect agent i to be inferior to agent j.

Let G be the N-node graph with directed edges defined by this matrix E. G should not contain cycles if the skill
expectations are coherent. Thus, G is a DAG, and we can extend it to its transitive closure by adding edges between
nodes u and v if there is a directed path from u to v in G.

We initialize our game records data by first including a fractional virtual win for agent i over agent j for each edge
(i, j) in G. This softly encodes our expectations on the relative skill levels of the agents.

After this, we repeatedly sample an edge of G and then run a set of matches between the corresponding agents. Consider
the digraph H formed by drawing an edge from u to v whenever u records at least one win (or draw) against v. We
require H to be strongly connected before we can make proper predictions, so our match sampling will initially favor
sampling edges that can potentially connect clusters of H. The sampling details can be found in the code.
"""
import argparse
import os
import random
import sqlite3
from typing import Optional, Dict, List

from natsort import natsorted
import numpy as np

from alphazero.ratings import extract_match_record, compute_ratings
from config import Config
from util import subprocess_util
from util.py_util import timed_print


class Args:
    alphazero_dir: str
    binary: Optional[str]
    game: str
    tag: str
    clear_db: bool
    n_games: int
    mcts_iters: int
    parallelism_factor: int
    num_rounds: int

    @staticmethod
    def load(args):
        Args.alphazero_dir = args.alphazero_dir
        Args.binary = args.binary
        Args.game = args.game
        Args.tag = args.tag
        Args.clear_db = bool(args.clear_db)
        Args.n_games = args.n_games
        Args.mcts_iters = args.mcts_iters
        Args.parallelism_factor = args.parallelism_factor
        Args.num_rounds = args.num_rounds
        assert Args.tag, 'Required option: -t'


def load_args():
    parser = argparse.ArgumentParser()
    cfg = Config.instance()

    cfg.add_parser_argument('alphazero_dir', parser, '-d', '--alphazero-dir', help='alphazero directory')
    parser.add_argument('-g', '--game', help='game to play (e.g. "c4")')
    parser.add_argument('-t', '--tag', help='tag for this run (e.g. "v1")')
    parser.add_argument('-b', '--binary', help='binary to use for playing games (default: binary saved by alphazero process')
    parser.add_argument('-C', '--clear-db', action='store_true', help='clear everything from database')
    parser.add_argument('-n', '--n-games', type=int, default=64,
                        help='number of games to play per matchup (default: %(default)s))')
    parser.add_argument('-i', '--mcts-iters', type=int, default=256,
                        help='number of MCTS iterations per move (default: %(default)s)')
    parser.add_argument('-p', '--parallelism-factor', type=int, default=64,
                        help='parallelism factor (default: %(default)s)')
    parser.add_argument('-r', '--num-rounds', type=int, default=10,
                        help='runs matches until each agent has played this many rounds (default: %(default)s)')

    args = parser.parse_args()
    Args.load(args)


def inject_arg(cmdline: str, arg_name: str, arg_value: str):
    """
    Takes a cmdline and adds {arg_name}={arg_value} into it, overriding any existing value for {arg_name}.
    """
    assert arg_name.startswith('-'), arg_name
    tokens = cmdline.split()
    for i, token in enumerate(tokens):
        if token == arg_name:
            tokens[i+1] = arg_value
            return ' '.join(tokens)

        if token.startswith(f'{arg_name}='):
            tokens[i] = f'{arg_name}={arg_value}'
            return ' '.join(tokens)

    return f'{cmdline} {arg_name} {arg_value}'


def inject_args(cmdline: str, kwargs: dict):
    for arg_name, arg_value in kwargs.items():
        cmdline = inject_arg(cmdline, arg_name, arg_value)
    return cmdline


def construct_cmd(binary: str,
                  player_str: str,
                  binary_kwargs: Optional[dict] = None,
                  player_kwargs: Optional[dict] = None)-> str:
    cmd = binary
    if binary_kwargs:
        for key, value in binary_kwargs.items():
            assert key.startswith('-'), key
            cmd += f' {key} {value}'

    if player_kwargs:
        for key, value in player_kwargs.items():
            player_str = inject_arg(player_str, key, str(value))

    cmd += f' --player "{player_str}"'
    return cmd


class Agent:
    def __init__(self, cmd: str, *,
                 index: int = -1,
                 rand: bool = False,
                 perfect: bool = False,
                 gen: Optional[int] = None,
                 iters: Optional[int] = None,
                 row_id: Optional[int] = None):
        """
        cmd looks like:

        <binary> --player "--type=MCTS-C -m /media/dshin/c4f/models/gen-10.ptj"

        See documentation at top of file for description of "special".
        """
        assert cmd.count('"') == 2, cmd
        tokens = cmd.split()
        assert tokens[1] == '--player', cmd
        assert '--player' not in tokens[3:], cmd
        assert tokens[2].startswith('"'), cmd
        assert tokens[-1].endswith('"'), cmd

        self.cmd = cmd
        self.binary = tokens[0]
        self.player_str = cmd[cmd.find('"') + 1: cmd.rfind('"')]

        self.index = index
        self.rand = rand
        self.perfect = perfect
        self.gen = gen
        self.iters = iters
        self.row_id = row_id

    def get_cmd(self, binary: Optional[str] = None, binary_kwargs: Optional[dict] = None,
                player_kwargs: Optional[dict] = None):
        binary = binary if binary else self.binary
        return construct_cmd(binary, self.player_str, binary_kwargs, player_kwargs)

    def __str__(self):
        return f'Agent("{self.player_str}")'

    def __repr__(self):
        return str(self)


class Arena:
    def __init__(self):
        self.base_dir = os.path.join(Args.alphazero_dir, Args.game, Args.tag)
        assert os.path.isdir(self.base_dir), self.base_dir
        self.arena_dir = os.path.join(self.base_dir, 'arena')
        os.makedirs(self.arena_dir, exist_ok=True)

        self.agents: List[Agent] = []
        self.agent_dict: Dict[int, Agent] = {}
        self.E = None  # expected relative skill matrix
        self.beta = None  # skill estimates
        self.virtual_wins = None
        self.real_wins = None
        self.db_filename = os.path.join(self.arena_dir, 'arena.db')
        self._conn = None

    @property
    def conn(self) -> sqlite3.Connection:
        if self._conn is None:
            self._conn = sqlite3.connect(self.db_filename)
        return self._conn

    def init_db(self):
        if os.path.isfile(self.db_filename):
            if Args.clear_db:
                os.remove(self.db_filename)
            else:
                return

        timed_print('Initializing database')
        c = self.conn.cursor()
        agents_table_exists = c.execute('SELECT name FROM sqlite_master WHERE type="table" AND name="agents"').fetchone()
        if not agents_table_exists:
            c.execute("""CREATE TABLE agents (
                cmd,
                rand,
                perfect,
                gen,
                iters);
            """)
            c.execute("""CREATE UNIQUE INDEX IF NOT EXISTS lookup ON agents (cmd);""")

            players_dir = os.path.join(self.base_dir, 'players')
            for filename in natsorted(os.listdir(players_dir)):  # gen-13.txt
                if filename.startswith('.') or not filename.endswith('.txt'):
                    continue

                gen = int(filename.split('.')[0].split('-')[1])
                if gen % 10 != 0:
                    # only use every 10th generation for now
                    continue

                with open(os.path.join(players_dir, filename)) as f:
                    cmd = f.read().strip()

                agent = Agent(cmd)
                cmd_with_iters = agent.get_cmd(player_kwargs={'-i': Args.mcts_iters})
                cmd_tuples = [(cmd_with_iters, False, False, gen, Args.mcts_iters)]
                if gen == 0:
                    binary = Args.binary if Args.binary else agent.binary
                    rand_cmd = f'{binary} --player "--type=Random"'
                    perfect_cmd = f'{binary} --player "--type=Perfect"'
                    cmd4 = agent.get_cmd(player_kwargs={'-i': 4})
                    cmd16 = agent.get_cmd(player_kwargs={'-i': 16})
                    cmd64 = agent.get_cmd(player_kwargs={'-i': 64})

                    cmd_tuples = [
                        (rand_cmd, True, False, -1, 0),
                        (perfect_cmd, False, True, -1, 0),
                        (cmd4, False, False, gen, 4),
                        (cmd16, False, False, gen, 16),
                        (cmd64, False, False, gen, 64),
                    ] + cmd_tuples

                c.executemany('INSERT INTO agents VALUES (?, ?, ?, ?, ?)', cmd_tuples)

        # TODO: generalize below table for multiplayer games
        c.execute("""CREATE TABLE IF NOT EXISTS matches (
            agent_id1 INT,
            agent_id2 INT,
            wins1 INT,
            draws INT,
            wins2 INT);
        """)
        c.execute("""CREATE INDEX IF NOT EXISTS matches_agent_id1 ON matches (agent_id1);""")
        c.execute("""CREATE INDEX IF NOT EXISTS matches_agent_id2 ON matches (agent_id2);""")

        # an entry in ratings is computed using all match data up to and including match_id
        c.execute("""CREATE TABLE IF NOT EXISTS ratings (
            agent_id INT,
            match_id INT,
            beta REAL);
        """)
        c.execute("""CREATE UNIQUE INDEX IF NOT EXISTS lookup ON ratings (agent_id, match_id);""")
        self.conn.commit()

    def init_agents(self):
        c = self.conn.cursor()
        res = c.execute('SELECT rowid, cmd, rand, perfect, gen, iters FROM agents')
        for row_id, cmd, rand, perfect, gen, iters in res.fetchall():
            agent = Agent(cmd, index=len(self.agents), row_id=row_id, rand=rand, perfect=perfect, gen=gen, iters=iters)
            self.agents.append(agent)
            self.agent_dict[row_id] = agent

        n = len(self.agents)
        self.beta = np.zeros(n, dtype=np.float64)

        assert self.agents[0].rand
        assert all(not agent.rand for agent in self.agents[1:])

        timed_print(f'Loaded {n} agents')

    def init_expected_relative_skill_matrix(self):
        n = len(self.agents)
        self.E = np.zeros((n, n), dtype=bool)
        for i in range(n):
            agent_i = self.agents[i]
            if agent_i.rand:
                self.E[i] = 1
                continue
            if agent_i.perfect:
                self.E[:, i] = 1
                continue
            for j in range(n):
                if i == j:
                    continue
                agent_j = self.agents[j]
                if agent_j.rand or agent_j.perfect:
                    continue

                if agent_i.gen < agent_j.gen:
                    self.E[i, j] = 1
                    continue
                elif agent_i.gen == agent_j.gen and agent_i.iters < agent_j.iters:
                    self.E[i, j] = 1
                    continue

        np.fill_diagonal(self.E, 0)

        # Above construction should yield a matrix that is its own transitive closure
        # self.E = transitive_closure(self.E)
        timed_print(f'Constructed ({n}, {n})-shaped Expected Relative Skill Matrix')

    def add_virtual_wins(self):
        # If agent i is expected to be inferior to agent j, record .01 wins of i over j, and .1 wins for j over i.
        Ef = self.E.astype(float)
        self.virtual_wins = 0.1 * Ef.T + 0.01 * Ef
        timed_print(f'Added virtual wins')

    def load_matches(self):
        n = len(self.agents)
        self.real_wins = np.zeros((n, n), dtype=float)
        c = self.conn.cursor()
        res = c.execute('SELECT rowid, agent_id1, agent_id2, wins1, draws, wins2 FROM matches')
        match_count = 0
        for row_id, agent_id1, agent_id2, wins1, draws, wins2 in res.fetchall():
            match_count += 1
            agent1 = self.agent_dict[agent_id1]
            agent2 = self.agent_dict[agent_id2]

            i1 = agent1.row_id - 1
            i2 = agent2.row_id - 1
            self.real_wins[i1, i2] = wins1 + 0.5 * draws
            self.real_wins[i2, i1] = wins2 + 0.5 * draws
        timed_print(f'Loaded {match_count} matches')

    def launch(self):
        self.init_db()
        self.init_agents()
        self.init_expected_relative_skill_matrix()
        self.add_virtual_wins()
        self.load_matches()
        self.update_ratings()
        self.play_rounds()

    def play_rounds(self):
        """
        For now, we do something really simple. We randomly pick an agent A that has played fewer than round_num
        total matches. We then identify the set S of agents that A has not yet played, and randomly choose an agent B
        among the agents of S that have played the fewest number of total matches. We then play a match between A and B.
        The round ends once every agent has played at least round_num total matches.

        Later, we can make this more sophisticated, choosing matchups that are more likely to be informative.
        """
        last_round_num = None
        while True:
            match_matrix = (self.real_wins + self.real_wins.T) > 0
            match_counts = np.sum(match_matrix, axis=1)
            round_num = np.min(match_counts)
            if last_round_num not in (None, round_num):
                self.commit_ratings()
            last_round_num = round_num
            if round_num == Args.num_rounds:
                break

            candidates = np.where(match_counts == round_num)[0]
            timed_print(f'Round {round_num + 1} num_candidates: {len(candidates)}')
            if len(candidates) == 0:
                break

            i = random.choice(candidates)

            candidate_opponent_arr = match_matrix[i] == 0
            candidate_opponent_arr[i] = False
            minimal_candidate_opponent_arr = match_counts * candidate_opponent_arr
            if np.sum(minimal_candidate_opponent_arr) == 0:
                candidates = np.where(candidate_opponent_arr)[0]
            else:
                m = np.min(minimal_candidate_opponent_arr[minimal_candidate_opponent_arr > 0])
                candidates = np.where((match_counts == m) & candidate_opponent_arr)[0]
            assert len(candidates) > 0

            j = random.choice(candidates)

            self.play_match(self.agents[i], self.agents[j])
            self.update_ratings()

    def play_match(self, agent1: Agent, agent2: Agent):
        timed_print('Playing match')
        timed_print(f'Agent 1: {agent1}')
        timed_print(f'Agent 2: {agent2}')

        binary_kwargs = {'-G': Args.n_games}

        player1_kwargs = {'--name': 'Player1'}
        player2_kwargs = {'--name': 'Player2'}

        port = 12345
        binary_kwargs['--port'] = port
        cmd1 = agent1.get_cmd(binary=Args.binary, binary_kwargs=binary_kwargs, player_kwargs=player1_kwargs)
        cmd2 = agent2.get_cmd(binary=Args.binary, binary_kwargs={'--remote-port': port}, player_kwargs=player2_kwargs)
        timed_print(f'cmd1: {cmd1}')
        proc1 = subprocess_util.Popen(cmd1)
        timed_print(f'cmd2: {cmd2}')
        subprocess_util.Popen(cmd2)

        stdout, stderr = proc1.communicate()
        if proc1.returncode:
            raise RuntimeError(f'proc1 exited with code {proc1.returncode}')
        record = extract_match_record(stdout)

        counts1 = record.get(0)
        counts2 = record.get(1)
        timed_print(f'Agent 1: {counts1}')

        i = agent1.index
        j = agent2.index
        self.real_wins[i, j] += counts1.win + 0.5 * counts1.draw
        self.real_wins[j, i] += counts2.win + 0.5 * counts2.draw

        match_tuple = (agent1.row_id, agent2.row_id, counts1.win, counts1.draw, counts2.win)
        c = self.conn.cursor()
        c.execute('INSERT INTO matches VALUES (?, ?, ?, ?, ?)', match_tuple)
        self.conn.commit()

    def update_ratings(self):
        prev_beta = self.beta
        w = self.real_wins + self.virtual_wins
        self.beta = compute_ratings(w)
        beta_delta = self.beta - prev_beta
        beta_delta_indices = list(sorted(range(len(self.beta)), key=lambda i: -np.abs(beta_delta[i])))

        timed_print('Updated ratings (top-3 beta changes: %.3f, %.3f, %.3f, max-beta:%.3f)' %
                    (beta_delta[beta_delta_indices[0]], beta_delta[beta_delta_indices[1]],
                     beta_delta[beta_delta_indices[2]], np.max(self.beta)))

    def commit_ratings(self):
        c = self.conn.cursor()

        match_id = c.execute('SELECT MAX(rowid) FROM matches').fetchone()[0]
        insert_tuples = []
        for agent in self.agents:
            agent_id = agent.row_id
            beta = self.beta[agent.index]
            insert_tuples.append((agent_id, match_id, beta))

        c.executemany('INSERT INTO ratings VALUES (?, ?, ?)', insert_tuples)
        self.conn.commit()
        timed_print(f'Committed ratings')


def main():
    load_args()
    arena = Arena()
    arena.launch()


if __name__ == '__main__':
    main()
