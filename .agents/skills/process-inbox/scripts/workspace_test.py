#!/usr/bin/env python3

import contextlib
import datetime
import io
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import workspace


TASK_ID = "2026/07/19/correct-recent-search-peer-actions"


class FrozenDate(datetime.date):
	@classmethod
	def today(cls):
		return cls(2026, 7, 20)


def task_state(status, claimed_by="macbook-twork"):
	return {
		"id": TASK_ID,
		"title": "Correct recent-search peer actions",
		"status": status,
		"created": "2026-07-19",
		"project": None,
		"depends_on": [],
		"claimed_by": claimed_by,
		"claimed_at": "2026-07-19T14:28:01+04:00",
		"claim_order": 1,
		"lease_until": None,
		"phase": "blocked" if status == "blocked" else None,
	}


def write_task(slot, status="blocked", claimed_by="macbook-twork"):
	directory = slot / "tasks" / TASK_ID
	(directory / "work").mkdir(parents=True)
	(directory / "task.md").write_text(
		"# Correct recent-search peer actions\n",
		encoding="utf-8",
	)
	owner = claimed_by or "null"
	claimed_at = "2026-07-19T14:28:01+04:00" if claimed_by else "null"
	claim_order = "1" if claimed_by else "null"
	phase = "blocked" if status == "blocked" else (
		"setup" if status == "in-progress" else "null"
	)
	(directory / "state.yaml").write_text(
		f"""status: {status}
created: 2026-07-19
project: null
depends_on: []
claimed_by: {owner}
claimed_at: {claimed_at}
claim_order: {claim_order}
lease_until: null
phase: {phase}
inbox_receipt: receipts/2026/07/19/test.md
""",
		encoding="utf-8",
	)
	return directory


def write_project(slot, slug):
	directory = slot / "projects" / slug
	directory.mkdir(parents=True)
	(directory / "project.md").write_text(f"# {slug}\n", encoding="utf-8")
	(directory / "tasks.md").write_text(
		"# Tasks\n\n- [Task](../../tasks/2026/01/10/some-task/task.md)\n",
		encoding="utf-8",
	)
	return directory


def write_project_task(slot, task_id, project, status, created):
	directory = slot / "tasks" / task_id
	directory.mkdir(parents=True)
	(directory / "task.md").write_text(
		f"# {task_id.rsplit('/', 1)[-1]}\n",
		encoding="utf-8",
	)
	claimed = status != "todo"
	(directory / "state.yaml").write_text(
		f"""status: {status}
created: {created}
project: {project}
depends_on: []
claimed_by: {"macbook-twork" if claimed else "null"}
claimed_at: {f"{created}T10:00:00+04:00" if claimed else "null"}
claim_order: {"1" if claimed else "null"}
lease_until: null
phase: {"complete" if status == "approved" else "null"}
inbox_receipt: receipts/2026/01/10/test.md
""",
		encoding="utf-8",
	)
	return directory


def git(repo, *args):
	return subprocess.run(
		["git", "-C", str(repo), *args],
		check=True,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		text=True,
	).stdout.strip()


def git_repo(path):
	path.mkdir(parents=True, exist_ok=True)
	git(path, "init")
	git(path, "config", "user.name", "Workflow Test")
	git(path, "config", "user.email", "workflow@example.invalid")


def inbox_worktrees(root):
	main = root / "ai-main"
	main.mkdir()
	git(main, "init", "--initial-branch=master")
	git(main, "config", "user.name", "Workflow Test")
	git(main, "config", "user.email", "workflow@example.invalid")
	(main / ".gitignore").write_text("inbox/\n", encoding="utf-8")
	(main / "AGENTS.md").write_text("# AI tasks\n", encoding="utf-8")
	active = main / "tasks" / "2026/07/18/active-task"
	active.mkdir(parents=True)
	(active / "task.md").write_text("# Active task\n", encoding="utf-8")
	(active / "state.yaml").write_text(
		"""status: todo
created: 2026-07-18
project: null
depends_on: []
claimed_by: null
claimed_at: null
claim_order: null
lease_until: null
phase: null
inbox_receipt: receipts/2026/07/18/seed.md
""",
		encoding="utf-8",
	)
	git(main, "add", ".gitignore", "AGENTS.md", "tasks")
	git(main, "commit", "-m", "Create AI workspace")
	worktrees = root / "worktrees"
	worktrees.mkdir()
	slot = worktrees / "macbook-twork"
	inbox_worktree = worktrees / "macbook-twork-inbox"
	git(
		main,
		"worktree",
		"add",
		"-b",
		"slot/macbook-twork",
		str(slot),
		"master",
	)
	git(
		main,
		"worktree",
		"add",
		"-b",
		"inbox/macbook-twork",
		str(inbox_worktree),
		"master",
	)
	inbox = main / "inbox"
	(inbox / "backup").mkdir(parents=True)
	(inbox / "inbox.md").write_text("Plan a parallel-safe task.\n", encoding="utf-8")
	return {
		"source_root": str(root / "tdesktop"),
		"machine_tag": "macbook",
		"checkout_folder": "twork",
		"checkout_tag": "macbook-twork",
		"ai_main": str(main),
		"worktrees_root": str(worktrees),
		"slot_worktree": str(slot),
		"slot_branch": "slot/macbook-twork",
		"inbox_worktree": str(inbox_worktree),
		"inbox_branch": "inbox/macbook-twork",
		"inbox": str(inbox),
	}


class WorkspaceTest(unittest.TestCase):
	def test_inbox_publication_paths_must_be_specific(self):
		for value in (
			"tasks",
			"projects",
			"projects/archive",
			"receipts/2026/07",
		):
			with self.subTest(value=value):
				with self.assertRaises(workspace.WorkspaceError):
					workspace.normalized_publish_path(value)
		self.assertEqual(
			workspace.normalized_publish_path(
				"tasks/2026/07/19/parallel-safe"
			),
			"tasks/2026/07/19/parallel-safe",
		)
		self.assertEqual(
			workspace.normalized_publish_path(
				"receipts/2026/07/19/parallel-safe.md"
			),
			"receipts/2026/07/19/parallel-safe.md",
		)

	def test_only_non_fast_forward_push_failures_are_retryable(self):
		self.assertTrue(workspace.retryable_push_failure(
			"! [rejected] HEAD -> master (fetch first)"
		))
		self.assertTrue(workspace.retryable_push_failure(
			"! [rejected] HEAD -> master (non-fast-forward)"
		))
		self.assertFalse(workspace.retryable_push_failure(
			"! [remote rejected] HEAD -> master (protected branch hook declined)"
		))

	def test_inbox_worktree_config_does_not_create_task_slot(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source = root / "twork"
			source.mkdir()
			git(source, "init", "--initial-branch=master")
			build = source / "Telegram" / "build"
			build.mkdir(parents=True)
			(build / "ai-machine-tag").write_text("macbook\n", encoding="utf-8")
			main = root / "ai-main"
			main.mkdir()
			git(main, "init", "--initial-branch=master")
			git(main, "config", "user.name", "Workflow Test")
			git(main, "config", "user.email", "workflow@example.invalid")
			(main / "AGENTS.md").write_text("# AI tasks\n", encoding="utf-8")
			git(main, "add", "AGENTS.md")
			git(main, "commit", "-m", "Create AI workspace")
			worktrees = root / "worktrees"
			args = SimpleNamespace(
				source_root=str(source),
				ai_main=str(main),
				worktrees_root=str(worktrees),
			)

			config = workspace.inbox_worktree_config(args, create=True)

			self.assertTrue(Path(config["inbox_worktree"]).is_dir())
			self.assertFalse((worktrees / "macbook-twork").exists())
			self.assertNotIn("slot_worktree", config)

	def test_inbox_lifecycle_ignores_dirty_task_slot(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			slot = Path(config["slot_worktree"])
			inbox_worktree = Path(config["inbox_worktree"])
			active = slot / "tasks" / "2026/07/18/active-task"
			state = active / "state.yaml"
			state.write_text(
				state.read_text(encoding="utf-8").replace(
					"status: todo",
					"status: in-progress",
				),
				encoding="utf-8",
			)
			dirty = active / "work" / "live.txt"
			dirty.parent.mkdir()
			dirty.write_text("active implementation\n", encoding="utf-8")

			prepared = io.StringIO()
			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				contextlib.redirect_stdout(prepared),
			):
				workspace.command_prepare(SimpleNamespace())
			prepared_result = json.loads(prepared.getvalue())
			transaction = prepared_result["transaction"]

			write_task(inbox_worktree, status="todo", claimed_by=None)
			receipt = "receipts/2026/07/19/parallel-safe.md"
			receipt_path = inbox_worktree / receipt
			receipt_path.parent.mkdir(parents=True)
			receipt_path.write_text(
				f"# Inbox receipt\n\nInbox digest: {prepared_result['digest']}\n",
				encoding="utf-8",
			)
			published = io.StringIO()
			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				contextlib.redirect_stdout(published),
			):
				workspace.command_inbox_publish(SimpleNamespace(
					transaction=transaction,
					receipt=receipt,
					paths=[
						f"tasks/{TASK_ID}",
						receipt,
					],
				))

			result = json.loads(published.getvalue())
			self.assertTrue(result["committed"])
			self.assertTrue(result["published"])
			self.assertTrue((Path(config["ai_main"]) / receipt).is_file())
			self.assertTrue(dirty.is_file())
			self.assertIn(
				state.relative_to(slot).as_posix(),
				workspace.changed_paths(slot),
			)
			self.assertIn(
				dirty.relative_to(slot).as_posix(),
				workspace.changed_paths(slot),
			)

			finalized = io.StringIO()
			with contextlib.redirect_stdout(finalized):
				workspace.command_finalize(SimpleNamespace(
					transaction=transaction,
					receipt=receipt,
				))
			final = json.loads(finalized.getvalue())
			self.assertTrue(final["cleared"])
			self.assertEqual(
				(Path(config["inbox"]) / "inbox.md").read_text(encoding="utf-8"),
				"",
			)
			self.assertTrue(Path(final["backup"]).is_dir())

	def test_inbox_publish_handles_restored_project_deletion(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			main = Path(config["ai_main"])
			archived = main / "projects" / "archive" / "legacy"
			archived.mkdir(parents=True)
			(archived / "project.md").write_text(
				"# Legacy project\n",
				encoding="utf-8",
			)
			(archived / "tasks.md").write_text("# Tasks\n", encoding="utf-8")
			git(main, "add", "projects/archive/legacy")
			git(main, "commit", "-m", "Archive legacy project")

			prepared = io.StringIO()
			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				contextlib.redirect_stdout(prepared),
			):
				workspace.command_prepare(SimpleNamespace())
			result = json.loads(prepared.getvalue())
			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_inbox_unarchive(
					SimpleNamespace(project="legacy")
				)

			receipt = "receipts/2026/07/19/restored-project.md"
			receipt_path = Path(config["inbox_worktree"]) / receipt
			receipt_path.parent.mkdir(parents=True)
			receipt_path.write_text(
				f"# Inbox receipt\n\nInbox digest: {result['digest']}\n",
				encoding="utf-8",
			)
			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_inbox_publish(SimpleNamespace(
					transaction=result["transaction"],
					receipt=receipt,
					paths=[
						"projects/legacy",
						"projects/archive/legacy",
						receipt,
					],
				))

			self.assertTrue((main / "projects" / "legacy").is_dir())
			self.assertFalse((main / "projects" / "archive" / "legacy").exists())

	def test_finalize_rejects_receipt_outside_master(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			prepared = io.StringIO()
			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				contextlib.redirect_stdout(prepared),
			):
				workspace.command_prepare(SimpleNamespace())
			result = json.loads(prepared.getvalue())
			(root / "outside.md").write_text(
				f"Inbox digest: {result['digest']}\n",
				encoding="utf-8",
			)

			with self.assertRaises(workspace.WorkspaceError):
				workspace.command_finalize(SimpleNamespace(
					transaction=result["transaction"],
					receipt="../outside.md",
				))

			self.assertTrue(Path(result["transaction"]).is_dir())
			self.assertTrue(
				(Path(config["inbox"]) / "inbox.md")
				.read_text(encoding="utf-8")
				.strip()
			)

	def test_finalize_requires_receipt_from_master_head(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			prepared = io.StringIO()
			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				contextlib.redirect_stdout(prepared),
			):
				workspace.command_prepare(SimpleNamespace())
			result = json.loads(prepared.getvalue())
			receipt = "receipts/2026/07/19/untracked.md"
			receipt_path = Path(config["ai_main"]) / receipt
			receipt_path.parent.mkdir(parents=True)
			receipt_path.write_text(
				f"Inbox digest: {result['digest']}\n",
				encoding="utf-8",
			)

			with self.assertRaisesRegex(
				workspace.WorkspaceError,
				"master HEAD",
			):
				workspace.command_finalize(SimpleNamespace(
					transaction=result["transaction"],
					receipt=receipt,
				))

			self.assertTrue(Path(result["transaction"]).is_dir())
			self.assertTrue(
				(Path(config["inbox"]) / "inbox.md")
				.read_text(encoding="utf-8")
				.strip()
			)

	def test_prepare_rejects_another_checkouts_transaction(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			main = root / "ai-main"
			inbox = main / "inbox"
			transaction = inbox / "backup" / ".processing-existing"
			transaction.mkdir(parents=True)
			(inbox / "inbox.md").write_text("New note\n", encoding="utf-8")
			(transaction / "transaction.json").write_text(
				json.dumps({
					"checkout_tag": "other-checkout",
					"ai_main": str(main),
					"inbox": str(inbox),
					"digest": "digest",
				}),
				encoding="utf-8",
			)
			config = {
				"checkout_tag": "macbook-twork",
				"ai_main": str(main),
				"inbox": str(inbox),
			}

			with (
				mock.patch.object(
					workspace,
					"inbox_worktree_config",
					return_value=config,
				),
				self.assertRaisesRegex(
					workspace.WorkspaceError,
					"other-checkout",
				),
			):
				workspace.command_prepare(SimpleNamespace())

	def test_resolve_prefers_blocked_over_approved_history(self):
		blocked = task_state("blocked")
		approved = {
			**task_state("approved"),
			"id": "2026/07/18/correct-recent-search-peer-actions",
		}
		states = {task["id"]: task for task in (approved, blocked)}

		resolved = workspace.resolve_task(states, "correct-recent-search-peer-actions")

		self.assertEqual(resolved["id"], TASK_ID)

	def test_retry_reopens_owned_blocked_task_and_resets_routing_marker(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			directory = write_task(slot)
			routed = directory / "work" / "discovered-routed.md"
			routed.write_text("routed\n", encoding="utf-8")
			config = {
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
			}
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
				mock.patch.object(workspace, "commit_paths") as commit,
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_retry(SimpleNamespace(task=TASK_ID))

			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["status"], "in-progress")
			self.assertEqual(state["phase"], "resume")
			self.assertFalse(routed.exists())
			commit.assert_not_called()

	def test_start_atomically_assigns_unclaimed_todo(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			directory = write_task(slot, status="todo", claimed_by=None)
			config = {
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
			}
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
				mock.patch.object(workspace, "commit_paths", return_value=True) as commit,
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_start(SimpleNamespace(task=TASK_ID))

			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["status"], "in-progress")
			self.assertEqual(state["claimed_by"], "macbook-twork")
			self.assertIsNotNone(state["claimed_at"])
			self.assertEqual(state["claim_order"], 1)
			self.assertEqual(state["phase"], "setup")
			self.assertEqual(
				commit.call_args.args[2],
				f"Start {TASK_ID} on macbook-twork",
			)

	def test_checkpoint_updates_only_local_task_state(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			directory = write_task(slot, status="in-progress")
			config = {"checkout_tag": "macbook-twork"}
			with (
				mock.patch.object(
					workspace,
					"task_action_config",
					return_value=(config, slot),
				),
				mock.patch.object(workspace, "commit_paths") as commit,
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_checkpoint(
					SimpleNamespace(task=TASK_ID, phase="review")
				)

			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["phase"], "review")
			commit.assert_not_called()

	def test_normal_lifecycle_publishes_only_start_and_approve(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			slot = root / "slot"
			source = root / "source"
			source.mkdir()
			directory = write_task(slot, status="todo", claimed_by=None)
			config = {
				"ai_main": str(root / "main"),
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
				"source_root": str(source),
			}
			subjects = []

			def record_commit(_config, _paths, subject):
				subjects.append(subject)
				return True

			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
				mock.patch.object(workspace, "commit_paths", side_effect=record_commit),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_start(SimpleNamespace(task=TASK_ID))

			with (
				mock.patch.object(
					workspace,
					"task_action_config",
					return_value=(config, slot),
				),
				mock.patch.object(workspace, "commit_paths", side_effect=record_commit),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_checkpoint(
					SimpleNamespace(task=TASK_ID, phase="review")
				)

			(directory / "work" / "result.md").write_text(
				f"""# Task result: {TASK_ID}
STATUS: DONE
Verdict: APPROVED
Checkout: clean-buildable
""",
				encoding="utf-8",
			)
			with (
				mock.patch.object(
					workspace,
					"task_action_config",
					return_value=(config, slot),
				),
				mock.patch.object(workspace, "ensure_clean"),
				mock.patch.object(workspace, "validate_source_state"),
				mock.patch.object(workspace, "delete_source_refs"),
				mock.patch.object(workspace, "commit_paths", side_effect=record_commit),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_finish(
					SimpleNamespace(task=TASK_ID, status="approved")
				)

			self.assertEqual(subjects, [
				f"Start {TASK_ID} on macbook-twork",
				f"Approve {TASK_ID}",
			])
			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["status"], "approved")

	def test_retry_refuses_to_compete_with_active_task(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			write_task(slot)
			active = slot / "tasks" / "2026/07/19/active-task"
			active.mkdir(parents=True)
			(active / "task.md").write_text("# Active task\n", encoding="utf-8")
			(active / "state.yaml").write_text(
				"""status: in-progress
created: 2026-07-19
project: null
depends_on: []
claimed_by: macbook-twork
claimed_at: 2026-07-19T15:00:00+04:00
claim_order: 1
lease_until: null
phase: setup
inbox_receipt: receipts/2026/07/19/test.md
""",
				encoding="utf-8",
			)
			config = {
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
			}
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
			):
				with self.assertRaisesRegex(workspace.WorkspaceError, "already in progress"):
					workspace.command_retry(SimpleNamespace(task=TASK_ID))

	def test_retained_task_commit_can_precede_later_work(self):
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary)
			git(repo, "init")
			git(repo, "config", "user.name", "Workflow Test")
			git(repo, "config", "user.email", "workflow@example.invalid")
			tracked = repo / "tracked.txt"
			tracked.write_text("base\n", encoding="utf-8")
			git(repo, "add", "tracked.txt")
			git(repo, "commit", "-m", "Create baseline")
			base = git(repo, "rev-parse", "HEAD")
			tracked.write_text("task\n", encoding="utf-8")
			git(repo, "commit", "-am", "Correct peer actions", "-m", f"Task: {TASK_ID}")
			green = git(repo, "rev-parse", "HEAD")
			tracked.write_text("later\n", encoding="utf-8")
			git(repo, "commit", "-am", "Add later independent work")

			self.assertEqual(workspace.task_series_refs(repo, TASK_ID), (base, green))
			config = {"source_root": str(repo)}
			with (
				mock.patch.object(workspace, "task_action_config", return_value=(config, None)),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_source_begin(SimpleNamespace(task=TASK_ID))
			self.assertEqual(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "base")),
				base,
			)
			self.assertEqual(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "green")),
				green,
			)
			self.assertEqual(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "run")),
				git(repo, "rev-parse", "HEAD"),
			)
			workspace.validate_source_state(config, TASK_ID, True)
			workspace.delete_source_refs(config, TASK_ID, retain_implementation=True)
			self.assertIsNotNone(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "base"))
			)
			self.assertIsNotNone(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "green"))
			)
			self.assertIsNone(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "run"))
			)

	def test_source_begin_initializes_new_task_without_a_green_ref(self):
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary)
			git(repo, "init")
			git(repo, "config", "user.name", "Workflow Test")
			git(repo, "config", "user.email", "workflow@example.invalid")
			(repo / "tracked.txt").write_text("base\n", encoding="utf-8")
			git(repo, "add", "tracked.txt")
			git(repo, "commit", "-m", "Create baseline")
			config = {"source_root": str(repo)}
			with (
				mock.patch.object(workspace, "task_action_config", return_value=(config, None)),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_source_begin(SimpleNamespace(task=TASK_ID))

			head = git(repo, "rev-parse", "HEAD")
			self.assertEqual(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "base")),
				head,
			)
			self.assertEqual(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "run")),
				head,
			)
			self.assertIsNone(
				workspace.resolved_ref(repo, workspace.source_task_ref(TASK_ID, "green"))
			)

	def test_archive_stale_moves_only_old_fully_approved_projects(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			git_repo(slot)
			write_project(slot, "old-project")
			write_project(slot, "fresh-project")
			write_project(slot, "pending-project")
			write_project_task(
				slot, "2026/01/10/old-task", "old-project", "approved", "2026-01-10"
			)
			write_project_task(
				slot, "2026/07/19/fresh-task", "fresh-project", "approved", "2026-07-19"
			)
			write_project_task(
				slot, "2026/01/11/pending-task", "pending-project", "blocked", "2026-01-11"
			)
			git(slot, "add", ".")
			git(slot, "commit", "-m", "Seed projects")
			config = {
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
			}
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
				mock.patch.object(workspace, "commit_paths", return_value=True) as commit,
				mock.patch.object(workspace.datetime, "date", FrozenDate),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_archive_stale(SimpleNamespace(days=90))

			archived = slot / "projects" / "archive" / "old-project"
			self.assertTrue(archived.is_dir())
			self.assertFalse((slot / "projects" / "old-project").exists())
			self.assertIn(
				"](../../../tasks/",
				(archived / "tasks.md").read_text(encoding="utf-8"),
			)
			self.assertTrue((slot / "projects" / "fresh-project").is_dir())
			self.assertTrue((slot / "projects" / "pending-project").is_dir())
			commit.assert_called_once()
			self.assertEqual(
				commit.call_args.args[1],
				["projects/archive/old-project"],
			)
			self.assertEqual(commit.call_args.args[2], "Archive old-project")

	def test_archive_stale_without_candidates_reports_nothing(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			config = {
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
			}
			out = io.StringIO()
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
				mock.patch.object(workspace, "commit_paths") as commit,
				contextlib.redirect_stdout(out),
			):
				workspace.command_archive_stale(SimpleNamespace(days=90))

			commit.assert_not_called()
			self.assertIn('"archived": []', out.getvalue())

	def test_unarchive_restores_links_and_stages_the_project(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			git_repo(slot)
			directory = slot / "projects" / "archive" / "old-project"
			directory.mkdir(parents=True)
			(directory / "project.md").write_text("# Old project\n", encoding="utf-8")
			(directory / "tasks.md").write_text(
				"# Tasks\n\n- [Task](../../../tasks/2026/01/10/some-task/task.md)\n",
				encoding="utf-8",
			)
			git(slot, "add", ".")
			git(slot, "commit", "-m", "Seed archive")
			config = {"slot_worktree": str(slot)}
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_unarchive(SimpleNamespace(project="old-project"))

			restored = slot / "projects" / "old-project"
			self.assertIn(
				"](../../tasks/",
				(restored / "tasks.md").read_text(encoding="utf-8"),
			)
			self.assertFalse((slot / "projects" / "archive").exists())
			staged = git(slot, "diff", "--cached", "--name-only")
			self.assertIn("projects/old-project/tasks.md", staged)


if __name__ == "__main__":
	unittest.main()
