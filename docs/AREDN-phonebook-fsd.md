# AREDN Phonebook SIP Server - Functional Specification Document

## 1. Overview

The AREDN Phonebook SIP Server is a specialized SIP proxy server designed for Amateur Radio Emergency Data Network (AREDN) mesh networks. The server provides centralized phonebook functionality by fetching CSV phonebook data from AREDN mesh servers and managing SIP user registrations and call routing between mesh nodes.

### 1.1 System Architecture

The system follows a modular C architecture with multi-threaded design:

- **Main Thread**: SIP message processing via UDP socket on port 5060
- **Phonebook Fetcher Thread**: Periodic CSV downloads and XML conversion
- **Status Updater Thread**: User status management and phonebook updates
- **Modular Components**: Separated functionality for maintainability

## 2. SIP Server

The SIP Server provides all SIP protocol message processing, call session management, and routing capabilities. It handles user registrations, call establishment, and call termination using a stateful proxy model.

### 2.1 SIP Core

**Purpose**: Handles all SIP protocol message processing, parsing, and routing.

**Key Functions**:

#### 2.1.1 Message Parsing

- `extract_sip_header()`: Extracts specific SIP headers from messages
- `parse_user_id_from_uri()`: Parses user ID from SIP URIs
- `extract_uri_from_header()`: Extracts complete URIs from headers
- `extract_tag_from_header()`: Extracts tag parameters from headers
- `get_sip_method()`: Identifies SIP method (REGISTER, INVITE, etc.)

#### 2.1.2 SIP Method Handling

**REGISTER**: Processes user registrations
- Extracts user ID and display name from From header
- Calls `add_or_update_registered_user()`
- Responds with "200 OK" and sets expiry to 3600 seconds
- No authentication required (mesh network trust model)

**INVITE**: Handles call initiation
- Looks up callee using `find_registered_user()`
- Resolves callee hostname using DNS (format: `{user_id}.local.mesh`)
- Creates call session using `create_call_session()`
- Sends "100 Trying" response
- Proxies INVITE to resolved callee address
- Reconstructs message with new Request-URI

**BYE**: Terminates calls
- Finds call session by Call-ID
- Determines caller vs callee by comparing addresses
- Proxies BYE to other party
- Responds with "200 OK"
- Terminates call session

**CANCEL**: Cancels pending calls
- Only valid for INVITE_SENT or RINGING states
- Proxies CANCEL to callee
- Responds with "200 OK"
- Terminates call session

**ACK**: Acknowledges call establishment
- Proxies ACK to callee for ESTABLISHED calls

**OPTIONS**: Capability negotiation
- Responds with "200 OK" and supported methods

#### 2.1.3 Response Handling

- Processes SIP responses (200 OK, 180 Ringing, etc.)
- Routes responses back to original caller using stored call session data
- Updates call session state based on response codes
- Handles error responses (4xx, 5xx) by terminating sessions

#### 2.1.4 Address Resolution

- Uses DNS resolution for call routing (see [Network Communication](#6-network-communication))
- Constructs hostnames: `{user_id}.local.mesh`
- Always uses port 5060 for SIP communication
- Falls back to "404 Not Found" if resolution fails

### 2.2 Call Session Management

**Purpose**: Tracks active SIP calls and maintains routing state.

**Data Structure**: `CallSession`
- `call_id[MAX_CONTACT_URI_LEN]`: Unique call identifier
- `state`: Call state (FREE, INVITE_SENT, RINGING, ESTABLISHED, TERMINATING)
- `original_caller_addr`: Address of call initiator
- `callee_addr`: Resolved address of call recipient
- `from_tag`, `to_tag`: SIP dialog identifiers
- `in_use`: Boolean indicating slot availability

**Key Functions**:

#### 2.2.1 Session Management

- `create_call_session()`: Allocates new call session slot
- `find_call_session_by_callid()`: Locates active sessions
- `terminate_call_session()`: Cleans up session data
- `init_call_sessions()`: Initializes session table

#### 2.2.2 State Tracking

- **CALL_STATE_FREE**: Unused session slot
- **CALL_STATE_INVITE_SENT**: INVITE proxied, awaiting response
- **CALL_STATE_RINGING**: 180 Ringing or 183 Session Progress received
- **CALL_STATE_ESTABLISHED**: 200 OK received for INVITE
- **CALL_STATE_TERMINATING**: BYE or CANCEL processed

#### 2.2.3 Routing Logic

- Stores both caller and callee addresses for bidirectional routing
- Routes responses back to `original_caller_addr`
- Routes requests to `callee_addr` (resolved via DNS)
- Handles both caller-initiated and callee-initiated BYE requests

### 2.3 SIP Call Flows

#### 2.3.1 Registration Flow

1. SIP client sends REGISTER request to server
2. `process_incoming_sip_message()` identifies REGISTER method
3. `add_or_update_registered_user()` updates user database
4. Server responds with "200 OK" and expires=3600
5. User marked as active and available for calls

#### 2.3.2 Call Establishment Flow

1. Caller sends INVITE to server
2. Server looks up callee using `find_registered_user()`
3. DNS resolution for callee hostname (`{user_id}.local.mesh`)
4. `create_call_session()` allocates session tracking
5. Server sends "100 Trying" to caller
6. INVITE proxied to resolved callee address
7. Callee responses proxied back to caller via session data
8. Call state updated based on response codes

#### 2.3.3 Call Termination Flow

1. Either party sends BYE request
2. Server identifies call session by Call-ID
3. BYE proxied to other party
4. Server responds "200 OK" to BYE sender
5. Call session terminated and resources freed

### 2.4 SIP Error Handling

**404 Not Found**: User not registered or DNS resolution failed

**503 Service Unavailable**: Maximum call sessions reached

**481 Call/Transaction Does Not Exist**: Invalid Call-ID for BYE/CANCEL

**501 Not Implemented**: Unsupported SIP methods

## 3. Phonebook

The Phonebook subsystem manages the download, processing, and publishing of phone directory data from AREDN mesh servers. It maintains user registration information and ensures service availability even during network outages through emergency boot capabilities.

### 3.1 Phonebook Update Flow (Overview)

This flow describes how phonebook data moves through the system:

1. Phonebook fetcher downloads CSV from configured servers
2. Hash calculation determines if changes exist
3. CSV parsed and user database populated
4. CSV converted to XML format
5. XML published to public web path
6. Status updater signaled for additional processing
7. Status updater reads XML and updates user statuses

### 3.2 User Manager

**Purpose**: Manages registered SIP users and phonebook directory entries.

**Data Structure**: `RegisteredUser`
- `user_id[MAX_PHONE_NUMBER_LEN]`: Numeric user identifier
- `display_name[MAX_DISPLAY_NAME_LEN]`: Full name (format: "FirstName LastName (Callsign)")
- `is_active`: Boolean indicating current registration status
- `is_known_from_directory`: Boolean indicating phonebook origin

**Key Functions**:

#### 3.2.1 User Lookup

- `find_registered_user()`: Finds active users by user_id
- Thread-safe with mutex protection
- Only returns users with `is_active = true`

#### 3.2.2 Dynamic Registration

- `add_or_update_registered_user()`: Handles SIP REGISTER requests
- Creates new users or updates existing ones
- Manages expiration (expires=0 deactivates registration)
- Differentiates between directory users and dynamic registrations
- Tracks counts: `num_registered_users` (dynamic), `num_directory_entries` (phonebook)

**Counter Separation Rationale:**
The system maintains separate counters for operational purposes:
- **Capacity Management**: Combined count (`num_registered_users + num_directory_entries`) is checked against `MAX_REGISTERED_USERS` to prevent resource exhaustion
- **Monitoring & Debugging**: Separate counts help operators distinguish between CSV phonebook entries and dynamic registrations in logs
- **Operational Visibility**: Knowing "224 directory entries loaded" vs "3 dynamic registrations active" provides useful system state information during emergency operations

#### 3.2.3 Phonebook Integration

- `populate_registered_users_from_csv()`: Loads users from CSV phonebook
- `add_csv_user_to_registered_users_table()`: Adds directory entries
- Marks users as `is_known_from_directory = true`
- Handles UTF-8 sanitization and whitespace trimming
- Format: "FirstName LastName (Callsign)" for display names

#### 3.2.4 Data Management

- `init_registered_users_table()`: Clears all user data
- Thread-safe operations with `registered_users_mutex`
- Maximum capacity: `MAX_REGISTERED_USERS`

### 3.3 Phonebook Fetcher

**Purpose**: Downloads CSV phonebook data from configured AREDN mesh servers.

**Key Functions**:

#### 3.3.1 Main Thread Loop

- `phonebook_fetcher_thread()`: Main execution loop
- Runs continuously with configurable intervals (`g_pb_interval_seconds`)
- Downloads CSV, converts to XML, publishes results
- Handles hash-based change detection
- **Webhook Reload**: Supports `phonebook_reload_requested` flag to interrupt sleep and fetch immediately
- Sleep loop checks flag every second to enable on-demand phonebook updates
- **Passive Safety**: Updates `g_fetcher_last_heartbeat` timestamp each cycle for thread health monitoring

#### 3.3.2 Download Process

1. Calls `csv_processor_download_csv()` to fetch from configured servers
2. Calculates file hash using `csv_processor_calculate_file_conceptual_hash()`
3. Compares with previous hash to detect changes
4. Skips processing if no changes detected (after initial population)

#### 3.3.3 Processing Pipeline

1. Populates user database via `populate_registered_users_from_csv()`
2. Converts CSV to XML via `csv_processor_convert_csv_to_xml_and_get_path()`
3. Publishes XML to public path via `publish_phonebook_xml()`
4. Updates hash file on successful processing
5. Signals status updater thread for additional processing

#### 3.3.4 File Management

- Creates necessary directories using `file_utils_ensure_directory_exists()` (see [System Components](#7-system-components))
- Publishes XML to `PB_XML_PUBLIC_PATH` for web access
- Maintains hash file at `PB_LAST_GOOD_CSV_HASH_PATH`
- Cleans up temporary files after processing

#### 3.3.5 Emergency Boot & Storage Strategy

**Emergency Boot Sequence:**

The phonebook fetcher implements an "emergency boot" feature that ensures immediate service availability after router startup or power outage. This is critical for emergency communications scenarios where the mesh network may be degraded but local services must remain operational.

**On Startup (lines 72-87 in `phonebook_fetcher.c`):**

1. **Check for Existing CSV** (`access(PB_CSV_PATH, F_OK)`):
   - Path: `/www/arednstack/phonebook.csv` (persistent flash storage)
   - If file exists: Execute emergency boot sequence
   - If file missing: Log "No existing phonebook found" and wait for first fetch

2. **Load Immediately**:
   ```c
   populate_registered_users_from_csv(PB_CSV_PATH);
   ```
   - Clears and reloads entire `registered_users[]` array
   - Populates all directory entries from CSV
   - Sets `initial_population_done = true` flag

3. **Publish XML**:
   - Converts CSV to XML for web interface
   - Makes directory available to SIP phones within seconds of boot
   - Enables phone monitoring (UAC) immediately

**Log Messages:**
```
FETCHER: Found existing phonebook CSV at '/www/arednstack/phonebook.csv'.
         Loading immediately for service availability.
FETCHER: Emergency boot: SIP user database loaded from persistent storage.
         Directory entries: 224.
FETCHER: Emergency boot: XML phonebook published from existing data.
```

**Why Emergency Boot Exists:**
- **Power Outage Resilience**: Router reboots don't lose phonebook
- **Network Independence**: Works even if phonebook servers are unreachable
- **Zero Downtime**: Service available within seconds instead of waiting 30-60 minutes
- **Emergency Operations**: Critical for disaster scenarios when mesh is degraded

**File Path Architecture:**

The system uses a two-tier storage strategy to minimize flash wear on embedded routers: **downloads occur first in RAM (`/tmp/`) where they can be inspected and compared via hash calculation without touching flash; only when the content has actually changed is the file copied from RAM to persistent flash storage (`/www/`)**, preventing unnecessary write cycles that would shorten router lifespan.

| Path | Storage | Purpose | Lifetime |
|------|---------|---------|----------|
| `/tmp/phonebook_download.csv` | **RAM (tmpfs)** | Temporary download buffer | Single fetch cycle |
| `/www/arednstack/phonebook.csv` | **Flash (persistent)** | Long-term storage | Survives reboot |
| `/www/arednstack/phonebook.csv.hash` | **Flash (persistent)** | Change detection | Survives reboot |
| `/tmp/phonebook.xml` | **RAM (tmpfs)** | XML conversion workspace | Single conversion |
| `/www/arednstack/phonebook_generic_direct.xml` | **Flash (persistent)** | Published XML for phones | Survives reboot |

**Path Constants:**
```c
#define PB_CSV_TEMP_PATH "/tmp/phonebook_download.csv"      // RAM download buffer
#define PB_CSV_PATH "/www/arednstack/phonebook.csv"         // Flash persistent storage
#define PB_XML_BASE_PATH "/tmp/phonebook.xml"               // RAM conversion buffer
#define PB_XML_PUBLIC_PATH "/www/arednstack/phonebook_generic_direct.xml"  // Flash published
#define PB_LAST_GOOD_CSV_HASH_PATH "/www/arednstack/phonebook.csv.hash"    // Flash hash
```

**Cross-Filesystem Copy Operation:**

Downloads occur in RAM to minimize flash writes, then only written to flash if content changes:

```c
// 1. Download to RAM (line 98)
csv_processor_download_csv()  // → writes to PB_CSV_TEMP_PATH (/tmp/)

// 2. Calculate hash of downloaded file (line 104)
csv_processor_calculate_file_conceptual_hash(PB_CSV_TEMP_PATH, new_csv_hash)

// 3. Compare with previous hash from flash (line 111-125)
if (strcmp(new_csv_hash, last_good_csv_hash) == 0 && initial_population_done) {
    // IDENTICAL - no flash write needed
    remove(PB_CSV_TEMP_PATH);  // Delete RAM copy
    goto end_fetcher_cycle;
}

// 4. Content changed - copy RAM to flash (line 140)
file_utils_copy_file(PB_CSV_TEMP_PATH, PB_CSV_PATH)  // RAM → Flash
remove(PB_CSV_TEMP_PATH);  // Clean up RAM buffer
```

**Flash Wear Optimization:**

Embedded routers have limited flash write cycles (typically 10,000-100,000 writes). The phonebook system minimizes flash wear through:

1. **Hash-Based Change Detection** (line 128-131):
   - Calculates 16-byte rolling checksum of CSV content
   - Compares with previous hash from flash
   - Only writes to flash if hash differs
   - Typical scenario: Phonebook unchanged → zero flash writes

2. **RAM-First Strategy**:
   - All downloads occur in `/tmp/` (RAM tmpfs)
   - All XML conversions occur in `/tmp/` (RAM tmpfs)
   - Flash only written when content actually changes

3. **Atomic Operations**:
   - Uses temporary files with atomic rename
   - Prevents corruption if operation interrupted
   - Rollback capability via backup files

**Startup Behavior Scenarios:**

**Scenario 1: First Boot (No Existing CSV)**
```
1. phonebook_fetcher_thread() starts
2. access(PB_CSV_PATH) returns -1 (file not found)
3. Log: "No existing phonebook found. Service will be available after first successful fetch."
4. Wait for g_pb_interval_seconds (default: 3600s = 1 hour)
5. Download CSV, populate users, publish XML
6. Set initial_population_done = true
```

**Scenario 2: Reboot (Existing CSV Present)**
```
1. phonebook_fetcher_thread() starts
2. access(PB_CSV_PATH) returns 0 (file exists)
3. Log: "Found existing phonebook CSV at '/www/arednstack/phonebook.csv'"
4. populate_registered_users_from_csv(PB_CSV_PATH)  // Immediate load
5. Convert CSV to XML, publish
6. Log: "Emergency boot: SIP user database loaded. Directory entries: 224."
7. Set initial_population_done = true
8. Service immediately available (within ~2-3 seconds)
9. Continue to main fetch loop for periodic updates
```

**Scenario 3: Normal Operation (Unchanged Phonebook)**
```
1. Wake on interval (3600s)
2. Download CSV to /tmp/phonebook_download.csv
3. Calculate hash: 11A8204BF5C4180A
4. Read previous hash from /www/arednstack/phonebook.csv.hash: 11A8204BF5C4180A
5. Hashes match + initial_population_done = true
6. Log: "Downloaded CSV is identical to flash copy. No flash write needed."
7. remove(/tmp/phonebook_download.csv)  // Clean up RAM
8. Skip processing, preserve flash lifespan
9. Sleep 3600s
```

**Scenario 4: Normal Operation (Changed Phonebook)**
```
1. Wake on interval (3600s)
2. Download CSV to /tmp/phonebook_download.csv
3. Calculate hash: 22B9315CG6D5291B (different!)
4. Read previous hash: 11A8204BF5C4180A
5. Hashes differ
6. Log: "CSV content changed. Updating persistent storage (flash write)."
7. copy(/tmp/phonebook_download.csv → /www/arednstack/phonebook.csv)  // Flash write!
8. remove(/tmp/phonebook_download.csv)
9. populate_registered_users_from_csv(PB_CSV_PATH)
10. Convert to XML, publish
11. Write new hash to /www/arednstack/phonebook.csv.hash  // Flash write!
12. Log: "Flash write: Updated CSV hash to '22B9315CG6D5291B'."
```

**The `initial_population_done` Flag:**

This boolean flag (line 66) prevents unnecessary processing:

```c
static bool initial_population_done = false;
```

**Purpose:**
- Tracks whether registered_users array has been populated at least once
- Set to `true` after emergency boot OR first successful fetch
- Used in hash comparison logic (line 128)

**Logic:**
```c
if (strcmp(new_csv_hash, last_good_csv_hash) == 0 && initial_population_done) {
    // Skip processing - already have this data
}
```

**Without Flag:** First fetch would always skip (hash matches itself)
**With Flag:** First fetch always processes (flag is false)

**Storage Strategy Rationale:**

1. **Why Keep CSV Even If XML Conversion Fails** (line 177-181):
   ```c
   // Do not delete CSV even if XML conversion fails
   // Emergency data preservation
   ```
   - CSV is authoritative data source
   - XML is just a presentation format
   - If XML generation fails, CSV still available for:
     - Emergency boot on next restart
     - Retry on next fetch cycle
     - Manual inspection/debugging

2. **Why Not Store in Database:**
   - Embedded systems have limited resources
   - CSV + hash approach is simple, robust
   - No database overhead (SQLite, etc.)
   - Easy to inspect and debug
   - Compatible with mesh file sharing

3. **Why Two Storage Locations:**
   - RAM (`/tmp/`): Fast, unlimited writes, lost on reboot
   - Flash (`/www/`): Persistent, limited writes, survives reboot
   - Combine both: Speed + Durability + Flash longevity

**Impact on Router Lifespan:**

Typical router flash has 10,000 write cycles per block. With hash-based optimization:

**Without Optimization:**
- 24 writes/day (hourly fetch) × 365 days = 8,760 writes/year
- Flash failure in ~1.1 years

**With Hash Optimization:**
- Phonebook typically changes 1-2 times/day
- 2 writes/day × 365 days = 730 writes/year
- Flash failure in ~13.7 years

**Flash wear reduced by 92%**, extending router lifespan significantly.

### 3.4 Status Updater

**Purpose**: Processes phonebook XML and manages user status updates.

**Key Functions**:

#### 3.4.1 Thread Coordination

- `status_updater_thread()`: Main execution loop
- Triggered by phonebook fetcher signals or timer intervals
- Uses condition variable `updater_trigger_cond` for coordination
- Configurable interval: `g_status_update_interval_seconds`
- **Passive Safety**: Updates `g_updater_last_heartbeat` timestamp each cycle for thread health monitoring

#### 3.4.2 XML Processing

- Reads published XML from `PB_XML_PUBLIC_PATH`
- Parses XML entries and extracts name/telephone data
- Strips leading asterisks from names (inactive markers)
- Updates user status based on XML content

#### 3.4.3 Status Management

- Performs DNS lookups for each phone to check network availability
- Updates XML display names by prepending asterisk (`*`) for online phones
- Does **not** modify the `registered_users` array (purely XML presentation layer)
- Re-publishes updated XML with status indicators for SIP phone displays
- Provides visual feedback on phone availability without affecting SIP routing

### 3.5 CSV Processor

**Purpose**: Handles CSV phonebook download, parsing, and XML conversion.

**Key Functions**:

#### 3.5.1 Download Management

- `csv_processor_download_csv()`: Downloads from configured servers
- Tries servers in order until successful download
- Uses HTTP requests to fetch CSV files (see [Network Communication](#6-network-communication))
- Handles connection failures and retries next server

#### 3.5.2 Data Processing

- `sanitize_utf8()`: Cleans UTF-8 encoding in CSV data
- Parses CSV format: **FirstName,LastName,Callsign,Telephone** (4 columns, no header row)
- Validates required fields (telephone number mandatory)
- Handles malformed lines gracefully with warnings
- Constructs display names in format: "FirstName LastName (Callsign)"

#### 3.5.3 XML Conversion

- `csv_processor_convert_csv_to_xml_and_get_path()`: Converts CSV to XML
- XML escapes special characters in data
- Creates structured XML format for web publication
- Generates temporary XML files for processing

#### 3.5.4 Hash Calculation

- `csv_processor_calculate_file_conceptual_hash()`: Generates file checksums
- Uses simple checksum algorithm for change detection
- Enables incremental processing (skip unchanged files)
- Stores hash as hexadecimal string

### 3.6 Phonebook Error Handling

**File Access Errors**: Graceful handling of missing/unreadable files

**Network Errors**: Retry logic for phonebook downloads across multiple configured servers

**Recovery Mechanisms**:
- **Default Configuration**: Uses defaults if config file missing
- **Hash Comparison**: Skips processing for unchanged phonebooks
- **Resource Cleanup**: Automatic cleanup of temporary files
- **Continuous Operation**: Threads continue despite individual failures

## 4. Monitoring

The User Agent Client (UAC) module provides non-intrusive SIP phone testing and health monitoring capabilities. This optional feature enables network operators to proactively monitor phone reachability and measure voice quality metrics across the mesh network.

### 4.1 Overview

**Key Capabilities:**
- **ICMP Ping Testing**: Network-layer connectivity verification
- **SIP OPTIONS Testing**: Application-layer SIP stack verification (non-intrusive)
- **SIP INVITE Testing**: Optional end-to-end call validation (rings phone)
- **Performance Metrics**: RTT, jitter, and packet loss measurement (RFC3550-compatible)
- **Bulk Testing**: Automated testing of all registered phones
- **Web Dashboard**: AREDNmon real-time monitoring interface

### 4.2 Module Structure

```
Phonebook/src/uac/
├── uac.h                    // UAC core API
├── uac.c                    // UAC state machine (INVITE/CANCEL/BYE)
├── uac_sip_builder.c        // SIP message builders
├── uac_sip_parser.c         // SIP response parser
├── uac_ping.h               // SIP OPTIONS ping API
├── uac_ping.c               // RTT/jitter measurement (RFC3550)
└── uac_bulk_tester.c        // Bulk phone testing coordinator
```

### 4.3 Testing Modes

#### 4.3.1 ICMP Ping Test (Network Layer)

- **Purpose**: Tests IP-level connectivity and network path quality
- **Protocol**: ICMP Echo Request/Reply
- **Metrics**: Network RTT, jitter, packet loss
- **Intrusive**: No (silent network test)
- **Target**: `{phone_number}.local.mesh` hostname

#### 4.3.2 SIP OPTIONS Test (Application Layer)

- **Purpose**: Tests SIP stack availability without ringing phone
- **Protocol**: SIP OPTIONS method
- **Port**: UDP 5060
- **Metrics**: SIP RTT, jitter, packet loss
- **Intrusive**: No (does not ring phone)
- **Response**: 200 OK with SIP capabilities

#### 4.3.3 SIP INVITE Test (End-to-End)

- **Purpose**: Validates complete call path (optional fallback)
- **Protocol**: SIP INVITE/CANCEL or INVITE/ACK/BYE
- **Behavior**: Rings phone briefly, then cancels/hangs up
- **Intrusive**: Yes (audible ring on phone)
- **Use Case**: Deep validation when OPTIONS test fails
- **Default**: Disabled (UAC_CALL_TEST_ENABLED=0)

### 4.4 Bulk Testing Workflow

The bulk tester runs automatically at configured intervals to test all phones in the registered_users array:

**Test Sequence (per phone):**
1. **DNS Resolution**: Check if `{phone_number}.local.mesh` resolves
   - If no DNS: Mark as "NO_DNS", skip to next phone

2. **Phase 1 - ICMP Ping Test** (if UAC_PING_COUNT > 0):
   - Send N ICMP ping requests (default: 5)
   - Measure RTT and jitter
   - Calculate packet loss percentage

3. **Phase 2 - SIP OPTIONS Test** (if UAC_OPTIONS_COUNT > 0):
   - Send N SIP OPTIONS requests (default: 5)
   - Measure SIP-level RTT and jitter
   - Verify SIP stack responsiveness

4. **Phase 3 - SIP INVITE Test** (if enabled AND both previous tests failed):
   - Send SIP INVITE to phone
   - Wait for 180 Ringing or 200 OK
   - Immediately CANCEL or BYE to minimize disturbance
   - Mark as ONLINE if phone responds

**Results Storage:**
- Written to `/tmp/uac_bulk_results.txt` (JSON format)
- Updated after each test cycle
- Consumed by AREDNmon dashboard

### 4.5 Performance Metrics (RFC3550)

#### 4.5.1 RTT (Round-Trip Time)

```c
// Calculated for each request/response pair:
rtt_ms = (receive_timestamp - send_timestamp) * 1000

// Aggregate metrics:
rtt_min = minimum RTT observed
rtt_max = maximum RTT observed
rtt_avg = sum(all_rtts) / count(responses)
```

#### 4.5.2 Jitter (Inter-arrival Jitter)

```c
// RFC3550 simplified - mean absolute difference:
jitter_ms = 0
for i in 1..(rtt_count-1):
    jitter_ms += |rtt[i] - rtt[i-1]|
jitter_ms /= (rtt_count - 1)
```

#### 4.5.3 Packet Loss

```c
loss_pct = 100 * (requests_sent - responses_received) / requests_sent
```

### 4.6 Configuration

Configuration parameters in `/etc/phonebook.conf`:

```ini
# ============================================================================
# MONITORING SETTINGS (UAC Testing)
# ============================================================================

# UAC Test Interval - how often to test all phones (seconds)
# Set to 0 to disable monitoring completely
# Default: 600 (10 minutes)
UAC_TEST_INTERVAL_SECONDS=600

# UAC Ping Test - ICMP ping count per phone (network layer)
# Tests network connectivity and measures RTT/jitter at IP level
# Range: 0-20, Default: 5, Set to 0 to disable
UAC_PING_COUNT=5

# UAC Options Test - SIP OPTIONS count per phone (application layer)
# Tests SIP connectivity and measures RTT/jitter at SIP level
# Range: 0-20, Default: 5, Set to 0 to disable
UAC_OPTIONS_COUNT=5

# UAC Call Test - enable INVITE testing (rings phone briefly)
# Only used as fallback if both ping and options fail
# 0 = disabled, 1 = enabled
# Default: 0 (disabled - recommended to avoid disturbing users)
UAC_CALL_TEST_ENABLED=0
```

### 4.7 CGI Endpoints

#### 4.7.1 AREDNmon Dashboard

**Endpoint**: `GET /cgi-bin/arednmon`

**Purpose**: Web-based real-time monitoring dashboard

**Features**:
- Real-time status display (ONLINE/OFFLINE/NO_DNS)
- Performance metrics (RTT, jitter) for both ping and OPTIONS
- Color-coded results: Green (<100ms), Orange (100-200ms), Red (>200ms)
- Contact names from phonebook
- Progress tracking during test cycles
- Auto-refresh every 30 seconds

**Dashboard Columns**:
| Column | Description |
|--------|-------------|
| Phone Number | SIP extension number |
| Name | Contact name from phonebook |
| Ping Status | ICMP network-layer connectivity |
| Ping RTT | Network round-trip time (ms) |
| Ping Jitter | Network jitter (ms) |
| OPTIONS Status | SIP application-layer connectivity |
| OPTIONS RTT | SIP round-trip time (ms) |
| OPTIONS Jitter | SIP jitter (ms) |

#### 4.7.2 Manual Phone Test (Non-Intrusive)

**Endpoint**: `GET /cgi-bin/uac_ping?target={phone_number}&count={N}`

**Parameters**:
- `target`: Phone number to test (required)
- `count`: Number of pings (1-20, default: 5)

**Example**:
```bash
curl "http://localnode.local.mesh/cgi-bin/uac_ping?target=441530&count=10"
```

**Response** (JSON):
```json
{
  "status": "success",
  "message": "UAC ping/options test triggered to 441530 with 10 requests",
  "target": "441530",
  "count": 10,
  "note": "Check logs with: logread | grep UAC_PING"
}
```

**Operation**: Runs both ICMP ping and SIP OPTIONS tests, does not ring the phone.

#### 4.7.3 Manual INVITE Test (Intrusive)

**Endpoint**: `GET /cgi-bin/uac_test?target={phone_number}`

**Example**:
```bash
curl "http://localnode.local.mesh/cgi-bin/uac_test?target=441530"
```

**Operation**: Triggers SIP INVITE that will ring the phone, then automatically cancels/hangs up.

**Warning**: Intrusive - use sparingly for diagnostic purposes only.

### 4.8 Thread Architecture

**UAC Bulk Tester Thread**:
- Thread ID: Spawned by `main.c` during initialization
- Wake Interval: `UAC_TEST_INTERVAL_SECONDS` (default: 600s)
- Lifecycle: Runs continuously in background
- Heartbeat: Updates `g_uac_tester_last_heartbeat` for passive safety monitoring
- Termination: Graceful shutdown on service stop

**Main Thread Integration**:
- UAC socket (UDP port 5070) added to main select() loop
- SIP responses processed by `uac_process_response()`
- Caller ID reserved: `999900` for UAC-generated calls

### 4.9 Status Indicators

**Test Result Status**:
- **ONLINE**: Phone responded successfully to test
- **OFFLINE**: DNS resolved but phone didn't respond
- **NO_DNS**: Phone hostname doesn't resolve (node not on mesh)
- **DISABLED**: Testing disabled in configuration

**Network Quality Indicators**:
- **Green** (<100ms RTT): Excellent voice quality
- **Orange** (100-200ms RTT): Acceptable voice quality
- **Red** (>200ms RTT): Degraded voice quality, may have issues

### 4.10 Monitoring Data Flow

**Bulk Test Cycle**:
```
1. Wake on interval (UAC_TEST_INTERVAL_SECONDS)
2. Lock registered_users_mutex
3. For each registered user:
   a. Check DNS resolution ({user_id}.local.mesh)
   b. If DNS fails: mark NO_DNS, continue
   c. Run ICMP ping test (UAC_PING_COUNT requests)
   d. Run SIP OPTIONS test (UAC_OPTIONS_COUNT requests)
   e. If both fail AND UAC_CALL_TEST_ENABLED: run INVITE test
   f. Record results with RTT/jitter/loss metrics
4. Unlock registered_users_mutex
5. Write results to /tmp/uac_bulk_results.txt
6. Log summary (phones online/offline)
7. Update passive safety heartbeat
8. Sleep until next interval
```

**AREDNmon Request Flow**:
```
1. Browser requests /cgi-bin/arednmon
2. CGI script reads /tmp/uac_bulk_results.txt
3. CGI script reads phonebook XML for contact names
4. Generate HTML table with color-coded metrics
5. Include JavaScript for 30-second auto-refresh
6. Return HTML response
```

### 4.11 Integration with Core Components

**User Manager Integration**:
- Bulk tester iterates `registered_users[]` array
- Uses `registered_users_mutex` for thread-safe access
- Only tests users with `is_active = true`
- Requires `is_known_from_directory = true` or dynamic registration

**Phonebook Integration**:
- `populate_registered_users_from_csv()` populates test targets
- All active phonebook entries (marked with `*`) become test targets
- AREDNmon displays names from phonebook XML

**Passive Safety Integration**:
- Bulk tester updates `g_uac_tester_last_heartbeat` every cycle
- Passive safety monitors for hung UAC thread (30-minute timeout)
- Automatic thread restart if heartbeat stops

### 4.12 Network Topology Mapping

The topology mapping subsystem automatically discovers and visualizes the mesh network topology by performing ICMP traceroute to each tested phone and aggregating the results into a geographic map display. This provides network operators with real-time visibility into mesh routing paths, connection quality, and node locations.

#### 4.12.1 Overview

**Key Capabilities:**
- **ICMP Traceroute**: Discovers intermediate hops between server and phones
- **Topology Database**: In-memory graph of nodes and connections
- **Geographic Visualization**: Leaflet.js map with node positions
- **Two-Phase Location Fetching**: Fetches router coordinates, propagates to phones
- **HTTP Client**: Retrieves location data from AREDN sysinfo.json endpoints
- **Color-Coded Routes**: Visual quality indicators based on RTT
- **RTT Information**: Permanent labels showing round-trip times on connections

**Module Structure:**
```
Phonebook/src/uac/
├── uac_traceroute.h         // ICMP traceroute API
├── uac_traceroute.c         // TTL-based path discovery
├── topology_db.h            // Topology graph API
├── topology_db.c            // Node/connection storage + location fetching
├── uac_http_client.h        // HTTP GET client API
└── uac_http_client.c        // Location data retrieval
```

**CGI Endpoints:**
- `/cgi-bin/topology_json`: Serves topology data as JSON
- `/cgi-bin/arednmon`: Enhanced dashboard with topology map

#### 4.12.2 ICMP Traceroute

**Implementation:** `uac_traceroute.c`

**Algorithm:**
```c
// TTL-based path discovery (similar to traceroute command)
for (ttl = 1; ttl <= max_hops; ttl++) {
    // Send ICMP Echo Request with TTL = ttl
    send_icmp_probe(target_ip, ttl, sequence);

    // Wait for ICMP Time Exceeded or Echo Reply
    response = wait_for_icmp_response(timeout_ms);

    if (response == ECHO_REPLY) {
        // Reached destination
        break;
    } else if (response == TIME_EXCEEDED) {
        // Record intermediate hop
        hops[hop_count++] = {ip, hostname, rtt_ms};
    } else {
        // Timeout - record as "*" hop
        hops[hop_count++] = {timeout: true};
    }
}
```

**Key Functions:**

- `uac_traceroute_to_phone()`: Performs traceroute to target phone
  - **Parameters**: `target_phone_number`, `max_hops`, `hops[]`, `hop_count`
  - **Returns**: 0 on success, -1 on failure
  - **Timeout**: 2 seconds per hop
  - **Socket**: Raw ICMP socket (requires root/CAP_NET_RAW)

- `send_icmp_probe()`: Sends ICMP Echo Request with specific TTL
  - Sets IP_TTL socket option
  - Includes sequence number for matching responses

- `wait_for_icmp_response()`: Receives and parses ICMP response
  - Handles both Time Exceeded and Echo Reply
  - Extracts source IP of intermediate router
  - Measures RTT (send_time → recv_time)

**Hop Data Structure:**
```c
typedef struct {
    char ip_address[INET_ADDRSTRLEN];  // 10.47.245.209
    char hostname[256];                // router-1.local.mesh
    double rtt_ms;                     // 1.234
    bool timeout;                      // true if hop didn't respond
} TracerouteHop;
```

**Example Traceroute Output** (logs):
```
UAC: Tracing route to 441530 (10.197.143.20)...
UAC: Hop 1: 10.167.247.74 (router-1.local.mesh) - RTT: 1.450 ms
UAC: Hop 2: 10.197.143.20 (441530.local.mesh) - RTT: 2.100 ms (destination reached)
UAC: Traceroute completed: 2 hops discovered
```

#### 4.12.3 Topology Database

**Implementation:** `topology_db.c`

**Purpose**: In-memory graph storage for network nodes and connections.

**Data Structures:**

**TopologyNode:** (Hostname-Based, v2.5.39+)
```c
typedef struct {
    char name[256];                  // Hostname (unique key, e.g., "HB9BLA-VM-1", "196630")
    char type[16];                   // "phone", "router", "server"
    char lat[32];                    // "47.47446" (decimal degrees)
    char lon[32];                    // "7.76728" (decimal degrees)
    char status[16];                 // "ONLINE", "OFFLINE", "NO_DNS"
    time_t last_seen;                // Unix timestamp
} TopologyNode;
```

**TopologyConnection:** (Hostname-Based, v2.5.39+)
```c
typedef struct {
    char from_name[256];             // Source node hostname (e.g., "HB9BLA-VM-1")
    char to_name[256];               // Destination node hostname (e.g., "196630")
    RTT_Sample samples[MAX_RTT_SAMPLES]; // Circular buffer (last 10 samples)
    int sample_count;                // Number of samples stored (0-10)
    int next_sample_index;           // Next position in circular buffer
    float rtt_avg_ms;                // Calculated average RTT
    float rtt_min_ms;                // Minimum RTT
    float rtt_max_ms;                // Maximum RTT
    time_t last_updated;             // Timestamp of last update
} TopologyConnection;
```

**RTT_Sample:**
```c
typedef struct {
    float rtt_ms;                    // RTT measurement in milliseconds
    time_t timestamp;                // When this sample was recorded
} RTT_Sample;
```

**Storage Capacity:**
```c
#define MAX_TOPOLOGY_NODES 500
#define MAX_TOPOLOGY_CONNECTIONS 2000
#define MAX_RTT_SAMPLES 10

static TopologyNode g_nodes[MAX_TOPOLOGY_NODES];
static TopologyConnection g_connections[MAX_TOPOLOGY_CONNECTIONS];
static pthread_mutex_t g_topology_mutex;  // Thread-safe access
```

**Key Functions:**

- `topology_db_add_node(name, type, lat, lon, status)`: Adds or updates a node
  - Deduplicates by hostname (name field is unique key)
  - Updates status and last_seen timestamp
  - Mutex-protected for thread safety

- `topology_db_add_connection(from_name, to_name, rtt_ms)`: Adds RTT sample to connection
  - Deduplicates by (from_name, to_name) hostname pair
  - Stores up to 10 RTT samples in circular buffer
  - Updates sample_count and next_sample_index

- `topology_db_calculate_aggregate_stats()`: Calculates min/avg/max RTT for all connections
  - Called after crawl completes, before JSON export
  - Computes statistics from circular buffer samples

- `topology_db_write_to_file(filepath)`: Exports entire topology as JSON
  - Writes to `/tmp/topology.json`
  - Includes version, timestamp, nodes, connections, statistics
  - Consumed by map visualization

- `topology_db_crawl_mesh_network()`: BFS mesh crawler
  - Fetches hosts list from localhost seed
  - Discovers nodes recursively via LQM neighbor links
  - Filters international nodes (HB prefix boundary)
  - Fetches phones separately from OLSR services

- `topology_db_strip_hostname_prefix(hostname, buffer, size)`: Removes interface prefixes
  - Strips mid1., mid2., dtdlink., etc. from hostnames
  - Prevents duplicate nodes for multi-interface devices

**Example JSON Output:** (Hostname-Based, v2.5.39+)
```json
{
  "version": "2.0",
  "generated_at": "2025-10-22T22:01:00Z",
  "source_node": {
    "name": "HB9BLA-VM-1",
    "type": "server"
  },
  "nodes": [
    {
      "name": "HB9BLA-VM-1",
      "type": "server",
      "lat": "47.47446",
      "lon": "7.76728",
      "status": "ONLINE",
      "last_seen": 1761170453
    },
    {
      "name": "HB9BLA-HAP-2",
      "type": "router",
      "lat": "47.47456",
      "lon": "7.76718",
      "status": "ONLINE",
      "last_seen": 1761170450
    },
    {
      "name": "196630",
      "type": "phone",
      "lat": "47.47458",
      "lon": "7.76720",
      "status": "ONLINE",
      "last_seen": 1761170212
    }
  ],
  "connections": [
    {
      "source": "HB9BLA-VM-1",
      "target": "HB9BLA-HAP-2",
      "rtt_avg_ms": 1.450,
      "rtt_min_ms": 0.370,
      "rtt_max_ms": 2.100,
      "sample_count": 10,
      "last_updated": 1761170450
    },
    {
      "source": "HB9BLA-HAP-2",
      "target": "196630",
      "rtt_avg_ms": 0.100,
      "rtt_min_ms": 0.100,
      "rtt_max_ms": 0.100,
      "sample_count": 1,
      "last_updated": 1761170212
    }
  ],
  "statistics": {
    "total_nodes": 186,
    "total_connections": 413
  }
}
```

**Key Changes from v2.5.38:**
- Nodes identified by hostname (`name` field) instead of IP address
- Connections use `source` and `target` (hostnames) instead of `from` and `to` (IPs)
- No `ip` field - hostname is the unique key
- Connections include `last_updated` timestamp
- Matches industry-standard graph terminology

#### 4.12.4 Two-Phase Location Fetching

**Problem**: Phones don't have sysinfo.json endpoints - only routers and servers do.

**Solution**: Two-phase approach implemented in `topology_db_fetch_all_locations()`:

**Phase 1: Fetch Router Locations** (HTTP GET to sysinfo.json)
```c
for each node in topology:
    if node.type == "router" OR node.type == "server":
        url = "http://{node.ip}/cgi-bin/sysinfo.json"
        if uac_http_get_location(url, &lat, &lon) == 0:
            node.lat = lat
            node.lon = lon
            fetched++
```

**Phase 2: Propagate to Phones** (via topology connections)
```c
for each phone in topology:
    if phone.lat is empty:
        // Find connection where phone is destination
        connection = find_connection_to(phone.ip)
        router = find_node_by_ip(connection.from_ip)

        if router.lat is valid:
            // Copy router location with random offset (~100m)
            phone.lat = router.lat + random_offset_lat()
            phone.lon = router.lon + random_offset_lon()
            propagated++
```

**Random Offset for Phone Visibility:**
```c
// Offset by ~100m in random direction for map visibility
// 0.001 degrees ≈ 111 meters latitude, ~100m longitude at mid-latitudes

double router_lat = atof(router->lat);
double router_lon = atof(router->lon);

// Generate random offset: -0.001 to +0.001 degrees
// Use phone IP as seed for consistency across test cycles
unsigned int seed = 0;
for (const char *p = phone->ip; *p; p++) {
    seed = seed * 31 + *p;  // Simple hash of IP string
}
srand(seed);

double lat_offset = ((double)rand() / RAND_MAX * 0.002) - 0.001;
double lon_offset = ((double)rand() / RAND_MAX * 0.002) - 0.001;

double phone_lat = router_lat + lat_offset;
double phone_lon = router_lon + lon_offset;

snprintf(phone->lat, sizeof(phone->lat), "%.7f", phone_lat);
snprintf(phone->lon, sizeof(phone->lon), "%.7f", phone_lon);
```

**Rationale**:
- Phones near same router would stack on same coordinates
- Random offset makes phones visible when zooming in
- Consistent seed ensures same phone always has same offset
- ~100m is large enough for visibility, small enough to stay "near" router

**Log Output:**
```
TOPOLOGY_DB: Phase 1: Fetching locations for routers and servers...
TOPOLOGY_DB: Fetched location for 10.107.42.189 (router): 46.81671, 7.15121
TOPOLOGY_DB: Phase 2: Propagating router locations to phones...
TOPOLOGY_DB: Location fetch complete: 13 routers fetched, 3 failed, 16 phones propagated
```

#### 4.12.5 HTTP Client for Location Data

**Implementation:** `uac_http_client.c`

**Purpose**: Fetch geographic coordinates from AREDN node sysinfo.json endpoints.

**Key Functions:**

- `uac_http_get_location()`: Fetches lat/lon from URL
  - **URL Format**: `http://10.107.42.189/cgi-bin/sysinfo.json`
  - **Timeout**: 2 seconds (configurable)
  - **Returns**: 0 on success, -1 on failure

**Implementation Details:**

1. **URL Parsing** (`parse_url()`):
   - Supports `http://host:port/path` format
   - Extracts host, port (default: 80), path
   - HTTPS not supported (unnecessary for mesh networks)

2. **HTTP GET Request** (`http_get()`):
   - Creates TCP socket
   - Sets SO_SNDTIMEO and SO_RCVTIMEO (2 seconds)
   - Sends HTTP/1.0 GET request
   - Receives response and strips HTTP headers
   - Returns JSON body only

3. **JSON Parsing** (`extract_json_string()`):
   - Looks for `"key": "value"` pattern
   - **Handles optional whitespace** after key and colon
   - Extracts lat and lon fields
   - Simple string-based parsing (no external dependencies)

**JSON Parsing Implementation:**

The parser handles optional whitespace in JSON formatting:

```c
// Looks for "lat" then skips optional whitespace before/after colon
char pattern[128];
snprintf(pattern, sizeof(pattern), "\"%s\"", key);  // Match key only

const char *start = strstr(json, pattern);
start += strlen(pattern);

// Skip optional whitespace and colon
while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
    start++;
}
if (*start != ':') return -1;
start++;  // Skip colon

// Skip optional whitespace after colon
while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
    start++;
}

// Now expect opening quote
if (*start != '"') return -1;
start++;
```

**AREDN sysinfo.json Format:**
```json
{
  "node": "router-1",
  "lat": "46.81671",
  "lon": "7.15121",
  "grid_square": "JN36",
  "band": "5.8GHz"
}
```

#### 4.12.6 Bulk Tester Integration

The traceroute functionality is integrated into the bulk tester loop to run for all online phones:

```c
if (options_result.online) {
    // Track RTT stats
    if (g_uac_ping_count <= 0) {
        total_avg_rtt += options_result.avg_rtt_ms;
        rtt_count++;
    }

    // PHASE 2.5: Traceroute Test
    if (g_uac_traceroute_enabled) {
        LOG_INFO("Tracing route to %s (%s)...", user->user_id, user->display_name);

        TracerouteHop hops[30];
        int hop_count = 0;

        if (uac_traceroute_to_phone(user->user_id, g_uac_traceroute_max_hops, hops, &hop_count) == 0) {
            // Get source IP for this route
            char source_ip[INET_ADDRSTRLEN];
            if (get_source_ip_for_target(ip_str, source_ip) == 0) {
                // Add source node (this server)
                topology_db_add_node(source_ip, "server", "this-node", NULL, NULL, "ONLINE");

                // Add destination node (phone)
                topology_db_add_node(ip_str, "phone", user->display_name, NULL, NULL, "ONLINE");

                // Add intermediate hops and connections
                char prev_ip[INET_ADDRSTRLEN];
                strncpy(prev_ip, source_ip, sizeof(prev_ip));

                for (int h = 0; h < hop_count; h++) {
                    if (!hops[h].timeout) {
                        // Determine node type
                        const char *node_type = (strcmp(hops[h].ip_address, ip_str) == 0) ?
                                              "phone" : "router";

                        // Add node to topology
                        topology_db_add_node(hops[h].ip_address, node_type,
                                           hops[h].hostname, NULL, NULL, "ONLINE");

                        // Add connection from previous hop
                        if (prev_ip[0] != '\0') {
                            topology_db_add_connection(prev_ip, hops[h].ip_address,
                                                     hops[h].rtt_ms);
                        }

                        strncpy(prev_ip, hops[h].ip_address, sizeof(prev_ip));
                    }
                }

                LOG_INFO("Topology updated: %d hops added for %s", hop_count, user->user_id);
            }
        }
    }

    // NOW write results and continue
    uac_test_db_write_result(&db_result);
    pthread_mutex_lock(&registered_users_mutex);
    continue;
}
```

**Topology Export:**

After each bulk test cycle:
```c
// Export topology to JSON for map visualization
topology_db_export_json("/tmp/arednmon/network_topology.json");
LOG_INFO("Topology mapping complete: %d nodes, %d connections",
         g_node_count, g_connection_count);
```

#### 4.12.7 Map Visualization

**Implementation:** `arednmon` CGI script (JavaScript + Leaflet.js)

**Features:**

1. **Interactive Map Display**
   - OpenStreetMap tile layer
   - **Geographic Bounds**: Constrained to Switzerland (45.8°N-47.8°N, 5.9°E-10.5°E)
   - **Max Bounds Enforcement**: Prevents panning/zooming outside Switzerland
   - **No Auto-Zoom**: Map stays at Switzerland overview on initial load
   - **Manual Refresh**: "Refresh Topology" button fits map to show all nodes
   - **Increased Height**: 1200px (double the original 600px) for better visibility
   - Click nodes to highlight route from server and show traceroute
   - Tooltips with node information

   ```javascript
   // Initialize map with Switzerland bounds
   const switzerlandBounds = [
       [45.8, 5.9],  // Southwest corner
       [47.8, 10.5]  // Northeast corner
   ];

   topologyMap = L.map('topologyMap', {
       maxBounds: switzerlandBounds,
       maxBoundsViscosity: 1.0  // Prevent panning outside bounds
   }).fitBounds(switzerlandBounds);

   L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
       maxZoom: 18,
       minZoom: 7  // Prevent zooming out beyond Switzerland
   }).addTo(topologyMap);
   ```

2. **Color-Coded Routes**
   - **Green** (<100ms RTT): Excellent connection quality
   - **Orange** (100-200ms RTT): Medium connection quality
   - **Red** (>200ms RTT): Poor connection quality

   ```javascript
   function getRttColor(rtt) {
       if (rtt > 200) return '#f44336';  // Red - poor
       if (rtt > 100) return '#FF9800';  // Orange - medium
       return '#4CAF50';                  // Green - good
   }
   ```

3. **RTT Information Labels**
   - Permanent labels at connection midpoints
   - Show average RTT in milliseconds
   - Colored border matching connection quality

   ```javascript
   L.marker(midpoint, {
       icon: L.divIcon({
           html: '<div style="background-color: white; padding: 2px 4px; ' +
                 'border-radius: 3px; font-size: 10px; font-weight: bold; ' +
                 'color: ' + color + '; border: 1px solid ' + color + ';">' +
                 conn.rtt_avg_ms.toFixed(1) + 'ms</div>',
           iconSize: [40, 20],
           iconAnchor: [20, 10]
       })
   }).addTo(topologyMap);
   ```

4. **Node Markers**
   - **Blue** (●): Server (this node)
   - **Green** (●): Phone (destination)
   - **Orange** (●): Router (intermediate hop)

5. **Traceroute Display**
   - **Panel Location**: Below the map
   - **Trigger**: Automatically shown when any node is clicked
   - **Live Execution**: Performs real-time ICMP traceroute to selected node
   - **Hop Information**: Shows hop number, hostname, IP address, and RTT
   - **Timeout Handling**: Displays "*" for unresponsive hops

   **CGI Endpoint**: `/cgi-bin/traceroute_json?ip=<target_ip>`

   **Implementation**:
   ```javascript
   // Fetch and display traceroute
   async function fetchTraceroute(targetIp, targetName) {
       const response = await fetch('/cgi-bin/traceroute_json?ip=' + targetIp);
       const data = await response.json();

       // Display each hop
       data.hops.forEach(hop => {
           if (hop.timeout) {
               html += '<div>Hop ' + hop.hop + ': * * * (timeout)</div>';
           } else {
               html += '<div>Hop ' + hop.hop + ': ' + hop.hostname +
                      ' (' + hop.ip + ') - ' + hop.rtt_ms + ' ms</div>';
           }
       });
   }
   ```

   **Traceroute Shell Script**: `/www/cgi-bin/traceroute_json`
   - Uses `traceroute -I` (ICMP) for path discovery
   - 30 hop maximum, 2 second timeout per hop
   - Performs reverse DNS lookup for hostnames
   - Strips `.local.mesh` suffix from hostnames
   - Returns JSON with hop array

6. **Map Legend**
   ```
   Node Types:
   ● Server - This node (source)
   ● Phone - SIP phone (destination)
   ● Router - Intermediate hop

   Connection Quality (RTT):
   ━━━ Green - Excellent (<100ms)
   ━━━ Orange - Medium (100-200ms)
   ━━━ Red - Poor (>200ms)

   Interaction: Click any node to highlight the route and show traceroute results below
   ```

7. **Map Controls**
   - **Reset View**: Returns to Switzerland overview
   - **Refresh Topology**: Reloads data and fits map to show all active nodes

**Data Loading:**
```javascript
async function loadTopology() {
    const response = await fetch('/cgi-bin/topology_json');
    const data = await response.json();

    if (data.error) {
        console.warn('No topology data available yet');
        return;
    }

    // Add node markers
    data.nodes.forEach(node => {
        if (node.lat && node.lon) {
            const color = getNodeColor(node.type);
            const marker = L.circleMarker([node.lat, node.lon], {
                color: color,
                radius: 8
            }).addTo(topologyMap);

            marker.bindTooltip(node.name + ' (' + node.type + ')');
            nodeMarkers[node.ip] = marker;
        }
    });

    // Add connections with color coding
    data.connections.forEach(conn => {
        const fromMarker = nodeMarkers[conn.from];
        const toMarker = nodeMarkers[conn.to];

        if (fromMarker && toMarker) {
            const color = getRttColor(conn.rtt_avg_ms);

            const line = L.polyline([
                fromMarker.getLatLng(),
                toMarker.getLatLng()
            ], {
                color: color,
                weight: 3,
                opacity: 0.7
            }).addTo(topologyMap);

            // Add RTT label at midpoint
            const midpoint = [
                (fromMarker.getLatLng().lat + toMarker.getLatLng().lat) / 2,
                (fromMarker.getLatLng().lng + toMarker.getLatLng().lng) / 2
            ];

            L.marker(midpoint, {
                icon: L.divIcon({
                    className: 'rtt-label',
                    html: '<div style="...">' +
                          conn.rtt_avg_ms.toFixed(1) + 'ms</div>',
                    iconSize: [40, 20],
                    iconAnchor: [20, 10]
                })
            }).addTo(topologyMap);

            connectionLines.push(line);
        }
    });
}
```

**Auto-Refresh:**
```javascript
// Reload topology every 60 seconds
setInterval(loadTopology, 60000);
```

#### 4.12.8 Configuration

Topology mapping configuration in `/etc/phonebook.conf`:

```ini
# ============================================================================
# TOPOLOGY MAPPING SETTINGS
# ============================================================================

# Enable ICMP traceroute to discover network paths
# 0 = disabled, 1 = enabled
UAC_TRACEROUTE_ENABLED=1

# Maximum hops for traceroute (default: 30)
UAC_TRACEROUTE_MAX_HOPS=30
```

**Performance Impact:**
- Traceroute adds ~2-3 seconds per phone test
- For 45 phones: +90-135 seconds per test cycle
- Total cycle time: ~10-12 minutes (vs 8-10 without traceroute)
- Minimal CPU/network impact (ICMP probes are lightweight)

#### 4.12.9 CGI Endpoints

**Topology JSON Endpoint:**

**File:** `/www/cgi-bin/topology_json` (shell script)

```bash
#!/bin/sh
# topology_json - Network Topology JSON Endpoint

TOPOLOGY_FILE="/tmp/arednmon/network_topology.json"

echo "Content-Type: application/json; charset=utf-8"
echo ""

if [ ! -f "$TOPOLOGY_FILE" ]; then
    # Return empty topology with error
    cat <<'EOF'
{
  "version": "2.0",
  "generated_at": "",
  "error": "No topology data available yet",
  "source_node": {"ip": "0.0.0.0", "name": "unknown", "type": "server"},
  "nodes": [],
  "connections": [],
  "statistics": {"total_nodes": 0, "total_connections": 0}
}
EOF
    exit 0
fi

cat "$TOPOLOGY_FILE"
```

**Traceroute JSON Endpoint:**

**File:** `/www/cgi-bin/traceroute_json` (shell script)

**Purpose:** Performs live ICMP traceroute to a specified IP address and returns results as JSON.

**Parameters:**
- `ip`: Target IP address (required, passed via query string)

**Example Request:**
```bash
curl "http://localnode.local.mesh/cgi-bin/traceroute_json?ip=10.197.143.20"
```

**Response Format:**
```json
{
  "target_ip": "10.197.143.20",
  "hops": [
    {
      "hop": 1,
      "ip": "10.167.247.74",
      "hostname": "router-1",
      "rtt_ms": 1.45,
      "timeout": false
    },
    {
      "hop": 2,
      "ip": "10.197.143.20",
      "hostname": "441530",
      "rtt_ms": 2.10,
      "timeout": false
    }
  ]
}
```

**Implementation:**
- Uses `traceroute -I` (ICMP Echo) instead of UDP for compatibility
- Maximum 30 hops with 2-second timeout per hop
- Performs reverse DNS lookup via `nslookup`
- Strips `.local.mesh` suffix from hostnames
- Calculates average RTT from multiple probe samples
- Handles timeouts by marking hop as `{"timeout": true}`

**Error Handling:**
```json
{
  "error": "Missing IP parameter. Usage: /cgi-bin/traceroute_json?ip=<target_ip>",
  "target_ip": "",
  "hops": []
}
```

**AREDNmon Dashboard:**

Enhanced with topology map section in `/www/cgi-bin/arednmon`:
- Displays interactive Leaflet.js map (1200px height) above phone monitoring table
- Geographic bounds constrained to Switzerland (45.8°N-47.8°N, 5.9°E-10.5°E)
- Loads topology from `/cgi-bin/topology_json` endpoint
- Shows color-coded routes and RTT labels
- Interactive traceroute panel below map
- Clicking any node triggers `/cgi-bin/traceroute_json` and displays results
- Refreshes topology every 60 seconds

#### 4.12.10 Data Flow

**Complete Topology Mapping Flow:**

```
1. Bulk Test Cycle Starts (every 10 minutes)
   ↓
2. For each online phone:
   ├─ OPTIONS test succeeds
   ├─ ICMP traceroute to phone
   │  └─ Discovers 1-5 intermediate hops
   ├─ Add nodes to topology database
   │  └─ Server, routers, phone
   ├─ Add connections to topology database
   │  └─ Hop-to-hop with RTT measurements
   └─ Continue to next phone
   ↓
3. After all phones tested:
   ├─ Phase 1: Fetch router locations
   │  └─ HTTP GET to sysinfo.json for each router
   │     └─ Extract lat/lon from JSON
   ├─ Phase 2: Propagate to phones
   │  └─ Find router connection, copy coordinates with offset
   ├─ Export to JSON
   │  └─ Write /tmp/arednmon/network_topology.json
   └─ Log summary (nodes, connections, locations)
   ↓
4. Browser requests /cgi-bin/arednmon
   ↓
5. JavaScript fetches /cgi-bin/topology_json
   ↓
6. Map renders with Leaflet.js
   └─ Nodes plotted by lat/lon
   └─ Connections drawn with color-coded lines
   └─ RTT labels displayed at midpoints
```

#### 4.12.11 Error Handling

**Network Failures:**
- DNS resolution failure: Skip phone, continue to next
- Traceroute timeout: Record hop as timeout, continue
- HTTP GET timeout (2s): Mark location fetch as failed, continue
- JSON parse failure: Mark location fetch as failed, continue

**Data Validation:**
- Empty coordinates: Skip node on map (don't crash)
- Invalid RTT values: Use default color (gray)
- Missing topology file: Return empty topology with error message

**Graceful Degradation:**
- No location data: Map shows nodes without positions (table still works)
- Traceroute disabled: No topology map (phone monitoring still works)
- HTTP client failure: Phase 1 fails, Phase 2 continues with available data

#### 4.12.12 Thread Safety and Resource Usage

**Thread Safety:**
- Topology database protected by `g_topology_mutex`
- Bulk tester is only writer (single-threaded)
- Map endpoint is reader (read-only access)

**Memory Usage:**
- Topology database: ~100 KB (500 nodes × 200 bytes each)
- JSON export: ~50 KB (typical mesh with 50 nodes, 30 connections)
- Total RAM impact: <200 KB

#### 4.12.13 Mesh Network Crawler

**Purpose**: Discovers all mesh nodes by fetching the OLSR hosts list and crawling each node to gather topology information.

**Implementation:** `topology_db.c` (`mesh_crawler_thread()`)

**Key Features:**

1. **Hostname Capacity** (v2.5.40+):
   - Array size: 500 entries (increased from 200)
   - Per-hostname buffer: 40 bytes (optimized from 256 bytes)
   - Memory footprint: 20 KB (60% reduction from 51 KB)
   - Rationale: AREDN hostnames without `.local.mesh` suffix fit in 40 bytes

2. **Hostname Filtering**:
   ```c
   // Only accept canonical short hostnames (without .local.mesh suffix)
   if (len > 0 && strstr(name_start, "local.mesh") == NULL) {
       // Truncate to 39 chars max (leave room for null terminator)
       if (len >= 40) len = 39;
       strncpy(hostnames[count], name_start, len);
       hostnames[count][len] = '\0';
       count++;
   }
   ```
   This filters out interface-specific names like:
   - `dtdlink.HB9BLA-VM-1.local.mesh`
   - `lan.HB9BLA-VM-1.local.mesh`
   - `mid.HB9BLA-VM-1.local.mesh`

   And keeps only canonical hostnames like:
   - `HB9BLA-VM-1`
   - `HB9BLA-HAP-2`

3. **Seed-Only Crawl** (Performance Optimization):
   - Fetches complete hosts list from localhost seed node only
   - DNS query: `http://127.0.0.1/cgi-bin/sysinfo.json?hosts=1`
   - Eliminates redundant fetches from every discovered node
   - Reduces network traffic by 95%+

4. **Crawler Logging** (Minimal):
   Simple INFO-level messages for major crawler events only:

   **BFS Crawl Messages:**
   - `Starting BFS mesh network crawl from localhost...`
   - `Seeding crawl queue with %d initial hostnames...`
   - `Starting BFS crawl with %d nodes in queue...`
   - `BOUNDARY: Skipping international node %s` (for filtered nodes)
   - `BFS mesh crawl complete: processed %d nodes, discovered %d nodes with details, %d total in queue`
   - `Added %d phones from OLSR services to topology`

   **Location & Statistics:**
   - `Fetching location data for %d nodes...`
   - `Location fetch complete: %d routers fetched, %d failed, %d phones propagated`
   - `Calculating aggregate statistics for %d connections...`
   - `Statistics calculation complete`

   **JSON Export:**
   - `Writing topology to %s...`
   - `Topology written to %s (%d nodes, %d connections)`

   **Benefits:**
   - Minimal log buffer usage
   - Clear major operation boundaries
   - BOUNDARY messages for debugging filtering logic
   - No per-node detailed diagnostics (use web UI instead)

5. **Node Aging System** (v2.5.40+):
   - Configuration: `TOPOLOGY_NODE_TIMEOUT_SECONDS` (default: 3600s)
   - Removes nodes not seen for > 1 hour
   - Prevents stale offline nodes cluttering topology map
   - Log: `TOPOLOGY_DB: Aged out 3 stale nodes (last seen > 3600s ago)`

6. **LQM Link Extraction** (Enhanced):
   - Parses ALL LQM tracker entries (RF, DTD, Tunnel, xlink0-7)
   - Discovers direct neighbor connections
   - Measures per-link RTT and quality
   - Implementation in `fetch_lqm_links_from_host()`:
   ```c
   static int fetch_lqm_links_from_host(const char *hostname,
                                        char *neighbors_buf,
                                        size_t buf_size) {
       // Fetch sysinfo.json?lqm=1
       // Parse each tracker entry
       // Extract neighbor hostname and ping_time_ms
       // Add connection to topology database
       // Append neighbor to buffer for logging
       return link_count;
   }
   ```

7. **Hostname-Based Topology API** (v2.5.39+):
   - Changed from IP-based to hostname-based node identification
   - Resolves tunnel/DTD IPs to main mesh IPs
   - Uses DNS reverse lookup to get canonical hostnames
   - Prevents duplicate nodes for multi-interface devices
   - Example: `10.167.247.74` (dtdlink) → `HB9BLA-VM-1`

8. **Boundary Filtering** (Swiss HB Prefix):
   - Prevents database overflow by filtering international supernode connections
   - Only crawls nodes with Swiss call sign prefix (HB*) or phones (numeric)
   - Blocks international supernodes like `m0mfs-eng-supernode`, `g7uod-eng-supernode`
   - Implementation in `should_crawl_node()`:
   ```c
   static bool should_crawl_node(const char *hostname) {
       // Always crawl phones (numeric)
       if (is_numeric(hostname)) return true;

       // Only crawl Swiss nodes (HB* prefix)
       if (hostname[0] == 'H' && hostname[1] == 'B') return true;

       return false;  // Skip international nodes
   }
   ```
   - Logs boundary decisions: `LOG_INFO("BOUNDARY: Skipping international node %s")`
   - Prevents 500-node database limit from filling with foreign routers
   - Swiss supernodes (e.g., `HB9BLA-BASEL-SUPERNODE`) are included but their international neighbors are filtered

**Crawler Workflow (BFS Algorithm):**
```
1. Initialize BFS queues (heap-allocated)
   └─ crawl_queue[1000]: Nodes to be crawled
   └─ visited[1000]: Nodes already crawled
   └─ initial_hostnames[500]: Seed from localhost

2. Fetch seed hostnames from localhost only
   └─ HTTP GET: http://127.0.0.1/cgi-bin/sysinfo.json?hosts=1
   └─ Parse up to 500 canonical hostnames
   └─ Add all to crawl_queue

3. BFS crawl loop (while queue not empty and visited < 1000):
   a. Pop next hostname from queue
   b. Skip if already visited, then mark as visited
   c. Check if numeric (phone) → skip phones during crawl
   d. Fetch router details:
      └─ HTTP GET: http://{hostname}.local.mesh/cgi-bin/sysinfo.json
      └─ Extract: lat, lon
      └─ Add router to topology database
   e. Fetch LQM neighbor links:
      └─ HTTP GET: http://{hostname}.local.mesh/cgi-bin/sysinfo.json?lqm=1
      └─ Parse all tracker entries (RF, DTD, Tunnel, xlink*)
      └─ Strip prefixes (mid1., dtdlink., etc.) from neighbor hostnames
      └─ Add connections to topology database
   f. Filter and queue neighbors for recursive discovery:
      └─ For each neighbor: check should_crawl_node()
         • If HB* prefix or numeric → add to crawl_queue (if not already queued/visited)
         • Otherwise → log "BOUNDARY: Skipping international node"
   g. 100ms delay (usleep)

4. After BFS complete, fetch phones separately:
   └─ Parse /var/run/services_olsr for [phone] tags
   └─ Extract phone numbers and router associations
   └─ Add phone nodes and connections to topology

5. Fetch location data:
   └─ Phase 1: Fetch GPS coordinates for routers (http://{router}.local.mesh/cgi-bin/sysinfo.json)
   └─ Phase 2: Propagate router locations to phones (with random offset)

6. Calculate aggregate RTT statistics:
   └─ Compute avg/min/max for each connection from samples

7. Age out stale nodes:
   └─ Remove nodes not seen for > TOPOLOGY_NODE_TIMEOUT_SECONDS
   └─ Remove orphaned connections

8. Export topology JSON:
   └─ Write to /tmp/topology.json
   └─ Log summary: X nodes, Y connections

9. Sleep until next interval (TOPOLOGY_CRAWLER_INTERVAL_SECONDS)
   └─ Repeat cycle
```

**Configuration:**
```ini
# /etc/phonebook.conf

# Enable mesh network crawler
TOPOLOGY_CRAWLER_ENABLED=1

# Crawler interval (seconds)
TOPOLOGY_CRAWLER_INTERVAL_SECONDS=600

# Node aging timeout (seconds)
# Nodes not seen for this duration are removed
TOPOLOGY_NODE_TIMEOUT_SECONDS=3600

# Fetch location data from sysinfo.json
TOPOLOGY_FETCH_LOCATIONS=1
```

**Performance Impact:**
- Crawl time for 215 nodes: ~25-30 seconds
- Memory: 20 KB (hostname array) + 100 KB (topology DB) = 120 KB total
- Network: ~1 KB per node × 215 = 215 KB per cycle
- CPU: Minimal (mostly I/O wait)

**Example Logs:**
```
TOPOLOGY_DB: Starting BFS mesh network crawl from localhost...
TOPOLOGY_DB: Fetched 215 hostnames from localhost
TOPOLOGY_DB: Seeding crawl queue with 215 initial hostnames...
TOPOLOGY_DB: Starting BFS crawl with 215 nodes in queue...
TOPOLOGY_DB: BOUNDARY: Skipping international node m0mfs-eng-supernode
TOPOLOGY_DB: BOUNDARY: Skipping international node g7uod-eng-supernode
TOPOLOGY_DB: BOUNDARY: Skipping international node n2mh-nj-supernode
TOPOLOGY_DB: BFS mesh crawl complete: processed 189 nodes, discovered 156 nodes with details, 215 total in queue
TOPOLOGY_DB: Fetched 33 phones from OLSR services
TOPOLOGY_DB: Added 33 phones from OLSR services to topology
TOPOLOGY_DB: Fetching location data for 189 nodes...
TOPOLOGY_DB: Location fetch complete: 156 routers fetched, 0 failed, 33 phones propagated
TOPOLOGY_DB: Calculating aggregate statistics for 425 connections...
TOPOLOGY_DB: Statistics calculation complete
TOPOLOGY_DB: Cleanup complete: removed 3 stale nodes, 12 orphaned connections
TOPOLOGY_DB: Writing topology to /tmp/topology.json...
TOPOLOGY_DB: Topology written to /tmp/topology.json (186 nodes, 413 connections)
```

#### 4.12.14 AREDNmon UI Enhancements (v2.5.38+)

**Performance Optimizations:**

1. **Update Interval Change**:
   - Old: 10 seconds (causes UI lag with 215 nodes)
   - New: 30 seconds
   - Result: 66% reduction in CPU load, smoother user experience

2. **Connection Field Rename**:
   - Changed from `from`/`to` to `source`/`target` for clarity
   - Matches industry-standard graph terminology
   - Updates in both topology database and JavaScript

3. **Map Visualization Improvements**:
   - **Popup Width**: Explicit `maxWidth: 300px` for Leaflet popups
   - **Zoom Preservation**: Map zoom level maintained during topology refresh
   - **Margin-Based Padding**: Fixed layout issues with proper CSS margin instead of padding
   - **Connection Labels**: RTT values displayed at connection midpoints with quality-based coloring

**Cache Control Headers** (topology_json endpoint):
```bash
echo "Cache-Control: no-cache, no-store, must-revalidate"
echo "Pragma: no-cache"
echo "Expires: 0"
```
Ensures browser always fetches latest topology data without stale cache issues.

## 5. Health Reporting

The Health Reporting subsystem monitors the operational health of the AREDN-Phonebook service itself, tracking CPU, memory, thread responsiveness, and crash diagnostics. This provides visibility into software stability and enables proactive maintenance.

### 5.1 Health Monitoring Scope

**Software health monitoring** (not mesh/routing):
- CPU usage of AREDN-Phonebook process
- Memory consumption
- Thread responsiveness (phonebook fetcher, status updater, UAC tester)
- Uptime and restart counts
- Crash detection and reporting
- Service health score (0-100)

**Thread health tracked via heartbeats:**
```c
g_fetcher_last_heartbeat    // Updated every phonebook fetch cycle
g_updater_last_heartbeat    // Updated every status update cycle
g_uac_tester_last_heartbeat // Updated every test cycle
```

**Passive safety monitoring:**
- Checks heartbeat freshness every 30 minutes
- Detects hung threads (no heartbeat for >30 min)
- Automatic thread restart on failure

### 5.2 Health Metrics Collected

**Process metrics:**
- `cpu_pct`: CPU usage percentage (0-100)
- `mem_mb`: Memory usage in megabytes
- `uptime_seconds`: Time since process start
- `restart_count`: Number of restarts in last 24h

**Thread health:**
- `threads_responsive`: Boolean (all threads heartbeating)
- `fetcher_responsive`: Phonebook fetcher thread status
- `updater_responsive`: Status updater thread status
- `tester_responsive`: UAC bulk tester thread status

**Service metrics:**
- `registered_users_count`: Active SIP registrations (dynamic)
- `directory_entries_count`: Phonebook entries (from CSV)
- `active_calls_count`: Current SIP calls in progress
- `phonebook_last_updated`: Timestamp of last phonebook fetch
- `phonebook_fetch_status`: SUCCESS, FAILED, STALE

**Health score computation:**
```
health_score = 100
- (cpu_pct > 20% ? 10 : 0)
- (mem_mb > 12 ? 10 : 0)
- (!threads_responsive ? 30 : 0)
- (restart_count > 0 ? 20 : 0)
- (phonebook_fetch_status == FAILED ? 10 : 0)
```

### 5.3 Crash Detection

**Signal handlers installed for:**
- SIGSEGV (segmentation fault)
- SIGBUS (bus error)
- SIGFPE (floating point exception)
- SIGABRT (abort)
- SIGILL (illegal instruction)

**Crash state captured:**
- Signal number and name
- Thread ID where crash occurred
- Last operation breadcrumb (thread-local tracking)
- Memory and CPU at crash time
- Stack backtrace (instruction pointers, 5-10 frames)
- Context: active calls, pending operations, system state

**Crash state persistence:**
- Written to `/tmp/meshmon_crash.bin` (RAM, survives crash but not reboot)
- Loaded on next startup, converted to crash report
- Sent to collector immediately after restart
- No flash writes (preserves router lifespan)

### 5.4 Reporting Frequency

**Event-driven health reporting:**

```
Scheduled baseline:   Every 4 hours (heartbeat)
Event-driven:         Immediately on:
                      - CPU change > 20%
                      - Memory change > 10 MB
                      - Thread becomes unresponsive
                      - Restart occurred
                      - Health score drops > 15 points
                      - Crash detected (on restart)
```

**Rationale:**
- Normal conditions: 6 reports per day (4-hour baseline)
- Problem conditions: Immediate notification
- **97% bandwidth reduction** vs fixed 10-minute intervals
- Maintains responsiveness while minimizing overhead

### 5.5 Dual-Output Architecture

Health reporting implements a **dual-output strategy** to support both local node visibility and mesh-wide monitoring:

**Output 1: Local File** - For AREDNmon dashboard integration
- Path: `/tmp/software_health.json`
- Update frequency: Every 60 seconds
- Storage: RAM (tmpfs) - no flash writes
- Consumer: Local CGI endpoint `/cgi-bin/health_status`
- Purpose: Immediate local visibility for field diagnostics

**Output 2: HTTP POST** - For collector aggregation
- Endpoint: Configurable `COLLECTOR_URL` (e.g., `http://pi-collector.local.mesh:5000/ingest`)
- Update frequency: Event-driven (4h baseline + events)
- Retry: Simple retry on failure, no buffering
- Purpose: Mesh-wide monitoring and issue detection

**Data flow:**
```
Health Monitoring Thread
        │
        ├──> Write /tmp/software_health.json (every 60s)
        │    └──> CGI /cgi-bin/health_status
        │         └──> AREDNmon Dashboard
        │
        └──> HTTP POST to Collector (4h + events)
             └──> Raspberry Pi Collector
                  └──> OpenWISP Dashboard
```

**Graceful degradation:**
- If collector is unreachable: Local monitoring continues unaffected
- If local file write fails: Remote reporting continues
- Both outputs use identical JSON format (no duplication of logic)

### 5.6 Local Dashboard Integration

#### 5.6.1 Health Status File

**Path**: `/tmp/software_health.json`

**Format** (agent_health message schema):
```json
{
  "schema": "meshmon.v2",
  "type": "agent_health",
  "node": "HB9BLA-HAP-2",
  "timestamp": 1760979845,
  "sent_at": "2025-10-20T17:04:05Z",
  "reporting_reason": "scheduled",
  "cpu_pct": 0.5,
  "mem_mb": 0.3,
  "uptime_seconds": 120,
  "restart_count": 0,
  "health_score": 100,
  "threads": {
    "all_responsive": true,
    "status_updater": {
      "responsive": true,
      "last_heartbeat": "2025-10-20T16:49:39Z",
      "heartbeat_age_seconds": 34
    },
    "passive_safety": {
      "responsive": true,
      "last_heartbeat": "2025-10-20T17:03:51Z",
      "heartbeat_age_seconds": 44
    },
    "uac_bulk_tester": {
      "responsive": true,
      "last_heartbeat": "2025-10-20T17:04:05Z",
      "heartbeat_age_seconds": 0
    },
    "health_reporter": {
      "responsive": true,
      "last_heartbeat": "2025-10-20T17:04:05Z",
      "heartbeat_age_seconds": 0
    },
    "phonebook_fetcher": {
      "responsive": true,
      "last_heartbeat": "2025-10-20T16:47:52Z",
      "heartbeat_age_seconds": 973
    }
  },
  "sip_service": {
    "registered_users": 0,
    "directory_entries": 226,
    "active_calls": 0
  },
  "phonebook": {
    "last_updated": "2025-10-20T16:47:51Z",
    "fetch_status": "BOOT",
    "csv_hash": "00000000F5C4180A",
    "entries_loaded": 226
  },
  "checks": {
    "memory_stable": true,
    "no_recent_crashes": true,
    "sip_service_ok": true,
    "phonebook_current": true,
    "cpu_normal": true,
    "all_threads_responsive": true
  }
}
```

**Update frequency**: Every 60 seconds (configurable via `HEALTH_LOCAL_UPDATE_SECONDS`)

**Storage rationale**:
- RAM-only (no flash writes) preserves router lifespan
- Survives process restarts (updated immediately on startup)
- Single file simplifies CGI endpoint implementation

#### 5.6.2 CGI Endpoint

**Endpoint**: `GET /cgi-bin/health_status`

**Implementation**: Shell script
```bash
#!/bin/sh
echo "Content-Type: application/json"
echo ""
cat /tmp/software_health.json 2>/dev/null || echo '{"error": "No health data"}'
```

**Response**:
- Success: Returns contents of `/tmp/software_health.json`
- Error: Returns `{"error": "No health data"}` if file missing

**Installation**: Deployed as part of AREDN-Phonebook package to `/www/cgi-bin/`

#### 5.6.3 AREDNmon Dashboard Enhancement

The existing AREDNmon dashboard (`/cgi-bin/arednmon`) is enhanced with a **Software Health Panel** displayed above the phone monitoring table:

**Visual Layout:**
```
┌──────────────────────────────────────────────────────┐
│ AREDNmon - Network Monitor                           │
├──────────────────────────────────────────────────────┤
│                                                       │
│ ┌─────────────────────────────────────────────────┐ │
│ │ Software Health Status                          │ │ ← NEW
│ │ ─────────────────────────────────────────────── │ │
│ │ CPU: 0.5% │ Memory: 0.3 MB │ Uptime: 2h 15m  │ │
│ │ SIP: 0 users, 0 calls                          │ │
│ │ ✓ Memory  ✓ No Crashes  ✓ SIP  ✓ Phonebook    │ │
│ │ ✓ CPU  ✓ All threads responsive                │ │
│ └─────────────────────────────────────────────────┘ │
│                                                       │
│ Phone Monitoring (existing table)                    │
│ ┌─────────────────────────────────────────────────┐ │
│ │ Phone Number │ Name │ Status │ RTT │ Jitter   │ │
│ │ 441530       │ ...  │ ONLINE │ 52  │ 8.3     │ │
│ └─────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

**HTML Structure:**
```html
<!-- Added before phone monitoring table -->
<div class="health-panel">
    <div class="health-header">
        <div class="health-title">Software Health Status</div>
    </div>
    <div class="health-metrics">
        <div class="health-metric">
            <div class="health-metric-label">CPU Usage</div>
            <div class="health-metric-value" id="healthCpu">-</div>
        </div>
        <div class="health-metric">
            <div class="health-metric-label">Memory Usage</div>
            <div class="health-metric-value" id="healthMemory">-</div>
        </div>
        <div class="health-metric">
            <div class="health-metric-label">Uptime</div>
            <div class="health-metric-value" id="healthUptime">-</div>
        </div>
        <div class="health-metric">
            <div class="health-metric-label">SIP Service</div>
            <div class="health-metric-value" id="healthSipUsers">-</div>
        </div>
    </div>
    <div class="health-checks" id="healthChecks">
        <!-- Health checks populated by JavaScript -->
    </div>
    <div class="thread-status" id="threadStatus">
        <!-- Thread status populated by JavaScript -->
    </div>
</div>
```

**JavaScript Integration:**
```javascript
// Load health status from CGI endpoint
async function loadHealthStatus() {
    try {
        const response = await fetch('/cgi-bin/health_status');
        const data = await response.json();

        if (data.error) {
            return;
        }

        // Update metrics
        document.getElementById('healthCpu').textContent =
            (data.cpu_pct || 0).toFixed(1) + '%';
        document.getElementById('healthMemory').textContent =
            (data.mem_mb || 0).toFixed(1) + ' MB';

        // Format uptime
        const uptimeSeconds = data.uptime_seconds || 0;
        const hours = Math.floor(uptimeSeconds / 3600);
        const minutes = Math.floor((uptimeSeconds % 3600) / 60);
        document.getElementById('healthUptime').textContent =
            hours + 'h ' + minutes + 'm';

        // SIP service metrics
        const users = data.sip_service?.registered_users || 0;
        const calls = data.sip_service?.active_calls || 0;
        document.getElementById('healthSipUsers').textContent =
            users + ' users, ' + calls + ' calls';

        // Update health checks
        const checks = data.checks || {};
        const checksHtml = [
            { label: 'Memory', pass: checks.memory_stable },
            { label: 'No Crashes', pass: checks.no_recent_crashes },
            { label: 'SIP Service', pass: checks.sip_service_ok },
            { label: 'Phonebook', pass: checks.phonebook_current },
            { label: 'CPU', pass: checks.cpu_normal }
        ].map(check =>
            '<span class="health-check ' + (check.pass ? 'pass' : 'fail') + '">' +
            check.label + ': ' + (check.pass ? '✓' : '✗') + '</span>'
        ).join('');
        document.getElementById('healthChecks').innerHTML = checksHtml;

        // Update thread status
        const threads = data.threads || {};
        const allResponsive = threads.all_responsive;
        let threadHtml = '';

        if (allResponsive) {
            threadHtml = '<span class="thread-status responsive">All threads responsive ✓</span>';
        } else {
            // Show individual unresponsive threads
            const threadNames = ['phonebook_fetcher', 'status_updater', 'passive_safety',
                               'uac_bulk_tester', 'health_reporter'];
            threadHtml = threadNames
                .filter(name => threads[name] && !threads[name].responsive)
                .map(name => '<span class="thread-status unresponsive">' + name + ' ✗</span>')
                .join('');
        }
        document.getElementById('threadStatus').innerHTML = threadHtml;

    } catch (error) {
        console.error('Failed to load health status:', error);
    }
}
```

**CSS Styling:**
```css
.health-panel {
    background-color: #ffffff;
    border-radius: 8px;
    padding: 15px;
    margin-bottom: 20px;
    box-shadow: 0 2px 4px rgba(0,0,0,0.1);
}

.health-metrics {
    display: flex;
    gap: 20px;
    margin: 10px 0;
}

.metric {
    display: flex;
    gap: 5px;
}

.health-excellent { color: #4CAF50; font-weight: bold; }
.health-good { color: #8BC34A; font-weight: bold; }
.health-degraded { color: #FF9800; font-weight: bold; }
.health-critical { color: #f44336; font-weight: bold; }

.thread-indicator {
    display: inline-block;
    margin: 0 5px;
    padding: 2px 8px;
    border-radius: 4px;
}

.thread-indicator.responsive {
    background-color: #4CAF50;
    color: white;
}

.thread-indicator.unresponsive {
    background-color: #f44336;
    color: white;
    animation: blink 1s infinite;
}

@keyframes blink {
    0%, 50% { opacity: 1; }
    51%, 100% { opacity: 0.3; }
}
```

**Update frequency**: JavaScript fetches health status every 30 seconds (matches existing AREDNmon refresh)

#### 5.6.4 Crash Reporting Integration

When a crash occurs and the process restarts, crash information is also made available locally:

**Crash state file**: `/tmp/last_crash.json`

**Format** (crash_report message schema):
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
  "uptime_at_crash": 3600,
  "memory_at_crash_mb": 8.2,
  "crash_count_24h": 2
}
```

**Dashboard display**: If crash file exists and is recent (<24h), AREDNmon shows warning banner:
```html
<div class="crash-alert">
  ⚠ Service crashed 2 times in last 24h (last: 3 minutes ago)
  <a href="#" onclick="showCrashDetails()">Details</a>
</div>
```

### 5.7 Remote Collector Integration

#### 5.7.1 HTTP Client Implementation

Health data is transmitted to the remote collector via HTTP POST:

**Endpoint**: Configured via `COLLECTOR_URL` parameter

**Request format**:
```http
POST /ingest HTTP/1.1
Host: pi-collector.local.mesh:5000
Content-Type: application/json
Content-Length: 1234

{JSON health message}
```

**Message types posted**:
1. `agent_health` - Regular health reports (4h baseline + events)
2. `crash_report` - Crash diagnostics (immediate on restart)

**HTTP client features**:
- Timeout: Configurable (default: 10 seconds)
- Retry: Single retry on network failure
- Error handling: Continues on failure (local monitoring unaffected)
- No buffering: Failed POSTs are dropped (next cycle retries)

**Implementation module**: `software_health/http_client.c`

```c
int http_post_json(const char *url, const char *json_data, int timeout_seconds) {
    // Parse URL for host, port, path
    // Create TCP socket
    // Set timeout using setsockopt(SO_SNDTIMEO, SO_RCVTIMEO)
    // Send HTTP POST with JSON payload
    // Read HTTP response status
    // Return 0 on success (200 OK), -1 on failure
}
```

#### 5.7.2 Event-Driven Transmission

**Baseline heartbeat**: Every 4 hours (configurable)
```c
if (now - last_report_time >= HEALTH_REPORT_BASELINE_SECONDS) {
    report_health_status(REASON_SCHEDULED);
}
```

**Event triggers** (immediate transmission):
```c
// CPU spike detected
if (abs(cpu_pct - last_cpu_pct) > HEALTH_CPU_THRESHOLD_PCT) {
    report_health_status(REASON_CPU_SPIKE);
}

// Memory increase detected
if ((mem_mb - last_mem_mb) > HEALTH_MEMORY_THRESHOLD_MB) {
    report_health_status(REASON_MEMORY_INCREASE);
}

// Thread became unresponsive
if (!thread_responsive && was_responsive) {
    report_health_status(REASON_THREAD_HUNG);
}

// Health score dropped significantly
if ((health_score - last_health_score) > HEALTH_SCORE_THRESHOLD) {
    report_health_status(REASON_HEALTH_DEGRADED);
}

// Process restarted (detected on startup)
if (is_first_report) {
    report_health_status(REASON_RESTART);
}
```

**Reporting reasons** (included in JSON):
- `scheduled`: 4-hour baseline heartbeat
- `cpu_spike`: CPU change > 20%
- `memory_increase`: Memory increase > 10 MB
- `thread_hung`: Thread stopped responding
- `restart`: Process restarted
- `health_degraded`: Health score dropped > 15 points
- `crash`: Crash detected (from crash_report message)

#### 5.7.3 Network Efficiency

**Bandwidth usage**:
- Normal operation: 6 reports/day × 5 KB = 30 KB/day
- Problem conditions: +1-5 event reports = 35-55 KB/day
- Crash reports: +2 KB per crash (rare)

**Comparison with fixed intervals**:
- Fixed 10-min intervals: 144 reports/day × 5 KB = 720 KB/day
- Event-driven: 30-55 KB/day
- **Bandwidth savings: 92-96%**

**Mesh impact**: 30 KB/day = 0.003 kbps average (negligible)

### 5.8 Configuration

Health reporting configuration parameters in `/etc/phonebook.conf`:

```ini
# ============================================================================
# HEALTH REPORTING SETTINGS
# ============================================================================

# Local Dashboard - Write health data for AREDNmon display
# Enabled by default for immediate local visibility
HEALTH_LOCAL_REPORTING=1
HEALTH_LOCAL_UPDATE_SECONDS=60

# Remote Collector - POST health data to mesh-wide monitoring
# Enable when Raspberry Pi collector is deployed
COLLECTOR_ENABLED=0
COLLECTOR_URL=http://pi-collector.local.mesh:5000/ingest
COLLECTOR_TIMEOUT_SECONDS=10

# Event-Driven Reporting - Baseline heartbeat interval
HEALTH_REPORT_BASELINE_HOURS=4

# Event Thresholds - Trigger immediate reports
HEALTH_CPU_THRESHOLD_PCT=20
HEALTH_MEMORY_THRESHOLD_MB=10
HEALTH_SCORE_THRESHOLD=15

# Crash Reporting
CRASH_REPORTING_ENABLED=1
CRASH_BACKTRACE_DEPTH=5
```

**Configuration validation**:
- `HEALTH_LOCAL_REPORTING=1`: Always recommended (no overhead)
- `COLLECTOR_ENABLED=0`: Disable if collector not deployed (prevents failed POSTs)
- `COLLECTOR_URL`: Must be reachable mesh address
- Thresholds: Validated at runtime, warnings logged if invalid

### 5.9 File Paths Summary

**Health monitoring files**:

| Path | Storage | Purpose | Update Frequency |
|------|---------|---------|------------------|
| `/tmp/software_health.json` | RAM | Current health status for AREDNmon | 60 seconds |
| `/tmp/last_crash.json` | RAM | Last crash report (if any) | On restart after crash |
| `/tmp/meshmon_crash.bin` | RAM | Crash state persistence (binary) | On crash |

**CGI endpoints**:

| Endpoint | Purpose | Consumer |
|----------|---------|----------|
| `/cgi-bin/health_status` | Serve health JSON | AREDNmon dashboard |
| `/cgi-bin/arednmon` | Network monitoring dashboard | Browser (enhanced with health panel) |

**No flash writes**: All health files stored in `/tmp/` (RAM) to preserve router lifespan

## 6. Configuration

### 6.1 Configuration Loader

**Purpose**: Loads runtime configuration from `/etc/sipserver.conf`.

**Configuration Parameters**:
- `PB_INTERVAL_SECONDS`: Phonebook fetch interval (default: 3600)
- `STATUS_UPDATE_INTERVAL_SECONDS`: Status update interval (default: 600)
- `SIP_HANDLER_NICE_VALUE`: Process priority for SIP handling (default: -5)
- `PHONEBOOK_SERVER`: Server definitions (host,port,path format)

**Key Functions**:
- `load_configuration()`: Parses configuration file
- Handles missing files gracefully (uses defaults)
- Supports multiple phonebook servers (up to `MAX_PB_SERVERS`)
- Validates numeric parameters and provides warnings

### 6.2 File Paths

- **Config File**: `/etc/sipserver.conf`
- **CSV Storage**: `PB_CSV_PATH` (persistent flash)
- **XML Publication**: `PB_XML_PUBLIC_PATH` (web accessible)
- **Hash Storage**: `PB_LAST_GOOD_CSV_HASH_PATH`

### 6.3 Limits and Constants

- **Max Users**: `MAX_REGISTERED_USERS`
- **Max Call Sessions**: `MAX_CALL_SESSIONS`
- **Max Phonebook Servers**: `MAX_PB_SERVERS`
- **String Lengths**: Various `MAX_*_LEN` constants

### 6.4 Threading

- **Main Thread**: SIP message processing
- **Fetcher Thread**: Phonebook management
- **Updater Thread**: Status synchronization
- **Synchronization**: Mutexes and condition variables

## 7. Network Communication

This chapter describes the network protocols used across the system. These protocols are referenced by multiple components.

### 7.1 SIP Protocol

**Referenced by**: [SIP Server](#2-sip-server)

- **Transport**: UDP on port 5060
- **Message Size**: Maximum 2048 bytes (`MAX_SIP_MSG_LEN`)
- **Encoding**: UTF-8 with sanitization
- **Authentication**: None (mesh network trust model)

### 7.2 HTTP Downloads

**Referenced by**: [Phonebook](#3-phonebook)

- **Protocol**: HTTP/1.1 for CSV downloads
- **Method**: GET requests to configured phonebook servers
- **Format**: CSV with specific column structure
- **Timeout**: Configurable per server

### 7.3 DNS Resolution

**Referenced by**: [SIP Server](#2-sip-server), [Monitoring](#4-monitoring)

- **Domain**: `.local.mesh` for AREDN mesh networks
- **Format**: `{user_id}.local.mesh`
- **Protocol**: Standard DNS A record lookups
- **Fallback**: Error response if resolution fails

## 8. System Components

This chapter describes system utilities used across multiple components.

### 8.1 File Utils

**Referenced by**: [Phonebook](#3-phonebook)

**Purpose**: Provides file system operations and utilities.

**Key Functions**:
- `file_utils_ensure_directory_exists()`: Creates directory paths
- `file_utils_publish_file_to_destination()`: Copies files atomically
- File existence checking and validation
- Cross-platform file operations

### 8.2 Log Manager

**Referenced by**: All components

**Purpose**: Centralized logging system with multiple severity levels.

**Log Levels**:
- `LOG_ERROR`: Critical errors requiring attention
- `LOG_WARN`: Warning conditions
- `LOG_INFO`: Informational messages
- `LOG_DEBUG`: Detailed debugging information

**Features**:
- Module-specific logging (MODULE_NAME macro)
- Thread-safe logging operations
- Configurable log levels
- Timestamp and process/thread identification

### 8.3 Security

**Referenced by**: All components

#### 8.3.1 Trust Model

- **No Authentication**: Relies on mesh network physical security
- **DNS Trust**: Assumes DNS infrastructure integrity
- **Local Network**: Designed for closed AREDN mesh networks

#### 8.3.2 Input Validation

- **UTF-8 Sanitization**: Cleans incoming CSV data
- **Buffer Protection**: Fixed-size buffers with bounds checking
- **SIP Parsing**: Robust parsing with malformed message handling
- **XML Escaping**: Prevents XML injection in generated output

#### 8.3.3 Resource Protection

- **Maximum Limits**: Enforced limits on users and sessions
- **File Access**: Controlled file system access patterns
- **Thread Safety**: Mutex protection for shared data structures
- **Memory Management**: Static allocation with bounds checking

---

This functional specification provides a comprehensive overview of the AREDN Phonebook SIP Server implementation based on the current codebase analysis.
