import unittest
from unittest.mock import patch,Mock

from bot_test_runner import classify_result,rcon_send


class ResultTests(unittest.TestCase):
    def test_unbound_localhost_udp_port_is_retryable_and_socket_closes(self):
        for error in (ConnectionResetError(10054,'not bound'),ConnectionRefusedError()):
            sock=Mock();sock.recvfrom.side_effect=error
            with patch('bot_test_runner.socket.socket',return_value=sock):
                self.assertIsNone(rcon_send(29999,'fixture','status'))
            sock.close.assert_called_once()

    def test_access_violation_without_log_marker(self):
        self.assertEqual(classify_result(None, None, False, 0xC0000005), 'ERROR_EXIT')

    def test_runner_timeout_is_not_process_failure(self):
        self.assertEqual(classify_result(None, None, True, None), 'TIMEOUT')

    def test_capture_and_clean_exit(self):
        self.assertEqual(classify_result(1.0, None, False, None), 'CAP')
        self.assertEqual(classify_result(None, None, False, 0), 'EXIT')

    def test_failure_not_hidden_by_capture(self):
        self.assertEqual(classify_result(1.0, None, False, 1), 'ERROR_EXIT')
        self.assertEqual(classify_result(1.0, 'FATAL', False, None), 'CRASH')


if __name__ == '__main__':
    unittest.main()
