#!/usr/bin/env python3
"""Independent ELF fixtures; run with -B and optionally ELF_IDENTITY_TEST_ROOT."""

import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import elf_identity as subject

HELPER = Path(__file__).with_name("elf_identity.py")
TEST_ROOT = Path(os.environ.get("ELF_IDENTITY_TEST_ROOT")
	or tempfile.mkdtemp(prefix="elf-identity-tests-"))
TEST_ROOT.mkdir(parents=True, exist_ok=True)

def _wait_command(process, deadline, arguments, timeout):
	while os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT) is None:
		remaining = deadline - time.monotonic()
		if remaining <= 0:
			raise subprocess.TimeoutExpired(arguments, timeout)
		time.sleep(min(0.01, remaining))

def _live_command_group(pgid):
	result = []
	for path in Path("/proc").iterdir():
		if not path.name.isdecimal():
			continue
		try:
			fields = (path / "stat").read_text().rsplit(")", 1)[1].split()
		except (FileNotFoundError, ProcessLookupError):
			continue
		if int(fields[2]) == pgid and int(fields[3]) == pgid and fields[0] not in ("Z", "X"):
			result.append({"pid": int(path.name), "start_ticks": int(fields[19]), "state": fields[0]})
	return result

def _cleanup_command(process):
	"""Keep the unreaped session leader as the group identity anchor until signals finish."""
	result = {"started": True, "signals": [], "errors": [], "remaining": [],
		"direct_child_reaped": False, "started_unix_ns": time.time_ns()}
	try:
		for sig, budget in ((signal.SIGTERM, 0.25), (signal.SIGKILL, 2.0)):
			try:
				os.killpg(process.pid, sig)
				result["signals"].append(sig.name)
			except ProcessLookupError:
				break
			deadline = time.monotonic() + budget
			while True:
				result["remaining"] = _live_command_group(process.pid)
				if not result["remaining"] or time.monotonic() >= deadline:
					break
				time.sleep(0.01)
			if not result["remaining"]:
				break
		if result["remaining"]:
			result["errors"].append("owned group still has live members after escalation")
	except BaseException as error:
		result["errors"].append(f"group cleanup: {type(error).__name__}: {error}")
		try:
			os.killpg(process.pid, signal.SIGKILL)
			result["signals"].append("SIGKILL")
		except ProcessLookupError:
			pass
		except OSError as error:
			result["errors"].append(f"emergency group kill: {error}")
	try:
		process.wait(timeout=1.0)
		result["direct_child_reaped"] = True
	except BaseException as error:
		result["errors"].append(f"direct child reap: {type(error).__name__}: {error}")
	result["remaining_owner"] = ({"pid": process.pid, "pgid": process.pid}
		if result["errors"] else None)
	result["finished_unix_ns"] = time.time_ns()
	return result

def packed(data, offset, format, value):
	result = bytearray(data)
	struct.pack_into("<" + format, result, offset, value)
	return result

def fixture(*, stripped=False, repack=False, reorder=False, compressed=False,
		dynamic=True, overrides=None, symbols=None, omit=(), names=None, table_offset=None):
	"""Literal ABI fixture specification, independent of every production layout."""
	base = 0x400000
	sections = [
		[".text", 1, 6, base + 0x300, 0x300, 16, 0, 0, 16, 0, b"\x90" * 15 + b"\xc3"],
		[".data", 1, 3, base + 0x420, 0x420, 16, 0, 0, 8, 0, b"data payload!!!!"],
		[".tdata", 1, 0x403, base + 0x440, 0x440, 8, 0, 0, 8, 0, b"TLS-init"],
		[".tbss", 8, 0x403, base + 0x448, 0x448, 24, 0, 0, 8, 0, b""],
		[".overlap", 1, 3, base + 0x450, 0x450, 8, 0, 0, 8, 0, b"ordinary"],
		[".bss", 8, 3, base + 0x600, 0x600, 128, 0, 0, 16, 0, b""],
		[".empty", 8, 3, base + 0x610, 0x600, 0, 0, 0, 1, 0, b""],
		[".dynstr", 3, 2, base + 0x480, 0x480, 9, 0, 0, 1, 0, b"\0dynamic\0"],
		[".dynsym", 11, 2, base + 0x4a0, 0x4a0, 48, ".dynstr", 1, 8, 24, b""],
		[".refs", 1, 0xc2, base + 0x510, 0x510, 8, ".data", ".text", 8, 0, b"refsdata"],
		[".version", 0x6ffffffe, 2, base + 0x530, 0x530, 16, ".dynstr", 1, 8, 0, bytes(16)],
		[".outside_note", 7, 0, 0, 0x800, 16, 0, 0, 4, 0, b"outsideLOAD-note"],
		[".unknown", 1, 0, 0, 0x900, 8, 0, 0, 1, 0, b"keep-me!"],
		[".comment", 1, 0, 0, 0x920, 8, 0, 0, 1, 0, b"compiler"],
		[".gnu.build.attributes", 7, 0, 0, 0x940, 16, 0, 0, 4, 0, b"build-attributes"],
		[".debug_info", 1, 0, 0, 0xa00, 32, 0, 0, 1, 0, b"D" * 32],
		[".symtab", 2, 0, 0, 0xb00, 0, ".strtab", 4, 8, 24, b""],
		[".strtab", 3, 0, 0, 0xc00, 0, 0, 0, 1, 0, b""],
		[".shstrtab", 3, 0, 0, 0xd00, 0, 0, 0, 1, 0, b""],
	]
	if compressed:
		sections[15][2] = 0x800
		sections[15][10] = struct.pack("<IIQQ", 1, 0, 123, 1) + b"not-zlib"
	if symbols is None:
		symbols = [
			[b"", 0, 0, 0, 0, 0],
			[b"unit.cpp", 4, 0, 0xfff1, 0, 0],
			[b"", 3, 0, ".debug_info", 0, 0],
			[b"object", 1, 0, ".data", base + 0x420, 8],
			[b"function", 0x12, 0, ".text", base + 0x300, 16],
		]
	else:
		symbols = copy.deepcopy(symbols)
	if stripped:
		omit = tuple(omit) + (".debug_info",)
		symbols = [entry for index, entry in enumerate(symbols) if index not in (1, 2)]
	sections = [row for row in sections if row[0] not in omit
		and (dynamic or row[0] != ".dynsym")]
	if reorder:
		sections = sections[:1] + list(reversed(sections[1:]))
	indexes = {row[0]: index for index, row in enumerate(sections, 1)}
	by_name = {row[0]: row for row in sections}
	string_data = bytearray(b"\0unused bookkeeping\0" if repack else b"\0")
	name_offsets = {b"": 0}
	for symbol in reversed(symbols) if repack else symbols:
		name = symbol[0]
		if name not in name_offsets:
			name_offsets[name] = len(string_data)
			string_data.extend(name + b"\0")
	if ".strtab" in by_name:
		by_name[".strtab"][5] = len(string_data)
		by_name[".strtab"][10] = string_data
	if ".symtab" in by_name:
		symbol_bytes = b"".join(struct.pack("<IBBHQQ", name_offsets[name],
			info, other, indexes[target] if isinstance(target, str) else target,
			value, size) for name, info, other, target, value, size in symbols)
		by_name[".symtab"][5] = len(symbol_bytes)
		by_name[".symtab"][7] = sum(info >> 4 == 0 for _, info, *_ in symbols)
		by_name[".symtab"][10] = symbol_bytes
	if dynamic:
		by_name[".dynsym"][10] = bytes(24) + struct.pack("<IBBHQQ",
			1, 0x12, 0, 1, base + 0x300, 16)
	shstrings = bytearray(b"\0")
	section_names = {}
	for row in reversed(sections) if repack else sections:
		section_names[row[0]] = len(shstrings)
		shstrings.extend((names or {}).get(row[0], row[0].encode()) + b"\0")
	by_name[".shstrtab"][5] = len(shstrings)
	by_name[".shstrtab"][10] = shstrings
	for row in sections:
		if repack and row[4] >= 0x900:
			row[4] += 0x1000
		for field, value in (overrides or {}).get(row[0], {}).items():
			row[field] = value
	shoff = table_offset or (0x2400 if repack else 0x1000)
	data = bytearray(max(shoff + (len(sections) + 1) * 64,
		max(row[4] + len(row[10]) for row in sections)))
	ident = b"\x7fELF\x02\x01\x01" + bytes(9)
	data[:64] = struct.pack("<16sHHIQQQIHHHHHH", ident, 2, 62, 1,
		base + 0x300, 64, shoff, 0, 64, 56, 6, 64,
		len(sections) + 1, indexes[".shstrtab"])
	programs = [
		(6, 4, 64, base + 64, base + 64, 336, 336, 8),
		(1, 7, 0, base, base, 0x600, 0x680, 0x1000),
		(7, 4, 0x440, base + 0x440, base + 0x440, 8, 32, 8),
		(4, 4, 0x800, 0, 0, 16, 16, 4),
		(0x6474e552, 4, 0x420, base + 0x420, base + 0x420, 64, 64, 1),
		(0x6474e551, 6, 0, 0, 0, 0, 0, 16),
	]
	for index, entry in enumerate(programs):
		struct.pack_into("<IIQQQQQQ", data, 64 + 56 * index, *entry)
	for row in sections:
		name, kind, flags, address, offset, size, link, info, align, entry, payload = row
		if payload:
			data[offset:offset + len(payload)] = payload
		struct.pack_into("<IIQQQQIIQQ", data, shoff + indexes[name] * 64,
			section_names[name], kind, flags, address, offset, size,
			indexes[link] if isinstance(link, str) else link,
			indexes[info] if isinstance(info, str) else info, align, entry)
	return data, {
		"shoff": shoff, "indexes": indexes,
		"headers": {name: shoff + index * 64 for name, index in indexes.items()},
		"payloads": {row[0]: row[4] for row in sections},
		"name_offsets": section_names, "symbols": symbols,
	}

def page_fixture(*, prefix=False, bss=0, writable=False, value=7,
		names_offset=None, shoff=0x1000, extra=(), interpreter=False):
	base = 0x400000
	names_offset = (0x80 if prefix else 0x200) if names_offset is None else names_offset
	data = bytearray(max(shoff + 3 * 64, names_offset + 19))
	code = (bytes.fromhex("bf07000000b83c0000000f05") if interpreter else
		bytes.fromhex("0fb63d") + struct.pack("<i", names_offset + 17 - 0x107)
		+ bytes.fromhex("b83c0000000f05"))
	programs = ([(3, 4, 41, base + 41, base + 41, 3, 3, 1)] if interpreter else [])
	programs += [(1, 7 if writable else 5, 0x100 if prefix else 0,
		base + (0x100 if prefix else 0), base + (0x100 if prefix else 0),
		0x20 if prefix else 0x120, (0x20 if prefix else 0x120) + bss, 4096)]
	programs += list(extra)
	struct.pack_into("<16sHHIQQQIHHHHHH", data, 0,
		b"\x7fELF\x02\x01\x01" + bytes(9), 2, 62, 1, base + 0x100,
		64, shoff, 0, 64, 56, len(programs), 64, 3, 2)
	for index, program in enumerate(programs):
		struct.pack_into("<IIQQQQQQ", data, 64 + 56 * index, *program)
	data[0x100:0x100 + len(code)] = code
	names = b"\0.text\0.shstrtab\0" + (b"" if interpreter else bytes((value, 0)))
	data[names_offset:names_offset + len(names)] = names
	struct.pack_into("<IIQQQQIIQQ", data, shoff + 64,
		1, 1, 6, base + 0x100, 0x100, len(code), 0, 0, 1, 0)
	struct.pack_into("<IIQQQQIIQQ", data, shoff + 128,
		7, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0)
	return data

class FixtureCase(unittest.TestCase):
	def setUp(self):
		self.directory = Path(tempfile.mkdtemp(prefix=self.id().split(".")[-1] + "-",
			dir=TEST_ROOT))
		self.command_count = 0
		self.refusal_count = 0
		self.baseline, self.layout = fixture()
		self.source = self.write("source.elf", self.baseline)

	def write(self, name, data):
		path = self.directory / name
		path.write_bytes(data)
		return path

	def compare(self, data, source=None):
		return subject.compare_elf(source or self.source, self.write("copy.elf", data))

	def refused(self, data, reason=None, source=None):
		self.refusal_count += 1
		path = self.write(f"refused-{self.refusal_count:03d}.elf", data)
		with self.assertRaises(subject.ElfIdentityError) as result:
			subject.compare_elf(source or self.source, path)
		self.assertIn(str(self.directory), str(result.exception))
		if reason:
			self.assertRegex(str(result.exception), reason)
		with (self.directory / "refusals.jsonl").open("a") as output:
			output.write(json.dumps({"case": str(self._subtest or self.id()),
				"source": str(source or self.source), "copy": str(path),
				"diagnostic": str(result.exception)}) + "\n")

	def command(self, arguments, expected=0, timeout=120):
		self.command_count += 1
		prefix = self.directory / f"command-{self.command_count:03d}"
		arguments = [str(value) for value in arguments]
		record = {
			"argv": arguments, "shell_display": shlex.join(arguments),
			"cwd": str(HELPER.parents[3]), "exit": None,
			"python_optimize": sys.flags.optimize,
			"started_unix_ns": time.time_ns(), "finished_unix_ns": None,
			"timeout_seconds": timeout, "outcome": "attempted", "exception": None,
			"pid": None, "pgid": None, "cleanup": {"started": False},
			"stdout_path": str(prefix.with_suffix(".stdout")),
			"stderr_path": str(prefix.with_suffix(".stderr")),
			"environment_additions": {"ELF_IDENTITY_TEST_ROOT": str(TEST_ROOT)},
		}
		def save():
			prefix.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n")
		save()
		process, failure = None, None
		previous_handler = signal.getsignal(signal.SIGINT)
		deadline = time.monotonic() + timeout
		record["deadline_monotonic"] = deadline
		try:
			with prefix.with_suffix(".stdout").open("wb") as stdout, \
					prefix.with_suffix(".stderr").open("wb") as stderr:
				try:
					launch_pending = []
					signal.signal(signal.SIGINT,
						lambda *_: launch_pending.append(True))
					try:
						process = subprocess.Popen(arguments, cwd=record["cwd"],
							stdout=stdout, stderr=stderr, start_new_session=True,
							env={**os.environ, **record["environment_additions"]})
						record.update(pid=process.pid, pgid=process.pid)
						save()
					finally:
						signal.signal(signal.SIGINT, previous_handler)
					if launch_pending:
						raise KeyboardInterrupt()
					_wait_command(process, deadline, arguments, timeout)
				except BaseException as error:
					failure = error
				finally:
					pending = False
					def defer_interrupt(*_):
						nonlocal pending
						pending = True
					signal.signal(signal.SIGINT, defer_interrupt)
					try:
						if process is not None:
							record["cleanup"] = _cleanup_command(process)
							record["exit"] = process.returncode
						if pending and failure is None:
							failure = KeyboardInterrupt()
						if record["cleanup"].get("errors"):
							record["outcome"] = "cleanup_failed"
							if failure is None:
								failure = RuntimeError(str(record["cleanup"]))
						else:
							record["outcome"] = (
								"timeout" if isinstance(failure, subprocess.TimeoutExpired) else
								"cancelled" if isinstance(failure, KeyboardInterrupt) else
								"launch_failed" if process is None else
								"error" if failure else
								"success" if process.returncode == 0 else "nonzero_exit")
						if failure is not None:
							record["exception"] = {"type": type(failure).__name__, "message": str(failure)}
						record["finished_unix_ns"] = time.time_ns()
						save()
					finally:
						previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT})
						try:
							signal.signal(signal.SIGINT, previous_handler)
							if pending or signal.SIGINT in signal.sigpending():
								failure = KeyboardInterrupt()
								record.update(outcome="cancelled", finished_unix_ns=time.time_ns(),
									exception={"type": "KeyboardInterrupt", "message": ""})
								save()
						finally:
							signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
		except BaseException as error:
			previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT})
			try:
				signal.signal(signal.SIGINT, previous_handler)
				if process is not None and process.returncode is None:
					record.update(pid=process.pid, pgid=process.pid)
					record["cleanup"] = _cleanup_command(process)
					record["exit"] = process.returncode
				record.update(outcome="cancelled" if isinstance(error, KeyboardInterrupt) else "error",
					finished_unix_ns=time.time_ns(),
					exception={"type": type(error).__name__, "message": str(error)})
				save()
			finally:
				signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
			raise
		if failure is not None:
			raise failure
		try:
			result = subprocess.CompletedProcess(arguments, process.returncode,
				prefix.with_suffix(".stdout").read_text(errors="replace"),
				prefix.with_suffix(".stderr").read_text(errors="replace"))
		except BaseException as error:
			record.update(outcome="cancelled" if isinstance(error, KeyboardInterrupt) else "error",
				exception={"type": type(error).__name__, "message": str(error)})
			save()
			raise
		self.assertEqual(result.returncode, expected, result.stderr)
		return result

	def cli(self, source, copy_path, *, optimize=False, valid=True, timeout=120):
		arguments = [sys.executable, "-B"] + (["-O"] if optimize else [])
		result = self.command(arguments + [HELPER, source, copy_path],
			expected=0 if valid else 1, timeout=timeout)
		if valid:
			self.assertEqual(result.stderr, "")
			report = json.loads(result.stdout)
			self.assertIs(report["load_equivalent"], True)
			return report
		self.assertEqual(result.stdout, "")
		self.assertIn("elf_identity:", result.stderr)
		self.assertNotIn("Traceback", result.stderr)
		return result.stderr

	def digest(self, path):
		return self.command(["sha256sum", path]).stdout.split()[0]

class ElfComparisonTests(FixtureCase):
	def test_baseline_exact_identity_and_coverage(self):
		report = self.compare(self.baseline)
		digest = hashlib.sha256(self.baseline).hexdigest()
		self.assertEqual(report["source"]["sha256"], digest)
		self.assertEqual(report["copy"]["sha256"], self.digest(self.source))
		self.assertTrue(report["exact_sha256_equal"])
		self.assertEqual(report["program_comparison"]["unique_loader_bytes"], 0x610)
		self.assertEqual(report["program_comparison"]["non_load_payload_outside_loads"],
			[{"offset": 0x800, "size": 16}])
		self.assertEqual(report["dynamic_symbol_section_indexes"][0]["identity"]["hex"],
			b".text".hex())
		self.assertEqual(report["source"]["acquisition"]["initial_stat"],
			report["source"]["acquisition"]["final_stat"])

	def test_loaded_bytes_padding_and_nonload_payload(self):
		for offset in (0x300, 0x420, 0x440, 0x450, 0x480 + 1, 0x510,
				0x200, 0x310, 0x5ff, 0x800, 0x80f):
			with self.subTest(offset=offset):
				self.refused(packed(self.baseline, offset, "B", self.baseline[offset] ^ 1),
					"payload|symbol")

	def test_allocated_section_fields(self):
		fields = [(0, "I", self.layout["name_offsets"][".text"] + 1),
			(4, "I", 7), (8, "Q", 2), (16, "Q", 0x400310),
			(24, "Q", 0x310), (32, "Q", 8), (40, "I", 2),
			(44, "I", 2), (48, "Q", 8), (56, "Q", 1)]
		for delta, format, value in fields:
			with self.subTest(field_offset=delta):
				self.refused(packed(self.baseline,
					self.layout["headers"][".text"] + delta, format, value))
		for name, delta, value in ((".refs", 40, 1), (".refs", 44, 2),
				(".version", 44, 2)):
			with self.subTest(section=name, field_offset=delta):
				self.refused(packed(self.baseline,
					self.layout["headers"][name] + delta, "I", value), "sh_link|sh_info")

	def test_all_program_fields_and_order(self):
		for delta, format, value in ((0, "I", 4), (4, "I", 6),
				(8, "Q", 1), (16, "Q", 0x401000), (24, "Q", 0x401000),
				(32, "Q", 0x601), (40, "Q", 0x681), (48, "Q", 0x2000)):
			with self.subTest(program_field_offset=delta):
				self.refused(packed(self.baseline, 120 + delta, format, value))
		data = self.baseline.copy()
		data[176:232], data[232:288] = data[232:288], data[176:232]
		self.refused(data, "program\\[")

	def test_nobits_and_tls_metadata(self):
		for name in (".bss", ".tbss", ".tdata"):
			for delta, format, value in ((8, "Q", 2), (16, "Q", 0x400608),
					(24, "Q", 0x601), (32, "Q", 7), (48, "Q", 1)):
				with self.subTest(section=name, field_offset=delta):
					self.refused(packed(self.baseline,
						self.layout["headers"][name] + delta, format, value))
		for delta, value in ((32, 7), (40, 33), (48, 16)):
			with self.subTest(tls_program_offset=delta):
				self.refused(packed(self.baseline, 176 + delta, "Q", value))

	def test_header_protected_bytes(self):
		for offset, format, value in ((7, "B", 3), (16, "H", 3),
				(24, "Q", 0x400301), (32, "Q", 72), (39, "B", 1),
				(48, "I", 1), (52, "H", 63), (54, "H", 55),
				(56, "H", 5), (58, "H", 63), (59, "B", 1)):
			with self.subTest(offset=offset):
				self.refused(packed(self.baseline, offset, format, value))

	def test_strip_and_repacked_bookkeeping(self):
		data, layout = fixture(stripped=True, repack=True, reorder=True)
		report = self.compare(data)
		self.assertFalse(report["exact_sha256_equal"])
		self.assertEqual(report["copy"]["sha256"], hashlib.sha256(data).hexdigest())
		for record in report["allowed_header_fields"]:
			self.assertNotEqual(record["source"], record["copy"])
			self.assertTrue(record["changed_bytes"])
		self.assertEqual([record["field"] for record in report["allowed_header_fields"]],
			["e_shoff", "e_shnum", "e_shstrndx"])
		symbols = report["static_symbol_comparison"]
		self.assertEqual(symbols["removed_source_local_bookkeeping"], {"FILE": 1, "SECTION": 1})
		self.assertEqual(symbols["retained_entries"], 3)
		self.assertGreater(symbols["repacked_name_offsets"], 0)
		self.assertGreater(symbols["repacked_section_indexes"], 0)
		for offset, format, value in ((40, "Q", 0x300), (60, "H", 4097),
				(62, "H", 0xffff), (58, "H", 63), (24, "Q", 0x400301),
				(layout["headers"][".shstrtab"], "I", 0xffffffff)):
			with self.subTest(paired_forbidden_offset=offset):
				self.refused(packed(data, offset, format, value))
		copy_path = self.write("valid-stripped.elf", data)
		self.refused(self.baseline, "invented", source=copy_path)

	def test_dynamic_index_cannot_retarget_when_payload_stays_equal(self):
		data = self.baseline.copy()
		text_header = self.layout["headers"][".text"]
		other_header = self.layout["headers"][".data"]
		data[text_header:text_header + 64], data[other_header:other_header + 64] = (
			data[other_header:other_header + 64], data[text_header:text_header + 64])
		self.refused(data, "DYNSYM section indexes")
		for delta, format, value in ((6, "H", 0xffff), (6, "H", 13),
				(0, "I", 99), (5, "B", 4)):
			with self.subTest(dynamic_symbol_offset=delta):
				self.refused(packed(self.baseline, 0x4a0 + 24 + delta, format, value))

	def test_section_index_remapping_preserves_numeric_info(self):
		overrides = {".refs": {7: ".tdata"}, ".version": {7: 3}}
		data, _ = fixture(overrides=overrides)
		original = self.write("numeric-info-source.elf", data)
		data, layout = fixture(stripped=True, repack=True, reorder=True, overrides=overrides)
		self.assertNotEqual(layout["indexes"][".tdata"], 3)
		self.assertTrue(self.compare(data, original)["load_equivalent"])
		self.refused(packed(data, layout["headers"][".version"] + 44,
			"I", layout["indexes"][".tdata"]), "sh_info", original)
		self.refused(packed(data, layout["headers"][".refs"] + 44,
			"I", layout["indexes"][".data"]), "sh_info", original)

	def test_large_nobits_memory_is_metadata_not_file_payload(self):
		data, _ = fixture(overrides={".bss": {5: 4294967296}})
		data = packed(data, 120 + 40, "Q", 0x600 + 4294967296)
		original = self.write("large-bss.elf", data)
		report = self.compare(data, original)
		self.assertTrue(report["load_equivalent"])
		self.assertLess(report["source"]["reads"]["bytes"], 100000)
		self.refused(packed(data, self.layout["headers"][".bss"] + 32,
			"Q", 4294967280), "sh_size", original)

	def test_unrelated_nonloaded_changes_and_unaccounted_bytes(self):
		for name in (".unknown", ".comment", ".gnu.build.attributes", ".debug_info"):
			with self.subTest(section=name, change="payload"):
				self.refused(packed(self.baseline, self.layout["payloads"][name], "B", 33),
					"payload")
			with self.subTest(section=name, change="metadata"):
				self.refused(packed(self.baseline, self.layout["headers"][name] + 48, "Q", 2))
		for name in (".unknown", ".comment", ".gnu.build.attributes"):
			with self.subTest(section=name, change="removed"):
				self.refused(fixture(omit=(name,))[0], "only qualified")
		self.refused(self.baseline + b"unaccounted trailer", "unaccounted")
		self.refused(packed(self.baseline, 0x850, "B", 1), "unaccounted")
		self.assertTrue(self.compare(self.baseline + bytes(500))["load_equivalent"])
		self.assertTrue(self.compare(fixture(repack=True)[0])["load_equivalent"])

	def test_retained_static_symbol_every_semantic_field(self):
		for delta, format, value in ((0, "I", 1), (4, "B", 0x11), (5, "B", 2),
				(6, "H", 2), (8, "Q", 0x400301), (16, "Q", 15)):
			with self.subTest(symbol_field_offset=delta):
				self.refused(packed(self.baseline, 0xb00 + 4 * 24 + delta, format, value),
					"retained static symbol")
		symbols = self.layout["symbols"]
		for changed in (symbols[:-1], symbols + [symbols[-1]],
				symbols[:3] + [symbols[4], symbols[3]]):
			with self.subTest(symbols=changed):
				self.refused(fixture(symbols=changed)[0], "symbol")
		data, layout = fixture(stripped=True, repack=True)
		self.refused(packed(data, layout["payloads"][".symtab"] + 2 * 24 + 8,
			"Q", 0x400301), "retained static symbol")
		symbols = copy.deepcopy(symbols) + [[b"second", 0x11, 0, ".data", 0x400420, 8]]
		original = self.write("two-globals.elf", fixture(symbols=symbols)[0])
		symbols[-2], symbols[-1] = symbols[-1], symbols[-2]
		self.refused(fixture(symbols=symbols)[0], "retained static symbol", original)
		self.refused(fixture(omit=(".symtab", ".strtab"))[0], "only qualified")

	def test_only_canonical_file_section_deletions(self):
		for target, value in ((".text", 0x400300), (".tdata", 0), (".tbss", 8)):
			with self.subTest(canonical_section_target=target):
				symbols = copy.deepcopy(self.layout["symbols"])
				symbols[2][3], symbols[2][4] = target, value
				original = self.write("canonical-symbols.elf", fixture(symbols=symbols)[0])
				remaining = [entry for index, entry in enumerate(symbols) if index not in (1, 2)]
				report = self.compare(fixture(symbols=remaining)[0], original)
				self.assertEqual(report["static_symbol_comparison"]["removed_source_local_bookkeeping"],
					{"FILE": 1, "SECTION": 1})
		for index, field, value in ((1, 2, 2), (1, 4, 1), (1, 5, 1),
				(2, 0, b"named-section"), (2, 2, 2), (2, 4, 1), (2, 5, 1)):
			with self.subTest(symbol=index, field=field):
				symbols = copy.deepcopy(self.layout["symbols"])
				symbols[index][field] = value
				original = self.write("noncanonical.elf", fixture(symbols=symbols)[0])
				self.refused(fixture(stripped=True)[0], "retained static symbol", original)
		symbols = copy.deepcopy(self.layout["symbols"])
		symbols[3][3] = ".debug_info"
		original = self.write("debug-target.elf", fixture(symbols=symbols)[0])
		self.refused(fixture(stripped=True)[0], "retained static symbol", original)

	def test_forged_debug_and_static_roles(self):
		for name in (".debug_info", ".symtab", ".strtab"):
			for flags, address, offset in ((2, 0x400550, 0x550), (0, 0, 0x550)):
				with self.subTest(section=name, flags=flags):
					data, _ = fixture(overrides={name: {2: flags, 3: address, 4: offset}})
					self.refused(data)
		for target in (".symtab", ".strtab", ".shstrtab"):
			with self.subTest(dependency=target):
				data, _ = fixture(overrides={".refs": {6: target}})
				self.refused(data, "reference|private")
		data, _ = fixture(overrides={".refs": {6: ".debug_info"}})
		original = self.write("debug-dependent.elf", data)
		self.refused(fixture(stripped=True)[0], "removed", original)

	def test_compressed_debug_envelope_and_retention(self):
		data, _ = fixture(compressed=True)
		original = self.write("compressed.elf", data)
		self.assertTrue(self.compare(data, original)["load_equivalent"])
		self.assertTrue(self.compare(fixture(stripped=True)[0], original)["load_equivalent"])
		self.refused(packed(data, 0xa00 + 24, "B", 1), "payload", original)
		for delta, format, value in ((0, "I", 3), (4, "I", 1), (8, "Q", 0),
				(8, "Q", (16 << 30) + 1), (16, "Q", 0), (16, "Q", 3)):
			with self.subTest(compression_offset=delta):
				self.refused(packed(data, 0xa00 + delta, format, value), "compression")

	def test_debug_profile_and_exact_raw_names(self):
		data, _ = fixture(names={".debug_info": b".debug_str"},
			overrides={".debug_info": {2: 0x30, 9: 1}})
		original = self.write("debug-strings.elf", data)
		self.assertTrue(self.compare(fixture(stripped=True)[0], original)["load_equivalent"])
		compressed = packed(fixture(compressed=True)[0], 0xa00, "I", 2)
		original = self.write("zstd-envelope.elf", compressed)
		self.assertTrue(self.compare(fixture(stripped=True)[0], original)["load_equivalent"])
		for options in ({"names": {".debug_info": b".debug_unknown"}},
				{"overrides": {".debug_info": {1: 7}}},
				{"overrides": {".debug_info": {2: 1}}}):
			with self.subTest(profile=options):
				original = self.write("unqualified-debug.elf", fixture(**options)[0])
				self.refused(fixture(stripped=True)[0], "only qualified", original)
		data, _ = fixture(names={".unknown": b".raw\xff"})
		original = self.write("raw-name.elf", data)
		self.assertTrue(self.compare(data, original)["load_equivalent"])
		self.refused(fixture(names={".unknown": b".raw\xfe"})[0], "identities", original)

	def test_cli_modes_valid_strip_invalid_and_loaded_mutation(self):
		positive = self.write("stripped.elf", fixture(stripped=True, repack=True)[0])
		malformed = self.write("malformed.elf", packed(self.baseline, 60, "H", 0))
		changed = self.write("changed.elf", packed(self.baseline, 0x300, "B", 0xcc))
		for optimized in (False, True):
			with self.subTest(optimize=optimized):
				self.cli(self.source, positive, optimize=optimized)
				self.assertIn("e_shnum", self.cli(self.source, malformed,
					optimize=optimized, valid=False))
				self.assertIn("payload", self.cli(self.source, changed,
					optimize=optimized, valid=False))

class ElfMappingTests(FixtureCase):
	def modes(self, source, copy_path, *, valid, reason=None):
		for optimize in (False, True):
			result = self.cli(source, copy_path, optimize=optimize, valid=valid)
			if reason:
				self.assertIn(reason, result)

	def test_literal_prefix_tail_and_final_bss_runtime_oracles(self):
		for name, options, expected, valid in (
				("tail", {}, (7, 9), False),
				("prefix", {"prefix": True}, (7, 9), False),
				("final-bss", {"bss": 0x20, "writable": True}, (0, 0), True)):
			paths = []
			for value, exit_code in zip((7, 9), expected):
				data = page_fixture(value=value, **options)
				code = data[0x100:0x10e]
				self.assertEqual(code[:3], bytes.fromhex("0fb63d"))
				self.assertEqual(code[7:], bytes.fromhex("b83c0000000f05"))
				address = 0x107 + struct.unpack_from("<i", code, 3)[0]
				self.assertEqual(address, 0x91 if options.get("prefix") else 0x211)
				self.assertEqual(data[address], value)
				path = self.write(f"{name}-{value}.elf", data)
				(self.directory / f"{name}-{value}.inspection.json").write_text(json.dumps({
					"code_hex": code.hex(), "only_operations": "movzx byte to edi; mov eax,60; syscall exit",
					"read_offset": address, "file_value": value, "expected_exit": exit_code,
				}, indent=2) + "\n")
				path.chmod(0o700)
				self.command([path], expected=exit_code, timeout=5)
				paths.append(path)
			self.modes(*paths, valid=valid, reason=None if valid else "bookkeeping overlaps")
			if valid:
				report = subject.compare_elf(*paths)
				mapping = report["program_comparison"]["load_mappings"][0]
				self.assertEqual(mapping["exposed_file_range"], (0, 0x120))
				self.assertEqual(mapping["zero_fill_virtual_range"], (0x400120, 0x401000))

	def test_nonaliased_prefix_tail_and_bookkeeping(self):
		for prefix in (False, True):
			data = page_fixture(prefix=prefix, names_offset=0x1000, shoff=0x2000)
			original = self.write(f"ordinary-{prefix}.elf", data)
			self.modes(original, original, valid=True)
			report = subject.compare_elf(original, original)
			mapping = report["program_comparison"]["load_mappings"][0]
			self.assertEqual(mapping["exposed_file_range"], (0, 0x1000))
			self.assertEqual(mapping["protected_prefix"], (0, 0x100 if prefix else 0))
			self.assertEqual(mapping["protected_tail"], (0x120, 0x1000))
			for offset in (0x91, 0x211, 0xfff):
				changed = self.write(f"ordinary-{prefix}-{offset}.elf", packed(data, offset, "B", 9))
				self.modes(original, changed, valid=False, reason="payload")
		inside = self.write("section-table-in-tail.elf",
			page_fixture(names_offset=0x2000, shoff=0x800))
		self.modes(inside, inside, valid=False, reason="section bookkeeping overlaps")
		at_eof = page_fixture(bss=0x20, writable=True, shoff=0x800)
		at_eof.extend(bytes(4096 - len(at_eof)))
		at_eof_path = self.write("exactly-backed-page.elf", at_eof)
		self.modes(at_eof_path, at_eof_path, valid=True)
		positive = self.write("all-fields.elf", fixture(stripped=True, repack=True, reorder=True)[0])
		self.modes(self.source, positive, valid=True)
		for field in subject.compare_elf(self.source, positive)["allowed_header_fields"]:
			self.assertNotEqual(field["source"], field["copy"])

	def test_nonfinal_bss_shared_file_page_union(self):
		extra = ((1, 5, 0x200, 0x402200, 0x402200, 0x20, 0x20, 4096),)
		data = page_fixture(bss=0xee0, writable=True, names_offset=0x1000,
			shoff=0x2000, extra=extra)
		data[0x211] = 7
		original = self.write("shared-page.elf", data)
		self.modes(original, original, valid=True)
		calls = []
		compare = subject.compare_range
		def spy(source, copy, left, right, size, label, mask_header=False):
			if label == "loader payload including padding":
				calls.append((left, size))
			return compare(source, copy, left, right, size, label, mask_header)
		with mock.patch.object(subject, "compare_range", side_effect=spy):
			report = subject.compare_elf(original, original)
		self.assertEqual(calls, [(0, 4096)])
		mappings = report["program_comparison"]["load_mappings"]
		self.assertEqual(mappings[0]["exposed_file_range"], (0, 0x120))
		self.assertEqual(mappings[0]["zero_fill_virtual_range"], (0x400120, 0x401000))
		self.assertEqual(mappings[1]["exposed_file_range"], (0, 4096))
		changed = self.write("shared-exposed-change.elf", packed(data, 0x211, "B", 9))
		self.modes(original, changed, valid=False, reason="payload")
		partial = self.write("nonfinal-partial-bss.elf", packed(data, 64 + 40, "Q", 0x140))
		self.modes(partial, partial, valid=False, reason="nonfinal BSS memory end")

	def test_mapping_profile_refusals(self):
		cases = [
			("readonly-bss", page_fixture(bss=0x20), "BSS requires a writable"),
			("partial-eof", page_fixture(shoff=0x800), "partial EOF page"),
			("empty-load", packed(page_fixture(), 64 + 32, "Q", 0), "empty file payload"),
			("rounded-overlap", page_fixture(names_offset=0x1000, shoff=0x2000,
				extra=((1, 5, 0x200, 0x400200, 0x400200, 32, 32, 4096),)), "rounded virtual"),
		]
		data = page_fixture(names_offset=0x1000, shoff=0x2000)
		for offset in (24, 64 + 16, 64 + 24):
			data = packed(data, offset, "Q", (1 << 64) - 4096 + (0x100 if offset == 24 else 0))
		data = packed(data, 64 + 4, "I", 7)
		data = packed(data, 64 + 40, "Q", 0xfff)
		cases.append(("round-overflow", data, "page rounding overflow"))
		for name, data, reason in cases:
			with self.subTest(name=name):
				path = self.write(name + ".elf", data)
				self.modes(path, path, valid=False, reason=reason)

	def test_interpreter_and_every_independent_record_mask_alias(self):
		paths = [self.write(name, page_fixture(interpreter=True, names_offset=0x2000, shoff=offset))
			for name, offset in (("interp-a.elf", 0x612f00), ("interp-b.elf", 0x622f00))]
		self.assertEqual(paths[0].read_bytes()[41:44], b"/a\0")
		self.assertEqual(paths[1].read_bytes()[41:44], b"/b\0")
		self.modes(*paths, valid=False, reason="independent program payload overlaps")
		for kind in (2, 3, 4, 6, 7, 0x6474e550, 0x6474e552, 0x6474e553):
			for start, end in ((40, 48), (60, 62), (62, 64)):
				data = page_fixture(interpreter=True, names_offset=0x2000, shoff=0x612f00)
				struct.pack_into("<IIQQQQQQ", data, 64, kind, 4, start,
					0x400000 + start, 0x400000 + start, end - start, end - start, 1)
				path = self.write(f"alias-{kind}-{start}.elf", data)
				self.modes(path, path, valid=False, reason="independent program payload overlaps")

class ElfMalformedTests(FixtureCase):
	def test_header_formats_counts_sizes_and_ranges(self):
		cases = [(0, "I", 0), (4, "B", 1), (5, "B", 2), (6, "B", 0),
			(7, "B", 9), (8, "B", 1), (9, "B", 1), (16, "H", 1),
			(18, "H", 183), (20, "I", 0), (24, "Q", 0),
			(32, "Q", 0), (32, "Q", 65), (32, "Q", (1 << 64) - 8),
			(40, "Q", 64), (40, "Q", 0x1001), (40, "Q", (1 << 64) - 8),
			(48, "I", 1), (52, "H", 65), (54, "H", 0),
			(56, "H", 0), (56, "H", 0xffff), (56, "H", 1025),
			(58, "H", 0), (60, "H", 0), (60, "H", 0xff00),
			(60, "H", 4097), (62, "H", 0), (62, "H", 0xffff), (62, "H", 20)]
		for offset, format, value in cases:
			with self.subTest(offset=offset, value=value):
				data = packed(self.baseline, offset, format, value)
				self.refused(data)
				invalid_source = self.write("invalid-source.elf", data)
				self.refused(data, source=invalid_source)
		for size in (0, 4, 63, 100, 0x800, len(self.baseline) - 1):
			with self.subTest(truncate=size):
				self.refused(self.baseline[:size], "range")

	def test_section_types_names_references_and_overlaps(self):
		text = self.layout["headers"][".text"]
		cases = [(0x1000 + 4, "I", 1), (text, "I", 0),
			(text, "I", 999999), (text, "I", self.layout["name_offsets"][".data"]),
			(text + 4, "I", 0), (text + 4, "I", 17), (text + 4, "I", 18),
			(text + 8, "Q", 0x10000000), (text + 16, "Q", (1 << 64) - 8),
			(text + 24, "Q", 0x1000), (text + 24, "Q", (1 << 64) - 1),
			(text + 32, "Q", (1 << 64) - 1), (text + 48, "Q", 3),
			(text + 48, "Q", 0x200), (text + 56, "Q", 3),
			(self.layout["headers"][".data"] + 24, "Q", 0x300),
			(self.layout["headers"][".refs"] + 40, "I", 99),
			(self.layout["headers"][".refs"] + 44, "I", 99),
			(self.layout["headers"][".symtab"] + 40, "I", 1),
			(self.layout["headers"][".symtab"] + 44, "I", 0),
			(self.layout["headers"][".version"] + 8, "Q", 0x42),
			(self.layout["headers"][".shstrtab"] + 24, "Q", 0x550),
			(0xd00, "B", 1), (0xc00, "B", 1)]
		for offset, format, value in cases:
			with self.subTest(offset=offset, value=value):
				self.refused(packed(self.baseline, offset, format, value))
		data, _ = fixture(names={".unknown": b"x" * 4097})
		self.refused(data, "length|overlap")
		data, layout = fixture()
		end = layout["payloads"][".shstrtab"] + struct.unpack_from("<Q", data,
			layout["headers"][".shstrtab"] + 32)[0] - 1
		self.refused(packed(data, end, "B", 1), "NUL")
		data, _ = fixture(overrides={".unknown": {1: 4, 5: 0, 6: ".symtab", 7: ".debug_info", 9: 24, 10: b""}})
		self.refused(data, "references to static|debug relocations")

	def test_program_validation(self):
		for offset, format, value in ((64, "I", 0x1234), (124, "I", 8),
				(128, "Q", 0x1000), (136, "Q", (1 << 64) - 1),
				(144, "Q", (1 << 64) - 1), (152, "Q", 0x681),
				(160, "Q", 0x500), (168, "Q", 3), (176, "I", 6),
				(232, "I", 0), (64 + 32, "Q", 335), (344 + 8, "Q", 1)):
			with self.subTest(offset=offset):
				self.refused(packed(self.baseline, offset, format, value))
		data = self.baseline.copy()
		struct.pack_into("<IIQQQQQQ", data, 344, 1, 6, 0x500,
			0x400500, 0x400500, 16, 16, 1)
		self.refused(data, "overlapping PT_LOAD")
		struct.pack_into("<IIQQQQQQ", data, 344, 1, 6, 0x500,
			0x401500, 0x401500, 16, 16, 1)
		self.refused(data, "overlaps PT_LOAD")

	def test_static_symbol_validation(self):
		for offset, format, value in ((0xb00, "I", 1),
				(0xb00 + 24, "I", 9999), (0xb00 + 24 + 4, "B", 0x14),
				(0xb00 + 24 + 5, "B", 4), (0xb00 + 24 + 6, "H", 1),
				(0xb00 + 48 + 6, "H", 0), (0xb00 + 96 + 6, "H", 0xfff2),
				(0xb00 + 96 + 6, "H", 0xffff), (0xb00 + 96 + 6, "H", 19),
				(0xb00 + 96 + 4, "B", 0x32), (0xb00 + 96 + 4, "B", 0x19)):
			with self.subTest(offset=offset, value=value):
				self.refused(packed(self.baseline, offset, format, value), "symbol")

class ElfAcquisitionTests(FixtureCase):
	def test_repeated_names_stay_streamed_and_budgeted(self):
		symbols = [[b"", 0, 0, 0, 0, 0]] + [
			[b"shared", 0x12, 0, ".text", 0x400300, 16] for _ in range(512)]
		data, _ = fixture(symbols=symbols, overrides={".symtab": {4: 0x10000},
			".strtab": {4: 0x30000}, ".shstrtab": {4: 0x31000}}, table_offset=0x32000)
		original = self.write("repeated-names.elf", data)
		report = self.compare(data, original)
		for role in ("source", "copy"):
			reads = report[role]["reads"]
			self.assertEqual(reads["name_cache_bytes"], 0)
			self.assertEqual(reads["symbol_name_read_count"], 512)
			self.assertEqual(reads["symbol_name_read_bytes"], 512 * 7)
			self.assertEqual(reads["resolved_symbol_name_bytes"], 1 + 512 * 7)
		self.assertEqual(report["static_symbol_comparison"]["retained_entries"], 513)

	def test_overlapping_program_coverage_is_read_as_one_union(self):
		calls = []
		original_compare = subject.compare_range
		def intercept(source, copy, left, right, size, label, mask_header=False):
			if label == "loader payload including padding":
				calls.append((left, size))
			return original_compare(source, copy, left, right, size, label, mask_header)
		with mock.patch.object(subject, "compare_range", side_effect=intercept):
			report = self.compare(self.baseline)
		self.assertEqual(calls, [(0, 0x600), (0x800, 16)])
		self.assertEqual(sum(size for _, size in calls),
			report["program_comparison"]["unique_loader_bytes"])

	def test_section_and_program_count_boundaries(self):
		program_count, section_count = 1024, 4096
		text_offset, names_offset, table_offset = 0x10000, 0x11000, 0x22000
		names = bytearray(b"\0.text\0.shstrtab\0")
		name_offsets = []
		for index in range(3, section_count):
			name_offsets.append(len(names))
			names.extend(f".empty-{index}".encode() + b"\0")
		data = bytearray(table_offset + section_count * 64)
		struct.pack_into("<16sHHIQQQIHHHHHH", data, 0,
			b"\x7fELF\x02\x01\x01" + bytes(9), 2, 62, 1, 0x410000,
			64, table_offset, 0, 64, 56, program_count, 64, section_count, 2)
		struct.pack_into("<IIQQQQQQ", data, 64, 6, 4, 64, 0x400040,
			0x400040, program_count * 56, program_count * 56, 8)
		struct.pack_into("<IIQQQQQQ", data, 120, 1, 5, 0, 0x400000,
			0x400000, text_offset + 16, text_offset + 16, 4096)
		data[text_offset:text_offset + 16] = b"\x90" * 16
		data[names_offset:names_offset + len(names)] = names
		struct.pack_into("<IIQQQQIIQQ", data, table_offset + 64,
			1, 1, 6, 0x410000, text_offset, 16, 0, 0, 16, 0)
		struct.pack_into("<IIQQQQIIQQ", data, table_offset + 128,
			7, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0)
		for index, name in enumerate(name_offsets, 3):
			struct.pack_into("<IIQQQQIIQQ", data, table_offset + index * 64,
				name, 1, 0, 0, 0, 0, 0, 0, 1, 0)
		original = self.write("at-count-limits.elf", data)
		report = self.compare(data, original)
		self.assertEqual(report["source"]["header"]["e_phnum"], 1024)
		self.assertEqual(report["source"]["header"]["e_shnum"], 4096)
		for offset, value in ((56, 1025), (60, 4097)):
			self.refused(packed(data, offset, "H", value), "limit", original)

	def test_actual_section_and_symbol_name_length_boundaries(self):
		for length in (4096, 4097):
			data, _ = fixture(names={".unknown": b"N" * length},
				overrides={".shstrtab": {4: 0x10000}}, table_offset=0x14000)
			original = self.write(f"section-name-{length}.elf", data)
			if length == 4096:
				self.assertTrue(self.compare(data, original)["load_equivalent"])
			else:
				self.refused(data, "length", original)
		for length in (65536, 65537):
			symbols = copy.deepcopy(self.layout["symbols"])
			symbols[1][0] = b"N" * length
			data, _ = fixture(symbols=symbols, overrides={
				".strtab": {4: 0x10000}, ".shstrtab": {4: 0x22000}}, table_offset=0x23000)
			original = self.write(f"symbol-name-{length}.elf", data)
			if length == 65536:
				self.assertTrue(self.compare(data, original)["load_equivalent"])
			else:
				self.refused(data, "name length", original)

	def test_backed_metadata_limits_before_payload_reads(self):
		for name, size, reason in ((".shstrtab", 16777217, "section name table size"),
				(".symtab", 4194305 * 24, "symbol count"),
				(".strtab", 1073741825, "linked string table size")):
			with self.subTest(section=name):
				data = packed(self.baseline, self.layout["headers"][name] + 24, "Q", 0x100000)
				data = packed(data, self.layout["headers"][name] + 32, "Q", size)
				path = self.write("sparse-declaration.elf", data)
				with path.open("r+b") as output:
					output.seek(0x100000)
					old_offset = self.layout["payloads"][name]
					old_size = struct.unpack_from("<Q", self.baseline,
						self.layout["headers"][name] + 32)[0]
					output.write(self.baseline[old_offset:old_offset + old_size])
					output.truncate(0x100000 + size)
				real_pread = os.pread
				requests = []
				def spy(fd, count, offset):
					self.assertLessEqual(count, 4096)
					requests.append(count)
					return real_pread(fd, count, offset)
				with mock.patch.object(subject.os, "pread", side_effect=spy):
					with self.assertRaisesRegex(subject.ElfIdentityError, reason):
						with subject.ElfFile(path) as reader:
							subject.parse_elf(reader)
				self.assertLess(sum(requests), 100000)

	def test_name_table_size_boundaries_parse_without_reading_whole_tables(self):
		for name, size in ((".shstrtab", 16777216), (".strtab", 1073741824)):
			with self.subTest(section=name):
				data = packed(self.baseline, self.layout["headers"][name] + 24, "Q", 0x100000)
				data = packed(data, self.layout["headers"][name] + 32, "Q", size)
				old_offset = self.layout["payloads"][name]
				old_size = struct.unpack_from("<Q", self.baseline,
					self.layout["headers"][name] + 32)[0]
				data[old_offset:old_offset + old_size] = bytes(old_size)
				path = self.write("at-name-table-limit.elf", data)
				with path.open("r+b") as output:
					output.seek(0x100000)
					output.write(self.baseline[old_offset:old_offset + old_size])
					output.truncate(0x100000 + size)
				real_pread = os.pread
				requests = []
				def spy(fd, count, offset):
					self.assertLessEqual(count, 4096)
					requests.append(count)
					return real_pread(fd, count, offset)
				with mock.patch.object(subject.os, "pread", side_effect=spy):
					with subject.ElfFile(path) as reader:
						subject.parse_elf(reader)
						reader.verify_unchanged()
				self.assertLess(sum(requests), 100000)

	def test_real_pread_spy_and_numerical_contract(self):
		real_pread = os.pread
		requests = []
		def spy(fd, size, offset):
			self.assertIs(type(size), int)
			self.assertGreater(size, 0)
			self.assertLessEqual(size, 1048576)
			self.assertGreaterEqual(offset, 0)
			requests.append((size, offset))
			return real_pread(fd, size, offset)
		with mock.patch.object(subject.os, "pread", side_effect=spy):
			report = self.compare(fixture(stripped=True, repack=True)[0])
			for invalid_size in (None, -1, 0, 1048577):
				with self.subTest(spy_control=invalid_size):
					with self.assertRaises(AssertionError):
						os.pread(-1, invalid_size, 0)
		expected = {"input_bytes": 17179869184, "read_bytes": 1048576,
			"program_headers": 1024, "sections": 4096,
			"section_name_table_bytes": 16777216, "section_name_bytes": 4096,
			"symbols_per_table": 4194304, "private_static_name_table_bytes": 1073741824,
			"symbol_name_bytes": 65536, "resolved_symbol_name_bytes_including_nul": 4294967296,
			"symbol_name_read_bytes": 21474836480, "symbol_name_read_count": 5242880,
			"symbol_name_read_chunk_bytes": 4096, "name_cache_bytes": 0,
			"interpreter_bytes": 4096, "load_page_bytes": 4096,
			"uncompressed_debug_bytes": 17179869184}
		self.assertEqual(report["limits_per_file"], expected)
		self.assertEqual(sum(row[0] for row in requests),
			report["source"]["reads"]["bytes"] + report["copy"]["reads"]["bytes"])
		self.assertEqual(max(row[0] for row in requests), max(
			report["source"]["reads"]["max_request"], report["copy"]["reads"]["max_request"]))
		(self.directory / "bounds.json").write_text(json.dumps({
			"limits": expected, "observed_max": max(row[0] for row in requests),
			"request_count": len(requests), "read_bytes": sum(row[0] for row in requests),
			"memory_contract": "two 1 MiB payload buffers; capped metadata; streaming symbols; zero name cache",
		}, indent=2) + "\n")

	def test_read_validation_and_aggregate_name_limits_before_io(self):
		with subject.ElfFile(self.source) as reader:
			with mock.patch.object(subject.os, "pread", side_effect=AssertionError("unexpected I/O")):
				self.assertEqual(reader.read_at(0, 0), b"")
				for offset, size in ((0, -1), (-1, 1), (0, None), (0, 1048577), (0, 1 << 64)):
					with self.subTest(offset=offset, size=size):
						with self.assertRaises(subject.ElfIdentityError):
							reader.read_at(offset, size)
				reader.name_read_bytes = 21474836480
				with self.assertRaisesRegex(subject.ElfIdentityError, "name read bytes"):
					reader.read_name_chunk(0, 1)
				reader.name_read_bytes = 0
				reader.name_read_count = 5242880
				with self.assertRaisesRegex(subject.ElfIdentityError, "name read count"):
					reader.read_name_chunk(0, 1)
				reader.resolved_name_bytes = 4294967295
				reader.account_symbol_name(1)
				with self.assertRaisesRegex(subject.ElfIdentityError, "resolved symbol name bytes"):
					reader.account_symbol_name(1)
			reader.name_read_count = 5242879
			reader.name_read_bytes = 21474836479
			self.assertEqual(reader.read_name_chunk(0, 1), b"\x7f")
			reader.verify_unchanged()
		with self.assertRaisesRegex(subject.ElfIdentityError, "closed"):
			reader.read_at(0, 1)
		path = self.write("read-limit.elf", self.baseline)
		with path.open("r+b") as output:
			output.truncate(2097152)
		with subject.ElfFile(path) as reader:
			self.assertEqual(len(reader.read_at(0, 1048576)), 1048576)
			with mock.patch.object(subject.os, "pread", side_effect=AssertionError("unexpected I/O")):
				with self.assertRaisesRegex(subject.ElfIdentityError, "read size.*limit"):
					reader.read_at(0, 1048577)

	def test_huge_declarations_never_drive_huge_reads(self):
		for offset, format, value in ((56, "H", 1025), (60, "H", 4097),
				(self.layout["headers"][".shstrtab"] + 32, "Q", 16777217),
				(self.layout["headers"][".symtab"] + 32, "Q", 4194305 * 24),
				(self.layout["headers"][".strtab"] + 32, "Q", 1073741825)):
			with self.subTest(offset=offset):
				real_pread = os.pread
				reads = []
				def spy(fd, size, position):
					self.assertLessEqual(size, 64)
					reads.append(size)
					return real_pread(fd, size, position)
				invalid = self.write("huge.elf", packed(self.baseline, offset, format, value))
				with mock.patch.object(subject.os, "pread", side_effect=spy):
					with self.assertRaises(subject.ElfIdentityError):
						with subject.ElfFile(invalid) as reader:
							subject.parse_elf(reader)
				self.assertLessEqual(sum(reads), 64 + 6 * 56 + 20 * 64)
		oversized = self.directory / "over-input-limit.elf"
		with oversized.open("wb") as output:
			output.truncate(17179869185)
		with mock.patch.object(subject.os, "pread", side_effect=AssertionError("unexpected I/O")):
			with self.assertRaisesRegex(subject.ElfIdentityError, "input size.*limit"):
				with subject.ElfFile(oversized):
					self.fail("over-limit acquisition succeeded")

	def test_nonregular_missing_and_symlinks_without_blocking(self):
		link = self.directory / "link.elf"
		link.symlink_to(self.source)
		fifo = self.directory / "fifo"
		os.mkfifo(fifo)
		for path in (self.directory / "missing", self.directory, link, fifo, Path("/dev/null")):
			with self.subTest(path=path):
				self.cli(self.source, path, valid=False, timeout=10)

	def test_descriptor_lifetimes_and_readonly_flags(self):
		import fcntl
		real_open, real_close = os.open, os.close
		for mode in ("success", "second-open", "second-nonregular", "second-parse",
				"first-parse", "short-read", "final-check"):
			with self.subTest(mode=mode):
				opened, closed = [], []
				def open_spy(path, flags, *args, **kwargs):
					fd = real_open(path, flags, *args, **kwargs)
					opened.append(fd)
					self.assertEqual(flags & os.O_ACCMODE, os.O_RDONLY)
					self.assertTrue(fcntl.fcntl(fd, fcntl.F_GETFD) & fcntl.FD_CLOEXEC)
					return fd
				def close_spy(fd):
					closed.append(fd)
					return real_close(fd)
				second = self.write("second.elf", self.baseline if mode != "second-parse" else b"bad")
				first = self.source if mode != "first-parse" else self.write("first-bad.elf", b"bad")
				if mode == "second-open":
					second = self.directory / "missing"
				elif mode == "second-nonregular":
					second = self.directory
				with mock.patch.object(subject.os, "open", side_effect=open_spy), mock.patch.object(
						subject.os, "close", side_effect=close_spy):
					if mode == "success":
						subject.compare_elf(first, second)
					elif mode == "short-read":
						with mock.patch.object(subject.os, "pread", return_value=b""), self.assertRaisesRegex(
								subject.ElfIdentityError, "short read"):
							subject.compare_elf(first, second)
					elif mode == "final-check":
						with mock.patch.object(subject.ElfFile, "verify_unchanged",
								side_effect=subject.ElfIdentityError("forced final failure")), self.assertRaisesRegex(
								subject.ElfIdentityError, "forced final failure"):
							subject.compare_elf(first, second)
					else:
						with self.assertRaises((subject.ElfIdentityError, OSError)):
							subject.compare_elf(first, second)
				self.assertEqual(sorted(opened), sorted(closed))
				for fd in opened:
					with self.assertRaises(OSError):
						os.fstat(fd)
				(self.directory / f"descriptors-{mode}.json").write_text(json.dumps({
					"opened": opened, "closed": closed, "all_closed": True,
					"readonly_and_cloexec": True}, indent=2) + "\n")

	def test_deterministic_late_mutation_and_replacement(self):
		for action in ("same-size", "truncate", "grow", "replace", "symlink"):
			with self.subTest(action=action):
				first = self.write("mutable.elf", self.baseline)
				second = self.write("stable.elf", self.baseline)
				before = first.stat()
				verify = subject.ElfFile.verify_unchanged
				triggered = []
				def intercept(reader):
					if reader.path == str(first):
						triggered.append(action)
						if action == "same-size":
							with first.open("r+b") as output:
								output.seek(0x300)
								output.write(b"\xcc")
							os.utime(first, ns=(before.st_atime_ns, before.st_mtime_ns + 2000000000))
							self.assertNotEqual(first.stat().st_mtime_ns, before.st_mtime_ns)
							self.assertEqual(first.stat().st_size, before.st_size)
						elif action in ("truncate", "grow"):
							with first.open("r+b") as output:
								output.truncate(before.st_size + (-1 if action == "truncate" else 1))
							self.assertNotEqual(first.stat().st_size, before.st_size)
						elif action in ("replace", "symlink"):
							replacement = self.directory / "replacement"
							if action == "replace":
								replacement.write_bytes(self.baseline)
							else:
								replacement.symlink_to(second)
							os.replace(replacement, first)
							self.assertNotEqual(first.lstat().st_ino, before.st_ino)
						after = first.lstat()
						(self.directory / f"mutation-{action}.json").write_text(json.dumps({
							"action": action, "trigger": "first subject final verification",
							"path": str(first), "before": {
								"inode": before.st_ino, "size": before.st_size,
								"mtime_ns": before.st_mtime_ns, "ctime_ns": before.st_ctime_ns},
							"after": {"inode": after.st_ino, "size": after.st_size,
								"mtime_ns": after.st_mtime_ns, "ctime_ns": after.st_ctime_ns},
						}, indent=2) + "\n")
						return verify(reader)
					return verify(reader)
				with mock.patch.object(subject.ElfFile, "verify_unchanged", intercept):
					with self.assertRaisesRegex(subject.ElfIdentityError, "stability"):
						subject.compare_elf(first, second)
				self.assertEqual(triggered, [action])

	def test_actual_atime_only_read_change_is_accepted(self):
		os.utime(self.source, ns=(1, self.source.stat().st_mtime_ns))
		before = self.source.stat()
		self.assertTrue(self.compare(self.baseline)["load_equivalent"])
		after = self.source.stat()
		self.assertNotEqual(before.st_atime_ns, after.st_atime_ns)
		self.assertEqual((before.st_mtime_ns, before.st_ctime_ns),
			(after.st_mtime_ns, after.st_ctime_ns))

def _command_owner_probe(mode, directory):
	import ctypes
	if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) != 0:
		raise OSError(ctypes.get_errno(), "test-only child subreaper failed")
	case = FixtureCase()
	case.directory = Path(directory)
	case.command_count = 0
	ready = case.directory / "worker-ready.json"
	worker = case.write("worker.py", (
		"import json, os, signal, time\nfrom pathlib import Path\n"
		"signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
		"print('worker stdout before trigger', flush=True)\n"
		"print('worker stderr before trigger', file=__import__('sys').stderr, flush=True)\n"
		f"Path({str(ready) + '.tmp'!r}).write_text(json.dumps({{'pid': os.getpid(), 'pgid': os.getpgrp(), "
		"'blocked_signals': sorted(signal.pthread_sigmask(signal.SIG_BLOCK, set())), "
		"'ready_unix_ns': time.time_ns()}))\n"
		f"os.replace({str(ready) + '.tmp'!r}, {str(ready)!r})\n"
		"time.sleep(120)\n").encode())
	code = case.write("worker.cpp", b"int main() { return 0; }\n")
	arguments = ["g++", "-g", "-O0", "-wrapper", f"{sys.executable},{worker}",
		code, "-o", case.directory / "unused"]
	if mode == "success-with-worker":
		driver = case.write("driver.py", (
			"import subprocess, sys, time\nfrom pathlib import Path\n"
			f"subprocess.Popen([sys.executable, {str(worker)!r}])\n"
			f"while not Path({str(ready)!r}).exists(): time.sleep(0.01)\n").encode())
		arguments = [sys.executable, "-B", driver]
	sentinel = subprocess.Popen([sys.executable, "-B", "-c", "import time; time.sleep(120)"],
		start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	facts = {"mode": mode, "sentinel_pid": sentinel.pid, "sentinel_pgid": sentinel.pid}
	wait_command = _wait_command
	original_signal = signal.signal
	original_handler = signal.getsignal(signal.SIGINT)
	original_mask = signal.pthread_sigmask(signal.SIG_BLOCK, set())
	cleanup_entry = mode in ("cleanup-before-install", "cleanup-after-install")
	facts["signals_sent"] = 0
	owner = None
	worker_pid = None
	try:
		def trigger(process, deadline, argv, timeout):
			nonlocal owner, worker_pid
			owner = process
			ready_deadline = time.monotonic() + 5
			while not ready.exists():
				if time.monotonic() >= ready_deadline:
					raise RuntimeError("worker did not synchronize before trigger")
				time.sleep(0.01)
			worker_facts = json.loads(ready.read_text())
			worker_pid = worker_facts["pid"]
			facts.update(worker=worker_facts, trigger_unix_ns=time.time_ns(), direct_pid=process.pid)
			case.assertEqual(worker_facts["pgid"], process.pid)
			case.assertEqual(worker_facts["blocked_signals"], sorted(original_mask))
			case.assertNotEqual(worker_facts["pgid"], sentinel.pid)
			case.assertIn(worker_pid, [row["pid"] for row in _live_command_group(process.pid)])
			attempt = json.loads((case.directory / "command-001.json").read_text())
			case.assertEqual(attempt["outcome"], "attempted")
			facts["attempt"] = attempt
			if mode == "cancel":
				raise KeyboardInterrupt("synchronized cancellation")
			if mode == "error":
				raise RuntimeError("synchronized wait error")
			try:
				return wait_command(process, deadline, argv, timeout)
			except subprocess.TimeoutExpired:
				facts["timeout_observed"] = True
				raise
		def install(sig, handler):
			entry = (cleanup_entry and sig == signal.SIGINT
				and getattr(handler, "__name__", "") == "defer_interrupt")
			if entry:
				case.assertTrue(facts.get("timeout_observed"))
				case.assertEqual(facts["signals_sent"], 0)
				facts["signals_sent"] += 1
				if mode == "cleanup-before-install":
					os.kill(os.getpid(), signal.SIGINT)
			result = original_signal(sig, handler)
			if entry and mode == "cleanup-after-install":
				os.kill(os.getpid(), signal.SIGINT)
			return result
		started = time.monotonic()
		expected = (KeyboardInterrupt if cleanup_entry else
			{"timeout": subprocess.TimeoutExpired, "cancel": KeyboardInterrupt,
				"error": RuntimeError}.get(mode))
		with mock.patch.dict(globals(), {"_wait_command": trigger}), \
				mock.patch.object(signal, "signal", install):
			if expected:
				with case.assertRaises(expected):
					case.command(arguments, timeout=0.5 if mode == "timeout" or cleanup_entry else 10)
			else:
				case.command(arguments, timeout=10)
		facts["elapsed_seconds"] = time.monotonic() - started
		facts["handler_restored"] = signal.getsignal(signal.SIGINT) is original_handler
		facts["mask_restored"] = signal.pthread_sigmask(signal.SIG_BLOCK, set()) == original_mask
		case.assertTrue(facts["handler_restored"])
		case.assertTrue(facts["mask_restored"])
		case.assertLess(facts["elapsed_seconds"], 8)
		record = json.loads((case.directory / "command-001.json").read_text())
		facts["final_record"] = record
		case.assertEqual(record["outcome"], "cancelled" if cleanup_entry else
			{"cancel": "cancelled", "success-with-worker": "success"}.get(mode, mode))
		if cleanup_entry:
			case.assertEqual(facts["signals_sent"], 1)
			case.assertEqual(record["exception"]["type"], "KeyboardInterrupt")
			case.assertTrue(record["cleanup"]["started"])
		case.assertTrue(record["cleanup"]["direct_child_reaped"])
		case.assertEqual(record["cleanup"]["remaining"], [])
		case.assertEqual(record["cleanup"]["errors"], [])
		case.assertIn("SIGKILL", record["cleanup"]["signals"])
		with case.assertRaises(ChildProcessError):
			os.waitpid(record["pid"], os.WNOHANG)
		case.assertEqual(_live_command_group(record["pgid"]), [])
		case.assertIsNone(sentinel.poll())
		for extension in ("stdout", "stderr"):
			case.assertIn(f"worker {extension} before trigger",
				(case.directory / f"command-001.{extension}").read_text())
		case.assertIsNotNone(worker_pid)
		reap_deadline = time.monotonic() + 1
		while True:
			pid, status = os.waitpid(worker_pid, os.WNOHANG)
			if pid:
				break
			if time.monotonic() >= reap_deadline:
				raise RuntimeError("worker did not become reapable")
			time.sleep(0.01)
		worker_pid = None
		facts.update(worker_reaped=True, worker_status=status, sentinel_survived=True,
			direct_child_reaped=True, final_record=record)
	finally:
		original_signal(signal.SIGINT, original_handler)
		if owner is not None and owner.returncode is None:
			try:
				os.killpg(owner.pid, signal.SIGKILL)
			except ProcessLookupError:
				pass
			owner.wait(timeout=2)
		if worker_pid is not None:
			try:
				if os.waitpid(worker_pid, os.WNOHANG)[0] == 0:
					os.kill(worker_pid, signal.SIGKILL)
					deadline = time.monotonic() + 1
					while os.waitpid(worker_pid, os.WNOHANG)[0] == 0:
						if time.monotonic() >= deadline:
							raise RuntimeError(f"test cleanup left owned worker {worker_pid}")
						time.sleep(0.01)
			except ProcessLookupError:
				pass
			except ChildProcessError:
				pass
		sentinel.terminate()
		try:
			sentinel.wait(timeout=1)
		except subprocess.TimeoutExpired:
			sentinel.kill()
			sentinel.wait(timeout=1)
		facts["finished_unix_ns"] = time.time_ns()
		facts["probe_owner_reaped"] = owner is None or owner.returncode is not None
		facts["sentinel_reaped"] = sentinel.returncode is not None
		facts["live_group_after_probe_cleanup"] = _live_command_group(owner.pid) if owner else []
		(case.directory / "owner-probe.json").write_text(json.dumps(facts, indent=2) + "\n")

class CommandOwnerTests(FixtureCase):
	def test_success_nonzero_and_launch_failure_records(self):
		self.command([sys.executable, "-B", "-c", "print('success output')"])
		self.command([sys.executable, "-B", "-c", "raise SystemExit(7)"], expected=7)
		with self.assertRaises(FileNotFoundError):
			self.command([self.directory / "missing-command"])
		for index, outcome in enumerate(("success", "nonzero_exit", "launch_failed"), 1):
			record = json.loads((self.directory / f"command-{index:03d}.json").read_text())
			self.assertEqual(record["outcome"], outcome)
			self.assertGreaterEqual(record["finished_unix_ns"], record["started_unix_ns"])
			self.assertIn("timeout_seconds", record)
			for extension in ("stdout", "stderr"):
				self.assertTrue((self.directory / f"command-{index:03d}.{extension}").exists())
			if index < 3:
				self.assertTrue(record["cleanup"]["direct_child_reaped"])
				self.assertIsNone(record["cleanup"]["remaining_owner"])
			else:
				self.assertIsNone(record["pid"])
				self.assertEqual(record["exception"]["type"], "FileNotFoundError")

	def test_owned_compiler_descendants_timeout_cancel_error_and_success(self):
		for mode in ("timeout", "cancel", "error", "success-with-worker"):
			with self.subTest(mode=mode):
				directory = self.directory / mode
				directory.mkdir()
				python = [sys.executable, "-B"] + (["-O"] if sys.flags.optimize else [])
				self.command(python + [Path(__file__).resolve(),
					"--command-owner-probe", mode, directory], timeout=20)
				facts = json.loads((directory / "owner-probe.json").read_text())
				self.assertTrue(facts["sentinel_survived"])
				self.assertTrue(facts["worker_reaped"])

	def test_cleanup_entry_sigint_keeps_owned_timeout_group(self):
		for mode in ("cleanup-before-install", "cleanup-after-install"):
			with self.subTest(mode=mode):
				directory = self.directory / mode
				directory.mkdir()
				python = [sys.executable, "-B"] + (["-O"] if sys.flags.optimize else [])
				self.command(python + [Path(__file__).resolve(),
					"--command-owner-probe", mode, directory], timeout=20)
				facts = json.loads((directory / "owner-probe.json").read_text())
				self.assertEqual(facts["signals_sent"], 1)
				self.assertTrue(facts["timeout_observed"])
				self.assertTrue(facts["worker_reaped"])
				self.assertTrue(facts["sentinel_survived"])
				self.assertTrue(facts["sentinel_reaped"])

	def test_finalization_sigint_records_cancellation_and_restores_owner(self):
		sentinel = subprocess.Popen([sys.executable, "-B", "-c", "import time; time.sleep(120)"],
			start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
		original_write, original_signal = Path.write_text, signal.signal
		original_pending = signal.sigpending
		original_handler = signal.getsignal(signal.SIGINT)
		original_mask = signal.pthread_sigmask(signal.SIG_BLOCK, set())
		try:
			for mode in ("write-before", "write-after", "restore-before", "restore-after", "pending-after",
					"repeated", "nonzero", "timeout", "launch-failed", "custom-handler"):
				with self.subTest(mode=mode):
					path = self.directory / f"command-{self.command_count + 1:03d}.json"
					facts = {"mode": mode, "signals": [], "observed": None, "final_writes": 0}
					final_saved = False
					custom_calls = []
					handler = (lambda *_: custom_calls.append(True)) if mode == "custom-handler" else original_handler
					original_signal(signal.SIGINT, handler)
					stages = ({"write-before", "write-after", "restore-before", "restore-after",
						"cancel-before", "cancel-after"} if mode == "repeated" else
						{mode} if mode in ("write-before", "write-after", "restore-before", "restore-after", "pending-after")
						else {"write-before"})
					def deliver(stage):
						if stage in stages:
							stages.remove(stage)
							facts["signals"].append(stage)
							os.kill(os.getpid(), signal.SIGINT)
					def write(path_to_write, data, *args, **kwargs):
						nonlocal final_saved
						final = path_to_write == path and json.loads(data)["finished_unix_ns"] is not None
						if final:
							facts["final_writes"] += 1
							cancelled = json.loads(data)["outcome"] == "cancelled"
							deliver("cancel-before" if cancelled else "write-before")
						result = original_write(path_to_write, data, *args, **kwargs)
						if final:
							final_saved = True
							deliver("cancel-after" if cancelled else "write-after")
						return result
					def restore(sig, next_handler):
						handoff = sig == signal.SIGINT and next_handler is handler and final_saved
						if handoff:
							deliver("restore-before")
						result = original_signal(sig, next_handler)
						if handoff:
							deliver("restore-after")
						return result
					def pending_signals():
						result = original_pending()
						deliver("pending-after")
						return result
					arguments = [sys.executable, "-B", "-c", "print('finished child')"]
					if mode == "nonzero":
						arguments[-1] = "raise SystemExit(7)"
					elif mode == "timeout":
						arguments[-1] = "import time; time.sleep(10)"
					elif mode == "launch-failed":
						arguments = [self.directory / "missing-command"]
					started = time.monotonic()
					try:
						with mock.patch.object(Path, "write_text", write), \
								mock.patch.object(signal, "signal", restore), \
								mock.patch.object(signal, "sigpending", pending_signals):
							try:
								self.command(arguments, expected=7 if mode == "nonzero" else 0,
									timeout=0.05 if mode == "timeout" else 2)
								facts["observed"] = "returned"
							except KeyboardInterrupt:
								facts["observed"] = "cancelled"
					finally:
						facts["handler_restored"] = signal.getsignal(signal.SIGINT) is handler
						facts["mask_restored"] = signal.pthread_sigmask(signal.SIG_BLOCK, set()) == original_mask
						original_signal(signal.SIGINT, original_handler)
					facts["elapsed_seconds"] = time.monotonic() - started
					record = json.loads(path.read_text())
					facts["record"] = record
					facts["sentinel_survived"] = sentinel.poll() is None
					if record["pid"] is not None:
						try:
							facts["waitpid"] = os.waitpid(record["pid"], os.WNOHANG)
						except ChildProcessError:
							facts["direct_child_reaped"] = True
						facts["live_group"] = _live_command_group(record["pgid"])
					(self.directory / f"finalization-{mode}.json").write_text(json.dumps(facts, indent=2) + "\n")
					self.assertEqual(facts["observed"], "cancelled")
					self.assertEqual(record["outcome"], "cancelled")
					self.assertEqual(record["exception"]["type"], "KeyboardInterrupt")
					self.assertTrue(facts["signals"])
					self.assertFalse(stages)
					self.assertLessEqual(facts["final_writes"], 3)
					self.assertLess(facts["elapsed_seconds"], 5)
					self.assertTrue(facts["handler_restored"])
					self.assertTrue(facts["mask_restored"])
					self.assertTrue(facts["sentinel_survived"])
					if record["pid"] is not None:
						self.assertTrue(facts["direct_child_reaped"])
						self.assertEqual(facts["live_group"], [])
						self.assertTrue(record["cleanup"]["direct_child_reaped"])
						self.assertEqual(record["cleanup"]["errors"], [])
					else:
						self.assertEqual(mode, "launch-failed")
		finally:
			original_signal(signal.SIGINT, original_handler)
			sentinel.terminate()
			try:
				sentinel.wait(timeout=1)
			except subprocess.TimeoutExpired:
				sentinel.kill()
				sentinel.wait(timeout=1)
			(self.directory / "finalization-owner.json").write_text(json.dumps({
				"sentinel_pid": sentinel.pid, "exit": sentinel.returncode,
				"direct_sentinel_reaped": sentinel.returncode is not None,
			}, indent=2) + "\n")

	def test_cleanup_failure_cannot_pass(self):
		with mock.patch.dict(globals(), {"_live_command_group": mock.Mock(side_effect=OSError("probe scan failure"))}):
			with self.assertRaisesRegex(RuntimeError, "probe scan failure"):
				self.command([sys.executable, "-B", "-c", "print('completed')"])
		record = json.loads((self.directory / "command-001.json").read_text())
		self.assertEqual(record["outcome"], "cleanup_failed")
		self.assertTrue(record["cleanup"]["direct_child_reaped"])
		self.assertEqual(record["cleanup"]["remaining_owner"], {"pid": record["pid"], "pgid": record["pgid"]})

class ElfToolchainTests(FixtureCase):
	def test_tiny_genuine_debug_tls_bss_strip_and_both_cli_modes(self):
		for tool in ("g++", "strip", "readelf", "sha256sum"):
			self.assertIsNotNone(shutil.which(tool), f"required prerequisite missing: {tool}")
		code = self.write("tiny.cpp", b"int initialized = 7;\nint bss[16];\n"
			b"thread_local int tls = 3;\nthread_local int tls_bss[4];\n"
			b"int main() { return initialized + bss[3] + tls + tls_bss[2]; }\n"
			+ b"".join(f"int qualification_page_backing_symbol_{index};\n".encode()
				for index in range(64)))
		original = self.directory / "tiny.debug"
		stripped = self.directory / "tiny.stripped"
		self.command(["g++", "-g", "-O0", code, "-o", original])
		before = original.stat()
		digest = self.digest(original)
		self.assertFalse(stripped.exists())
		self.command(["strip", "--strip-debug", "--no-merge-notes", "-o", stripped, original])
		sections = self.command(["readelf", "-SW", original]).stdout
		copy_sections = self.command(["readelf", "-SW", stripped]).stdout
		for name in (".debug_info", ".data", ".bss", ".tdata", ".tbss"):
			self.assertIn(name, sections)
		self.assertNotIn(".debug_info", copy_sections)
		for optimized in (False, True):
			with self.subTest(optimize=optimized):
				report = self.cli(original, stripped, optimize=optimized)
				self.assertEqual(report["source"]["sha256"], digest)
				self.assertEqual(report["copy"]["sha256"], self.digest(stripped))
				self.assertFalse(report["exact_sha256_equal"])
		self.assertEqual(digest, self.digest(original))
		after = original.stat()
		self.assertEqual((before.st_dev, before.st_ino, before.st_size,
			before.st_mtime_ns, before.st_ctime_ns), (after.st_dev, after.st_ino,
			after.st_size, after.st_mtime_ns, after.st_ctime_ns))
		match = re.search(r"\[\s*\d+\]\s+\.text\s+PROGBITS\s+[0-9a-f]+\s+([0-9a-f]+)",
			copy_sections)
		self.assertIsNotNone(match)
		offset = int(match.group(1), 16)
		data = bytearray(stripped.read_bytes())
		data[offset] ^= 1
		bad = self.write("tiny.changed", data)
		for optimized in (False, True):
			self.assertIn("payload", self.cli(original, bad, optimize=optimized, valid=False))

	def test_sparse_metadata_beyond_four_gib_real_cli_and_digests(self):
		self.assertIsNotNone(shutil.which("sha256sum"), "required prerequisite missing: sha256sum")
		high = (1 << 32) + 0x1000
		data = self.baseline.copy()
		table = data[0x1000:]
		data = data[:0x1000]
		struct.pack_into("<Q", data, 40, high)
		paths = [self.directory / name for name in ("sparse-source.elf", "sparse-copy.elf")]
		for path in paths:
			with path.open("wb") as output:
				output.write(data)
				output.seek(high)
				output.write(table)
			self.assertGreater(path.stat().st_size, 4294967296)
			self.assertLess(path.stat().st_blocks * 512, 1 << 20)
			with path.open("rb") as input:
				input.seek(high + 64 + 4)
				self.assertEqual(input.read(4), b"\x01\0\0\0")
		report = self.cli(*paths, timeout=300)
		digests = [self.digest(path) for path in paths]
		self.assertEqual(digests[0], digests[1])
		for label, digest in zip(("source", "copy"), digests):
			self.assertEqual(report[label]["sha256"], digest)
			self.assertEqual(report[label]["header"]["e_shoff"], high)
			self.assertEqual(report[label]["reads"]["max_request"], 1048576)
		with paths[1].open("r+b") as output:
			output.seek(high + 64 + 48)
			output.write(struct.pack("<Q", 8))
		mutated_digest = self.digest(paths[1])
		self.assertNotEqual(mutated_digest, digests[1])
		self.assertIn("sh_addralign", self.cli(*paths, valid=False, timeout=300))
		with paths[1].open("r+b") as output:
			output.seek(40)
			output.write(struct.pack("<Q", high + len(table) + 8))
		self.assertIn("section table", self.cli(*paths, valid=False, timeout=300))
		(self.directory / "sparse-facts.json").write_text(json.dumps({
			"high_offset": high, "logical_size": paths[0].stat().st_size,
			"allocated_bytes": paths[0].stat().st_blocks * 512,
			"original_sha256": digests[0], "changed_sha256": mutated_digest,
			"max_read": report["source"]["reads"]["max_request"],
		}, indent=2) + "\n")

if __name__ == "__main__":
	if len(sys.argv) == 4 and sys.argv[1] == "--command-owner-probe":
		_command_owner_probe(sys.argv[2], sys.argv[3])
		sys.exit(0)
	print(f"ELF fixture/log root: {TEST_ROOT}", flush=True)
	print("The full suite includes real compile/strip and >4-GiB sparse hashing.", flush=True)
	unittest.main()
