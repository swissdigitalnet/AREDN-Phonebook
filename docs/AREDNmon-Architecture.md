# Mesh Monitoring System

**Purpose:** Architecture for mesh monitoring system based on **AREDN-Phonebook** as the agent. The agent combines SIP proxy functionality with network monitoring capabilities, reporting to a Python-based collector on Raspberry Pi.

**Version History:**
- **V1:** Greenfield design for dedicated monitoring agent
- **V2:** Adapted to use AREDN-Phonebook SIP proxy as the monitoring agent

---

## 1) Objectives & Scope

- **Primary goal:** Detect and present **network issues** (what/where/how-bad/likely-cause/next-step) for an AREDN mesh running VoIP services.
- **Agent:** **AREDN-Phonebook** SIP proxy server (C, OpenWrt) with integrated monitoring capabilities
- **Scope:**
  - **Agent** (AREDN-Phonebook): SIP proxy + phone monitoring + health reporting + JSON reporting to collector
  - **Collector/Analyzer** (Python, Raspberry Pi): ingestion, storage, issue detection, API for UI
  - **UI**: Render within **OpenWISP** via "Issues" panel consuming Pi API
  - **Database:** **PostgreSQL + TimescaleDB** (single DB for time-series + metadata)
- **Out of scope:** Security/auth (closed network), long-term historical analytics beyond retention policy, automated remediation

---

## 2) Agent Overview: AREDN-Phonebook

### 2.1 Agent Identity

**AREDN-Phonebook** is a SIP proxy server that provides:
- **Primary function:** SIP call routing and phonebook services for mesh VoIP
- **Monitoring function:** Network quality measurement and health reporting
- **Architecture:** Multi-threaded C application running as OpenWrt service

**Agent capabilities:**
- SIP proxy with user registration and call routing
- Automated phonebook fetching and XML publishing
- Phone availability monitoring (ICMP + SIP OPTIONS)
- Network quality metrics (RTT, jitter, packet loss)
- Software health monitoring and reporting
- JSON reporting to remote collector

### 2.2 Agent Threads

**Main Thread:** SIP message processing (UDP port 5060)

**Phonebook Fetcher Thread:** Downloads CSV phonebook from mesh servers (hourly)

**Status Updater Thread:** Updates phonebook XML with phone availability (10 minutes)

**UAC Bulk Tester Thread:** Network monitoring via phone probes (10 minutes)

**Passive Safety Monitor:** Watches thread health via heartbeat timestamps

### 2.3 Agent Responsibilities

The agent is a **stateless data producer** for monitoring purposes:
- Performs lightweight **phone availability tests** (ICMP ping + SIP OPTIONS)
- Measures **network quality metrics** per phone (RTT, jitter, loss)
- Monitors **software health** (CPU, memory, threads, uptime, crashes)
- Packages results in **self-contained JSON messages**
- POSTs to Pi collector at configured intervals
- **No historical data retention** for monitoring metrics (constant RAM usage)

**Agent limitations (by design):**
- No statistical analysis of network trends
- No baseline computation
- No issue detection
- No long-term data storage
- Monitoring has constant RAM footprint

---

## 3) Network Monitoring

**See:** [AREDN Phonebook FSD - Chapter 4: Monitoring](AREDN-phonebook-fsd.md#4-monitoring)

The AREDN-Phonebook agent implements comprehensive network monitoring through the UAC (User Agent Client) module:

**Monitoring capabilities:**
- ICMP Ping testing (network layer)
- SIP OPTIONS testing (application layer, non-intrusive)
- SIP INVITE testing (optional, intrusive)
- RFC3550-compatible performance metrics (RTT, jitter, packet loss)
- Bulk testing of all registered phones
- AREDNmon dashboard for real-time visualization

**Key characteristics:**
- Tests every 600 seconds (configurable)
- Only tests phones with DNS resolution (`{phone}.local.mesh`)
- Results stored in `/tmp/uac_bulk_results.txt`
- Adaptive behavior (defers tests during active calls or high CPU)

For complete details on test execution flow, metrics collected, configuration options, and dashboard features, refer to the Phonebook FSD Chapter 4.

---

## 4) Health Reporting

**See:** [AREDN Phonebook FSD - Chapter 5: Health Reporting](AREDN-phonebook-fsd.md#5-health-reporting)

The AREDN-Phonebook agent monitors its own operational health to enable proactive maintenance and failure detection:

**Health monitoring scope:**
- Process metrics (CPU, memory, uptime, restart counts)
- Thread responsiveness via heartbeat timestamps
- Service metrics (users, calls, phonebook status)
- Crash detection and diagnostics
- Health score computation (0-100)

**Reporting strategy:**
- Scheduled baseline: Every 4 hours
- Event-driven: Immediate on significant changes (CPU spike, memory increase, thread hung, restart, health degraded, crash)
- 97% bandwidth reduction vs fixed intervals

**Passive safety:**
- Monitors thread heartbeats every 30 minutes
- Automatic thread restart on failure

For complete details on health metrics, crash detection mechanisms, and reporting frequency, refer to the Phonebook FSD Chapter 5.

---

## 5) Data Model & JSON Messages

### 5.1 Message Architecture

**Design principle:** Separate network monitoring from health reporting due to different frequencies.

**Message types:**
1. `phone_test_result`: Network monitoring results (every 10 min)
2. `agent_health`: Software health status (4h baseline + events)
3. `crash_report`: Crash diagnostics (immediate on restart)

### 5.2 Network Test Result

**Sent:** Every test cycle (default 600 seconds)

**Content:** Network quality metrics for all tested phones

```json
{
  "schema": "meshmon.v2",
  "type": "phone_test_result",
  "node": "node-A",
  "sent_at": "2025-10-13T12:00:00Z",
  "test_interval_seconds": 600,
  "total_phones": 224,
  "phones_tested": 46,
  "phones_online": 42,
  "phones_offline": 4,
  "phones_no_dns": 178,
  "tests": [
    {
      "phone_number": "441530",
      "display_name": "John Smith (HB9ABC)",
      "dns_hostname": "441530.local.mesh",
      "dns_resolved": true,
      "dns_ip": "10.51.55.233",
      "ping": {
        "status": "ONLINE",
        "packets_sent": 5,
        "packets_received": 5,
        "loss_pct": 0.0,
        "rtt_min_ms": 45.2,
        "rtt_avg_ms": 52.7,
        "rtt_max_ms": 67.1,
        "jitter_ms": 8.3
      },
      "options": {
        "status": "ONLINE",
        "packets_sent": 5,
        "packets_received": 5,
        "loss_pct": 0.0,
        "rtt_min_ms": 48.5,
        "rtt_avg_ms": 55.2,
        "rtt_max_ms": 71.8,
        "jitter_ms": 9.1
      }
    },
    {
      "phone_number": "441422",
      "display_name": "Jane Doe (HB9XYZ)",
      "dns_hostname": "441422.local.mesh",
      "dns_resolved": false,
      "ping": {
        "status": "NO_DNS"
      },
      "options": {
        "status": "NO_DNS"
      }
    }
  ],
  "summary": {
    "avg_rtt_online_ms": 67.3,
    "max_rtt_online_ms": 145.2,
    "avg_jitter_online_ms": 12.4,
    "phones_high_latency": 3,
    "phones_high_jitter": 2
  }
}
```

**Status values:**
- `ONLINE`: Phone responded successfully
- `OFFLINE`: DNS resolved but no response
- `NO_DNS`: Hostname doesn't resolve
- `DISABLED`: Test disabled in configuration

### 5.3 Agent Health

**Sent:** Every 4 hours baseline OR immediately on significant change

```json
{
  "schema": "meshmon.v2",
  "type": "agent_health",
  "node": "node-A",
  "sent_at": "2025-10-13T12:00:00Z",
  "reporting_reason": "scheduled",
  "cpu_pct": 3.1,
  "mem_mb": 5.9,
  "uptime_seconds": 86400,
  "restart_count": 0,
  "health_score": 98.5,
  "threads": {
    "all_responsive": true,
    "phonebook_fetcher": {
      "responsive": true,
      "last_heartbeat": "2025-10-13T11:55:00Z",
      "heartbeat_age_seconds": 300
    },
    "status_updater": {
      "responsive": true,
      "last_heartbeat": "2025-10-13T11:58:00Z",
      "heartbeat_age_seconds": 120
    },
    "uac_bulk_tester": {
      "responsive": true,
      "last_heartbeat": "2025-10-13T11:59:45Z",
      "heartbeat_age_seconds": 15
    }
  },
  "sip_service": {
    "registered_users": 3,
    "directory_entries": 224,
    "active_calls": 0
  },
  "phonebook": {
    "last_updated": "2025-10-13T11:00:00Z",
    "fetch_status": "SUCCESS",
    "csv_hash": "11A8204BF5C4180A",
    "xml_published": true,
    "entries_loaded": 224
  },
  "monitoring": {
    "test_interval_seconds": 600,
    "last_test_completed": "2025-10-13T11:50:00Z",
    "phones_tested_last_cycle": 46,
    "phones_online_last_cycle": 42
  },
  "checks": {
    "memory_stable": true,
    "no_recent_crashes": true,
    "sip_service_ok": true,
    "phonebook_current": true,
    "all_threads_responsive": true
  }
}
```

**Reporting reasons:**
- `scheduled`: 4-hour baseline heartbeat
- `cpu_spike`: CPU >20% change
- `memory_increase`: Memory >10MB increase
- `thread_hung`: Thread stopped responding
- `restart`: Process restarted
- `health_degraded`: Health score dropped >15 points

### 5.4 Crash Report

**Sent:** Immediately on restart after crash

```json
{
  "schema": "meshmon.v2",
  "type": "crash_report",
  "node": "node-A",
  "sent_at": "2025-10-13T12:05:00Z",
  "crash_time": "2025-10-13T12:02:13Z",
  "signal": 11,
  "signal_name": "SIGSEGV",
  "description": "Segmentation fault",
  "thread_id": "fetcher",
  "last_operation": "phonebook_fetch",
  "uptime_at_crash": 3600,
  "memory_at_crash_mb": 8.2,
  "cpu_at_crash_pct": 12.5,
  "crash_count_24h": 2,
  "backtrace": [
    "phonebook_fetcher.c:142",
    "pthread_start:0x7f4a2c4d4e50",
    "clone:0x4e4d0"
  ],
  "context": {
    "active_calls": 0,
    "registered_users": 3,
    "last_successful_fetch": "2025-10-13T11:00:00Z",
    "phones_being_tested": 46
  }
}
```

---

## 9) Collector API Endpoints

### 9.1 Ingestion

**POST /ingest**
- Accepts all message types (phone_test_result, agent_health, crash_report)
- Validates schema and timestamps
- Inserts into appropriate hypertables
- Updates node last_seen timestamp
- Returns 200 OK or 400 Bad Request

### 9.2 Issues API

**GET /issues?status=open&since=X**
- List open issues with filters
- Returns: issue_id, kind, severity, scope, started_at, impact, summary

**GET /issues/{id}**
- Issue details with full evidence and timeline

**GET /issues/by-node?node={id}**
- All issues affecting specific node

**GET /issues/by-phone?phone={number}**
- All issues affecting specific phone

### 9.3 Nodes API

**GET /nodes?status=active**
- List active nodes (last_seen < 30 min)

**GET /nodes/{id}**
- Node details: health, location, version, crash history

**GET /nodes/{id}/phones**
- All phones tested by this node with current status

### 9.4 Crashes API

**GET /crashes?node={id}&since=X**
- Crash reports for specific node

**GET /crashes/patterns?timeframe=7d**
- Crash pattern analysis across mesh

---

## 10) Resource Budget & Adaptive Behavior

### 10.1 Agent Resource Limits

**Memory:**
- Base SIP proxy: ~8 MB
- Monitoring overhead: ~2 MB
- **Total: ≤10 MB** (constant, no growth over time)

**CPU:**
- SIP processing: ~2-3% average
- Phone testing: ~1-2% during test cycle
- Health monitoring: <0.5%
- **Total: ≤5% average** on 400-700 MHz MIPS/ARM

**Network bandwidth:**
- Phone tests: ~1 kbps per phone (5 pings + 5 OPTIONS over 10 minutes)
- 46 phones: ~46 kbps
- Collector reports: ~5 KB per 10 min = 0.07 kbps
- **Total: <50 kbps** for typical deployment

**Flash writes:**
- **Zero** during normal operation (all state in `/tmp` RAM)

### 10.2 Adaptive Test Intensity

| Condition | Test Frequency | Bandwidth |
|-----------|---------------|-----------|
| Normal operation | Every 600s | ~50 kbps |
| Active SIP calls | Every 1800s | ~17 kbps |
| High CPU (>50%) | Every 1800s | ~17 kbps |
| Collector unreachable | Buffer 3-5 cycles | 0 kbps |

### 10.3 Event-Driven Reporting Benefits

**Without event-driven (fixed 10-minute intervals):**
- Health reports: 144 per day
- Bandwidth: ~720 KB per day per node

**With event-driven (4-hour baseline + events):**
- Health reports: 6 per day (normal), +1-5 during problems
- Bandwidth: ~30 KB per day per node (normal)
- **Savings: 96%** bandwidth reduction
- **Responsiveness:** Immediate notification on problems

---

## 11) Network Monitoring Data Transmission Strategy

### 11.1 Transmission Frequency Options

**Question:** How often should the agent POST network monitoring data to the collector?

**Current state:** Tests run every 600 seconds (10 minutes), results written to local file only.

**Options for remote monitoring:**

| Option | Frequency | Data per POST | Daily Bandwidth | Latency to Detection | Complexity |
|--------|-----------|---------------|-----------------|---------------------|------------|
| **A: Immediate** | After every test cycle (600s) | 1 cycle (5 KB) | ~240 KB | 10 min max | Low |
| **B: Batched (30 min)** | Every 3 cycles (1800s) | 3 cycles (15 KB) | ~80 KB | 30 min max | Medium |
| **C: Batched (1 hour)** | Every 6 cycles (3600s) | 6 cycles (30 KB) | ~40 KB | 60 min max | Medium |
| **D: Event-driven** | On status changes only | Variable | ~10 KB (normal) | Sub-minute | High |

**Recommended: Option A (Immediate)**

**Rationale:**
- **Low latency:** Issues detected within one test cycle (10 minutes)
- **Simple implementation:** No batching logic or buffering required
- **Acceptable bandwidth:** 240 KB/day = 0.02 kbps average (negligible on mesh)
- **No data loss risk:** No buffering means no risk of losing data on crash/restart
- **Consistent with health reporting:** Health events already reported immediately
- **Aligns with emergency use case:** Fast detection critical for emergency communications

**Trade-offs accepted:**
- Slightly higher POST frequency (144 per day vs. 48 or 24)
- Each POST is smaller (less compression efficiency)
- More HTTP overhead (headers per POST)

**Implementation:**
```c
// After UAC bulk test completes
if (collector_enabled && test_results_available) {
    char *json = format_phone_test_result_json(test_results);
    http_post_to_collector(collector_url, json);
    free(json);
}
```

### 11.2 Agent-Side Pre-calculation Options

**Question:** Should the agent pre-calculate summary statistics to reduce network traffic?

**Current JSON design (Section 5.2):** Includes both per-phone details and summary statistics.

**Analysis:**

| Approach | Data Sent | Message Size | Collector Flexibility | Agent CPU |
|----------|-----------|--------------|----------------------|-----------|
| **A: Full details** | All phones + summary | ~5 KB | Full (can recompute anything) | Minimal |
| **B: Summary only** | Aggregates only | ~500 bytes | Limited (trends/baselines only) | Minimal |
| **C: Delta reporting** | Changed phones + summary | ~1-3 KB | Good (full context on changes) | Low |

**Recommended: Option A (Full details with embedded summary)**

**Rationale:**
- **Message size acceptable:** 5 KB per 10 minutes = 240 KB/day is negligible
- **Collector flexibility:** Can perform any analysis (baselines, trends, correlations)
- **Issue detection accuracy:** Per-phone data enables precise issue attribution
- **Future-proof:** New analysis doesn't require agent changes
- **Debugging capability:** Full data enables troubleshooting individual phones
- **No premature optimization:** Current bandwidth usage not a constraint

**Summary section provides:**
- Quick overview for dashboards without processing all phones
- Pre-calculated mesh-wide health indicators
- Reduces collector CPU for simple queries
- Zero bandwidth increase (summary always included)

**Example optimization already included (Section 5.2):**
```json
"summary": {
  "avg_rtt_online_ms": 67.3,          // Collector doesn't need to recalculate
  "max_rtt_online_ms": 145.2,         // Quick access to worst case
  "avg_jitter_online_ms": 12.4,       // Voice quality indicator
  "phones_high_latency": 3,           // Pre-counted issue indicators
  "phones_high_jitter": 2
}
```

**Trade-offs accepted:**
- Slightly larger messages than minimal summary-only approach
- Collector receives data it may not immediately use
- More storage required in TimescaleDB

**When to reconsider:**
- If mesh has >500 phones (message size >50 KB)
- If bandwidth becomes constrained (<100 kbps available)
- If agent CPU becomes bottleneck (>10% average)

### 11.3 Bandwidth Impact Summary

**Baseline configuration:**
- Test interval: 600 seconds (10 minutes)
- Phones tested: 46 (typical deployment, DNS-resolved only)
- Phone tests (ICMP + SIP): ~1 kbps per phone during test = ~46 kbps burst
- Collector reports: 5 KB per 10 min = **0.07 kbps average**

**Comparison with health reporting:**
- Network monitoring: 240 KB/day (every 10 min)
- Health reporting: 30 KB/day (4h baseline + events)
- **Total monitoring overhead: 270 KB/day = 0.025 kbps average**

**Context:**
- Typical AREDN link: 5-30 Mbps
- Monitoring overhead: **0.0001% of link capacity**
- Single voice call (G.711): 87 kbps
- Monitoring equivalent to: **0.03% of one voice call**

**Conclusion:** Network bandwidth is NOT a constraint. Optimize for detection speed and collector flexibility instead.

---

## 12) Implementation Status

### Currently Implemented

**AREDN-Phonebook SIP proxy** (production):
- ✅ SIP call routing and user registration
- ✅ Phonebook fetching and XML publishing
- ✅ UAC monitoring (ICMP + SIP OPTIONS tests)
- ✅ Network metrics collection (RTT, jitter, packet loss)
- ✅ Results to local file (`/tmp/uac_bulk_results.txt`)
- ✅ Passive safety monitoring (thread heartbeats)
- ✅ Local AREDNmon dashboard (CGI web interface)
- ✅ Test configuration via `/etc/phonebook.conf`
- ✅ DNS pre-check to reduce unnecessary traffic
- ✅ Adaptive test intensity (defers tests during active calls)

### For Future Releases

**Remote monitoring capabilities:**
- ☐ Health metrics collection (CPU, memory, uptime, thread health)
- ☐ JSON message formatting (phone_test_result, agent_health, crash_report)
- ☐ HTTP POST to collector endpoint
- ☐ Event-driven health reporting (4h baseline + events)
- ☐ Crash detection and reporting (signal handlers, backtrace capture)
- ☐ Configuration for collector URL and reporting intervals

**Collector infrastructure:**
- ☐ Pi collector service (Python + FastAPI)
- ☐ PostgreSQL + TimescaleDB database
- ☐ Ingestion endpoint for JSON messages
- ☐ Issue detection engine (rule-based analysis)
- ☐ REST API for UI integration

**UI integration:**
- ☐ OpenWISP panel for mesh-wide monitoring
- ☐ Topology view integration
- ☐ Issue detail views
- ☐ Node health dashboard

---

## 13) Configuration

### 13.1 Agent Configuration

**File:** `/etc/phonebook.conf`

```ini
# Network Monitoring
UAC_TEST_INTERVAL_SECONDS=600
UAC_PING_COUNT=5
UAC_OPTIONS_COUNT=5
UAC_CALL_TEST_ENABLED=0

# Remote Collector
COLLECTOR_ENABLED=1
COLLECTOR_URL=http://pi-collector.local.mesh:5000/ingest
COLLECTOR_TIMEOUT_SECONDS=10

# Health Reporting
HEALTH_REPORT_BASELINE_HOURS=4
HEALTH_REPORT_EVENT_DRIVEN=1
HEALTH_CPU_THRESHOLD_PCT=20
HEALTH_MEMORY_THRESHOLD_MB=10

# Crash Reporting
CRASH_REPORTING_ENABLED=1
CRASH_BACKTRACE_DEPTH=5
```

### 13.2 Collector Configuration

**File:** `/opt/meshmon/collector/config.yaml`

```yaml
server:
  host: 0.0.0.0
  port: 5000
  workers: 4

database:
  host: localhost
  port: 5432
  database: meshmon
  user: meshmon
  password: <secure-password>

node_lifecycle:
  inactive_threshold_minutes: 30
  dead_threshold_hours: 24

rules:
  phone_offline:
    enabled: true
    consecutive_cycles: 3
  high_latency:
    enabled: true
    threshold_ms: 150
    duration_minutes: 10
  repeated_crashes:
    enabled: true
    crashes_24h: 2
    crashes_7d: 3

retention:
  raw_data_days: 90
  crash_reports_days: 365
  aggregates_days: 365
```

---

