#!/usr/bin/env python3
"""Run Codex CLI leaves from a non-Codex host and wait for them.

The contract is .agents/shared/codex-cli-leaves.md. Each leaf runs detached,
so a host whose tool calls are time-limited can start several leaves at once
and block on them with repeated bounded `wait` calls.

  codex_leaf.py check
  codex_leaf.py start --run-dir DIR --name NAME --cwd DIR --prompt-file FILE
      [--sandbox workspace-write|read-only] [--effort xhigh] [--model MODEL]
      [--resume LEAF] [--output-schema FILE] [--no-project-doc]
      [--max-minutes 120] [--replace]
  codex_leaf.py wait --run-dir DIR [--name NAME ...] [--timeout 540]
  codex_leaf.py stop --run-dir DIR --name NAME

Every leaf keeps NAME.json (state), NAME.jsonl (event transcript),
NAME.stderr.txt and NAME.last.md (the final message) in the run directory.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

SANDBOXES = ('workspace-write', 'read-only')
RUNNING = ('starting', 'running')
WINDOWS = (os.name == 'nt')


def fail(text):
    print('codex_leaf: ' + text, file=sys.stderr)
    sys.exit(1)


def now():
    return time.time()


def codex_binary():
    explicit = os.environ.get('CODEX_BIN')
    if explicit:
        return explicit
    names = ('codex.exe', 'codex.cmd', 'codex.bat') if WINDOWS else ('codex',)
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def paths(run_dir, name):
    base = os.path.join(run_dir, name)
    return {
        'state': base + '.json',
        'jsonl': base + '.jsonl',
        'stderr': base + '.stderr.txt',
        'last': base + '.last.md',
    }


def read_state(run_dir, name):
    try:
        with open(paths(run_dir, name)['state'], encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_state(run_dir, name, state):
    target = paths(run_dir, name)['state']
    temporary = target + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as f:
        json.dump(state, f, indent=1)
    os.replace(temporary, target)


def pid_alive(pid):
    if not pid:
        return False
    if WINDOWS:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x1000, False, int(pid))
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return bool(ok) and code.value == 259
    try:
        os.kill(int(pid), 0)
    except OSError:
        return False
    return True


def kill_tree(pid):
    if not pid:
        return
    if WINDOWS:
        subprocess.run(
            ['taskkill', '/PID', str(pid), '/T', '/F'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
    else:
        import signal
        try:
            os.killpg(os.getpgid(int(pid)), signal.SIGTERM)
        except OSError:
            pass


def thread_id(jsonl_path):
    try:
        with open(jsonl_path, encoding='utf-8', errors='replace') as f:
            for line in f:
                line = line.strip()
                if not line.startswith('{'):
                    continue
                try:
                    event = json.loads(line)
                except ValueError:
                    continue
                if event.get('type') == 'thread.started':
                    return event.get('thread_id')
    except OSError:
        pass
    return None


def command_check(args):
    binary = codex_binary()
    if not binary:
        print(json.dumps({'ok': False, 'reason': 'codex is not on PATH'}))
        return 1
    flags = subprocess.CREATE_NO_WINDOW if WINDOWS else 0
    try:
        version = subprocess.run(
            [binary, '--version'],
            capture_output=True, text=True, encoding='utf-8', errors='replace',
            timeout=120, creationflags=flags)
        login = subprocess.run(
            [binary, 'login', 'status'],
            capture_output=True, text=True, encoding='utf-8', errors='replace',
            timeout=120, creationflags=flags)
    except (OSError, subprocess.TimeoutExpired) as e:
        print(json.dumps({'ok': False, 'codex': binary, 'reason': str(e)}))
        return 1
    status = (login.stdout + login.stderr).strip()
    result = {
        'ok': version.returncode == 0 and login.returncode == 0,
        'codex': binary,
        'version': version.stdout.strip(),
        'login': status,
    }
    if not result['ok']:
        result['reason'] = (version.stderr.strip() or status)[-500:]
    print(json.dumps(result))
    return 0 if result['ok'] else 1


def command_start(args):
    run_dir = os.path.abspath(args.run_dir)
    os.makedirs(run_dir, exist_ok=True)
    files = paths(run_dir, args.name)
    existing = read_state(run_dir, args.name)
    if existing:
        if existing.get('status') in RUNNING and pid_alive(existing.get('runner_pid')):
            fail('leaf ' + args.name + ' is still running; stop it first')
        if not args.replace:
            fail('leaf ' + args.name + ' already exists; pass --replace to rerun it')
        stamp = time.strftime('%Y%m%d-%H%M%S')
        for path in files.values():
            if os.path.exists(path):
                os.replace(path, path + '.prev-' + stamp)
    if not os.path.isdir(args.cwd):
        fail('working directory does not exist: ' + args.cwd)
    if not os.path.isfile(args.prompt_file):
        fail('prompt file does not exist: ' + args.prompt_file)
    resume_thread = None
    if args.resume:
        source = read_state(run_dir, args.resume)
        resume_thread = source and source.get('thread_id')
        if not resume_thread:
            fail('leaf ' + args.resume + ' has no recorded Codex thread to resume')
    binary = codex_binary()
    if not binary:
        fail('codex is not on PATH')
    state = {
        'name': args.name,
        'status': 'starting',
        'codex': binary,
        'cwd': os.path.abspath(args.cwd),
        'prompt_file': os.path.abspath(args.prompt_file),
        'sandbox': args.sandbox,
        'effort': args.effort,
        'model': args.model,
        'output_schema': args.output_schema and os.path.abspath(args.output_schema),
        'no_project_doc': args.no_project_doc,
        'resume_of': args.resume,
        'resume_thread': resume_thread,
        'max_minutes': args.max_minutes,
        'started': now(),
    }
    write_state(run_dir, args.name, state)
    runner = [sys.executable, os.path.abspath(__file__), '_run',
              '--run-dir', run_dir, '--name', args.name]
    options = {
        'stdin': subprocess.DEVNULL,
        'stdout': subprocess.DEVNULL,
        'stderr': subprocess.DEVNULL,
        'close_fds': True,
    }
    if WINDOWS:
        # CREATE_BREAKAWAY_FROM_JOB keeps the leaf alive after the calling
        # tool's job ends; a job that forbids breakaway rejects the flag.
        flags = (subprocess.DETACHED_PROCESS
            | subprocess.CREATE_NEW_PROCESS_GROUP
            | subprocess.CREATE_NO_WINDOW)
        try:
            subprocess.Popen(runner, creationflags=flags | 0x01000000, **options)
        except OSError:
            subprocess.Popen(runner, creationflags=flags, **options)
    else:
        subprocess.Popen(runner, start_new_session=True, **options)
    print(json.dumps({'name': args.name, 'state': files['state']}))
    return 0


def command_run(args):
    run_dir = args.run_dir
    files = paths(run_dir, args.name)
    state = read_state(run_dir, args.name)
    if not state or state.get('status') != 'starting':
        return 1
    state['runner_pid'] = os.getpid()
    state['status'] = 'running'
    write_state(run_dir, args.name, state)
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
        command = [state['codex'], 'exec', 'resume'] + common + [
            '-c', 'sandbox_mode=' + state['sandbox'],
            state['resume_thread'], '-']
    else:
        command = [state['codex'], 'exec'] + common + [
            '-C', state['cwd'], '-s', state['sandbox'], '-']
    environment = dict(os.environ)
    # The Windows sandbox runs commands as another user, and git refuses a
    # checkout owned by someone else unless the directory is marked safe.
    parameters = environment.get('GIT_CONFIG_PARAMETERS', '')
    environment['GIT_CONFIG_PARAMETERS'] = (
        parameters + " 'safe.directory=*'").strip()
    flags = subprocess.CREATE_NO_WINDOW if WINDOWS else 0
    started = now()
    exit_code = None
    reason = None
    with open(state['prompt_file'], 'rb') as prompt, \
            open(files['jsonl'], 'wb') as jsonl, \
            open(files['stderr'], 'wb') as stderr:
        try:
            child = subprocess.Popen(
                command,
                cwd=state['cwd'],
                stdin=prompt,
                stdout=jsonl,
                stderr=stderr,
                env=environment,
                creationflags=flags)
        except OSError as e:
            reason = 'could not start codex: ' + str(e)
            child = None
        if child:
            state['codex_pid'] = child.pid
            state['command'] = command
            write_state(run_dir, args.name, state)
            try:
                exit_code = child.wait(timeout=state['max_minutes'] * 60)
            except subprocess.TimeoutExpired:
                kill_tree(child.pid)
                child.wait()
                reason = 'exceeded %d minutes' % state['max_minutes']
    state = read_state(run_dir, args.name) or state
    state['finished'] = now()
    state['minutes'] = round((state['finished'] - started) / 60, 1)
    state['exit_code'] = exit_code
    state['thread_id'] = thread_id(files['jsonl'])
    has_last = (os.path.isfile(files['last'])
        and os.path.getsize(files['last']) > 0)
    if state.get('status') == 'stopped':
        pass
    elif reason:
        state['status'] = 'failed'
        state['reason'] = reason
    elif exit_code != 0:
        state['status'] = 'failed'
        state['reason'] = 'codex exited with %s' % exit_code
    elif not has_last:
        state['status'] = 'failed'
        state['reason'] = 'codex produced no final message'
    else:
        state['status'] = 'done'
    write_state(run_dir, args.name, state)
    return 0


def leaf_names(run_dir, names):
    if names:
        return names
    found = []
    for entry in sorted(os.listdir(run_dir)):
        if entry.endswith('.json'):
            found.append(entry[:-len('.json')])
    return found


def describe(run_dir, name, state, show_last):
    files = paths(run_dir, name)
    if not state:
        return [name + ': missing']
    status = state.get('status')
    parts = [name + ': ' + str(status)]
    if status in RUNNING:
        parts.append('%.1fm' % ((now() - state.get('started', now())) / 60))
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
    if status == 'failed' and os.path.isfile(files['stderr']):
        with open(files['stderr'], encoding='utf-8', errors='replace') as f:
            tail = f.read().strip().splitlines()[-5:]
        for line in tail:
            lines.append('  ! ' + line)
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
        states = {}
        running = []
        for name in names:
            state = read_state(run_dir, name)
            if state and state.get('status') in RUNNING:
                started = state.get('started', 0)
                grace = (now() - started) < 30
                if not grace and not pid_alive(state.get('runner_pid')):
                    state['status'] = 'failed'
                    state['reason'] = 'runner exited without a result'
                    write_state(run_dir, name, state)
                else:
                    running.append(name)
            states[name] = state
        if not running or now() >= deadline:
            for name in names:
                for line in describe(run_dir, name, states[name], args.show_last):
                    print(line)
            if running:
                print('WAITING: %d leaf(s) still running; wait again' % len(running))
                return 2
            print('FINISHED')
            return 0
        time.sleep(min(5, max(0.5, deadline - now())))


def command_stop(args):
    run_dir = os.path.abspath(args.run_dir)
    state = read_state(run_dir, args.name)
    if not state:
        fail('no leaf ' + args.name)
    state['status'] = 'stopped'
    write_state(run_dir, args.name, state)
    kill_tree(state.get('runner_pid'))
    kill_tree(state.get('codex_pid'))
    print(args.name + ': stopped')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)

    commands.add_parser('check')

    start = commands.add_parser('start')
    start.add_argument('--run-dir', required=True)
    start.add_argument('--name', required=True)
    start.add_argument('--cwd', required=True)
    start.add_argument('--prompt-file', required=True)
    start.add_argument('--sandbox', choices=SANDBOXES, default='read-only')
    start.add_argument('--effort', default='xhigh')
    start.add_argument('--model')
    start.add_argument('--resume')
    start.add_argument('--output-schema')
    start.add_argument('--no-project-doc', action='store_true')
    start.add_argument('--max-minutes', type=int, default=120)
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

    args = parser.parse_args()
    handlers = {
        'check': command_check,
        'start': command_start,
        'wait': command_wait,
        'stop': command_stop,
        '_run': command_run,
    }
    sys.exit(handlers[args.command](args))


if __name__ == '__main__':
    main()
