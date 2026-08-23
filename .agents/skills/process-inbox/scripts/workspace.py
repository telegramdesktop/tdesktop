#!/usr/bin/env python3

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import subprocess
import sys
import time


TAG_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]*")
TASK_ID_PATTERN = re.compile(
	r"[0-9]{4}/[0-9]{2}/[0-9]{2}/[a-z0-9][a-z0-9-]*"
)
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
VALID_STATUSES = {
	"todo",
	"in-progress",
	"approved",
	"blocked",
	"split-required",
}
DEFAULT_TASK_TYPE = "implement"
LEGACY_TASK_TYPES = {"verify", "minimal"}
VALID_TASK_TYPES = {DEFAULT_TASK_TYPE, *LEGACY_TASK_TYPES}
MODEL_PATTERN = re.compile(r"[a-z0-9][a-z0-9.-]{0,39}")
CONSOLIDATION_PENDING = "work/consolidation-pending.md"
CONSOLIDATION_COMPLETE = "work/consolidation-complete.md"
COMMIT_HASH_PATTERN = re.compile(
	r"(?i)\b(?:commit|revision|sha(?:-1)?)\b[^\r\n]{0,32}(?<!#)\b[0-9a-f]{7,64}\b"
)
LEGACY_COMMIT_FIELDS = ("Task-Base-SHA:", "Implementation-SHA:")
PROJECT_ARCHIVE_DIR = "archive"
PROJECT_LINK_PATTERN = re.compile(r"\]\(\.\./\.\./(?!\.\./)")
ARCHIVED_PROJECT_LINK_PATTERN = re.compile(r"\]\(\.\./\.\./\.\./")
STATE_FIELD_ORDER = [
	"status",
	"type",
	"created",
	"project",
	"depends_on",
	"claimed_by",
	"claimed_at",
	"claim_order",
	"lease_until",
	"phase",
	"carried_from",
	"model",
	"inbox_receipt",
]
PORTABLE_GOLDEN = "test_TelegramForcePortable"
PORTABLE_LIVE = "TelegramForcePortable"
PORTABLE_REAL = "real_TelegramForcePortable"
PORTABLE_MARKER = "testing"
OVERLAY_PATHS_FILE = "test-overlay.paths"
OVERLAY_PATCH_FILE = "test-overlay.patch"
OVERLAY_SUBMODULES_FILE = "test-overlay-submodules.json"
OVERLAY_SUBMODULES_DIR = "test-overlay-submodules"
TEST_LOG_FILE = "test_log.txt"
TEST_COMPLETE_MARKER = "TEST_COMPLETE"
STALE_CRASH_DIR = "stale-crash"
CRASHPAD_COMPLETED_DIR = "completed"
BUILD_LOCK_PROCESS_NAMES = {
	"cl.exe",
	"cmake.exe",
	"cvtres.exe",
	"link.exe",
	"moc.exe",
	"mspdbsrv.exe",
	"msbuild.exe",
	"ninja.exe",
	"rc.exe",
	"rcc.exe",
	"uic.exe",
}


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


def repository_config(args, create=False):
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
	if create:
		inbox = main / "inbox"
		(inbox / "backup").mkdir(parents=True, exist_ok=True)
		(inbox / "inbox.md").touch(exist_ok=True)
		worktrees.mkdir(parents=True, exist_ok=True)
	return {
		"source_root": str(root),
		"machine_tag": machine,
		"checkout_folder": checkout,
		"checkout_tag": tag,
		"ai_main": str(main),
		"worktrees_root": str(worktrees),
		"inbox": str(main / "inbox"),
	}


def linked_worktree(config, path, branch, create, label):
	main = Path(config["ai_main"])
	if create and not path.exists():
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
		arguments.extend([str(path), branch if branch_exists else "master"])
		run_git(main, *arguments)
	if not path.is_dir():
		raise WorkspaceError(f"Missing {label}: {path}")
	if absolute_git_dir(main) != absolute_git_dir(path):
		raise WorkspaceError(
			f"The {label} does not belong to {main}: {path}"
		)
	actual_branch = run_git(path, "branch", "--show-current").stdout.strip()
	if actual_branch != branch:
		raise WorkspaceError(
			f"Expected {path} on {branch}, found {actual_branch!r}."
		)


def worktree_config(args, create=False):
	config = repository_config(args, create=create)
	tag = config["checkout_tag"]
	branch = f"slot/{tag}"
	slot = Path(config["worktrees_root"]) / tag
	linked_worktree(config, slot, branch, create, "checkout AI worktree")
	return {
		**config,
		"slot_worktree": str(slot),
		"slot_branch": branch,
	}


def inbox_worktree_config(args, create=False):
	config = repository_config(args, create=create)
	tag = config["checkout_tag"]
	branch = f"inbox/{tag}"
	worktree = Path(config["worktrees_root"]) / f"{tag}-inbox"
	linked_worktree(config, worktree, branch, create, "inbox AI worktree")
	return {
		**config,
		"inbox_worktree": str(worktree),
		"inbox_branch": branch,
	}


def ensure_clean(path, label):
	status = run_git(path, "status", "--porcelain", "--untracked-files=all").stdout
	if status.strip():
		raise WorkspaceError(f"{label} is not clean:\n{status.rstrip()}")


def sync_local_slot(config):
	sync_branch_worktree(
		config,
		"slot_worktree",
		"slot_branch",
		"ai-tdesktop slot",
	)


def sync_inbox_worktree(config):
	sync_branch_worktree(
		config,
		"inbox_worktree",
		"inbox_branch",
		"ai-tdesktop inbox worktree",
	)


def sync_branch_worktree(config, worktree_key, branch_key, label):
	main = Path(config["ai_main"])
	worktree = Path(config[worktree_key])
	branch = config[branch_key]
	ensure_clean(main, "ai-tdesktop master")
	ensure_clean(worktree, label)
	counts = run_git(
		worktree,
		"rev-list",
		"--left-right",
		"--count",
		f"master...{branch}",
	).stdout.strip().split()
	master_only, worktree_only = (int(value) for value in counts)
	if worktree_only:
		raise WorkspaceError(
			f"{branch} has {worktree_only} unpublished commit(s)."
		)
	if master_only:
		run_git(worktree, "merge", "--ff-only", "master")


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


def retryable_push_failure(message):
	lowered = message.lower()
	return "non-fast-forward" in lowered or "fetch first" in lowered


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
	kind = parse_scalar(values.get("type", DEFAULT_TASK_TYPE))
	kind = DEFAULT_TASK_TYPE if kind is None else str(kind)
	if kind not in VALID_TASK_TYPES:
		raise WorkspaceError(f"Invalid task type {kind!r} in {path}")
	if kind in LEGACY_TASK_TYPES and status != "approved":
		raise WorkspaceError(
			f"Only type 'implement' is valid for unfinished work in {path}; "
			f"{kind!r} is accepted only for approved history"
		)
	project = parse_scalar(values["project"])
	if project is not None and not TAG_PATTERN.fullmatch(str(project)):
		raise WorkspaceError(f"Invalid project slug {project!r} in {path}")
	order = parse_scalar(values.get("claim_order", "null"))
	if order is not None:
		try:
			order = int(order)
		except ValueError as error:
			raise WorkspaceError(f"Invalid claim_order in {path}: {order!r}") from error
	model = parse_scalar(values.get("model", "null"))
	if model is not None and not MODEL_PATTERN.fullmatch(str(model)):
		raise WorkspaceError(f"Invalid model name {model!r} in {path}")
	carried_from = parse_scalar(values.get("carried_from", "null"))
	if (
		carried_from is not None
		and not TASK_ID_PATTERN.fullmatch(str(carried_from))
	):
		raise WorkspaceError(f"Invalid carried_from in {path}: {carried_from!r}")
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
		"type": kind,
		"created": str(parse_scalar(values["created"])),
		"project": project,
		"depends_on": parse_dependencies(values["depends_on"]),
		"claimed_by": parse_scalar(values["claimed_by"]),
		"claimed_at": parse_scalar(values.get("claimed_at", "null")),
		"claim_order": order,
		"lease_until": parse_scalar(values.get("lease_until", "null")),
		"phase": parse_scalar(values.get("phase", "null")),
		"carried_from": carried_from,
		"model": model,
		"inbox_receipt": parse_scalar(values.get("inbox_receipt", "null")),
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
		for value in os.fsdecode(run_git_binary(path, *arguments, "-z")).split("\0"):
			if value:
				result.add(value)
	return sorted(result)


def path_is_stageable(root, path):
	return (
		os.path.lexists(root / path)
		or bool(run_git(root, "ls-files", "--", path).stdout.strip())
	)


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


def sync_inbox_canonical(config):
	update_main_from_origin(config)
	sync_inbox_worktree(config)


def publish_worktree(config, worktree_key, branch_key, label, validate=None):
	main = Path(config["ai_main"])
	worktree = Path(config[worktree_key])
	branch = config[branch_key]
	ensure_clean(main, "ai-tdesktop master")
	ensure_clean(worktree, label)
	while True:
		update_main_from_origin(config)
		rebase = run_git(worktree, "rebase", "master", check=False)
		if rebase.returncode:
			run_git(worktree, "rebase", "--abort", check=False)
			raise WorkspaceError(
				"AI state conflicts with newer master; the worktree commits were preserved. "
				+ (rebase.stderr.strip() or rebase.stdout.strip())
			)
		if validate is not None:
			validate(worktree)
		if has_origin(main):
			push = run_git(worktree, "push", "origin", "HEAD:master", check=False)
			if push.returncode:
				message = push.stderr.strip() or push.stdout.strip()
				if retryable_push_failure(message):
					continue
				raise WorkspaceError(message)
		merge = run_git(main, "merge", "--ff-only", branch, check=False)
		if not merge.returncode:
			return True
		master_advanced = run_git(
			main,
			"merge-base",
			"--is-ancestor",
			"master",
			branch,
			check=False,
		).returncode != 0
		if master_advanced:
			continue
		raise WorkspaceError(
			"Could not fast-forward local AI master: "
			+ (merge.stderr.strip() or merge.stdout.strip())
		)


def publish_slot(config, validate=None):
	return publish_worktree(
		config,
		"slot_worktree",
		"slot_branch",
		"ai-tdesktop slot",
		validate,
	)


def publish_inbox(config):
	return publish_worktree(
		config,
		"inbox_worktree",
		"inbox_branch",
		"ai-tdesktop inbox worktree",
	)


def commit_paths(config, paths, subject, validate=None):
	slot = Path(config["slot_worktree"])
	for path in paths:
		run_git(slot, "add", "--", path)
	if run_git(slot, "diff", "--cached", "--quiet", check=False).returncode == 0:
		raise WorkspaceError("No AI task state changed")
	run_git(slot, "commit", "-m", subject)
	return publish_slot(config, validate=validate)


def task_summary(task, states):
	return {
		**task,
		"ready": (
			task_ready(task, states)
			if task["status"] != "split"
			else False
		),
	}


def normalized_task_name(value):
	return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")


def superseded_paths(root):
	tasks = root / "tasks"
	if not tasks.is_dir():
		return []
	return sorted(tasks.glob("*/*/*/*/superseded.yaml"))


def split_paths(root):
	tasks = root / "tasks"
	if not tasks.is_dir():
		return []
	return sorted(tasks.glob("*/*/*/*/split.yaml"))


def retained_task_digest(directory):
	# Read the committed blobs rather than the working tree. A checkout may
	# materialize tracked text with different line endings than the commit
	# stores, so a working-tree digest is host-specific: an alias sealed on a
	# CRLF checkout can never revalidate on an LF one, and vice versa. Git's
	# stored bytes are the same everywhere, and they also exclude ignored
	# files such as the disposable per-task `.local/` directory for free.
	if not directory.is_dir():
		raise WorkspaceError(f"Task directory does not exist: {directory}")
	root = git_root(directory)
	prefix = directory.resolve().relative_to(root).as_posix()
	listing = run_git_binary(root, "ls-tree", "-r", "-z", "HEAD", "--", prefix)
	entries = []
	for record in listing.split(b"\0"):
		if not record:
			continue
		meta, _, encoded = record.partition(b"\t")
		fields = meta.split(b" ")
		if len(fields) != 3:
			raise WorkspaceError(f"Unexpected git ls-tree record: {record!r}")
		mode, kind, oid = fields
		path = encoded.decode("utf-8")
		if mode == b"120000":
			raise WorkspaceError(f"Task retained content must not be a symlink: {path}")
		if kind != b"blob":
			continue
		relative = PurePosixPath(path).relative_to(prefix).as_posix()
		if relative in ("state.yaml", "superseded.yaml", "split.yaml"):
			continue
		entries.append((relative, oid.decode("ascii")))
	if not entries:
		raise WorkspaceError(f"Task retained content is not committed: {prefix}")
	digest = hashlib.sha256()
	for relative, oid in sorted(entries):
		name = relative.encode("utf-8")
		data = run_git_binary(root, "cat-file", "blob", oid)
		digest.update(len(name).to_bytes(8, "big"))
		digest.update(name)
		digest.update(len(data).to_bytes(8, "big"))
		digest.update(data)
	return digest.hexdigest()


def load_superseded(root):
	result = {}
	for path in superseded_paths(root):
		values = {}
		for line in path.read_text(encoding="utf-8-sig").splitlines():
			if not line or line[0].isspace() or ":" not in line:
				continue
			key, value = line.split(":", 1)
			values[key] = parse_scalar(value)
		old_id = "/".join(path.relative_to(root).parts[1:5])
		if not TASK_ID_PATTERN.fullmatch(old_id):
			raise WorkspaceError(f"Invalid superseded task path: {path}")
		for field in (
			"superseded_by",
			"receipt",
			"type",
			"project",
			"content_sha256",
		):
			if field not in values:
				raise WorkspaceError(f"Missing {field} in {path}")
		target = values["superseded_by"]
		if not isinstance(target, str) or not TASK_ID_PATTERN.fullmatch(target):
			raise WorkspaceError(f"Invalid superseded_by in {path}: {target!r}")
		kind = values["type"]
		if kind not in VALID_TASK_TYPES:
			raise WorkspaceError(f"Invalid type in {path}: {kind!r}")
		project = values["project"]
		if project is not None and not TAG_PATTERN.fullmatch(str(project)):
			raise WorkspaceError(f"Invalid project in {path}: {project!r}")
		if not isinstance(values["receipt"], str):
			raise WorkspaceError(f"Invalid receipt in {path}: {values['receipt']!r}")
		content_digest = values["content_sha256"]
		if (
			not isinstance(content_digest, str)
			or not SHA256_PATTERN.fullmatch(content_digest)
		):
			raise WorkspaceError(
				f"Invalid content_sha256 in {path}: {content_digest!r}"
			)
		result[old_id] = {
			"id": old_id,
			"superseded_by": target,
			"receipt": values["receipt"],
			"type": kind,
			"project": project,
			"content_sha256": content_digest,
			"path": str(path),
		}
	return result


def load_splits(root):
	result = {}
	for path in split_paths(root):
		values = {}
		for line in path.read_text(encoding="utf-8-sig").splitlines():
			if not line or line[0].isspace() or ":" not in line:
				continue
			key, value = line.split(":", 1)
			values[key] = value.strip()
		task_id = "/".join(path.relative_to(root).parts[1:5])
		if not TASK_ID_PATTERN.fullmatch(task_id):
			raise WorkspaceError(f"Invalid split task path: {path}")
		for field in (
			"split_into",
			"implementation_carrier",
			"receipt",
			"type",
			"created",
			"project",
			"depends_on",
			"model",
			"content_sha256",
		):
			if field not in values:
				raise WorkspaceError(f"Missing {field} in {path}")
		targets = parse_dependencies(values["split_into"])
		if (
			len(targets) < 2
			or len(targets) != len(set(targets))
			or any(not TASK_ID_PATTERN.fullmatch(value) for value in targets)
			or task_id in targets
		):
			raise WorkspaceError(f"Invalid split_into in {path}: {targets!r}")
		carrier = parse_scalar(values["implementation_carrier"])
		if carrier is not None and carrier not in targets:
			raise WorkspaceError(
				f"Invalid implementation_carrier in {path}: {carrier!r}"
			)
		kind = parse_scalar(values["type"])
		if kind not in VALID_TASK_TYPES:
			raise WorkspaceError(f"Invalid type in {path}: {kind!r}")
		project = parse_scalar(values["project"])
		if project is not None and not TAG_PATTERN.fullmatch(str(project)):
			raise WorkspaceError(f"Invalid project in {path}: {project!r}")
		receipt = parse_scalar(values["receipt"])
		if not isinstance(receipt, str) or not receipt.startswith("receipts/"):
			raise WorkspaceError(f"Invalid receipt in {path}: {receipt!r}")
		content_digest = parse_scalar(values["content_sha256"])
		if (
			not isinstance(content_digest, str)
			or not SHA256_PATTERN.fullmatch(content_digest)
		):
			raise WorkspaceError(
				f"Invalid content_sha256 in {path}: {content_digest!r}"
			)
		dependencies = parse_dependencies(values["depends_on"])
		if any(not TASK_ID_PATTERN.fullmatch(value) for value in dependencies):
			raise WorkspaceError(f"Invalid depends_on in {path}: {dependencies!r}")
		model = parse_scalar(values["model"])
		if model is None or not MODEL_PATTERN.fullmatch(str(model)):
			raise WorkspaceError(f"Invalid model in {path}: {model!r}")
		result[task_id] = {
			"id": task_id,
			"split_into": targets,
			"implementation_carrier": carrier,
			"receipt": receipt,
			"type": kind,
			"created": str(parse_scalar(values["created"])),
			"project": project,
			"depends_on": dependencies,
			"model": model,
			"content_sha256": content_digest,
			"path": str(path),
		}
	return result


def resolve_split_targets(states, superseded, splits, task_id, visited=None):
	visited = [] if visited is None else visited
	if task_id in visited:
		raise WorkspaceError(
			"Split task cycle: " + " -> ".join(visited + [task_id])
		)
	if task_id in states:
		return [task_id]
	if task_id in superseded:
		return resolve_split_targets(
			states,
			superseded,
			splits,
			superseded[task_id]["superseded_by"],
			visited + [task_id],
		)
	split = splits.get(task_id)
	if split is None:
		raise WorkspaceError(f"Split task {task_id} targets missing task state")
	result = []
	for target in split["split_into"]:
		for resolved in resolve_split_targets(
			states,
			superseded,
			splits,
			target,
			visited + [task_id],
		):
			if resolved not in result:
				result.append(resolved)
	return result


def split_task_summary(root, states, superseded, splits, task_id):
	split = splits[task_id]
	task_file = root / "tasks" / task_id / "task.md"
	title = task_id.rsplit("/", 1)[-1]
	if task_file.is_file():
		for line in task_file.read_text(encoding="utf-8-sig").splitlines():
			if line.startswith("# "):
				title = line[2:].strip()
				break
	return {
		"id": task_id,
		"title": title,
		"status": "split",
		"type": split["type"],
		"created": split["created"],
		"project": split["project"],
		"depends_on": split["depends_on"],
		"claimed_by": None,
		"split_into": resolve_split_targets(
			states, superseded, splits, task_id,
		),
		"implementation_carrier": split["implementation_carrier"],
		"receipt": split["receipt"],
		"model": split["model"],
	}


def resolve_task_reference(root, states, superseded, splits, task_id):
	visited = []
	current = task_id
	while current not in states and current not in splits:
		if current in visited:
			raise WorkspaceError(
				"Superseded task cycle: " + " -> ".join(visited + [current])
			)
		visited.append(current)
		alias = superseded.get(current)
		if alias is None:
			raise WorkspaceError(f"Task does not exist: {task_id}")
		current = alias["superseded_by"]
	result = (
		dict(states[current])
		if current in states
		else split_task_summary(root, states, superseded, splits, current)
	)
	if current != task_id:
		result["superseded_from"] = task_id
	return result


def resolve_task_id(states, superseded, task_id):
	visited = []
	current = task_id
	while current not in states:
		if current in visited:
			raise WorkspaceError(
				"Superseded task cycle: " + " -> ".join(visited + [current])
			)
		visited.append(current)
		alias = superseded.get(current)
		if alias is None:
			raise WorkspaceError(f"Task does not exist: {task_id}")
		current = alias["superseded_by"]
	result = dict(states[current])
	if current != task_id:
		result["superseded_from"] = task_id
	return result


def resolve_task(root, states, value):
	superseded = load_superseded(root)
	splits = load_splits(root)
	if TASK_ID_PATTERN.fullmatch(value):
		return resolve_task_reference(root, states, superseded, splits, value)
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
	if not exact:
		alias_ids = [
			task_id for task_id in superseded
			if task_id.rsplit("/", 1)[-1] == name
		]
		exact = [
			resolve_task_reference(root, states, superseded, splits, task_id)
			for task_id in alias_ids
		]
		exact = list({task["id"]: task for task in exact}.values())
	if not exact:
		split_ids = [
			task_id for task_id in splits
			if task_id.rsplit("/", 1)[-1] == name
		]
		exact = [
			split_task_summary(root, states, superseded, splits, task_id)
			for task_id in split_ids
		]
	unfinished = [
		task for task in exact
		if task["status"] in (
			"todo", "in-progress", "blocked", "split-required"
		)
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


def pending_consolidations(root):
	tasks = root / "tasks"
	if not tasks.is_dir():
		return []
	result = []
	for marker in sorted(tasks.glob(
			"*/*/*/*/" + CONSOLIDATION_PENDING
	)):
		parts = marker.relative_to(root).parts
		task_id = "/".join(parts[1:5])
		if not TASK_ID_PATTERN.fullmatch(task_id):
			raise WorkspaceError(f"Invalid consolidation marker path: {marker}")
		result.append({
			"source_task": task_id,
			"marker": marker.relative_to(root).as_posix(),
		})
	return result


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
	own_split_required = sorted(
		(
			task for task in values
			if task["claimed_by"] == tag
			and task["status"] == "split-required"
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
	if len(own_split_required) > 1:
		violations.append(f"{tag} has more than one split-required task")
	if own_in_progress and own_split_required:
		violations.append(
			f"{tag} has both in-progress and split-required work"
		)
	for task in values:
		if task["status"] == "in-progress" and task["claimed_by"] is None:
			violations.append(f"{task['id']} is in-progress but unclaimed")
		if task["status"] == "blocked" and task["claimed_by"] is None:
			violations.append(f"{task['id']} is blocked but unclaimed")
		if task["status"] == "split-required" and task["claimed_by"] is None:
			violations.append(f"{task['id']} is split-required but unclaimed")
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
		"own_split_required": own_split_required,
		"unclaimed_todo": unclaimed_todo,
		"pending_consolidations": pending_consolidations(slot),
		"other_claimed_unfinished": sum(
			1 for task in values
			if task["claimed_by"] not in (None, tag)
			and task["status"] in (
				"todo", "in-progress", "blocked", "split-required"
			)
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
	task = task_summary(resolve_task(slot, states, args.name), states)
	active = sorted(
		value["id"] for value in states.values()
		if value["claimed_by"] == config["checkout_tag"]
		and value["status"] in ("in-progress", "split-required")
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


def command_start(args):
	config = worktree_config(args, create=True)
	sync_canonical(config)
	slot = Path(config["slot_worktree"])
	states = load_states(slot)
	task = states.get(args.task)
	if task is None:
		raise WorkspaceError(f"Task does not exist: {args.task}")
	if task["status"] != "todo" or task["claimed_by"] not in (
		None,
		config["checkout_tag"],
	):
		raise WorkspaceError(
			f"Task is not available todo work for this checkout: {args.task}"
		)
	carried_from = task["carried_from"]
	if carried_from is not None:
		split = load_splits(slot).get(carried_from)
		if (
			split is None
			or split["implementation_carrier"] != args.task
			or task["claimed_by"] != config["checkout_tag"]
		):
			raise WorkspaceError(
				f"Task has an invalid carried implementation source: {args.task}"
			)
		validate_carried_worktree(config, slot, carried_from)
	if not task_ready(task, states):
		raise WorkspaceError(f"Task has unfinished dependencies: {args.task}")
	lineage = source_lineage_report(
		config,
		slot,
		args.task,
		getattr(args, "require", []),
	)
	if lineage.get("unfinished_dependencies"):
		raise WorkspaceError(
			"Task has unfinished explicit source dependencies: "
			+ ", ".join(lineage["unfinished_dependencies"])
		)
	if not lineage["current_satisfies"]:
		branches = ", ".join(lineage["compatible_local_branches"]) or "none"
		raise WorkspaceError(
			"Task source dependencies are absent from the current branch: "
			+ ", ".join(lineage["missing_source_tasks"])
			+ f"; compatible local branches: {branches}"
		)
	active = [
		value["id"] for value in states.values()
		if value["claimed_by"] == config["checkout_tag"]
		and value["status"] in ("in-progress", "split-required")
	]
	if active:
		raise WorkspaceError("Another task is already in progress: " + ", ".join(active))
	if carried_from is not None:
		transfer_source_refs(config, carried_from, args.task)
	path = state_path(slot, args.task)
	update_state(path, {
		"status": "in-progress",
		"claimed_by": config["checkout_tag"],
		"claimed_at": (
			task["claimed_at"]
			or datetime.datetime.now().astimezone().isoformat()
		),
		"claim_order": task["claim_order"] or 1,
		"lease_until": None,
		"phase": "setup",
	})
	try:
		commit = commit_paths(
			config,
			[str(path.relative_to(slot))],
			f"Start {args.task} on {config['checkout_tag']}",
		)
	except WorkspaceError as error:
		main_states = load_states(Path(config["ai_main"]))
		main_task = main_states.get(args.task)
		lost = (
			main_task is not None
			and (
				main_task["status"] != "todo"
				or main_task["claimed_by"] not in (
					None,
					config["checkout_tag"],
				)
			)
		)
		counts = unpublished_counts(config)
		if lost and counts["slot_only"] == 1:
			head = run_git(slot, "rev-parse", "HEAD").stdout.strip()
			run_git(slot, "rebase", "--onto", "master", head)
			raise WorkspaceError(
				f"Start lost to newer master for {args.task}"
			) from error
		raise
	if carried_from is not None:
		delete_source_refs(config, carried_from)
	print(json.dumps({
		"task": args.task,
		"status": "in-progress",
		"published": bool(commit),
	}, indent=2, sort_keys=True))


def command_source_lineage(args):
	config = worktree_config(args, create=True)
	slot = Path(config["slot_worktree"])
	states = load_states(slot)
	task = resolve_task(slot, states, args.task)
	report = source_lineage_report(
		config,
		slot,
		task["id"],
		args.require,
	)
	print(json.dumps(report, indent=2, sort_keys=True))


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
	lineage = source_lineage_report(
		config,
		slot,
		args.task,
		getattr(args, "require", []),
	)
	if lineage.get("unfinished_dependencies"):
		raise WorkspaceError(
			"Task has unfinished explicit source dependencies: "
			+ ", ".join(lineage["unfinished_dependencies"])
		)
	if not lineage["current_satisfies"]:
		branches = ", ".join(lineage["compatible_local_branches"]) or "none"
		raise WorkspaceError(
			"Task source dependencies are absent from the current branch: "
			+ ", ".join(lineage["missing_source_tasks"])
			+ f"; compatible local branches: {branches}"
		)
	active = [
		value["id"] for value in states.values()
		if value["claimed_by"] == config["checkout_tag"]
		and value["status"] in ("in-progress", "split-required")
	]
	if active:
		raise WorkspaceError("Another task is already in progress: " + ", ".join(active))
	path = state_path(slot, args.task)
	update_state(path, {
		"status": "in-progress",
		"phase": "resume",
		"lease_until": None,
	})
	for marker in (
		"work/discovered-routed.md",
		CONSOLIDATION_PENDING,
		CONSOLIDATION_COMPLETE,
	):
		marker_path = path.parent / marker
		if marker_path.is_file():
			marker_path.unlink()
	print(json.dumps({
		"task": args.task,
		"status": "in-progress",
		"local": True,
		"published": False,
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
	_, slot = task_action_config(args)
	path = state_path(slot, args.task)
	update_state(path, {"phase": args.phase})
	print(json.dumps({
		"task": args.task,
		"phase": args.phase,
		"local": True,
		"published": False,
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


def task_tip_commits(source, task_id):
	commits = [
		commit for commit in run_git(
			source,
			"log",
			"--all",
			"--format=%H",
			"--fixed-strings",
			f"--grep=Task: {task_id}",
		).stdout.splitlines()
		if task_commit_matches(source, commit, task_id)
	]
	return [
		commit for commit in commits
		if not any(
			commit != other and is_ancestor(source, commit, other)
			for other in commits
		)
	]


def task_has_retained_source_change(slot, state):
	if state["type"] == "verify":
		return False
	result_path = (
		Path(slot)
		/ task_relative_dir(state["id"])
		/ "work"
		/ "result.md"
	)
	if not result_path.is_file():
		return True
	outcomes = [
		line.split(":", 1)[1].strip()
		for line in result_path.read_text(encoding="utf-8-sig").splitlines()
		if line.startswith("Outcome:")
	]
	return outcomes != ["already-satisfied"]


def source_lineage_report(config, slot, task_id, extra_requirements=()):
	states = load_states(slot)
	task = states.get(task_id)
	if task is None:
		raise WorkspaceError(f"Task does not exist: {task_id}")
	superseded = load_superseded(slot)
	required = []
	unfinished = []
	for dependency in (*task["depends_on"], *extra_requirements):
		resolved = resolve_task_id(states, superseded, dependency)["id"]
		state = states[resolved]
		if state["status"] != "approved":
			if resolved not in unfinished:
				unfinished.append(resolved)
			continue
		if not task_has_retained_source_change(slot, state) or resolved in required:
			continue
		required.append(resolved)

	source = Path(config["source_root"])
	tips = {}
	for dependency in required:
		commits = task_tip_commits(source, dependency)
		carried_from = states[dependency]["carried_from"]
		if not commits and carried_from is not None:
			commits = task_tip_commits(source, carried_from)
		tips[dependency] = commits
	missing_history = [
		dependency for dependency, commits in tips.items()
		if not commits
	]
	missing = [
		dependency for dependency, commits in tips.items()
		if not commits or not any(is_ancestor(source, commit) for commit in commits)
	]
	branches = run_git(
		source,
		"for-each-ref",
		"--format=%(refname:short)",
		"refs/heads",
	).stdout.splitlines()
	compatible = [
		branch for branch in branches
		if all(
			commits and any(
				is_ancestor(source, commit, branch)
				for commit in commits
			)
			for commits in tips.values()
		)
	]
	current_branch = run_git(
		source,
		"branch",
		"--show-current",
	).stdout.strip() or None
	return {
		"task": task_id,
		"current_branch": current_branch,
		"current_satisfies": not missing,
		"unfinished_dependencies": unfinished,
		"required_source_tasks": required,
		"missing_source_tasks": missing,
		"unavailable_source_tasks": missing_history,
		"compatible_local_branches": compatible,
	}


def is_ancestor(source, older, newer="HEAD"):
	return not run_git(
		source,
		"merge-base",
		"--is-ancestor",
		older,
		newer,
		check=False,
	).returncode


def task_type(slot, task_id):
	return load_state(slot, state_path(slot, task_id))["type"]


def validate_source_state(config, task_id, expected_commit):
	source = Path(config["source_root"])
	slot_value = config.get("slot_worktree")
	carried_from = None
	if slot_value:
		slot = Path(slot_value)
		path = slot / task_relative_dir(task_id) / "state.yaml"
		if path.is_file():
			carried_from = load_state(slot, path)["carried_from"]
	base = resolved_ref(source, source_task_ref(task_id, "base"))
	green = resolved_ref(source, source_task_ref(task_id, "green"))
	run = resolved_ref(source, source_task_ref(task_id, "run"))
	head = resolved_ref(source, "HEAD")
	if base is None:
		raise WorkspaceError("The local task baseline ref is missing")
	if run is None or head != run:
		raise WorkspaceError("Telegram HEAD no longer matches the task run ref")
	if green is None:
		if expected_commit is True:
			raise WorkspaceError("An approved task must retain a Telegram implementation commit")
		if head != base:
			raise WorkspaceError(
				"A task without an implementation must be restored to its local baseline"
			)
		return
	if expected_commit is False:
		raise WorkspaceError(
			"An already-satisfied task must not retain a Telegram implementation commit"
		)
	if not is_ancestor(source, green, head):
		raise WorkspaceError(
			"The retained task implementation is not in Telegram HEAD history"
		)
	if not task_commit_matches(source, green, task_id):
		if carried_from is None or not task_commit_matches(
			source, green, carried_from,
		):
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


def transfer_source_refs(config, source_task, target_task):
	source = Path(config["source_root"])
	base = resolved_ref(source, source_task_ref(source_task, "base"))
	run = resolved_ref(source, source_task_ref(source_task, "run"))
	head = resolved_ref(source, "HEAD")
	if base is None or run is None or run != head:
		raise WorkspaceError(
			f"Carried source refs are incomplete or stale for {source_task}"
		)
	for name in ("base", "green", "run"):
		value = resolved_ref(source, source_task_ref(source_task, name))
		target = source_task_ref(target_task, name)
		if value is None:
			if resolved_ref(source, target) is not None:
				run_git(source, "update-ref", "-d", target)
		else:
			run_git(source, "update-ref", target, value)


def command_source_begin(args):
	config, slot = task_action_config(args)
	source = Path(config["source_root"])
	carried_from = (
		load_state(slot, state_path(slot, args.task))["carried_from"]
		if slot is not None
		else None
	)
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
	carried = (
		carried_from is not None
		and base_value is not None
		and resolved_ref(source, run) == head
		and (
			green_value is None
			or task_commit_matches(source, green_value, args.task)
			or task_commit_matches(source, green_value, carried_from)
		)
	)
	if carried:
		state = "adopted"
	elif retained or (
		base_value is not None
		and green_value is None
		and head == base_value
	):
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


def mark_source_green(config, task_id):
	source = Path(config["source_root"])
	ensure_clean(source, "Telegram source checkout")
	base = source_task_ref(task_id, "base")
	if resolved_ref(source, base) is None:
		raise WorkspaceError("The local task baseline ref is missing")
	if run_git(source, "merge-base", "--is-ancestor", base, "HEAD", check=False).returncode:
		raise WorkspaceError("The retained implementation does not descend from the task baseline")
	validate_task_commit(source, "HEAD", task_id)
	run_git(source, "update-ref", source_task_ref(task_id, "green"), "HEAD")
	run_git(source, "update-ref", source_task_ref(task_id, "run"), "HEAD")


def command_source_mark_green(args):
	config, _ = task_action_config(args)
	mark_source_green(config, args.task)
	print(json.dumps({
		"task": args.task,
		"source_state": "retained",
	}, indent=2, sort_keys=True))


def run_git_binary(path, *args):
	result = subprocess.run(
		["git", "-C", str(path), *args],
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
	)
	if result.returncode:
		raise WorkspaceError(
			result.stderr.decode("utf-8", "replace").strip()
			or "git failed"
		)
	return result.stdout


def literal_paths(path, *args):
	return [
		value
		for value in os.fsdecode(run_git_binary(path, *args, "-z")).split("\0")
		if value
	]


def resolved_exe(value):
	path = Path(value).expanduser().resolve()
	if not path.is_file():
		raise WorkspaceError(f"Test executable does not exist: {path}")
	return path


def portable_root_for(exe, override):
	if override:
		root = Path(override).expanduser().resolve()
		if not root.is_dir():
			raise WorkspaceError(f"Portable root does not exist: {root}")
		return root
	for parent in exe.parents:
		if parent.suffix == ".app":
			return parent.parent
	return exe.parent


def unique_destination(directory, name):
	candidate = directory / name
	index = 2
	while candidate.exists():
		candidate = directory / (
			f"{Path(name).stem}-{index:02d}{Path(name).suffix}"
		)
		index += 1
	return candidate


def move_stale_leftover(path, target):
	for attempt in reversed(range(5)):
		moved = unique_destination(target, path.name)
		try:
			shutil.move(path, moved)
			return moved
		except OSError:
			try:
				moved.unlink(missing_ok=True)
			except OSError:
				pass
			if not attempt:
				raise
			time.sleep(0.2)


def clear_stale_crash_state(live, destination):
	report = live / "tdata" / "working"
	dumps_dir = live / "tdata" / "dumps"
	leftovers = []
	if report.is_file() and report.stat().st_size > 0:
		leftovers.append(("report", report))
	if dumps_dir.is_dir():
		leftovers.extend(
			("dump", path) for path in sorted(dumps_dir.glob("*.dmp"))
			if path.is_file()
		)
	cleared = []
	for kind, path in leftovers:
		target = destination / "dumps" if kind == "dump" else destination
		target.mkdir(parents=True, exist_ok=True)
		try:
			moved = move_stale_leftover(path, target)
		except OSError as error:
			if kind == "report":
				raise WorkspaceError(
					f"Cannot clear the stale crash report {path}: {error}"
				) from error
			cleared.append({"from": str(path), "kind": kind, "to": None})
			continue
		cleared.append({"from": str(path), "kind": kind, "to": str(moved)})
	return cleared


def setup_test_account(root):
	golden = root / PORTABLE_GOLDEN
	live = root / PORTABLE_LIVE
	real = root / PORTABLE_REAL
	if not golden.is_dir():
		raise WorkspaceError(f"Missing golden test account: {golden}")
	if (live / PORTABLE_MARKER).exists():
		return "reused-marked-live"
	if live.exists():
		if real.exists():
			shutil.rmtree(live)
			state = "replaced-manual-live"
		else:
			live.rename(real)
			state = "preserved-real"
	else:
		state = "fresh-copy"
	shutil.copytree(golden, live)
	(live / PORTABLE_MARKER).write_text("1\n", encoding="utf-8")
	return state


def reset_broken_test_account(root):
	live = root / PORTABLE_LIVE
	if not (live / PORTABLE_MARKER).exists():
		raise WorkspaceError(
			f"Refusing to reset an unmarked live folder: {live}"
		)
	shutil.rmtree(live)
	return setup_test_account(root)


def processes_with_executable(exe):
	value = str(exe)
	pids = []
	if sys.platform == "win32":
		escaped = value.replace("'", "''")
		script = (
			"Get-CimInstance Win32_Process | "
			f"Where-Object {{ $_.ExecutablePath -eq '{escaped}' }} | "
			"ForEach-Object { $_.ProcessId }"
		)
		result = subprocess.run(
			["powershell", "-NoProfile", "-Command", script],
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			text=True,
		)
		for line in result.stdout.splitlines():
			line = line.strip()
			if line.isdigit():
				pids.append(int(line))
		return pids
	result = subprocess.run(
		["ps", "-axo", "pid=,comm="],
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		text=True,
	)
	for line in result.stdout.splitlines():
		parts = line.strip().split(None, 1)
		if len(parts) != 2 or not parts[0].isdigit():
			continue
		pid, comm = int(parts[0]), parts[1]
		if comm == value:
			pids.append(pid)
			continue
		if sys.platform.startswith("linux"):
			try:
				if os.readlink(f"/proc/{pid}/exe") == value:
					pids.append(pid)
			except OSError:
				continue
	return pids


def kill_processes_with_executable(exe):
	killed = []
	for pid in processes_with_executable(exe):
		try:
			if sys.platform == "win32":
				subprocess.run(
					["taskkill", "/PID", str(pid), "/F"],
					stdout=subprocess.PIPE,
					stderr=subprocess.PIPE,
				)
			else:
				os.kill(pid, signal.SIGKILL)
			killed.append(pid)
		except (OSError, subprocess.SubprocessError):
			continue
	return killed


def windows_process_records():
	if sys.platform != "win32":
		return []
	script = (
		"Get-CimInstance Win32_Process | "
		"Select-Object ProcessId,ParentProcessId,Name,ExecutablePath,CommandLine | "
		"ConvertTo-Json -Compress"
	)
	result = subprocess.run(
		["powershell", "-NoProfile", "-Command", script],
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		text=True,
	)
	if result.returncode or not result.stdout.strip():
		return []
	try:
		rows = json.loads(result.stdout)
	except json.JSONDecodeError:
		return []
	if isinstance(rows, dict):
		rows = [rows]
	records = []
	for row in rows:
		try:
			pid = int(row["ProcessId"])
			parent_pid = int(row["ParentProcessId"])
		except (KeyError, TypeError, ValueError):
			continue
		records.append({
			"pid": pid,
			"parent_pid": parent_pid,
			"name": str(row.get("Name") or ""),
			"executable": str(row.get("ExecutablePath") or ""),
			"command_line": str(row.get("CommandLine") or ""),
		})
	return records


def locking_process_ids(paths):
	if sys.platform != "win32" or not paths:
		return [], None
	import ctypes
	from ctypes import wintypes

	class UniqueProcess(ctypes.Structure):
		_fields_ = [
			("process_id", wintypes.DWORD),
			("process_start_time", wintypes.FILETIME),
		]

	class ProcessInfo(ctypes.Structure):
		_fields_ = [
			("process", UniqueProcess),
			("app_name", wintypes.WCHAR * 256),
			("service_name", wintypes.WCHAR * 64),
			("app_type", wintypes.DWORD),
			("app_status", wintypes.ULONG),
			("terminal_session_id", wintypes.DWORD),
			("restartable", wintypes.BOOL),
		]

	manager = ctypes.WinDLL("Rstrtmgr")
	manager.RmStartSession.argtypes = [
		ctypes.POINTER(wintypes.DWORD),
		wintypes.DWORD,
		wintypes.LPWSTR,
	]
	manager.RmStartSession.restype = wintypes.DWORD
	manager.RmRegisterResources.argtypes = [
		wintypes.DWORD,
		wintypes.UINT,
		ctypes.POINTER(wintypes.LPCWSTR),
		wintypes.UINT,
		ctypes.c_void_p,
		wintypes.UINT,
		ctypes.c_void_p,
	]
	manager.RmRegisterResources.restype = wintypes.DWORD
	manager.RmGetList.argtypes = [
		wintypes.DWORD,
		ctypes.POINTER(wintypes.UINT),
		ctypes.POINTER(wintypes.UINT),
		ctypes.POINTER(ProcessInfo),
		ctypes.POINTER(wintypes.DWORD),
	]
	manager.RmGetList.restype = wintypes.DWORD
	manager.RmEndSession.argtypes = [wintypes.DWORD]
	manager.RmEndSession.restype = wintypes.DWORD

	session = wintypes.DWORD()
	key = ctypes.create_unicode_buffer(33)
	started = manager.RmStartSession(ctypes.byref(session), 0, key)
	if started:
		return [], f"Restart Manager session failed: {started}"
	try:
		resources = (wintypes.LPCWSTR * len(paths))(
			*(str(path) for path in paths)
		)
		registered = manager.RmRegisterResources(
			session,
			len(paths),
			resources,
			0,
			None,
			0,
			None,
		)
		if registered:
			return [], f"Restart Manager registration failed: {registered}"
		needed = wintypes.UINT()
		count = wintypes.UINT()
		reboot_reasons = wintypes.DWORD()
		status = manager.RmGetList(
			session,
			ctypes.byref(needed),
			ctypes.byref(count),
			None,
			ctypes.byref(reboot_reasons),
		)
		if status == 0:
			return [], None
		if status != 234:
			return [], f"Restart Manager query failed: {status}"
		entries = (ProcessInfo * needed.value)()
		count.value = needed.value
		status = manager.RmGetList(
			session,
			ctypes.byref(needed),
			ctypes.byref(count),
			entries,
			ctypes.byref(reboot_reasons),
		)
		if status:
			return [], f"Restart Manager detail query failed: {status}"
		return sorted({
			entries[index].process.process_id
			for index in range(count.value)
		}), None
	finally:
		manager.RmEndSession(session)


def normalized_path_text(path):
	return os.path.normcase(str(Path(path).expanduser().resolve())).casefold()


def recoverable_build_processes(records, build_root, exe, holder_pids):
	build_text = normalized_path_text(build_root)
	build_command_text = build_text.replace("\\", "/")
	exe_text = normalized_path_text(exe)
	by_pid = {record["pid"]: record for record in records}
	reasons = {}
	for record in records:
		pid = record["pid"]
		name = record["name"].casefold()
		executable = record["executable"]
		command_line = record["command_line"]
		if executable and normalized_path_text(executable) == exe_text:
			reasons[pid] = "exact-checkout-executable"
		elif name in BUILD_LOCK_PROCESS_NAMES and pid in holder_pids:
			reasons[pid] = "direct-build-artifact-holder"
		elif (
			name in BUILD_LOCK_PROCESS_NAMES
			and build_command_text
			in command_line.casefold().replace("\\", "/")
		):
			reasons[pid] = "exact-build-tree-command"
	changed = True
	while changed:
		changed = False
		for record in records:
			pid = record["pid"]
			if pid in reasons:
				continue
			if (
				record["name"].casefold() in BUILD_LOCK_PROCESS_NAMES
				and record["parent_pid"] in reasons
				and record["parent_pid"] in by_pid
			):
				reasons[pid] = "verified-build-process-descendant"
				changed = True
	return [
		{
			**by_pid[pid],
			"reason": reason,
		}
		for pid, reason in sorted(reasons.items())
	]


def terminate_process_ids(processes):
	results = []
	for process in sorted(processes, key=lambda value: value["pid"], reverse=True):
		pid = process["pid"]
		try:
			if sys.platform == "win32":
				result = subprocess.run(
					["taskkill", "/PID", str(pid), "/F"],
					stdout=subprocess.PIPE,
					stderr=subprocess.PIPE,
					text=True,
				)
				stopped = not result.returncode
				error = (
					None
					if stopped
					else (result.stderr.strip() or result.stdout.strip())
				)
			else:
				os.kill(pid, signal.SIGKILL)
				stopped = True
				error = None
		except (OSError, subprocess.SubprocessError) as exception:
			stopped = False
			error = str(exception)
		results.append({
			**process,
			"stopped": stopped,
			"error": error,
		})
	return results


def path_inside(path, root):
	try:
		path.relative_to(root)
		return path != root
	except ValueError:
		return False


def build_root_matches_source(build, source):
	cache = build / "CMakeCache.txt"
	if not cache.is_file():
		return False
	prefix = "CMAKE_HOME_DIRECTORY:INTERNAL="
	for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
		if line.startswith(prefix):
			return (
				normalized_path_text(line[len(prefix):])
				== normalized_path_text(source)
			)
	return False


def command_build_lock_recover(args):
	source = source_root(args.source_root)
	build = Path(args.build_root).expanduser().resolve()
	exe = Path(args.exe).expanduser().resolve()
	if (
		not build.is_dir()
		or not path_inside(build, source)
		or not build_root_matches_source(build, source)
	):
		raise WorkspaceError(
			"Build root must be a configured CMake tree for this checkout: "
			f"{build}"
		)
	if not path_inside(exe, build):
		raise WorkspaceError(f"Executable is outside the build root: {exe}")
	artifacts = []
	for value in args.artifact:
		path = Path(value).expanduser().resolve()
		if not path_inside(path, build):
			raise WorkspaceError(f"Locked artifact is outside the build root: {path}")
		if path.is_dir():
			raise WorkspaceError(f"Locked artifact must be a file: {path}")
		if path not in artifacts:
			artifacts.append(path)
	if not 0 <= args.wait <= 60:
		raise WorkspaceError("--wait must be between 0 and 60 seconds")
	if args.wait:
		time.sleep(args.wait)

	exact_exe_killed = kill_processes_with_executable(exe)
	existing = [path for path in artifacts if path.exists()]
	holder_pids, holder_query_error = locking_process_ids(existing)
	records = windows_process_records()
	recoverable = recoverable_build_processes(
		records,
		build,
		exe,
		set(holder_pids),
	)
	stopped = terminate_process_ids(recoverable)
	if stopped:
		time.sleep(1)

	deleted = []
	already_absent = []
	delete_errors = {}
	for path in artifacts:
		if not path.exists():
			already_absent.append(str(path))
			continue
		try:
			path.unlink()
			deleted.append(str(path))
		except OSError as exception:
			delete_errors[str(path)] = str(exception)

	remaining = [path for path in artifacts if path.exists()]
	remaining_holder_pids, remaining_query_error = locking_process_ids(remaining)
	records_by_pid = {
		record["pid"]: record for record in windows_process_records()
	}
	remaining_holders = [
		records_by_pid.get(pid, {
			"pid": pid,
			"parent_pid": None,
			"name": "",
			"executable": "",
			"command_line": "",
		})
		for pid in remaining_holder_pids
	]
	safe_to_retry = (
		not delete_errors
		and not remaining
		and not remaining_holders
	)
	safety_basis = (
		"all-named-artifacts-deleted-or-absent"
		if safe_to_retry
		else "named-artifact-or-holder-remains"
	)
	print(json.dumps({
		"already_absent": already_absent,
		"artifacts": [str(path) for path in artifacts],
		"build_root": str(build),
		"delete_errors": delete_errors,
		"deleted": deleted,
		"exact_exe_killed": exact_exe_killed,
		"holder_query_error": holder_query_error,
		"remaining_holder_query_error": remaining_query_error,
		"remaining_holders": remaining_holders,
		"safe_to_retry": safe_to_retry,
		"safety_basis": safety_basis,
		"stopped_processes": stopped,
		"wait_seconds": args.wait,
	}, indent=2, sort_keys=True))
	if not safe_to_retry:
		sys.exit(2)


def parse_test_log(text):
	steps = []
	passed = []
	failed = []
	screenshots = []
	for line in text.splitlines():
		if line.startswith("TEST_STEP: "):
			steps.append(line[len("TEST_STEP: "):])
		elif line.startswith("TEST_RESULT: PASS: "):
			passed.append(line[len("TEST_RESULT: PASS: "):])
		elif line.startswith("TEST_RESULT: FAIL: "):
			failed.append(line[len("TEST_RESULT: FAIL: "):])
		elif line.startswith("SCREENSHOT: "):
			screenshots.append(line[len("SCREENSHOT: "):])
	return {
		"steps": steps,
		"pass": passed,
		"fail": failed,
		"screenshots": screenshots,
	}


def tail_of_file(path, lines=60):
	if not path.is_file():
		return None
	text = path.read_text(encoding="utf-8", errors="replace")
	return "\n".join(text.splitlines()[-lines:]) if text.strip() else None


def parse_env_values(values):
	environment = {}
	for value in values or ():
		if "=" not in value:
			raise WorkspaceError(f"Invalid --env value (want NAME=VALUE): {value!r}")
		name, content = value.split("=", 1)
		if not name:
			raise WorkspaceError(f"Invalid --env value (empty name): {value!r}")
		environment[name] = content
	return environment


def command_test_run(args):
	exe = resolved_exe(args.exe)
	run_dir = Path(args.run_dir).expanduser().resolve()
	run_dir.mkdir(parents=True, exist_ok=True)
	(run_dir / "screenshots").mkdir(exist_ok=True)
	portable = portable_root_for(exe, args.portable_root)
	account = setup_test_account(portable)
	stragglers = kill_processes_with_executable(exe)

	log_path = run_dir / TEST_LOG_FILE
	if log_path.exists():
		log_path.unlink()

	environment = os.environ.copy()
	environment["TDESKTOP_TEST_EVIDENCE_DIR"] = str(run_dir)
	environment.update(parse_env_values(args.env))

	cleared = (
		clear_stale_crash_state(
			portable / PORTABLE_LIVE, run_dir / STALE_CRASH_DIR
		)
		if account == "reused-marked-live"
		else []
	)

	stdout_path = run_dir / "app_stdout.txt"
	stderr_path = run_dir / "app_stderr.txt"
	working = portable / PORTABLE_LIVE / "tdata" / "working"
	dumps_dir = portable / PORTABLE_LIVE / "tdata" / "dumps"
	completed_dir = dumps_dir / CRASHPAD_COMPLETED_DIR
	completed_before = set(completed_dir.glob("*.dmp"))
	dumps_before = set(dumps_dir.glob("*.dmp"))
	working_before = working.stat().st_mtime_ns if working.is_file() else None

	launched_at = time.time()
	with stdout_path.open("wb") as out, stderr_path.open("wb") as err:
		process = subprocess.Popen(
			[str(exe), "-testagent", "-noupdate"],
			stdout=out,
			stderr=err,
			env=environment,
			cwd=str(portable),
		)
		outcome = None
		exit_code = None
		complete_seen_at = None
		last_size = -1
		last_change = launched_at
		while True:
			time.sleep(0.5)
			now = time.time()
			size = log_path.stat().st_size if log_path.is_file() else -1
			if size != last_size:
				last_size = size
				last_change = now
			complete = False
			if size > 0:
				complete = TEST_COMPLETE_MARKER in log_path.read_text(
					encoding="utf-8", errors="replace"
				)
			if complete and complete_seen_at is None:
				complete_seen_at = now
			exit_code = process.poll()
			if exit_code is not None:
				outcome = "exited"
				break
			if complete_seen_at is not None and now - complete_seen_at > args.grace:
				process.kill()
				outcome = "killed-after-complete"
				break
			if now - launched_at > args.deadline:
				process.kill()
				outcome = "deadline-killed"
				break
			if now - last_change > args.quiet and complete_seen_at is None:
				process.kill()
				outcome = "quiet-killed"
				break
		process.wait()
	ended_at = time.time()
	kill_processes_with_executable(exe)

	log_text = (
		log_path.read_text(encoding="utf-8", errors="replace")
		if log_path.is_file()
		else ""
	)
	test_complete = TEST_COMPLETE_MARKER in log_text
	crash_report_fresh = (
		working.is_file()
		and working.stat().st_mtime_ns != working_before
		and working.stat().st_size > 0
	)
	dumps = sorted(
		str(path)
		for path in set(dumps_dir.glob("*.dmp")) - dumps_before
	) if dumps_dir.is_dir() else []
	crashpad_dumps_added = sorted(
		str(path)
		for path in set(completed_dir.glob("*.dmp")) - completed_before
	)
	death_signals = []
	if dumps:
		death_signals.append("breakpad_dump")
	if crashpad_dumps_added:
		death_signals.append("crashpad_dump")
	if outcome == "exited" and exit_code:
		death_signals.append("exit_code")
	after_complete = "died-after-complete" if death_signals else "complete"
	if outcome == "exited":
		if test_complete:
			verdict_hint = after_complete
		elif crash_report_fresh or dumps:
			verdict_hint = "crash"
		else:
			verdict_hint = "died-without-complete"
	elif outcome == "killed-after-complete":
		verdict_hint = after_complete
	else:
		verdict_hint = "hang"

	print(json.dumps({
		"account": account,
		"crash_report": str(working) if working.is_file() else None,
		"crash_report_excerpt": (
			working.read_text(encoding="utf-8", errors="replace")[:4000]
			if crash_report_fresh
			else None
		),
		"crash_report_fresh": crash_report_fresh,
		"crashpad_dumps_added": crashpad_dumps_added,
		"death_signals": death_signals,
		"dumps": dumps,
		"duration_seconds": round(ended_at - launched_at, 1),
		"exe": str(exe),
		"exit_code": exit_code,
		"log_path": str(log_path) if log_path.is_file() else None,
		"markers": parse_test_log(log_text),
		"outcome": outcome,
		"portable_root": str(portable),
		"run_dir": str(run_dir),
		"stale_crash_cleared": cleared,
		"stderr_tail": tail_of_file(stderr_path),
		"stragglers_killed": stragglers,
		"test_complete": test_complete,
		"verdict_hint": verdict_hint,
	}, indent=2, sort_keys=True))


def command_test_cleanup(args):
	exe = Path(args.exe).expanduser().resolve()
	killed = kill_processes_with_executable(exe)
	deleted = False
	if args.delete_exe and exe.is_file():
		exe.unlink()
		deleted = True
	print(json.dumps({
		"deleted_exe": deleted,
		"exe": str(exe),
		"killed": killed,
	}, indent=2, sort_keys=True))


def command_test_account_reset(args):
	exe = resolved_exe(args.exe)
	kill_processes_with_executable(exe)
	portable = portable_root_for(exe, args.portable_root)
	account = reset_broken_test_account(portable)
	print(json.dumps({
		"account": account,
		"portable_root": str(portable),
	}, indent=2, sort_keys=True))


def overlay_work_dir(config, slot, task_id):
	work = slot / task_relative_dir(task_id) / "work"
	if not work.is_dir():
		raise WorkspaceError(f"Task work directory does not exist: {work}")
	return work


def read_overlay_paths(work):
	paths_file = work / OVERLAY_PATHS_FILE
	if not paths_file.is_file():
		raise WorkspaceError(f"Missing overlay inventory: {paths_file}")
	paths = [
		line.strip() for line in
		paths_file.read_text(encoding="utf-8-sig").splitlines()
		if line.strip()
	]
	if not paths:
		raise WorkspaceError(f"Empty overlay inventory: {paths_file}")
	return paths


def initialized_submodule_paths(source):
	lines = run_git(
		source, "submodule", "status", "--recursive"
	).stdout.splitlines()
	result = []
	for line in lines:
		if not line or line[0] == "-":
			continue
		parts = line[1:].split()
		if len(parts) >= 2:
			result.append(parts[1])
	return sorted(result, key=lambda path: (-path.count("/"), path))


def overlay_inventory_groups(source, inventory):
	submodules = initialized_submodule_paths(source)
	groups = {"": []}
	for value in inventory:
		path = PurePosixPath(value)
		if path.is_absolute() or not path.parts or ".." in path.parts:
			raise WorkspaceError(f"Invalid overlay inventory path: {value!r}")
		value = path.as_posix()
		if value in submodules:
			raise WorkspaceError(
				"Overlay inventory must name a tracked file inside the "
				"submodule, not its gitlink: " + value
			)
		owner = next(
			(
				submodule for submodule in submodules
				if value.startswith(submodule + "/")
			),
			"",
		)
		local = value[len(owner) + 1:] if owner else value
		repository = source / owner if owner else source
		tracked = run_git(
			repository,
			"ls-files",
			"--error-unmatch",
			"--",
			local,
			check=False,
		)
		if tracked.returncode:
			raise WorkspaceError(
				"Overlay inventory paths must be tracked files: " + value
			)
		groups.setdefault(owner, []).append(local)
	return groups, submodules


def overlay_coverage(inventory, repository_path):
	if not repository_path:
		return inventory
	prefix = repository_path + "/"
	return [
		path[len(prefix):]
		for path in inventory
		if path.startswith(prefix)
	]


def overlay_outside_inventory(source, inventory, submodules):
	outside = []
	for repository_path in [""] + submodules:
		repository = source / repository_path if repository_path else source
		coverage = overlay_coverage(inventory, repository_path)
		dirty = changed_paths(repository)
		gitlinks = set(gitlink_paths(repository, dirty))
		for path in dirty:
			covered = path_is_covered(path, coverage)
			covered_gitlink = (
				path in gitlinks
				and any(value.startswith(path + "/") for value in coverage)
			)
			if covered or covered_gitlink:
				continue
			outside.append(
				f"{repository_path}/{path}" if repository_path else path
			)
	return outside


def clear_overlay_submodule_bundle(work):
	manifest = work / OVERLAY_SUBMODULES_FILE
	patches = work / OVERLAY_SUBMODULES_DIR
	if manifest.is_file():
		manifest.unlink()
	if patches.is_dir():
		shutil.rmtree(patches)


def read_overlay_submodule_bundle(work):
	manifest = work / OVERLAY_SUBMODULES_FILE
	if not manifest.is_file():
		return []
	data = json.loads(manifest.read_text(encoding="utf-8"))
	if data.get("version") != 1 or not isinstance(data.get("submodules"), list):
		raise WorkspaceError(f"Invalid overlay submodule manifest: {manifest}")
	result = []
	seen = set()
	for entry in data["submodules"]:
		if not isinstance(entry, dict):
			raise WorkspaceError(f"Invalid overlay submodule entry: {entry!r}")
		repository = PurePosixPath(str(entry.get("path", "")))
		patch = PurePosixPath(str(entry.get("patch", "")))
		if (
			repository.is_absolute()
			or not repository.parts
			or ".." in repository.parts
			or patch.is_absolute()
			or not patch.parts
			or ".." in patch.parts
			or patch.parts[0] != OVERLAY_SUBMODULES_DIR
		):
			raise WorkspaceError(f"Invalid overlay submodule entry: {entry!r}")
		repository_value = repository.as_posix()
		if repository_value in seen:
			raise WorkspaceError(
				"Duplicate overlay submodule entry: " + repository_value
			)
		seen.add(repository_value)
		result.append({
			"patch": patch.as_posix(),
			"path": repository_value,
		})
	return result


def command_overlay_save(args):
	config, slot = task_action_config(args)
	source = Path(config["source_root"])
	work = overlay_work_dir(config, slot, args.task)
	inventory = read_overlay_paths(work)
	groups, submodules = overlay_inventory_groups(source, inventory)
	outside = overlay_outside_inventory(source, inventory, submodules)
	if outside:
		raise WorkspaceError(
			"Dirty source paths are outside the overlay inventory: "
			+ ", ".join(outside)
		)
	clear_overlay_submodule_bundle(work)
	root_paths = groups.get("", [])
	patch = (
		run_git_binary(source, "diff", "--binary", "HEAD", "--", *root_paths)
		if root_paths
		else b""
	)
	patch_path = work / OVERLAY_PATCH_FILE
	if patch.strip():
		patch_path.write_bytes(patch)
	else:
		patch_path.unlink(missing_ok=True)
	submodule_entries = []
	patches_dir = work / OVERLAY_SUBMODULES_DIR
	for repository_path, paths in groups.items():
		if not repository_path:
			continue
		repository = source / repository_path
		repository_patch = run_git_binary(
			repository, "diff", "--binary", "HEAD", "--", *paths
		)
		if not repository_patch.strip():
			continue
		patches_dir.mkdir(parents=True, exist_ok=True)
		name = hashlib.sha256(repository_path.encode("utf-8")).hexdigest()[:16]
		relative_patch = f"{OVERLAY_SUBMODULES_DIR}/{name}.patch"
		submodule_patch_path = work / relative_patch
		submodule_patch_path.write_bytes(repository_patch)
		submodule_entries.append({
			"patch": relative_patch,
			"path": repository_path,
		})
	if submodule_entries:
		(work / OVERLAY_SUBMODULES_FILE).write_text(
			json.dumps({
				"submodules": submodule_entries,
				"version": 1,
			}, indent=2, sort_keys=True) + "\n",
			encoding="utf-8",
		)
	if not patch.strip() and not submodule_entries:
		raise WorkspaceError("The overlay diff is empty; nothing to save")
	checks = []
	if patch.strip():
		checks.append((source, patch_path))
	checks.extend(
		(source / entry["path"], work / entry["patch"])
		for entry in submodule_entries
	)
	for repository, saved_patch in checks:
		check = subprocess.run(
			[
				"git", "-C", str(repository), "apply", "--check",
				"--reverse", str(saved_patch),
			],
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			text=True,
		)
		if check.returncode:
			raise WorkspaceError(
				"The saved overlay patch does not verify: "
				+ check.stderr.strip()
			)
	restored = []
	if args.restore != "none":
		ref = source_task_ref(args.task, args.restore)
		if resolved_ref(source, ref) is None:
			raise WorkspaceError(f"Missing task ref for restore: {ref}")
		for repository_path, paths in sorted(
			groups.items(), key=lambda item: -item[0].count("/")
		):
			if not repository_path:
				continue
			run_git(source / repository_path, "checkout", "HEAD", "--", *paths)
		if root_paths:
			run_git(source, "checkout", ref, "--", *root_paths)
		restored = inventory
		remaining = []
		for repository_path, paths in groups.items():
			repository = source / repository_path if repository_path else source
			remaining.extend(
				(
					f"{repository_path}/{path}"
					if repository_path else path
				)
				for path in changed_paths(repository)
				if path_is_covered(path, paths)
			)
		if remaining:
			raise WorkspaceError(
				"Overlay paths remain dirty after restore: "
				+ ", ".join(remaining)
			)
	print(json.dumps({
		"patch": str(patch_path) if patch.strip() else None,
		"patch_bytes": len(patch) + sum(
			(work / entry["patch"]).stat().st_size
			for entry in submodule_entries
		),
		"restored": restored,
		"submodules": [entry["path"] for entry in submodule_entries],
		"task": args.task,
	}, indent=2, sort_keys=True))


def command_overlay_apply(args):
	config, slot = task_action_config(args)
	source = Path(config["source_root"])
	work = overlay_work_dir(config, slot, args.task)
	patch_path = work / OVERLAY_PATCH_FILE
	submodule_entries = read_overlay_submodule_bundle(work)
	root_patch = patch_path.is_file() and patch_path.stat().st_size
	if not root_patch and not submodule_entries:
		raise WorkspaceError(f"Missing overlay patch: {patch_path}")
	inventory = read_overlay_paths(work)
	groups, submodules = overlay_inventory_groups(source, inventory)
	for entry in submodule_entries:
		if entry["path"] not in groups or entry["path"] not in submodules:
			raise WorkspaceError(
				"Overlay submodule manifest is outside the inventory: "
				+ entry["path"]
			)
		if not (work / entry["patch"]).is_file():
			raise WorkspaceError(
				"Missing overlay submodule patch: " + entry["patch"]
			)
	applications = []
	if root_patch:
		applications.append(("", source, patch_path))
	applications.extend(
		(entry["path"], source / entry["path"], work / entry["patch"])
		for entry in submodule_entries
	)
	conflicts = []
	errors = []
	for repository_path, repository, saved_patch in applications:
		result = subprocess.run(
			[
				"git", "-C", str(repository), "apply", "--3way",
				str(saved_patch),
			],
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			text=True,
		)
		if result.returncode:
			errors.append(
				f"{repository_path or '.'}: {result.stderr.strip()}"
			)
		for path in literal_paths(
			repository, "diff", "--name-only", "--diff-filter=U"
		):
			conflicts.append(
				f"{repository_path}/{path}" if repository_path else path
			)
	outside = overlay_outside_inventory(source, inventory, submodules)
	applied = not errors and not conflicts and not outside
	print(json.dumps({
		"applied": applied,
		"conflicts": conflicts,
		"error": "\n".join(errors) if errors else None,
		"outside_inventory": outside,
		"submodules": [entry["path"] for entry in submodule_entries],
		"task": args.task,
	}, indent=2, sort_keys=True))


def gitlink_paths(source, paths):
	result = []
	for path in paths:
		entry = run_git(source, "ls-files", "-s", "--", path).stdout
		if entry.startswith("160000 "):
			result.append(path)
	return result


def command_source_commit(args):
	config, slot = task_action_config(args)
	source = Path(config["source_root"])
	subject = args.subject.strip()
	if not subject or "\n" in subject:
		raise WorkspaceError("The commit subject must be a single non-empty line")
	if len(subject) > 72:
		raise WorkspaceError(
			f"The commit subject is too long ({len(subject)} > 72 characters)"
		)
	work = slot / task_relative_dir(args.task) / "work"
	owned_file = work / "owned-paths.txt"
	if not owned_file.is_file():
		raise WorkspaceError(f"Missing owned-paths inventory: {owned_file}")
	owned = [
		line.strip() for line in
		owned_file.read_text(encoding="utf-8-sig").splitlines()
		if line.strip()
	]
	if not owned:
		raise WorkspaceError(f"Empty owned-paths inventory: {owned_file}")
	source_note = f"tasks/{args.task}.md"
	carried_from = load_state(slot, state_path(slot, args.task))["carried_from"]
	allowed = owned + [source_note]
	if carried_from is not None:
		allowed.append(f"tasks/{carried_from}.md")
	dirty = changed_paths(source)
	if not dirty:
		raise WorkspaceError("The source checkout has no changes to commit")
	outside = [
		path for path in dirty
		if not path_is_covered(path, allowed)
	]
	if outside:
		raise WorkspaceError(
			"Dirty source paths are outside the owned write set: "
			+ ", ".join(outside)
		)
	submodules = gitlink_paths(source, dirty)
	if submodules:
		raise WorkspaceError(
			"Submodule pointers must be committed explicitly first: "
			+ ", ".join(submodules)
		)
	for path in dirty:
		if path_is_stageable(source, path):
			run_git(source, "add", "--", path)
	run_git(
		source,
		"commit",
		"-m",
		f"{subject}\n\nTask: {args.task}",
	)
	validate_task_commit(source, "HEAD", args.task)
	if args.mark_green:
		mark_source_green(config, args.task)
	print(json.dumps({
		"committed": dirty,
		"marked_green": bool(args.mark_green),
		"subject": subject,
		"task": args.task,
	}, indent=2, sort_keys=True))


def command_source_verify_commit(args):
	source = source_root(args.source_root)
	validate_task_commit(source, args.ref, args.task)
	subject = run_git(
		source, "show", "-s", "--format=%s", args.ref
	).stdout.strip()
	print(json.dumps({
		"ref": args.ref,
		"subject": subject,
		"task": args.task,
		"valid": True,
	}, indent=2, sort_keys=True))


def file_sha256(path):
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		while True:
			chunk = stream.read(1024 * 1024)
			if not chunk:
				break
			digest.update(chunk)
	return digest.hexdigest()


def command_fence_create(args):
	root = Path(args.root).expanduser().resolve()
	if not root.is_dir():
		raise WorkspaceError(f"Fence root does not exist: {root}")
	if not args.paths:
		raise WorkspaceError("No fence paths were provided")
	lines = []
	for value in args.paths:
		path = root / value
		if not path.is_file():
			raise WorkspaceError(f"Fence path does not exist: {path}")
		lines.append(f"{file_sha256(path)}  {value}")
	target = Path(args.file).expanduser().resolve()
	target.parent.mkdir(parents=True, exist_ok=True)
	target.write_text("\n".join(lines) + "\n", encoding="utf-8")
	print(json.dumps({
		"file": str(target),
		"paths": len(lines),
	}, indent=2, sort_keys=True))


def command_fence_check(args):
	root = Path(args.root).expanduser().resolve()
	target = Path(args.file).expanduser().resolve()
	if not target.is_file():
		raise WorkspaceError(f"Fence baseline does not exist: {target}")
	mismatched = []
	missing = []
	checked = 0
	for line in target.read_text(encoding="utf-8-sig").splitlines():
		line = line.strip()
		if not line:
			continue
		if "  " not in line:
			raise WorkspaceError(f"Invalid fence line: {line!r}")
		expected, value = line.split("  ", 1)
		path = root / value
		checked += 1
		if not path.is_file():
			missing.append(value)
		elif file_sha256(path) != expected:
			mismatched.append(value)
	ok = not mismatched and not missing
	print(json.dumps({
		"checked": checked,
		"mismatched": mismatched,
		"missing": missing,
		"ok": ok,
	}, indent=2, sort_keys=True))
	if not ok:
		sys.exit(2)


def command_source_preflight(args):
	config, slot = task_action_config(args)
	source = Path(config["source_root"])
	dirty = changed_paths(source)
	submodule_lines = run_git(
		source, "submodule", "status", "--recursive"
	).stdout.splitlines()
	submodules_dirty = [
		line.strip() for line in submodule_lines
		if line and line[0] in "+-U"
	]
	work = slot / task_relative_dir(args.task) / "work"
	owned_file = work / "owned-paths.txt"
	owned = [
		line.strip() for line in
		owned_file.read_text(encoding="utf-8-sig").splitlines()
		if line.strip()
	] if owned_file.is_file() else []
	dirty_outside_owned = [
		path for path in dirty
		if not path_is_covered(path, owned + [f"tasks/{args.task}.md"])
	]
	result = {
		"dirty": dirty,
		"dirty_outside_owned": dirty_outside_owned,
		"owned_paths_present": owned_file.is_file(),
		"source_clean": not dirty,
		"submodules_dirty": submodules_dirty,
		"task": args.task,
	}
	if args.exe:
		exe = Path(args.exe).expanduser().resolve()
		result["exe_present"] = exe.is_file()
		if exe.is_file():
			portable = portable_root_for(exe, None)
			result["golden_account_present"] = (
				portable / PORTABLE_GOLDEN
			).is_dir()
			result["live_marker_present"] = (
				portable / PORTABLE_LIVE / PORTABLE_MARKER
			).exists()
	print(json.dumps(result, indent=2, sort_keys=True))


def required_result_value(lines, result_path, field):
	prefix = f"{field}:"
	values = [
		line.split(":", 1)[1].strip() for line in lines
		if line.startswith(prefix)
	]
	if len(values) != 1 or not values[0]:
		raise WorkspaceError(
			f"Task result must record exactly one nonempty {field}: {result_path}"
		)
	return values[0]


def validate_outcome_result(lines, result_path, approved):
	outcome = required_result_value(lines, result_path, "Outcome")
	touched = required_result_value(lines, result_path, "Touched")
	allowed = {"changed", "already-satisfied"} if approved else {"blocked"}
	if outcome not in allowed:
		raise WorkspaceError(
			"Task result Outcome must be "
			+ " | ".join(sorted(allowed))
			+ f": {result_path}"
		)
	if approved and "Test-Report: work/test.md" not in lines:
		raise WorkspaceError(
			f"An approved task must retain its adaptive evidence report: {result_path}"
		)
	if outcome == "changed" and touched == "none":
		raise WorkspaceError(
			f"A changed task must name its touched paths: {result_path}"
		)
	if outcome == "already-satisfied" and touched != "none":
		raise WorkspaceError(
			f"An already-satisfied task must report Touched: none: {result_path}"
		)
	return outcome


def validate_blocked_result(lines, result_path):
	blocker = required_result_value(lines, result_path, "Blocker-Type")
	if blocker not in {"test", "impl", "unrecoverable"}:
		raise WorkspaceError(
			"A blocked task needs Blocker-Type: test | impl | unrecoverable: "
			f"{result_path}"
		)
	if blocker != "test":
		return
	verdict = required_result_value(lines, result_path, "Verdict")
	unverified = required_result_value(lines, result_path, "Unverified")
	if unverified.lower() == "none":
		raise WorkspaceError(
			f"A test-blocked task must name exact unverified behavior: {result_path}"
		)
	lowered = verdict.lower().replace("-", "_")
	for forbidden in (
		"test_flaw",
		"max_test_runs",
		"max test runs",
		"run_cap",
		"run cap",
		"blank_capture",
		"blank capture",
		"missing_capture",
		"missing capture",
		"missing_screenshot",
		"missing screenshot",
	):
		if forbidden in lowered:
			raise WorkspaceError(
				"A recoverable harness or evidence failure cannot publish "
				f"Blocker-Type: test ({forbidden}): {result_path}"
			)
	task_dir = result_path.parents[1]
	if verdict.lower().startswith("computer-use-unavailable:"):
		capability = task_dir / "computer-use-capability.md"
		if not capability.is_file():
			raise WorkspaceError(
				"A computer-use-unavailable block needs its capability report: "
				f"{capability}"
			)
		return
	test_path = result_path.parent / "test.md"
	if not test_path.is_file() or "## Recovery exhaustion" not in test_path.read_text(
		encoding="utf-8-sig",
	):
		raise WorkspaceError(
			"A test block requires work/test.md with ## Recovery exhaustion; "
			"a run cap or recoverable TEST_FLAW must start a focused campaign: "
			f"{test_path}"
		)


def owned_source_paths(slot, task_id):
	path = slot / task_relative_dir(task_id) / "work" / "owned-paths.txt"
	if not path.is_file():
		return []
	return [
		line.strip()
		for line in path.read_text(encoding="utf-8-sig").splitlines()
		if line.strip()
	]


def source_worktree_snapshot(config, slot, task_id):
	source = Path(config["source_root"])
	owned = owned_source_paths(slot, task_id)
	dirty = changed_paths(source)
	allowed = owned + [f"tasks/{task_id}.md"]
	gitlinks = set(gitlink_paths(source, dirty))
	nested_owned = {}
	for path in gitlinks:
		prefix = path + "/"
		nested_allowed = [
			value[len(prefix):]
			for value in allowed
			if value.startswith(prefix)
		]
		if not nested_allowed:
			continue
		nested_dirty = changed_paths(source / path)
		if all(path_is_covered(value, nested_allowed) for value in nested_dirty):
			nested_owned[path] = (nested_allowed, nested_dirty)
	owned_dirty = [
		path for path in dirty
		if path_is_covered(path, allowed) or path in nested_owned
	]
	outside = [path for path in dirty if path not in owned_dirty]
	digest = hashlib.sha256()
	if owned_dirty:
		digest.update(run_git_binary(
			source,
			"diff",
			"--binary",
			"--submodule=diff",
			"HEAD",
			"--",
			*owned_dirty,
		))
		untracked = set(literal_paths(
			source,
			"ls-files",
			"--others",
			"--exclude-standard",
			"--",
			*owned_dirty,
		))
		for relative in sorted(untracked):
			path = source / relative
			name = relative.encode("utf-8")
			digest.update(len(name).to_bytes(8, "big"))
			digest.update(name)
			if path.is_file():
				data = path.read_bytes()
				digest.update(len(data).to_bytes(8, "big"))
				digest.update(data)
		for submodule, (nested_allowed, nested_dirty) in sorted(
			nested_owned.items()
		):
			nested = source / submodule
			nested_untracked = set(literal_paths(
				nested,
				"ls-files",
				"--others",
				"--exclude-standard",
				"--",
				*nested_dirty,
			)) if nested_dirty else set()
			for relative in sorted(nested_untracked):
				if not path_is_covered(relative, nested_allowed):
					continue
				path = nested / relative
				name = f"{submodule}/{relative}".encode("utf-8")
				digest.update(len(name).to_bytes(8, "big"))
				digest.update(name)
				if path.is_file():
					data = path.read_bytes()
					digest.update(len(data).to_bytes(8, "big"))
					digest.update(data)
	return {
		"owned_dirty_paths": owned_dirty,
		"outside_owned_paths": outside,
		"worktree_digest": digest.hexdigest(),
	}


def validate_carried_worktree(config, slot, task_id):
	carried_path = slot / task_relative_dir(task_id) / "work" / "carried-work.json"
	if not carried_path.is_file():
		raise WorkspaceError(f"Split source lacks carried-work.json: {carried_path}")
	carried = json.loads(carried_path.read_text(encoding="utf-8-sig"))
	current = source_worktree_snapshot(config, slot, task_id)
	for field in (
		"owned_dirty_paths",
		"outside_owned_paths",
		"worktree_digest",
	):
		if carried.get(field) != current[field]:
			raise WorkspaceError(
				f"Source worktree changed after split-required publication: {field}"
			)
	return carried


def reconcile_split_source_refs(config, task_id):
	source = Path(config["source_root"])
	base_ref = source_task_ref(task_id, "base")
	green_ref = source_task_ref(task_id, "green")
	run_ref = source_task_ref(task_id, "run")
	base = resolved_ref(source, base_ref)
	run = resolved_ref(source, run_ref)
	head = resolved_ref(source, "HEAD")
	if base is None or run is None or run != head:
		raise WorkspaceError(
			"Split-required publication needs current task base/run refs at HEAD"
		)
	green = resolved_ref(source, green_ref)
	if green is None:
		series = task_series_refs(source, task_id)
		if series is not None:
			_, green = series
			run_git(source, "update-ref", green_ref, green)
	if green is not None:
		if not is_ancestor(source, green, head):
			raise WorkspaceError(
				"The retained split implementation is not in current source history"
			)
		validate_task_commit(source, green, task_id)
	return green is not None


def validate_split_required_result(lines, result_path, retained):
	if "STATUS: SPLIT_REQUIRED" not in lines:
		raise WorkspaceError(
			f"Task result does not contain STATUS: SPLIT_REQUIRED: {result_path}"
		)
	if "Verdict: SPLIT_REQUIRED" not in lines:
		raise WorkspaceError(
			f"Task result does not contain Verdict: SPLIT_REQUIRED: {result_path}"
		)
	if "Checkout: source-state-retained" not in lines:
		raise WorkspaceError(
			f"Split result does not preserve source state: {result_path}"
		)
	outcome = required_result_value(lines, result_path, "Outcome")
	if outcome != "split-required":
		raise WorkspaceError(
			f"Split result Outcome must be split-required: {result_path}"
		)
	implementation = required_result_value(lines, result_path, "Implementation")
	expected = "retained" if retained else "none"
	if implementation != expected:
		raise WorkspaceError(
			f"Split result Implementation must be {expected}: {result_path}"
		)
	touched = required_result_value(lines, result_path, "Touched")
	if retained == (touched == "none"):
		raise WorkspaceError(
			f"Split result Touched does not match retained implementation: {result_path}"
		)
	if "Split-Proposal: work/split-proposal.md" not in lines:
		raise WorkspaceError(
			f"Split result must name work/split-proposal.md: {result_path}"
		)
	proposal = result_path.parent / "split-proposal.md"
	if not proposal.is_file() or not proposal.read_text(
		encoding="utf-8-sig",
	).strip():
		raise WorkspaceError(f"Split proposal is missing or empty: {proposal}")


def write_carried_work(path, snapshot, retained):
	payload = {
		"implementation": "retained" if retained else "none",
		**snapshot,
	}
	state = path.parents[1] / "state.yaml"
	newline = "\r\n" if b"\r\n" in state.read_bytes() else "\n"
	text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
	path.write_bytes(text.replace("\n", newline).encode("utf-8"))


def command_finish(args):
	model = args.model.strip()
	if not MODEL_PATTERN.fullmatch(model):
		raise WorkspaceError(
			f"Invalid --model short name {args.model!r}. Report the model you are "
			"actually running as, lowercase, for example claude-opus-5, "
			"gpt-5.6-sol, glm-5.3."
		)
	config, slot = task_action_config(args, allow_project=True)
	kind = task_type(slot, args.task)
	if kind != DEFAULT_TASK_TYPE:
		raise WorkspaceError(
			f"Historical task type {kind!r} cannot enter the current workflow"
		)
	approved = args.status == "approved"
	split_required = args.status == "split-required"
	result_path = slot / task_relative_dir(args.task) / "work" / "result.md"
	if not result_path.is_file():
		raise WorkspaceError(f"Task result is missing: {result_path}")
	result = result_path.read_text(encoding="utf-8-sig")
	lines = result.splitlines()
	if not split_required:
		ensure_clean(Path(config["source_root"]), "Telegram source checkout")
		expected = "STATUS: DONE" if approved else "STATUS: BLOCKED"
		if expected not in lines:
			raise WorkspaceError(
				f"Task result does not contain {expected}: {result_path}"
			)
	if approved and "Verdict: APPROVED" not in lines:
		raise WorkspaceError(f"Task result does not contain an approved verdict: {result_path}")
	if not split_required and "Checkout: clean-buildable" not in lines:
		raise WorkspaceError(f"Task result does not confirm a clean checkout: {result_path}")
	if not approved and not split_required:
		validate_blocked_result(lines, result_path)
	if split_required:
		snapshot = source_worktree_snapshot(config, slot, args.task)
		retained_commit = reconcile_split_source_refs(config, args.task)
		retained = retained_commit or bool(snapshot["owned_dirty_paths"])
		validate_split_required_result(lines, result_path, retained)
		write_carried_work(
			result_path.parent / "carried-work.json",
			snapshot,
			retained,
		)
		outcome = "split-required"
	else:
		outcome = validate_outcome_result(lines, result_path, approved)
	if approved and not (result_path.parent / "test.md").is_file():
		raise WorkspaceError(
			"An approved task must retain work/test.md as adaptive evidence: "
			f"{result_path.parent / 'test.md'}"
		)
	ensure_no_persisted_commit_hashes(result_path.parents[1])
	source_note = Path(config["source_root"]) / "tasks" / f"{args.task}.md"
	if source_note.is_file():
		ensure_no_persisted_commit_hashes(source_note)
	if not split_required:
		expected_commit = outcome == "changed" if approved else None
		validate_source_state(config, args.task, expected_commit)
	path = state_path(slot, args.task)
	update_state(path, {
		"status": args.status,
		"phase": (
			"complete" if approved
			else "split-required" if split_required
			else "blocked"
		),
		"lease_until": None,
		"model": model,
	})
	verb = (
		"Approve" if approved
		else "Split-required" if split_required
		else "Block"
	)
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
	if not split_required:
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
	slot = Path(config["slot_worktree"])
	consolidation_validate = consolidation_validation_for_head(slot)
	split_validate = split_validation_for_head(slot)
	validate = consolidation_validate or split_validate
	published = publish_slot(config, validate=validate)
	if published and split_validate is not None:
		source_task = run_git(
			slot, "show", "-s", "--format=%s", "HEAD",
		).stdout.strip()[len("Split "):]
		if load_splits(slot)[source_task]["implementation_carrier"] is None:
			delete_source_refs(config, source_task)
	print(json.dumps({"published": bool(published)}, indent=2, sort_keys=True))


def normalized_publish_path(value):
	path = PurePosixPath(value.replace("\\", "/"))
	minimum_parts = {
		"tasks": 5,
		"projects": 2,
		"receipts": 5,
	}
	if (
		path.is_absolute()
		or not path.parts
		or ".." in path.parts
		or path.parts[0] not in minimum_parts
		or len(path.parts) < minimum_parts.get(path.parts[0], 0)
		or (
			path.parts[0] == "projects"
			and len(path.parts) == 2
			and path.parts[1] == PROJECT_ARCHIVE_DIR
		)
	):
		raise WorkspaceError(f"Invalid inbox publication path: {value!r}")
	return path.as_posix()


def path_is_covered(path, roots):
	return any(path == root or path.startswith(root + "/") for root in roots)


def parse_consolidation_mapping(value):
	parts = value.split("=", 1)
	if (
		len(parts) != 2
		or not TASK_ID_PATTERN.fullmatch(parts[0])
		or not TASK_ID_PATTERN.fullmatch(parts[1])
		or parts[0] == parts[1]
	):
		raise WorkspaceError(
			f"Invalid consolidation mapping {value!r}; use old-task=new-task"
		)
	return tuple(parts)


def validate_dependency_graph(states):
	dependents = {task_id: [] for task_id in states}
	remaining = {}
	for task in states.values():
		missing = [
			dependency for dependency in task["depends_on"]
			if dependency not in states
		]
		if missing:
			raise WorkspaceError(
				f"{task['id']} has missing dependencies: " + ", ".join(missing)
			)
		dependencies = set(task["depends_on"])
		remaining[task["id"]] = len(dependencies)
		for dependency in dependencies:
			dependents[dependency].append(task["id"])
	ready = [task_id for task_id, count in remaining.items() if count == 0]
	processed = 0
	while ready:
		task_id = ready.pop()
		processed += 1
		for dependent in dependents[task_id]:
			remaining[dependent] -= 1
			if remaining[dependent] == 0:
				ready.append(dependent)
	if processed != len(states):
		cycle_members = sorted(
			task_id for task_id, count in remaining.items()
			if count
		)
		raise WorkspaceError(
			"Task dependency cycle includes: " + ", ".join(cycle_members)
		)


def validate_superseded_graph(root, states, superseded):
	splits = load_splits(root)
	for task_id, alias in superseded.items():
		if task_id in states:
			raise WorkspaceError(
				f"Superseded task still has live state.yaml: {task_id}"
			)
		directory = root / "tasks" / task_id
		if not (directory / "task.md").is_file():
			raise WorkspaceError(f"Superseded task lost task.md: {task_id}")
		actual_digest = retained_task_digest(directory)
		if actual_digest != alias["content_sha256"]:
			raise WorkspaceError(
				f"Superseded task retained content changed: {task_id}"
			)
		current = task_id
		visited = []
		while current not in states and current not in splits:
			if current in visited:
				raise WorkspaceError(
					"Superseded task cycle: " + " -> ".join(visited + [current])
				)
			visited.append(current)
			current_alias = superseded.get(current)
			if current_alias is None:
				raise WorkspaceError(
					f"Superseded task {task_id} targets missing task {current}"
				)
			current = current_alias["superseded_by"]


def validate_split_graph(root, states, superseded, splits):
	for task_id, split in splits.items():
		if task_id in states or task_id in superseded:
			raise WorkspaceError(
				f"Split task still has live or superseded state: {task_id}"
			)
		directory = root / "tasks" / task_id
		if not (directory / "task.md").is_file():
			raise WorkspaceError(f"Split task lost task.md: {task_id}")
		actual_digest = retained_task_digest(directory)
		if actual_digest != split["content_sha256"]:
			raise WorkspaceError(f"Split task retained content changed: {task_id}")
		receipt = root / split["receipt"]
		if not receipt.is_file():
			raise WorkspaceError(f"Split task receipt is missing: {receipt}")
		resolve_split_targets(states, superseded, splits, task_id)


def validate_split_tree(root, source_task, replacements, receipt, carrier):
	states = load_states(root)
	superseded = load_superseded(root)
	splits = load_splits(root)
	split = splits.get(source_task)
	if split is None:
		raise WorkspaceError(f"Missing split record for {source_task}")
	if split["split_into"] != replacements:
		raise WorkspaceError(
			f"Split replacements changed for {source_task}: {split['split_into']!r}"
		)
	if split["receipt"] != receipt or split["implementation_carrier"] != carrier:
		raise WorkspaceError(f"Split routing metadata changed for {source_task}")
	validate_dependency_graph(states)
	validate_superseded_graph(root, states, superseded)
	validate_split_graph(root, states, superseded, splits)
	receipt_path = root / receipt
	receipt_text = receipt_path.read_text(encoding="utf-8-sig")
	for task_id in [source_task, *replacements]:
		if task_id not in receipt_text:
			raise WorkspaceError(f"Split receipt omits task {task_id}: {receipt}")
	for index, task_id in enumerate(replacements):
		task = states.get(task_id)
		if task is None:
			raise WorkspaceError(f"Split replacement has no live state: {task_id}")
		if task["type"] != DEFAULT_TASK_TYPE or task["status"] != "todo":
			raise WorkspaceError(
				f"Split replacement is not todo implementation work: {task_id}"
			)
		if task["inbox_receipt"] != receipt or task["model"] is not None:
			raise WorkspaceError(
				f"Split replacement has invalid routing state: {task_id}"
			)
		if task_id == carrier:
			if index != 0:
				raise WorkspaceError(
					"The implementation carrier must be the first replacement"
				)
			if (
				task["carried_from"] != source_task
				or task["claimed_by"] is None
				or task["claimed_at"] is None
				or task["claim_order"] is None
				or task["phase"] is not None
			):
				raise WorkspaceError(
					f"Implementation carrier does not own the split handoff: {task_id}"
				)
		elif any(task[field] is not None for field in (
			"claimed_by",
			"claimed_at",
			"claim_order",
			"lease_until",
			"phase",
			"carried_from",
		)):
			raise WorkspaceError(
				f"Non-carrier split replacement is not pristine: {task_id}"
			)
	for project_path in sorted((root / "projects").glob("*/tasks.md")):
		text = project_path.read_text(encoding="utf-8-sig")
		if f"tasks/{source_task}/task.md" in text:
			raise WorkspaceError(
				f"Project index still links split task {source_task}: {project_path}"
			)
	for task_id in replacements:
		project = states[task_id]["project"]
		if project is None:
			continue
		project_path = root / "projects" / project / "tasks.md"
		if (
			not project_path.is_file()
			or f"tasks/{task_id}/task.md" not in project_path.read_text(
				encoding="utf-8-sig",
			)
		):
			raise WorkspaceError(
				f"Project index omits split replacement {task_id}: {project_path}"
			)


def split_validation_for_head(slot):
	subject = run_git(slot, "show", "-s", "--format=%s", "HEAD").stdout.strip()
	prefix = "Split "
	if not subject.startswith(prefix):
		return None
	source_task = subject[len(prefix):]
	if not TASK_ID_PATTERN.fullmatch(source_task):
		raise WorkspaceError(f"Invalid split commit subject: {subject!r}")
	split = load_splits(slot).get(source_task)
	if split is None:
		raise WorkspaceError(f"Split commit lost its split record: {source_task}")
	return lambda root: validate_split_tree(
		root,
		source_task,
		split["split_into"],
		split["receipt"],
		split["implementation_carrier"],
	)


def write_split_record(path, source_state, replacements, receipt, carrier, digest):
	newline = "\r\n" if b"\r\n" in path.with_name("state.yaml").read_bytes() else "\n"
	values = [
		"split_into: [" + ", ".join(replacements) + "]",
		f"implementation_carrier: {format_scalar(carrier)}",
		f"receipt: {receipt}",
		f"type: {source_state['type']}",
		f"created: {source_state['created']}",
		f"project: {format_scalar(source_state['project'])}",
		"depends_on: [" + ", ".join(source_state["depends_on"]) + "]",
		f"model: {source_state['model']}",
		f"content_sha256: {digest}",
	]
	path.write_bytes((newline.join(values) + newline).encode("utf-8"))


def command_split_publish(args):
	config = worktree_config(args, create=True)
	slot = Path(config["slot_worktree"])
	ensure_clean(Path(config["ai_main"]), "ai-tdesktop master")
	if not TASK_ID_PATTERN.fullmatch(args.source_task):
		raise WorkspaceError(f"Invalid split source task: {args.source_task!r}")
	replacements = list(dict.fromkeys(args.replacements))
	if (
		len(replacements) < 2
		or len(replacements) != len(args.replacements)
		or any(not TASK_ID_PATTERN.fullmatch(value) for value in replacements)
		or args.source_task in replacements
	):
		raise WorkspaceError("A split needs at least two unique replacement task ids")
	carrier = args.implementation_carrier
	if carrier is not None and (carrier not in replacements or carrier != replacements[0]):
		raise WorkspaceError(
			"The implementation carrier must be the first replacement task"
		)
	receipt = normalized_publish_path(args.receipt)
	if not receipt.startswith("receipts/"):
		raise WorkspaceError("The split receipt must be below receipts/")
	paths = sorted({normalized_publish_path(value) for value in args.paths})
	required_paths = [
		f"tasks/{args.source_task}",
		*(f"tasks/{task_id}" for task_id in replacements),
		receipt,
	]
	for required in required_paths:
		if not path_is_covered(required, paths):
			raise WorkspaceError(
				f"Split publication path does not cover {required}"
			)
	changes = changed_paths(slot)
	unexpected = [path for path in changes if not path_is_covered(path, paths)]
	if unexpected:
		raise WorkspaceError(
			"Split changes are outside the explicit publication paths: "
			+ ", ".join(unexpected)
		)
	source_prefix = f"tasks/{args.source_task}/"
	source_changes = [path for path in changes if path.startswith(source_prefix)]
	if source_changes:
		raise WorkspaceError(
			"The split worker must not rewrite retained source-task content: "
			+ ", ".join(source_changes)
		)
	states = load_states(slot)
	source_state = states.get(args.source_task)
	if (
		source_state is None
		or source_state["status"] != "split-required"
		or source_state["claimed_by"] != config["checkout_tag"]
		or source_state["model"] is None
	):
		raise WorkspaceError(
			f"Task is not split-required work owned by this checkout: {args.source_task}"
		)
	carried = validate_carried_worktree(config, slot, args.source_task)
	retained = carried.get("implementation") == "retained"
	if retained != (carrier is not None):
		raise WorkspaceError(
			"Retained implementation requires exactly one implementation carrier"
		)
	for task_id in replacements:
		task = states.get(task_id)
		if task is None:
			raise WorkspaceError(f"Split replacement is missing: {task_id}")
		if (
			task["status"] != "todo"
			or task["type"] != DEFAULT_TASK_TYPE
			or task["claimed_by"] is not None
			or task["model"] is not None
			or task["carried_from"] is not None
			or task["inbox_receipt"] != receipt
		):
			raise WorkspaceError(
				f"Split replacement is not pristine routed todo work: {task_id}"
			)
	if carrier is not None:
		old_owned = (
			slot / task_relative_dir(args.source_task) / "work" / "owned-paths.txt"
		)
		if not old_owned.is_file():
			raise WorkspaceError(
				f"Retained implementation lacks owned-paths.txt: {old_owned}"
			)
		carrier_work = slot / task_relative_dir(carrier) / "work"
		carrier_work.mkdir(parents=True, exist_ok=True)
		shutil.copyfile(old_owned, carrier_work / "owned-paths.txt")
		update_state(state_path(slot, carrier), {
			"claimed_by": config["checkout_tag"],
			"claimed_at": source_state["claimed_at"],
			"claim_order": source_state["claim_order"] or 1,
			"lease_until": None,
			"phase": None,
			"carried_from": args.source_task,
		})
	source_dir = slot / task_relative_dir(args.source_task)
	digest = retained_task_digest(source_dir)
	source_state_path = source_dir / "state.yaml"
	split_path = source_dir / "split.yaml"
	write_split_record(
		split_path,
		source_state,
		replacements,
		receipt,
		carrier,
		digest,
	)
	source_state_path.unlink()
	post_changes = changed_paths(slot)
	required_source_changes = {
		f"tasks/{args.source_task}/state.yaml",
		f"tasks/{args.source_task}/split.yaml",
	}
	actual_source_changes = {
		path for path in post_changes if path.startswith(source_prefix)
	}
	if actual_source_changes != required_source_changes:
		raise WorkspaceError(
			"Split retirement changed unexpected source-task content: "
			+ ", ".join(sorted(actual_source_changes ^ required_source_changes))
		)
	validate = lambda root: validate_split_tree(
		root, args.source_task, replacements, receipt, carrier,
	)
	validate(slot)
	commit = commit_paths(
		config,
		paths,
		f"Split {args.source_task}",
		validate=validate,
	)
	if carrier is None:
		delete_source_refs(config, args.source_task)
	print(json.dumps({
		"implementation_carrier": carrier,
		"published": bool(commit),
		"replacements": replacements,
		"source_task": args.source_task,
		"status": "split",
	}, indent=2, sort_keys=True))


def validate_consolidation_tree(root, source_task, mappings, receipt):
	states = load_states(root)
	superseded = load_superseded(root)
	splits = load_splits(root)
	if source_task not in states:
		raise WorkspaceError(
			f"Consolidation source task does not exist: {source_task}"
		)
	if states[source_task]["status"] not in ("approved", "blocked"):
		raise WorkspaceError(
			f"Consolidation source task is not finished: {source_task}"
		)
	source = root / "tasks" / source_task
	pending = source / CONSOLIDATION_PENDING
	complete = source / CONSOLIDATION_COMPLETE
	if pending.exists():
		raise WorkspaceError(f"Consolidation pending marker remains: {pending}")
	if not complete.is_file():
		raise WorkspaceError(f"Missing consolidation completion marker: {complete}")
	complete_text = complete.read_text(encoding="utf-8-sig")
	expected_status = "MERGED" if mappings else "NO_MERGE"
	if source_task not in complete_text or f"STATUS: {expected_status}" not in complete_text:
		raise WorkspaceError(
			f"Consolidation completion marker must name {source_task} and "
			f"contain STATUS: {expected_status}"
		)
	validate_dependency_graph(states)
	validate_superseded_graph(root, states, superseded)
	validate_split_graph(root, states, superseded, splits)
	if not mappings:
		if receipt is not None:
			raise WorkspaceError("A no-merge consolidation must not publish a receipt")
		return
	if receipt is None:
		raise WorkspaceError("A merged consolidation requires a receipt")
	receipt_path = root / receipt
	if not receipt_path.is_file():
		raise WorkspaceError(f"Missing consolidation receipt: {receipt}")
	receipt_text = receipt_path.read_text(encoding="utf-8-sig")
	targets = {}
	for old_id, new_id in mappings.items():
		alias = superseded.get(old_id)
		if alias is None or alias["superseded_by"] != new_id:
			raise WorkspaceError(
				f"Missing exact superseded mapping {old_id} -> {new_id}"
			)
		if alias["receipt"] != receipt:
			raise WorkspaceError(
				f"Superseded task {old_id} names the wrong receipt"
			)
		new_task = states.get(new_id)
		if new_task is None:
			raise WorkspaceError(f"Replacement task does not exist: {new_id}")
		if (
			new_task["status"] != "todo"
			or any(new_task[field] is not None for field in (
				"claimed_by",
				"claimed_at",
				"claim_order",
				"lease_until",
				"phase",
			))
		):
			raise WorkspaceError(
				f"Replacement task is not pristine unclaimed todo work: {new_id}"
			)
		if (
			alias["type"] != new_task["type"]
			or alias["project"] != new_task["project"]
		):
			raise WorkspaceError(
				f"Replacement changes type or project for {old_id}: {new_id}"
			)
		if new_task["inbox_receipt"] != receipt:
			raise WorkspaceError(f"Replacement task names the wrong receipt: {new_id}")
		if old_id not in receipt_text or new_id not in receipt_text:
			raise WorkspaceError(
				f"Consolidation receipt omits mapping {old_id} -> {new_id}"
			)
		targets.setdefault(new_id, []).append(old_id)
	for new_id, old_ids in targets.items():
		if len(old_ids) < 2:
			raise WorkspaceError(
				f"Replacement {new_id} consolidates fewer than two tasks"
			)
	for project_path in sorted((root / "projects").glob("*/tasks.md")):
		text = project_path.read_text(encoding="utf-8-sig")
		for old_id in mappings:
			if f"tasks/{old_id}/task.md" in text:
				raise WorkspaceError(
					f"Project index still links superseded task {old_id}: {project_path}"
				)


def consolidation_validation_for_head(slot):
	subject = run_git(slot, "show", "-s", "--format=%s", "HEAD").stdout.strip()
	prefix = "Consolidate pending tasks after "
	if not subject.startswith(prefix):
		return None
	source_task = subject[len(prefix):]
	if not TASK_ID_PATTERN.fullmatch(source_task):
		raise WorkspaceError(f"Invalid consolidation commit subject: {subject!r}")
	changed = literal_paths(
		slot,
		"diff-tree",
		"--no-commit-id",
		"--name-only",
		"-r",
		"HEAD^",
		"HEAD",
	)
	superseded = load_superseded(slot)
	mappings = {}
	for path in changed:
		if not path.endswith("/superseded.yaml"):
			continue
		parts = PurePosixPath(path).parts
		if len(parts) != 6 or parts[0] != "tasks":
			raise WorkspaceError(
				f"Invalid superseded path in consolidation commit: {path}"
			)
		old_id = "/".join(parts[1:5])
		alias = superseded.get(old_id)
		if alias is None:
			raise WorkspaceError(
				f"Consolidation commit removed superseded alias: {old_id}"
			)
		mappings[old_id] = alias["superseded_by"]
	receipts = {
		superseded[old_id]["receipt"]
		for old_id in mappings
	}
	if len(receipts) > 1:
		raise WorkspaceError("Consolidation mappings name different receipts")
	receipt = next(iter(receipts), None)
	return lambda root: validate_consolidation_tree(
		root,
		source_task,
		mappings,
		receipt,
	)


def command_task_content_digest(args):
	config = worktree_config(args, create=True)
	slot = Path(config["slot_worktree"])
	path = state_path(slot, args.task).parent
	print(json.dumps({
		"content_sha256": retained_task_digest(path),
		"task": args.task,
	}, indent=2, sort_keys=True))


def command_consolidate_publish(args):
	config = worktree_config(args, create=True)
	slot = Path(config["slot_worktree"])
	if not TASK_ID_PATTERN.fullmatch(args.source_task):
		raise WorkspaceError(f"Invalid source task: {args.source_task!r}")
	mappings = {}
	for value in args.mappings:
		old_id, new_id = parse_consolidation_mapping(value)
		if old_id in mappings:
			raise WorkspaceError(f"Duplicate consolidation source task: {old_id}")
		mappings[old_id] = new_id
	receipt = normalized_publish_path(args.receipt) if args.receipt else None
	if receipt is not None and not receipt.startswith("receipts/"):
		raise WorkspaceError("The consolidation receipt must be below receipts/")
	if mappings and receipt is None:
		raise WorkspaceError("A merged consolidation requires a receipt")
	if not mappings and receipt is not None:
		raise WorkspaceError("A no-merge consolidation must not publish a receipt")
	paths = sorted({normalized_publish_path(value) for value in args.paths})
	complete = f"tasks/{args.source_task}/{CONSOLIDATION_COMPLETE}"
	if not path_is_covered(complete, paths):
		raise WorkspaceError(
			"The consolidation completion marker is not covered by a publication path"
		)
	if receipt is not None and not path_is_covered(receipt, paths):
		raise WorkspaceError(
			"The consolidation receipt is not covered by a publication path"
		)
	changes = changed_paths(slot)
	unexpected = [path for path in changes if not path_is_covered(path, paths)]
	if unexpected:
		raise WorkspaceError(
			"Consolidation changes are outside the explicit publication paths: "
			+ ", ".join(unexpected)
		)
	changed_aliases = {
		"/".join(PurePosixPath(path).parts[1:5])
		for path in changes
		if path.endswith("/superseded.yaml")
	}
	if changed_aliases != set(mappings):
		raise WorkspaceError(
			"Changed superseded aliases do not match explicit mappings: "
			+ ", ".join(sorted(changed_aliases ^ set(mappings)))
		)
	for old_id in mappings:
		required = {
			f"tasks/{old_id}/state.yaml",
			f"tasks/{old_id}/superseded.yaml",
		}
		if not required.issubset(changes):
			raise WorkspaceError(
				f"Consolidation does not retire {old_id} with state and alias changes"
			)
		old_prefix = f"tasks/{old_id}/"
		unexpected_old = [
			path for path in changes
			if path.startswith(old_prefix) and path not in required
		]
		if unexpected_old:
			raise WorkspaceError(
				f"Consolidation modifies retained content for {old_id}: "
				+ ", ".join(unexpected_old)
			)
	source_allowed = {
		f"tasks/{args.source_task}/{CONSOLIDATION_PENDING}",
		f"tasks/{args.source_task}/{CONSOLIDATION_COMPLETE}",
	}
	if changes and not source_allowed.issubset(changes):
		raise WorkspaceError(
			"Consolidation must replace the pending marker with a completion marker"
		)
	unexpected_source = [
		path for path in changes
		if path.startswith(f"tasks/{args.source_task}/")
		and path not in source_allowed
	]
	if unexpected_source:
		raise WorkspaceError(
			"Consolidation modifies discovery source content: "
			+ ", ".join(unexpected_source)
		)
	if not mappings:
		unexpected_no_merge = [
			path for path in changes
			if path not in source_allowed
		]
		if unexpected_no_merge:
			raise WorkspaceError(
				"No-merge consolidation changes non-marker paths: "
				+ ", ".join(unexpected_no_merge)
			)
	else:
		for new_id in set(mappings.values()):
			required = {
				f"tasks/{new_id}/task.md",
				f"tasks/{new_id}/state.yaml",
			}
			if not required.issubset(changes):
				raise WorkspaceError(
					f"Replacement task is not newly created: {new_id}"
				)
		if receipt not in changes:
			raise WorkspaceError(
				f"Consolidation receipt is not newly written: {receipt}"
			)
	counts = unpublished_counts(config)
	if changes and counts["slot_only"]:
		raise WorkspaceError(
			"The AI slot has unpublished commits before consolidation staging"
		)
	validate = lambda root: validate_consolidation_tree(
		root,
		args.source_task,
		mappings,
		receipt,
	)
	validate(slot)
	committed = False
	if changes:
		for path in paths:
			if path_is_stageable(slot, path):
				run_git(slot, "add", "-A", "--", path)
		unstaged = literal_paths(slot, "diff", "--name-only")
		untracked = literal_paths(
			slot,
			"ls-files",
			"--others",
			"--exclude-standard",
		)
		if unstaged or untracked:
			raise WorkspaceError(
				"Consolidation changes remain unstaged: "
				+ ", ".join(sorted(set(unstaged + untracked)))
			)
		if run_git(slot, "diff", "--cached", "--quiet", check=False).returncode == 0:
			raise WorkspaceError("No consolidation state changed")
		run_git(
			slot,
			"commit",
			"-m",
			f"Consolidate pending tasks after {args.source_task}",
		)
		committed = True
	elif not counts["slot_only"]:
		raise WorkspaceError("No consolidation changes or unpublished commit")
	published = publish_slot(config, validate=validate)
	print(json.dumps({
		"committed": committed,
		"mappings": mappings,
		"published": bool(published),
		"receipt": receipt,
		"source_task": args.source_task,
	}, indent=2, sort_keys=True))


def validate_receipt_text(text, metadata, label):
	if metadata["digest"] not in text:
		raise WorkspaceError(
			f"{label} does not contain the inbox digest: {metadata['digest']}"
		)


def command_inbox_publish(args):
	config = inbox_worktree_config(args, create=True)
	_, metadata = load_transaction(args.transaction)
	if metadata["checkout_tag"] != config["checkout_tag"]:
		raise WorkspaceError("The inbox transaction belongs to another checkout")
	if Path(metadata["ai_main"]).resolve() != Path(config["ai_main"]).resolve():
		raise WorkspaceError(
			"The inbox transaction belongs to another ai-tdesktop repository"
		)
	worktree = Path(config["inbox_worktree"])
	receipt = normalized_publish_path(args.receipt)
	if not receipt.startswith("receipts/"):
		raise WorkspaceError("The tracked receipt must be below receipts/")
	if not (worktree / receipt).is_file():
		raise WorkspaceError(
			f"Tracked receipt is missing from the inbox worktree: {receipt}"
		)
	validate_receipt_text(
		(worktree / receipt).read_text(encoding="utf-8-sig"),
		metadata,
		"Tracked receipt",
	)
	paths = sorted({normalized_publish_path(value) for value in args.paths})
	if not path_is_covered(receipt, paths):
		raise WorkspaceError(
			"The tracked receipt is not covered by an explicit publication path"
		)
	changes = changed_paths(worktree)
	unexpected = [path for path in changes if not path_is_covered(path, paths)]
	if unexpected:
		raise WorkspaceError(
			"Inbox worktree changes are outside the explicit publication paths: "
			+ ", ".join(unexpected)
		)
	unstaged_before = set(literal_paths(worktree, "diff", "--name-only"))
	unstaged_before.update(literal_paths(
		worktree,
		"ls-files",
		"--others",
		"--exclude-standard",
	))
	for path in paths:
		if (
			(worktree / path).exists()
			or any(path_is_covered(change, [path]) for change in unstaged_before)
		):
			run_git(worktree, "add", "-A", "--", path)
	unstaged = literal_paths(worktree, "diff", "--name-only")
	untracked = literal_paths(
		worktree,
		"ls-files",
		"--others",
		"--exclude-standard",
	)
	if unstaged or untracked:
		raise WorkspaceError(
			"Inbox worktree changes remain unstaged: "
			+ ", ".join(sorted(set(unstaged + untracked)))
		)
	committed = run_git(
		worktree,
		"diff",
		"--cached",
		"--quiet",
		check=False,
	).returncode != 0
	if committed:
		run_git(
			worktree,
			"commit",
			"-m",
			f"Process inbox for {config['checkout_tag']}",
		)
	published = publish_inbox(config)
	print(json.dumps({
		"committed": committed,
		"inbox_branch": config["inbox_branch"],
		"inbox_worktree": config["inbox_worktree"],
		"published": bool(published),
		"receipt": receipt,
	}, indent=2, sort_keys=True))


def rewrite_project_links(directory, pattern, replacement):
	for path in sorted(directory.rglob("*.md")):
		data = path.read_bytes()
		if data.startswith(b"\xef\xbb\xbf"):
			raise WorkspaceError(f"Refusing to preserve a UTF-8 BOM in {path}")
		text = data.decode("utf-8")
		replaced = pattern.sub(replacement, text)
		if replaced != text:
			path.write_bytes(replaced.encode("utf-8"))


def project_last_activity(states):
	activity = {}
	unfinished = set()
	for task in states.values():
		project = task["project"]
		if project is None:
			continue
		try:
			created = datetime.date.fromisoformat(task["created"])
		except ValueError as error:
			raise WorkspaceError(
				f"Invalid created date in {task['id']}: {task['created']!r}"
			) from error
		if project not in activity or created > activity[project]:
			activity[project] = created
		if task["status"] != "approved":
			unfinished.add(project)
	return activity, unfinished


def command_archive_stale(args):
	if args.days < 1:
		raise WorkspaceError("The archive threshold must be at least one day")
	config = worktree_config(args, create=True)
	sync_canonical(config)
	slot = Path(config["slot_worktree"])
	activity, unfinished = project_last_activity(load_states(slot))
	cutoff = datetime.date.today() - datetime.timedelta(days=args.days)
	projects = slot / "projects"
	candidates = sorted(
		entry.name for entry in projects.iterdir()
		if entry.is_dir() and entry.name != PROJECT_ARCHIVE_DIR
	) if projects.is_dir() else []
	archived = []
	published = False
	for slug in candidates:
		if slug in unfinished or slug not in activity or activity[slug] > cutoff:
			continue
		target = projects / PROJECT_ARCHIVE_DIR / slug
		if target.exists():
			raise WorkspaceError(f"The archive already contains a project: {slug}")
		target.parent.mkdir(exist_ok=True)
		run_git(
			slot,
			"mv",
			f"projects/{slug}",
			f"projects/{PROJECT_ARCHIVE_DIR}/{slug}",
		)
		rewrite_project_links(target, PROJECT_LINK_PATTERN, "](../../../")
		published = commit_paths(
			config,
			[f"projects/{PROJECT_ARCHIVE_DIR}/{slug}"],
			f"Archive {slug}",
		)
		archived.append(slug)
	print(json.dumps({
		"archived": archived,
		"days": args.days,
		"published": bool(published),
	}, indent=2, sort_keys=True))


def unarchive_project(config, worktree_key, slug):
	if slug == PROJECT_ARCHIVE_DIR or not TAG_PATTERN.fullmatch(slug):
		raise WorkspaceError(f"Invalid project slug: {slug!r}")
	worktree = Path(config[worktree_key])
	archived = worktree / "projects" / PROJECT_ARCHIVE_DIR / slug
	target = worktree / "projects" / slug
	if not archived.is_dir():
		raise WorkspaceError(f"No archived project: {slug}")
	if target.exists():
		raise WorkspaceError(f"The project is already live: {slug}")
	run_git(
		worktree,
		"mv",
		f"projects/{PROJECT_ARCHIVE_DIR}/{slug}",
		f"projects/{slug}",
	)
	rewrite_project_links(target, ARCHIVED_PROJECT_LINK_PATTERN, "](../../")
	run_git(worktree, "add", "--", f"projects/{slug}")
	archive_root = worktree / "projects" / PROJECT_ARCHIVE_DIR
	if archive_root.is_dir() and not any(archive_root.iterdir()):
		archive_root.rmdir()
	print(json.dumps({
		"project": slug,
		"published": False,
		"unarchived": True,
	}, indent=2, sort_keys=True))


def command_unarchive(args):
	config = worktree_config(args, create=True)
	unarchive_project(config, "slot_worktree", args.project)


def command_inbox_unarchive(args):
	config = inbox_worktree_config(args, create=True)
	unarchive_project(config, "inbox_worktree", args.project)


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


def command_inbox_ensure(args):
	config = inbox_worktree_config(args, create=True)
	print(json.dumps(config, indent=2, sort_keys=True))


def command_prepare(args):
	config = inbox_worktree_config(args, create=True)
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
		if metadata["checkout_tag"] != config["checkout_tag"]:
			raise WorkspaceError(
				"Active inbox transaction belongs to "
				f"{metadata['checkout_tag']}, not {config['checkout_tag']}"
			)
		if Path(metadata["ai_main"]).resolve() != Path(config["ai_main"]).resolve():
			raise WorkspaceError(
				"Active inbox transaction belongs to another ai-tdesktop repository"
			)
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
	sync_inbox_canonical(config)

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
	main = Path(metadata["ai_main"]).resolve()
	receipt_value = normalized_publish_path(args.receipt)
	if not receipt_value.startswith("receipts/"):
		raise WorkspaceError("The tracked receipt must be below receipts/")
	if git_root(main) != main:
		raise WorkspaceError(f"ai-tdesktop is not a worktree root: {main}")
	main_branch = run_git(main, "branch", "--show-current").stdout.strip()
	if main_branch != "master":
		raise WorkspaceError(
			f"The main ai-tdesktop worktree must be on master, not {main_branch!r}."
		)
	tracked = run_git(main, "show", f"HEAD:{receipt_value}", check=False)
	if tracked.returncode:
		raise WorkspaceError(
			f"Tracked receipt is missing from master HEAD: {main / receipt_value}"
		)
	validate_receipt_text(tracked.stdout, metadata, "Tracked receipt on master")
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
		receipt_value,
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

	inbox_ensure = subparsers.add_parser("inbox-ensure")
	add_common_arguments(inbox_ensure)
	inbox_ensure.set_defaults(handler=command_inbox_ensure)

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

	start = subparsers.add_parser("start")
	add_common_arguments(start)
	start.add_argument("--task", required=True)
	start.add_argument("--require", action="append", default=[])
	start.set_defaults(handler=command_start)

	source_lineage = subparsers.add_parser("source-lineage")
	add_common_arguments(source_lineage)
	source_lineage.add_argument("--task", required=True)
	source_lineage.add_argument("--require", action="append", default=[])
	source_lineage.set_defaults(handler=command_source_lineage)

	retry = subparsers.add_parser("retry")
	add_common_arguments(retry)
	retry.add_argument("--task", required=True)
	retry.add_argument("--require", action="append", default=[])
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

	source_commit = subparsers.add_parser("source-commit")
	add_common_arguments(source_commit)
	source_commit.add_argument("--task", required=True)
	source_commit.add_argument("--subject", required=True)
	source_commit.add_argument("--mark-green", action="store_true")
	source_commit.set_defaults(handler=command_source_commit)

	source_verify_commit = subparsers.add_parser("source-verify-commit")
	add_common_arguments(source_verify_commit)
	source_verify_commit.add_argument("--task", required=True)
	source_verify_commit.add_argument("--ref", default="HEAD")
	source_verify_commit.set_defaults(handler=command_source_verify_commit)

	source_preflight = subparsers.add_parser("source-preflight")
	add_common_arguments(source_preflight)
	source_preflight.add_argument("--task", required=True)
	source_preflight.add_argument("--exe")
	source_preflight.set_defaults(handler=command_source_preflight)

	build_lock_recover = subparsers.add_parser("build-lock-recover")
	build_lock_recover.add_argument("--source-root", required=True)
	build_lock_recover.add_argument("--build-root", required=True)
	build_lock_recover.add_argument("--exe", required=True)
	build_lock_recover.add_argument("--artifact", action="append", required=True)
	build_lock_recover.add_argument("--wait", type=float, default=10.0)
	build_lock_recover.set_defaults(handler=command_build_lock_recover)

	overlay_save = subparsers.add_parser("overlay-save")
	add_common_arguments(overlay_save)
	overlay_save.add_argument("--task", required=True)
	overlay_save.add_argument(
		"--restore",
		choices=("run", "green", "none"),
		default="run",
	)
	overlay_save.set_defaults(handler=command_overlay_save)

	overlay_apply = subparsers.add_parser("overlay-apply")
	add_common_arguments(overlay_apply)
	overlay_apply.add_argument("--task", required=True)
	overlay_apply.set_defaults(handler=command_overlay_apply)

	test_run = subparsers.add_parser("test-run")
	test_run.add_argument("--exe", required=True)
	test_run.add_argument("--run-dir", required=True)
	test_run.add_argument("--portable-root")
	test_run.add_argument("--deadline", type=float, default=120.0)
	test_run.add_argument("--quiet", type=float, default=60.0)
	test_run.add_argument("--grace", type=float, default=15.0)
	test_run.add_argument("--env", action="append")
	test_run.set_defaults(handler=command_test_run)

	test_cleanup = subparsers.add_parser("test-cleanup")
	test_cleanup.add_argument("--exe", required=True)
	test_cleanup.add_argument("--delete-exe", action="store_true")
	test_cleanup.set_defaults(handler=command_test_cleanup)

	test_account_reset = subparsers.add_parser("test-account-reset")
	test_account_reset.add_argument("--exe", required=True)
	test_account_reset.add_argument("--portable-root")
	test_account_reset.set_defaults(handler=command_test_account_reset)

	fence_create = subparsers.add_parser("fence-create")
	fence_create.add_argument("--file", required=True)
	fence_create.add_argument("--root", default=".")
	fence_create.add_argument("paths", nargs="*")
	fence_create.set_defaults(handler=command_fence_create)

	fence_check = subparsers.add_parser("fence-check")
	fence_check.add_argument("--file", required=True)
	fence_check.add_argument("--root", default=".")
	fence_check.set_defaults(handler=command_fence_check)

	finish = subparsers.add_parser("finish")
	add_common_arguments(finish)
	finish.add_argument("--task", required=True)
	finish.add_argument(
		"--status",
		choices=("approved", "blocked", "split-required"),
		required=True,
	)
	finish.add_argument("--model", required=True)
	finish.set_defaults(handler=command_finish)

	publish = subparsers.add_parser("publish")
	add_common_arguments(publish)
	publish.set_defaults(handler=command_publish)

	inbox_publish = subparsers.add_parser("inbox-publish")
	add_common_arguments(inbox_publish)
	inbox_publish.add_argument("--transaction", required=True)
	inbox_publish.add_argument("--receipt", required=True)
	inbox_publish.add_argument(
		"--path",
		action="append",
		dest="paths",
		required=True,
	)
	inbox_publish.set_defaults(handler=command_inbox_publish)

	consolidate_publish = subparsers.add_parser("consolidate-publish")
	add_common_arguments(consolidate_publish)
	consolidate_publish.add_argument("--source-task", required=True)
	consolidate_publish.add_argument("--receipt")
	consolidate_publish.add_argument(
		"--mapping",
		action="append",
		dest="mappings",
		default=[],
	)
	consolidate_publish.add_argument(
		"--path",
		action="append",
		dest="paths",
		required=True,
	)
	consolidate_publish.set_defaults(handler=command_consolidate_publish)

	split_publish = subparsers.add_parser("split-publish")
	add_common_arguments(split_publish)
	split_publish.add_argument("--source-task", required=True)
	split_publish.add_argument("--receipt", required=True)
	split_publish.add_argument(
		"--replacement",
		action="append",
		dest="replacements",
		required=True,
	)
	split_publish.add_argument("--implementation-carrier")
	split_publish.add_argument(
		"--path",
		action="append",
		dest="paths",
		required=True,
	)
	split_publish.set_defaults(handler=command_split_publish)

	task_content_digest = subparsers.add_parser("task-content-digest")
	add_common_arguments(task_content_digest)
	task_content_digest.add_argument("--task", required=True)
	task_content_digest.set_defaults(handler=command_task_content_digest)

	archive_stale = subparsers.add_parser("archive-stale")
	add_common_arguments(archive_stale)
	archive_stale.add_argument("--days", type=int, default=90)
	archive_stale.set_defaults(handler=command_archive_stale)

	unarchive = subparsers.add_parser("unarchive")
	add_common_arguments(unarchive)
	unarchive.add_argument("--project", required=True)
	unarchive.set_defaults(handler=command_unarchive)

	inbox_unarchive = subparsers.add_parser("inbox-unarchive")
	add_common_arguments(inbox_unarchive)
	inbox_unarchive.add_argument("--project", required=True)
	inbox_unarchive.set_defaults(handler=command_inbox_unarchive)

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
