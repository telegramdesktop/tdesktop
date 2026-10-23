#!/usr/bin/env python3

import argparse
import fractions
import hashlib
import json
import math
import os
import sys

import native_exec


SCHEMA = 1
TOOL = "paint_replay_assess"
REPLAY_TOOL = "paint_replay"
MANIFEST_SCHEMA = 1
REPLAY_SCHEMA = 1
ROLES = ("timed", "absent", "narrow", "empty")
ROOTED_ROLES = ("timed", "absent", "narrow")
WHOLE_ROLES = ("timed", "absent")
IDENTITY_KEYS = ("qpic", "tables")
ROW_ERRORS = (
	"invalid-stream-name",
	"missing-stream-file",
	"empty-stream-file",
	"missing-sidecar",
	"empty-sidecar",
	"picture-format-mismatch",
	"stream-load-failed",
	"sidecar-unreadable",
	"sidecar-version-mismatch",
	"sidecar-count-mismatch",
	"playback-failed",
)
CONTRACT_KEYS = (
	("version", "qt-version-mismatch"),
	("pictureFormat", "picture-format-mismatch"),
	("sidecar", "sidecar-version-mismatch"),
	("platform", "platform-mismatch"),
	("colorScheme", "platform-mismatch"),
)
SAME_QT = ("build the replay component against the recording's exact Qt "
	"version and record and replay with it")
REGATE = ("gate the exact replay executable with native_exec.py --out "
	"before the replay and pass that verdict")
RECORD_AGAIN = ("fix the overlay or fixture that wrote the run directory and "
	"record again; never edit streams, sidecars or digests by hand")
KEEP_FILES = "keep the complete run directory as written; " + RECORD_AGAIN


def sentence(text):
	return text[0].upper() + text[1:] + "."


REFUSALS = {
	"manifest-schema": (
		"The manifest is not a schema-1 paint manifest with a qt block, a "
		"replayable surface and a list of stream rows.",
		"Pass the manifest.json that WritePaintManifest() wrote."),
	"replay-schema": (
		"The replay result is not a schema-1 paint_replay result with a rows "
		"list.",
		"Pass the --out file of the gated paint_replay run."),
	"replay-manifest-mismatch": (
		"The replay result names another manifest SHA-256 than the manifest "
		"assessed, or none.",
		"Assess the manifest the replay read, unchanged since the replay."),
	"missing-gate": (
		"No gate verdict was given for the replay executable.",
		sentence(REGATE + " with --gate")),
	"gate-not-passed": (
		"The gate verdict is not a PASS of native_exec schema 1, or its own "
		"execution record contradicts the PASS.",
		sentence(REGATE)),
	"gate-no-execution": (
		"The gate verdict carries no execution record with a report: it did "
		"not come from a launch.",
		sentence(REGATE)),
	"gate-nonce": (
		"The gate report's nonce is not the nonce the gate sent.",
		sentence(REGATE)),
	"gate-executable-mismatch": (
		"The gate verdict gated another file than the replay's executable.",
		"Gate the file that ran the replay; a Debug or another tree's "
		"verdict does not cover it."),
	"gate-stale-binary": (
		"The replay executable's current SHA-256 differs from the gated one: "
		"it was rebuilt or replaced after gating, or it is another file.",
		"Gate the current file again and replay with it unchanged."),
	"gate-host-arch": (
		"The gate verdict declared another host architecture than "
		"--host-arch.",
		"Gate and assess with the architecture the host runs natively."),
	"gate-after-replay": (
		"The gate finished after the replay started, or a timestamp is "
		"missing, so the gate did not precede the measured run.",
		"Gate first, then replay with the unchanged file."),
	"replay-not-native": (
		"The replay result's own native block reports another architecture "
		"than --host-arch, or translation.",
		"Rebuild the component for the host architecture, gate it and replay "
		"again."),
	"replay-fatal": (
		"The replayer stopped with a fatal error (facts.error) before or "
		"instead of replaying rows.",
		"Act on the replayer's code: version, format or sidecar mismatch - "
		"build against the recording's Qt and test_paint_stream.h; "
		"platform-mismatch - replay under the recorded platform in the "
		"recorded appearance, or record again; font-load-failed - pass "
		"readable font files; manifest-unreadable or manifest-schema - pass "
		"the written manifest."),
	"unverified-qt-build": (
		"The replay's qt.debug is missing or not false: the raster did not "
		"run in verified optimized Qt.",
		"Replay with the component built with -DCMAKE_BUILD_TYPE=Release; a "
		"Debug-Qt raster never enters a paint cost."),
	"qt-version-mismatch": (
		"Recorder and replayer report different qVersion().",
		sentence(SAME_QT)),
	"picture-format-mismatch": (
		"The QPicture stream format differs between recorder and replayer "
		"(the manifest's qt.pictureFormat, or a .qpic header).",
		sentence(SAME_QT)),
	"sidecar-version-mismatch": (
		"The sidecar version differs between recorder and replayer.",
		"Build the replay component from the recorder's test_paint_stream.h."),
	"platform-mismatch": (
		"Recorder and replayer ran under different platform plugins or color "
		"schemes, which rasterize text differently.",
		"Replay under the recorded platform in the recorded appearance, or "
		"record again."),
	"duplicate-record": (
		"Two manifest rows carry one stream name.",
		"Give every recording its own name; " + RECORD_AGAIN + "."),
	"duplicate-replay": (
		"Two replay rows carry one stream name.",
		"Replay the manifest once with the unmodified replayer."),
	"missing-replay": (
		"A recorded stream has no replay row.",
		"Replay the complete manifest; a stream left out is not measured."),
	"extra-replay": (
		"A replay row has no recorded stream.",
		"Replay exactly the manifest assessed."),
	"mismatched-identity": (
		"A stream's .qpic or .tables SHA-256 differs between the manifest "
		"and the bytes the replayer read.",
		"Replay the files the recorder wrote, unchanged; " + RECORD_AGAIN
		+ "."),
	"unknown-role": (
		"A manifest row has a role other than timed, absent, narrow or "
		"empty, such as a Debug baseline.",
		"Record only the four roles; a Debug baseline never composes with an "
		"optimized replay."),
	"invalid-stream-name": (
		"A stream name is not letters, digits, '_', '.' and '-'.",
		sentence(RECORD_AGAIN)),
	"missing-stream-file": (
		"The stream's .qpic file is missing.",
		sentence(KEEP_FILES)),
	"empty-stream-file": (
		"The stream's .qpic file is empty.",
		sentence(KEEP_FILES)),
	"missing-sidecar": (
		"The stream's .tables sidecar is missing.",
		sentence(KEEP_FILES)),
	"empty-sidecar": (
		"The stream's .tables sidecar is empty.",
		sentence(KEEP_FILES)),
	"stream-load-failed": (
		"QPicture::load() refused the .qpic header or checksum.",
		sentence(KEEP_FILES)),
	"sidecar-unreadable": (
		"The .tables sidecar does not parse.",
		sentence(KEEP_FILES)),
	"sidecar-count-mismatch": (
		"The parsed sidecar lists differ from the row's counts: the "
		"sidecar is not this stream's.",
		"Keep each stream with its own sidecar; " + RECORD_AGAIN + "."),
	"playback-failed": (
		"QPicture::play() returned false for the stream.",
		sentence(KEEP_FILES)),
	"row-error": (
		"The replayer reported a row error this assessor does not know "
		"(facts.error).",
		"Read the replayer's code and update the assessor with it."),
	"replay-differs-from-recording": (
		"The optimized replay's raster digest differs from the recorder's "
		"reference replay of the same stream: a sidecar, font or "
		"rasterization difference.",
		"Replay with every font file the recorder loaded and the complete "
		"sidecars; never add a tolerance."),
	"font-not-retained": (
		"The text fonts the replay resolves differ from the recorder's.",
		"Pass every font file the recorder used with --font."),
	"recording-not-faithful": (
		"A recording's direct render differs from its in-memory replay in "
		"the recorder (directDifference is not 0).",
		"Record an owner whose stream reproduces its paint; text that "
		"re-lays out differently cannot be measured this way."),
	"recording-fidelity-unmeasured": (
		"No timed row carries directDifference.",
		"Call PaintDirectDifference() on at least one timed recording."),
	"wrong-paint-root": (
		"A timed, absent or narrow row's root type or size is not the "
		"declared surface's.",
		"Record the declared owner itself, or declare the owner that was "
		"recorded."),
	"missing-timed-stream": (
		"The manifest has no timed row.",
		"Record the timed samples the claim is about."),
	"not-whole-owner": (
		"A timed or absent row does not cover every device pixel of the "
		"declared surface, or was recorded over a partial region.",
		"Record the whole opaque owner with RecordPaint's default flags, or "
		"DrawWindowBackground for a top-level window owner; a translucent "
		"child or a region cannot carry a whole-owner cost."),
	"missing-empty-control": (
		"No replayed empty control row.",
		"Record EmptyPaintRecording() and replay it."),
	"empty-changes-pixels": (
		"The empty control changed pixels.",
		"Investigate the replayer; the empty stream must change nothing."),
	"missing-absent-control": (
		"No replayed absent control row.",
		"Record the owner without the measured field, with pairedWith and "
		"fieldRect."),
	"absent-pair-invalid": (
		"The absent row's pairedWith is not a timed stream, or its replay "
		"pair is missing, failed or for another stream.",
		"Pair the absent row with the timed recording of the same state."),
	"absent-removes-nothing": (
		"Removing the field changed no pixel against its pair.",
		"Hide the measured field in the absent recording, and pair it with a "
		"recording that shows it."),
	"absent-outside-field": (
		"The absent pair's differing pixels reach outside the declared "
		"fieldRect (in device pixels), or no fieldRect is declared.",
		"Declare the field's owner-logical rectangle, and change only the "
		"field between the pair."),
	"missing-narrow-control": (
		"No replayed narrow control row.",
		"Record a region smaller than the owner as the narrow control."),
	"narrow-not-narrow": (
		"The narrow control covers the whole surface.",
		"Record the narrow control over a region smaller than the owner."),
	"sample-accounting": (
		"The replay's warmup or sample count differs from the declared one, "
		"a timed row lacks exactly the declared samples, or a non-timed row "
		"carries timings.",
		"Replay with the declared --warmup and --samples and assess with the "
		"same values."),
	"missing-record-cost": (
		"A timed row has no recordMs.",
		"Save recordings made by RecordPaint()."),
}
BOUNDARY = {
	"recording": (
		"recordMs: monotonic elapsed time (QElapsedTimer) of the painter "
		"session around QWidget::render() that emits the whole owner into an "
		"in-memory QPicture, in the recorder's Qt build; Debug Qt (Debug "
		"Telegram) over-estimates it"),
	"replay": (
		"timesNs: monotonic elapsed time of QPainter construction, "
		"QPicture::play() and painter end that raster the whole stream onto "
		"an ARGB32_Premultiplied target of the declared surface in gated "
		"optimized Qt, after the declared warmup; target fills, file loads, "
		"decoding, digests and encoding are outside"),
	"composition": (
		"envelopeMs = recordMs + the envelope quantile of the same stream's "
		"replay; this same-stream composition is the only one admitted, and "
		"no Debug raster term enters it"),
}
LIMITS = [
	"not a Release Telegram frame time: one recorded stream is replayed in "
	"a standalone process, without Telegram's event loop, layout, other "
	"widgets or backing-store flush",
	"not a compositor or presentation cadence: nothing is shown on screen, "
	"so frame pacing, display refresh and the window server are outside",
	"not a population or confidence bound: the order statistics describe "
	"this run's kept samples only, not other runs, sessions, hosts or a "
	"tail",
	"consecutive replays of one stream run with warm caches (glyphs, image "
	"conversions), which a live paint need not have",
	"text is laid out and shaped again at replay from the recorded text and "
	"font; a font missing on the replay side changes the raster and is "
	"refused",
	"opaque owners only: RecordPaint's default flags record a child owner "
	"without the palette Window fill, so a translucent child leaves device "
	"pixels uncovered and is refused as not-whole-owner; DrawWindowBackground, "
	"for a top-level window owner, records the fill that window paints, which "
	"covers the surface and is part of the stream and its timed cost",
	"one exact Qt version, picture format, sidecar version, platform plugin "
	"and color scheme on both sides, macOS only (paint-replay.md)",
]
MISSING = object()


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


def load_json(path):
	with open(path, "rb") as file:
		return json.loads(file.read().decode("utf-8"))


def read_input(path):
	try:
		with open(path, "rb") as file:
			data = file.read()
		value = json.loads(data.decode("utf-8"))
	except (OSError, UnicodeDecodeError, ValueError) as error:
		raise ValueError(f"cannot read {path}: {error}") from error
	return value, {
		"path": os.path.realpath(path),
		"bytes": len(data),
		"sha256": hashlib.sha256(data).hexdigest(),
	}


def stream_name(row):
	name = row.get("name") if isinstance(row, dict) else None
	return name if isinstance(name, str) and name else None


def identity_digest(row, key):
	value = row.get(key) if isinstance(row, dict) else None
	digest = value.get("sha256") if isinstance(value, dict) else None
	return digest if isinstance(digest, str) and digest else None


def group_by_name(rows):
	groups = {}
	unnamed = []
	for position, row in enumerate(rows or []):
		name = stream_name(row)
		if name is None:
			unnamed.append(position)
		else:
			groups.setdefault(name, []).append(row)
	return groups, unnamed


def account_streams(recorded, replayed):
	records, unnamed_records = group_by_name(recorded)
	replays, unnamed_replays = group_by_name(replayed)
	problems = []
	for name, rows in records.items():
		if len(rows) > 1:
			problems.append((
				"duplicate-record",
				{"stream": name, "count": len(rows)}))
	for name, rows in replays.items():
		if len(rows) > 1:
			problems.append((
				"duplicate-replay",
				{"stream": name, "count": len(rows)}))
	for name in records:
		if name not in replays:
			problems.append(("missing-replay", {"stream": name}))
	for position in unnamed_records:
		problems.append(("missing-replay", {"stream": None, "row": position}))
	for name in replays:
		if name not in records:
			problems.append(("extra-replay", {"stream": name}))
	for position in unnamed_replays:
		problems.append(("extra-replay", {"stream": None, "row": position}))
	for name, rows in records.items():
		for key in IDENTITY_KEYS:
			recorded_digests = {
				digest for digest in (identity_digest(row, key) for row in rows)
				if digest
			}
			replayed_digests = {
				digest for digest in (
					identity_digest(row, key) for row in replays.get(name, []))
				if digest
			}
			if (recorded_digests
				and replayed_digests
				and recorded_digests != replayed_digests):
				problems.append(("mismatched-identity", {
					"stream": name,
					"key": key,
					"recorded": sorted(recorded_digests),
					"replayed": sorted(replayed_digests),
				}))
	return problems


def quantile_fraction(q):
	if not is_number(q) or not 0 < q <= 1:
		raise ValueError(f"a quantile must be a number in (0, 1]: {q!r}")
	if isinstance(q, float):
		return fractions.Fraction(repr(q))
	return fractions.Fraction(q)


def sample_values(times_ns):
	if not isinstance(times_ns, (list, tuple)) or not times_ns:
		raise ValueError("timesNs must be a non-empty list of nanoseconds")
	for value in times_ns:
		if not is_number(value) or value < 0:
			raise ValueError(f"not a nanosecond value: {value!r}")
	return sorted(times_ns)


def milliseconds(nanoseconds):
	return round(nanoseconds / 1e6, 6)


def quantile(times_ns, q):
	values = sample_values(times_ns)
	count = len(values)
	rank = math.ceil(quantile_fraction(q) * count)
	return {
		"q": q,
		"valueMs": milliseconds(values[rank - 1]),
		"rank": rank,
		"n": count,
	}


def statistics(times_ns, quantiles):
	values = sample_values(times_ns)
	median = quantile(values, 0.5)
	return {
		"n": len(values),
		"minMs": milliseconds(values[0]),
		"medianMs": median["valueMs"],
		"medianRank": median["rank"],
		"maxMs": milliseconds(values[-1]),
		"quantiles": [quantile(values, q) for q in quantiles],
	}


def qt_round(value):
	return int(math.floor(value + 0.5))


def parse_surface(value):
	surface = as_dict(value)
	root_type = surface.get("rootType")
	size = surface.get("size")
	dpr = surface.get("dpr")
	if (not isinstance(root_type, str)
		or not root_type
		or not isinstance(size, list)
		or len(size) != 2
		or not all(is_int(side) and side > 0 for side in size)
		or not is_number(dpr)
		or not dpr > 0):
		return None
	device = [qt_round(size[0] * dpr), qt_round(size[1] * dpr)]
	return {
		"rootType": root_type,
		"size": list(size),
		"dpr": dpr,
		"deviceSize": device,
		"devicePixels": device[0] * device[1],
	}


def is_rect(value):
	return (isinstance(value, list)
		and len(value) == 4
		and all(is_int(item) for item in value)
		and value[2] >= 0
		and value[3] >= 0)


def device_rect(rect, dpr):
	if (not isinstance(rect, list)
		or len(rect) != 4
		or not all(is_number(item) for item in rect)
		or not rect[2] > 0
		or not rect[3] > 0):
		return None
	left = math.floor(rect[0] * dpr)
	top = math.floor(rect[1] * dpr)
	right = math.ceil((rect[0] + rect[2]) * dpr)
	bottom = math.ceil((rect[1] + rect[3]) * dpr)
	return [left, top, right - left, bottom - top]


def rect_inside(inner, outer):
	return (inner[0] >= outer[0]
		and inner[1] >= outer[1]
		and inner[0] + inner[2] <= outer[0] + outer[2]
		and inner[1] + inner[3] <= outer[1] + outer[3])


def font_set(value):
	if not isinstance(value, list):
		return None
	result = []
	for item in value:
		item = as_dict(item)
		family = item.get("family")
		resolved = item.get("resolved")
		if not isinstance(family, str) or not isinstance(resolved, str):
			return None
		result.append((family, resolved))
	return sorted(set(result))


def declared_options(options):
	options = as_dict(options)
	host_arch = options.get("hostArch")
	warmup = options.get("warmup")
	samples = options.get("samples")
	quantiles = options.get("quantiles")
	envelope = options.get("envelopeQuantile")
	if host_arch not in native_exec.ARCHITECTURES:
		raise ValueError(f"hostArch must be one of {native_exec.ARCHITECTURES}")
	if not is_int(warmup) or warmup < 0:
		raise ValueError(f"warmup must be an integer >= 0: {warmup!r}")
	if not is_int(samples) or samples < 1:
		raise ValueError(f"samples must be an integer >= 1: {samples!r}")
	if not isinstance(quantiles, (list, tuple)) or not quantiles:
		raise ValueError("quantiles must be a non-empty list")
	for q in list(quantiles) + [envelope]:
		quantile_fraction(q)
	if envelope not in quantiles:
		raise ValueError("envelopeQuantile must be one of the quantiles")
	digest = options.get("manifestSha256")
	return {
		"hostArch": host_arch,
		"warmup": warmup,
		"samples": samples,
		"quantiles": list(quantiles),
		"envelopeQuantile": envelope,
		"manifestSha256": digest if isinstance(digest, str) else None,
	}


class Context:
	def __init__(self, manifest, replay, gate, declared):
		self.manifest_raw = manifest
		self.replay_raw = replay
		self.manifest = as_dict(manifest)
		self.replay = as_dict(replay)
		self.gate = gate
		self.declared = declared
		self.manifest_qt = as_dict(self.manifest.get("qt"))
		self.replay_qt = as_dict(self.replay.get("qt"))
		self.surface = parse_surface(self.manifest.get("surface"))
		self.records, self.records_complete = self.rows(
			self.manifest,
			"streams")
		self.replays, self.replays_complete = self.rows(self.replay, "rows")
		self.record_by_name = {}
		for row in self.records:
			name = stream_name(row)
			if name is not None and name not in self.record_by_name:
				self.record_by_name[name] = row
		self.replay_by_name = {}
		for row in self.replays:
			name = stream_name(row)
			if name is not None and name not in self.replay_by_name:
				self.replay_by_name[name] = row

	@staticmethod
	def rows(container, key):
		value = container.get(key)
		if not isinstance(value, list):
			return [], False
		rows = [row for row in value if isinstance(row, dict)]
		return rows, len(rows) == len(value)

	def role_of(self, name):
		return as_dict(self.record_by_name.get(name)).get("role")

	def named(self, roles):
		return [
			(name, row) for name, row in self.record_by_name.items()
			if row.get("role") in roles
		]

	def usable(self, name):
		row = self.replay_by_name.get(name)
		return row if row is not None and "error" not in row else None

	def whole_region(self):
		if self.surface is None:
			return None
		return [0, 0] + self.surface["size"]

	def device_pixels(self):
		return self.surface["devicePixels"] if self.surface else None


def check_inputs(ctx):
	failures = Failures()
	if not isinstance(ctx.manifest_raw, dict):
		failures.add("manifest-schema", {"problem": "not a JSON object"})
	else:
		schema = ctx.manifest.get("schema")
		if not is_int(schema) or schema != MANIFEST_SCHEMA:
			failures.add("manifest-schema", {"problem": f"schema is {schema!r}"})
		if not isinstance(ctx.manifest.get("qt"), dict):
			failures.add("manifest-schema", {"problem": "qt is not an object"})
		if ctx.surface is None:
			failures.add("manifest-schema", {
				"problem": "surface is not {rootType, size [w, h], dpr}",
			})
		if not ctx.records_complete:
			failures.add("manifest-schema", {
				"problem": "streams is not a list of objects",
			})
	if not isinstance(ctx.replay_raw, dict):
		failures.add("replay-schema", {"problem": "not a JSON object"})
	else:
		tool = ctx.replay.get("tool")
		schema = ctx.replay.get("schema")
		if tool != REPLAY_TOOL:
			failures.add("replay-schema", {"problem": f"tool is {tool!r}"})
		if not is_int(schema) or schema != REPLAY_SCHEMA:
			failures.add("replay-schema", {"problem": f"schema is {schema!r}"})
		if "error" not in ctx.replay and not ctx.replays_complete:
			failures.add("replay-schema", {
				"problem": "rows is not a list of objects",
			})
	replayed = as_dict(ctx.replay.get("manifest"))
	expected = ctx.declared["manifestSha256"]
	if not expected or replayed.get("sha256") != expected:
		failures.add("replay-manifest-mismatch", {
			"assessed": expected,
			"replayed": replayed.get("sha256"),
			"replayedPath": replayed.get("path"),
		})
	facts = {
		"manifestStreams": len(ctx.records),
		"replayRows": len(ctx.replays),
		"manifestSha256": expected,
	}
	return facts, failures


def check_gate(ctx):
	failures = Failures()
	host_arch = ctx.declared["hostArch"]
	executable = ctx.replay.get("executable")
	gate = as_dict(ctx.gate)
	facts = {
		"replayExecutable": executable,
		"replayStartedAt": ctx.replay.get("startedAt"),
	}
	if ctx.gate is None:
		failures.add("missing-gate", {})
	else:
		for problem in native_exec.check_verdict(ctx.gate, executable):
			failures.add(problem["code"], {"message": problem["message"]})
		declared = as_dict(gate.get("host")).get("declaredArch")
		finished = gate.get("finishedAt")
		started = ctx.replay.get("startedAt")
		facts.update({
			"gateVerdict": gate.get("verdict"),
			"gateReason": gate.get("reason"),
			"gatePath": as_dict(gate.get("executable")).get("path"),
			"gateDeclaredArch": declared,
			"gateFinishedAt": finished,
		})
		if declared != host_arch:
			failures.add("gate-host-arch", {
				"gate": declared,
				"declared": host_arch,
			})
		if not (is_int(finished) and is_int(started) and finished <= started):
			failures.add("gate-after-replay", {
				"gateFinishedAt": finished,
				"replayStartedAt": started,
			})
	native = as_dict(ctx.replay.get("native"))
	facts["replayNative"] = native or None
	if (native.get("buildArch") != host_arch
		or native.get("runningArch") != host_arch
		or native.get("translated") is not False):
		failures.add("replay-not-native", {
			"native": native or None,
			"declared": host_arch,
		})
	return facts, failures


def check_replay_build(ctx):
	failures = Failures()
	error = ctx.replay.get("error")
	if "error" in ctx.replay:
		failures.add("replay-fatal", {
			"error": error,
			"detail": ctx.replay.get("errorDetail"),
		})
	debug = ctx.replay_qt.get("debug", MISSING)
	if debug is not False:
		failures.add("unverified-qt-build", {
			"debug": None if debug is MISSING else debug,
			"present": debug is not MISSING,
		})
	facts = {
		"replayError": error,
		"replayQtDebug": None if debug is MISSING else debug,
		"recorderQtDebug": ctx.manifest_qt.get("debug"),
	}
	return facts, failures


def check_qt_contract(ctx):
	failures = Failures()
	for key, code in CONTRACT_KEYS:
		recorded = ctx.manifest_qt.get(key)
		replayed = ctx.replay_qt.get(key)
		if recorded is None or replayed is None or recorded != replayed:
			failures.add(code, {
				"key": key,
				"recorder": recorded,
				"replayer": replayed,
			})
	keys = [key for key, _ in CONTRACT_KEYS]
	facts = {
		"recorder": {key: ctx.manifest_qt.get(key) for key in keys},
		"replayer": {key: ctx.replay_qt.get(key) for key in keys},
	}
	return facts, failures


def check_accounting(ctx):
	failures = Failures()
	for code, detail in account_streams(ctx.records, ctx.replays):
		failures.add(code, detail)
	facts = {
		"recorded": len(ctx.records),
		"replayed": len(ctx.replays),
		"keys": list(IDENTITY_KEYS),
	}
	return facts, failures


def check_roles(ctx):
	failures = Failures()
	counts = {}
	for row in ctx.records:
		role = row.get("role")
		counts[str(role)] = counts.get(str(role), 0) + 1
		if role not in ROLES:
			failures.add("unknown-role", {
				"stream": stream_name(row),
				"role": role,
			})
	return {"roles": counts}, failures


def check_streams(ctx):
	failures = Failures()
	compared = 0
	for name, record in ctx.record_by_name.items():
		row = ctx.replay_by_name.get(name)
		if row is None:
			continue
		if "error" in row:
			error = row["error"]
			failures.add(error if error in ROW_ERRORS else "row-error", {
				"stream": name,
				"error": error,
			})
			continue
		compared += 1
		reference = record.get("referenceSha256")
		replayed = row.get("replaySha256")
		if (not isinstance(reference, str)
			or not reference
			or replayed != reference):
			failures.add("replay-differs-from-recording", {
				"stream": name,
				"referenceSha256": reference,
				"replaySha256": replayed,
			})
		recorded_fonts = font_set(record.get("fonts"))
		replayed_fonts = font_set(row.get("fonts"))
		if (recorded_fonts is None
			or replayed_fonts is None
			or recorded_fonts != replayed_fonts):
			failures.add("font-not-retained", {
				"stream": name,
				"recorded": record.get("fonts"),
				"replayed": row.get("fonts"),
			})
	return {"compared": compared}, failures


def check_recording_fidelity(ctx):
	failures = Failures()
	measured = []
	for name, record in ctx.record_by_name.items():
		if "directDifference" not in record:
			continue
		value = record["directDifference"]
		if not is_int(value) or value != 0:
			failures.add("recording-not-faithful", {
				"stream": name,
				"directDifference": value,
			})
		if record.get("role") == "timed" and is_int(value) and value >= 0:
			measured.append(name)
	if not measured:
		failures.add("recording-fidelity-unmeasured", {
			"timedStreams": len(ctx.named(("timed",))),
		})
	return {"measured": measured}, failures


def check_paint_root(ctx):
	failures = Failures()
	surface = ctx.surface
	declared = {
		"type": surface["rootType"] if surface else None,
		"size": surface["size"] if surface else None,
	}
	for name, record in ctx.named(ROOTED_ROLES):
		root = as_dict(record.get("root"))
		if (surface is None
			or root.get("type") != surface["rootType"]
			or root.get("size") != surface["size"]):
			failures.add("wrong-paint-root", {
				"stream": name,
				"type": root.get("type"),
				"size": root.get("size"),
			})
	return {"declared": declared}, failures


def check_whole_owner(ctx):
	failures = Failures()
	expected = ctx.device_pixels()
	whole = ctx.whole_region()
	rows = []
	if not ctx.named(("timed",)):
		failures.add("missing-timed-stream", {})
	for name, record in ctx.named(WHOLE_ROLES):
		row = ctx.usable(name)
		if row is None:
			continue
		coverage = row.get("coveragePixels")
		surface_pixels = row.get("surfacePixels")
		region = as_dict(record.get("root")).get("region")
		rows.append({
			"stream": name,
			"coveragePixels": coverage,
			"surfacePixels": surface_pixels,
		})
		if (expected is None
			or not is_int(coverage)
			or coverage != expected
			or surface_pixels != expected
			or region != whole):
			failures.add("not-whole-owner", {
				"stream": name,
				"coveragePixels": coverage,
				"surfacePixels": surface_pixels,
				"declaredPixels": expected,
				"region": region,
				"wholeRegion": whole,
			})
	return {"declaredPixels": expected, "rows": rows}, failures


def control_rows(ctx, role):
	declared = ctx.named((role,))
	usable = [
		(name, record, ctx.usable(name)) for name, record in declared
		if ctx.usable(name) is not None
	]
	return declared, usable


def check_empty_control(ctx):
	failures = Failures()
	declared, usable = control_rows(ctx, "empty")
	if not usable:
		failures.add("missing-empty-control", {"manifestRows": len(declared)})
	rows = []
	for name, _, row in usable:
		changed = row.get("changedPixels")
		rows.append({"stream": name, "changedPixels": changed})
		if not is_int(changed) or changed != 0:
			failures.add("empty-changes-pixels", {
				"stream": name,
				"changedPixels": changed,
			})
	return {"rows": rows}, failures


def check_absent_control(ctx):
	failures = Failures()
	declared, usable = control_rows(ctx, "absent")
	timed = {name for name, _ in ctx.named(("timed",))}
	if not usable:
		failures.add("missing-absent-control", {"manifestRows": len(declared)})
	rows = []
	for name, record, row in usable:
		paired = record.get("pairedWith")
		pair = row.get("pair")
		field = (device_rect(record.get("fieldRect"), ctx.surface["dpr"])
			if ctx.surface else None)
		rows.append({
			"stream": name,
			"pairedWith": paired,
			"pair": pair,
			"fieldRect": record.get("fieldRect"),
			"fieldDeviceRect": field,
		})
		problems = []
		if not isinstance(paired, str) or paired not in timed:
			problems.append("pairedWith does not name a timed stream")
		shaped = (isinstance(pair, dict)
			and "error" not in pair
			and pair.get("with") == paired
			and is_int(pair.get("differingPixels"))
			and pair["differingPixels"] >= 0
			and is_rect(pair.get("bounds")))
		if not shaped:
			problems.append("the replay pair is missing, failed or for another "
				"stream")
		if problems:
			failures.add("absent-pair-invalid", {
				"stream": name,
				"pairedWith": paired,
				"pair": pair,
				"problems": problems,
			})
		if not shaped:
			continue
		if pair["differingPixels"] == 0:
			failures.add("absent-removes-nothing", {
				"stream": name,
				"pairedWith": paired,
			})
		elif field is None or not rect_inside(pair["bounds"], field):
			failures.add("absent-outside-field", {
				"stream": name,
				"bounds": pair["bounds"],
				"fieldRect": record.get("fieldRect"),
				"fieldDeviceRect": field,
			})
	return {"rows": rows}, failures


def check_narrow_control(ctx):
	failures = Failures()
	declared, usable = control_rows(ctx, "narrow")
	expected = ctx.device_pixels()
	if not usable:
		failures.add("missing-narrow-control", {"manifestRows": len(declared)})
	rows = []
	for name, _, row in usable:
		coverage = row.get("coveragePixels")
		surface_pixels = row.get("surfacePixels")
		rows.append({
			"stream": name,
			"coveragePixels": coverage,
			"surfacePixels": surface_pixels,
		})
		if (expected is None
			or not is_int(coverage)
			or surface_pixels != expected
			or coverage >= expected):
			failures.add("narrow-not-narrow", {
				"stream": name,
				"coveragePixels": coverage,
				"surfacePixels": surface_pixels,
				"declaredPixels": expected,
			})
	return {"rows": rows}, failures


def valid_times(times, samples):
	if not isinstance(times, list) or len(times) != samples:
		return False
	return all(is_number(value) and value >= 0 for value in times)


def check_timing(ctx):
	failures = Failures()
	declared = ctx.declared
	warmup = ctx.replay.get("warmup")
	samples = ctx.replay.get("samples")
	if (not is_int(warmup)
		or not is_int(samples)
		or warmup != declared["warmup"]
		or samples != declared["samples"]):
		failures.add("sample-accounting", {
			"problem": "the replay's warmup or samples differ from the "
				"declared ones",
			"declared": {
				"warmup": declared["warmup"],
				"samples": declared["samples"],
			},
			"replay": {"warmup": warmup, "samples": samples},
		})
	for name, record in ctx.named(("timed",)):
		row = ctx.usable(name)
		if row is not None and not valid_times(
				row.get("timesNs"),
				declared["samples"]):
			times = row.get("timesNs")
			failures.add("sample-accounting", {
				"stream": name,
				"problem": "the timed row does not carry exactly the declared "
					"samples",
				"samples": len(times) if isinstance(times, list) else None,
			})
		cost = record.get("recordMs")
		if not is_number(cost) or cost < 0:
			failures.add("missing-record-cost", {
				"stream": name,
				"recordMs": cost,
			})
	for row in ctx.replays:
		name = stream_name(row)
		role = ctx.role_of(name)
		if role != "timed" and "timesNs" in row:
			failures.add("sample-accounting", {
				"stream": name,
				"role": role,
				"problem": "timings on a row that is not a recorded timed "
					"stream",
			})
	facts = {
		"declared": {
			"warmup": declared["warmup"],
			"samples": declared["samples"],
		},
		"replay": {"warmup": warmup, "samples": samples},
	}
	return facts, failures


CHECKS = (
	("inputs", check_inputs),
	("gate", check_gate),
	("replay-build", check_replay_build),
	("qt-contract", check_qt_contract),
	("accounting", check_accounting),
	("roles", check_roles),
	("streams", check_streams),
	("recording-fidelity", check_recording_fidelity),
	("paint-root", check_paint_root),
	("whole-owner", check_whole_owner),
	("empty-control", check_empty_control),
	("absent-control", check_absent_control),
	("narrow-control", check_narrow_control),
	("timing", check_timing),
)


def refusal_message(code, details):
	meaning, action = REFUSALS.get(code, (None, None))
	if meaning is None:
		messages = [
			detail.get("message") for detail in details
			if isinstance(detail, dict) and detail.get("message")
		]
		return " ".join(messages) or code
	return f"{meaning} {action}"


def run_checks(ctx):
	checks = []
	for name, check in CHECKS:
		facts, failures = check(ctx)
		if not failures.items:
			checks.append({
				"check": name,
				"pass": True,
				"reason": None,
				"message": None,
				"facts": facts,
			})
			continue
		for code, details in failures.items.items():
			checks.append({
				"check": name,
				"pass": False,
				"reason": code,
				"message": refusal_message(code, details),
				"facts": dict(facts, details=details),
			})
	return checks


def stream_reports(ctx):
	declared = ctx.declared
	reports = []
	for name, record in ctx.named(("timed",)):
		row = ctx.usable(name)
		cost = record.get("recordMs")
		entry = {
			"name": name,
			"window": record.get("window"),
			"recordMs": cost,
			"recorderDebug": ctx.manifest_qt.get("debug"),
			"replayerDebug": ctx.replay_qt.get("debug"),
			"replay": None,
			"envelopeQuantile": None,
			"envelopeMs": None,
		}
		try:
			values = sample_values(row.get("timesNs") if row else None)
		except ValueError:
			reports.append(entry)
			continue
		envelope = quantile(values, declared["envelopeQuantile"])
		entry["replay"] = statistics(values, declared["quantiles"])
		entry["envelopeQuantile"] = envelope
		if is_number(cost):
			entry["envelopeMs"] = round(cost + envelope["valueMs"], 6)
		reports.append(entry)
	return reports


def control_reports(ctx):
	def rows(role, keys):
		result = []
		for name, record in ctx.named((role,)):
			row = as_dict(ctx.replay_by_name.get(name))
			entry = {"name": name, "error": row.get("error")}
			for key in keys:
				entry[key] = row.get(key, record.get(key))
			result.append(entry)
		return result

	return {
		"surface": ctx.surface,
		"timed": rows("timed", ("coveragePixels", "surfacePixels")),
		"absent": [
			dict(entry, fieldDeviceRect=(
				device_rect(entry.get("fieldRect"), ctx.surface["dpr"])
				if ctx.surface else None))
			for entry in rows("absent", (
				"coveragePixels",
				"surfacePixels",
				"pairedWith",
				"fieldRect",
				"pair",
			))
		],
		"narrow": rows("narrow", ("coveragePixels", "surfacePixels")),
		"empty": rows("empty", ("changedPixels",)),
		"fidelity": [
			{"name": name, "directDifference": record.get("directDifference")}
			for name, record in ctx.named(("timed",))
			if "directDifference" in record
		],
	}


def scope_report(declared):
	return {
		"statistics": (
			"empirical nearest-rank order statistics of this run: the value "
			"of rank ceil(q * n), 1-based, among the n kept replays of one "
			"stream; medianMs is the q = 0.5 rank"),
		"samples": (
			f"{declared['samples']} consecutive replays of each timed stream "
			f"in one process onto one reused target, after "
			f"{declared['warmup']} discarded warmup replays"),
		"claim": (
			"per timed stream: its own recording cost plus its own replay "
			"order statistic, when allPassed; nothing about other streams, "
			"frames, runs or hosts, and no number of a refused assessment is "
			"evidence"),
		"quantiles": declared["quantiles"],
		"envelopeQuantile": declared["envelopeQuantile"],
	}


def assess(manifest, replay, gate, options):
	declared = declared_options(options)
	ctx = Context(manifest, replay, gate, declared)
	checks = run_checks(ctx)
	reasons = []
	for check in checks:
		if not check["pass"] and check["reason"] not in reasons:
			reasons.append(check["reason"])
	return {
		"schema": SCHEMA,
		"tool": TOOL,
		"allPassed": not reasons,
		"reasons": reasons,
		"checks": checks,
		"declared": {
			"hostArch": declared["hostArch"],
			"warmup": declared["warmup"],
			"samples": declared["samples"],
			"quantiles": declared["quantiles"],
			"envelopeQuantile": declared["envelopeQuantile"],
		},
		"qt": {
			"recorder": ctx.manifest_qt or None,
			"replayer": ctx.replay_qt or None,
		},
		"streams": stream_reports(ctx),
		"controls": control_reports(ctx),
		"boundary": BOUNDARY,
		"scope": scope_report(declared),
		"limits": LIMITS,
	}


def quantile_argument(text):
	try:
		value = float(text)
	except ValueError as error:
		raise argparse.ArgumentTypeError(f"not a number: {text!r}") from error
	if not math.isfinite(value) or not 0 < value <= 1:
		raise argparse.ArgumentTypeError(f"must lie in (0, 1]: {text!r}")
	return value


def count_argument(minimum):
	def parse(text):
		try:
			value = int(text)
		except ValueError as error:
			raise argparse.ArgumentTypeError(
				f"not an integer: {text!r}") from error
		if value < minimum:
			raise argparse.ArgumentTypeError(f"must be >= {minimum}: {text!r}")
		return value

	return parse


def parse_args(argv=None):
	parser = argparse.ArgumentParser(
		description=(
			"Assess one paint replay: the manifest test_paint_stream.h wrote, "
			"the result of the gated optimized paint_replay run over it and "
			"the native_exec.py verdict for that replayer. Exit 0 when every "
			"check passes, 1 on a refusal, 2 on a usage or input error."))
	parser.add_argument("--manifest", required=True,
		help="manifest.json written by WritePaintManifest()")
	parser.add_argument("--replay", required=True,
		help="the paint_replay --out result")
	parser.add_argument("--gate",
		help="native_exec.py verdict for the replay executable")
	parser.add_argument("--host-arch", required=True,
		choices=native_exec.ARCHITECTURES,
		help="architecture this host runs natively (`uname -m`)")
	parser.add_argument("--warmup", required=True, type=count_argument(0),
		help="declared warmup replays per timed stream")
	parser.add_argument("--samples", required=True, type=count_argument(1),
		help="declared kept replays per timed stream")
	parser.add_argument("--quantile", action="append", required=True,
		type=quantile_argument, dest="quantiles",
		help="reported nearest-rank quantile in (0, 1]; repeat for more")
	parser.add_argument("--envelope-quantile", required=True,
		type=quantile_argument,
		help="the --quantile added to recordMs in envelopeMs")
	parser.add_argument("--out",
		help="also write the assessment JSON to this file")
	options = parser.parse_args(argv)
	unique = []
	for value in options.quantiles:
		if value not in unique:
			unique.append(value)
	options.quantiles = unique
	if options.envelope_quantile not in options.quantiles:
		parser.error("--envelope-quantile must be one of the --quantile values")
	if options.out:
		target = os.path.realpath(options.out)
		for path in (options.manifest, options.replay, options.gate):
			if path and os.path.realpath(path) == target:
				parser.error("--out must not name an input file")
	return options


def main(argv=None):
	options = parse_args(argv)
	try:
		manifest, manifest_identity = read_input(options.manifest)
		replay, replay_identity = read_input(options.replay)
		gate, gate_identity = (read_input(options.gate)
			if options.gate else (None, None))
	except ValueError as error:
		print(f"error: {error}", file=sys.stderr)
		return 2
	result = assess(manifest, replay, gate, {
		"hostArch": options.host_arch,
		"warmup": options.warmup,
		"samples": options.samples,
		"quantiles": options.quantiles,
		"envelopeQuantile": options.envelope_quantile,
		"manifestSha256": manifest_identity["sha256"],
	})
	result["inputs"] = {
		"manifest": manifest_identity,
		"replay": replay_identity,
		"gate": gate_identity,
	}
	text = json.dumps(result, indent=2, sort_keys=True)
	print(text)
	if options.out:
		try:
			with open(options.out, "w", encoding="utf-8") as file:
				file.write(text + "\n")
		except OSError as error:
			print(f"error: cannot write {options.out}: {error}", file=sys.stderr)
			return 2
	return 0 if result["allPassed"] else 1


if __name__ == "__main__":
	sys.exit(main())
