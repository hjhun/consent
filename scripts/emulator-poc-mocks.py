#!/usr/bin/env python3
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
"""Explicit-device PoC actors; the human alone responds through the native UI."""

import argparse
import configparser
import hashlib
import io
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import subprocess
import sys
import tempfile
import time


CONTROL = "/opt/var/lib/consent-poc-control"
TOOLS = "/usr/libexec/consent/poc"
ACTORS = ("argo", "holder")
IDENTIFIER = re.compile(r"[A-Za-z0-9_.:-]{1,256}\Z")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def quote(value):
    return shlex.quote(str(value))


def redacted(text):
    return re.sub(r'("resume_token"\s*:\s*")[^"]*', r'\1[REDACTED]', text)


class Driver:
    def __init__(self, arguments):
        self.args = arguments
        self.artifacts = Path(arguments.artifact_dir).absolute()
        self.artifacts.mkdir(mode=0o700, parents=True, exist_ok=True)
        require(not self.artifacts.is_symlink(), "artifact directory must not be a symlink")
        os.chmod(self.artifacts, 0o700)
        self.state_path = self.artifacts / "state.json"
        self.state = {}
        self.helper_paths = {}

    def evidence(self, name, text):
        path = self.artifacts / name
        require(not path.is_symlink(), "evidence path must not be a symlink")
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(redacted(text))

    def remote(self, command, timeout=15):
        marker = "__CONSENT_POC_EXIT_" + secrets.token_hex(12) + "__"
        wrapper = ("/bin/sh -c " + quote(command) + "\nconsent_poc_status=$?\n"
                   "printf '\\n" + marker + ":%s\\n' \"$consent_poc_status\"\n"
                   "exit \"$consent_poc_status\"\n")
        completed = subprocess.run(
            ["sdb", "-s", self.args.serial, "shell", "/bin/sh -c " + quote(wrapper)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
        output = completed.stdout.replace("\r\n", "\n")
        self.evidence("remote-" + secrets.token_hex(6) + ".log",
                      "$ " + command + "\n" + output + completed.stderr)
        matches = list(re.finditer(r"(?:^|\n)" + re.escape(marker) + r":([0-9]+)\n?", output))
        require(len(matches) == 1, "SDB command did not return its explicit remote exit marker")
        require(completed.returncode == 0 and int(matches[0].group(1)) == 0,
                "remote command failed; see private remote-*.log evidence")
        return (output[:matches[0].start()] + output[matches[0].end():]).strip()

    def privileged(self, command, timeout=15):
        unit = "consent-poc-driver-" + secrets.token_hex(8)
        path = CONTROL + "/." + unit + ".sh"
        self.upload(path, "#!/bin/sh\nset -eu\n" + command + "\n")
        return self.remote("systemd-run --quiet --wait --pipe --unit=" + unit +
                           " -p User=root -p Group=root -p SmackProcessLabel=System::Privileged"
                           " -p TimeoutStartSec=10 -p RuntimeMaxSec=10 /bin/sh " + quote(path), timeout)

    def save(self):
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=self.artifacts,
                                         prefix=".state-", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(self.state, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, self.state_path)

    def load(self):
        require(self.state_path.is_file() and not self.state_path.is_symlink(), "no saved PoC run")
        require(self.state_path.stat().st_size <= 65536, "saved state exceeds bound")
        self.state = json.loads(self.state_path.read_text(encoding="utf-8"))
        require(self.state.get("serial") == self.args.serial, "saved run belongs to another emulator")
        require(re.fullmatch(r"[0-9a-f]{16}", self.state.get("run", "")), "invalid saved run identity")
        require(self.state.get("expect") in ("ALLOWED", "DENIED"), "invalid saved expectation")
        require(self.state.get("locale") in ("en-US", "ko-KR"), "invalid saved locale")

    @property
    def directory(self):
        return CONTROL + "/runs/" + self.state["run"]

    def unit(self, role):
        require(role in ACTORS, "invalid persistent actor")
        return "consent-poc-mock-" + role + "-" + self.state["run"] + ".service"

    def upload(self, destination, content):
        data = content.encode("utf-8")
        require(len(data) <= 65536, "generated fixture exceeds bound")
        # These are generated root-private paths, never caller-selected device
        # destinations. Script bodies are files, not systemd ExecStart strings.
        parent = str(Path(destination).parent)
        require(parent in (CONTROL, self.directory), "unexpected upload destination")
        self.remote("[ -d " + quote(parent) + " ] && [ ! -L " + quote(parent) + " ] && "
                    "[ \"$(stat -c '%u:%g:%a' " + quote(parent) + ")\" = 0:0:700 ]")
        with tempfile.NamedTemporaryFile(dir=self.artifacts, prefix=".push-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        try:
            completed = subprocess.run(["sdb", "-s", self.args.serial, "push", str(temporary), destination],
                                       capture_output=True, text=True, timeout=15)
            self.evidence("push-" + Path(destination).name + ".log", completed.stdout + completed.stderr)
            require(completed.returncode == 0, "SDB fixture push failed")
        finally:
            temporary.unlink()
        digest = hashlib.sha256(data).hexdigest()
        verify = ("import hashlib,os,stat,sys\n"
                  "fd=os.open(sys.argv[1],os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC)\n"
                  "with os.fdopen(fd,\"rb\") as source:\n"
                  " s=os.fstat(source.fileno())\n"
                  " if not stat.S_ISREG(s.st_mode) or (s.st_uid,s.st_gid,s.st_nlink,s.st_size)!=(0,0,1,int(sys.argv[2])): raise RuntimeError(\"unsafe upload\")\n"
                  " data=source.read(65537)\n"
                  " if len(data)!=int(sys.argv[2]) or hashlib.sha256(data).hexdigest()!=sys.argv[3]: raise RuntimeError(\"upload digest mismatch\")\n"
                  " os.fchmod(source.fileno(),0o600)\n")
        self.remote("python3 -c " + quote(verify) + " " + quote(destination) + " " +
                    str(len(data)) + " " + quote(digest))
        return destination

    def python_helper(self, name, content):
        # Keep Python bodies out of the multiply quoted SDB shell service name.
        # The small upload bootstrap uses no single quotes in its Python body.
        key = (name, content)
        if key not in self.helper_paths:
            require(re.fullmatch(r"[a-z-]+", name), "invalid helper name")
            path = CONTROL + "/.driver-" + name + "-" + secrets.token_hex(8) + ".py"
            self.helper_paths[key] = self.upload(path, content)
        return self.helper_paths[key]

    def push(self, filename, content):
        require(re.fullmatch(r"[A-Za-z0-9_.-]+", filename), "invalid generated file name")
        destination = self.upload(self.directory + "/" + filename, content)
        self.privileged("chsmack -a System " + quote(destination))
        return destination

    def fixture(self, name, options=None, parameters=None):
        require(re.fullmatch(r"[A-Za-z0-9-]+", name), "invalid fixture name")
        source = CONTROL + "/fixtures/" + name + ".ini"
        content = self.remote("[ -f " + quote(source) + " ] && [ ! -L " + quote(source) + " ] && "
                              "[ \"$(stat -c %u " + quote(source) + ")\" = 0 ] && "
                              "[ \"$(stat -c %s " + quote(source) + ")\" -le 65536 ] && cat " + quote(source))
        parser = configparser.ConfigParser(interpolation=None, strict=True)
        parser.optionxform = str
        parser.read_string(content)
        require(set(parser.sections()) == {"mock", "params"}, "unexpected fixture sections")
        tokens = self.state.get("tokens", {})
        for section in parser.sections():
            for key, value in list(parser[section].items()):
                for token, replacement in tokens.items():
                    require(IDENTIFIER.fullmatch(str(replacement)), "invalid returned fixture identifier")
                    value = value.replace("@" + token + "@", str(replacement))
                require(not re.search(r"@[A-Z][A-Z0-9_]*@", value), "unresolved fixture token in " + name)
                parser[section][key] = value
        for key in ("operation_id", "client_request_id"):
            if key in parser["params"] and name != "01-installer-register":
                parser["params"][key] += "-" + self.state["run"]
        parser["mock"].update(options or {})
        parser["params"].update(parameters or {})
        output = io.StringIO()
        output.write(content.split("[mock]", 1)[0])
        parser.write(output, space_around_delimiters=False)
        return self.push(name + ".ini", output.getvalue()), parser["mock"]["id"]

    def events(self, role, timeout=5):
        path = self.directory + "/" + role + ".jsonl"
        text = self.remote("[ -f " + quote(path) + " ] && [ ! -L " + quote(path) + " ] && "
                           "[ \"$(stat -c %s " + quote(path) + ")\" -le 1048576 ] && cat " + quote(path), timeout)
        events = []
        for line in text.splitlines():
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                # A concurrently appended last line may not have completed.
                require(line == text.splitlines()[-1] and not line.endswith("}"), "malformed actor JSONL")
                continue
            require(value.get("schema") == 1 and value.get("role") == role, "unexpected actor output")
            events.append(value)
        return events

    def wait_event(self, role, event, command_id=None, seconds=10):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for value in self.events(role, min(5, max(1, deadline - time.monotonic()))):
                if value.get("event") == "input-error":
                    raise RuntimeError("actor rejected its fixture; inspect JSONL evidence")
                if value.get("event") == event and (command_id is None or value.get("id") == command_id):
                    return value
                if event == "pending" and value.get("event") == "result" and value.get("id") == command_id:
                    raise RuntimeError("request completed before a real UI prompt: " + str(value.get("decision")))
            time.sleep(0.1)
        raise RuntimeError("timed out waiting for " + role + " " + event)

    def send(self, role, command, path=None):
        require(re.fullmatch(r"[a-z-]+", command), "invalid actor command")
        line = command if path is None else command + " " + path
        fifo = self.directory + "/" + role + ".fifo"
        writer = ("import os,stat,sys\n"
                  "path,line=sys.argv[1:]\n"
                  "if '\\n' in line or '\\r' in line: raise RuntimeError('invalid command line')\n"
                  "data=(line+'\\n').encode('utf-8')\n"
                  "fd=os.open(path,os.O_WRONLY|os.O_NONBLOCK|os.O_NOFOLLOW|os.O_CLOEXEC)\n"
                  "try:\n"
                  " s=os.fstat(fd)\n"
                  " if not stat.S_ISFIFO(s.st_mode) or (s.st_uid,s.st_gid,stat.S_IMODE(s.st_mode))!=(0,0,0o600): raise RuntimeError('unsafe FIFO')\n"
                  " if len(data)>=os.fpathconf(fd,'PC_PIPE_BUF'): raise RuntimeError('command exceeds atomic write limit')\n"
                  " if os.write(fd,data)!=len(data): raise RuntimeError('incomplete FIFO write')\n"
                  "finally: os.close(fd)\n")
        helper = self.python_helper("fifo", writer)
        self.remote("systemctl is-active --quiet " + quote(self.unit(role)) + " && python3 " +
                    quote(helper) + " " + quote(fifo) + " " + quote(line), timeout=6)

    def stop_actor(self, role):
        program = ("import subprocess,sys\n"
                   "unit=sys.argv[1]\n"
                   "result=subprocess.run(['systemctl','stop',unit],timeout=7)\n"
                   "state=subprocess.run(['systemctl','show','-p','LoadState','-p','MainPID',unit],capture_output=True,text=True,timeout=2)\n"
                   "fields=dict(line.split('=',1) for line in state.stdout.splitlines() if '=' in line)\n"
                   "absent=fields.get('LoadState')=='not-found' and fields.get('MainPID','0')=='0'\n"
                   "if not absent and (result.returncode or state.returncode or fields.get('MainPID')!='0'): raise RuntimeError('actor did not stop')\n")
        helper = self.python_helper("stop", program)
        self.remote("python3 " + quote(helper) + " " + quote(self.unit(role)), 12)

    @staticmethod
    def verify(value, decision=None):
        require(value.get("status") == 0 and value.get("fields", {}).get("matches_expectation") == "1",
                "mock command failed or did not match expectation")
        if decision is not None:
            require(value.get("decision") == decision, "unexpected consent decision")
        return value.get("fields", {})

    def execute(self, role, command, fixture, options=None):
        path, command_id = self.fixture(fixture, options)
        if role in ACTORS:
            self.send(role, command, path)
            value = self.wait_event(role, "result", command_id)
        else:
            unit = "consent-poc-call-" + secrets.token_hex(8)
            output = self.remote("systemd-run --quiet --wait --pipe --unit=" + unit +
                                 " -p User=root -p Group=root -p SmackProcessLabel=System"
                                 " -p TimeoutStartSec=8 -p RuntimeMaxSec=8 " + quote(TOOLS + "/consent-mock-" + role) +
                                 " " + quote(command) + " " + quote(path), 12)
            values = [json.loads(line) for line in output.splitlines() if line.startswith("{")]
            results = [item for item in values if item.get("event") == "result" and item.get("id") == command_id]
            require(len(results) == 1, "missing or duplicate mock result")
            value = results[0]
        self.verify(value)
        return value.get("fields", {})

    def start(self):
        require(not self.state_path.exists(), "use a fresh artifact directory for each PoC run")
        self.state = {"serial": self.args.serial, "run": secrets.token_hex(8), "expect": self.args.expect,
                      "locale": self.args.locale, "phase": "preparing", "tokens": {}}
        self.save()
        self.remote("[ \"$(id -u)\" = 0 ] && command -v python3 && "
                    "systemctl is-active --quiet consentd-poc.service")
        self.privileged("[ -d " + CONTROL + " ] && [ ! -L " + CONTROL + " ]\n"
                        "[ \"$(stat -c '%u:%g:%a' " + CONTROL + ")\" = 0:0:700 ]\n"
                        "if [ ! -e " + CONTROL + "/runs ] && [ ! -L " + CONTROL + "/runs ]; then mkdir -m 0700 " + CONTROL + "/runs; fi\n"
                        "[ -d " + CONTROL + "/runs ] && [ ! -L " + CONTROL + "/runs ]\n"
                        "[ \"$(stat -c '%u:%g:%a' " + CONTROL + "/runs)\" = 0:0:700 ]\n"
                        "mkdir -m 0700 " + quote(self.directory) + "\n"
                        "chsmack -a System " + CONTROL + " " + CONTROL + "/runs " + quote(self.directory))
        for role in ACTORS:
            fifo = self.directory + "/" + role + ".fifo"
            log = self.directory + "/" + role + ".jsonl"
            self.privileged("umask 077\nmkfifo -m 0600 " + quote(fifo) + "\n: > " + quote(log) +
                            "\nchsmack -a System " + quote(fifo) + " " + quote(log))
            shell = ("exec 3<>" + quote(fifo) + "; exec " + quote(TOOLS + "/consent-mock-" + role) +
                     " serve <&3 >>" + quote(log) + " 2>&1")
            script = self.push(role + "-serve.sh", "#!/bin/sh\nset -eu\n" + shell + "\n")
            self.remote("systemd-run --quiet --unit=" + quote(self.unit(role)) +
                        " -p Type=simple -p User=root -p Group=root -p SmackProcessLabel=System"
                        " -p TimeoutStopSec=5 -p KillMode=control-group /bin/sh " + quote(script))
            self.wait_event(role, "ready")
        self.execute("installer", "register", "01-installer-register")
        self.execute("cm", "query", "02-cm-query-before")
        path, command_id = self.fixture("03-argo-request", {"expect_decision": self.args.expect})
        self.send("argo", "request", path)
        pending = self.wait_event("argo", "pending", command_id)
        request_id = pending.get("request_id", "")
        require(IDENTIFIER.fullmatch(request_id), "pending response has no valid request ID")
        self.state.update(request_id=request_id, phase="awaiting-human", request_command=command_id)
        self.save()
        self.remote("systemd-run --quiet --wait --pipe --unit=consent-poc-launch-" + secrets.token_hex(8) +
                    " -p User=owner -p Group=users -p SmackProcessLabel=System::Privileged"
                    " -p TimeoutStartSec=10 -p RuntimeMaxSec=10 " +
                    quote(TOOLS + "/consent-poc-launch") + " org.tizen.consentui " + quote(request_id) +
                    " " + quote(self.args.locale), 15)
        self.collect()
        print(json.dumps({"phase": "awaiting-human", "request_id": request_id, "expected": self.args.expect,
                          "locale": self.args.locale, "instruction": "Choose the response in the native UI; no automatic approval was sent."}))

    def finish(self):
        self.load()
        require(self.state["expect"] == "ALLOWED" and self.state["phase"] == "awaiting-human",
                "finish-allow requires an unfinished ALLOWED scenario")
        result = self.wait_event("argo", "result", self.state["request_command"])
        self.verify(result, "ALLOWED")
        self.execute("cm", "query", "04-cm-query-approved")
        opened = self.execute("argo", "session-open", "05-argo-session-open")
        self.state["tokens"].update(SESSION=opened["session"], SESSION_GENERATION=opened["generation"])
        self.state["phase"] = "holder-flow"
        self.save()
        acquired = self.execute("ce", "authorize", "06-ce-authorize")
        self.state["tokens"]["RECEIPT"] = acquired["receipt"]
        retried = self.execute("ce", "authorize", "07-ce-authorize-retry")
        require(retried.get("receipt") == acquired["receipt"] and retried.get("retry") == "1", "ONCE retry changed acquisition")
        self.execute("ce", "authorize", "08-ce-once-consumed")
        original = self.execute("holder", "data-register", "09-holder-register")
        self.state["tokens"]["ARTIFACT"] = original["artifact"]
        derived = self.execute("holder", "derive", "10-holder-derive")
        self.state["tokens"]["DERIVED_ARTIFACT"] = derived["artifact"]
        require(int(derived["expires"]) <= int(original["expires"]), "derived artifact extended retention")
        self.save()
        self.execute("holder", "reuse", "11-holder-reuse")
        closed = self.execute("argo", "session-close", "12-argo-session-close")
        require(closed.get("state") == "CLOSING", "session closed without holder cleanup")
        pending = self.execute("holder", "cleanup-list", "13-holder-cleanup-list")
        require(pending.get("count") == "2" and all(pending.get("a%d.state" % index) == "CLEANUP_PENDING" for index in range(2)),
                "holder cleanup metadata did not retain both artifacts")
        for name, remaining in (("14-holder-release-derived", "1"), ("15-holder-release-original", "0")):
            released = self.execute("holder", "release", name)
            require(released.get("state") == "DELETED" and released.get("resident_artifacts") == remaining,
                    "holder did not clear its real fixture buffers before ACK")
        final = self.execute("argo", "session-state", "16-argo-session-state")
        require(final.get("state") == "CLOSED" and final.get("cleanup_pending") == "0", "cleanup did not complete")
        self.state["phase"] = "verified"
        self.save()
        self.collect()
        print("PASS real UI ALLOWED, QUERY, ONCE AUTHORIZE/retry/exhaustion, holder provenance/reuse/cleanup")

    def collect(self):
        if not self.state:
            return
        for role in ACTORS:
            try:
                path = self.directory + "/" + role + ".jsonl"
                output = self.remote("if [ -f " + quote(path) + " ] && [ ! -L " + quote(path) +
                                     " ]; then tail -c 1048576 " + quote(path) + "; fi", 5)
                self.evidence(role + ".jsonl", output + "\n")
            except (RuntimeError, subprocess.SubprocessError) as error:
                self.evidence(role + "-collect-error.txt", str(error) + "\n")
        try:
            output = self.remote("journalctl -u consentd-poc.service -n 200 --no-pager -o cat", 5)
            self.evidence("consentd-poc.log", output + "\n")
        except (RuntimeError, subprocess.SubprocessError) as error:
            self.evidence("journal-collect-error.txt", str(error) + "\n")

    def stop(self):
        self.load()
        failures = []
        if self.state["phase"] == "awaiting-human" and self.state["expect"] == "DENIED":
            try:
                result = self.wait_event("argo", "result", self.state["request_command"])
                self.verify(result, "DENIED")
                self.execute("cm", "query", "02-cm-query-before")
                self.state["phase"] = "verified-denial"
            except (RuntimeError, subprocess.SubprocessError) as error:
                failures.append(str(error))
        for role in ACTORS:
            try:
                # Explicit stop detaches pending work when systemd terminates
                # after the bounded grace period; it never supplies a UI answer.
                self.send(role, "quit")
            except (RuntimeError, subprocess.SubprocessError) as error:
                self.evidence(role + "-quit-diagnostic.txt", str(error) + "\n")
            try:
                # An inactive/missing actor has no FIFO reader. Only the
                # bounded final process-state verification determines failure.
                self.stop_actor(role)
            except (RuntimeError, subprocess.SubprocessError) as error:
                failures.append(role + ": " + str(error))
        self.collect()
        self.state["stopped"] = not failures
        self.save()
        require(not failures, "stop completed with failures: " + "; ".join(failures))
        print("PASS PoC actors stopped; private logs retained in " + str(self.artifacts))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="explicit development emulator SDB serial")
    parser.add_argument("--artifact-dir", required=True, help="private evidence/state directory, reused by finish/stop")
    commands = parser.add_subparsers(dest="command", required=True)
    start = commands.add_parser("start-request")
    start.add_argument("--expect", choices=("ALLOWED", "DENIED"), required=True)
    start.add_argument("--locale", choices=("en-US", "ko-KR"), required=True)
    commands.add_parser("finish-allow")
    commands.add_parser("stop")
    arguments = parser.parse_args()
    driver = Driver(arguments)
    try:
        if arguments.command == "start-request":
            driver.start()
        elif arguments.command == "finish-allow":
            driver.finish()
        else:
            driver.stop()
        return 0
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        driver.collect()
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
