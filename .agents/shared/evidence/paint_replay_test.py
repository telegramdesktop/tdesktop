#!/usr/bin/env python3

import copy
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

import native_exec
import paint_replay


HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
COMPONENT_SOURCE = os.path.join(HERE, "paint_replay_component")
ASSESSOR = os.path.join(HERE, "paint_replay.py")
WINDOW_ASSESSOR = os.path.join(HERE, "motion_window.py")
DOCUMENT = os.path.join(HERE, "paint-replay.md")
FONT = os.path.join(
	SOURCE_ROOT,
	"Telegram",
	"lib_ui",
	"fonts",
	"OpenSans-Regular.ttf")
SKIP_COMPONENT_VARIABLE = "PAINT_REPLAY_TEST_SKIP_COMPONENT"
HOST = {"platform": "Darwin", "machine": "arm64", "translated": False}
WARMUP = 3
SAMPLES = 25
QUANTILES = [0.5, 0.9]
ENVELOPE = 0.9
SURFACE = {"rootType": "Synthetic::Owner", "size": [4, 2], "dpr": 2}
DEVICE_PIXELS = 32
RECORDER_QT = {
	"version": "6.99.0",
	"debug": True,
	"pictureFormat": 99,
	"sidecar": 1,
	"platform": "synthetic",
	"colorScheme": "light",
}
FONTS = [{"family": "Synthetic Sans", "resolved": "Synthetic Sans"}]
COMPONENT_TIMEOUT = 900
FIXTURE_PRESS_AT = 2000
FIXTURE_RELEASE_AT = 2100
WINDOW_BUDGET_MS = 50.0
WINDOW_QUANTILE = 0.9
SIDECAR_MAGIC = b"PSTB"
FORMAT_ARGB32_PREMULTIPLIED = 6
NULL_BYTE_ARRAY = 0xFFFFFFFF
FFMPEG_ROW_ALIGNMENT = 64


def text_digest(text):
	return hashlib.sha256(text.encode("utf-8")).hexdigest()


def bytes_digest(data):
	return hashlib.sha256(data).hexdigest()


def write(root, name, data):
	path = os.path.join(root, name)
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "wb") as file:
		file.write(data)
	return path


def load(path):
	with open(path, "r", encoding="utf-8") as file:
		return json.load(file)


def save(path, value):
	with open(path, "w", encoding="utf-8") as file:
		json.dump(value, file, indent=1)


def report_runner(argv, timeout):
	report = {
		"schema": 1,
		"nonce": argv[2],
		"buildArch": "arm64",
		"runningArch": "arm64",
		"translated": False,
	}
	return {
		"exitCode": 0,
		"stdout": native_exec.REPORT_PREFIX + json.dumps(report) + "\n",
		"stderr": "",
		"durationMs": 1.0,
	}


def exit_three_runner(argv, timeout):
	return dict(report_runner(argv, timeout), exitCode=3)


def controlled_gate(path, runner=report_runner):
	return native_exec.run_gate(
		path,
		"arm64",
		timeout=5,
		runner=runner,
		host=lambda: dict(HOST))


def failed(result):
	return {check["reason"] for check in result["checks"] if not check["pass"]}


def by_name(rows, name):
	return next(row for row in rows if row.get("name") == name)


def times(seed):
	return [
		100000 + ((index * 7 + seed) % SAMPLES) * 1000
		for index in range(SAMPLES)
	]


def record_row(name, role, region=(0, 0, 4, 2), **fields):
	row = {
		"name": name,
		"role": role,
		"recordMs": 0.25,
		"root": {
			"type": SURFACE["rootType"],
			"size": list(SURFACE["size"]),
			"region": list(region),
			"flags": 1,
		},
		"qpic": {
			"file": name + ".qpic",
			"bytes": 100,
			"sha256": text_digest("qpic " + name),
		},
		"tables": {
			"file": name + ".tables",
			"bytes": 200,
			"sha256": text_digest("tables " + name),
		},
		"counts": {"images": 1, "pixmaps": 1, "brushes": 2, "pens": 2},
		"fonts": copy.deepcopy(FONTS),
		"referenceSha256": text_digest("reference " + name),
	}
	row.update(fields)
	return row


def replay_row(record, **fields):
	row = {
		"name": record["name"],
		"role": record["role"],
		"qpic": {
			"bytes": record["qpic"]["bytes"],
			"sha256": record["qpic"]["sha256"],
		},
		"tables": {
			"bytes": record["tables"]["bytes"],
			"sha256": record["tables"]["sha256"],
		},
		"replaySha256": record["referenceSha256"],
		"changedPixels": DEVICE_PIXELS,
		"coveragePixels": DEVICE_PIXELS,
		"surfacePixels": DEVICE_PIXELS,
		"fonts": copy.deepcopy(record["fonts"]),
	}
	row.update(fields)
	return row


def synthetic_manifest():
	empty = record_row(
		"empty_0",
		"empty",
		recordMs=0,
		counts={"images": 0, "pixmaps": 0, "brushes": 0, "pens": 0},
		fonts=[])
	empty["root"] = {"type": "", "size": [-1, -1], "region": [0, 0, 0, 0],
		"flags": 0}
	return {
		"schema": 1,
		"qt": dict(RECORDER_QT),
		"surface": copy.deepcopy(SURFACE),
		"sentinels": [[70, 100, 120], [250, 10, 160]],
		"streams": [
			record_row(
				"rest_0",
				"timed",
				window="rest",
				at=1000,
				pressed=False,
				moving=False,
				position=[1, 1],
				directDifference=0),
			record_row(
				"drag_0",
				"timed",
				window="drag",
				at=2000,
				pressed=True,
				moving=True,
				position=[2, 1]),
			record_row(
				"absent_0",
				"absent",
				pairedWith="rest_0",
				fieldRect=[1, 0, 2, 1]),
			record_row("narrow_0", "narrow", region=(0, 0, 1, 2)),
			empty,
		],
	}


def synthetic_replay(manifest, executable, started_at, manifest_bytes):
	records = {row["name"]: row for row in manifest["streams"]}
	return {
		"schema": 1,
		"tool": "paint_replay",
		"startedAt": started_at,
		"finishedAt": started_at + 10,
		"executable": executable,
		"native": {
			"buildArch": "arm64",
			"runningArch": "arm64",
			"translated": False,
		},
		"qt": dict(RECORDER_QT, debug=False),
		"fonts": [],
		"warmup": WARMUP,
		"samples": SAMPLES,
		"surface": copy.deepcopy(SURFACE),
		"manifest": {
			"path": "/run/manifest.json",
			"bytes": len(manifest_bytes),
			"sha256": bytes_digest(manifest_bytes),
		},
		"rows": [
			replay_row(records["rest_0"], timesNs=times(0)),
			replay_row(records["drag_0"], timesNs=times(3)),
			replay_row(records["absent_0"], pair={
				"with": "rest_0",
				"differingPixels": 4,
				"bounds": [2, 0, 4, 2],
			}),
			replay_row(records["narrow_0"], changedPixels=8, coveragePixels=8),
			replay_row(records["empty_0"], changedPixels=0, coveragePixels=0),
		],
	}


def replay_start(gate):
	return gate["finishedAt"] + 5


def replay_after_gate(case):
	case["replay"]["startedAt"] = replay_start(case["gate"])
	case["replay"]["finishedAt"] = case["replay"]["startedAt"] + 10


def synthetic_case(executable):
	manifest = synthetic_manifest()
	manifest_bytes = json.dumps(manifest).encode("utf-8")
	gate = controlled_gate(executable)
	replay = synthetic_replay(
		manifest,
		executable,
		replay_start(gate),
		manifest_bytes)
	options = {
		"hostArch": "arm64",
		"warmup": WARMUP,
		"samples": SAMPLES,
		"quantiles": list(QUANTILES),
		"envelopeQuantile": ENVELOPE,
		"manifestSha256": bytes_digest(manifest_bytes),
	}
	return {
		"manifest": manifest,
		"replay": replay,
		"gate": gate,
		"options": options,
	}


class AccountingTests(unittest.TestCase):
	def rows(self):
		return [
			{"name": "a", "qpic": {"sha256": "1"}, "tables": {"sha256": "2"}},
			{"name": "b", "qpic": {"sha256": "3"}},
		]

	def codes(self, recorded, replayed):
		return [code for code, _ in paint_replay.account_streams(
			recorded,
			replayed)]

	def test_bijection_passes(self):
		self.assertEqual(self.codes(self.rows(), self.rows()), [])

	def test_duplicate_record(self):
		recorded = self.rows() + [self.rows()[0]]
		problems = paint_replay.account_streams(recorded, self.rows())
		self.assertEqual(problems, [
			("duplicate-record", {"stream": "a", "count": 2}),
		])

	def test_duplicate_replay(self):
		replayed = self.rows() + [self.rows()[1]]
		self.assertEqual(
			self.codes(self.rows(), replayed),
			["duplicate-replay"])

	def test_missing_replay(self):
		problems = paint_replay.account_streams(self.rows(), self.rows()[:1])
		self.assertEqual(problems, [("missing-replay", {"stream": "b"})])

	def test_extra_replay(self):
		replayed = self.rows() + [{"name": "c"}]
		problems = paint_replay.account_streams(self.rows(), replayed)
		self.assertEqual(problems, [("extra-replay", {"stream": "c"})])

	def test_mismatched_identity(self):
		for key in ("qpic", "tables"):
			with self.subTest(key=key):
				replayed = self.rows()
				replayed[0][key]["sha256"] = "other"
				problems = paint_replay.account_streams(self.rows(), replayed)
				self.assertEqual([code for code, _ in problems], [
					"mismatched-identity",
				])
				self.assertEqual(problems[0][1]["key"], key)
				self.assertEqual(problems[0][1]["stream"], "a")

	def test_digest_absent_on_one_side_is_not_compared(self):
		replayed = self.rows()
		del replayed[0]["qpic"]
		replayed[1]["tables"] = {"sha256": "4"}
		self.assertEqual(self.codes(self.rows(), replayed), [])

	def test_unnamed_rows_are_unmatched(self):
		self.assertEqual(
			self.codes(self.rows() + [{"role": "timed"}], self.rows()),
			["missing-replay"])
		self.assertEqual(
			self.codes(self.rows(), self.rows() + [{"name": ""}]),
			["extra-replay"])


class QuantileTests(unittest.TestCase):
	def check(self, count, q, rank):
		values = list(range(count, 0, -1))
		result = paint_replay.quantile([value * 1000 for value in values], q)
		self.assertEqual(result["rank"], rank)
		self.assertEqual(result["n"], count)
		self.assertEqual(result["q"], q)
		self.assertEqual(result["valueMs"], rank / 1000)

	def test_nearest_rank(self):
		cases = (
			(1, 0.5, 1),
			(1, 1, 1),
			(19, 0.5, 10),
			(19, 0.9, 18),
			(19, 0.99, 19),
			(25, 0.5, 13),
			(25, 0.9, 23),
			(25, 0.99, 25),
			(400, 0.5, 200),
			(400, 0.95, 380),
			(400, 0.99, 396),
		)
		for count, q, rank in cases:
			with self.subTest(n=count, q=q):
				self.check(count, q, rank)

	def test_decimal_quantiles_have_exact_ranks(self):
		self.check(100, 0.07, 7)
		self.check(100, 0.14, 14)
		self.check(25, 0.28, 7)

	def test_refuses_invalid_input(self):
		for q in (0, -0.1, 1.5, float("nan"), True, "0.5"):
			with self.subTest(q=q):
				with self.assertRaises(ValueError):
					paint_replay.quantile([1], q)
		for samples in ([], None, [1, -1], [1, True], [float("inf")]):
			with self.subTest(samples=samples):
				with self.assertRaises(ValueError):
					paint_replay.quantile(samples, 0.5)

	def test_statistics(self):
		result = paint_replay.statistics(times(0), QUANTILES)
		self.assertEqual(result["n"], SAMPLES)
		self.assertEqual(result["minMs"], 0.1)
		self.assertEqual(result["maxMs"], 0.124)
		self.assertEqual(result["medianMs"], 0.112)
		self.assertEqual(result["medianRank"], 13)
		self.assertEqual(
			[(item["q"], item["rank"], item["valueMs"])
				for item in result["quantiles"]],
			[(0.5, 13, 0.112), (0.9, 23, 0.122)])


class AssessmentTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix="paint replay ")
		self.addCleanup(temporary.cleanup)
		self.root = temporary.name
		self.component = write(self.root, "build/paint_replay", b"replayer")
		self.other = write(self.root, "debug/paint_replay", b"other replayer")
		self.case = synthetic_case(self.component)
		positive = self.assess()
		self.assertTrue(positive["allPassed"], positive["reasons"])

	def assess(self, mutate=None):
		case = copy.deepcopy(self.case)
		if mutate:
			mutate(case)
		return paint_replay.assess(
			case["manifest"],
			case["replay"],
			case["gate"],
			case["options"])

	def assertRefused(self, mutate, codes):
		result = self.assess(mutate)
		self.assertFalse(result["allPassed"])
		self.assertEqual(failed(result), set(codes))
		self.assertEqual(set(result["reasons"]), set(codes))
		for check in result["checks"]:
			if not check["pass"]:
				self.assertTrue(check["message"], check["reason"])
				self.assertIn("details", check["facts"])
		return result

	def record(self, case, name):
		return by_name(case["manifest"]["streams"], name)

	def replayed(self, case, name):
		return by_name(case["replay"]["rows"], name)

	def test_positive_reports(self):
		result = self.assess()
		self.assertEqual(result["reasons"], [])
		self.assertTrue(all(check["pass"] for check in result["checks"]))
		self.assertEqual(
			[stream["name"] for stream in result["streams"]],
			["rest_0", "drag_0"])
		rest = result["streams"][0]
		self.assertEqual(rest["replay"]["n"], SAMPLES)
		self.assertEqual(
			[item["rank"] for item in rest["replay"]["quantiles"]],
			[13, 23])
		self.assertEqual(rest["envelopeQuantile"]["q"], ENVELOPE)
		self.assertEqual(rest["envelopeQuantile"]["rank"], 23)
		self.assertEqual(rest["envelopeMs"], 0.372)
		self.assertIs(rest["recorderDebug"], True)
		self.assertIs(rest["replayerDebug"], False)
		self.assertEqual(result["controls"]["absent"][0]["fieldDeviceRect"], [
			2, 0, 4, 2,
		])
		self.assertEqual(result["controls"]["surface"]["devicePixels"], 32)
		self.assertIn("composition", result["boundary"])
		self.assertIn("statistics", result["scope"])
		limits = " ".join(result["limits"])
		for phrase in (
				"not a Release Telegram frame time",
				"not a compositor or presentation cadence",
				"not a population or confidence bound"):
			self.assertIn(phrase, limits)

	def test_missing_gate(self):
		def mutate(case):
			case["gate"] = None
		self.assertRefused(mutate, {"missing-gate"})

	def test_gate_for_another_file(self):
		def mutate(case):
			case["gate"] = controlled_gate(self.other)
			replay_after_gate(case)
		self.assertRefused(mutate, {
			"gate-executable-mismatch",
			"gate-stale-binary",
		})

	def test_refused_gate(self):
		def mutate(case):
			case["gate"] = controlled_gate(self.component, exit_three_runner)
			replay_after_gate(case)
		self.assertRefused(mutate, {"gate-not-passed"})

	def test_gate_without_execution(self):
		def mutate(case):
			del case["gate"]["execution"]
		self.assertRefused(mutate, {"gate-no-execution"})

	def test_gate_nonce(self):
		def mutate(case):
			case["gate"]["execution"]["report"]["nonce"] = "0" * 32
		self.assertRefused(mutate, {"gate-nonce"})

	def test_replayer_rebuilt_after_gating(self):
		write(self.root, "build/paint_replay", b"rebuilt replayer")
		self.assertRefused(None, {"gate-stale-binary"})

	def test_gate_for_another_host_arch(self):
		def mutate(case):
			case["options"]["hostArch"] = "x86_64"
		self.assertRefused(mutate, {"gate-host-arch", "replay-not-native"})

	def test_gate_after_replay(self):
		def mutate(case):
			case["replay"]["startedAt"] = case["gate"]["finishedAt"] - 1
		self.assertRefused(mutate, {"gate-after-replay"})

	def test_gate_without_finish_time(self):
		def mutate(case):
			del case["gate"]["finishedAt"]
		self.assertRefused(mutate, {"gate-after-replay"})

	def test_replay_not_native(self):
		for key, value in (("translated", True), ("runningArch", "x86_64")):
			with self.subTest(key=key):
				def mutate(case):
					case["replay"]["native"][key] = value
				self.assertRefused(mutate, {"replay-not-native"})

	def test_unverified_qt_build(self):
		def debug(case):
			case["replay"]["qt"]["debug"] = True

		def missing(case):
			del case["replay"]["qt"]["debug"]
		self.assertRefused(debug, {"unverified-qt-build"})
		self.assertRefused(missing, {"unverified-qt-build"})

	def test_replay_fatal(self):
		def mutate(case):
			case["replay"]["error"] = "font-load-failed"
			case["replay"]["errorDetail"] = "font.ttf did not load"
			del case["replay"]["rows"]
		result = self.assertRefused(mutate, {
			"replay-fatal",
			"missing-replay",
			"missing-empty-control",
			"missing-absent-control",
			"missing-narrow-control",
		})
		fatal = next(
			check for check in result["checks"]
			if check["reason"] == "replay-fatal")
		self.assertEqual(
			fatal["facts"]["details"][0]["error"],
			"font-load-failed")

	def test_qt_contract(self):
		cases = (
			("version", "6.99.1", "qt-version-mismatch"),
			("pictureFormat", 98, "picture-format-mismatch"),
			("sidecar", 2, "sidecar-version-mismatch"),
			("platform", "offscreen", "platform-mismatch"),
			("colorScheme", "dark", "platform-mismatch"),
		)
		for key, value, code in cases:
			with self.subTest(key=key):
				def mutate(case):
					case["manifest"]["qt"][key] = value

				def missing(case):
					del case["manifest"]["qt"][key]
					del case["replay"]["qt"][key]
				self.assertRefused(mutate, {code})
				self.assertRefused(missing, {code})

	def test_duplicate_record(self):
		def mutate(case):
			streams = case["manifest"]["streams"]
			streams.append(copy.deepcopy(by_name(streams, "drag_0")))
		self.assertRefused(mutate, {"duplicate-record"})

	def test_duplicate_replay(self):
		def mutate(case):
			rows = case["replay"]["rows"]
			rows.append(copy.deepcopy(by_name(rows, "drag_0")))
		self.assertRefused(mutate, {"duplicate-replay"})

	def test_extra_replay(self):
		def mutate(case):
			extra = copy.deepcopy(self.replayed(case, "narrow_0"))
			extra["name"] = "ghost_0"
			case["replay"]["rows"].append(extra)
		self.assertRefused(mutate, {"extra-replay"})

	def test_missing_replay(self):
		def mutate(case):
			case["replay"]["rows"].remove(self.replayed(case, "drag_0"))
		self.assertRefused(mutate, {"missing-replay"})

	def test_mismatched_identity(self):
		def mutate(case):
			self.replayed(case, "drag_0")["tables"]["sha256"] = "0" * 64
		self.assertRefused(mutate, {"mismatched-identity"})

	def test_baseline_role(self):
		def mutate(case):
			baseline = record_row("baseline_0", "baseline")
			case["manifest"]["streams"].append(baseline)
			case["replay"]["rows"].append(
				replay_row(baseline, timesNs=times(5)))
		self.assertRefused(mutate, {"unknown-role", "sample-accounting"})

	def test_reference_mismatch(self):
		def mutate(case):
			self.replayed(case, "drag_0")["replaySha256"] = "0" * 64
		self.assertRefused(mutate, {"replay-differs-from-recording"})

	def test_fonts_differ(self):
		def mutate(case):
			self.replayed(case, "drag_0")["fonts"] = [{
				"family": "Synthetic Sans",
				"resolved": "Fallback Sans",
			}]
		self.assertRefused(mutate, {"font-not-retained"})

	def test_unfaithful_recording(self):
		def mutate(case):
			self.record(case, "rest_0")["directDifference"] = 1
		self.assertRefused(mutate, {"recording-not-faithful"})

	def test_fidelity_unmeasured(self):
		def mutate(case):
			del self.record(case, "rest_0")["directDifference"]
		self.assertRefused(mutate, {"recording-fidelity-unmeasured"})

	def test_wrong_paint_root(self):
		for key, value in (("type", "Synthetic::Container"), ("size", [5, 2])):
			with self.subTest(key=key):
				def mutate(case):
					self.record(case, "drag_0")["root"][key] = value
				self.assertRefused(mutate, {"wrong-paint-root"})

	def test_coverage_short(self):
		def mutate(case):
			self.replayed(case, "drag_0")["coveragePixels"] = DEVICE_PIXELS - 1
		self.assertRefused(mutate, {"not-whole-owner"})

	def test_partial_region(self):
		def mutate(case):
			self.record(case, "drag_0")["root"]["region"] = [0, 0, 1, 2]
		self.assertRefused(mutate, {"not-whole-owner"})

	def test_missing_timed_stream(self):
		def mutate(case):
			for rows in (case["manifest"]["streams"], case["replay"]["rows"]):
				rows[:] = [row for row in rows if row["role"] != "timed"]
		self.assertRefused(mutate, {
			"missing-timed-stream",
			"recording-fidelity-unmeasured",
			"absent-pair-invalid",
		})

	def test_empty_control(self):
		def missing(case):
			case["manifest"]["streams"].remove(self.record(case, "empty_0"))
			case["replay"]["rows"].remove(self.replayed(case, "empty_0"))

		def changed(case):
			self.replayed(case, "empty_0")["changedPixels"] = 1
		self.assertRefused(missing, {"missing-empty-control"})
		self.assertRefused(changed, {"empty-changes-pixels"})

	def test_absent_missing(self):
		def mutate(case):
			case["manifest"]["streams"].remove(self.record(case, "absent_0"))
			case["replay"]["rows"].remove(self.replayed(case, "absent_0"))
		self.assertRefused(mutate, {"missing-absent-control"})

	def test_absent_self_paired(self):
		def mutate(case):
			self.record(case, "absent_0")["pairedWith"] = "absent_0"
			self.replayed(case, "absent_0")["pair"] = {
				"with": "absent_0",
				"differingPixels": 0,
				"bounds": [0, 0, 0, 0],
			}
		self.assertRefused(mutate, {
			"absent-pair-invalid",
			"absent-removes-nothing",
		})

	def test_absent_pair_error(self):
		def failed_pair(case):
			self.replayed(case, "absent_0")["pair"] = {
				"with": "rest_0",
				"error": "missing-pair",
			}

		def other_pair(case):
			self.replayed(case, "absent_0")["pair"]["with"] = "drag_0"
		self.assertRefused(failed_pair, {"absent-pair-invalid"})
		self.assertRefused(other_pair, {"absent-pair-invalid"})

	def test_absent_outside_field(self):
		def moved(case):
			self.record(case, "absent_0")["fieldRect"] = [0, 0, 1, 1]

		def undeclared(case):
			del self.record(case, "absent_0")["fieldRect"]
		self.assertRefused(moved, {"absent-outside-field"})
		self.assertRefused(undeclared, {"absent-outside-field"})

	def test_narrow_control(self):
		def missing(case):
			case["manifest"]["streams"].remove(self.record(case, "narrow_0"))
			case["replay"]["rows"].remove(self.replayed(case, "narrow_0"))

		def full(case):
			self.replayed(case, "narrow_0")["coveragePixels"] = DEVICE_PIXELS
		self.assertRefused(missing, {"missing-narrow-control"})
		self.assertRefused(full, {"narrow-not-narrow"})

	def test_sample_accounting(self):
		def declared(case):
			case["options"]["samples"] = SAMPLES + 1

		def warmup(case):
			case["replay"]["warmup"] = 0

		def short(case):
			self.replayed(case, "drag_0")["timesNs"].pop()

		def untimed(case):
			self.replayed(case, "narrow_0")["timesNs"] = times(1)
		for mutate in (declared, warmup, short, untimed):
			with self.subTest(mutate=mutate.__name__):
				self.assertRefused(mutate, {"sample-accounting"})

	def test_missing_record_cost(self):
		def mutate(case):
			del self.record(case, "drag_0")["recordMs"]
		self.assertRefused(mutate, {"missing-record-cost"})

	def test_row_errors(self):
		for error, code in (
				("playback-failed", "playback-failed"),
				("missing-sidecar", "missing-sidecar"),
				("new-replayer-code", "row-error")):
			with self.subTest(error=error):
				def mutate(case):
					row = self.replayed(case, "drag_0")
					for key in list(row):
						if key not in ("name", "role", "qpic", "tables"):
							del row[key]
					row["error"] = error
				self.assertRefused(mutate, {code})

	def test_replay_of_another_manifest(self):
		def other(case):
			case["options"]["manifestSha256"] = "0" * 64

		def unnamed(case):
			del case["replay"]["manifest"]
		self.assertRefused(other, {"replay-manifest-mismatch"})
		self.assertRefused(unnamed, {"replay-manifest-mismatch"})

	def test_input_schemas(self):
		def manifest(case):
			case["manifest"]["schema"] = 2

		def replay(case):
			case["replay"]["tool"] = "other"
		self.assertRefused(manifest, {"manifest-schema"})
		self.assertRefused(replay, {"replay-schema"})

	def test_refuses_invalid_options(self):
		for key, value in (
				("hostArch", "arm64e"),
				("warmup", -1),
				("samples", 0),
				("quantiles", []),
				("envelopeQuantile", 0.99)):
			with self.subTest(key=key):
				options = dict(self.case["options"], **{key: value})
				with self.assertRaises(ValueError):
					paint_replay.assess(
						self.case["manifest"],
						self.case["replay"],
						self.case["gate"],
						options)


class CliTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix="paint replay cli ")
		self.addCleanup(temporary.cleanup)
		self.root = temporary.name
		component = write(self.root, "build/paint_replay", b"replayer")
		case = synthetic_case(component)
		manifest_bytes = json.dumps(case["manifest"], indent=1).encode("utf-8")
		case["replay"]["manifest"]["sha256"] = bytes_digest(manifest_bytes)
		self.manifest = write(self.root, "run/manifest.json", manifest_bytes)
		self.replay = write(
			self.root,
			"run/replay.json",
			json.dumps(case["replay"]).encode("utf-8"))
		self.gate = write(
			self.root,
			"run/gate.json",
			json.dumps(case["gate"]).encode("utf-8"))

	def run_cli(self, *extra, gate=True):
		argv = [
			sys.executable,
			ASSESSOR,
			"--manifest", self.manifest,
			"--replay", self.replay,
			"--host-arch", "arm64",
			"--warmup", str(WARMUP),
			"--samples", str(SAMPLES),
			"--quantile", "0.5",
			"--quantile", "0.9",
		]
		if gate:
			argv += ["--gate", self.gate]
		argv += list(extra)
		if "--envelope-quantile" not in extra:
			argv += ["--envelope-quantile", "0.9"]
		return subprocess.run(
			argv,
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			text=True,
			check=False)

	def test_positive_exits_zero_and_writes_out(self):
		out = os.path.join(self.root, "assessment.json")
		result = self.run_cli("--out", out)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		printed = json.loads(result.stdout)
		self.assertEqual(load(out), printed)
		self.assertTrue(printed["allPassed"])
		self.assertEqual(
			printed["inputs"]["manifest"]["path"],
			os.path.realpath(self.manifest))
		with open(self.manifest, "rb") as file:
			self.assertEqual(
				printed["inputs"]["manifest"]["sha256"],
				bytes_digest(file.read()))

	def test_refusal_exits_one(self):
		result = self.run_cli(gate=False)
		self.assertEqual(result.returncode, 1, result.stderr)
		self.assertEqual(json.loads(result.stdout)["reasons"], ["missing-gate"])

	def test_manifest_edited_after_replay(self):
		manifest = load(self.manifest)
		by_name(manifest["streams"], "absent_0")["fieldRect"] = [0, 0, 4, 2]
		save(self.manifest, manifest)
		result = self.run_cli()
		self.assertEqual(result.returncode, 1, result.stderr)
		self.assertEqual(
			json.loads(result.stdout)["reasons"],
			["replay-manifest-mismatch"])

	def test_usage_errors(self):
		for extra in (
				("--envelope-quantile", "0.99"),
				("--quantile", "0"),
				("--quantile", "1.5"),
				("--out", self.replay)):
			with self.subTest(extra=extra):
				result = self.run_cli(*extra)
				self.assertEqual(result.returncode, 2, result.stderr)
				self.assertEqual(result.stdout, "")

	def test_unreadable_input(self):
		os.remove(self.replay)
		result = self.run_cli()
		self.assertEqual(result.returncode, 2)
		self.assertIn("error: cannot read", result.stderr)


class DocumentationTests(unittest.TestCase):
	def test_refusal_table_matches_codes(self):
		with open(DOCUMENT, "r", encoding="utf-8") as file:
			text = file.read()
		section = text.split("\n## Refusals\n", 1)[1].split("\n## ", 1)[0]
		documented = re.findall(r"^\| `([a-z0-9-]+)` \|", section, re.MULTILINE)
		self.assertEqual(len(documented), len(set(documented)))
		self.assertEqual(set(documented), set(paint_replay.REFUSALS))


class ComponentTests(unittest.TestCase):
	"""Real Debug-Qt cocoa recordings replayed by the gated Release replayer."""

	@classmethod
	def setUpClass(cls):
		if os.environ.get(SKIP_COMPONENT_VARIABLE) == "1":
			raise unittest.SkipTest(
				f"{SKIP_COMPONENT_VARIABLE}=1: component runs skipped")
		if platform.system() != "Darwin":
			raise unittest.SkipTest("the paint replay component is macOS only")
		cls.host = platform.machine()
		if cls.host not in native_exec.ARCHITECTURES:
			raise AssertionError(f"unsupported host architecture {cls.host!r}")
		cls.cmake = shutil.which("cmake")
		if not cls.cmake:
			raise AssertionError(
				f"cmake is unavailable; set {SKIP_COMPONENT_VARIABLE}=1 only "
				"on a host that cannot build the component")
		if not os.path.isfile(FONT):
			raise AssertionError(f"the repository font {FONT} is missing")
		cls.environment = dict(os.environ)
		cls.environment.pop("QT_QPA_PLATFORM", None)
		temporary = tempfile.TemporaryDirectory(prefix="paint_replay_")
		cls.temporary = temporary
		cls.root = temporary.name
		try:
			release = cls.build("Release")
			debug = cls.build("Debug")
			cls.replayer = os.path.join(release, "paint_replay")
			cls.debug_replayer = os.path.join(debug, "paint_replay")
			cls.fixture = os.path.join(debug, "paint_replay_fixture")
			cls.gate_path = cls.gate_file(cls.replayer, "gate-release.json")
			cls.debug_gate_path = cls.gate_file(
				cls.debug_replayer,
				"gate-debug.json")
			cls.recording = cls.record("positive")
			cls.replay_path = cls.replay(cls.recording, "positive")
		except BaseException:
			temporary.cleanup()
			raise

	@classmethod
	def tearDownClass(cls):
		cls.temporary.cleanup()

	@classmethod
	def execute(cls, argv):
		try:
			return subprocess.run(
				argv,
				stdin=subprocess.DEVNULL,
				stdout=subprocess.PIPE,
				stderr=subprocess.PIPE,
				text=True,
				env=cls.environment,
				timeout=COMPONENT_TIMEOUT,
				check=False)
		except (OSError, subprocess.TimeoutExpired) as error:
			raise AssertionError(f"cannot run {argv[0]}: {error}") from error

	@classmethod
	def require(cls, result, what, code=0):
		if result.returncode != code:
			raise AssertionError(
				f"{what} exited {result.returncode}, expected {code}:\n"
				f"{' '.join(result.args)}\n{result.stdout}\n{result.stderr}")

	@classmethod
	def build(cls, config):
		directory = os.path.join(cls.root, "component-" + config.lower())
		argv = [
			cls.cmake,
			"-S", COMPONENT_SOURCE,
			"-B", directory,
			f"-DCMAKE_BUILD_TYPE={config}",
			f"-DCMAKE_OSX_ARCHITECTURES={cls.host}",
		]
		if shutil.which("ninja"):
			argv += ["-G", "Ninja"]
		cls.require(cls.execute(argv), f"configuring the {config} component")
		cls.require(
			cls.execute([cls.cmake, "--build", directory]),
			f"building the {config} component")
		return directory

	@classmethod
	def gate_file(cls, executable, name):
		verdict = native_exec.run_gate(executable, cls.host)
		if verdict["verdict"] != "PASS":
			raise AssertionError(
				f"the gate refused {executable}: {verdict['reason']}: "
				f"{verdict['message']}")
		path = os.path.join(cls.root, name)
		save(path, verdict)
		return path

	@classmethod
	def record(cls, name, *extra):
		directory = os.path.join(cls.root, "runs", name, "record")
		cls.require(
			cls.execute([
				cls.fixture,
				"--out", directory,
				"--font", FONT,
				*extra,
			]),
			f"recording {name} (a logged-in GUI session is required)")
		return directory

	@classmethod
	def replay(cls, directory, name, replayer=None, fonts=True, code=0):
		out = os.path.join(cls.root, "runs", name, "replay.json")
		argv = [
			replayer or cls.replayer,
			"--manifest", os.path.join(directory, "manifest.json"),
			"--out", out,
			"--warmup", str(WARMUP),
			"--samples", str(SAMPLES),
		]
		if fonts:
			argv += ["--font", FONT]
		cls.require(cls.execute(argv), f"replaying {name}", code)
		return out

	def assess(self, directory, replay, gate):
		out = replay[:-len(".json")] + "-assessment.json"
		argv = [
			sys.executable,
			ASSESSOR,
			"--manifest", os.path.join(directory, "manifest.json"),
			"--replay", replay,
			"--host-arch", self.host,
			"--warmup", str(WARMUP),
			"--samples", str(SAMPLES),
			"--quantile", "0.5",
			"--quantile", "0.9",
			"--envelope-quantile", "0.9",
			"--out", out,
		]
		if gate:
			argv += ["--gate", gate]
		result = self.execute(argv)
		self.assertIn(result.returncode, (0, 1), result.stderr)
		assessment = load(out)
		self.assertEqual(json.loads(result.stdout), assessment)
		self.assertEqual(result.returncode == 0, assessment["allPassed"])
		return result.returncode, assessment

	def assertRefusedWith(self, assessment, codes, exact=False):
		reasons = set(assessment["reasons"])
		self.assertFalse(assessment["allPassed"])
		if exact:
			self.assertEqual(reasons, set(codes))
		else:
			self.assertTrue(set(codes) <= reasons, reasons)

	def manifest(self, directory):
		return load(os.path.join(directory, "manifest.json"))

	def edit_manifest(self, directory, mutate):
		path = os.path.join(directory, "manifest.json")
		manifest = load(path)
		mutate(manifest)
		save(path, manifest)

	def copy_recording(self, name):
		directory = os.path.join(self.root, "runs", name, "record")
		shutil.copytree(self.recording, directory)
		return directory

	def rewrite_identity(self, directory, stream, key):
		def mutate(manifest):
			identity = by_name(manifest["streams"], stream)[key]
			with open(os.path.join(directory, identity["file"]), "rb") as file:
				data = file.read()
			identity["bytes"] = len(data)
			identity["sha256"] = bytes_digest(data)
		self.edit_manifest(directory, mutate)

	def rows(self, replay):
		return {row["name"]: row for row in load(replay)["rows"]}

	def assertRowRefused(self, directory, name, stream, error, codes):
		replay = self.replay(directory, name)
		self.assertEqual(self.rows(replay)[stream].get("error"), error)
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, codes, exact=True)

	def stream_file(self, directory, stream, suffix):
		return os.path.join(directory, stream + suffix)

	def test_positive(self):
		code, assessment = self.assess(
			self.recording,
			self.replay_path,
			self.gate_path)
		self.assertEqual(code, 0, assessment["reasons"])
		manifest = self.manifest(self.recording)
		replay = load(self.replay_path)
		rows = self.rows(self.replay_path)
		records = {row["name"]: row for row in manifest["streams"]}
		self.assertEqual(set(rows), set(records))
		family = replay["fonts"][0]["families"][0]
		for name, record in records.items():
			self.assertNotIn("error", rows[name], name)
			self.assertEqual(
				rows[name]["replaySha256"],
				record["referenceSha256"],
				name)
		timed = [name for name, row in records.items() if row["role"] == "timed"]
		self.assertTrue(timed)
		for name in timed:
			record = records[name]
			self.assertEqual(record["directDifference"], 0, name)
			self.assertEqual(record["root"]["flags"], 0, name)
			for key in ("images", "pixmaps", "brushes", "pens"):
				self.assertGreater(record["counts"][key], 0, (name, key))
			images = sidecar_images(
				os.path.join(self.recording, record["tables"]["file"]))
			self.assertEqual(len(images), record["counts"]["images"], name)
			self.assertTrue(
				padded_images(images),
				f"premise: {name} records a 64-byte-padded image whose width "
				f"is not a multiple of 16: {images}")
			self.assertEqual(
				record["fonts"],
				[{"family": family, "resolved": family}])
			self.assertEqual(rows[name]["fonts"], record["fonts"])
			self.assertEqual(
				rows[name]["coveragePixels"],
				rows[name]["surfacePixels"])
			self.assertEqual(len(rows[name]["timesNs"]), SAMPLES)
		self.assertEqual(rows["empty_0"]["changedPixels"], 0)
		self.assertLess(
			rows["narrow_0"]["coveragePixels"],
			rows["narrow_0"]["surfacePixels"])
		pair = rows["absent_0"]["pair"]
		field = paint_replay.device_rect(
			records["absent_0"]["fieldRect"],
			manifest["surface"]["dpr"])
		self.assertGreater(pair["differingPixels"], 0)
		self.assertTrue(paint_replay.rect_inside(pair["bounds"], field))
		self.assertIs(manifest["qt"]["debug"], True)
		self.assertIs(replay["qt"]["debug"], False)
		self.assertEqual(manifest["qt"]["platform"], "cocoa")
		self.assertEqual(replay["qt"]["platform"], "cocoa")
		self.assertEqual(
			manifest["qt"]["colorScheme"],
			replay["qt"]["colorScheme"])
		self.assertEqual((replay["warmup"], replay["samples"]), (WARMUP, SAMPLES))
		self.assertEqual(
			[stream["name"] for stream in assessment["streams"]],
			timed)
		for stream in assessment["streams"]:
			self.assertEqual(stream["replay"]["n"], SAMPLES)
			self.assertEqual(
				[item["rank"] for item in stream["replay"]["quantiles"]],
				[13, 23])
			self.assertAlmostEqual(
				stream["envelopeMs"],
				stream["recordMs"] + stream["envelopeQuantile"]["valueMs"],
				places=6)

	def test_motion_windows_read_fixture_rows(self):
		budget = {
			"budgetMs": WINDOW_BUDGET_MS,
			"replayQuantile": WINDOW_QUANTILE,
		}
		spec = {
			"schema": 1,
			"actions": [
				{
					"name": "press",
					"before": FIXTURE_PRESS_AT,
					"after": FIXTURE_PRESS_AT,
				},
				{
					"name": "release",
					"before": FIXTURE_RELEASE_AT,
					"after": FIXTURE_RELEASE_AT,
					"handlerAt": FIXTURE_RELEASE_AT,
				},
			],
			"windows": [
				dict({"name": "rest", "kind": "rest"}, **budget),
				dict({
					"name": "drag",
					"kind": "pressed",
					"origin": "press",
					"phases": 3,
				}, **budget),
				dict({
					"name": "glide",
					"kind": "release",
					"origin": "release",
					"phases": 3,
				}, **budget),
			],
		}
		directory = os.path.join(self.root, "runs", "windows")
		os.makedirs(directory, exist_ok=True)
		spec_path = os.path.join(directory, "window-spec.json")
		out = os.path.join(directory, "window-assessment.json")
		save(spec_path, spec)
		result = self.execute([
			sys.executable,
			WINDOW_ASSESSOR,
			"--spec", spec_path,
			"--manifest", os.path.join(self.recording, "manifest.json"),
			"--replay", self.replay_path,
			"--out", out,
		])
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		assessment = load(out)
		self.assertEqual(json.loads(result.stdout), assessment)
		self.assertTrue(assessment["allPassed"], assessment["reasons"])
		manifest = self.manifest(self.recording)
		rows = self.rows(self.replay_path)
		for row in manifest["streams"]:
			self.assertEqual(
				rows[row["name"]]["qpic"]["sha256"],
				row["qpic"]["sha256"])
		accounting = next(
			check for check in assessment["checks"]
			if check["check"] == "accounting")
		self.assertTrue(accounting["pass"])
		self.assertEqual(
			accounting["facts"]["manifestStreams"],
			len(manifest["streams"]))
		records = {
			row["name"]: row for row in manifest["streams"] if "window" in row
		}
		windows = {item["name"]: item for item in assessment["windows"]}
		self.assertEqual(set(windows), {"rest", "drag", "glide"})
		for name, item in windows.items():
			self.assertTrue(item["pass"], (name, item["reasons"]))
			self.assertEqual(item["records"], [
				record for record, row in records.items()
				if row["window"] == name
			])
			for entry in item["budget"]["records"]:
				self.assertEqual(
					(entry["replay"]["rank"], entry["replay"]["n"]),
					(23, SAMPLES))
		for name in ("drag", "glide"):
			self.assertTrue(all(windows[name]["phases"]["counts"]), name)
		glide = windows["glide"]
		self.assertEqual(glide["states"]["stopped"], 1)
		self.assertFalse(records[glide["records"][-1]]["moving"])
		self.assertEqual(glide["firstObservationDelay"], {
			"claim": "exact",
			"origin": "release",
			"ms": records[glide["records"][0]]["at"] - FIXTURE_RELEASE_AT,
		})
		self.assertEqual(assessment["unassessedRecords"], [])

	def test_debug_replayer(self):
		replay = self.replay(
			self.recording,
			"debug-replayer",
			replayer=self.debug_replayer)
		self.assertIs(load(replay)["qt"]["debug"], True)
		code, assessment = self.assess(
			self.recording,
			replay,
			self.debug_gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"unverified-qt-build"}, exact=True)

	def test_replay_without_font(self):
		replay = self.replay(self.recording, "no-font", fonts=False)
		recorded = {
			font["resolved"]
			for row in self.manifest(self.recording)["streams"]
			for font in row["fonts"]
		}
		replayed = {
			font["resolved"]
			for row in load(replay)["rows"]
			for font in row.get("fonts", [])
		}
		if replayed == recorded:
			self.fail(
				"premise: without --font the replay resolves the recorded "
				f"families {sorted(recorded)}, so the font control is "
				"undecidable on this host")
		code, assessment = self.assess(self.recording, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {
			"font-not-retained",
			"replay-differs-from-recording",
		})

	def test_deleted_sidecar(self):
		directory = self.copy_recording("deleted-sidecar")
		os.remove(self.stream_file(directory, "drag_1", ".tables"))
		self.assertRowRefused(
			directory,
			"deleted-sidecar",
			"drag_1",
			"missing-sidecar",
			{"missing-sidecar"})

	def test_empty_sidecar(self):
		directory = self.copy_recording("empty-sidecar")
		open(self.stream_file(directory, "drag_1", ".tables"), "wb").close()
		self.rewrite_identity(directory, "drag_1", "tables")
		self.assertRowRefused(
			directory,
			"empty-sidecar",
			"drag_1",
			"empty-sidecar",
			{"empty-sidecar"})

	def test_swapped_sidecar(self):
		directory = self.copy_recording("swapped-sidecar")
		shutil.copyfile(
			self.stream_file(directory, "empty_0", ".tables"),
			self.stream_file(directory, "drag_1", ".tables"))
		self.rewrite_identity(directory, "drag_1", "tables")
		self.assertRowRefused(
			directory,
			"swapped-sidecar",
			"drag_1",
			"sidecar-count-mismatch",
			{"sidecar-count-mismatch"})

	def test_swapped_sidecar_with_counts(self):
		directory = self.copy_recording("swapped-sidecar-counts")
		shutil.copyfile(
			self.stream_file(directory, "empty_0", ".tables"),
			self.stream_file(directory, "drag_1", ".tables"))
		self.rewrite_identity(directory, "drag_1", "tables")

		def counts(manifest):
			streams = manifest["streams"]
			by_name(streams, "drag_1")["counts"] = dict(
				by_name(streams, "empty_0")["counts"])
		self.edit_manifest(directory, counts)
		replay = self.replay(directory, "swapped-sidecar-counts")
		row = self.rows(replay)["drag_1"]
		self.assertNotIn("error", row)
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"replay-differs-from-recording"})

	def test_empty_stream(self):
		directory = self.copy_recording("empty-stream")
		open(self.stream_file(directory, "drag_1", ".qpic"), "wb").close()
		self.rewrite_identity(directory, "drag_1", "qpic")
		self.assertRowRefused(
			directory,
			"empty-stream",
			"drag_1",
			"empty-stream-file",
			{"empty-stream-file"})

	def test_corrupted_stream(self):
		directory = self.copy_recording("corrupted-stream")
		path = self.stream_file(directory, "drag_1", ".qpic")
		with open(path, "rb") as file:
			data = bytearray(file.read())
		self.assertGreater(len(data), 64)
		data[len(data) // 2] ^= 0x40
		with open(path, "wb") as file:
			file.write(data)
		self.rewrite_identity(directory, "drag_1", "qpic")
		self.assertRowRefused(
			directory,
			"corrupted-stream",
			"drag_1",
			"stream-load-failed",
			{"stream-load-failed"})

	def test_header_only_stream(self):
		directory = self.copy_recording("header-only-stream")
		path = self.stream_file(directory, "drag_1", ".qpic")
		with open(path, "rb") as file:
			data = bytearray(file.read()[:32])
		data[4:6] = struct.pack(">H", x25(bytes(data[6:])))
		with open(path, "wb") as file:
			file.write(data)
		self.rewrite_identity(directory, "drag_1", "qpic")
		self.assertRowRefused(
			directory,
			"header-only-stream",
			"drag_1",
			"playback-failed",
			{"playback-failed"})

	def test_qt_version_changed(self):
		directory = self.copy_recording("qt-version")

		def mutate(manifest):
			manifest["qt"]["version"] += "-altered"
		self.edit_manifest(directory, mutate)
		replay = self.replay(directory, "qt-version", code=1)
		self.assertEqual(load(replay)["error"], "qt-version-mismatch")
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {
			"replay-fatal",
			"qt-version-mismatch",
		})

	def test_platform_changed(self):
		directory = self.copy_recording("platform")
		scheme = self.manifest(directory)["qt"]["colorScheme"]
		self.assertNotEqual(
			scheme,
			"unknown",
			"premise: cocoa reports a color scheme, which offscreen cannot")

		def mutate(manifest):
			manifest["qt"]["platform"] = "offscreen"
		self.edit_manifest(directory, mutate)
		replay = self.replay(directory, "platform", code=1)
		result = load(replay)
		self.assertEqual(result["error"], "platform-mismatch")
		self.assertEqual(result["qt"]["platform"], "offscreen")
		self.assertEqual(result["qt"]["colorScheme"], "unknown")
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"replay-fatal"})

	def test_container_root(self):
		directory = self.record("container", "--root", "container")
		replay = self.replay(directory, "container")
		rows = self.rows(replay)
		for row in self.manifest(directory)["streams"]:
			if row["role"] == "timed":
				self.assertLess(
					rows[row["name"]]["coveragePixels"],
					rows[row["name"]]["surfacePixels"],
					"premise: the container does not cover the surface")
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {
			"wrong-paint-root",
			"not-whole-owner",
		})

	def test_translucent_owner(self):
		directory = self.record("translucent", "--owner", "translucent")
		manifest = self.manifest(directory)
		replay = self.replay(directory, "translucent")
		rows = self.rows(replay)
		timed = [row for row in manifest["streams"] if row["role"] == "timed"]
		self.assertTrue(timed)
		for row in timed:
			self.assertEqual(
				row["root"]["type"],
				manifest["surface"]["rootType"])
			self.assertEqual(
				row["root"]["region"],
				[0, 0] + manifest["surface"]["size"])
			self.assertEqual(
				row["root"]["flags"],
				0,
				"premise: recorded with RecordPaint's default flags")
			self.assertLess(
				rows[row["name"]]["coveragePixels"],
				rows[row["name"]]["surfacePixels"],
				"premise: the translucent owner does not cover the surface")
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"not-whole-owner"}, exact=True)

	def test_band_timed_region(self):
		directory = self.record("band", "--timed-region", "band")
		manifest = self.manifest(directory)
		for row in manifest["streams"]:
			if row["role"] == "timed":
				self.assertEqual(
					row["root"]["type"],
					manifest["surface"]["rootType"])
				self.assertNotEqual(
					row["root"]["region"],
					[0, 0] + manifest["surface"]["size"])
		replay = self.replay(directory, "band")
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"not-whole-owner"})
		self.assertNotIn("wrong-paint-root", assessment["reasons"])

	def test_self_paired_absent(self):
		directory = self.copy_recording("self-paired")

		def mutate(manifest):
			by_name(manifest["streams"], "absent_0")["pairedWith"] = "absent_0"
		self.edit_manifest(directory, mutate)
		replay = self.replay(directory, "self-paired")
		self.assertEqual(self.rows(replay)["absent_0"]["pair"]["differingPixels"], 0)
		code, assessment = self.assess(directory, replay, self.gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"absent-removes-nothing"})

	def test_gates(self):
		code, assessment = self.assess(self.recording, self.replay_path, None)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"missing-gate"}, exact=True)
		code, assessment = self.assess(
			self.recording,
			self.replay_path,
			self.debug_gate_path)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"gate-executable-mismatch"})
		late = self.gate_file(self.replayer, "gate-late.json")
		self.assertGreater(
			load(late)["finishedAt"],
			load(self.replay_path)["startedAt"])
		code, assessment = self.assess(self.recording, self.replay_path, late)
		self.assertEqual(code, 1)
		self.assertRefusedWith(assessment, {"gate-after-replay"}, exact=True)


def sidecar_images(path):
	with open(path, "rb") as file:
		data = file.read()
	if data[:4] != SIDECAR_MAGIC:
		raise AssertionError(f"{path} is not a paint sidecar")
	offset = 12
	(count,) = struct.unpack_from(">i", data, offset)
	offset += 4
	images = []
	for _ in range(count):
		image_format, width, height, bytes_per_line = struct.unpack_from(
			">4i",
			data,
			offset)
		offset += 16 + 8
		(colors,) = struct.unpack_from(">i", data, offset)
		offset += 4 + 4 * colors
		(profile,) = struct.unpack_from(">I", data, offset)
		offset += 4 + (0 if profile == NULL_BYTE_ARRAY else profile)
		offset += bytes_per_line * height
		images.append({
			"format": image_format,
			"width": width,
			"height": height,
			"bytesPerLine": bytes_per_line,
		})
	return images


def padded_images(images):
	return [
		image for image in images
		if image["format"] == FORMAT_ARGB32_PREMULTIPLIED
		and image["width"] % 16
		and image["bytesPerLine"] > image["width"] * 4
		and image["bytesPerLine"] % FFMPEG_ROW_ALIGNMENT == 0
	]


def x25(data):
	crc = 0xFFFF
	for byte in data:
		crc ^= byte
		for _ in range(8):
			crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
	return crc ^ 0xFFFF


if __name__ == "__main__":
	unittest.main()
