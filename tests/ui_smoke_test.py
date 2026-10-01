#!/usr/bin/env python3
# Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Observable host safety failures; no emulator or product state is touched."""

import json
import hashlib
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ui_smoke import artifacts, commands, transaction, ui
from ui_smoke import device_audit, device_cleanup, aurum_tree


def node(text='', visible=True, children=None):
    return {'text': text, 'widgetType': 'TextLabel', 'isVisible': visible,
            'isShowing': visible, 'isEnabled': True,
            'geometry': {'x': 0, 'y': 0, 'width': 10, 'height': 10},
            'children': children or []}


class HostSafetyTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.output = Path(self.directory.name)
        commands.set_output(self.output)
        transaction.configure(Mock(), self.output)

    def tearDown(self):
        commands.set_output(None)
        self.directory.cleanup()

    def test_page_wait_observes_delayed_exact_advance_without_new_input(self):
        actor = object.__new__(ui.Aurum)
        actor.deadline = 100
        actor.observe = Mock()
        actor.page = Mock(side_effect=[(1, 4), (2, 4)])
        actor.click = Mock()
        with patch.object(ui.time, 'monotonic', return_value=0), \
                patch.object(ui.time, 'sleep'):
            actor.wait_page(2, 4)
        self.assertEqual(actor.observe.call_count, 2)
        actor.click.assert_not_called()

    def test_page_wait_rejects_wrong_count_backwards_skipped_and_foreign(self):
        actor = object.__new__(ui.Aurum)
        actor.deadline = 100
        actor.observe = Mock()
        actor.click = Mock()
        for value in ((1, 5), (0, 4), (3, 4)):
            actor.page = Mock(return_value=value)
            with patch.object(ui.time, 'monotonic', return_value=0), \
                    self.assertRaisesRegex(RuntimeError, 'count/order'):
                actor.wait_page(2, 4)
        actor.observe.side_effect = commands.Unavailable('foreign window')
        with patch.object(ui.time, 'monotonic', return_value=0), \
                self.assertRaisesRegex(commands.Unavailable, 'foreign'):
            actor.wait_page(2, 4)
        actor.click.assert_not_called()

    def test_page_wait_deadline_does_not_redeliver_input(self):
        actor = object.__new__(ui.Aurum)
        actor.deadline = 100
        actor.observe = Mock()
        actor.page = Mock(return_value=(1, 4))
        actor.click = Mock()
        with patch.object(ui.time, 'monotonic',
                          side_effect=[0, 0, 0, 16]), \
                patch.object(ui.time, 'sleep'), \
                self.assertRaisesRegex(RuntimeError, 'transition timeout'):
            actor.wait_page(2, 4)
        actor.observe.assert_called_once_with(budget=8)
        actor.click.assert_not_called()

    def test_deny_diagnostic_errors_never_replace_original_failure(self):
        actor = object.__new__(ui.Aurum)
        actor.transaction = Mock(output=self.output)
        actor.transaction.audit.side_effect = PermissionError('own PID unknown')
        actor.sequence = 1
        actor.click = Mock()
        actor.observe = Mock(side_effect=RuntimeError('frame unavailable'))
        original = RuntimeError('matching DENIED unavailable')
        journal = Mock()
        journal.denied.side_effect = original
        with self.assertRaises(RuntimeError) as raised:
            actor.deny_and_verify(journal, 1, 20)
        self.assertIs(raised.exception, original)
        actor.click.assert_called_once_with('거절')
        actor.observe.assert_called_once()
        journal.denied.assert_called_once_with(1, 20)
        actor.transaction.remote.assert_not_called()
        evidence = json.loads((self.output / 'deny-diagnostics-001.json')
                              .read_text())
        self.assertIn('matching DENIED', evidence['original_error'])
        self.assertEqual(len(evidence['errors']), 2)

    def test_deny_failure_collects_only_verified_own_pids_once(self):
        actor = object.__new__(ui.Aurum)
        actor.transaction = Mock(output=self.output)
        actor.transaction.audit.return_value = [42]
        actor.transaction.remote.side_effect = RuntimeError('dlog unavailable')
        actor.sequence = 2
        actor.click = Mock()
        actor.observe = Mock()
        journal = Mock()
        journal.denied.side_effect = RuntimeError('no terminal DENIED')
        with self.assertRaisesRegex(RuntimeError, 'no terminal DENIED'):
            actor.deny_and_verify(journal, 1, 20)
        args = actor.transaction.remote.call_args.args[0]
        self.assertEqual(args, ['dlogutil', '--pid', '42', '-t', '200',
                                '--color', 'never', '-v', 'time'])
        actor.click.assert_called_once_with('거절')
        evidence = json.loads((self.output / 'deny-diagnostics-002.json')
                              .read_text())
        self.assertEqual(evidence['pids'], [42])
        self.assertIn('dlog unavailable', evidence['errors'][0])

    def test_functional_reset_prompts_are_distinct_before_positive_effect(self):
        actor, journal, owner = Mock(), Mock(), Mock()
        owner.output = self.output
        journal.effect.return_value = {}
        journal.mark.side_effect = [10, 20, 30, 40]
        events = []
        actor.settings_task.side_effect = lambda task: events.append(
            ('task', task))
        actor.deny_review_reset.side_effect = lambda value, j, count, mark: (
            events.append(('reset', value)), j.denied(count, mark))
        actor.approve.side_effect = lambda *args, **kwargs: events.append(
            ('approve', args, kwargs))
        journal.denied.side_effect = lambda count, mark: events.append(
            ('denied', count, mark))
        actor.deny_and_verify.side_effect = lambda j, count, mark: (
            j.denied(count, mark))
        journal.effect.side_effect = lambda count, worker: events.append(
            ('effect', count, worker)) or {}
        with patch.object(ui, 'Aurum', return_value=actor), \
                patch.object(ui, 'Journal', return_value=journal):
            ui.scenario(owner, 'cli', 1234, 'functional')
        first = events.index(('reset', True))
        self.assertEqual(events[first - 1:first + 7], [
            ('task', 'expanded'), ('reset', True), ('denied', 1, 20),
            ('task', 'expanded'), ('reset', False), ('denied', 1, 30),
            ('task', 'expanded'), ('approve', (True,), {})])
        self.assertEqual(events[first + 7], ('effect', 1, 'ce'))
        actor.close.assert_called_once()

    def test_reset_directions_review_once_then_deny_each_fresh_prompt(self):
        for selected in (True, False):
            actor = object.__new__(ui.Aurum)
            actor.wait = Mock()
            actor.control = Mock(return_value={'isEnabled': True})
            sequence = []
            actor.choose_persistent = Mock(side_effect=lambda value:
                sequence.append(('choice', value)) or
                ('항상 허용' if value else '표시한 기간 허용'))
            actor.review = Mock(side_effect=lambda primary:
                sequence.append(('review', primary)))
            actor.deny_and_verify = Mock(side_effect=lambda j, count, mark:
                sequence.append(('deny', count, mark)))
            actor.deny_review_reset(selected, Mock(), 1, 20)
            expected = ([] if selected else [('choice', True)])
            expected += [('review', '표시한 기간 허용' if selected
                          else '항상 허용'), ('choice', selected),
                         ('deny', 1, 20)]
            self.assertEqual(sequence, expected)
            actor.review.assert_called_once()

    def test_approval_requires_actual_maximum_retention_disclosure(self):
        actor = object.__new__(ui.Aurum)
        actor.wait = Mock()
        actor.texts = Mock(return_value=['표시한 기간 허용'])
        actor.control = Mock(return_value={'isEnabled': True})
        actor.click = Mock()
        disclosure = ('범위:향후30일목적:대화요약수신자:이기기의현재대화'
                      '동작:달력읽기접근승인기간:이번한번'
                      '취득데이터보관:최대30분')
        actor.review = Mock(return_value=disclosure)
        actor.approve()
        actor.click.assert_called_once_with('표시한 기간 허용')
        actor.click.reset_mock()
        actor.review.return_value = disclosure.replace('최대30분', '30분')
        with self.assertRaisesRegex(RuntimeError, 'protected tuple'):
            actor.approve()
        actor.click.assert_not_called()

    def test_schema_rejects_drift_and_boolean_coordinates(self):
        for value in ({'action': 'click', 'x': 1, 'y': 1},
                      {'method': 'gate'},
                      {'method': 'gate', 'allowed': 1},
                      {'method': 'click', 'x': True, 'y': 1},
                      {'method': 'click', 'x': 1920, 'y': 1}):
            with self.assertRaises(RuntimeError):
                ui.validate_action(value, 1920, 1080)
        self.assertEqual(list(self.output.iterdir()), [])

    def test_device_classification_rejects_physical_x86_target(self):
        with patch.object(commands, 'run', return_value=
                          b'List of devices\nphysical device phone\n'), \
                patch.object(commands, 'emulator_inventory', return_value={}), \
                patch.object(commands, 'remote') as remote:
            with self.assertRaises(commands.Unavailable):
                commands.discover('physical')
            remote.assert_not_called()

    def test_real_sdb_display_name_is_not_classification(self):
        row = b'emulator-26101 device calendar-resolution-p5\n'
        evidence = {'emulator-26101': {'classification': 'live-sdk'}}
        with patch.object(commands, 'run', return_value=row), \
                patch.object(commands, 'emulator_inventory',
                             return_value=evidence), \
                patch.object(commands, 'remote', return_value='x86_64'):
            self.assertEqual(commands.discover('emulator-26101'), 'x86_64')
        record = json.loads((self.output / 'selected-device.json').read_text())
        self.assertEqual(record['evidence'], evidence['emulator-26101'])

    def test_invisible_tree_cannot_hide_unbounded_nodes(self):
        tree = {'status': 0, 'roots': [node(children=
            [node(visible=False) for _ in range(4096)])]}
        with self.assertRaises(RuntimeError):
            ui.nodes(tree, 1920, 1080)
        bad = node('x')
        bad['geometry']['x'] = float('nan')
        with self.assertRaises(RuntimeError):
            ui.nodes({'status': 0, 'roots': [bad]}, 1920, 1080)
        hidden = node(visible=False, children=[node('fake visible')])
        with self.assertRaises(commands.Unavailable):
            ui.nodes({'status': 0, 'roots': [hidden]}, 1920, 1080)

    def test_timeout_logs_partial_output_and_reaps_child(self):
        program = 'import os,time;print(os.getpid(),flush=True);time.sleep(10)'
        with self.assertRaises(TimeoutError):
            commands.run([sys.executable, '-c', program], timeout=.1)
        evidence = next(self.output.glob('command-*.json'))
        record = json.loads(evidence.read_text())
        self.assertEqual(record['category'], 'timeout')
        self.assertLess(record['exit'], 0)
        pid = int(record['stdout'].strip())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    def test_overflow_retains_failure_and_reaps(self):
        program = 'import os;print(os.getpid(),flush=True);print("x"*200000)'
        with self.assertRaisesRegex(RuntimeError, 'exceeded bound'):
            commands.run([sys.executable, '-c', program], limit=1024)
        evidence = next(self.output.glob('command-*.json'))
        record = json.loads(evidence.read_text())
        self.assertEqual(record['category'], 'output-limit')
        pid = int(record['stdout'].splitlines()[0])
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    def native_element(self, identity='root', package=aurum_tree.PACKAGE,
                       active=True, children=()):
        return SimpleNamespace(elementId=identity, package=package,
            isShowing=True, isVisible=True, isActive=active, isEnabled=True,
            text='actual', widgetType='Button', role='button', automationId='',
            geometry=SimpleNamespace(x=0, y=0, width=10, height=10),
            child=children)

    def native_reply(self, elements=(), roots=(), status=0):
        return SimpleNamespace(status=status, elements=elements, roots=roots,
                               ByteSize=lambda: 100)

    def native_client(self, candidates, dumps=None, windows=None):
        client = Mock()
        responses = [self.native_reply(elements=windows or candidates),
                     self.native_reply(elements=candidates)]

        def flat(element):
            result = [element]
            for child in element.child:
                result.extend(flat(child))
            return result

        responses.extend(self.native_reply(elements=flat(e))
                         for e in candidates)
        client.findElements.side_effect = responses
        client.dumpObjectTree.side_effect = dumps or [
            self.native_reply(roots=[e]) for e in candidates]
        pb = SimpleNamespace(ReqFindElements=SimpleNamespace,
                             ReqDumpObjectTree=SimpleNamespace, OK=0, ERROR=2)
        return client, pb

    def window_metadata(self, package, x=0, y=0, width=100, height=100,
                        active=True):
        return {'elementId': package, 'package': package, 'isVisible': True,
                'isShowing': True, 'isActive': active,
                'geometry': {'x': x, 'y': y, 'width': width, 'height': height}}

    def test_invalid_chrome_proof_prevents_collection(self):
        owner = Mock(output=self.output)
        owner.audit.return_value = {'package': 'foreign'}
        controller = ui.Aurum(owner, 'cli', 12345, Path('/existing/cache'))
        with patch.object(commands, 'run') as run:
            with self.assertRaisesRegex(commands.Unavailable, 'proof package'):
                controller.observe()
        run.assert_not_called()

    def test_changed_chrome_incarnation_prevents_collection(self):
        owner = Mock(output=self.output)
        owner.audit.return_value = {'package': aurum_tree.CHROME, 'pid': 2}
        controller = ui.Aurum(owner, 'cli', 12345, Path('/existing/cache'))
        controller.chrome_proof = {'package': aurum_tree.CHROME, 'pid': 1}
        with patch.object(commands, 'run') as run:
            with self.assertRaisesRegex(commands.Unavailable, 'incarnation'):
                controller.observe()
        run.assert_not_called()

    def test_verified_chrome_observation_keeps_full_metadata(self):
        own = self.native_element()
        chrome = self.native_element('bar', aurum_tree.CHROME)
        client, pb = self.native_client([own], windows=[own, chrome])
        result = aurum_tree.collect(client, pb, chrome_verified=True)
        self.assertEqual(result['status'], 0)
        self.assertEqual(result['windows'][1]['package'], aurum_tree.CHROME)
        self.assertNotIn('text', result['windows'][1])

    def test_center_outside_verified_chrome_can_be_used(self):
        control = node()
        control['geometry'] = {'x': 40, 'y': 84, 'width': 20, 'height': 12}
        windows = [self.window_metadata(aurum_tree.PACKAGE),
                   self.window_metadata(aurum_tree.CHROME, y=95, height=5)]
        self.assertEqual(ui.input_point(control, windows, 100, 100), (50, 90))

    def test_center_inside_chrome_blocks_input_without_search(self):
        windows = [self.window_metadata(aurum_tree.PACKAGE),
                   self.window_metadata(aurum_tree.CHROME)]
        with self.assertRaisesRegex(commands.Unavailable, 'covers input'):
            ui.input_point(node(), windows, 100, 100)

    def test_unknown_foreign_window_remains_blocked_outside_center(self):
        windows = [self.window_metadata(aurum_tree.PACKAGE),
                   self.window_metadata('unknown.overlay', y=95, height=5)]
        with self.assertRaisesRegex(commands.Unavailable, 'unknown foreign'):
            ui.input_point(node(), windows, 100, 100)

    def test_invalid_foreign_geometry_blocks_input(self):
        for width in (-1, float('nan'), 100000):
            windows = [self.window_metadata(aurum_tree.PACKAGE),
                       self.window_metadata(aurum_tree.CHROME, width=width)]
            with self.assertRaises(commands.Unavailable):
                ui.input_point(node(), windows, 100, 100)

    def test_missing_own_active_coverage_blocks_input(self):
        windows = [self.window_metadata(aurum_tree.PACKAGE, active=False)]
        with self.assertRaisesRegex(commands.Unavailable, 'own active'):
            ui.input_point(node(), windows, 100, 100)

    def test_native_registered_roots_eliminate_proven_overlap(self):
        child = self.native_element('child')
        root = self.native_element(children=[child])
        client, pb = self.native_client([root, child])
        result = aurum_tree.collect(client, pb)
        self.assertEqual(result['root_count'], 1)
        self.assertEqual(result['eliminated'], ['child'])
        self.assertEqual(result['roots'][0]['package'], aurum_tree.PACKAGE)
        self.assertEqual(set(result['windows'][0]), {
            'elementId', 'package', 'isShowing', 'isVisible', 'isActive',
            'geometry'})
        self.assertEqual(client.findElements.call_args_list[0].args[0]
                         .maxDepth, 1)

    def test_foreign_active_metadata_blocks_without_foreign_tree(self):
        own = self.native_element()
        foreign = self.native_element('overlay', 'foreign.package')
        client, pb = self.native_client([own], windows=[own, foreign])
        result = aurum_tree.collect(client, pb)
        self.assertEqual(result['status'], 3)
        client.dumpObjectTree.assert_not_called()
        self.assertNotIn('text', result['windows'][1])

    def test_native_tree_foreign_descendant_rejected(self):
        root = self.native_element(children=[
            self.native_element('foreign', 'foreign.package')])
        client, pb = self.native_client([root])
        with self.assertRaisesRegex(RuntimeError, 'foreign.*package'):
            aurum_tree.collect(client, pb)

    def test_omitted_dump_package_requires_exact_full_refresh_pair(self):
        raw_child = self.native_element('child', '')
        raw_root = self.native_element(package='', children=[raw_child])
        fresh_child = self.native_element('child')
        fresh_root = self.native_element(children=[fresh_child])
        client, pb = self.native_client([fresh_root],
            dumps=[self.native_reply(roots=[raw_root])])
        result = aurum_tree.collect(client, pb)
        root = result['roots'][0]
        self.assertEqual(root['package'], aurum_tree.PACKAGE)
        self.assertEqual(root['raw_dump']['package'], '')
        self.assertTrue(root['raw_dump']['isEnabled'])
        self.assertEqual(root['package_source'], 'full-refresh-exact-ID')
        self.assertEqual(root['children'][0]['raw_dump']['package'], '')
        request = client.findElements.call_args_list[-1].args[0]
        self.assertEqual(request.elementId, 'root')
        self.assertEqual(request.packageName, aurum_tree.PACKAGE)
        self.assertFalse(hasattr(request, 'isShowing'))

    def test_missing_descendant_cannot_inherit_parent_package(self):
        raw = self.native_element(children=[self.native_element('missing', '')])
        client, pb = self.native_client([self.native_element()],
            dumps=[self.native_reply(roots=[raw])])
        diagnostics = {}
        with self.assertRaisesRegex(RuntimeError, 'paired native ID missing'):
            aurum_tree.collect(client, pb, diagnostics=diagnostics)
        self.assertEqual(diagnostics['current_raw']['elementId'], 'missing')
        self.assertEqual(diagnostics['current_raw']['package'], '')
        self.assertNotIn('text', diagnostics['current_raw'])

    def test_duplicate_full_refresh_ids_are_rejected(self):
        root = self.native_element()
        client, pb = self.native_client([root])
        client.findElements.side_effect = [self.native_reply(elements=[root]),
            self.native_reply(elements=[root]),
            self.native_reply(elements=[root, root])]
        with self.assertRaisesRegex(RuntimeError, 'duplicate full-refresh'):
            aurum_tree.collect(client, pb)

    def test_changed_full_refresh_root_cannot_cover_old_root(self):
        root = self.native_element()
        client, pb = self.native_client([root])
        client.findElements.side_effect = [self.native_reply(elements=[root]),
            self.native_reply(elements=[root]),
            self.native_reply(elements=[self.native_element('changed')])]
        with self.assertRaisesRegex(RuntimeError, 'root identity changed'):
            aurum_tree.collect(client, pb)

    def test_conflicting_nonempty_dump_package_is_rejected(self):
        client, pb = self.native_client([self.native_element()],
            dumps=[self.native_reply(roots=[self.native_element(
                package='foreign.package')])])
        with self.assertRaisesRegex(RuntimeError, 'foreign package in own'):
            aurum_tree.collect(client, pb)

    def test_native_registered_stale_id_rejected(self):
        client, pb = self.native_client([self.native_element()],
                                       dumps=[self.native_reply()])
        with self.assertRaisesRegex(RuntimeError, 'became stale'):
            aurum_tree.collect(client, pb)

    def test_native_empty_candidates_are_explicit(self):
        client, pb = self.native_client([], windows=[self.native_element()])
        client.findElements.side_effect = [
            self.native_reply(elements=[self.native_element()]),
            self.native_reply(status=2)]
        result = aurum_tree.collect(client, pb)
        self.assertEqual(result['calls'][-1]['status'], 2)
        self.assertEqual(result['root_count'], 0)
        client.dumpObjectTree.assert_not_called()

    def test_native_error_with_nonempty_elements_is_failure(self):
        client, pb = self.native_client([self.native_element()])
        client.findElements.side_effect = [self.native_reply(
            elements=[self.native_element()], status=2)]
        with self.assertRaisesRegex(RuntimeError, 'status failure'):
            aurum_tree.collect(client, pb)

    def test_native_global_no_match_is_unavailable_metadata(self):
        client, pb = self.native_client([])
        client.findElements.side_effect = [self.native_reply(status=2)]
        result = aurum_tree.collect(client, pb)
        self.assertEqual(result['status'], 3)
        self.assertEqual(result['windows'], [])
        self.assertEqual(result['calls'][0]['count'], 0)
        self.assertEqual(result['calls'][0]['status'], 2)
        client.dumpObjectTree.assert_not_called()

    def test_native_aggregate_deadline_bounds_each_rpc(self):
        client, pb = self.native_client([self.native_element()])
        ticks = iter([0, 0, 1, 2, 9])
        with self.assertRaisesRegex(RuntimeError, 'aggregate'):
            aurum_tree.collect(client, pb, clock=lambda: next(ticks))
        for call in client.findElements.call_args_list:
            self.assertLessEqual(call.kwargs['timeout'], 3)
        client.dumpObjectTree.assert_not_called()

    def test_empty_startup_tree_waits_for_actual_semantics(self):
        controller = ui.Aurum(Mock(), 'cli', 12345)
        controller.deadline = 100
        controller.observe = Mock(side_effect=[
            commands.Unavailable('transient empty native roots'), []])
        controller.texts = Mock(return_value=['선택 기능 설정'])
        with patch.object(ui.time, 'monotonic', side_effect=[0, 1, 1, 2, 2]), \
                patch.object(ui.time, 'sleep'):
            controller.wait('선택 기능 설정')
        self.assertEqual(controller.observe.call_count, 2)

    def test_persistent_empty_tree_has_finite_unavailable_deadline(self):
        controller = ui.Aurum(Mock(), 'cli', 12345)
        controller.deadline = 100
        controller.observe = Mock(side_effect=commands.Unavailable(
            'transient empty native roots'))
        with patch.object(ui.time, 'monotonic', side_effect=[0, 1, 1, 16]), \
                patch.object(ui.time, 'sleep'):
            with self.assertRaisesRegex(commands.Unavailable,
                                        'native semantic state unavailable'):
                controller.wait('선택 기능 설정')
        self.assertEqual(controller.observe.call_count, 1)

    def test_actual_aurum_lifecycle_diagnostics(self):
        controller = ui.Aurum(Mock(), 'cli', 12345)
        samples = {
            'session-start': '... successfully launched pid = 501158'
                             ' with debug 0\r\n',
            'session-stop': '\t Terminate appId: org.tizen.aurum-bootstrap\r\n',
        }
        for method, prefix in samples.items():
            with patch.object(commands, 'run',
                              return_value=(prefix + '{"status":"ok"}\n')
                              .encode()):
                self.assertEqual(controller.command([method]),
                                 {'status': 'ok'})
        for method, reply in [
                ('health', samples['session-start'] + '{}'),
                ('session-start', 'unknown diagnostic\n{}'),
                ('session-stop', 'Terminate appId: foreign.app\n{}'),
                ('session-start', samples['session-start'] + '{}{}'),
                ('session-stop', samples['session-stop'] + '{}trailing')]:
            with patch.object(commands, 'run', return_value=reply.encode()):
                with self.assertRaises((RuntimeError, ValueError)):
                    controller.command([method])

    def test_expired_ui_still_attempts_independent_teardown(self):
        owner = Mock()
        controller = ui.Aurum(owner, 'external-cli', 12345)
        controller.started = True
        controller.deadline = 0
        controller.pids = Mock(return_value=[])
        with patch.object(commands, 'run', side_effect=[b'{}', b'']):
            controller.close()

    def test_failed_ui_stop_forbids_restore_and_purge(self):
        owner = transaction.Transaction()
        owner.install_attempted = True
        owner.setup_succeeded = True
        owner.audit = Mock(return_value=[123])
        owner.remote = Mock(side_effect=RuntimeError('close failed'))
        owner.script = Mock()
        owner.package = Mock()
        with self.assertRaisesRegex(RuntimeError, 'teardown'):
            owner.finish()
        owner.package.assert_not_called()
        self.assertNotIn(('purge',),
                         [call.args for call in owner.script.call_args_list])

    def test_unacquired_preflight_failure_does_not_take_over(self):
        owner = transaction.Transaction()
        owner.script = Mock()
        owner.remote = Mock()
        owner.finish()
        owner.script.assert_not_called()
        owner.remote.assert_not_called()

    def test_journal_machine_output_disables_colors_and_stays_strict(self):
        owner = Mock()
        envelope = {'_PID': '42', 'MESSAGE': json.dumps(
            {'event': 'feature-ready'})}
        owner.remote.side_effect = ['-- cursor: cursor', '42',
                                    json.dumps(envelope)]
        journal = ui.Journal(owner)
        self.assertEqual(journal.rows()[0]['_source_pid'], 42)
        for index in (0, 2):
            args = owner.remote.call_args_list[index].args[0]
            self.assertEqual(args[:3],
                             ['env', 'SYSTEMD_COLORS=0', 'journalctl'])
        owner.remote.return_value = '{"\x1b[0;32m_PID": "42"}'
        owner.remote.side_effect = None
        with self.assertRaises(json.JSONDecodeError):
            journal.rows()

    def test_duplicate_receipt_is_not_a_new_protected_effect(self):
        owner = Mock()
        owner.remote.side_effect = ['-- cursor: cursor', '42']
        journal = ui.Journal(owner)
        journal.receipts.add('old')
        event = {'worker': 'ce', 'status': '0', 'action_started': '1',
                 'action_retry': '0', 'retired': '0', 'receipt': 'old',
                 'worker_pid': '43', 'feature': 'calendar.read',
                 'operation_id': 'new'}
        journal.rows = Mock(return_value=[
            {'event': 'action', 'operation': 'new', 'retry': False,
             '_source_pid': 43},
            {'event': 'feature-execution-result', 'evidence': event,
             '_source_pid': 42},
            {'event': 'feature-state', '_source_pid': 42,
             'state': {'decision': 'ALLOWED', 'coordinator_epoch': 'epoch',
                       'action_count': '1', 'grant_mode': 'ONCE'}}])
        with self.assertRaisesRegex(RuntimeError, 'receipt admission'):
            journal.effect(0, 'ce')

    def test_positive_fd_inventory_cleanup_with_nested_owned_files(self):
        root = self.output / 'positive'
        child = root / 'one' / 'two'
        child.mkdir(parents=True)
        leaf = child / 'owned'
        leaf.write_bytes(b'owned')
        leaf.chmod(0o600)
        expected = {'one/two/owned': hashlib.sha256(b'owned').hexdigest()}
        fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            device_cleanup.remove_inventory(fd, expected, os.getuid())
        finally:
            os.close(fd)
        self.assertEqual(list(root.iterdir()), [])

    def test_cleanup_retains_injected_and_replaced_foreign_inode(self):
        for replacement in (False, True):
            with self.subTest(replacement=replacement):
                root = self.output / ('replace' if replacement else 'inject')
                root.mkdir()
                leaf = root / 'owned'
                leaf.write_bytes(b'owned')
                leaf.chmod(0o600)
                expected = {'owned': hashlib.sha256(b'owned').hexdigest()}
                original_list = os.listdir
                calls = 0
                retained = os.open(leaf, os.O_RDONLY)

                def listing(fd):
                    nonlocal calls
                    calls += 1
                    if calls == 2:
                        if replacement:
                            leaf.unlink()
                            leaf.write_bytes(b'foreign')
                            leaf.chmod(0o600)
                        else:
                            (root / 'foreign').write_bytes(b'foreign')
                    return original_list(fd)

                fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    with patch.object(device_cleanup.os, 'listdir', listing):
                        with self.assertRaises(RuntimeError):
                            device_cleanup.remove_inventory(fd, expected,
                                                            os.getuid())
                finally:
                    os.close(fd)
                    os.close(retained)
                foreign = leaf if replacement else root / 'foreign'
                self.assertEqual(foreign.read_bytes(), b'foreign')

    def test_confirmed_loader_permission_error_is_not_gui_stopped(self):
        proc = Mock(name='proc')
        proc.name = '123'
        maps = Mock()
        maps.read_text.side_effect = PermissionError('maps denied')
        proc.__truediv__ = Mock(side_effect=lambda name: maps)
        with patch.object(device_audit.Path, 'iterdir', return_value=[proc]), \
                patch.object(device_audit.os, 'readlink',
                             return_value='/usr/bin/dotnet-hydra-loader'):
            with self.assertRaises(PermissionError):
                device_audit.gui_pids()

    def test_cgroup_descendant_task_and_unknown_hierarchy(self):
        root = self.output / 'cgroup'
        group = root / 'owned'
        child = group / 'child'
        child.mkdir(parents=True)
        (group / 'cgroup.procs').write_text('')
        (child / 'cgroup.procs').write_text('123\n')
        original = Path.read_text
        mount = '1 0 0:1 / ' + str(root) + ' rw - cgroup2 cgroup rw'

        def read(path, *args, **kwargs):
            if str(path) == '/proc/self/mountinfo':
                return mount
            return original(path, *args, **kwargs)

        with patch.object(device_audit.Path, 'read_text', read):
            self.assertFalse(device_audit.cgroup_empty('/owned'))
        with patch.object(device_audit.Path, 'read_text', return_value=''):
            with self.assertRaisesRegex(RuntimeError, 'unknown'):
                device_audit.cgroup_empty('/owned')

    def test_signed_manifest_mismatch_rejected_before_install(self):
        archive = Mock()
        data = ('<manifest xmlns="http://tizen.org/ns/packages" '
                'package="foreign"><ui-application '
                'appid="org.tizen.consentui" exec="ConsentUI.dll" '
                'type="dotnet"/></manifest>').encode()
        with patch.object(artifacts, 'zip_bytes', return_value=data):
            with self.assertRaisesRegex(RuntimeError, 'package identity'):
                artifacts.manifest_identity(archive)

    def test_stage_preflight_rejection_has_no_acquired_cleanup(self):
        owner = transaction.Transaction()
        owner.remote = Mock(return_value='active')
        owner.script = Mock()
        with self.assertRaises(RuntimeError):
            owner.stage()
        self.assertFalse(owner.created_root)
        owner.finish()
        owner.script.assert_not_called()
        self.assertFalse(any('mkdir' in call.args[0]
                             for call in owner.remote.call_args_list))

    def test_uncertain_setup_requires_matching_invocation_proof(self):
        for valid in (False, True):
            with self.subTest(valid=valid):
                owner = transaction.Transaction()
                owner.created_root = True
                owner.setup_dispatched = True
                owner.root_identity = {'dev': 1, 'inode': 2, 'uid': 0,
                                       'gid': 3, 'mode': 0o750, 'smack': '_'}
                proof = {'root': owner.root_identity, 'acquired': True,
                         'invocation': owner.nonce if valid else 'foreign',
                         'created_runtime': {}}
                owner.audit = Mock(return_value=proof)
                owner.capture_generated = Mock()
                owner.script = Mock()
                owner.remote = Mock(return_value='')
                owner.dispatch = Mock(return_value='OWNED_PAYLOAD_CLEANUP0')

                def upload(source, relative):
                    owner.owned_files[relative] = 'a' * 64

                owner.upload = Mock(side_effect=upload)
                if valid:
                    owner.finish()
                    owner.script.assert_any_call('cleanup')
                    owner.script.assert_any_call('purge')
                else:
                    with self.assertRaisesRegex(RuntimeError, 'invocation'):
                        owner.finish()
                    owner.script.assert_not_called()
                    owner.dispatch.assert_not_called()

    def test_root_replacement_prevents_upload(self):
        owner = transaction.Transaction()
        owner.created_root = True
        owner.root_identity = {'dev': 1, 'inode': 2}
        owner.remote = Mock(return_value='[1,3]')
        source = self.output / 'source'
        source.write_bytes(b'owned')
        with patch.object(commands, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'root replaced'):
                owner.upload(source, 'marker')
            run.assert_not_called()

    def test_failed_restore_retains_owned_payload(self):
        owner = transaction.Transaction()
        owner.created_root = True
        owner.install_attempted = True
        owner.audit = Mock(return_value=[])
        owner.package = Mock(side_effect=RuntimeError('restore failed'))
        owner.script = Mock()
        owner.upload = Mock()
        with self.assertRaisesRegex(RuntimeError, 'restore failed'):
            owner.finish()
        owner.upload.assert_not_called()
        owner.script.assert_not_called()

    def test_effect_cannot_adopt_unannounced_epoch_or_old_pid(self):
        for wrong_pid in (False, True):
            owner = Mock()
            owner.remote.side_effect = ['-- cursor: cursor', '42']
            journal = ui.Journal(owner)
            journal.epoch = 'current'
            producer = 41 if wrong_pid else 42
            event = {'worker': 'ce', 'operation_id': 'new'}
            journal.rows = Mock(return_value=[
                {'event': 'action', 'operation': 'new', '_source_pid': 43},
                {'event': 'feature-execution-result', 'evidence': event,
                 '_source_pid': producer},
                {'event': 'feature-state', '_source_pid': producer,
                 'state': {'decision': 'ALLOWED', 'coordinator_epoch': 'other',
                           'action_count': '1', 'grant_mode': 'ONCE'}}])
            with self.assertRaises(RuntimeError):
                journal.effect(0, 'ce', timeout=.01)
            self.assertEqual(journal.epoch, 'current')
            self.assertFalse(journal.operations)

    def test_denial_without_correlated_selection_cannot_pass(self):
        owner = Mock()
        owner.remote.side_effect = ['-- cursor: cursor', '42']
        journal = ui.Journal(owner)
        journal.epoch = 'epoch'
        states = [{'event': 'feature-state', '_source_pid': 42,
                   'state': {'decision': decision, 'coordinator_epoch': 'epoch',
                             'action_count': '0'}}
                  for decision in ('PENDING', 'DENIED')]
        journal.rows = Mock(return_value=states)
        with self.assertRaisesRegex(RuntimeError, 'denial is stale'):
            journal.denied(0, 0)

    def test_no_popup_claim_requires_refresh_of_actual_observation(self):
        controller = ui.Aurum(Mock(), 'cli', 12345)
        controller.texts = Mock(return_value=['작업 요청'])

        def refresh():
            controller.texts.return_value = ['거절', '항상 허용']

        controller.observe = Mock(side_effect=refresh)
        with self.assertRaisesRegex(RuntimeError, 'actual consent'):
            controller.verify_no_approval()
        controller.observe.assert_called_once()

    def test_long_python_transport_preserves_interpreter_output_and_exit(self):
        code = ('padding=' + repr('repeated' * 1000) + '\n' +
                'print("한글 \\\"quoted\\\"\\nnext")\n' +
                'import sys;sys.exit(7)')
        arguments = ['python3', '-c', code]
        wire, evidence = commands.remote_service(arguments,
                                                  'SAFE_TOKEN')
        self.assertEqual(evidence['transport'], 'zlib-base64')
        self.assertEqual(evidence['original_argv'], ['python3', '-c', code])
        self.assertEqual(evidence['code_sha256'],
                         hashlib.sha256(code.encode()).hexdigest())
        result = subprocess.run(['/bin/sh', '-c', wire], capture_output=True,
                                timeout=5)
        self.assertEqual(result.returncode, 7)
        self.assertIn('한글 "quoted"\nnext', result.stdout.decode())
        self.assertIn('SAFE_TOKEN:7', result.stdout.decode())
        self.assertLessEqual(evidence['service_bytes'], commands.MAX_SERVICE)

    def test_long_or_nonpython_service_cannot_fall_back_to_upload(self):
        random_code = 'x=' + repr(os.urandom(6000).hex())
        for arguments in (['python3', '-c', 'x' * 32769],
                          ['python3', '-c', random_code],
                          ['/bin/sh', '-c', 'x' * 3000]):
            with self.assertRaises(RuntimeError):
                commands.remote_service(arguments, 'SAFE_TOKEN')

    def test_tpk_duplicate_entry_rejected(self):
        archive = Mock()
        item = Mock(filename='bin/core', file_size=1)
        archive.infolist.return_value = [item, item]
        with self.assertRaisesRegex(RuntimeError, 'duplicate'):
            artifacts.zip_bytes(archive, 'bin/core')
        archive.read.assert_not_called()


if __name__ == '__main__':
    unittest.main()
