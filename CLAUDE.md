# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AREDN-Phonebook1 is a SIP proxy server designed for AREDN (Amateur Radio Emergency Data Network) mesh networks. The server provides phonebook functionality by fetching CSV phonebook data from AREDN mesh servers and managing SIP user registrations and call routing.

## Build System

This project uses GitHub Actions to create the bin files.

**Important:** The user expects this automated workflow. When making changes:

1. Commit and push to the branch
2. Create a version tag to trigger the build (e.g., `git tag v1.5.2 && git push origin v1.5.2`)
3. The build happens automatically via GitHub Actions
4. Download the built package and install via opkg

### Download and Installation

**Download Process:**
1. Get the release version from GitHub API:
   ```bash
   curl -sL https://api.github.com/repos/swissdigitalnet/AREDN-Phonebook/releases/tags/v{VERSION} | grep browser_download_url
   ```
2. Download the appropriate architecture package (x86 or ath79):
   ```bash
   curl -L -o /tmp/AREDN-Phonebook.ipk {DOWNLOAD_URL}
   ```

**Installation Process:**
1. Ask for the node name if not known (e.g., `hb9bla-vm-1.local.mesh`)
2. Detect architecture: `ssh root@{NODE} -p 2222 "uname -m"`
3. Transfer package: `scp -P 2222 -O /tmp/AREDN-Phonebook.ipk root@{NODE}:/tmp/`
4. Install: `ssh root@{NODE} -p 2222 "opkg install /tmp/AREDN-Phonebook.ipk"`
5. Restart service: `ssh root@{NODE} -p 2222 "/etc/init.d/AREDN-Phonebook restart"`

