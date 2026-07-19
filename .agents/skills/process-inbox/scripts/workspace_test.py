#!/usr/bin/env python3

import contextlib
import io
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import workspace


TASK_ID = "2026/07/19/correct-recent-search-peer-actions"


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


def write_task(slot, status="blocked"):
	directory = slot / "tasks" / TASK_ID
	(directory / "work").mkdir(parents=True)
	(directory / "task.md").write_text(
		"# Correct recent-search peer actions\n",
		encoding="utf-8",
	)
	(directory / "state.yaml").write_text(
		f"""status: {status}
created: 2026-07-19
project: null
depends_on: []
claimed_by: macbook-twork
claimed_at: 2026-07-19T14:28:01+04:00
claim_order: 1
lease_until: null
phase: {status}
inbox_receipt: receipts/2026/07/19/test.md
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


class WorkspaceTest(unittest.TestCase):
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
				mock.patch.object(workspace, "commit_paths", return_value=True) as commit,
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_retry(SimpleNamespace(task=TASK_ID))

			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["status"], "in-progress")
			self.assertEqual(state["phase"], "resume")
			self.assertFalse(routed.exists())
			paths = commit.call_args.args[1]
			self.assertIn(f"tasks/{TASK_ID}/state.yaml", paths)
			self.assertIn(f"tasks/{TASK_ID}/work/discovered-routed.md", paths)

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


if __name__ == "__main__":
	unittest.main()
