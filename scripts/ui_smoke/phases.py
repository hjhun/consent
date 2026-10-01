#!/usr/bin/python3
#
# Copyright (c) 2026 Samsung Electronics Co., Ltd. All rights reserved.
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
#
import hashlib
import json
from pathlib import Path
import secrets
import time

ROOT = '/opt/var/lib/consent-ui-native-09'
STATE = '/opt/var/lib/consent-ui09-state'
PACKAGE = 'org.tizen.consentui'


def require(value, message):
    if not value:
        raise RuntimeError(message)


def properties():
    return {'User': 'root', 'Group': 'root', 'SmackProcessLabel': 'System',
            'PrivateMounts': 'yes', 'RuntimeMaxSec': '10',
            'TimeoutStartSec': '10', 'BindReadOnlyPaths': ' '.join(
                [ROOT + '/tools:/usr/libexec/consent/poc'] +
                [ROOT + '/libraries/' + n + ':/usr/lib64/' + n for n in
                 ('libconsent-poc.so.0.1.0',
                  'libconsent-feature-poc.so.0.1.0')])}


def gate(transaction, allowed, old_operation=None, stale=False):
    require(type(allowed) is bool, 'gate expected value must be boolean')
    key = 'phase-' + secrets.token_hex(12)
    expected = 'ALLOWED' if allowed else 'CONSENT_REQUIRED'
    expected_status = -116 if stale else 0
    operation = old_operation or key
    # Exact registered calendar-expanded tuple. No caller receipt, no UI field,
    # no chosen/effective/internal mode, no session revival.
    content = ('[mock]\nid=' + key + '\nexpect_status=' +
        str(expected_status) + '\n' +
               ('' if stale else 'expect_decision=' + expected + '\n') +
                   '[params]\nsubject=owner\nprofile=default\n'
               'count=1\noperation_id=' + operation +
               '\nstep_id=mock-execute\nr0.definition=mock.calendar.read\n'
               'r0.policy_version=1\nr0.scope=calendar.default.next30days\n'
               'r0.operation=read\nr0.purpose=conversation-summary\n'
               'r0.recipient=local-conversation\nr0.holder=mock-holder\n')
    local = transaction_output(transaction) / (key + '.ini')
    local.write_text(content);local.chmod(0o600)
    transaction.upload(local, key + '.ini')
    transaction.remote(['chmod', '0600', ROOT + '/' + key + '.ini'],
        'gate-mode')
    transaction.remote(['chsmack', '-a', 'System', ROOT + '/' + key + '.ini'],
                       'gate-label')
    result = transaction.dispatch('gate',
        ['/usr/libexec/consent/poc/consent-mock-ce', 'authorize',
         ROOT + '/' + key + '.ini'], properties(), 15)
    rows = [json.loads(line) for line in result.splitlines()
            if line.startswith('{')]
    results = [row for row in rows if row.get('event') == 'result']
    require(len(results) == 1, 'missing/duplicate authoritative result')
    row = results[0]
    require(row['status'] == expected_status and
            (stale or row.get('decision') == expected) and
            row['fields']['matches_expectation'] == '1', 'gate mismatch')
    require(bool(row.get('receipt')) == allowed, 'gate receipt mismatch')
    # This actor only calls the public gate. Actual protected effects remain the
    # feature worker's separately journaled real synthetic actions.
    (transaction_output(transaction) / (key + '-gate.json')).write_text(
        json.dumps(row, indent=2) + '\n')
    if allowed:
        transaction.last_gate_operation = operation
        transaction.last_gate_epoch = row['fields'].get('epoch')
    return row


def transaction_output(transaction):
    return transaction.output


def close_ui(transaction):
    if transaction.audit('gui-pids'):
        transaction.remote(['pkgcmd', '-k', '-n', PACKAGE, '--global'],
                           'phase-close-ui')
    deadline = time.monotonic() + 10
    while transaction.audit('gui-pids') and time.monotonic() < deadline:
        time.sleep(.1)
    require(transaction.audit('gui-pids') == [], 'UI not stopped before fence')


def fixture_code(body):
    return ('import sys;sys.dont_write_bytecode=True;import importlib.util;'
            's=importlib.util.spec_from_file_location("owned",' +
            repr(ROOT + '/tests/emulator-ui-native.py') + ');'
            'm=importlib.util.module_from_spec(s);s.loader.exec_module(m);'
            'm.payload();m.verify_units();' + body)


def start(transaction, coordinator=True):
    names = ['consentd-ui09.socket', 'consentd-ui09.service']
    if coordinator:
        names += ['consent-feature-ui09.socket', 'consent-feature-ui09.service']
    transaction.remote(['python3', '-c', fixture_code(
        'm.command("systemctl","start",*' + repr(names) + ');'
        'm.require(m.show("consentd-ui09.service","ActiveState")=="active",'
        '"daemon not active");print("OWNED_START0")')], 'phase-start', 30)
    if coordinator:
        value = transaction.remote(['systemctl', 'show', '-p', 'MainPID',
            '--value', 'consent-feature-ui09.service'], 'phase-coordinator-pid')
        require(value.isdecimal() and int(value) > 0,
                'owned phase coordinator not active')
        if not hasattr(transaction, 'phase_coordinators'):
            transaction.phase_coordinators = []
        transaction.phase_coordinators.append(int(value))



def inspect(transaction, zero=False):
    # Stop/drain all owned services via the exact digest/FragmentPath validator
    # BEFORE readonly SQLite inspection. No runner DB access while daemon live.
    transaction.script('stop')
    body = ('import sqlite3;'
            'm.protected(m.STATE,directory=True,uid=m.pwd.getpwnam('
            '"security_fw").pw_uid);'
            'm.read(m.STATE/"consent.db",uid=m.pwd.getpwnam('
            '"security_fw").pw_uid,limit=16777216);'
            'd=sqlite3.connect("file:"+str(m.STATE/"consent.db")+'
            '"?mode=ro",uri=True);'
            'r={"integrity":d.execute("PRAGMA integrity_check").fetchone()[0],'
            '"schema":d.execute("PRAGMA user_version").fetchone()[0],'
            '"definitions":d.execute("SELECT count(*) FROM definitions '
            'WHERE active=1").fetchone()[0],'
            '"grants":d.execute("SELECT count(*) FROM grants WHERE '
            'revoked=0").fetchone()[0],'
            '"persistent":d.execute("SELECT count(*) FROM grants WHERE '
            'revoked=0 AND mode=\'PERSISTENT\'").fetchone()[0],'
            '"cleanup_unknown":d.execute("SELECT value FROM meta WHERE '
            'key=\'cleanup_unknown\'").fetchone()[0]};'
            'd.close();m.require(r["integrity"]=="ok" and r["schema"]==2 '
            'and r["definitions"]==2,"readback invalid");')
    if zero:
        body += ('m.require(r["grants"]==0 and r["cleanup_unknown"]=="1",'
                 '"old approvals recovered");')
    else:
        body += 'm.require(r["persistent"]>=1,"persistent approval missing");'
    body += 'print(m.json.dumps(r))'
    output = transaction.remote(['python3', '-c', fixture_code(body)],
                                'phase-db-readback')
    rows = [json.loads(line) for line in output.splitlines()
            if line.startswith('{')]
    keys = {'integrity','schema','definitions','grants','persistent',
        'cleanup_unknown'}
    readbacks = [row for row in rows if set(row)==keys]
    require(len(readbacks)==1,'missing/duplicate bounded DB readback')
    return readbacks[0]


def transition(transaction, phase):
    require(phase in ('restart', 'delete', 'generation'), 'unknown phase')
    old_operation = getattr(transaction, 'last_gate_operation', None)
    require(old_operation,
            'transition needs prior authoritative allowed receipt')
    close_ui(transaction)
    transaction.script('stop')
    before = transaction.remote(['cat', ROOT + '/generation'],
                                 'phase-current-generation').strip()
    if phase == 'delete':
        body = ('import os,stat;'
                'u=m.pwd.getpwnam("security_fw").pw_uid;'
                'm.protected(m.STATE,directory=True,uid=u);'
                'p=os.open(m.STATE,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW);'
                'f=os.open("consent.db",os.O_RDONLY|os.O_NOFOLLOW,dir_fd=p);'
                'a=os.fstat(f);b=os.stat("consent.db",dir_fd=p,'
                'follow_symlinks=False);'
                'm.require(stat.S_ISREG(a.st_mode) and a.st_nlink==1 and '
                'a.st_uid==u and not a.st_mode&0o077 and '
                '(a.st_dev,a.st_ino)==(b.st_dev,b.st_ino),'
                '"unsafe DB identity");'
                'os.unlink("consent.db",dir_fd=p);os.fsync(p);'
                'os.close(f);os.close(p);print("OWNED_DB_DELETE0")')
        transaction.remote(['python3', '-c', fixture_code(body)],
                           'phase-delete')
    if phase == 'generation':
        helper = ROOT + '/tests/consent-installation-authority-ui09'
        new = transaction.dispatch('authority', [helper, '--image-root',
            '/', 'begin',
                PACKAGE, 'ui09-phase-new-begin', before], properties(), 15)
        require(new != before and len(new) == 36, 'generation unchanged')
        transaction.dispatch('authority', [helper, '--image-root', '/',
            'attach', PACKAGE,
                            PACKAGE, 'ui09-phase-new-attach', new],
                           properties(), 15)
        transaction.dispatch('authority', [helper, '--image-root', '/',
            'commit', PACKAGE,
                            'ui09-phase-new-commit', new], properties(), 15)
        # Guarded update of this invocation's protected fixture generation file.
        transaction.remote(['python3', '-c', fixture_code(
            'm.require(m.read(m.ROOT/"generation").decode().strip()==' +
            repr(before) + ',"generation file changed");'
            'import os;g=m.ROOT/"generation";a=m.protected(g);'
            'f=os.open(g,os.O_WRONLY|os.O_NOFOLLOW);b=os.fstat(f);'
            'm.require((a.st_dev,a.st_ino)==(b.st_dev,b.st_ino),'
            '"generation inode");'
            'os.ftruncate(f,0);v=' + repr((new+'\n').encode()) + ';'
            'm.require(os.write(f,v)==len(v),"generation write");'
            'os.fsync(f);os.close(f);'
            'print("OWNED_GENERATION_UPDATE0")')], 'generation-file')
        transaction.owned_files['generation'] = hashlib.sha256(
            (new + '\n').encode()).hexdigest()
        start(transaction, False)
        registration = transaction.dispatch('register-new-generation',
            ['/usr/libexec/consent/poc/consent-mock-installer',
             'feature-register-choice', new], properties(), 15)
        rows = [json.loads(x) for x in registration.splitlines()
                if x.startswith('{')]
        require(len(rows)==2 and all(x['status']==0 for x in rows),
                'new generation registration failed')
        start(transaction)
        blocked = gate(transaction, False)
        old_retry = gate(transaction, False, old_operation, stale=True)
        evidence = {'previous': before, 'current': new, 'gate': blocked,
            'old_retry': old_retry,
                    'kind': 'real owned installation-generation helper change; '
                            'NOT an actual TPK reinstall'}
    else:
        start(transaction)
        gate_result = gate(transaction, phase == 'restart')
        old_retry = (gate(transaction, False, old_operation)
                     if phase == 'delete' else None)
        readback = inspect(transaction, zero=phase == 'delete')
        start(transaction)
        evidence = {'gate': gate_result, 'old_retry': old_retry,
            'readback': readback,
                    'handles': 'new authenticated process handles; old UI and '
                               'coordinator/worker processes stopped/drained'}
    transaction.launch()
    (transaction_output(transaction) / ('phase-' + phase + '.json')).write_text(
        json.dumps(evidence, indent=2) + '\n')
    return evidence
