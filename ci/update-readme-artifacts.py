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
        "platform": "Windows",
        "icon": ".github/readme/platforms/windows.svg",
        "version": "Windows 10 or later (x64)",
        "job": "Windows Server 2022",
        "artifact_prefix": "ZoinGallery-windows-2022-b",
        "download_label": "Download Windows x64",
    },
    {
        "platform": "Windows",
        "icon": ".github/readme/platforms/windows.svg",
        "version": "Windows 11 or later (ARM64)",
        "job": "Windows / ARM64",
        "artifact_prefix": "ZoinGallery-windows-arm64-b",
        "download_label": "Download Windows ARM64",
    },
    {
        "platform": "macOS",
        "icon": ".github/readme/platforms/macos.svg",
        "version": "macOS 10.15 or later",
        "job": "macOS 15",
        "artifact_prefix": "ZoinGallery-macos-15-b",
        "download_label": "Download DMG",
    },
    {
        "platform": "Linux",
        "icon": ".github/readme/platforms/linux.svg",
        "version": "AppImage x86_64",
        "job": "Linux / Ubuntu 24.04",
        "artifact_prefix": "ZoinGallery-appimage-x86_64-b",
        "download_label": "Download AppImage",
    },
    {
        "platform": "Linux",
        "icon": ".github/readme/platforms/linux.svg",
        "version": "Flatpak x86_64",
        "job": "Linux / Ubuntu 24.04",
        "artifact_prefix": "ZoinGallery-flatpak-x86_64-b",
        "download_label": "Download Flatpak",
    },
    {
        "platform": "Linux",
        "icon": ".github/readme/platforms/linux.svg",
        "version": "Ubuntu 24.04",
        "job": "Linux / Ubuntu 24.04",
        "artifact_prefix": "ZoinGallery-linux-ubuntu-24.04-b",
        "download_label": "Download Ubuntu",
    },
    {
        "platform": "Linux",
        "icon": ".github/readme/platforms/linux.svg",
        "version": "Debian 12",
        "job": "Linux / Debian 12",
        "artifact_prefix": "ZoinGallery-linux-debian-12-b",
        "download_label": "Download Debian",
    },
    {
        "platform": "Linux",
        "icon": ".github/readme/platforms/linux.svg",
        "version": "Fedora latest",
        "job": "Linux / Fedora latest",
        "artifact_prefix": "ZoinGallery-linux-fedora-latest-b",
        "download_label": "Download Fedora",
    },
    {
        "platform": "Linux",
        "icon": ".github/readme/platforms/linux.svg",
        "version": "Arch rolling",
        "job": "Linux / Arch latest",
        "artifact_prefix": "ZoinGallery-linux-arch-latest-b",
        "download_label": "Download Arch",
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


def artifact_for_platform(artifacts: list[dict[str, Any]], artifact_prefix: str) -> dict[str, Any]:
    matches = [
        artifact
        for artifact in artifacts
        if str(artifact.get("name", "")).startswith(artifact_prefix)
    ]
    if not matches:
        return {}
    return max(matches, key=lambda artifact: artifact.get("created_at") or "")


def artifact_build_number(artifact: dict[str, Any], artifact_prefix: str) -> str:
    name = str(artifact.get("name", ""))
    suffix = name.removeprefix(artifact_prefix)
    return suffix if name.startswith(artifact_prefix) and suffix.isdigit() else "unknown"


def platform_cell(platform: dict[str, str]) -> str:
    icon = platform["icon"]
    name = platform["platform"]
    return f'<img src="{icon}" width="22" alt="{name} logo"> {name}'


def build_section() -> str:
    repo = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    server_url = os.environ.get("GITHUB_SERVER_URL", "https://github.com")

    run = api_json(f"/actions/runs/{run_id}")
    jobs = paged_api(f"/actions/runs/{run_id}/jobs", "jobs")
    artifacts = paged_api(f"/actions/runs/{run_id}/artifacts", "artifacts")

    jobs_by_name = {job["name"]: job for job in jobs}

    lines = [
        "| Platform | Version | Built | Download |",
        "| --- | --- | --- | --- |",
    ]

    for platform in PLATFORMS:
        job = jobs_by_name.get(platform["job"], {})
        artifact = artifact_for_platform(artifacts, platform["artifact_prefix"])

        if artifact:
            artifact_url = f"{server_url}/{repo}/actions/runs/{run_id}/artifacts/{artifact['id']}"
            build_number = artifact_build_number(artifact, platform["artifact_prefix"])
            artifact_cell = f"[{platform['download_label']} (build {build_number})]({artifact_url})"
            built_at = format_time(artifact.get("created_at"))
        else:
            artifact_cell = "Unavailable"
            built_at = format_time(job.get("completed_at") or run.get("updated_at"))

        lines.append(
            f"| {platform_cell(platform)} | {platform['version']} | {built_at} | {artifact_cell} |"
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
