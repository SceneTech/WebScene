#!/usr/bin/env python3
"""Regression checks for stacked-PR validation and release-only publishing."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STACKED_BASE = "feature/code-oss-browser-compatibility"


def indented_section(text: str, name: str, indent: int) -> list[str]:
    lines = text.splitlines()
    header = f"{' ' * indent}{name}:"
    try:
        start = lines.index(header) + 1
    except ValueError as error:
        raise AssertionError(f"Missing YAML section {header!r}") from error

    result: list[str] = []
    for line in lines[start:]:
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
        result.append(line)
    return result


def yaml_list(section: list[str], name: str, indent: int) -> list[str]:
    header = f"{' ' * indent}{name}:"
    try:
        start = section.index(header) + 1
    except ValueError as error:
        raise AssertionError(f"Missing YAML list {header!r}") from error

    result: list[str] = []
    marker = f"{' ' * indent}- "
    for line in section[start:]:
        if line.startswith(marker):
            result.append(line[len(marker):].strip(" '\""))
            continue
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
    return result


class WorkflowTriggerPolicyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.ci = (ROOT / ".github/workflows/CI.yml").read_text(encoding="utf-8")
        self.packages = (
            ROOT / ".github/workflows/native-runtime-packages.yml"
        ).read_text(encoding="utf-8")

    def test_full_ci_validates_stacked_pull_requests_without_expanding_pushes(self) -> None:
        events = indented_section(self.ci, "on", 0)
        pull_request = indented_section("\n".join(events), "pull_request", 2)
        push = indented_section("\n".join(events), "push", 2)

        self.assertEqual(
            yaml_list(pull_request, "branches", 4),
            ["main", "release/*", STACKED_BASE],
        )
        self.assertEqual(yaml_list(push, "branches", 4), ["main", "release/*"])

    def test_runtime_packages_validate_stacked_pull_requests(self) -> None:
        events = indented_section(self.packages, "on", 0)
        pull_request = indented_section("\n".join(events), "pull_request", 2)
        push = indented_section("\n".join(events), "push", 2)

        self.assertEqual(
            yaml_list(pull_request, "branches", 4),
            ["main", "release/*", STACKED_BASE],
        )
        self.assertEqual(yaml_list(push, "branches", 4), ["main"])
        self.assertEqual(yaml_list(push, "tags", 4), ["v*"])
        self.assertIn(
            "scripts/tests/test_workflow_trigger_policy.py",
            yaml_list(pull_request, "paths", 4),
        )

    def test_pull_requests_cannot_enable_package_publication(self) -> None:
        self.assertIn('publish="${REQUESTED_PUBLISH:-false}"', self.packages)
        self.assertIn('if [[ "$GITHUB_REF_TYPE" == tag ]]', self.packages)
        self.assertGreaterEqual(
            self.packages.count("if: needs.metadata.outputs.publish == 'true'"),
            2,
        )


if __name__ == "__main__":
    unittest.main()
