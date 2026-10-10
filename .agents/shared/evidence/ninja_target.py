#!/usr/bin/env python3

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
import os
import platform
import re
import shlex
import subprocess
import sys
from typing import Dict, List, Optional, Tuple


SCHEMA = 1
SUPPORTED_GENERATORS = {"Ninja", "Ninja Multi-Config"}
MIN_NINJA_VERSION = (1, 10)
COMPILER_PATTERN = re.compile(
	r"(?:[A-Za-z0-9_]+-)*(?:c\+\+|g\+\+|gcc|cc|clang|clang\+\+)"
	r"(?:-[0-9]+(?:\.[0-9]+)*)?"
)
LAUNCHERS = {"ccache", "sccache"}
LINK_SEPARATE_ARGS = frozenset([
	"-o",
	"-arch",
	"-framework",
	"-weak_framework",
	"-Xlinker",
	"-target",
	"-isysroot",
	"-L",
	"-l",
	"-u",
	"-e",
	"-install_name",
])
COMPILE_SEPARATE_ARGS = frozenset([
	"-o",
	"-c",
	"-MF",
	"-MT",
	"-MQ",
	"-x",
	"-Xclang",
	"-include",
	"-include-pch",
	"-I",
	"-isystem",
	"-iquote",
	"-idirafter",
	"-iframework",
	"-F",
	"-isysroot",
	"-arch",
	"-target",
	"-D",
	"-U",
])
QUERY_CHUNK = 512
ALLOWED_TOOLS = {"query", "compdb"}
SHELL_REFUSED_CHARACTERS = "\n\r$`;|<>()"
SHELL_PUNCTUATION = ";&|<>()"
AND_OPERATOR = re.compile(r"(?<=[ \t])&&(?=[ \t])")
VERSION_PATTERN = re.compile(r"^([0-9]+)\.([0-9]+)")
CACHE_LINE_PATTERN = re.compile(
	r'^(?:"([^"]+)"|([^\s:="#/][^:="]*)):([A-Za-z_]+)=(.*)$'
)
MACRO_NAME_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
OBJECT_SUFFIXES = (".o", ".obj")
SHARED_LIBRARY_PATTERN = re.compile(r"(?:\.so(?:\.[0-9]+)*|\.dylib|\.tbd)$")
COMPDB_KEYS = ("directory", "command", "file", "output")
QUERY_SECTIONS = {
	"  validations:": "validations",
	"  outputs:": "outputs",
	"  validation for:": "validation_for",
}
QUERY_INPUT_PREFIX = "  input: "
QUERY_ENTRY_PREFIX = "    "
COMPILE_RULE_PATTERN = re.compile(r"(?:C|CXX|OBJC|OBJCXX)_COMPILER__.+")
PCH_SUFFIXES = (".pch", ".gch")
CACHE_BOOL_VALUES = {
	"ON": True,
	"TRUE": True,
	"YES": True,
	"1": True,
	"OFF": False,
	"FALSE": False,
	"NO": False,
	"0": False,
}
FAILURE_LIST_LIMIT = 10
LIMITS = [
	"configured graph only: Ninja's evaluated -t query and -t compdb -x "
	"records, not a build log",
	"no execution proof: nothing here shows that a command ran or that an "
	"object exists on disk",
	"no archive-member reachability: archives are listed, their members are "
	"not resolved",
	"command-line definitions only: macros from headers, the compiler's "
	"built-ins or source code are not observed",
]


class NinjaTargetError(RuntimeError):
	pass


@dataclass
class Build:
	build_dir: str
	manifest: str
	config: str
	generator: str
	configured: str
	cache: Dict[str, Tuple[str, str]]
	host: Optional[str] = None
	path_mapping: Optional[Dict[str, str]] = None

	def identity(self, token):
		return identity(token, self.configured)


@dataclass
class QueryNode:
	rule: Optional[str]
	explicit: List[str] = field(default_factory=list)
	implicit: List[str] = field(default_factory=list)
	order_only: List[str] = field(default_factory=list)
	validations: List[str] = field(default_factory=list)
	outputs: List[str] = field(default_factory=list)
	validation_for: List[str] = field(default_factory=list)


@dataclass
class CompdbEntry:
	directory: str
	command: str
	file: str
	output: str


@dataclass
class LinkCommand:
	output: str
	cwd: str
	driver: str
	launcher: Optional[str]
	objects: List[str]
	archives: Dict[str, int]
	shared_libraries: List[str]


@dataclass
class CompileCommand:
	output: str
	source: str
	driver: str
	launcher: Optional[str]
	language: Optional[str]
	definitions: List[Tuple[str, str, Optional[str]]]
	pch: Optional[str] = None
	pch_header: Optional[str] = None
	pch_dialect: Optional[str] = None
	emits_pch: bool = False
	forced_includes: List[str] = field(default_factory=list)


def execute(argv):
	result = subprocess.run(
		argv,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		text=True,
		check=False,
	)
	return result.returncode, result.stdout, result.stderr


def ninja_tool_argv(ninja, host_dir, manifest, tool_args):
	tool_args = list(tool_args)
	if not tool_args or tool_args[0] not in ALLOWED_TOOLS:
		raise NinjaTargetError(
			f"refusing ninja tool {tool_args[:1]}: only query and compdb")
	tool = tool_args[0]
	if tool_args == ["compdb", "-h"]:
		operands = []
	elif tool == "compdb":
		if tool_args[1:2] != ["-x"] or len(tool_args) < 3:
			raise NinjaTargetError(
				"refusing ninja -t compdb without -x and explicit rules")
		operands = tool_args[2:]
	else:
		operands = tool_args[1:]
		if not operands:
			raise NinjaTargetError("refusing ninja -t query without paths")
	for operand in operands:
		if not operand or operand.startswith("-") or "\n" in operand:
			raise NinjaTargetError(
				f"refusing ninja -t {tool} operand {operand!r}")
	return [ninja, "-C", host_dir, "-f", manifest, "-t", *tool_args]


def run_ninja(ninja, host_dir, manifest, tool_args, runner=execute):
	argv = ninja_tool_argv(ninja, host_dir, manifest, tool_args)
	code, stdout, stderr = runner(argv)
	if code != 0:
		raise NinjaTargetError(
			f"ninja -t {' '.join(tool_args[:2])} failed with exit {code}: "
			f"{stderr.strip() or stdout.strip()}")
	return stdout


def check_ninja(ninja, host_dir, manifest, runner=execute):
	code, stdout, stderr = runner([ninja, "--version"])
	if code != 0:
		raise NinjaTargetError(
			f"ninja --version failed with exit {code}: {stderr.strip()}")
	version = stdout.strip()
	match = VERSION_PATTERN.match(version)
	if not match:
		raise NinjaTargetError(f"unrecognized ninja version {version!r}")
	if (int(match.group(1)), int(match.group(2))) < MIN_NINJA_VERSION:
		raise NinjaTargetError(
			f"ninja {version} is too old: need >= "
			f"{MIN_NINJA_VERSION[0]}.{MIN_NINJA_VERSION[1]}")
	argv = ninja_tool_argv(ninja, host_dir, manifest, ["compdb", "-h"])
	_, stdout, stderr = runner(argv)
	usage = stdout + stderr
	if not re.search(r"^\s*-x\s", usage, re.MULTILINE):
		raise NinjaTargetError(
			f"ninja {version} lacks the -t compdb -x capability "
			"(response file expansion)")
	return version


def read_cmake_cache(path):
	cache = {}
	with open(path, "r", encoding="utf-8", errors="surrogateescape") as file:
		for number, line in enumerate(file, 1):
			line = line.rstrip("\n").rstrip("\r")
			if not line.strip() or line.startswith("//") or line.startswith("#"):
				continue
			match = CACHE_LINE_PATTERN.match(line)
			if not match:
				raise NinjaTargetError(
					f"malformed CMakeCache.txt line {number}: {line[:80]!r}")
			name = match.group(1) or match.group(2)
			if name in cache:
				raise NinjaTargetError(
					f"duplicate CMakeCache.txt entry {name} at line {number}")
			cache[name] = (match.group(3), match.group(4))
	return cache


def cache_value(cache, name):
	if name not in cache:
		raise NinjaTargetError(f"CMakeCache.txt has no {name}")
	return cache[name][1]


def select_build(build_dir, manifest, config):
	if (not manifest
			or "/" in manifest
			or os.sep in manifest
			or manifest in (".", "..")):
		raise NinjaTargetError(
			f"manifest must be a plain file name, got {manifest!r}")
	cache_path = os.path.join(build_dir, "CMakeCache.txt")
	if not os.path.isfile(cache_path):
		raise NinjaTargetError(
			f"{build_dir} is not a CMake build tree: no CMakeCache.txt")
	if not os.path.isfile(os.path.join(build_dir, manifest)):
		raise NinjaTargetError(f"{build_dir} has no manifest {manifest}")
	cache = read_cmake_cache(cache_path)
	generator = cache_value(cache, "CMAKE_GENERATOR")
	if generator not in SUPPORTED_GENERATORS:
		raise NinjaTargetError(
			f"unsupported CMake generator {generator!r}: need one of "
			f"{sorted(SUPPORTED_GENERATORS)}")
	if generator == "Ninja Multi-Config":
		configurations = [
			value
			for value in cache_value(
				cache,
				"CMAKE_CONFIGURATION_TYPES").split(";")
			if value
		]
		if config not in configurations:
			raise NinjaTargetError(
				f"configuration {config!r} is not in "
				f"CMAKE_CONFIGURATION_TYPES {configurations}")
		expected = f"build-{config}.ninja"
	else:
		build_type = cache.get("CMAKE_BUILD_TYPE", ("", ""))[1]
		if build_type != config:
			raise NinjaTargetError(
				f"configuration {config!r} does not match single-config "
				f"CMAKE_BUILD_TYPE {build_type!r}")
		expected = "build.ninja"
	if manifest != expected:
		raise NinjaTargetError(
			f"manifest {manifest} does not select configuration {config} "
			f"for generator {generator!r}: expected {expected}")
	configured = cache_value(cache, "CMAKE_CACHEFILE_DIR")
	if not configured.startswith("/") or configured != configured.rstrip("/"):
		raise NinjaTargetError(
			f"unsupported CMAKE_CACHEFILE_DIR {configured!r}")
	return Build(
		build_dir=build_dir,
		manifest=manifest,
		config=config,
		generator=generator,
		configured=configured,
		cache=cache)


def bind_host(build, directory):
	if not directory.startswith("/"):
		raise NinjaTargetError(
			f"compdb directory {directory!r} is not absolute")
	if build.host is not None:
		if directory != build.host:
			raise NinjaTargetError(
				f"compdb directory {directory!r} differs from earlier "
				f"{build.host!r}")
		return
	if not os.path.samefile(directory, build.build_dir):
		raise NinjaTargetError(
			f"compdb directory {directory!r} is not --build-dir "
			f"{build.build_dir!r}")
	build.host = directory
	if directory == build.configured:
		build.path_mapping = None
		return
	if os.path.exists(build.configured):
		if not os.path.samefile(build.configured, directory):
			raise NinjaTargetError(
				f"CMAKE_CACHEFILE_DIR {build.configured!r} exists locally "
				f"but is not the build directory {directory!r}")
		validated_by = "samefile"
	else:
		validated_by = "CMakeCache.txt CMAKE_CACHEFILE_DIR"
	build.path_mapping = {
		"configured": build.configured,
		"host": directory,
		"validated_by": validated_by,
	}


def identity(token, configured):
	prefix = configured + "/"
	if token.startswith(prefix):
		return token[len(prefix):]
	return token


def check_graph_path(path, what):
	if not path or "\n" in path or "\r" in path:
		raise NinjaTargetError(f"unsupported {what} path {path!r}")
	components = path.split("/")
	if path.startswith("/"):
		components = components[1:]
	for component in components:
		if component in ("", ".", ".."):
			raise NinjaTargetError(
				f"unsupported {what} path {path!r}: empty, '.' or '..' "
				"component")
	return path


def snapshot_manifests(build_dir):
	candidates = [("CMakeCache.txt", os.path.join(build_dir, "CMakeCache.txt"))]
	for relative in ("", "CMakeFiles"):
		directory = os.path.join(build_dir, relative) if relative else build_dir
		if not os.path.isdir(directory):
			continue
		with os.scandir(directory) as entries:
			for entry in entries:
				if entry.name.endswith(".ninja") and entry.is_file():
					name = (f"{relative}/{entry.name}"
						if relative
						else entry.name)
					candidates.append((name, entry.path))
	snapshot = {}
	for name, path in sorted(candidates):
		stat = os.stat(path)
		digest = hashlib.sha256()
		with open(path, "rb") as file:
			for chunk in iter(lambda: file.read(1 << 20), b""):
				digest.update(chunk)
		snapshot[name] = (stat.st_size, stat.st_mtime_ns, digest.hexdigest())
	return snapshot


def snapshot_digest(snapshot):
	digest = hashlib.sha256()
	for name in sorted(snapshot):
		size, mtime_ns, sha256 = snapshot[name]
		digest.update(f"{name}\0{size}\0{mtime_ns}\0{sha256}\n".encode(
			"utf-8",
			"surrogateescape"))
	return digest.hexdigest()


def compare_snapshots(before, after):
	if before != after:
		changed = sorted(
			name
			for name in set(before) | set(after)
			if before.get(name) != after.get(name))
		raise NinjaTargetError(
			"build manifests changed during acquisition: "
			+ ", ".join(changed[:10]))


def parse_query(text):
	nodes = {}
	node = None
	section = None
	for number, line in enumerate(text.split("\n"), 1):
		if not line:
			continue
		if not line.startswith(" "):
			if not line.endswith(":") or len(line) < 2:
				raise NinjaTargetError(
					f"unknown ninja -t query line {number}: {line!r}")
			path = line[:-1]
			if path in nodes:
				raise NinjaTargetError(
					f"repeated ninja -t query header {path!r}")
			node = QueryNode(rule=None)
			nodes[path] = node
			section = None
			seen = set()
			continue
		if node is None:
			raise NinjaTargetError(
				f"ninja -t query line {number} outside a node: {line!r}")
		if line.startswith(QUERY_ENTRY_PREFIX):
			entry = line[len(QUERY_ENTRY_PREFIX):]
			if section is None or not entry:
				raise NinjaTargetError(
					f"ninja -t query entry outside a section at line "
					f"{number}: {line!r}")
			if section != "input":
				getattr(node, section).append(entry)
			elif entry.startswith("|| "):
				node.order_only.append(entry[3:])
			elif entry.startswith("| "):
				node.implicit.append(entry[2:])
			elif entry.startswith("|"):
				raise NinjaTargetError(
					f"unknown ninja -t query input marker at line {number}: "
					f"{line!r}")
			else:
				node.explicit.append(entry)
			continue
		if line.startswith(QUERY_INPUT_PREFIX):
			section = "input"
			node.rule = line[len(QUERY_INPUT_PREFIX):]
			if not node.rule:
				raise NinjaTargetError(
					f"empty ninja -t query rule at line {number}")
		elif line in QUERY_SECTIONS:
			section = QUERY_SECTIONS[line]
		else:
			raise NinjaTargetError(
				f"unknown ninja -t query line {number}: {line!r}")
		if section in seen:
			raise NinjaTargetError(
				f"repeated ninja -t query section at line {number}: "
				f"{line!r}")
		seen.add(section)
	return nodes


def query(run, paths):
	requested = list(dict.fromkeys(paths))
	nodes = {}
	for start in range(0, len(requested), QUERY_CHUNK):
		chunk = requested[start:start + QUERY_CHUNK]
		parsed = parse_query(run(["query", *chunk]))
		unexpected = [path for path in parsed if path not in chunk]
		missing = [path for path in chunk if path not in parsed]
		if unexpected or missing:
			raise NinjaTargetError(
				"non-canonical selector: requested "
				f"{missing[:5]} but ninja reported {unexpected[:5]}")
		nodes.update(parsed)
	return nodes


def load_compdb(text, directory=None):
	try:
		records = json.loads(text)
	except ValueError as error:
		raise NinjaTargetError(f"malformed ninja -t compdb output: {error}")
	if not isinstance(records, list):
		raise NinjaTargetError("ninja -t compdb output is not a JSON list")
	entries = {}
	for record in records:
		if not isinstance(record, dict) or not all(
				isinstance(record.get(key), str) for key in COMPDB_KEYS):
			raise NinjaTargetError(
				f"ninja -t compdb record lacks {', '.join(COMPDB_KEYS)}")
		entry = CompdbEntry(*(record[key] for key in COMPDB_KEYS))
		if directory is None:
			directory = entry.directory
		elif entry.directory != directory:
			raise NinjaTargetError(
				f"ninja -t compdb directory {entry.directory!r} differs "
				f"from {directory!r}")
		if entry.output in entries:
			raise NinjaTargetError(
				f"duplicate ninja -t compdb output {entry.output!r}")
		entries[entry.output] = entry
	return directory, entries


def compdb(run, build, rules, outputs, kind="compile"):
	rules = sorted(set(rules))
	directory, entries = load_compdb(
		run(["compdb", "-x", *rules]),
		build.host)
	if directory is not None:
		bind_host(build, directory)
	for output in outputs:
		if output not in entries:
			raise NinjaTargetError(f"missing {kind} entry for {output}")
	return entries


def split_command(command):
	for character in SHELL_REFUSED_CHARACTERS:
		if character in command:
			raise NinjaTargetError(
				f"unsupported shell syntax {character!r} in command")
	if "&" in AND_OPERATOR.sub("", command):
		raise NinjaTargetError(
			"unsupported shell syntax: '&' outside a whitespace-delimited "
			"'&&'")
	lexer = shlex.shlex(
		command,
		posix=True,
		punctuation_chars=SHELL_PUNCTUATION)
	lexer.whitespace_split = True
	lexer.commenters = ""
	try:
		tokens = list(lexer)
	except ValueError as error:
		raise NinjaTargetError(f"unsupported shell quoting: {error}")
	segments = [[]]
	for token in tokens:
		if token == "&&":
			segments.append([])
		elif token and all(
				character in SHELL_PUNCTUATION for character in token):
			raise NinjaTargetError(
				f"unsupported shell operator {token!r} in command")
		else:
			segments[-1].append(token)
	if any(not segment for segment in segments):
		raise NinjaTargetError("empty simple command in '&&' list")
	return segments


def split_driver(argv, what):
	launcher = None
	index = 0
	if os.path.basename(argv[0]) in LAUNCHERS:
		launcher = argv[0]
		index = 1
	if index >= len(argv) or not COMPILER_PATTERN.fullmatch(
			os.path.basename(argv[index])):
		driver = argv[index] if index < len(argv) else ""
		raise NinjaTargetError(f"unsupported {what} driver {driver!r}")
	return launcher, argv[index], index + 1


def parse_link(entry, target, build):
	segments = split_command(entry.command)
	cwd = entry.directory
	linker = None
	linker_cwd = None
	for segment in segments:
		if segment == [":"]:
			continue
		if segment[0] == "cd":
			if len(segment) != 2 or not segment[1].startswith("/"):
				raise NinjaTargetError(
					f"unsupported cd form in link command: {segment}")
			cwd = segment[1]
			continue
		links_target = any(
			segment[index] == "-o"
			and build.identity(segment[index + 1]) == target
			for index in range(len(segment) - 1))
		if links_target:
			if linker is not None:
				raise NinjaTargetError(
					f"several link commands produce {target}")
			linker = segment
			linker_cwd = cwd
	if linker is None:
		raise NinjaTargetError(f"no link command produces -o {target}")
	if linker_cwd not in (build.configured, build.host, entry.directory):
		raise NinjaTargetError(
			f"link command for {target} runs in {linker_cwd!r}, not the "
			"build directory")
	launcher, driver, index = split_driver(linker, "linker")
	output = None
	objects = []
	archives = {}
	shared = []
	while index < len(linker):
		token = linker[index]
		if token in LINK_SEPARATE_ARGS:
			if index + 1 >= len(linker):
				raise NinjaTargetError(f"link option {token} lacks a value")
			if token == "-o":
				if output is not None:
					raise NinjaTargetError("link command has several -o")
				output = build.identity(linker[index + 1])
			index += 2
			continue
		index += 1
		if token.startswith("@"):
			raise NinjaTargetError(
				f"unexpanded response file {token!r} in link command")
		if token.startswith("-"):
			continue
		operand = build.identity(token)
		if operand.endswith(OBJECT_SUFFIXES):
			objects.append(check_graph_path(operand, "object"))
		elif operand.endswith(".a"):
			archives[operand] = archives.get(operand, 0) + 1
		elif SHARED_LIBRARY_PATTERN.search(operand):
			shared.append(operand)
		else:
			raise NinjaTargetError(
				f"unsupported link operand {token!r}")
	return LinkCommand(
		output=output,
		cwd=linker_cwd,
		driver=driver,
		launcher=launcher,
		objects=objects,
		archives=archives,
		shared_libraries=shared)


def parse_definition(kind, text):
	if kind == "D":
		name, separator, value = text.partition("=")
		value = value if separator else "1"
	else:
		name, value = text, None
	if not MACRO_NAME_PATTERN.fullmatch(name):
		raise NinjaTargetError(f"unsupported definition -{kind}{text}")
	return (kind, name, value)


def refuse_define_channel(token):
	if token.startswith(("-imacros", "--imacros")):
		raise NinjaTargetError(f"unsupported definition channel {token!r}")
	if token.startswith("-Wp,") and any(
			part.startswith(("-D", "-U", "-imacros", "-include"))
			for part in token[4:].split(",")):
		raise NinjaTargetError(f"unsupported definition channel {token!r}")
	if token == "-Xpreprocessor" or token.startswith(
			("--define-macro", "--undefine-macro")):
		raise NinjaTargetError(f"unsupported definition channel {token!r}")
	if token.startswith(("-include", "--include")) and token not in (
			"-include",
			"-include-pch"):
		raise NinjaTargetError(f"unsupported include form {token!r}")


def parse_compile(entry, build, graph_inputs):
	segments = split_command(entry.command)
	if len(segments) != 1:
		raise NinjaTargetError(
			f"compile command for {entry.output} is not a single command")
	argv = segments[0]
	launcher, driver, index = split_driver(argv, "compiler")
	outputs = []
	sources = []
	language = None
	definitions = []
	includes = []
	xclang_pch = []
	xclang_includes = []
	emits = False
	claims_pch = False
	while index < len(argv):
		token = argv[index]
		if token.startswith("@"):
			raise NinjaTargetError(
				f"unexpanded response file {token!r} in compile command")
		refuse_define_channel(token)
		if token == "-Xclang":
			value = argv[index + 1] if index + 1 < len(argv) else None
			if value is None:
				raise NinjaTargetError("-Xclang lacks a value")
			if value in ("-include-pch", "-include"):
				if index + 3 >= len(argv) or argv[index + 2] != "-Xclang":
					raise NinjaTargetError(
						f"-Xclang {value} lacks its -Xclang operand")
				operand = build.identity(argv[index + 3])
				if value == "-include-pch":
					xclang_pch.append(operand)
				else:
					xclang_includes.append(operand)
				index += 4
				continue
			if value == "-emit-pch":
				if emits:
					raise NinjaTargetError("second -Xclang -emit-pch")
				emits = True
			elif value.startswith(("-D", "-U", "-imacros", "-include")):
				raise NinjaTargetError(
					f"unsupported definition channel -Xclang {value}")
			index += 2
			continue
		if token in COMPILE_SEPARATE_ARGS:
			if index + 1 >= len(argv):
				raise NinjaTargetError(f"compile option {token} lacks a value")
			value = argv[index + 1]
			index += 2
			if token == "-o":
				outputs.append(build.identity(value))
			elif token == "-c":
				sources.append(build.identity(value))
			elif token == "-x":
				language = value
			elif token in ("-D", "-U"):
				definitions.append(parse_definition(token[1], value))
			elif token == "-include":
				includes.append(build.identity(value))
			elif token == "-include-pch":
				xclang_pch.append(build.identity(value))
			continue
		index += 1
		if token == "-Winvalid-pch":
			claims_pch = True
		elif token.startswith(("-D", "-U")):
			definitions.append(parse_definition(token[1], token[2:]))
		elif not token.startswith("-"):
			raise NinjaTargetError(
				f"unexpected compile operand {token!r} for {entry.output}")
	if len(outputs) != 1 or len(sources) != 1:
		raise NinjaTargetError(
			f"compile command for {entry.output} needs exactly one -o and "
			"one -c")
	command = CompileCommand(
		output=check_graph_path(outputs[0], "object"),
		source=sources[0],
		driver=driver,
		launcher=launcher,
		language=language,
		definitions=definitions)
	classify_pch(command, xclang_pch, xclang_includes, includes, emits,
		claims_pch, {build.identity(path) for path in graph_inputs})
	return command


def classify_pch(
		command, xclang_pch, xclang_includes, includes, emits, claims_pch,
		inputs):
	if len(xclang_pch) > 1 or len(xclang_includes) > 1:
		raise NinjaTargetError(
			f"second PCH inclusion in compile command for {command.output}")
	if xclang_pch and emits:
		raise NinjaTargetError(
			f"compile command for {command.output} both uses and emits a PCH")
	if xclang_includes and not (xclang_pch or emits):
		raise NinjaTargetError(
			f"-Xclang -include outside the PCH form for {command.output}")
	if xclang_pch or emits:
		if emits and not xclang_includes:
			raise NinjaTargetError(
				f"-Xclang -emit-pch without its header for {command.output}")
		command.pch_dialect = "clang"
		command.pch_header = xclang_includes[0] if xclang_includes else None
		if emits:
			command.emits_pch = True
		else:
			command.pch = check_graph_path(xclang_pch[0], "PCH")
		command.forced_includes = list(includes)
		return
	if command.language and command.language.endswith("-header"):
		if len(includes) != 1 or command.output != includes[0] + ".gch":
			raise NinjaTargetError(
				f"unsupported header compile for {command.output}")
		command.pch_dialect = "gcc"
		command.pch_header = includes[0]
		command.emits_pch = True
		return
	used = [path for path in includes if path + ".gch" in inputs]
	if len(used) > 1:
		raise NinjaTargetError(
			f"second PCH inclusion in compile command for {command.output}")
	if claims_pch and not used:
		# CMake adds -Winvalid-pch only to commands that use a PCH, so a GCC
		# consumer without its .gch graph input is incoherent, not "no PCH".
		claimed = [path + ".gch" for path in includes] or ["a .gch"]
		raise NinjaTargetError(
			f"incoherent PCH for {command.output}: "
			f"{', '.join(claimed)} is used on the command line but is not an "
			"implicit graph input (-Winvalid-pch without a .gch input)")
	if used:
		command.pch_dialect = "gcc"
		command.pch_header = used[0]
		command.pch = check_graph_path(used[0] + ".gch", "PCH")
	command.forced_includes = [path for path in includes if path not in used]


def definition_state(definitions, name):
	state = (False, None)
	for kind, macro, value in definitions:
		if macro == name:
			state = (True, value) if kind == "D" else (False, None)
	return state


def macro_state(command, name):
	if command.forced_includes:
		raise NinjaTargetError(
			f"forced include may change requested macro {name} in "
			f"{command.output}: {command.forced_includes[:3]}")
	return definition_state(command.definitions, name)


@dataclass
class Expectation:
	macro: str
	defined: bool
	value: Optional[str]
	origin: str


@dataclass
class Produced:
	path: str
	graph_path: str
	edge: str
	rule: str
	command: CompileCommand
	implicit: List[str]


def utc_now():
	return datetime.now(timezone.utc).isoformat(timespec="seconds")


def check_selector(value, what):
	if "/" not in value:
		raise NinjaTargetError(
			f"--select-{what} {value!r}: a basename is not an identity; pass "
			"the exact graph path")
	return value


def parse_expectations(options, cache):
	expectations = {}
	contracts = []

	def add(macro, defined, value, origin):
		if not MACRO_NAME_PATTERN.fullmatch(macro):
			raise NinjaTargetError(f"{origin}: invalid macro name {macro!r}")
		if macro in expectations:
			raise NinjaTargetError(
				f"macro {macro} is requested twice ({expectations[macro].origin} "
				f"and {origin})")
		expectations[macro] = Expectation(macro, defined, value, origin)

	for text in options.expect_defined:
		name, separator, value = text.partition("=")
		add(name, True, value if separator else None, "--expect-defined")
	for text in options.expect_undefined:
		add(text, False, None, "--expect-undefined")
	for text in options.option_macro:
		option, separator, macro = text.partition("=")
		if not separator or not option or not macro:
			raise NinjaTargetError(
				f"--option-macro {text!r}: expected OPTION=MACRO")
		if option not in cache:
			raise NinjaTargetError(
				f"--option-macro {text}: CMakeCache.txt has no {option}")
		kind, value = cache[option]
		state = CACHE_BOOL_VALUES.get(value.upper())
		if kind != "BOOL" or state is None:
			raise NinjaTargetError(
				f"--option-macro {text}: cache entry {option}:{kind}={value} is "
				"not a BOOL ON/OFF value")
		add(macro, state, None, f"--option-macro {option}")
		contracts.append({
			"option": option,
			"macro": macro,
			"cache": f"{option}:{kind}={value}",
			"expect": "defined" if state else "undefined",
		})
	result = list(expectations.values())
	if result and not options.policy_target:
		raise NinjaTargetError(
			"--expect-defined, --expect-undefined and --option-macro require "
			"--policy-target")
	if any(not item.defined for item in result) and not any(
			item.defined for item in result):
		raise NinjaTargetError(
			"an absence expectation needs a known-present control: add at "
			"least one --expect-defined (or an ON --option-macro) for the same "
			"cohort")
	return result, contracts


def reconcile_link(build, target, node, link):
	counts = {}
	for path in link.objects:
		counts[path] = counts.get(path, 0) + 1
	duplicates = [path for path, count in counts.items() if count > 1]
	if duplicates:
		raise NinjaTargetError(
			f"duplicate object operand in link command for {target}: "
			f"{duplicates[:FAILURE_LIST_LIMIT]}")
	graph = {}
	for edge, paths in (("explicit", node.explicit), ("implicit", node.implicit)):
		for path in paths:
			key = build.identity(path)
			if not key.endswith(OBJECT_SUFFIXES):
				continue
			if key in graph:
				raise NinjaTargetError(
					f"duplicate object graph input {key} of {target}")
			graph[key] = (path, edge)
	order_only = {build.identity(path) for path in node.order_only}
	for path in link.objects:
		if path not in graph:
			detail = " (order-only only)" if path in order_only else ""
			raise NinjaTargetError(
				f"substituted or extra link operand {path}{detail}: not an "
				f"explicit or implicit graph input of {target}")
	missing = [path for path in graph if path not in counts]
	if missing:
		raise NinjaTargetError(
			f"missing link entry for graph input(s) of {target}: "
			f"{missing[:FAILURE_LIST_LIMIT]}")
	return [(path, graph[path][0], graph[path][1]) for path in link.objects]


def check_producer(build, graph_path, node, entry, what):
	path = build.identity(graph_path)
	command = parse_compile(entry, build, node.explicit + node.implicit)
	if command.output != path:
		raise NinjaTargetError(
			f"{what}producer/output mismatch for {path}: the compile command "
			f"writes {command.output}")
	if len(node.explicit) != 1:
		raise NinjaTargetError(
			f"{what}producer/source mismatch for {path}: "
			f"{len(node.explicit)} explicit graph inputs")
	source = build.identity(node.explicit[0])
	file = build.identity(entry.file)
	if command.source != source or file != source:
		raise NinjaTargetError(
			f"{what}producer/source mismatch for {path}: -c {command.source}, "
			f"graph input {source}, compdb file {file}")
	return command


def resolve_objects(build, run, target, consumed):
	nodes = query(run, [graph_path for _, graph_path, _ in consumed])
	externals = []
	pending = []
	for path, graph_path, edge in consumed:
		node = nodes[graph_path]
		if target not in node.outputs:
			raise NinjaTargetError(
				f"graph reverse edge missing: {graph_path} does not list "
				f"{target} among its consumers")
		if node.rule is None:
			externals.append(path)
			continue
		if not COMPILE_RULE_PATTERN.fullmatch(node.rule):
			raise NinjaTargetError(
				f"non-compile producer rule {node.rule} for object {path}")
		pending.append((path, graph_path, edge, node))
	entries = {}
	if pending:
		entries = compdb(
			run,
			build,
			[node.rule for _, _, _, node in pending],
			[graph_path for _, graph_path, _, _ in pending])
	produced = []
	for path, graph_path, edge, node in pending:
		command = check_producer(build, graph_path, node, entries[graph_path], "")
		if command.emits_pch:
			raise NinjaTargetError(
				f"incoherent PCH: consumed object {path} is produced by a "
				"PCH-emitting command")
		produced.append(Produced(
			path,
			graph_path,
			edge,
			node.rule,
			command,
			node.implicit))
		implicit = {build.identity(input): input for input in node.implicit}
		if command.pch is not None and command.pch not in implicit:
			raise NinjaTargetError(
				f"incoherent PCH for {path}: {command.pch} is used on the "
				"command line but is not an implicit graph input")
		for input in implicit:
			if input.endswith(PCH_SUFFIXES) and input != command.pch:
				raise NinjaTargetError(
					f"incoherent PCH for {path}: graph input {input} is not "
					"used on the command line")
	return produced, externals, entries


def resolve_pchs(build, run, produced, entries):
	users = {}
	graph_paths = {}
	for item in produced:
		pch = item.command.pch
		if pch is None:
			continue
		users.setdefault(pch, []).append(item)
		graph_paths[pch] = next(
			input
			for input in item.implicit
			if build.identity(input) == pch)
	if not users:
		return {}
	nodes = query(run, sorted(set(graph_paths.values())))
	pchs = {}
	for pch in sorted(users):
		graph_path = graph_paths[pch]
		node = nodes[graph_path]
		if node.rule is None:
			raise NinjaTargetError(f"incoherent PCH: {pch} has no producer")
		for item in users[pch]:
			if item.rule != node.rule:
				raise NinjaTargetError(
					f"incoherent PCH: {pch} is produced by rule {node.rule} but "
					f"used by {item.path} compiled by rule {item.rule}")
		entry = entries.get(graph_path)
		if entry is None:
			raise NinjaTargetError(
				f"incoherent PCH: missing compile entry for {pch}")
		command = check_producer(
			build,
			graph_path,
			node,
			entry,
			"incoherent PCH: ")
		if not command.emits_pch:
			raise NinjaTargetError(
				f"incoherent PCH: the producer of {pch} does not emit a PCH")
		for item in users[pch]:
			if (item.command.pch_header != command.pch_header
					or item.command.pch_dialect != command.pch_dialect):
				raise NinjaTargetError(
					f"incoherent PCH: {item.path} includes "
					f"{item.command.pch_header} ({item.command.pch_dialect}) "
					f"with {pch} built from {command.pch_header} "
					f"({command.pch_dialect})")
		pchs[pch] = (node.rule, command, len(users[pch]))
	return pchs


def describe_state(state):
	defined, value = state
	return f"defined={value}" if defined else "undefined"


def expectation_met(expectation, state):
	defined, value = state
	if defined != expectation.defined:
		return False
	return expectation.value is None or value == expectation.value


def evaluate_policy(build, name, produced, pchs, expectations, contracts):
	pattern = (
		r"(?:C|CXX|OBJC|OBJCXX)_COMPILER__" + re.escape(name)
		+ r"(?:_unscanned)?_" + re.escape(build.config))
	matcher = re.compile(pattern)
	members = [item for item in produced if matcher.fullmatch(item.rule)]
	if not members:
		raise NinjaTargetError(
			f"policy target {name} has no consumed objects compiled by a rule "
			f"matching {pattern}")
	used = sorted({item.command.pch for item in members if item.command.pch})
	checked = [(item.path, item.command) for item in members]
	checked += [(pch, pchs[pch][1]) for pch in used]
	if build.generator == "Ninja Multi-Config":
		expected = f'"{build.config}"'
		for path, command in checked:
			defined, value = definition_state(command.definitions, "CMAKE_INTDIR")
			if defined and value != expected:
				raise NinjaTargetError(
					f"CMAKE_INTDIR in {path} is {value!r}, expected {expected!r}")
	names = {}
	for item in members:
		for macro in sorted({macro for _, macro, _ in item.command.definitions}):
			if definition_state(item.command.definitions, macro)[0]:
				names[macro] = names.get(macro, 0) + 1
	readings = []
	for expectation in expectations:
		states = {
			path: macro_state(command, expectation.macro)
			for path, command in checked
		}
		mismatched = [
			item
			for item in members
			if item.command.pch
			and states[item.path] != states[item.command.pch]
		]
		if mismatched:
			raise NinjaTargetError(
				f"requested macro {expectation.macro} differs between object "
				"and PCH: " + ", ".join(
					f"{item.path} ({describe_state(states[item.path])}) vs "
					f"{item.command.pch} "
					f"({describe_state(states[item.command.pch])})"
					for item in mismatched[:FAILURE_LIST_LIMIT]))
		wanted = ("defined" if expectation.defined else "undefined") + (
			f"={expectation.value}" if expectation.value is not None else "")
		wrong = [
			(path, state)
			for path, state in states.items()
			if not expectation_met(expectation, state)
		]
		if wrong:
			raise NinjaTargetError(
				f"policy expectation unmet in cohort {name}: "
				f"{expectation.macro} expected {wanted} ({expectation.origin}); "
				f"{len(wrong)} of {len(checked)} objects/PCHs differ: "
				+ ", ".join(
					f"{path} ({describe_state(state)})"
					for path, state in wrong[:FAILURE_LIST_LIMIT]))
		readings.append({
			"macro": expectation.macro,
			"expected": wanted,
			"origin": expectation.origin,
			"objects": len(members),
			"pchs": len(used),
			"values": sorted({
				value
				for defined, value in states.values()
				if defined
			}),
		})
	rules = {}
	for item in members:
		rules[item.rule] = rules.get(item.rule, 0) + 1
	return {
		"target": name,
		"rule_pattern": pattern,
		"rules": rules,
		"objects": len(members),
		"pchs": used,
		"definition_names": names,
		"macros": readings,
		"option_contracts": contracts,
	}


def object_record(item, pchs):
	if isinstance(item, Produced):
		pch = item.command.pch
		return {
			"object": item.path,
			"edge": item.edge,
			"rule": item.rule,
			"source": item.command.source,
			"pch": pch,
			"pch_rule": pchs[pch][0] if pch else None,
			"external": False,
		}
	path, edge = item
	return {
		"object": path,
		"edge": edge,
		"rule": None,
		"source": None,
		"pch": None,
		"pch_rule": None,
		"external": True,
	}


def select_objects(build, run, target, selectors, index, pchs):
	selections = []
	for selector in selectors:
		path = build.identity(selector)
		if path in index:
			selections.append(object_record(index[path], pchs))
			continue
		node = query(run, [selector])[selector]
		raise NinjaTargetError(
			f"object {selector} not consumed by {target}; its graph consumers: "
			f"{node.outputs[:FAILURE_LIST_LIMIT]}")
	return selections


def select_sources(build, run, target, selectors, index, pchs):
	selections = []
	for selector in selectors:
		node = query(run, [selector])[selector]
		source = build.identity(selector)
		matching = []
		siblings = []
		for output in node.outputs:
			path = build.identity(output)
			item = index.get(path)
			if isinstance(item, Produced) and item.command.source == source:
				matching.append(item)
			elif item is None and path.endswith(OBJECT_SUFFIXES):
				siblings.append(output)
		if len(matching) > 1:
			raise NinjaTargetError(
				f"ambiguous source {selector}: {target} consumes "
				f"{[item.path for item in matching]}")
		if not matching:
			raise NinjaTargetError(
				f"source not consumed by {target}: {selector}; its graph "
				f"outputs: {node.outputs[:FAILURE_LIST_LIMIT]}")
		excluded = []
		if siblings:
			sibling_nodes = query(run, siblings)
			for output in siblings:
				excluded.append({
					"object": build.identity(output),
					"rule": sibling_nodes[output].rule,
					"consumers": sibling_nodes[output].outputs,
				})
		record = object_record(matching[0], pchs)
		record["selected_source"] = selector
		record["excluded_siblings"] = excluded
		selections.append(record)
	return selections


def resolve(options, runner=execute):
	for value in options.select_object:
		check_selector(value, "object")
	for value in options.select_source:
		check_selector(value, "source")
	started = utc_now()
	build = select_build(options.build_dir, options.manifest, options.config)
	expectations, contracts = parse_expectations(options, build.cache)
	before = snapshot_manifests(build.build_dir)
	version = check_ninja(options.ninja, build.build_dir, build.manifest, runner)

	def run(tool_args):
		return run_ninja(
			options.ninja,
			build.build_dir,
			build.manifest,
			tool_args,
			runner)

	target = options.target
	node = query(run, [target])[target]
	if node.rule == "phony":
		raise NinjaTargetError(
			f"{target} is a phony alias, not a link edge; pass one of its "
			f"exact inputs: {node.explicit[:FAILURE_LIST_LIMIT]}")
	if node.rule is None:
		raise NinjaTargetError(f"{target} has no producing edge in the graph")
	entries = compdb(run, build, [node.rule], [target], kind="link")
	link = parse_link(entries[target], target, build)
	if link.output != target:
		raise NinjaTargetError(
			f"link command writes {link.output}, not {target}")
	consumed = reconcile_link(build, target, node, link)
	produced, externals, compile_entries = resolve_objects(
		build,
		run,
		target,
		consumed)
	pchs = resolve_pchs(build, run, produced, compile_entries)
	index = {item.path: item for item in produced}
	edges = {path: edge for path, _, edge in consumed}
	for path in externals:
		index[path] = (path, edges[path])
	selections = select_objects(
		build,
		run,
		target,
		options.select_object,
		index,
		pchs)
	selections += select_sources(
		build,
		run,
		target,
		options.select_source,
		index,
		pchs)
	cohort = None
	if options.policy_target:
		cohort = evaluate_policy(
			build,
			options.policy_target,
			produced,
			pchs,
			expectations,
			contracts)
	after = snapshot_manifests(build.build_dir)
	compare_snapshots(before, after)
	finished = utc_now()

	by_edge = {}
	for _, _, edge in consumed:
		by_edge[edge] = by_edge.get(edge, 0) + 1
	by_rule = {}
	for item in produced:
		by_rule[item.rule] = by_rule.get(item.rule, 0) + 1
	objects = {
		"count": len(consumed),
		"by_edge": by_edge,
		"by_rule": by_rule,
		"external": externals,
	}
	if options.list_objects:
		objects["list"] = [
			object_record(index[path], pchs)
			for path, _, _ in consumed
		]
	return {
		"schema": SCHEMA,
		"verdict": "PASS",
		"versions": {
			"ninja": version,
			"python": platform.python_version(),
		},
		"build": {
			"build_dir": build.build_dir,
			"host": build.host,
			"configured": build.configured,
			"path_mapping": build.path_mapping,
			"generator": build.generator,
			"manifest": build.manifest,
			"config": build.config,
		},
		"acquisition": {
			"started": started,
			"finished": finished,
			"snapshot_files": len(before),
			"snapshot_digest": snapshot_digest(before),
		},
		"target": {
			"output": target,
			"link_rule": node.rule,
			"linker": link.driver,
			"launcher": link.launcher,
			"order_only_inputs": len(node.order_only),
		},
		"objects": objects,
		"archives": dict(sorted(link.archives.items())),
		"shared_libraries": sorted(set(link.shared_libraries)),
		"pchs": [
			{
				"path": pch,
				"rule": rule,
				"header": command.pch_header,
				"dialect": command.pch_dialect,
				"consumers": consumers,
			}
			for pch, (rule, command, consumers) in sorted(pchs.items())
		],
		"cohort": cohort,
		"selections": selections,
		"limits": LIMITS,
	}


def parse_args(argv=None):
	parser = argparse.ArgumentParser(
		description=(
			"Resolve one exact consuming Ninja output to the objects its link "
			"edge consumes, their compile producers, sources and PCHs, and "
			"check requested command-line macro state in a policy cohort. "
			"Reads Ninja's evaluated graph only; never runs graph commands."))
	parser.add_argument("--build-dir", required=True,
		help="CMake Ninja build directory (contains CMakeCache.txt)")
	parser.add_argument("--manifest", required=True,
		help="manifest file name, e.g. build-Debug.ninja or build.ninja")
	parser.add_argument("--config", required=True,
		help="configuration the manifest selects, e.g. Debug")
	parser.add_argument("--target", required=True,
		help="exact consuming output, e.g. Debug/Telegram")
	parser.add_argument("--ninja", default="ninja",
		help="ninja executable (default: ninja on PATH)")
	parser.add_argument("--policy-target",
		help="CMake target whose consumed compile cohort is checked")
	parser.add_argument("--expect-defined", action="append", default=[],
		metavar="NAME[=VALUE]",
		help="macro that must be defined in every cohort object and PCH")
	parser.add_argument("--expect-undefined", action="append", default=[],
		metavar="NAME",
		help="macro that must be undefined (needs a known-present control)")
	parser.add_argument("--option-macro", action="append", default=[],
		metavar="OPTION=MACRO",
		help="explicit BOOL cache option to macro contract: ON -> defined, "
			"OFF -> undefined")
	parser.add_argument("--select-object", action="append", default=[],
		metavar="PATH", help="exact object path that must be consumed")
	parser.add_argument("--select-source", action="append", default=[],
		metavar="PATH",
		help="exact source graph path whose consumed object is reported")
	parser.add_argument("--list-objects", action="store_true",
		help="include the full ordered consumed-object list")
	return parser.parse_args(argv)


def main(argv=None):
	options = parse_args(argv)
	try:
		result = resolve(options)
	except (OSError, ValueError, NinjaTargetError) as error:
		print(f"error: {error}", file=sys.stderr)
		return 1
	print(json.dumps(result, indent=2, sort_keys=True))
	return 0


if __name__ == "__main__":
	sys.exit(main())
