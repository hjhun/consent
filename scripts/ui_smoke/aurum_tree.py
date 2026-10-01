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
"""Bounded native Aurum registration and package-scoped tree collection."""

import json
import math
from pathlib import Path
import sys
import time

PACKAGE = 'org.tizen.consentui'
CHROME = 'org.tizen.taskbar'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def metadata(element):
    geometry = {key: getattr(element.geometry, key)
                for key in ('x', 'y', 'width', 'height')}
    require(all(type(v) in (int, float) and math.isfinite(v)
                for v in geometry.values()), 'invalid native geometry')
    identity = element.elementId
    package = element.package
    require(isinstance(identity, str) and 0 < len(identity) <= 512 and
            isinstance(package, str) and len(package) <= 512,
            'invalid native identity/package')
    return {'elementId': identity, 'package': package,
            'isShowing': bool(element.isShowing),
            'isVisible': bool(element.isVisible),
            'isActive': bool(element.isActive), 'geometry': geometry}


def values(element):
    result = metadata(element)
    for key in ('text', 'widgetType', 'role', 'automationId'):
        value = getattr(element, key)
        require(isinstance(value, str) and len(value) <= 8192,
                'invalid native tree string')
        result[key] = value
    result['isEnabled'] = bool(element.isEnabled)
    return result


def convert(element, current, counter, diagnostics, depth=0):
    counter[0] += 1
    require(counter[0] <= 4096 and depth <= 24, 'native tree exceeded bound')
    raw = metadata(element)
    diagnostics['current_raw'] = dict(
        raw, isEnabled=bool(element.isEnabled))
    require(raw['package'] in ('', PACKAGE), 'foreign package in own tree')
    fresh = current.get(raw['elementId'])
    require(fresh is not None, 'paired native ID missing')
    require(fresh.package == PACKAGE, 'foreign full-refresh package')
    result = values(fresh)
    result['raw_dump'] = values(element)
    result['package_source'] = 'full-refresh-exact-ID'
    result['children'] = [
        convert(child, current, counter, diagnostics, depth + 1)
        for child in element.child]
    return result


def ids(node):
    result = {node['elementId']}
    for child in node['children']:
        child_ids = ids(child)
        require(not result.intersection(child_ids), 'duplicate tree identity')
        result.update(child_ids)
    return result


def collect(client, pb, clock=time.monotonic, budget=8,
            chrome_verified=False, diagnostics=None):
    require(0 < budget <= 8, 'invalid aggregate RPC budget')
    deadline = clock() + budget
    calls = []
    diagnostics = {} if diagnostics is None else diagnostics
    diagnostics['calls'] = calls

    def call(method, request):
        remaining = deadline - clock()
        require(remaining > 0, 'aggregate Aurum RPC deadline')
        response = getattr(client, method)(request, timeout=min(3, remaining))
        require(clock() <= deadline, 'aggregate Aurum RPC deadline')
        require(response.ByteSize() <= 2 * 1024 * 1024,
                'Aurum response exceeded byte bound')
        status = int(response.status)
        count = (len(response.elements) if method == 'findElements'
                 else len(response.roots))
        no_match = (method == 'findElements' and
                    status == int(pb.ERROR) and count == 0)
        require(status == int(pb.OK) or no_match,
                'native Aurum status failure')
        calls.append({'method': method, 'bytes': response.ByteSize(),
                      'status': status, 'count': count})
        return response

    global_reply = call('findElements', pb.ReqFindElements(
        isShowing=True, maxDepth=1))
    require(len(global_reply.elements) <= 32, 'window metadata exceeded bound')
    windows = [metadata(e) for e in global_reply.elements]
    diagnostics['windows'] = windows
    require(len({e['elementId'] for e in windows}) == len(windows),
            'duplicate window metadata identity')
    active = [e for e in windows if e['isActive'] and e['isVisible'] and
              e['isShowing']]
    blocked = (not any(e['package'] == PACKAGE for e in active) or
               any(e['package'] != PACKAGE and not (chrome_verified and
                   e['package'] == CHROME) for e in active))
    if blocked:
        return {'status': 3, 'reason': 'ambiguous/inactive native windows',
                'windows': windows, 'calls': calls, 'roots': [],
                'root_count': 0}
    found = call('findElements', pb.ReqFindElements(
        packageName=PACKAGE, isShowing=True, maxDepth=1))
    require(len(found.elements) <= 8, 'own window candidates exceeded bound')
    candidates = [metadata(e) for e in found.elements]
    diagnostics['candidates'] = candidates
    require(all(e['package'] == PACKAGE for e in candidates),
            'foreign candidate package')
    require(len({e['elementId'] for e in candidates}) == len(candidates),
            'duplicate candidate identity')
    roots = []
    pairings = []
    refreshed_count = 0
    counter = [0]
    for candidate in candidates:
        reply = call('dumpObjectTree', pb.ReqDumpObjectTree(
            elementId=candidate['elementId']))
        require(len(reply.roots) == 1, 'registered native ID became stale')
        raw = reply.roots[0]
        require(raw.elementId == candidate['elementId'],
                'native dump root identity changed')
        refreshed = call('findElements', pb.ReqFindElements(
            elementId=candidate['elementId'], packageName=PACKAGE, maxDepth=24))
        refreshed_count += len(refreshed.elements)
        require(refreshed_count <= 4096,
                'full-refresh nodes exceeded bound')
        current = {}
        for element in refreshed.elements:
            item = metadata(element)
            diagnostics['current_refresh'] = item
            require(item['package'] == PACKAGE, 'foreign full-refresh package')
            require(item['elementId'] not in current,
                    'duplicate full-refresh identity')
            current[item['elementId']] = element
        require(candidate['elementId'] in current,
                'full-refresh root identity changed')
        root = convert(raw, current, counter, diagnostics)
        pairings.append({'root': candidate['elementId'],
                         'refreshed_ids': list(current)})
        require(root['elementId'] == candidate['elementId'],
                'native dump root identity changed')
        roots.append(root)
    descendants = [ids(root) - {root['elementId']} for root in roots]
    kept = [root for root in roots if not any(
        root['elementId'] in values for values in descendants)]
    require(not roots or kept, 'cyclic native root relation')
    require(clock() <= deadline, 'aggregate Aurum RPC deadline')
    return {'status': 0, 'root_count': len(kept), 'roots': kept,
            'windows': windows, 'candidates': candidates, 'calls': calls,
            'pairings': pairings,
            'eliminated': [r['elementId'] for r in roots if r not in kept]}


def main():
    require(len(sys.argv) == 5 and sys.argv[4] == 'verified-taskbar',
            'cache, loopback port, budget and chrome proof required')
    cache = Path(sys.argv[1]).resolve(strict=True)
    port = int(sys.argv[2])
    budget = float(sys.argv[3])
    require(math.isfinite(budget) and 0 < budget <= 8,
            'invalid aggregate RPC budget')
    require(1024 <= port <= 65535, 'invalid loopback port')
    sys.path.insert(0, str(cache / 'generated'))
    import grpc
    import aurum_pb2 as pb
    import aurum_pb2_grpc as rpc
    with grpc.insecure_channel('127.0.0.1:' + str(port), options=[
            ('grpc.max_receive_message_length', 2 * 1024 * 1024)]) as channel:
        diagnostics = {}
        try:
            result = collect(rpc.BootstrapStub(channel), pb, budget=budget,
                             chrome_verified=True, diagnostics=diagnostics)
            output = json.dumps(result, ensure_ascii=False)
            require(len(output.encode()) <= 2 * 1024 * 1024,
                    'collected tree exceeded output byte bound')
        except Exception as error:
            print(json.dumps({'status': 1, 'reason': str(error)[:1024],
                              'diagnostics': diagnostics}, ensure_ascii=False))
            raise SystemExit(1)
    print(output)


if __name__ == '__main__':
    main()
