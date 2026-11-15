#!/usr/bin/env python3

import argparse
import fractions
import hashlib
import json
import math
import os
import sys

import paint_replay


SCHEMA = 1
TOOL = "motion_window"
SPEC_SCHEMA = 1
KINDS = ("rest", "pressed", "release")
MOVING_KINDS = ("pressed", "release")
SPEC_KEYS = ("schema", "actions", "windows")
ACTION_KEYS = ("name", "before", "after", "handlerAt")
WINDOW_KEYS = (
	"name",
	"kind",
	"origin",
	"phases",
	"budgetMs",
	"replayQuantile",
	"law",
)
LAW_KEYS = ("name", "origins", "tolerance")
DIAGNOSIS = "time-origin-discrepancy"
RECORD_AGAIN = ("fix the overlay that wrote the records and record again; "
	"never edit records, digests or replays by hand")


def sentence(text):
	return text[0].upper() + text[1:] + "."


REFUSALS = {
	"invalid-spec": (
		"The window spec is not a schema-1 spec: an unknown key or kind, a "
		"pressed or release window without an integer phases >= 1, phases "
		"on a rest window, budgetMs without a replayQuantile in (0, 1] or "
		"the reverse, an origin or law origin that names no declared "
		"action, a law name the laws file does not define, a duplicate "
		"name, or an action with before > after.",
		"Fix the spec; nothing was assessed."),
	"invalid-original": (
		"The --reassess file is not UTF-8 JSON with a top-level checks list "
		"of {check, pass} entries.",
		"Pass the original result file of the judge being reassessed, "
		"unchanged."),
	"would-overwrite-input": (
		"--out names an input file, by path or as the same file.",
		"Write the result to a new path; no input, and never an original "
		"result, is overwritten."),
	"original-modified": (
		"The original result's SHA-256 changed between the start of the "
		"reassessment and the re-read after the result was written.",
		"Restore the original from where it was produced and reassess "
		"again; an original result is never edited."),
	"duplicate-record": (
		"Two manifest rows carry one stream name.",
		sentence("give every recording its own name; " + RECORD_AGAIN)),
	"duplicate-replay": (
		"Two replay rows carry one stream name.",
		"Replay the manifest once with the unmodified replayer."),
	"missing-replay": (
		"A recorded stream has no replay row; a budgeted record without "
		"one is also refused in its window's budget check.",
		"Replay the complete manifest; a stream left out is not measured."),
	"extra-replay": (
		"A replay row has no recorded stream.",
		"Replay exactly the manifest assessed."),
	"mismatched-identity": (
		"A stream's .qpic or .tables SHA-256 differs between the manifest "
		"and the replay row.",
		sentence("replay the files the recorder wrote, unchanged; "
			+ RECORD_AGAIN)),
	"invalid-record": (
		"A window record lacks a non-empty name, a numeric at, boolean "
		"pressed and moving, or a numeric position list of the window's "
		"dimension.",
		"Record every sample with the window, at, pressed, moving and "
		"position fields."),
	"empty-window": (
		"A declared window has no record.",
		"Record the window, and name it in each record's window field."),
	"unordered-records": (
		"A window's at values are not strictly increasing in record order.",
		"Record in time order on one clock; never sort or renumber records "
		"afterwards."),
	"motion-in-rest-window": (
		"A rest record is pressed or moving, or the position changes.",
		"Record rest only while the owner is at rest, or declare the "
		"window as pressed or release."),
	"not-started-in-motion": (
		"A pressed or release window's first record is not moving.",
		"Start the window at the first moving observation; a static record "
		"cannot stand for the start of a motion."),
	"released-in-pressed-window": (
		"A pressed window holds an unpressed record.",
		"End the pressed window at the release and record the release as "
		"its own window."),
	"static-substitution": (
		"A pressed window holds a record that is not moving, a release "
		"window holds a stopped record before its terminal, the position "
		"never changes, or no moving record exists to place in phases.",
		"Record the real motion; a static or frozen state cannot stand in "
		"for it."),
	"pressed-in-release-window": (
		"A release window holds a pressed record.",
		"Start the release window after the release action."),
	"missing-stopped-terminal": (
		"A release window's last record is still moving.",
		"Keep recording until the first stopped observation and keep that "
		"record as the window's last."),
	"phase-uncovered": (
		"A declared phase of the first-to-last displacement holds no moving "
		"record, or the first and last records coincide so progress is "
		"undefined.",
		"Record densely enough to observe every declared phase; more "
		"records elsewhere do not cover it."),
	"over-budget": (
		"A record's envelope, its recordMs plus the replayQuantile order "
		"statistic of its own stream's replays, exceeds budgetMs.",
		"Report the over-budget streams; the declared budget is never "
		"widened."),
	"missing-cost": (
		"A budgeted record has no recordMs, or its replay row has no "
		"usable timesNs.",
		"Record with RecordPaint() and replay every timed stream with the "
		"gated replayer."),
	"inconsistent-clock": (
		"An origin action's handlerAt lies outside its before/after "
		"bracket, so the stamps are not of one clock or one dispatch.",
		"Stamp before, handlerAt and after on one crl::now() clock around "
		"and inside the same dispatch."),
	"missing-exact-origin": (
		"A law origin has no handlerAt, so no exact law claim is made.",
		"Record handler-time telemetry for every law origin, such as a "
		"Test::Probe row in the handler path; without it only the "
		"conservative delay bounds hold."),
	"law-residual": (
		"The law evaluated at the handler-time origins misses an observed "
		"position by more than the declared tolerance.",
		"Report the residual; never widen the tolerance or judge with "
		"caller stamps instead."),
	"law-error": (
		"The law callable raised, or returned something other than a "
		"finite point of the records' dimension.",
		"Fix the laws file."),
}
SCOPE = {
	"decision": (
		"per declared window: its own records, in record order, cover the "
		"declared states and phases, every recorded stream has exactly one "
		"matching replay, and when a budget is declared every record's own "
		"envelope is within it"),
	"statistics": (
		"each envelope uses the record's own recordMs and the nearest-rank "
		"order statistic of its own stream's kept replays: rank ceil(q * n) "
		"of n, reported per record"),
	"origins": (
		"exact delays and law residuals come from handler-time origins "
		"(handlerAt); caller stamps give conservative delay bounds and the "
		"time-origin diagnosis only"),
	"notClaimed": (
		"every presented frame, frames between records, compositor or "
		"presentation cadence, a Release Telegram frame time, or a "
		"population, tail or confidence bound"),
}
LIMITS = [
	"declared windows and their records only: frames, presentations and "
	"states between two records are not observed",
	"no population or tail claim: an envelope is one record's recordMs plus "
	"an order statistic of its own stream's kept replays in one run",
	"budgets assume a replay that paint_replay.py approved; this tool does "
	"not re-check the gate, the Qt build, fidelity or sample accounting",
	"phases are declared equal fractions of each moving record's progress "
	"along the window's first-to-last displacement; no record count "
	"substitutes for them, and a record outside that segment covers none",
	"delays and laws are on the recorder's clock relative to declared "
	"actions; exact claims need handler time, caller stamps give bounds",
	"laws are caller callables run in this process; the tool compares their "
	"positions with the records and does not validate the physics",
]


class InputError(Exception):
	pass


class Failures:
	def __init__(self):
		self.items = {}

	def add(self, code, detail):
		self.items.setdefault(code, []).append(detail)


def is_int(value):
	return isinstance(value, int) and not isinstance(value, bool)


def is_number(value):
	return (isinstance(value, (int, float))
		and not isinstance(value, bool)
		and math.isfinite(value))


def as_dict(value):
	return value if isinstance(value, dict) else {}


def ms(value):
	return round(value, 6)


def exact(value):
	return fractions.Fraction(value)


def valid_quantile(value):
	return is_number(value) and 0 < value <= 1


def unknown_keys(where, value, allowed, problems):
	extra = sorted(str(key) for key in value if key not in allowed)
	if extra:
		problems.append(f"{where}: unknown keys {extra}")


def parse_action(where, action, problems):
	if not isinstance(action, dict):
		problems.append(f"{where} is not an object")
		return None
	unknown_keys(where, action, ACTION_KEYS, problems)
	name = action.get("name")
	if not isinstance(name, str) or not name:
		problems.append(f"{where}: name is not a non-empty string")
		return None
	before = action.get("before")
	after = action.get("after")
	handler = action.get("handlerAt")
	valid = True
	if not is_number(before):
		problems.append(f"action {name!r}: before is not a number")
		valid = False
	if after is not None and not is_number(after):
		problems.append(f"action {name!r}: after is not a number")
		valid = False
	if handler is not None and not is_number(handler):
		problems.append(f"action {name!r}: handlerAt is not a number")
		valid = False
	if valid and after is not None and before > after:
		problems.append(f"action {name!r}: before > after")
		valid = False
	if not valid:
		return None
	return {"name": name, "before": before, "after": after, "handlerAt": handler}


def parse_actions(value, problems):
	actions = {}
	if not isinstance(value, list):
		problems.append("actions is not a list")
		return actions
	names = set()
	for position, item in enumerate(value):
		action = parse_action(f"actions[{position}]", item, problems)
		name = as_dict(item).get("name")
		if isinstance(name, str) and name:
			if name in names:
				problems.append(f"actions[{position}]: duplicate name {name!r}")
				continue
			names.add(name)
		if action is not None:
			actions[action["name"]] = action
	return actions


def parse_law(where, law, actions, laws, problems):
	if not isinstance(law, dict):
		problems.append(f"{where}: law is not an object")
		return None
	unknown_keys(f"{where}.law", law, LAW_KEYS, problems)
	name = law.get("name")
	origins = law.get("origins")
	tolerance = law.get("tolerance")
	valid = True
	if not isinstance(name, str) or name not in (laws or {}):
		problems.append(f"{where}: law {name!r} is not defined by the laws "
			"file")
		valid = False
	if (not isinstance(origins, list)
		or not origins
		or not all(isinstance(item, str) for item in origins)
		or len(set(origins)) != len(origins)):
		problems.append(f"{where}: law origins is not a non-empty list of "
			"distinct action names")
		valid = False
	else:
		for origin in origins:
			if origin not in actions:
				problems.append(f"{where}: law origin {origin!r} is not a "
					"declared action")
				valid = False
	if not is_number(tolerance) or tolerance < 0:
		problems.append(f"{where}: law tolerance is not a number >= 0")
		valid = False
	if not valid:
		return None
	return {"name": name, "origins": list(origins), "tolerance": tolerance}


def parse_window(where, window, actions, laws, problems):
	if not isinstance(window, dict):
		problems.append(f"{where} is not an object")
		return None
	unknown_keys(where, window, WINDOW_KEYS, problems)
	name = window.get("name")
	if not isinstance(name, str) or not name:
		problems.append(f"{where}: name is not a non-empty string")
		return None
	where = f"window {name!r}"
	kind = window.get("kind")
	origin = window.get("origin")
	phases = window.get("phases")
	budget = window.get("budgetMs")
	q = window.get("replayQuantile")
	valid = True
	if kind not in KINDS:
		problems.append(f"{where}: kind {kind!r} is not one of {list(KINDS)}")
		valid = False
	if origin is not None and (not isinstance(origin, str)
			or origin not in actions):
		problems.append(f"{where}: origin {origin!r} is not a declared "
			"action")
		valid = False
	if kind in MOVING_KINDS and (not is_int(phases) or phases < 1):
		problems.append(f"{where}: a {kind} window needs an integer phases "
			">= 1")
		valid = False
	elif kind == "rest" and phases is not None:
		problems.append(f"{where}: phases apply to pressed and release "
			"windows only")
		valid = False
	if budget is not None:
		if not is_number(budget) or budget <= 0:
			problems.append(f"{where}: budgetMs is not a number > 0")
			valid = False
		if not valid_quantile(q):
			problems.append(f"{where}: budgetMs needs a replayQuantile in "
				"(0, 1]")
			valid = False
	elif q is not None:
		problems.append(f"{where}: replayQuantile without budgetMs")
		valid = False
	law = None
	if window.get("law") is not None:
		law = parse_law(where, window["law"], actions, laws, problems)
		valid = valid and law is not None
	if not valid:
		return None
	return {
		"name": name,
		"kind": kind,
		"origin": origin,
		"phases": phases,
		"budgetMs": budget,
		"replayQuantile": q,
		"law": law,
	}


def parse_spec(spec, laws):
	problems = []
	if not isinstance(spec, dict):
		return None, ["the spec is not a JSON object"]
	unknown_keys("spec", spec, SPEC_KEYS, problems)
	schema = spec.get("schema")
	if not is_int(schema) or schema != SPEC_SCHEMA:
		problems.append(f"schema is {schema!r}, not {SPEC_SCHEMA}")
	actions = parse_actions(spec.get("actions", []), problems)
	windows = []
	value = spec.get("windows")
	if not isinstance(value, list) or not value:
		problems.append("windows is not a non-empty list")
		value = []
	names = set()
	for position, item in enumerate(value):
		window = parse_window(
			f"windows[{position}]",
			item,
			actions,
			laws,
			problems)
		name = as_dict(item).get("name")
		if isinstance(name, str) and name:
			if name in names:
				problems.append(f"windows[{position}]: duplicate name "
					f"{name!r}")
				continue
			names.add(name)
		if window is not None:
			windows.append(window)
	if problems:
		return None, problems
	return {"actions": actions, "windows": windows}, []


def record_problems(record):
	problems = []
	name = record.get("name")
	if not isinstance(name, str) or not name:
		problems.append("name")
	if not is_number(record.get("at")):
		problems.append("at")
	for key in ("pressed", "moving"):
		if not isinstance(record.get(key), bool):
			problems.append(key)
	position = record.get("position")
	if (not isinstance(position, list)
		or not position
		or not all(is_number(value) for value in position)):
		problems.append("position")
	return problems


def manifest_rows(manifest):
	rows = as_dict(manifest).get("streams")
	return rows if isinstance(rows, list) else None


def replay_rows(replay):
	rows = as_dict(replay).get("rows")
	return rows if isinstance(rows, list) else None


def replays_by_name(rows):
	result = {}
	for row in rows:
		name = paint_replay.stream_name(row)
		if name is not None:
			result.setdefault(name, []).append(row)
	return result


def clock_consistent(action):
	handler = action["handlerAt"]
	if handler is None:
		return True
	return (action["before"] <= handler
		and (action["after"] is None or handler <= action["after"]))


def clock_detail(action):
	return {
		"action": action["name"],
		"before": action["before"],
		"after": action["after"],
		"handlerAt": action["handlerAt"],
	}


def delay(action, at):
	handler = action["handlerAt"]
	if handler is not None and clock_consistent(action):
		return {
			"claim": "exact",
			"origin": action["name"],
			"ms": ms(at - handler),
		}
	after = action["after"]
	return {
		"claim": "conservative-bound",
		"origin": action["name"],
		"bounds": [
			None if after is None else ms(at - after),
			ms(at - action["before"]),
		],
	}


def record_checks(name, records):
	failures = Failures()
	if not records:
		failures.add("empty-window", {"window": name})
	valid = True
	for record in records:
		problems = record_problems(record)
		if problems:
			valid = False
			failures.add("invalid-record", {
				"stream": record.get("name"),
				"fields": problems,
			})
	if valid:
		dimensions = sorted({len(record["position"]) for record in records})
		if len(dimensions) > 1:
			valid = False
			failures.add("invalid-record", {
				"problem": "positions of different dimensions",
				"dimensions": dimensions,
			})
	if valid:
		for previous, record in zip(records, records[1:]):
			if not previous["at"] < record["at"]:
				failures.add("unordered-records", {
					"stream": record["name"],
					"at": record["at"],
					"previous": previous["name"],
					"previousAt": previous["at"],
				})
	facts = {"records": len(records)}
	return facts, failures, bool(records) and valid


def state_checks(kind, records):
	failures = Failures()
	first = records[0]
	last = records[-1]
	changed = [
		record["name"] for record in records
		if record["position"] != first["position"]
	]
	if kind == "rest":
		for record in records:
			if record["pressed"] or record["moving"]:
				failures.add("motion-in-rest-window", {
					"stream": record["name"],
					"pressed": record["pressed"],
					"moving": record["moving"],
				})
		if changed:
			failures.add("motion-in-rest-window", {
				"problem": "the position changes",
				"streams": changed,
			})
		return failures
	if not first["moving"]:
		failures.add("not-started-in-motion", {"stream": first["name"]})
	if kind == "pressed":
		released = [r["name"] for r in records if not r["pressed"]]
		if released:
			failures.add("released-in-pressed-window", {"streams": released})
		static = [r["name"] for r in records if not r["moving"]]
		if static:
			failures.add("static-substitution", {
				"problem": "records that are not moving",
				"streams": static,
			})
	else:
		pressed = [r["name"] for r in records if r["pressed"]]
		if pressed:
			failures.add("pressed-in-release-window", {"streams": pressed})
		if last["moving"]:
			failures.add("missing-stopped-terminal", {"stream": last["name"]})
		early = [r["name"] for r in records[:-1] if not r["moving"]]
		if early:
			failures.add("static-substitution", {
				"problem": "stopped records before the terminal record",
				"streams": early,
			})
	if not changed:
		failures.add("static-substitution", {
			"problem": "the position never changes",
			"position": first["position"],
		})
	return failures


def phase_checks(records, phases):
	failures = Failures()
	first = records[0]["position"]
	last = records[-1]["position"]
	report = {
		"declared": phases,
		"bounds": [
			[ms(index / phases), ms((index + 1) / phases)]
			for index in range(phases)
		],
		"from": records[0]["name"],
		"till": records[-1]["name"],
		"counts": [0] * phases,
		"records": [[] for _ in range(phases)],
		"outside": [],
		"progress": [],
	}
	moving = [record for record in records if record["moving"]]
	changed = any(record["position"] != first for record in records)
	if not moving or not changed:
		failures.add("static-substitution", {
			"problem": ("no moving record" if not moving
				else "the position never changes"),
		})
		return report, failures
	direction = [exact(b) - exact(a) for a, b in zip(first, last)]
	length = sum(item * item for item in direction)
	if length == 0:
		failures.add("phase-uncovered", {
			"problem": "the first and last records coincide, so progress is "
				"undefined",
			"phases": list(range(1, phases + 1)),
		})
		return report, failures
	for record in moving:
		offset = [exact(v) - exact(a) for v, a in zip(record["position"], first)]
		progress = sum(o * d for o, d in zip(offset, direction)) / length
		entry = {
			"stream": record["name"],
			"progress": ms(float(progress)),
			"phase": None,
		}
		if 0 <= progress <= 1:
			index = min(math.floor(progress * phases), phases - 1)
			entry["phase"] = index + 1
			report["counts"][index] += 1
			report["records"][index].append(record["name"])
		else:
			report["outside"].append(record["name"])
		report["progress"].append(entry)
	uncovered = [
		index + 1 for index, count in enumerate(report["counts"]) if not count
	]
	if uncovered:
		failures.add("phase-uncovered", {
			"phases": uncovered,
			"counts": report["counts"],
		})
	return report, failures


def budget_checks(window, records, replays):
	failures = Failures()
	budget = window["budgetMs"]
	q = window["replayQuantile"]
	entries = []
	for record in records:
		name = record["name"]
		cost = record.get("recordMs")
		has_cost = is_number(cost) and cost >= 0
		entry = {
			"stream": name,
			"recordMs": cost,
			"replay": None,
			"envelopeMs": None,
		}
		if not has_cost:
			failures.add("missing-cost", {"stream": name, "missing": "recordMs"})
		rows = replays.get(name, [])
		if not rows:
			failures.add("missing-replay", {"stream": name})
		values = []
		for row in rows:
			try:
				values.append(paint_replay.quantile(row.get("timesNs"), q))
			except ValueError as error:
				failures.add("missing-cost", {
					"stream": name,
					"missing": "timesNs",
					"rowError": row.get("error"),
					"problem": str(error),
				})
		if values:
			chosen = max(values, key=lambda value: value["valueMs"])
			entry["replay"] = chosen
			if has_cost:
				envelope = ms(cost + chosen["valueMs"])
				entry["envelopeMs"] = envelope
				if envelope > budget:
					failures.add("over-budget", {
						"stream": name,
						"recordMs": cost,
						"replayMs": chosen["valueMs"],
						"rank": chosen["rank"],
						"n": chosen["n"],
						"envelopeMs": envelope,
						"budgetMs": budget,
					})
		entries.append(entry)
	envelopes = [
		entry["envelopeMs"] for entry in entries
		if entry["envelopeMs"] is not None
	]
	report = {
		"budgetMs": budget,
		"replayQuantile": q,
		"envelopeMaxMs": max(envelopes) if envelopes else None,
		"records": entries,
	}
	return report, failures


def evaluate_law(law, origins, records, tolerance):
	residual = 0.0
	worst = None
	for record in records:
		try:
			point = law(dict(origins), record["at"])
		except Exception as error:
			raise ValueError(f"{record['name']}: {type(error).__name__}: "
				f"{error}") from error
		if (not isinstance(point, (list, tuple))
			or len(point) != len(record["position"])
			or not all(is_number(value) for value in point)):
			raise ValueError(f"{record['name']}: the law returned {point!r}, "
				f"not a point of dimension {len(record['position'])}")
		distance = math.sqrt(sum(
			(observed - predicted) ** 2
			for observed, predicted in zip(record["position"], point)))
		if worst is None or distance > residual:
			residual = distance
			worst = record["name"]
	return {
		"origins": dict(origins),
		"residualPx": ms(residual),
		"worst": worst,
		"tolerance": tolerance,
		"pass": residual <= tolerance,
	}


def judge_law(records, actions, law_spec, law):
	failures = Failures()
	tolerance = law_spec["tolerance"]
	origins = [actions[name] for name in law_spec["origins"]]
	report = {
		"name": law_spec["name"],
		"origins": list(law_spec["origins"]),
		"tolerance": tolerance,
		"claim": None,
		"handler": None,
		"caller": None,
		"offsetsMs": {
			action["name"]: ms(action["handlerAt"] - action["before"])
			for action in origins if action["handlerAt"] is not None
		},
		"diagnosis": None,
	}
	missing = [a["name"] for a in origins if a["handlerAt"] is None]
	if missing:
		failures.add("missing-exact-origin", {"origins": missing})
		return report, failures
	inconsistent = [a for a in origins if not clock_consistent(a)]
	for action in inconsistent:
		failures.add("inconsistent-clock", clock_detail(action))
	if inconsistent:
		return report, failures
	handler_origins = {a["name"]: a["handlerAt"] for a in origins}
	caller_origins = {a["name"]: a["before"] for a in origins}
	try:
		handler = evaluate_law(law, handler_origins, records, tolerance)
		caller = evaluate_law(law, caller_origins, records, tolerance)
	except ValueError as error:
		failures.add("law-error", {"law": law_spec["name"], "error": str(error)})
		return report, failures
	report["claim"] = "exact"
	report["handler"] = handler
	report["caller"] = caller
	if not handler["pass"]:
		failures.add("law-residual", {
			"law": law_spec["name"],
			"residualPx": handler["residualPx"],
			"worst": handler["worst"],
			"tolerance": tolerance,
		})
	elif not caller["pass"]:
		report["diagnosis"] = {
			"code": DIAGNOSIS,
			"message": (
				"the law meets the tolerance at handler-time origins and "
				"misses it at the caller stamps: the caller stamps are not the "
				"handler's time origin (see offsetsMs); the exact claim uses "
				"handler time and the tolerance is unchanged"),
			"handlerResidualPx": handler["residualPx"],
			"callerResidualPx": caller["residualPx"],
			"tolerance": tolerance,
		}
	return report, failures


def judge_window(window, records, actions, replays, laws):
	name = window["name"]
	kind = window["kind"]
	report = {
		"name": name,
		"kind": kind,
		"pass": True,
		"reasons": [],
		"records": [record.get("name") for record in records],
	}
	checks = []
	facts, failures, usable = record_checks(name, records)
	checks.append(("records", facts, failures))
	if not usable:
		return report, checks
	first = records[0]
	last = records[-1]
	gaps = [ms(b["at"] - a["at"]) for a, b in zip(records, records[1:])]
	moving = sum(1 for record in records if record["moving"])
	origin = actions.get(window["origin"]) if window["origin"] else None
	report.update({
		"states": {
			"records": len(records),
			"pressed": sum(1 for record in records if record["pressed"]),
			"moving": moving,
			"stopped": len(records) - moving,
		},
		"firstAt": first["at"],
		"lastAt": last["at"],
		"spanMs": ms(last["at"] - first["at"]),
		"gapsMs": gaps,
		"maxGapMs": max(gaps) if gaps else None,
		"firstObservationDelay": delay(origin, first["at"]) if origin else None,
		"terminalDelay": delay(origin, last["at"]) if origin else None,
		"phases": None,
		"budget": None,
		"law": None,
		"scope": {
			"records": (
				"the window's own records in record order; what happened "
				"between two records is not observed"),
		},
	})
	checks.append(("states", {"kind": kind}, state_checks(kind, records)))
	if kind in MOVING_KINDS:
		phases, failures = phase_checks(records, window["phases"])
		report["phases"] = phases
		checks.append(("phases", {"counts": phases["counts"]}, failures))
	if window["budgetMs"] is not None:
		budget, failures = budget_checks(window, records, replays)
		report["budget"] = budget
		report["scope"]["budget"] = (
			f"each record's own recordMs plus the q = "
			f"{window['replayQuantile']} nearest-rank order statistic of "
			"its own stream's kept replays (rank of n per record); not "
			"every presentation, not a population or tail bound")
		checks.append(("budget", {
			"budgetMs": budget["budgetMs"],
			"envelopeMaxMs": budget["envelopeMaxMs"],
		}, failures))
	if origin is not None:
		failures = Failures()
		if not clock_consistent(origin):
			failures.add("inconsistent-clock", clock_detail(origin))
		checks.append(("origin", clock_detail(origin), failures))
	if window["law"] is not None:
		law_spec = window["law"]
		law, failures = judge_law(
			records,
			actions,
			law_spec,
			laws[law_spec["name"]])
		report["law"] = law
		checks.append(("law", {
			"law": law_spec["name"],
			"claim": law["claim"],
			"diagnosis": (law["diagnosis"] or {}).get("code"),
		}, failures))
	return report, checks


def refusal_message(code):
	meaning, action = REFUSALS[code]
	return f"{meaning} {action}"


def check_entries(check, facts, failures, window=None):
	base = {"check": check}
	if window is not None:
		base["window"] = window
	if not failures.items:
		return [dict(base, **{
			"pass": True,
			"reason": None,
			"message": None,
			"facts": facts,
		})]
	return [
		dict(base, **{
			"pass": False,
			"reason": code,
			"message": refusal_message(code),
			"facts": dict(facts, details=details),
		})
		for code, details in failures.items.items()
	]


def finish(checks, extra):
	reasons = []
	for check in checks:
		if not check["pass"] and check["reason"] not in reasons:
			reasons.append(check["reason"])
	result = {
		"schema": SCHEMA,
		"tool": TOOL,
		"allPassed": not reasons,
		"reasons": reasons,
		"checks": checks,
	}
	result.update(extra)
	result["scope"] = SCOPE
	result["limits"] = LIMITS
	return result


def refusal(code, details, check="inputs"):
	failures = Failures()
	for detail in details:
		failures.add(code, detail)
	return finish(check_entries(check, {}, failures), {
		"windows": [],
		"diagnoses": [],
	})


def assess(spec, manifest, replay, laws=None):
	parsed, problems = parse_spec(spec, laws)
	if parsed is None:
		return refusal(
			"invalid-spec",
			[{"problem": problem} for problem in problems],
			check="spec")
	actions = parsed["actions"]
	windows = parsed["windows"]
	streams = manifest_rows(manifest)
	rows = replay_rows(replay)
	checks = check_entries("spec", {
		"actions": sorted(actions),
		"windows": [window["name"] for window in windows],
	}, Failures())
	failures = Failures()
	for code, detail in paint_replay.account_streams(streams or [], rows or []):
		failures.add(code, detail)
	checks += check_entries("accounting", {
		"manifestStreams": None if streams is None else len(streams),
		"replayRows": None if rows is None else len(rows),
		"replayError": as_dict(replay).get("error"),
		"keys": list(paint_replay.IDENTITY_KEYS),
	}, failures)
	records = [
		row for row in streams or []
		if isinstance(row, dict) and "window" in row
	]
	replays = replays_by_name(row for row in rows or [] if isinstance(row, dict))
	declared = {window["name"] for window in windows}
	reports = []
	diagnoses = []
	for window in windows:
		members = [row for row in records if row.get("window") == window["name"]]
		report, window_checks = judge_window(
			window,
			members,
			actions,
			replays,
			laws)
		entries = []
		for check, facts, failures in window_checks:
			entries += check_entries(check, facts, failures, window["name"])
		for entry in entries:
			if not entry["pass"] and entry["reason"] not in report["reasons"]:
				report["reasons"].append(entry["reason"])
		report["pass"] = not report["reasons"]
		diagnosis = as_dict(report.get("law")).get("diagnosis")
		if diagnosis:
			diagnoses.append(dict(diagnosis, window=window["name"]))
		checks += entries
		reports.append(report)
	unassessed = [
		row.get("name") for row in records
		if not isinstance(row.get("window"), str)
		or row["window"] not in declared
	]
	return finish(checks, {
		"windows": reports,
		"diagnoses": diagnoses,
		"unassessedRecords": unassessed,
	})


def file_identity(path, data):
	return {
		"path": os.path.realpath(path),
		"bytes": len(data),
		"sha256": hashlib.sha256(data).hexdigest(),
	}


def read_bytes(path):
	try:
		with open(path, "rb") as file:
			return file.read()
	except OSError as error:
		raise InputError(f"cannot read {path}: {error}") from error


def read_json(path):
	try:
		return paint_replay.read_input(path)
	except ValueError as error:
		raise InputError(str(error)) from error


def load_laws(path):
	data = read_bytes(path)
	namespace = {"__name__": "motion_window_laws", "__file__": path}
	try:
		exec(compile(data, path, "exec"), namespace)
	except Exception as error:
		raise InputError(f"cannot load the laws file {path}: "
			f"{type(error).__name__}: {error}") from error
	laws = namespace.get("LAWS")
	if (not isinstance(laws, dict)
		or not all(isinstance(key, str) and callable(value)
			for key, value in laws.items())):
		raise InputError(f"{path} does not define LAWS as a dict of name -> "
			"callable(origins, at)")
	return laws, file_identity(path, data)


def original_problems(value):
	checks = value.get("checks") if isinstance(value, dict) else None
	if not isinstance(checks, list):
		return ["no top-level checks list"]
	problems = []
	for position, check in enumerate(checks):
		if (not isinstance(check, dict)
			or not isinstance(check.get("check"), str)
			or not check["check"]
			or not isinstance(check.get("pass"), bool)):
			problems.append(f"checks[{position}] is not a {{check, pass}} "
				"entry")
	return problems


def read_original(path):
	data = read_bytes(path)
	identity = file_identity(path, data)
	try:
		value = json.loads(data.decode("utf-8"))
	except (UnicodeDecodeError, ValueError) as error:
		return identity, None, [f"not UTF-8 JSON: {error}"]
	problems = original_problems(value)
	return identity, (None if problems else value), problems


def same_file(out, path):
	if os.path.realpath(out) == os.path.realpath(path):
		return True
	try:
		return os.path.samefile(out, path)
	except OSError:
		return False


def write_text(path, text):
	with open(path, "w", encoding="utf-8") as file:
		file.write(text + "\n")


def current_sha256(path):
	try:
		with open(path, "rb") as file:
			return hashlib.sha256(file.read()).hexdigest()
	except OSError:
		return None


def text_argument(text):
	if not text.strip():
		raise argparse.ArgumentTypeError("must not be empty")
	return text


def parse_args(argv=None):
	parser = argparse.ArgumentParser(
		description=(
			"Judge declared rest, pressed and release windows from paint "
			"manifest records and their replay rows: state and phase "
			"coverage, exact stream identities, per-record budgets and "
			"handler-time laws. Exit 0 when every check passes, 1 on a "
			"refusal, 2 on a usage or input error."))
	parser.add_argument("--spec", required=True,
		help="window spec JSON: actions and windows")
	parser.add_argument("--manifest", required=True,
		help="JSON object whose streams rows carry the records "
			"(a test_paint_stream.h manifest.json)")
	parser.add_argument("--replay", required=True,
		help="JSON object whose rows carry name and timesNs "
			"(a paint_replay result)")
	parser.add_argument("--laws",
		help="Python file defining LAWS = {name: callable(origins, at)}; "
			"it runs in this process")
	parser.add_argument("--reassess", metavar="ORIGINAL",
		help="original failing result to embed, never modified")
	parser.add_argument("--correction", type=text_argument,
		help="the instrument correction behind the reassessment")
	parser.add_argument("--claim", type=text_argument,
		help="the bounded claim the reassessment supports")
	parser.add_argument("--out",
		help="also write the result JSON to this file")
	options = parser.parse_args(argv)
	given = [
		value is not None
		for value in (options.reassess, options.correction, options.claim)
	]
	if any(given) and not all(given):
		parser.error("--reassess, --correction and --claim go together")
	return options


def dumps(result):
	return json.dumps(result, indent=2, sort_keys=True)


def original_check(identity, after):
	failures = Failures()
	failures.add("original-modified", {
		"path": identity["path"],
		"sha256": identity["sha256"],
		"sha256After": after,
	})
	return check_entries("reassessment", {}, failures)


def main(argv=None, writer=write_text):
	options = parse_args(argv)
	inputs = (
		("spec", options.spec),
		("manifest", options.manifest),
		("replay", options.replay),
		("laws", options.laws),
		("original", options.reassess),
	)
	if options.out:
		clashes = [
			key for key, path in inputs
			if path and same_file(options.out, path)
		]
		if clashes:
			print(dumps(refusal("would-overwrite-input", [{
				"out": os.path.realpath(options.out),
				"inputs": clashes,
			}])))
			return 1
	try:
		spec, spec_identity = read_json(options.spec)
		manifest, manifest_identity = read_json(options.manifest)
		replay, replay_identity = read_json(options.replay)
		laws, laws_identity = (load_laws(options.laws)
			if options.laws else (None, None))
		original = read_original(options.reassess) if options.reassess else None
	except InputError as error:
		print(f"error: {error}", file=sys.stderr)
		return 2
	if original is not None and original[1] is None:
		result = refusal("invalid-original", [
			dict(original[0], problems=original[2]),
		])
	else:
		result = assess(spec, manifest, replay, laws)
		if original is not None:
			identity, value, _ = original
			result["reassessment"] = {
				"original": dict(identity, failedChecks=[
					check for check in value["checks"] if not check["pass"]
				]),
				"correction": options.correction,
				"claim": options.claim,
			}
	result["inputs"] = {
		"spec": spec_identity,
		"manifest": manifest_identity,
		"replay": replay_identity,
		"laws": laws_identity,
		"original": original[0] if original else None,
	}
	text = dumps(result)
	try:
		if options.out:
			writer(options.out, text)
		if original is not None:
			after = current_sha256(options.reassess)
			if after != original[0]["sha256"]:
				result = finish(
					result["checks"] + original_check(original[0], after),
					{
						key: value for key, value in result.items()
						if key not in ("schema", "tool", "allPassed",
							"reasons", "checks", "scope", "limits")
					})
				text = dumps(result)
				if options.out:
					writer(options.out, text)
	except OSError as error:
		print(f"error: cannot write {options.out}: {error}", file=sys.stderr)
		return 2
	print(text)
	return 0 if result["allPassed"] else 1


if __name__ == "__main__":
	sys.exit(main())
