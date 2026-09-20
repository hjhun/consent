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

"""Compile and development-sign the PoC UI using the GBS-installed SDK."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zipfile


VERSION = "0.1.0"
FRAMEWORK = "net8.0-tizen10.1"
SDK_ROOT = Path("/usr/share/dotnet-build-tools/sdk")


def run(command, cwd, env, log_path):
    # Signing uses only SDK-provided development defaults. Never pass signing
    # credentials on the command line or inherit user certificate profiles.
    limit = 2 * 1024 * 1024
    deadline = time.monotonic() + 600
    sensitive = re.compile(r"password|authorpass|distributorpass|private.?key|"
                           r"passphrase|credential|secret", re.IGNORECASE)
    pending = bytearray()
    total = 0
    with log_path.open("w", encoding="utf-8") as log, selectors.DefaultSelector() as reader:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        completed = False
        try:
            os.set_blocking(process.stdout.fileno(), False)
            reader.register(process.stdout, selectors.EVENT_READ)
            while reader.get_map():
                if time.monotonic() >= deadline:
                    raise subprocess.TimeoutExpired(command, 600)
                for key, _ in reader.select(timeout=1):
                    chunk = os.read(key.fd, 4096)
                    if not chunk:
                        reader.unregister(key.fileobj)
                        continue
                    total += len(chunk)
                    if total > limit:
                        raise ValueError("dotnet output exceeded the bounded build log")
                    pending.extend(chunk)
                    if len(pending) > 65536 and b"\n" not in pending:
                        raise ValueError("dotnet emitted an oversized log line")
                    while b"\n" in pending:
                        line, _, rest = pending.partition(b"\n")
                        pending = bytearray(rest)
                        text = line.decode("utf-8", errors="replace")
                        if sensitive.search(text):
                            text = "[credential-related build output omitted]"
                        print(text, flush=True)
                        log.write(text + "\n")
            if pending:
                text = pending.decode("utf-8", errors="replace")
                if sensitive.search(text):
                    text = "[credential-related build output omitted]"
                print(text, flush=True)
                log.write(text + "\n")
            status = process.wait(timeout=max(0.01, deadline - time.monotonic()))
            completed = True
            if status:
                raise subprocess.CalledProcessError(status, command)
        finally:
            if not completed:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if process.poll() is None:
                process.wait()
            process.stdout.close()


def stage_sources(source, work, include_tests=False):
    stage = work / "source"
    if stage.exists():
        if not (work / ".consent-poc-build").is_file():
            raise ValueError("refusing to replace an unmarked build directory")
        shutil.rmtree(stage)
    work.mkdir(parents=True, exist_ok=True)
    (work / ".consent-poc-build").write_text("consent-poc-source-build-v1\n")
    stage.mkdir()
    copied = []
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if any(part in {"bin", "obj", ".git"} for part in relative.parts):
            continue
        if not include_tests and "tests" in relative.parts:
            continue
        if not path.is_file():
            continue
        if path.is_symlink():
            raise ValueError("UI input must not be a symbolic link")
        if not (path.suffix in {".cs", ".csproj"} or
                relative.as_posix() in {"tizen-manifest.xml",
                                        "negative/tizen-manifest.xml"} or
                relative.parts[0] == "res"):
            continue
        if path.suffix.lower() in {".dll", ".pdb", ".so", ".tpk", ".exe"}:
            raise ValueError("precompiled UI inputs are forbidden")
        destination = stage / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        copied.append({"path": relative.as_posix(),
                       "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    return stage, copied


def verify_tpk(path, manifest, package, assembly):
    namespace = {"m": "http://tizen.org/ns/packages"}
    root = ET.fromstring(manifest)
    application = root.find("m:ui-application", namespace)
    if (root.attrib.get("package") != package or
            root.attrib.get("version") != VERSION or
            root.attrib.get("api-version") != "10.1" or
            application is None or application.attrib.get("appid") != package or
            application.attrib.get("exec") != assembly + ".dll" or
            application.attrib.get("api-version") != "14" or
            application.attrib.get("type") != "dotnet"):
        raise ValueError("unexpected PoC manifest identity/version/entry point")
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or archive.testzip() is not None:
            raise ValueError("TPK contains duplicate names or invalid CRC")
        for name in names:
            parts = Path(name).parts
            if name.startswith("/") or ".." in parts:
                raise ValueError("TPK contains an unsafe archive path")
        required = {"tizen-manifest.xml", "bin/" + assembly + ".dll",
                    "author-signature.xml", "signature1.xml"}
        if not required.issubset(names):
            raise ValueError("TPK is missing the compiled UI or signatures")
        if archive.read("tizen-manifest.xml") != manifest:
            raise ValueError("packaging changed the reviewed manifest")
        for name in ("author-signature.xml", "signature1.xml"):
            signature = ET.fromstring(archive.read(name))
            ds = {"ds": "http://www.w3.org/2000/09/xmldsig#"}
            if signature.find("ds:SignatureValue", ds) is None:
                raise ValueError("TPK signature element is missing")
    # ZIP structure is checked here. The Tizen installer must verify certificate
    # trust and XML signatures on the development target before runtime claims.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--project", type=Path, default=Path("ConsentUI.csproj"))
    parser.add_argument("--package", default="org.tizen.consentui")
    parser.add_argument("--assembly", default="ConsentUI")
    parser.add_argument("--tizen-net-version", default="14.0.0.19364")
    parser.add_argument("--run-tests", action=argparse.BooleanOptionalAction,
                        default=None, help="run managed unit tests (default: main UI only)")
    args = parser.parse_args()
    source = args.source_dir.resolve(strict=True)
    work = args.work_dir.resolve()
    output = args.output.resolve()
    if source == work or source in work.parents or work in source.parents:
        raise ValueError("source and build directories must be separate")
    if args.project.is_absolute() or ".." in args.project.parts:
        raise ValueError("project must be a relative path inside the UI sources")
    contracts = {"org.tizen.consentui": ("ConsentUI.csproj", "ConsentUI"),
                 "org.tizen.consentui.negative":
                     ("negative/ConsentUINegative.csproj", "ConsentUINegative")}
    if contracts.get(args.package) != (args.project.as_posix(), args.assembly):
        raise ValueError("unrecognized PoC project/package/assembly combination")
    if not (SDK_ROOT / "dotnet").is_file():
        raise ValueError("GBS dotnet-build-tools SDK is required")
    nuget = Path("/nuget")
    for name in ("Tizen.NET", "Tizen.NET.API14"):
        package = nuget / (name + "." + args.tizen_net_version + ".nupkg")
        if not package.is_file():
            raise ValueError("GBS NuGet input is missing: " + package.name)
    run_tests = args.run_tests if args.run_tests is not None else args.package == "org.tizen.consentui"
    stage, sources = stage_sources(source, work, include_tests=run_tests)
    project = stage / args.project
    if not project.is_file():
        raise ValueError("PoC project is missing")
    (work / "global.json").write_text(json.dumps({
        "sdk": {"version": "8.0.421", "rollForward": "disable"}}) + "\n")
    config = work / "NuGet.Config"
    config.write_text('<configuration><packageSources><clear />'
                      '<add key="gbs" value="/nuget" />'
                      '</packageSources></configuration>\n')
    env = os.environ.copy()
    env.update({"DOTNET_CLI_TELEMETRY_OPTOUT": "1",
                "DOTNET_SKIP_FIRST_TIME_EXPERIENCE": "1",
                "DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE": "true",
                "MSBUILDDISABLENODEREUSE": "true",
                "NUGET_PACKAGES": str(work / "nuget-packages")})
    for name in ("AuthorPath", "AuthorPass", "DistributorPath", "DistributorPass",
                 "DistributorPath2", "DistributorPass2"):
        env.pop(name, None)
    properties = ["-p:TargetFramework=" + FRAMEWORK,
                  "-p:TizenNetVersion=" + args.tizen_net_version,
                  "-p:Deterministic=true", "-p:ContinuousIntegrationBuild=true",
                  "-p:PathMap=" + str(stage) + "=/src/consent-ui",
                  "-p:UseSharedCompilation=false"]
    # Use the exact RPM SDK directly: the /usr/bin wrapper mutates a shared
    # NuGet configuration/cache, which would race the two parallel UI builds.
    dotnet = str(SDK_ROOT / "dotnet")
    run([dotnet, "restore", str(project), "--configfile", str(config),
         "--disable-parallel", "--verbosity", "minimal", *properties], work, env,
        work / "ui-restore.log")
    run([dotnet, "build", str(project), "--no-restore", "--nologo",
         "-c", "Release", "--verbosity", "minimal",
         "-p:TizenCreateTpkOnBuild=true", *properties], work, env, work / "ui-build.log")
    tpk = (project.parent / "bin" / "Release" / FRAMEWORK /
           (args.package + "-" + VERSION + ".tpk"))
    verify_tpk(tpk, (project.parent / "tizen-manifest.xml").read_bytes(),
               args.package, args.assembly)
    test_evidence = {"executed": False}
    if run_tests:
        test_project = stage / "tests" / "ConsentUI.Tests.csproj"
        if not test_project.is_file():
            raise ValueError("the managed regression test project is missing")
        test_env = dict(env, NUGET_PACKAGES=str(work / "test-nuget-packages"))
        test_properties = ["-p:TargetFramework=net8.0", "-p:Deterministic=true",
                           "-p:ContinuousIntegrationBuild=true",
                           "-p:PathMap=" + str(stage) + "=/src/consent-ui",
                           "-p:UseSharedCompilation=false"]
        run([dotnet, "restore", str(test_project), "--configfile", str(config),
             "--disable-parallel", "--verbosity", "minimal", *test_properties],
            work, test_env, work / "tests-restore.log")
        run([dotnet, "build", str(test_project), "--no-restore", "--nologo",
             "-c", "Release", "--verbosity", "minimal", *test_properties],
            work, test_env, work / "tests-build.log")
        test_dll = test_project.parent / "bin/Release/net8.0/ConsentUI.Tests.dll"
        run([dotnet, str(test_dll)], work, test_env, work / "tests-run.log")
        test_evidence = {"executed": True, "exit_code": 0, "framework": "net8.0",
                         "assembly_sha256": hashlib.sha256(test_dll.read_bytes()).hexdigest(),
                         "output_sha256": hashlib.sha256(
                             (work / "tests-run.log").read_bytes()).hexdigest()}
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tpk.tmp")
    shutil.copyfile(tpk, temporary)
    os.replace(temporary, output)
    evidence = {"format": 1, "framework": FRAMEWORK,
                "tizen_net_version": args.tizen_net_version,
                "project": args.project.as_posix(), "package": args.package,
                "compiler": str(SDK_ROOT / "dotnet"),
                "signer": "GBS Samsung.Tizen.Sdk development defaults",
                "sources": sources,
                "managed_tests": test_evidence,
                "tpk_sha256": hashlib.sha256(output.read_bytes()).hexdigest()}
    output.with_suffix(".build.json").write_text(
        json.dumps(evidence, sort_keys=True, indent=2) + "\n")
    print("PASS GBS source compilation and development TPK packaging:", output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ET.ParseError, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        print("PoC TPK build failed:", error, file=sys.stderr)
        sys.exit(1)
