# AREDN Phonebook — Functional Specification
## With Integrated Mesh Quality Monitoring

**Audience:** C developers and AREDN network operators
**Goal:** AREDN-Phonebook with SIP server and lightweight mesh quality monitoring while maintaining its core emergency phonebook functionality

---

> **⚠️ SCOPE NOTE:** This document describes the **agent** component only. Backend services (remote collector, historical trending, web dashboards, alerting systems) are implemented in a separate project and are not part of this codebase. The agent exposes all data via local CGI endpoints for consumption by any backend system.

---

## Executive Summary

AREDN Phonebook Enhanced extends the existing emergency phonebook SIP server with integrated mesh network quality monitoring. This unified solution provides:

1. **Original Phonebook Features** - Emergency-resilient directory services for SIP phones
2. **Mesh Health Monitoring** - Real-time path quality metrics for network operators
3. **Unified Architecture** - Single daemon, shared resources, minimal overhead
4. **Emergency Focus** - Degradation modes ensure phonebook always works

The enhanced system maintains backward compatibility while adding optional monitoring that helps operators identify network issues before they affect emergency communications.

---

## 1) Architecture Overview

### 1.1 Unified Process Model

```
AREDN-Phonebook-Enhanced (single process)
├── Core SIP Proxy Module
│   ├── REGISTER handler
│   ├── INVITE routing
│   └── User database
├── Phonebook Module
│   ├── CSV fetcher thread
│   ├── XML generator
│   └── Status updater thread
├── Passive Safety Module
│   └── Self-healing thread
└── UAC Phone Testing Module
    ├── OPTIONS ping test
    ├── RTT/jitter measurement
    └── Bulk phone testing
```

### 1.2 Thread Architecture

- **Main Thread:** SIP message processing
- **Fetcher Thread:** Phonebook updates
- **Updater Thread:** User status management
- **Safety Thread:** Self-healing operations
- **UAC Thread:** SIP phone testing (OPTIONS ping and optional INVITE)

### 1.3 Resource Sharing

- Shared logging system (`log_manager`)
- Shared configuration (`/etc/sipserver.conf`)
- Shared memory pools for efficiency
- Unified signal handling and lifecycle

---

## 2) Deployment

### 2.1 Test Environment

**Primary Test Node:**
- **Hostname:** `hb9bla-vm-1.local.mesh`
- **Role:** x86_64 test server
- **SSH Access:** Port 2222 (`ssh -p 2222 root@hb9bla-vm-1.local.mesh`)
- **Package:** AREDN-Phonebook x86_64 .ipk

**Deployment Workflow:**

1. **Download artifact from GitHub Actions:**
   ```bash
   # Get the latest x86 build from a specific Actions run
   gh run download <RUN_ID> --name "AREDN-Phonebook-x86-UAC-<BUILD>-<COMMIT>.ipk" --dir ./build-output

   # Example:
   gh run download 18399485975 --name "AREDN-Phonebook-x86-UAC-133-2cb71620c874762a6283d6b11e83bab1869d18e8" --dir ./build-output
   ```

2. **Upload to test node:**
   ```bash
   # Upload via SCP (port 2222, legacy protocol)
   scp -P 2222 -O ./build-output/AREDN-Phonebook-*.ipk root@hb9bla-vm-1.local.mesh:/tmp/
   ```

3. **Install on test node:**
   ```bash
   # SSH into the server
   ssh -p 2222 root@hb9bla-vm-1.local.mesh

   # Check for existing installation
   opkg list-installed | grep -i phonebook

   # Remove old version if present
   opkg remove AREDN-Phonebook

   # Install new package
   opkg install /tmp/AREDN-Phonebook-x86-UAC-*.ipk
   ```

4. **Verify installation:**
   ```bash
   # Check installed version
   opkg list-installed | grep -i phonebook

   # Verify process is running
   ps | grep AREDN-Phonebook | grep -v grep

   # Check logs for startup messages
   logread | tail -50

   # Monitor real-time logs
   logread -f | grep -E '(PHONEBOOK|UAC|SIP|CONFIG)'
   ```

**Testing UAC Monitoring Features:**

```bash
# Monitor UAC bulk testing cycle (runs every 60 seconds by default)
# Shows ICMP ping and SIP OPTIONS tests with RTT/jitter/loss statistics
logread -f | grep -E '(UAC|✓|✗|RTT|jitter|loss)'
```

### 2.2 Production Deployment

**Target Architectures:**
- `ath79/generic` - MikroTik and similar MIPS routers
- `x86/64` - x86_64 systems and VMs

**GitHub Actions Build:**
- Triggered on tag pushes or PR merges
- Artifacts available in Actions runs
- Release packages published for tagged versions

---

## 3) Core Features

### 2.1 SIP Proxy Server (Primary Functionality)
- ✅ **SIP REGISTER handling** - User registration with 3600-second expiry
- ✅ **INVITE routing** - Call establishment with DNS resolution (`{user_id}.local.mesh`)
- ✅ **Call session tracking** - BYE, CANCEL, ACK, and OPTIONS method support
- ✅ **Stateful proxy** - Tracks active calls and routes responses correctly
- ✅ **No authentication** - Trust-based model for mesh networks
- ✅ **Error handling** - 404, 503, 481, 501 responses with appropriate recovery
- ✅ **UDP port 5060** - Standard SIP port binding

### 2.2 Phonebook Module (Core Directory Service)
- ✅ **Automatic CSV fetching** - Downloads phonebook every 1 hour (default: 3600 seconds) from configured servers
- ✅ **XML directory generation** - Creates Yealink-compatible directory at `/www/arednstack/phonebook_generic_direct.xml`
- ✅ **CSV storage** - Persists phonebook data at `/www/arednstack/phonebook.csv` for emergency boot
- ✅ **Emergency boot mode** - Loads existing phonebook on startup for immediate service
- ✅ **Flash-friendly operation** - Minimal writes, 16-byte hash-based change detection
- ✅ **User status tracking** - Updates registered user availability every 10 minutes (default: 600 seconds)
- ✅ **Webhook endpoints** - `/cgi-bin/loadphonebook` (trigger fetch), `/cgi-bin/showphonebook` (view entries)

### 2.3 Passive Safety System (Self-Healing)
- ✅ **Configuration validation** - Auto-corrects invalid fetch/update intervals (minimum 5 min for fetch, 1 min for updates)
- ✅ **Thread health monitoring** - Detects hung threads (30 min timeout for fetcher, 20 min for updater)
- ✅ **Stale session cleanup** - Terminates call sessions >2 hours old (7200 seconds)
- ✅ **Resource leak prevention** - RAII-style cleanup, static allocation, orphaned file cleanup
- ✅ **Continuous operation** - Individual thread failures don't stop entire service, automatic thread restart
- ✅ **Silent operation** - Runs in background every 5 minutes without user intervention
- ✅ **Graceful degradation** - Doubles fetch interval when call load exceeds 80% capacity
- ✅ **File integrity checks** - Validates phonebook files (minimum 50 bytes), automatic rollback on corruption
- ✅ **Default fallback server** - Uses `localnode.local.mesh:80/phonebook.csv` if no servers configured

### 2.4 UAC Phone Testing (Optional Add-On)
- ✅ **OPTIONS ping test** - Non-intrusive phone reachability testing (default mode)
- ✅ **INVITE call test** - Optional brief ring test (disabled by default)
- ✅ **RTT/jitter measurement** - RFC3550-like statistics (min/max/avg RTT, jitter, packet loss)
- ✅ **Bulk testing thread** - Automated testing of all registered phones
- ✅ **Configurable intervals** - Test frequency, ping count, and INVITE test phone prefix filtering
- ✅ **CGI endpoint** - `/cgi-bin/uac_ping` for on-demand tests

---

## 3) Module Specifications

### 3.1 UAC (User Agent Client) Module Structure

```c
// Existing UAC (User Agent Client) module for SIP phone testing
Phonebook/src/uac/
├── uac.h                    // UAC core API
├── uac.c                    // UAC state machine (INVITE/CANCEL/BYE)
├── uac_sip_builder.c        // SIP message builders
├── uac_sip_parser.c         // SIP response parser
├── uac_ping.h               // ✅ SIP OPTIONS/PING ping API
├── uac_ping.c               // ✅ RTT/jitter measurement (RFC3550-like)
└── uac_bulk_tester.c        // ✅ Bulk phone testing with dual-mode
```

### 3.2 SIP Protocol Handling (`sip_core/`)

**Purpose**: Handles all SIP protocol message processing, parsing, and routing.

#### 3.2.1 Message Parsing Functions
- `extract_sip_header()`: Extracts specific SIP headers from messages
- `parse_user_id_from_uri()`: Parses user ID from SIP URIs
- `extract_uri_from_header()`: Extracts complete URIs from headers
- `extract_tag_from_header()`: Extracts tag parameters from headers
- `get_sip_method()`: Identifies SIP method (REGISTER, INVITE, etc.)

#### 3.2.2 Registration Process
**REGISTER Method Handling:**
1. Client sends `REGISTER` to server
2. Server extracts user ID and display name from SIP headers
3. Calls `add_or_update_registered_user()` to update database
4. User marked as active and available for calls
5. Server responds `200 OK` with 3600-second expiry
6. **No authentication required** (mesh network trust model)

#### 3.2.3 Call Establishment Process
**INVITE Method Handling:**
1. Client sends `INVITE` with target user ID
2. Server looks up target using `find_registered_user()`
3. **DNS resolution**: Constructs hostname `{user_id}.local.mesh`
4. Creates call session using `create_call_session()` for tracking
5. Server sends "100 Trying" response to caller
6. INVITE proxied to resolved callee address with reconstructed Request-URI
7. Callee responses proxied back to caller via session data
8. Call state updated based on response codes

#### 3.2.4 Call Termination Process
**BYE Method Handling:**
- Finds call session by Call-ID
- Determines caller vs callee by comparing addresses
- BYE proxied to other party
- Server responds "200 OK" to BYE sender
- Call session terminated and resources freed

**CANCEL Method Handling:**
- Only valid for INVITE_SENT or RINGING states
- Proxies CANCEL to callee
- Responds with "200 OK"
- Terminates call session

**ACK Method:**
- Acknowledges call establishment
- Proxies ACK to callee for ESTABLISHED calls

**OPTIONS Method:**
- Capability negotiation
- Responds with "200 OK" and supported methods

#### 3.2.5 Response Handling
- Processes SIP responses (200 OK, 180 Ringing, etc.)
- Routes responses back to original caller using stored call session data
- Updates call session state based on response codes
- Handles error responses (4xx, 5xx) by terminating sessions

#### 3.2.6 Address Resolution
- Uses DNS resolution for call routing
- Constructs hostnames: `{user_id}.local.mesh`
- Always uses port 5060 for SIP communication
- Falls back to "404 Not Found" if resolution fails

---

## 4) Configuration Schema

### 4.1 Current /etc/sipserver.conf

```ini
# ===================================================================
# Phonebook Configuration
# ===================================================================
[phonebook]
# Phonebook fetch interval in seconds
# How often to download the phonebook CSV from configured servers
# Default: 3600 (1 hour)
# Minimum: 300 (5 minutes) - enforced by passive safety
PB_INTERVAL_SECONDS=3600

# Status update interval in seconds
# How often to check and update registered user availability
# Default: 600 (10 minutes)
# Minimum: 60 (1 minute) - enforced by passive safety
STATUS_UPDATE_INTERVAL_SECONDS=600

# Phonebook servers (comma-separated list)
# Format: hostname1.local.mesh,hostname2.local.mesh
# Default: localnode.local.mesh (if none configured)
servers = pb1.local.mesh,pb2.local.mesh

# Flash protection mode
# 1 = enabled (hash-based change detection, minimal writes)
# 0 = disabled (write on every fetch)
# Default: 1
flash_protection = 1

# ===================================================================
# UAC (User Agent Client) - SIP Phone Testing
# ===================================================================
# UAC Test Interval (in seconds)
# The interval at which the UAC bulk test thread will test all reachable phones.
# Default: 60 (1 minute)
UAC_TEST_INTERVAL_SECONDS=60

# UAC Call Test (INVITE - rings phone briefly)
# Enable/disable the INVITE call test that actually rings phones.
# When disabled, only OPTIONS ping is used (non-intrusive).
# 0 = disabled (OPTIONS ping only), 1 = enabled (OPTIONS + INVITE fallback)
# Default: 0 (disabled - OPTIONS ping only)
UAC_CALL_TEST_ENABLED=0

# UAC Ping Test Settings (ICMP - Network Layer)
# Number of ICMP ping requests to send per phone for latency measurement.
# Tests network-layer connectivity and measures RTT/jitter at the IP level.
# Set to 0 to disable ICMP ping testing.
# Range: 0-20, Default: 5
UAC_PING_COUNT=5

# UAC Options Test Settings (SIP OPTIONS - Application Layer)
# Number of SIP OPTIONS requests to send per phone for latency measurement.
# Tests application-layer connectivity and measures RTT/jitter at the SIP level.
# Each OPTIONS request measures round-trip time. Multiple requests allow jitter calculation.
# Set to 0 to disable OPTIONS testing.
# Range: 0-20, Default: 5
UAC_OPTIONS_COUNT=5

# UAC Test Phone Number Prefix (INVITE test only)
# Only perform INVITE tests on phone numbers starting with this prefix.
# Ping and OPTIONS tests will run for ALL phones regardless of prefix.
# This allows selective calling tests while monitoring all phones.
# Default: 4415
UAC_TEST_PREFIX=4415
```

---

## 5) CGI Command Reference

The AREDN-Phonebook exposes several CGI endpoints for manual operations and testing. All endpoints are accessible via HTTP GET requests.

### 5.1 Phonebook Management

**Trigger Phonebook Fetch**
```bash
curl "http://node.local.mesh/cgi-bin/loadphonebook"
```
- Immediately triggers phonebook CSV download from configured servers
- Sends SIGUSR1 signal to daemon
- Returns JSON status response

**Show Phonebook Status**
```bash
curl "http://node.local.mesh/cgi-bin/showphonebook"
```
- Returns current phonebook entries and statistics
- JSON response with user count and registration status
- Useful for monitoring and debugging

### 5.2 UAC Testing (Phone Monitoring)

**Single-Phone Ping/Options Test** ✅ (Non-Intrusive)
```bash
# Test with default count (5 requests)
curl "http://node.local.mesh/cgi-bin/uac_ping?target=4415001"

# Test with custom count (1-20 requests)
curl "http://node.local.mesh/cgi-bin/uac_ping?target=4415001&count=10"
```
- Runs both ICMP ping and SIP OPTIONS tests
- Tests network layer (ping) and application layer (SIP)
- Returns RTT, jitter, and packet loss statistics
- **Non-intrusive** - does not ring the phone
- Check results: `logread | grep UAC_PING`

**Single-Phone INVITE Test** ⚠️ (Rings Phone)
```bash
curl "http://node.local.mesh/cgi-bin/uac_test?target=4415001"
```
- Triggers actual SIP INVITE (will ring the phone)
- Used for end-to-end call path validation
- Automatically cancels/hangs up when phone responds
- **Intrusive** - use sparingly
- Check results: `logread | grep UAC`

**View Bulk Test Results** (Read-Only)
```bash
# View in browser
http://node.local.mesh/cgi-bin/uac_test_all

# Or fetch with curl
curl "http://node.local.mesh/cgi-bin/uac_test_all"
```
- Displays results from the most recent automated bulk test cycle
- Shows HTML table with phone number, name, ping/options status, RTT, and jitter
- Color-coded status (green=online, red=offline, gray=no DNS)
- Auto-refreshes every 60 seconds
- **Read-only** - does not trigger new tests (tests run automatically every `UAC_TEST_INTERVAL_SECONDS`)

### 5.3 CGI Response Format

All CGI endpoints return JSON responses:

**Success Response:**
```json
{
  "status": "success",
  "message": "UAC ping/options test triggered to 4415001 with 5 requests",
  "pid": 12345,
  "target": "4415001",
  "count": 5,
  "note": "Check logs with: logread | grep UAC_PING"
}
```

**Error Response:**
```json
{
  "status": "error",
  "message": "Missing target parameter. Usage: /cgi-bin/uac_ping?target=441422&count=5"
}
```

### 5.4 Testing Workflow Example

```bash
# 1. View all bulk test results in browser (recommended)
# Open in web browser:
http://hb9bla-vm-1.local.mesh/cgi-bin/uac_test_all

# 2. Check phonebook status
curl "http://hb9bla-vm-1.local.mesh/cgi-bin/showphonebook"

# 3. Test a specific phone (non-intrusive)
curl "http://hb9bla-vm-1.local.mesh/cgi-bin/uac_ping?target=4415001&count=5"

# 4. Monitor results in real-time logs
ssh -p 2222 root@hb9bla-vm-1.local.mesh "logread -f | grep UAC_PING"

# 5. If needed, test with actual INVITE (rings phone)
curl "http://hb9bla-vm-1.local.mesh/cgi-bin/uac_test?target=4415001"
```

### 5.5 Notes

- **DNS Requirement:** All UAC tests require DNS resolution of `{phone_number}.local.mesh`
- **Signal Handling:** CGI scripts use SIGUSR2 to trigger daemon operations
- **Bulk Testing:** Automated bulk tests run every `UAC_TEST_INTERVAL_SECONDS` (default: 60s)
- **Prefix Filtering:** `UAC_TEST_PREFIX` only affects INVITE tests, not ping/options tests

---

## 6) Error Handling

### 5.1 SIP Protocol Errors

The SIP proxy implements comprehensive error handling for protocol-level issues:

**404 Not Found:**
- **Trigger:** User not registered or DNS resolution failed
- **Behavior:** Sends "404 Not Found" response to caller
- **Resolution:** Verify target user is registered and DNS is operational

**503 Service Unavailable:**
- **Trigger:** Maximum call sessions reached (`MAX_CALL_SESSIONS` limit)
- **Behavior:** Rejects new INVITE requests
- **Resolution:** Wait for active calls to terminate or increase session limit

**481 Call/Transaction Does Not Exist:**
- **Trigger:** BYE or CANCEL received for unknown Call-ID
- **Behavior:** Responds with "481" error
- **Resolution:** Indicates client state mismatch, usually benign

**501 Not Implemented:**
- **Trigger:** Unsupported SIP methods received
- **Behavior:** Responds with "501 Not Implemented"
- **Supported Methods:** REGISTER, INVITE, ACK, BYE, CANCEL, OPTIONS

### 5.2 System-Level Errors

**File Access Errors:**
- **Configuration Missing:** Uses default configuration values
- **Phonebook CSV Unreadable:** Continues with existing cached data
- **XML Generation Failure:** Retries on next fetch cycle
- **Hash File Corruption:** Regenerates hash on next successful fetch

**Network Errors:**
- **Phonebook Server Unreachable:** Tries next configured server in sequence
- **DNS Resolution Failure:** Returns 404 to caller, logs warning
- **Socket Binding Failure:** Fatal error, logs and exits (requires restart)
- **Network Timeout:** Abandons current operation, retries next cycle

**Resource Limit Errors:**
- **Maximum Users Reached (`MAX_REGISTERED_USERS=256`):** Rejects new registrations, logs warning
- **Maximum Call Sessions (`MAX_CALL_SESSIONS=10`):** Returns 503 to new callers
- **Memory Allocation Failure:** Logs error, attempts graceful degradation
- **Thread Creation Failure:** Fatal error for critical threads, logs and exits

### 5.3 Recovery Mechanisms

**Configuration Self-Correction:**
- **Invalid Fetch Interval:** Adjusts to minimum 300 seconds (5 minutes)
- **Invalid Update Interval:** Adjusts to minimum 60 seconds (1 minute)
- **No Servers Configured:** Logs error, uses fallback default
- Passive safety system validates and corrects parameters automatically

**Hash-Based Change Detection:**
- **Purpose:** Prevents unnecessary processing of unchanged phonebooks
- **Mechanism:** Calculates 16-byte conceptual hash of CSV content
- **Storage:** Hash stored at `/www/arednstack/phonebook.csv.hash`
- **Behavior:** Skips user database update if hash matches previous
- **Benefit:** Reduces CPU usage, prevents unnecessary flash writes, avoids race conditions

**File Integrity Protection:**
- **Backup Strategy:** Creates `.backup` file before any phonebook update
- **Temporary Files:** Uses `.temp` extension for atomic operations
- **Corruption Detection:** Validates files are at least 50 bytes
- **Automatic Rollback:** Restores `.backup` if update fails
- **Orphan Cleanup:** Removes leftover `.backup` and `.temp` files every 5 minutes

**Resource Cleanup:**
- **Stale Call Sessions:** Passive safety terminates sessions >2 hours old (7200 seconds)
- **Hung Threads:** Automatic detection and restart
  - **Fetcher thread:** 30-minute timeout (1800 seconds)
  - **Updater thread:** 20-minute timeout (1200 seconds)
- **File Handle Leaks:** Ensures all file operations use RAII-style cleanup
- **Memory Cleanup:** Static allocation minimizes leak potential

**Continuous Operation:**
- **Phonebook Fetch Failure:** Continues with cached data indefinitely
- **Individual Thread Failure:** Other threads continue operation
- **Transient Errors:** Logged but do not interrupt service
- **Emergency Boot:** Loads existing phonebook on startup for immediate service

---

# PLANNED FEATURES

## 9) Mesh Monitoring Features (Future - Phase 2)

### 9.1 Monitoring Module Structure (PLANNED)

```c
// Future mesh monitoring (mesh-wide network quality)
Phonebook/src/mesh_monitor/
├── mesh_monitor.h           // Public API
├── mesh_monitor.c           // Main coordinator
├── routing_adapter.c        // OLSR/Babel interface
├── probe_engine.c          // UDP probe sender/receiver
├── metrics_calculator.c    // RFC3550 jitter, loss, RTT
├── health_reporter.c       // JSON generation and batching
├── software_health.c       // Software health tracking
└── monitor_config.c        // Configuration parser
```

### 9.3 New Monitoring Features (PLANNED)
- 🆕 **Mesh Path Quality:** Loss, RTT, jitter (RFC3550) measurements
- 🆕 **Routing Awareness:** OLSR/Babel link quality metrics
- 🆕 **Hop-by-Hop Analysis:** Per-link quality in multi-hop paths
- 🆕 **Network Health Dashboard:** JSON API for monitoring tools
- 🆕 **Degradation Detection:** Early warning for voice quality issues
- 🆕 **Historical Trending:** Rolling window statistics

---

## 10) Enhanced Configuration (Future)

### 10.1 Mesh Monitor Configuration Section (PLANNED)

```ini
# ===================================================================
# Mesh Monitor - Network-Wide Quality Monitoring (Future)
# ===================================================================
[mesh_monitor]
enabled = 0               # Disabled by default (SIP phone testing is separate)
mode = lightweight        # lightweight | full | disabled

# Network status measurement
network_status_interval_s = 40      # How often to measure network status (UDP probes)
probe_window_s = 5                  # Duration of each probe burst
agent_discovery_interval_s = 3600   # Agent discovery scan interval (1 hour)
agent_discovery_timeout_s = 2       # Timeout per discovery probe
max_discovered_agents = 100         # Maximum agents to cache
max_probe_kbps = 80                 # Bandwidth limit per probe
probe_port = 40050                  # UDP port for probes
dscp_ef = 1                         # Mark probes with DSCP EF (voice-like)

# Phonebook health status monitoring
phonebook_health_update_s = 60        # How often passive_safety updates /tmp/meshmon_health.json
phonebook_health_report_s = 14400     # Report to collector every 4 hours (or on significant change)
health_change_threshold_cpu = 20      # Report if CPU change > 20%
health_change_threshold_mem_mb = 10   # Report if memory change > 10 MB
crash_detection = 1                   # Monitor and report crashes immediately
thread_monitoring = 1                 # Track thread health and responsiveness
restart_threshold = 3                 # Max restarts before alerting

# Routing daemon integration
routing_daemon = auto           # auto | olsr | babel
routing_cache_s = 5             # Cache routing info for N seconds

# Remote reporting (optional)
network_status_report_s = 40    # Report network status every 40 seconds
collector_url =                 # Optional: external collector endpoint
```

### 10.2 Monitoring Modes (PLANNED)

- **Disabled:** No monitoring overhead (default for low-memory nodes)
- **Lightweight:** Agent discovery with basic metrics
- **Full:** Complete path analysis with hop-by-hop metrics

### 10.3 Agent Discovery Strategy (PLANNED)

**Purpose:** Discover all nodes with agents (phonebook servers or probe responders) mesh-wide, not just direct neighbors.

**Why:** For network management we need an overview across the entire topology.

**Discovery Process:**

1. **Topology Query** (every 1 hour):
   - Query OLSR: `GET http://127.0.0.1:9090/topology`
   - Extract all unique node IPs from mesh topology

2. **Agent Detection** (one-time per discovered node):
   - Send ONE test probe (UDP packet) to each IP (small) only to active nodes (topology)
   - Wait up to 10 seconds for echo response
   - If responds: node has agent (phonebook or responder)

3. **Cache Management** (**RAM only - `/tmp/`**):
   - Save discovered agents to `/tmp/aredn_agent_cache.txt` (tmpfs)
   - Zero flash memory writes
   - Cache cleared on reboot → fresh discovery

4. **Regular Monitoring** (every 40s):
   - Probe only cached agent list
   - Measure RTT, jitter, packet loss
   - Update network status JSON

---

## 11) JSON Wire Protocols (Future)

### 11.1 Enhanced Phonebook Status (PLANNED)

**Endpoint:** `GET /cgi-bin/showphonebook`

```json
{
  "phonebook": {
    "entries": 47,
    "last_updated": "2025-09-29T18:45:00Z",
    "source": "pb1.local.mesh"
  },
  "sip_status": {
    "registered_users": 12,
    "active_calls": 2,
    "uptime_seconds": 86400
  },
  "software_health": {
    "status": "healthy",
    "uptime_seconds": 86400,
    "restart_count": 0,
    "last_restart": null,
    "threads": {
      "main": {"responsive": true, "cpu_pct": 1.2},
      "fetcher": {"responsive": true, "last_heartbeat": "2025-09-29T18:45:00Z"},
      "monitor": {"responsive": true, "probe_success_rate": 98.5},
      "safety": {"responsive": true, "recoveries_performed": 2}
    },
    "memory": {
      "rss_mb": 8.2,
      "heap_mb": 6.1,
      "growth_rate_mb_per_hour": 0.1,
      "leak_suspected": false
    },
    "errors": {
      "crash_count_24h": 0,
      "last_crash": null,
      "sip_errors_per_hour": 1,
      "probe_failures_per_hour": 2
    }
  },
  "mesh_health": {
    "mode": "lightweight",
    "neighbors": 5,
    "avg_link_quality": 0.87,
    "problem_paths": [
      {"to": "node-K", "issue": "high_jitter", "jitter_ms": 45}
    ]
  }
}
```

### 11.2 Connection Check Query (PLANNED)

**Endpoint:** `GET /cgi-bin/connectioncheck?target=W6XYZ-2`

Queries existing network probe data for specific destination (does not trigger new probe):

**Response:**
```json
{
  "target": "W6XYZ-2",
  "last_probed": "2025-09-29T18:44:00Z",
  "status": "degraded",
  "path_result": {
    "rtt_ms_avg": 86.2,
    "jitter_ms": 15.3,
    "loss_pct": 2.1
  },
  "hop_result": {
    "hops": [
      {"seq": 0, "node": "node-A", "link_type": "RF", "etx": 1.19},
      {"seq": 1, "node": "node-D", "link_type": "tunnel", "etx": 1.0, "rtt_ms": 45.2},
      {"seq": 2, "node": "node-H", "link_type": "RF", "etx": 2.15, "warning": "High ETX"}
    ]
  },
  "recommendation": "Check node-H RF link (ETX=2.15)"
}
```

---

## 12) Monitoring Access Architecture (Future)

### 12.1 Local-First Design Principle

**All monitoring data is accessible locally via CGI endpoints first.** Remote reporting to a centralized collector is optional and uses the same data.

### 12.2 Access Methods

#### 12.2.1 Local CGI Access (Primary, Always Available)

**Interface:** HTTP CGI scripts on the agent (`uhttpd` on OpenWrt)
**Endpoints:** See Section 11 for all endpoints
**Access:** `http://node.local.mesh/cgi-bin/...`
**Dependencies:** None - works standalone
**Use cases:**
- Local troubleshooting with `curl`
- Web dashboard on node itself
- Manual monitoring and diagnostics
- Emergency access when collector unavailable

**Benefits:**
- Zero network overhead
- Instant access to current state
- No external dependencies
- Works even if mesh partitioned

#### 12.2.2 Remote Reporting (Optional, Centralized Monitoring)

> **Note:** Remote reporting configuration is documented here for agent completeness, but the backend collector is implemented in a separate project.

**Interface:** HTTP POST to collector endpoint
**Endpoint:** `POST http://collector.local.mesh:5000/ingest` (example)
**Frequency:** Configurable (default: health every 5min, probes every 40s)
**Dependencies:** External backend collector (not part of this project)
**Use cases:**
- Mesh-wide visibility
- Historical trend analysis
- Automated issue detection
- Alerting and notifications

**Benefits:**
- Centralized dashboard (backend)
- Pattern detection across nodes (backend)
- Long-term data retention (backend)
- Automated alerting (backend)

---

## 13) Network Behavior & Resource Budgets (Future)

### 13.1 Enhanced Resource Targets

| Component | Original | With Monitoring | Notes |
|-----------|----------|-----------------|-------|
| Binary Size | ~800 KB | ~1.1 MB | +300 KB for monitoring |
| RAM (idle) | 4-6 MB | 8-10 MB | +4 MB for probe buffers |
| RAM (peak) | 8-10 MB | 14-16 MB | During probe windows |
| CPU (average) | <2% | <5% | MIPS single-core |
| Flash Writes | 1-2/day | 2-3/day | Quality history cache |
| Network BW | ~1 KB/s | ~10 KB/s | During probe windows |

### 13.2 Degradation Strategy

```
IF (memory < 64MB) THEN
    Disable monitoring entirely
ELSE IF (memory < 128MB) THEN
    Use lightweight mode (neighbors only)
ELSE
    Full monitoring available
```

---

## 14) Routing Daemon Integration (Future)

### 14.1 OLSR Integration (AREDN primary)

```c
// Query jsoninfo plugin
GET http://127.0.0.1:9090/routes
GET http://127.0.0.1:9090/neighbors

// Parse for phonebook correlation
typedef struct {
    char callsign[32];      // From phonebook
    char node_name[64];     // From OLSR
    float link_quality;      // LQ metric
    float neighbor_lq;       // NLQ metric
    float etx;              // Expected Transmission Count
    int is_registered;       // SIP registration status
} unified_peer_info_t;
```

### 14.2 Babel Support (Future AREDN)

- Control socket at `/var/run/babeld.sock`
- Text protocol parsing for routes and neighbors
- Automatic detection and fallback

### 14.3 Link Technology Detection (RF vs Tunnel)

**Goal:** Identify whether each hop uses wireless/RF or wired/tunnel technology to help isolate bottlenecks.

**Implementation Strategy:**

**Option 1: Query Routing Daemon Interface Info**
```c
// OLSR jsoninfo interfaces endpoint
GET http://127.0.0.1:9090/interfaces

// Response example:
{
  "interfaces": [
    {
      "name": "wlan0",
      "ipv4Address": "10.0.0.1",
      "olsr4": "UP"
    },
    {
      "name": "tun50",
      "ipv4Address": "172.16.50.1",
      "olsr4": "UP"
    }
  ]
}

// Map neighbor IPs to interface names
typedef struct {
    char neighbor_ip[16];
    char interface[16];      // "wlan0", "tun50", etc.
    char link_type[16];      // "RF", "tunnel", "ethernet"
} neighbor_interface_t;
```

**Option 2: Interface Name Pattern Matching**
```c
// Simple heuristic classification
const char* classify_link_type(const char* iface) {
    if (strncmp(iface, "wlan", 4) == 0) return "RF";
    if (strncmp(iface, "tun", 3) == 0) return "tunnel";
    if (strncmp(iface, "eth", 3) == 0) return "ethernet";
    if (strncmp(iface, "br-", 3) == 0) return "bridge";
    return "unknown";
}
```

**Implementation Priority:**
1. Start with Option 2 (interface name matching) - simplest, no external dependencies
2. Add Option 1 (OLSR interface query) when routing adapter is implemented
3. Consider Option 3 (AREDN sysinfo) for maximum accuracy in Phase 3

---

# APPENDICES

## Appendix A: File Structure

```
Phonebook/
├── src/
│   ├── main.c                    (modified)
│   ├── common.h                   (modified)
│   ├── passive_safety/           (enhanced)
│   │   └── passive_safety.c      (monitoring checks added)
│   ├── uac/                      (✅ COMPLETE - Phase 1)
│   │   ├── uac.h                 // UAC core API
│   │   ├── uac.c                 // UAC state machine
│   │   ├── uac_sip_builder.c     // SIP message builders
│   │   ├── uac_sip_parser.c      // SIP response parser
│   │   ├── uac_ping.h            // OPTIONS/PING API
│   │   ├── uac_ping.c            // RTT/jitter measurement
│   │   └── uac_bulk_tester.c     // Bulk phone testing
│   └── mesh_monitor/             (PLANNED - Phase 2)
│       ├── mesh_monitor.h
│       ├── mesh_monitor.c
│       ├── routing_adapter.c
│       ├── probe_engine.c
│       ├── metrics_calculator.c
│       ├── health_reporter.c
│       ├── software_health.c
│       └── monitor_config.c
├── files/
│   ├── etc/
│   │   ├── sipserver.conf        (enhanced)
│   │   └── config/
│   │       └── meshmon           (PLANNED - UCI config)
│   └── www/
│       └── cgi-bin/
│           ├── loadphonebook     (existing - trigger fetch)
│           ├── showphonebook     (existing - show entries)
│           ├── uac_test          (existing - trigger INVITE test)
│           ├── uac_test_all      (existing - trigger bulk INVITE test)
│           ├── uac_ping          (✅ COMPLETE - on-demand OPTIONS ping test)
│           ├── health            (PLANNED - phonebook health status)
│           ├── network           (PLANNED - network performance)
│           ├── crash             (PLANNED - crash reports)
│           └── connectioncheck   (PLANNED - on-demand probe trigger)
└── Makefile                       (updated dependencies)
```

---

## Appendix B: Interface Specifications for Implementers

This appendix provides complete technical specifications for the two primary interfaces in the mesh monitoring system. Use these specifications to implement:
- **Lightweight monitoring agents** (alternative implementations)
- **Centralized collectors** (backend aggregation systems)
- **Integration tools** (dashboards, alerting systems)

### B.1 Agent-to-Agent Interface (UDP Probe Protocol) - PLANNED

#### B.1.1 Overview

**Purpose:** Network quality measurement between mesh nodes (RTT, jitter, packet loss)
**Transport:** UDP
**Port:** 40050 (configurable via `probe_port` in config)
**Protocol:** Echo request/response (similar to ICMP ping but over UDP)
**Bi-directional:** All agents act as both probe sender and responder

#### B.1.2 Probe Packet Format

```c
// Wire format (network byte order - big endian)
typedef struct {
    uint32_t sequence;           // Probe sequence number (0, 1, 2, ...)
    uint32_t timestamp_sec;      // Unix timestamp seconds (when probe sent)
    uint32_t timestamp_usec;     // Microseconds component (0-999999)
    char src_node[64];          // Source node identifier (null-terminated)
} probe_packet_t;

// Total packet size: 76 bytes
// - 4 bytes: sequence
// - 4 bytes: timestamp_sec
// - 4 bytes: timestamp_usec
// - 64 bytes: src_node (null-terminated string)
```

**Byte Order:** All integer fields use **network byte order (big-endian)**. Use `htonl()` to convert to network byte order before sending, `ntohl()` to convert from network byte order when receiving.

#### B.1.3 Protocol Flow

```
Node A (Probe Sender)                    Node B (Probe Responder)
================                          ===================

1. Get neighbor list from routing daemon
   (OLSR jsoninfo or Babel socket)

2. For each neighbor to probe:

   Create probe packet:
   - sequence = probe_number (0-9)
   - timestamp = gettimeofday()
   - src_node = hostname

   Convert to network byte order ────────>  3. Listen on UDP port 40050

   Send to neighbor:40050 ──────────────>  4. Receive probe packet

   Record send_time                         Convert from network byte order

                                           5. Echo packet back immediately
                                              (no modification needed)

   6. Receive echoed packet  <──────────   Send same packet back to source

   Record receive_time

   Calculate metrics:
   - RTT = receive_time - send_time
   - Store for jitter calculation

7. After probe window (5s default):

   Calculate final metrics:
   - RTT_avg = average of all RTTs
   - Jitter = average absolute difference between consecutive RTTs (RFC3550)
   - Loss% = (probes_sent - probes_received) / probes_sent * 100

8. Store results in probe_history[]
   Export to /tmp/meshmon_network.json
```

#### B.1.4 Probe Timing

**Default Configuration:**
```ini
network_status_interval_s = 40    # Probe cycle every 40 seconds
probe_window_s = 5                # Wait 5 seconds for responses
neighbor_targets = 2              # Probe 2 neighbors per cycle
```

**Typical Probe Sequence:**
```
T+0s:    Send 10 probes to Neighbor 1 (100ms apart = 1 second total)
T+5s:    Calculate metrics for Neighbor 1
T+6s:    Send 10 probes to Neighbor 2
T+11s:   Calculate metrics for Neighbor 2
T+12s:   Export network.json
T+40s:   Start next probe cycle
```

#### B.1.5 Metrics Calculation (RFC3550)

**RTT (Round-Trip Time):**
```c
// For each received probe response:
long sec_diff = recv_time.tv_sec - sent_time.tv_sec;
long usec_diff = recv_time.tv_usec - sent_time.tv_usec;
float rtt_ms = (sec_diff * 1000.0) + (usec_diff / 1000.0);

// Average RTT:
rtt_ms_avg = sum(all_rtts) / count(received_probes);
```

**Jitter (Inter-arrival Jitter per RFC3550 Section 6.4.1):**
```c
// Simplified implementation - mean absolute difference:
float jitter = 0.0;
for (int i = 1; i < rtt_count; i++) {
    float diff = rtt[i] - rtt[i-1];
    jitter += (diff < 0 ? -diff : diff);  // Absolute value
}
jitter_ms = jitter / (rtt_count - 1);
```

**Packet Loss:**
```c
float loss_pct = 100.0 * (1.0 - (received_count / sent_count));
```

#### B.1.6 Implementation Example (Lightweight Agent)

**Minimal UDP Probe Responder (Python):**
```python
#!/usr/bin/env python3
import socket

# Simple UDP echo responder for mesh monitoring
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', 40050))

print("Mesh probe responder listening on UDP port 40050...")

while True:
    data, addr = sock.recvfrom(1024)
    # Echo packet back unchanged
    sock.sendto(data, addr)
```

**Minimal Probe Sender (Python):**
```python
#!/usr/bin/env python3
import socket
import struct
import time

def send_probe(target_ip, target_port=40050):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)

    # Create probe packet
    sequence = 0
    now = time.time()
    timestamp_sec = int(now)
    timestamp_usec = int((now - timestamp_sec) * 1000000)
    src_node = socket.gethostname()[:63]  # Max 63 chars + null

    # Pack to network byte order
    packet = struct.pack('!III64s',
                        sequence,
                        timestamp_sec,
                        timestamp_usec,
                        src_node.encode('utf-8'))

    # Send probe
    send_time = time.time()
    sock.sendto(packet, (target_ip, target_port))

    # Wait for echo
    try:
        data, addr = sock.recvfrom(1024)
        recv_time = time.time()
        rtt_ms = (recv_time - send_time) * 1000
        print(f"RTT to {target_ip}: {rtt_ms:.2f}ms")
        return rtt_ms
    except socket.timeout:
        print(f"Probe to {target_ip} timed out")
        return None

# Example usage
send_probe("10.124.142.47")
```

---

### B.2 Agent-to-Collector Interface (HTTP JSON API) - PLANNED

> **⚠️ BACKEND IMPLEMENTATION:** This section documents the interface for backend implementers. The collector server, database, dashboards, and alerting are implemented in a separate backend project, not in this agent codebase.

#### B.2.1 Overview

**Purpose:** Centralized collection of health and network data from multiple agents
**Transport:** HTTP POST
**Port:** Configurable (default 5000 suggested)
**Endpoint:** `POST /ingest`
**Content-Type:** `application/json`
**Schema:** meshmon.v1 (see B.2.3)
**Authentication:** None (trusted mesh network) or implement custom auth in backend

#### B.2.2 Configuration

```ini
[mesh_monitor]
# Enable remote reporting
network_status_report_s = 40        # Report network status every 40s
collector_url = http://collector.local.mesh:5000/ingest

# (Future implementation - not in current code)
```

#### B.2.3 Message Schemas (meshmon.v1)

**Schema Version:** All messages include `"schema": "meshmon.v1"`

##### **Message Type 1: agent_health**

Sent periodically (every 60s default) or on significant changes:

```json
{
  "schema": "meshmon.v1",
  "type": "agent_health",
  "node": "HB9BLA-HAP-2",
  "sent_at": "2025-09-30T21:04:35Z",
  "routing_daemon": "olsr",
  "lat": "47.47497",
  "lon": "7.76720",
  "grid_square": "JN37vl",
  "hardware_model": "MikroTik RouterBOARD 952Ui-5ac2nD (hAP ac lite)",
  "firmware_version": "3.25.8.0",
  "cpu_pct": 2.5,
  "mem_mb": 12.3,
  "queue_len": 0,
  "uptime_seconds": 18245,
  "restart_count": 0,
  "threads_responsive": true,
  "health_score": 100.0,
  "checks": {
    "memory_stable": true,
    "no_recent_crashes": true,
    "sip_service_ok": true,
    "phonebook_current": true
  },
  "sip_service": {
    "active_calls": 0,
    "registered_users": 3
  },
  "monitoring": {
    "probe_queue_depth": 0,
    "last_probe_sent": "2025-09-30T21:04:30Z"
  }
}
```

##### **Message Type 2: network_status**

Sent after each probe cycle (every 40s default):

```json
{
  "schema": "meshmon.v1",
  "type": "network_status",
  "node": "HB9BLA-HAP-2",
  "sent_at": "2025-09-30T21:05:00Z",
  "routing_daemon": "olsr",
  "probe_count": 2,
  "probes": [
    {
      "dst_node": "HB9EDI-ROUTER",
      "dst_ip": "10.124.142.47",
      "timestamp": "2025-09-30T21:04:55Z",
      "routing_daemon": "olsr",
      "rtt_ms_avg": 12.34,
      "jitter_ms": 1.23,
      "loss_pct": 0.0,
      "hop_count": 2,
      "path": [
        {
          "node": "HB9ABC-RELAY",
          "interface": "wlan0",
          "link_type": "RF",
          "lq": 1.0,
          "nlq": 0.98,
          "etx": 1.02,
          "rtt_ms": 0.0
        },
        {
          "node": "HB9EDI-ROUTER",
          "interface": "wlan0",
          "link_type": "RF",
          "lq": 0.95,
          "nlq": 1.0,
          "etx": 1.05,
          "rtt_ms": 0.0
        }
      ]
    }
  ]
}
```

##### **Message Type 3: crash_report**

Sent immediately when a crash is detected:

```json
{
  "schema": "meshmon.v1",
  "type": "crash_report",
  "node": "HB9BLA-HAP-2",
  "sent_at": "2025-09-30T21:10:00Z",
  "crash_time": "2025-09-30T21:09:58Z",
  "signal": 11,
  "signal_name": "SIGSEGV",
  "reason": "Segmentation fault at 0x00000000",
  "restart_count": 1,
  "uptime_before_crash": 18300
}
```

#### B.2.4 Collector Implementation Example

**Minimal Collector (Python Flask):**

```python
#!/usr/bin/env python3
from flask import Flask, request, jsonify
from datetime import datetime
import json

app = Flask(__name__)

@app.route('/ingest', methods=['POST'])
def ingest():
    data = request.get_json()

    # Validate schema
    if data.get('schema') != 'meshmon.v1':
        return jsonify({'error': 'Invalid schema'}), 400

    # Route by message type
    msg_type = data.get('type')
    node = data.get('node', 'unknown')

    print(f"[{datetime.now()}] Received {msg_type} from {node}")

    if msg_type == 'agent_health':
        handle_health(data)
    elif msg_type == 'network_status':
        handle_network(data)
    elif msg_type == 'crash_report':
        handle_crash(data)
    else:
        return jsonify({'error': 'Unknown message type'}), 400

    return jsonify({'status': 'accepted'}), 200

def handle_health(data):
    # Store in database, check health score, etc.
    print(f"  Health Score: {data.get('health_score')}")
    print(f"  Location: {data.get('lat')}, {data.get('lon')}")
    # TODO: Store in InfluxDB, Prometheus, etc.

def handle_network(data):
    # Process network probes
    for probe in data.get('probes', []):
        print(f"  Probe to {probe['dst_ip']}: RTT={probe['rtt_ms_avg']}ms, Loss={probe['loss_pct']}%")
    # TODO: Detect degraded paths, calculate network-wide metrics

def handle_crash(data):
    # Alert on crashes
    print(f"  CRASH: {data.get('signal_name')} - {data.get('reason')}")
    # TODO: Send alert, track crash patterns

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
```

**Run the collector:**
```bash
pip3 install flask
python3 collector.py
```

**Configure agents to report:**
```ini
# /etc/sipserver.conf
[mesh_monitor]
network_status_report_s = 40
collector_url = http://collector.local.mesh:5000/ingest
```

#### B.2.5 Collector Best Practices

**1. Message Deduplication:**
```python
# Track last message timestamp per node
last_seen = {}

def is_duplicate(node, sent_at):
    key = f"{node}:{sent_at}"
    if key in last_seen:
        return True
    last_seen[key] = datetime.now()
    return False
```

**2. Time-Series Storage:**
- **InfluxDB:** For metrics (RTT, jitter, health_score)
- **PostgreSQL/TimescaleDB:** For structured data and queries
- **Elasticsearch:** For log-style search and aggregation

**3. Alerting Rules:**
```python
def check_alerts(health_data):
    if health_data['health_score'] < 80:
        alert(f"Low health on {health_data['node']}")

    if health_data['checks']['memory_stable'] == False:
        alert(f"Memory leak detected on {health_data['node']}")

    for probe in network_data['probes']:
        if probe['loss_pct'] > 50:
            alert(f"High packet loss to {probe['dst_ip']}")
```

**4. Geographic Visualization:**
```python
# Generate GeoJSON for mapping
def to_geojson(health_messages):
    features = []
    for msg in health_messages:
        if msg['lat'] != 'unknown':
            features.append({
                'type': 'Feature',
                'geometry': {
                    'type': 'Point',
                    'coordinates': [float(msg['lon']), float(msg['lat'])]
                },
                'properties': {
                    'node': msg['node'],
                    'health_score': msg['health_score']
                }
            })
    return {'type': 'FeatureCollection', 'features': features}
```

#### B.2.6 Local CGI Access (Read-Only Query Interface)

Agents also expose data via HTTP GET for local access (always available, no configuration needed):

```bash
# Get agent health
curl http://node.local.mesh/cgi-bin/health

# Get network status
curl http://node.local.mesh/cgi-bin/network

# Get crash history
curl http://node.local.mesh/cgi-bin/crash
```

**Response Format:** Same JSON as sent to collector (meshmon.v1 schema)

**Use Cases:**
- Local troubleshooting without backend infrastructure
- Emergency access when collector is down
- Integration with other mesh services

---

### B.3 Implementation Checklist

#### B.3.1 For Lightweight Agent Implementers

**Minimum Requirements:**
- [ ] UDP listener on port 40050 that echoes packets back
- [ ] No parsing needed - just echo the exact bytes received
- [ ] Optional: Implement probe sender for testing
- [ ] Optional: Export metrics in meshmon.v1 JSON format

**Reference Implementation:** `Phonebook/src/mesh_monitor/probe_engine.c` (PLANNED)

#### B.3.2 For Collector Implementers

**Minimum Requirements:**
- [ ] HTTP server accepting POST on `/ingest`
- [ ] Parse JSON with `schema: "meshmon.v1"`
- [ ] Handle three message types: `agent_health`, `network_status`, `crash_report`
- [ ] Store or forward data as needed

**Optional Enhancements:**
- [ ] Time-series database integration (InfluxDB, Prometheus)
- [ ] Geographic visualization (map with health scores)
- [ ] Alerting on health degradation or crashes
- [ ] Network topology graph generation
- [ ] Historical trending and analysis

**Reference:** Section 12 and AREDNmon-Architecture.md

---

### B.4 Testing Tools

**Test UDP Probe Protocol:**
```bash
# On responder node:
nc -ul 40050 | nc -u localhost 40050  # Echo server

# On sender node:
echo "test" | nc -u target-node.local.mesh 40050
```

**Test Collector API:**
```bash
# Send test health message
curl -X POST http://collector:5000/ingest \
  -H "Content-Type: application/json" \
  -d '{
    "schema": "meshmon.v1",
    "type": "agent_health",
    "node": "TEST-NODE",
    "sent_at": "2025-09-30T12:00:00Z",
    "health_score": 95.0
  }'
```

**Validate JSON Schema:**
```bash
# Install jq for JSON validation
cat /tmp/meshmon_health.json | jq .
cat /tmp/meshmon_network.json | jq .
```

---

**End of Appendix B**

This appendix provides complete specifications for implementing compatible agents and collectors. All implementations following these interfaces will be interoperable with the AREDN-Phonebook monitoring ecosystem.

---

**End of Specification v1.0**

*This enhanced specification maintains the emergency communication focus of AREDN-Phonebook while adding valuable network observability features. The modular design ensures monitoring never compromises core phonebook functionality.*
