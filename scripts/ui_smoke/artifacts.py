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
"""Finite matching RPM/TPK provenance and an independently verified backup."""

import hashlib
import json
import re
from pathlib import Path
import stat
import xml.etree.ElementTree as ET
import zipfile

from . import commands

HELPERS = ('consentd-ui09', 'consent-storage-prepare-ui09',
           'consent-installation-authority-ui09', 'consent-ui09-admin',
           'emulator-ui-native.py')
TOOLS = tuple('consent-mock-' + role for role in
              ('argo', 'installer', 'cm', 'ce', 'holder')) + (
                  'consent-poc-launch',)
LIBRARIES = ('libconsent-poc.so.0', 'libconsent-feature-poc.so.0')
TPK_NAME = 'org.tizen.consentui-0.1.0.tpk'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def zip_bytes(archive, name, limit=32 * 1024 * 1024):
    entries = [item for item in archive.infolist() if item.filename == name]
    require(len(entries) == 1 and entries[0].file_size <= limit,
            'missing/duplicate/oversized TPK entry: ' + name)
    return archive.read(entries[0])


def manifest_identity(archive):
    payload = zip_bytes(archive, 'tizen-manifest.xml', 65536)
    require(b'<!DOCTYPE' not in payload.upper() and
            b'<!ENTITY' not in payload.upper(), 'unsupported manifest entity')
    root = ET.fromstring(payload)
    namespace = '{http://tizen.org/ns/packages}'
    require(root.tag == namespace + 'manifest' and
            root.get('package') == 'org.tizen.consentui',
            'unexpected signed TPK package identity')
    applications = root.findall(namespace + 'ui-application')
    require(len(applications) == 1 and
            applications[0].get('appid') == 'org.tizen.consentui' and
            applications[0].get('exec') == 'ConsentUI.dll' and
            applications[0].get('type') == 'dotnet',
            'unexpected signed TPK application/executable identity')
    require(not root.findall(namespace + 'service-application'),
            'unexpected additional TPK service')
    return {'package': root.get('package'),
            'appid': applications[0].get('appid'),
            'executable': applications[0].get('exec')}


def certificates(archive):
    root = ET.fromstring(zip_bytes(archive, 'author-signature.xml', 1048576))
    namespace = {'ds': 'http://www.w3.org/2000/09/xmldsig#'}
    values = [node.text.strip() for node in root.findall(
        './/ds:X509Certificate', namespace) if node.text]
    require(bool(values), 'missing author certificate')
    return values


class Build:
    def __init__(self, directory, output, architecture):
        self.output = output
        self.files = {}
        self.rpms = {}
        self.original = None
        self.dependencies = set()
        metadata = []
        for path in sorted(directory.glob('*.rpm')):
            require(path.is_file() and not path.is_symlink(), 'unsafe RPM')
            row = commands.run(['rpm', '-qp', '--qf',
                '%{NAME}\t%{VERSION}\t%{RELEASE}\t%{ARCH}', str(path)])
            fields = row.decode().split('\t')
            if fields[0] not in ('consent-tests', 'consent-poc'):
                continue
            require(fields[0] not in self.rpms, 'ambiguous matching RPM')
            require(len(fields) == 4 and fields[1] == '0.1.0' and
                    fields[2] == '28' and fields[3] == architecture,
                    'unsupported package snapshot/architecture')
            self.rpms[fields[0]] = path
            metadata.append(fields)
        require(set(self.rpms) == {'consent-tests', 'consent-poc'},
                'both matching tests and PoC RPMs are required')
        require(metadata[0][1:] == metadata[1][1:], 'mixed RPM snapshots')
        self.release = int(metadata[0][2])
        self.extract()
        self.verify_tpk()

    def extract_file(self, package, absolute, relative):
        rpm = self.rpms[package]
        listing = commands.run(['rpm', '-qp', '--qf',
            '[%{FILENAMES}\t%{FILEMODES:octal}\n]', str(rpm)]).decode()
        rows = [row.split('\t') for row in listing.splitlines()
                if row.split('\t')[0] == absolute]
        require(len(rows) == 1 and len(rows[0]) == 2 and
                stat.S_ISREG(int(rows[0][1], 8)), 'not a regular RPM payload')
        # Extract one allowlisted regular file to stdout, never archive paths
        # into the host filesystem. RPM and cpio are separate checked commands.
        cpio = commands.run(['rpm2cpio', str(rpm)], limit=128 * 1024 * 1024)
        payload = commands.run(
            ['cpio', '-i', '--quiet', '--to-stdout', '.' + absolute],
            input_data=cpio, timeout=30, limit=32 * 1024 * 1024)
        digests = commands.run(['rpm', '-qp', '--qf',
            '%{FILEDIGESTALGO}\n[%{FILENAMES}\t%{FILEDIGESTS}\n]',
            str(rpm)]).decode().splitlines()
        expected = [row.split('\t')[1] for row in digests[1:]
                    if row.split('\t')[0] == absolute]
        require(digests[0] == '8' and len(expected) == 1 and
                hashlib.sha256(payload).hexdigest() == expected[0],
                'extracted bytes differ from RPM file digest')
        path = self.output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream:
            stream.write(payload)
        self.files[relative] = path
        return path

    def extract(self):
        for name in HELPERS:
            self.extract_file('consent-tests',
                '/usr/libexec/consent/tests/' + name, 'tests/' + name)
        for name in TOOLS:
            self.extract_file('consent-poc',
                '/usr/libexec/consent/poc/' + name, 'tools/' + name)
        for name in (TPK_NAME, TPK_NAME.replace('.tpk', '.build.json')):
            self.extract_file('consent-poc', '/usr/share/consent/poc/' + name,
                'metadata/' + name)
        self.files['tpk/ui09.tpk'] = self.files.pop('metadata/' + TPK_NAME)

    def verify_tpk(self):
        tpk = self.files['tpk/ui09.tpk']
        sidecar = self.files.pop('metadata/' +
                                TPK_NAME.replace('.tpk', '.build.json'))
        data = json.loads(sidecar.read_text())
        require(data.get('format') == 1 and
                data.get('package') == 'org.tizen.consentui' and
                data.get('tpk_sha256') == digest(tpk), 'TPK metadata mismatch')
        with zipfile.ZipFile(tpk) as archive:
            self.manifest = manifest_identity(archive)
            self.certificates = certificates(archive)
            for name in LIBRARIES:
                rows = [row for row in data['native_libraries']
                        if row['soname'] == name]
                payload = zip_bytes(archive, 'bin/' + name)
                sha = hashlib.sha256(payload).hexdigest()
                require(len(rows) == 1 and rows[0]['sha256'] == sha and
                        payload.startswith(b'\x7fELF'), 'native hash mismatch')
                path = self.output / 'libraries' / (name + '.1.0')
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(payload)
                elf = commands.run(['readelf', '-h', '-d', str(path)]).decode()
                self.record_dependencies(elf)
                require('Advanced Micro Devices X86-64' in elf and
                        '[' + name + ']' in elf, 'ELF arch/SONAME mismatch')
                self.files['libraries/' + path.name] = path
        for relative, path in self.files.items():
            if relative.startswith(('tools/', 'tests/')) and not (
                    relative.endswith('.py')):
                require(path.read_bytes().startswith(b'\x7fELF'),
                        'required helper is not ELF')
                elf = commands.run(['readelf', '-h', '-d', str(path)]).decode()
                require('Advanced Micro Devices X86-64' in elf,
                        'required helper architecture mismatch')
                self.record_dependencies(elf)
                (self.output / (path.name + '-elf.txt')).write_text(elf)
        self.verify_dependencies()
        (self.output / 'build-provenance.json').write_text(json.dumps({
            'rpms': {n: {'path': str(p), 'sha256': digest(p)}
                     for n, p in self.rpms.items()},
            'sidecar': data, 'files': {n: digest(p)
                                      for n, p in self.files.items()},
            'gbs_test_pass_inferred': False}, indent=2) + '\n')

    def record_dependencies(self, elf):
        names = re.findall(r'\(NEEDED\).*\[([^]\n]+)\]', elf)
        require(all(re.fullmatch(r'[A-Za-z0-9_.+-]+', name)
                    for name in names), 'unsafe native dependency name')
        self.dependencies.update(names)

    def verify_dependencies(self):
        # Never load candidate native code during provenance inspection.
        needed = sorted(self.dependencies - set(LIBRARIES))
        code = ("import os,stat,json,subprocess,re;needed=" + repr(needed) +
            ";text=subprocess.check_output(['ldconfig','-p'],text=True);"
            "rows=re.findall(r'^\\s*(\\S+) .* => (\\S+)$',text,re.M);"
            "result={};"
            "\nfor name in needed:\n"
            " paths={os.path.realpath(p) for n,p in rows if n==name};"
            "assert len(paths)==1,'missing/ambiguous native dependency';"
            "path=paths.pop();assert path.startswith(('"
            "/usr/lib/','/usr/lib64/',"
            "'/lib/','/lib64/'));"
            "fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW);info=os.fstat(fd);"
            "assert stat.S_ISREG(info.st_mode) and info.st_uid==0 and "
            "not info.st_mode&0o022;head=os.read(fd,20);os.close(fd);"
            "assert head[:6]==b'\\x7fELF\\x02\\x01' and "
            "int.from_bytes(head[18:20],'little')==62;"
            "result[name]={'path':path,'dev':info.st_dev,'inode':info.st_ino};"
            "\nprint(json.dumps(result))")
        result = commands.remote(['python3', '-c', code],
                                 'native-dependencies.log')
        (self.output / 'native-dependencies.json').write_text(result + '\n')

    def backup(self):
        rows = commands.remote(['sha256sum', commands.OLD_TPK,
            commands.APP + '/bin/ConsentUI.dll'],
                'original-ui.log').splitlines()
        expected = {name: sha for sha, name in map(str.split, rows)}
        path = self.output / 'original.tpk'
        commands.run(['sdb', '-s', commands.SERIAL, 'pull',
                      commands.OLD_TPK, str(path)], 45)
        require(digest(path) == expected[commands.OLD_TPK],
                'protected original TPK backup mismatch')
        with zipfile.ZipFile(path) as archive:
            require(manifest_identity(archive) == self.manifest,
                    'restoration TPK identity mismatch')
            dll = hashlib.sha256(zip_bytes(archive,
                                          'bin/ConsentUI.dll')).hexdigest()
            require(dll == expected[commands.APP + '/bin/ConsentUI.dll'],
                    'installed DLL differs from restoration TPK')
            require(certificates(archive) == self.certificates,
                    'original/new author certificate identity differs')
        self.original = {'tpk_sha256': digest(path), 'dll_sha256': dll}
        self.files['tpk/original.tpk'] = path
        (self.output / 'original-backup.json').write_text(
            json.dumps(self.original, indent=2) + '\n')

    def payload(self):
        require(self.original is not None, 'restoration backup required')
        return self.files.copy()
