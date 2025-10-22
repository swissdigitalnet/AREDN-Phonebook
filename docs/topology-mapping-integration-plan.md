# AREDNmon Topology Mapping - Integration Plan

**Date**: 2025-10-22
**Branch**: reporting
**Base Commit**: c992c09 (Health Monitoring Implementation)

## 1. Current AREDNmon Architecture Analysis

### 1.1 Existing Components

**UAC Bulk Tester Thread** (`uac_bulk_tester.c`):
- **Location**: `Phonebook/src/uac/uac_bulk_tester.c`
- **Thread**: Runs continuously, 10-minute interval (default 600s)
- **Current Test Flow**:
  1. DNS resolution check (`<phone>.local.mesh`)
  2. ICMP ping test (5 probes) - via `uac_ping_test()`
  3. SIP OPTIONS test (5 probes) - via `uac_options_test()`
  4. SIP INVITE test (optional, if enabled)
- **Output**:
  - Writes to `/tmp/uac_bulk_results.txt` (legacy file format)
  - Writes to shared memory database via `uac_test_db_write_result()`
- **Health Monitoring**: Registered thread, sends heartbeats

**Test Database** (`uac_test_db.c/.h`):
- **Storage**: Shared memory (`/uac_test_db`)
- **Structure**: `uac_test_db_t`
  - Header: `num_results`, `num_testable_phones`, `last_update`, `test_interval`
  - Results array: 100 entries max
  - Per-result data: `phone_number`, `ping_status`, `ping_rtt`, `ping_jitter`, `options_status`, `options_rtt`, `options_jitter`, `timestamp`
- **API**:
  - `uac_test_db_init()` - Initialize shared memory
  - `uac_test_db_write_result()` - Write result
  - `uac_test_db_update_header()` - Update header stats

**Ping/OPTIONS Tests** (`uac_ping.c`):
- **Functions**:
  - `uac_ping_test()` - ICMP ping with RTT/jitter
  - `uac_options_test()` - SIP OPTIONS with RTT/jitter
- **Returns**: `uac_timing_result` structure with RTT stats

**AREDNmon Dashboard** (`/cgi-bin/arednmon`):
- **Type**: Shell script generating HTML + JavaScript
- **Data Sources**:
  - `/cgi-bin/uac_test_db_json` - Test results (reads shared memory)
  - `/cgi-bin/health_status` - Health metrics (reads `/tmp/software_health.json`)
  - `/cgi-bin/showphonebook` - Phonebook data
- **Display**:
  - Software Health Status panel (CPU, memory, uptime, SIP service)
  - Network Monitor table (phone number, ping/OPTIONS status, RTT, jitter)
  - Auto-refresh every 30 seconds
- **Features**:
  - Phonebook caching in localStorage
  - Progress bar showing test completion
  - Color-coded RTT values

**HTTP Client** (`software_health/http_client.c`):
- **Functions**:
  - `health_http_post_json()` - HTTP POST for health reporting
  - URL parsing, DNS resolution, socket connection, timeout handling
- **Exists but POST-only** - Need GET variant for topology mapping

### 1.2 Data Flow

```
UAC Bulk Tester Thread (every 10 min)
  ↓
For each registered phone:
  1. DNS check (getaddrinfo)
  2. Ping test (ICMP) → RTT stats
  3. OPTIONS test (SIP) → RTT stats
  ↓
Write results to:
  - /tmp/uac_bulk_results.txt (legacy)
  - Shared memory (uac_test_db)
  ↓
Dashboard reads via CGI:
  - /cgi-bin/uac_test_db_json
  ↓
Renders table with:
  - Phone status (ONLINE/OFFLINE)
  - RTT/jitter metrics
```

## 2. Integration Strategy

### 2.1 Coexistence Approach

**Option A: Parallel Systems (RECOMMENDED)**
- Keep existing ping/OPTIONS tests for compatibility
- Add traceroute as **additional** test phase
- Both databases coexist:
  - `uac_test_db` (existing) - ping/OPTIONS results
  - `topology_db` (new) - network topology
- **Benefit**: Backward compatibility, gradual migration
- **Drawback**: Slightly more resource usage

**Option B: Full Replacement**
- Replace ping/OPTIONS with traceroute completely
- Single topology database
- **Benefit**: Cleaner, less redundancy
- **Drawback**: Breaking change, loses RTT comparison

**DECISION**: Use Option A for this integration

### 2.2 Modified Test Flow

```
UAC Bulk Tester Thread (every 10 min)
  ↓
Initialize topology_db for this cycle
  ↓
For each registered phone:
  1. DNS check (getaddrinfo)
     ↓
  2. EXISTING: Ping test → uac_test_db
     ↓
  3. EXISTING: OPTIONS test → uac_test_db
     ↓
  4. NEW: Traceroute test → topology_db
     - Run traceroute to phone
     - Extract hops (IP, RTT)
     - Reverse DNS lookup per hop
     - Add nodes to topology_db
     - Add connections (hop→hop) with RTT
  ↓
After all phones tested:
  5. NEW: Fetch location data
     - For each unique node in topology_db
     - HTTP GET to http://<ip>/cgi-bin/sysinfo.json
     - Extract lat/lon
     - Update node in topology_db
  ↓
  6. NEW: Calculate aggregate stats
     - For each connection, compute avg/min/max RTT
     - From last 10 samples
  ↓
  7. NEW: Write topology JSON
     - Write /tmp/arednmon/network_topology.json
  ↓
  8. EXISTING: Write test results
     - uac_test_db (shared memory)
     - /tmp/uac_bulk_results.txt (legacy file)
```

### 2.3 Database Strategy

**Topology Database Location**: `/tmp/arednmon/network_topology.json`
- **Format**: JSON file (not shared memory)
- **Why JSON file?**:
  - Map visualization needs JSON anyway
  - Larger dataset (500 nodes, 2000 connections)
  - Static data between scans
  - Easy debugging and inspection
- **Why /tmp/arednmon/?**:
  - Consistent with existing `/tmp/uac_bulk_results.txt`
  - Cleared on reboot (transient network state)

**Shared Memory vs. File Trade-offs**:
| Aspect | Shared Memory (uac_test_db) | File (topology_db) |
|--------|----------------------------|-------------------|
| **Speed** | Fast (IPC) | Moderate (disk I/O) |
| **Size** | Limited (100 entries) | Scalable (2000 connections) |
| **Persistence** | Process lifetime | Survives until reboot |
| **Debugging** | Hard (binary) | Easy (JSON text) |
| **CGI Access** | Need reader binary | Direct file read |
| **Use Case** | Rapid updates | Periodic snapshots |

**DECISION**: Use JSON file for topology, keep shared memory for ping/OPTIONS

## 3. New Modules Design

### 3.1 Module: uac_traceroute.c

**Purpose**: ICMP traceroute implementation with RTT measurement

**Location**: `Phonebook/src/uac/uac_traceroute.c` + `.h`

**Key Functions**:
```c
// Main traceroute function
int uac_traceroute_to_phone(
    const char *phone_number,  // e.g., "196330"
    int max_hops,              // e.g., 20
    TracerouteHop *results,    // Output array
    int *hop_count             // Output count
);

// Helper: Reverse DNS lookup
int reverse_dns_lookup(
    const char *ip,            // e.g., "10.51.55.1"
    char *hostname,            // Output buffer
    size_t hostname_len        // Buffer size
);

// Helper: Get source IP for target
int get_source_ip_for_target(
    const char *target_ip,     // Destination IP
    char *source_ip            // Output buffer (INET_ADDRSTRLEN)
);
```

**Data Structure**:
```c
typedef struct {
    int hop_number;                    // 1-indexed
    char ip_address[INET_ADDRSTRLEN]; // e.g., "10.51.55.1"
    char hostname[256];                // e.g., "hb9bla-mikrotik-1"
    float rtt_ms;                      // Round-trip time
    bool timeout;                      // True if no response
} TracerouteHop;
```

**Implementation Notes**:
- Use raw ICMP sockets (requires CAP_NET_RAW or root)
- Send UDP probes with incrementing TTL (like standard traceroute)
- Wait for ICMP TIME_EXCEEDED or DEST_UNREACH replies
- 2-second timeout per hop
- Reverse DNS via `getnameinfo()` (already used in bulk tester)

**Reuse Existing Code**:
- DNS resolution: Same pattern as uac_bulk_tester.c:126-132
- Socket handling: Similar to uac_ping.c ICMP implementation
- Logging: Use existing LOG_INFO/LOG_DEBUG macros

### 3.2 Module: topology_db.c

**Purpose**: In-memory topology database with JSON file persistence

**Location**: `Phonebook/src/uac/topology_db.c` + `.h`

**Key Functions**:
```c
// Initialization
void topology_db_init(void);
void topology_db_reset(void);

// Node management
int topology_db_add_node(
    const char *ip,
    const char *type,          // "phone", "router", "server"
    const char *name,
    const char *lat,           // Can be NULL initially
    const char *lon,           // Can be NULL initially
    const char *status         // "ONLINE", "OFFLINE", "NO_DNS"
);
TopologyNode* topology_db_find_node(const char *ip);
int topology_db_get_node_count(void);

// Connection management
int topology_db_add_connection(
    const char *from_ip,
    const char *to_ip,
    float rtt_ms               // New RTT sample
);
TopologyConnection* topology_db_find_connection(
    const char *from_ip,
    const char *to_ip
);
int topology_db_get_connection_count(void);

// Location fetching
void topology_db_fetch_all_locations(void);

// Statistics
void topology_db_calculate_aggregate_stats(void);

// Persistence
int topology_db_write_to_file(const char *filepath);
```

**Data Structures**:
```c
#define MAX_TOPOLOGY_NODES 500
#define MAX_TOPOLOGY_CONNECTIONS 2000
#define MAX_RTT_SAMPLES 10

typedef struct {
    char ip[INET_ADDRSTRLEN];
    char type[16];               // "phone", "router", "server"
    char name[256];
    char lat[32];
    char lon[32];
    char status[16];             // "ONLINE", "OFFLINE", "NO_DNS"
    time_t last_seen;
} TopologyNode;

typedef struct {
    float rtt_ms;
    time_t timestamp;
} RTT_Sample;

typedef struct {
    char from_ip[INET_ADDRSTRLEN];
    char to_ip[INET_ADDRSTRLEN];
    RTT_Sample samples[MAX_RTT_SAMPLES]; // Circular buffer
    int sample_count;            // 0-10
    int next_sample_index;       // For circular buffer
    float rtt_avg_ms;            // Calculated
    float rtt_min_ms;            // Calculated
    float rtt_max_ms;            // Calculated
    time_t last_updated;
} TopologyConnection;

// Global state (static in .c file)
static TopologyNode g_nodes[MAX_TOPOLOGY_NODES];
static int g_node_count = 0;
static TopologyConnection g_connections[MAX_TOPOLOGY_CONNECTIONS];
static int g_connection_count = 0;
static pthread_mutex_t g_topology_mutex = PTHREAD_MUTEX_INITIALIZER;
```

**Memory Footprint**:
- 500 nodes × ~350 bytes = 175 KB
- 2000 connections × ~300 bytes = 600 KB
- **Total: ~775 KB** (acceptable for embedded system)

**JSON Output Format**:
```json
{
  "version": "2.0",
  "generated_at": "2025-10-22T20:15:00Z",
  "source_node": {
    "ip": "10.51.55.233",
    "name": "vm-1.local.mesh",
    "type": "server"
  },
  "nodes": [
    {
      "ip": "10.51.55.1",
      "type": "router",
      "name": "hb9bla-mikrotik-1",
      "lat": "47.48123",
      "lon": "7.77456",
      "status": "ONLINE",
      "last_seen": 1729629300
    }
  ],
  "connections": [
    {
      "from": "10.51.55.233",
      "to": "10.51.55.1",
      "rtt_avg_ms": 2.543,
      "rtt_min_ms": 2.1,
      "rtt_max_ms": 3.2,
      "sample_count": 10,
      "last_updated": 1729629300
    }
  ],
  "statistics": {
    "total_nodes": 4,
    "total_connections": 3,
    "phones_tested": 46,
    "phones_reachable": 42
  }
}
```

### 3.3 Module: HTTP GET Client

**Purpose**: Fetch sysinfo.json for location data

**Option 1: Extend Existing** (`software_health/http_client.c`)
- Add `http_get_json()` function
- Reuse URL parsing and socket code
- **Benefit**: Code reuse
- **Drawback**: Mixing concerns (health + topology)

**Option 2: New Module** (`uac/uac_http_client.c`)
- Lightweight HTTP GET specifically for topology
- **Benefit**: Clean separation
- **Drawback**: Some code duplication

**DECISION**: Use Option 2 with simplified implementation

**Key Functions**:
```c
// Simple HTTP GET with timeout
int uac_http_get_json(
    const char *url,           // "http://10.51.55.1/cgi-bin/sysinfo.json"
    char *response,            // Output buffer
    size_t response_len,       // Buffer size
    int timeout_sec            // Timeout (2 seconds)
);

// Extract lat/lon from sysinfo.json response
int parse_sysinfo_json(
    const char *json,          // Raw JSON string
    char *lat,                 // Output buffer (32 bytes)
    char *lon                  // Output buffer (32 bytes)
);
```

**Implementation Notes**:
- Reuse URL parsing logic from software_health/http_client.c
- Use same socket timeout approach
- Simple JSON parsing (strstr for "lat" and "lon" fields)
- No external JSON library needed for this simple case

## 4. Integration Points

### 4.1 Modification: uac_bulk_tester.c

**Changes Required**:

1. **Add includes** (line ~10):
   ```c
   #include "uac_traceroute.h"
   #include "topology_db.h"
   ```

2. **Initialize topology DB** (line ~74, after LOG_INFO):
   ```c
   // Initialize topology database for this cycle
   topology_db_init();
   topology_db_reset(); // Clear previous cycle's data
   ```

3. **Add traceroute phase** (after OPTIONS test, ~line 286):
   ```c
   // ====================================================
   // PHASE 4: Traceroute Test (NEW - Network Topology)
   // ====================================================
   if (g_uac_traceroute_enabled) {  // New config option
       LOG_INFO("Tracing route to %s...", user->user_id);

       TracerouteHop hops[20];
       int hop_count = 0;

       if (uac_traceroute_to_phone(user->user_id, 20, hops, &hop_count) == 0) {
           LOG_DEBUG("Traced %d hops to %s", hop_count, user->user_id);

           // Get source IP
           char source_ip[INET_ADDRSTRLEN];
           get_source_ip_for_target(ip_str, source_ip);

           // Add source node
           topology_db_add_node(source_ip, "server", g_node_hostname,
                               NULL, NULL, "ONLINE");

           // Add destination node
           topology_db_add_node(ip_str, "phone", user->display_name,
                               NULL, NULL, "ONLINE");

           // Process hops
           char *prev_ip = source_ip;
           for (int h = 0; h < hop_count; h++) {
               if (hops[h].timeout) {
                   prev_ip = NULL;
                   continue;
               }

               // Determine node type
               const char *node_type = (h == hop_count - 1) ? "phone" : "router";

               // Add node
               topology_db_add_node(hops[h].ip_address, node_type,
                                   hops[h].hostname, NULL, NULL, "ONLINE");

               // Add connection from previous hop
               if (prev_ip != NULL) {
                   topology_db_add_connection(prev_ip, hops[h].ip_address,
                                             hops[h].rtt_ms);
               }

               prev_ip = hops[h].ip_address;
           }
       }
   }
   ```

4. **Post-cycle processing** (line ~440, before sleep):
   ```c
   // NEW: Process topology database
   if (g_uac_traceroute_enabled) {
       LOG_INFO("Fetching location data for %d unique nodes...",
                topology_db_get_node_count());
       topology_db_fetch_all_locations();

       LOG_INFO("Calculating aggregate statistics for %d connections...",
                topology_db_get_connection_count());
       topology_db_calculate_aggregate_stats();

       LOG_INFO("Writing topology to /tmp/arednmon/network_topology.json...");
       topology_db_write_to_file("/tmp/arednmon/network_topology.json");

       LOG_INFO("Topology mapping complete: %d nodes, %d connections",
                topology_db_get_node_count(), topology_db_get_connection_count());
   }
   ```

### 4.2 Modification: config_loader.c

**Add new configuration options**:

```c
// In config_loader.h:
extern int g_uac_traceroute_enabled;
extern int g_uac_traceroute_max_hops;
extern int g_topology_fetch_locations;

// In config_loader.c:
int g_uac_traceroute_enabled = 1;      // Default: enabled
int g_uac_traceroute_max_hops = 20;    // Default: 20 hops
int g_topology_fetch_locations = 1;     // Default: fetch locations

// In load_config():
parse_int_config(config, "UAC_TRACEROUTE_ENABLED", &g_uac_traceroute_enabled);
parse_int_config(config, "UAC_TRACEROUTE_MAX_HOPS", &g_uac_traceroute_max_hops);
parse_int_config(config, "TOPOLOGY_FETCH_LOCATIONS", &g_topology_fetch_locations);
```

**Add to /etc/phonebook.conf**:
```ini
# ============================================================================
# NETWORK TOPOLOGY MAPPING (AREDNmon Enhancement)
# ============================================================================

# Enable traceroute-based topology mapping
UAC_TRACEROUTE_ENABLED=1

# Maximum hops for traceroute
UAC_TRACEROUTE_MAX_HOPS=20

# Fetch location data from sysinfo.json
TOPOLOGY_FETCH_LOCATIONS=1
```

### 4.3 Enhancement: AREDNmon Dashboard

**Changes Required**:

1. **Add Leaflet.js library** (in HTML <head>):
   ```html
   <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
   <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
   ```

2. **Add map container** (after health panel, before table):
   ```html
   <div class="panel">
       <h2>Network Topology Map</h2>
       <div id="topologyMap" style="height: 700px;"></div>
       <div id="routeDetails" style="display: none;"></div>
   </div>
   ```

3. **Add map rendering JavaScript** (before </body>):
   ```javascript
   async function loadTopologyMap() {
       const response = await fetch('/tmp/arednmon/network_topology.json');
       const data = await response.json();

       // Initialize map
       const firstNode = data.nodes.find(n => n.lat && n.lon);
       const map = L.map('topologyMap').setView(
           [parseFloat(firstNode.lat), parseFloat(firstNode.lon)], 12
       );

       L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png').addTo(map);

       // Render nodes and connections
       renderTopologyNodes(map, data);
       renderTopologyConnections(map, data);
   }

   loadTopologyMap();
   ```

**Full implementation**: See FSD V2 section 5.3 for complete JavaScript

### 4.4 New CGI Script: /cgi-bin/topology_json

**Purpose**: Serve topology JSON to dashboard

**Implementation**:
```bash
#!/bin/sh
# Topology JSON CGI Endpoint
echo "Content-Type: application/json"
echo ""

TOPOLOGY_FILE="/tmp/arednmon/network_topology.json"

if [ -f "$TOPOLOGY_FILE" ]; then
    cat "$TOPOLOGY_FILE"
else
    echo '{"error": "No topology data available"}'
fi
```

**Install**: Add to Makefile:
```makefile
$(INSTALL_BIN) ./files/www/cgi-bin/topology_json $(1)/www/cgi-bin/
```

## 5. Build System Integration

### 5.1 Makefile Changes

**Add new source files** (line ~60):
```makefile
$(PKG_BUILD_DIR)/uac/uac_traceroute.c \
$(PKG_BUILD_DIR)/uac/topology_db.c \
$(PKG_BUILD_DIR)/uac/uac_http_client.c \
```

**Add new CGI scripts** (line ~110):
```makefile
$(INSTALL_BIN) ./files/www/cgi-bin/topology_json $(1)/www/cgi-bin/
```

**Create directory** (line ~105):
```makefile
$(INSTALL_DIR) $(1)/tmp/arednmon
```

### 5.2 File Structure

**New files to create**:
```
Phonebook/
├── src/
│   └── uac/
│       ├── uac_traceroute.c         (NEW - 400 lines)
│       ├── uac_traceroute.h         (NEW - 50 lines)
│       ├── topology_db.c            (NEW - 600 lines)
│       ├── topology_db.h            (NEW - 80 lines)
│       ├── uac_http_client.c        (NEW - 250 lines)
│       └── uac_http_client.h        (NEW - 30 lines)
├── files/
│   └── www/
│       └── cgi-bin/
│           └── topology_json        (NEW - 10 lines)
└── docs/
    └── topology-mapping-integration-plan.md (THIS FILE)
```

**Modified files**:
```
Phonebook/
├── src/
│   ├── uac/
│   │   └── uac_bulk_tester.c        (MODIFY - add traceroute phase)
│   └── config_loader/
│       ├── config_loader.c          (MODIFY - add topology config)
│       └── config_loader.h          (MODIFY - add extern declarations)
├── files/
│   ├── www/
│   │   └── cgi-bin/
│   │       └── arednmon             (MODIFY - add map visualization)
│   └── etc/
│       └── phonebook.conf           (MODIFY - add topology settings)
└── Makefile                         (MODIFY - add new modules)
```

## 6. Testing Strategy

### 6.1 Unit Testing

**Test: uac_traceroute.c**
```bash
# Manual test from development container
ssh vm-1
cd /tmp
# Create test program
cat > test_traceroute.c << 'EOF'
#include "uac_traceroute.h"
int main() {
    TracerouteHop hops[20];
    int hop_count = 0;
    uac_traceroute_to_phone("196330", 20, hops, &hop_count);
    for (int i = 0; i < hop_count; i++) {
        printf("Hop %d: %s (%s) - %.2f ms\n",
               hops[i].hop_number, hops[i].hostname,
               hops[i].ip_address, hops[i].rtt_ms);
    }
    return 0;
}
EOF
gcc -o test_traceroute test_traceroute.c -L. -lARED-Phonebook
./test_traceroute
```

**Test: topology_db.c**
```bash
# Test JSON generation
tail -f /tmp/arednmon/network_topology.json
# Validate JSON
cat /tmp/arednmon/network_topology.json | jq .
```

**Test: HTTP client**
```bash
# Test location fetch
curl -s http://10.51.55.1/cgi-bin/sysinfo.json | jq '.lat, .lon'
```

### 6.2 Integration Testing

**Test: End-to-end topology mapping**
1. Install package on vm-1
2. Enable traceroute in config
3. Restart service
4. Wait for one test cycle (10 minutes)
5. Check log: `logread | grep TOPOLOGY`
6. Verify file: `cat /tmp/arednmon/network_topology.json | jq .statistics`
7. Open dashboard: `http://vm-1.local.mesh/cgi-bin/arednmon`
8. Verify map displays with nodes
9. Verify connections drawn
10. Test double-click route highlighting

### 6.3 Performance Testing

**Metrics to measure**:
- Traceroute duration per phone (~10s expected)
- Full scan duration for 46 phones (~11 min expected)
- Memory usage increase (~800 KB expected)
- CPU usage during scan (<5% expected)
- Topology JSON file size (~50 KB for 46 phones expected)

**Test procedure**:
```bash
# Monitor during scan
ssh vm-1 "top -b -n 1 | grep AREDN-Phonebook"
ssh vm-1 "du -h /tmp/arednmon/network_topology.json"
ssh vm-1 "time curl -s localhost/cgi-bin/arednmon > /dev/null"
```

## 7. Implementation Phases

### Phase 1: Core Modules (Week 1)
**Goal**: Implement and test individual modules in isolation

**Tasks**:
1. Create `uac_traceroute.c` + `.h`
   - Implement `uac_traceroute_to_phone()`
   - Implement `reverse_dns_lookup()`
   - Implement `get_source_ip_for_target()`
   - Unit test on development machine

2. Create `topology_db.c` + `.h`
   - Implement node management functions
   - Implement connection management functions
   - Implement RTT sample circular buffer
   - Implement JSON file writing
   - Unit test with hardcoded data

3. Create `uac_http_client.c` + `.h`
   - Implement `uac_http_get_json()`
   - Implement `parse_sysinfo_json()`
   - Test against real AREDN node

**Deliverable**: Three tested modules, not yet integrated

### Phase 2: Bulk Tester Integration (Week 2)
**Goal**: Integrate topology mapping into UAC bulk tester

**Tasks**:
1. Modify `uac_bulk_tester.c`
   - Add traceroute phase
   - Add topology DB calls
   - Add post-cycle processing

2. Modify `config_loader.c` + `.h`
   - Add topology configuration options
   - Add to `/etc/phonebook.conf`

3. Update `Makefile`
   - Add new source files
   - Add CGI scripts
   - Add directory creation

4. Build and deploy to vm-1
   - Test traceroute execution
   - Verify JSON file generation
   - Verify no regression in ping/OPTIONS tests

**Deliverable**: Working topology data collection, JSON file output

### Phase 3: Dashboard Visualization (Week 3)
**Goal**: Add interactive map to AREDNmon dashboard

**Tasks**:
1. Modify `arednmon` CGI script
   - Add Leaflet.js includes
   - Add map container HTML
   - Add topology loading JavaScript
   - Add node rendering
   - Add connection rendering
   - Add route highlighting (BFS algorithm)

2. Create `topology_json` CGI script
   - Simple file server for topology JSON

3. Test visualization
   - Verify map displays
   - Verify markers appear
   - Verify connections drawn
   - Verify color coding
   - Verify double-click highlighting

**Deliverable**: Full visualization working on vm-1

### Phase 4: Testing & Documentation (Week 4)
**Goal**: Comprehensive testing and documentation

**Tasks**:
1. Integration testing
   - Test full cycle on vm-1
   - Test with hap-2
   - Test with 40+ phones
   - Measure performance

2. Update documentation
   - Update main FSD (AREDN-phonebook-fsd.md)
   - Update README.md
   - Update CLAUDE.md
   - Write user guide section

3. Code review and cleanup
   - Add comprehensive logging
   - Add error handling
   - Add comments
   - Memory leak check (valgrind)

**Deliverable**: Production-ready feature

## 8. Rollout Plan

### 8.1 Deployment Steps

1. **Commit to reporting branch**
   ```bash
   git add .
   git commit -m "feat: add network topology mapping to AREDNmon"
   git push origin reporting
   ```

2. **Create version tag**
   ```bash
   git tag v2.6.0
   git push origin v2.6.0
   ```

3. **GitHub Actions builds package**
   - Wait for workflow completion
   - Download .ipk artifacts

4. **Deploy to vm-1**
   ```bash
   scp AREDN-Phonebook-2.6.0-x86_64.ipk vm-1:/tmp/
   ssh vm-1 "opkg install /tmp/AREDN-Phonebook-2.6.0-x86_64.ipk"
   ssh vm-1 "/etc/init.d/AREDN-Phonebook restart"
   ```

5. **Verify deployment**
   ```bash
   ssh vm-1 "logread -f | grep TOPOLOGY"
   # Wait 10 minutes for first scan
   curl http://vm-1.local.mesh/cgi-bin/arednmon
   ```

6. **Deploy to hap-2** (if successful)
   ```bash
   scp AREDN-Phonebook-2.6.0-ath79.ipk hap-2:/tmp/
   ssh hap-2 "opkg install /tmp/AREDN-Phonebook-2.6.0-ath79.ipk"
   ssh hap-2 "/etc/init.d/AREDN-Phonebook restart"
   ```

### 8.2 Configuration Migration

**Default behavior**: Topology mapping enabled by default

**Backward compatibility**: Existing ping/OPTIONS tests continue unchanged

**Disable topology mapping** (if needed):
```ini
# /etc/phonebook.conf
UAC_TRACEROUTE_ENABLED=0
```

### 8.3 Rollback Plan

If issues arise:
```bash
# Rollback to previous version
ssh vm-1 "opkg remove AREDN-Phonebook"
ssh vm-1 "opkg install /tmp/AREDN-Phonebook-2.5.16-x86_64.ipk"
ssh vm-1 "/etc/init.d/AREDN-Phonebook restart"
```

## 9. Success Criteria

### 9.1 Functional Requirements

- ✅ Traceroute discovers full path to each phone
- ✅ Topology database stores nodes and connections
- ✅ RTT statistics calculated (avg/min/max from last 10 samples)
- ✅ Location data fetched from sysinfo.json
- ✅ JSON file generated in correct format
- ✅ Map displays all nodes with location data
- ✅ Connections color-coded by RTT
- ✅ Double-click highlights route with BFS
- ✅ Existing ping/OPTIONS tests still work
- ✅ Dashboard shows both test results and map

### 9.2 Performance Requirements

- ✅ Full scan completes within 12 minutes (46 phones)
- ✅ Memory overhead < 1 MB
- ✅ CPU usage < 5% average
- ✅ No impact on SIP service
- ✅ Dashboard loads in < 2 seconds

### 9.3 Quality Requirements

- ✅ No memory leaks (valgrind clean)
- ✅ Comprehensive error handling
- ✅ Detailed logging at all stages
- ✅ Thread-safe operations
- ✅ Graceful degradation (map hidden if no topology data)
- ✅ Health monitoring integration (traceroute thread heartbeats)

## 10. Risk Analysis

### 10.1 Technical Risks

**Risk: ICMP traceroute requires root/CAP_NET_RAW**
- **Impact**: HIGH - Feature won't work without permissions
- **Mitigation**: Service already runs as root, verify with `getcap`
- **Status**: LOW - Service has required permissions

**Risk: Traceroute duration too long**
- **Impact**: MEDIUM - Might exceed 10-minute interval
- **Mitigation**: Measure actual duration, adjust timeouts if needed
- **Status**: LOW - Estimated 11 min for 46 phones fits in cycle

**Risk: Memory exhaustion on embedded devices**
- **Impact**: HIGH - Could crash service on low-memory routers
- **Mitigation**: Hard limits (500 nodes, 2000 connections), monitor RSS
- **Status**: MEDIUM - 800 KB should be acceptable, needs testing on hap-2

**Risk: Location fetch delays**
- **Impact**: MEDIUM - Could slow down scan cycle
- **Mitigation**: 2-second timeout per node, parallel if needed
- **Status**: LOW - 100 nodes × 2s = 200s is acceptable

**Risk: Map rendering performance**
- **Impact**: LOW - Dashboard might be slow with many nodes
- **Mitigation**: Leaflet.js is optimized, tested with 500+ markers
- **Status**: LOW - Expected node count (50-100) well within limits

### 10.2 Operational Risks

**Risk: Backward compatibility issues**
- **Impact**: HIGH - Could break existing monitoring
- **Mitigation**: Parallel implementation, existing tests unchanged
- **Status**: LOW - Coexistence approach minimizes risk

**Risk: Configuration complexity**
- **Impact**: MEDIUM - Users might not know how to enable/configure
- **Mitigation**: Enabled by default, minimal configuration needed
- **Status**: LOW - Simple on/off flag

**Risk: Disk space on /tmp**
- **Impact**: MEDIUM - Topology JSON might fill /tmp
- **Mitigation**: JSON file typically 50-100 KB, /tmp is 100+ MB
- **Status**: LOW - File size negligible

## 11. Future Enhancements (Out of Scope)

### Phase 2 Features (Post-Initial Implementation)

1. **Bidirectional Topology Sharing**
   - Nodes share topology via HTTP API
   - Build full mesh view (not just star from THIS node)
   - Detect routing asymmetries

2. **Historical Topology Tracking**
   - Store snapshots in SQLite database
   - Track topology changes over time
   - Alert on new nodes/failed links

3. **Automatic Bottleneck Detection**
   - Identify high-latency paths
   - Suggest alternative routes
   - Generate optimization reports

4. **Export Formats**
   - GraphML for network analysis tools
   - PDF topology reports
   - CSV for spreadsheet analysis

5. **Advanced Visualization**
   - Heatmap mode (RTT coloring)
   - Traffic flow animation
   - 3D topology view

6. **Performance Optimizations**
   - Parallel traceroute (5 concurrent)
   - Smart location caching (1-hour TTL)
   - Incremental updates (only changed routes)

## 12. References

**Related Documents**:
- [Route Analysis FSD V2](route-analysis-fsd-v2.md) - Full functional specification
- [AREDN Phonebook FSD](AREDN-phonebook-fsd.md) - Main project specification
- [AREDNmon Architecture](AREDNmon-Architecture.md) - Existing architecture docs

**External References**:
- [Leaflet.js Documentation](https://leafletjs.com/) - Map library
- [OpenStreetMap Tile Usage Policy](https://operations.osmfoundation.org/policies/tiles/) - Map tiles
- [AREDN Documentation](https://docs.arednmesh.org/) - Mesh networking

---

**Document Status**: Ready for Implementation
**Last Updated**: 2025-10-22
**Author**: Claude Code + User
**Review Status**: Pending user approval
