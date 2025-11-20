# Claude Code - AREDN Phonebook Development Guide

## SSH Access to Test Devices

This document describes how to access the AREDN test devices for development and debugging.

### Device Overview

- **vm-1**: HB9BLA-VM-1 (Virtual Machine running AREDN)
- **hap-2**: Physical MikroTik hAP router running AREDN

### SSH Configuration

SSH shortcuts are configured in `~/.ssh/config`:

#### Direct Access to vm-1

```bash
ssh vm-1
# or
ssh root@192.168.0.198 -p 2222
```

**Details:**
- Hostname: `192.168.0.198`
- Port: `2222`
- User: `root`
- Identity: `~/.ssh/aredn`

#### Access to hap-2 (via vm-1 ProxyJump)

```bash
ssh hap-2
```

**Details:**
- Hostname: `10.51.55.233`
- Port: `2222`
- User: `root`
- Identity: `~/.ssh/aredn`
- ProxyJump: `vm-1` (connection tunnels through vm-1)

**Note:** hap-2 is not directly accessible from the development environment. All connections must go through vm-1 as a jump host.

#### Access to Any AREDN Mesh Node

```bash
ssh <nodename>.local.mesh
```

**Details:**
- Port: `2222`
- User: `root`
- Identity: `~/.ssh/aredn`
- ProxyJump: `vm-1` (all *.local.mesh connections tunnel through vm-1)

### File Transfer Examples

#### Transfer file to vm-1:
```bash
cat file.ipk | ssh vm-1 "cat > /tmp/file.ipk"
```

#### Transfer file to hap-2 (direct, via configured ProxyJump):
```bash
cat file.ipk | ssh hap-2 "cat > /tmp/file.ipk"
```

#### Transfer file to hap-2 (explicit two-hop):
```bash
# Step 1: Transfer to vm-1
cat file.ipk | ssh vm-1 "cat > /tmp/file.ipk"

# Step 2: From vm-1 to hap-2
ssh vm-1 "cat /tmp/file.ipk | ssh root@10.51.55.233 'cat > /tmp/file.ipk'"
```

### Package Installation

#### Install IPK on vm-1:
```bash
ssh vm-1 "opkg install /tmp/package.ipk"
```

#### Install IPK on hap-2:
```bash
ssh hap-2 "opkg install /tmp/package.ipk"
```

#### Force reinstall (upgrade):
```bash
ssh hap-2 "opkg install --force-reinstall /tmp/package.ipk"
```

### Common Operations

#### Check running services:
```bash
ssh hap-2 "ps | grep AREDN-Phonebook"
```

#### View logs:
```bash
ssh hap-2 "logread | grep AREDN-Phonebook"
```

#### Check config:
```bash
ssh hap-2 "cat /etc/phonebook.conf"
```

#### Restart service:
```bash
ssh hap-2 "/etc/init.d/AREDN-Phonebook restart"
```

### Network Topology

```
Development Environment (aredn-dev)
    |
    | (192.168.0.21 -> 192.168.0.198:2222)
    v
  vm-1 (HB9BLA-VM-1)
    |
    | (AREDN Mesh Network)
    | (10.x.x.x -> 10.51.55.233:2222)
    v
  hap-2 (MikroTik hAP)
```

### Troubleshooting

#### Connection refused to hap-2:
- Ensure SSH is enabled on hap-2 via AREDN web interface
- Check that vm-1 is reachable
- Verify hap-2 is online: `ssh vm-1 "ping -c 2 10.51.55.233"`

#### ProxyJump not working:
- Verify vm-1 connection works first: `ssh vm-1`
- Check SSH config: `cat ~/.ssh/config`
- Ensure SSH key is present: `ls -la ~/.ssh/aredn`

#### Name resolution fails (*.local.mesh):
- Mesh names are resolved through vm-1's OLSR routing
- Use IP addresses directly if DNS fails
- Check routes: `ssh vm-1 "cat /var/run/hosts_olsr"`

### Notes for Claude

- Always use SSH shortcuts (`ssh vm-1`, `ssh hap-2`) instead of full connection strings
- All hap-2 connections automatically proxy through vm-1 (configured in ~/.ssh/config)
- Use `cat | ssh` for file transfers to avoid sftp-server dependency
- AREDN devices use port 2222 (not default 22) for SSH
- The mesh network uses 10.x.x.x IP addresses (AREDN private range)
