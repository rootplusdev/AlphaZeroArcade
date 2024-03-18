from alphazero.servers.loop_control.subcontrols.aux_subcontroller import AuxSubcontroller
from alphazero.logic.common_params import CommonParams
from alphazero.logic.custom_types import ClientType
from alphazero.logic.training_params import TrainingParams
from alphazero.servers.loop_control.loop_control_data import LoopControlData, LoopControllerParams
from alphazero.servers.loop_control.subcontrols.ratings_subcontroller import RatingsSubcontroller
from alphazero.servers.loop_control.subcontrols.self_play_subcontroller import SelfPlaySubcontroller
from alphazero.servers.loop_control.subcontrols.training_subcontroller import TrainingSubcontroller
from util.logging_util import get_logger


import threading
import time


logger = get_logger()


class LoopController:
    """
    The LoopController is a server that acts as the "brains" of the AlphaZero system. It performs
    the neural network training, and also coordinates the activity of external self-play and rating
    servers.

    The LoopController manages various subcontrollers, which do the actual work:

    - TrainingSubcontroller: manages training of the neural network
    - SelfPlaySubcontroller: manages self-play games
    - RatingsSubcontroller: manages rating games
    - AuxSubcontroller: shared by other subcontrollers for various tasks
    """
    def __init__(self, params: LoopControllerParams, training_params: TrainingParams,
                 common_params: CommonParams):
        self.data = LoopControlData(params, training_params, common_params)
        self.aux_subcontroller = AuxSubcontroller(self.data)
        self.training_subcontroller = TrainingSubcontroller(self.aux_subcontroller)
        self.self_play_subcontroller = SelfPlaySubcontroller(self.training_subcontroller)
        self.ratings_subcontroller = RatingsSubcontroller(self.aux_subcontroller)

        # Currently, there appears to be a rare situation where the controller deadlocks. The
        # below block of code makes it so that we can send a SIGUSR1 signal to the controller
        # process to dump a per-thread stack trace, which can be useful for debugging.
        #
        # Cmd to send signal:
        #
        # $ kill -SIGUSR1 <pid>
        #
        # Once we determine the cause of the deadlock, we can remove this block of code.
        import faulthandler
        import signal
        faulthandler.register(signal.SIGUSR1.value)
        faulthandler.enable()

    @property
    def params(self) -> LoopControllerParams:
        return self.data.params

    def _run_setup(self):
        logger.info('Performing LoopController setup...')
        self.aux_subcontroller.setup()
        self.training_subcontroller.setup()
        self.launch_accept_clients()

    def run(self):
        shutdown_code = 0
        try:
            threading.Thread(target=self.main_loop, name='main_loop', daemon=True).start()

            while True:
                if self.data.error_signaled():
                    logger.info('Child thread error detected, shutting down...')
                    shutdown_code = 1
                    break

                time.sleep(1)
        except KeyboardInterrupt:
            logger.info('Caught Ctrl-C')
        finally:
            self.data.shutdown(shutdown_code)

    def main_loop(self):
        try:
            self._run_setup()

            self.self_play_subcontroller.wait_for_gen0_completion()
            self.training_subcontroller.train_gen1_model_if_necessary()

            while self.data.active():
                self.training_subcontroller.wait_until_enough_training_data()
                self.training_subcontroller.train_step()
        except:
            logger.error('Unexpected error in main_loop():', exc_info=True)
            self.data.signal_error()

    def launch_accept_clients(self):
        logger.info(f'Listening for clients on port {self.params.port}...')
        threading.Thread(target=self.accept_clients, name='accept_clients', daemon=True).start()

    def accept_clients(self):
        """
        Loop that checks for new clients. For each new client, spawns a thread to handle it.
        """
        try:
            while True:
                client_data = self.aux_subcontroller.accept_client()
                client_type = client_data.client_type

                if client_type == ClientType.SELF_PLAY_MANAGER:
                    self.self_play_subcontroller.add_self_play_manager(client_data)
                elif client_type == ClientType.SELF_PLAY_WORKER:
                    self.self_play_subcontroller.add_self_play_worker(client_data)
                elif client_type == ClientType.RATINGS_MANAGER:
                    self.ratings_subcontroller.add_ratings_manager(client_data)
                elif client_type == ClientType.RATINGS_WORKER:
                    self.ratings_subcontroller.add_ratings_worker(client_data)
                else:
                    raise Exception(f'Unknown client type: {client_type}')
        except:
            logger.error('Exception in accept_clients():', exc_info=True)
            self.data.signal_error()
