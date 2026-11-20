#!/usr/bin/env python3
"""Native-server helpers the runners share: rcon over UDP, server shutdown and a run's result class.

This module was the classic Bot2 CTF capture smoke test (every installed CTF map, bot_forcebot2, a capture within a
timeout). The classic Bot2 was removed and the test with it; it also launched an
internet-mode server. The helpers below are what defense_laps, play_vs_runner, movement_bench, route_approve and the
other runners import. The test is in Git history.
"""
import os
import re
import socket
import subprocess
import time
from typing import Optional


def rcon_send(port: int, password: str, cmd: str,
              timeout: float = 2.0, collect: float = 0.0) -> Optional[str]:
    """Send a single Q3-style rcon UDP command to 127.0.0.1:port.

    Quake protocol:
        send: \\xff\\xff\\xff\\xff rcon <password> <cmd>
        recv: \\xff\\xff\\xff\\xff print\\n<response>

    The server flushes its redirect buffer in roughly one-kilobyte packets, so
    a long reply arrives as several datagrams. With ``collect`` > 0, keep
    reading until that many seconds pass without a further packet and return
    every packet's text joined in arrival order; the default returns only the
    first packet, preserving the historical behavior of short status polls.

    Returns the response text without the OOB header, or None on timeout.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    def strip(data: bytes) -> str:
        # Strip the four-byte OOB header and the leading "print\n" if present.
        if data.startswith(b'\xff\xff\xff\xff'):
            data = data[4:]
        if data.startswith(b'print\n'):
            data = data[len('print\n'):]
        return data.decode('utf-8', errors='replace')

    try:
        payload = b'\xff\xff\xff\xff' + f'rcon {password} {cmd}'.encode('utf-8')
        sock.sendto(payload, ('127.0.0.1', port))
        try:
            data, _ = sock.recvfrom(8192)
        except (socket.timeout, ConnectionResetError, ConnectionRefusedError):
            # Windows reports ICMP port-unreachable as a UDP connection reset
            # while a newly launched localhost server has not bound its port.
            return None
        parts = [strip(data)]
        if collect > 0:
            sock.settimeout(collect)
            while True:
                try:
                    data, _ = sock.recvfrom(8192)
                except (socket.timeout, ConnectionResetError, ConnectionRefusedError):
                    break
                parts.append(strip(data))
        return ''.join(parts)
    finally:
        sock.close()


def kill_server(proc: subprocess.Popen) -> None:
    """Terminate gracefully; hard-kill if it doesn't respect terminate."""
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            pass


def classify_result(cap_at, crashed_line, timed_out, natural_exit_code) -> str:
    """Do not mistake an unlogged process failure for a normal server exit.

    natural_exit_code is sampled BEFORE runner cleanup; terminate/kill exit
    codes must never turn an intentional timeout into a crash report.
    """
    if crashed_line:
        return 'CRASH'
    if natural_exit_code not in (None, 0):
        return 'ERROR_EXIT'
    if cap_at is not None:
        return 'CAP'
    if timed_out:
        return 'TIMEOUT'
    return 'EXIT'
