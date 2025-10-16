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

### Download and Installation

**Download Process (from GitHub Actions artifacts):**
1. Get the workflow run for the tag:
   ```bash
   curl -sL https://api.github.com/repos/swissdigitalnet/AREDN-Phonebook/actions/runs?event=push | grep -A 5 "v{VERSION}"
   ```
2. Download artifacts from the workflow run using GitHub token:
   ```bash
   # GitHub token (extract from git remote URL: git config --get remote.origin.url)
   TOKEN=$(git config --get remote.origin.url | grep -oP 'https://\K[^@]+')

   # Download artifact
   curl -L -H "Authorization: token $TOKEN" -o /tmp/AREDN-Phonebook-ath79.zip \
     "https://api.github.com/repos/swissdigitalnet/AREDN-Phonebook/actions/artifacts/{ARTIFACT_ID}/zip"

   # Extract
   unzip -o /tmp/AREDN-Phonebook-ath79.zip -d /tmp/
   ```
3. Alternatively, if releases exist (created by workflow), download from:
   ```bash
   curl -sL https://api.github.com/repos/swissdigitalnet/AREDN-Phonebook/releases/tags/v{VERSION} | grep browser_download_url
   ```

**Installation Process:**
1. Detect architecture: `ssh {NODE} "uname -m"`
2. Transfer package (use cat since scp/sftp may not be available):
   ```bash
   cat /tmp/AREDN-Phonebook.ipk | ssh {NODE} "cat > /tmp/AREDN-Phonebook.ipk"
   ```
3. Install: `ssh {NODE} "opkg install /tmp/AREDN-Phonebook.ipk"`
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

