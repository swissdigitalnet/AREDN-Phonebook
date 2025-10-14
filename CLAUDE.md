# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AREDN-Phonebook1 is a SIP proxy server designed for AREDN (Amateur Radio Emergency Data Network) mesh networks. The server provides phonebook functionality by fetching CSV phonebook data from AREDN mesh servers and managing SIP user registrations and call routing.

## Build System

**OFFICIAL BUILD METHOD: GitHub Actions ONLY**

This project uses GitHub Actions for all official builds. Do NOT build locally unless specifically requested for testing.

**IMPORTANT: NO RELEASES** - The user does NOT want GitHub releases. Tags are only used to trigger builds.

**Official Deployment Workflow:**
1. Commit and push changes to the branch
2. Create a version tag to trigger the build (e.g., `git tag v1.5.2 && git push origin v1.5.2`)
3. GitHub Actions builds automatically for all architectures (x86_64, ath79, etc.)
4. Download the built .ipk package from the workflow artifacts
5. Install via opkg on target node
6. **DO NOT create or manage GitHub releases**

**Local builds** (dev container) are only for quick testing and should NOT be used for deployment.

**CRITICAL: NEVER use local GCC compilation to verify code correctness for OpenWRT**
- Local environment uses glibc (Debian), OpenWRT uses musl libc
- Different headers, different function availability (e.g., backtrace)
- Different build constraints and static linking behavior
- Code that compiles locally with GCC may NOT compile in OpenWRT SDK
- ONLY GitHub Actions builds with OpenWRT SDK are valid for verification

### Download and Installation

**GitHub Token:**
- A GitHub Personal Access Token is configured in `~/.bashrc` as `GITHUB_TOKEN`
- This token enables automatic download of GitHub Actions artifacts
- Export it before downloading: `export GITHUB_TOKEN=<token_from_bashrc>`

**Automatic Download Process (from GitHub Actions artifacts):**

```bash
# Set required variables
export GITHUB_TOKEN=$(grep "^export GITHUB_TOKEN=" ~/.bashrc | cut -d= -f2)
VERSION="2.3.5"  # Replace with target version
ARCH="x86"       # x86, ath79, or ipq40xx

# Get the workflow run ID for the version
RUN_ID=$(curl -sL "https://api.github.com/repos/swissdigitalnet/AREDN-Phonebook/actions/runs?event=push&per_page=50" | \
  grep -B 10 "\"head_branch\": \"v${VERSION}\"" | grep '"id":' | head -1 | grep -o '[0-9]*')

echo "Workflow run ID: $RUN_ID"

# Get artifact ID
ARTIFACT_ID=$(curl -sL -H "Authorization: token $GITHUB_TOKEN" \
  "https://api.github.com/repos/swissdigitalnet/AREDN-Phonebook/actions/runs/${RUN_ID}/artifacts" | \
  grep -B 3 "\"name\": \"AREDN-Phonebook-${ARCH}-v${VERSION}\"" | grep '"id":' | grep -o '[0-9]*')

echo "Artifact ID: $ARTIFACT_ID"

# Download artifact
curl -sL -H "Authorization: token $GITHUB_TOKEN" \
  "https://api.github.com/repos/swissdigitalnet/AREDN-Phonebook/actions/artifacts/${ARTIFACT_ID}/zip" \
  -o "/tmp/AREDN-Phonebook-${ARCH}-v${VERSION}.zip"

# Extract IPK
cd /tmp
unzip -o "AREDN-Phonebook-${ARCH}-v${VERSION}.zip"
echo "Downloaded: $(ls -lh AREDN-Phonebook*.ipk)"
```

**Installation Process:**
1. Detect architecture: `ssh {NODE} "uname -m"`
2. Transfer package: `scp /tmp/AREDN-Phonebook*.ipk {NODE}:/tmp/`
3. Install: `ssh {NODE} "opkg install /tmp/AREDN-Phonebook*.ipk"`
4. Restart service: `ssh {NODE} "/etc/init.d/AREDN-Phonebook restart"`

**Note:** Replace `{NODE}` with `vm-1`, `hap-2`, or any AREDN node hostname.

## Development Environment

**Setup Required:** Before using these commands, complete the development environment setup described in [SetupDevContainer.md](SetupDevContainer.md).

### Quick Access to Test Nodes

Once setup is complete, use these SSH shortcuts from the container:

```bash
# Connect to VM-1 gateway
ssh vm-1

# Connect to HAP-2 router (via VM-1 jump host)
ssh hap-2

# Connect to any AREDN node (via VM-1 jump host)
ssh nodename.local.mesh
```

### Available Test Nodes

- **vm-1**: Gateway node with access to AREDN mesh (192.168.0.198)
- **hap-2**: MikroTik hAP ac² router on mesh (10.51.55.233)

### Important Notes

- All mesh access goes through VM-1 as a jump host
- AREDN mesh networks (10.x.x.x) are not directly routable from the container
- SSH keys provide passwordless access to all nodes

