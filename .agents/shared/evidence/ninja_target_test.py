#!/usr/bin/env python3

import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

import ninja_target


NINJA = shutil.which("ninja")
SKIP_CMAKE_VARIABLE = "NINJA_TARGET_TEST_SKIP_CMAKE"

APP_MAIN = "CMakeFiles/app.dir/Debug/app/main.cpp.o"
APP_COMMON = "CMakeFiles/app.dir/Debug/shared/common.cpp.o"
APP_PLAIN = "CMakeFiles/app.dir/Debug/app/plain.c.o"
LIB_UTIL = "lib dir/CMakeFiles/objlib.dir/Debug/util.cpp.o"
LIB_COMMON = "lib dir/CMakeFiles/objlib.dir/Debug/__/shared/common.cpp.o"
TOOL_COMMON = "CMakeFiles/tool.dir/Debug/shared/common.cpp.o"
APP_HEADER = "CMakeFiles/app.dir/Debug/cmake_pch.hxx"
LIB_HEADER = "lib dir/CMakeFiles/objlib.dir/Debug/cmake_pch.hxx"
APP_RULE = "CXX_COMPILER__app_unscanned_Debug"
APP_C_RULE = "C_COMPILER__app_unscanned_Debug"
LIB_RULE = "CXX_COMPILER__objlib_unscanned_Debug"
TOOL_RULE = "CXX_COMPILER__tool_unscanned_Debug"
APP_LINK_RULE = "CXX_EXECUTABLE_LINKER__app_Debug"
TOOL_LINK_RULE = "CXX_EXECUTABLE_LINKER__tool_Debug"
APP_DEFINES = '-DCMAKE_INTDIR=\\"Debug\\" -DFX_APP_POLICY -DFX_LEVEL=0'
APP_OPTIONS = "-UFX_LEVEL -DFX_LEVEL=2"
LIB_DEFINES = '-DCMAKE_INTDIR=\\"Debug\\" -DFX_OBJLIB_POLICY'
TOOL_DEFINES = '-DCMAKE_INTDIR=\\"Debug\\" -DFX_TOOL_POLICY'
CONTAINER_DIR = "/usr/src/fx/out"


def ninja_path(path):
	return path.replace("$", "$$").replace(" ", "$ ").replace(":", "$:")


def ninja_value(value):
	return value.replace("$", "$$")


def pure_build(configured="/work/out", host=None):
	return ninja_target.Build(
		build_dir=configured,
		manifest="build-Debug.ninja",
		config="Debug",
		generator="Ninja Multi-Config",
		configured=configured,
		cache={},
		host=host or configured)


def compile_entry(command, output="o.o", file="/src/a.cpp"):
	return ninja_target.CompdbEntry("/work/out", command, file, output)


def expectation_options(**values):
	defaults = dict(
		expect_defined=[],
		expect_undefined=[],
		option_macro=[],
		policy_target="app")
	return SimpleNamespace(**{**defaults, **values})


def refuse_runner(argv):
	raise AssertionError(f"runner must not be called: {argv}")


class HandTree:
	"""A handwritten CMake-shaped Ninja Multi-Config tree; never built."""

	def __init__(self, root, dialect="clang", configured=None, rsp=False):
		self.root = root
		self.build_dir = os.path.join(root, "build tree")
		os.makedirs(os.path.join(self.build_dir, "CMakeFiles"))
		self.source_dir = os.path.join(root, "src dir")
		self.configured = configured or self.build_dir
		self.dialect = dialect
		self.rsp = rsp
		self.pch_suffix = ".pch" if dialect == "clang" else ".gch"
		self.prebuilt = os.path.join(root, "pre built", "init.cpp.o")
		self.cache = [
			"# This is the CMakeCache file.",
			"",
			"//Generator the fixture pretends to be configured with.",
			"CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config",
			"CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release",
			f"CMAKE_CACHEFILE_DIR:INTERNAL={self.configured}",
			"FX_DISABLE_FEATURE:BOOL=OFF",
		]
		self.rules = {}
		self.edges = {}
		self.populate()

	def absolute(self, relative):
		return f"{self.configured}/{relative}"

	def source(self, relative):
		return os.path.join(self.source_dir, relative)

	def pch(self, header):
		return header + self.pch_suffix

	def use_flags(self, header):
		quoted = shlex.quote(self.absolute(header))
		if self.dialect == "gcc":
			return f"-Winvalid-pch -include {quoted}"
		pch = shlex.quote(self.absolute(self.pch(header)))
		return (f"-Winvalid-pch -Xclang -include-pch -Xclang {pch} "
			f"-Xclang -include -Xclang {quoted}")

	def emit_flags(self, header):
		quoted = shlex.quote(self.absolute(header))
		if self.dialect == "gcc":
			return f"-x c++-header -include {quoted}"
		return f"-Xclang -emit-pch -Xclang -include -Xclang {quoted} -x c++-header"

	def add_object(self, output, rule, source, defines, options="", header=None):
		flags = " ".join(
			part
			for part in (options, self.use_flags(header) if header else "")
			if part)
		self.edges[output] = {
			"rule": rule,
			"explicit": [source],
			"implicit": [header, self.pch(header)] if header else [],
			"order_only": [],
			"vars": {"DEFINES": defines, "FLAGS": flags},
		}

	def add_pch(self, header, rule, defines, options=""):
		target_dir = os.path.dirname(os.path.dirname(header))
		self.edges[self.pch(header)] = {
			"rule": rule,
			"explicit": [self.absolute(f"{target_dir}/cmake_pch.hxx.cxx")],
			"implicit": [header],
			"order_only": [],
			"vars": {
				"DEFINES": defines,
				"FLAGS": " ".join(
					part for part in (options, self.emit_flags(header)) if part),
			},
		}

	def populate(self):
		gcc = self.dialect == "gcc"
		compiler = "ccache /usr/bin/g++-13" if gcc else "/usr/bin/c++"
		c_compiler = "/usr/bin/gcc" if gcc else "/usr/bin/cc"
		linker = "/usr/bin/g++-13" if gcc else "/usr/bin/c++"
		tail = "-MD -MT $out -MF $out.d"
		for rule in (APP_RULE, LIB_RULE, TOOL_RULE):
			self.rules[rule] = {
				"command": f"{compiler} $DEFINES $FLAGS {tail} -o $out -c $in",
			}
		self.rules[APP_C_RULE] = {
			"command": f"{c_compiler} $DEFINES $FLAGS {tail} -o $out -c $in",
		}
		self.rules["CXX_COMPILER__app_wrongout_Debug"] = {
			"command": f"{compiler} $DEFINES $FLAGS -o $out.tmp -c $in",
		}
		self.rules["CXX_COMPILER__app_wrongsrc_Debug"] = {
			"command": f"{compiler} $DEFINES $FLAGS -o $out -c $in.cc",
		}
		self.rules["CUSTOM_COMMAND"] = {"command": "$COMMAND"}
		if self.rsp:
			link = f"{linker} $FLAGS @$RSP_FILE -o $TARGET_FILE"
			self.rules[APP_LINK_RULE] = {
				"command": f"$PRE_LINK && {link} && $POST_BUILD",
				"rspfile": "$RSP_FILE",
				"rspfile_content": "$in_newline $LINK_PATH $LINK_LIBRARIES",
			}
		else:
			link = (f"{linker} $FLAGS $in -o $TARGET_FILE $LINK_PATH "
				"$LINK_LIBRARIES")
			wrapped = (f": && {link} && :"
				if gcc
				else f"$PRE_LINK && {link} && $POST_BUILD")
			self.rules[APP_LINK_RULE] = {"command": wrapped}
		self.rules[TOOL_LINK_RULE] = {"command": f"{linker} $in -o $out"}

		self.add_pch(APP_HEADER, APP_RULE, APP_DEFINES, APP_OPTIONS)
		self.add_object(APP_MAIN, APP_RULE, self.source("app/main.cpp"),
			APP_DEFINES, APP_OPTIONS, APP_HEADER)
		self.add_object(APP_COMMON, APP_RULE, self.source("shared/common.cpp"),
			APP_DEFINES, APP_OPTIONS, APP_HEADER)
		self.add_object(APP_PLAIN, APP_C_RULE, self.source("app/plain.c"),
			APP_DEFINES, APP_OPTIONS)
		self.add_pch(LIB_HEADER, LIB_RULE, LIB_DEFINES)
		self.add_object(LIB_UTIL, LIB_RULE, self.source("lib dir/util.cpp"),
			LIB_DEFINES, header=LIB_HEADER)
		self.add_object(TOOL_COMMON, TOOL_RULE,
			self.source("shared/common.cpp"), TOOL_DEFINES)
		self.edges["Debug/tool"] = {
			"rule": TOOL_LINK_RULE,
			"explicit": [TOOL_COMMON],
			"implicit": [],
			"order_only": [],
			"vars": {},
		}
		self.edges["Debug/app"] = {
			"rule": APP_LINK_RULE,
			"explicit": [APP_MAIN, APP_COMMON, APP_PLAIN, LIB_UTIL],
			"implicit": [self.prebuilt],
			"order_only": ["Debug/tool"],
			"vars": {
				"PRE_LINK": ":",
				"POST_BUILD": ":",
				"FLAGS": "-g",
				"TARGET_FILE": "Debug/app",
				"LINK_PATH": "-L/opt/fx/lib",
				"LINK_LIBRARIES": (
					f"{shlex.quote(self.prebuilt)} libz.a -lm libz.a"),
				"RSP_FILE": "CMakeFiles/app.rsp",
			},
		}
		self.edges["app"] = {
			"rule": "phony",
			"explicit": ["Debug/app"],
			"implicit": [],
			"order_only": [],
			"vars": {},
		}

	def link_vars(self):
		return self.edges["Debug/app"]["vars"]

	def manifest_path(self):
		return os.path.join(self.build_dir, "build-Debug.ninja")

	def write(self):
		lines = []
		for name, bindings in self.rules.items():
			lines.append(f"rule {name}")
			for key, value in bindings.items():
				lines.append(f"  {key} = {value}")
		for output, edge in self.edges.items():
			line = f"build {ninja_path(output)}: {edge['rule']}"
			for path in edge["explicit"]:
				line += " " + ninja_path(path)
			if edge["implicit"]:
				line += " | " + " ".join(ninja_path(p) for p in edge["implicit"])
			if edge["order_only"]:
				line += " || " + " ".join(
					ninja_path(p) for p in edge["order_only"])
			lines.append(line)
			for key, value in edge["vars"].items():
				lines.append(f"  {key} = {ninja_value(value)}")
		with open(self.manifest_path(), "w", encoding="utf-8") as file:
			file.write("\n".join(lines) + "\n")
		with open(os.path.join(self.build_dir, "CMakeCache.txt"), "w",
				encoding="utf-8") as file:
			file.write("\n".join(self.cache) + "\n")


def resolve_tree(tree, *extra, target="Debug/app", config="Debug",
		runner=None):
	argv = [
		"--build-dir", tree.build_dir,
		"--manifest", "build-Debug.ninja",
		"--config", config,
		"--target", target,
		"--ninja", NINJA,
		*extra,
	]
	return ninja_target.resolve(
		ninja_target.parse_args(argv),
		runner or ninja_target.execute)


def base_args(tree):
	return [
		"--policy-target", "app",
		"--expect-defined", "FX_APP_POLICY",
		"--expect-defined", "FX_LEVEL=2",
		"--expect-undefined", "FX_TOOL_POLICY",
		"--option-macro", "FX_DISABLE_FEATURE=FX_FEATURE_DISABLED",
		"--select-source", tree.source("shared/common.cpp"),
		"--list-objects",
	]


class SplitCommandTests(unittest.TestCase):
	def test_accepts_and_lists(self):
		cases = (
			("a && b", [["a"], ["b"]]),
			(": && c++ 'a b.o' -o x && :",
				[[":"], ["c++", "a b.o", "-o", "x"], [":"]]),
			("cd /x && c++ -o y", [["cd", "/x"], ["c++", "-o", "y"]]),
			("cc -Wno-error=#warnings -c x.c",
				[["cc", "-Wno-error=#warnings", "-c", "x.c"]]),
			('cc -DQ=\\"Debug\\" -c x.c', [["cc", '-DQ="Debug"', "-c", "x.c"]]),
		)
		for command, expected in cases:
			with self.subTest(command=command):
				self.assertEqual(ninja_target.split_command(command), expected)

	def test_refuses_other_shell_syntax(self):
		cases = (
			("a ; b", "';'"),
			("a || b", "'\\|'"),
			("a | b", "'\\|'"),
			("a > f", "'>'"),
			("a < f", "'<'"),
			("a $x", "'\\$'"),
			("a `b`", "'`'"),
			("a\nb", "'\\\\n'"),
			("(a)", "'\\('"),
			('a "&&" b', "outside a whitespace-delimited"),
			("a & b", "outside a whitespace-delimited"),
			("a &&b", "outside a whitespace-delimited"),
			("a && && b", "empty simple command"),
			("a 'b", "unsupported shell quoting"),
		)
		for command, message in cases:
			with self.subTest(command=command):
				with self.assertRaisesRegex(ninja_target.NinjaTargetError, message):
					ninja_target.split_command(command)


class LinkGrammarTests(unittest.TestCase):
	def parse(self, command, target="Debug/app", build=None):
		build = build or pure_build()
		entry = ninja_target.CompdbEntry(
			build.host, command, "a.o", target)
		return ninja_target.parse_link(entry, target, build)

	def test_classifies_operands(self):
		link = self.parse(
			"/usr/bin/c++ -g -arch arm64 -framework Cocoa -weak_framework Metal "
			"-Xlinker -dead_strip a.o /work/out/b.o -o /work/out/Debug/app "
			"-L/x -lfoo -l bar libz.a libz.a /usr/lib/libssl.so.3 "
			"/usr/lib/libc.dylib x.tbd")
		self.assertEqual(link.output, "Debug/app")
		self.assertEqual(link.objects, ["a.o", "b.o"])
		self.assertEqual(link.archives, {"libz.a": 2})
		self.assertEqual(
			link.shared_libraries,
			["/usr/lib/libssl.so.3", "/usr/lib/libc.dylib", "x.tbd"])
		self.assertIsNone(link.launcher)

	def test_cd_segments_and_launcher(self):
		link = self.parse(
			"cd /work/out/Telegram && mkdir -p x && cd /work/out && "
			"ccache /usr/bin/g++ a.o -o Debug/app && cd /work/out/Telegram && :")
		self.assertEqual(link.cwd, "/work/out")
		self.assertEqual(link.launcher, "ccache")
		self.assertEqual(link.driver, "/usr/bin/g++")
		self.assertEqual(link.objects, ["a.o"])

	def test_host_namespace_cd(self):
		build = pure_build(configured="/usr/src/fx/out", host="/home/u/out")
		link = self.parse(
			"cd /home/u/out && c++ /usr/src/fx/out/a.o -o Debug/app",
			build=build)
		self.assertEqual(link.objects, ["a.o"])

	def test_refusals(self):
		cases = (
			("c++ a.o @CMakeFiles/app.rsp -o Debug/app",
				"unexpanded response file"),
			("c++ a.o notes.txt -o Debug/app", "unsupported link operand"),
			("c++ a.o -o Debug/other", "no link command produces"),
			("c++ a.o -o Debug/app && c++ b.o -o Debug/app",
				"several link commands"),
			("/usr/bin/ld a.o -o Debug/app", "unsupported linker driver"),
			("cd /elsewhere && c++ a.o -o Debug/app", "not the build directory"),
			("cd relative && c++ a.o -o Debug/app", "unsupported cd form"),
			("c++ a.o -o Debug/app -o Debug/app", "several -o"),
			("c++ a/../a.o -o Debug/app", "'..' component"),
		)
		for command, message in cases:
			with self.subTest(command=command):
				with self.assertRaisesRegex(ninja_target.NinjaTargetError, message):
					self.parse(command)


class CompileGrammarTests(unittest.TestCase):
	def parse(self, command, inputs=(), output="o.o"):
		return ninja_target.parse_compile(
			compile_entry(command, output),
			pure_build(),
			list(inputs))

	def test_clang_pch_consumer_and_producer(self):
		command = self.parse(
			"/usr/bin/c++ -DA -Winvalid-pch -Xclang -include-pch -Xclang "
			"/work/out/x/cmake_pch.hxx.pch -Xclang -include -Xclang "
			"/work/out/x/cmake_pch.hxx -o o.o -c /src/a.cpp")
		self.assertEqual(
			(command.pch, command.pch_header, command.pch_dialect,
				command.emits_pch, command.source),
			("x/cmake_pch.hxx.pch", "x/cmake_pch.hxx", "clang", False,
				"/src/a.cpp"))
		producer = self.parse(
			"/usr/bin/c++ -Xclang -emit-pch -Xclang -include -Xclang "
			"/work/out/x/cmake_pch.hxx -x c++-header -o x/cmake_pch.hxx.pch "
			"-c /work/out/x/cmake_pch.hxx.cxx",
			output="x/cmake_pch.hxx.pch")
		self.assertTrue(producer.emits_pch)
		self.assertEqual(producer.pch_header, "x/cmake_pch.hxx")
		self.assertIsNone(producer.pch)
		self.assertEqual(producer.source, "x/cmake_pch.hxx.cxx")

	def test_gcc_pch_dialect_with_launcher(self):
		gcc = ("ccache /usr/bin/g++-13 -Winvalid-pch -include "
			"/work/out/x/cmake_pch.hxx -o o.o -c /src/a.cpp")
		command = self.parse(
			gcc,
			["/src/a.cpp", "x/cmake_pch.hxx", "x/cmake_pch.hxx.gch"])
		self.assertEqual(command.launcher, "ccache")
		self.assertEqual(command.driver, "/usr/bin/g++-13")
		self.assertEqual(command.pch, "x/cmake_pch.hxx.gch")
		self.assertEqual(command.pch_dialect, "gcc")
		self.assertEqual(command.forced_includes, [])
		producer = self.parse(
			"/usr/bin/g++ -x c++-header -include /work/out/x/cmake_pch.hxx "
			"-o x/cmake_pch.hxx.gch -c /work/out/x/cmake_pch.hxx.cxx",
			output="x/cmake_pch.hxx.gch")
		self.assertTrue(producer.emits_pch)
		self.assertEqual(producer.pch_dialect, "gcc")
		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				re.escape(
					"incoherent PCH for o.o: x/cmake_pch.hxx.gch is used on the "
					"command line but is not an implicit graph input")):
			self.parse(gcc, ["/src/a.cpp", "x/cmake_pch.hxx"])
		forced = self.parse(
			gcc.replace("-Winvalid-pch ", ""),
			["/src/a.cpp", "x/cmake_pch.hxx"])
		self.assertIsNone(forced.pch)
		self.assertEqual(forced.forced_includes, ["x/cmake_pch.hxx"])
		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				"forced include may change requested macro"):
			ninja_target.macro_state(forced, "A")

	def test_ordered_definitions(self):
		command = self.parse(
			"c++ -DX -DE= -DZ=0 -D S -US -DV=1 -UV -DV=2 -U W -DW "
			"-UW -DQ=\\\"Debug\\\" -o o.o -c a.cpp")
		expected = {
			"X": (True, "1"),
			"E": (True, ""),
			"Z": (True, "0"),
			"S": (False, None),
			"V": (True, "2"),
			"W": (False, None),
			"Q": (True, '"Debug"'),
			"MISSING": (False, None),
		}
		for name, state in expected.items():
			with self.subTest(name=name):
				self.assertEqual(ninja_target.macro_state(command, name), state)

	def test_option_values_are_not_definitions(self):
		command = self.parse(
			"c++ -MT -DX -MF -UY -I -DZ -o o.o -c a.cpp")
		self.assertEqual(command.definitions, [])

	def test_refusals(self):
		cases = (
			("c++ -Xclang -DX -o o.o -c a.cpp", "-Xclang -DX"),
			("c++ -Xclang -UX -o o.o -c a.cpp", "-Xclang -UX"),
			("c++ -Xpreprocessor -DX -o o.o -c a.cpp", "'-Xpreprocessor'"),
			("c++ -imacros m.h -o o.o -c a.cpp", "'-imacros'"),
			("c++ -Wp,-DX -o o.o -c a.cpp", "'-Wp,-DX'"),
			("c++ --define-macro=X -o o.o -c a.cpp", "'--define-macro=X'"),
			("c++ -includem.h -o o.o -c a.cpp", "unsupported include form"),
			("c++ @flags.rsp -o o.o -c a.cpp", "unexpanded response file"),
			("c++ -Xclang -include-pch -Xclang /p1.pch -Xclang -include-pch "
				"-Xclang /p2.pch -Xclang -include -Xclang /h -o o.o -c a.cpp",
				"second PCH inclusion"),
			("c++ -Xclang -include -Xclang /h -o o.o -c a.cpp",
				"outside the PCH form"),
			("c++ -o o.o -o p.o -c a.cpp", "exactly one -o"),
			("c++ -DX -o o.o", "exactly one -o"),
			("c++ -o o.o a.cpp", "unexpected compile operand"),
			("c++ -o o.o -c a.cpp extra.cpp", "unexpected compile operand"),
			("c++ -o o.o -c a.cpp && :", "not a single command"),
			("/usr/bin/cl.exe -o o.o -c a.cpp", "unsupported compiler driver"),
			("c++ -D1BAD -o o.o -c a.cpp", "unsupported definition"),
		)
		for command, message in cases:
			with self.subTest(command=command):
				with self.assertRaisesRegex(
						ninja_target.NinjaTargetError,
						re.escape(message)):
					self.parse(command)


class QueryAndCompdbTests(unittest.TestCase):
	def test_query_sections(self):
		nodes = ninja_target.parse_query(
			"Debug/app:\n"
			"  input: CXX_EXECUTABLE_LINKER__app_Debug\n"
			"    a.o\n"
			"    lib dir/b.o\n"
			"    | /pre built/init.o\n"
			"    || Debug/tool\n"
			"  validations:\n"
			"    check\n"
			"  outputs:\n"
			"    all\n"
			"x.cpp:\n"
			"  outputs:\n"
			"    a.o\n"
			"    a.o\n"
			"check:\n"
			"  input: phony\n"
			"  validation for:\n"
			"    Debug/app\n")
		app = nodes["Debug/app"]
		self.assertEqual(app.rule, "CXX_EXECUTABLE_LINKER__app_Debug")
		self.assertEqual(app.explicit, ["a.o", "lib dir/b.o"])
		self.assertEqual(app.implicit, ["/pre built/init.o"])
		self.assertEqual(app.order_only, ["Debug/tool"])
		self.assertEqual(app.validations, ["check"])
		self.assertEqual(app.outputs, ["all"])
		self.assertIsNone(nodes["x.cpp"].rule)
		self.assertEqual(nodes["x.cpp"].outputs, ["a.o", "a.o"])
		self.assertEqual(nodes["check"].validation_for, ["Debug/app"])

	def test_query_refusals(self):
		cases = (
			("a.o:\n  inputs: x\n", "unknown ninja -t query line"),
			("a.o:\n  input: r\n    |x.o\n", "unknown ninja -t query input"),
			("a.o:\n    x.o\n", "entry outside a section"),
			("  input: r\n", "outside a node"),
			("a.o:\n  outputs:\n    x\na.o:\n", "repeated ninja -t query header"),
			("a.o:\n  outputs:\n  outputs:\n", "repeated ninja -t query section"),
			("garbage\n", "unknown ninja -t query line"),
		)
		for text, message in cases:
			with self.subTest(text=text):
				with self.assertRaisesRegex(ninja_target.NinjaTargetError, message):
					ninja_target.parse_query(text)

	def test_compdb_refusals(self):
		record = {"directory": "/b", "command": "c", "file": "f", "output": "o"}
		cases = (
			([record, dict(record)], "duplicate ninja -t compdb output"),
			([record, dict(record, directory="/c", output="p")], "differs"),
			([{"directory": "/b", "command": "c", "file": "f"}], "lacks"),
			({"a": 1}, "not a JSON list"),
		)
		for records, message in cases:
			with self.subTest(message=message):
				with self.assertRaisesRegex(ninja_target.NinjaTargetError, message):
					ninja_target.load_compdb(json.dumps(records))
		with self.assertRaisesRegex(ninja_target.NinjaTargetError, "malformed"):
			ninja_target.load_compdb("[")

	def test_tool_allowlist(self):
		cases = (
			["commands", "Debug/app"],
			["compdb"],
			["compdb", "RULE"],
			["query"],
			["query", "-h"],
			["targets", "all"],
			["inputs", "Debug/app"],
		)
		for args in cases:
			with self.subTest(args=args):
				with self.assertRaisesRegex(ninja_target.NinjaTargetError, "refusing"):
					ninja_target.ninja_tool_argv("ninja", "/b", "build.ninja", args)


class CacheIdentityAndSelectorTests(unittest.TestCase):
	def test_cmake_cache(self):
		with tempfile.TemporaryDirectory() as temporary:
			path = os.path.join(temporary, "CMakeCache.txt")
			with open(path, "w", encoding="utf-8") as file:
				file.write(
					"# comment\n//doc\n\nA:BOOL=ON\n"
					"\"QUOTED NAME\":STRING=x=y\nEMPTY:STRING=\n")
			cache = ninja_target.read_cmake_cache(path)
			self.assertEqual(cache["A"], ("BOOL", "ON"))
			self.assertEqual(cache["QUOTED NAME"], ("STRING", "x=y"))
			self.assertEqual(cache["EMPTY"], ("STRING", ""))
			for text, message in (
					("garbage\n", "malformed CMakeCache.txt line 1"),
					("A:BOOL=ON\nA:BOOL=OFF\n", "duplicate CMakeCache.txt entry A")):
				with self.subTest(text=text):
					with open(path, "w", encoding="utf-8") as file:
						file.write(text)
					with self.assertRaisesRegex(ninja_target.NinjaTargetError, message):
						ninja_target.read_cmake_cache(path)

	def test_bool_option_contracts(self):
		cases = (
			("ON", True),
			("on", True),
			("TRUE", True),
			("YES", True),
			("1", True),
			("OFF", False),
			("False", False),
			("NO", False),
			("0", False),
		)
		for value, defined in cases:
			with self.subTest(value=value):
				expectations, contracts = ninja_target.parse_expectations(
					expectation_options(
						expect_defined=["CONTROL"],
						option_macro=["OPT=MACRO"]),
					{"OPT": ("BOOL", value)})
				macro = [item for item in expectations if item.macro == "MACRO"][0]
				self.assertEqual(macro.defined, defined)
				self.assertEqual(
					contracts[0]["expect"],
					"defined" if defined else "undefined")

	def test_expectation_refusals(self):
		cache = {"OPT": ("BOOL", "ON"), "LABEL": ("STRING", "ON")}
		cases = (
			(dict(option_macro=["LABEL=MACRO"]), "not a BOOL"),
			(dict(option_macro=["MISSING=MACRO"]), "has no MISSING"),
			(dict(option_macro=["OPT"]), "expected OPTION=MACRO"),
			(dict(expect_undefined=["X"]), "known-present control"),
			(dict(expect_defined=["X"], expect_undefined=["X"]),
				"requested twice"),
			(dict(expect_defined=["1X"]), "invalid macro name"),
			(dict(expect_defined=["X"], policy_target=None),
				"require --policy-target"),
		)
		for values, message in cases:
			with self.subTest(values=values):
				with self.assertRaisesRegex(ninja_target.NinjaTargetError, message):
					ninja_target.parse_expectations(
						expectation_options(**values),
						cache)
		with self.assertRaisesRegex(ninja_target.NinjaTargetError, "not a BOOL"):
			ninja_target.parse_expectations(
				expectation_options(
					expect_defined=["C"],
					option_macro=["OPT=MACRO"]),
				{"OPT": ("BOOL", "maybe")})

	def test_configured_namespace_identity(self):
		cases = (
			("/usr/src/fx/out/CMakeFiles/a.o", "CMakeFiles/a.o"),
			("/usr/src/fx/outer/a.o", "/usr/src/fx/outer/a.o"),
			("/usr/src/fx/out", "/usr/src/fx/out"),
			("/opt/other/out/a.o", "/opt/other/out/a.o"),
			("CMakeFiles/a.o", "CMakeFiles/a.o"),
		)
		for token, expected in cases:
			with self.subTest(token=token):
				self.assertEqual(
					ninja_target.identity(token, CONTAINER_DIR),
					expected)

	def test_basename_selectors_refused_before_any_tool(self):
		for flag in ("--select-source", "--select-object"):
			with self.subTest(flag=flag):
				options = ninja_target.parse_args([
					"--build-dir", "/nonexistent",
					"--manifest", "build-Debug.ninja",
					"--config", "Debug",
					"--target", "Debug/app",
					flag, "common.cpp",
				])
				with self.assertRaisesRegex(
						ninja_target.NinjaTargetError,
						"a basename is not an identity"):
					ninja_target.resolve(options, refuse_runner)


class HandwrittenManifestTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		if NINJA is None:
			raise AssertionError("ninja is required on PATH for these tests")

	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix="ninja target ")
		self.addCleanup(self.temporary.cleanup)

	def tree(self, name="case", **arguments):
		root = os.path.join(self.temporary.name, name)
		os.makedirs(root)
		return HandTree(root, **arguments)

	def assert_base_report(self, tree, result):
		self.assertEqual(result["verdict"], "PASS")
		objects = result["objects"]
		self.assertEqual(
			[item["object"] for item in objects["list"]],
			[APP_MAIN, APP_COMMON, APP_PLAIN, LIB_UTIL, tree.prebuilt])
		self.assertEqual(objects["by_edge"], {"explicit": 4, "implicit": 1})
		self.assertEqual(objects["external"], [tree.prebuilt])
		self.assertEqual(
			objects["by_rule"],
			{APP_RULE: 2, APP_C_RULE: 1, LIB_RULE: 1})
		self.assertNotIn(
			TOOL_COMMON,
			[item["object"] for item in objects["list"]])
		records = {item["object"]: item for item in objects["list"]}
		self.assertEqual(records[APP_MAIN]["pch"], tree.pch(APP_HEADER))
		self.assertIsNone(records[APP_PLAIN]["pch"])
		self.assertEqual(records[LIB_UTIL]["pch"], tree.pch(LIB_HEADER))
		self.assertEqual(
			records[APP_COMMON]["source"],
			tree.source("shared/common.cpp"))
		self.assertEqual(result["archives"], {"libz.a": 2})
		self.assertEqual(result["target"]["order_only_inputs"], 1)
		pchs = {item["path"]: item for item in result["pchs"]}
		self.assertEqual(
			{path: (item["rule"], item["consumers"]) for path, item in pchs.items()},
			{
				tree.pch(APP_HEADER): (APP_RULE, 2),
				tree.pch(LIB_HEADER): (LIB_RULE, 1),
			})
		cohort = result["cohort"]
		self.assertEqual(cohort["rules"], {APP_RULE: 2, APP_C_RULE: 1})
		self.assertEqual(cohort["pchs"], [tree.pch(APP_HEADER)])
		readings = {item["macro"]: item for item in cohort["macros"]}
		self.assertEqual(readings["FX_LEVEL"]["values"], ["2"])
		self.assertEqual(readings["FX_TOOL_POLICY"]["expected"], "undefined")
		self.assertEqual(readings["FX_FEATURE_DISABLED"]["expected"], "undefined")
		self.assertEqual(
			cohort["option_contracts"][0]["cache"],
			"FX_DISABLE_FEATURE:BOOL=OFF")
		self.assertNotIn("FX_TOOL_POLICY", cohort["definition_names"])
		selection = result["selections"][0]
		self.assertEqual(selection["object"], APP_COMMON)
		self.assertEqual(selection["rule"], APP_RULE)
		self.assertEqual(
			selection["excluded_siblings"],
			[{
				"object": TOOL_COMMON,
				"rule": TOOL_RULE,
				"consumers": ["Debug/tool"],
			}])

	def test_clang_base_passes(self):
		tree = self.tree()
		tree.write()
		result = resolve_tree(tree, *base_args(tree))
		self.assert_base_report(tree, result)
		self.assertEqual(result["build"]["configured"], tree.build_dir)
		if os.path.realpath(tree.build_dir) == tree.build_dir:
			self.assertIsNone(result["build"]["path_mapping"])
		else:
			self.assertEqual(
				result["build"]["path_mapping"]["validated_by"],
				"samefile")
		self.assertEqual(
			[item["dialect"] for item in result["pchs"]],
			["clang", "clang"])

	def test_gcc_dialect_passes(self):
		tree = self.tree(dialect="gcc")
		tree.write()
		result = resolve_tree(tree, *base_args(tree))
		self.assert_base_report(tree, result)
		self.assertTrue(tree.pch(APP_HEADER).endswith(".gch"))
		self.assertEqual(
			[item["dialect"] for item in result["pchs"]],
			["gcc", "gcc"])
		self.assertEqual(result["target"]["linker"], "/usr/bin/g++-13")

	def test_gcc_pch_use_without_gch_input_is_incoherent(self):
		tree = self.tree(dialect="gcc")
		tree.edges[APP_MAIN]["implicit"] = [APP_HEADER]
		tree.write()
		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				re.escape(
					f"incoherent PCH for {APP_MAIN}: {tree.pch(APP_HEADER)} is "
					"used on the command line but is not an implicit graph "
					"input")):
			resolve_tree(tree, *base_args(tree))

	def test_gcc_plain_forced_include_refuses_macro_request(self):
		tree = self.tree(dialect="gcc")
		tree.edges[APP_MAIN]["implicit"] = [APP_HEADER]
		flags = tree.edges[APP_MAIN]["vars"]["FLAGS"]
		self.assertIn("-Winvalid-pch ", flags)
		tree.edges[APP_MAIN]["vars"]["FLAGS"] = flags.replace(
			"-Winvalid-pch ", "")
		tree.write()
		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				"forced include may change requested macro"):
			resolve_tree(tree, *base_args(tree))

	def test_response_file_link_expands(self):
		tree = self.tree(rsp=True)
		tree.write()
		with open(tree.manifest_path(), encoding="utf-8") as file:
			self.assertIn("rspfile_content", file.read())
		result = resolve_tree(tree, *base_args(tree))
		self.assert_base_report(tree, result)

	def test_container_namespace_mapping(self):
		tree = self.tree(configured=CONTAINER_DIR)
		tree.write()
		result = resolve_tree(tree, *base_args(tree))
		self.assert_base_report(tree, result)
		mapping = result["build"]["path_mapping"]
		self.assertEqual(mapping["configured"], CONTAINER_DIR)
		self.assertEqual(mapping["validated_by"], "CMakeCache.txt CMAKE_CACHEFILE_DIR")
		self.assertTrue(os.path.samefile(mapping["host"], tree.build_dir))

	def test_container_namespace_tokens(self):
		cases = (
			(f"{CONTAINER_DIR}/{APP_MAIN}", "duplicate object operand"),
			(f"/opt/other/out/{APP_MAIN}",
				"substituted or extra link operand /opt/other/out/"),
		)
		for token, message in cases:
			with self.subTest(token=token):
				tree = self.tree(name=message[:9], configured=CONTAINER_DIR)
				tree.link_vars()["LINK_LIBRARIES"] += " " + token
				tree.write()
				with self.assertRaisesRegex(
						ninja_target.NinjaTargetError,
						re.escape(message)):
					resolve_tree(tree, *base_args(tree))

	def test_configured_dir_that_is_another_local_dir_refuses(self):
		other = os.path.join(self.temporary.name, "other")
		os.makedirs(other)
		tree = self.tree(configured=other)
		tree.write()
		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				"exists locally but is not the build directory"):
			resolve_tree(tree, *base_args(tree))

	def test_one_mutation_negatives(self):
		def link_libraries(extra):
			def mutate(tree):
				tree.link_vars()["LINK_LIBRARIES"] += " " + extra
			return mutate

		def drop_prebuilt(tree):
			tree.link_vars()["LINK_LIBRARIES"] = "libz.a -lm libz.a"

		def order_only_sibling(tree):
			tree.edges["Debug/app"]["order_only"].append(TOOL_COMMON)
			tree.link_vars()["LINK_LIBRARIES"] += " " + TOOL_COMMON

		def main_rule(rule):
			def mutate(tree):
				tree.edges[APP_MAIN]["rule"] = rule
				tree.edges[APP_MAIN]["vars"]["COMMAND"] = ":"
			return mutate

		def main_flags(flags):
			def mutate(tree):
				tree.edges[APP_MAIN]["vars"]["FLAGS"] = flags(tree)
			return mutate

		def pch_edge(key, value):
			def mutate(tree):
				edge = tree.edges[tree.pch(APP_HEADER)]
				if key == "FLAGS":
					edge["vars"]["FLAGS"] = value(tree)
				else:
					edge[key] = value
			return mutate

		def drop_main_pch_input(tree):
			tree.edges[APP_MAIN]["implicit"] = [APP_HEADER]

		def plain_intdir(tree):
			tree.edges[APP_PLAIN]["vars"]["DEFINES"] = APP_DEFINES.replace(
				"Debug",
				"Release")

		def post_build(tree):
			tree.link_vars()["POST_BUILD"] = "echo linked; :"

		def ambiguous_source(tree):
			tree.add_object(LIB_COMMON, LIB_RULE,
				tree.source("shared/common.cpp"), LIB_DEFINES, header=LIB_HEADER)
			tree.edges["Debug/app"]["explicit"].append(LIB_COMMON)

		def no_mutation(tree):
			pass

		second_pch = (lambda tree: tree.edges[APP_MAIN]["vars"]["FLAGS"]
			+ " -Xclang -include-pch -Xclang "
			+ shlex.quote(tree.absolute(tree.pch(APP_HEADER))))
		policy = ["--policy-target", "app"]
		cases = (
			("duplicate object operand",
				link_libraries(APP_MAIN), None,
				f"duplicate object operand in link command for Debug/app: "
				f"['{APP_MAIN}']"),
			("graph input missing from argv", drop_prebuilt, None,
				"missing link entry for graph input(s) of Debug/app"),
			("substituted sibling object", link_libraries(TOOL_COMMON), None,
				f"substituted or extra link operand {TOOL_COMMON}: not an"),
			("object only order-only", order_only_sibling, None,
				f"substituted or extra link operand {TOOL_COMMON} "
				"(order-only only)"),
			("producer output mismatch",
				main_rule("CXX_COMPILER__app_wrongout_Debug"), None,
				f"producer/output mismatch for {APP_MAIN}"),
			("producer source mismatch",
				main_rule("CXX_COMPILER__app_wrongsrc_Debug"), None,
				f"producer/source mismatch for {APP_MAIN}"),
			("non-compile producer", main_rule("CUSTOM_COMMAND"), None,
				f"non-compile producer rule CUSTOM_COMMAND for object {APP_MAIN}"),
			("PCH used but not a graph input", drop_main_pch_input, None,
				"is used on the command line but is not an implicit graph input"),
			("PCH graph input not used",
				main_flags(lambda tree: APP_OPTIONS), None,
				"is not used on the command line"),
			("two include-pch", main_flags(second_pch), None,
				f"second PCH inclusion in compile command for {APP_MAIN}"),
			("PCH produced by another target rule",
				pch_edge("rule", LIB_RULE), None,
				f"is produced by rule {LIB_RULE} but used by"),
			("PCH producer not emitting",
				pch_edge("FLAGS", lambda tree: APP_OPTIONS), None,
				"does not emit a PCH"),
			("PCH producer built from another header",
				pch_edge("FLAGS", lambda tree: APP_OPTIONS
					+ " -Xclang -emit-pch -Xclang -include -Xclang "
					+ shlex.quote(tree.absolute("CMakeFiles/app.dir/other.hxx"))
					+ " -x c++-header"), None,
				"CMakeFiles/app.dir/other.hxx (clang)"),
			("macro differs between object and PCH",
				main_flags(lambda tree: tree.edges[APP_MAIN]["vars"]["FLAGS"]
					+ " -UFX_APP_POLICY"), None,
				"requested macro FX_APP_POLICY differs between object and PCH: "
				f"{APP_MAIN} (undefined)"),
			("policy expectation unmet", no_mutation,
				lambda tree: policy + ["--expect-defined", "FX_TOOL_POLICY"],
				"policy expectation unmet in cohort app: FX_TOOL_POLICY expected "
				"defined (--expect-defined); 4 of 4 objects/PCHs differ"),
			("absence without known-present control", no_mutation,
				lambda tree: policy + ["--expect-undefined", "FX_TOOL_POLICY"],
				"an absence expectation needs a known-present control"),
			("wrong CMAKE_INTDIR", plain_intdir, None,
				f"CMAKE_INTDIR in {APP_PLAIN} is '\"Release\"', expected "
				"'\"Debug\"'"),
			("unsupported link shell syntax", post_build, None,
				"unsupported shell syntax ';' in command"),
			("literal response file without rspfile",
				link_libraries("@CMakeFiles/app.rsp"), None,
				"unexpanded response file '@CMakeFiles/app.rsp' in link command"),
			("ambiguous source", ambiguous_source, None,
				f"{APP_COMMON}', '{LIB_COMMON}'"),
			("sibling object selected", no_mutation,
				lambda tree: ["--select-object", TOOL_COMMON],
				f"object {TOOL_COMMON} not consumed by Debug/app; its graph "
				"consumers: ['Debug/tool']"),
			("source not consumed", no_mutation,
				lambda tree: [
					"--select-source",
					tree.absolute("CMakeFiles/app.dir/cmake_pch.hxx.cxx"),
				],
				"source not consumed by Debug/app"),
		)
		for number, (name, mutate, args, message) in enumerate(cases):
			with self.subTest(name=name):
				tree = self.tree(name=f"case {number}")
				mutate(tree)
				tree.write()
				args = base_args(tree) if args is None else args(tree)
				with self.assertRaisesRegex(
						ninja_target.NinjaTargetError,
						re.escape(message)):
					resolve_tree(tree, *args)

	def test_manifest_argument_refusals(self):
		tree = self.tree()
		tree.write()
		cases = (
			(dict(config="Release"),
				"manifest build-Debug.ninja does not select configuration Release"),
			(dict(config="RelWithDebInfo"), "is not in CMAKE_CONFIGURATION_TYPES"),
			(dict(target="app"),
				"app is a phony alias, not a link edge; pass one of its exact "
				"inputs: ['Debug/app']"),
		)
		for arguments, message in cases:
			with self.subTest(arguments=arguments):
				with self.assertRaisesRegex(
						ninja_target.NinjaTargetError,
						re.escape(message)):
					resolve_tree(tree, **arguments)

	def test_missing_compile_entry(self):
		tree = self.tree()
		tree.write()

		def runner(argv):
			code, stdout, stderr = ninja_target.execute(argv)
			if argv[5:8] == ["-t", "compdb", "-x"]:
				records = [
					record
					for record in json.loads(stdout)
					if record["output"] != APP_MAIN
				]
				stdout = json.dumps(records)
			return code, stdout, stderr

		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				re.escape(f"missing compile entry for {APP_MAIN}")):
			resolve_tree(tree, *base_args(tree), runner=runner)

	def test_manifest_touched_during_acquisition(self):
		tree = self.tree()
		tree.write()
		calls = []

		def runner(argv):
			calls.append(argv)
			if argv[5:7] == ["-t", "query"] and len(calls) == 3:
				with open(tree.manifest_path(), "a", encoding="utf-8") as file:
					file.write("# touched during acquisition\n")
			return ninja_target.execute(argv)

		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				"build manifests changed during acquisition: build-Debug.ninja"):
			resolve_tree(tree, *base_args(tree), runner=runner)

	def test_graph_commands_are_never_executed(self):
		tree = self.tree()
		sentinel = os.path.join(tree.root, "pre link sentinel")
		tree.link_vars()["PRE_LINK"] = f"touch {shlex.quote(sentinel)}"
		tree.write()
		calls = []

		def runner(argv):
			calls.append(list(argv))
			return ninja_target.execute(argv)

		result = resolve_tree(tree, *base_args(tree), runner=runner)
		self.assertEqual(result["verdict"], "PASS")
		self.assertFalse(os.path.exists(sentinel))
		kinds = set()
		prefix = [NINJA, "-C", tree.build_dir, "-f", "build-Debug.ninja", "-t"]
		for argv in calls:
			if argv == [NINJA, "--version"]:
				kinds.add("--version")
			elif argv[:6] == prefix and argv[6:] == ["compdb", "-h"]:
				kinds.add("compdb -h")
			elif argv[:6] == prefix and argv[6] == "query" and len(argv) > 7:
				kinds.add("query")
			elif argv[:6] == prefix and argv[6:8] == ["compdb", "-x"]:
				kinds.add("compdb -x")
			else:
				self.fail(f"unexpected ninja invocation {argv}")
		self.assertEqual(kinds, {"--version", "compdb -h", "query", "compdb -x"})


CMAKE_TOOLS = ("cmake", "ninja", "c++", "nm")
FIXTURE_FILES = {
	"CMakeLists.txt": """cmake_minimum_required(VERSION 3.16)
project(fx C CXX)
option(FX_DISABLE_FEATURE "Disable the fixture feature" OFF)
option(FX_DUPLICATE_OBJECT "Link the object library objects twice" OFF)
set(FX_PREBUILT_OBJECT "" CACHE FILEPATH "Prebuilt plugin-init analogue")
add_subdirectory("lib dir")
add_executable(fx_tool tool/main.cpp shared/common.cpp)
target_compile_definitions(fx_tool PRIVATE FX_TOOL_POLICY)
target_precompile_headers(fx_tool PRIVATE
	"$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/tool/tool_pch.h>")
add_executable(fx_app app/main.cpp shared/common.cpp app/plain.c)
target_compile_definitions(fx_app PRIVATE FX_APP_POLICY FX_LEVEL=0)
target_compile_options(fx_app PRIVATE -UFX_LEVEL -DFX_LEVEL=2)
target_precompile_headers(fx_app PRIVATE
	"$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/app/app_pch.h>")
if (FX_DISABLE_FEATURE)
	target_compile_definitions(fx_app PRIVATE FX_FEATURE_DISABLED)
endif()
target_link_libraries(fx_app PRIVATE fx_objlib "${FX_PREBUILT_OBJECT}")
if (FX_DUPLICATE_OBJECT)
	add_library(fx_extra OBJECT "lib dir/extra.cpp")
	target_link_libraries(fx_app PRIVATE fx_extra "$<TARGET_OBJECTS:fx_extra>")
endif()
add_dependencies(fx_app fx_tool)
""",
	"lib dir/CMakeLists.txt": """add_library(fx_objlib OBJECT util.cpp)
target_compile_definitions(fx_objlib PRIVATE FX_OBJLIB_POLICY=1)
target_precompile_headers(fx_objlib PRIVATE objlib_pch.h)
""",
	"lib dir/objlib_pch.h": "#pragma once\n#define FX_OBJLIB_PCH 1\n",
	"lib dir/extra.cpp": "extern \"C\" int fx_extra_value() { return 5; }\n",
	"lib dir/util.cpp": (
		"extern \"C\" int fx_objlib_value() { return FX_OBJLIB_POLICY; }\n"),
	"shared/common.cpp": """#ifdef FX_APP_POLICY
#if FX_LEVEL != 2
#error FX_LEVEL must be 2 in fx_app
#endif
extern "C" int fx_app_marker() { return FX_LEVEL; }
#endif
#ifdef FX_TOOL_POLICY
extern "C" int fx_tool_marker() { return 7; }
#endif
""",
	"app/app_pch.h": "#pragma once\n#define FX_APP_PCH 1\n",
	"app/main.cpp": """extern "C" int fx_app_marker();
extern "C" int fx_objlib_value();
extern "C" int fx_plain_value(void);
extern "C" int fx_prebuilt_init();
int main() {
	return fx_app_marker() + fx_objlib_value() + fx_plain_value()
		+ fx_prebuilt_init() == 10 ? 0 : 1;
}
""",
	"app/plain.c": "int fx_plain_value(void) { return 3; }\n",
	"tool/tool_pch.h": "#pragma once\n#define FX_TOOL_PCH 1\n",
	"tool/main.cpp": """extern "C" int fx_tool_marker();
int main() { return fx_tool_marker() == 7 ? 0 : 1; }
""",
}
FX_APP_COMMON = "CMakeFiles/fx_app.dir/Debug/shared/common.cpp.o"
FX_TOOL_COMMON = "CMakeFiles/fx_tool.dir/Debug/shared/common.cpp.o"
FX_APP_PCH = "CMakeFiles/fx_app.dir/Debug/cmake_pch.hxx.pch"
FX_OBJLIB_PCH = "lib dir/CMakeFiles/fx_objlib.dir/Debug/cmake_pch.hxx.pch"


class CompiledFixtureTests(unittest.TestCase):
	"""fx_app and fx_tool compile the same shared/common.cpp."""

	@classmethod
	def setUpClass(cls):
		if os.environ.get(SKIP_CMAKE_VARIABLE) == "1":
			raise unittest.SkipTest(
				f"{SKIP_CMAKE_VARIABLE}=1: compiled CMake fixture skipped")
		missing = [tool for tool in CMAKE_TOOLS if shutil.which(tool) is None]
		if missing:
			raise AssertionError(
				f"compiled fixture needs {', '.join(missing)} on PATH; set "
				f"{SKIP_CMAKE_VARIABLE}=1 only on a host that cannot provide them")
		cls.temporary = tempfile.TemporaryDirectory(prefix="ninja target fx ")
		root = cls.temporary.name
		cls.source_dir = os.path.join(root, "fx source")
		for relative, text in FIXTURE_FILES.items():
			path = os.path.join(cls.source_dir, relative)
			os.makedirs(os.path.dirname(path), exist_ok=True)
			with open(path, "w", encoding="utf-8") as file:
				file.write(text)
		prebuilt_dir = os.path.join(root, "pre built")
		os.makedirs(prebuilt_dir)
		prebuilt_source = os.path.join(prebuilt_dir, "init.cpp")
		with open(prebuilt_source, "w", encoding="utf-8") as file:
			file.write("extern \"C\" int fx_prebuilt_init() { return 4; }\n")
		cls.prebuilt = os.path.join(prebuilt_dir, "init.cpp.o")
		cls.check([
			shutil.which("c++"), "-c", prebuilt_source, "-o", cls.prebuilt])
		cls.trees = {}
		variants = (
			("off", ["-DFX_DISABLE_FEATURE=OFF"], True),
			("on", ["-DFX_DISABLE_FEATURE=ON"], True),
			("rsp", [
				"-DFX_DISABLE_FEATURE=ON",
				"-DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON",
			], False),
			("duplicate", ["-DFX_DUPLICATE_OBJECT=ON"], False),
		)
		try:
			for name, arguments, build in variants:
				build_dir = os.path.join(root, f"build {name}")
				cls.check([
					"cmake",
					"-G", "Ninja Multi-Config",
					f"-DCMAKE_MAKE_PROGRAM={shutil.which('ninja')}",
					f"-DFX_PREBUILT_OBJECT={cls.prebuilt}",
					*arguments,
					"-S", cls.source_dir,
					"-B", build_dir,
				])
				if build:
					cls.check(["cmake", "--build", build_dir, "--config", "Debug"])
				cls.trees[name] = build_dir
		except BaseException:
			cls.temporary.cleanup()
			raise

	@classmethod
	def tearDownClass(cls):
		cls.temporary.cleanup()

	@staticmethod
	def check(argv):
		result = subprocess.run(
			argv,
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT,
			text=True,
			check=False)
		if result.returncode != 0:
			raise AssertionError(
				f"{argv} failed with exit {result.returncode}:\n{result.stdout}")
		return result.stdout

	def resolve(self, variant, *extra):
		argv = [
			"--build-dir", self.trees[variant],
			"--manifest", "build-Debug.ninja",
			"--config", "Debug",
			"--target", "Debug/fx_app",
			"--ninja", shutil.which("ninja"),
			*extra,
		]
		return ninja_target.resolve(ninja_target.parse_args(argv))

	def app_args(self, macro="FX_FEATURE_DISABLED"):
		return [
			"--policy-target", "fx_app",
			"--option-macro", f"FX_DISABLE_FEATURE={macro}",
			"--expect-defined", "FX_APP_POLICY",
			"--expect-defined", "FX_LEVEL=2",
			"--expect-undefined", "FX_TOOL_POLICY",
			"--select-source", os.path.join(self.source_dir, "shared/common.cpp"),
			"--list-objects",
		]

	def symbols(self, variant, relative):
		return self.check(["nm", os.path.join(self.trees[variant], relative)])

	def test_selects_app_object_and_excludes_tool_sibling(self):
		result = self.resolve("off", *self.app_args())
		selection = result["selections"][0]
		self.assertEqual(selection["object"], FX_APP_COMMON)
		self.assertEqual(selection["pch"], FX_APP_PCH)
		self.assertEqual(
			selection["excluded_siblings"],
			[{
				"object": FX_TOOL_COMMON,
				"rule": "CXX_COMPILER__fx_tool_unscanned_Debug",
				"consumers": ["Debug/fx_tool"],
			}])
		listed = [item["object"] for item in result["objects"]["list"]]
		self.assertIn(FX_APP_COMMON, listed)
		self.assertNotIn(FX_TOOL_COMMON, listed)
		self.assertEqual(result["objects"]["external"], [self.prebuilt])
		records = {item["object"]: item for item in result["objects"]["list"]}
		self.assertIsNone(records["CMakeFiles/fx_app.dir/Debug/app/plain.c.o"]["pch"])
		build_dir = self.trees["off"]
		mapping = result["build"]["path_mapping"]
		if os.path.realpath(build_dir) == build_dir:
			self.assertIsNone(mapping)
		else:
			self.assertEqual(mapping["configured"], build_dir)
			self.assertEqual(mapping["validated_by"], "samefile")
		readings = {item["macro"]: item for item in result["cohort"]["macros"]}
		self.assertEqual(readings["FX_LEVEL"]["values"], ["2"])
		self.assertEqual(readings["FX_FEATURE_DISABLED"]["expected"], "undefined")
		self.assertEqual(result["cohort"]["pchs"], [FX_APP_PCH])

		selected = self.symbols("off", selection["object"])
		sibling = self.symbols("off", FX_TOOL_COMMON)
		self.assertIn("fx_app_marker", selected)
		self.assertNotIn("fx_tool_marker", selected)
		self.assertIn("fx_tool_marker", sibling)
		self.assertNotIn("fx_app_marker", sibling)

	def test_object_library_cohort_has_its_own_pch(self):
		result = self.resolve(
			"off",
			"--policy-target", "fx_objlib",
			"--expect-defined", "FX_OBJLIB_POLICY=1",
			"--expect-undefined", "FX_APP_POLICY")
		self.assertEqual(result["cohort"]["pchs"], [FX_OBJLIB_PCH])
		self.assertEqual(result["cohort"]["objects"], 1)

	def test_option_contract_in_both_variants(self):
		off = self.resolve("off", *self.app_args())
		on = self.resolve("on", *self.app_args())
		self.assertEqual(
			off["cohort"]["option_contracts"][0]["cache"],
			"FX_DISABLE_FEATURE:BOOL=OFF")
		self.assertEqual(
			on["cohort"]["option_contracts"][0]["cache"],
			"FX_DISABLE_FEATURE:BOOL=ON")
		readings = {item["macro"]: item for item in on["cohort"]["macros"]}
		self.assertEqual(readings["FX_FEATURE_DISABLED"]["expected"], "defined")
		self.assertEqual(readings["FX_FEATURE_DISABLED"]["pchs"], 1)

	def test_same_name_contract_fails_when_on(self):
		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				"policy expectation unmet in cohort fx_app: FX_DISABLE_FEATURE "
				"expected defined"):
			self.resolve("on", *self.app_args(macro="FX_DISABLE_FEATURE"))

	def test_response_file_variant_has_the_same_objects(self):
		on = self.resolve("on", *self.app_args())
		rsp = self.resolve("rsp", *self.app_args())
		self.assertEqual(
			[item["object"] for item in rsp["objects"]["list"]],
			[item["object"] for item in on["objects"]["list"]])
		self.assertEqual(rsp["selections"], on["selections"])

	def test_duplicate_object_variant_refuses(self):
		with self.assertRaisesRegex(
				ninja_target.NinjaTargetError,
				"duplicate object operand in link command for Debug/fx_app"):
			self.resolve("duplicate")


if __name__ == "__main__":
	unittest.main()
