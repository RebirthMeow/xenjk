"""Rcon status reader shared by the movement tools.

This module once staged the pre-rewrite runner's cost workload (supported
hops, route catalog, flight artifacts). That runner and the C code it drove
were removed; tools/movement_cost_runner.py measures the
rewrite. Only the status reader the current tools import remains.
"""
import socket


def status_response(port,password):
    """Rcon status can span several 1 KB UDP print packets with sixteen bots."""
    parts=[]
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.settimeout(.5)
        sock.sendto(b'\xff\xff\xff\xff'+f'rcon {password} status'.encode(),('127.0.0.1',port))
        while True:
            try:data,_=sock.recvfrom(8192)
            except (socket.timeout,ConnectionResetError,ConnectionRefusedError):break
            if data.startswith(b'\xff\xff\xff\xffprint\n'):data=data[10:]
            parts.append(data.decode('utf-8',errors='replace'));sock.settimeout(.1)
    return ''.join(parts)
