# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AREDN-Phonebook1 is a SIP proxy server designed for AREDN (Amateur Radio Emergency Data Network) mesh networks. The server provides phonebook functionality by fetching CSV phonebook data from AREDN mesh servers and managing SIP user registrations and call routing.

## Build System

This project uses OpenWRT's build system to create `.ipk` packages for AREDN routers.

### Local Development Commands

```bash
# Navigate to the Phonebook directory
cd Phonebook

# Manual compilation for testing (requires OpenWRT SDK setup)
make defconfig
make package/phonebook/compile V=s
```

### GitHub Actions Build & Deploy

The project uses GitHub Actions for **automated build and installation**:

**Build Process:**
- Triggered automatically on tag pushes (format: `*.*.*`)
- Builds for `ath79/generic` and `x86/64` architectures
- Uses OpenWRT SDK 23.05.3
- Output: `.ipk` files attached to GitHub releases

**Deployment Workflow:**
1. **Push changes** to the UAC branch
2. **Create and push a version tag** (e.g., `v1.5.2`) to trigger the build
3. **Wait for GitHub Actions** to build the packages automatically
4. **Download the `.ipk`** from the GitHub releases page
5. **Install via AREDN web interface**: Administration → Package Management → Upload → Install

**Important:** The user expects this automated workflow. When making changes:
1. Commit and push to the branch
2. Create a version tag to trigger the build
3. The build happens automatically via GitHub Actions
4. Installation is done via AREDN web interface (not manual commands)

