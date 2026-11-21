#!/usr/bin/env python3
"""Tests of hook.py: python3 tools/rules/test_hook.py"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HOOK = os.path.join(HERE, "hook.py")
LICENSE = "/*\nThis file is part of Telegram Desktop,\nthe official desktop"\
    " application for the Telegram messaging service.\n\nFor license and"\
    " copyright information please follow this link:\n"\
    "https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL\n*/\n"


def edit(new, old="x", path="/tmp/rules/foo.cpp"):
    return run({"tool_name": "Edit", "tool_input": {
        "file_path": path, "old_string": old, "new_string": new}})


def write(content, path):
    return run({"tool_name": "Write", "tool_input": {
        "file_path": path, "content": content}})


def run(payload):
    done = subprocess.run([sys.executable, HOOK], input=json.dumps(payload),
        capture_output=True, text=True)
    return done.returncode, done.stderr


class EditHook(unittest.TestCase):
    def test_one_line_comment_passes(self):
        self.assertEqual(edit("// the server rejects more\n++count;\n")[0], 0)

    def test_two_line_comment_fails(self):
        code, why = edit("// the server rejects more,\n// so we stop\nf();\n")
        self.assertEqual(code, 2)
        self.assertIn("2-line comment: a comment is one line", why)
        self.assertIn("reference URL", why)

    def test_url_block_passes(self):
        self.assertEqual(edit("// the scheme, see\n"
            "// https://core.telegram.org\nf();\n")[0], 0)

    def test_long_url_block_passes(self):
        self.assertEqual(edit(
            "// a\n// b\n// https://core.telegram.org\n// c\n// d\nf();\n")[0],
            0)

    def test_commented_out_code_is_a_comment(self):
        self.assertEqual(edit("//foo();\n//bar();\n//baz();\n")[0], 2)

    def test_block_comment_over_two_lines_fails(self):
        code, _ = edit("/* the server\n   rejects more */\nf();\n")
        self.assertEqual(code, 2)

    def test_one_line_block_comment_passes(self):
        self.assertEqual(edit("/* rejected */ f();\n")[0], 0)

    def test_trailing_comment_opening_a_block_is_one_comment(self):
        code, _ = edit("f(); /* the server\nrejects more */\n")
        self.assertEqual(code, 2)

    def test_edit_has_no_budget(self):
        many = "".join(f"a{i} = {i}; // set a{i}\n" for i in range(6))
        self.assertEqual(edit(many)[0], 0)
        self.assertEqual(edit("// one\nf();\n// two\ng();\n")[0], 0)

    def test_moved_block_is_not_new(self):
        block = "// the server\n// rejects more\n"
        self.assertEqual(edit(block + "g();\n", old=block + "f();\n")[0], 0)

    def test_harness_is_exempt_from_comment_rules(self):
        path = "/tmp/Telegram/SourceFiles/test/harness.cpp"
        self.assertEqual(edit("// a\n// b\n// c\n// d\n", path=path)[0], 0)
        self.assertEqual(edit("qMin(a, b);\n", path=path)[0], 2)

    def test_banned_function_fails_with_replacement(self):
        code, why = edit("w = qMin(a, b);\n")
        self.assertEqual(code, 2)
        self.assertIn("qMin is banned in this repository -- write std::min",
            why)
        self.assertIn("std::clamp", edit("w = qBound(0, v, 9);\n")[1])
        self.assertIn("std::abs", edit("w = qAbs (v);\n")[1])

    def test_banned_name_inside_identifier_passes(self):
        self.assertEqual(edit("w = myqMin(a, b) + qMinimum(c);\n")[0], 0)

    def test_banned_name_in_comment_or_string_passes(self):
        self.assertEqual(edit("// qMin(a, b) was here\n")[0], 0)
        self.assertEqual(edit('log("qMin(a, b)");\n')[0], 0)

    def test_banned_call_after_url_string_still_fails(self):
        self.assertEqual(edit('u = "http://x"; v = qMin(a, b);\n')[0], 2)

    def test_untouched_banned_call_on_edited_line_still_fails(self):
        self.assertEqual(
            edit("w = qMin(a, b) + 2;\n", old="w = qMin(a, b) + 1;\n")[0], 2)

    def test_non_source_paths_are_ignored(self):
        self.assertEqual(edit("// a\n// b\n// c\n", path="/tmp/x.py")[0], 0)
        self.assertEqual(edit("qMin(a, b)\n", path="/tmp/x.style")[0], 0)

    def test_malformed_payload_passes(self):
        done = subprocess.run([sys.executable, HOOK], input="not json",
            capture_output=True, text=True)
        self.assertEqual(done.returncode, 0)

    def test_write_of_new_file_with_license_passes(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "new.cpp")
            code, why = write(LICENSE + "#include \"a.h\"\n\nf();\n", path)
            self.assertEqual(code, 0, why)
            code, why = write(LICENSE + "// a\n// b\nf();\n", path)
            self.assertEqual(code, 2)
            code, why = write(LICENSE + "// a\nf();\n", path)
            self.assertEqual(code, 0, why)

    def test_multi_edit_shape_is_read(self):
        code, why = run({"tool_name": "Edit", "tool_input": {
            "file_path": "/tmp/rules/foo.cpp",
            "edits": [{"old_string": "x", "new_string": "qMin(a, b);\n"}]}})
        self.assertEqual(code, 2)


class Repository:
    def __init__(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = self.folder.name
        self.git("init", "-q")
        self.git("config", "user.email", "t@t")
        self.git("config", "user.name", "t")
        os.makedirs(os.path.join(self.path, "hooks"))
        # Machine-wide hooks have no say in this disposable repository.
        self.git("config", "core.hooksPath", "hooks")

    def git(self, *arguments):
        return subprocess.run(["git", *arguments], cwd=self.path, check=True,
            capture_output=True, text=True).stdout

    def put(self, files):
        for name, content in files.items():
            full = os.path.join(self.path, name)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w", encoding="utf-8") as stream:
                stream.write(content)
            self.git("add", name)

    def commit(self, files):
        self.put(files)
        self.git("commit", "-q", "-m", "base")

    def staged(self):
        done = subprocess.run([sys.executable, HOOK, "--staged"],
            cwd=self.path, capture_output=True, text=True)
        return done.returncode, done.stderr


class StagedHook(unittest.TestCase):
    def setUp(self):
        self.repo = Repository()
        self.repo.commit({"a.cpp": "int a() {\n\treturn 1;\n}\n"})

    def tearDown(self):
        self.repo.folder.cleanup()

    def stage(self, files):
        self.repo.put(files)
        return self.repo.staged()

    def test_two_plain_lines_pass(self):
        code, why = self.stage({"a.cpp": "// one\nint a() {\n"
            "\treturn 1; // two\n}\n"})
        self.assertEqual(code, 0, why)

    def test_third_plain_line_fails(self):
        code, why = self.stage({"a.cpp": "// one\nint a() {\n"
            "\t// two\n\treturn 1;\n}\n// three\n"})
        self.assertEqual(code, 1)
        self.assertIn("adds 3 comment lines; 2 is the budget", why)

    def test_trailing_comments_count(self):
        code, why = self.stage({"a.cpp": "int a() {\n\tint b = 1; // b\n"
            "\tint c = 2; // c\n\treturn b + c; // sum\n}\n"})
        self.assertEqual(code, 1)
        self.assertIn("adds 3 comment lines", why)

    def test_closers_and_directives_carry_free_labels(self):
        code, why = self.stage({"a.cpp": "#ifndef A\n#define A\n"
            "namespace A {\nint a() {\n\treturn 1; // one\n}\n"
            "} // namespace A\n#endif // A\n// two\n"})
        self.assertEqual(code, 0, why)

    def test_moved_comment_is_free(self):
        self.repo.commit({"a.cpp": "// one\n// two\n// three\nint a() {\n"
            "\treturn 1;\n}\n"})
        code, why = self.stage({
            "a.cpp": "int a() {\n\treturn 1;\n}\n",
            "b.cpp": "// one\n// two\n// three\nint b() {\n\treturn 2;\n}\n"})
        self.assertEqual(code, 0, why)

    def test_edited_line_keeps_its_trailing_comment(self):
        self.repo.commit({"a.cpp": "int a() {\n\treturn 1; // the server\n"
            "}\n"})
        code, why = self.stage({"a.cpp": "int a() {\n"
            "\treturn 2; // the server\n}\n// one\n// two\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:4: a comment is one line", why)
        self.assertNotIn("budget", why)

    def test_url_block_is_free(self):
        code, why = self.stage({"a.cpp": "// one\n\n// two\n\n// the docs\n"
            "// https://core.telegram.org\nint a() {\n\treturn 1;\n}\n"})
        self.assertEqual(code, 0, why)

    def test_url_block_over_budget_is_free(self):
        code, why = self.stage({"a.cpp": "// one\n// two\n// the docs\n"
            "// https://core.telegram.org\nint a() {\n\treturn 1;\n}\n"})
        self.assertEqual(code, 0, why)

    def test_long_block_is_named_by_line(self):
        code, why = self.stage({"a.cpp": "int a() {\n\treturn 1;\n}\n\n"
            "// one\n// two\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:5: a comment is one line", why)

    def test_banned_call_is_named_by_line(self):
        code, why = self.stage({"a.cpp": "int a() {\n\treturn qMax(1, 2);\n"
            "}\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:2: qMax is banned -- write std::max", why)

    def test_harness_is_exempt_from_comment_rules(self):
        code, why = self.stage({"Telegram/SourceFiles/test/t.cpp":
            "// a\n// b\n// c\n// d\nint t() {\n\treturn 1;\n}\n"})
        self.assertEqual(code, 0, why)

    def test_other_files_are_ignored(self):
        code, why = self.stage({
            "t.py": "# a\n# b\n// c\n// d\nqMin(1, 2)\n",
            "a.style": "// a\n// b\nx: 1px;\n" + "x" * 79 + "\n"})
        self.assertEqual(code, 0, why)

    def test_line_width_counts_tab_stops_and_allows_78_columns(self):
        line = "\t" + "x" * 69
        code, why = self.stage({"a.cpp": line + ";\n"})
        self.assertEqual(code, 0, why)
        code, why = self.stage({"a.cpp": line + "x;\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:1: 79 columns (limit 78, tab width 8)", why)

    def test_only_staged_changes_count(self):
        long_line = "x" * 79 + "\n"
        self.repo.commit({"a.cpp": long_line + "int a;\n"})
        self.repo.put({"a.cpp": long_line + "int b;\n"})
        with open(os.path.join(self.repo.path, "a.cpp"), "a") as stream:
            stream.write(long_line)
        code, why = self.repo.staged()
        self.assertEqual(code, 0, why)

    def test_overlong_line_with_quoted_path_and_plus_prefix(self):
        code, why = self.stage({"répertoire/file name.cpp":
            "\n++value; " + "x" * 70 + "\n"})
        self.assertEqual(code, 1)
        self.assertIn("répertoire/file name.cpp:2: 79 columns", why)

    def test_multiline_call_requires_first_argument_on_next_line(self):
        code, why = self.stage({"a.cpp":
            "void draw() {\n\tAppendArc(path,\n\t\tcenter,\n\t\tradius);\n}\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:2: Put each argument of the multiline call", why)

    def test_multiline_call_rejects_later_arguments_on_same_line(self):
        code, why = self.stage({"a.cpp":
            "void draw() {\n\tAppendArc(\n\t\tpath,\n\t\tcenter, radius);\n}\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:4: Put each argument of the multiline call", why)

    def test_argument_cannot_share_the_end_of_a_multiline_argument(self):
        code, why = self.stage({"a.cpp": "void draw() {\n\tAppendArc(\n"
            "\t\t{\n\t\t\tx,\n\t\t\ty,\n\t\t}, radius);\n}\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:6: Put each argument of the multiline call", why)

    def test_call_layout_understands_nested_cpp_syntax(self):
        code, why = self.stage({"a.cpp": "void declare(int first,\n"
            "\tint second);\n"
            "void draw() {\n"
            "\tAppendArc(\n"
            "\t\tpath,\n"
            "\t\t{ x, y },\n"
            "\t\tRadius<int, short>(1, 2),\n"
            "\t\tR\"(0, 90)\",\n"
            "\t\t/* end */ 90.);\n"
            "\tvisit([&] {\n"
            "\t\tdraw();\n"
            "\t});\n"
            "}\n"})
        self.assertEqual(code, 0, why)

    def test_call_layout_checks_full_call_from_staged_hunk(self):
        original = ("void draw() {\n\tAppendArc(path, { x, y }, radius,\n"
            "\t\t180., 270.);\n}\n")
        self.repo.commit({"a.cpp": original})
        code, why = self.stage({"a.cpp": original + "int width;\n"})
        self.assertEqual(code, 0, why)
        code, why = self.stage({"a.cpp": original.replace("270.", "360.")})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:2: Put each argument of the multiline call", why)

    def test_multiline_sum_rejects_packed_terms_on_both_lines(self):
        code, why = self.stage({"a.cpp": "auto left = outer + width * ratio\n"
            "\t- anchor * bubbleWidth - padding;\n"})
        self.assertEqual(code, 1)
        for line in (1, 2):
            self.assertIn(f"a.cpp:{line}: Start each continuation term", why)

    def test_multiline_sum_keeps_operator_with_term(self):
        code, why = self.stage({"a.cpp": "auto width = outer +\n"
            "\tlimits\n"
            "\t-\n"
            "\tpadding;\n"})
        self.assertEqual(code, 1)
        for line in (1, 3):
            self.assertIn(f"a.cpp:{line}: Start each continuation term", why)

    def test_sum_layout_understands_precedence_and_unary_signs(self):
        code, why = self.stage({"a.cpp": "auto total = a - b + c;\n"
            "auto width = outer\n"
            "\t+ ratio * (left + right)\n"
            "\t- (-padding + border);\n"})
        self.assertEqual(code, 0, why)

    def test_sum_layout_only_checks_affected_expressions(self):
        original = "auto width = outer + left\n\t- right;\n"
        self.repo.commit({"a.cpp": original})
        code, why = self.stage({"a.cpp": original + "auto height = top;\n"})
        self.assertEqual(code, 0, why)
        code, why = self.stage({"a.cpp": original.replace("right", "padding")})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:1: Start each continuation term", why)

    def test_commit_is_rejected_by_installed_hook(self):
        hook = os.path.join(self.repo.path, "hooks", "pre-commit")
        with open(hook, "w", encoding="utf-8") as stream:
            stream.write(f'#!/bin/sh\nexec "{sys.executable}" "{HOOK}"'
                ' --staged\n')
        os.chmod(hook, 0o755)
        self.repo.put({"a.cpp": "x" * 79 + "\n"})
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            self.repo.git("commit", "-m", "Overlong line")
        self.assertIn("a.cpp:1: 79 columns", failure.exception.stderr)
        self.repo.put({"a.cpp": "void draw() {\n"
            "\tAppendArc(path, { x, y }, radius,\n\t\t180., 270.);\n}\n"})
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            self.repo.git("commit", "-m", "Packed call")
        self.assertIn("a.cpp:2: Put each argument of the multiline call",
            failure.exception.stderr)
        self.repo.put({"a.cpp": "void draw() {\n\tAppendArc(\n"
            "\t\tpath,\n\t\t{ x, y },\n\t\tradius,\n\t\t180.,\n\t\t270.);\n}\n"})
        self.repo.git("commit", "-m", "Expanded call")

    def test_license_header_of_new_file_is_free(self):
        code, why = self.stage({"b.cpp": LICENSE + "int b() {\n"
            "\treturn 2;\n}\n"})
        self.assertEqual(code, 0, why)


if __name__ == "__main__":
    unittest.main()
