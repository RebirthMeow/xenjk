"""Launch helper shared by the native runners: start a child process without Windows crash dialogs.

This module was the solo strafe-movement probe runner. Its engine side (the bot_movebench probe and the strafe
engine) was removed and its analysis modules with it; the helper below is
what the live runners (movement_bench, movement_explore, route_approve, play_vs_runner, movement_cost_runner,
human_route_record) import from it. The run path and its parity checks are in Git history.
"""
import os
import subprocess


def spawn_without_crash_dialogs(command, **kwargs):
    """Scope Windows error-mode suppression to our child, restoring the parent.

    Child processes inherit the process error mode. Do not use
    CREATE_DEFAULT_ERROR_MODE here, which would defeat that inheritance.
    https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode
    """
    if os.name != 'nt':
        return subprocess.Popen(command, **kwargs)
    import ctypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetErrorMode.restype = ctypes.c_uint
    kernel.SetErrorMode.argtypes = [ctypes.c_uint]
    kernel.SetErrorMode.restype = ctypes.c_uint
    previous = kernel.GetErrorMode()
    kernel.SetErrorMode(previous | 0x8003)  # critical errors, WER, missing files
    try:
        return subprocess.Popen(command, **kwargs)
    finally:
        kernel.SetErrorMode(previous)


if __name__ == '__main__':
    raise SystemExit('strafe_fixture_runner: its probe was removed; use movement_bench.py')
