<!-- SPDX-License-Identifier: MPL-2.0 -->

# Release workflow

Tag pushes run the firmware regression tests and the complete board matrix. Firmware files,
their SHA-256 digests, and the source commit are stored together as an Actions artifact for
14 days. A separate publication job verifies this bundle and uploads files sequentially.
Matching release assets are preserved; interrupted uploads resume with bounded retries.
New releases are published after every artifact is verified. Release titles and notes use
English, and existing release notes are preserved.
Changes to CI scripts and this workflow on `master` also run the release protocol tests
through a lightweight check job.

To rebuild and republish an existing tag using the current workflow:

```sh
gh workflow run build.yml --ref master -f release_tag=0.8.0
```

To retry publication using the saved bundle, select **Re-run failed jobs** in Actions or run:

```sh
gh run rerun RUN_ID --failed
```

Run the release protocol regressions locally:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s .github/scripts -p 'test_*.py'
```
