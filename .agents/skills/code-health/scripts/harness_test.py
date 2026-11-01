import datetime
import os
from pathlib import Path
import subprocess
import unittest

import harness
import report
import sources
from health_test import Repo


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.source = Repo({'Telegram/CMakeLists.txt': ''})
        self.queue = Repo({'tasks/.keep': ''})
        self.addCleanup(self.source.temp.cleanup)
        self.addCleanup(self.queue.temp.cleanup)
        self.today = datetime.date(2026, 10, 22)
        self.task('unrelated', code='Unrelated();')

    def commit(self, repo, subject, when, task=None):
        env = dict(os.environ, GIT_AUTHOR_DATE=when + 'T12:00:00Z',
                   GIT_COMMITTER_DATE=when + 'T12:00:00Z')
        repo.git('add', '-A')
        message = subject + ('\n\nTask: ' + task if task else '')
        subprocess.run(['git', '-C', str(repo.root), 'commit', '-q', '--allow-empty', '-m', message],
                       env=env, check=True, capture_output=True)

    def helper(self, name='test_rare', when='2026-07-24', task=None, header='void Rare();\n'):
        self.source.write({harness.ROOT + name + '.h': header,
                           harness.ROOT + name + '.cpp': '#include "test/' + name + '.h"\n'})
        self.commit(self.source, '[ai] Add helper', when, task)

    def task(self, slug, when='2026-10-21', code='Test::Rare();', kind='product', status='approved'):
        task = '2026/07/24/' + slug
        self.queue.write({
            'tasks/' + task + '/state.yaml': 'status: ' + status + '\n',
            'tasks/' + task + '/task.md': '# ' + slug + '\n\nUse test_rare.\n',
            'tasks/' + task + '/work/test-overlay.patch':
                'diff --git a/Telegram/SourceFiles/test/test_scenario.cpp b/Telegram/SourceFiles/test/test_scenario.cpp\n'
                '--- a/Telegram/SourceFiles/test/test_scenario.cpp\n'
                '+++ b/Telegram/SourceFiles/test/test_scenario.cpp\n@@ -1 +1 @@\n-old();\n+' + code + '\n',
        })
        self.commit(self.queue, ('Approve ' if status == 'approved' else 'Start ') + task, when)
        if kind != 'unknown':
            self.source.write({'Telegram/SourceFiles/app/' + slug + '.cpp': 'void Product() {}\n'})
            self.commit(self.source, ('[ai] ' if kind == 'harness' else '') + 'Implement ' + slug, when, task)
        return task

    def inspect(self, queue=True):
        return harness.inspect(sources.from_ref(self.source.root, 'HEAD'),
                               self.queue.root if queue else None, self.today)

    def row(self, name='test_rare', **kwargs):
        return next(row for row in self.inspect(**kwargs)['modules'] if row['module'] == name)

    def test_ninety_day_boundary_and_low_lifetime_use(self):
        self.helper()
        self.assertEqual(self.row()['age_days'], 90)
        self.assertEqual(len(self.row()['review_reasons']), 2)
        self.task('one')
        self.assertEqual(len(self.row()['review_reasons']), 1)
        self.task('two')
        self.assertEqual(len(self.row()['review_reasons']), 1)
        self.task('three')
        self.assertEqual(self.row()['review_reasons'], [])
        data = harness.inspect(sources.from_ref(self.source.root, 'HEAD'), self.queue.root,
                               self.today + datetime.timedelta(days=89))
        row = data['modules'][0]
        self.assertEqual(row['recent_product_tasks'], 0)
        self.assertEqual(len(row['review_reasons']), 1)

    def test_young_helper_and_missing_archive_are_not_zero_use_proof(self):
        self.helper(when='2026-07-25')
        self.assertEqual(self.row()['age_days'], 89)
        self.assertEqual(self.row()['review_reasons'], [])
        row = self.row(queue=False)
        self.assertIsNone(row['product_tasks'])
        self.assertEqual(row['review_reasons'], [])

    def test_no_saved_patches_is_unavailable_not_zero_use(self):
        self.helper()
        self.queue.remove('tasks/2026/07/24/unrelated/work/test-overlay.patch')
        self.commit(self.queue, 'Remove unavailable archive', '2026-10-21')
        self.assertFalse(self.inspect()['coverage']['available'])
        self.assertIsNone(self.row()['product_tasks'])
        self.assertEqual(self.row()['review_reasons'], [])

    def test_creation_harness_and_self_test_calls_are_separate(self):
        creator = '2026/07/24/create'
        self.helper(task=creator, header='void Rare();\nvoid AppendRareSelfTest();\n')
        self.task('create')
        self.task('sweep', kind='harness')
        self.task('self-test', code='#include "test/test_rare.h"\n+AppendRareSelfTest(runner);')
        self.assertEqual(self.row()['product_tasks'], {})
        self.assertEqual(len(self.row()['harness_tasks']), 1)
        self.assertEqual(len(self.row()['self_test_tasks']), 1)

    def test_final_patch_context_counts_but_deletions_and_helper_edits_do_not(self):
        self.helper()
        self.task('only-deleted', code='Other();\n-Test::Rare();')
        self.assertEqual(self.row()['product_tasks'], {})
        self.task('context', code='Other();\n Test::Rare();')
        self.assertEqual(len(self.row()['product_tasks']), 1)
        patch = ('diff --git a/x b/x\n+++ b/' + harness.ROOT
                 + 'test_rare.cpp\n@@ -1 +1 @@\n+Test::Rare();\n')
        self.assertEqual(harness.patch_code(patch), '')

    def test_distinct_tasks_not_duplicate_patches_or_calls(self):
        self.helper()
        task = self.task('repeat', code='Test::Rare(); Test::Rare();')
        directory = self.queue.root / 'tasks' / task / 'work'
        (directory / 'base-overlay.patch').write_text((directory / 'test-overlay.patch').read_text())
        self.commit(self.queue, 'Approve ' + task, '2026-10-21')
        self.assertEqual(len(self.row()['product_tasks']), 1)

    def test_comments_strings_and_commented_includes_are_not_consumers(self):
        self.helper()
        self.task('mentions-only', code='// Test::Rare();\n+/*\n+#include "test/test_rare.h"\n+*/\n+'
                  'auto text = R"tag(Test::Rare())tag";\n+auto other = "Test::Rare()";')
        self.assertEqual(self.row()['product_tasks'], {})
        self.task('real-include', code='#include "test/test_rare.h"')
        self.assertEqual(len(self.row()['product_tasks']), 1)

    def test_self_tests_internal_consumers_and_open_work(self):
        self.helper('test_check', header='class Runner;\nvoid AppendCheckSelfTest(Runner*);\n')
        self.helper('test_internal', header='namespace Test::Internal {\nvoid ReadInternal();\n}\n')
        self.helper(header='#include "test/test_internal.h"\nvoid Rare();\n')
        self.task('consumer')
        active = self.task('active', status='in-progress')
        self.assertEqual(self.row('test_check')['kind'], 'self-test')
        self.assertEqual(self.row('test_check')['review_reasons'], [])
        internal = self.row('test_internal')
        self.assertEqual(internal['kind'], 'internal')
        self.assertIn('test_rare', internal['required_by'])
        self.assertEqual(len(internal['product_tasks']), 1)
        self.assertIn(active, internal['open_tasks'])
        self.assertEqual(self.row()['routing'], 'held: open consumer')

    def test_unclassified_consumers_do_not_become_known_zero(self):
        self.helper()
        for i in range(3):
            self.task('unknown-' + str(i), kind='unknown')
        row = self.row()
        self.assertEqual(row['product_tasks'], {})
        self.assertEqual(len(row['uncertain_tasks']), 3)
        self.assertEqual(row['review_reasons'], [])

    def test_docs_are_read_from_the_scanned_ref(self):
        self.helper()
        self.source.write({harness.README: 'old' * 3000})
        self.commit(self.source, '[ai] Document', '2026-10-21')
        self.source.write({harness.README: 'local short guide'})
        self.assertEqual(self.inspect()['readme_bytes'], 9000)
        data = harness.inspect(sources.from_worktree(self.source.root), None, self.today)
        self.assertEqual(data['readme_bytes'], len('local short guide'))

    def test_dirty_archive_cannot_supply_retirement_evidence(self):
        self.helper()
        task = self.task('consumer')
        (self.queue.root / 'tasks' / task / 'work/test-overlay.patch').write_text('local changes')
        data = self.inspect()
        self.assertFalse(data['coverage']['available'])
        self.assertTrue(data['coverage']['dirty_queue'])
        self.assertIsNone(data['modules'][0]['product_tasks'])
        self.assertEqual(data['modules'][0]['review_reasons'], [])

    def test_other_namespaces_are_not_harness_dependencies(self):
        self.helper(header='namespace Ui { class InputField; }\nnamespace Test { void Rare(); }\n')
        self.source.write({'Telegram/SourceFiles/app/other.cpp': 'Ui::Rare(); InputField field;\n',
                           'Telegram/SourceFiles/app/actual.cpp': 'Test::Rare();\n'})
        self.commit(self.source, 'Add callers', '2026-10-21')
        self.assertNotIn('InputField', harness.identifiers(
            self.source.root.joinpath(harness.ROOT, 'test_rare.h').read_text()))
        self.assertEqual(self.row()['source_callers'], ['Telegram/SourceFiles/app/actual.cpp'])

    def test_stable_identity_and_unavailable_evidence_do_not_resolve_findings(self):
        self.helper()
        row = self.row()
        finding = {'id': row['id'], 'kind': 'harness-retention', 'status': 'open',
                   'first_seen': '2026-10-22T00:00:00+00:00'}
        previous = {'findings': {row['id']: finding}}
        pending = report.merge(previous, {}, '2026-10-23T00:00:00+00:00', ('harness-retention',))
        self.assertEqual(pending[row['id']]['status'], 'unverified')
        again = report.merge(previous, {row['id']: dict(finding)}, '2026-10-24T00:00:00+00:00')
        self.assertEqual(again[row['id']]['first_seen'], finding['first_seen'])
        resolved = report.merge(previous, {}, '2026-10-25T00:00:00+00:00')
        kept = report.merge({'findings': resolved}, {}, '2026-10-26T00:00:00+00:00', ('harness-retention',))
        self.assertEqual(kept[row['id']]['status'], 'resolved')


if __name__ == '__main__':
    unittest.main()
