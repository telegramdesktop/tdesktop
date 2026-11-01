"""What changes and what it costs: churn from git, agent effort from the
ai-tdesktop queue (Start/Approve lifecycle commits and `Task:` trailers)."""
import datetime
from pathlib import Path
import re
import statistics
import subprocess

from sources import git


LIFECYCLE = re.compile(r'^(Start|Approve) (\d{4}/\d{2}/\d{2}/[^ ]+)')
REVIEW = re.compile(r'^review(\d+)-')


def churn(tree, days):
    """Commits per scanned path over the last `days` days."""
    since = f'--since={days}.days'
    result = {}
    head = tree.commit
    collect(result, tree.repo, head, since, '', ['Telegram'])
    for path, commit in tree.submodules.items():
        try:
            collect(result, tree.repo / path, commit, since, path + '/', ['.'])
        except subprocess.CalledProcessError:
            continue
    return {path: count for path, count in result.items() if path in tree.files}


def collect(result, repo, commit, since, prefix, paths):
    raw = git(repo, 'log', commit, since, '--format=tformat:@', '--name-only',
              '--no-renames', '--', *paths).decode('utf-8', 'replace')
    for line in raw.splitlines():
        if line and line != '@':
            path = prefix + line
            result[path] = result.get(path, 0) + 1


def task_files(tree, days):
    """Task id -> source paths changed by commits carrying its trailer."""
    raw = git(tree.repo, 'log', tree.commit, f'--since={days}.days',
              '--format=tformat:@%(trailers:key=Task,valueonly,separator=%x2C)',
              '--name-only', '--no-renames', '--', 'Telegram').decode('utf-8', 'replace')
    result = {}
    current = None
    for line in raw.splitlines():
        if line.startswith('@'):
            current = line[1:].strip() or None
        elif line and current:
            result.setdefault(current, set()).add(line)
    return result


def agent_cost(tree, queue, days):
    """Per task: wall hours from Start to Approve and review rounds."""
    queue = Path(queue)
    if not (queue / 'tasks').is_dir():
        return {}
    files = task_files(tree, days)
    if not files:
        return {}
    raw = git(queue, 'log', f'--since={days + 30}.days', '--format=%at %s').decode(
        'utf-8', 'replace')
    started = {}
    approved = {}
    for line in raw.splitlines():
        stamp, _, subject = line.partition(' ')
        match = LIFECYCLE.match(subject)
        if not match:
            continue
        kind, task = match.groups()
        when = int(stamp)
        if kind == 'Start':
            started[task] = min(started.get(task, when), when)
        else:
            approved[task] = max(approved.get(task, when), when)
    result = {}
    for task, paths in files.items():
        if task not in started or task not in approved:
            continue
        hours = (approved[task] - started[task]) / 3600
        if hours <= 0:
            continue
        work = queue / 'tasks' / task / 'work'
        rounds = 0
        if work.is_dir():
            for name in (entry.name for entry in work.iterdir()):
                match = REVIEW.match(name)
                if match:
                    rounds = max(rounds, int(match.group(1)))
        result[task] = {
            'hours': round(hours, 2),
            'review_rounds': rounds,
            'paths': sorted(paths),
            'approved': datetime.datetime.fromtimestamp(
                approved[task], datetime.timezone.utc).date().isoformat(),
        }
    return result


def summarize(values):
    if not values:
        return None
    return {
        'count': len(values),
        'median': round(statistics.median(values), 2),
        'total': round(sum(values), 1),
    }
