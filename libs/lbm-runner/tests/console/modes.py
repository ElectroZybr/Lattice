"""Regression checks: python tests/console/modes.py <probe> <lbm> (POSIX)."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios

probe, lbm = (str(Path(arg).resolve()) for arg in sys.argv[1:])

def invoke(args, cwd, tty=False, columns=100):
    if not tty:
        p = subprocess.run(args, cwd=cwd, capture_output=True, timeout=10)
        return p.returncode, p.stdout + p.stderr
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, columns, 0, 0))
    p = subprocess.Popen(args, cwd=cwd, stdin=subprocess.DEVNULL, stdout=slave, stderr=slave,
                         env={**os.environ, 'TERM': 'xterm'})
    os.close(slave)
    chunks = []
    try:
        while True:
            if not select.select([master], [], [], 10)[0]:
                p.kill()
                raise TimeoutError('benchmark console did not finish')
            try:
                data = os.read(master, 65536)
                if not data:
                    break
                chunks.append(data)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                break
    finally:
        os.close(master)
    return p.wait(timeout=10), b''.join(chunks)

with tempfile.TemporaryDirectory(prefix='lbm-console-') as folder:
    root = Path(folder)
    (root / 'lbm.toml').write_text(f'[suites]\nProbe = "{probe}"\n')
    for tty in (False, True):
        reports = []
        for index, flags in enumerate(([], ['-v'], ['-vv'], ['-v', '-v'], ['--verbose'])):
            report = root / f'{tty}-{index}.tsv'
            status, text = invoke([probe, 'ConsoleProbe/Pass*', '--tsv', str(report), *flags], folder, tty)
            assert status == 0, text
            assert b'probe warning' in text, text
            assert (b'probe diagnostic' in text) == bool(flags), text
            if index < 2:
                assert (b'\x1b[2K' in text) == tty, text
                assert b'2/2' in text, text
                plain = re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', text).decode()
                assert 'samples 6/6' in plain, plain
                if tty:
                    assert b'100%' in text, text
                    assert b'1/2' in text, text
                    assert b'100%' in text.rsplit(b'\x1b[2K', 1)[-1], text
                    for frame in '⠙⠹⠸⠼⠴⠦':
                        assert frame in plain, plain
                    assert '✓' in plain and '█' in plain, plain
                    assert b'\x1b[38;' in text.rsplit(b'\x1b[2K', 1)[-1], text
            raw = report.read_bytes()
            assert b'\x1b' not in raw
            # Deterministic probe metrics: presentation must not change exported results.
            reports.append([line for line in raw.splitlines() if not line.startswith(b'#')])
        assert all(report == reports[0] for report in reports), reports
        status, text = invoke([probe, 'WarmupCount'], folder, tty)
        plain = re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', text).decode()
        assert status == 0 and 'samples 5/5' in plain, plain
        status, text = invoke([probe, 'Fail'], folder, tty)
        assert status == 1 and b'probe failure' in text and b'100%' not in text, text
        status, text = invoke([probe, 'missing'], folder, tty)
        assert status == 1 and b'no benchmark matches' in text and b'100%' not in text, text
        for flags in ([], ['-v'], ['-vv']):
            status, text = invoke([lbm, 'run', 'Probe', 'ConsoleProbe/Pass*', '--no-build', '--no-save', *flags], folder, tty)
            assert status == 0, text
            assert (b'probe diagnostic' in text) == bool(flags), text
    status, text = invoke([probe, 'ConsoleProbe/Pass*'], folder, True, columns=45)
    assert status == 0 and b'100%' in text, text
print('Console modes: PTY + pipes, warnings, errors, TSV, narrow terminal, lbm forwarding passed')
