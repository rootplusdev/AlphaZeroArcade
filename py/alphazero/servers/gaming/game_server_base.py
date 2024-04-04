from alphazero.logic.custom_types import ClientRole
from alphazero.logic.shutdown_manager import ShutdownManager
from alphazero.servers.gaming.base_params import BaseParams
from games.game_spec import GameSpec
from games.index import get_game_spec
from util.logging_util import LoggingParams, QueueStream, configure_logger, get_logger
from util.repo_util import Repo
from util.socket_util import JsonDict, Socket, SocketRecvException, SocketSendException

import abc
import os
import queue
import socket
import subprocess
import sys
import threading
import time
from typing import Optional


logger = get_logger()


class GameServerBase:
    """
    Common base class for SelfPlayServer and RatingsServer. Contains shared logic for
    interacting with the LoopController and for running games.
    """

    def __init__(self, params: BaseParams, logging_params: LoggingParams,
                 client_type: ClientRole):
        self._game = None
        self._game_spec = None
        self.logging_params = logging_params
        self.params = params
        self.client_type = client_type

        self.shutdown_manager = ShutdownManager()
        self.logging_queue = QueueStream()
        self.loop_controller_socket: Optional[Socket] = None
        self.client_id = None

    @property
    def game(self) -> str:
        if self._game is None:
            raise ValueError('game not set')
        return self._game

    @property
    def game_spec(self) -> GameSpec:
        if self._game_spec is None:
            self._game_spec = get_game_spec(self.game)
        return self._game_spec

    @property
    def loop_controller_host(self):
        return self.params.loop_controller_host

    @property
    def loop_controller_port(self):
        return self.params.loop_controller_port

    @property
    def cuda_device(self):
        return self.params.cuda_device

    @property
    def binary_path(self):
        if self.params.binary_path:
            return self.params.binary_path
        return os.path.join(Repo.root(), 'target/Release/bin', self.game_spec.name)

    def init_socket(self):
        loop_controller_address = (self.loop_controller_host, self.loop_controller_port)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(loop_controller_address)

        self.loop_controller_socket = Socket(sock)
        self.shutdown_manager.register(lambda: self.loop_controller_socket.close())

    def send_handshake(self):
        data = {
            'type': 'handshake',
            'role': self.client_type.value,
            'start_timestamp': time.time_ns(),
            'cuda_device': self.cuda_device,
        }

        self.loop_controller_socket.send_json(data)

    def log_loop(self, q: queue.Queue, src: Optional[str]=None):
        try:
            while True:
                line = q.get()
                if line is None:
                    break

                data = {
                    'type': 'log',
                    'line': line,
                    }
                if src is not None:
                    data['src'] = src
                self.loop_controller_socket.send_json(data)
        except SocketSendException:
            logger.warn('Loop controller appears to have disconnected, shutting down...')
            self.shutdown_manager.request_shutdown(0)
        except:
            logger.error(f'Unexpected error in log_loop(src={src}):', exc_info=True)
            self.shutdown_manager.request_shutdown(1)

    def forward_output(self, src: str, proc: subprocess.Popen, stdout_buffer=None,
                       close_remote_log=True):
        """
        Accepts a subprocess.Popen object and forwards its stdout and stderr to the loop controller
        for remote logging. Assumes that the proc was constructed with stdout=subprocess.PIPE and
        stderr=subprocess.PIPE.

        If stdout_buffer is provided, captures the stdout lines in the buffer.

        Note that the relative ordering of stdout and stderr lines is not guaranteed when
        forwarding. This should not be a big deal, since typically proc itself has non-deterministic
        ordering of stdout vs stderr lines.

        Waits for the process to return. Checks the error code and logs the stderr if the process
        returns a non-zero error code.
        """
        proc_log_queue = queue.Queue()
        stderr_buffer = []

        stdout_thread = threading.Thread(target=self._forward_output_thread, daemon=True,
                                         args=(src, proc.stdout, proc_log_queue, stdout_buffer),
                                         name=f'{src}-forward-stdout')
        stderr_thread = threading.Thread(target=self._forward_output_thread, daemon=True,
                                         args=(src, proc.stderr, proc_log_queue, stderr_buffer),
                                         name=f'{src}-forward-stderr')
        forward_thread = threading.Thread(target=self.log_loop, daemon=True,
                                          args=(proc_log_queue, src),
                                          name=f'{src}-log-loop')

        stdout_thread.start()
        stderr_thread.start()
        forward_thread.start()

        stdout_thread.join()
        stderr_thread.join()

        proc_log_queue.put(None)
        forward_thread.join()
        proc.wait()

        try:
            data = {
                'type': 'worker-exit',
                'src': src,
                'close_log': close_remote_log,
            }
            self.loop_controller_socket.send_json(data)
        except SocketSendException:
            pass

        if proc.returncode:
            logger.error(f'Process failed with return code {proc.returncode}')
            for line in stderr_buffer:
                logger.error(line.strip())
            raise Exception()

    def _forward_output_thread(self, src: str, stream, q: queue.Queue, buf=None):
        try:
            for line in stream:
                if line is None:
                    break
                q.put(line)
                if buf is not None:
                    buf.append(line)
        except:
            logger.error(f'Unexpected error in _forward_output_thread({src}):', exc_info=True)
            self.shutdown_manager.request_shutdown(1)

    def recv_handshake(self):
        data = self.loop_controller_socket.recv_json(timeout=1)
        assert data['type'] == 'handshake-ack', data

        rejection = data.get('rejection', None)
        if rejection is not None:
            raise Exception(f'Handshake rejected: {rejection}')

        self.client_id = data['client_id']
        self._game = data['game']

        configure_logger(params=self.logging_params, queue_stream=self.logging_queue)
        threading.Thread(target=self.log_loop, daemon=True, args=(self.logging_queue.log_queue,),
                         name='log-loop').start()

        logger.info(f'**** Starting {self.client_type.value} ****')
        logger.info(f'Received client id assignment: {self.client_id}')

    def recv_loop(self):
        try:
            self.recv_loop_prelude()
            while True:
                msg = self.loop_controller_socket.recv_json()
                if self.handle_msg(msg):
                    break
        except SocketRecvException:
            logger.warn('Encountered SocketRecvException in recv_loop(). '
                        'Loop controller likely shut down.')
            self.shutdown_manager.request_shutdown(0)
        except SocketSendException:
            # Include exc_info in send-case because it's a bit more unexpected
            logger.warn('Encountered SocketSendException in recv_loop(). '
                        'Loop controller likely shut down.', exc_info=True)
            self.shutdown_manager.request_shutdown(0)
        except:
            logger.error(f'Unexpected error in recv_loop():', exc_info=True)
            self.shutdown_manager.request_shutdown(1)

    @abc.abstractmethod
    def handle_msg(self, msg: JsonDict) -> bool:
        """
        Handle the message, return True if should break the loop.

        Must override in subclass.
        """
        pass

    def recv_loop_prelude(self):
        """
        Override to do any work after the handshake is complete but before the recv-loop
        starts.
        """
        pass

    def quit(self):
        logger.info(f'Received quit command')
        self.shutdown_manager.request_shutdown(0)
