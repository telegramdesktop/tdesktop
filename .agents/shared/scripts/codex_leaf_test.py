#!/usr/bin/env python3
"""Lifecycle tests for codex_leaf.py with a fake Codex; run with
`python3 -B .agents/shared/scripts/codex_leaf_test.py -v`.
"""

import collections
import contextlib
import csv
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import uuid

import codex_leaf

HELPER = Path(__file__).with_name('codex_leaf.py')
WINDOWS = codex_leaf.WINDOWS
GRACE = 3.0 if WINDOWS else 1.0
POLL = 0.05 if WINDOWS else 0.02

Result = collections.namedtuple('Result', 'code out err')

FAKE_CODEX = r'''import json
import os
import sys
import time

EXPIRY = 120


def hold_life():
    lives = os.environ.get('CODEX_LEAF_TEST_LIVES')
    if not lives:
        return None
    fd = os.open(os.path.join(lives, 'fake-%d' % os.getpid()),
        os.O_RDWR | os.O_CREAT, 0o644)
    if os.name == 'nt':
        import msvcrt
        msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
    else:
        import fcntl
        fcntl.flock(fd, fcntl.LOCK_EX)
    return fd


def emit(event):
    sys.stdout.write(json.dumps(event) + '\n')
    sys.stdout.flush()


argv = sys.argv[1:]
if argv == ['--version']:
    print('codex-cli fake')
    sys.exit(0)
if argv == ['login', 'status']:
    print('Logged in; home %s' % os.environ.get('CODEX_HOME'))
    sys.exit(0)
life = hold_life()
if argv[:1] != ['exec']:
    sys.exit(2)
directive = json.loads(sys.stdin.read() or '{}')
last = argv[argv.index('-o') + 1]
if argv[:2] == ['exec', 'resume']:
    thread = argv[-2]
else:
    thread = 'thread-%d-%d' % (os.getpid(), int(time.time() * 1000))
emit({'type': 'thread.started', 'thread_id': thread})
emit({'type': 'turn.started'})
if directive.get('marker'):
    temporary = directive['marker'] + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as f:
        json.dump({
            'pid': os.getpid(),
            'argv': sys.argv,
            'cwd': os.getcwd(),
            'thread': thread,
            'GIT_CONFIG_PARAMETERS': os.environ.get('GIT_CONFIG_PARAMETERS'),
            'CODEX_HOME': os.environ.get('CODEX_HOME'),
        }, f)
    os.replace(temporary, directive['marker'])
deadline = time.time() + EXPIRY
if directive.get('hold'):
    while not os.path.exists(directive['hold']) and time.time() < deadline:
        time.sleep(0.02)
if directive.get('forever'):
    while time.time() < deadline:
        time.sleep(0.05)
for event in directive.get('events', []):
    emit(event)
turn = directive.get('turn', 'completed')
if turn == 'completed':
    emit({'type': 'turn.completed', 'usage': {'output_tokens': 1}})
elif turn == 'failed':
    emit({'type': 'turn.failed', 'error': {
        'message': directive.get('message', '429 Too Many Requests')}})
if directive.get('partial'):
    sys.stdout.write(directive['partial'])
    sys.stdout.flush()
text = directive.get('last', 'fake final line 1\nfake final line 2\n')
if text is not None:
    with open(last, 'w', encoding='utf-8') as f:
        f.write(text)
if directive.get('stderr'):
    sys.stderr.write(directive['stderr'])
    sys.stderr.flush()
sys.exit(directive.get('exit', 0))
'''

HOOK = r'''import errno
import json
import os
import sys
import time

EXPIRY = 120
_original_replace = os.replace


def _patiently(action):
    deadline = time.time() + 2
    while True:
        try:
            return action()
        except (PermissionError, ValueError):
            if time.time() >= deadline:
                raise
            time.sleep(0.01)


def _rules():
    path = os.environ.get('CODEX_LEAF_TEST_HOOK')
    if not path:
        return []

    def load():
        with open(path, encoding='utf-8') as f:
            return json.load(f)

    try:
        return _patiently(load)
    except (OSError, ValueError):
        return []


def _store(rules):
    path = os.environ['CODEX_LEAF_TEST_HOOK']
    temporary = '%s.%d.tmp' % (path, os.getpid())
    with open(temporary, 'w', encoding='utf-8') as f:
        json.dump(rules, f)
    _patiently(lambda: _original_replace(temporary, path))


def _name():
    argv = sys.argv
    return argv[argv.index('--name') + 1] if '--name' in argv[:-1] else None


def _matching(point):
    return [rule for rule in _rules()
        if rule.get('name') == _name() and rule.get('pause') == point]


def _pause(gate, extra=''):
    temporary = gate + '.paused.tmp'
    with open(temporary, 'w', encoding='utf-8') as f:
        f.write(('%d %s' % (os.getpid(), extra)).strip())
    _original_replace(temporary, gate + '.paused')
    deadline = time.time() + EXPIRY
    while not os.path.exists(gate) and time.time() < deadline:
        time.sleep(0.02)


def _hold_life(kind):
    lives = os.environ.get('CODEX_LEAF_TEST_LIVES')
    if not lives:
        return None
    fd = os.open(os.path.join(lives, '%s-%d' % (kind, os.getpid())),
        os.O_RDWR | os.O_CREAT, 0o644)
    if os.name == 'nt':
        import msvcrt
        msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
    else:
        import fcntl
        fcntl.flock(fd, fcntl.LOCK_EX)
    return fd


def _note_denied(source):
    path = os.environ.get('CODEX_LEAF_TEST_DENIED')
    if not path:
        return
    try:
        with open(path, 'a', encoding='utf-8') as f:
            f.write('%d %s\n' % (os.getpid(), os.path.basename(str(source))))
    except OSError:
        pass


if os.path.basename(sys.argv[0] if sys.argv else '') == 'codex_leaf.py':
    import subprocess

    if sys.argv[1:2] == ['_run']:
        _life = _hold_life('runner')
        for _rule in _matching('launched'):
            _pause(_rule['gate'])
    elif sys.argv[1:2] == ['wait']:
        _life = _hold_life('wait')

    class _Popen(subprocess.Popen):
        def __init__(self, args, *rest, **options):
            super().__init__(args, *rest, **options)
            self._hook_codex = bool(isinstance(args, (list, tuple)) and args
                and args[0] == os.environ.get('CODEX_BIN'))
            self._hook_waited = False
            if self._hook_codex:
                for rule in _matching('spawned'):
                    _pause(rule['gate'], str(self.pid))

        def wait(self, timeout=None):
            if getattr(self, '_hook_codex', False) and not self._hook_waited:
                self._hook_waited = True
                for rule in _matching('recorded'):
                    _pause(rule['gate'], str(self.pid))
            return super().wait(timeout)

    subprocess.Popen = _Popen

    def _replace(source, target, *rest, **options):
        rules = _rules()
        for rule in rules:
            spec = rule.get('fail_replace')
            if (not spec or rule.get('name') != _name()
                    or rule.get('count', 0) <= 0
                    or not str(source).endswith(spec['suffix'])):
                continue
            if spec.get('contains'):
                try:
                    with open(source, encoding='utf-8', errors='replace') as f:
                        text = f.read()
                except OSError:
                    text = ''
                if spec['contains'] not in text:
                    continue
            rule['count'] -= 1
            _store(rules)
            raise PermissionError(
                errno.EACCES, 'injected sharing violation', source)
        try:
            return _original_replace(source, target, *rest, **options)
        except PermissionError:
            _note_denied(source)
            raise

    os.replace = _replace
'''

LOCK_HOLDER = r'''import os
import sys
import time

sys.path.insert(0, sys.argv[1])
import codex_leaf

with codex_leaf.leaf_lock(sys.argv[2], 'a'):
    print('locked', flush=True)
    deadline = time.time() + 120
    while not os.path.exists(sys.argv[3]) and time.time() < deadline:
        time.sleep(0.05)
'''

SLEEPER = r'''import os
import sys
import time

deadline = time.time() + 120
while not os.path.exists(sys.argv[1]) and time.time() < deadline:
    time.sleep(0.05)
'''

CALLER = r'''import ctypes
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, sys.argv[1])
import codex_leaf

flags, result, gate = int(sys.argv[2]), sys.argv[3], sys.argv[4]
deadline = time.time() + 120


def report(**fields):
    temporary = result + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as f:
        json.dump(fields, f)
    os.replace(temporary, result)


def runner_inside(job, path):
    state = {}
    while not state.get('runner_pid') and time.time() < deadline:
        time.sleep(0.05)
        try:
            with open(path, encoding='utf-8') as f:
                state = json.load(f)
        except (OSError, ValueError):
            pass
    process = library.OpenProcess(codex_leaf.PROCESS_QUERY_LIMITED_INFORMATION,
        False, state.get('runner_pid') or 0)
    if not process:
        return None
    try:
        inside = ctypes.c_long()
        if not library.IsProcessInJob(process, job, ctypes.byref(inside)):
            return None
        return bool(inside.value)
    finally:
        library.CloseHandle(process)


library = codex_leaf.kernel32()
job = library.CreateJobObjectW(None, None)
if (not job or not codex_leaf.set_job_limits(job, flags)
        or not library.AssignProcessToJobObject(
            job, library.GetCurrentProcess())):
    report(code=None, err='caller job: %s' % codex_leaf.windows_error())
    sys.exit(1)
start = subprocess.run([sys.executable, '-B'] + sys.argv[5:],
    capture_output=True, text=True, encoding='utf-8', errors='replace')
inside = None
if start.returncode == 0:
    inside = runner_inside(job, json.loads(start.stdout)['state'])
report(code=start.returncode, out=start.stdout, err=start.stderr,
    inside=inside)
while not os.path.exists(gate) and time.time() < deadline:
    time.sleep(0.02)
library.CloseHandle(job)
'''


def write(path, text, newline=None):
    with open(path, 'w', encoding='utf-8', newline=newline) as f:
        f.write(text)


def read_bytes(path):
    with open(path, 'rb') as f:
        return f.read()


def life_held(path):
    try:
        fd = os.open(path, os.O_RDWR)
    except FileNotFoundError:
        return False
    except OSError:
        return True
    try:
        if WINDOWS:
            import msvcrt
            try:
                msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
            except OSError:
                return True
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
            return False
        import fcntl
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            return True
        return False
    finally:
        os.close(fd)


def listed_pids(line):
    found = re.search(r' pid=([0-9,]+)', line)
    return [int(pid) for pid in found.group(1).split(',')] if found else []


def refused_pids(text):
    found = re.search(r'\bpid ([0-9]+(?:, [0-9]+)*)\)', text)
    return [int(pid) for pid in found.group(1).split(', ')] if found else []


def image_name(pid):
    listing = subprocess.run(
        ['tasklist', '/FI', 'PID eq %d' % pid, '/FO', 'CSV', '/NH'],
        capture_output=True, text=True, errors='replace', timeout=60)
    rows = [row for row in csv.reader(listing.stdout.splitlines()) if row]
    return rows[0][0] if rows else listing.stdout + listing.stderr


def breakaway_allowed():
    try:
        probe = subprocess.Popen([sys.executable, '-B', '-c', ''],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, creationflags=(
                subprocess.CREATE_BREAKAWAY_FROM_JOB
                | subprocess.CREATE_NO_WINDOW))
    except OSError:
        return False
    probe.wait(timeout=60)
    return True


class TempCase(unittest.TestCase):
    def setUp(self):
        self.root = os.path.realpath(
            tempfile.mkdtemp(prefix='codex-leaf-unit-'))
        self.addCleanup(shutil.rmtree, self.root, True)

    def snapshot(self, path):
        return read_bytes(path), os.stat(path).st_mtime_ns


class LeafCase(unittest.TestCase):
    def setUp(self):
        self.root = os.path.realpath(
            tempfile.mkdtemp(prefix='codex-leaf-test-'))
        self.addCleanup(self.remove_root)
        for folder in ('bin', 'cwd', 'gates', 'home', 'hooks', 'lives',
                'markers', 'prompts', 'runs'):
            os.mkdir(os.path.join(self.root, folder))
        self.home = os.path.join(self.root, 'home')
        self.work_home = os.path.join(self.home, '.codex-accounts', 'work')
        os.makedirs(self.work_home)
        self.run_dir = os.path.join(self.root, 'runs')
        self.cwd = os.path.join(self.root, 'cwd')
        self.fake = os.path.join(self.root, 'bin', 'fake_codex.py')
        write(self.fake, FAKE_CODEX)
        write(os.path.join(self.root, 'hooks', 'sitecustomize.py'), HOOK)
        self.hook_file = os.path.join(self.root, 'hook.json')
        self.hook()
        if WINDOWS:
            self.binary = os.path.join(self.root, 'bin', 'codex.cmd')
            write(self.binary, '@echo off\n"%s" "%s" %%*\n' % (
                sys.executable, self.fake), newline='\r\n')
        else:
            self.binary = os.path.join(self.root, 'bin', 'codex')
            write(self.binary, '#!/bin/sh\n"%s" "%s" "$@"\n' % (
                sys.executable, self.fake))
            os.chmod(self.binary, 0o755)
        self.env = {key: value for key, value in os.environ.items()
            if not key.startswith('CODEX_LEAF_')
            and key not in ('GIT_CONFIG_PARAMETERS', 'CODEX_HOME')}
        python_path = [os.path.join(self.root, 'hooks')]
        if self.env.get('PYTHONPATH'):
            python_path.append(self.env['PYTHONPATH'])
        self.env.update({
            'CODEX_BIN': self.binary,
            'HOME': self.home,
            'USERPROFILE': self.home,
            'CODEX_LEAF_START_GRACE': str(GRACE),
            'CODEX_LEAF_POLL': '0.05',
            'CODEX_LEAF_RETRY': '2',
            'CODEX_LEAF_TEST_HOOK': self.hook_file,
            'CODEX_LEAF_TEST_LIVES': os.path.join(self.root, 'lives'),
            'CODEX_LEAF_TEST_DENIED': os.path.join(self.root, 'denied.log'),
            'PYTHONPATH': os.pathsep.join(python_path),
            'PYTHONDONTWRITEBYTECODE': '1',
            'PYTHONIOENCODING': 'utf-8',
        })
        self.leaves, self.gates, self.count = [], [], 0
        self.addCleanup(self.end_leaves)

    def use_binary(self, path):
        self.binary = path
        self.env['CODEX_BIN'] = path

    def direct_binary(self):
        path = os.path.join(self.root, 'bin', 'codex-direct')
        write(path, '#!%s\n%s' % (sys.executable, FAKE_CODEX))
        os.chmod(path, 0o755)
        return path

    def end_leaves(self):
        self.hook()
        for name in self.leaves:
            if os.path.exists(self.files(name)['state']):
                self.stop(name)
        for gate in self.gates:
            self.release(gate)
        deadline = time.time() + 30
        while self.live():
            if time.time() >= deadline:
                self.fail('still running after the test: %s'
                    % ', '.join(self.live()))
            time.sleep(POLL)

    def remove_root(self):
        for _ in range(100):
            try:
                shutil.rmtree(self.root)
                return
            except FileNotFoundError:
                return
            except OSError:
                time.sleep(0.1)
        shutil.rmtree(self.root, ignore_errors=True)

    def until(self, predicate, timeout=20, what='the condition'):
        deadline = time.time() + timeout
        while True:
            value = predicate()
            if value:
                return value
            if time.time() >= deadline:
                self.fail('timed out after %gs waiting for %s' % (
                    timeout, what))
            time.sleep(POLL)

    def run_helper(self, *args, env=None, timeout=90):
        environment = dict(self.env)
        environment.update(env or {})
        process = subprocess.run(
            [sys.executable, '-B', str(HELPER)] + [str(arg) for arg in args],
            capture_output=True, text=True, encoding='utf-8',
            errors='replace', timeout=timeout, env=environment)
        return Result(process.returncode, process.stdout, process.stderr)

    def directive(self, **options):
        self.count += 1
        options.setdefault('marker', os.path.join(
            self.root, 'markers', 'm%d.json' % self.count))
        return options

    def prompt(self, directive):
        self.count += 1
        path = os.path.join(self.root, 'prompts', 'p%d.json' % self.count)
        write(path, json.dumps(directive))
        return path

    def start(self, name, directive, *flags, settle=True, env=None):
        prompt = self.prompt(directive)
        if name not in self.leaves:
            self.leaves.append(name)
        result = self.run_helper(
            'start', '--run-dir', self.run_dir, '--name', name,
            '--cwd', self.cwd, '--prompt-file', prompt, *flags, env=env)
        if settle and result.code == 0:
            self.until(lambda: self.status(name) not in (None, 'starting'),
                what='leaf %s to leave starting' % name)
        return result

    def wait(self, *names, timeout=30, env=None):
        args = ['wait', '--run-dir', self.run_dir, '--timeout', timeout]
        for name in names:
            args += ['--name', name]
        return self.run_helper(*args, env=env, timeout=timeout + 60)

    def stop(self, name):
        return self.run_helper(
            'stop', '--run-dir', self.run_dir, '--name', name)

    def line(self, result, name):
        for line in result.out.splitlines():
            if line.startswith(name + ': '):
                return line
        self.fail('no line for %s in %r' % (name, result))

    def files(self, name):
        base = os.path.join(self.run_dir, name)
        return {
            'state': base + '.json',
            'jsonl': base + '.jsonl',
            'stderr': base + '.stderr.txt',
            'last': base + '.last.md',
            'runner_log': base + '.runner.log',
            'lock': base + '.lock',
        }

    def state(self, name):
        try:
            with open(self.files(name)['state'], encoding='utf-8') as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def status(self, name):
        return (self.state(name) or {}).get('status')

    def read(self, path):
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                return f.read()
        except OSError:
            return ''

    def entries(self, name):
        return sorted(entry for entry in os.listdir(self.run_dir)
            if entry.startswith(name + '.'))

    def archived(self, name):
        return [entry for entry in self.entries(name) if '.prev-' in entry]

    def digest(self, name):
        result = {}
        for entry in self.entries(name):
            if entry.endswith('.lock') or entry.endswith('.tmp'):
                continue
            data = read_bytes(os.path.join(self.run_dir, entry))
            result[entry] = hashlib.sha256(data).hexdigest()
        return result

    def marker(self, directive):
        try:
            with open(directive['marker'], encoding='utf-8') as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def wait_marker(self, directive):
        return self.until(lambda: self.marker(directive),
            what='the fake to write %s' % directive['marker'])

    def markers(self):
        return len([entry for entry in os.listdir(
            os.path.join(self.root, 'markers')) if entry.endswith('.json')])

    def gate(self, label):
        path = os.path.join(self.root, 'gates', label)
        self.gates.append(path)
        return path

    def release(self, gate):
        write(gate, '')

    def paused(self, gate):
        self.until(lambda: os.path.exists(gate + '.paused'),
            what='a runner to pause at %s' % os.path.basename(gate))
        with open(gate + '.paused', encoding='utf-8') as f:
            return [int(value) for value in f.read().split()]

    def hook(self, *rules):
        temporary = self.hook_file + '.tmp'
        write(temporary, json.dumps(list(rules)))
        deadline = time.time() + 10
        while True:
            try:
                os.replace(temporary, self.hook_file)
                return
            except PermissionError:
                if time.time() >= deadline:
                    raise
                time.sleep(POLL)

    def live(self):
        folder = os.path.join(self.root, 'lives')
        return [entry for entry in sorted(os.listdir(folder))
            if life_held(os.path.join(folder, entry))]

    def runner_alive(self, pid):
        return life_held(os.path.join(self.root, 'lives', 'runner-%d' % pid))

    def fake_alive(self, directive):
        marker = self.marker(directive)
        return bool(marker) and life_held(os.path.join(
            self.root, 'lives', 'fake-%d' % marker['pid']))

    def kill_runner(self, pid):
        self.assertTrue(self.runner_alive(pid), 'no live runner %d' % pid)
        os.kill(pid, signal.SIGTERM if WINDOWS else signal.SIGKILL)
        self.until(lambda: not self.runner_alive(pid),
            what='runner %d to exit' % pid)

    def lose_runner(self, name, directive, point, *flags):
        gate = self.gate('%s-%s' % (name, point))
        self.hook({'name': name, 'pause': point, 'gate': gate})
        result = self.start(name, directive, *flags,
            settle=(point != 'launched'))
        self.assertEqual(result.code, 0, result)
        runner = self.paused(gate)[0]
        if point != 'launched':
            self.wait_marker(directive)
        self.kill_runner(runner)
        self.hook()
        self.release(gate)
        return runner

    def outlast_grace(self):
        time.sleep(GRACE + 0.2)

    def token(self, name):
        return codex_leaf.transcript_writer(self.files(name)['jsonl'])[0]

    def assert_done(self, name, timeout=30):
        result = self.wait(name, timeout=timeout)
        self.assertEqual(result.code, 0, result)
        self.assertTrue(self.line(result, name).startswith(name + ': done '),
            result)
        return result

    def assert_failed(self, name, reason, timeout=30):
        result = self.wait(name, timeout=timeout)
        self.assertEqual(result.code, 0, result)
        line = self.line(result, name)
        self.assertTrue(line.startswith(name + ': failed '), result)
        self.assertIn(' reason=' + reason, line)
        return result

    def assert_still_running(self, name, result):
        self.assertEqual(result.code, 2, result)
        line = self.line(result, name)
        self.assertTrue(line.startswith(name + ': running '), result)
        self.assertNotIn('failed', result.out)
        self.assertIn('WAITING: 1 leaf(s) still running; wait again',
            result.out)
        self.assertEqual(self.status(name), 'running')
        return line

    def assert_error(self, result, reason):
        self.assertEqual(result.code, 1, result)
        self.assertIn('codex_leaf: %s: ' % reason, result.err)
        self.assertNotIn('Traceback', result.err)
        self.assertEqual(result.out, '')
        return refused_pids(result.err)

    def finish(self, name, directive=None):
        result = self.start(name, directive or self.directive())
        self.assertEqual(result.code, 0, result)
        self.assert_done(name)
        runner = self.state(name)['runner_pid']
        self.until(lambda: not self.runner_alive(runner),
            what='the runner of %s to exit' % name)

    def load(self, path):
        try:
            with open(path, encoding='utf-8') as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def assert_archived_whole(self, name, old):
        archived = self.archived(name)
        suffixes = {entry[entry.index('.prev-'):] for entry in archived}
        self.assertEqual(len(suffixes), 1, archived)
        suffix = suffixes.pop()
        self.assertEqual(sorted(archived),
            sorted(entry + suffix for entry in old))
        after = self.digest(name)
        for entry, digest in old.items():
            self.assertEqual(after[entry + suffix], digest, entry)
        return suffix

    def runner_exited(self, name):
        log = self.files(name)['runner_log']
        found = self.until(
            lambda: re.search(r'runner \d+ exiting \((.*)\)', self.read(log)),
            what='the runner exit line of %s' % name)
        return found.group(1)

    def locked(self, name):
        lock = getattr(codex_leaf, 'leaf_lock', None)
        return lock(self.run_dir, name) if lock else contextlib.nullcontext()

    def force(self, name, **changes):
        with self.locked(name):
            state = codex_leaf.read_state(self.run_dir, name)
            state.update(changes)
            codex_leaf.write_state(self.run_dir, name, state)
        return state

    def denied(self):
        return self.read(os.path.join(self.root, 'denied.log')).splitlines()

    def end_process(self, process, gate=None):
        if gate:
            self.release(gate)
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        for stream in (process.stdout, process.stderr):
            if stream:
                stream.close()


class JudgeTest(TempCase):
    STARTED = {'type': 'thread.started', 'thread_id': 'thread-1'}
    TURN = {'type': 'turn.started'}
    COMPLETED = {'type': 'turn.completed', 'usage': {'output_tokens': 1}}
    LIMIT = 'exceeded retry limit, last status: 429 Too Many Requests'

    def setUp(self):
        super().setUp()
        self.files = codex_leaf.paths(self.root, 'a')
        self.state = {'started': time.time()}

    def failed(self, message=None):
        event = {'type': 'turn.failed'}
        if message:
            event['error'] = {'message': message}
        return event

    def error(self, message):
        return {'type': 'error', 'message': message}

    def judge(self, events, last='final\n', partial=None, exit_code=None):
        with open(self.files['jsonl'], 'wb') as f:
            for event in events:
                line = event if isinstance(event, str) else json.dumps(event)
                f.write((line + '\n').encode('utf-8'))
            if partial is not None:
                f.write(partial.encode('utf-8'))
        if last is None:
            if os.path.exists(self.files['last']):
                os.remove(self.files['last'])
        else:
            write(self.files['last'], last)
        return codex_leaf.judge(self.state, self.files, exit_code)

    def test_completed_turn_with_a_fresh_message_is_done(self):
        events = [self.STARTED, self.TURN, self.COMPLETED]
        self.assertEqual(self.judge(events), ('done', None))
        self.assertEqual(self.judge(events, exit_code=0), ('done', None))

    def test_transient_errors_before_a_completed_turn_are_done(self):
        events = [self.STARTED, self.TURN, self.error('Reconnecting... 1/5'),
            self.error('Reconnecting... 2/5'), self.COMPLETED]
        self.assertEqual(self.judge(events, exit_code=0), ('done', None))

    def test_partial_trailing_line_is_ignored(self):
        events = [self.STARTED, self.TURN, self.COMPLETED]
        self.assertEqual(self.judge(events, partial='{"type": "turn.failed"'),
            ('done', None))
        self.assertEqual(
            self.judge(events[:2], partial=json.dumps(self.COMPLETED)),
            ('failed', 'transcript has no completed turn'))

    def test_unparseable_lines_are_skipped(self):
        events = ['not json', '[1, 2]', '{"type": "turn.failed"', self.STARTED,
            self.TURN, self.COMPLETED, '"turn.failed"']
        self.assertEqual(self.judge(events), ('done', None))

    def test_unreadable_transcript_fails(self):
        write(self.files['last'], 'final\n')
        self.assertEqual(codex_leaf.judge(self.state, self.files, 0),
            ('failed', 'cannot read the transcript'))

    def test_nonzero_exit_fails_with_the_turn_failure(self):
        events = [self.STARTED, self.TURN, self.error(self.LIMIT),
            self.failed(self.LIMIT)]
        self.assertEqual(self.judge(events, exit_code=1),
            ('failed', 'codex exited with 1: turn failed: ' + self.LIMIT))
        events = [self.STARTED, self.TURN, self.COMPLETED]
        self.assertEqual(self.judge(events, exit_code=2),
            ('failed', 'codex exited with 2'))

    def test_failed_turn_fails(self):
        events = [self.STARTED, self.TURN, self.failed(self.LIMIT)]
        for exit_code in (None, 0):
            self.assertEqual(self.judge(events, exit_code=exit_code),
                ('failed', 'turn failed: ' + self.LIMIT))
        events = [self.STARTED, self.TURN, self.failed()]
        self.assertEqual(self.judge(events), ('failed', 'turn failed'))

    def test_missing_completed_turn_fails(self):
        reason = 'transcript has no completed turn'
        self.assertEqual(self.judge([self.STARTED, self.TURN]),
            ('failed', reason))
        self.assertEqual(self.judge([self.STARTED]), ('failed', reason))
        events = [self.STARTED, self.TURN, self.error('Reconnecting... 4/5'),
            self.error('Reconnecting... 5/5')]
        self.assertEqual(self.judge(events),
            ('failed', reason + ' (last error: Reconnecting... 5/5)'))
        events = [self.STARTED, self.TURN, self.COMPLETED, self.TURN]
        self.assertEqual(self.judge(events), ('failed', reason))

    def test_missing_or_empty_final_message_fails(self):
        events = [self.STARTED, self.TURN, self.COMPLETED]
        for last in (None, ''):
            self.assertEqual(self.judge(events, last=last, exit_code=0),
                ('failed', 'codex produced no final message'))

    def test_final_message_older_than_the_leaf_fails(self):
        events = [self.STARTED, self.TURN, self.COMPLETED]
        self.judge(events)
        stale = self.state['started'] - 3600
        os.utime(self.files['last'], (stale, stale))
        self.assertEqual(codex_leaf.judge(self.state, self.files, 0),
            ('failed', 'final message predates the leaf'))
        slack = self.state['started'] - codex_leaf.FRESHNESS / 2
        os.utime(self.files['last'], (slack, slack))
        self.assertEqual(codex_leaf.judge(self.state, self.files, 0),
            ('done', None))

    def test_thread_id_comes_from_the_first_thread_started(self):
        self.judge([self.STARTED, self.TURN,
            {'type': 'thread.started', 'thread_id': 'thread-2'}])
        self.assertEqual(codex_leaf.thread_id(self.files['jsonl']), 'thread-1')
        os.remove(self.files['jsonl'])
        self.assertIsNone(codex_leaf.thread_id(self.files['jsonl']))


class StateWriteTest(TempCase):
    def setUp(self):
        super().setUp()
        self.path = codex_leaf.paths(self.root, 'a')['state']

    def test_replace_retries_sharing_violations(self):
        source = os.path.join(self.root, 'source')
        write(source, 'new')
        busy = PermissionError(errno.EACCES, 'sharing violation')
        attempts = [busy, busy, None]
        with mock.patch('os.replace', side_effect=attempts) as replace:
            codex_leaf.replace_file(source, self.path)
        self.assertEqual(replace.call_count, 3)

    def test_replace_does_not_retry_other_errors(self):
        broken = OSError(errno.EIO, 'input/output error')
        with mock.patch('os.replace', side_effect=broken) as replace:
            with self.assertRaises(OSError):
                codex_leaf.replace_file(self.path + '.tmp', self.path)
        self.assertEqual(replace.call_count, 1)

    def test_exhausted_retry_budget_is_a_named_error(self):
        codex_leaf.write_state(self.root, 'a', {'status': 'running'})
        before = read_bytes(self.path)
        busy = PermissionError(errno.EACCES, 'held by a reader')
        with mock.patch.dict(os.environ, {'CODEX_LEAF_RETRY': '0.1'}), \
                mock.patch('os.replace', side_effect=busy) as replace:
            began = time.time()
            with self.assertRaises(codex_leaf.LeafError) as caught:
                codex_leaf.write_state(self.root, 'a', {'status': 'done'})
            elapsed = time.time() - began
        self.assertEqual(caught.exception.reason, 'state-write-failed')
        self.assertIn('held by a reader', caught.exception.text)
        self.assertGreater(replace.call_count, 1)
        self.assertGreaterEqual(elapsed, 0.1)
        self.assertLess(elapsed, 2)
        self.assertEqual(read_bytes(self.path), before)

    def test_update_without_a_change_writes_nothing(self):
        codex_leaf.write_state(self.root, 'a', {'status': 'running', 'n': 1})
        old = time.time() - 3600
        os.utime(self.path, (old, old))
        before = self.snapshot(self.path)
        seen = []

        def keep(state):
            seen.append(state)
            return None

        stored = codex_leaf.update_state(self.root, 'a', keep)
        self.assertEqual(seen, [{'status': 'running', 'n': 1}])
        self.assertEqual(stored, {'status': 'running', 'n': 1})
        self.assertEqual(self.snapshot(self.path), before)
        self.assertFalse(os.path.exists(self.path + '.tmp'))
        stored = codex_leaf.update_state(
            self.root, 'a', lambda state: dict(state, n=2))
        self.assertEqual(stored, {'status': 'running', 'n': 2})
        self.assertEqual(json.loads(read_bytes(self.path)), stored)


class LockTest(TempCase):
    def test_nested_acquire_is_busy(self):
        with mock.patch.object(codex_leaf, 'LOCK_TIMEOUT', 0.2):
            with codex_leaf.leaf_lock(self.root, 'a'):
                with self.assertRaises(codex_leaf.LeafError) as caught:
                    with codex_leaf.leaf_lock(self.root, 'a'):
                        pass
            with codex_leaf.leaf_lock(self.root, 'a'):
                pass
        self.assertEqual(caught.exception.reason, 'state-lock-busy')

    def test_lock_is_released_when_its_holder_dies(self):
        gate = os.path.join(self.root, 'gate')
        holder = subprocess.Popen(
            [sys.executable, '-B', '-c', LOCK_HOLDER, str(HELPER.parent),
                self.root, gate],
            stdout=subprocess.PIPE, text=True)
        self.addCleanup(self.end_holder, holder, gate)
        self.assertEqual(holder.stdout.readline().strip(), 'locked')
        with mock.patch.object(codex_leaf, 'LOCK_TIMEOUT', 0.2):
            with self.assertRaises(codex_leaf.LeafError) as caught:
                with codex_leaf.leaf_lock(self.root, 'a'):
                    pass
        self.assertEqual(caught.exception.reason, 'state-lock-busy')
        holder.kill()
        holder.wait(timeout=30)
        began = time.time()
        with mock.patch.object(codex_leaf, 'LOCK_TIMEOUT', 2):
            with codex_leaf.leaf_lock(self.root, 'a'):
                elapsed = time.time() - began
        self.assertLess(elapsed, 2)

    def end_holder(self, holder, gate):
        write(gate, '')
        holder.wait(timeout=30)
        holder.stdout.close()


class ArchiveTest(TempCase):
    STAMP = '20260101-000000'

    def setUp(self):
        super().setUp()
        self.files = codex_leaf.paths(self.root, 'a')

    def plant(self):
        contents = {}
        for key in codex_leaf.ARCHIVED:
            contents[key] = ('%s bytes\n' % key).encode('utf-8')
            with open(self.files[key], 'wb') as f:
                f.write(contents[key])
        return contents

    def prev(self):
        return sorted(entry for entry in os.listdir(self.root)
            if '.prev-' in entry)

    def failing(self, *indices):
        real, calls = os.replace, []

        def replace(source, target):
            calls.append(source)
            if len(calls) - 1 in indices:
                raise OSError(errno.EIO, 'injected failure', source)
            return real(source, target)

        return replace

    def test_failure_at_any_rename_moves_nothing(self):
        for index, key in enumerate(codex_leaf.ARCHIVED):
            with self.subTest(index=index, key=key):
                contents = self.plant()
                replace = self.failing(index)
                with mock.patch('os.replace', side_effect=replace), \
                        self.assertRaises(codex_leaf.LeafError) as caught:
                    codex_leaf.archive(self.files, list(codex_leaf.ARCHIVED))
                self.assertEqual(caught.exception.reason, 'archive-failed')
                self.assertIn('could not archive %s' % self.files[key],
                    caught.exception.text)
                self.assertIn('nothing was moved', caught.exception.text)
                for name, data in contents.items():
                    self.assertEqual(read_bytes(self.files[name]), data)
                self.assertEqual(self.prev(), [])

    def test_failed_rollback_names_the_stranded_file(self):
        contents = self.plant()
        with mock.patch('os.replace', side_effect=self.failing(2, 3)):
            with self.assertRaises(codex_leaf.LeafError) as caught:
                codex_leaf.archive(self.files, list(codex_leaf.ARCHIVED))
        stranded = self.prev()
        self.assertEqual(len(stranded), 1)
        self.assertTrue(stranded[0].startswith('a.stderr.txt.prev-'), stranded)
        moved = os.path.join(self.root, stranded[0])
        self.assertIn('%s stays at %s' % (self.files['stderr'], moved),
            caught.exception.text)
        self.assertNotIn('nothing was moved', caught.exception.text)
        self.assertEqual(read_bytes(moved), contents['stderr'])
        for key in ('jsonl', 'last', 'runner_log', 'state'):
            self.assertEqual(read_bytes(self.files[key]), contents[key])

    def test_success_moves_every_file_under_one_suffix(self):
        contents = self.plant()
        suffix = codex_leaf.archive(self.files, list(codex_leaf.ARCHIVED))
        self.assertRegex(suffix, r'^\.prev-\d{8}-\d{6}$')
        self.assertEqual(len(self.prev()), 5)
        for key, data in contents.items():
            self.assertFalse(os.path.exists(self.files[key]))
            self.assertEqual(read_bytes(self.files[key] + suffix), data)

    def test_taken_suffix_gets_a_counter(self):
        contents = self.plant()
        older = {
            self.files['jsonl'] + '.prev-' + self.STAMP: b'older jsonl',
            self.files['state'] + '.prev-' + self.STAMP + '-1': b'older state',
        }
        for path, data in older.items():
            with open(path, 'wb') as f:
                f.write(data)
        with mock.patch('time.strftime', return_value=self.STAMP):
            suffix = codex_leaf.archive(self.files, list(codex_leaf.ARCHIVED))
        self.assertEqual(suffix, '.prev-%s-2' % self.STAMP)
        for path, data in older.items():
            self.assertEqual(read_bytes(path), data)
        for key, data in contents.items():
            self.assertEqual(read_bytes(self.files[key] + suffix), data)


class ActivityDecisionTest(unittest.TestCase):
    FILES = {'jsonl': os.path.join('runs', 'a.jsonl')}
    TRACKED = {'status': 'running', 'run_id': 'r1', 'runner_pid': 50,
        'pgid': 50}
    LEGACY = {'status': 'running', 'runner_pid': 50}
    PROBE_ERROR = ('error',
        'cannot probe a.jsonl: [Errno 13] Permission denied')

    def posix(self, state, writer, group='exists', runner=True):
        with mock.patch.object(codex_leaf, 'WINDOWS', False), \
                mock.patch.object(codex_leaf, 'transcript_writer',
                    return_value=writer), \
                mock.patch.object(codex_leaf, 'group_status',
                    return_value=group), \
                mock.patch.object(codex_leaf, 'group_members',
                    return_value=[50, 51]), \
                mock.patch.object(codex_leaf, 'posix_runner_alive',
                    return_value=runner):
            return codex_leaf.leaf_activity(state, self.FILES)

    def windows(self, state, writer=('free', None), pids=(), error=None):
        listing = (mock.Mock(side_effect=error) if error
            else mock.Mock(return_value=list(pids)))
        with mock.patch.object(codex_leaf, 'WINDOWS', True), \
                mock.patch.object(codex_leaf, 'transcript_writer',
                    return_value=writer), \
                mock.patch.object(codex_leaf, 'job_pids', listing):
            return codex_leaf.leaf_activity(state, self.FILES)

    def test_posix_held_token_is_alive_whatever_the_state(self):
        unknown = codex_leaf.Activity('alive', [], None, None)
        activity = self.posix({}, ('held', None))
        self.assertEqual(activity, unknown)
        self.assertEqual(codex_leaf.describe_pids(activity), 'pid unknown')
        activity = self.posix(self.LEGACY, ('held', None))
        self.assertEqual(activity, unknown)
        activity = self.posix(self.TRACKED, ('held', None))
        self.assertEqual(activity,
            codex_leaf.Activity('alive', [50, 51], True, None))
        self.assertEqual(codex_leaf.describe_pids(activity), 'pid 50, 51')
        activity = self.posix(self.TRACKED, ('held', None), runner=False)
        self.assertIs(activity.runner, False)

    def test_posix_free_token_ignores_a_foreign_group(self):
        for writer in (('free', None), ('missing', None)):
            activity = self.posix(self.TRACKED, writer, group='exists')
            self.assertEqual(activity,
                codex_leaf.Activity('gone', [], None, None))

    def test_posix_probe_error_is_unresolved(self):
        activity = self.posix(self.TRACKED, self.PROBE_ERROR, group='exists')
        self.assertEqual(activity, codex_leaf.Activity(
            'unresolved', [50, 51], None, self.PROBE_ERROR[1]))
        activity = self.posix({}, self.PROBE_ERROR)
        self.assertEqual(activity, codex_leaf.Activity(
            'unresolved', [], None, self.PROBE_ERROR[1]))
        activity = self.posix(self.TRACKED, self.PROBE_ERROR, group='empty')
        self.assertEqual(activity.verdict, 'gone')

    def test_posix_legacy_state_waits_for_its_group(self):
        activity = self.posix(self.LEGACY, ('free', None), group='empty')
        self.assertEqual(activity, codex_leaf.Activity('gone', [], None, None))
        activity = self.posix(self.LEGACY, ('free', None), group='exists')
        self.assertEqual(activity, codex_leaf.Activity(
            'unresolved', [50, 51], None, 'process group 50 still exists'))
        activity = self.posix(self.LEGACY, ('missing', None), group='error')
        self.assertEqual(activity, codex_leaf.Activity(
            'unresolved', [50, 51], None, 'cannot probe process group 50'))
        self.assertEqual(self.posix({}, ('free', None)).verdict, 'gone')

    def test_windows_job_members_are_alive(self):
        state = {'status': 'running', 'job': 'Local\\codex_leaf_r1',
            'runner_pid': 10}
        activity = self.windows(state, pids=[10, 20])
        self.assertEqual(activity,
            codex_leaf.Activity('alive', [10, 20], True, None))
        activity = self.windows(state, pids=[20, 30])
        self.assertEqual(activity,
            codex_leaf.Activity('alive', [20, 30], False, None))

    def test_windows_writer_outside_the_job_is_alive(self):
        state = {'status': 'running', 'job': 'Local\\codex_leaf_r1',
            'runner_pid': 10}
        for probed in (state, {}):
            activity = self.windows(probed, writer=('held', None))
            self.assertEqual(activity[:3], ('alive', [], None))
            self.assertIn('a.jsonl is open for writing outside',
                activity.detail)

    def test_windows_query_or_probe_error_is_unresolved(self):
        state = {'status': 'running', 'job': 'Local\\codex_leaf_r1'}
        activity = self.windows(state, error=OSError(5, 'access denied'))
        self.assertEqual(activity.verdict, 'unresolved')
        self.assertTrue(activity.detail.startswith('cannot query the job: '),
            activity)
        activity = self.windows({}, writer=self.PROBE_ERROR)
        self.assertEqual(activity, codex_leaf.Activity(
            'unresolved', [], None, self.PROBE_ERROR[1]))

    def test_windows_empty_job_and_free_token_is_gone(self):
        state = {'status': 'running', 'job': 'Local\\codex_leaf_r1'}
        for writer in (('free', None), ('missing', None)):
            activity = self.windows(state, writer=writer)
            self.assertEqual(activity.verdict, 'gone')
        self.assertEqual(self.windows({}).verdict, 'gone')

    def test_termination_signals_only_a_verified_group(self):
        cases = (
            ({}, ('held', None), 'exists'),
            (self.LEGACY, ('free', None), 'exists'),
            (self.TRACKED, ('free', None), 'exists'),
            (self.TRACKED, self.PROBE_ERROR, 'exists'),
        )
        for state, writer, group in cases:
            with self.subTest(state=state, writer=writer), \
                    mock.patch.object(codex_leaf, 'WINDOWS', False), \
                    mock.patch.object(codex_leaf, 'transcript_writer',
                        return_value=writer), \
                    mock.patch.object(codex_leaf, 'group_status',
                        return_value=group), \
                    mock.patch.object(codex_leaf, 'group_members',
                        return_value=[50]), \
                    mock.patch.object(codex_leaf, 'signal_group') as signals:
                codex_leaf.terminate_leaf(state, self.FILES)
            signals.assert_not_called()
        with mock.patch.object(codex_leaf, 'WINDOWS', False), \
                mock.patch.object(codex_leaf, 'transcript_writer',
                    side_effect=[('held', None), ('free', None)]), \
                mock.patch.object(codex_leaf, 'group_members',
                    return_value=[50]), \
                mock.patch.object(codex_leaf, 'posix_runner_alive',
                    return_value=True), \
                mock.patch.object(codex_leaf, 'signal_group') as signals:
            activity = codex_leaf.terminate_leaf(self.TRACKED, self.FILES)
        self.assertEqual(activity.verdict, 'gone')
        signals.assert_called_once_with(50, signal.SIGTERM)


class CheckLeafTest(TempCase):
    def store(self, **state):
        with codex_leaf.leaf_lock(self.root, 'a'):
            codex_leaf.write_state(self.root, 'a', dict(state, name='a'))
        path = codex_leaf.paths(self.root, 'a')['state']
        old = time.time() - 3600
        os.utime(path, (old, old))
        return path

    def test_terminal_states_are_never_rewritten(self):
        for status in ('done', 'failed', 'stopped'):
            with self.subTest(status=status):
                path = self.store(status=status, run_id='r1', runner_pid=1,
                    started=time.time() - 3600, reason='kept')
                before = self.snapshot(path)
                with mock.patch.object(codex_leaf, 'write_state',
                        wraps=codex_leaf.write_state) as written:
                    state, activity = codex_leaf.check_leaf(self.root, 'a')
                written.assert_not_called()
                self.assertEqual(state['status'], status)
                self.assertIsNone(activity)
                self.assertEqual(self.snapshot(path), before)

    def test_starting_within_the_grace_is_left_alone(self):
        path = self.store(status='starting', run_id='r1', started=time.time())
        before = self.snapshot(path)
        with mock.patch.dict(os.environ, {'CODEX_LEAF_START_GRACE': '30'}):
            state, activity = codex_leaf.check_leaf(self.root, 'a')
        self.assertEqual((state['status'], activity), ('starting', None))
        self.assertEqual(self.snapshot(path), before)

    def test_busy_lock_is_unresolved_and_writes_nothing(self):
        path = self.store(status='running', run_id='r1',
            started=time.time() - 3600, max_minutes=120)
        before = self.snapshot(path)
        with mock.patch.object(codex_leaf, 'LOCK_TIMEOUT', 0.2):
            with codex_leaf.leaf_lock(self.root, 'a'):
                state, activity = codex_leaf.check_leaf(self.root, 'a')
        self.assertEqual(state['status'], 'running')
        self.assertEqual(activity.verdict, 'unresolved')
        self.assertTrue(activity.detail.startswith('state-lock-busy: '),
            activity)
        self.assertEqual(self.snapshot(path), before)
        state, activity = codex_leaf.check_leaf(self.root, 'a')
        self.assertEqual(state['status'], 'failed')
        self.assertEqual(state['reason'],
            'runner lost; cannot read the transcript')

    def test_missing_state_creates_nothing(self):
        self.assertEqual(codex_leaf.check_leaf(self.root, 'b'), (None, None))
        self.assertEqual(os.listdir(self.root), [])


class OrdinaryLeafTest(LeafCase):
    def test_leaf_finishes_done(self):
        directive = self.directive(stderr='fake stderr text\n')
        result = self.start('a', directive)
        self.assertEqual(result.code, 0, result)
        reply = json.loads(result.out)
        self.assertEqual((reply['name'], reply['state']),
            ('a', self.files('a')['state']))
        result = self.assert_done('a')
        lines = result.out.splitlines()
        self.assertIn('  | fake final line 1', lines)
        self.assertIn('  | fake final line 2', lines)
        self.assertEqual(lines[-1], 'FINISHED')
        state = self.state('a')
        marker = self.marker(directive)
        self.assertEqual(state['exit_code'], 0)
        self.assertEqual(state['thread_id'], marker['thread'])
        for key in ('run_id', 'runner_pid', 'codex_pid', 'command'):
            self.assertTrue(state.get(key), key)
        self.assertEqual(state['launch'], reply['launch'])
        if WINDOWS:
            self.assertIn(state['launch'], ('breakaway', 'no-breakaway'))
            self.assertEqual(state['job'],
                'Local\\codex_leaf_' + state['run_id'])
            self.assertIsInstance(state['runner_in_job'], bool)
        else:
            self.assertEqual(state['launch'], 'session')
            self.assertEqual(state['pgid'], state['runner_pid'])
        log = self.files('a')['runner_log']
        exiting = 'runner %d exiting (exit 0)' % state['runner_pid']
        self.until(lambda: exiting in self.read(log),
            what='the runner exit line')
        lines = [line for line in self.read(log).splitlines()
            if 'breakaway launch failed' not in line]
        self.assertEqual(len(lines), 2, lines)
        self.assertTrue(lines[0].endswith('runner %d started (launch %s)' % (
            state['runner_pid'], state['launch'])), lines)
        self.assertTrue(lines[1].endswith(exiting), lines)
        self.assertNotIn('fake stderr text', self.read(log))
        self.assertIn('fake stderr text', self.read(self.files('a')['stderr']))
        self.until(lambda: self.token('a') == 'free', what='the token')

    def test_argv_carries_every_flag(self):
        schema = os.path.join(self.root, 'schema.json')
        write(schema, '{}')
        directive = self.directive()
        result = self.start('f', directive, '--sandbox', 'workspace-write',
            '--effort', 'high', '--model', 'm1', '--no-project-doc',
            '--output-schema', schema, '--max-minutes', '7',
            env={'GIT_CONFIG_PARAMETERS': "'user.name=leaf'"})
        self.assertEqual(result.code, 0, result)
        self.assert_done('f')
        expected = [self.binary, 'exec', '--json', '--skip-git-repo-check',
            '-o', self.files('f')['last'], '-c', 'model_reasoning_effort=high',
            '-m', 'm1', '-c', 'project_doc_max_bytes=0',
            '--output-schema', schema, '-C', self.cwd, '-s', 'workspace-write',
            '-']
        state = self.state('f')
        marker = self.marker(directive)
        self.assertEqual(state['command'], expected)
        self.assertEqual(marker['argv'][1:], expected[1:])
        self.assertEqual(marker['GIT_CONFIG_PARAMETERS'],
            "'user.name=leaf' 'safe.directory=*'")
        self.assertEqual(os.path.normcase(os.path.realpath(marker['cwd'])),
            os.path.normcase(self.cwd))
        self.assertEqual(state['max_minutes'], 7)

    def test_argv_defaults(self):
        directive = self.directive()
        self.assertEqual(self.start('d', directive).code, 0)
        self.assert_done('d')
        expected = [self.binary, 'exec', '--json', '--skip-git-repo-check',
            '-o', self.files('d')['last'],
            '-c', 'model_reasoning_effort=xhigh',
            '-C', self.cwd, '-s', 'read-only', '-']
        state = self.state('d')
        marker = self.marker(directive)
        self.assertEqual(state['command'], expected)
        self.assertEqual(marker['argv'][1:], expected[1:])
        self.assertEqual(marker['GIT_CONFIG_PARAMETERS'], "'safe.directory=*'")
        self.assertEqual(state['max_minutes'], 120)
        self.assertEqual(state['codex_home'], self.work_home)
        self.assertEqual(marker['CODEX_HOME'], self.work_home)


@unittest.skipIf(WINDOWS, 'a bare executable fake needs a POSIX shebang')
class DirectBinaryTest(LeafCase):
    def setUp(self):
        super().setUp()
        self.use_binary(self.direct_binary())

    def test_leaf_finishes_done(self):
        directive = self.directive()
        self.assertEqual(self.start('a', directive).code, 0)
        self.assert_done('a')
        state = self.state('a')
        marker = self.marker(directive)
        self.assertEqual(state['codex_pid'], marker['pid'])
        self.assertEqual(state['command'][0], self.binary)
        self.assertEqual(state['thread_id'], marker['thread'])

    def test_runner_lost_at_spawned_keeps_running(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.lose_runner('g', directive, 'spawned')
        marker = self.marker(directive)
        self.outlast_grace()
        result = self.wait('g', timeout=1.5)
        self.assertEqual(result.code, 2, result)
        line = self.line(result, 'g')
        self.assertTrue(line.startswith('g: running '), result)
        self.assertIn(' runner-lost', line)
        self.assertIn(marker['pid'], listed_pids(line))
        self.assertNotIn('failed', result.out)
        state = self.state('g')
        self.assertEqual(state['status'], 'running')
        self.assertNotIn('codex_pid', state)
        self.release(hold)
        self.assert_done('g')
        self.assertIs(self.state('g')['runner_lost'], True)


class ResumedLeafTest(LeafCase):
    def test_resume_argv_and_thread(self):
        self.assertEqual(self.start('a', self.directive()).code, 0)
        self.assert_done('a')
        thread = self.state('a')['thread_id']
        directive = self.directive()
        result = self.start('b', directive, '--resume', 'a',
            '--sandbox', 'workspace-write')
        self.assertEqual(result.code, 0, result)
        self.assert_done('b')
        expected = [self.binary, 'exec', 'resume', '--json',
            '--skip-git-repo-check', '-o', self.files('b')['last'],
            '-c', 'model_reasoning_effort=xhigh',
            '-c', 'sandbox_mode=workspace-write', thread, '-']
        state = self.state('b')
        self.assertEqual(state['command'], expected)
        self.assertEqual(self.marker(directive)['argv'][1:], expected[1:])
        self.assertEqual(
            (state['resume_of'], state['resume_thread'], state['thread_id']),
            ('a', thread, thread))

    def test_resume_takes_the_thread_from_a_lost_runners_transcript(self):
        hold = self.gate('hold')
        source = self.directive(hold=hold)
        self.lose_runner('s', source, 'recorded')
        self.release(hold)
        self.until(lambda: self.token('s') == 'free',
            what='the source leaf to finish')
        state = self.state('s')
        self.assertEqual(state['status'], 'running')
        self.assertNotIn('thread_id', state)
        thread = self.marker(source)['thread']
        directive = self.directive()
        result = self.start('b', directive, '--resume', 's')
        self.assertEqual(result.code, 0, result)
        self.assert_done('b')
        self.assertEqual(self.state('b')['resume_thread'], thread)
        self.assertEqual(self.marker(directive)['argv'][-2], thread)

    def test_resume_refused_while_the_source_runs(self):
        hold = self.gate('hold')
        source = self.directive(hold=hold)
        self.assertEqual(self.start('s', source).code, 0)
        self.wait_marker(source)
        result = self.start('b', self.directive(), '--resume', 's')
        self.assertEqual(result.code, 1, result)
        self.assertIn('codex_leaf: resume-source-running: leaf s is still '
            'running (pid ', result.err)
        self.assertNotIn('Traceback', result.err)
        self.assertEqual(self.entries('b'), [])
        self.release(hold)
        self.assert_done('s')


class HomeTest(LeafCase):
    def check(self, *flags, env=None):
        result = self.run_helper('check', *flags, env=env)
        return result, json.loads(result.out)

    def test_check_reads_the_login_of_the_chosen_home(self):
        result, reply = self.check()
        self.assertEqual(result.code, 0, result)
        self.assertEqual((reply['home'], reply['login']),
            (self.work_home, 'Logged in; home %s' % self.work_home))
        result, reply = self.check('--home', 'default',
            env={'CODEX_HOME': self.cwd})
        self.assertEqual(result.code, 0, result)
        self.assertEqual((reply['home'], reply['login']),
            ('default', 'Logged in; home %s' % self.cwd))

    def test_missing_work_home_fails_check_and_start(self):
        os.rmdir(self.work_home)
        result, reply = self.check()
        self.assertEqual(result.code, 1, result)
        self.assertIs(reply['ok'], False)
        self.assertIn('codex home %s does not exist' % self.work_home,
            reply['reason'])
        self.assert_error(self.start('a', self.directive()), 'home-missing')
        self.assertEqual(self.entries('a'), [])
        self.assertEqual(self.markers(), 0)

    def test_default_home_keeps_the_environment(self):
        directive = self.directive()
        result = self.start('a', directive, '--home', 'default',
            env={'CODEX_HOME': self.cwd})
        self.assertEqual(result.code, 0, result)
        self.assert_done('a')
        self.assertIsNone(self.state('a')['codex_home'])
        self.assertEqual(self.marker(directive)['CODEX_HOME'], self.cwd)

    def test_resume_runs_in_the_source_home(self):
        self.assertEqual(self.start('w', self.directive()).code, 0)
        self.assertEqual(
            self.start('d', self.directive(), '--home', 'default').code, 0)
        self.assert_done('w')
        self.assert_done('d')
        with self.locked('d'):
            state = codex_leaf.read_state(self.run_dir, 'd')
            del state['codex_home']
            codex_leaf.write_state(self.run_dir, 'd', state)
        on_work, on_default = self.directive(), self.directive()
        self.assertEqual(self.start('w2', on_work, '--resume', 'w',
            '--home', 'default').code, 0)
        self.assertEqual(self.start('d2', on_default, '--resume', 'd').code, 0)
        self.assert_done('w2')
        self.assert_done('d2')
        self.assertEqual(self.state('w2')['codex_home'], self.work_home)
        self.assertEqual(self.marker(on_work)['CODEX_HOME'], self.work_home)
        self.assertIsNone(self.state('d2')['codex_home'])
        self.assertIsNone(self.marker(on_default)['CODEX_HOME'])


class StartupFailureTest(LeafCase):
    def test_missing_binary_fails_the_leaf(self):
        directive = self.directive()
        missing = os.path.join(self.root, 'bin', 'missing-codex')
        result = self.start('n', directive, env={'CODEX_BIN': missing})
        self.assertEqual(result.code, 0, result)
        result = self.wait('n')
        self.assertEqual(result.code, 0, result)
        line = self.line(result, 'n')
        self.assertTrue(line.startswith('n: failed '), result)
        self.assertIn('reason=could not start codex: ', line)
        self.assertIsNone(self.marker(directive))

    def test_runner_lost_before_claiming_fails_after_the_grace(self):
        directive = self.directive()
        self.lose_runner('k', directive, 'launched')
        self.assertEqual(self.status('k'), 'starting')
        self.outlast_grace()
        result = self.wait('k')
        self.assertEqual(result.code, 0, result)
        line = self.line(result, 'k')
        self.assertTrue(line.startswith('k: failed '), result)
        self.assertIn('reason=runner did not start within %gs' % GRACE, line)
        self.assertEqual(self.markers(), 0)
        self.assertFalse(os.path.exists(self.files('k')['jsonl']))

    def test_invalid_arguments_create_and_archive_nothing(self):
        prompt = self.prompt(self.directive())
        missing = os.path.join(self.root, 'nosuch')
        cases = (
            (('--cwd', missing, '--prompt-file', prompt),
                'working directory does not exist: '),
            (('--cwd', self.cwd, '--prompt-file', missing),
                'prompt file does not exist: '),
        )
        for flags, message in cases:
            result = self.run_helper(
                'start', '--run-dir', self.run_dir, '--name', 'b', *flags)
            self.assertEqual(result.code, 1, result)
            self.assertIn('codex_leaf: ' + message, result.err)
            self.assertEqual(self.entries('b'), [])
        self.assertEqual(self.start('a', self.directive()).code, 0)
        self.assert_done('a')
        before = self.digest('a')
        for flags, message in cases:
            result = self.run_helper(
                'start', '--run-dir', self.run_dir, '--name', 'a', *flags,
                '--replace')
            self.assertEqual(result.code, 1, result)
            self.assertIn('codex_leaf: ' + message, result.err)
            self.assertEqual(self.digest('a'), before)
            self.assertEqual(self.archived('a'), [])


class UnsuccessfulCompletionTest(LeafCase):
    def test_failed_turn_and_exit_code(self):
        directive = self.directive(turn='failed', exit=1,
            stderr='boom line\n', message='429 Too Many Requests')
        self.assertEqual(self.start('t', directive).code, 0)
        result = self.wait('t')
        self.assertEqual(result.code, 0, result)
        line = self.line(result, 't')
        self.assertTrue(line.startswith('t: failed '), result)
        self.assertIn('reason=codex exited with 1: turn failed: '
            '429 Too Many Requests', line)
        self.assertIn('  ! boom line', result.out.splitlines())
        self.assertEqual(self.state('t')['exit_code'], 1)

    def test_missing_or_empty_final_message(self):
        for name, last in (('n', None), ('e', '')):
            with self.subTest(last=last):
                directive = self.directive(last=last)
                self.assertEqual(self.start(name, directive).code, 0)
                result = self.wait(name)
                line = self.line(result, name)
                self.assertTrue(line.startswith(name + ': failed '), result)
                self.assertIn('reason=codex produced no final message', line)
                self.assertEqual(self.state(name)['exit_code'], 0)

    def test_stale_final_message_on_the_runner_path(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold, last=None)
        self.assertEqual(self.start('s', directive).code, 0)
        self.wait_marker(directive)
        last = self.files('s')['last']
        write(last, 'an earlier run\n')
        stale = time.time() - 3600
        os.utime(last, (stale, stale))
        self.release(hold)
        result = self.wait('s')
        line = self.line(result, 's')
        self.assertIn('reason=final message predates the leaf', line)
        self.assertTrue(line.startswith('s: failed '), result)
        state = self.state('s')
        self.assertEqual(state['exit_code'], 0)
        self.assertNotIn('runner_lost', state)


class StopTest(LeafCase):
    def test_stop_ends_a_held_leaf(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.assertEqual(self.start('s', directive).code, 0)
        self.wait_marker(directive)
        runner = self.state('s')['runner_pid']
        result = self.stop('s')
        self.assertEqual((result.code, result.out), (0, 's: stopped\n'),
            result)
        state = self.state('s')
        self.assertEqual(state['status'], 'stopped')
        self.assertIn('finished', state)
        self.assertIn('minutes', state)
        self.assertEqual(self.token('s'), 'free')
        self.until(lambda: not self.fake_alive(directive),
            what='the fake to end')
        self.until(lambda: not self.runner_alive(runner),
            what='the runner to end')
        self.release(hold)
        before = self.snapshot('s')
        result = self.wait('s')
        self.assertEqual(result.code, 0, result)
        self.assertTrue(self.line(result, 's').startswith('s: stopped '),
            result)
        self.assertEqual(self.snapshot('s'), before)

    def test_stop_keeps_a_finished_status(self):
        self.assertEqual(self.start('a', self.directive()).code, 0)
        self.assert_done('a')
        runner = self.state('a')['runner_pid']
        self.until(lambda: not self.runner_alive(runner),
            what='the runner to end')
        before = self.snapshot('a')
        result = self.stop('a')
        self.assertEqual((result.code, result.out), (0, 'a: done\n'), result)
        self.assertEqual(self.snapshot('a'), before)
        result = self.stop('nosuch')
        self.assertEqual(result.code, 1, result)
        self.assertIn('codex_leaf: no leaf nosuch', result.err)
        self.assertEqual(self.entries('nosuch'), [])

    def test_stop_after_runner_loss_ends_the_fake(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.lose_runner('g', directive, 'spawned')
        self.assertTrue(self.fake_alive(directive))
        result = self.stop('g')
        self.assertEqual((result.code, result.out), (0, 'g: stopped\n'),
            result)
        self.until(lambda: not self.fake_alive(directive),
            what='the fake to end')
        self.assertEqual(self.token('g'), 'free')
        self.assertEqual(self.status('g'), 'stopped')

    def snapshot(self, name):
        path = self.files(name)['state']
        return read_bytes(path), os.stat(path).st_mtime_ns


class TimeLimitTest(LeafCase):
    def test_runner_enforces_the_time_limit(self):
        recorded = self.gate('recorded')
        self.hook({'name': 't', 'pause': 'recorded', 'gate': recorded})
        directive = self.directive(forever=True)
        result = self.start('t', directive, '--max-minutes', '0.01')
        self.assertEqual(result.code, 0, result)
        self.paused(recorded)
        self.wait_marker(directive)
        self.hook()
        self.release(recorded)
        result = self.wait('t', timeout=60)
        self.assertEqual(result.code, 0, result)
        line = self.line(result, 't')
        self.assertTrue(line.startswith('t: failed '), result)
        self.assertIn('reason=exceeded 0.01 minutes', line)
        state = self.state('t')
        self.assertNotIn('runner_lost', state)
        exiting = 'runner %d exiting (exit 0)' % state['runner_pid']
        log = self.files('t')['runner_log']
        self.until(lambda: exiting in self.read(log),
            what='the runner exit line')
        self.until(lambda: not self.fake_alive(directive),
            what='the fake to end')
        self.until(lambda: self.token('t') == 'free', what='the token')


class LostRunnerTest(LeafCase):
    def test_lost_runner_with_a_live_codex_keeps_running(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        runner = self.lose_runner('l', directive, 'recorded')
        marker = self.marker(directive)
        codex = self.state('l')['codex_pid']
        self.outlast_grace()
        line = self.assert_still_running('l', self.wait('l', timeout=1.5))
        self.assertIn(' runner-lost', line)
        pids = listed_pids(line)
        self.assertIn(marker['pid'], pids)
        self.assertIn(codex, pids)
        self.assertNotIn(runner, pids)
        self.release(hold)
        self.assert_done('l')
        state = self.state('l')
        self.assertIs(state['runner_lost'], True)
        self.assertEqual(state['thread_id'], marker['thread'])
        self.assertNotIn('exit_code', state)
        self.assertEqual(self.token('l'), 'free')


class OutcomeAfterLossTest(LeafCase):
    def lose(self, name, **options):
        hold = self.gate(name + '-hold')
        self.lose_runner(name, self.directive(hold=hold, **options),
            'recorded')
        return hold

    def assert_lost_failure(self, name, reason):
        result = self.assert_failed(name, 'runner lost; ' + reason)
        state = self.state(name)
        self.assertIs(state['runner_lost'], True)
        self.assertIn('  runner: launch=%s lost' % state['launch'],
            result.out.splitlines())

    def test_incomplete_turn_with_a_final_message_fails(self):
        self.release(self.lose('n', turn='none'))
        self.assert_lost_failure('n', 'transcript has no completed turn')
        self.assertGreater(os.path.getsize(self.files('n')['last']), 0)

    def test_failed_turn_fails(self):
        self.release(self.lose('f', turn='failed', exit=1))
        self.assert_lost_failure('f', 'turn failed: 429 Too Many Requests')

    def test_stale_final_message_fails(self):
        hold = self.lose('s', last=None)
        last = self.files('s')['last']
        write(last, 'an earlier run\n')
        stale = time.time() - 3600
        os.utime(last, (stale, stale))
        self.release(hold)
        self.assert_lost_failure('s', 'final message predates the leaf')

    def test_empty_final_message_fails(self):
        self.release(self.lose('e', last=''))
        self.assert_lost_failure('e', 'codex produced no final message')


class TimeLimitAfterLossTest(LeafCase):
    def test_wait_ends_a_lost_leaf_past_its_limit(self):
        directive = self.directive(forever=True)
        self.lose_runner('t', directive, 'spawned', '--max-minutes', '0.02')
        self.assertTrue(self.fake_alive(directive))
        result = self.assert_failed(
            't', 'exceeded 0.02 minutes (runner lost; ended by wait)')
        state = self.state('t')
        self.assertIs(state['runner_lost'], True)
        self.assertIn('  runner: launch=%s lost' % state['launch'],
            result.out.splitlines())
        self.until(lambda: not self.fake_alive(directive),
            what='the fake to end')
        self.assertEqual(self.token('t'), 'free')


class UntrackedWriterTest(LeafCase):
    def test_runner_lost_in_the_spawn_gap_stays_discoverable(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.lose_runner('g', directive, 'spawned')
        marker = self.marker(directive)
        self.assertNotIn('codex_pid', self.state('g'))
        self.outlast_grace()
        line = self.assert_still_running('g', self.wait('g', timeout=1))
        self.assertIn(' runner-lost', line)
        self.assertIn(marker['pid'], listed_pids(line))
        before, count = self.digest('g'), self.markers()
        result = self.start('g', self.directive(), '--replace')
        pids = self.assert_error(result, 'still-running')
        self.assertIn(marker['pid'], pids)
        self.assertIn('leaf g still has a running process (pid ', result.err)
        self.assertEqual(self.digest('g'), before)
        self.assertEqual(self.markers(), count)
        result = self.stop('g')
        self.assertEqual((result.code, result.out), (0, 'g: stopped\n'),
            result)
        self.until(lambda: not self.fake_alive(directive),
            what='the fake to end')
        self.assertEqual(self.token('g'), 'free')

    def test_files_without_state_and_a_live_writer_are_kept(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.lose_runner('h', directive, 'spawned')
        state = self.files('h')['state']
        hidden = os.path.join(self.root, 'hidden-h.json')
        os.replace(state, hidden)
        before, count = self.digest('h'), self.markers()
        self.assertNotIn('h.json', before)
        result = self.start('h', self.directive())
        self.assert_error(result, 'files-without-state')
        self.assertIn('leaf h has files but no state; pass --replace to '
            'archive them', result.err)
        self.assertEqual(self.digest('h'), before)
        result = self.start('h', self.directive(), '--replace')
        self.assert_error(result, 'still-running')
        self.assertIn('leaf h still has a running process', result.err)
        self.assertEqual(self.digest('h'), before)
        self.assertEqual(self.markers(), count)
        self.assertEqual(self.token('h'), 'held')
        os.replace(hidden, state)
        result = self.stop('h')
        self.assertEqual((result.code, result.out), (0, 'h: stopped\n'),
            result)
        self.until(lambda: not self.fake_alive(directive),
            what='the fake to end')

    def plant(self, name, pid, events, last):
        files = self.files(name)
        with open(files['jsonl'], 'wb') as f:
            for event in events:
                f.write((json.dumps(event) + '\n').encode('utf-8'))
        if last is not None:
            write(files['last'], last)
        state = {
            'name': name, 'status': 'running', 'run_id': uuid.uuid4().hex,
            'codex': self.binary, 'cwd': self.cwd,
            'prompt_file': self.prompt({}), 'sandbox': 'read-only',
            'effort': 'xhigh', 'model': None, 'output_schema': None,
            'no_project_doc': False, 'resume_of': None,
            'resume_thread': None, 'max_minutes': 120,
            'started': time.time() - 60, 'runner_pid': pid,
            'codex_pid': pid, 'command': [self.binary, 'exec'],
        }
        if WINDOWS:
            state.update(launch='breakaway', runner_in_job=False,
                job=codex_leaf.job_name(uuid.uuid4().hex))
        else:
            state.update(launch='session', pgid=pid)
        with self.locked(name):
            codex_leaf.write_state(self.run_dir, name, state)

    def test_reused_pid_is_not_taken_for_the_leaf(self):
        gate = self.gate('unrelated')
        options = ({'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP}
            if WINDOWS else {'start_new_session': True})
        unrelated = subprocess.Popen(
            [sys.executable, '-B', '-c', SLEEPER, gate],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, **options)
        self.addCleanup(self.end_process, unrelated, gate)
        if not WINDOWS:
            self.assertEqual(os.getpgid(unrelated.pid), unrelated.pid)
        completed = [{'type': 'thread.started', 'thread_id': 'thread-reused'},
            {'type': 'turn.started'}, {'type': 'turn.completed'}]
        self.plant('p', unrelated.pid, completed, 'reused final line\n')
        self.plant('q', unrelated.pid, completed[:2], None)
        result = self.stop('q')
        self.assertEqual((result.code, result.out), (0, 'q: stopped\n'),
            result)
        self.assertIsNone(unrelated.poll())
        result = self.assert_done('p')
        self.assertIn('  | reused final line', result.out.splitlines())
        self.assertIs(self.state('p')['runner_lost'], True)
        self.assertIsNone(unrelated.poll())
        result = self.stop('p')
        self.assertEqual((result.code, result.out), (0, 'p: done\n'), result)
        self.assertIsNone(unrelated.poll())
        old, count = self.digest('p'), self.markers()
        result = self.start('p', self.directive(), '--replace')
        self.assertEqual(result.code, 0, result)
        self.assert_archived_whole('p', old)
        self.assert_done('p')
        self.assertEqual(self.markers(), count + 1)
        self.assertIsNone(unrelated.poll())

    @unittest.skipIf(WINDOWS, 'chmod cannot make a Windows file unreadable')
    def test_probe_failure_is_unresolved(self):
        if os.geteuid() == 0:
            self.skipTest('root opens a file whatever its mode')
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.lose_runner('u', directive, 'recorded')
        marker = self.marker(directive)
        before = self.digest('u')
        transcript = self.files('u')['jsonl']
        os.chmod(transcript, 0)
        self.addCleanup(os.chmod, transcript, 0o644)
        line = self.assert_still_running('u', self.wait('u', timeout=1))
        self.assertIn(' unresolved=cannot probe u.jsonl: ', line)
        self.assertIn(marker['pid'], listed_pids(line))
        result = self.start('u', self.directive(), '--replace')
        self.assertIn(marker['pid'], self.assert_error(result, 'unresolved'))
        self.assertIn('cannot confirm that leaf u has no running process '
            '(cannot probe u.jsonl: ', result.err)
        os.chmod(transcript, 0o644)
        self.assertEqual(self.digest('u'), before)
        self.release(hold)
        self.assert_done('u')


class NoRollbackTest(LeafCase):
    CODEX_PID_WRITE = {'suffix': '.json.tmp', 'contains': '"codex_pid"'}

    def test_refused_state_write_is_retried(self):
        self.hook({'name': 'n', 'fail_replace': self.CODEX_PID_WRITE,
            'count': 3})
        self.assertEqual(self.start('n', self.directive()).code, 0)
        self.assert_done('n')
        self.assertEqual(self.load(self.hook_file)[0]['count'], 0)
        state = self.state('n')
        self.assertTrue(state.get('codex_pid'), state)
        self.assertNotIn('runner_lost', state)
        self.assertEqual(self.runner_exited('n'), 'exit 0')
        log = self.read(self.files('n')['runner_log'])
        self.assertNotIn('Traceback', log)
        self.assertNotIn('state write failed', log)

    def test_exhausted_state_write_is_logged_and_survived(self):
        self.hook({'name': 'n', 'fail_replace': self.CODEX_PID_WRITE,
            'count': 1000})
        result = self.start('n', self.directive(),
            env={'CODEX_LEAF_RETRY': '0.3'})
        self.assertEqual(result.code, 0, result)
        self.assert_done('n')
        state = self.state('n')
        self.assertNotIn('codex_pid', state)
        self.assertEqual(state['exit_code'], 0)
        self.assertNotIn('runner_lost', state)
        self.assertEqual(self.runner_exited('n'), 'exit 0')
        log = self.read(self.files('n')['runner_log'])
        self.assertIn('state write failed: state-write-failed: could not '
            'write ', log)
        self.assertNotIn('Traceback', log)
        self.assertNotIn('state write failed',
            self.read(self.files('n')['stderr']))

    def test_refused_running_write_never_starts_codex(self):
        self.hook({'name': 'n', 'count': 1000, 'fail_replace': {
            'suffix': '.json.tmp', 'contains': '"status": "running"'}})
        result = self.start('n', self.directive(), settle=False,
            env={'CODEX_LEAF_RETRY': '0.3'})
        self.assertEqual(result.code, 0, result)
        self.assertEqual(self.runner_exited('n'), 'state-write-failed')
        self.hook()
        self.assertEqual(self.status('n'), 'starting')
        self.outlast_grace()
        result = self.assert_failed(
            'n', 'runner did not start within %gs' % GRACE)
        lines = [line for line in result.out.splitlines()
            if line.startswith('  > ')]
        self.assertTrue([line for line in lines
            if 'state-write-failed: could not write ' in line], result)
        self.assertEqual(self.markers(), 0)

    def test_concurrent_waits_never_report_failed(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.assertEqual(self.start('c', directive).code, 0)
        self.wait_marker(directive)
        command = [sys.executable, '-B', str(HELPER), 'wait',
            '--run-dir', self.run_dir, '--name', 'c', '--timeout', '60']
        waiters = []
        for _ in range(4):
            waiter = subprocess.Popen(command, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, encoding='utf-8',
                errors='replace', env=self.env)
            self.addCleanup(self.end_process, waiter)
            waiters.append(waiter)
        self.until(lambda: len([entry for entry in self.live()
            if entry.startswith('wait-')]) == 4, what='four waits to start')
        self.release(hold)
        for waiter in waiters:
            out, err = waiter.communicate(timeout=90)
            self.assertEqual(waiter.returncode, 0, (out, err))
            self.assertIn('c: done ', out)
            self.assertNotIn('failed', out + err)
        self.assertEqual(self.status('c'), 'done')


class ReplacementTest(LeafCase):
    def test_live_leaf_is_refused_whatever_its_status(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.assertEqual(self.start('r', directive).code, 0)
        marker = self.wait_marker(directive)
        self.until(lambda: 'codex_pid' in (self.state('r') or {}),
            what='the runner to record codex')
        run_id = self.state('r').get('run_id')
        for status in ('failed', 'stopped', 'done'):
            with self.subTest(status=status):
                self.force('r', status=status)
                before, count = self.digest('r'), self.markers()
                result = self.start('r', self.directive(), '--replace')
                self.assertIn(marker['pid'],
                    self.assert_error(result, 'still-running'))
                self.assertEqual(self.digest('r'), before)
                self.assertEqual(self.markers(), count)
                self.assertEqual(self.state('r').get('run_id'), run_id)
        self.force('r', status='running')
        self.release(hold)
        self.assert_done('r')
        runner = self.state('r')['runner_pid']
        self.until(lambda: not self.runner_alive(runner),
            what='the runner to exit')
        old, count = self.digest('r'), self.markers()
        self.assertEqual(sorted(old), ['r.json', 'r.jsonl', 'r.last.md',
            'r.runner.log', 'r.stderr.txt'])
        result = self.start('r', self.directive(),
            '--cwd', os.path.join(self.root, 'nosuch'), '--replace')
        self.assertEqual(result.code, 1, result)
        self.assertIn('codex_leaf: working directory does not exist: ',
            result.err)
        self.assertEqual(self.digest('r'), old)
        replacement = self.directive()
        result = self.start('r', replacement, '--replace')
        self.assertEqual(result.code, 0, result)
        self.assert_archived_whole('r', old)
        self.assert_done('r')
        self.assertEqual(self.markers(), count + 1)
        self.assertTrue(self.marker(replacement))
        self.assertNotEqual(self.state('r')['run_id'], run_id)

    def test_files_without_state_are_kept_then_archived_whole(self):
        self.finish('w')
        os.replace(self.files('w')['state'],
            os.path.join(self.root, 'hidden-w.json'))
        old, count = self.digest('w'), self.markers()
        self.assertEqual(sorted(old),
            ['w.jsonl', 'w.last.md', 'w.runner.log', 'w.stderr.txt'])
        result = self.start('w', self.directive())
        self.assert_error(result, 'files-without-state')
        self.assertEqual(self.digest('w'), old)
        result = self.start('w', self.directive(), '--replace')
        self.assertEqual(result.code, 0, result)
        self.assert_archived_whole('w', old)
        self.assert_done('w')
        self.assertEqual(self.markers(), count + 1)

    def test_archive_failure_moves_nothing(self):
        self.finish('a')
        old, count = self.digest('a'), self.markers()
        self.hook({'name': 'a', 'fail_replace': {'suffix': '.last.md'},
            'count': 1000})
        result = self.start('a', self.directive(), '--replace',
            env={'CODEX_LEAF_RETRY': '0.3'})
        self.assert_error(result, 'archive-failed')
        self.assertIn('could not archive %s: ' % self.files('a')['last'],
            result.err)
        self.assertIn('; nothing was moved', result.err)
        self.assertEqual(self.digest('a'), old)
        self.assertEqual(self.archived('a'), [])
        self.assertEqual(self.markers(), count)
        self.hook()
        result = self.start('a', self.directive(), '--replace')
        self.assertEqual(result.code, 0, result)
        self.assert_archived_whole('a', old)
        self.assert_done('a')
        self.assertEqual(self.markers(), count + 1)


class DiagnosticsTest(LeafCase):
    def test_runner_exception_is_kept_apart_from_codex_stderr(self):
        gate = self.gate('x-launched')
        self.hook({'name': 'x', 'pause': 'launched', 'gate': gate})
        result = self.start('x', self.directive(), settle=False)
        self.assertEqual(result.code, 0, result)
        runner = self.paused(gate)[0]
        self.force('x', effort=None)
        self.hook()
        self.release(gate)
        self.assertEqual(self.runner_exited('x'), 'TypeError')
        self.until(lambda: not self.runner_alive(runner),
            what='the runner to exit')
        result = self.assert_failed(
            'x', 'runner lost; transcript has no completed turn')
        lines = result.out.splitlines()
        logged = [line for line in lines if line.startswith('  > ')]
        self.assertTrue([line for line in logged if 'TypeError' in line],
            result)
        self.assertTrue(logged[-1].endswith(
            'runner %d exiting (TypeError)' % runner), logged)
        self.assertEqual([line for line in lines if line.startswith('  ! ')],
            [])
        self.assertIn('  runner: launch=%s lost' % self.state('x')['launch'],
            lines)
        self.assertNotIn('TypeError', self.read(self.files('x')['stderr']))
        self.assertEqual(self.markers(), 0)

    def test_failed_report_keeps_runner_and_codex_lines_apart(self):
        directive = self.directive(turn='failed', exit=1,
            stderr='codex says boom\n')
        result = self.start('f', directive)
        self.assertEqual(result.code, 0, result)
        launch = json.loads(result.out)['launch']
        if WINDOWS:
            self.assertIn(launch, ('breakaway', 'no-breakaway'))
            self.assertIsInstance(self.state('f')['runner_in_job'], bool)
        else:
            self.assertEqual(launch, 'session')
        self.assertEqual(self.runner_exited('f'), 'exit 0')
        result = self.assert_failed(
            'f', 'codex exited with 1: turn failed: 429 Too Many Requests')
        state = self.state('f')
        self.assertEqual(state['launch'], launch)
        lines = result.out.splitlines()
        codex = [line for line in lines if line.startswith('  ! ')]
        logged = [line for line in lines if line.startswith('  > ')]
        heading = '  runner: launch=' + launch
        self.assertEqual(codex, ['  ! codex says boom'])
        self.assertIn(heading, lines)
        self.assertLess(lines.index(codex[0]), lines.index(heading))
        self.assertLess(lines.index(heading), lines.index(logged[0]))
        runner = state['runner_pid']
        self.assertTrue([line for line in logged if line.endswith(
            'runner %d started (launch %s)' % (runner, launch))], logged)
        self.assertTrue(logged[-1].endswith(
            'runner %d exiting (exit 0)' % runner), logged)
        self.assertNotIn('boom', '\n'.join(logged))


class ObservedSequenceTest(LeafCase):
    FLAGS = ('--resume', 'a', '--sandbox', 'workspace-write',
        '--no-project-doc', '--effort', 'xhigh')

    def test_incident_sequence(self):
        self.finish('a')
        thread = self.state('a')['thread_id']
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.lose_runner('b', directive, 'spawned', *self.FLAGS)
        marker = self.marker(directive)
        self.assertEqual(marker['argv'][1:3], ['exec', 'resume'])
        self.assertEqual(marker['thread'], thread)
        self.assertNotIn('codex_pid', self.state('b'))
        self.outlast_grace()
        line = self.assert_still_running('b', self.wait('b', timeout=1.5))
        self.assertIn(' runner-lost', line)
        self.assertIn(marker['pid'], listed_pids(line))
        before, count = self.digest('b'), self.markers()
        result = self.start('b', self.directive(), *self.FLAGS, '--replace')
        pids = self.assert_error(result, 'still-running')
        self.assertIn(marker['pid'], pids)
        self.assertEqual(self.digest('b'), before)
        self.assertEqual(self.markers(), count)
        self.release(hold)
        self.assert_done('b')
        state = self.state('b')
        self.assertIs(state['runner_lost'], True)
        self.assertEqual(state['thread_id'], thread)
        old = self.digest('b')
        replacement = self.directive()
        result = self.start('b', replacement, *self.FLAGS, '--replace')
        self.assertEqual(result.code, 0, result)
        self.assert_archived_whole('b', old)
        self.assert_done('b')
        self.assertEqual(self.markers(), count + 1)
        self.assertEqual(self.marker(replacement)['thread'], thread)


@unittest.skipUnless(WINDOWS,
    'needs Windows job objects and file sharing semantics')
class WindowsTest(LeafCase):
    KILL_ON_JOB_CLOSE = 0x2000

    def test_job_lists_the_chain_after_runner_loss(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        runner = self.lose_runner('g', directive, 'spawned')
        codex = self.paused(os.path.join(self.root, 'gates', 'g-spawned'))[1]
        marker = self.marker(directive)
        state = self.state('g')
        self.assertNotIn('codex_pid', state)
        self.assertEqual(state['job'], codex_leaf.job_name(state['run_id']))
        pids = codex_leaf.job_pids(state['job'])
        self.assertIn(codex, pids)
        self.assertIn(marker['pid'], pids)
        self.assertNotIn(runner, pids)
        self.assertEqual(image_name(codex).lower(), 'cmd.exe')
        self.assertEqual(image_name(marker['pid']).lower(),
            os.path.basename(sys.executable).lower())
        result = self.stop('g')
        self.assertEqual((result.code, result.out), (0, 'g: stopped\n'),
            result)
        self.assertEqual(codex_leaf.job_pids(state['job']), [])
        self.until(lambda: not self.fake_alive(directive),
            what='the fake to end')
        self.assertEqual(self.token('g'), 'free')

    def test_plain_reader_is_not_a_writer(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        self.lose_runner('p', directive, 'recorded')
        files = codex_leaf.paths(self.run_dir, 'p')
        with open(files['jsonl'], 'rb'):
            self.assertEqual(codex_leaf.transcript_writer(files['jsonl']),
                ('held', None))
            self.release(hold)
            self.assert_done('p')
            self.assertEqual(codex_leaf.transcript_writer(files['jsonl']),
                ('free', None))
            state = self.state('p')
            self.assertIs(state['runner_lost'], True)
            self.assertEqual(codex_leaf.leaf_activity(state, files),
                codex_leaf.Activity('gone', [], None, None))

    def test_state_write_survives_a_sharing_violation(self):
        gate = self.gate('v-spawned')
        self.hook({'name': 'v', 'pause': 'spawned', 'gate': gate})
        result = self.start('v', self.directive(),
            env={'CODEX_LEAF_RETRY': '3'})
        self.assertEqual(result.code, 0, result)
        runner, codex = self.paused(gate)
        self.hook()
        denial = '%d v.json.tmp' % runner
        with open(self.files('v')['state'], 'rb'):
            self.release(gate)
            self.until(lambda: denial in self.denied(),
                what='the runner to meet the sharing violation')
            self.assertNotIn('codex_pid', self.state('v'))
        self.assert_done('v')
        state = self.state('v')
        self.assertEqual(state['codex_pid'], codex)
        self.assertNotIn('runner_lost', state)
        self.assertEqual(self.runner_exited('v'), 'exit 0')
        log = self.read(self.files('v')['runner_log'])
        self.assertNotIn('state write failed', log)
        self.assertNotIn('Traceback', log)

    def test_archive_sharing_violation_moves_nothing(self):
        self.finish('r')
        old, count = self.digest('r'), self.markers()
        last = self.files('r')['last']
        with open(last, 'rb'):
            result = self.start('r', self.directive(), '--replace',
                env={'CODEX_LEAF_RETRY': '0.3'})
            self.assert_error(result, 'archive-failed')
            self.assertIn('could not archive %s: ' % last, result.err)
            self.assertIn('; nothing was moved', result.err)
            self.assertEqual(self.digest('r'), old)
            self.assertEqual(self.archived('r'), [])
            self.assertEqual(self.markers(), count)
            self.assertTrue([line for line in self.denied()
                if line.endswith(' r.last.md')], self.denied())
        result = self.start('r', self.directive(), '--replace')
        self.assertEqual(result.code, 0, result)
        self.assert_archived_whole('r', old)
        self.assert_done('r')
        self.assertEqual(self.markers(), count + 1)

    def start_in_caller_job(self, name, directive, flags):
        caller = os.path.join(self.root, 'caller.py')
        write(caller, CALLER)
        reply = os.path.join(self.root, 'caller-%s.json' % name)
        gate = self.gate(name + '-caller')
        self.leaves.append(name)
        process = subprocess.Popen(
            [sys.executable, '-B', caller, str(HELPER.parent), str(flags),
                reply, gate, str(HELPER), 'start', '--run-dir', self.run_dir,
                '--name', name, '--cwd', self.cwd,
                '--prompt-file', self.prompt(directive)],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, env=self.env)
        self.addCleanup(self.end_process, process, gate)
        deadline = time.time() + 60
        while not self.load(reply) and time.time() < deadline:
            time.sleep(POLL)
        result = self.load(reply)
        self.assertTrue(result, 'no reply from the caller job: %r, %r' % (
            self.state(name), self.read(self.files(name)['runner_log'])))
        self.assertEqual(result['code'], 0, result)
        return process, gate, result

    def test_caller_job_without_breakaway(self):
        hold = self.gate('hold')
        process, gate, reply = self.start_in_caller_job(
            'n', self.directive(hold=hold), 0)
        self.assertEqual(json.loads(reply['out'])['launch'], 'no-breakaway')
        self.assertIs(reply['inside'], True)
        self.release(hold)
        self.end_process(process, gate)
        self.assert_done('n')
        state = self.state('n')
        self.assertEqual(state['launch'], 'no-breakaway')
        self.assertIs(state['runner_in_job'], True)
        self.assertRegex(self.read(self.files('n')['runner_log']),
            r'breakaway launch failed: .+; launching without breakaway')

    def test_caller_job_with_breakaway(self):
        if not breakaway_allowed():
            self.skipTest('this test process sits in a job that forbids '
                'breakaway')
        hold = self.gate('hold')
        process, gate, reply = self.start_in_caller_job(
            'b', self.directive(hold=hold),
            codex_leaf.JOB_OBJECT_LIMIT_BREAKAWAY_OK)
        self.assertEqual(json.loads(reply['out'])['launch'], 'breakaway')
        self.assertIs(reply['inside'], False)
        self.release(hold)
        self.end_process(process, gate)
        self.assert_done('b')
        state = self.state('b')
        self.assertEqual(state['launch'], 'breakaway')
        self.assertIsInstance(state['runner_in_job'], bool)
        self.assertNotIn('breakaway launch failed',
            self.read(self.files('b')['runner_log']))

    def test_caller_job_closed_while_codex_runs(self):
        hold = self.gate('hold')
        directive = self.directive(hold=hold)
        process, gate, reply = self.start_in_caller_job(
            'k', directive, self.KILL_ON_JOB_CLOSE)
        self.assertEqual(json.loads(reply['out'])['launch'], 'no-breakaway')
        self.assertIs(reply['inside'], True)
        self.wait_marker(directive)
        runner = self.state('k')['runner_pid']
        self.end_process(process, gate)
        self.until(lambda: not self.runner_alive(runner)
            and not self.fake_alive(directive),
            what='the caller job to end the leaf')
        result = self.assert_failed(
            'k', 'runner lost; transcript has no completed turn')
        self.assertIn('  runner: launch=no-breakaway lost',
            result.out.splitlines())
        self.assertIs(self.state('k')['runner_lost'], True)


if __name__ == '__main__':
    unittest.main()
