from alphazero.logic.build_params import BuildParams
from alphazero.logic.custom_types import ClientRole
from alphazero.logic.ratings import extract_match_record
from alphazero.logic.shutdown_manager import ShutdownManager
from alphazero.servers.gaming.base_params import BaseParams
from alphazero.servers.gaming.log_forwarder import LogForwarder
from alphazero.servers.gaming.session_data import SessionData
from util.logging_util import LoggingParams, get_logger
from util.socket_util import JsonDict, SocketRecvException, SocketSendException
from util.str_util import make_args_str
from util import subprocess_util

from dataclasses import dataclass, fields
import logging
import threading


logger = get_logger()


@dataclass
class RatingsServerParams(BaseParams):
    rating_tag: str = ''

    @staticmethod
    def create(args) -> 'RatingsServerParams':
        kwargs = {f.name: getattr(args, f.name) for f in fields(RatingsServerParams)}
        return RatingsServerParams(**kwargs)

    @staticmethod
    def add_args(parser, omit_base=False):
        defaults = RatingsServerParams()

        group = parser.add_argument_group(f'RatingsServer options')
        if not omit_base:
            BaseParams.add_base_args(group)

        group.add_argument('-r', '--rating-tag', default=defaults.rating_tag,
                           help='ratings tag. Loop controller collates ratings by this str. It is '
                           'the responsibility of the user to make sure that the same '
                           'binary/params are used across different RatingsServer processes '
                           'sharing the same rating-tag. (default: "%(default)s")')


class RatingsServer:
    def __init__(self, params: RatingsServerParams, logging_params: LoggingParams,
                 build_params: BuildParams):
        self._params = params
        self._build_params = build_params
        self._session_data = SessionData(params)
        self._shutdown_manager = ShutdownManager()
        self._log_forwarder = LogForwarder(self._shutdown_manager, logging_params)
        self._running = False

    def run(self):
        try:
            threading.Thread(target=self._main_loop, name='main_loop', daemon=True).start()
            self._shutdown_manager.wait_for_shutdown_request()
        except KeyboardInterrupt:
            logger.info('Caught Ctrl-C')
        finally:
            self._shutdown_manager.shutdown()

    def _main_loop(self):
        try:
            self._init_socket()
            self._send_handshake()
            self._recv_handshake()

            threading.Thread(target=self._recv_loop, daemon=True).start()
        except:
            logger.error('Unexpected error in main_loop():', exc_info=True)
            self._shutdown_manager.request_shutdown(1)

    def _init_socket(self):
        self._session_data.init_socket()
        self._log_forwarder.set_socket(self._session_data.socket)
        self._shutdown_manager.register(lambda: self._session_data.socket.close())

    def _send_handshake(self):
        aux = { 'tag': self._params.rating_tag, }
        self._session_data.send_handshake(ClientRole.RATINGS_SERVER, aux=aux)

    def _recv_handshake(self):
        self._session_data.recv_handshake(ClientRole.RATINGS_SERVER, self._log_forwarder)

    def _recv_loop(self):
        try:
            self._send_ready()
            while True:
                msg = self._session_data.socket.recv_json()
                if self._handle_msg(msg):
                    break
        except SocketRecvException:
            logger.warn('Encountered SocketRecvException in recv_loop(). '
                        'Loop controller likely shut down.')
            self._shutdown_manager.request_shutdown(0)
        except SocketSendException:
            # Include exc_info in send-case because it's a bit more unexpected
            logger.warn('Encountered SocketSendException in recv_loop(). '
                        'Loop controller likely shut down.', exc_info=True)
            self._shutdown_manager.request_shutdown(0)
        except:
            logger.error(f'Unexpected error in recv_loop():', exc_info=True)
            self._shutdown_manager.request_shutdown(1)

    def _send_ready(self):
        data = { 'type': 'ready', }
        self._session_data.socket.send_json(data)

    def _handle_msg(self, msg: JsonDict) -> bool:
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(f'ratings-server received json message: {msg}')

        msg_type = msg['type']
        if msg_type == 'match-request':
            self._handle_match_request(msg)
        elif msg_type == 'quit':
            self._quit()
            return True
        else:
            raise Exception(f'Unknown message type: {msg_type}')
        return False

    def _handle_match_request(self, msg: JsonDict):
        thread = threading.Thread(target=self._run_match, args=(msg,), daemon=True,
                                  name=f'run-match')
        thread.start()

    def _quit(self):
        logger.info(f'Received quit command')
        self._shutdown_manager.request_shutdown(0)

    def _run_match(self, msg: JsonDict):
        try:
            self._run_match_helper(msg)
        except:
            logger.error(f'Unexpected error in run-match:', exc_info=True)
            self._shutdown_manager.request_shutdown(1)

    def _run_match_helper(self, msg: JsonDict):
        assert not self._running
        self._running = True

        mcts_gen = msg['mcts_gen']
        ref_strength = msg['ref_strength']
        n_games = msg['n_games']

        ps1 = self._get_mcts_player_str(mcts_gen)
        ps2 = self._get_reference_player_str(ref_strength)
        binary = self._build_params.get_binary_path(self._session_data.game)

        args = {
            '-G': n_games,
            '--loop-controller-hostname': self._params.loop_controller_host,
            '--loop-controller-port': self._params.loop_controller_port,
            '--client-role': ClientRole.RATINGS_WORKER.value,
            '--manager-id': self._session_data.client_id,
            '--ratings-tag': f'"{self._params.rating_tag}"',
            '--cuda-device': self._params.cuda_device,
            '--weights-request-generation': mcts_gen,
            '--do-not-report-metrics': None,
        }
        args.update(self._session_data.game_spec.rating_options)
        cmd = [
            binary,
            '--player', f'"{ps1}"',
            '--player', f'"{ps2}"',
            ]
        cmd.append(make_args_str(args))
        cmd = ' '.join(map(str, cmd))

        mcts_name = RatingsServer._get_mcts_player_name(mcts_gen)
        ref_name = RatingsServer._get_reference_player_name(ref_strength)

        proc = subprocess_util.Popen(cmd)
        logger.info(f'Running {mcts_name} vs {ref_name} match [{proc.pid}]: {cmd}')
        stdout_buffer = []
        self._log_forwarder.forward_output(
            'ratings-worker', proc, stdout_buffer, close_remote_log=False)

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

        self._session_data.socket.send_json(data)
        self._send_ready()

    @staticmethod
    def _get_mcts_player_name(gen: int):
        return f'MCTS-{gen}'

    def _get_mcts_player_str(self, gen: int):
        name = RatingsServer._get_mcts_player_name(gen)

        player_args = {
            '--type': 'MCTS-C',
            '--name': name,
            '--cuda-device': self._params.cuda_device,
        }
        player_args.update(self._session_data.game_spec.rating_player_options)
        return make_args_str(player_args)

    @staticmethod
    def _get_reference_player_name(strength: int):
        return f'ref-{strength}'

    def _get_reference_player_str(self, strength: int):
        name = RatingsServer._get_reference_player_name(strength)
        family = self._session_data.game_spec.reference_player_family
        type_str = family.type_str
        strength_param = family.strength_param

        player_args = {
            '--type': type_str,
            '--name': name,
            strength_param: strength,
        }
        return make_args_str(player_args)
