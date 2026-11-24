#!/usr/bin/env python3
"""Tests of hook.py: python3 tools/rules/test_hook.py"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HOOK = os.path.join(HERE, "hook.py")
CPP_LAYOUT = os.path.join(HERE, "cpp_layout.py")
INSTALL = os.path.join(HERE, "install.py")
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

    def test_raw_literal_contents_are_neither_calls_nor_comments(self):
        literal = 'auto text = u8R"example(\n" // qMin(1, 2)\n// prose\n)example";\n'
        self.assertEqual(edit(literal)[0], 0)
        self.assertEqual(edit(literal + "qMin(1, 2);\n")[0], 2)

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


class CallLayoutFix(unittest.TestCase):
    def fix(self, path):
        done = subprocess.run([sys.executable, CPP_LAYOUT, "--fix", path],
            capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        with open(path, "rb") as stream:
            return stream.read()

    def test_fix_orders_whole_include_groups_and_preserves_crlf(self):
        original = (
            b'#include "panel.h"\r\n\r\n'
            b'#include "data/data_user.h"\r\n'
            b'#include "styles/style_panel.h"\r\n'
            b'#include "data/stickers/z.h"\r\n'
            b'#include "data/stickers/a.h" // needed by the panel\r\n'
            b'#include "apiwrap.h"\r\n'
            b'#include <QtCore/QtMath>\r\n'
            b'// Keep this group separate.\r\n'
            b'#include "a.h"\r\n'
            b'#if FEATURE\r\n'
            b'#include "data/z.h"\r\n'
            b'#include "data/a.h"\r\n'
            b'#endif\r\n')
        expected = (
            b'#include "panel.h"\r\n\r\n'
            b'#include "apiwrap.h"\r\n'
            b'#include "data/stickers/a.h" // needed by the panel\r\n'
            b'#include "data/stickers/z.h"\r\n'
            b'#include "data/data_user.h"\r\n\r\n'
            b'#include "styles/style_panel.h"\r\n'
            b'#include <QtCore/QtMath>\r\n'
            b'// Keep this group separate.\r\n'
            b'#include "a.h"\r\n'
            b'#if FEATURE\r\n'
            b'#include "data/a.h"\r\n'
            b'#include "data/z.h"\r\n'
            b'#endif\r\n')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "panel.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_expands_split_arguments_in_a_method_chain(self):
        original = (b"void send() {\n"
            b"\t_api.request(request()\n"
            b"\t).done(completed\n"
            b"\t).fail(failed\n"
            b"\t).handleFloodErrors().send();\n}\n")
        expected = (b"void send() {\n"
            b"\t_api.request(\n\t\trequest()\n"
            b"\t).done(\n\t\tcompleted\n"
            b"\t).fail(\n\t\tfailed\n"
            b"\t).handleFloodErrors().send();\n}\n")
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "send.cpp")
            with open(path, "wb") as stream:
                stream.write(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_preserves_crlf_and_ignores_other_call_layouts(self):
        original = (b"void send() {\r\n"
            b"\trequest(peer\r\n\t).send();\r\n"
            b"\tupdate(peer // the current recipient\r\n\t);\r\n"
            b"\tobserve(\r\n\t);\r\n"
            b"\tdone([&] {\r\n\t\tfinish();\r\n\t\tnotify();\r\n\t});\r\n}\r\n")
        expected = original.replace(b"request(peer\r\n", b"request(\r\n\t\tpeer\r\n")
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "send.cpp")
            with open(path, "wb") as stream:
                stream.write(original)
            self.assertEqual(self.fix(path), expected)

    def test_fix_joins_short_lambda_parameter_and_preserves_crlf(self):
        original = (b"void setup() {\r\n"
            b"\tauto callback = [=](\r\n\t\tint value\r\n"
            b"\t) mutable {\r\n\t\tuse(value);\r\n\t\tnotify();\r\n\t};\r\n}\r\n")
        expected = original.replace(b"[=](\r\n\t\tint value\r\n\t)",
            b"[=](int value)")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "setup.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_wraps_pipeline_at_empty_producer_and_preserves_crlf(self):
        original = (b"void setup() {\r\n\treceive(\r\n"
            b"\t\tpanel->emojiChosen() | rpl::map_to(\r\n"
            b"\t\t\tShowRequest::HideAnimated),\r\n"
            b"\t\tother());\r\n}\r\n")
        expected = original.replace(
            b"emojiChosen() | rpl::map_to(\r\n\t\t\t",
            b"emojiChosen(\r\n\t\t) | rpl::map_to(")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "setup.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_keeps_each_pipeline_bar_with_the_previous_closer(self):
        original = (b"void setup() {\r\n"
            b"\tauto label = rpl::single(loading())\r\n"
            b"\t\t| rpl::then(std::move(progress))\r\n"
            b"\t\t| rpl::distinct_until_changed();\r\n}\r\n")
        expected = (b"void setup() {\r\n\tauto label = rpl::single(\r\n"
            b"\t\tloading()\r\n\t) | rpl::then(\r\n"
            b"\t\tstd::move(progress)\r\n"
            b"\t) | rpl::distinct_until_changed();\r\n}\r\n")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "setup.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_compacts_short_lambda_body_without_changing_crlf(self):
        original = (b"void setup() {\r\n\tbind(\r\n\t\t[=](\r\n"
            b"\t\t\tint value) {\r\n\t\t\tuse(value);\r\n\t\t},\r\n"
            b"\t\tstate);\r\n}\r\n")
        expected = (b"void setup() {\r\n\tbind(\r\n"
            b"\t\t[=](int value) { use(value); },\r\n\t\tstate);\r\n}\r\n")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "setup.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_keeps_simple_receiver_with_method_and_preserves_crlf(self):
        original = (b"void show() {\r\n\t\t\tcontroller\r\n"
            b"\t\t\t\t->showToast(\r\n"
            b"\t\t\t\t\ttr::lng_edit_media_invalid_file(\r\n"
            b"\t\t\t\t\t\ttr::now));\r\n}\r\n")
        expected = (b"void show() {\r\n\t\t\tcontroller->showToast(\r\n"
            b"\t\t\t\ttr::lng_edit_media_invalid_file(tr::now));\r\n}\r\n")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "toast.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_does_not_create_a_dangling_closer_for_a_long_argument(self):
        indent = b"\t" * 6
        original = (b"void draw() {\n\tconst auto over = allowed()\n"
            + indent + b"? _inMediaEditOver\n"
            + indent + b"\t.value(_inMediaEdit\n"
            + indent + b"\t? 1.\n"
            + indent + b"\t: 0.)\n"
            + indent + b": 0.;\n}\n")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "draw.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), original)

    def test_fix_attaches_nested_dangling_closers_without_changing_crlf(self):
        original = (b"void draw() {\r\n\touter(\r\n\t\tinner(\r\n"
            b"\t\t\tvalue\r\n\t\t)\r\n\t);\r\n}\r\n")
        expected = (b"void draw() {\r\n\touter(\r\n\t\tinner(\r\n"
            b"\t\t\tvalue));\r\n}\r\n")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "draw.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), expected)
            self.assertEqual(self.fix(path), expected)

    def test_fix_leaves_closer_after_line_comment_for_manual_reflow(self):
        original = b"void send() {\n\trequest(peer // recipient\n\t);\n}\n"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "send.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), original)

    def test_fix_tracks_closers_after_line_256(self):
        original = b"\n" * 300 + b"void draw() {\n\tpaint(\n\t\t42\n\t);\n}\n"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "draw.cpp"
            path.write_bytes(original)
            self.assertEqual(self.fix(path), original.replace(b"42\n\t);", b"42);"))


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

    def staged(self, env=None):
        done = subprocess.run([sys.executable, HOOK, "--staged"],
            cwd=self.path, env=env, capture_output=True, text=True)
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

    def test_short_lambda_parameter_stays_with_capture(self):
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\tbind(\n\t\tbutton,\n\t\t[=](\n"
            "\t\t\t\tnot_null<Ui::PopupMenu*> menu) {\n"
            "\t\t\tuse(menu);\n\t\t\tnotify();\n\t\t});\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:4: Keep the sole lambda parameter", why)

    def test_short_lambda_body_stays_on_one_line(self):
        code, why = self.stage({"a.cpp": "void setup() {\n\tbind(\n"
            "\t\t[=] {\n\t\t\tnotify();\n\t\t},\n\t\tstate);\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:3: Keep the short lambda body on one line", why)

    def test_lambda_layout_preserves_comments_long_and_compound_bodies(self):
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\tauto body = [someExceptionallyLongCaptureName] {\n"
            "\t\treturn someExceptionallyLongCaptureName;\n\t};\n"
            "\tauto withComment = [=] {\n\t\t// retain this explanation\n"
            "\t\tuse();\n\t};\n"
            "\tauto branch = [=] {\n\t\tif (valid()) use();\n\t};\n"
            "\tauto several = [=] {\n\t\tprepare();\n\t\tuse();\n\t};\n}\n"})
        self.assertEqual(code, 0, why)

    def test_lambda_parameter_keeps_long_and_commented_signatures(self):
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\tauto longName = [=](\n"
            "\t\tVeryLongNamespace::VeryLongMenuClassName parameter) {\n"
            "\t\tuse(parameter);\n\t};\n"
            "\tauto explained = [=]( // theme owns this menu\n"
            "\t\tMenu *menu) {\n\t\tuse(menu);\n\t};\n"
            "\tauto configured = [=](\n#if ENABLED\n"
            "\t\tMenu *menu\n#else\n\t\tint menu\n#endif\n"
            "\t) {\n\t\tuse(menu);\n\t};\n"
            "\tauto both = [=](\n\t\tMenu *menu,\n\t\tint index) {\n"
            "\t\tuse(menu, index);\n\t};\n}\n"})
        self.assertEqual(code, 0, why)

    def test_lambda_param_layout_only_checks_changed_signature(self):
        original = ("void setup() {\n\tauto callback = [=](\n"
            "\t\tint value) {\n\t\tuse(value);\n\t\tnotify();\n\t};\n}\n")
        self.repo.commit({"a.cpp": original})
        code, why = self.stage({"a.cpp": original.replace("use(", "handle(")})
        self.assertEqual(code, 0, why)

    def test_pipeline_puts_short_rpl_stage_on_the_continuation_line(self):
        code, why = self.stage({"a.cpp": "void setup() {\n\treceive(\n"
            "\t\tpanel->emojiChosen() | rpl::map_to(\n"
            "\t\t\tShowRequest::HideAnimated),\n\t\tother());\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:3: Wrap before the producer", why)

    def test_pipeline_keeps_long_and_commented_stage_arguments(self):
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\tstream() | rpl::map_to(\n"
            "\t\tChoice::LongNamedValueThatNeedsItsOwnPipelineContinuation);\n"
            "\tstream() | rpl::map_to( // stages need context\n"
            "\t\tvalue);\n"
            "\tstream(value) | rpl::map_to(\n\t\tvalue);\n"
            "\tstream() | other::map_to(\n\t\tvalue);\n}\n"})
        self.assertEqual(code, 0, why)

    def test_pipeline_only_checks_changed_lines(self):
        original = ("void setup() {\n\tstream() | rpl::map_to(\n"
            "\t\tvalue);\n}\n")
        self.repo.commit({"a.cpp": original})
        code, why = self.stage({"a.cpp": original + "void next() {}\n"})
        self.assertEqual(code, 0, why)

    def test_pipeline_rejects_a_leading_bar_after_an_rpl_call(self):
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\tauto label = rpl::single(loading())\n"
            "\t\t| rpl::then(std::move(progress))\n"
            "\t\t| rpl::distinct_until_changed();\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:3: Keep the RPL pipe with the closing parenthesis", why)
        self.assertIn("a.cpp:4: Keep the RPL pipe with the closing parenthesis", why)

    def test_pipeline_preserves_leading_bar_in_historical_shapes(self):
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\t_layout->events()\n"
            "\t| rpl::start_with_next_done(handler());\n"
            "\twrap->toggleOn(rpl::duplicate(value)\n"
            "\t\t| rpl::map(!rpl::mappers::_1));\n"
            "\tflags()\n\t\t| options();\n}\n"})
        self.assertEqual(code, 0, why)

    def test_simple_arrow_receiver_stays_with_method(self):
        code, why = self.stage({"a.cpp": "void show() {\n\tcontroller\n"
            "\t\t->showToast(\n\t\t\ttext());\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:2: Keep the method name with its receiver", why)

    def test_simple_dot_receiver_in_ternary_branch_stays_with_method(self):
        code, why = self.stage({"a.cpp": "void draw() {\n"
            "\tconst auto over = allowed()\n\t\t? progress\n"
            "\t\t\t.value(active ? 1. : 0.)\n\t\t: 0.;\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:3: Keep the method name with its receiver", why)

    def test_arrow_layout_keeps_long_receivers_and_comments(self):
        code, why = self.stage({"a.cpp": "void show() {\n"
            "\tcontrollerForThisLongerNestedOperation()\n"
            "\t\t->showToast(text());\n"
            "\tcontroller // use the parent controller\n"
            "\t\t->showToast(text());\n"
            "\tcontroller\n"
            "\t\t->ShowLongNamedMenuActionThatDoesNotFitAfterItsReceiverNameXX(\n"
            "\t\t\ttext());\n}\n"})
        self.assertEqual(code, 0, why)

    def test_staged_raw_literal_uses_unchanged_delimiters(self):
        def snippet(body):
            return 'auto text = R"demo(\n' + body + '\n)demo";\n'

        self.repo.commit({"raw.cpp": snippet("old")})
        code, why = self.stage({
            "raw.cpp": snippet('qMin(1, 2)\n// prose\n// more prose'),
        })
        self.assertEqual(code, 0, why)

    def test_staged_block_comment_uses_unchanged_delimiters(self):
        self.repo.commit({"block.cpp": "/*\nold\n*/\n"})
        code, why = self.stage({"block.cpp": "/*\nqMin(1, 2)\n*/\n"})
        self.assertEqual(code, 0, why)

    def test_real_call_after_literal_and_digit_separator_is_rejected(self):
        code, why = self.stage({
            "a.cpp": 'auto text = R"(qMax(1, 2))";\n'
                "\treturn qMin(1'000, 2);\n",
        })
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:2: qMin is banned", why)
        self.assertNotIn("qMax is banned", why)

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

    def test_nonempty_call_rejects_a_standalone_closer(self):
        code, why = self.stage({"a.cpp": "void draw() {\n"
            "\tauto x = value(\n\t\tactive ? 1. : 0.\n\t);\n"
            "\tpaint(\n\t\tx,\n\t\ty\n\t);\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:4: Keep the closing parenthesis", why)
        self.assertIn("a.cpp:8: Keep the closing parenthesis", why)

    def test_pipeline_may_keep_a_closer_with_the_bar(self):
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\tupdates(\n\t) | rpl::map_to(value);\n"
            "\trpl::single(\n\t\tstate\n\t) | rpl::map_to(value);\n}\n"})
        self.assertEqual(code, 0, why)

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
            "\t\tnotify();\n"
            "\t});\n"
            "}\n"})
        self.assertEqual(code, 0, why)

    def test_call_layout_rejects_dangling_parentheses_in_method_chain(self):
        code, why = self.stage({"a.cpp": "void send() {\n"
            "\t_api.request(request()\n"
            "\t).done(completed\n"
            "\t).fail(failed\n"
            "\t).handleFloodErrors().send();\n}\n"})
        self.assertEqual(code, 1)
        self.assertEqual(why.count("Keep the closing parenthesis"), 3)
        for line in (3, 4, 5):
            self.assertIn(f"a.cpp:{line}: Keep the closing parenthesis", why)

    def test_call_layout_allows_breaks_between_chain_methods(self):
        code, why = self.stage({"a.cpp": "void send() {\n"
            "\t_api.request(request())\n"
            "\t\t.done(completed)\n"
            "\t\t.fail(failed)\n"
            "\t\t.handleFloodErrors()\n"
            "\t\t.send();\n}\n"})
        self.assertEqual(code, 0, why)

    def test_call_layout_allows_expanded_or_multiline_single_arguments(self):
        code, why = self.stage({"a.cpp": "void send() {\n"
            "\t_api.request(Request(\n"
            "\t\tpeer,\n"
            "\t\tflags\n"
            "\t)).done([=] {\n"
            "\t\tfinish();\n"
            "\t\tnotify();\n"
            "\t}).fail(\n"
            "\t\tfailed\n"
            "\t).send();\n}\n"})
        self.assertEqual(code, 0, why)

    def test_call_layout_allows_inline_rpl_handler_with_lifetime(self):
        code, why = self.stage({"a.cpp": "void subscribe() {\n"
            "\ttoggle->paintRequest(\n"
            "\t) | rpl::on_next([=] {\n"
            "\t\tpaint(toggle);\n"
            "\t\tnotify();\n"
            "\t}, toggle->lifetime());\n"
            "\tupdates() | rpl::on_next([=] {\n"
            "\t\trefresh();\n"
            "\t\tnotify();\n"
            "\t});\n}\n"})
        self.assertEqual(code, 0, why)

    def test_call_layout_allows_fitting_final_lambda(self):
        head = "(label, [=](int count) {"
        name = "f" * (78 - 8 - len(head))
        code, why = self.stage({"a.cpp": "void setup() {\n"
            "\tbox->addButton(rpl::duplicate(submit), [=] {\n"
            "\t\trefresh();\n"
            "\t\tnotify();\n"
            "\t});\n"
            f"\t{name}{head}\n"
            "\t\trefresh(count);\n"
            "\t\tnotify();\n"
            "\t});\n"
            "}\n"})
        self.assertEqual(code, 0, why)

    def test_call_layout_rejects_other_packed_final_lambdas(self):
        head = "(label, [=] {"
        name = "f" * (79 - 8 - len(head))
        code, why = self.stage({"a.cpp": "void setup() {\n"
            f"\t{name}{head}\n"
            "\t\trefresh();\n"
            "\t});\n"
            "\tobserve([] {}, [=] {\n"
            "\t\trefresh();\n"
            "\t});\n"
            "\tobserve(label, flags, [=] {\n"
            "\t\trefresh();\n"
            "\t});\n"
            "\tobserve(label, [=](\n"
            "\t\t\tint first,\n"
            "\t\t\tint last) {\n"
            "\t\trefresh(first, last);\n"
            "\t});\n"
            "}\n"})
        self.assertEqual(code, 1, why)
        for line in (2, 5, 8, 11):
            self.assertIn(f"a.cpp:{line}: Put each argument", why)

    def test_call_layout_keeps_other_callbacks_under_argument_rules(self):
        code, why = self.stage({"a.cpp": "void subscribe() {\n"
            "\tobserve([=] {\n"
            "\t\trefresh();\n"
            "\t}, lifetime());\n"
            "\tupdates() | rpl::on_next_error([=] {\n"
            "\t\trefresh();\n"
            "\t}, [](auto) {}, lifetime());\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:2: Put each argument", why)
        self.assertIn("a.cpp:5: Put each argument", why)

    def test_call_layout_allows_empty_lists_and_comments(self):
        code, why = self.stage({"a.cpp": "void observe() {\n"
            "\tupdates(\n"
            "\t) | observe();\n"
            "\trequest(peer); // the current recipient\n}\n"})
        self.assertEqual(code, 0, why)

    def test_call_layout_leaves_unchanged_chain_arguments_alone(self):
        original = ("void send() {\n"
            "\tbuilder.prepare(oldState)\n"
            "\t\t.done(completed\n"
            "\t\t).send();\n}\n")
        self.repo.commit({"a.cpp": original})
        code, why = self.stage({"a.cpp": original.replace(
            "oldState", "newState")})
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

    def test_enumerate_accepts_two_inline_lambda_branches(self):
        code, why = self.stage({"a.cpp": "void paint() {\n"
            "\tenumerate([&](Single single) {\n"
            "\t\tpaintSingle(single);\n"
            "\t}, [&](Single single) {\n"
            "\t\tpaintFull(single);\n"
            "\t\tnotify();\n"
            "\t});\n}\n"})
        self.assertEqual(code, 0, why)

    def test_enumerate_requires_inline_lambda_branches_to_start_separately(self):
        code, why = self.stage({"a.cpp": "void paint() {\n"
            "\tenumerate([&] {}, [&] {\n"
            "\t\tpaintFull();\n"
            "\t});\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("a.cpp:2: Put each argument", why)

    def test_match_accepts_adding_an_inline_visitor_branch(self):
        original = ("void refresh() {\n"
            "\tv::match(origin.data, [&](Message data) {\n"
            "\t\trequest(data);\n"
            "\t}, [&](Missing) {\n"
            "\t\tfail();\n"
            "\t});\n}\n")
        self.repo.commit({"a.cpp": original})
        code, why = self.stage({"a.cpp": original.replace(
            "\t}, [&](Missing) {\n",
            "\t}, [&](Preview data) {\n\t\trequestPreview(data);\n"
            "\t}, [&](Missing) {\n")})
        self.assertEqual(code, 0, why)

    def test_member_match_accepts_inline_visitor_branches(self):
        code, why = self.stage({"a.cpp": "void refresh() {\n"
            "\torigin.match([&](Message data) {\n"
            "\t\trequest(data);\n"
            "\t}, [&](Missing) {\n"
            "\t\tfail();\n"
            "\t\tnotify();\n"
            "\t});\n}\n"})
        self.assertEqual(code, 0, why)

    def test_inline_match_still_checks_calls_inside_its_branches(self):
        code, why = self.stage({"a.cpp": "void refresh() {\n"
            "\tv::match(origin.data, [&](Message data) {\n"
            "\t\trequest(data,\n"
            "\t\t\tflags);\n"
            "\t}, [&](Missing) {\n"
            "\t\tfail();\n"
            "\t});\n}\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:3: Put each argument of the multiline call", why)
        self.assertNotIn("a.cpp:2:", why)

    def test_match_with_named_handler_checks_argument_layout(self):
        code, why = self.stage({"a.cpp": "void refresh() {\n"
            "\tv::match(origin.data, message,\n"
            "\t\t[&](Missing) {\n"
            "\t\t\tfail();\n"
            "\t\t});\n}\n"})
        self.assertEqual(code, 1)
        self.assertIn("a.cpp:2: Put each argument of the multiline call", why)

    def test_inline_match_rejects_two_branches_starting_on_one_line(self):
        code, why = self.stage({"a.cpp": "void refresh() {\n"
            "\tv::match(origin, [](Message) {}, [](Missing) {\n"
            "\t\tfail();\n"
            "\t});\n}\n"})
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

    def test_logical_operators_start_continuation_lines(self):
        code, why = self.stage({"logic.cpp": "bool both() {\n"
            "\treturn a &&\n\t\tb ||\n\t\tc;\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("logic.cpp:2: Start the continuation with &&", why)
        self.assertIn("logic.cpp:3: Start the continuation with ||", why)

    def test_multiline_logical_chain_puts_every_term_on_its_own_line(self):
        code, why = self.stage({"logic.cpp": "bool canAdd() {\n"
            "\treturn canChange() && status == Ready\n"
            "\t\t&& count < limit;\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("logic.cpp:2: Start the continuation with &&", why)

    def test_multiline_logical_chain_handles_mixed_precedence(self):
        code, why = self.stage({"logic.cpp": "bool enabled() {\n"
            "\treturn a || b\n\t\t&& c;\n}\n"})
        self.assertEqual(code, 1, why)
        self.assertIn("logic.cpp:2: Start the continuation with ||", why)

    def test_logical_chain_allows_inline_parenthesized_operands(self):
        code, why = self.stage({"logic.cpp": "bool enabled() {\n"
            "\treturn (a || b)\n\t\t&& (c || d);\n}\n"})
        self.assertEqual(code, 0, why)

    def test_logical_layout_allows_leading_operators_and_other_tokens(self):
        code, why = self.stage({"logic.cpp": "bool both(Value &&\n\t\ta) {\n"
            "\treturn a\n\t\t&& b\n\t\t|| c;\n}\n"
            'auto raw = R"(a &&\nb)";\n'
            "bool enabled() {\n\treturn a && // kept with its comment\n"
            "\t\tb;\n}\n"
            "bool configured() {\n\treturn a &&\n#if FLAG\n"
            "\t\tb;\n#else\n\t\tc;\n#endif\n}\n"})
        self.assertEqual(code, 0, why)

    def test_logical_layout_only_checks_changed_operator_boundaries(self):
        original = "bool both() {\n\treturn a &&\n\t\t(c\n\t\t\t|| d);\n}\n"
        self.repo.commit({"logic.cpp": original})
        code, why = self.stage({"logic.cpp": original.replace("|| d", "|| e")})
        self.assertEqual(code, 0, why)
        code, why = self.stage({"logic.cpp": original.replace("(c", "(e")})
        self.assertEqual(code, 1, why)
        self.assertIn("logic.cpp:2: Start the continuation with &&", why)

    def test_license_header_of_new_file_is_free(self):
        code, why = self.stage({"b.cpp": LICENSE + "int b() {\n"
            "\treturn 2;\n}\n"})
        self.assertEqual(code, 0, why)


class ClangTidyHook(unittest.TestCase):
    def clang_tools(self):
        default_plugin = (Path(HERE).parents[2]
            / "stylecheck/build/libtdesktopstylecheck.so")
        plugin = Path(os.environ.get("STYLECHECK_PLUGIN", default_plugin)).resolve()
        tidy = (os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
            or shutil.which("clang-tidy", path="/opt/homebrew/opt/llvm/bin"))
        if not plugin.is_file() or not tidy:
            self.skipTest("built stylecheck plugin and clang-tidy required")
        return str(plugin), tidy

    def test_constructor_indent_checks_only_staged_parameter_lines(self):
        plugin, tidy = self.clang_tools()
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        root = Path(repo.path)
        name = "Telegram/SourceFiles/preview.cpp"
        source = root / name
        database = root / "out/clang-tidy"
        database.mkdir(parents=True)
        (database / "compile_commands.json").write_text(json.dumps([{
            "directory": str(root),
            "file": str(source),
            "arguments": ["clang++", "-std=c++20", "-c", str(source)],
        }]))
        env = dict(os.environ, CLANG_TIDY=tidy, STYLECHECK_PLUGIN=plugin,
            STYLECHECK_DB=str(database))
        legacy = ("struct PreviewPage {\n\tint width = 0;\n"
            "\tPreviewPage(int first, int second);\n};\n\n"
            "PreviewPage::PreviewPage(\n\t\tint first,\n\tint second)\n"
            ": width(first + second) {\n}\n")
        repo.commit({name: legacy})
        broken = legacy.replace("\tint second)", "\t\tint second)")
        repo.put({name: broken})
        source.write_text(legacy)
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("preview.cpp:8:", why)
        self.assertIn("tdesktop-constructor-param-indent", why)
        self.assertNotIn("preview.cpp:7:", why)

        fixed = legacy.replace("\t\tint first,", "\tint first,")
        repo.put({name: fixed})
        source.write_text(broken)
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

    def test_empty_function_param_indent_uses_the_staged_body(self):
        plugin, tidy = self.clang_tools()
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        root = Path(repo.path)
        name = "Telegram/SourceFiles/empty.cpp"
        source = root / name
        database = root / "out/clang-tidy"
        database.mkdir(parents=True)
        (database / "compile_commands.json").write_text(json.dumps([{
            "directory": str(root),
            "file": str(source),
            "arguments": ["clang++", "-std=c++20", "-c", str(source)],
        }]))
        env = dict(os.environ, CLANG_TIDY=tidy, STYLECHECK_PLUGIN=plugin,
            STYLECHECK_DB=str(database))
        empty = "void check(\n\t\tint width) {\n}\n"
        nonempty = empty.replace(") {\n", ") {\n\t++width;\n")
        repo.commit({name: "void check(int width);\n"})
        repo.put({name: empty})
        source.write_text(nonempty)
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("empty.cpp:2:", why)
        self.assertIn("tdesktop-empty-function-param-indent", why)

        repo.put({name: nonempty})
        source.write_text(empty)
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

    def test_lambda_argument_indent_checks_changed_capture_line(self):
        plugin, tidy = self.clang_tools()
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        root = Path(repo.path)
        name = "Telegram/SourceFiles/callback.cpp"
        source = root / name
        database = root / "out/clang-tidy"
        database.mkdir(parents=True)
        (database / "compile_commands.json").write_text(json.dumps([{
            "directory": str(root),
            "file": str(source),
            "arguments": ["clang++", "-std=c++20", "-c", str(source)],
        }]))
        env = dict(os.environ, CLANG_TIDY=tidy, STYLECHECK_PLUGIN=plugin,
            STYLECHECK_DB=str(database))
        original = ("template <typename F> void guard(int, F) {}\n"
            "void setup() {\n\tguard(\n\t\t1,\n\t\t[=](\n"
            "\t\t\t\tint first,\n\t\t\t\tint second) {\n"
            "\t\t\treturn first + second;\n\t\t});\n}\n")
        repo.commit({name: original})
        changed = original.replace("[=]", "[&]")
        repo.put({name: changed})
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("callback.cpp:6:", why)
        self.assertIn("tdesktop-lambda-argument-param-indent", why)

        repo.put({name: changed.replace("\t\t\t\tint", "\t\t\tint")})
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

    def test_nested_multiline_call_checks_the_staged_opening(self):
        plugin, tidy = self.clang_tools()
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        root = Path(repo.path)
        name = "Telegram/SourceFiles/nesting.cpp"
        source = root / name
        database = root / "out/clang-tidy"
        database.mkdir(parents=True)
        (database / "compile_commands.json").write_text(json.dumps([{
            "directory": str(root),
            "file": str(source),
            "arguments": ["clang++", "-std=c++20", "-c", str(source)],
        }]))
        env = dict(os.environ, CLANG_TIDY=tidy, STYLECHECK_PLUGIN=plugin,
            STYLECHECK_DB=str(database))
        declarations = ("int add(int value);\nint make(int x, int y);\n\n"
            "int preview() {\n")
        packed = declarations + "\treturn add(make(\n\t\t1,\n\t\t2));\n}\n"
        split = declarations + "\treturn add(\n\t\tmake(\n\t\t\t1,\n\t\t\t2));\n}\n"
        repo.commit({name: split})
        repo.put({name: packed})
        source.write_text(split)
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("nesting.cpp:5:", why)
        self.assertIn("tdesktop-nested-multiline-call", why)

        repo.commit({name: packed})
        repo.put({name: packed.replace("2));", "3));")})
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

        repo.put({name: split})
        source.write_text(packed)
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

    def test_logical_checks_use_changed_continuation_lines(self):
        plugin, tidy = self.clang_tools()
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        root = Path(repo.path)
        name = "Telegram/SourceFiles/logic.cpp"
        source = root / name
        database = root / "out/clang-tidy"
        database.mkdir(parents=True)
        (database / "compile_commands.json").write_text(json.dumps([{
            "directory": str(root),
            "file": str(source),
            "arguments": ["clang++", "-std=c++20", "-c", str(source)],
        }]))
        env = dict(os.environ, CLANG_TIDY=tidy, STYLECHECK_PLUGIN=plugin,
            STYLECHECK_DB=str(database))
        original = ("bool enabled(bool a, bool b, bool c) {\n"
            "\treturn a && (b\n\t\t|| c);\n}\n")
        flattened = original.replace("a && (b", "a\n\t\t&& (b")
        repo.commit({name: original})
        repo.put({name: flattened})
        self.assertNotIn("+\t\t|| c", repo.git("diff", "--cached", "-U0"))
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("logic.cpp:4:", why)
        self.assertIn("tdesktop-logical-group-indent", why)

        repo.put({name: flattened.replace("\t\t|| c", "\t\t\t|| c")})
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

        legacy = ("int choose(bool a, bool b, bool c) {\n"
            "\treturn a\n\t\t&& b\n\t\t? 1\n\t\t: 0;\n}\n")
        repo.commit({name: legacy})
        flattened = legacy.replace("&& b", "&& c")
        repo.put({name: flattened})
        self.assertNotIn("+\treturn", repo.git("diff", "--cached", "-U0"))
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("tdesktop-ternary-branch-indent", why)

        indented = flattened.replace("\t\t?", "\t\t\t?").replace("\t\t:", "\t\t\t:")
        repo.put({name: indented})
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

    def test_header_only_commit_uses_a_built_consumer_and_staged_header(self):
        plugin, tidy = self.clang_tools()
        if not shutil.which("ninja") or not shutil.which("clang++"):
            self.skipTest("Ninja and clang++ required")
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        header = "Telegram/SourceFiles/component.h"
        untouched = "Telegram/SourceFiles/untouched.h"
        source = "Telegram/SourceFiles/consumer.cpp"
        root = Path(repo.path)
        build = root / "out"
        build.mkdir()
        (build / "build.ninja").write_text(
            "rule cxx\n"
            "  command = clang++ -MMD -MF $out.d -I.. -c $in -o $out\n"
            "  depfile = $out.d\n"
            "  deps = gcc\n"
            f"build consumer.o: cxx ../{source}\n")
        good = "struct Component { int count = 0; };\n"
        bad = "struct Component { int count; };\n"
        repo.commit({header: good, untouched: "struct Legacy { int value; };\n",
            source: f'#include "{header}"\n#include "{untouched}"\n'})
        subprocess.run(["ninja", "-C", str(build)], check=True,
            capture_output=True)
        env = dict(os.environ, CLANG_TIDY=tidy, STYLECHECK_PLUGIN=plugin,
            STYLECHECK_DB=str(build / "clang-tidy"))
        generator = Path(HERE).parent / "clangtidy/compile_db.py"
        subprocess.run([sys.executable, generator, build], cwd=root,
            env=env, check=True, capture_output=True)

        repo.put({header: bad})
        (root / header).write_text(good)
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("component.h:1:", why)
        self.assertIn("tdesktop-init-basic-field", why)
        self.assertNotIn("untouched.h:", why)

        repo.put({header: good + "\n"})
        (root / header).write_text(bad)
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

        constructor = ("struct Component {\n\tint count = 0;\n"
            "\tComponent(\n\t\t\tint value)\n\t: count(value) {\n\t}\n};\n")
        repo.put({header: constructor})
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("component.h:4:", why)
        self.assertIn("tdesktop-constructor-param-indent", why)

    def test_clang_tidy_reads_staged_source_and_header(self):
        plugin, tidy = self.clang_tools()
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        name = "Telegram/SourceFiles/check.cpp"
        header = "Telegram/SourceFiles/check.h"
        root = Path(repo.path)
        source = root / name
        database = root / "out/clang-tidy"
        database.mkdir(parents=True)
        (database / "compile_commands.json").write_text(json.dumps([{
            "directory": str(root),
            "file": str(source),
            "arguments": ["clang++", "-c", str(source)],
        }]))
        clean = '#include "check.h"\n\nvoid use() {\n}\n'
        removable = "\tint count = 0;\n"
        old_violation = ("\nvoid existing() {\n" + removable
            + "\tItem *item = nullptr;\n}\n")
        repo.commit({name: clean + old_violation, header: "struct Item {};\n"})
        repo.put({name: clean.replace("void use() {\n",
            "void use() {\n\tItem *item = nullptr;\n") + old_violation})
        source.write_text(clean)
        (root / header).write_text("#error Unstaged header must not be read\n")
        env = dict(os.environ, CLANG_TIDY=tidy, STYLECHECK_PLUGIN=str(plugin),
            STYLECHECK_DB=str(database))
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("check.cpp:4:", why)
        self.assertIn("tdesktop-pointer-nullptr-auto", why)
        self.assertNotIn("Unstaged header", why)

        fixed = clean.replace("void use() {\n",
            "void use() {\n\tauto item = static_cast<Item*>(nullptr);\n")
        repo.put({name: fixed + old_violation})
        code, why = repo.staged(env)
        self.assertEqual(code, 0, why)

        repo.commit({name: fixed + old_violation})
        repo.put({name: fixed + old_violation.replace(removable, "")})
        code, why = repo.staged(env)
        self.assertEqual(code, 1, why)
        self.assertIn("tdesktop-pointer-nullptr-auto", why)


class CommitMsgHook(unittest.TestCase):
    def test_staged_hook_changes_require_scope(self):
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        root = Path(repo.path)
        name = "tools/rules/check_rule.py"
        repo.git("config", "core.hooksPath", ".git/hooks")
        repo.commit({name: "before\n", "ordinary.txt": "before\n",
            ".clang-tidy": "",
            **{"tools/rules/" + source: (Path(HERE) / source).read_text()
                for source in ("hook.py", "clang_style.py", "cpp_layout.py",
                    "laws/catalog.json", "laws/closing_parenthesis.py")}})
        self.install(repo)

        (root / name).write_text("after\n")
        repo.put({"ordinary.txt": "after\n"})
        repo.git("commit", "-qm", "Improved ordinary file.")
        repo.put({"a.cpp": "x" * 79 + "\n"})
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            repo.git("commit", "-qm", "Added line to source.")
        self.assertIn("79 columns", failure.exception.stderr)
        repo.git("rm", "--cached", "--force", "a.cpp")

        repo.git("add", name)
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            repo.git("commit", "-qm", "Improved pre-commit hook.")
        self.assertIn("must start with [git-hook]", failure.exception.stderr)
        repo.git("commit", "-qm", "[git-hook] change hook checks")

        repo.git("mv", name, "moved.py")
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            repo.git("commit", "-qm", "Moved pre-commit hook.")
        self.assertIn("must start with [git-hook]", failure.exception.stderr)
        repo.git("commit", "-qm", "[git-hook] Moved pre-commit hook.")
        repo.git("revert", "--no-commit", "HEAD")
        repo.git("commit", "-qm",
            '[git-hook] Reverted "[git-hook] Moved pre-commit hook."')

    def test_reinstall_keeps_existing_commit_msg(self):
        repo = Repository()
        self.addCleanup(repo.folder.cleanup)
        repo.git("config", "core.hooksPath", ".git/hooks")
        repo.commit({"ordinary.txt": "before\n"})
        previous = Path(repo.path) / ".git/hooks/commit-msg"
        previous.write_text("#!/bin/sh\necho Existing validator >&2\nexit 1\n")
        previous.chmod(0o755)
        self.install(repo)
        self.install(repo)

        repo.put({"ordinary.txt": "after\n"})
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            repo.git("commit", "-qm", "Improved ordinary file.")
        self.assertEqual(failure.exception.stderr.count("Existing validator"), 1)

    def install(self, repo):
        result = subprocess.run([sys.executable, INSTALL], cwd=repo.path,
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
