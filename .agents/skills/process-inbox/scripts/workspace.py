#!/usr/bin/env python3

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


TAG_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]*")
TASK_ID_PATTERN = re.compile(
	r"[0-9]{4}/[0-9]{2}/[0-9]{2}/[a-z0-9][a-z0-9-]*"
)
VALID_STATUSES = {"todo", "in-progress", "approved", "blocked"}
COMMIT_HASH_PATTERN = re.compile(
	r"(?i)\b(?:commit|revision|sha(?:-1)?)\b[^\r\n]{0,32}(?<!#)\b[0-9a-f]{7,64}\b"
)
LEGACY_COMMIT_FIELDS = ("Task-Base-SHA:", "Implementation-SHA:")
STATE_FIELD_ORDER = [
	"status",
	"created",
	"project",
	"depends_on",
	"claimed_by",
	"claimed_at",
	"claim_order",
	"lease_until",
	"phase",
	"inbox_receipt",
]


class WorkspaceError(RuntimeError):
	pass


def run_git(path, *args, check=True):
	result = subprocess.run(
		["git", "-C", str(path), *args],
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		text=True,
	)
	if check and result.returncode:
		raise WorkspaceError(result.stderr.strip() or result.stdout.strip())
	return result


def git_root(path):
	result = run_git(path, "rev-parse", "--show-toplevel")
	return Path(result.stdout.strip()).resolve()


def absolute_git_dir(path):
	result = run_git(path, "rev-parse", "--git-common-dir")
	value = Path(result.stdout.strip())
	if not value.is_absolute():
		value = Path(path) / value
	return value.resolve()


def source_root(value):
	start = Path(value).expanduser().resolve() if value else Path.cwd()
	root = git_root(start)
	if not (root / "Telegram" / "build").is_dir():
		raise WorkspaceError(f"Not a Telegram Desktop checkout: {root}")
	return root


def read_machine_tag(root):
	path = root / "Telegram" / "build" / "ai-machine-tag"
	if not path.is_file():
		raise WorkspaceError(
			f"Missing {path}. Create it with a stable lowercase machine tag."
		)
	value = path.read_text(encoding="utf-8").strip()
	if not TAG_PATTERN.fullmatch(value):
		raise WorkspaceError(
			f"Invalid machine tag {value!r}; use lowercase letters, digits, and hyphens."
		)
	return value


def worktree_config(args, create=False):
	root = source_root(args.source_root)
	machine = read_machine_tag(root)
	checkout = root.name
	if not TAG_PATTERN.fullmatch(checkout):
		raise WorkspaceError(
			f"Invalid checkout folder {checkout!r}; use lowercase letters, digits, and hyphens."
		)

	main_value = args.ai_main or os.environ.get("AI_TDESKTOP_ROOT")
	main = (
		Path(main_value).expanduser().resolve()
		if main_value
		else (root.parent / "ai-tdesktop").resolve()
	)
	worktrees_value = args.worktrees_root or os.environ.get(
		"AI_TDESKTOP_WORKTREES_ROOT"
	)
	worktrees = (
		Path(worktrees_value).expanduser().resolve()
		if worktrees_value
		else (root.parent / "ai-tdesktop-worktrees").resolve()
	)

	if not (main / ".git").exists():
		raise WorkspaceError(f"Missing ai-tdesktop repository: {main}")
	if git_root(main) != main:
		raise WorkspaceError(f"ai-tdesktop is not a worktree root: {main}")

	main_branch = run_git(main, "branch", "--show-current").stdout.strip()
	if main_branch != "master":
		raise WorkspaceError(
			f"The main ai-tdesktop worktree must be on master, not {main_branch!r}."
		)

	tag = f"{machine}-{checkout}"
	branch = f"slot/{tag}"
	slot = worktrees / tag
	if create:
		inbox = main / "inbox"
		(inbox / "backup").mkdir(parents=True, exist_ok=True)
		(inbox / "inbox.md").touch(exist_ok=True)
		worktrees.mkdir(parents=True, exist_ok=True)
		if not slot.exists():
			branch_exists = run_git(
				main,
				"show-ref",
				"--verify",
				"--quiet",
				f"refs/heads/{branch}",
				check=False,
			).returncode == 0
			arguments = ["worktree", "add"]
			if not branch_exists:
				arguments.extend(["-b", branch])
			arguments.extend([str(slot), branch if branch_exists else "master"])
			run_git(main, *arguments)

	if not slot.is_dir():
		raise WorkspaceError(f"Missing checkout AI worktree: {slot}")
	if absolute_git_dir(main) != absolute_git_dir(slot):
		raise WorkspaceError(f"The slot does not belong to {main}: {slot}")
	slot_branch = run_git(slot, "branch", "--show-current").stdout.strip()
	if slot_branch != branch:
		raise WorkspaceError(
			f"Expected {slot} on {branch}, found {slot_branch!r}."
		)

	return {
		"source_root": str(root),
		"machine_tag": machine,
		"checkout_folder": checkout,
		"checkout_tag": tag,
		"ai_main": str(main),
		"worktrees_root": str(worktrees),
		"slot_worktree": str(slot),
		"slot_branch": branch,
		"inbox": str(main / "inbox"),
	}


def ensure_clean(path, label):
	status = run_git(path, "status", "--porcelain", "--untracked-files=all").stdout
	if status.strip():
		raise WorkspaceError(f"{label} is not clean:\n{status.rstrip()}")


def sync_local_slot(config):
	main = Path(config["ai_main"])
	slot = Path(config["slot_worktree"])
	ensure_clean(main, "ai-tdesktop master")
	ensure_clean(slot, "ai-tdesktop slot")
	counts = run_git(
		slot,
		"rev-list",
		"--left-right",
		"--count",
		f"master...{config['slot_branch']}",
	).stdout.strip().split()
	master_only, slot_only = (int(value) for value in counts)
	if slot_only:
		raise WorkspaceError(
			f"{config['slot_branch']} has {slot_only} unpublished commit(s)."
		)
	if master_only:
		run_git(slot, "merge", "--ff-only", "master")


def has_origin(path):
	return run_git(path, "remote", "get-url", "origin", check=False).returncode == 0


def origin_master_exists(path):
	return run_git(
		path,
		"show-ref",
		"--verify",
		"--quiet",
		"refs/remotes/origin/master",
		check=False,
	).returncode == 0


def sync_canonical(config):
	main = Path(config["ai_main"])
	ensure_clean(main, "ai-tdesktop master")
	ensure_clean(Path(config["slot_worktree"]), "ai-tdesktop slot")
	if has_origin(main):
		run_git(main, "fetch", "origin")
		if not origin_master_exists(main):
			raise WorkspaceError("origin/master does not exist")
		run_git(main, "merge", "--ff-only", "origin/master")
	sync_local_slot(config)


def state_paths(root):
	tasks = root / "tasks"
	if not tasks.is_dir():
		return []
	return sorted(tasks.glob("*/*/*/*/state.yaml"))


def task_id_for_state(root, path):
	relative = path.relative_to(root)
	parts = relative.parts
	if len(parts) != 6 or parts[0] != "tasks" or parts[-1] != "state.yaml":
		raise WorkspaceError(f"Invalid task state path: {path}")
	value = "/".join(parts[1:5])
	if not TASK_ID_PATTERN.fullmatch(value):
		raise WorkspaceError(f"Invalid task identifier: {value}")
	return value


def state_path(root, task_id):
	if not TASK_ID_PATTERN.fullmatch(task_id):
		raise WorkspaceError(f"Invalid task identifier: {task_id!r}")
	path = root / "tasks" / task_id / "state.yaml"
	if not path.is_file():
		raise WorkspaceError(f"Task does not exist: {task_id}")
	return path


def parse_scalar(value):
	value = value.strip()
	if value == "null":
		return None
	if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
		return value[1:-1]
	return value


def parse_dependencies(value):
	value = value.strip()
	if value == "[]":
		return []
	if not value.startswith("[") or not value.endswith("]"):
		raise WorkspaceError(f"depends_on must be an inline YAML list, found {value!r}")
	return [
		str(parse_scalar(part))
		for part in value[1:-1].split(",")
		if part.strip()
	]


def load_state(root, path):
	values = {}
	for line in path.read_text(encoding="utf-8-sig").splitlines():
		if not line or line[0].isspace() or ":" not in line:
			continue
		key, value = line.split(":", 1)
		values[key] = value.strip()
	for required in ("status", "created", "project", "depends_on", "claimed_by"):
		if required not in values:
			raise WorkspaceError(f"Missing {required} in {path}")
	status = str(parse_scalar(values["status"]))
	if status not in VALID_STATUSES:
		raise WorkspaceError(f"Invalid status {status!r} in {path}")
	project = parse_scalar(values["project"])
	if project is not None and not TAG_PATTERN.fullmatch(str(project)):
		raise WorkspaceError(f"Invalid project slug {project!r} in {path}")
	order = parse_scalar(values.get("claim_order", "null"))
	if order is not None:
		try:
			order = int(order)
		except ValueError as error:
			raise WorkspaceError(f"Invalid claim_order in {path}: {order!r}") from error
	task_id = task_id_for_state(root, path)
	task_file = path.with_name("task.md")
	title = task_id.rsplit("/", 1)[-1]
	if task_file.is_file():
		for line in task_file.read_text(encoding="utf-8-sig").splitlines():
			if line.startswith("# "):
				title = line[2:].strip()
				break
	return {
		"id": task_id,
		"title": title,
		"status": status,
		"created": str(parse_scalar(values["created"])),
		"project": project,
		"depends_on": parse_dependencies(values["depends_on"]),
		"claimed_by": parse_scalar(values["claimed_by"]),
		"claimed_at": parse_scalar(values.get("claimed_at", "null")),
		"claim_order": order,
		"lease_until": parse_scalar(values.get("lease_until", "null")),
		"phase": parse_scalar(values.get("phase", "null")),
		"state_path": str(path),
	}


def load_states(root):
	return {
		state["id"]: state
		for state in (load_state(root, path) for path in state_paths(root))
	}


def format_scalar(value):
	if value is None:
		return "null"
	return str(value)


def update_state(path, changes):
	data = path.read_bytes()
	if data.startswith(b"\xef\xbb\xbf"):
		raise WorkspaceError(f"Refusing to preserve a UTF-8 BOM in {path}")
	newline = "\r\n" if b"\r\n" in data else "\n"
	lines = data.decode("utf-8").splitlines()
	positions = {}
	for index, line in enumerate(lines):
		if line and not line[0].isspace() and ":" in line:
			positions[line.split(":", 1)[0]] = index
	for key, value in changes.items():
		line = f"{key}: {format_scalar(value)}"
		if key in positions:
			lines[positions[key]] = line
			continue
		if key not in STATE_FIELD_ORDER:
			raise WorkspaceError(f"Unknown task state field: {key}")
		order = STATE_FIELD_ORDER.index(key)
		insert_at = len(lines)
		for later in STATE_FIELD_ORDER[order + 1:]:
			if later in positions:
				insert_at = positions[later]
				break
		lines.insert(insert_at, line)
		positions = {
			name: index + (1 if index >= insert_at else 0)
			for name, index in positions.items()
		}
		positions[key] = insert_at
	path.write_bytes((newline.join(lines) + newline).encode("utf-8"))


def task_ready(task, states):
	return all(
		dependency in states and states[dependency]["status"] == "approved"
		for dependency in task["depends_on"]
	)


def queue_sort_key(task):
	return (
		task["claimed_at"] or "9999",
		task["claim_order"] if task["claim_order"] is not None else 1_000_000,
		task["created"],
		task["id"],
	)


def changed_paths(path):
	result = set()
	for arguments in (
		("diff", "--name-only"),
		("diff", "--cached", "--name-only"),
		("ls-files", "--others", "--exclude-standard"),
	):
		for value in run_git(path, *arguments).stdout.splitlines():
			if value:
				result.add(value)
	return sorted(result)


def unpublished_counts(config):
	values = run_git(
		config["slot_worktree"],
		"rev-list",
		"--left-right",
		"--count",
		f"master...{config['slot_branch']}",
	).stdout.strip().split()
	return {"master_only": int(values[0]), "slot_only": int(values[1])}


def task_relative_dir(task_id):
	return f"tasks/{task_id}"


def source_task_ref(task_id, name):
	if not TASK_ID_PATTERN.fullmatch(task_id):
		raise WorkspaceError(f"Invalid task identifier: {task_id!r}")
	return f"refs/ai-tasks/{task_id}/{name}"


def resolved_ref(path, value):
	result = run_git(
		path,
		"rev-parse",
		"--verify",
		f"{value}^{{commit}}",
		check=False,
	)
	return result.stdout.strip() if not result.returncode else None


def ensure_changes_scoped(slot, task_ids, extra_paths=()):
	allowed = tuple(task_relative_dir(task_id) + "/" for task_id in task_ids)
	unexpected = [
		path for path in changed_paths(slot)
		if not path.startswith(allowed) and path not in extra_paths
	]
	if unexpected:
		raise WorkspaceError(
			"The AI slot has changes outside the active task: "
			+ ", ".join(unexpected)
		)


def update_main_from_origin(config):
	main = Path(config["ai_main"])
	ensure_clean(main, "ai-tdesktop master")
	if has_origin(main):
		run_git(main, "fetch", "origin")
		if not origin_master_exists(main):
			raise WorkspaceError("origin/master does not exist")
		run_git(main, "merge", "--ff-only", "origin/master")


def publish_slot(config):
	main = Path(config["ai_main"])
	slot = Path(config["slot_worktree"])
	ensure_clean(main, "ai-tdesktop master")
	ensure_clean(slot, "ai-tdesktop slot")
	while True:
		update_main_from_origin(config)
		rebase = run_git(slot, "rebase", "master", check=False)
		if rebase.returncode:
			run_git(slot, "rebase", "--abort", check=False)
			raise WorkspaceError(
				"AI state conflicts with newer master; the slot commits were preserved. "
				+ (rebase.stderr.strip() or rebase.stdout.strip())
			)
		if has_origin(main):
			push = run_git(slot, "push", "origin", "HEAD:master", check=False)
			if push.returncode:
				message = push.stderr.strip() or push.stdout.strip()
				if "rejected" in message or "fetch first" in message:
					continue
				raise WorkspaceError(message)
		merge = run_git(main, "merge", "--ff-only", config["slot_branch"], check=False)
		if not merge.returncode:
			return True
		if has_origin(main):
			update_main_from_origin(config)
			continue


def commit_paths(config, paths, subject):
	slot = Path(config["slot_worktree"])
	for path in paths:
		run_git(slot, "add", "--", path)
	if run_git(slot, "diff", "--cached", "--quiet", check=False).returncode == 0:
		raise WorkspaceError("No AI task state changed")
	run_git(slot, "commit", "-m", subject)
	return publish_slot(config)


def task_summary(task, states):
	return {
		**task,
		"ready": task_ready(task, states),
	}


def normalized_task_name(value):
	return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")


def resolve_task(states, value):
	if TASK_ID_PATTERN.fullmatch(value):
		if value not in states:
			raise WorkspaceError(f"Task does not exist: {value}")
		return states[value]
	name = normalized_task_name(value)
	if not name:
		raise WorkspaceError("Task name is empty")
	exact = [
		task for task in states.values()
		if task["id"].rsplit("/", 1)[-1] == name
	]
	if not exact:
		exact = [
			task for task in states.values()
			if normalized_task_name(task["title"]) == name
		]
	unfinished = [
		task for task in exact
		if task["status"] in ("todo", "in-progress", "blocked")
	]
	candidates = unfinished or exact
	if not candidates:
		raise WorkspaceError(f"No task matches short name: {value!r}")
	if len(candidates) != 1:
		raise WorkspaceError(
			f"Task name {value!r} is ambiguous: "
			+ ", ".join(sorted(task["id"] for task in candidates))
		)
	return candidates[0]


def command_queue(args):
	config = worktree_config(args, create=True)
	main = Path(config["ai_main"])
	slot = Path(config["slot_worktree"])
	main_dirty = changed_paths(main)
	slot_dirty = changed_paths(slot)
	if not main_dirty and not slot_dirty:
		counts = unpublished_counts(config)
		if not counts["slot_only"]:
			sync_canonical(config)
	states = load_states(slot)
	tag = config["checkout_tag"]
	values = [task_summary(task, states) for task in states.values()]
	own_in_progress = sorted(
		(
			task for task in values
			if task["claimed_by"] == tag and task["status"] == "in-progress"
		),
		key=queue_sort_key,
	)
	own_todo = sorted(
		(
			task for task in values
			if task["claimed_by"] == tag and task["status"] == "todo"
		),
		key=queue_sort_key,
	)
	own_blocked = sorted(
		(
			task for task in values
			if task["claimed_by"] == tag and task["status"] == "blocked"
		),
		key=queue_sort_key,
	)
	unclaimed_todo = sorted(
		(
			task for task in values
			if task["claimed_by"] is None and task["status"] == "todo"
		),
		key=lambda task: (task["created"], task["id"]),
	)
	violations = []
	if len(own_in_progress) > 1:
		violations.append(f"{tag} has more than one in-progress task")
	for task in values:
		if task["status"] == "in-progress" and task["claimed_by"] is None:
			violations.append(f"{task['id']} is in-progress but unclaimed")
		if task["status"] == "blocked" and task["claimed_by"] is None:
			violations.append(f"{task['id']} is blocked but unclaimed")
	inbox_file = main / "inbox" / "inbox.md"
	result = {
		**config,
		"inbox_nonempty": (
			inbox_file.is_file()
			and bool(inbox_file.read_text(encoding="utf-8").strip())
		),
		"own_in_progress": own_in_progress,
		"own_todo": own_todo,
		"own_blocked": own_blocked,
		"unclaimed_todo": unclaimed_todo,
		"other_claimed_unfinished": sum(
			1 for task in values
			if task["claimed_by"] not in (None, tag)
			and task["status"] in ("todo", "in-progress", "blocked")
		),
		"ai_main_dirty": main_dirty,
		"slot_dirty": slot_dirty,
		"commits": unpublished_counts(config),
		"violations": violations,
	}
	print(json.dumps(result, indent=2, sort_keys=True))


def command_resolve(args):
	config = worktree_config(args, create=True)
	main = Path(config["ai_main"])
	slot = Path(config["slot_worktree"])
	main_dirty = changed_paths(main)
	if main_dirty:
		raise WorkspaceError(
			"ai-tdesktop master is not clean: " + ", ".join(main_dirty)
		)
	if not changed_paths(slot) and not unpublished_counts(config)["slot_only"]:
		sync_canonical(config)
	states = load_states(slot)
	task = task_summary(resolve_task(states, args.name), states)
	active = sorted(
		value["id"] for value in states.values()
		if value["claimed_by"] == config["checkout_tag"]
		and value["status"] == "in-progress"
	)
	print(json.dumps({
		**config,
		"task": task,
		"other_active_task": next(
			(value for value in active if value != task["id"]),
			None,
		),
		"slot_dirty": changed_paths(slot),
		"commits": unpublished_counts(config),
	}, indent=2, sort_keys=True))


def command_claim(args):
	config = worktree_config(args, create=True)
	sync_canonical(config)
	slot = Path(config["slot_worktree"])
	states = load_states(slot)
	tasks = []
	for task_id in args.task:
		if task_id in (task["id"] for task in tasks):
			raise WorkspaceError(f"Task was listed twice: {task_id}")
		task = states.get(task_id)
		if task is None:
			raise WorkspaceError(f"Task does not exist: {task_id}")
		if task["status"] != "todo" or task["claimed_by"] is not None:
			raise WorkspaceError(f"Task is not unclaimed todo work: {task_id}")
		tasks.append(task)
	claimed_at = datetime.datetime.now().astimezone().isoformat()
	paths = []
	for order, task in enumerate(tasks, 1):
		path = state_path(slot, task["id"])
		update_state(path, {
			"claimed_by": config["checkout_tag"],
			"claimed_at": claimed_at,
			"claim_order": order,
			"lease_until": None,
		})
		paths.append(str(path.relative_to(slot)))
	subject = (
		f"Claim {tasks[0]['id']} for {config['checkout_tag']}"
		if len(tasks) == 1
		else f"Claim {len(tasks)} tasks for {config['checkout_tag']}"
	)
	try:
		commit = commit_paths(config, paths, subject)
	except WorkspaceError as error:
		main_states = load_states(Path(config["ai_main"]))
		lost = [
			task["id"] for task in tasks
			if task["id"] in main_states
			and (
				main_states[task["id"]]["status"] != "todo"
				or main_states[task["id"]]["claimed_by"] is not None
			)
		]
		counts = unpublished_counts(config)
		if lost and counts["slot_only"] == 1:
			head = run_git(slot, "rev-parse", "HEAD").stdout.strip()
			run_git(slot, "rebase", "--onto", "master", head)
			raise WorkspaceError(
				"Claim lost to newer master for " + ", ".join(lost)
			) from error
		raise
	print(json.dumps({
		"checkout_tag": config["checkout_tag"],
		"claimed": [task["id"] for task in tasks],
		"published": bool(commit),
	}, indent=2, sort_keys=True))


def command_start(args):
	config = worktree_config(args, create=True)
	sync_canonical(config)
	slot = Path(config["slot_worktree"])
	states = load_states(slot)
	task = states.get(args.task)
	if task is None:
		raise WorkspaceError(f"Task does not exist: {args.task}")
	if task["status"] != "todo" or task["claimed_by"] != config["checkout_tag"]:
		raise WorkspaceError(f"Task is not claimed todo work for this checkout: {args.task}")
	if not task_ready(task, states):
		raise WorkspaceError(f"Task has unfinished dependencies: {args.task}")
	active = [
		value["id"] for value in states.values()
		if value["claimed_by"] == config["checkout_tag"]
		and value["status"] == "in-progress"
	]
	if active:
		raise WorkspaceError("Another task is already in progress: " + ", ".join(active))
	path = state_path(slot, args.task)
	update_state(path, {"status": "in-progress", "phase": "setup"})
	commit = commit_paths(
		config,
		[str(path.relative_to(slot))],
		f"Start {args.task} on {config['checkout_tag']}",
	)
	print(json.dumps({
		"task": args.task,
		"status": "in-progress",
		"published": bool(commit),
	}, indent=2, sort_keys=True))


def command_retry(args):
	config = worktree_config(args, create=True)
	sync_canonical(config)
	slot = Path(config["slot_worktree"])
	states = load_states(slot)
	task = states.get(args.task)
	if task is None:
		raise WorkspaceError(f"Task does not exist: {args.task}")
	if (
		task["status"] != "blocked"
		or task["claimed_by"] != config["checkout_tag"]
	):
		raise WorkspaceError(
			f"Task is not blocked work owned by this checkout: {args.task}"
		)
	if not task_ready(task, states):
		raise WorkspaceError(f"Task has unfinished dependencies: {args.task}")
	active = [
		value["id"] for value in states.values()
		if value["claimed_by"] == config["checkout_tag"]
		and value["status"] == "in-progress"
	]
	if active:
		raise WorkspaceError("Another task is already in progress: " + ", ".join(active))
	path = state_path(slot, args.task)
	update_state(path, {
		"status": "in-progress",
		"phase": "resume",
		"lease_until": None,
	})
	paths = [str(path.relative_to(slot))]
	routed = path.parent / "work" / "discovered-routed.md"
	if routed.is_file():
		routed.unlink()
		paths.append(str(routed.relative_to(slot)))
	commit = commit_paths(
		config,
		paths,
		f"Resume {args.task} on {config['checkout_tag']}",
	)
	print(json.dumps({
		"task": args.task,
		"status": "in-progress",
		"published": bool(commit),
	}, indent=2, sort_keys=True))


def task_action_config(args, require_status="in-progress", allow_project=False):
	config = worktree_config(args, create=True)
	slot = Path(config["slot_worktree"])
	ensure_clean(Path(config["ai_main"]), "ai-tdesktop master")
	task = load_state(slot, state_path(slot, args.task))
	extra_paths = ()
	if allow_project and task["project"] is not None:
		extra_paths = (f"projects/{task['project']}/project.md",)
	ensure_changes_scoped(slot, [args.task], extra_paths)
	if task["status"] != require_status or task["claimed_by"] != config["checkout_tag"]:
		raise WorkspaceError(
			f"Task is not {require_status} work owned by this checkout: {args.task}"
		)
	return config, slot


def command_checkpoint(args):
	config, slot = task_action_config(args)
	path = state_path(slot, args.task)
	update_state(path, {"phase": args.phase})
	commit = commit_paths(
		config,
		[task_relative_dir(args.task)],
		f"Checkpoint {args.task}: {args.phase}",
	)
	print(json.dumps({
		"task": args.task,
		"phase": args.phase,
		"published": bool(commit),
	}, indent=2, sort_keys=True))


def task_commit_matches(source, value, task_id):
	message = run_git(source, "show", "-s", "--format=%B", value).stdout.rstrip("\n")
	lines = message.split("\n")
	return (
		len(lines) == 3
		and bool(lines[0])
		and not lines[1]
		and lines[2] == f"Task: {task_id}"
	)


def validate_task_commit(source, value, task_id):
	if not task_commit_matches(source, value, task_id):
		raise WorkspaceError(
			"Telegram commit message must be exactly a one-line subject, a blank line, "
			f"and Task: {task_id}"
		)


def task_series_refs(source, task_id):
	commits = run_git(
		source,
		"log",
		"--first-parent",
		"--format=%H",
		"--fixed-strings",
		f"--grep=Task: {task_id}",
		"HEAD",
	).stdout.splitlines()
	for green in commits:
		if not task_commit_matches(source, green, task_id):
			continue
		current = green
		while True:
			parent = resolved_ref(source, f"{current}^")
			if parent is None:
				raise WorkspaceError(
					"A task implementation cannot start at the repository root"
				)
			if not task_commit_matches(source, parent, task_id):
				return parent, green
			current = parent
	return None


def is_ancestor(source, older, newer="HEAD"):
	return not run_git(
		source,
		"merge-base",
		"--is-ancestor",
		older,
		newer,
		check=False,
	).returncode


def validate_source_state(config, task_id, required):
	source = Path(config["source_root"])
	base = resolved_ref(source, source_task_ref(task_id, "base"))
	green = resolved_ref(source, source_task_ref(task_id, "green"))
	run = resolved_ref(source, source_task_ref(task_id, "run"))
	head = resolved_ref(source, "HEAD")
	if base is None:
		raise WorkspaceError("The local task baseline ref is missing")
	if run is None or head != run:
		raise WorkspaceError("Telegram HEAD no longer matches the task run ref")
	if green is None:
		if required:
			raise WorkspaceError("An approved task must retain a Telegram implementation commit")
		if head != base:
			raise WorkspaceError(
				"A blocked task without an implementation must be restored to its local baseline"
			)
		return
	if not is_ancestor(source, green, head):
		raise WorkspaceError(
			"The retained task implementation is not in Telegram HEAD history"
		)
	validate_task_commit(source, green, task_id)


def ensure_no_persisted_commit_hashes(root):
	text_suffixes = {".json", ".log", ".md", ".txt", ".yaml", ".yml"}
	paths = [root] if root.is_file() else sorted(root.rglob("*"))
	for path in paths:
		if not path.is_file() or path.suffix.lower() not in text_suffixes:
			continue
		try:
			text = path.read_text(encoding="utf-8-sig")
		except UnicodeDecodeError:
			continue
		if any(field in text for field in LEGACY_COMMIT_FIELDS):
			raise WorkspaceError(f"Legacy commit-hash field found in {path}")
		if COMMIT_HASH_PATTERN.search(text):
			raise WorkspaceError(f"Persisted commit hash found in {path}")


def delete_source_refs(config, task_id, retain_implementation=False):
	source = Path(config["source_root"])
	names = ("run",) if retain_implementation else ("run", "green", "base")
	for name in names:
		value = source_task_ref(task_id, name)
		if resolved_ref(source, value) is not None:
			run_git(source, "update-ref", "-d", value)


def command_source_begin(args):
	config, _ = task_action_config(args)
	source = Path(config["source_root"])
	base = source_task_ref(args.task, "base")
	green = source_task_ref(args.task, "green")
	run = source_task_ref(args.task, "run")
	base_value = resolved_ref(source, base)
	green_value = resolved_ref(source, green)
	head = resolved_ref(source, "HEAD")
	retained = (
		base_value is not None
		and green_value is not None
		and is_ancestor(source, base_value, green_value)
		and is_ancestor(source, green_value, head)
		and task_commit_matches(source, green_value, args.task)
	)
	if retained or (base_value is not None and green_value is None and head == base_value):
		state = "resumed"
	else:
		series = task_series_refs(source, args.task)
		if series is not None:
			series_base, series_green = series
			run_git(source, "update-ref", base, series_base)
			run_git(source, "update-ref", green, series_green)
			state = "reconciled" if base_value is not None else "recovered"
		else:
			ensure_clean(source, "Telegram source checkout")
			run_git(source, "update-ref", base, "HEAD")
			if green_value is not None:
				run_git(source, "update-ref", "-d", green)
			state = "reconciled" if base_value is not None else "initialized"
	run_git(source, "update-ref", run, "HEAD")
	print(json.dumps({
		"task": args.task,
		"source_state": state,
		"has_retained_implementation": (
			resolved_ref(source, green) is not None
		),
	}, indent=2, sort_keys=True))


def command_source_mark_green(args):
	config, _ = task_action_config(args)
	source = Path(config["source_root"])
	ensure_clean(source, "Telegram source checkout")
	base = source_task_ref(args.task, "base")
	if resolved_ref(source, base) is None:
		raise WorkspaceError("The local task baseline ref is missing")
	if run_git(source, "merge-base", "--is-ancestor", base, "HEAD", check=False).returncode:
		raise WorkspaceError("The retained implementation does not descend from the task baseline")
	validate_task_commit(source, "HEAD", args.task)
	run_git(source, "update-ref", source_task_ref(args.task, "green"), "HEAD")
	run_git(source, "update-ref", source_task_ref(args.task, "run"), "HEAD")
	print(json.dumps({
		"task": args.task,
		"source_state": "retained",
	}, indent=2, sort_keys=True))


def command_finish(args):
	config, slot = task_action_config(args, allow_project=True)
	ensure_clean(Path(config["source_root"]), "Telegram source checkout")
	result_path = slot / task_relative_dir(args.task) / "work" / "result.md"
	if not result_path.is_file():
		raise WorkspaceError(f"Task result is missing: {result_path}")
	result = result_path.read_text(encoding="utf-8-sig")
	expected = "STATUS: DONE" if args.status == "approved" else "STATUS: BLOCKED"
	if expected not in result.splitlines():
		raise WorkspaceError(f"Task result does not contain {expected}: {result_path}")
	if args.status == "approved" and not any(
		line in ("Verdict: APPROVED", "Verdict: NOT_APPLICABLE")
		for line in result.splitlines()
	):
		raise WorkspaceError(f"Task result does not contain an approved verdict: {result_path}")
	if "Checkout: clean-buildable" not in result.splitlines():
		raise WorkspaceError(f"Task result does not confirm a clean checkout: {result_path}")
	ensure_no_persisted_commit_hashes(result_path.parents[1])
	source_note = Path(config["source_root"]) / "tasks" / f"{args.task}.md"
	if source_note.is_file():
		ensure_no_persisted_commit_hashes(source_note)
	validate_source_state(config, args.task, args.status == "approved")
	path = state_path(slot, args.task)
	update_state(path, {
		"status": args.status,
		"phase": "complete" if args.status == "approved" else "blocked",
		"lease_until": None,
	})
	verb = "Approve" if args.status == "approved" else "Block"
	paths = [task_relative_dir(args.task)]
	project = load_state(slot, path)["project"]
	if project is not None:
		project_path = f"projects/{project}/project.md"
		if project_path in changed_paths(slot):
			ensure_no_persisted_commit_hashes(slot / project_path)
			paths.append(project_path)
	commit = commit_paths(
		config,
		paths,
		f"{verb} {args.task}",
	)
	delete_source_refs(
		config,
		args.task,
		retain_implementation=(args.status == "blocked"),
	)
	print(json.dumps({
		"task": args.task,
		"status": args.status,
		"published": bool(commit),
	}, indent=2, sort_keys=True))


def command_publish(args):
	config = worktree_config(args, create=True)
	published = publish_slot(config)
	print(json.dumps({"published": bool(published)}, indent=2, sort_keys=True))


def payload_entries(inbox):
	return sorted(
		(path for path in inbox.iterdir() if path.name != "backup"),
		key=lambda path: path.name,
	)


def iter_payload_files(inbox):
	for entry in payload_entries(inbox):
		if entry.is_symlink():
			raise WorkspaceError(f"Inbox symlinks are not supported: {entry}")
		if entry.is_file():
			yield entry
		elif entry.is_dir():
			for path in sorted(entry.rglob("*")):
				if path.is_symlink():
					raise WorkspaceError(f"Inbox symlinks are not supported: {path}")
				if path.is_file():
					yield path


def payload_digest(inbox):
	digest = hashlib.sha256()
	for path in iter_payload_files(inbox):
		relative = path.relative_to(inbox).as_posix().encode("utf-8")
		digest.update(relative)
		digest.update(b"\0")
		with path.open("rb") as stream:
			while True:
				chunk = stream.read(1024 * 1024)
				if not chunk:
					break
				digest.update(chunk)
		digest.update(b"\0")
	return digest.hexdigest()


def copy_payload(inbox, destination):
	for entry in payload_entries(inbox):
		target = destination / entry.name
		if entry.is_symlink():
			raise WorkspaceError(f"Inbox symlinks are not supported: {entry}")
		if entry.is_dir():
			shutil.copytree(entry, target)
		else:
			shutil.copy2(entry, target)


def unique_backup_name(backup, base):
	candidate = base
	index = 2
	while (backup / candidate).exists() or (backup / f".processing-{candidate}").exists():
		candidate = f"{base}-{index:02d}"
		index += 1
	return candidate


def command_ensure(args):
	config = worktree_config(args, create=True)
	print(json.dumps(config, indent=2, sort_keys=True))


def command_prepare(args):
	config = worktree_config(args, create=True)
	inbox = Path(config["inbox"])
	inbox_file = inbox / "inbox.md"
	backup = inbox / "backup"
	active = sorted(backup.glob(".processing-*"))
	if active:
		if len(active) != 1:
			raise WorkspaceError(
				"Several inbox transactions are active: "
				+ ", ".join(str(path) for path in active)
			)
		transaction, metadata = load_transaction(active[0])
		current_digest = payload_digest(inbox)
		print(json.dumps({
			**config,
			"transaction": str(transaction),
			"digest": metadata["digest"],
			"resumed": True,
			"inbox_changed": current_digest != metadata["digest"],
		}, indent=2, sort_keys=True))
		return
	if not inbox_file.read_text(encoding="utf-8").strip():
		raise WorkspaceError(f"Inbox is empty: {inbox_file}")
	sync_local_slot(config)

	stamp = datetime.datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
	name = unique_backup_name(backup, f"{stamp}-inbox")
	transaction = backup / f".processing-{name}"
	transaction.mkdir()
	digest = payload_digest(inbox)
	copy_payload(inbox, transaction)
	metadata = {
		"version": 1,
		"name": name,
		"digest": digest,
		"created_at": datetime.datetime.now().astimezone().isoformat(),
		**config,
	}
	(transaction / "transaction.json").write_text(
		json.dumps(metadata, indent=2, sort_keys=True) + "\n",
		encoding="utf-8",
	)
	print(json.dumps({
		**config,
		"transaction": str(transaction),
		"digest": digest,
		"resumed": False,
		"inbox_changed": False,
	}, indent=2, sort_keys=True))


def load_transaction(value):
	transaction = Path(value).expanduser().resolve()
	metadata_path = transaction / "transaction.json"
	if not metadata_path.is_file():
		raise WorkspaceError(f"Invalid transaction: {transaction}")
	metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
	backup = Path(metadata["inbox"]) / "backup"
	if transaction.parent.resolve() != backup.resolve():
		raise WorkspaceError(f"Transaction is outside the inbox backup: {transaction}")
	return transaction, metadata


def write_local_receipt(path, metadata, receipt, result):
	text = (
		f"Processed: {metadata['created_at']}\n"
		f"Checkout: {metadata['checkout_tag']}\n"
		f"Digest: {metadata['digest']}\n"
		f"Tracked receipt: {receipt}\n"
		f"Result: {result}\n"
	)
	(path / "receipt.txt").write_text(text, encoding="utf-8")


def clear_payload(inbox):
	for entry in payload_entries(inbox):
		if entry.is_dir() and not entry.is_symlink():
			shutil.rmtree(entry)
		else:
			entry.unlink()
	(inbox / "inbox.md").write_text("", encoding="utf-8")


def command_finalize(args):
	transaction, metadata = load_transaction(args.transaction)
	inbox = Path(metadata["inbox"])
	main = Path(metadata["ai_main"])
	receipt = main / args.receipt
	if not receipt.is_file():
		raise WorkspaceError(f"Tracked receipt is missing from master: {receipt}")
	current_digest = payload_digest(inbox)
	unchanged = current_digest == metadata["digest"]
	backup = inbox / "backup"
	name = metadata["name"] if unchanged else f"{metadata['name']}-processed-snapshot"
	final = backup / name
	if final.exists():
		name = unique_backup_name(backup, name)
		final = backup / name
	transaction.rename(final)
	if unchanged:
		clear_payload(inbox)
	write_local_receipt(
		final,
		metadata,
		args.receipt,
		"inbox-cleared" if unchanged else "inbox-changed-and-left-intact",
	)
	print(json.dumps({
		"backup": str(final),
		"cleared": unchanged,
		"published": True,
	}, indent=2, sort_keys=True))


def command_abort(args):
	transaction, metadata = load_transaction(args.transaction)
	backup = transaction.parent
	name = unique_backup_name(backup, f"{metadata['name']}-failed")
	final = backup / name
	transaction.rename(final)
	write_local_receipt(final, metadata, "not-created", "processing-failed")
	print(json.dumps({"backup": str(final), "cleared": False}, indent=2, sort_keys=True))


def add_common_arguments(parser):
	parser.add_argument("--source-root")
	parser.add_argument("--ai-main")
	parser.add_argument("--worktrees-root")


def parse_args():
	parser = argparse.ArgumentParser()
	subparsers = parser.add_subparsers(dest="command", required=True)

	ensure = subparsers.add_parser("ensure")
	add_common_arguments(ensure)
	ensure.set_defaults(handler=command_ensure)

	prepare = subparsers.add_parser("prepare")
	add_common_arguments(prepare)
	prepare.set_defaults(handler=command_prepare)

	finalize = subparsers.add_parser("finalize")
	finalize.add_argument("--transaction", required=True)
	finalize.add_argument("--receipt", required=True)
	finalize.set_defaults(handler=command_finalize)

	abort = subparsers.add_parser("abort")
	abort.add_argument("--transaction", required=True)
	abort.set_defaults(handler=command_abort)

	queue = subparsers.add_parser("queue")
	add_common_arguments(queue)
	queue.set_defaults(handler=command_queue)

	resolve = subparsers.add_parser("resolve")
	add_common_arguments(resolve)
	resolve.add_argument("--name", required=True)
	resolve.set_defaults(handler=command_resolve)

	claim = subparsers.add_parser("claim")
	add_common_arguments(claim)
	claim.add_argument("--task", action="append", required=True)
	claim.set_defaults(handler=command_claim)

	start = subparsers.add_parser("start")
	add_common_arguments(start)
	start.add_argument("--task", required=True)
	start.set_defaults(handler=command_start)

	retry = subparsers.add_parser("retry")
	add_common_arguments(retry)
	retry.add_argument("--task", required=True)
	retry.set_defaults(handler=command_retry)

	checkpoint = subparsers.add_parser("checkpoint")
	add_common_arguments(checkpoint)
	checkpoint.add_argument("--task", required=True)
	checkpoint.add_argument("--phase", required=True)
	checkpoint.set_defaults(handler=command_checkpoint)

	source_begin = subparsers.add_parser("source-begin")
	add_common_arguments(source_begin)
	source_begin.add_argument("--task", required=True)
	source_begin.set_defaults(handler=command_source_begin)

	source_mark_green = subparsers.add_parser("source-mark-green")
	add_common_arguments(source_mark_green)
	source_mark_green.add_argument("--task", required=True)
	source_mark_green.set_defaults(handler=command_source_mark_green)

	finish = subparsers.add_parser("finish")
	add_common_arguments(finish)
	finish.add_argument("--task", required=True)
	finish.add_argument("--status", choices=("approved", "blocked"), required=True)
	finish.set_defaults(handler=command_finish)

	publish = subparsers.add_parser("publish")
	add_common_arguments(publish)
	publish.set_defaults(handler=command_publish)

	return parser.parse_args()


def main():
	args = parse_args()
	try:
		args.handler(args)
	except (OSError, ValueError, WorkspaceError) as error:
		print(f"error: {error}", file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
