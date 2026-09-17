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
                preceding_step = "\n".join(lines[max(0, index - 7) : index])
                self.assertIn("Repair incomplete .NET install", preceding_step, path)
                self.assertIn(
                    'python3 scripts/prepare_dotnet_install.py "$DOTNET_INSTALL_DIR"',
                    preceding_step,
                    path,
                )
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

    def test_candidate_evidence_skips_only_incomplete_prerequisites(self) -> None:
        package_workflow = self.workflows[
            ROOT / ".github/workflows/native-runtime-packages.yml"
        ]
        candidate = package_workflow.split("\n  candidate-evidence:\n", 1)[1].split(
            "\n  consumer:\n", 1
        )[0]
        condition = next(
            line.strip()
            for line in candidate.splitlines()
            if line.strip().startswith("if:")
        )

        self.assertIn("always()", condition)
        self.assertIn("needs.metadata.result == 'success'", condition)
        self.assertIn("needs.native.result != 'cancelled'", condition)
        self.assertNotIn("needs.native.result == 'success'", condition)

    def test_v8_windows_environment_boundary_is_wired_into_build_and_ci(self) -> None:
        package_workflow = self.workflows[
            ROOT / ".github/workflows/native-runtime-packages.yml"
        ]
        runtime_build = (
            ROOT / "scripts/build-native-engine-runtime.ps1"
        ).read_text(encoding="utf-8")
        test_path = "scripts/tests/test_v8_windows_environment.ps1"

        self.assertIn(f"- '{test_path}'", package_workflow)
        self.assertIn(f"./{test_path}", package_workflow)
        test_step = package_workflow.split(
            "- name: Test V8 Windows child environment", 1
        )[1].split("- name:", 1)[0]
        self.assertIn("if: matrix.rid == 'win-x64'", test_step)
        self.assertIn("shell: pwsh", test_step)
        self.assertIn("Invoke-WebSceneV8ChildPowerShell", runtime_build)
        self.assertIn("-V8ChildBuild", runtime_build)
        self.assertGreaterEqual(
            package_workflow.count(
                "hashFiles(matrix.v8_cache_script, matrix.v8_cache_patch, "
                "'scripts/V8WindowsEnvironment.psm1')"
            ),
            2,
        )
        self.assertIn(
            "The exact Windows V8 cache identity was not restored; "
            "forcing a clean child build.",
            package_workflow,
        )
        self.assertIn("Verify fresh Windows V8 child build", package_workflow)
        self.assertIn(
            "Fresh Windows V8 checkout, GN generation, and Ninja output verified.",
            package_workflow,
        )
        restore_keys = package_workflow.split("        restore-keys: |\n", 1)[1].split(
            "    - id: restored-v8-sdk", 1
        )[0]
        self.assertEqual(restore_keys.count("matrix.rid != 'win-x64'"), 2)
        self.assertNotIn("\n          webscene-v8-sdk-", restore_keys)

    def test_release_package_gate_covers_every_supported_rid(self) -> None:
        package_workflow = self.workflows[
            ROOT / ".github/workflows/native-runtime-packages.yml"
        ]
        native = package_workflow.split("\n  native:\n", 1)[1].split(
            "\n  required-evidence:\n", 1
        )[0]
        required = package_workflow.split("\n  required-evidence:\n", 1)[1].split(
            "\n  package-set:\n", 1
        )[0]
        package_set = package_workflow.split("\n  package-set:\n", 1)[1].split(
            "\n  candidate-evidence:\n", 1
        )[0]
        candidate = package_workflow.split("\n  candidate-evidence:\n", 1)[1].split(
            "\n  consumer:\n", 1
        )[0]
        consumer = package_workflow.split("\n  consumer:\n", 1)[1].split(
            "\n  publish:\n", 1
        )[0]

        for rid in ("osx-arm64", "linux-x64", "win-x64"):
            with self.subTest(rid=rid):
                self.assertIn(f"rid: {rid}", native)
                self.assertIn(f"--expected-rid {rid}", required)
                self.assertIn(f"--native-rid {rid}", package_set)
                self.assertIn(f"--expected-rid {rid}", candidate)
                self.assertIn(f"rid: {rid}", consumer)


if __name__ == "__main__":
    unittest.main()
