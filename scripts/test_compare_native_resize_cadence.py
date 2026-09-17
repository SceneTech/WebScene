"""Keep headless callback measurements separate from physical presentation gates."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("resize_compare", Path(__file__).with_name("compare-native-resize-cadence.py"))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class ResizeMeasurementScopeTests(unittest.TestCase):
    def sample(self):
        sample = {"schema": "webscene-native-resize-cadence-v3",
                  "sourceKind": "deterministic-fixture", "sourceIdentity": "sha256:fixture",
                  "waveform": "triangle-v1", "baseWidth": 1180, "baseHeight": 720,
                  "widthSpan": 24, "heightSpan": 30,
                  "resizeBoundsSpace": "avalonia-headless-window",
                  "measurementScope": "headless-cpu-draw-callback",
                  "composition": True, "certificationTelemetryEnabled": False,
                  "requestedHz": 60, "warmupSeconds": 2, "requestedSeconds": 10,
                  "submitted": 600,
                  "cpuCadenceGate": {"passed": True},
                  "renderedFramesPerSecond": 60, "drawCallbackCompletionsPerSecond": 60,
                  "normalizedProcessCpuPercent": 10, "layoutPassesPerAppliedResize": 1,
                  "dispatchMilliseconds": {"average": 1}}
        for name in ["renderLatencyMilliseconds", "publicationLatencyMilliseconds",
                     "publicationToRenderLatencyMilliseconds", "drawCallbackIntervalMilliseconds"]:
            sample[name] = {"p95": 1, "maximum": 2}
        return sample

    def test_legacy_ambiguous_measurement_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for schema in ["webscene-native-resize-cadence-v1", "webscene-native-resize-cadence-v2"]:
                with self.subTest(schema=schema):
                    (root / "sample.json").write_text(json.dumps({"schema": schema}))
                    with self.assertRaises(RuntimeError):
                        compare.read_samples(root, 1)

    def test_good_cpu_cadence_cannot_pass_physical_vsync(self):
        sample = self.sample()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ["control", "candidate"]:
                (root / name).mkdir()
                (root / name / "sample.json").write_text(json.dumps(sample))
            output = root / "comparison.json"
            args = ["compare", "--control-dir", str(root / "control"),
                    "--candidate-dir", str(root / "candidate"), "--minimum-samples", "1",
                    "--output", str(output), "--require-vsync"]
            with patch.object(sys, "argv", args), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(), 1)
            report = json.loads(output.read_text())
            self.assertFalse(report["passed"])
            self.assertFalse(report["physicalPresentationVerified"])
            self.assertEqual(report["candidateCpuCadencePasses"], 1)

    def test_unmatched_or_unidentified_workloads_are_rejected(self):
        for field in ["sourceIdentity", "waveform", "baseWidth", "baseHeight",
                      "widthSpan", "heightSpan", "certificationTelemetryEnabled",
                      "resizeBoundsSpace", "measurementScope"]:
            for missing in [False, True]:
                with self.subTest(field=field, missing=missing), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    baseline = self.sample()
                    candidate = self.sample()
                    if missing:
                        # Missing on both sides must not compare equal as None.
                        del baseline[field]
                        del candidate[field]
                    else:
                        candidate[field] = "different"
                    for name, sample in [("control", baseline), ("candidate", candidate)]:
                        (root / name).mkdir()
                        (root / name / "sample.json").write_text(json.dumps(sample))
                    args = ["compare", "--control-dir", str(root / "control"),
                            "--candidate-dir", str(root / "candidate"), "--minimum-samples", "1",
                            "--output", str(root / "out.json")]
                    with patch.object(sys, "argv", args), self.assertRaises(RuntimeError):
                        compare.main()


if __name__ == "__main__":
    unittest.main()
