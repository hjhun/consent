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
"""Explicit-emulator feature settings workflow; all approvals remain human UI actions."""

import argparse
import importlib.util
import json
from pathlib import Path
import secrets
import sys

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("consent_poc_mocks", HERE / "emulator-poc-mocks.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FeatureDriver(module.Driver):
    def load(self):
        module.require(self.state_path.is_file() and not self.state_path.is_symlink(), "no saved feature run")
        module.require(self.state_path.stat().st_size <= 65536, "oversized saved state")
        self.state = json.loads(self.state_path.read_text(encoding="utf-8"))
        module.require(self.state.get("serial") == self.args.serial, "saved run belongs to another emulator")
        module.require(self.state.get("kind") == "feature-settings", "not a feature workflow state")
        module.require(self.state.get("locale") in ("en-US", "ko-KR"), "invalid saved locale")

    def setup(self, phase):
        path = module.CONTROL + "/.feature-setup-" + self.state["run"] + ".sh"
        if phase == "start":
            self.upload(path, (HERE / "emulator-feature-setup.sh").read_text(encoding="utf-8"))
        self.remote("systemd-run --quiet --wait --pipe --unit=consent-feature-setup-" + secrets.token_hex(8) +
                    " -p User=root -p Group=root -p SmackProcessLabel=System::Privileged"
                    " -p TimeoutStartSec=40 -p RuntimeMaxSec=40 /bin/sh " + module.quote(path) + " " + phase, 45)

    def launch(self):
        self.remote("systemd-run --quiet --wait --pipe --unit=consent-feature-launch-" + secrets.token_hex(8) +
                    " -p User=owner -p Group=users -p SmackProcessLabel=System::Privileged"
                    " -p TimeoutStartSec=10 -p RuntimeMaxSec=10 " +
                    module.quote(module.TOOLS + "/consent-poc-launch") +
                    " org.tizen.consentui --settings " + module.quote(self.state["locale"]), 15)

    def start(self):
        module.require(not self.state_path.exists(), "use a fresh artifact directory")
        self.state = {"kind": "feature-settings", "serial": self.args.serial,
                      "run": secrets.token_hex(8), "locale": self.args.locale, "phase": "preparing"}
        self.save()
        self.remote('[ "$(id -u)" = 0 ] && command -v python3')
        self.setup("start")
        self.state["phase"] = "awaiting-human"
        self.save()
        self.launch()
        self.collect()
        print(json.dumps({"phase": "awaiting-human", "steps": [
            "Confirm all features initially unselected; a selected-task run must be denied.",
            "Select calendar.read and SESSION, review every settings page, then approve the native consent popup.",
            "Run calendar-summary twice; second use must reuse the same conversation artifact.",
            "Select both features and save; the missing device permission is separately displayed with its exact target and impact.",
            "Run calendar-expanded with explicit task-only ONCE and deny the wider request; no wider action may start.",
            "Run calendar-alternative; only the existing narrow artifact may be used.",
            "Clear all selected features; ordinary task use must fail. Close the conversation and confirm CLOSED with cleanup_pending=0."],
            "approval_automation": False}, ensure_ascii=False, indent=2))

    def collect(self):
        for unit in ("consent-feature-poc.service", "consentd-poc.service"):
            output = self.remote("journalctl --no-pager -u " + unit + " -n 500 -o cat", 15)
            module.require(len(output.encode("utf-8")) <= 1048576, "journal evidence exceeds bound")
            self.evidence(unit + ".log", output + "\n")
        status = self.remote("systemctl show consent-feature-poc.service -p ActiveState -p MainPID -p ExecMainStatus")
        self.evidence("service-state.txt", status + "\n")
        print(json.dumps({"phase": self.state.get("phase"), "artifacts": str(self.artifacts),
                          "note": "Logs do not assert a human choice; inspect actual request and action evidence."}))

    def stop(self):
        # User should first choose conversation-close and observe cleanup ACKs.
        # Service stop is bounded transport teardown, never proof of an ACK.
        self.setup("stop")
        self.state["phase"] = "stopped"
        self.save()
        self.collect()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--artifact-dir", required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    start = commands.add_parser("start")
    start.add_argument("--locale", choices=("en-US", "ko-KR"), required=True)
    for command in ("reopen", "collect", "stop"):
        commands.add_parser(command)
    arguments = parser.parse_args()
    driver = FeatureDriver(arguments)
    if arguments.command == "start":
        driver.start()
    else:
        driver.load()
        if arguments.command == "reopen":
            driver.launch()
            driver.collect()
        else:
            getattr(driver, arguments.command)()


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, module.subprocess.SubprocessError) as error:
        print("feature flow failed: " + str(error), file=sys.stderr)
        sys.exit(1)
