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
"""Executable schema rejection and deterministic Parcel generation tests."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "src/tools/parcel_codegen.py"
IDL = ROOT / "src/protocol/consent.idl.json"
spec = importlib.util.spec_from_file_location("parcel_codegen", COMPILER)
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


class CodegenTest(unittest.TestCase):
    def setUp(self):
        self.schema = json.loads(IDL.read_text(encoding="utf-8"))

    def test_deterministic_output_and_atomic_rejection(self):
        with tempfile.TemporaryDirectory(prefix="consent-idl-test-") as directory:
            output = Path(directory) / "wire.hh"
            command = [sys.executable, str(COMPILER), str(IDL), str(output)]
            subprocess.run(command, check=True)
            expected, stamp = output.read_bytes(), output.stat().st_mtime_ns
            subprocess.run(command, check=True)
            self.assertEqual(expected, output.read_bytes())
            self.assertEqual(stamp, output.stat().st_mtime_ns)
            text = expected.decode("utf-8")
            self.assertIn("class Envelope final : public tizen_base::Parcelable", text)
            self.assertIn("ReadFromParcel(tizen_base::Parcel* parcel) override", text)
            invalid = Path(directory) / "bad.json"
            invalid.write_text('{"namespace":"one", "namespace":"two"}')
            result = subprocess.run([sys.executable, str(COMPILER), str(invalid),
                                     str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(expected, output.read_bytes())

    def test_duplicate_names_and_json_keys(self):
        duplicate = copy.deepcopy(self.schema["records"][0])
        self.schema["records"].append(duplicate)
        with self.assertRaises(ValueError):
            compiler.generate(self.schema)
        with self.assertRaises(ValueError):
            json.loads('{"x":1,"x":2}', object_pairs_hook=compiler.unique_object)
        schema = json.loads(IDL.read_text())
        schema["records"][0]["fields"].append(copy.deepcopy(schema["records"][0]["fields"][0]))
        with self.assertRaises(ValueError):
            compiler.generate(schema)

    def test_reject_bad_identifiers_types_and_bounds(self):
        mutations = [
            lambda s: s.update(namespace="x; system(1)"),
            lambda s: s["records"][0].update(name="class"),
            lambda s: s["records"][0].update(name="Reader"),
            lambda s: s["records"][0].update(name="Writer"),
            lambda s: s["records"][0].update(name="Valid"),
            lambda s: s["records"][0].update(name="ValidString"),
            lambda s: s["records"][0].update(name="WriteToParcel"),
            lambda s: s["records"][0].update(name="ReadFromParcel"),
            lambda s: s["records"][0]["fields"][0].update(name="__hidden"),
            lambda s: s["records"][0]["fields"][0].update(name="uint32_t"),
            lambda s: s["records"][0]["fields"][0].update(type="void*"),
            lambda s: s["records"][0]["fields"][0].update(type="Field"),
            lambda s: s["records"][0]["fields"][0].pop("max_bytes"),
            lambda s: s["records"][0]["fields"][0].update(max_bytes=-1),
            lambda s: s["records"][0]["fields"][0].update(max_bytes=True),
            lambda s: s["records"][0]["fields"][0].update(min_bytes=999),
            lambda s: s["records"][1]["fields"][-1].pop("max_count"),
            lambda s: s["records"][1]["fields"][-1].update(max_count=4097),
            lambda s: s["records"][1]["fields"][-1].update(element="Missing"),
            lambda s: s["records"][1]["fields"][0].update(default=-1),
            lambda s: s["records"][1]["fields"][0].update(default=True),
            lambda s: s["records"][1]["fields"][0].update(execute="anything"),
        ]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                schema = copy.deepcopy(self.schema)
                mutate(schema)
                with self.assertRaises(ValueError):
                    compiler.generate(schema)

    def test_transitive_size_and_array_bounds(self):
        schema = copy.deepcopy(self.schema)
        schema["records"] = [{"name": "Leaf", "fields": [{"name": "value", "type": "u64"}]}]
        for index in range(40):
            previous = schema["records"][-1]["name"]
            schema["records"].append({"name": "Branch" + str(index), "fields": [
                {"name": "left", "type": previous},
                {"name": "right", "type": previous}]})
        with self.assertRaisesRegex(ValueError, "transitive"):
            compiler.generate(schema)
        schema["records"] = [{"name": "Leaf", "fields": [
            {"name": "value", "type": "string", "max_bytes": 65536}]},
            {"name": "Many", "fields": [
                {"name": "values", "type": "array", "element": "Leaf", "max_count": 4096}]}]
        with self.assertRaisesRegex(ValueError, "transitive"):
            compiler.generate(schema)
        layout = compiler.validate(self.schema)
        self.assertEqual(layout["Field"]["minimum"], 11)
        text = compiler.generate(self.schema)
        check = text.index("count_fields > (parcel->GetDataSize() - parcel->GetReader()) / 11")
        self.assertLess(check, text.index("decoded.fields.resize(count_fields)"))

    def test_license_metadata(self):
        for mutate in [lambda s: s.pop("license"),
                       lambda s: s["license"].update(spdx="Unknown"),
                       lambda s: s["license"].update(notice="Apache-2.0")]:
            schema = copy.deepcopy(self.schema)
            mutate(schema)
            with self.assertRaises(ValueError):
                compiler.generate(schema)


if __name__ == "__main__":
    unittest.main()
