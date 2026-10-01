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
"""Actual Aurum observations, semantic input and protected-effect evidence."""

import json
import math
from pathlib import Path
import re
import socket
import sys
import time

from . import commands, phases, aurum_tree


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_action(value, width, height):
    require(isinstance(value, dict), 'action object required')
    method = value.get('method')
    fields = {'click': {'method', 'x', 'y'},
              'screenshot': {'method', 'name'},
              'gate': {'method', 'allowed'},
              'generation': {'method'}, 'finish': {'method'}}
    require(method in fields and set(value) == fields[method],
            'exact finite action schema required')
    if method == 'click':
        require(type(value['x']) is int and type(value['y']) is int and
                0 <= value['x'] < width and 0 <= value['y'] < height,
                'click must have bounded integer coordinates')
    elif method == 'gate':
        require(type(value['allowed']) is bool, 'gate boolean required')
    elif method == 'screenshot':
        require(isinstance(value['name'], str) and
                re.fullmatch(r'[A-Za-z0-9-]{1,80}', value['name']),
                'finite screenshot name required')


def input_point(node, windows, width, height):
    rect = node['geometry']
    x = round(rect['x'] + rect['width'] / 2)
    y = round(rect['y'] + rect['height'] / 2)
    validate_action({'method': 'click', 'x': x, 'y': y}, width, height)
    covered = False
    for window in windows:
        geometry = window.get('geometry')
        if (not isinstance(geometry, dict) or not all(
                type(geometry.get(k)) in (int, float) and
                math.isfinite(geometry[k]) for k in ('x', 'y', 'width',
                                                   'height'))):
            raise commands.Unavailable('unknown active window geometry')
        left, top = geometry['x'], geometry['y']
        right, bottom = left + geometry['width'], top + geometry['height']
        if (geometry['width'] <= 0 or geometry['height'] <= 0 or
                abs(left) > width * 4 or abs(top) > height * 4 or
                geometry['width'] > width * 4 or
                geometry['height'] > height * 4 or
                min(width, right) <= max(0, left) or
                min(height, bottom) <= max(0, top)):
            raise commands.Unavailable('unbounded/offscreen window geometry')
        if not (window['isActive'] and window['isVisible'] and
                window['isShowing']):
            continue
        contains = (max(0, left) <= x < min(width, right) and
                    max(0, top) <= y < min(height, bottom))
        if window['package'] == aurum_tree.PACKAGE:
            covered |= contains
        elif window['package'] != aurum_tree.CHROME:
            raise commands.Unavailable('unknown foreign active window')
        elif contains:
            raise commands.Unavailable('foreign active window covers input')
    if not covered:
        raise commands.Unavailable('input lacks own active window coverage')
    return x, y


def nodes(tree, width, height):
    require(isinstance(tree, dict) and tree.get('status') == 0 and
            isinstance(tree.get('roots'), list), 'invalid Aurum tree reply')
    result = []
    visited = 0

    def visit(node, parents, depth):
        nonlocal visited
        visited += 1
        require(isinstance(node, dict) and depth <= 24 and visited <= 4096,
                'tree exceeded node/depth bound')
        for key in ('text', 'widgetType'):
            require(isinstance(node.get(key), str) and len(node[key]) <= 8192,
                    'tree string is invalid')
        for key in ('isVisible', 'isShowing', 'isEnabled'):
            require(type(node.get(key)) is bool, 'tree flag is invalid')
        geometry = node.get('geometry')
        require(isinstance(geometry, dict) and
                all(type(geometry.get(k)) in (int, float) and
                    math.isfinite(geometry[k])
                    for k in ('x', 'y', 'width', 'height')),
                'tree geometry is invalid')
        if (node['isVisible'] and node['isShowing'] and
                all(p['isVisible'] and p['isShowing'] for p in parents)):
            result.append((node, parents))
        children = node.get('children', [])
        require(isinstance(children, list), 'tree children must be a list')
        for child in children:
            visit(child, parents + [node], depth + 1)

    for root in tree['roots']:
        visit(root, [], 0)
    if not result or not any(n['text'] for n, _ in result):
        raise commands.Unavailable('UNSUPPORTED_TREE: no usable native text')
    return result


class Aurum:
    def __init__(self, transaction, cli, port, cache=None):
        self.transaction = transaction
        self.cli = cli
        self.port = str(port)
        self.cache = cache
        self.started = False
        self.sequence = 0
        self.width = self.height = 0
        self.deadline = time.monotonic() + 600
        self.current = []
        self.windows = []
        self.chrome_proof = None

    def command(self, arguments, cleanup=False):
        require(cleanup or time.monotonic() < self.deadline,
                '600s subrun deadline')
        output = commands.run([self.cli, *arguments], timeout=25)
        text = output.decode('utf-8')
        patterns = {
            'session-start': r'\.\.\. successfully launched pid = [1-9][0-9]*'
                             r' with debug 0',
            'session-stop': r'Terminate appId: org\.tizen\.aurum-bootstrap',
        }
        lines = text.splitlines()
        if (arguments[0] in patterns and lines and
                not lines[0].lstrip().startswith('{')):
            require(re.fullmatch(patterns[arguments[0]], lines[0].strip()),
                    'unknown Aurum lifecycle diagnostic')
            text = '\n'.join(lines[1:])
        value = json.loads(text)
        require(isinstance(value, dict), 'Aurum reply must be an object')
        return value

    def pids(self):
        code = ("from pathlib import Path;import os,json;found=[];\n"
                "for p in Path('/proc').iterdir():\n"
                " if p.name.isdecimal():\n"
                "  try:\n"
                "   if os.readlink(p/'exe').startswith("
                "'/usr/apps/org.tizen.aurum-bootstrap/'):\n"
                "    found.append(int(p.name))\n"
                "  except (FileNotFoundError,ProcessLookupError):pass\n"
                "print(json.dumps(found))")
        return json.loads(self.transaction.remote(
            ['python3', '-c', code], 'aurum-pids'))

    def start(self):
        require(not self.pids(), 'foreign Aurum bootstrap is running')
        forward = commands.run(['sdb', '-s', commands.SERIAL,
                                'forward', '--list']).decode()
        require('tcp:' + self.port not in forward, 'foreign forward exists')
        with socket.socket() as check:
            check.bind(('127.0.0.1', int(self.port)))
        self.started = True
        self.command(['session-start', '--serial', commands.SERIAL,
                      '--port', self.port, '--retries', '4'])
        health = self.command(['health', '--port', self.port])
        require(health.get('status') == 'ok', 'Aurum health failed')
        self.width, self.height = health['width'], health['height']
        require(type(self.width) is int and type(self.height) is int and
                320 <= self.width <= 4096 and 240 <= self.height <= 4096,
                'unsupported observed screen geometry')

    def close(self):
        if not self.started:
            return
        error = None
        try:
            self.command(['session-stop', '--serial', commands.SERIAL,
                          '--port', self.port, '--stop-bootstrap'],
                              cleanup=True)
        except BaseException as value:
            error = value
        deadline = time.monotonic() + 10
        while self.pids() and time.monotonic() < deadline:
            time.sleep(.1)
        require(not self.pids(), 'owned bootstrap survived stop')
        forward = commands.run(['sdb', '-s', commands.SERIAL,
                                'forward', '--list']).decode()
        require('tcp:' + self.port not in forward, 'owned forward survived')
        if error:
            raise error

    def observe(self, budget=8):
        self.sequence += 1
        prefix = self.transaction.output / f'ui-{self.sequence:03}'
        require(self.cache is not None, 'existing Aurum cache required')
        started = time.monotonic()
        try:
            proof = self.transaction.audit('chrome', timeout=min(5, budget))
        except RuntimeError as error:
            raise commands.Unavailable(
                'trusted chrome proof unavailable') from error
        if proof.get('package') != aurum_tree.CHROME:
            raise commands.Unavailable('trusted chrome proof package mismatch')
        if self.chrome_proof is not None and proof != self.chrome_proof:
            raise commands.Unavailable('trusted chrome incarnation changed')
        self.chrome_proof = proof
        chrome_path = prefix.with_name(prefix.name + '-chrome')
        chrome_path.with_suffix('.json').write_text(
            json.dumps(proof, indent=2) + '\n')
        budget -= time.monotonic() - started
        require(budget > 0, 'native observation budget expired')
        output = commands.run([str(self.cache / 'venv/bin/python'), '-B',
            str(Path(__file__).with_name('aurum_tree.py')),
            str(self.cache), self.port, str(budget), 'verified-taskbar'],
            timeout=budget + 1, limit=2 * 1024 * 1024)
        tree = json.loads(output)
        require(isinstance(tree, dict), 'native tree object required')
        prefix.with_suffix('.json').write_text(
            json.dumps(tree, indent=2) + '\n')
        shot = prefix.with_suffix('.png')
        self.command(['screenshot', '--port', self.port, str(shot)])
        from PIL import Image
        with Image.open(shot) as image:
            require(image.size == (self.width, self.height),
                    'capture geometry differs from actual screen')
            # A black/transient display cannot establish an actionable frame.
            grey = image.convert('L')
            lit = sum(grey.histogram()[48:])
            if lit < self.width * self.height / 50:
                raise commands.Unavailable('black/transient actual capture')
        if tree.get('status') == 3:
            self.current = []
            raise commands.Unavailable('native window unavailable: ' +
                                       str(tree.get('reason')))
        if (tree.get('status') == 0 and tree.get('root_count') == 0 and
                tree.get('roots') == []):
            self.current = []
            raise commands.Unavailable('transient empty native roots')
        self.windows = tree.get('windows', [])
        self.current = nodes(tree, self.width, self.height)
        if any('Low memory' in node['text'] for node, _ in self.current):
            raise commands.Unavailable('observed system Low memory overlay')
        return self.current

    def texts(self):
        return [n['text'] for n, _ in self.current if n['text']]

    def control(self, text, kind='Button'):
        matches = {}
        for node, parents in self.current:
            if node['text'] != text:
                continue
            for candidate in [node, *reversed(parents)]:
                widget = re.split(r'[.:]', candidate['widgetType'])[-1]
                if widget != kind:
                    continue
                if candidate['isVisible'] and candidate['isShowing']:
                    identity = candidate.get('elementId')
                    require(isinstance(identity, str) and identity,
                            'control lacks native identity')
                    matches[identity] = candidate
                break
        require(len(matches) == 1, 'missing/ambiguous native ' + kind +
                ': ' + text)
        return next(iter(matches.values()))

    def click(self, text, kind='Button'):
        self.observe()
        node = self.control(text, kind)
        require(node['isEnabled'], 'native control is disabled: ' + text)
        rect = node['geometry']
        require(rect['width'] > 0 and rect['height'] > 0 and
                rect['x'] >= 0 and rect['y'] >= 0 and
                rect['x'] + rect['width'] <= self.width and
                rect['y'] + rect['height'] <= self.height,
                'control geometry outside actual screen')
        x, y = input_point(node, self.windows, self.width, self.height)
        value = {'method': 'click', 'x': x, 'y': y}
        validate_action(value, self.width, self.height)
        name = f'action-{self.sequence:03}.json'
        action_path = self.transaction.output / name
        action_path.write_text(
            json.dumps(value) + '\n')
        result = self.command(['click', '--port', self.port,
                               str(value['x']), str(value['y'])])
        require(result.get('status') == 0, 'actual input failed')

    def wait(self, text, timeout=15):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < deadline:
            try:
                self.observe(budget=min(8, deadline - time.monotonic()))
                if text in self.texts():
                    return
            except commands.Unavailable as error:
                if ('black/transient' not in str(error) and
                        str(error) != 'transient empty native roots' and
                        not str(error).startswith(
                            'native window unavailable:')):
                    raise
            time.sleep(.1)
        raise commands.Unavailable('native semantic state unavailable: ' + text)

    def page(self):
        rows = [re.fullmatch(r'(\d+) / (\d+)', text)
                for text in self.texts()]
        rows = [r for r in rows if r]
        require(len(rows) == 1, 'ambiguous native page counter')
        page, count = map(int, rows[0].groups())
        require(1 <= page <= count <= 24, 'unbounded fixture pagination')
        return page, count

    def wait_page(self, expected, count, timeout=15):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < deadline:
            self.observe(budget=min(8, deadline - time.monotonic()))
            page, actual_count = self.page()
            require(actual_count == count and
                    page in (expected - 1, expected),
                    'native page count/order changed during transition')
            if page == expected:
                return
            time.sleep(.1)
        raise RuntimeError('native page transition timeout')

    def review(self, primary):
        self.observe()
        page, count = self.page()
        require(page == 1, 'review must start at first page')
        text = []
        for expected in range(1, count + 1):
            self.wait_page(expected, count)
            text.extend(self.texts())
            next_button = self.control('다음')
            require(next_button['isEnabled'] == (expected < count),
                    'native Next state disagrees with page counter')
            if expected < count:
                self.click('다음')
        require(self.control(primary)['isEnabled'],
                'full native review did not enable approval')
        return ''.join(text).replace(' ', '').replace('\n', '')

    def settings_task(self, task):
        self.wait('선택 기능 설정')
        if '[ ] 이번 작업만 한 번' in self.texts():
            self.click('[ ] 이번 작업만 한 번')
            self.wait('[x] 이번 작업만 한 번')
        require('[x] 이번 작업만 한 번' in self.texts(),
                'TaskOnly must be explicitly selected')
        wanted = {'expanded': '작업: 30일 일정 요청',
                  'device': '작업: 기기 끄기'}[task]
        for _ in range(6):
            self.observe()
            if wanted in self.texts():
                break
            labels = [t for t in self.texts() if t.startswith('작업: ')]
            require(len(set(labels)) == 1, 'unknown settings task')
            self.click(labels[0])
        self.wait(wanted)
        self.click('작업 요청')
        self.wait('검토한 작업 요청')
        require('돌아가기' in self.texts() and '거절' not in self.texts(),
                'settings review confused with actual consent')
        self.review('검토한 작업 요청')
        self.click('검토한 작업 요청')

    def verify_no_approval(self):
        self.observe()
        require('거절' not in self.texts() and
                '항상 허용' not in self.texts(),
                'persistent reuse unexpectedly opened actual consent')
        self.wait('작업 요청')
        require('[x] 이번 작업만 한 번' in self.texts(),
                'reuse did not return to actual settings')

    def choose_persistent(self, selected):
        self.click('항상 허용', 'CheckBox')
        self.observe()
        primary = '항상 허용' if selected else '표시한 기간 허용'
        page, count = self.page()
        require(page == 1 and primary in self.texts(),
                'choice failed to reset native review')
        require(self.control(primary)['isEnabled'] == (count == 1),
                'choice bypassed full review')
        return primary

    def deny_and_verify(self, journal, before, mark):
        self.click('거절')
        diagnostics = {'errors': [], 'pids': []}
        observation_error = None
        try:
            self.observe()
        except Exception as error:
            observation_error = error
            diagnostics['errors'].append('post-input: ' + repr(error))
        try:
            journal.denied(before, mark)
        except Exception as original:
            try:
                pids = self.transaction.audit('gui-pids')
                require(isinstance(pids, list) and len(pids) <= 4 and
                        len(set(pids)) == len(pids) and
                        all(type(pid) is int and pid > 0 for pid in pids),
                        'invalid verified own GUI PID inventory')
                diagnostics['pids'] = pids
                for pid in pids:
                    try:
                        self.transaction.remote([
                            'dlogutil', '--pid', str(pid), '-t', '200',
                            '--color', 'never', '-v', 'time'],
                            'deny-ui-dlog-' + str(pid), timeout=10)
                    except Exception as error:
                        diagnostics['errors'].append('dlog: ' + repr(error))
            except Exception as error:
                diagnostics['errors'].append('own-PID: ' + repr(error))
            diagnostics['original_error'] = repr(original)
            try:
                path = self.transaction.output / (
                    f'deny-diagnostics-{self.sequence:03}.json')
                path.write_text(json.dumps(diagnostics, indent=2) + '\n')
            except Exception as error:
                print('deny diagnostics write failed: ' + repr(error),
                      file=sys.stderr)
            raise
        if observation_error is not None:
            raise observation_error

    def deny_review_reset(self, selected, journal, before, mark):
        self.wait('거절')
        primary = '표시한 기간 허용'
        require(self.control('항상 허용', 'CheckBox')['isEnabled'],
                'reset fixture does not support period choice')
        if not selected:
            primary = self.choose_persistent(True)
        self.review(primary)
        require(self.control(primary)['isEnabled'],
                'review never became enabled before toggle')
        self.choose_persistent(selected)
        self.deny_and_verify(journal, before, mark)

    def approve(self, persistent=False, once_only=False):
        self.wait('거절')
        primary = '표시한 기간 허용'
        require(primary in self.texts(), 'unknown bound base approval')
        checkbox = self.control('항상 허용', 'CheckBox')
        require(checkbox['isEnabled'] != once_only, 'checkbox policy mismatch')
        if once_only:
            require('이 요청과 정책은 항상 허용을 지원하지 않습니다.'
                    in self.texts(), 'missing ONCE-only explanation')
        elif persistent:
            primary = self.choose_persistent(True)
        text = self.review(primary)
        for required in ('범위:', '목적:', '수신자:',
                         '접근승인기간:', '취득데이터보관:'):
            require(required in text, 'incomplete consent disclosure')
        if not once_only:
            for required in ('범위:향후30일', '목적:대화요약',
                             '수신자:이기기의현재대화', '동작:달력읽기',
                             '취득데이터보관:최대30분'):
                require(required in text, 'protected tuple disclosure missing')
        if persistent:
            require('반복허용' in text, 'missing persistent period disclosure')
        self.click(primary)


class Journal:
    def __init__(self, transaction):
        self.transaction = transaction
        self.cursor = transaction.remote(['env', 'SYSTEMD_COLORS=0',
            'journalctl', '-n', '0',
            '--show-cursor', '--no-pager'],
                'journal-cursor').split('-- cursor: ')
        require(len(self.cursor) == 2, 'missing actual journal cursor')
        self.cursor = self.cursor[1].strip()
        self.operations = set()
        self.receipts = set()
        self.coordinators = set()
        self.epoch = None
        self.count = 0
        self.adopt_coordinator()

    def adopt_coordinator(self):
        value = self.transaction.remote(['systemctl', 'show', '-p', 'MainPID',
            '--value', 'consent-feature-ui09.service'], 'coordinator-pid')
        require(value.isdecimal() and int(value) > 0,
                'actual coordinator PID unavailable')
        pid = int(value)
        if getattr(self, 'current_coordinator', None) != pid:
            self.epoch = None
            self.count = 0
        self.current_coordinator = pid
        self.coordinators.add(pid)

    def rows(self):
        output = self.transaction.remote(['env', 'SYSTEMD_COLORS=0',
            'journalctl',
            '--after-cursor=' + self.cursor, '-u',
            'consent-feature-ui09.service', '-o', 'json', '--no-pager'],
            'worker-journal')
        result = []
        for line in output.splitlines():
            if not line.startswith('{'):
                continue
            envelope = json.loads(line)
            message = envelope.get('MESSAGE', '')
            require(isinstance(message, str), 'journal MESSAGE type invalid')
            if not message.startswith('{'):
                continue
            row = json.loads(message)
            require(isinstance(row, dict) and 'event' in row,
                    'malformed actual worker event')
            source = envelope.get('_PID', '')
            require(isinstance(source, str) and source.isdecimal(),
                    'missing actual journal producer PID')
            source = int(source)
            if row['event'].startswith('feature-'):
                require(source in self.coordinators or
                        row['event'] == 'feature-worker-stage',
                        'unknown coordinator journal producer')
            row['_source_pid'] = source
            result.append(row)
        return result

    def mark(self):
        return len(self.rows())

    def baseline(self):
        return len([r for r in self.rows() if r['event'] == 'action'])

    def effect(self, before, worker, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            rows = self.rows()
            actions = [r for r in rows if r['event'] == 'action']
            evidence = [r for r in rows
                        if r['event'] == 'feature-execution-result' and
                        r['_source_pid'] == self.current_coordinator and
                        r['evidence'].get('operation_id')
                        not in self.operations]
            states = [r for r in rows if r['event'] == 'feature-state' and
                      r['_source_pid'] == self.current_coordinator]
            if len(actions) > before and evidence and states:
                require(len(actions) == before + 1 and len(evidence) == 1,
                        'unexpected duplicate protected effect')
                event = evidence[0]
                row = event['evidence']
                state = states[-1]['state']
                if state.get('decision') != 'ALLOWED':
                    time.sleep(.1)
                    continue
                epoch = state.get('coordinator_epoch')
                require(self.epoch is None or epoch == self.epoch,
                        'unannounced coordinator epoch reset')
                expected_count = self.count + 1
                require(epoch and state.get('action_count') ==
                        str(expected_count) and state.get('grant_mode')
                            == 'ONCE'
                        and states[-1]['_source_pid'] == event['_source_pid'],
                        'actual epoch/base/counter mismatch')
                require(row.get('worker') == worker and row.get('status') == '0'
                        and row.get('action_started') == '1' and
                        row.get('action_retry') == '0' and
                        row.get('retired') == '0' and row.get('receipt') and
                        row['receipt'] not in self.receipts and
                        row.get('operation_id') == actions[-1]['operation'] and
                        actions[-1].get('retry') is False and
                        row.get('worker_pid') ==
                        str(actions[-1]['_source_pid']) and
                        row.get('feature') == ('calendar.read' if worker == 'ce'
                                               else 'device.control'),
                        'worker/new operation/receipt admission mismatch')
                pid = int(row['worker_pid'])
                code = ('import os;print(os.readlink(' +
                        repr('/proc/' + str(pid) + '/exe') + '))')
                executable = self.transaction.remote(['python3', '-c', code],
                                                       'effect-worker-exe')
                require(executable == '/usr/libexec/consent/poc/consent-mock-' +
                        worker, 'effect came from wrong actual worker')
                self.operations.add(row['operation_id'])
                self.receipts.add(row['receipt'])
                self.epoch, self.count = epoch, expected_count
                return {'effect': row, 'coordinator_pid': event['_source_pid'],
                        'coordinator_epoch': epoch, 'state': state}
            time.sleep(.1)
        raise RuntimeError('missing actual protected effect/new receipt')

    def denied(self, before, mark, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            rows = self.rows()
            fresh = [r for r in rows[mark:] if r['event'] == 'feature-state' and
                     r['_source_pid'] == self.current_coordinator]
            pending = [r for r in fresh
                       if r['state'].get('decision') == 'PENDING']
            if fresh and pending and fresh[-1]['state'].get('decision') == \
                    'DENIED':
                state = fresh[-1]['state']
                require(isinstance(state.get('selection_id'), str) and
                        bool(state['selection_id'])
                            and state.get('selection_id') ==
                        pending[-1]['state'].get('selection_id') and
                        pending[-1]['state'].get('coordinator_epoch') ==
                        self.epoch and
                        state.get('coordinator_epoch') == self.epoch and
                        state.get('action_count') == str(self.count) and
                        self.baseline() == before,
                        'denial is stale or admitted an effect')
                return
            time.sleep(.1)
        raise RuntimeError('fresh actual denial callback/state unavailable')


def scenario(transaction, cli, port, name, cache=None):
    ui = Aurum(transaction, cli, port, cache)
    journal = Journal(transaction)
    results = []
    try:
        ui.start()
        transaction.launch()
        ui.settings_task('expanded')
        ui.approve(persistent=name != 'functional')
        results.append(journal.effect(0, 'ce'))
        if name == 'functional':
            mark = journal.mark()
            ui.settings_task('expanded')
            ui.wait('거절')
            ui.deny_and_verify(journal, 1, mark)
            for selected in (True, False):
                mark = journal.mark()
                ui.settings_task('expanded')
                ui.deny_review_reset(selected, journal, 1, mark)
            ui.settings_task('expanded')
            ui.approve(True)
            results.append(journal.effect(1, 'ce'))
            ui.settings_task('expanded')
            ui.verify_no_approval()
            results.append(journal.effect(2, 'ce'))
            ui.verify_no_approval()
            transaction.revoke_calendar()
            mark = journal.mark()
            ui.settings_task('expanded')
            ui.wait('거절')
            ui.deny_and_verify(journal, 3, mark)
            ui.settings_task('device')
            ui.approve(once_only=True)
            results.append(journal.effect(3, 'cm'))
            transaction.maps()
        else:
            phases.gate(transaction, True)
            phase = phases.transition(transaction, name)
            journal.coordinators.update(transaction.phase_coordinators)
            journal.adopt_coordinator()
            ui.settings_task('expanded')
            if name != 'restart':
                ui.approve(True)
            if name == 'restart':
                ui.verify_no_approval()
            results.append(journal.effect(1, 'ce'))
            if name == 'restart':
                ui.verify_no_approval()
            phases.gate(transaction, True)
            results.append({'phase': phase})
        (transaction.output / 'scenario.json').write_text(
            json.dumps({'scenario': name, 'results': results}, indent=2) + '\n')
    finally:
        ui.close()
