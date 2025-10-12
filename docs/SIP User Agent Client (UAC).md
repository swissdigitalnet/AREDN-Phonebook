# SIP User Agent Client (UAC)

## Current Scope
The in-tree UAC is a Phase 1 signalling client that runs alongside the SIP server to validate registration health. It binds to UDP port 5070 with the reserved caller ID `999900`, generates SIP INVITE/ACK/BYE/CANCEL traffic, and measures control-plane responsiveness. Media (RTP/RTCP) simulation and multi-call fan-out are deferred; one call context is maintained at a time.

## Module Layout
- `uac.c` – owns the singleton `uac_context_t`, socket lifecycle, state machine, and timeout enforcement.
- `uac_sip_builder.c` / `uac_sip_parser.c` – craft Yealink-compatible INVITE/ACK/BYE payloads and extract `To` tags for dialog tracking.
- `uac_ping.c` – reuses the UAC socket to emit SIP OPTIONS probes and compute RTT/jitter statistics.
- `uac_bulk_tester.c` – supervisory thread that schedules OPTIONS or INVITE checks across registered users when enabled.

Main loop integration lives in `src/main.c`: once the server IP is detected, `uac_init()` is called, the UAC socket is added to the `select()` set, and `uac_process_response()` runs for any signalling replies.

## Call Lifecycle
Calling is initiated through `uac_make_call(target, g_server_ip)` after confirming the state is `IDLE`. The builder populates a unique Call-ID, From tag, and Via branch, then sends the INVITE to `target@localnode.local.mesh:5060`. Response handling covers:
- `100/180` progress responses: state transitions to `CALLING` and `RINGING`.
- `200 OK`: ACK is generated, `to_tag` captured, and the call moves to `ESTABLISHED`. `uac_hang_up()` issues a BYE that is acknowledged by a second 200 OK.
- Non-2xx results: ACK is still sent per RFC 3261 and the context resets to `IDLE`.
- `uac_cancel_call()` is available while ringing and reuses the original Via branch and CSeq as required.

`uac_check_timeout()` guards against stuck states (5s response timeout, 10s ringing ceiling, 30s max established duration) and forces a reset if a transition stalls.

## Bulk Testing Workflow
`uac_bulk_tester_thread()` wakes on `g_uac_test_interval_seconds` (default 60s). For each `RegisteredUser`:
1. DNS is resolved to `<user>.local.mesh` to avoid hammering offline nodes.
2. ICMP ping test (`g_uac_ping_count`, default 5) measures network-layer connectivity.
3. SIP OPTIONS test (`g_uac_options_count`, default 5) measures application-layer connectivity.
4. If both tests fail and `g_uac_call_test_enabled` is true, an INVITE is attempted. As soon as `RINGING` or `ESTABLISHED` is observed, the tester cancels or hangs up to minimize audible impact.

Metrics (online/offline counts, average RTT) are summarized at the end of each cycle, and the passive safety watchdog heartbeat is updated while the loop runs.

## Manual Triggers
- Web CGI `/cgi-bin/uac_test?target=<number>` writes the requested number to `/tmp/uac_test_target` and sends `SIGUSR2`. `main.c` consumes the file on the next select timeout and calls `uac_make_call()`. Progress is visible in `logread | grep UAC`.
- A companion `/cgi-bin/uac_ping` script exists, but the daemon does not yet read `/tmp/uac_ping_request`, so OPTIONS pings should be exercised through the bulk tester or future wiring.
- Developers can also call `uac_make_call()` directly from bespoke diagnostics after ensuring `uac_init()` returned success.

## Configuration Knobs
All settings live in `Phonebook/files/etc/phonebook.conf` and load via `config_loader`:
- `UAC_TEST_INTERVAL_SECONDS` – wake interval for the bulk tester (0 disables the thread).
- `UAC_CALL_TEST_ENABLED` – allow INVITE validation when ping and options tests fail.
- `UAC_PING_COUNT` – number of ICMP ping requests per phone (network layer, default: 5, tests ALL phones).
- `UAC_OPTIONS_COUNT` – number of SIP OPTIONS requests per phone (application layer, default: 5, tests ALL phones).

Changes require a service restart or config reload.

## Known Limitations & Next Steps
- Only one active call context exists; concurrent load will require expanding the state machine and locking strategy.
- RTP generation, port pooling, and media quality metrics are not implemented.
- UAC currently assumes the target domain `localnode.local.mesh`; multi-domain support will need builder updates.
- The OPTIONS helper script needs a daemon-side handler for `/tmp/uac_ping_request`.

Despite these gaps, the module delivers end-to-end signalling coverage with automated health checks, making it suitable for monitoring mesh phone availability in the current release.
