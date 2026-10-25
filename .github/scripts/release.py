#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Package verified firmware and resume GitHub Release uploads."""

import argparse
import hashlib
from http.client import HTTPException
import json
import os
from pathlib import Path
import re
import time
from urllib.error import HTTPError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(directory, source, tag, commit):
    version = dict(re.findall(r"^(VERSION_MAJOR|VERSION_MINOR|PATCHLEVEL)\s*=\s*(\d+)",
                              (source / "VERSION").read_text(), re.MULTILINE))
    if tag != ".".join(version[key] for key in ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL")):
        raise ValueError("Release tag and firmware version differ")
    boards = re.findall(r'name: "([^"]+)"', (source / "build.ts").read_text())
    required = {board + suffix for board in boards for suffix in (".bin", ".elf")}
    files = {path.name: path for path in directory.iterdir() if path.name != "manifest.json"}
    if not boards or len(set(boards)) != len(boards) or not required <= files.keys():
        raise ValueError("The complete board matrix is required")
    if any(path.stem not in boards or path.suffix not in (".bin", ".elf", ".uf2")
           or not path.is_file() or path.is_symlink() or not path.stat().st_size
           for path in files.values()):
        raise ValueError("Unexpected firmware artifact")
    manifest = {"tag": tag, "commit": commit, "assets": [
        {"name": name, "size": path.stat().st_size, "sha256": digest(path)}
        for name, path in sorted(files.items())
    ]}
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def verify(directory, tag, commit):
    manifest = json.loads((directory / "manifest.json").read_text())
    if manifest["tag"] != tag or manifest["commit"] != commit:
        raise ValueError("Artifact provenance differs from the selected release")
    assets = manifest["assets"]
    names = [asset["name"] for asset in assets]
    if not names or len(set(names)) != len(names) or any(
        not re.fullmatch(r"[A-Za-z0-9_.-]+\.(bin|elf|uf2)", name) for name in names
    ):
        raise ValueError("Invalid artifact names")
    if {path.name for path in directory.iterdir()} != set(names) | {"manifest.json"}:
        raise ValueError("Artifact files differ from the manifest")
    for asset in assets:
        path = directory / asset["name"]
        if (not path.is_file() or path.is_symlink() or not path.stat().st_size
                or path.stat().st_size != asset["size"] or digest(path) != asset["sha256"]):
            raise ValueError("Artifact integrity check failed: " + asset["name"])
    return manifest


class GitHub:
    def __init__(self, repo, token, attempts=5, sleep=time.sleep):
        self.api = "https://api.github.com/repos/" + repo
        self.token = token
        self.attempts = attempts
        self.sleep = sleep

    def request(self, url, method="GET", data=None):
        headers = {"Authorization": "Bearer " + self.token,
                   "User-Agent": "OSKey-release",
                   "Accept": "application/vnd.github+json",
                   "X-GitHub-Api-Version": "2022-11-28"}
        if isinstance(data, dict):
            headers["Content-Type"] = "application/json"
            data = json.dumps(data).encode()
        elif data is not None:
            headers["Content-Type"] = "application/octet-stream"
        with urlopen(Request(url, method=method, headers=headers, data=data), timeout=60) as response:
            body = response.read()
            return json.loads(body) if body else None

    def retry(self, operation):
        for attempt in range(self.attempts):
            delay = min(2 ** attempt, 30)
            try:
                return operation()
            except HTTPError as error:
                headers = error.headers or {}
                retry_after = headers.get("Retry-After", "")
                limited = error.code == 403 and (retry_after or headers.get("X-RateLimit-Remaining") == "0")
                if error.code not in (408, 422, 429, 500, 502, 503, 504) and not limited:
                    raise
                if retry_after.isdigit():
                    delay = min(int(retry_after), 60)
                if attempt + 1 == self.attempts:
                    raise
            except (OSError, HTTPException, json.JSONDecodeError):
                if attempt + 1 == self.attempts:
                    raise
            print(f"Retrying release operation in {delay}s ({attempt + 1}/{self.attempts})", flush=True)
            self.sleep(delay)

    @staticmethod
    def check_notes(release):
        notes = (release["name"] or "") + (release["body"] or "")
        if re.search(r"[\u3400-\u9fff\uf900-\ufaff]", notes):
            raise ValueError("Release titles and notes must use English")

    def draft(self, tag):
        # The tag endpoint exposes published releases; drafts are listed separately.
        page = 1
        while True:
            items = self.request(self.api + "/releases?" + urlencode({"per_page": 100, "page": page}))
            draft = next((item for item in items if item["draft"] and item["tag_name"] == tag), None)
            if draft or len(items) < 100:
                return draft
            page += 1

    def release(self, tag):
        try:
            release = self.request(self.api + "/releases/tags/" + quote(tag, safe=""))
        except HTTPError as error:
            if error.code != 404:
                raise
            release = self.draft(tag)
            if release is None:
                release = self.request(self.api + "/releases", method="POST", data={
                    "tag_name": tag, "name": "OSKey " + tag,
                    "draft": True, "prerelease": False, "generate_release_notes": True,
                })
        self.check_notes(release)
        return release

    @staticmethod
    def matches(remote, asset):
        return (remote["state"] == "uploaded" and remote["size"] == asset["size"]
                and remote.get("digest") == "sha256:" + asset["sha256"])

    def upload(self, release, directory, asset):
        # Reconcile after each interruption: the server may have accepted the upload.
        current = self.request(self.api + "/releases/" + str(release["id"]))
        existing = next((item for item in current["assets"] if item["name"] == asset["name"]), None)
        if existing and self.matches(existing, asset):
            print("Verified existing " + asset["name"], flush=True)
            return
        if existing:
            self.request(self.api + "/releases/assets/" + str(existing["id"]), method="DELETE")
        url = release["upload_url"].split("{")[0] + "?" + urlencode({"name": asset["name"]})
        remote = self.request(url, method="POST", data=(directory / asset["name"]).read_bytes())
        if not self.matches(remote, asset):
            raise ValueError("Uploaded artifact digest differs: " + asset["name"])
        print("Uploaded and verified " + asset["name"], flush=True)

    def publish(self, directory, tag, commit):
        manifest = verify(directory, tag, commit)
        release = self.retry(lambda: self.release(tag))
        for asset in manifest["assets"]:
            self.retry(lambda: self.upload(release, directory, asset))
        current = self.retry(lambda: self.request(self.api + "/releases/" + str(release["id"])))
        self.check_notes(current)
        remote = {asset["name"]: asset for asset in current["assets"]}
        if any(asset["name"] not in remote or not self.matches(remote[asset["name"]], asset)
               for asset in manifest["assets"]):
            raise ValueError("Release verification failed")
        if current["draft"]:
            self.retry(lambda: self.request(self.api + "/releases/" + str(release["id"]),
                                            method="PATCH", data={"draft": False}))
        print(f"Verified {len(manifest['assets'])} release assets for {tag} at {commit}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "publish"))
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--repo")
    args = parser.parse_args()
    if args.command == "prepare":
        manifest = prepare(args.artifacts, Path.cwd(), args.tag, args.commit)
        print(f"Prepared {len(manifest['assets'])} firmware assets", flush=True)
    else:
        if not args.repo:
            parser.error("--repo is required for publishing")
        GitHub(args.repo, os.environ["GH_TOKEN"]).publish(args.artifacts, args.tag, args.commit)


if __name__ == "__main__":
    main()
