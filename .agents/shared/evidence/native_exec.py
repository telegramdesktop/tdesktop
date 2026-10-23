#!/usr/bin/env python3

import argparse
import ctypes
import errno
import hashlib
import json
import math
import os
import platform
import secrets
import subprocess
import sys
import time


SCHEMA = 1
TOOL = "native_exec"
REPORT_FLAG = "--native-report"
REPORT_PREFIX = "NATIVE_REPORT "
REPORT_KEYS = ("schema", "nonce", "buildArch", "runningArch", "translated")
ARCHITECTURES = ("arm64", "x86_64")
DEFAULT_TIMEOUT = 30.0
EBADARCH = getattr(errno, "EBADARCH", 86)
OUTPUT_LIMIT = 4096
TRANSLATION_KEY = b"sysctl.proc_translated"
PROVES = (
	"on this macOS host, a gate process running natively as the declared "
	"architecture launched the exact file whose SHA-256 it read just before "
	"the launch; that file ran its NATIVE_REPORT entry to exit 0, echoed this "
	"launch's nonce and reported build and running architectures equal to "
	"the declared host architecture, without translation")
LIMITS = [
	"macOS (Mach-O) hosts only: other platforms are refused, not emulated, "
	"and nothing here says how they build, link or behave",
	"the report entry only: the gate does not run the component's campaign "
	"mode, so it does not show that the campaign works or that its results "
	"are correct",
	"one launch on this host: other hosts, architectures and build "
	"configurations are not covered",
	"self-reported architecture: the component's own native_report.h reading "
	"is trusted once the kernel launched it; arm64e reports as arm64",
	"file identity is read just before the launch; check_verdict re-reads it "
	"when the verdict is used, and checks consistency, not authenticity",
]


class Refusal(Exception):
	def __init__(self, code, message, facts=None):
		super().__init__(f"{code}: {message}")
		self.code = code
		self.message = message
		self.facts = facts or {}


def now_ms():
	return int(time.time() * 1000)


def is_int(value):
	return isinstance(value, int) and not isinstance(value, bool)


def process_translated():
	if platform.system() != "Darwin":
		return False
	libc = ctypes.CDLL(None, use_errno=True)
	libc.sysctlbyname.argtypes = [
		ctypes.c_char_p,
		ctypes.c_void_p,
		ctypes.POINTER(ctypes.c_size_t),
		ctypes.c_void_p,
		ctypes.c_size_t,
	]
	libc.sysctlbyname.restype = ctypes.c_int
	value = ctypes.c_int(0)
	size = ctypes.c_size_t(ctypes.sizeof(value))
	result = libc.sysctlbyname(
		TRANSLATION_KEY,
		ctypes.byref(value),
		ctypes.byref(size),
		None,
		0)
	if result == 0:
		return value.value == 1
	return False if ctypes.get_errno() == errno.ENOENT else None


def host_facts():
	return {
		"platform": platform.system(),
		"machine": platform.machine(),
		"translated": process_translated(),
	}


def file_sha256(path):
	digest = hashlib.sha256()
	with open(path, "rb") as file:
		for chunk in iter(lambda: file.read(1 << 20), b""):
			digest.update(chunk)
	return digest.hexdigest()


def file_identity(path):
	real = os.path.realpath(path)
	status = os.stat(real)
	return {
		"path": real,
		"size": status.st_size,
		"mtimeNs": status.st_mtime_ns,
		"sha256": file_sha256(real),
	}


def launch_refusal(error):
	if isinstance(error, subprocess.TimeoutExpired):
		return Refusal(
			"launch-failed",
			f"timed out after {error.timeout:g} s",
			{"timeoutSeconds": error.timeout})
	number = error.errno
	reason = error.strerror or os.strerror(number or 0)
	return Refusal(
		"launch-failed",
		f"errno {number} ({reason})",
		{"errno": number})


def call_runner(runner, argv, timeout):
	try:
		return runner(argv, timeout)
	except (OSError, subprocess.TimeoutExpired) as error:
		raise launch_refusal(error) from error


def launch(argv, timeout):
	started = time.monotonic()
	try:
		result = subprocess.run(
			argv,
			stdin=subprocess.DEVNULL,
			capture_output=True,
			text=True,
			encoding="utf-8",
			errors="replace",
			timeout=timeout,
			check=False)
	except (OSError, subprocess.TimeoutExpired) as error:
		raise launch_refusal(error) from error
	return {
		"exitCode": result.returncode,
		"stdout": result.stdout,
		"stderr": result.stderr,
		"durationMs": round((time.monotonic() - started) * 1000, 3),
	}


def protocol_advice():
	return ("make `if (NativeReport::Requested(argc, argv)) { return "
		"NativeReport::Print(argv[2]); }` the first statement of main(), "
		"before any application object, output or file work")


def parse_report(stdout, nonce):
	lines = [
		line for line in (stdout or "").splitlines()
		if line.startswith(REPORT_PREFIX)
	]
	if len(lines) != 1:
		raise Refusal(
			"no-architecture-report",
			f"stdout carries {len(lines)} {REPORT_PREFIX.strip()} lines, "
			"not exactly one; " + protocol_advice())
	try:
		report = json.loads(lines[0][len(REPORT_PREFIX):])
	except ValueError as error:
		raise Refusal(
			"no-architecture-report",
			f"the {REPORT_PREFIX.strip()} line is not JSON ({error}); "
			+ protocol_advice()) from error
	if not isinstance(report, dict) or set(report) != set(REPORT_KEYS):
		raise Refusal(
			"no-architecture-report",
			f"the report must hold exactly the keys {', '.join(REPORT_KEYS)}; "
			"print it only through NativeReport::Print")
	if (not is_int(report["schema"]) or report["schema"] != SCHEMA
		or not isinstance(report["nonce"], str)
		or not isinstance(report["buildArch"], str)
		or not isinstance(report["runningArch"], str)
		or not isinstance(report["translated"], bool)):
		raise Refusal(
			"no-architecture-report",
			f"the report is not schema {SCHEMA} with string nonce, buildArch, "
			"runningArch and a boolean translated; print it only through "
			"NativeReport::Print")
	if report["nonce"] != nonce:
		raise Refusal(
			"report-nonce-mismatch",
			f"the report echoes nonce {report['nonce']!r}, not this launch's; "
			"the component must pass the nonce it receives as argv[2] to "
			"NativeReport::Print, never print a canned report",
			{"report": report})
	return report


def rebuild_advice(host_arch):
	return (f"rebuild the component with -DCMAKE_OSX_ARCHITECTURES={host_arch} "
		f"(or -arch {host_arch}) and gate the rebuilt file")


def check_host(facts, host_arch):
	system = facts.get("platform")
	if system != "Darwin":
		raise Refusal(
			"unsupported-host-platform",
			f"the gate supports macOS hosts only and this host is {system!r}; "
			"run the component and the gate on a macOS host")
	machine = facts.get("machine")
	if host_arch not in ARCHITECTURES or host_arch != machine:
		raise Refusal(
			"host-mismatch",
			f"declared host architecture {host_arch!r} differs from the gate "
			f"process's {machine!r}; declare the architecture `uname -m` "
			"prints in a native shell")
	translated = facts.get("translated")
	if translated is None:
		raise Refusal(
			"host-mismatch",
			"cannot read sysctl.proc_translated for the gate process, so it "
			"cannot show that it runs natively; run the gate with a native "
			"python3 such as /usr/bin/python3")
	if translated:
		raise Refusal(
			"host-mismatch",
			"the gate process itself runs translated, so its machine reading "
			"is not the host's; run the gate with a native python3 such as "
			"/usr/bin/python3")


def read_identity(executable):
	if not os.path.isfile(executable):
		raise Refusal(
			"missing-executable",
			f"{executable} is not an existing regular file; build the "
			"component and pass the exact path of the linked executable")
	try:
		return file_identity(executable)
	except OSError as error:
		raise Refusal(
			"missing-executable",
			f"{executable} cannot be read ({error.strerror}); pass a readable "
			"linked executable") from error


def launch_failure(refusal, path, host_arch):
	number = refusal.facts.get("errno")
	if number == EBADARCH:
		return Refusal(
			"unsupported-architecture",
			f"the kernel refused to launch {path}: {refusal.message}; the file "
			"has no slice this host runs natively; " + rebuild_advice(host_arch),
			refusal.facts)
	if "timeoutSeconds" in refusal.facts:
		return Refusal(
			"launch-failed",
			f"launching {path} {refusal.message}; the report entry must exit "
			"at once: " + protocol_advice(),
			refusal.facts)
	return Refusal(
		"launch-failed",
		f"launching {path} failed: {refusal.message}; the file is not an "
		"executable this host can start (an object file, a library, a script "
		"or a damaged image); gate the linked executable itself",
		refusal.facts)


def exit_failure(code, path):
	if code < 0:
		what = f"was terminated by signal {-code}"
	else:
		what = f"exited with code {code}"
	return Refusal(
		"nonzero-exit",
		f"{path} {what} in its report entry; read its stderr and "
		+ protocol_advice())


def check_report(report, host_arch):
	if report["translated"]:
		raise Refusal(
			"translated-execution",
			"the component reports translated execution (Rosetta), so it did "
			"not run natively; " + rebuild_advice(host_arch))
	if report["buildArch"] != host_arch or report["runningArch"] != host_arch:
		raise Refusal(
			"architecture-mismatch",
			f"the report says build {report['buildArch']!r}, running "
			f"{report['runningArch']!r}, but the declared host is "
			f"{host_arch!r}; " + rebuild_advice(host_arch)
			+ ", and print the report only through NativeReport::Print")


def run_gate(
		executable,
		host_arch,
		timeout=DEFAULT_TIMEOUT,
		runner=launch,
		host=host_facts):
	if not timeout > 0 or math.isinf(timeout):
		raise ValueError(f"timeout must be a finite positive number: {timeout}")
	started = now_ms()
	facts = host()
	verdict = {
		"schema": SCHEMA,
		"tool": TOOL,
		"verdict": "REFUSED",
		"reason": None,
		"message": "",
		"startedAt": started,
		"finishedAt": None,
		"host": dict(facts, declaredArch=host_arch),
		"executable": {"path": os.path.realpath(executable)},
		"proves": PROVES,
		"limits": LIMITS,
	}
	try:
		check_host(facts, host_arch)
		identity = read_identity(executable)
		verdict["executable"] = identity
		path = identity["path"]
		nonce = secrets.token_hex(16)
		execution = {
			"argv": [path, REPORT_FLAG, nonce],
			"nonce": nonce,
			"exitCode": None,
			"errno": None,
			"durationMs": None,
			"stdout": None,
			"stderr": None,
			"report": None,
		}
		verdict["execution"] = execution
		try:
			launched = call_runner(runner, list(execution["argv"]), timeout)
		except Refusal as refusal:
			execution["errno"] = refusal.facts.get("errno")
			raise launch_failure(refusal, path, host_arch) from refusal
		execution["exitCode"] = launched["exitCode"]
		execution["durationMs"] = launched.get("durationMs")
		execution["stdout"] = (launched.get("stdout") or "")[:OUTPUT_LIMIT]
		execution["stderr"] = (launched.get("stderr") or "")[:OUTPUT_LIMIT]
		if launched["exitCode"] != 0:
			raise exit_failure(launched["exitCode"], path)
		try:
			report = parse_report(launched.get("stdout"), nonce)
		except Refusal as refusal:
			execution["report"] = refusal.facts.get("report")
			raise
		execution["report"] = report
		check_report(report, host_arch)
		verdict["verdict"] = "PASS"
		verdict["message"] = (
			f"{path} ran natively as {host_arch} and echoed this launch's "
			"nonce")
	except Refusal as refusal:
		verdict["reason"] = refusal.code
		verdict["message"] = refusal.message
	verdict["finishedAt"] = now_ms()
	return verdict


def problem(code, message):
	return {"code": code, "message": message}


def execution_supports_pass(verdict, execution, report):
	host = verdict.get("host") if isinstance(verdict.get("host"), dict) else {}
	declared = host.get("declaredArch")
	return (host.get("platform") == "Darwin"
		and host.get("machine") == declared
		and host.get("translated") is False
		and is_int(execution.get("exitCode"))
		and execution.get("exitCode") == 0
		and report.get("translated") is False
		and declared in ARCHITECTURES
		and report.get("buildArch") == declared
		and report.get("runningArch") == declared)


def check_verdict(verdict, executable):
	if (not isinstance(verdict, dict)
		or verdict.get("tool") != TOOL
		or not is_int(verdict.get("schema"))
		or verdict.get("schema") != SCHEMA):
		return [problem(
			"gate-not-passed",
			f"not a {TOOL} schema {SCHEMA} verdict; gate the executable with "
			"native_exec.py and pass its verdict")]
	if verdict.get("verdict") != "PASS":
		return [problem(
			"gate-not-passed",
			f"the gate refused the component ({verdict.get('reason')}): "
			f"{verdict.get('message')}")]
	problems = []
	execution = verdict.get("execution")
	report = execution.get("report") if isinstance(execution, dict) else None
	if not isinstance(report, dict):
		problems.append(problem(
			"gate-no-execution",
			"the verdict carries no execution record with a report, so it did "
			"not come from a launch; gate the executable again"))
	else:
		nonce = execution.get("nonce")
		if not isinstance(nonce, str) or not nonce or report.get("nonce") != nonce:
			problems.append(problem(
				"gate-nonce",
				"the report's nonce differs from the nonce the gate sent, so "
				"the report does not belong to that launch; gate it again"))
		if not execution_supports_pass(verdict, execution, report):
			problems.append(problem(
				"gate-not-passed",
				"the verdict says PASS but its own execution record does not "
				"support it; gate the executable again"))
	target = (os.path.realpath(executable)
		if isinstance(executable, str) and executable else None)
	identity = verdict.get("executable")
	gated = identity.get("path") if isinstance(identity, dict) else None
	if (target is None
		or not isinstance(gated, str)
		or os.path.realpath(gated) != target):
		problems.append(problem(
			"gate-executable-mismatch",
			f"the verdict gated {gated!r}, not {target!r}; gate the file the "
			"campaign runs"))
	expected = identity.get("sha256") if isinstance(identity, dict) else None
	try:
		current = file_sha256(target) if target else None
	except OSError:
		current = None
	if not isinstance(expected, str) or current != expected:
		problems.append(problem(
			"gate-stale-binary",
			"the file's current SHA-256 differs from the gated one (rebuilt, "
			"replaced or unreadable since gating); gate the current file "
			"again"))
	return problems


def positive_seconds(text):
	try:
		value = float(text)
	except ValueError as error:
		raise argparse.ArgumentTypeError(f"not a number: {text!r}") from error
	if not value > 0 or math.isinf(value):
		raise argparse.ArgumentTypeError(
			f"must be a finite number above zero: {text!r}")
	return value


def parse_args(argv=None):
	parser = argparse.ArgumentParser(
		description=(
			"Launch one standalone evidence executable in its NATIVE_REPORT "
			"entry with a fresh nonce and decide whether it runs natively on "
			"this macOS host as the declared architecture. Exit 0 on PASS, "
			"1 on a refusal, 2 on a usage error."))
	parser.add_argument("--executable", required=True,
		help="exact path of the linked component executable")
	parser.add_argument("--host-arch", required=True, choices=ARCHITECTURES,
		help="architecture this host runs natively (`uname -m`)")
	parser.add_argument("--timeout-seconds", type=positive_seconds,
		default=DEFAULT_TIMEOUT,
		help=f"launch timeout (default: {DEFAULT_TIMEOUT:g})")
	parser.add_argument("--out",
		help="also write the verdict JSON to this file")
	options = parser.parse_args(argv)
	if options.out and (
			os.path.realpath(options.out) == os.path.realpath(options.executable)):
		parser.error("--out must not name the executable under test")
	return options


def main(argv=None):
	options = parse_args(argv)
	verdict = run_gate(
		options.executable,
		options.host_arch,
		options.timeout_seconds)
	text = json.dumps(verdict, indent=2, sort_keys=True)
	print(text)
	if options.out:
		try:
			with open(options.out, "w", encoding="utf-8") as file:
				file.write(text + "\n")
		except OSError as error:
			print(f"error: cannot write {options.out}: {error}", file=sys.stderr)
			return 2
	return 0 if verdict["verdict"] == "PASS" else 1


if __name__ == "__main__":
	sys.exit(main())
