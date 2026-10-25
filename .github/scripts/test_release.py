# SPDX-License-Identifier: MPL-2.0
"""Release protocol regressions with network and server-side fault injection."""

import copy
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlparse

import release


TAG = "0.8.0"
COMMIT = "a" * 40


def http_error(code, headers=None):
    return HTTPError("https://api.github.com/test", code, "API error", headers or {}, None)


class Server(release.GitHub):
    def __init__(self, exists=True):
        self.delays = []
        super().__init__("test/repo", "test-token", attempts=3, sleep=self.delays.append)
        self.data = {"id": 1, "tag_name": TAG, "name": "OSKey " + TAG, "body": "English release notes.",
                     "draft": False, "assets": [],
                     "upload_url": "https://uploads.github.com/repos/test/repo/releases/1/assets{?name}"} if exists else None
        self.calls = []
        self.get_errors = []
        self.upload_errors = []
        self.lose_upload_reply = False
        self.lose_create_reply = False
        self.corrupt_upload = False
        self.pages = None
        self.next_id = 100

    def add(self, name, data, state="uploaded"):
        asset = {"id": self.next_id, "name": name, "size": len(data), "state": state,
                 "digest": "sha256:" + hashlib.sha256(data).hexdigest()}
        self.next_id += 1
        self.data["assets"].append(asset)
        return copy.deepcopy(asset)

    def request(self, url, method="GET", data=None):
        self.calls.append((method, url))
        if method == "GET":
            if self.get_errors:
                raise self.get_errors.pop(0)
            if url.startswith(self.api + "/releases?"):
                if self.pages is not None:
                    return copy.deepcopy(self.pages[int(parse_qs(urlparse(url).query)["page"][0])])
                return [copy.deepcopy(self.data)] if self.data else []
            if self.data is None or ("/releases/tags/" in url and self.data["draft"]):
                raise http_error(404)
            return copy.deepcopy(self.data)
        if method == "POST" and url == self.api + "/releases":
            if self.data is not None:
                raise http_error(422)
            self.data = {**data, "id": 1, "body": "Generated English notes.", "assets": [],
                         "upload_url": "https://uploads.github.com/assets{?name}"}
            if self.lose_create_reply:
                self.lose_create_reply = False
                raise URLError("Reply lost after release creation")
            return copy.deepcopy(self.data)
        if method == "DELETE":
            self.data["assets"] = [asset for asset in self.data["assets"] if asset["id"] != int(url.rsplit("/", 1)[1])]
            return None
        if method == "PATCH":
            self.data.update(data)
            return copy.deepcopy(self.data)
        if method == "POST":
            if self.upload_errors:
                raise self.upload_errors.pop(0)
            name = parse_qs(urlparse(url).query)["name"][0]
            asset = self.add(name, data if not self.corrupt_upload else b"wrong data")
            if self.lose_upload_reply:
                self.lose_upload_reply = False
                raise URLError("TLS connection closed after accepting upload")
            return asset
        raise AssertionError((method, url))


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name)
        (self.source / "VERSION").write_text("VERSION_MAJOR = 0\nVERSION_MINOR = 8\nPATCHLEVEL = 0\n")
        (self.source / "build.ts").write_text('const boards = [{name: "board"}];')
        self.directory = self.source / "artifacts"
        self.directory.mkdir()
        for name in ("board.bin", "board.elf"):
            (self.directory / name).write_bytes(b"firmware")
        self.manifest = release.prepare(self.directory, self.source, TAG, COMMIT)

    def publish(self, server):
        server.publish(self.directory, TAG, COMMIT)

    def test_requires_every_board_artifact(self):
        (self.directory / "board.elf").unlink()
        with self.assertRaises(ValueError):
            release.prepare(self.directory, self.source, TAG, COMMIT)

    def test_requires_matching_version(self):
        with self.assertRaises(ValueError):
            release.prepare(self.directory, self.source, "9.9.9", COMMIT)

    def test_rejects_empty_firmware(self):
        (self.directory / "board.bin").write_bytes(b"")
        with self.assertRaises(ValueError):
            release.prepare(self.directory, self.source, TAG, COMMIT)

    def test_tampering_fails_before_any_api_write(self):
        (self.directory / "board.bin").write_bytes(b"tampered")
        server = Server()
        with self.assertRaises(ValueError):
            self.publish(server)
        self.assertEqual(server.calls, [])

    def test_rejects_wrong_source_commit(self):
        with self.assertRaises(ValueError):
            release.verify(self.directory, TAG, "b" * 40)

    def test_rejects_duplicate_and_traversal_names(self):
        for names in (("board.bin", "board.bin"), ("../board.bin", "board.elf")):
            with self.subTest(names=names):
                manifest = copy.deepcopy(self.manifest)
                for asset, name in zip(manifest["assets"], names):
                    asset["name"] = name
                (self.directory / "manifest.json").write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    release.verify(self.directory, TAG, COMMIT)

    def test_matching_assets_and_release_notes_are_preserved(self):
        server = Server()
        for asset in self.manifest["assets"]:
            server.add(asset["name"], (self.directory / asset["name"]).read_bytes())
        self.publish(server)
        self.assertTrue(all(method == "GET" for method, _ in server.calls))
        self.assertEqual(server.data["body"], "English release notes.")

    def test_network_interruption_is_retried(self):
        server = Server()
        server.upload_errors = [URLError("TLS handshake interrupted"), http_error(502)]
        self.publish(server)
        self.assertEqual(len(server.data["assets"]), 2)
        self.assertEqual(len(server.delays), 2)

    def test_accepted_upload_with_lost_reply_is_not_uploaded_twice(self):
        server = Server()
        server.lose_upload_reply = True
        self.publish(server)
        self.assertEqual(sum(method == "POST" for method, _ in server.calls), 2)
        self.assertEqual(len(server.data["assets"]), 2)

    def test_incomplete_and_changed_assets_are_replaced(self):
        server = Server()
        server.add("board.bin", b"incomplete", state="starter")
        server.add("board.elf", b"old version")
        self.publish(server)
        self.assertEqual(sum(method == "DELETE" for method, _ in server.calls), 2)
        self.assertTrue(all(server.matches(remote, asset) for remote, asset in
                            zip(sorted(server.data["assets"], key=lambda item: item["name"]), self.manifest["assets"])))

    def test_permission_failure_stops_immediately(self):
        server = Server()
        server.get_errors = [http_error(403)]
        with self.assertRaises(HTTPError):
            self.publish(server)
        self.assertEqual(len(server.calls), 1)
        self.assertEqual(server.delays, [])

    def test_truncated_json_response_is_retried(self):
        server = Server()
        server.get_errors = [json.JSONDecodeError("Truncated response", "{", 1)]
        self.publish(server)
        self.assertEqual(len(server.delays), 1)

    def test_retry_budget_is_bounded(self):
        server = Server()
        server.get_errors = [URLError("Disconnected")] * 4
        with self.assertRaises(URLError):
            self.publish(server)
        self.assertEqual(len(server.calls), 3)

    def test_rate_limit_honors_retry_after(self):
        for code in (403, 429):
            with self.subTest(code=code):
                server = Server()
                server.get_errors = [http_error(code, {"Retry-After": "3"})]
                self.publish(server)
                self.assertEqual(server.delays, [3])

    def test_http_transport_preserves_json_and_binary_payloads(self):
        client = release.GitHub("test/repo", "test-token")
        for body, content_type in (({"draft": True}, "application/json"),
                                   (b"\x00firmware", "application/octet-stream")):
            with self.subTest(content_type=content_type):
                with patch("release.urlopen", return_value=io.BytesIO(b'{"id": 1}')) as opener:
                    self.assertEqual(client.request(client.api, method="POST", data=body), {"id": 1})
                request = opener.call_args.args[0]
                self.assertEqual(request.get_method(), "POST")
                self.assertEqual(request.get_header("Content-type"), content_type)
                self.assertEqual(request.get_header("Authorization"), "Bearer test-token")
                self.assertEqual(json.loads(request.data) if isinstance(body, dict) else request.data, body)

    def test_draft_is_published_after_all_assets_are_verified(self):
        server = Server(exists=False)
        server.lose_create_reply = True
        self.publish(server)
        self.assertFalse(server.data["draft"])
        self.assertEqual(len(server.data["assets"]), 2)
        self.assertEqual(sum(method == "POST" and url == server.api + "/releases" for method, url in server.calls), 1)
        self.assertEqual(server.calls[-1][0], "PATCH")

    def test_existing_draft_is_resumed_without_creating_another(self):
        server = Server()
        server.data["draft"] = True
        server.add("board.bin", b"firmware")
        self.publish(server)
        self.assertFalse(server.data["draft"])
        self.assertEqual(len(server.data["assets"]), 2)
        self.assertFalse(any(method == "POST" and url == server.api + "/releases" for method, url in server.calls))

    def test_draft_lookup_follows_release_pages(self):
        server = Server()
        server.data["draft"] = True
        server.pages = {1: [{"tag_name": "old-" + str(i), "draft": False} for i in range(100)],
                        2: [server.data]}
        self.publish(server)
        self.assertFalse(server.data["draft"])
        self.assertTrue(any("page=2" in url for _, url in server.calls))

    def test_corrupt_upload_keeps_new_release_as_draft(self):
        server = Server(exists=False)
        server.corrupt_upload = True
        with self.assertRaises(ValueError):
            self.publish(server)
        self.assertTrue(server.data["draft"])

    def test_english_release_notes_are_required(self):
        server = Server()
        server.data["body"] = "\u4e2d"
        with self.assertRaises(ValueError):
            self.publish(server)
        self.assertTrue(all(method == "GET" for method, _ in server.calls))


if __name__ == "__main__":
    unittest.main()
