#!/usr/bin/env python3

import copy
import hashlib
import json
import os
import platform
import subprocess
import sys
import tempfile
import unittest

import native_exec


HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE_EXEC = os.path.join(HERE, "native_exec.py")
CXX_VARIABLE = "NATIVE_EXEC_TEST_CXX"
SKIP_COMPILE_VARIABLE = "NATIVE_EXEC_TEST_SKIP_COMPILE"
HOST = {"platform": "Darwin", "machine": "arm64", "translated": False}
CANNED_NONCE = "canned-nonce-of-an-earlier-launch"
ACCEPTANCE_REASONS = (
	"unsupported-architecture",
	"translated-execution",
	"launch-failed",
	"nonzero-exit",
	"missing-executable",
	"architecture-mismatch",
)
ACTION_WORDS = ("rebuild", "make `if", "build the component", "gate the")

POSITIVE = r'''#include "native_report.h"

int main(int argc, char **argv) {
	if (NativeReport::Requested(argc, argv)) {
		return NativeReport::Print(argv[2]);
	}
	return 0;
}
'''

EXIT_THREE = r'''#include "native_report.h"

int main(int argc, char **argv) {
	if (NativeReport::Requested(argc, argv)) {
		NativeReport::Print(argv[2]);
		return 3;
	}
	return 0;
}
'''

LYING = r'''#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
	if (argc == 3 && !std::strcmp(argv[1], "--native-report")) {
		std::printf(
			"NATIVE_REPORT {\"schema\":1,\"nonce\":\"%s\","
			"\"buildArch\":\"@LIE@\",\"runningArch\":\"@LIE@\","
			"\"translated\":false}\n",
			argv[2]);
	}
	return 0;
}
'''

STALE = r'''#include "native_report.h"

int main(int argc, char **argv) {
	if (NativeReport::Requested(argc, argv)) {
		return NativeReport::Print("@NONCE@");
	}
	return 0;
}
'''

SILENT = r'''#include <cstdio>

int main() {
	std::printf("hello\n");
	return 0;
}
'''


def report_text(
		nonce,
		build="arm64",
		running="arm64",
		translated=False,
		schema=1):
	return native_exec.REPORT_PREFIX + json.dumps({
		"schema": schema,
		"nonce": nonce,
		"buildArch": build,
		"runningArch": running,
		"translated": translated,
	}) + "\n"


def echo_runner(**fields):
	def runner(argv, timeout):
		return {
			"exitCode": 0,
			"stdout": report_text(argv[2], **fields),
			"stderr": "",
			"durationMs": 1.0,
		}
	return runner


def exit_runner(code):
	def runner(argv, timeout):
		return {
			"exitCode": code,
			"stdout": report_text(argv[2]),
			"stderr": "failed\n",
			"durationMs": 1.0,
		}
	return runner


def raising_runner(error):
	def runner(argv, timeout):
		raise error
	return runner


def refusing_runner(argv, timeout):
	raise AssertionError(f"runner must not be called: {argv}")


def host_with(**changes):
	return lambda: dict(HOST, **changes)


def codes(problems):
	return [item["code"] for item in problems]


def sha256(path):
	with open(path, "rb") as file:
		return hashlib.sha256(file.read()).hexdigest()


class TemporaryFiles:
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix="native exec ")
		self.addCleanup(temporary.cleanup)
		self.root = temporary.name
		self.component = self.write("component", b"component bytes")

	def write(self, name, data):
		path = os.path.join(self.root, name)
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "wb") as file:
			file.write(data)
		return path

	def gate(self, runner=None, host=None, arch="arm64", path=None):
		return native_exec.run_gate(
			path or self.component,
			arch,
			timeout=5,
			runner=runner or echo_runner(),
			host=host or host_with())


class ReportTests(unittest.TestCase):
	def refusal(self, stdout, nonce=CANNED_NONCE):
		with self.assertRaises(native_exec.Refusal) as caught:
			native_exec.parse_report(stdout, nonce)
		return caught.exception

	def test_valid_line(self):
		report = native_exec.parse_report(report_text(CANNED_NONCE), CANNED_NONCE)
		self.assertEqual(report, {
			"schema": 1,
			"nonce": CANNED_NONCE,
			"buildArch": "arm64",
			"runningArch": "arm64",
			"translated": False,
		})

	def test_no_line(self):
		self.assertEqual(
			self.refusal("hello\n").code,
			"no-architecture-report")
		self.assertEqual(self.refusal("").code, "no-architecture-report")
		self.assertEqual(self.refusal(None).code, "no-architecture-report")

	def test_two_lines(self):
		text = report_text(CANNED_NONCE) * 2
		self.assertEqual(self.refusal(text).code, "no-architecture-report")

	def test_malformed_json(self):
		text = native_exec.REPORT_PREFIX + "{\"schema\":1,\n"
		self.assertEqual(self.refusal(text).code, "no-architecture-report")

	def test_missing_key(self):
		report = json.loads(report_text(CANNED_NONCE)[
			len(native_exec.REPORT_PREFIX):])
		del report["runningArch"]
		text = native_exec.REPORT_PREFIX + json.dumps(report) + "\n"
		self.assertEqual(self.refusal(text).code, "no-architecture-report")

	def test_wrong_types(self):
		for fields in (
				{"translated": "false"},
				{"schema": True},
				{"schema": 2},
				{"build": None}):
			with self.subTest(fields=fields):
				text = report_text(CANNED_NONCE, **fields)
				self.assertEqual(
					self.refusal(text).code,
					"no-architecture-report")

	def test_wrong_nonce(self):
		refusal = self.refusal(report_text(CANNED_NONCE), nonce="f" * 32)
		self.assertEqual(refusal.code, "report-nonce-mismatch")
		self.assertEqual(refusal.facts["report"]["nonce"], CANNED_NONCE)


class ControlledGateTests(TemporaryFiles, unittest.TestCase):
	def assertRefused(self, verdict, reason):
		self.assertEqual(verdict["verdict"], "REFUSED")
		self.assertEqual(verdict["reason"], reason)
		self.assertTrue(verdict["message"])
		self.assertGreaterEqual(verdict["finishedAt"], verdict["startedAt"])

	def test_controlled_runner_passes(self):
		verdict = self.gate()
		self.assertEqual(verdict["verdict"], "PASS")
		self.assertIsNone(verdict["reason"])
		self.assertEqual(verdict["schema"], native_exec.SCHEMA)
		self.assertEqual(verdict["tool"], native_exec.TOOL)
		self.assertEqual(verdict["host"]["declaredArch"], "arm64")
		self.assertEqual(
			verdict["executable"]["path"],
			os.path.realpath(self.component))
		self.assertEqual(
			verdict["executable"]["sha256"],
			sha256(self.component))
		execution = verdict["execution"]
		self.assertEqual(execution["argv"], [
			os.path.realpath(self.component),
			native_exec.REPORT_FLAG,
			execution["nonce"],
		])
		self.assertEqual(len(execution["nonce"]), 32)
		self.assertEqual(execution["report"]["nonce"], execution["nonce"])
		self.assertEqual(execution["exitCode"], 0)
		self.assertEqual(verdict["proves"], native_exec.PROVES)
		self.assertEqual(verdict["limits"], native_exec.LIMITS)

	def test_non_darwin_host(self):
		verdict = self.gate(
			runner=refusing_runner,
			host=host_with(platform="Linux"))
		self.assertRefused(verdict, "unsupported-host-platform")
		self.assertNotIn("execution", verdict)

	def test_declared_architecture_differs_from_gate(self):
		verdict = self.gate(
			runner=refusing_runner,
			host=host_with(machine="x86_64"))
		self.assertRefused(verdict, "host-mismatch")

	def test_translated_gate(self):
		for translated in (True, None):
			with self.subTest(translated=translated):
				verdict = self.gate(
					runner=refusing_runner,
					host=host_with(translated=translated))
				self.assertRefused(verdict, "host-mismatch")

	def test_unknown_declared_architecture(self):
		verdict = self.gate(
			runner=refusing_runner,
			host=host_with(machine="arm64e"),
			arch="arm64e")
		self.assertRefused(verdict, "host-mismatch")

	def test_bad_cpu_type(self):
		verdict = self.gate(runner=raising_runner(
			OSError(86, "Bad CPU type in executable")))
		self.assertRefused(verdict, "unsupported-architecture")
		self.assertEqual(verdict["execution"]["errno"], 86)
		self.assertIn("-DCMAKE_OSX_ARCHITECTURES=arm64", verdict["message"])

	def test_exec_format_error(self):
		verdict = self.gate(runner=raising_runner(
			OSError(8, "Exec format error")))
		self.assertRefused(verdict, "launch-failed")
		self.assertEqual(verdict["execution"]["errno"], 8)

	def test_refusal_from_launch_keeps_its_errno(self):
		refusal = native_exec.Refusal(
			"launch-failed",
			"errno 86 (Bad CPU type in executable)",
			{"errno": 86})
		verdict = self.gate(runner=raising_runner(refusal))
		self.assertRefused(verdict, "unsupported-architecture")

	def test_timeout(self):
		verdict = self.gate(runner=raising_runner(
			subprocess.TimeoutExpired(["component"], 5)))
		self.assertRefused(verdict, "launch-failed")
		self.assertIsNone(verdict["execution"]["errno"])
		self.assertIn("timed out after 5 s", verdict["message"])

	def test_real_launch_of_a_missing_file(self):
		missing = os.path.join(self.root, "missing")
		with self.assertRaises(native_exec.Refusal) as caught:
			native_exec.launch([missing], 5)
		self.assertEqual(caught.exception.code, "launch-failed")
		self.assertEqual(caught.exception.facts["errno"], 2)

	def test_real_launch_timeout(self):
		with self.assertRaises(native_exec.Refusal) as caught:
			native_exec.launch(["/bin/sleep", "5"], 0.2)
		self.assertEqual(caught.exception.code, "launch-failed")
		self.assertEqual(caught.exception.facts["timeoutSeconds"], 0.2)

	def test_translated_report(self):
		verdict = self.gate(runner=echo_runner(
			build="x86_64",
			running="x86_64",
			translated=True))
		self.assertRefused(verdict, "translated-execution")
		self.assertTrue(verdict["execution"]["report"]["translated"])

	def test_nonzero_exit(self):
		verdict = self.gate(runner=exit_runner(3))
		self.assertRefused(verdict, "nonzero-exit")
		self.assertIn("exited with code 3", verdict["message"])
		self.assertEqual(verdict["execution"]["stderr"], "failed\n")

	def test_signal_exit(self):
		verdict = self.gate(runner=exit_runner(-9))
		self.assertRefused(verdict, "nonzero-exit")
		self.assertIn("signal 9", verdict["message"])

	def test_running_architecture_mismatch(self):
		verdict = self.gate(runner=echo_runner(running="x86_64"))
		self.assertRefused(verdict, "architecture-mismatch")

	def test_build_architecture_mismatch(self):
		verdict = self.gate(runner=echo_runner(build="x86_64"))
		self.assertRefused(verdict, "architecture-mismatch")

	def test_missing_executable(self):
		for path in (os.path.join(self.root, "missing"), self.root):
			with self.subTest(path=path):
				verdict = self.gate(runner=refusing_runner, path=path)
				self.assertRefused(verdict, "missing-executable")
				self.assertNotIn("execution", verdict)

	def test_acceptance_reasons_are_distinct_and_actionable(self):
		verdicts = [
			self.gate(runner=raising_runner(
				OSError(86, "Bad CPU type in executable"))),
			self.gate(runner=echo_runner(translated=True)),
			self.gate(runner=raising_runner(OSError(8, "Exec format error"))),
			self.gate(runner=exit_runner(3)),
			self.gate(
				runner=refusing_runner,
				path=os.path.join(self.root, "missing")),
			self.gate(runner=echo_runner(running="x86_64")),
		]
		reasons = [verdict["reason"] for verdict in verdicts]
		self.assertEqual(tuple(reasons), ACCEPTANCE_REASONS)
		self.assertEqual(len(set(reasons)), len(reasons))
		for verdict in verdicts:
			with self.subTest(reason=verdict["reason"]):
				self.assertTrue(verdict["message"])
				self.assertTrue(
					any(word in verdict["message"] for word in ACTION_WORDS),
					verdict["message"])


class VerdictTests(TemporaryFiles, unittest.TestCase):
	def setUp(self):
		super().setUp()
		self.verdict = self.gate()
		self.assertEqual(self.verdict["verdict"], "PASS")

	def test_pass_verdict_is_usable(self):
		self.assertEqual(
			native_exec.check_verdict(self.verdict, self.component),
			[])

	def test_verdict_without_execution(self):
		verdict = copy.deepcopy(self.verdict)
		del verdict["execution"]
		self.assertEqual(
			codes(native_exec.check_verdict(verdict, self.component)),
			["gate-no-execution"])
		verdict = copy.deepcopy(self.verdict)
		verdict["execution"]["report"] = None
		self.assertEqual(
			codes(native_exec.check_verdict(verdict, self.component)),
			["gate-no-execution"])

	def test_altered_report_nonce(self):
		verdict = copy.deepcopy(self.verdict)
		verdict["execution"]["report"]["nonce"] = CANNED_NONCE
		self.assertEqual(
			codes(native_exec.check_verdict(verdict, self.component)),
			["gate-nonce"])

	def test_another_path(self):
		other = self.write("other", b"component bytes")
		self.assertEqual(
			codes(native_exec.check_verdict(self.verdict, other)),
			["gate-executable-mismatch"])
		self.assertEqual(
			codes(native_exec.check_verdict(self.verdict, None)),
			["gate-executable-mismatch", "gate-stale-binary"])

	def test_symlinked_directory_is_the_same_file(self):
		link = os.path.join(self.root, "linked dir")
		os.symlink(self.root, link)
		self.assertEqual(
			native_exec.check_verdict(
				self.verdict,
				os.path.join(link, "component")),
			[])

	def test_rebuilt_after_gating(self):
		with open(self.component, "ab") as file:
			file.write(b" rebuilt")
		self.assertEqual(
			codes(native_exec.check_verdict(self.verdict, self.component)),
			["gate-stale-binary"])

	def test_removed_after_gating(self):
		os.remove(self.component)
		self.assertEqual(
			codes(native_exec.check_verdict(self.verdict, self.component)),
			["gate-stale-binary"])

	def test_refused_verdict(self):
		verdict = self.gate(runner=exit_runner(3))
		self.assertEqual(
			codes(native_exec.check_verdict(verdict, self.component)),
			["gate-not-passed"])

	def test_not_a_gate_verdict(self):
		for verdict in (None, {}, dict(self.verdict, tool="ninja_target")):
			with self.subTest(verdict=type(verdict).__name__):
				self.assertEqual(
					codes(native_exec.check_verdict(verdict, self.component)),
					["gate-not-passed"])

	def test_pass_contradicted_by_its_record(self):
		for path, value in (
				(("execution", "report", "translated"), True),
				(("execution", "report", "runningArch"), "x86_64"),
				(("execution", "exitCode"), 3),
				(("host", "translated"), True)):
			with self.subTest(path=path):
				verdict = copy.deepcopy(self.verdict)
				target = verdict
				for key in path[:-1]:
					target = target[key]
				target[path[-1]] = value
				self.assertEqual(
					codes(native_exec.check_verdict(verdict, self.component)),
					["gate-not-passed"])


class CompiledGateTests(unittest.TestCase):
	"""Real fixtures compiled against native_report.h and launched."""

	@classmethod
	def setUpClass(cls):
		if os.environ.get(SKIP_COMPILE_VARIABLE) == "1":
			raise unittest.SkipTest(
				f"{SKIP_COMPILE_VARIABLE}=1: compiled fixtures skipped")
		if platform.system() != "Darwin":
			raise unittest.SkipTest("the gate and its fixtures are macOS only")
		cls.host = platform.machine()
		if cls.host not in native_exec.ARCHITECTURES:
			raise AssertionError(f"unsupported host architecture {cls.host!r}")
		cls.other = next(
			arch for arch in native_exec.ARCHITECTURES if arch != cls.host)
		cls.cxx = os.environ.get(CXX_VARIABLE) or cls.tool_output(
			["xcrun", "--find", "clang++"])
		cls.sdk = os.environ.get("SDKROOT") or cls.tool_output(
			["xcrun", "--show-sdk-path"])
		temporary = tempfile.TemporaryDirectory(prefix="native exec fx ")
		cls.temporary = temporary
		cls.root = temporary.name
		try:
			cls.positive = cls.compile("positive", POSITIVE, cls.host)
			cls.exit_three = cls.compile("exit three", EXIT_THREE, cls.host)
			cls.lying = cls.compile(
				"lying",
				LYING.replace("@LIE@", cls.other),
				cls.host)
			cls.stale = cls.compile(
				"stale",
				STALE.replace("@NONCE@", CANNED_NONCE),
				cls.host)
			cls.silent = cls.compile("silent", SILENT, cls.host)
			cls.foreign = cls.compile("foreign", POSITIVE, cls.other)
			cls.object = cls.compile(
				"object",
				POSITIVE,
				cls.host,
				object_only=True)
			os.chmod(cls.object, 0o755)
		except BaseException:
			temporary.cleanup()
			raise

	@classmethod
	def tearDownClass(cls):
		cls.temporary.cleanup()

	@classmethod
	def tool_output(cls, argv):
		try:
			result = subprocess.run(
				argv,
				stdout=subprocess.PIPE,
				stderr=subprocess.STDOUT,
				text=True,
				check=False)
		except OSError as error:
			raise AssertionError(
				f"{argv[0]} is unavailable ({error}); set {CXX_VARIABLE} or "
				f"{SKIP_COMPILE_VARIABLE}=1 only on a host that cannot "
				"provide a compiler") from error
		if result.returncode != 0 or not result.stdout.strip():
			raise AssertionError(
				f"{' '.join(argv)} failed with exit {result.returncode}: "
				f"{result.stdout}; set {CXX_VARIABLE} or "
				f"{SKIP_COMPILE_VARIABLE}=1 only on a host that cannot "
				"provide a compiler")
		return result.stdout.strip().splitlines()[-1]

	@classmethod
	def compile(cls, name, source, arch, object_only=False):
		source_path = os.path.join(cls.root, name + ".cpp")
		with open(source_path, "w", encoding="utf-8") as file:
			file.write(source)
		output = os.path.join(cls.root, name + (".o" if object_only else ""))
		argv = [
			cls.cxx,
			"-std=c++20",
			"-Wall",
			"-Wextra",
			"-Werror",
			"-isysroot", cls.sdk,
			"-I", HERE,
			"-arch", arch,
		]
		if object_only:
			argv.append("-c")
		argv += [source_path, "-o", output]
		try:
			result = subprocess.run(
				argv,
				stdout=subprocess.PIPE,
				stderr=subprocess.STDOUT,
				text=True,
				check=False)
		except OSError as error:
			raise AssertionError(
				f"cannot run the compiler {cls.cxx!r} ({error}); set "
				f"{CXX_VARIABLE} or {SKIP_COMPILE_VARIABLE}=1 only on a host "
				"that cannot provide one") from error
		if result.returncode != 0:
			raise AssertionError(
				f"compiling {name} failed with exit {result.returncode}:\n"
				f"{' '.join(argv)}\n{result.stdout}")
		return output

	def archs(self, path):
		result = subprocess.run(
			["lipo", "-archs", path],
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT,
			text=True,
			check=False)
		self.assertEqual(result.returncode, 0, result.stdout)
		return result.stdout.split()

	def gate(self, path):
		return native_exec.run_gate(path, self.host, timeout=30)

	def rosetta_available(self):
		if self.host != "arm64":
			return False
		result = subprocess.run(
			["/usr/bin/arch", "-x86_64", "/usr/bin/true"],
			stdout=subprocess.DEVNULL,
			stderr=subprocess.DEVNULL,
			check=False)
		return result.returncode == 0

	def test_positive_runs_natively(self):
		verdict = self.gate(self.positive)
		self.assertEqual(verdict["verdict"], "PASS", verdict["message"])
		report = verdict["execution"]["report"]
		self.assertEqual(report["runningArch"], report["buildArch"])
		self.assertEqual(report["buildArch"], verdict["host"]["declaredArch"])
		self.assertEqual(verdict["host"]["declaredArch"], platform.machine())
		self.assertIs(report["translated"], False)
		self.assertIs(verdict["host"]["translated"], False)
		self.assertEqual(report["nonce"], verdict["execution"]["nonce"])
		self.assertEqual(verdict["execution"]["exitCode"], 0)
		self.assertEqual(native_exec.check_verdict(verdict, self.positive), [])
		again = self.gate(self.positive)
		self.assertEqual(again["verdict"], "PASS", again["message"])
		self.assertNotEqual(
			again["execution"]["nonce"],
			verdict["execution"]["nonce"])

	def test_nonzero_exit(self):
		verdict = self.gate(self.exit_three)
		self.assertEqual(verdict["reason"], "nonzero-exit", verdict["message"])
		self.assertEqual(verdict["execution"]["exitCode"], 3)

	def test_lying_report(self):
		verdict = self.gate(self.lying)
		self.assertEqual(
			verdict["reason"],
			"architecture-mismatch",
			verdict["message"])
		report = verdict["execution"]["report"]
		self.assertEqual(report["nonce"], verdict["execution"]["nonce"])
		self.assertEqual(report["runningArch"], self.other)

	def test_canned_nonce(self):
		verdict = self.gate(self.stale)
		self.assertEqual(
			verdict["reason"],
			"report-nonce-mismatch",
			verdict["message"])
		self.assertEqual(verdict["execution"]["report"]["nonce"], CANNED_NONCE)

	def test_silent_component(self):
		verdict = self.gate(self.silent)
		self.assertEqual(
			verdict["reason"],
			"no-architecture-report",
			verdict["message"])
		self.assertEqual(verdict["execution"]["stdout"], "hello\n")

	def test_foreign_architecture_never_passes(self):
		self.assertEqual(self.archs(self.foreign), [self.other])
		verdict = self.gate(self.foreign)
		if self.rosetta_available():
			self.assertEqual(
				verdict["reason"],
				"translated-execution",
				verdict["message"])
			self.assertIs(verdict["execution"]["report"]["translated"], True)
		else:
			self.assertEqual(
				verdict["reason"],
				"unsupported-architecture",
				verdict["message"])
			self.assertEqual(
				verdict["execution"]["errno"],
				native_exec.EBADARCH)
			self.assertIn(
				f"-DCMAKE_OSX_ARCHITECTURES={self.host}",
				verdict["message"])

	def test_object_file_naming_the_host_never_passes(self):
		self.assertEqual(self.archs(self.object), [self.host])
		verdict = self.gate(self.object)
		self.assertEqual(verdict["reason"], "launch-failed", verdict["message"])
		self.assertEqual(verdict["execution"]["errno"], 8)

	def test_missing_executable(self):
		verdict = self.gate(os.path.join(self.root, "never built"))
		self.assertEqual(verdict["reason"], "missing-executable")

	def cli(self, *arguments):
		return subprocess.run(
			[sys.executable, NATIVE_EXEC, *arguments],
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			text=True,
			check=False)

	def test_cli(self):
		out = os.path.join(self.root, "positive verdict.json")
		result = self.cli(
			"--executable", self.positive,
			"--host-arch", self.host,
			"--out", out)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		with open(out, encoding="utf-8") as file:
			written = json.load(file)
		self.assertEqual(written, json.loads(result.stdout))
		self.assertEqual(written["verdict"], "PASS")
		self.assertEqual(native_exec.check_verdict(written, self.positive), [])
		out = os.path.join(self.root, "silent verdict.json")
		result = self.cli(
			"--executable", self.silent,
			"--host-arch", self.host,
			"--out", out)
		self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
		with open(out, encoding="utf-8") as file:
			self.assertEqual(json.load(file)["reason"], "no-architecture-report")

	def test_cli_usage_errors(self):
		result = self.cli("--executable", self.positive)
		self.assertEqual(result.returncode, 2, result.stderr)
		result = self.cli(
			"--executable", self.positive,
			"--host-arch", self.host,
			"--timeout-seconds", "0")
		self.assertEqual(result.returncode, 2, result.stderr)
		before = sha256(self.positive)
		result = self.cli(
			"--executable", self.positive,
			"--host-arch", self.host,
			"--out", self.positive)
		self.assertEqual(result.returncode, 2, result.stderr)
		self.assertEqual(sha256(self.positive), before)


if __name__ == "__main__":
	unittest.main()
