#!/usr/bin/env python3

import contextlib
import copy
import hashlib
import io
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import unittest

import motion_window


HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "motion_window.py")
DOCUMENT = os.path.join(HERE, "motion-window.md")
PRESS_AT = 800
RELEASE_AT = 1000
MOVE_HANDLER = (900, 980)
MOVE_CALLER = (900, 979)
V0_PX_PER_MS = 0.472
TAU_MS = 200.0
STOP_PX_PER_MS = 0.05 / 1000
FIRST_DELAY_MS = 117
GAPS_MS = (55, 164, 55, 109)
DISTANCE_PX = V0_PX_PER_MS * (MOVE_HANDLER[1] - MOVE_HANDLER[0])
START = (40.0, 96.0)
REST = ((600, 4.0), (655, 4.0), (719, 4.0))
DRAG = ((902, 4.0), (935, 16.0), (968, 28.0), (985, 40.0))
SAMPLES = 25
BUDGET_MS = 2.0
REPLAY_QUANTILE = 0.9
TOLERANCE_PX = 0.5
CORRECTION = (
	"the >= 20 records gate was the instrument's own invention; the window "
	"is decided by declared state and phase coverage")
CLAIM = (
	"the declared glide window starts in motion, covers its three phases and "
	"ends with a stopped record; nothing about frames between records")
ORIGINAL = {
	"allPassed": False,
	"checks": [
		{"check": "rest window recorded", "pass": True, "facts": {"records": 3}},
		{
			"check": "glide window has >= 20 records",
			"pass": False,
			"facts": {"records": 19, "required": 20},
		},
	],
}
LAWS_SOURCE = """import math

DISTANCE_PX = {distance!r}
STOP_PX_PER_MS = {stop!r}
TAU_MS = {tau!r}
START = {start!r}


def glide(origins, at):
	velocity = DISTANCE_PX / (origins["move_2"] - origins["move_1"])
	stop = TAU_MS * math.log(velocity / STOP_PX_PER_MS)
	elapsed = min(at - origins["release"], stop)
	travel = velocity * TAU_MS * (1 - math.exp(-elapsed / TAU_MS))
	return [START[0] + travel, START[1]]


LAWS = {{"glide": glide}}
""".format(
	distance=DISTANCE_PX,
	stop=STOP_PX_PER_MS,
	tau=TAU_MS,
	start=START)


def digest(text):
	return hashlib.sha256(text.encode("utf-8")).hexdigest()


def glide_law(tau=TAU_MS):
	def law(origins, at):
		velocity = DISTANCE_PX / (origins["move_2"] - origins["move_1"])
		stop = tau * math.log(velocity / STOP_PX_PER_MS)
		elapsed = min(at - origins["release"], stop)
		travel = velocity * tau * (1 - math.exp(-elapsed / tau))
		return [START[0] + travel, START[1]]

	return law


def handler_origins():
	return {
		"release": RELEASE_AT,
		"move_1": MOVE_HANDLER[0],
		"move_2": MOVE_HANDLER[1],
	}


def record(window, index, at, pressed, moving, position):
	return {
		"name": f"{window}_{index}",
		"window": window,
		"at": at,
		"pressed": pressed,
		"moving": moving,
		"position": list(position),
		"recordMs": round(0.2 + 0.01 * (index % 5), 6),
	}


def natural_glide():
	law = glide_law()
	origins = handler_origins()
	velocity = DISTANCE_PX / (MOVE_HANDLER[1] - MOVE_HANDLER[0])
	records = []
	elapsed = FIRST_DELAY_MS
	while True:
		moving = velocity * math.exp(-elapsed / TAU_MS) >= STOP_PX_PER_MS
		at = RELEASE_AT + elapsed
		records.append(record(
			"glide",
			len(records),
			at,
			False,
			moving,
			law(origins, at)))
		if not moving:
			return records
		elapsed += GAPS_MS[(len(records) - 1) % len(GAPS_MS)]


def times(seed):
	return [
		100000 + ((index * 7 + seed) % SAMPLES) * 1000
		for index in range(SAMPLES)
	]


def all_records():
	rest = [
		record("rest", index, at, False, False, (x, START[1]))
		for index, (at, x) in enumerate(REST)
	]
	drag = [
		record("drag", index, at, True, True, (x, START[1]))
		for index, (at, x) in enumerate(DRAG)
	]
	return rest + drag + natural_glide()


def replay_for(records):
	return {
		"rows": [
			{"name": row["name"], "timesNs": times(index)}
			for index, row in enumerate(records)
		],
	}


def make_spec(law=False):
	budget = {"budgetMs": BUDGET_MS, "replayQuantile": REPLAY_QUANTILE}
	glide = dict({
		"name": "glide",
		"kind": "release",
		"origin": "release",
		"phases": 3,
	}, **budget)
	if law:
		glide["law"] = {
			"name": "glide",
			"origins": ["release", "move_1", "move_2"],
			"tolerance": TOLERANCE_PX,
		}
	return {
		"schema": 1,
		"actions": [
			{
				"name": "press",
				"before": PRESS_AT,
				"after": PRESS_AT + 1,
				"handlerAt": PRESS_AT,
			},
			{
				"name": "move_1",
				"before": MOVE_CALLER[0],
				"handlerAt": MOVE_HANDLER[0],
			},
			{
				"name": "move_2",
				"before": MOVE_CALLER[1],
				"handlerAt": MOVE_HANDLER[1],
			},
			{
				"name": "release",
				"before": RELEASE_AT,
				"after": RELEASE_AT + 1,
				"handlerAt": RELEASE_AT,
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
			glide,
		],
	}


def make_case(law=False):
	records = all_records()
	return {
		"spec": make_spec(law),
		"manifest": {"streams": records},
		"replay": replay_for(records),
		"laws": {"glide": glide_law()} if law else None,
	}


def by_name(rows, name):
	return next(row for row in rows if row.get("name") == name)


def window(result, name):
	return by_name(result["windows"], name)


def action(case, name):
	return by_name(case["spec"]["actions"], name)


def failed(result):
	return {check["reason"] for check in result["checks"] if not check["pass"]}


def drop_window(case, name):
	streams = case["manifest"]["streams"]
	names = {row["name"] for row in streams if row.get("window") == name}
	streams[:] = [row for row in streams if row["name"] not in names]
	rows = case["replay"]["rows"]
	rows[:] = [row for row in rows if row["name"] not in names]


def keep_only(case, names):
	streams = case["manifest"]["streams"]
	removed = {
		row["name"] for row in streams
		if row.get("window") == "glide" and row["name"] not in names
	}
	streams[:] = [row for row in streams if row["name"] not in removed]
	rows = case["replay"]["rows"]
	rows[:] = [row for row in rows if row["name"] not in removed]


def glide_records(case):
	return [
		row for row in case["manifest"]["streams"]
		if row.get("window") == "glide"
	]


class Assessing(unittest.TestCase):
	law = False

	def setUp(self):
		self.case = make_case(self.law)

	def assess(self, mutate=None):
		case = copy.deepcopy(self.case)
		if mutate:
			mutate(case)
		return motion_window.assess(
			case["spec"],
			case["manifest"],
			case["replay"],
			case["laws"])

	def assertPasses(self, mutate=None):
		result = self.assess(mutate)
		self.assertTrue(result["allPassed"], result["reasons"])
		self.assertTrue(all(check["pass"] for check in result["checks"]))
		return result

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


class CoverageTests(Assessing):
	def test_natural_glide_premise(self):
		records = natural_glide()
		self.assertEqual(len(records), 19)
		self.assertEqual(sum(1 for row in records if row["moving"]), 18)
		self.assertFalse(records[-1]["moving"])
		self.assertTrue(all(row["moving"] for row in records[:-1]))
		self.assertEqual(records[-1]["at"] - RELEASE_AT, 1868)

	def test_natural_window_passes(self):
		result = self.assertPasses()
		glide = window(result, "glide")
		self.assertTrue(glide["pass"])
		self.assertEqual(glide["states"], {
			"records": 19,
			"pressed": 0,
			"moving": 18,
			"stopped": 1,
		})
		expected = [GAPS_MS[index % len(GAPS_MS)] for index in range(18)]
		self.assertEqual(glide["gapsMs"], expected)
		self.assertEqual(min(glide["gapsMs"]), 55)
		self.assertEqual(glide["maxGapMs"], 164)
		self.assertEqual(glide["spanMs"], 1868 - FIRST_DELAY_MS)
		delay = glide["firstObservationDelay"]
		self.assertEqual(delay["claim"], "exact")
		self.assertEqual(delay["origin"], "release")
		self.assertEqual(delay["ms"], FIRST_DELAY_MS)
		self.assertEqual(glide["terminalDelay"]["ms"], 1868)
		phases = glide["phases"]
		self.assertEqual(phases["counts"], [2, 1, 15])
		self.assertEqual(phases["records"][1], ["glide_2"])
		self.assertEqual(phases["outside"], [])
		self.assertEqual((phases["from"], phases["till"]), (
			"glide_0",
			"glide_18",
		))
		budget = glide["budget"]
		self.assertEqual(len(budget["records"]), 19)
		for entry in budget["records"]:
			self.assertEqual(entry["replay"]["rank"], 23)
			self.assertEqual(entry["replay"]["n"], SAMPLES)
			self.assertAlmostEqual(
				entry["envelopeMs"],
				entry["recordMs"] + entry["replay"]["valueMs"],
				places=6)
		self.assertLessEqual(budget["envelopeMaxMs"], BUDGET_MS)
		self.assertIn("budget", glide["scope"])
		for key in ("decision", "statistics", "origins", "notClaimed"):
			self.assertIn(key, result["scope"])
		self.assertTrue(result["limits"])
		self.assertEqual(result["diagnoses"], [])
		self.assertEqual(result["unassessedRecords"], [])

	def test_conservative_bound_without_handler_time(self):
		def mutate(case):
			del action(case, "release")["handlerAt"]
		glide = window(self.assertPasses(mutate), "glide")
		self.assertEqual(glide["firstObservationDelay"], {
			"claim": "conservative-bound",
			"origin": "release",
			"bounds": [116, 117],
		})
		self.assertEqual(glide["terminalDelay"]["bounds"], [1867, 1868])

	def test_exact_delay_uses_handler_time(self):
		def mutate(case):
			action(case, "release")["handlerAt"] = RELEASE_AT + 0.5
		glide = window(self.assertPasses(mutate), "glide")
		self.assertEqual(glide["firstObservationDelay"], {
			"claim": "exact",
			"origin": "release",
			"ms": FIRST_DELAY_MS - 0.5,
		})

	def test_conservative_bound_without_after(self):
		def mutate(case):
			release = action(case, "release")
			del release["handlerAt"]
			del release["after"]
		glide = window(self.assertPasses(mutate), "glide")
		self.assertEqual(glide["firstObservationDelay"]["bounds"], [None, 117])

	def test_rest_and_pressed_windows_pass(self):
		result = self.assertPasses()
		rest = window(result, "rest")
		self.assertTrue(rest["pass"])
		self.assertEqual(rest["states"]["stopped"], 3)
		self.assertIsNone(rest["phases"])
		drag = window(result, "drag")
		self.assertTrue(drag["pass"])
		self.assertEqual(drag["states"]["pressed"], 4)
		self.assertEqual(drag["phases"]["counts"], [1, 1, 2])
		self.assertEqual(drag["firstObservationDelay"]["ms"], 102)

	def test_few_records_covering_every_phase_pass(self):
		def mutate(case):
			keep_only(case, {"glide_0", "glide_2", "glide_3", "glide_18"})
		glide = window(self.assertPasses(mutate), "glide")
		self.assertEqual(glide["states"]["records"], 4)
		self.assertEqual(glide["phases"]["counts"], [1, 1, 1])

	def test_records_of_undeclared_windows_are_listed(self):
		def mutate(case):
			extra = record("hover", 0, 50, False, False, (1, 1))
			case["manifest"]["streams"].append(extra)
			case["replay"]["rows"].append({"name": "hover_0", "timesNs": [1]})
		result = self.assertPasses(mutate)
		self.assertEqual(result["unassessedRecords"], ["hover_0"])

	def test_non_window_rows_are_accounted_only(self):
		def mutate(case):
			case["manifest"]["streams"].append({"name": "empty_0"})
		self.assertRefused(mutate, {"missing-replay"})

		def replayed(case):
			case["manifest"]["streams"].append({"name": "empty_0"})
			case["replay"]["rows"].append({"name": "empty_0"})
		self.assertPasses(replayed)


class RejectionTests(Assessing):
	def test_empty_window(self):
		def mutate(case):
			drop_window(case, "glide")
		result = self.assertRefused(mutate, {"empty-window"})
		self.assertFalse(window(result, "glide")["pass"])
		self.assertTrue(window(result, "rest")["pass"])

	def test_missing_stopped_terminal(self):
		def mutate(case):
			keep_only(case, {f"glide_{index}" for index in range(18)})
		self.assertRefused(mutate, {"missing-stopped-terminal"})

	def test_pressed_substitution(self):
		def mutate(case):
			for row in glide_records(case):
				row["pressed"] = True
		self.assertRefused(mutate, {"pressed-in-release-window"})

	def test_static_records(self):
		def mutate(case):
			for row in glide_records(case):
				row["moving"] = False
		self.assertRefused(mutate, {
			"not-started-in-motion",
			"static-substitution",
		})

	def test_frozen_positions(self):
		def mutate(case):
			for row in glide_records(case):
				row["position"] = list(START)
		result = self.assertRefused(mutate, {"static-substitution"})
		self.assertEqual(
			{
				check["check"] for check in result["checks"]
				if not check["pass"]
			},
			{"states", "phases"})

	def test_stopped_before_terminal(self):
		def mutate(case):
			by_name(case["manifest"]["streams"], "glide_9")["moving"] = False
		self.assertRefused(mutate, {"static-substitution"})

	def test_unordered_records(self):
		def mutate(case):
			first = by_name(case["manifest"]["streams"], "glide_3")
			second = by_name(case["manifest"]["streams"], "glide_4")
			first["at"], second["at"] = second["at"], first["at"]
		self.assertRefused(mutate, {"unordered-records"})

	def test_renamed_replay(self):
		def mutate(case):
			by_name(case["replay"]["rows"], "glide_5")["name"] = "glide_5x"
		result = self.assertRefused(mutate, {"missing-replay", "extra-replay"})
		budget = next(
			check for check in result["checks"]
			if check.get("window") == "glide" and check["check"] == "budget")
		self.assertEqual(budget["reason"], "missing-replay")

	def test_mismatched_identity(self):
		def digests(case):
			for row in case["manifest"]["streams"] + case["replay"]["rows"]:
				row["qpic"] = {"sha256": digest("qpic " + row["name"])}

		def mutate(case):
			digests(case)
			rows = case["replay"]["rows"]
			by_name(rows, "glide_7")["qpic"]["sha256"] = digest("other")
		self.assertPasses(digests)
		result = self.assertRefused(mutate, {"mismatched-identity"})
		accounting = next(
			check for check in result["checks"]
			if check["check"] == "accounting")
		self.assertEqual(accounting["facts"]["details"][0]["stream"], "glide_7")

	def test_duplicate_replay(self):
		def mutate(case):
			rows = case["replay"]["rows"]
			rows.append(copy.deepcopy(by_name(rows, "glide_5")))
		self.assertRefused(mutate, {"duplicate-replay"})

	def test_duplicate_record(self):
		def mutate(case):
			streams = case["manifest"]["streams"]
			streams.insert(1, copy.deepcopy(by_name(streams, "rest_0")))
		self.assertRefused(mutate, {"duplicate-record", "unordered-records"})

	def test_over_budget(self):
		def mutate(case):
			by_name(case["manifest"]["streams"], "glide_11")["recordMs"] = 5.0
		result = self.assertRefused(mutate, {"over-budget"})
		check = next(
			check for check in result["checks"]
			if check["reason"] == "over-budget")
		detail = check["facts"]["details"][0]
		self.assertEqual(detail["stream"], "glide_11")
		self.assertEqual(detail["budgetMs"], BUDGET_MS)
		self.assertEqual((detail["rank"], detail["n"]), (23, SAMPLES))

	def test_missing_cost(self):
		def record_cost(case):
			del by_name(case["manifest"]["streams"], "drag_2")["recordMs"]

		def replay_cost(case):
			row = by_name(case["replay"]["rows"], "drag_2")
			del row["timesNs"]
			row["error"] = "playback-failed"
		self.assertRefused(record_cost, {"missing-cost"})
		self.assertRefused(replay_cost, {"missing-cost"})

	def test_motion_in_rest_window(self):
		def moving(case):
			by_name(case["manifest"]["streams"], "rest_1")["moving"] = True

		def pressed(case):
			by_name(case["manifest"]["streams"], "rest_1")["pressed"] = True

		def moved(case):
			by_name(case["manifest"]["streams"], "rest_2")["position"][0] += 1
		for mutate in (moving, pressed, moved):
			with self.subTest(mutate=mutate.__name__):
				self.assertRefused(mutate, {"motion-in-rest-window"})

	def test_pressed_window_rules(self):
		def released(case):
			by_name(case["manifest"]["streams"], "drag_2")["pressed"] = False

		def late_start(case):
			by_name(case["manifest"]["streams"], "drag_0")["moving"] = False
		self.assertRefused(released, {"released-in-pressed-window"})
		result = self.assertRefused(late_start, {
			"not-started-in-motion",
			"static-substitution",
			"phase-uncovered",
		})
		self.assertEqual(window(result, "drag")["phases"]["counts"], [0, 1, 2])

	def test_invalid_record(self):
		def missing(case):
			del by_name(case["manifest"]["streams"], "glide_4")["moving"]

		def dimension(case):
			by_name(case["manifest"]["streams"], "glide_4")["position"] = [1]
		self.assertRefused(missing, {"invalid-record"})
		self.assertRefused(dimension, {"invalid-record"})

	def test_clustered_count_cannot_cover_phases(self):
		def mutate(case):
			drop_window(case, "glide")
			clustered = [
				record(
					"glide",
					index,
					RELEASE_AT + FIRST_DELAY_MS + 10 * index,
					False,
					True,
					(START[0] + 0.1 * index, START[1]))
				for index in range(30)
			]
			clustered.append(record(
				"glide",
				30,
				RELEASE_AT + FIRST_DELAY_MS + 700,
				False,
				False,
				(START[0] + 60, START[1])))
			case["manifest"]["streams"] += clustered
			case["replay"]["rows"] += replay_for(clustered)["rows"]
		result = self.assertRefused(mutate, {"phase-uncovered"})
		glide = window(result, "glide")
		self.assertEqual(glide["states"]["records"], 31)
		self.assertGreater(glide["states"]["records"], len(natural_glide()))
		self.assertEqual(glide["phases"]["counts"], [30, 0, 0])
		check = next(
			check for check in result["checks"]
			if check["reason"] == "phase-uncovered")
		self.assertEqual(check["facts"]["details"][0]["phases"], [2, 3])

	def test_records_outside_the_segment_cover_no_phase(self):
		def mutate(case):
			streams = case["manifest"]["streams"]
			for name, x in (("drag_1", -20.0), ("drag_2", 28.0)):
				by_name(streams, name)["position"][0] = x
		result = self.assertRefused(mutate, {"phase-uncovered"})
		phases = window(result, "drag")["phases"]
		self.assertEqual(phases["outside"], ["drag_1"])
		self.assertEqual(phases["counts"], [1, 0, 2])
		self.assertEqual(phases["records"][2], ["drag_2", "drag_3"])

	def test_returning_motion_has_no_progress(self):
		def mutate(case):
			last = by_name(case["manifest"]["streams"], "drag_3")
			last["position"] = list(by_name(
				case["manifest"]["streams"],
				"drag_0")["position"])
		self.assertRefused(mutate, {"phase-uncovered"})


class InvalidSpecTests(Assessing):
	def assertInvalid(self, mutate):
		result = self.assertRefused(mutate, {"invalid-spec"})
		self.assertEqual(result["windows"], [])
		self.assertEqual(result["checks"][0]["check"], "spec")
		return result

	def windowed(self, name, **fields):
		def mutate(case):
			target = by_name(case["spec"]["windows"], name)
			for key, value in fields.items():
				if value is None:
					target.pop(key, None)
				else:
					target[key] = value
		return mutate

	def test_window_rules(self):
		cases = (
			("unknown kind", self.windowed("glide", kind="glide")),
			("no phases", self.windowed("glide", phases=None)),
			("zero phases", self.windowed("drag", phases=0)),
			("boolean phases", self.windowed("drag", phases=True)),
			("phases on rest", self.windowed("rest", phases=3)),
			("budget alone", self.windowed("glide", replayQuantile=None)),
			("budget zero", self.windowed("glide", budgetMs=0)),
			("quantile zero", self.windowed("glide", replayQuantile=0)),
			("quantile above one", self.windowed("glide", replayQuantile=1.5)),
			("quantile alone", self.windowed("rest", budgetMs=None)),
			("unknown origin", self.windowed("glide", origin="lift")),
			("misspelt key", self.windowed("rest", budgetMS=1)),
		)
		for label, mutate in cases:
			with self.subTest(label):
				self.assertInvalid(mutate)

	def test_action_rules(self):
		def reversed_bracket(case):
			release = action(case, "release")
			release["before"], release["after"] = 1001, 1000

		def duplicate(case):
			case["spec"]["actions"].append(copy.deepcopy(action(case, "press")))

		def textual(case):
			action(case, "press")["before"] = "800"
		for mutate in (reversed_bracket, duplicate, textual):
			with self.subTest(mutate=mutate.__name__):
				result = self.assertInvalid(mutate)
				self.assertTrue(result["checks"][0]["facts"]["details"])

	def test_law_rules(self):
		def law(**fields):
			def mutate(case):
				glide = by_name(case["spec"]["windows"], "glide")
				glide["law"] = dict({
					"name": "glide",
					"origins": ["release", "move_1", "move_2"],
					"tolerance": TOLERANCE_PX,
				}, **fields)
				case["laws"] = {"glide": glide_law()}
			return mutate

		def no_laws(case):
			law()(case)
			case["laws"] = None
		self.assertPasses(law())
		for label, mutate in (
				("unknown law", law(name="spring")),
				("unknown origin", law(origins=["lift"])),
				("no origins", law(origins=[])),
				("negative tolerance", law(tolerance=-1)),
				("no laws file", no_laws)):
			with self.subTest(label):
				self.assertInvalid(mutate)

	def test_spec_shape(self):
		def schema(case):
			case["spec"]["schema"] = 2

		def duplicate_window(case):
			windows = case["spec"]["windows"]
			windows.append(copy.deepcopy(windows[0]))

		def no_windows(case):
			case["spec"]["windows"] = []
		for mutate in (schema, duplicate_window, no_windows):
			with self.subTest(mutate=mutate.__name__):
				self.assertInvalid(mutate)


class TimeOriginTests(Assessing):
	law = True

	def law_report(self, result):
		return window(result, "glide")["law"]

	def test_handler_law_passes_and_caller_stamps_are_diagnosed(self):
		result = self.assertPasses()
		law = self.law_report(result)
		self.assertEqual(law["claim"], "exact")
		self.assertLess(law["handler"]["residualPx"], 1e-6)
		self.assertTrue(law["handler"]["pass"])
		self.assertGreater(law["caller"]["residualPx"], TOLERANCE_PX)
		self.assertAlmostEqual(law["caller"]["residualPx"], 1.19, delta=0.01)
		self.assertFalse(law["caller"]["pass"])
		self.assertEqual(law["offsetsMs"], {
			"release": 0,
			"move_1": 0,
			"move_2": 1,
		})
		self.assertEqual(law["handler"]["origins"], handler_origins())
		self.assertEqual(law["caller"]["origins"]["move_2"], MOVE_CALLER[1])
		self.assertEqual(law["tolerance"], TOLERANCE_PX)
		self.assertEqual(law["handler"]["tolerance"], TOLERANCE_PX)
		self.assertEqual(law["caller"]["tolerance"], TOLERANCE_PX)
		self.assertEqual(law["diagnosis"]["code"], "time-origin-discrepancy")
		self.assertEqual(law["diagnosis"]["tolerance"], TOLERANCE_PX)
		self.assertTrue(window(result, "glide")["pass"])
		self.assertEqual(
			[(item["window"], item["code"]) for item in result["diagnoses"]],
			[("glide", "time-origin-discrepancy")])

	def test_wrong_law(self):
		def mutate(case):
			case["laws"] = {"glide": glide_law(150.0)}
		result = self.assertRefused(mutate, {"law-residual"})
		law = self.law_report(result)
		self.assertGreater(law["handler"]["residualPx"], TOLERANCE_PX)
		self.assertEqual(law["tolerance"], TOLERANCE_PX)

	def test_handler_outside_bracket(self):
		def early(case):
			action(case, "move_2")["handlerAt"] = MOVE_CALLER[1] - 1

		def late(case):
			action(case, "release")["handlerAt"] = RELEASE_AT + 2
		result = self.assertRefused(early, {"inconsistent-clock"})
		self.assertIsNone(self.law_report(result)["claim"])
		result = self.assertRefused(late, {"inconsistent-clock"})
		glide = window(result, "glide")
		self.assertIsNone(glide["law"]["claim"])
		self.assertEqual(
			glide["firstObservationDelay"]["claim"],
			"conservative-bound")
		self.assertEqual(
			{check["check"] for check in result["checks"] if not check["pass"]},
			{"law", "origin"})

	def test_missing_exact_origin(self):
		def mutate(case):
			del action(case, "move_2")["handlerAt"]
		result = self.assertRefused(mutate, {"missing-exact-origin"})
		law = self.law_report(result)
		self.assertIsNone(law["claim"])
		self.assertIsNone(law["handler"])
		self.assertIsNone(law["caller"])
		self.assertIsNone(law["diagnosis"])
		self.assertNotIn('"exact"', json.dumps(law))
		check = next(
			check for check in result["checks"]
			if check["reason"] == "missing-exact-origin")
		self.assertEqual(check["facts"]["details"][0]["origins"], ["move_2"])

	def test_law_error(self):
		def raises(origins, at):
			raise RuntimeError("no such state")

		def mutate(case):
			case["laws"] = {"glide": raises}

		def short(case):
			case["laws"] = {"glide": lambda origins, at: [1.0]}
		self.assertRefused(mutate, {"law-error"})
		self.assertRefused(short, {"law-error"})


class FileCase(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix="motion window ")
		self.addCleanup(temporary.cleanup)
		self.root = temporary.name
		case = make_case()
		self.spec = self.write("spec.json", make_spec(law=True))
		self.manifest = self.write("manifest.json", case["manifest"])
		self.replay = self.write("replay.json", case["replay"])
		self.laws = self.write_text("laws.py", LAWS_SOURCE)
		self.original_bytes = (json.dumps(ORIGINAL, indent=4) + "\n").encode(
			"utf-8")
		self.original = os.path.join(self.root, "original.json")
		with open(self.original, "wb") as file:
			file.write(self.original_bytes)

	def path(self, name):
		return os.path.join(self.root, name)

	def write(self, name, value):
		return self.write_text(name, json.dumps(value, indent=1))

	def write_text(self, name, text):
		path = self.path(name)
		with open(path, "w", encoding="utf-8") as file:
			file.write(text)
		return path

	def argv(self, *extra, laws=True):
		argv = [
			"--spec", self.spec,
			"--manifest", self.manifest,
			"--replay", self.replay,
		]
		if laws:
			argv += ["--laws", self.laws]
		return argv + list(extra)

	def run_cli(self, *extra, laws=True):
		return subprocess.run(
			[sys.executable, TOOL] + self.argv(*extra, laws=laws),
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			text=True,
			check=False)

	def reassess(self, *extra):
		return (
			"--reassess", self.original,
			"--correction", CORRECTION,
			"--claim", CLAIM,
		) + extra

	def assertOriginalIntact(self):
		with open(self.original, "rb") as file:
			self.assertEqual(file.read(), self.original_bytes)


class ReassessmentTests(FileCase):
	def test_original_stays_byte_identical(self):
		out = self.path("reassessed.json")
		result = self.run_cli(*self.reassess("--out", out))
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertOriginalIntact()
		printed = json.loads(result.stdout)
		with open(out, "r", encoding="utf-8") as file:
			self.assertEqual(json.load(file), printed)
		self.assertTrue(printed["allPassed"])
		reassessment = printed["reassessment"]
		original = reassessment["original"]
		self.assertEqual(original["path"], os.path.realpath(self.original))
		self.assertEqual(original["bytes"], len(self.original_bytes))
		self.assertEqual(
			original["sha256"],
			hashlib.sha256(self.original_bytes).hexdigest())
		self.assertEqual(original["failedChecks"], [ORIGINAL["checks"][1]])
		self.assertEqual(reassessment["correction"], CORRECTION)
		self.assertEqual(reassessment["claim"], CLAIM)
		self.assertEqual(printed["inputs"]["original"]["sha256"], original["sha256"])

	def test_missing_correction_is_a_usage_error(self):
		result = self.run_cli("--reassess", self.original, "--claim", CLAIM)
		self.assertEqual(result.returncode, 2, result.stderr)
		self.assertEqual(result.stdout, "")
		result = self.run_cli(*self.reassess("--correction", " "))
		self.assertEqual(result.returncode, 2, result.stderr)
		self.assertOriginalIntact()

	def test_out_naming_an_input(self):
		linked = self.path("linked.json")
		os.link(self.original, linked)
		for out, inputs in (
				(self.original, ["original"]),
				(linked, ["original"]),
				(self.manifest, ["manifest"]),
				(self.laws, ["laws"])):
			with self.subTest(out=os.path.basename(out)):
				result = self.run_cli(*self.reassess("--out", out))
				self.assertEqual(result.returncode, 1, result.stderr)
				printed = json.loads(result.stdout)
				self.assertEqual(printed["reasons"], ["would-overwrite-input"])
				detail = printed["checks"][0]["facts"]["details"][0]
				self.assertEqual(detail["inputs"], inputs)
				self.assertOriginalIntact()
		with open(self.manifest, "r", encoding="utf-8") as file:
			self.assertIn("streams", json.load(file))

	def test_invalid_original(self):
		for name, data in (
				("no-checks.json", b'{"allPassed": false}'),
				("bad-entry.json", b'{"checks": [{"check": "x"}]}'),
				("not-json.json", b"allPassed=false")):
			with self.subTest(name=name):
				path = self.path(name)
				with open(path, "wb") as file:
					file.write(data)
				result = self.run_cli(
					"--reassess", path,
					"--correction", CORRECTION,
					"--claim", CLAIM)
				self.assertEqual(result.returncode, 1, result.stderr)
				printed = json.loads(result.stdout)
				self.assertEqual(printed["reasons"], ["invalid-original"])
				with open(path, "rb") as file:
					self.assertEqual(file.read(), data)

	def test_original_modified_while_reassessing(self):
		out = self.path("reassessed.json")
		calls = []

		def writer(path, text):
			calls.append(path)
			motion_window.write_text(path, text)
			with open(self.original, "ab") as file:
				file.write(b" ")

		stdout = io.StringIO()
		with contextlib.redirect_stdout(stdout):
			code = motion_window.main(
				self.argv(*self.reassess("--out", out)),
				writer=writer)
		self.assertEqual(code, 1)
		printed = json.loads(stdout.getvalue())
		self.assertEqual(printed["reasons"], ["original-modified"])
		self.assertEqual(calls, [out, out])
		with open(out, "r", encoding="utf-8") as file:
			self.assertEqual(json.load(file), printed)
		check = printed["checks"][-1]
		self.assertEqual(check["check"], "reassessment")
		self.assertEqual(
			check["facts"]["details"][0]["sha256"],
			hashlib.sha256(self.original_bytes).hexdigest())


class CliTests(FileCase):
	def test_natural_window_with_laws(self):
		out = self.path("result.json")
		result = self.run_cli("--out", out)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		printed = json.loads(result.stdout)
		with open(out, "r", encoding="utf-8") as file:
			self.assertEqual(json.load(file), printed)
		self.assertTrue(printed["allPassed"])
		law = window(printed, "glide")["law"]
		self.assertEqual(law["claim"], "exact")
		self.assertEqual(law["diagnosis"]["code"], "time-origin-discrepancy")
		with open(self.laws, "rb") as file:
			self.assertEqual(
				printed["inputs"]["laws"]["sha256"],
				hashlib.sha256(file.read()).hexdigest())
		self.assertNotIn("reassessment", printed)

	def test_rejecting_control_exits_one(self):
		case = make_case()
		by_name(case["manifest"]["streams"], "glide_11")["recordMs"] = 5.0
		self.manifest = self.write("over-budget.json", case["manifest"])
		result = self.run_cli()
		self.assertEqual(result.returncode, 1, result.stderr)
		self.assertEqual(json.loads(result.stdout)["reasons"], ["over-budget"])

	def test_invalid_spec_exits_one(self):
		spec = make_spec(law=True)
		spec["windows"][0]["kind"] = "hover"
		self.spec = self.write("invalid-spec.json", spec)
		result = self.run_cli()
		self.assertEqual(result.returncode, 1, result.stderr)
		printed = json.loads(result.stdout)
		self.assertEqual(printed["reasons"], ["invalid-spec"])
		self.assertEqual(printed["windows"], [])
		result = self.run_cli(laws=False)
		self.assertEqual(result.returncode, 1, result.stderr)
		self.assertEqual(json.loads(result.stdout)["reasons"], ["invalid-spec"])

	def test_input_errors_exit_two(self):
		os.remove(self.replay)
		result = self.run_cli()
		self.assertEqual(result.returncode, 2)
		self.assertIn("error: cannot read", result.stderr)
		self.assertEqual(result.stdout, "")

	def test_laws_file_errors_exit_two(self):
		for name, source in (
				("no-laws.py", "GLIDE = 1\n"),
				("not-callable.py", "LAWS = {'glide': 1}\n"),
				("raises.py", "raise RuntimeError('broken')\n")):
			with self.subTest(name=name):
				self.laws = self.write_text(name, source)
				result = self.run_cli()
				self.assertEqual(result.returncode, 2, result.stdout)
				self.assertIn("error:", result.stderr)


class DocumentationTests(unittest.TestCase):
	def test_refusal_table_matches_codes(self):
		with open(DOCUMENT, "r", encoding="utf-8") as file:
			text = file.read()
		section = text.split("\n## Refusals\n", 1)[1].split("\n## ", 1)[0]
		documented = re.findall(r"^\| `([a-z0-9-]+)` \|", section, re.MULTILINE)
		self.assertEqual(len(documented), len(set(documented)))
		self.assertEqual(set(documented), set(motion_window.REFUSALS))
		self.assertIn(motion_window.DIAGNOSIS, text)


if __name__ == "__main__":
	unittest.main()
