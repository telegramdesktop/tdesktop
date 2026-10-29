import contextlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
from types import SimpleNamespace
import unittest

import atlas
import clones
import cmake_model
import cpp_text
import health
import includes
import metrics
import report
import review
import sources


def dedent(text):
    return textwrap.dedent(text).lstrip('\n')


CMAKE = dedent('''
    add_library(td_low OBJECT)
    add_library(tdesktop::td_low ALIAS td_low)
    target_precompile_headers(td_low PRIVATE ${src_loc}/low/low_pch.h)
    nice_target_sources(td_low ${src_loc}
    PRIVATE
        low/low_pch.h
        low/low.cpp
        low/low.h
    )
    target_link_libraries(td_low PUBLIC desktop-app::lib_base)
    add_executable(Telegram WIN32 MACOSX_BUNDLE)
    nice_target_sources(Telegram ${src_loc}
    PRIVATE
        app/app.cpp
        app/app.h
    )
    target_link_libraries(Telegram PRIVATE tdesktop::td_low)
''')

LOW = dedent('''
    #include "low/low.h"

    namespace Low {

    int Twice(int value) {
    	return value * 2;
    }

    } // namespace Low
''')

APP = dedent('''
    #include "app/app.h"
    #include "low/low.h"

    namespace App {

    int Run(
    		int value) {
    	return Low::Twice(value);
    }

    } // namespace App
''')

PANEL = dedent('''
    #include "low/low.h"

    namespace Low {

    class Panel {
    public:
    	void resize(int width);
    	void finalizeLayout(int width);

    private:
    	int _width = 0;

    };

    void Panel::resize(int width) {
    	if (_width != width) {
    		finalizeLayout(width);
    	}
    }

    // keeps the width positive, as std::max(width, 1) does in callers
    void Panel::finalizeLayout(int width) {
    	_width = width;
    }

    } // namespace Low
''')


class Repo:
    def __init__(self, files):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.git('init', '-q', '-b', 'dev')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.write(files)
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'Initial.')

    def git(self, *args):
        return subprocess.run(
            ['git', '-C', str(self.root), *args],
            capture_output=True, check=True).stdout.decode()

    def write(self, files, newline=None):
        for path, text in files.items():
            target = self.root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            if newline is None:
                target.write_text(text, encoding='utf-8')
            else:
                target.write_bytes(text.replace('\n', newline).encode('utf-8'))

    def remove(self, path):
        (self.root / path).unlink()


def fixture():
    src = 'Telegram/SourceFiles/'
    return {
        'Telegram/CMakeLists.txt': CMAKE,
        src + 'low/low_pch.h': '#pragma once\n',
        src + 'low/low.h': '#pragma once\n\nnamespace Low {\nint Twice(int value);\n} // namespace Low\n',
        src + 'low/low.cpp': LOW,
        src + 'app/app.h': '#pragma once\n',
        src + 'app/app.cpp': APP,
    }


class TextTests(unittest.TestCase):
    def test_functions_span_signature_to_closing_brace(self):
        found = cpp_text.functions(APP)
        self.assertEqual([(f.name, f.start, f.end) for f in found], [('Run', 6, 9)])

    def test_initializer_list_is_part_of_constructor(self):
        text = dedent('''
            Foo::Foo(int value)
            : _value(value)
            , _other(std::make_unique<Other>(
            	value)) {
            	init();
            }
        ''')
        found = cpp_text.functions(text)
        self.assertEqual([(f.name, f.lines) for f in found], [('Foo::Foo', 6)])

    def test_license_header_does_not_hide_the_first_function(self):
        text = dedent('''
            /*
            This file is part of Telegram Desktop,
            the official desktop application for the Telegram messaging service.
            */
            #include "app/app.h"

            namespace App {

            void First() {
            	run();
            }

            } // namespace App
        ''')
        self.assertEqual([f.name for f in cpp_text.functions(text)], ['First'])

    def test_aggregates_and_forward_declarations_are_not_functions(self):
        text = 'const auto kList = Items{\n\t1,\n};\nclass Foo;\nvoid Bar();\n'
        self.assertEqual(cpp_text.functions(text), [])

    def test_object_constructed_from_braces_is_not_a_function(self):
        text = 'base::options::toggle OptionFoo({\n\t.id = kOptionFoo,\n});\n'
        self.assertEqual(cpp_text.functions(text), [])

    def test_class_members_are_counted(self):
        text = dedent('''
            class Foo final : public Bar {
            public:
            	void a();
            	[[nodiscard]] int b(
            		int value) const;

            private:
            	int _x = 0;
            	QString _y;

            };
        ''')
        [found] = cpp_text.classes(text)
        self.assertEqual((found.name, found.methods, found.fields), ('Foo', 2, 2))

    def test_block_comments_do_not_count_as_code(self):
        lines = cpp_text.code_lines('/*\nThis file is part of X\n*/\nint a = 0;')
        self.assertEqual([cpp_text.normalize(line) for line in lines],
                         ['', '', '', 'int a = 0;'])

    def test_numeric_literal_letters_are_not_names(self):
        literals = ('1e-4', '0x1F', '10ull', '0.5f', '.5f', '0b1010', '0x1.fp+2F',
                    "1'000ULL", '1e+4', '42_units')
        for literal in literals:
            with self.subTest(literal=literal):
                line = f'constexpr auto kScaled1 = kBaseStep * {literal} + kBaseStep;'
                self.assertEqual(review.identifiers(line),
                                 ['constexpr', 'auto', 'kScaled1', 'kBaseStep', 'kBaseStep'])

    def test_nonmatching_pattern_does_not_declare(self):
        self.assertFalse(metrics.declares('constexpr auto kEpsilon = 1e-4;', re.compile(r'\be\b')))


class GraphTests(unittest.TestCase):
    def setUp(self):
        self.repo = Repo(fixture())
        self.addCleanup(self.repo.temp.cleanup)

    def model_and_graph(self):
        tree = sources.from_worktree(self.repo.root)
        model = cmake_model.build(tree.files)
        return tree, model, includes.IncludeGraph(tree, model)

    def test_targets_and_pch(self):
        _, model, _ = self.model_and_graph()
        self.assertEqual(model.target_of('Telegram/SourceFiles/low/low.cpp'), 'td_low')
        self.assertEqual(model.pch['td_low'], 'Telegram/SourceFiles/low/low_pch.h')
        self.assertIn('td_low', model.dependencies('Telegram'))
        self.assertNotIn('Telegram', model.dependencies('td_low'))

    def test_upward_include_is_a_layering_violation(self):
        self.repo.write({'Telegram/SourceFiles/low/low.cpp': '#include "app/app.h"\n' + LOW})
        _, _, graph = self.model_and_graph()
        found = graph.layering()
        self.assertEqual([(v['from'], v['to']) for v in found], [('td_low', 'Telegram')])

    def test_reach_and_weight(self):
        tree, _, graph = self.model_and_graph()
        costs = graph.costs()
        self.assertEqual(costs['reach']['Telegram/SourceFiles/low/low.h'], 2)
        self.assertEqual(costs['weight']['Telegram/SourceFiles/app/app.cpp'],
                         tree.lines('Telegram/SourceFiles/app/app.h')
                         + tree.lines('Telegram/SourceFiles/low/low.h'))


class CloneTests(unittest.TestCase):
    BODY = ''.join(f'\tauto value{i} = compute({i}, other{i});\n' for i in range(12))

    def test_repeated_block_is_found_once(self):
        files = {
            'Telegram/SourceFiles/a.cpp': 'void A() {\n' + self.BODY + '}\n',
            'Telegram/SourceFiles/b.cpp': 'void B() {\n' + self.BODY + '}\n',
        }
        [clone] = clones.find(files)
        self.assertEqual(clone['lines'], 12)
        self.assertEqual(clone['left']['path'], 'Telegram/SourceFiles/a.cpp')

    def test_mirrored_declarations_are_not_clones(self):
        decls = ''.join(f'\tvoid method{i}(int value);\n' for i in range(12))
        files = {
            'Telegram/SourceFiles/a.h': 'class A {\n' + decls + '};\n',
            'Telegram/SourceFiles/b.cpp': 'class Impl {\n' + decls + '};\n',
        }
        self.assertEqual(clones.find(files), [])


class UnusedTests(unittest.TestCase):
    def test_declared_but_never_called(self):
        repo = Repo(dict(fixture(), **{
            'Telegram/SourceFiles/low/low.h': '#pragma once\nint Twice(int value);\nint Thrice(int value);\n',
            'Telegram/SourceFiles/low/low.cpp': LOW + '\nint Thrice(int value) {\n\treturn value * 3;\n}\n',
        }))
        self.addCleanup(repo.temp.cleanup)
        tree = sources.from_worktree(repo.root)
        names = {item['name'] for item in metrics.unused_candidates(tree)}
        self.assertEqual(names, {'Run', 'Thrice'})

    def test_initializer_list_call_is_a_use(self):
        helper = 'int Label(int value) {\n\treturn value;\n}\n\n'
        owner = 'Owner::Owner(int value)\n: _first(value)\n, _label(Label(value)) {\n}\n'
        repo = Repo(dict(fixture(), **{
            'Telegram/SourceFiles/low/low.cpp': LOW + '\n' + helper + owner,
        }))
        self.addCleanup(repo.temp.cleanup)
        tree = sources.from_worktree(repo.root)
        names = {item['name'] for item in metrics.unused_candidates(tree)}
        self.assertNotIn('Label', names)


class MapTests(unittest.TestCase):
    def test_map_naming_a_missing_file_is_stale(self):
        repo = Repo(dict(fixture(), **{
            'Telegram/SourceFiles/low/AGENTS.md': 'Pure helpers live in `low.cpp`; '
                                                  'paths in `low_paths.cpp`.\n',
        }))
        self.addCleanup(repo.temp.cleanup)
        tree = sources.from_worktree(repo.root)
        maps = report.map_references(tree)
        self.assertEqual(maps, {'Telegram/SourceFiles/low/AGENTS.md': {'missing': ['low_paths.cpp']}})


class CheckTests(unittest.TestCase):
    def setUp(self):
        self.repo = Repo(fixture())
        self.addCleanup(self.repo.temp.cleanup)

    def run_check(self, strict=False):
        tree = sources.from_worktree(self.repo.root)
        return review.check(tree, 'HEAD', False, strict)

    def test_clean_change(self):
        self.repo.write({'Telegram/SourceFiles/app/app.cpp': APP + '\n'})
        self.assertEqual(self.run_check(), ([], []))

    def test_new_upward_include_is_an_error(self):
        self.repo.write({'Telegram/SourceFiles/low/low.cpp': '#include "app/app.h"\n' + LOW})
        errors, _ = self.run_check()
        self.assertEqual(len(errors), 1)
        self.assertIn('does not link', errors[0])

    def test_oversized_file_may_not_grow(self):
        big = 'Telegram/SourceFiles/app/app.cpp'
        filler = ''.join(f'int Value{i}() {{\n\treturn {i};\n}}\n\n' for i in range(1100))
        self.repo.write({big: APP + filler})
        self.repo.git('commit', '-qam', 'Big.')
        self.repo.write({big: APP + filler + 'int Extra() {\n\treturn 0;\n}\n'})
        errors, warnings = self.run_check(strict=True)
        self.assertEqual(errors, [])
        self.assertFalse(any('grows' in w for w in warnings))
        feature = ''.join(f'int More{i}() {{\n\treturn {i};\n}}\n' for i in range(10))
        self.repo.write({big: APP + filler + feature})
        _, warnings = self.run_check()
        self.assertTrue(any('grows' in w for w in warnings))
        errors, _ = self.run_check(strict=True)
        self.assertTrue(any('grows' in e for e in errors))

    def test_copy_paste_is_reported(self):
        body = ''.join(f'\tauto value{i} = compute({i}, other{i});\n' for i in range(12))
        self.repo.write({'Telegram/SourceFiles/low/low.cpp': LOW + 'void A() {\n' + body + '}\n'})
        self.repo.git('commit', '-qam', 'A.')
        self.repo.write({'Telegram/SourceFiles/app/app.cpp': APP + 'void B() {\n' + body + '}\n'})
        _, warnings = self.run_check()
        self.assertTrue(any('repeats' in w for w in warnings))

    def test_removed_definition_still_mentioned_is_reported(self):
        platform = 'Telegram/SourceFiles/platform/win/specific_win.cpp'
        self.repo.write({platform: 'int Native() {\n\treturn Low::Twice(1);\n}\n'})
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Platform.')
        self.repo.write({'Telegram/SourceFiles/low/low.cpp': LOW.replace(
            'int Twice(int value) {\n\treturn value * 2;\n}\n\n', '')})
        _, warnings = self.run_check()
        stranded = [w for w in warnings if w.startswith('Twice:')]
        self.assertEqual(len(stranded), 1)
        self.assertIn(platform, stranded[0])
        self.assertIn('low/low.h', stranded[0])

    def test_untracked_new_file_is_checked(self):
        filler = ''.join(f'int Value{i}() {{\n\treturn {i};\n}}\n\n' for i in range(1100))
        self.repo.write({'Telegram/SourceFiles/app/big.cpp': filler})
        errors, _ = self.run_check()
        self.assertTrue(any('new file starts at' in e for e in errors), errors)

    def test_known_crossing_is_a_warning(self):
        self.repo.write({'Telegram/SourceFiles/low/low.h': '#include "app/app.h"\n#pragma once\n'})
        self.repo.git('commit', '-qam', 'Existing crossing.')
        self.repo.write({'Telegram/SourceFiles/low/low.cpp': '#include "app/app.h"\n' + LOW})
        errors, warnings = self.run_check()
        self.assertEqual(errors, [])
        self.assertTrue(any('already exist' in w for w in warnings), warnings)

    def test_platform_file_mention_of_removed_method_is_reported(self):
        src = 'Telegram/SourceFiles/'
        method = 'int Panel::uniqueRadius() const {\n\treturn 4;\n}\n'
        mm = src + 'platform/mac/specific_mac.mm'
        self.repo.write({
            src + 'low/low.cpp': LOW + '\n' + method,
            mm: 'void Use(Holder *holder) {\n\tuse(holder->panel()->uniqueRadius());\n}\n',
        })
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Panel.')
        self.repo.write({src + 'low/low.cpp': LOW + '\n'})
        _, warnings = self.run_check()
        self.assertTrue(any(w.startswith('uniqueRadius:') and mm in w for w in warnings), warnings)

    def test_removed_method_counts_only_files_naming_its_class(self):
        src = 'Telegram/SourceFiles/'
        method = 'int Panel::radius() const {\n\treturn 4;\n}\n'
        self.repo.write({
            src + 'low/low.cpp': LOW + '\n' + method,
            src + 'app/other.cpp': 'void Paint() {\n\tconst auto radius = 2;\n\tuse(radius);\n}\n',
            src + 'app/user.cpp': 'void Use(Panel *panel) {\n\tuse(panel->radius());\n}\n',
        })
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Panel.')
        self.repo.write({src + 'low/low.cpp': LOW + '\n'})
        _, warnings = self.run_check()
        stranded = [w for w in warnings if w.startswith('radius:')]
        self.assertEqual(len(stranded), 1, warnings)
        self.assertIn('app/user.cpp', stranded[0])
        self.assertNotIn('app/other.cpp', stranded[0])

    def test_removed_last_use_reports_orphaned_helper(self):
        src = 'Telegram/SourceFiles/'
        helper = 'int Helper(int value) {\n\treturn value + 1;\n}\n\n'
        caller = 'int Caller(int value) {\n\treturn Helper(value);\n}\n'
        self.repo.write({src + 'low/low.cpp': LOW + '\n' + helper + caller})
        self.repo.git('commit', '-qam', 'Helper.')
        self.repo.write({src + 'low/low.cpp': LOW + '\n' + helper})
        _, warnings = self.run_check()
        self.assertTrue(any(w.startswith('Helper:') and 'last use' in w for w in warnings), warnings)

    def test_else_branch_call_is_a_use(self):
        src = 'Telegram/SourceFiles/'
        helpers = ('int Helper(int value) {\n\treturn value + 1;\n}\n\n'
                   'int Fallback(int value) {\n\treturn value - 1;\n}\n\n')
        branches = dedent('''
            int Pick(bool flag, int value) {
            	return flag
            		? value
            		: Helper(value);
            }

            int Chain(bool flag, bool other, int value) {
            	return flag
            		? 0
            		: other
            		? Apply([=] {
            			return value;
            		})
            		: Fallback(value);
            }
        ''')
        plain = '\nint Plain(int value) {\n\treturn Helper(value) + Fallback(value);\n}\n'
        self.repo.write({src + 'low/low.cpp': LOW + '\n' + helpers + branches + plain})
        self.repo.git('commit', '-qam', 'Helpers.')
        self.repo.write({src + 'low/low.cpp': LOW + '\n' + helpers + branches
                         + plain.replace('Helper(value) + Fallback(value)', 'value')})
        self.assertEqual(self.run_check(), ([], []))

    def test_removed_else_branch_call_reports_orphaned_helper(self):
        src = 'Telegram/SourceFiles/'
        helper = 'int Helper(int value) {\n\treturn value + 1;\n}\n\n'
        pick = ('int Pick(bool flag, int value) {\n\treturn flag\n'
                '\t\t? value\n\t\t: Helper(value);\n}\n')
        self.repo.write({src + 'low/low.cpp': LOW + '\n' + helper + pick})
        self.repo.git('commit', '-qam', 'Helper.')
        self.repo.write({src + 'low/low.cpp': LOW + '\n' + helper
                         + pick.replace(': Helper(value);', ': value + 1;')})
        _, warnings = self.run_check()
        orphans = [w.split(':')[0] for w in warnings if 'last use' in w]
        self.assertEqual(orphans, ['Helper'], warnings)

    def test_initializers_still_define_their_fields(self):
        src = 'Telegram/SourceFiles/'
        owner = dedent('''
            class Owner {
            public:
            	Owner()
            	: _first(0)
            	, _second{ 0 } {
            	}
            	Owner(int value);

            	[[nodiscard]] int sum() const;

            private:
            	int _first = 0;
            	int _second = 0;

            };

            Owner::Owner(int value)
            : _first(value)
            , _second{ value } {
            }

            int Owner::sum() const {
            	return _first + _second;
            }
        ''')
        self.repo.write({src + 'low/owner.cpp': owner})
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Owner.')
        self.repo.write({src + 'low/owner.cpp': owner.replace('_first + _second', '0')})
        _, warnings = self.run_check()
        orphans = [w.split(':')[0] for w in warnings if 'last use' in w]
        self.assertEqual(sorted(orphans), ['_first', '_second'], warnings)

    def test_base_class_mention_is_a_use(self):
        src = 'Telegram/SourceFiles/'
        base = 'class Host {\npublic:\n\tvirtual ~Host() = default;\n};\n'
        impl = 'class Engine::Impl final : public Host {\n};\n'
        user = 'class Fake final : public Host {\n};\n'
        self.repo.write({
            src + 'low/host.h': '#pragma once\n\n' + base,
            src + 'low/engine.cpp': impl,
            src + 'low/fake.cpp': user,
        })
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Host.')
        self.repo.remove(src + 'low/fake.cpp')
        _, warnings = self.run_check()
        self.assertFalse(any(w.startswith('Host:') for w in warnings), warnings)

    def test_removed_last_use_reports_orphaned_style_and_key(self):
        src = 'Telegram/SourceFiles/'
        style = 'using "ui/basic.style";\n\nmenuIconUnique: icon {{ "menu/unique", menuIconColor }};\nmenuIconShared: icon {{ "menu/shared", menuIconColor }};\n'
        lang = '"lng_unique_label" = "Unique";\n"lng_shared_label" = "Shared";\n'
        use = ('void Fill() {\n\tuse(st::menuIconUnique, tr::lng_unique_label(tr::now));\n'
               '\tuse(st::menuIconShared, tr::lng_shared_label(tr::now));\n}\n')
        other = 'void Other() {\n\tuse(st::menuIconShared, tr::lng_shared_label(tr::now));\n}\n'
        self.repo.write({
            src + 'ui/menu_icons.style': style,
            'Telegram/Resources/langs/lang.strings': lang,
            src + 'low/fill.cpp': use,
            src + 'app/other.cpp': other,
        })
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Styles.')
        self.repo.write({src + 'low/fill.cpp': use.replace(
            '\tuse(st::menuIconUnique, tr::lng_unique_label(tr::now));\n', '')})
        _, warnings = self.run_check()
        orphans = [w.split(':')[0] for w in warnings if 'last use' in w]
        self.assertEqual(sorted(orphans), ['lng_unique_label', 'menuIconUnique'], warnings)

    def test_removed_numeric_literals_keep_the_check_running(self):
        src = 'Telegram/SourceFiles/'
        constants = ('constexpr auto kEpsilon = 1e-4;\nconstexpr auto kMask = 0x1F;\n'
                     'constexpr auto kLimit = 10ull;\nconstexpr auto kHalf = 0.5f;\n\n')
        self.repo.write({src + 'low/low.cpp': LOW.replace(
            'namespace Low {\n\n', 'namespace Low {\n\n' + constants)})
        self.repo.git('commit', '-qam', 'Constants.')
        self.repo.write({src + 'low/low.cpp': LOW})
        self.assertEqual(self.run_check(), ([], []))

    def test_last_use_beside_a_numeric_literal_reports_orphan(self):
        src = 'Telegram/SourceFiles/'
        step = 'constexpr auto kBaseStep = 4;\n'
        scaled = 'constexpr auto kScaled = kBaseStep * 1e-4;\n'
        self.repo.write({src + 'low/low.cpp': LOW.replace(
            'namespace Low {\n\n', 'namespace Low {\n\n' + step + scaled + '\n')})
        self.repo.git('commit', '-qam', 'Scaled.')
        self.repo.write({src + 'low/low.cpp': LOW.replace(
            'namespace Low {\n\n', 'namespace Low {\n\n' + step + '\n')})
        errors, warnings = self.run_check()
        self.assertEqual(errors, [])
        self.assertEqual(len(warnings), 1, warnings)
        self.assertTrue(warnings[0].startswith('kBaseStep:'), warnings)
        self.assertIn('the change removed its last use; only its definition remains',
                      warnings[0], warnings)
        self.assertIn(src + 'low/low.cpp', warnings[0], warnings)

    def test_removed_style_definition_still_used_is_reported(self):
        src = 'Telegram/SourceFiles/'
        style = 'menuIconGone: icon {{ "menu/gone", menuIconColor }};\n'
        self.repo.write({
            src + 'ui/menu_icons.style': style,
            src + 'platform/win/menu_win.cpp': 'void Use() {\n\tuse(st::menuIconGone);\n}\n',
        })
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Style.')
        self.repo.write({src + 'ui/menu_icons.style': '\n'})
        _, warnings = self.run_check()
        self.assertTrue(any(w.startswith('menuIconGone:') and 'menu_win.cpp' in w for w in warnings), warnings)

    def test_complete_deletion_is_clean(self):
        src = 'Telegram/SourceFiles/'
        self.repo.write({
            src + 'app/app.cpp': APP.replace('Low::Twice(value)', 'value'),
            src + 'low/low.cpp': LOW.replace(
                'int Twice(int value) {\n\treturn value * 2;\n}\n\n', ''),
            src + 'low/low.h': '#pragma once\n',
        })
        self.assertEqual(self.run_check(), ([], []))

    def test_moved_definition_is_not_stranded(self):
        src = 'Telegram/SourceFiles/'
        self.repo.write({
            src + 'low/low.cpp': LOW.replace(
                'int Twice(int value) {\n\treturn value * 2;\n}\n\n', ''),
            src + 'low/twice.cpp': '#include "low/low.h"\n\nnamespace Low {\n\n'
                'int Twice(int value) {\n\treturn value * 2;\n}\n\n} // namespace Low\n',
        })
        _, warnings = self.run_check()
        self.assertFalse(any(w.startswith('Twice:') for w in warnings), warnings)

    def commit_panel(self):
        path = 'Telegram/SourceFiles/low/panel.cpp'
        self.repo.write({'.gitattributes': '* text=auto\n', path: PANEL}, newline='\n')
        self.assertNotIn(b'\r\n', (self.repo.root / path).read_bytes())
        self.repo.git('add', '-A')
        self.repo.git('commit', '-qm', 'Panel.')
        self.assertNotIn('\r\n', self.repo.git('show', 'HEAD:' + path))
        return path

    def test_crlf_tree_keeps_a_commented_definition(self):
        path = self.commit_panel()
        grown = PANEL.replace('} // namespace Low\n',
                              'int Unrelated() {\n\treturn 0;\n}\n\n} // namespace Low\n')
        self.repo.write({path: grown}, newline='\r\n')
        self.assertIn(b'\r\n', (self.repo.root / path).read_bytes())
        crlf = self.run_check()
        self.repo.write({path: grown}, newline='\n')
        self.assertNotIn(b'\r\n', (self.repo.root / path).read_bytes())
        lf = self.run_check()
        with self.subTest('CRLF keeps the definition'):
            self.assertFalse(any('definition removed' in w for w in crlf[1]), crlf[1])
        with self.subTest('CRLF equals LF'):
            self.assertEqual(crlf, lf)
        with self.subTest('LF is clean'):
            self.assertEqual(lf, ([], []))

    def test_crlf_tree_still_reports_a_removed_definition(self):
        path = self.commit_panel()
        definition = ('// keeps the width positive, as std::max(width, 1) does in callers\n'
                      'void Panel::finalizeLayout(int width) {\n\t_width = width;\n}\n\n')
        removed = PANEL.replace(definition, '')
        self.assertNotEqual(removed, PANEL)
        self.repo.write({path: removed}, newline='\r\n')
        self.assertIn(b'\r\n', (self.repo.root / path).read_bytes())
        crlf = self.run_check()
        self.repo.write({path: removed}, newline='\n')
        self.assertNotIn(b'\r\n', (self.repo.root / path).read_bytes())
        lf = self.run_check()
        errors, warnings = crlf
        self.assertEqual(errors, [])
        self.assertEqual(len(warnings), 1, warnings)
        self.assertTrue(warnings[0].startswith('finalizeLayout:'), warnings)
        self.assertIn('definition removed but still mentioned', warnings[0], warnings)
        self.assertEqual(crlf, lf)


class VerifyTests(unittest.TestCase):
    def test_deleted_candidate_is_resolved(self):
        repo = Repo(fixture())
        self.addCleanup(repo.temp.cleanup)
        script = Path(__file__).resolve().parent / 'health.py'
        identity = 'unused:Telegram/SourceFiles/app/app.cpp:Run'

        def verify():
            return subprocess.run(
                [sys.executable, str(script), '--repo', str(repo.root), 'verify', identity],
                capture_output=True, text=True)

        before = verify()
        self.assertEqual(before.returncode, 1)
        self.assertIn('present', before.stdout)
        repo.write({'Telegram/SourceFiles/app/app.cpp': APP.replace(
            'int Run(\n\t\tint value) {\n\treturn Low::Twice(value);\n}\n\n', '')})
        after = verify()
        self.assertEqual(after.returncode, 0, after.stdout + after.stderr)
        self.assertIn('resolved', after.stdout)
        typo = subprocess.run(
            [sys.executable, str(script), '--repo', str(repo.root), 'verify', 'clone:000000000000'],
            capture_output=True, text=True)
        self.assertEqual(typo.returncode, 2)
        self.assertIn('unknown', typo.stdout)


class AtlasTests(unittest.TestCase):
    TEXT = dedent("""
        # Source atlas

        ## Model

        ### low — Low-level helpers
        - Paths: `SourceFiles/low/`
        - Owns arithmetic.

        ### app — Application
        - Paths: `SourceFiles/app/`, `SourceFiles/low/special_*`, `SourceFiles/low/low.h`
        - Owns the entry point.
    """)

    def test_most_specific_pattern_wins(self):
        parsed = atlas.Atlas(self.TEXT)
        self.assertEqual(parsed.area_of('Telegram/SourceFiles/low/low.cpp'), 'low')
        self.assertEqual(parsed.area_of('Telegram/SourceFiles/low/special_case.cpp'), 'app')
        self.assertEqual(parsed.area_of('Telegram/SourceFiles/app/app.cpp'), 'app')
        self.assertIsNone(parsed.area_of('Telegram/SourceFiles/other/x.cpp'))

    def test_check_reports_unmapped_and_unused(self):
        repo = Repo(dict(fixture(), **{
            'Telegram/SourceFiles/other/other.cpp': 'void Other() {\n}\n',
        }))
        self.addCleanup(repo.temp.cleanup)
        tree = sources.from_worktree(repo.root)
        result = atlas.check(atlas.Atlas(self.TEXT), tree)
        self.assertEqual(result['unmapped'], ['Telegram/SourceFiles/other/other.cpp'])
        self.assertEqual(result['unused'], [('app', 'SourceFiles/low/special_*')])

    def test_scan_names_areas_by_the_atlas(self):
        repo = Repo(dict(fixture(), **{atlas.ATLAS_PATH: self.TEXT}))
        self.addCleanup(repo.temp.cleanup)
        snap = report.snapshot(sources.from_worktree(repo.root))
        self.assertEqual(snap['files']['Telegram/SourceFiles/low/low.cpp']['area'], 'low')
        self.assertEqual(snap['files']['Telegram/SourceFiles/low/low.h']['area'], 'app')
        found = report.findings(snap)
        self.assertIn('stale-atlas:app:SourceFiles/low/special_*', found)
        self.assertNotIn(f'stale-map:{atlas.ATLAS_PATH}', found)

    def test_large_directories_split_into_large_subdirectories(self):
        files = {
            'Telegram/SourceFiles/big/a.cpp': 20000,
            'Telegram/SourceFiles/big/sub/b.cpp': 15000,
            'Telegram/SourceFiles/big/tiny/c.cpp': 500,
            'Telegram/SourceFiles/small/d.cpp': 1000,
        }
        folders, _ = atlas.candidates(files)
        self.assertEqual(folders, {
            'Telegram/SourceFiles',
            'Telegram/SourceFiles/big',
            'Telegram/SourceFiles/big/sub',
            'Telegram/SourceFiles/small',
        })
        self.assertEqual(atlas.owner('Telegram/SourceFiles/big/tiny/c.cpp', folders),
                         'Telegram/SourceFiles/big')


class StateTests(unittest.TestCase):
    def setUp(self):
        self.repo = Repo(fixture())
        self.addCleanup(self.repo.temp.cleanup)
        queue = tempfile.TemporaryDirectory()
        self.addCleanup(queue.cleanup)
        self.queue = Path(queue.name)
        self.published = self.queue / 'code-health'
        self.state = self.queue / 'worktree' / 'code-health'
        self.commit = self.repo.git('rev-parse', 'HEAD').strip()

    def run_health(self, handler, **values):
        args = dict(repo=str(self.repo.root), queue=str(self.queue), ref='HEAD',
                    fetch=False, state=None, force=False)
        args.update(values)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = handler(SimpleNamespace(**args))
        return code, output.getvalue()

    def test_recorded_scan_writes_state_once_a_day(self):
        _, output = self.run_health(health.command_scan, state=str(self.state))
        self.assertEqual(json.loads(output)['ledger'], str(self.state / 'ledger.json'))
        ledger = json.loads((self.state / 'ledger.json').read_text(encoding='utf-8'))
        self.assertEqual(ledger['commit'], self.commit)
        self.assertTrue((self.state / ledger['run'] / 'report.md').is_file())
        self.assertTrue((self.state / ledger['run'] / 'harness.md').is_file())
        harness_data = json.loads((self.state / ledger['run'] / 'harness.json').read_text())
        self.assertEqual(harness_data['commit'], self.commit)
        self.assertFalse(harness_data['coverage']['available'])
        self.assertTrue((self.published / '.local' / 'snapshots'
                         / f'{self.commit}.json').is_file())
        _, output = self.run_health(health.command_scan, state=str(self.state))
        self.assertIn('skipped', json.loads(output))
        self.run_health(health.command_scan, state=str(self.state), force=True)
        history = (self.state / 'history.jsonl').read_text(encoding='utf-8')
        self.assertEqual(len(history.splitlines()), 2)

    def test_working_tree_scan_leaves_the_ledger_alone(self):
        _, output = self.run_health(health.command_scan, ref=None)
        result = json.loads(output)
        self.assertIsNone(result['ledger'])
        self.assertTrue(Path(result['run_directory'], 'report.md').is_file())
        self.assertFalse(self.published.exists())
        code, _ = self.run_health(health.command_scan, ref=None, state=str(self.state))
        self.assertEqual(code, 2)
        self.assertFalse(self.state.exists())

    def test_area_rebuilds_a_missing_snapshot_of_the_published_scan(self):
        self.run_health(health.command_scan, state=str(self.published))
        (self.published / '.local' / 'snapshots' / f'{self.commit}.json').unlink()
        code, output = self.run_health(health.command_area, area='low')
        self.assertEqual(code, 0)
        self.assertIn('Telegram/SourceFiles/low/low.cpp', output)

    def test_source_worktree_is_created_reused_and_removed(self):
        _, output = self.run_health(health.command_worktree, remove=None)
        path = Path(json.loads(output)['path'])
        self.addCleanup(lambda: path.exists() and self.run_health(
            health.command_worktree, remove=str(path)))
        self.assertEqual(self.repo.git('-C', str(path), 'rev-parse', 'HEAD').strip(),
                         self.commit)
        _, again = self.run_health(health.command_worktree, remove=None)
        self.assertEqual(Path(json.loads(again)['path']), path)
        code, _ = self.run_health(health.command_worktree, remove=str(self.queue))
        self.assertEqual(code, 1)
        self.run_health(health.command_worktree, remove=str(path))
        self.assertFalse(path.exists())


class MoveTests(unittest.TestCase):
    def test_extracting_a_function_into_a_new_file_is_move_only(self):
        repo = Repo(fixture())
        self.addCleanup(repo.temp.cleanup)
        repo.write({
            'Telegram/SourceFiles/app/app.cpp': APP.replace(
                '#include "low/low.h"\n', '#include "low/low.h"\n#include "app/run.h"\n').replace(
                'int Run(\n\t\tint value) {\n\treturn Low::Twice(value);\n}\n\n', ''),
            'Telegram/SourceFiles/app/run.h': '#pragma once\n\nnamespace App {\n\nint Run(\n\t\tint value);\n\n} // namespace App\n',
            'Telegram/SourceFiles/app/run.cpp': '#include "app/run.h"\n\nnamespace App {\n\nint Run(\n\t\tint value) {\n\treturn Low::Twice(value);\n}\n\n} // namespace App\n',
        })
        repo.git('add', '-A')
        result = review.moves(repo.root, 'HEAD', staged=True)
        self.assertTrue(result['move_only'], result)

    def test_move_into_untracked_file_is_move_only(self):
        repo = Repo(fixture())
        self.addCleanup(repo.temp.cleanup)
        repo.write({
            'Telegram/SourceFiles/app/app.cpp': APP.replace(
                'int Run(\n\t\tint value) {\n\treturn Low::Twice(value);\n}\n\n', ''),
            'Telegram/SourceFiles/app/run.cpp': '#include "low/low.h"\n\nnamespace App {\n\nint Run(\n\t\tint value) {\n\treturn Low::Twice(value);\n}\n\n} // namespace App\n',
        })
        result = review.moves(repo.root, 'HEAD')
        self.assertTrue(result['move_only'], result)

    def test_changed_initializer_value_is_not_move_only(self):
        body = 'auto Make() {\n\treturn Options{\n\t\t.limit = 100,\n\t\t.name = u"a"_q,\n\t};\n}\n'
        repo = Repo(dict(fixture(), **{'Telegram/SourceFiles/low/make.cpp': body}))
        self.addCleanup(repo.temp.cleanup)
        repo.remove('Telegram/SourceFiles/low/make.cpp')
        repo.write({'Telegram/SourceFiles/app/make.cpp': body.replace('100', '5000')})
        result = review.moves(repo.root, 'HEAD')
        self.assertFalse(result['move_only'], result)

    def test_reordered_statements_are_not_move_only(self):
        body = 'void Run() {\n\tfirst();\n\tsecond();\n\tthird();\n}\n'
        repo = Repo(dict(fixture(), **{'Telegram/SourceFiles/low/run.cpp': body}))
        self.addCleanup(repo.temp.cleanup)
        repo.remove('Telegram/SourceFiles/low/run.cpp')
        repo.write({'Telegram/SourceFiles/app/run.cpp': body.replace(
            '\tfirst();\n\tsecond();', '\tsecond();\n\tfirst();')})
        result = review.moves(repo.root, 'HEAD')
        self.assertFalse(result['move_only'], result)

    def test_reordered_functions_are_move_only(self):
        one = 'void One() {\n\tfirst();\n}\n\n'
        two = 'void Two() {\n\tsecond();\n}\n'
        repo = Repo(dict(fixture(), **{'Telegram/SourceFiles/low/run.cpp': one + two}))
        self.addCleanup(repo.temp.cleanup)
        repo.remove('Telegram/SourceFiles/low/run.cpp')
        repo.write({'Telegram/SourceFiles/app/run.cpp': two + '\n' + one})
        result = review.moves(repo.root, 'HEAD')
        self.assertTrue(result['move_only'], result)

    def test_changed_logic_is_not_move_only(self):
        repo = Repo(fixture())
        self.addCleanup(repo.temp.cleanup)
        repo.write({'Telegram/SourceFiles/low/low.cpp': LOW.replace('value * 2', 'value * 3')})
        result = review.moves(repo.root, 'HEAD')
        self.assertFalse(result['move_only'])
        self.assertEqual(result['lost'], [('return value * 2;', 1)])


if __name__ == '__main__':
    unittest.main()
