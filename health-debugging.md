# Health Monitoring System - Debugging Notes

## Investigation Date
October 16, 2025

## Objective
Determine if the health monitoring system is starting on hap-2 and check for any crashes or failures.

## Debugging Approach

### 1. Initial Log Review
- Connected to hap-2 via SSH
- Read system logs using `logread`
- Observed normal operation: UAC ping tests, phonebook updates running smoothly
- No obvious errors or crashes in recent logs

### 2. Search for Health System Messages
Searched logs for health-related keywords:
- `grep -i 'health'` - No results
- `grep -i 'monitoring'` - No results
- `grep -i 'health reporter'` - No results
- `grep -E 'init|starting|initialized'` - Only found UAC_PING messages, no health system startup

### 3. Service Restart Test
- Restarted AREDN-Phonebook service: `/etc/init.d/AREDN-Phonebook restart`
- Captured startup logs
- Result: Still NO health monitoring messages in logs

### 4. Binary Analysis
Checked if health monitoring code exists in the deployed binary:
```bash
strings /usr/bin/AREDN-Phonebook | grep -i 'health monitoring'
strings /usr/bin/AREDN-Phonebook | grep -i 'health reporter'
```

**Found strings**:
- "Initializing software health monitoring..."
- "Health monitoring system initialized"
- "Creating health reporter thread..."
- "Health reporter thread launched."
- Multiple health-related error messages

**Binary timestamp**: March 22, 2024 (7+ months old!)

### 5. Code Review
Examined current codebase (main.c:175-298):
- Health system initialization is enabled at lines 175-181
- Health reporter thread creation at lines 292-298
- Code should log "Initializing software health monitoring..." on startup

### 6. Version Check
- **Installed package**: AREDN-Phonebook v2.2.3-1
- **Current git tag**: v2.2.3 (commit 342e550: "feat: enable health monitoring system in main.c")
- **Binary date**: March 22, 2024
- **Tag date**: Much more recent (based on git log)

### 7. Git History Analysis
```
* 342e550 (HEAD -> main, tag: v2.2.3) feat: enable health monitoring system in main.c
* 56240e4 (tag: v2.2.2) feat: instrument json_formatter.c for crash debugging
* 9d16ddc (tag: v2.2.1) fix: disable health monitoring JSON/HTTP/crash handlers to prevent segfault
* 8d98931 (tag: v2.2.0) fix: remove execinfo.h dependency for musl libc compatibility
```

The health monitoring feature was disabled in v2.2.1, then re-enabled in v2.2.3.

## Findings

### Binary/Code Mismatch
The installed binary on hap-2 does NOT match the current v2.2.3 codebase:
- Binary is from March 2024 (pre-dating recent health monitoring work)
- Version tag shows v2.2.3-1 but binary is stale
- Health monitoring code exists in binary but from an older/incomplete version

### Health System Status
**The health monitoring system is NOT starting** because:
1. The deployed binary is outdated
2. The old binary may have health code stubs but not the current implementation
3. No initialization logs appear when service starts

### Crash Analysis
- **No crashes detected** in logs
- No segfaults, signals, or core dumps
- Service running normally with current (old) binary
- No SIGUSR signals or abnormal terminations

## Conclusions

1. **Health monitoring is NOT enabled** on the deployed hap-2 system
2. The binary needs to be rebuilt from current codebase (commit 342e550)
3. The package version numbering may be misleading - the binary timestamp is the source of truth
4. **No stability issues** - service is running reliably with the old code

## Next Steps

To enable health monitoring on hap-2:
1. Build new .ipk package from current v2.2.3 codebase via GitHub Actions
2. Download artifact from GitHub Actions workflow
3. Transfer to hap-2 and install via opkg
4. Restart service and verify health monitoring starts correctly
5. Monitor logs for:
   - "Initializing software health monitoring..."
   - "Health monitoring system initialized"
   - "Health reporter thread launched."

## Testing Commands

### Verify health system is running (after deployment):
```bash
ssh hap-2 "logread | grep -i health | tail -20"
ssh hap-2 "logread | grep 'Health monitoring system initialized'"
```

### Check for health status files:
```bash
ssh hap-2 "ls -la /tmp/aredn_health_status.json"
ssh hap-2 "cat /tmp/aredn_health_status.json"
```

### Monitor health reporter thread:
```bash
ssh hap-2 "logread -f | grep -i health"
```

## UPDATE: v2.2.4 Deployment Results (Oct 16, 2025 21:31 CEST / 19:31 UTC)

### Deployment Success
- ✅ Built and deployed v2.2.4 with BLUEBIRD marker
- ✅ Health monitoring system initialized successfully
- ✅ All 6 threads running (main + 5 worker threads)
- ✅ No crashes or segfaults detected
- ✅ Process stable for 6+ minutes

### Issue Identified
❌ **Health JSON formatter appears to be crashing silently**

Evidence:
1. Config shows `HEALTH_LOCAL_REPORTING=1` and `HEALTH_LOCAL_UPDATE_SECONDS=60`
2. Health reporter thread (TID 26541) is alive and running
3. After 6+ minutes (6+ cycles), NO logs from health_write_status_file()
4. Expected logs missing:
   - `LOG_ERROR("Failed to format health JSON")`
   - `LOG_ERROR("Failed to open health status file")`
   - `LOG_DEBUG("Wrote health status to /tmp/software_health.json")`
5. No `/tmp/software_health.json` file created

### Root Cause Analysis
The `health_format_agent_health_json()` function is likely causing a **silent failure** (possibly seg fault within try-catch or similar) when called at `software_health.c:255`. This prevents ANY subsequent logging from occurring, including error logs.

This matches the original concern - the JSON formatter was crashing in previous testing, and appears to still be problematic even with recent fixes.

### Next Steps
1. Add instrumentation BEFORE the health_format_agent_health_json() call
2. Test JSON formatter in isolation
3. Check for buffer overflows, null pointers, or format string issues
4. Consider disabling JSON formatter temporarily to confirm diagnosis

## References

- Source file: `/home/AREDN-Phonebook/Phonebook/src/main.c` (lines 175-298)
- Health module: `/home/AREDN-Phonebook/Phonebook/src/software_health/software_health.c`
- Health reporter: `/home/AREDN-Phonebook/Phonebook/src/software_health/health_reporter.c`
- Current commit: 7ead892 (v2.2.4 with BLUEBIRD)
- Deployed binary: `/usr/bin/AREDN-Phonebook` on hap-2 (Oct 16 21:23 CEST / 19:23 UTC)
- Service started: Oct 16 21:25:34 CEST / 19:25:34 UTC
- Test time: Oct 16 21:31 CEST / 19:31 UTC (6 minutes runtime)
