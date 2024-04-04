from alphazero.logic.custom_types import ClientRole
from alphazero.logic.ratings import extract_match_record
from alphazero.servers.gaming.game_server_base import GameServerBase, GameServerBaseParams
from util.logging_util import LoggingParams, get_logger
from util.socket_util import JsonDict
from util import subprocess_util

from dataclasses import dataclass
import logging


logger = get_logger()


@dataclass
class RatingsServerParams(GameServerBaseParams):
    n_search_threads: int = 4
    parallelism_factor: int = 100

    @staticmethod
    def add_args(parser, omit_base=False):
        defaults = RatingsServerParams()

        group = parser.add_argument_group(f'RatingsServer options')
        if not omit_base:
            GameServerBaseParams.add_args_helper(group)

        group.add_argument('-n', '--n-search-threads', type=int, default=defaults.n_search_threads,
                           help='num search threads per game (default: %(default)s)')
        group.add_argument('-p', '--parallelism-factor', type=int,
                           default=defaults.parallelism_factor,
                           help='parallelism factor (default: %(default)s)')


class RatingsServer(GameServerBase):
    def __init__(self, params: RatingsServerParams, logging_params: LoggingParams):
        super().__init__(params, logging_params, ClientRole.RATINGS_SERVER)
        self.params = params
        self._running = False

    def send_ready(self):
        data = {
            'type': 'ready',
            }
        self.loop_controller_socket.send_json(data)

    def recv_loop_prelude(self):
        self.send_ready()

    def handle_msg(self, msg: JsonDict) -> bool:
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(f'ratings-server received json message: {msg}')

        msg_type = msg['type']
        if msg_type == 'match-request':
            self.run_func_in_new_thread(self.handle_match_request, args=(msg,))
        elif msg_type == 'quit':
            self.quit()
            return True
        else:
            raise Exception(f'Unknown message type: {msg_type}')
        return False

    def handle_match_request(self, msg: JsonDict):
        assert not self._running
        self._running = True

        mcts_gen = msg['mcts_gen']
        ref_strength = msg['ref_strength']
        n_games = msg['n_games']
        n_mcts_iters = msg['n_mcts_iters']

        n_search_threads = self.params.n_search_threads
        parallelism_factor = self.params.parallelism_factor

        ps1 = self.get_mcts_player_str(mcts_gen, n_mcts_iters, n_search_threads)
        ps2 = self.get_reference_player_str(ref_strength)
        binary = self.binary_path
        cmd = [
            binary,
            '-G', n_games,
            '--loop-controller-hostname', self.loop_controller_host,
            '--loop-controller-port', self.loop_controller_port,
            '--client-role', ClientRole.RATINGS_WORKER.value,
            '--cuda-device', self.cuda_device,
            '--weights-request-generation', mcts_gen,
            '--do-not-report-metrics',
            '-p', parallelism_factor,
            '--player', f'"{ps1}"',
            '--player', f'"{ps2}"',
            ]
        cmd = ' '.join(map(str, cmd))

        mcts_name = RatingsServer.get_mcts_player_name(mcts_gen)
        ref_name = RatingsServer.get_reference_player_name(ref_strength)

        proc = subprocess_util.Popen(cmd)
        logger.info(f'Running {mcts_name} vs {ref_name} match [{proc.pid}]: {cmd}')
        stdout_buffer = []
        self.forward_output('ratings-worker', proc, stdout_buffer, close_remote_log=False)

        # NOTE: extracting the match record from stdout is potentially fragile. Consider
        # changing this to have the c++ process directly communicate its win/loss data to the
        # loop-controller. Doing so would better match how the self-play server works.
        record = extract_match_record(stdout_buffer)
        logger.info(f'Match result: {record.get(0)}')

        self._running = False

        data = {
            'type': 'match-result',
            'record': record.get(0).to_json(),
            'mcts_gen': mcts_gen,
            'ref_strength': ref_strength,
        }

        self.loop_controller_socket.send_json(data)
        self.send_ready()

    @staticmethod
    def get_mcts_player_name(gen: int):
        return f'MCTS-{gen}'

    def get_mcts_player_str(self, gen: int, n_mcts_iters: int, n_search_threads: int):
        name = RatingsServer.get_mcts_player_name(gen)

        player_args = [
            '--type=MCTS-C',
            '--name', name,
            '-i', n_mcts_iters,
            '-n', n_search_threads,
            '--cuda-device', self.cuda_device,
        ]

        return ' '.join(map(str, player_args))

    @staticmethod
    def get_reference_player_name(strength: int):
        return f'ref-{strength}'

    def get_reference_player_str(self, strength: int):
        name = RatingsServer.get_reference_player_name(strength)
        family = self.game_spec.reference_player_family
        type_str = family.type_str
        strength_param = family.strength_param

        player_args = [
            '--type', type_str,
            '--name', name,
            strength_param, strength,
        ]

        return ' '.join(map(str, player_args))
