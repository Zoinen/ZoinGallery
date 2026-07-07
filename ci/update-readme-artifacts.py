#!/usr/bin/env python3
"""Update the README section that links to the latest GitHub Actions artifacts."""

from __future__ import annotations

import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import urlencode
from urllib.request import Request, urlopen


START_MARKER = "<!-- CI_ARTIFACTS_START -->"
END_MARKER = "<!-- CI_ARTIFACTS_END -->"

PLATFORMS = [
    {
        "label": "Windows Server 2022",
        "job": "Windows Server 2022",
        "artifact_prefix": "ZoinGallery-windows-2022-b",
    },
    {
        "label": "macOS 15",
        "job": "macOS 15",
        "artifact_prefix": "ZoinGallery-macos-15-b",
    },
    {
        "label": "Ubuntu 24.04",
        "job": "Linux / Ubuntu 24.04",
        "artifact_prefix": "ZoinGallery-linux-ubuntu-24.04-b",
    },
    {
        "label": "Debian 12",
        "job": "Linux / Debian 12",
        "artifact_prefix": "ZoinGallery-linux-debian-12-b",
    },
    {
        "label": "Fedora latest",
        "job": "Linux / Fedora latest",
        "artifact_prefix": "ZoinGallery-linux-fedora-latest-b",
    },
    {
        "label": "Arch latest",
        "job": "Linux / Arch latest",
        "artifact_prefix": "ZoinGallery-linux-arch-latest-b",
    },
]


def api_json(path: str) -> dict[str, Any]:
    repo = os.environ["GITHUB_REPOSITORY"]
    token = os.environ.get("GITHUB_TOKEN", "")
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "ZoinGallery-readme-artifact-updater",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"

    request = Request(f"https://api.github.com/repos/{repo}{path}", headers=headers)
    with urlopen(request, timeout=30) as response:
        return json.load(response)


def paged_api(path: str, key: str) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    page = 1
    separator = "&" if "?" in path else "?"
    while True:
        data = api_json(f"{path}{separator}{urlencode({'page': page, 'per_page': 100})}")
        page_items = data.get(key, [])
        items.extend(page_items)
        if len(page_items) < 100:
            return items
        page += 1


def format_time(value: str | None) -> str:
    if not value:
        return "Unknown"
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    return parsed.astimezone(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")


def status_text(conclusion: str | None) -> str:
    if conclusion == "success":
        return "Pass"
    if conclusion:
        return conclusion.replace("_", " ").title()
    return "Unknown"


def artifact_for_platform(artifacts: list[dict[str, Any]], artifact_prefix: str) -> dict[str, Any]:
    matches = [
        artifact
        for artifact in artifacts
        if str(artifact.get("name", "")).startswith(artifact_prefix)
    ]
    if not matches:
        return {}
    return max(matches, key=lambda artifact: artifact.get("created_at") or "")


def build_section() -> str:
    repo = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    server_url = os.environ.get("GITHUB_SERVER_URL", "https://github.com")

    run = api_json(f"/actions/runs/{run_id}")
    jobs = paged_api(f"/actions/runs/{run_id}/jobs", "jobs")
    artifacts = paged_api(f"/actions/runs/{run_id}/artifacts", "artifacts")

    jobs_by_name = {job["name"]: job for job in jobs}
    run_url = run.get("html_url") or f"{server_url}/{repo}/actions/runs/{run_id}"
    run_number = str(run.get("run_number") or os.environ.get("GITHUB_RUN_NUMBER", "unknown"))
    sha = run.get("head_sha") or os.environ.get("GITHUB_SHA", "")
    short_sha = sha[:7] if sha else "unknown"
    commit_cell = (
        f"[`{short_sha}`]({server_url}/{repo}/commit/{sha})" if sha else "`unknown`"
    )

    lines = [
        f"Latest successful run: [{run.get('display_title') or 'CI'}]({run_url})",
        f"Build number: `{run_number}`",
        "",
        "| Platform | CI Status | Download | Built | Commit |",
        "| --- | --- | --- | --- | --- |",
    ]

    for platform in PLATFORMS:
        job = jobs_by_name.get(platform["job"], {})
        artifact = artifact_for_platform(artifacts, platform["artifact_prefix"])

        status = status_text(job.get("conclusion"))
        status_url = job.get("html_url") or run_url
        status_cell = f"[{status}]({status_url})"

        if artifact:
            artifact_url = f"{server_url}/{repo}/actions/runs/{run_id}/artifacts/{artifact['id']}"
            artifact_cell = f"[{artifact['name']}]({artifact_url})"
            built_at = format_time(artifact.get("created_at"))
        else:
            artifact_cell = "Unavailable"
            built_at = format_time(job.get("completed_at") or run.get("updated_at"))

        lines.append(
            f"| {platform['label']} | {status_cell} | {artifact_cell} | {built_at} | {commit_cell} |"
        )

    lines.extend(
        [
            "",
            "_These are unsigned CI validation builds. GitHub may require sign-in to download artifacts._",
        ]
    )
    return "\n".join(lines)


def update_readme(section: str) -> None:
    readme_path = Path(os.environ.get("README_PATH", "README.md"))
    readme = readme_path.read_text(encoding="utf-8")

    start = readme.index(START_MARKER) + len(START_MARKER)
    end = readme.index(END_MARKER)
    updated = f"{readme[:start]}\n{section}\n{readme[end:]}"
    readme_path.write_text(updated, encoding="utf-8")


def main() -> None:
    update_readme(build_section())


if __name__ == "__main__":
    main()
