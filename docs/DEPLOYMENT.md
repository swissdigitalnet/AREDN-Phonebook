# Deployment Guide

How to build and release AREDN-Phonebook. Deployment is driven by CI: **push a
version tag and GitHub Actions builds every architecture and publishes a
release.** This document covers the release flow, the platform/packaging facts
behind it, local builds, and installing on a node.

---

## Platform: AREDN 4.x / OpenWrt 25.12 / apk

AREDN **4.26.7.0** (the first major-4 release) is based on **OpenWrt 25.12**,
which replaced the old `opkg`/`.ipk` package manager with **apk** (`.apk`
packages). Everything we ship is therefore built as `.apk`.

| | AREDN 3.x (legacy) | AREDN 4.x (current) |
|---|---|---|
| OpenWrt base | 23.05 | 25.12 |
| Toolchain | gcc 12.3.0 | gcc 14.3.0 |
| SDK tarball | `.tar.xz` | `.tar.zst` (needs `zstd`) |
| Package manager | opkg | apk |
| Package file | `.ipk` | `.apk` |

`.ipk` packages are **not** installable on AREDN 4.x and are removed on
firmware upgrade. See [Legacy .ipk (AREDN 3.x)](#legacy-ipk-aredn-3x).

Targets: **ath79** (`mips_24kc`), **ipq40xx** (`arm_cortex-a7_neon-vfpv4`),
**x86-64** (`x86_64`).

---

## Releasing via CI (the normal path)

The workflow is [`.github/workflows/test-build.yml`](../.github/workflows/test-build.yml).
It triggers on any tag matching `*.*.*`.

```bash
# 1. Land your changes on main
git checkout main
git pull
# ... commits ...
git push origin main

# 2. Tag a version and push the tag -> this is what fires CI
git tag -a 2.6.3 -m "Short release summary"
git push origin 2.6.3
```

On the tag push, CI:

1. Builds the package for **ath79, ipq40xx, x86** in parallel, each against the
   OpenWrt 25.12.4 SDK (downloads the SDK, `apk`-builds a static binary).
2. Uploads each build as a workflow artifact.
3. **Creates the GitHub release** for the tag if it doesn't exist yet
   (a tag push alone does not create a release), then uploads all three `.apk`
   assets to it, named by OpenWrt arch:
   - `AREDN-Phonebook_<version>-1_x86_64.apk`
   - `AREDN-Phonebook_<version>-1_mips_24kc.apk`
   - `AREDN-Phonebook_<version>-1_arm_cortex-a7_neon-vfpv4.apk`

The release page then has all three files for download. No manual release
creation or asset upload is needed.

### Version numbering

- The tag **is** the version (`git describe --tags`, leading `v` stripped).
- CI passes it through as `PKG_VERSION_OVERRIDE`, and the package release is
  always `-1` (so the built file is `AREDN-Phonebook-<version>-r1.apk`).
- Use plain `MAJOR.MINOR.PATCH` (e.g. `2.6.3`). Bump PATCH for fixes, MINOR for
  features.

### Watching a run

```bash
gh run list  --repo swissdigitalnet/AREDN-Phonebook --limit 5
gh run watch <run-id> --repo swissdigitalnet/AREDN-Phonebook
gh release view <tag> --repo swissdigitalnet/AREDN-Phonebook
```

### Re-cutting a tag

If you need to move a tag to a newer commit (e.g. a CI fix), the workflow that
runs is the one **at the tagged commit**, so re-point and force-push:

```bash
git tag -f -a 2.6.3 -m "..."
git push -f origin 2.6.3
```

Cancel any superseded in-flight run with `gh run cancel <run-id>`.

---

## Building locally (optional)

Useful for testing a build without cutting a release. Requires `zstd`
(`sudo apt-get install -y zstd`) — the 25.12 SDKs are zstd-compressed.

```bash
./build-all-ipks.sh                 # all three arches -> build-output/*.apk
./build-all-ipks.sh x86             # single arch
./build-all-ipks.sh -v 2.6.3 x86    # override version
./build-all-ipks.sh -c              # clean first
```

The script downloads the correct 25.12.4 SDK per arch (into `ath79-sdk/`,
`ipq40xx-sdk/`, `x86-sdk/` — all gitignored), injects the `Phonebook/` package,
compiles, and copies the resulting `.apk` to `build-output/`. It refuses to
reuse a stale non-25.12 SDK directory.

> Do **not** hand-build production `.apk`s for release — CI is the source of
> truth. Local builds are for development only.

---

## Installing on a node

### Via the AREDN web UI (end users)

**Administration → Package Management → Choose File** → select the `.apk` for
your hardware → **Fetch and Install**. Power-cycle if the service doesn't come
up.

### Via SSH/CLI (developers)

AREDN nodes run dropbear on **port 2222** with **no SFTP**, so use `scp -O`:

```bash
scp -O -P 2222 AREDN-Phonebook_2.6.3-1_x86_64.apk root@<node>:/tmp/pb.apk
ssh -p 2222 root@<node> 'apk add --allow-untrusted /tmp/pb.apk'
ssh -p 2222 root@<node> '/etc/init.d/AREDN-Phonebook restart'
```

Notes:
- `--allow-untrusted` is required for a locally-provided (unsigned) `.apk`.
- `apk add` first tries to refresh the online AREDN feeds; if the node has no
  internet you'll see `wget: exited with error 8` warnings — **harmless**, the
  local file still installs.
- `apk list -I | grep -i phonebook` shows the installed version.

### Verify after install

```bash
curl -s http://<node>/cgi-bin/health_status        | grep -E 'node|health_score|all_responsive'
curl -s http://<node>/cgi-bin/active_calls_json                       # expect total_active_calls: 0 after restart
curl -s http://<node>/cgi-bin/topology_json | grep -c '"type": *"phone"'   # phones repopulate after a crawl cycle (~3 min)
```

---

## Legacy .ipk (AREDN 3.x)

CI builds `.apk` only. The last `.ipk`-compatible build is **frozen** on the
[`2.5.1`](https://github.com/swissdigitalnet/AREDN-Phonebook/releases/tag/2.5.1)
release and is **not** rebuilt — this avoids regressing 3.x, since some code
(e.g. phone-on-map discovery) is specific to the AREDN 4.x arednlink host
format. AREDN 3.x users install the `.ipk` from `2.5.1` with `opkg`. Drop this
section (and the pointer in the README) once 3.x support is retired.
