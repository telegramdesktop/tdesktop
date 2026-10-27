#!/usr/bin/env python3
"""Run Codex CLI leaves from a non-Codex host and wait for them.

The contract is .agents/shared/codex-cli-leaves.md. Each leaf runs detached,
so a host whose tool calls are time-limited can start several leaves at once
and block on them with repeated bounded `wait` calls.

  codex_leaf.py check [--home work|default]
  codex_leaf.py start --run-dir DIR --name NAME --cwd DIR --prompt-file FILE
      [--home work|default]
      [--sandbox workspace-write|read-only] [--effort xhigh] [--model MODEL]
      [--resume LEAF] [--output-schema FILE] [--no-project-doc]
      [--max-minutes 120] [--replace]
  codex_leaf.py wait --run-dir DIR [--name NAME ...] [--timeout 540]
  codex_leaf.py stop --run-dir DIR --name NAME

Every leaf keeps NAME.json (state), NAME.jsonl (event transcript),
NAME.stderr.txt (Codex's stderr), NAME.last.md (the final message),
NAME.runner.log (the runner's own launch and exit diagnostics) and NAME.lock
(serializes state changes) in the run directory.
"""

import argparse
import collections
import contextlib
import functools
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import traceback
import uuid

SANDBOXES = ('workspace-write', 'read-only')
HOMES = ('work', 'default')
WORK_HOME = os.path.join('~', '.codex-accounts', 'work')
RUNNING = ('starting', 'running')
TERMINAL = ('done', 'failed', 'stopped')
ARCHIVED = ('jsonl', 'stderr', 'last', 'runner_log', 'state')
WINDOWS = (os.name == 'nt')
CODEX_APP_MAC = '/Applications/Codex.app/Contents/Resources/codex'
CODEX_APP_PACKAGES = ('Software\\Classes\\Local Settings\\Software\\Microsoft'
    '\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages')
FRESHNESS = 2
LOCK_TIMEOUT = 60
KILL_GRACE = 10

JOB_OBJECT_QUERY = 0x4
JOB_OBJECT_TERMINATE = 0x8
JOB_OBJECT_LIMIT_BREAKAWAY_OK = 0x800
JOB_PROCESS_ID_LIST = 3
JOB_EXTENDED_LIMITS = 9
GENERIC_READ = 0x80000000
FILE_SHARE_READ = 0x1
FILE_SHARE_DELETE = 0x4
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
PROCESS_TERMINATE = 0x1
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
ERROR_FILE_NOT_FOUND = 2
ERROR_PATH_NOT_FOUND = 3
ERROR_SHARING_VIOLATION = 32
ERROR_INVALID_PARAMETER = 87
ERROR_MORE_DATA = 234

Activity = collections.namedtuple('Activity', 'verdict pids runner detail')


class LeafError(Exception):
    def __init__(self, reason, text):
        super().__init__(reason + ': ' + text)
        self.reason = reason
        self.text = text


def fail(text):
    print('codex_leaf: ' + text, file=sys.stderr)
    sys.exit(1)


def now():
    return time.time()


def stamp():
    return time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())


def log(text):
    try:
        sys.stderr.write('%s %s\n' % (stamp(), text))
        sys.stderr.flush()
    except (OSError, ValueError):
        pass


def setting(name, default):
    try:
        return float(os.environ['CODEX_LEAF_' + name])
    except (KeyError, ValueError):
        return default


def codex_binary():
    explicit = os.environ.get('CODEX_BIN')
    if explicit:
        return explicit
    names = ('codex.exe', 'codex.cmd', 'codex.bat') if WINDOWS else ('codex',)
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return codex_app_binary()


def codex_app_binary():
    # The Codex app bundles the CLI, but on Windows the package folder is
    # renamed by every app update, so it cannot stay on PATH.
    if not WINDOWS:
        return CODEX_APP_MAC if os.access(CODEX_APP_MAC, os.X_OK) else None
    import winreg
    found = []
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CODEX_APP_PACKAGES) as packages:
            for index in range(winreg.QueryInfoKey(packages)[0]):
                name = winreg.EnumKey(packages, index)
                parts = name.split('_')
                if (len(parts) != 5 or parts[0] != 'OpenAI.Codex'
                        or parts[4] != '2p2nqsd0c76g0'):
                    continue
                with winreg.OpenKey(packages, name) as package:
                    root = winreg.QueryValueEx(package, 'PackageRootFolder')[0]
                binary = os.path.join(root, 'app', 'resources', 'codex.exe')
                if os.path.isfile(binary):
                    version = tuple(int(x) for x in parts[1].split('.'))
                    found.append((version, binary))
    except (OSError, ValueError):
        return None
    return max(found)[1] if found else None


def codex_home(home):
    # `work` is a Codex login kept apart from the user's own ~/.codex;
    # `default` leaves CODEX_HOME to the environment.
    return os.path.expanduser(WORK_HOME) if home == 'work' else None


def missing_home(home):
    if home and not os.path.isdir(home):
        return ('codex home %s does not exist; log in with CODEX_HOME=%s '
            'codex login --device-auth' % (home, home))
    return None


def codex_environment(home):
    environment = dict(os.environ)
    if home:
        environment['CODEX_HOME'] = home
    return environment


def paths(run_dir, name):
    base = os.path.join(run_dir, name)
    return {
        'state': base + '.json',
        'jsonl': base + '.jsonl',
        'stderr': base + '.stderr.txt',
        'last': base + '.last.md',
        'runner_log': base + '.runner.log',
        'lock': base + '.lock',
    }


def try_lock(fd):
    if WINDOWS:
        import msvcrt
        os.lseek(fd, 0, os.SEEK_SET)
        try:
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
        except PermissionError:
            return False
        return True
    import fcntl
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return False
    return True


def unlock(fd):
    if WINDOWS:
        import msvcrt
        try:
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
        except OSError:
            pass


@contextlib.contextmanager
def leaf_lock(run_dir, name):
    path = paths(run_dir, name)['lock']
    try:
        fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
    except OSError as e:
        raise LeafError('state-lock-failed', 'could not open %s: %s' % (path, e))
    try:
        deadline = now() + LOCK_TIMEOUT
        while True:
            try:
                if try_lock(fd):
                    break
            except OSError as e:
                raise LeafError(
                    'state-lock-failed', 'could not lock %s: %s' % (path, e))
            if now() >= deadline:
                raise LeafError(
                    'state-lock-busy',
                    '%s is still locked after %gs' % (path, LOCK_TIMEOUT))
            time.sleep(0.02)
        try:
            yield
        finally:
            unlock(fd)
    finally:
        os.close(fd)


def read_state(run_dir, name):
    try:
        with open(paths(run_dir, name)['state'], encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def replace_file(source, target):
    deadline = now() + setting('RETRY', 10)
    delay = 0.02
    while True:
        try:
            os.replace(source, target)
            return
        except PermissionError:
            if now() >= deadline:
                raise
        time.sleep(min(delay, max(0, deadline - now())))
        delay = min(delay * 2, 0.5)


def write_state(run_dir, name, state):
    target = paths(run_dir, name)['state']
    temporary = target + '.tmp'
    try:
        with open(temporary, 'w', encoding='utf-8') as f:
            json.dump(state, f, indent=1)
        replace_file(temporary, target)
    except OSError as e:
        raise LeafError(
            'state-write-failed', 'could not write %s: %s' % (target, e))


def update_state(run_dir, name, change):
    with leaf_lock(run_dir, name):
        state = read_state(run_dir, name)
        changed = change(state)
        if changed is None:
            return state
        write_state(run_dir, name, changed)
        return changed


def ended(state, status, reason=None, finished=None):
    result = dict(state, status=status)
    if reason:
        result['reason'] = reason
    result['finished'] = finished or now()
    started = state.get('started') or result['finished']
    result['minutes'] = round((result['finished'] - started) / 60, 1)
    return result


def transcript_events(path):
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return None
    events = []
    for line in data.split(b'\n')[:-1]:
        line = line.strip()
        if not line.startswith(b'{'):
            continue
        try:
            event = json.loads(line.decode('utf-8', 'replace'))
        except ValueError:
            continue
        if isinstance(event, dict):
            events.append(event)
    return events


def thread_id(jsonl_path):
    for event in transcript_events(jsonl_path) or ():
        if event.get('type') == 'thread.started':
            return event.get('thread_id')
    return None


def turn_failure(event):
    error = event.get('error')
    message = error.get('message') if isinstance(error, dict) else None
    return ('turn failed: ' + str(message)) if message else 'turn failed'


def judge(state, files, exit_code=None):
    events = transcript_events(files['jsonl'])
    if events is None:
        return 'failed', 'cannot read the transcript'
    turns = [event for event in events if event.get('type') in (
        'turn.started', 'turn.completed', 'turn.failed')]
    last = turns[-1] if turns else {}
    failure = turn_failure(last) if last.get('type') == 'turn.failed' else None
    if exit_code not in (None, 0):
        reason = 'codex exited with %s' % exit_code
        return 'failed', (reason + ': ' + failure) if failure else reason
    if failure:
        return 'failed', failure
    if last.get('type') != 'turn.completed':
        reason = 'transcript has no completed turn'
        errors = [event for event in events if event.get('type') == 'error']
        if errors:
            reason += ' (last error: %s)' % errors[-1].get('message', '')
        return 'failed', reason
    try:
        final = os.stat(files['last'])
    except OSError:
        return 'failed', 'codex produced no final message'
    if final.st_size == 0:
        return 'failed', 'codex produced no final message'
    if final.st_mtime < (state.get('started') or 0) - FRESHNESS:
        return 'failed', 'final message predates the leaf'
    return 'done', None


def transcript_writer(path):
    if WINDOWS:
        return windows_transcript_writer(path)
    import fcntl
    try:
        fd = os.open(path, os.O_RDONLY)
    except FileNotFoundError:
        return 'missing', None
    except OSError as e:
        return 'error', 'cannot probe %s: %s' % (os.path.basename(path), e)
    try:
        fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
    except BlockingIOError:
        return 'held', None
    except OSError as e:
        return 'error', 'cannot probe %s: %s' % (os.path.basename(path), e)
    finally:
        os.close(fd)
    return 'free', None


def group_status(pgid):
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return 'empty'
    except OSError:
        return 'error'
    return 'exists'


def group_members(pgid):
    try:
        listing = subprocess.run(
            ['ps', '-A', '-o', 'pid=,pgid=,stat='],
            capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return []
    if listing.returncode != 0:
        return []
    members = []
    for line in listing.stdout.splitlines():
        fields = line.split()
        if (len(fields) == 3
                and fields[1] == str(pgid)
                and 'Z' not in fields[2]
                and fields[0] != str(os.getpid())):
            members.append(int(fields[0]))
    return members


def posix_runner_alive(state):
    pid = state.get('runner_pid')
    if not pid:
        return False
    try:
        return os.getpgid(pid) == state.get('pgid')
    except OSError:
        return False


@functools.lru_cache(maxsize=None)
def kernel32():
    import ctypes
    library = ctypes.WinDLL('kernel32', use_last_error=True)
    handle, dword, boolean = ctypes.c_void_p, ctypes.c_ulong, ctypes.c_long
    signatures = (
        ('CreateJobObjectW', handle, (ctypes.c_void_p, ctypes.c_wchar_p)),
        ('OpenJobObjectW', handle, (dword, boolean, ctypes.c_wchar_p)),
        ('SetInformationJobObject', boolean,
            (handle, ctypes.c_int, ctypes.c_void_p, dword)),
        ('AssignProcessToJobObject', boolean, (handle, handle)),
        ('IsProcessInJob', boolean,
            (handle, handle, ctypes.POINTER(boolean))),
        ('QueryInformationJobObject', boolean,
            (handle, ctypes.c_int, ctypes.c_void_p, dword,
                ctypes.POINTER(dword))),
        ('TerminateJobObject', boolean, (handle, ctypes.c_uint)),
        ('DuplicateHandle', boolean,
            (handle, handle, handle, ctypes.POINTER(handle), dword, boolean,
                dword)),
        ('GetCurrentProcess', handle, ()),
        ('OpenProcess', handle, (dword, boolean, dword)),
        ('TerminateProcess', boolean, (handle, ctypes.c_uint)),
        ('CreateFileW', handle,
            (ctypes.c_wchar_p, dword, dword, ctypes.c_void_p, dword, dword,
                handle)),
        ('CloseHandle', boolean, (handle,)),
    )
    for name, result, arguments in signatures:
        function = getattr(library, name)
        function.restype = result
        function.argtypes = arguments
    return library


def windows_error():
    import ctypes
    return ctypes.WinError(ctypes.get_last_error())


def windows_transcript_writer(path):
    import ctypes
    library = kernel32()
    handle = library.CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, None,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, None)
    if not handle or handle == ctypes.c_void_p(-1).value:
        error = ctypes.get_last_error()
        if error == ERROR_SHARING_VIOLATION:
            return 'held', None
        if error in (ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND):
            return 'missing', None
        return 'error', 'cannot probe %s: %s' % (
            os.path.basename(path), ctypes.WinError(error))
    library.CloseHandle(handle)
    return 'free', None


def job_name(run_id):
    return 'Local\\codex_leaf_' + run_id


def set_job_limits(job, flags):
    import ctypes

    class BasicLimits(ctypes.Structure):
        _fields_ = [
            ('PerProcessUserTimeLimit', ctypes.c_longlong),
            ('PerJobUserTimeLimit', ctypes.c_longlong),
            ('LimitFlags', ctypes.c_ulong),
            ('MinimumWorkingSetSize', ctypes.c_size_t),
            ('MaximumWorkingSetSize', ctypes.c_size_t),
            ('ActiveProcessLimit', ctypes.c_ulong),
            ('Affinity', ctypes.c_size_t),
            ('PriorityClass', ctypes.c_ulong),
            ('SchedulingClass', ctypes.c_ulong),
        ]

    class IoCounters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in (
            'ReadOperationCount', 'WriteOperationCount',
            'OtherOperationCount', 'ReadTransferCount',
            'WriteTransferCount', 'OtherTransferCount')]

    class ExtendedLimits(ctypes.Structure):
        _fields_ = [
            ('BasicLimitInformation', BasicLimits),
            ('IoInfo', IoCounters),
            ('ProcessMemoryLimit', ctypes.c_size_t),
            ('JobMemoryLimit', ctypes.c_size_t),
            ('PeakProcessMemoryUsed', ctypes.c_size_t),
            ('PeakJobMemoryUsed', ctypes.c_size_t),
        ]

    limits = ExtendedLimits()
    limits.BasicLimitInformation.LimitFlags = flags
    return bool(kernel32().SetInformationJobObject(
        job, JOB_EXTENDED_LIMITS, ctypes.byref(limits),
        ctypes.sizeof(limits)))


def job_failure(api):
    return LeafError('job-failed', '%s: %s' % (api, windows_error()))


def make_job(run_id):
    import ctypes
    library = kernel32()
    current = library.GetCurrentProcess()
    job = library.CreateJobObjectW(None, job_name(run_id))
    if not job:
        raise job_failure('CreateJobObjectW')
    in_job = ctypes.c_long()
    inheritable = ctypes.c_void_p()
    try:
        if not set_job_limits(job, JOB_OBJECT_LIMIT_BREAKAWAY_OK):
            raise job_failure('SetInformationJobObject')
        if not library.IsProcessInJob(current, None, ctypes.byref(in_job)):
            raise job_failure('IsProcessInJob')
        if not library.AssignProcessToJobObject(job, current):
            raise job_failure('AssignProcessToJobObject')
        if not library.DuplicateHandle(
                current, job, current, ctypes.byref(inheritable),
                JOB_OBJECT_QUERY, True, 0):
            raise job_failure('DuplicateHandle')
    except BaseException:
        library.CloseHandle(job)
        raise
    return job, inheritable.value, bool(in_job.value)


def query_job_pids(job):
    import ctypes
    count = 256
    for _ in range(2):
        class ProcessIds(ctypes.Structure):
            _fields_ = [
                ('NumberOfAssignedProcesses', ctypes.c_ulong),
                ('NumberOfProcessIdsInList', ctypes.c_ulong),
                ('ProcessIdList', ctypes.c_size_t * count),
            ]
        listing = ProcessIds()
        if kernel32().QueryInformationJobObject(
                job, JOB_PROCESS_ID_LIST, ctypes.byref(listing),
                ctypes.sizeof(listing), None):
            break
        error = ctypes.get_last_error()
        if error != ERROR_MORE_DATA:
            raise ctypes.WinError(error)
        count = listing.NumberOfAssignedProcesses + 16
    return list(listing.ProcessIdList[:listing.NumberOfProcessIdsInList])


def open_job(name, access):
    import ctypes
    job = kernel32().OpenJobObjectW(access, False, name)
    if not job:
        error = ctypes.get_last_error()
        if error != ERROR_FILE_NOT_FOUND:
            raise ctypes.WinError(error)
    return job


def job_pids(name):
    job = open_job(name, JOB_OBJECT_QUERY)
    if not job:
        return []
    try:
        return query_job_pids(job)
    finally:
        kernel32().CloseHandle(job)


def terminate_job(name):
    job = open_job(name, JOB_OBJECT_TERMINATE)
    if not job:
        return
    try:
        if not kernel32().TerminateJobObject(job, 1):
            raise windows_error()
    finally:
        kernel32().CloseHandle(job)


def terminate_job_members(job, spare_pid):
    import ctypes
    library = kernel32()
    try:
        pids = query_job_pids(job)
    except OSError as e:
        return ['cannot list the job: %s' % e]
    failures = []
    for pid in pids:
        if pid == spare_pid:
            continue
        process = library.OpenProcess(
            PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not process:
            if ctypes.get_last_error() != ERROR_INVALID_PARAMETER:
                failures.append('pid %d: %s' % (pid, windows_error()))
            continue
        try:
            inside = ctypes.c_long()
            if not library.IsProcessInJob(process, job, ctypes.byref(inside)):
                failures.append('pid %d: %s' % (pid, windows_error()))
            elif inside.value and not library.TerminateProcess(process, 1):
                failures.append('pid %d: %s' % (pid, windows_error()))
        finally:
            library.CloseHandle(process)
    return failures


def posix_activity(state, files):
    writer, problem = transcript_writer(files['jsonl'])
    pgid = state.get('pgid')
    tracked = bool(state.get('run_id') and pgid)
    if writer == 'held':
        if not tracked:
            return Activity('alive', [], None, None)
        return Activity(
            'alive', group_members(pgid), posix_runner_alive(state), None)
    if writer != 'error' and tracked:
        return Activity('gone', [], None, None)
    candidate = pgid or state.get('runner_pid')
    if not candidate:
        if writer == 'error':
            return Activity('unresolved', [], None, problem)
        return Activity('gone', [], None, None)
    group = group_status(candidate)
    if group == 'empty':
        return Activity('gone', [], None, None)
    if writer == 'error':
        detail = problem
    elif group == 'exists':
        detail = 'process group %d still exists' % candidate
    else:
        detail = 'cannot probe process group %d' % candidate
    return Activity('unresolved', group_members(candidate), None, detail)


def windows_activity(state, files):
    failure = None
    if state.get('job'):
        try:
            pids = job_pids(state['job'])
        except OSError as e:
            pids, failure = [], 'cannot query the job: %s' % e
        if pids:
            return Activity('alive', pids, state.get('runner_pid') in pids, None)
    writer, problem = transcript_writer(files['jsonl'])
    if writer == 'held':
        return Activity('alive', [], None, '%s is open for writing outside '
            'the leaf\'s job' % os.path.basename(files['jsonl']))
    if failure or writer == 'error':
        return Activity('unresolved', [], None, failure or problem)
    return Activity('gone', [], None, None)


def leaf_activity(state, files):
    if WINDOWS:
        return windows_activity(state, files)
    return posix_activity(state, files)


def describe_pids(activity):
    if not activity.pids:
        return 'pid unknown'
    return 'pid ' + ', '.join(str(pid) for pid in activity.pids)


def signal_group(pgid, number):
    try:
        os.killpg(pgid, number)
    except OSError:
        pass


def settle(state, files):
    deadline = now() + KILL_GRACE
    while True:
        activity = leaf_activity(state, files)
        if activity.verdict == 'gone' or now() >= deadline:
            return activity
        time.sleep(0.05)


def terminate_leaf(state, files):
    activity = leaf_activity(state, files)
    pgid = state.get('pgid')
    tracked = bool(state.get('run_id') and pgid)
    if activity.verdict == 'gone':
        return activity
    if WINDOWS and state.get('job'):
        try:
            terminate_job(state['job'])
        except OSError as e:
            return activity._replace(detail='cannot end the job: %s' % e)
    elif not WINDOWS and activity.verdict == 'alive' and tracked:
        signal_group(pgid, signal.SIGTERM)
    else:
        return activity
    activity = settle(state, files)
    if not WINDOWS and activity.verdict == 'alive' and tracked:
        signal_group(pgid, signal.SIGKILL)
        activity = settle(state, files)
    return activity


def command_check(args):
    binary = codex_binary()
    if not binary:
        print(json.dumps({
            'ok': False,
            'reason': 'codex is neither on PATH nor in the Codex app',
        }))
        return 1
    home = codex_home(args.home)
    missing = missing_home(home)
    if missing:
        print(json.dumps({
            'ok': False, 'codex': binary, 'home': home, 'reason': missing}))
        return 1
    environment = codex_environment(home)
    flags = subprocess.CREATE_NO_WINDOW if WINDOWS else 0
    try:
        version = subprocess.run(
            [binary, '--version'],
            capture_output=True, text=True, encoding='utf-8', errors='replace',
            timeout=120, creationflags=flags, env=environment)
        login = subprocess.run(
            [binary, 'login', 'status'],
            capture_output=True, text=True, encoding='utf-8', errors='replace',
            timeout=120, creationflags=flags, env=environment)
    except (OSError, subprocess.TimeoutExpired) as e:
        print(json.dumps({'ok': False, 'codex': binary, 'reason': str(e)}))
        return 1
    status = (login.stdout + login.stderr).strip()
    result = {
        'ok': version.returncode == 0 and login.returncode == 0,
        'codex': binary,
        'home': home or 'default',
        'version': version.stdout.strip(),
        'login': status,
    }
    if not result['ok']:
        result['reason'] = (version.stderr.strip() or status)[-500:]
    print(json.dumps(result))
    return 0 if result['ok'] else 1


def resumable_thread(run_dir, source):
    files = paths(run_dir, source)
    missing = LeafError('resume-no-thread',
        'leaf %s has no recorded Codex thread to resume' % source)
    if not os.path.exists(files['state']):
        raise missing
    with leaf_lock(run_dir, source):
        state = read_state(run_dir, source)
        if not state:
            raise missing
        if state.get('status') in RUNNING:
            activity = leaf_activity(state, files)
            if activity.verdict != 'gone':
                raise LeafError('resume-source-running',
                    'leaf %s is still running (%s); wait for it before '
                    'resuming' % (source, describe_pids(activity)))
        thread = state.get('thread_id') or thread_id(files['jsonl'])
        # The thread lives in the source leaf's Codex home.
        home = state.get('codex_home')
    if not thread:
        raise missing
    return thread, home


def refuse_if_active(name, state, files):
    activity = leaf_activity(state, files)
    if activity.verdict == 'alive':
        raise LeafError('still-running',
            'leaf %s still has a running process (%s); wait for it or stop '
            'it first' % (name, describe_pids(activity)))
    if activity.verdict == 'unresolved':
        raise LeafError('unresolved',
            'cannot confirm that leaf %s has no running process (%s; %s)'
            % (name, activity.detail, describe_pids(activity)))


def archive(files, present):
    base = '.prev-' + time.strftime('%Y%m%d-%H%M%S')
    suffix, index = base, 0
    while any(os.path.lexists(files[key] + suffix) for key in present):
        index += 1
        suffix = '%s-%d' % (base, index)
    moved = []
    for key in ARCHIVED:
        if key not in present:
            continue
        try:
            replace_file(files[key], files[key] + suffix)
        except OSError as e:
            failure = 'could not archive %s: %s' % (files[key], e)
            stranded = []
            for back in reversed(moved):
                try:
                    replace_file(files[back] + suffix, files[back])
                except OSError as again:
                    stranded.append('%s stays at %s (%s)' % (
                        files[back], files[back] + suffix, again))
            raise LeafError('archive-failed', failure + '; ' + (
                '; '.join(stranded) if stranded else 'nothing was moved'))
        moved.append(key)
    return suffix


def launch_runner(run_dir, name, run_id, files):
    command = [sys.executable, os.path.abspath(__file__), '_run',
               '--run-dir', run_dir, '--name', name, '--run-id', run_id,
               '--launch']
    try:
        with open(files['runner_log'], 'ab') as output:
            options = {
                'stdin': subprocess.DEVNULL,
                'stdout': output,
                'stderr': output,
                'close_fds': True,
            }
            if not WINDOWS:
                subprocess.Popen(
                    command + ['session'], start_new_session=True, **options)
                return 'session'
            # CREATE_BREAKAWAY_FROM_JOB keeps the leaf alive after the calling
            # tool's job ends; a job that forbids breakaway rejects the flag.
            flags = (subprocess.DETACHED_PROCESS
                | subprocess.CREATE_NEW_PROCESS_GROUP
                | subprocess.CREATE_NO_WINDOW)
            try:
                subprocess.Popen(
                    command + ['breakaway'],
                    creationflags=flags | subprocess.CREATE_BREAKAWAY_FROM_JOB,
                    **options)
                return 'breakaway'
            except OSError as e:
                output.write(('%s breakaway launch failed: %s; launching '
                    'without breakaway\n' % (stamp(), e)).encode('utf-8'))
                output.flush()
            subprocess.Popen(
                command + ['no-breakaway'], creationflags=flags, **options)
            return 'no-breakaway'
    except OSError as e:
        reason = 'could not launch the runner: %s' % e

    def mark(state):
        if (state and state.get('run_id') == run_id
                and state.get('status') == 'starting'):
            return ended(state, 'failed', reason)
        return None

    update_state(run_dir, name, mark)
    raise LeafError('launch-failed', reason)


def command_start(args):
    run_dir = os.path.abspath(args.run_dir)
    if not os.path.isdir(args.cwd):
        fail('working directory does not exist: ' + args.cwd)
    if not os.path.isfile(args.prompt_file):
        fail('prompt file does not exist: ' + args.prompt_file)
    binary = codex_binary()
    if not binary:
        fail('codex is neither on PATH nor in the Codex app')
    resume_thread = None
    home = codex_home(args.home)
    if args.resume:
        if args.resume == args.name:
            raise LeafError('resume-self',
                'leaf %s cannot resume itself; start the continuation under '
                'another name' % args.name)
        resume_thread, home = resumable_thread(run_dir, args.resume)
    missing = missing_home(home)
    if missing:
        raise LeafError('home-missing', missing)
    try:
        os.makedirs(run_dir, exist_ok=True)
    except OSError as e:
        fail('could not create the run directory: %s' % e)
    files = paths(run_dir, args.name)
    run_id = uuid.uuid4().hex
    with leaf_lock(run_dir, args.name):
        existing = read_state(run_dir, args.name)
        present = [key for key in ARCHIVED if os.path.exists(files[key])]
        if existing or present:
            if not args.replace and existing:
                raise LeafError('exists', 'leaf %s already exists; pass '
                    '--replace to rerun it' % args.name)
            if not args.replace:
                raise LeafError('files-without-state', 'leaf %s has files but '
                    'no state; pass --replace to archive them' % args.name)
            refuse_if_active(args.name, existing or {}, files)
            archive(files, present)
        write_state(run_dir, args.name, {
            'name': args.name,
            'status': 'starting',
            'run_id': run_id,
            'codex': binary,
            'codex_home': home,
            'cwd': os.path.abspath(args.cwd),
            'prompt_file': os.path.abspath(args.prompt_file),
            'sandbox': args.sandbox,
            'effort': args.effort,
            'model': args.model,
            'output_schema': (args.output_schema
                and os.path.abspath(args.output_schema)),
            'no_project_doc': args.no_project_doc,
            'resume_of': args.resume,
            'resume_thread': resume_thread,
            'max_minutes': args.max_minutes,
            'started': now(),
        })
    launch = launch_runner(run_dir, args.name, run_id, files)
    print(json.dumps({
        'name': args.name, 'state': files['state'], 'launch': launch}))
    return 0


def codex_command(state, files):
    common = [
        '--json',
        '--skip-git-repo-check',
        '-o', files['last'],
        '-c', 'model_reasoning_effort=' + state['effort'],
    ]
    if state.get('model'):
        common += ['-m', state['model']]
    if state.get('no_project_doc'):
        common += ['-c', 'project_doc_max_bytes=0']
    if state.get('output_schema'):
        common += ['--output-schema', state['output_schema']]
    if state.get('resume_thread'):
        return [state['codex'], 'exec', 'resume'] + common + [
            '-c', 'sandbox_mode=' + state['sandbox'],
            state['resume_thread'], '-']
    return [state['codex'], 'exec'] + common + [
        '-C', state['cwd'], '-s', state['sandbox'], '-']


def claim_leaf(args, files, held):
    with leaf_lock(args.run_dir, args.name):
        state = read_state(args.run_dir, args.name)
        if (not state or state.get('run_id') != args.run_id
                or state.get('status') != 'starting'):
            log('nothing to start: the leaf is %s, not this run starting' % (
                'without a state' if not state else '%s for run %s' % (
                    state.get('status'), state.get('run_id'))))
            return None
        try:
            with contextlib.ExitStack() as opened:
                transcript = opened.enter_context(open(files['jsonl'], 'xb'))
                errors = opened.enter_context(open(files['stderr'], 'xb'))
                if WINDOWS:
                    job, inheritable, in_job = make_job(args.run_id)
                    opened.callback(kernel32().CloseHandle, job)
                    opened.callback(kernel32().CloseHandle, inheritable)
                else:
                    import fcntl
                    fcntl.flock(
                        transcript.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                    job = inheritable = None
                held.enter_context(opened.pop_all())
        except (OSError, LeafError) as e:
            reason = 'could not prepare the leaf: %s' % e
            log(reason)
            write_state(
                args.run_dir, args.name, ended(state, 'failed', reason))
            return None
        state = dict(state, status='running', runner_pid=os.getpid(),
            launch=args.launch)
        if WINDOWS:
            state.update(job=job_name(args.run_id), runner_in_job=in_job)
        else:
            state['pgid'] = os.getpgid(0)
        write_state(args.run_dir, args.name, state)
    return state, transcript, errors, job, inheritable


def launch_codex(state, command, transcript, errors, inheritable):
    environment = codex_environment(state.get('codex_home'))
    # The Windows sandbox runs commands as another user, and git refuses a
    # checkout owned by someone else unless the directory is marked safe.
    parameters = environment.get('GIT_CONFIG_PARAMETERS', '')
    environment['GIT_CONFIG_PARAMETERS'] = (
        parameters + " 'safe.directory=*'").strip()
    options = {}
    if WINDOWS:
        options['creationflags'] = subprocess.CREATE_NO_WINDOW
        options['startupinfo'] = subprocess.STARTUPINFO(
            lpAttributeList={'handle_list': [inheritable]})
    try:
        prompt = open(state['prompt_file'], 'rb')
    except OSError as e:
        return None, 'could not read the prompt: %s' % e
    with prompt:
        try:
            return subprocess.Popen(
                command,
                cwd=state['cwd'],
                stdin=prompt,
                stdout=transcript,
                stderr=errors,
                env=environment,
                **options), None
        except OSError as e:
            return None, 'could not start codex: %s' % e


def end_own_leaf(child, state, job):
    if WINDOWS:
        for failure in terminate_job_members(job, os.getpid()):
            log('could not end %s' % failure)
    else:
        # The runner leads the group it signals.
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal_group(state['pgid'], signal.SIGTERM)
    try:
        child.wait(KILL_GRACE)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait()


def supervise(args):
    files = paths(args.run_dir, args.name)
    with contextlib.ExitStack() as held:
        claimed = claim_leaf(args, files, held)
        if not claimed:
            return 1
        state, transcript, errors, job, inheritable = claimed
        command = codex_command(state, files)
        child, reason = launch_codex(
            state, command, transcript, errors, inheritable)
        exit_code = None
        if child:
            def record(current):
                if (current and current.get('run_id') == args.run_id
                        and current.get('status') in RUNNING):
                    return dict(current, codex_pid=child.pid, command=command)
                return None

            try:
                update_state(args.run_dir, args.name, record)
            except LeafError as e:
                log('state write failed: %s' % e)
            try:
                exit_code = child.wait(timeout=state['max_minutes'] * 60)
            except subprocess.TimeoutExpired:
                reason = 'exceeded %g minutes' % state['max_minutes']
                log('codex %s; ending the leaf' % reason)
                end_own_leaf(child, state, job)
                exit_code = child.returncode

        def outcome(current):
            if not current or current.get('run_id') != args.run_id:
                return None
            status = current.get('status')
            if status in TERMINAL and status != 'stopped':
                return None
            current = dict(current, exit_code=exit_code,
                thread_id=thread_id(files['jsonl']))
            if status == 'stopped':
                return current
            if reason:
                return ended(current, 'failed', reason)
            return ended(current, *judge(current, files, exit_code))

        try:
            update_state(args.run_dir, args.name, outcome)
        except LeafError as e:
            log('final state write failed: %s; wait will judge the leaf' % e)
    return 0


def command_run(args):
    log('runner %d started (launch %s)' % (os.getpid(), args.launch))
    result = 'interrupted'
    try:
        code = supervise(args)
        result = 'exit %d' % code
        return code
    except LeafError as e:
        result = e.reason
        log(str(e))
        return 1
    except Exception as e:
        result = e.__class__.__name__
        log(traceback.format_exc().rstrip())
        return 1
    finally:
        log('runner %d exiting (%s)' % (os.getpid(), result))


def leaf_names(run_dir, names):
    if names:
        return names
    found = []
    for entry in sorted(os.listdir(run_dir)):
        if entry.endswith('.json'):
            found.append(entry[:-len('.json')])
    return found


def lost_outcome(state, files):
    status, reason = judge(state, files)
    stamps = []
    for key in ('jsonl', 'last'):
        try:
            stamps.append(os.stat(files[key]).st_mtime)
        except OSError:
            pass
    result = ended(state, status, reason and 'runner lost; ' + reason,
        max(stamps) if stamps else None)
    result['runner_lost'] = True
    result['thread_id'] = thread_id(files['jsonl'])
    return result


def check_leaf(run_dir, name):
    files = paths(run_dir, name)
    if not os.path.exists(files['state']):
        return None, None
    try:
        with leaf_lock(run_dir, name):
            state = read_state(run_dir, name)
            status = state and state.get('status')
            if status not in RUNNING:
                return state, None
            started = state.get('started') or 0
            grace = setting('START_GRACE', 30)
            if status == 'starting' and now() - started < grace:
                return state, None
            activity = leaf_activity(state, files)
            if activity.verdict == 'gone':
                if status == 'starting':
                    state = ended(state, 'failed',
                        'runner did not start within %gs' % grace)
                else:
                    state = lost_outcome(state, files)
                write_state(run_dir, name, state)
                return state, activity
            limit = state.get('max_minutes')
            if (status != 'running' or activity.runner is not False
                    or limit is None or now() - started <= limit * 60):
                return state, activity
            state = ended(state, 'failed',
                'exceeded %g minutes (runner lost; ended by wait)' % limit)
            state['runner_lost'] = True
            write_state(run_dir, name, state)
    except LeafError as e:
        return read_state(run_dir, name), Activity(
            'unresolved', [], None, str(e))
    return state, terminate_leaf(state, files)


def tail_lines(path, count):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            lines = [line for line in f.read().splitlines() if line.strip()]
    except OSError:
        return []
    return lines[-count:]


def describe(run_dir, name, state, activity, show_last):
    files = paths(run_dir, name)
    if not state:
        return [name + ': missing']
    status = state.get('status')
    parts = [name + ': ' + str(status)]
    if status in RUNNING:
        parts.append('%.1fm' % ((now() - state.get('started', now())) / 60))
        if activity and activity.runner is False:
            parts.append('runner-lost')
        if activity and activity.pids:
            parts.append('pid=' + ','.join(str(pid) for pid in activity.pids))
        if activity and activity.verdict == 'unresolved':
            parts.append('unresolved=' + str(activity.detail))
    else:
        if state.get('minutes') is not None:
            parts.append('%sm' % state['minutes'])
        if state.get('reason'):
            parts.append('reason=' + state['reason'])
        if os.path.isfile(files['last']):
            parts.append('last=' + files['last'])
    lines = [' '.join(parts)]
    if status == 'done' and show_last and os.path.isfile(files['last']):
        with open(files['last'], encoding='utf-8', errors='replace') as f:
            last = f.read().strip().splitlines()
        for line in last[:show_last]:
            lines.append('  | ' + line)
        if len(last) > show_last:
            lines.append('  | ... %d more lines' % (len(last) - show_last))
    if status == 'failed':
        for line in tail_lines(files['stderr'], 5):
            lines.append('  ! ' + line)
        lines.append('  runner: launch=%s%s' % (
            state.get('launch') or 'unknown',
            ' lost' if state.get('runner_lost') else ''))
        for line in tail_lines(files['runner_log'], 5):
            lines.append('  > ' + line)
    return lines


def command_wait(args):
    run_dir = os.path.abspath(args.run_dir)
    if not os.path.isdir(run_dir):
        fail('run directory does not exist: ' + run_dir)
    names = leaf_names(run_dir, args.name)
    if not names:
        fail('no leaves in ' + run_dir)
    deadline = now() + args.timeout
    while True:
        checked = {name: check_leaf(run_dir, name) for name in names}
        running = [name for name in names if checked[name][0]
            and checked[name][0].get('status') in RUNNING]
        if not running or now() >= deadline:
            for name in names:
                state, activity = checked[name]
                for line in describe(
                        run_dir, name, state, activity, args.show_last):
                    print(line)
            if running:
                print('WAITING: %d leaf(s) still running; wait again' % len(running))
                return 2
            print('FINISHED')
            return 0
        time.sleep(max(0.01, min(setting('POLL', 5), deadline - now())))


def command_stop(args):
    run_dir = os.path.abspath(args.run_dir)
    files = paths(run_dir, args.name)
    if not os.path.exists(files['state']):
        fail('no leaf ' + args.name)

    def mark(state):
        if state and state.get('status') in RUNNING:
            return ended(state, 'stopped')
        return None

    state = update_state(run_dir, args.name, mark)
    if not state:
        fail('no leaf ' + args.name)
    activity = terminate_leaf(state, files)
    status = state.get('status')
    if activity.verdict == 'gone':
        print('%s: %s' % (args.name, status))
        return 0
    detail = describe_pids(activity)
    if activity.detail:
        detail = activity.detail + '; ' + detail
    print('%s: %s; still running (%s)' % (args.name, status, detail))
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)

    check = commands.add_parser('check')
    check.add_argument('--home', choices=HOMES, default='work')

    start = commands.add_parser('start')
    start.add_argument('--run-dir', required=True)
    start.add_argument('--name', required=True)
    start.add_argument('--cwd', required=True)
    start.add_argument('--prompt-file', required=True)
    start.add_argument('--home', choices=HOMES, default='work')
    start.add_argument('--sandbox', choices=SANDBOXES, default='read-only')
    start.add_argument('--effort', default='xhigh')
    start.add_argument('--model')
    start.add_argument('--resume')
    start.add_argument('--output-schema')
    start.add_argument('--no-project-doc', action='store_true')
    start.add_argument('--max-minutes', type=float, default=120)
    start.add_argument('--replace', action='store_true')

    wait = commands.add_parser('wait')
    wait.add_argument('--run-dir', required=True)
    wait.add_argument('--name', action='append')
    wait.add_argument('--timeout', type=float, default=540)
    wait.add_argument('--show-last', type=int, default=8)

    stop = commands.add_parser('stop')
    stop.add_argument('--run-dir', required=True)
    stop.add_argument('--name', required=True)

    run = commands.add_parser('_run')
    run.add_argument('--run-dir', required=True)
    run.add_argument('--name', required=True)
    run.add_argument('--run-id', required=True)
    run.add_argument('--launch', default='session')

    args = parser.parse_args()
    handlers = {
        'check': command_check,
        'start': command_start,
        'wait': command_wait,
        'stop': command_stop,
        '_run': command_run,
    }
    try:
        code = handlers[args.command](args)
    except LeafError as e:
        fail(str(e))
    sys.exit(code)


if __name__ == '__main__':
    main()
