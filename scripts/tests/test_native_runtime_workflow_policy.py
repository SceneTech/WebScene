#!/usr/bin/env python3
"""Regression checks for isolated self-hosted .NET toolchains."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = {
    ROOT / ".github/workflows/CI.yml": 2,
    ROOT / ".github/workflows/native-runtime-packages.yml": 5,
}
HOSTED_WORKFLOWS = {ROOT / ".github/workflows/docs.yml"}


class NativeRuntimeWorkflowPolicyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.workflows = {
            path: path.read_text(encoding="utf-8")
            for path in WORKFLOWS
        }

    def test_every_self_hosted_dotnet_setup_uses_the_job_temp_directory(self) -> None:
        for path, expected_count in WORKFLOWS.items():
            lines = self.workflows[path].splitlines()
            setup_indices = [
                index
                for index, line in enumerate(lines)
                if line.strip().removeprefix("- ") == "uses: actions/setup-dotnet@v5"
            ]
            self.assertEqual(len(setup_indices), expected_count, path)

            for index in setup_indices:
                step = "\n".join(lines[index : index + 7])
                self.assertIn("env:", step, path)
                self.assertIn(
                    "DOTNET_INSTALL_DIR: ${{ runner.temp }}/dotnet",
                    step,
                    path,
                )
                self.assertIn("dotnet-version: 8.0.x", step, path)
                self.assertIn("global-json-file: global.json", step, path)

    def test_every_dotnet_workflow_has_an_explicit_runner_policy(self) -> None:
        setup_workflows = {
            path
            for path in (ROOT / ".github/workflows").glob("*.yml")
            if "actions/setup-dotnet@v5" in path.read_text(encoding="utf-8")
        }
        self.assertEqual(setup_workflows, set(WORKFLOWS) | HOSTED_WORKFLOWS)
        for path in HOSTED_WORKFLOWS:
            self.assertIn("runs-on: ubuntu-latest", path.read_text(encoding="utf-8"))

    def test_policy_test_runs_before_dotnet_setup(self) -> None:
        policy = "scripts/tests/test_native_runtime_workflow_policy.py"
        package_workflow = self.workflows[
            ROOT / ".github/workflows/native-runtime-packages.yml"
        ]
        first_setup = package_workflow.index("uses: actions/setup-dotnet@v5")
        self.assertLess(
            package_workflow.index(policy, package_workflow.index("jobs:")),
            first_setup,
        )
        self.assertIn(f"- '{policy}'", package_workflow)

    def test_candidate_gaps_are_advisory_without_masking_job_failures(self) -> None:
        package_workflow = self.workflows[
            ROOT / ".github/workflows/native-runtime-packages.yml"
        ]
        candidate = package_workflow.split("\n  candidate-evidence:\n", 1)[1].split(
            "\n  consumer:\n", 1
        )[0]
        self.assertNotIn("\n    continue-on-error: true", candidate)
        self.assertIn("--advisory-test-failures", candidate)


if __name__ == "__main__":
    unittest.main()
