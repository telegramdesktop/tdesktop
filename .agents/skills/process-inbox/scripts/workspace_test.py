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


def git_bytes(repo, *args):
	return subprocess.run(
		["git", "-C", str(repo), *args],
		check=True,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
	).stdout


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

	def test_inbox_publish_stages_a_removed_non_ascii_publication_path(self):
		name = "\u00e9\u6587.md"
		self.assertEqual(name.encode("utf-8"), b"\xc3\xa9\xe6\x96\x87.md")
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			main = Path(config["ai_main"])
			project = main / "projects" / "sample"
			project.mkdir(parents=True)
			(project / name).write_text("# Sample note\n", encoding="utf-8")
			git(main, "add", "projects/sample")
			git(main, "commit", "-m", "Add sample project")

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

			worktree = Path(config["inbox_worktree"])
			(worktree / "projects" / "sample" / name).unlink()
			(worktree / "projects" / "sample").rmdir()
			receipt = "receipts/2026/07/19/removed-note.md"
			receipt_path = worktree / receipt
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
					paths=["projects/sample", receipt],
				))

			self.assertFalse((main / "projects" / "sample").exists())
			self.assertEqual(
				git_bytes(
					main,
					"-c", "core.quotePath=false",
					"show", "--name-status", "--format=", "HEAD",
				).decode("utf-8").strip().splitlines(),
				["D\tprojects/sample/" + name, "A\t" + receipt],
			)

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

		with tempfile.TemporaryDirectory() as temporary:
			resolved = workspace.resolve_task(
				Path(temporary),
				states,
				"correct-recent-search-peer-actions",
			)

		self.assertEqual(resolved["id"], TASK_ID)

	def test_resolve_follows_durable_superseded_task_alias(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			git_repo(root)
			write_task(root, status="todo", claimed_by=None)
			old_id = "2026/07/18/old-recent-search-task"
			old = root / "tasks" / old_id
			old.mkdir(parents=True)
			(old / "task.md").write_text("# Old task\n", encoding="utf-8")
			git(root, "add", "-A")
			git(root, "commit", "-m", "Retain the old task")
			content_digest = workspace.retained_task_digest(old)
			(old / "superseded.yaml").write_text(
				f"""superseded_by: {TASK_ID}
receipt: receipts/2026/07/20/consolidation.md
type: implement
project: null
content_sha256: {content_digest}
""",
				encoding="utf-8",
			)
			states = workspace.load_states(root)

			resolved = workspace.resolve_task(root, states, old_id)

			self.assertEqual(resolved["id"], TASK_ID)
			self.assertEqual(resolved["superseded_from"], old_id)

	def test_dependency_validation_rejects_missing_and_cyclic_edges(self):
		first = {**task_state("todo", None), "depends_on": ["missing"]}
		with self.assertRaisesRegex(workspace.WorkspaceError, "missing dependencies"):
			workspace.validate_dependency_graph({TASK_ID: first})

		other_id = "2026/07/20/other-task"
		first = {**task_state("todo", None), "depends_on": [other_id]}
		other = {
			**task_state("todo", None),
			"id": other_id,
			"depends_on": [TASK_ID],
		}
		with self.assertRaisesRegex(workspace.WorkspaceError, "dependency cycle"):
			workspace.validate_dependency_graph({TASK_ID: first, other_id: other})

	def test_queue_inventory_finds_pending_consolidation_markers(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			directory = write_task(root, status="approved")
			marker = directory / workspace.CONSOLIDATION_PENDING
			marker.write_text("Created: one\n", encoding="utf-8")

			self.assertEqual(workspace.pending_consolidations(root), [{
				"source_task": TASK_ID,
				"marker": f"tasks/{TASK_ID}/{workspace.CONSOLIDATION_PENDING}",
			}])

	def test_generic_publish_recognizes_consolidation_validation(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			git_repo(root)
			directory = write_task(root, status="approved")
			pending = directory / workspace.CONSOLIDATION_PENDING
			pending.write_text("pending\n", encoding="utf-8")
			git(root, "add", ".")
			git(root, "commit", "-m", "Seed completed task")
			pending.unlink()
			complete = directory / workspace.CONSOLIDATION_COMPLETE
			complete.write_text(
				f"# Consolidation\n\nSource: {TASK_ID}\nSTATUS: NO_MERGE\n",
				encoding="utf-8",
			)
			git(root, "add", "-A")
			git(
				root,
				"commit",
				"-m",
				f"Consolidate pending tasks after {TASK_ID}",
			)

			validate = workspace.consolidation_validation_for_head(root)

			self.assertIsNotNone(validate)
			validate(root)

	def test_consolidation_validation_rejects_a_non_ascii_superseded_path(self):
		slug = "\u00e9\u6587"
		self.assertEqual(slug.encode("utf-8"), b"\xc3\xa9\xe6\x96\x87")
		planted = f"tasks/2026/07/19/{slug}/work/superseded.yaml"
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			git_repo(root)
			directory = write_task(root, status="approved")
			pending = directory / workspace.CONSOLIDATION_PENDING
			pending.write_text("pending\n", encoding="utf-8")
			git(root, "add", ".")
			git(root, "commit", "-m", "Seed completed task")
			pending.unlink()
			complete = directory / workspace.CONSOLIDATION_COMPLETE
			complete.write_text(
				f"# Consolidation\n\nSource: {TASK_ID}\nSTATUS: NO_MERGE\n",
				encoding="utf-8",
			)
			alias = root / planted
			alias.parent.mkdir(parents=True)
			alias.write_text(
				"superseded_by: 2026/07/20/successor-task\n"
				"receipt: receipts/2026/07/19/test.md\n"
				"type: implement\n"
				"project: null\n"
				"content_sha256: " + "0" * 64 + "\n",
				encoding="utf-8",
			)
			git(root, "add", "-A")
			git(
				root,
				"commit",
				"-m",
				f"Consolidate pending tasks after {TASK_ID}",
			)
			self.assertEqual(
				git_bytes(
					root,
					"-c", "core.quotePath=false",
					"diff-tree", "--no-commit-id", "--name-only", "-r",
					"HEAD^", "HEAD",
				).decode("utf-8").splitlines().count(planted),
				1,
			)
			self.assertEqual(workspace.superseded_paths(root), [])

			with self.assertRaises(workspace.WorkspaceError) as caught:
				workspace.consolidation_validation_for_head(root)

			self.assertEqual(
				str(caught.exception),
				"Invalid superseded path in consolidation commit: " + planted,
			)

	def test_no_merge_consolidation_publishes_durable_completion(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			main = Path(config["ai_main"])
			slot = Path(config["slot_worktree"])
			source_task = "2026/07/18/active-task"
			directory = main / "tasks" / source_task
			state = directory / "state.yaml"
			state.write_text(
				state.read_text(encoding="utf-8")
				.replace("status: todo", "status: approved")
				.replace("phase: null", "phase: complete"),
				encoding="utf-8",
			)
			pending = directory / workspace.CONSOLIDATION_PENDING
			pending.parent.mkdir()
			pending.write_text("# Pending task consolidation\n", encoding="utf-8")
			git(main, "add", f"tasks/{source_task}")
			git(main, "commit", "-m", "Route follow-ups")
			git(slot, "merge", "--ff-only", "master")
			slot_directory = slot / "tasks" / source_task
			(slot_directory / workspace.CONSOLIDATION_PENDING).unlink()
			(slot_directory / workspace.CONSOLIDATION_COMPLETE).write_text(
				f"# Consolidation\n\nSource: {source_task}\nSTATUS: NO_MERGE\n",
				encoding="utf-8",
			)
			out = io.StringIO()
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				contextlib.redirect_stdout(out),
			):
				workspace.command_consolidate_publish(SimpleNamespace(
					source_task=source_task,
					mappings=[],
					receipt=None,
					paths=[f"tasks/{source_task}"],
				))

			result = json.loads(out.getvalue())
			self.assertTrue(result["committed"])
			self.assertTrue(result["published"])
			self.assertFalse(
				(main / "tasks" / source_task / workspace.CONSOLIDATION_PENDING).exists()
			)
			self.assertTrue(
				(main / "tasks" / source_task / workspace.CONSOLIDATION_COMPLETE).is_file()
			)

	def test_merged_consolidation_publishes_a_removed_directory(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			main = Path(config["ai_main"])
			slot = Path(config["slot_worktree"])
			source_task = "2026/07/18/active-task"
			old_ids = ["2026/07/18/first-task", "2026/07/18/second-task"]
			new_id = "2026/07/20/combined-task"
			receipt = "receipts/2026/07/20/consolidation.md"
			directory = main / "tasks" / source_task
			state = directory / "state.yaml"
			state.write_text(
				state.read_text(encoding="utf-8")
				.replace("status: todo", "status: approved")
				.replace("phase: null", "phase: complete"),
				encoding="utf-8",
			)
			pending = directory / workspace.CONSOLIDATION_PENDING
			pending.parent.mkdir()
			pending.write_text("# Pending task consolidation\n", encoding="utf-8")
			for old_id in old_ids:
				old = main / "tasks" / old_id
				old.mkdir(parents=True)
				(old / "task.md").write_text(f"# {old_id}\n", encoding="utf-8")
				(old / "state.yaml").write_text(
					"""status: todo
type: implement
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
			retired = main / "projects" / "legacy"
			retired.mkdir(parents=True)
			(retired / "project.md").write_text("# Legacy project\n", encoding="utf-8")
			(retired / "tasks.md").write_text("# Tasks\n", encoding="utf-8")
			git(main, "add", "-A")
			git(main, "commit", "-m", "Route follow-ups")
			git(slot, "merge", "--ff-only", "master")
			slot_directory = slot / "tasks" / source_task
			(slot_directory / workspace.CONSOLIDATION_PENDING).unlink()
			(slot_directory / workspace.CONSOLIDATION_COMPLETE).write_text(
				f"# Consolidation\n\nSource: {source_task}\nSTATUS: MERGED\n",
				encoding="utf-8",
			)
			for old_id in old_ids:
				old = slot / "tasks" / old_id
				digest = workspace.retained_task_digest(old)
				(old / "state.yaml").unlink()
				(old / "superseded.yaml").write_text(
					f"""superseded_by: {new_id}
receipt: {receipt}
type: implement
project: null
content_sha256: {digest}
""",
					encoding="utf-8",
				)
			new = slot / "tasks" / new_id
			new.mkdir(parents=True)
			(new / "task.md").write_text("# Combined task\n", encoding="utf-8")
			(new / "state.yaml").write_text(
				f"""status: todo
type: implement
created: 2026-07-20
project: null
depends_on: []
claimed_by: null
claimed_at: null
claim_order: null
lease_until: null
phase: null
inbox_receipt: {receipt}
""",
				encoding="utf-8",
			)
			receipt_path = slot / receipt
			receipt_path.parent.mkdir(parents=True)
			receipt_path.write_text(
				"\n".join(old_ids + [new_id]) + "\n",
				encoding="utf-8",
			)
			git(slot, "rm", "-r", "-q", "--", "projects/legacy")
			self.assertEqual(git(slot, "ls-files", "--", "projects/legacy"), "")

			out = io.StringIO()
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				contextlib.redirect_stdout(out),
			):
				workspace.command_consolidate_publish(SimpleNamespace(
					source_task=source_task,
					mappings=[f"{old_id}={new_id}" for old_id in old_ids],
					receipt=receipt,
					paths=[
						f"tasks/{source_task}",
						f"tasks/{old_ids[0]}",
						f"tasks/{old_ids[1]}",
						f"tasks/{new_id}",
						receipt,
						"projects/legacy",
					],
				))

			result = json.loads(out.getvalue())
			self.assertTrue(result["committed"])
			self.assertTrue(result["published"])
			self.assertIn(
				"D\tprojects/legacy/project.md",
				git(slot, "show", "--name-status", "--no-renames", "--format=", "HEAD"),
			)
			self.assertFalse((main / "projects" / "legacy").exists())
			self.assertTrue((main / "tasks" / new_id / "state.yaml").is_file())

	def test_merged_consolidation_validates_aliases_and_dependencies(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			git_repo(root)
			source = write_task(root, status="approved")
			(source / workspace.CONSOLIDATION_COMPLETE).write_text(
				f"# Consolidation\n\nSource: {TASK_ID}\nSTATUS: MERGED\n",
				encoding="utf-8",
			)
			old_ids = ["2026/07/18/first-task", "2026/07/18/second-task"]
			new_id = "2026/07/20/combined-task"
			receipt = "receipts/2026/07/20/consolidation.md"
			for old_id in old_ids:
				directory = root / "tasks" / old_id
				directory.mkdir(parents=True)
				(directory / "task.md").write_text(
					f"# {old_id}\n", encoding="utf-8",
				)
				git(root, "add", "-A")
				git(root, "commit", "-m", f"Retain {old_id}")
				content_digest = workspace.retained_task_digest(directory)
				(directory / "superseded.yaml").write_text(
					f"""superseded_by: {new_id}
receipt: {receipt}
type: implement
project: null
content_sha256: {content_digest}
""",
					encoding="utf-8",
				)
			new = root / "tasks" / new_id
			new.mkdir(parents=True)
			(new / "task.md").write_text("# Combined task\n", encoding="utf-8")
			(new / "state.yaml").write_text(
				f"""status: todo
type: implement
created: 2026-07-20
project: null
depends_on: []
claimed_by: null
claimed_at: null
claim_order: null
lease_until: null
phase: null
inbox_receipt: {receipt}
""",
				encoding="utf-8",
			)
			receipt_path = root / receipt
			receipt_path.parent.mkdir(parents=True)
			receipt_path.write_text(
				"\n".join(old_ids + [new_id]) + "\n",
				encoding="utf-8",
			)
			mappings = {old_id: new_id for old_id in old_ids}

			workspace.validate_consolidation_tree(
				root,
				TASK_ID,
				mappings,
				receipt,
			)
			(root / "tasks" / old_ids[0] / "task.md").write_text(
				"# Late changed acceptance\n", encoding="utf-8",
			)
			git(root, "add", "-A")
			git(root, "commit", "-m", "Change retained content")
			with self.assertRaisesRegex(
				workspace.WorkspaceError,
				"retained content changed",
			):
				workspace.validate_consolidation_tree(
					root,
					TASK_ID,
					mappings,
					receipt,
				)
			(root / "tasks" / old_ids[0] / "task.md").write_text(
				f"# {old_ids[0]}\n", encoding="utf-8",
			)
			git(root, "add", "-A")
			git(root, "commit", "-m", "Restore retained content")

			dependent = root / "tasks" / "2026/07/20/racing-dependent"
			dependent.mkdir(parents=True)
			(dependent / "task.md").write_text(
				"# Racing dependent\n", encoding="utf-8",
			)
			(dependent / "state.yaml").write_text(
				f"""status: todo
type: implement
created: 2026-07-20
project: null
depends_on: [{old_ids[0]}]
claimed_by: null
claimed_at: null
claim_order: null
lease_until: null
phase: null
inbox_receipt: receipts/2026/07/20/race.md
""",
				encoding="utf-8",
			)
			with self.assertRaisesRegex(workspace.WorkspaceError, "missing dependencies"):
				workspace.validate_consolidation_tree(
					root,
					TASK_ID,
					mappings,
					receipt,
				)

	def test_retry_reopens_owned_blocked_task_and_resets_routing_marker(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			directory = write_task(slot)
			routed = directory / "work" / "discovered-routed.md"
			routed.write_text("routed\n", encoding="utf-8")
			pending = directory / workspace.CONSOLIDATION_PENDING
			pending.write_text("pending\n", encoding="utf-8")
			complete = directory / workspace.CONSOLIDATION_COMPLETE
			complete.write_text("complete\n", encoding="utf-8")
			config = {
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
			}
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
				mock.patch.object(
					workspace,
					"source_lineage_report",
					return_value={"current_satisfies": True},
				),
				mock.patch.object(workspace, "commit_paths") as commit,
				contextlib.redirect_stdout(io.StringIO()),
			):
				workspace.command_retry(SimpleNamespace(task=TASK_ID))

			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["status"], "in-progress")
			self.assertEqual(state["phase"], "resume")
			self.assertFalse(routed.exists())
			self.assertFalse(pending.exists())
			self.assertFalse(complete.exists())
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
				mock.patch.object(
					workspace,
					"source_lineage_report",
					return_value={"current_satisfies": True},
				),
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

	def test_start_refuses_source_dependency_absent_from_branch(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			directory = write_task(slot, status="todo", claimed_by=None)
			config = {
				"checkout_tag": "macbook-twork",
				"slot_worktree": str(slot),
			}
			report = {
				"current_satisfies": False,
				"missing_source_tasks": ["2026/07/18/source-task"],
				"compatible_local_branches": ["layer229"],
			}
			with (
				mock.patch.object(workspace, "worktree_config", return_value=config),
				mock.patch.object(workspace, "sync_canonical"),
				mock.patch.object(
					workspace,
					"source_lineage_report",
					return_value=report,
				) as lineage,
				mock.patch.object(workspace, "commit_paths") as commit,
			):
				with self.assertRaisesRegex(
					workspace.WorkspaceError,
					"compatible local branches: layer229",
				):
					workspace.command_start(SimpleNamespace(
						task=TASK_ID,
						require=["2026/07/18/source-task"],
					))

			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["status"], "todo")
			self.assertIsNone(state["claimed_by"])
			lineage.assert_called_once_with(
				config,
				slot,
				TASK_ID,
				["2026/07/18/source-task"],
			)
			commit.assert_not_called()

	def test_source_lineage_finds_compatible_local_branch(self):
		dependency_id = "2026/07/18/source-task"
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source = root / "source"
			slot = root / "slot"
			git_repo(source)
			tracked = source / "tracked.txt"
			tracked.write_text("base\n", encoding="utf-8")
			git(source, "add", "tracked.txt")
			git(source, "commit", "-m", "Create baseline")
			git(source, "branch", "without-dependency")
			tracked.write_text("source task\n", encoding="utf-8")
			git(
				source,
				"commit",
				"-am",
				"Add source behavior",
				"-m",
				f"Task: {dependency_id}",
			)
			git(source, "branch", "with-dependency")
			git(source, "switch", "without-dependency")

			dependency = slot / "tasks" / dependency_id
			dependency.mkdir(parents=True)
			(dependency / "task.md").write_text(
				"# Add source behavior\n",
				encoding="utf-8",
			)
			(dependency / "state.yaml").write_text(
				"""status: approved
type: implement
created: 2026-07-18
project: null
depends_on: []
claimed_by: macbook-twork
claimed_at: 2026-07-18T10:00:00+04:00
claim_order: 1
lease_until: null
phase: complete
inbox_receipt: receipts/2026/07/18/test.md
""",
				encoding="utf-8",
			)
			target = write_task(slot, status="todo", claimed_by=None)
			state_path = target / "state.yaml"
			state_path.write_text(
				state_path.read_text(encoding="utf-8").replace(
					"depends_on: []",
					f"depends_on: [{dependency_id}]",
				),
				encoding="utf-8",
			)

			config = {"source_root": str(source)}
			report = workspace.source_lineage_report(
				config,
				slot,
				TASK_ID,
			)
			self.assertFalse(report["current_satisfies"])
			self.assertEqual(report["missing_source_tasks"], [dependency_id])
			self.assertEqual(report["unavailable_source_tasks"], [])
			self.assertIn("with-dependency", report["compatible_local_branches"])

			result_path = dependency / "work" / "result.md"
			result_path.parent.mkdir()
			result_path.write_text(
				"Outcome: already-satisfied\n",
				encoding="utf-8",
			)
			report = workspace.source_lineage_report(
				config,
				slot,
				TASK_ID,
			)
			self.assertTrue(report["current_satisfies"])
			self.assertEqual(report["required_source_tasks"], [])
			result_path.unlink()

			git(source, "switch", "with-dependency")
			report = workspace.source_lineage_report(
				config,
				slot,
				TASK_ID,
			)
			self.assertTrue(report["current_satisfies"])
			self.assertEqual(report["missing_source_tasks"], [])

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
				mock.patch.object(
					workspace,
					"source_lineage_report",
					return_value={"current_satisfies": True},
				),
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
Outcome: changed
Touched: tracked.txt
Verdict: APPROVED
Test-Report: work/test.md
Checkout: clean-buildable
""",
				encoding="utf-8",
			)
			(directory / "work" / "test.md").write_text(
				"# Adaptive evidence\n",
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
					SimpleNamespace(
						task=TASK_ID,
						status="approved",
						model="gpt-5.6-sol",
					)
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
				mock.patch.object(
					workspace,
					"source_lineage_report",
					return_value={"current_satisfies": True},
				),
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


def write_dump_after_complete_exe(path, dump, tail, windows_tail):
	directory = dump.parent
	return write_fake_exe(path, (
		'LOG="$TDESKTOP_TEST_EVIDENCE_DIR/test_log.txt"\n'
		'echo "TEST_COMPLETE" >> "$LOG"\n'
		f'mkdir -p "{directory}"\n'
		f'echo "MDMP fresh minidump" > "{dump}"\n'
		+ tail
	), (
		'set "LOG=%TDESKTOP_TEST_EVIDENCE_DIR%\\test_log.txt"\n'
		'echo TEST_COMPLETE>>"%LOG%"\n'
		f'if not exist "{directory}" mkdir "{directory}"\n'
		f'echo MDMP fresh minidump>"{dump}"\n'
		+ windows_tail
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
		build = Path(
			"C:/Telegram/twin/out" if os.name == "nt" else "/Telegram/twin/out"
		)
		exe = build / "Debug/Telegram.exe"
		records = [
			{
				"pid": 10,
				"parent_pid": 1,
				"name": "cmake.exe",
				"executable": "C:/Tools/cmake.exe",
				"command_line": f"cmake --build {build.as_posix()}",
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
			{process["pid"]: process["reason"] for process in selected},
			{
				10: "exact-build-tree-command",
				11: "verified-build-process-descendant",
				12: "direct-build-artifact-holder",
				14: "exact-checkout-executable",
			},
		)

	def test_build_lock_recovery_deletes_only_named_build_artifacts(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
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

	def test_test_run_reports_a_death_after_complete(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			exe = write_fake_exe(debug / "Telegram", (
				'LOG="$TDESKTOP_TEST_EVIDENCE_DIR/test_log.txt"\n'
				'echo "TEST_RESULT: PASS: row painted" >> "$LOG"\n'
				'echo "TEST_COMPLETE" >> "$LOG"\n'
				"exit 3\n"
			), (
				'set "LOG=%TDESKTOP_TEST_EVIDENCE_DIR%\\test_log.txt"\n'
				'echo TEST_RESULT: PASS: row painted>>"%LOG%"\n'
				'echo TEST_COMPLETE>>"%LOG%"\n'
				"exit /b 3\n"
			))
			result = run_test_run(exe, root / "run1")
			self.assertEqual(result["outcome"], "exited")
			self.assertEqual(result["verdict_hint"], "died-after-complete")
			self.assertTrue(result["test_complete"])
			self.assertEqual(result["exit_code"], 3)
			self.assertEqual(result["death_signals"], ["exit_code"])
			self.assertEqual(result["crashpad_dumps_added"], [])
			self.assertFalse(result["crash_report_fresh"])
			self.assertEqual(result["dumps"], [])

	def test_test_run_reports_both_death_signals_after_complete(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			fresh = (
				debug / workspace.PORTABLE_LIVE / "tdata" / "dumps"
				/ workspace.CRASHPAD_COMPLETED_DIR / "both.dmp"
			)
			exe = write_dump_after_complete_exe(
				debug / "Telegram", fresh, "exit 11\n", "exit /b 11\n",
			)
			result = run_test_run(exe, root / "run1")
			self.assertEqual(result["verdict_hint"], "died-after-complete")
			self.assertEqual(result["exit_code"], 11)
			self.assertEqual(
				result["death_signals"],
				["crashpad_dump", "exit_code"],
			)
			self.assertEqual(result["crashpad_dumps_added"], [str(fresh)])

	def test_test_run_counts_a_crashpad_dump_written_during_the_run(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			fresh = (
				debug / workspace.PORTABLE_LIVE / "tdata" / "dumps"
				/ workspace.CRASHPAD_COMPLETED_DIR / "fresh.dmp"
			)
			exe = write_dump_after_complete_exe(
				debug / "Telegram", fresh, "exit 0\n", "exit /b 0\n",
			)
			result = run_test_run(exe, root / "run1")
			self.assertEqual(result["outcome"], "exited")
			self.assertEqual(result["exit_code"], 0)
			self.assertTrue(result["test_complete"])
			self.assertEqual(result["verdict_hint"], "died-after-complete")
			self.assertEqual(result["death_signals"], ["crashpad_dump"])
			self.assertEqual(result["crashpad_dumps_added"], [str(fresh)])
			self.assertEqual(result["dumps"], [])

	def test_test_run_counts_a_breakpad_dump_written_during_the_run(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			fresh = (
				debug / workspace.PORTABLE_LIVE / "tdata" / "dumps"
				/ "breakpad.dmp"
			)
			exe = write_dump_after_complete_exe(
				debug / "Telegram", fresh, "exit 0\n", "exit /b 0\n",
			)
			result = run_test_run(exe, root / "run1")
			self.assertEqual(result["outcome"], "exited")
			self.assertEqual(result["exit_code"], 0)
			self.assertTrue(result["test_complete"])
			self.assertEqual(result["verdict_hint"], "died-after-complete")
			self.assertEqual(result["death_signals"], ["breakpad_dump"])
			self.assertEqual(result["dumps"], [str(fresh)])
			self.assertEqual(result["crashpad_dumps_added"], [])

	def test_test_run_ignores_a_crashpad_dump_from_before_the_run(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			self.assertEqual(workspace.setup_test_account(debug), "fresh-copy")
			completed = (
				debug / workspace.PORTABLE_LIVE / "tdata" / "dumps"
				/ workspace.CRASHPAD_COMPLETED_DIR
			)
			completed.mkdir(parents=True)
			old = completed / "old.dmp"
			old.write_bytes(b"MDMP old minidump\n")
			exe = write_complete_markers_exe(debug / "Telegram")
			run_dir = root / "run1"
			result = run_test_run(exe, run_dir)
			self.assertEqual(result["account"], "reused-marked-live")
			self.assertEqual(result["verdict_hint"], "complete")
			self.assertEqual(result["exit_code"], 0)
			self.assertEqual(result["death_signals"], [])
			self.assertEqual(result["crashpad_dumps_added"], [])
			self.assertEqual(result["stale_crash_cleared"], [])
			self.assertEqual(old.read_bytes(), b"MDMP old minidump\n")
			self.assertFalse((run_dir / workspace.STALE_CRASH_DIR).exists())

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

	def test_test_run_keeps_a_grace_kill_complete(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			debug = make_portable_root(root)
			exe = write_fake_exe(debug / "Telegram", (
				'LOG="$TDESKTOP_TEST_EVIDENCE_DIR/test_log.txt"\n'
				'echo "TEST_COMPLETE" >> "$LOG"\n'
				"sleep 30\n"
			), (
				'set "LOG=%TDESKTOP_TEST_EVIDENCE_DIR%\\test_log.txt"\n'
				'echo TEST_COMPLETE>>"%LOG%"\n'
				":loop\ngoto loop\n"
			))
			result = run_test_run(exe, root / "run1", grace=1.0)
			self.assertEqual(result["outcome"], "killed-after-complete")
			self.assertEqual(result["verdict_hint"], "complete")
			self.assertTrue(result["test_complete"])
			self.assertIsNone(result["exit_code"])
			self.assertEqual(result["death_signals"], [])

	def test_test_run_reports_a_grace_kill_with_a_dump(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			dump = (
				debug / workspace.PORTABLE_LIVE / "tdata" / "dumps"
				/ workspace.CRASHPAD_COMPLETED_DIR / "grace.dmp"
			)
			exe = write_dump_after_complete_exe(
				debug / "Telegram", dump, "sleep 30\n", ":loop\ngoto loop\n",
			)
			result = run_test_run(exe, root / "run1", grace=1.0)
			self.assertEqual(result["outcome"], "killed-after-complete")
			self.assertEqual(result["verdict_hint"], "died-after-complete")
			self.assertTrue(result["test_complete"])
			self.assertIsNone(result["exit_code"])
			self.assertEqual(result["death_signals"], ["crashpad_dump"])
			self.assertEqual(result["crashpad_dumps_added"], [str(dump)])

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

	def test_test_run_ignores_a_stale_crash_report_copied_from_golden(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			debug = make_portable_root(root)
			golden = debug / workspace.PORTABLE_GOLDEN
			(golden / "tdata" / "dumps").mkdir(parents=True)
			report = golden / "tdata" / "working"
			report.write_bytes(b"Assertion: last month\n")
			dump = golden / "tdata" / "dumps" / "golden.dmp"
			dump.write_bytes(b"MDMP old minidump\n")
			for path in (report, dump):
				os.utime(path, (1600000000, 1600000000))
			exe = write_complete_markers_exe(debug / "Telegram")
			result = run_test_run(exe, root / "run1")
			live = debug / workspace.PORTABLE_LIVE
			working = live / "tdata" / "working"
			self.assertEqual(result["account"], "fresh-copy")
			self.assertTrue(result["test_complete"])
			self.assertEqual(working.read_bytes(), b"Assertion: last month\n")
			self.assertEqual(working.stat().st_mtime, report.stat().st_mtime)
			self.assertEqual(result["crash_report"], str(working))
			self.assertFalse(result["crash_report_fresh"])
			self.assertIsNone(result["crash_report_excerpt"])
			self.assertEqual(result["dumps"], [])
			self.assertEqual(result["death_signals"], [])
			self.assertEqual(result["verdict_hint"], "complete")

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

	def test_overlay_apply_reports_a_non_ascii_conflict_literally(self):
		name = "\u00e9\u6587.txt"
		self.assertEqual(name.encode("utf-8"), b"\xc3\xa9\xe6\x96\x87.txt")
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			target = source / name
			target.write_text("base\n", encoding="utf-8")
			git(source, "add", "-A")
			git(source, "commit", "-m", "Add the non-ASCII sample")
			git(
				source, "update-ref",
				workspace.source_task_ref(TASK_ID, "run"), "HEAD",
			)
			(work / workspace.OVERLAY_PATHS_FILE).write_text(
				name + "\n", encoding="utf-8",
			)
			target.write_text("overlay\n", encoding="utf-8")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				saved = run_command(
					workspace.command_overlay_save,
					task=TASK_ID,
					restore="run",
				)
			self.assertEqual(saved["restored"], [name])
			self.assertEqual(target.read_text(encoding="utf-8"), "base\n")
			target.write_text("diverged\n", encoding="utf-8")
			git(source, "add", "-A")
			git(source, "commit", "-m", "Diverge the non-ASCII sample")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				applied = run_command(
					workspace.command_overlay_apply,
					task=TASK_ID,
				)
			self.assertEqual(applied["conflicts"], [name])
			self.assertFalse(applied["applied"])
			self.assertEqual(applied["outside_inventory"], [])
			self.assertEqual(
				git_bytes(
					source,
					"-c", "core.quotePath=false",
					"diff", "--name-only", "--diff-filter=U",
				).decode("utf-8").splitlines(),
				[name],
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
				with self.assertRaisesRegex(
					workspace.WorkspaceError,
					r"outside the overlay inventory: stray\.txt",
				):
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

	def test_source_commit_stages_a_removed_tracked_file(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			(work / "owned-paths.txt").write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			git(source, "rm", "-q", "tracked.txt")
			with mock.patch.object(
				workspace, "task_action_config", return_value=(config, slot),
			):
				result = run_command(
					workspace.command_source_commit,
					task=TASK_ID,
					subject="Remove the orphaned tracked file",
					mark_green=False,
				)
			self.assertEqual(result["committed"], ["tracked.txt"])
			self.assertEqual(
				git(source, "show", "--name-status", "--format=", "HEAD"),
				"D\ttracked.txt",
			)
			self.assertEqual(
				git(source, "show", "-s", "--format=%B", "HEAD"),
				f"Remove the orphaned tracked file\n\nTask: {TASK_ID}",
			)
			self.assertFalse((source / "tracked.txt").exists())
			self.assertEqual(git(source, "status", "--porcelain"), "")
			self.assertEqual(git(source, "rev-list", "--count", "HEAD"), "2")

	def test_source_commit_stages_a_non_ascii_tracked_file(self):
		name = "\u00e9\u6587.txt"
		self.assertEqual(name.encode("utf-8"), b"\xc3\xa9\xe6\x96\x87.txt")
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			(source / name).write_text("base\n", encoding="utf-8")
			git(source, "add", "-A")
			git(source, "commit", "-m", "Add the non-ASCII sample")
			(work / "owned-paths.txt").write_text(
				name + "\n", encoding="utf-8",
			)
			(source / name).write_text("task\n", encoding="utf-8")
			covered = workspace.path_is_covered
			compared = []

			def record_covered(path, roots):
				compared.append(path)
				return covered(path, roots)

			with (
				mock.patch.object(
					workspace, "task_action_config", return_value=(config, slot),
				),
				mock.patch.object(
					workspace, "path_is_covered", side_effect=record_covered,
				),
			):
				result = run_command(
					workspace.command_source_commit,
					task=TASK_ID,
					subject="Correct the non-ASCII sample",
					mark_green=False,
				)
			self.assertEqual(compared, [name])
			self.assertEqual(result["committed"], [name])
			self.assertEqual(
				git_bytes(
					source,
					"-c", "core.quotePath=false",
					"show", "--name-status", "--format=", "HEAD",
				).decode("utf-8").strip(),
				"M\t" + name,
			)
			self.assertEqual(
				git(source, "show", "-s", "--format=%B", "HEAD"),
				f"Correct the non-ASCII sample\n\nTask: {TASK_ID}",
			)
			self.assertEqual(git(source, "status", "--porcelain"), "")
			self.assertEqual(git(source, "rev-list", "--count", "HEAD"), "3")

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

	def test_already_satisfied_source_state_requires_baseline(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, _, _, config = source_repo_with_task(root)
			for name in ("base", "run"):
				git(source, "update-ref", workspace.source_task_ref(TASK_ID, name), "HEAD")
			workspace.validate_source_state(config, TASK_ID, False)

			(source / "tracked.txt").write_text("task\n", encoding="utf-8")
			git(source, "commit", "-am", "Correct peer actions", "-m", f"Task: {TASK_ID}")
			for name in ("green", "run"):
				git(source, "update-ref", workspace.source_task_ref(TASK_ID, name), "HEAD")
			with self.assertRaisesRegex(workspace.WorkspaceError, "already-satisfied"):
				workspace.validate_source_state(config, TASK_ID, False)
			workspace.validate_source_state(config, TASK_ID, True)

	def test_adaptive_outcome_result_contract(self):
		path = Path("work/result.md")
		workspace.validate_outcome_result([
			"Outcome: changed",
			"Touched: tracked.txt",
			"Test-Report: work/test.md",
		], path, True)
		workspace.validate_outcome_result([
			"Outcome: already-satisfied",
			"Touched: none",
			"Test-Report: work/test.md",
		], path, True)
		workspace.validate_outcome_result([
			"Outcome: blocked",
			"Touched: none",
		], path, False)

		with self.assertRaisesRegex(workspace.WorkspaceError, "adaptive evidence"):
			workspace.validate_outcome_result([
				"Outcome: changed",
				"Touched: tracked.txt",
			], path, True)
		with self.assertRaisesRegex(workspace.WorkspaceError, "touched paths"):
			workspace.validate_outcome_result([
				"Outcome: changed",
				"Touched: none",
				"Test-Report: work/test.md",
			], path, True)
		with self.assertRaisesRegex(workspace.WorkspaceError, "Touched: none"):
			workspace.validate_outcome_result([
				"Outcome: already-satisfied",
				"Touched: tracked.txt",
				"Test-Report: work/test.md",
			], path, True)
		with self.assertRaisesRegex(workspace.WorkspaceError, "Outcome must be"):
			workspace.validate_outcome_result([
				"Outcome: blocked",
				"Touched: none",
				"Test-Report: work/test.md",
			], path, True)


	def test_test_block_requires_real_recovery_exhaustion(self):
		with tempfile.TemporaryDirectory() as temporary:
			task = Path(temporary) / "task"
			work = task / "work"
			work.mkdir(parents=True)
			result = work / "result.md"

			def check(verdict, unverified="full presentation"):
				workspace.validate_blocked_result([
					f"Verdict: {verdict}",
					"Blocker-Type: test",
					f"Unverified: {unverified}",
				], result)

			(work / "test.md").write_text(
				"## Recovery exhaustion\n\n| Strategy | Evidence |\n",
				encoding="utf-8",
			)
			check("recovery-exhausted: fixture unavailable")

			for verdict in (
				"TEST_FLAW at MAX_TEST_RUNS",
				"blank-capture at run cap",
				"missing screenshot",
			):
				with self.assertRaisesRegex(
					workspace.WorkspaceError,
					"recoverable harness or evidence failure",
				):
					check(verdict)

			with self.assertRaisesRegex(
				workspace.WorkspaceError,
				"exact unverified behavior",
			):
				check("recovery-exhausted: fixture unavailable", "none")

			(work / "test.md").unlink()
			with self.assertRaisesRegex(
				workspace.WorkspaceError,
				"Recovery exhaustion",
			):
				check("recovery-exhausted: fixture unavailable")

			with self.assertRaisesRegex(
				workspace.WorkspaceError,
				"capability report",
			):
				check("computer-use-unavailable: exact app identity")
			(task / "computer-use-capability.md").write_text(
				"unavailable\n",
				encoding="utf-8",
			)
			check("computer-use-unavailable: exact app identity")

	def test_task_type_defaults_to_implementation(self):
		with tempfile.TemporaryDirectory() as temporary:
			slot = Path(temporary)
			directory = write_task(slot, status="todo", claimed_by=None)
			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["type"], "implement")

			workspace.update_state(directory / "state.yaml", {"type": "verify"})
			with self.assertRaisesRegex(workspace.WorkspaceError, "Only type 'implement'"):
				workspace.load_state(slot, directory / "state.yaml")

			workspace.update_state(directory / "state.yaml", {"status": "approved"})
			state = workspace.load_state(slot, directory / "state.yaml")
			self.assertEqual(state["type"], "verify")
			text = (directory / "state.yaml").read_text(encoding="utf-8")
			self.assertEqual(
				text.splitlines()[:2],
				["status: approved", "type: verify"],
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

	def test_finish_publishes_split_required_with_carried_work(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			config["slot_worktree"] = str(slot)
			for name in ("base", "run"):
				git(
					source,
					"update-ref",
					workspace.source_task_ref(TASK_ID, name),
					"HEAD",
				)
			(work / "owned-paths.txt").write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			(work / "split-proposal.md").write_text(
				"# Split proposal\n\nTwo independent boundaries.\n",
				encoding="utf-8",
			)
			(work / "result.md").write_text(
				"""STATUS: SPLIT_REQUIRED
Outcome: split-required
Verdict: SPLIT_REQUIRED
Implementation: retained
Touched: tracked.txt
Split-Proposal: work/split-proposal.md
Checkout: source-state-retained
""",
				encoding="utf-8",
			)
			(source / "tracked.txt").write_text("carried\n", encoding="utf-8")
			with (
				mock.patch.object(
					workspace,
					"task_action_config",
					return_value=(config, slot),
				),
				mock.patch.object(workspace, "commit_paths", return_value=True),
			):
				result = run_command(
					workspace.command_finish,
					task=TASK_ID,
					status="split-required",
					model="gpt-5.6-sol",
				)

			state = workspace.load_state(slot, work.parent / "state.yaml")
			self.assertEqual(result["status"], "split-required")
			self.assertEqual(state["status"], "split-required")
			self.assertEqual(state["phase"], "split-required")
			carried = json.loads((work / "carried-work.json").read_text(
				encoding="utf-8-sig",
			))
			self.assertEqual(carried["implementation"], "retained")
			self.assertEqual(carried["owned_dirty_paths"], ["tracked.txt"])
			self.assertEqual((source / "tracked.txt").read_text(), "carried\n")

	def test_carried_work_snapshot_seals_owned_submodule_changes(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source, slot, work, config = source_repo_with_task(root)
			nested = source / "nested"
			git_repo(nested)
			(nested / "owned.txt").write_text("base\n", encoding="utf-8")
			git(nested, "add", "owned.txt")
			git(nested, "commit", "-m", "Create nested baseline")
			nested_head = git(nested, "rev-parse", "HEAD")
			git(
				source,
				"update-index",
				"--add",
				"--cacheinfo",
				f"160000,{nested_head},nested",
			)
			git(source, "commit", "-m", "Track nested repository")
			(work / "owned-paths.txt").write_text(
				"nested/owned.txt\n", encoding="utf-8",
			)
			(nested / "owned.txt").write_text("carried\n", encoding="utf-8")

			snapshot = workspace.source_worktree_snapshot(
				config, slot, TASK_ID,
			)

			self.assertEqual(snapshot["owned_dirty_paths"], ["nested"])
			self.assertEqual(snapshot["outside_owned_paths"], [])
			self.assertNotEqual(snapshot["worktree_digest"], "0" * 64)

	def test_split_publish_routes_and_starts_implementation_carrier(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			config = inbox_worktrees(root)
			main = Path(config["ai_main"])
			slot = Path(config["slot_worktree"])
			source = root / "source"
			git_repo(source)
			(source / "Telegram" / "build").mkdir(parents=True)
			(source / "tracked.txt").write_text("base\n", encoding="utf-8")
			git(source, "add", "-A")
			git(source, "commit", "-m", "Create baseline")
			config["source_root"] = str(source)
			source_task = "2026/07/18/active-task"
			source_dir = main / "tasks" / source_task
			state_path = source_dir / "state.yaml"
			state_path.write_text(
				state_path.read_text(encoding="utf-8")
				.replace("status: todo", "status: split-required")
				.replace("claimed_by: null", "claimed_by: macbook-twork")
				.replace("claimed_at: null", "claimed_at: 2026-07-18T10:00:00+04:00")
				.replace("claim_order: null", "claim_order: 1")
				.replace("phase: null", "phase: split-required")
				.replace(
					"inbox_receipt:",
					"model: gpt-5.6-sol\ninbox_receipt:",
				),
				encoding="utf-8",
			)
			work = source_dir / "work"
			work.mkdir()
			(work / "owned-paths.txt").write_text(
				"tracked.txt\n", encoding="utf-8",
			)
			for name in ("base", "run"):
				git(
					source,
					"update-ref",
					workspace.source_task_ref(source_task, name),
					"HEAD",
				)
			(source / "tracked.txt").write_text("carried\n", encoding="utf-8")
			snapshot = workspace.source_worktree_snapshot(
				config, main, source_task,
			)
			(work / "carried-work.json").write_text(
				json.dumps({"implementation": "retained", **snapshot}) + "\n",
				encoding="utf-8",
			)
			git(main, "add", f"tasks/{source_task}")
			git(main, "commit", "-m", f"Split-required {source_task}")
			git(slot, "merge", "--ff-only", "master")

			replacements = [
				"2026/07/20/adopt-active-task-implementation",
				"2026/07/20/finish-active-task-integration",
			]
			receipt = "receipts/2026/07/20/split-active-task.md"
			for task_id in replacements:
				directory = slot / "tasks" / task_id
				directory.mkdir(parents=True)
				(directory / "task.md").write_text(
					f"# {task_id.rsplit('/', 1)[-1]}\n",
					encoding="utf-8",
				)
				(directory / "state.yaml").write_text(
					f"""status: todo
type: implement
created: 2026-07-20
project: null
depends_on: []
claimed_by: null
claimed_at: null
claim_order: null
lease_until: null
phase: null
inbox_receipt: {receipt}
""",
					encoding="utf-8",
				)
			receipt_path = slot / receipt
			receipt_path.parent.mkdir(parents=True)
			receipt_path.write_text(
				"\n".join([source_task, *replacements]) + "\n",
				encoding="utf-8",
			)
			with mock.patch.object(
				workspace, "worktree_config", return_value=config,
			):
				result = run_command(
					workspace.command_split_publish,
					source_task=source_task,
					replacements=replacements,
					receipt=receipt,
					implementation_carrier=replacements[0],
					paths=[
						f"tasks/{source_task}",
						*(f"tasks/{task_id}" for task_id in replacements),
						receipt,
					],
				)
			self.assertEqual(result["status"], "split")
			self.assertTrue((main / "tasks" / source_task / "split.yaml").is_file())
			self.assertFalse((main / "tasks" / source_task / "state.yaml").exists())
			resolved = workspace.resolve_task(
				main, workspace.load_states(main), source_task,
			)
			self.assertEqual(resolved["status"], "split")
			self.assertEqual(resolved["split_into"], replacements)
			carrier = workspace.load_states(main)[replacements[0]]
			self.assertEqual(carrier["claimed_by"], "macbook-twork")
			self.assertEqual(carrier["carried_from"], source_task)
			self.assertEqual((source / "tracked.txt").read_text(), "carried\n")

			with mock.patch.object(
				workspace, "worktree_config", return_value=config,
			):
				started = run_command(
					workspace.command_start,
					task=replacements[0],
					require=[],
				)
			self.assertEqual(started["status"], "in-progress")
			self.assertIsNotNone(workspace.resolved_ref(
				source,
				workspace.source_task_ref(replacements[0], "base"),
			))
			self.assertIsNone(workspace.resolved_ref(
				source,
				workspace.source_task_ref(source_task, "base"),
			))


if __name__ == "__main__":
	unittest.main()
