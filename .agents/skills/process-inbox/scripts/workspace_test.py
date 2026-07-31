#!/usr/bin/env python3

import contextlib
import datetime
import io
import json
import os
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


def write_fake_exe(path, script, windows_script):
	path.parent.mkdir(parents=True, exist_ok=True)
	if os.name == "nt":
		path = path.with_suffix(".cmd")
		path.write_text("@echo off\n" + windows_script, encoding="utf-8")
		return path
	path.write_text("#!/bin/sh\n" + script, encoding="utf-8")
	path.chmod(0o755)
	return path


def write_complete_markers_exe(path):
	return write_fake_exe(path, (
		'LOG="$TDESKTOP_TEST_EVIDENCE_DIR/test_log.txt"\n'
		'echo "TEST_STEP: open settings" >> "$LOG"\n'
		'echo "TEST_RESULT: PASS: row painted" >> "$LOG"\n'
		'echo "SCREENSHOT: /tmp/fake.png" >> "$LOG"\n'
		'echo "TEST_COMPLETE" >> "$LOG"\n'
		"exit 0\n"
	), (
		'set "LOG=%TDESKTOP_TEST_EVIDENCE_DIR%\\test_log.txt"\n'
		'echo TEST_STEP: open settings>>"%LOG%"\n'
		'echo TEST_RESULT: PASS: row painted>>"%LOG%"\n'
		'echo SCREENSHOT: /tmp/fake.png>>"%LOG%"\n'
		'echo TEST_COMPLETE>>"%LOG%"\n'
		"exit /b 0\n"
	))


def make_portable_root(root):
	debug = root / "out" / "Debug"
	golden = debug / workspace.PORTABLE_GOLDEN
	(golden / "tdata").mkdir(parents=True)
	(golden / "tdata" / "key_data").write_text("golden\n", encoding="utf-8")
	return debug


def plant_leftover_crash_state(live):
	dumps_dir = live / "tdata" / "dumps"
	dumps_dir.mkdir(parents=True, exist_ok=True)
	report = live / "tdata" / "working"
	report.write_bytes(b"Assertion: previous run\n" * 20)
	dump = dumps_dir / "stale.dmp"
	dump.write_bytes(b"MDMP stale minidump\n")
	return report, dump


def source_repo_with_task(root, kind="implement"):
	source = root / "source"
	git_repo(source)
	(source / "Telegram" / "build").mkdir(parents=True)
	tracked = source / "tracked.txt"
	tracked.write_text("base\n", encoding="utf-8")
	git(source, "add", "tracked.txt")
	git(source, "commit", "-m", "Create baseline")
	slot = root / "slot"
	work = slot / "tasks" / TASK_ID / "work"
	work.mkdir(parents=True)
	(work.parent / "task.md").write_text(
		"# Correct recent-search peer actions\n",
		encoding="utf-8",
	)
	(work.parent / "state.yaml").write_text(
		f"""status: in-progress
type: {kind}
created: 2026-07-19
project: null
depends_on: []
claimed_by: macbook-twork
claimed_at: 2026-07-19T14:28:01+04:00
claim_order: 1
lease_until: null
phase: setup
inbox_receipt: receipts/2026/07/19/test.md
""",
		encoding="utf-8",
	)
	config = {"source_root": str(source)}
	return source, slot, work, config


def run_command(handler, **kwargs):
	out = io.StringIO()
	with contextlib.redirect_stdout(out):
		handler(SimpleNamespace(**kwargs))
	return json.loads(out.getvalue())


def run_test_run(exe, run_dir, **overrides):
	arguments = {
		"exe": str(exe),
		"run_dir": str(run_dir),
		"portable_root": None,
		"deadline": 20.0,
		"quiet": 10.0,
		"grace": 5.0,
		"env": None,
	}
	arguments.update(overrides)
	return run_command(workspace.command_test_run, **arguments)


class MechanicsTest(unittest.TestCase):
	def test_build_lock_recovery_selects_only_exact_owned_processes(self):
		build = Path("C:/Telegram/twin/out")
		exe = build / "Debug/Telegram.exe"
		records = [
			{
				"pid": 10,
				"parent_pid": 1,
				"name": "cmake.exe",
				"executable": "C:/Tools/cmake.exe",
				"command_line": "cmake --build C:/Telegram/twin/out",
			},
			{
				"pid": 11,
				"parent_pid": 10,
				"name": "cl.exe",
				"executable": "C:/Tools/cl.exe",
				"command_line": "cl @compile.rsp",
			},
			{
				"pid": 12,
				"parent_pid": 1,
				"name": "mspdbsrv.exe",
				"executable": "C:/Tools/mspdbsrv.exe",
				"command_line": "mspdbsrv.exe",
			},
			{
				"pid": 13,
				"parent_pid": 1,
				"name": "devenv.exe",
				"executable": "C:/Tools/devenv.exe",
				"command_line": "devenv.exe",
			},
			{
				"pid": 14,
				"parent_pid": 1,
				"name": "Telegram.exe",
				"executable": str(exe),
				"command_line": str(exe),
			},
			{
				"pid": 15,
				"parent_pid": 1,
				"name": "Telegram.exe",
				"executable": "C:/Users/test/Telegram Desktop/Telegram.exe",
				"command_line": "Telegram.exe",
			},
		]
		selected = workspace.recoverable_build_processes(
			records,
			build,
			exe,
			{12, 13},
		)
		self.assertEqual(
			{process["pid"] for process in selected},
			{10, 11, 12, 14},
		)

	def test_build_lock_recovery_deletes_only_named_build_artifacts(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, _, _, _ = source_repo_with_task(root)
			build = source / "out"
			debug = build / "Debug"
			debug.mkdir(parents=True)
			(build / "CMakeCache.txt").write_text(
				f"CMAKE_HOME_DIRECTORY:INTERNAL={source}\n",
				encoding="utf-8",
			)
			exe = debug / "Telegram.exe"
			obj = build / "Telegram.dir" / "locked.obj"
			obj.parent.mkdir(parents=True)
			exe.write_bytes(b"exe")
			obj.write_bytes(b"obj")
			records = [{
				"pid": 22,
				"parent_pid": 1,
				"name": "mspdbsrv.exe",
				"executable": "C:/Tools/mspdbsrv.exe",
				"command_line": "mspdbsrv.exe",
			}]
			with (
				mock.patch.object(
					workspace,
					"kill_processes_with_executable",
					return_value=[21],
				),
				mock.patch.object(
					workspace,
					"locking_process_ids",
					side_effect=[([22], None), ([], None)],
				),
				mock.patch.object(
					workspace,
					"windows_process_records",
					return_value=records,
				),
				mock.patch.object(
					workspace,
					"terminate_process_ids",
					return_value=[{
						**records[0],
						"reason": "direct-build-artifact-holder",
						"stopped": True,
						"error": None,
					}],
				) as terminate,
			):
				result = run_command(
					workspace.command_build_lock_recover,
					source_root=str(source),
					build_root=str(build),
					exe=str(exe),
					artifact=[str(exe), str(obj)],
					wait=0,
				)
			self.assertTrue(result["safe_to_retry"])
			self.assertEqual(
				result["safety_basis"],
				"all-named-artifacts-deleted-or-absent",
			)
			self.assertEqual(result["exact_exe_killed"], [21])
			self.assertEqual(
				{Path(path) for path in result["deleted"]},
				{exe, obj},
			)
			self.assertFalse(exe.exists())
			self.assertFalse(obj.exists())
			self.assertEqual(
				{process["pid"] for process in terminate.call_args.args[0]},
				{22},
			)

	def test_build_lock_recovery_rejects_artifact_outside_build_tree(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, _, _, _ = source_repo_with_task(root)
			build = source / "out"
			debug = build / "Debug"
			debug.mkdir(parents=True)
			(build / "CMakeCache.txt").write_text(
				f"CMAKE_HOME_DIRECTORY:INTERNAL={source}\n",
				encoding="utf-8",
			)
			exe = debug / "Telegram.exe"
			exe.write_bytes(b"exe")
			outside = source / "Telegram" / "outside.obj"
			outside.write_bytes(b"obj")
			with self.assertRaisesRegex(
				workspace.WorkspaceError,
				"outside the build root",
			):
				run_command(
					workspace.command_build_lock_recover,
					source_root=str(source),
					build_root=str(build),
					exe=str(exe),
					artifact=[str(outside)],
					wait=0,
				)

	def test_parse_env_values_requires_name_value_pairs(self):
		self.assertEqual(
			workspace.parse_env_values(["A=1", "B=x=y"]),
			{"A": "1", "B": "x=y"},
		)
		with self.assertRaises(workspace.WorkspaceError):
			workspace.parse_env_values(["NOVALUE"])
		with self.assertRaises(workspace.WorkspaceError):
			workspace.parse_env_values(["=x"])

	def test_setup_test_account_lifecycle(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			live = root / workspace.PORTABLE_LIVE
			real = root / workspace.PORTABLE_REAL
			golden = root / workspace.PORTABLE_GOLDEN

			with self.assertRaisesRegex(workspace.WorkspaceError, "golden"):
				workspace.setup_test_account(root)

			(golden / "tdata").mkdir(parents=True)
			(golden / "tdata" / "key_data").write_text("golden\n", encoding="utf-8")
			self.assertEqual(workspace.setup_test_account(root), "fresh-copy")
			self.assertTrue((live / workspace.PORTABLE_MARKER).is_file())
			self.assertTrue((golden / "tdata" / "key_data").is_file())

			self.assertEqual(
				workspace.setup_test_account(root),
				"reused-marked-live",
			)

			(live / workspace.PORTABLE_MARKER).unlink()
			(live / "tdata" / "user_file").write_text("mine\n", encoding="utf-8")
			self.assertEqual(workspace.setup_test_account(root), "preserved-real")
			self.assertTrue((real / "tdata" / "user_file").is_file())
			self.assertTrue((live / workspace.PORTABLE_MARKER).is_file())
			self.assertFalse((live / "tdata" / "user_file").exists())

			(live / workspace.PORTABLE_MARKER).unlink()
			self.assertEqual(
				workspace.setup_test_account(root),
				"replaced-manual-live",
			)
			self.assertTrue((real / "tdata" / "user_file").is_file())

			self.assertEqual(
				workspace.reset_broken_test_account(root),
				"fresh-copy",
			)
			(live / workspace.PORTABLE_MARKER).unlink()
			with self.assertRaisesRegex(workspace.WorkspaceError, "unmarked"):
				workspace.reset_broken_test_account(root)

	def test_test_run_reports_complete_markers(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			exe = write_complete_markers_exe(debug / "Telegram")
			result = run_test_run(exe, root / "run1", env=["EXTRA_FLAG=1"])
			self.assertEqual(result["outcome"], "exited")
			self.assertEqual(result["verdict_hint"], "complete")
			self.assertTrue(result["test_complete"])
			self.assertEqual(result["exit_code"], 0)
			self.assertEqual(result["account"], "fresh-copy")
			self.assertEqual(result["markers"]["pass"], ["row painted"])
			self.assertEqual(result["markers"]["screenshots"], ["/tmp/fake.png"])
			self.assertFalse(result["crash_report_fresh"])

	def test_test_run_reports_crash_diagnostics(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			live_tdata = debug / workspace.PORTABLE_LIVE / "tdata"
			live_working = live_tdata / "working"
			exe = write_fake_exe(debug / "Telegram", (
				'LOG="$TDESKTOP_TEST_EVIDENCE_DIR/test_log.txt"\n'
				'echo "TEST_STEP: about to crash" >> "$LOG"\n'
				f'mkdir -p "{live_tdata}"\n'
				f'echo "Assertion: boom" > "{live_working}"\n'
				"exit 0\n"
			), (
				'set "LOG=%TDESKTOP_TEST_EVIDENCE_DIR%\\test_log.txt"\n'
				'echo TEST_STEP: about to crash>>"%LOG%"\n'
				f'if not exist "{live_tdata}" mkdir "{live_tdata}"\n'
				f'echo Assertion: boom>"{live_working}"\n'
				"exit /b 0\n"
			))
			result = run_test_run(exe, root / "run1")
			self.assertEqual(result["outcome"], "exited")
			self.assertEqual(result["verdict_hint"], "crash")
			self.assertFalse(result["test_complete"])
			self.assertTrue(result["crash_report_fresh"])
			self.assertIn("Assertion: boom", result["crash_report_excerpt"])

	def test_test_run_still_diagnoses_a_crash_after_clearing_stale_state(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			self.assertEqual(workspace.setup_test_account(debug), "fresh-copy")
			live = debug / workspace.PORTABLE_LIVE
			report, dump = plant_leftover_crash_state(live)
			report_payload = report.read_bytes()
			dump_payload = dump.read_bytes()
			exe = write_fake_exe(debug / "Telegram", (
				f'echo "Assertion: fresh boom" > "{report}"\n'
				"exit 0\n"
			), (
				f'echo Assertion: fresh boom>"{report}"\n'
				"exit /b 0\n"
			))
			run_dir = root / "run1"
			result = run_test_run(exe, run_dir)
			stale = run_dir / workspace.STALE_CRASH_DIR
			self.assertEqual(result["account"], "reused-marked-live")
			self.assertEqual(result["stale_crash_cleared"], [
				{
					"from": str(report),
					"kind": "report",
					"to": str(stale / "working"),
				},
				{
					"from": str(dump),
					"kind": "dump",
					"to": str(stale / "dumps" / "stale.dmp"),
				},
			])
			self.assertEqual((stale / "working").read_bytes(), report_payload)
			self.assertEqual(
				(stale / "dumps" / "stale.dmp").read_bytes(),
				dump_payload,
			)
			self.assertEqual(result["outcome"], "exited")
			self.assertFalse(result["test_complete"])
			self.assertEqual(result["verdict_hint"], "crash")
			self.assertTrue(result["crash_report_fresh"])
			self.assertEqual(result["crash_report"], str(report))
			self.assertIn(
				"Assertion: fresh boom",
				result["crash_report_excerpt"],
			)
			self.assertIn(
				"Assertion: fresh boom",
				report.read_text(encoding="utf-8"),
			)

	def test_test_run_kills_on_deadline(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			exe = write_fake_exe(
				debug / "Telegram", "sleep 30\n", ":loop\ngoto loop\n",
			)
			result = run_test_run(
				exe, root / "run1", deadline=2.0, quiet=30.0,
			)
			self.assertEqual(result["outcome"], "deadline-killed")
			self.assertEqual(result["verdict_hint"], "hang")
			self.assertFalse(result["test_complete"])

	def test_test_run_clears_and_preserves_stale_crash_state(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			golden = debug / workspace.PORTABLE_GOLDEN
			real = debug / workspace.PORTABLE_REAL
			(real / "tdata" / "dumps").mkdir(parents=True)
			(real / "tdata" / "working").write_bytes(b"real crash\n")
			(real / "tdata" / "dumps" / "real.dmp").write_bytes(b"real dump\n")
			self.assertEqual(workspace.setup_test_account(debug), "fresh-copy")
			live = debug / workspace.PORTABLE_LIVE
			report, dump = plant_leftover_crash_state(live)
			report_payload = report.read_bytes()
			dump_payload = dump.read_bytes()
			exe = write_complete_markers_exe(debug / "Telegram")
			run_dir = root / "run1"
			result = run_test_run(exe, run_dir)
			stale = run_dir / workspace.STALE_CRASH_DIR
			self.assertEqual(result["account"], "reused-marked-live")
			self.assertTrue(result["test_complete"])
			self.assertEqual(result["verdict_hint"], "complete")
			self.assertEqual(result["markers"]["pass"], ["row painted"])
			self.assertEqual(result["stale_crash_cleared"], [
				{
					"from": str(report),
					"kind": "report",
					"to": str(stale / "working"),
				},
				{
					"from": str(dump),
					"kind": "dump",
					"to": str(stale / "dumps" / "stale.dmp"),
				},
			])
			self.assertFalse(report.exists())
			self.assertFalse(dump.exists())
			self.assertEqual((stale / "working").read_bytes(), report_payload)
			self.assertEqual(
				(stale / "dumps" / "stale.dmp").read_bytes(),
				dump_payload,
			)
			self.assertIsNone(result["crash_report"])
			self.assertFalse(result["crash_report_fresh"])
			self.assertEqual(result["dumps"], [])
			self.assertEqual(
				(golden / "tdata" / "key_data").read_text(encoding="utf-8"),
				"golden\n",
			)
			self.assertFalse((golden / "tdata" / "working").exists())
			self.assertFalse((golden / workspace.PORTABLE_MARKER).exists())
			self.assertEqual(
				(real / "tdata" / "working").read_bytes(),
				b"real crash\n",
			)
			self.assertEqual(
				(real / "tdata" / "dumps" / "real.dmp").read_bytes(),
				b"real dump\n",
			)

	def test_test_run_without_leftovers_reports_nothing_cleared(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			self.assertEqual(workspace.setup_test_account(debug), "fresh-copy")
			exe = write_complete_markers_exe(debug / "Telegram")
			run_dir = root / "run1"
			result = run_test_run(exe, run_dir)
			self.assertEqual(result["account"], "reused-marked-live")
			self.assertEqual(result["stale_crash_cleared"], [])
			self.assertFalse((run_dir / workspace.STALE_CRASH_DIR).exists())
			self.assertTrue(result["test_complete"])

	def test_test_run_leaves_an_unmarked_live_folder_alone(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			golden = debug / workspace.PORTABLE_GOLDEN
			(golden / "tdata" / "dumps").mkdir(parents=True)
			(golden / "tdata" / "working").write_bytes(b"golden crash\n")
			golden_dump = golden / "tdata" / "dumps" / "golden.dmp"
			golden_dump.write_bytes(b"golden dump\n")
			live = debug / workspace.PORTABLE_LIVE
			(live / "tdata" / "dumps").mkdir(parents=True)
			(live / "tdata" / "working").write_bytes(b"user crash\n")
			(live / "tdata" / "dumps" / "user.dmp").write_bytes(b"user dump\n")
			(live / "tdata" / "user_file").write_text("mine\n", encoding="utf-8")
			exe = write_complete_markers_exe(debug / "Telegram")
			run_dir = root / "run1"
			result = run_test_run(exe, run_dir)
			real = debug / workspace.PORTABLE_REAL
			self.assertEqual(result["account"], "preserved-real")
			self.assertEqual(result["stale_crash_cleared"], [])
			self.assertFalse((run_dir / workspace.STALE_CRASH_DIR).exists())
			self.assertEqual(
				(real / "tdata" / "working").read_bytes(),
				b"user crash\n",
			)
			self.assertEqual(
				(real / "tdata" / "dumps" / "user.dmp").read_bytes(),
				b"user dump\n",
			)
			self.assertTrue((real / "tdata" / "user_file").is_file())
			self.assertEqual(
				(golden / "tdata" / "working").read_bytes(),
				b"golden crash\n",
			)
			self.assertEqual(golden_dump.read_bytes(), b"golden dump\n")
			self.assertTrue((live / workspace.PORTABLE_MARKER).is_file())
			self.assertEqual(
				(live / "tdata" / "working").read_bytes(),
				b"golden crash\n",
			)
			self.assertEqual(
				(live / "tdata" / "dumps" / "golden.dmp").read_bytes(),
				b"golden dump\n",
			)

	def test_test_run_refuses_to_launch_when_the_stale_report_cannot_move(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			self.assertEqual(workspace.setup_test_account(debug), "fresh-copy")
			live = debug / workspace.PORTABLE_LIVE
			report, dump = plant_leftover_crash_state(live)
			report_payload = report.read_bytes()
			exe = write_complete_markers_exe(debug / "Telegram")
			run_dir = root / "run1"
			with mock.patch.object(
				workspace.shutil, "move", side_effect=OSError("locked"),
			):
				with self.assertRaisesRegex(workspace.WorkspaceError, "working"):
					run_test_run(exe, run_dir)
			self.assertEqual(report.read_bytes(), report_payload)
			self.assertTrue(dump.is_file())
			self.assertFalse((run_dir / "app_stdout.txt").exists())

	def test_test_run_reports_a_dump_that_could_not_be_moved(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			self.assertEqual(workspace.setup_test_account(debug), "fresh-copy")
			live = debug / workspace.PORTABLE_LIVE
			report, dump = plant_leftover_crash_state(live)
			dump_payload = dump.read_bytes()
			exe = write_complete_markers_exe(debug / "Telegram")
			run_dir = root / "run1"
			real_move = workspace.shutil.move

			def move_unless_dump(source, target):
				if str(source).endswith(".dmp"):
					Path(target).write_bytes(Path(source).read_bytes())
					raise OSError("locked")
				return real_move(source, target)

			with mock.patch.object(
				workspace.shutil, "move", side_effect=move_unless_dump,
			):
				result = run_test_run(exe, run_dir)
			stale = run_dir / workspace.STALE_CRASH_DIR
			self.assertTrue(result["test_complete"])
			self.assertEqual(result["verdict_hint"], "complete")
			self.assertEqual(result["stale_crash_cleared"], [
				{
					"from": str(report),
					"kind": "report",
					"to": str(stale / "working"),
				},
				{
					"from": str(dump),
					"kind": "dump",
					"to": None,
				},
			])
			self.assertEqual(dump.read_bytes(), dump_payload)
			self.assertEqual(result["dumps"], [])
			self.assertEqual(list((stale / "dumps").iterdir()), [])
			self.assertEqual(
				sorted(path.name for path in stale.iterdir()),
				["dumps", "working"],
			)

	def test_test_run_discards_a_partial_report_copy_from_a_failed_move(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			self.assertEqual(workspace.setup_test_account(debug), "fresh-copy")
			live = debug / workspace.PORTABLE_LIVE
			report, dump = plant_leftover_crash_state(live)
			report_payload = report.read_bytes()
			exe = write_complete_markers_exe(debug / "Telegram")
			run_dir = root / "run1"

			def write_then_fail(source, target):
				Path(target).write_bytes(Path(source).read_bytes())
				raise OSError("locked")

			with mock.patch.object(
				workspace.shutil, "move", side_effect=write_then_fail,
			):
				with self.assertRaisesRegex(workspace.WorkspaceError, "working"):
					run_test_run(exe, run_dir)
			stale = run_dir / workspace.STALE_CRASH_DIR
			self.assertEqual(report.read_bytes(), report_payload)
			self.assertTrue(dump.is_file())
			self.assertEqual(list(stale.iterdir()), [])
			self.assertFalse((run_dir / "app_stdout.txt").exists())

	def test_portable_root_for_prefers_app_bundle_parent(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			binary = (
				root / "out" / "Debug" / "Telegram.app"
				/ "Contents" / "MacOS" / "Telegram"
			)
			binary.parent.mkdir(parents=True)
			binary.write_text("", encoding="utf-8")
			self.assertEqual(
				workspace.portable_root_for(binary, None),
				root / "out" / "Debug",
			)
			plain = root / "out" / "Debug" / "Telegram.exe"
			plain.write_text("", encoding="utf-8")
			self.assertEqual(
				workspace.portable_root_for(plain, None),
				root / "out" / "Debug",
			)

	def test_unique_destination_avoids_an_existing_name(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			self.assertEqual(
				workspace.unique_destination(root, "working"),
				root / "working",
			)
			(root / "working").write_text("stale\n", encoding="utf-8")
			self.assertEqual(
				workspace.unique_destination(root, "working"),
				root / "working-02",
			)
			(root / "stale.dmp").write_text("dump\n", encoding="utf-8")
			self.assertEqual(
				workspace.unique_destination(root, "stale.dmp"),
				root / "stale-02.dmp",
			)

	def test_overlay_save_and_apply_roundtrip(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			git(
				source, "update-ref",
				workspace.source_task_ref(TASK_ID, "run"), "HEAD",
			)
			tracked = source / "tracked.txt"
			tracked.write_text("base\noverlay\n", encoding="utf-8")
			(work / workspace.OVERLAY_PATHS_FILE).write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				saved = run_command(
					workspace.command_overlay_save,
					task=TASK_ID,
					restore="run",
				)
			self.assertGreater(saved["patch_bytes"], 0)
			self.assertEqual(saved["restored"], ["tracked.txt"])
			self.assertEqual(
				tracked.read_text(encoding="utf-8"),
				"base\n",
			)
			self.assertEqual(workspace.changed_paths(source), [])

			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				applied = run_command(
					workspace.command_overlay_apply,
					task=TASK_ID,
				)
			self.assertTrue(applied["applied"])
			self.assertEqual(applied["conflicts"], [])
			self.assertEqual(
				tracked.read_text(encoding="utf-8"),
				"base\noverlay\n",
			)

	def test_overlay_save_rejects_uninventoried_and_untracked_paths(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			(work / workspace.OVERLAY_PATHS_FILE).write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			(source / "tracked.txt").write_text("base\noverlay\n", encoding="utf-8")
			(source / "stray.txt").write_text("stray\n", encoding="utf-8")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				with self.assertRaisesRegex(workspace.WorkspaceError, "untracked"):
					run_command(
						workspace.command_overlay_save,
						task=TASK_ID,
						restore="run",
					)
			(source / "stray.txt").unlink()
			other = source / "other.txt"
			other.write_text("tracked other\n", encoding="utf-8")
			git(source, "add", "other.txt")
			git(source, "commit", "-m", "Add other")
			other.write_text("dirty\n", encoding="utf-8")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				with self.assertRaisesRegex(workspace.WorkspaceError, "outside"):
					run_command(
						workspace.command_overlay_save,
						task=TASK_ID,
						restore="run",
					)

	def test_source_commit_stages_owned_paths_and_marks_green(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			git(
				source, "update-ref",
				workspace.source_task_ref(TASK_ID, "base"), "HEAD",
			)
			(work / "owned-paths.txt").write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			(source / "tracked.txt").write_text("task\n", encoding="utf-8")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				result = run_command(
					workspace.command_source_commit,
					task=TASK_ID,
					subject="Correct peer actions",
					mark_green=True,
				)
			self.assertEqual(result["committed"], ["tracked.txt"])
			self.assertTrue(result["marked_green"])
			message = git(source, "show", "-s", "--format=%B", "HEAD")
			self.assertEqual(
				message,
				f"Correct peer actions\n\nTask: {TASK_ID}",
			)
			head = git(source, "rev-parse", "HEAD")
			self.assertEqual(
				workspace.resolved_ref(
					source, workspace.source_task_ref(TASK_ID, "green"),
				),
				head,
			)
			self.assertEqual(
				workspace.resolved_ref(
					source, workspace.source_task_ref(TASK_ID, "run"),
				),
				head,
			)
			verified = run_command(
				workspace.command_source_verify_commit,
				source_root=str(source),
				task=TASK_ID,
				ref="HEAD",
			)
			self.assertTrue(verified["valid"])
			self.assertEqual(verified["subject"], "Correct peer actions")

	def test_source_commit_rejects_paths_outside_owned_set(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			(work / "owned-paths.txt").write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			(source / "tracked.txt").write_text("task\n", encoding="utf-8")
			(source / "extra.txt").write_text("extra\n", encoding="utf-8")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				with self.assertRaisesRegex(workspace.WorkspaceError, "extra.txt"):
					run_command(
						workspace.command_source_commit,
						task=TASK_ID,
						subject="Correct peer actions",
						mark_green=False,
					)
				with self.assertRaisesRegex(workspace.WorkspaceError, "single"):
					run_command(
						workspace.command_source_commit,
						task=TASK_ID,
						subject="Bad\nsubject",
						mark_green=False,
					)
				with self.assertRaisesRegex(workspace.WorkspaceError, "too long"):
					run_command(
						workspace.command_source_commit,
						task=TASK_ID,
						subject="x" * 80,
						mark_green=False,
					)

	def test_verification_task_cannot_commit_telegram_source(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root, kind="verify")
			(work / "owned-paths.txt").write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			(source / "tracked.txt").write_text("task\n", encoding="utf-8")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				with self.assertRaisesRegex(workspace.WorkspaceError, "follow-up"):
					run_command(
						workspace.command_source_commit,
						task=TASK_ID,
						subject="Correct peer actions",
						mark_green=False,
					)
			self.assertEqual(
				git(source, "show", "-s", "--format=%s", "HEAD"),
				"Create baseline",
			)

	def test_verification_source_state_requires_an_untouched_baseline(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, _, _, config = source_repo_with_task(root, kind="verify")
			for name in ("base", "run"):
				git(source, "update-ref", workspace.source_task_ref(TASK_ID, name), "HEAD")
			workspace.validate_source_state(config, TASK_ID, True, "verify")

			(source / "tracked.txt").write_text("task\n", encoding="utf-8")
			git(source, "commit", "-am", "Correct peer actions", "-m", f"Task: {TASK_ID}")
			git(source, "update-ref", workspace.source_task_ref(TASK_ID, "run"), "HEAD")
			with self.assertRaisesRegex(workspace.WorkspaceError, "local baseline"):
				workspace.validate_source_state(config, TASK_ID, True, "verify")

			git(source, "update-ref", workspace.source_task_ref(TASK_ID, "green"), "HEAD")
			with self.assertRaisesRegex(workspace.WorkspaceError, "implementation commit"):
				workspace.validate_source_state(config, TASK_ID, True, "verify")
			workspace.validate_source_state(config, TASK_ID, True)

	def test_verification_result_contract(self):
		path = Path("work/result.md")

		def check(lines, approved=True):
			workspace.validate_verify_result(lines, path, approved)

		check(["Touched: none", "Finding: confirmed"])
		check(["Touched: none", "Finding: deviation", "Discovered: present"])
		check(["Touched: none", "Finding: inconclusive"], approved=False)

		with self.assertRaisesRegex(workspace.WorkspaceError, "Touched: none"):
			check(["Touched: Telegram/SourceFiles/main.cpp", "Finding: confirmed"])
		with self.assertRaisesRegex(workspace.WorkspaceError, "exactly one Finding"):
			check(["Touched: none"])
		with self.assertRaisesRegex(workspace.WorkspaceError, "exactly one Finding"):
			check(["Touched: none", "Finding: probably-fine"])
		with self.assertRaisesRegex(workspace.WorkspaceError, "discovered follow-up"):
			check(["Touched: none", "Finding: deviation", "Discovered: none"])
		with self.assertRaisesRegex(workspace.WorkspaceError, "never approved"):
			check(["Touched: none", "Finding: inconclusive"])
		with self.assertRaisesRegex(workspace.WorkspaceError, "could not measure"):
			check(["Touched: none", "Finding: deviation"], approved=False)

	def test_task_type_defaults_to_implementation(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			directory = write_task(slot, status="todo", claimed_by=None)
			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["type"], "implement")

			workspace.update_state(directory / "state.yaml", {"type": "verify"})
			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["type"], "verify")
			text = (directory / "state.yaml").read_text(encoding="utf-8")
			self.assertEqual(
				text.splitlines()[:2],
				["status: todo", "type: verify"],
			)

			workspace.update_state(directory / "state.yaml", {"type": "guess"})
			with self.assertRaisesRegex(workspace.WorkspaceError, "Invalid task type"):
				workspace.load_state(slot, directory / "state.yaml")

	def test_fence_create_and_check_detects_changes(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			(root / "a.png").write_bytes(b"aaa")
			(root / "sub").mkdir()
			(root / "sub" / "b.png").write_bytes(b"bbb")
			baseline = root / "fence.txt"
			created = run_command(
				workspace.command_fence_create,
				file=str(baseline),
				root=str(root),
				paths=["a.png", "sub/b.png"],
			)
			self.assertEqual(created["paths"], 2)
			checked = run_command(
				workspace.command_fence_check,
				file=str(baseline),
				root=str(root),
			)
			self.assertTrue(checked["ok"])
			(root / "a.png").write_bytes(b"changed")
			(root / "sub" / "b.png").unlink()
			out = io.StringIO()
			with contextlib.redirect_stdout(out):
				with self.assertRaises(SystemExit):
					workspace.command_fence_check(SimpleNamespace(
						file=str(baseline),
						root=str(root),
					))
			result = json.loads(out.getvalue())
			self.assertFalse(result["ok"])
			self.assertEqual(result["mismatched"], ["a.png"])
			self.assertEqual(result["missing"], ["sub/b.png"])

	def test_source_preflight_reports_dirty_and_account_state(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			(work / "owned-paths.txt").write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			(source / "tracked.txt").write_text("dirty\n", encoding="utf-8")
			(source / "unrelated.txt").write_text("stray\n", encoding="utf-8")
			debug = make_portable_root(root)
			exe = write_fake_exe(debug / "Telegram", "exit 0\n", "exit /b 0\n")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				result = run_command(
					workspace.command_source_preflight,
					task=TASK_ID,
					exe=str(exe),
				)
			self.assertFalse(result["source_clean"])
			self.assertIn("tracked.txt", result["dirty"])
			self.assertEqual(result["dirty_outside_owned"], ["unrelated.txt"])
			self.assertTrue(result["owned_paths_present"])
			self.assertTrue(result["exe_present"])
			self.assertTrue(result["golden_account_present"])
			self.assertFalse(result["live_marker_present"])


if __name__ == "__main__":
	unittest.main()
