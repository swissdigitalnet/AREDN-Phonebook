// softphone.c - SIP User Agent Client Core Implementation
#include "softphone.h"
#include "../common.h"
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MODULE_NAME "SOFTPHONE"

// Timeout constants (in seconds)
#define SOFTPHONE_CALL_TIMEOUT 30      // Max time for entire call (INVITE to cleanup)
#define SOFTPHONE_RINGING_TIMEOUT 10   // Max time in RINGING state
#define SOFTPHONE_RESPONSE_TIMEOUT 5   // Max time waiting for any response

// Global softphone context
static softphone_context_t g_softphone_ctx = {
    .sockfd = -1,
    .local_port = 0,
    .local_ip = {0},
    .call = {
        .state = SOFTPHONE_STATE_IDLE,
        .call_id = {0},
        .from_tag = {0},
        .to_tag = {0},
        .target_number = {0},
        .cseq = 0,
        .state_timestamp = 0
    }
};

// Initialize UAC module
int softphone_init(const char *local_ip) {
    LOG_DEBUG("[SOFTPHONE_INIT] Starting UAC initialization");
    LOG_DEBUG("[SOFTPHONE_INIT] Local IP parameter: %s", local_ip ? local_ip : "NULL");

    if (!local_ip || strlen(local_ip) == 0) {
        LOG_ERROR("[SOFTPHONE_INIT] Invalid local IP provided to UAC");
        return -1;
    }

    LOG_DEBUG("[SOFTPHONE_INIT] Creating UDP socket for UAC");
    // Create UDP socket
    g_softphone_ctx.sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_softphone_ctx.sockfd < 0) {
        LOG_ERROR("[SOFTPHONE_INIT] Failed to create UAC socket: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("[SOFTPHONE_INIT] Socket created successfully (fd=%d)", g_softphone_ctx.sockfd);

    // Bind to unique port (5070)
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = inet_addr(local_ip);
    bind_addr.sin_port = htons(SOFTPHONE_SIP_PORT);

    LOG_DEBUG("[SOFTPHONE_INIT] Attempting to bind to %s:%d", local_ip, SOFTPHONE_SIP_PORT);
    if (bind(g_softphone_ctx.sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERROR("[SOFTPHONE_INIT] Failed to bind UAC socket to %s:%d: %s", local_ip, SOFTPHONE_SIP_PORT, strerror(errno));
        close(g_softphone_ctx.sockfd);
        g_softphone_ctx.sockfd = -1;
        return -1;
    }

    strncpy(g_softphone_ctx.local_ip, local_ip, sizeof(g_softphone_ctx.local_ip) - 1);
    g_softphone_ctx.local_port = SOFTPHONE_SIP_PORT;
    g_softphone_ctx.call.state = SOFTPHONE_STATE_IDLE;

    LOG_INFO("[SOFTPHONE_INIT] ✓ UAC initialized on %s:%d (Phone: %s)", local_ip, SOFTPHONE_SIP_PORT, SOFTPHONE_PHONE_NUMBER);
    LOG_DEBUG("[SOFTPHONE_INIT] UAC context - sockfd=%d, local_ip=%s, local_port=%d, state=%s",
              g_softphone_ctx.sockfd, g_softphone_ctx.local_ip, g_softphone_ctx.local_port,
              softphone_state_to_string(g_softphone_ctx.call.state));
    return 0;
}

// Shutdown UAC module
void softphone_shutdown(void) {
    LOG_DEBUG("[SOFTPHONE_SHUTDOWN] Starting UAC shutdown");
    if (g_softphone_ctx.sockfd >= 0) {
        LOG_DEBUG("[SOFTPHONE_SHUTDOWN] Closing socket fd=%d", g_softphone_ctx.sockfd);
        close(g_softphone_ctx.sockfd);
        g_softphone_ctx.sockfd = -1;
    }
    LOG_INFO("[SOFTPHONE_SHUTDOWN] ✓ UAC shutdown complete");
}

// Get UAC socket file descriptor
int softphone_get_sockfd(void) {
    return g_softphone_ctx.sockfd;
}

// Get current call state
softphone_call_state_t softphone_get_state(void) {
    return g_softphone_ctx.call.state;
}

// Get call state as string
const char* softphone_state_to_string(softphone_call_state_t state) {
    switch (state) {
        case SOFTPHONE_STATE_IDLE:        return "IDLE";
        case SOFTPHONE_STATE_CALLING:     return "CALLING";
        case SOFTPHONE_STATE_RINGING:     return "RINGING";
        case SOFTPHONE_STATE_ESTABLISHED: return "ESTABLISHED";
        case SOFTPHONE_STATE_TERMINATING: return "TERMINATING";
        case SOFTPHONE_STATE_TERMINATED:  return "TERMINATED";
        default:                    return "UNKNOWN";
    }
}

// Reset UAC to IDLE state
void softphone_reset_state(void) {
    softphone_call_state_t old_state = g_softphone_ctx.call.state;
    g_softphone_ctx.call.state = SOFTPHONE_STATE_IDLE;
    memset(&g_softphone_ctx.call, 0, sizeof(g_softphone_ctx.call));
    g_softphone_ctx.call.state = SOFTPHONE_STATE_IDLE;
    g_softphone_ctx.call.state_timestamp = time(NULL);

    if (old_state != SOFTPHONE_STATE_IDLE) {
        LOG_INFO("[SOFTPHONE_RESET] Reset UAC from %s to IDLE state", softphone_state_to_string(old_state));
    }
}

// Make a call
int softphone_make_call(const char *target_number, const char *server_ip) {
    LOG_INFO("[SOFTPHONE_CALL] Making call to %s via server %s",
             target_number ? target_number : "NULL",
             server_ip ? server_ip : "NULL");

    if (!target_number || !server_ip) {
        LOG_ERROR("[SOFTPHONE_CALL] Invalid parameters to softphone_make_call");
        return -1;
    }

    if (g_softphone_ctx.call.state != SOFTPHONE_STATE_IDLE) {
        LOG_WARN("[SOFTPHONE_CALL] Call already in progress (state: %s), forcing reset", softphone_state_to_string(g_softphone_ctx.call.state));
        softphone_reset_state();
    }

    if (g_softphone_ctx.sockfd < 0) {
        LOG_ERROR("[SOFTPHONE_CALL] UAC not initialized (sockfd=%d)", g_softphone_ctx.sockfd);
        return -1;
    }

    LOG_DEBUG("[SOFTPHONE_CALL] Current state: %s, sockfd: %d",
              softphone_state_to_string(g_softphone_ctx.call.state), g_softphone_ctx.sockfd);

    // Setup server address
    memset(&g_softphone_ctx.call.server_addr, 0, sizeof(g_softphone_ctx.call.server_addr));
    g_softphone_ctx.call.server_addr.sin_family = AF_INET;
    g_softphone_ctx.call.server_addr.sin_addr.s_addr = inet_addr(server_ip);
    g_softphone_ctx.call.server_addr.sin_port = htons(5060);

    LOG_DEBUG("[SOFTPHONE_CALL] Server address set to %s:5060", server_ip);

    // Generate Call-ID, tags, and Via branch
    snprintf(g_softphone_ctx.call.call_id, sizeof(g_softphone_ctx.call.call_id),
             "uac-%ld@%s", time(NULL), g_softphone_ctx.local_ip);
    snprintf(g_softphone_ctx.call.from_tag, sizeof(g_softphone_ctx.call.from_tag),
             "tag-%ld", random());
    snprintf(g_softphone_ctx.call.via_branch, sizeof(g_softphone_ctx.call.via_branch),
             "z9hG4bK%ld", random());
    g_softphone_ctx.call.to_tag[0] = '\0';  // No To tag yet
    strncpy(g_softphone_ctx.call.target_number, target_number,
            sizeof(g_softphone_ctx.call.target_number) - 1);
    g_softphone_ctx.call.cseq = 1;

    LOG_DEBUG("[SOFTPHONE_CALL] Call-ID: %s", g_softphone_ctx.call.call_id);
    LOG_DEBUG("[SOFTPHONE_CALL] From-tag: %s", g_softphone_ctx.call.from_tag);
    LOG_DEBUG("[SOFTPHONE_CALL] Via-branch: %s", g_softphone_ctx.call.via_branch);
    LOG_DEBUG("[SOFTPHONE_CALL] CSeq: %d", g_softphone_ctx.call.cseq);

    // Build INVITE message
    char invite_msg[2048];
    LOG_DEBUG("[SOFTPHONE_CALL] Building INVITE message");
    if (softphone_build_invite(invite_msg, sizeof(invite_msg), &g_softphone_ctx.call,
                         g_softphone_ctx.local_ip, g_softphone_ctx.local_port) < 0) {
        LOG_ERROR("[SOFTPHONE_CALL] Failed to build INVITE message");
        return -1;
    }

    LOG_DEBUG("[SOFTPHONE_CALL] INVITE message built (%zu bytes)", strlen(invite_msg));

    // Send INVITE
    LOG_DEBUG("[SOFTPHONE_CALL] Sending INVITE to %s:5060", server_ip);
    ssize_t sent = sendto(g_softphone_ctx.sockfd, invite_msg, strlen(invite_msg), 0,
                          (struct sockaddr*)&g_softphone_ctx.call.server_addr,
                          sizeof(g_softphone_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("[SOFTPHONE_CALL] Failed to send INVITE: %s", strerror(errno));
        return -1;
    }

    LOG_DEBUG("[SOFTPHONE_CALL] INVITE sent successfully (%zd bytes)", sent);

    g_softphone_ctx.call.state = SOFTPHONE_STATE_CALLING;
    g_softphone_ctx.call.state_timestamp = time(NULL);
    LOG_INFO("[SOFTPHONE_CALL] ✓ INVITE sent to %s for %s (Call-ID: %s, state: %s)",
             server_ip, target_number, g_softphone_ctx.call.call_id,
             softphone_state_to_string(g_softphone_ctx.call.state));
    return 0;
}

// Send ACK
static int softphone_send_ack(void) {
    LOG_DEBUG("[SOFTPHONE_ACK] Preparing to send ACK");
    char ack_msg[1024];

    LOG_DEBUG("[SOFTPHONE_ACK] Building ACK message");
    if (softphone_build_ack(ack_msg, sizeof(ack_msg), &g_softphone_ctx.call,
                      g_softphone_ctx.local_ip, g_softphone_ctx.local_port) < 0) {
        LOG_ERROR("[SOFTPHONE_ACK] Failed to build ACK message");
        return -1;
    }

    LOG_DEBUG("[SOFTPHONE_ACK] Sending ACK (%zu bytes) to server", strlen(ack_msg));
    ssize_t sent = sendto(g_softphone_ctx.sockfd, ack_msg, strlen(ack_msg), 0,
                          (struct sockaddr*)&g_softphone_ctx.call.server_addr,
                          sizeof(g_softphone_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("[SOFTPHONE_ACK] Failed to send ACK: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("[SOFTPHONE_ACK] ✓ ACK sent successfully (%zd bytes)", sent);
    return 0;
}

// Cancel a ringing call
int softphone_cancel_call(void) {
    LOG_INFO("[SOFTPHONE_CANCEL] Canceling call (current state: %s)",
             softphone_state_to_string(g_softphone_ctx.call.state));

    if (g_softphone_ctx.call.state != SOFTPHONE_STATE_CALLING && g_softphone_ctx.call.state != SOFTPHONE_STATE_RINGING) {
        LOG_WARN("[SOFTPHONE_CANCEL] No ringing call to cancel (state: %s)",
                 softphone_state_to_string(g_softphone_ctx.call.state));
        return -1;
    }

    // CANCEL must use same CSeq and Via branch as INVITE (RFC 3261)
    char cancel_msg[1024];
    int written = snprintf(cancel_msg, sizeof(cancel_msg),
        "CANCEL sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
        "From: <sip:%s@%s:%d>;tag=%s\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d CANCEL\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        g_softphone_ctx.call.target_number,
        g_softphone_ctx.local_ip, g_softphone_ctx.local_port, g_softphone_ctx.call.via_branch,
        SOFTPHONE_PHONE_NUMBER, g_softphone_ctx.local_ip, g_softphone_ctx.local_port, g_softphone_ctx.call.from_tag,
        g_softphone_ctx.call.target_number,
        g_softphone_ctx.call.call_id,
        g_softphone_ctx.call.cseq);

    if (written >= sizeof(cancel_msg)) {
        LOG_ERROR("[SOFTPHONE_CANCEL] CANCEL message truncated");
        return -1;
    }

    LOG_DEBUG("[SOFTPHONE_CANCEL] Sending CANCEL (%d bytes) to server", written);
    ssize_t sent = sendto(g_softphone_ctx.sockfd, cancel_msg, written, 0,
                          (struct sockaddr*)&g_softphone_ctx.call.server_addr,
                          sizeof(g_softphone_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("[SOFTPHONE_CANCEL] Failed to send CANCEL: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("[SOFTPHONE_CANCEL] ✓ CANCEL sent successfully (%zd bytes)", sent);
    g_softphone_ctx.call.state = SOFTPHONE_STATE_TERMINATING;
    g_softphone_ctx.call.state_timestamp = time(NULL);
    return 0;
}

// Hang up current call
int softphone_hang_up(void) {
    LOG_INFO("[SOFTPHONE_BYE] Initiating hang up (current state: %s)",
             softphone_state_to_string(g_softphone_ctx.call.state));

    if (g_softphone_ctx.call.state != SOFTPHONE_STATE_ESTABLISHED) {
        LOG_ERROR("[SOFTPHONE_BYE] No established call to hang up (state: %s)",
                  softphone_state_to_string(g_softphone_ctx.call.state));
        return -1;
    }

    char bye_msg[1024];
    g_softphone_ctx.call.cseq++;  // Increment CSeq for BYE
    LOG_DEBUG("[SOFTPHONE_BYE] CSeq incremented to %d", g_softphone_ctx.call.cseq);

    LOG_DEBUG("[SOFTPHONE_BYE] Building BYE message");
    if (softphone_build_bye(bye_msg, sizeof(bye_msg), &g_softphone_ctx.call,
                      g_softphone_ctx.local_ip, g_softphone_ctx.local_port) < 0) {
        LOG_ERROR("[SOFTPHONE_BYE] Failed to build BYE message");
        return -1;
    }

    LOG_DEBUG("[SOFTPHONE_BYE] Sending BYE (%zu bytes) to server", strlen(bye_msg));
    ssize_t sent = sendto(g_softphone_ctx.sockfd, bye_msg, strlen(bye_msg), 0,
                          (struct sockaddr*)&g_softphone_ctx.call.server_addr,
                          sizeof(g_softphone_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("[SOFTPHONE_BYE] Failed to send BYE: %s", strerror(errno));
        return -1;
    }

    g_softphone_ctx.call.state = SOFTPHONE_STATE_TERMINATING;
    g_softphone_ctx.call.state_timestamp = time(NULL);
    LOG_INFO("[SOFTPHONE_BYE] ✓ BYE sent successfully (%zd bytes, state: %s)",
             sent, softphone_state_to_string(g_softphone_ctx.call.state));
    return 0;
}

// Process incoming SIP response
int softphone_process_response(const char *response, size_t response_len) {
    LOG_DEBUG("[SOFTPHONE_RESPONSE] Received response (%zu bytes)", response_len);

    if (!response || response_len == 0) {
        LOG_ERROR("[SOFTPHONE_RESPONSE] Invalid response parameters");
        return -1;
    }

    // Parse status line
    int status_code = 0;
    if (sscanf(response, "SIP/2.0 %d", &status_code) != 1) {
        LOG_ERROR("[SOFTPHONE_RESPONSE] Failed to parse SIP response status line");
        int preview_len = (response_len < 200) ? (int)response_len : 200;
        LOG_DEBUG("[SOFTPHONE_RESPONSE] Response: %.*s", preview_len, response);
        return -1;
    }

    LOG_INFO("[SOFTPHONE_RESPONSE] ← Received %d response (state: %s)", status_code,
             softphone_state_to_string(g_softphone_ctx.call.state));
    LOG_DEBUG("[SOFTPHONE_RESPONSE] First line: %.80s", response);

    switch (status_code) {
        case 100:  // Trying
            if (g_softphone_ctx.call.state == SOFTPHONE_STATE_CALLING) {
                LOG_INFO("[SOFTPHONE_RESPONSE] ✓ Call setup in progress (100 Trying)");
                LOG_DEBUG("[SOFTPHONE_RESPONSE] State remains: %s", softphone_state_to_string(g_softphone_ctx.call.state));
            } else {
                LOG_WARN("[SOFTPHONE_RESPONSE] Unexpected 100 in state %s", softphone_state_to_string(g_softphone_ctx.call.state));
            }
            break;

        case 180:  // Ringing
            if (g_softphone_ctx.call.state == SOFTPHONE_STATE_CALLING) {
                g_softphone_ctx.call.state = SOFTPHONE_STATE_RINGING;
                g_softphone_ctx.call.state_timestamp = time(NULL);
                LOG_INFO("[SOFTPHONE_RESPONSE] ✓ Phone is ringing (180 Ringing, state: %s)",
                         softphone_state_to_string(g_softphone_ctx.call.state));
            } else {
                LOG_WARN("[SOFTPHONE_RESPONSE] Unexpected 180 in state %s", softphone_state_to_string(g_softphone_ctx.call.state));
            }
            break;

        case 200:  // OK
            if (g_softphone_ctx.call.state == SOFTPHONE_STATE_RINGING ||
                g_softphone_ctx.call.state == SOFTPHONE_STATE_CALLING) {
                LOG_DEBUG("[SOFTPHONE_RESPONSE] Processing 200 OK for INVITE");

                // Extract To tag from 200 OK
                LOG_DEBUG("[SOFTPHONE_RESPONSE] Extracting To tag from response");
                if (softphone_extract_to_tag(response, g_softphone_ctx.call.to_tag,
                                       sizeof(g_softphone_ctx.call.to_tag)) < 0) {
                    LOG_WARN("[SOFTPHONE_RESPONSE] Failed to extract To tag from 200 OK");
                } else {
                    LOG_DEBUG("[SOFTPHONE_RESPONSE] To tag extracted: %s", g_softphone_ctx.call.to_tag);
                }

                // Send ACK
                LOG_DEBUG("[SOFTPHONE_RESPONSE] Sending ACK for 200 OK");
                if (softphone_send_ack() < 0) {
                    LOG_ERROR("[SOFTPHONE_RESPONSE] Failed to send ACK");
                    return -1;
                }

                g_softphone_ctx.call.state = SOFTPHONE_STATE_ESTABLISHED;
                g_softphone_ctx.call.state_timestamp = time(NULL);
                LOG_INFO("[SOFTPHONE_RESPONSE] ✓ Call established (200 OK received, ACK sent, state: %s)",
                         softphone_state_to_string(g_softphone_ctx.call.state));

            } else if (g_softphone_ctx.call.state == SOFTPHONE_STATE_TERMINATING) {
                LOG_DEBUG("[SOFTPHONE_RESPONSE] Processing 200 OK for BYE");
                g_softphone_ctx.call.state = SOFTPHONE_STATE_TERMINATED;
                g_softphone_ctx.call.state_timestamp = time(NULL);
                LOG_INFO("[SOFTPHONE_RESPONSE] ✓ Call terminated successfully (200 OK for BYE)");

                // Reset call context
                LOG_DEBUG("[SOFTPHONE_RESPONSE] Resetting call context to IDLE");
                g_softphone_ctx.call.state = SOFTPHONE_STATE_IDLE;
                g_softphone_ctx.call.state_timestamp = time(NULL);
                memset(&g_softphone_ctx.call, 0, sizeof(g_softphone_ctx.call));
            } else {
                LOG_WARN("[SOFTPHONE_RESPONSE] Unexpected 200 OK in state %s", softphone_state_to_string(g_softphone_ctx.call.state));
            }
            break;

        case 486:  // Busy Here
            LOG_WARN("[SOFTPHONE_RESPONSE] Target phone busy (486 Busy Here)");
            // Extract To tag and send ACK to complete transaction
            if (softphone_extract_to_tag(response, g_softphone_ctx.call.to_tag,
                                   sizeof(g_softphone_ctx.call.to_tag)) >= 0) {
                softphone_send_ack();
            }
            LOG_DEBUG("[SOFTPHONE_RESPONSE] Transitioning to IDLE state");
            g_softphone_ctx.call.state = SOFTPHONE_STATE_IDLE;
            g_softphone_ctx.call.state_timestamp = time(NULL);
            break;

        case 487:  // Request Terminated
            LOG_WARN("[SOFTPHONE_RESPONSE] Request terminated (487)");
            // Extract To tag and send ACK to complete transaction
            if (softphone_extract_to_tag(response, g_softphone_ctx.call.to_tag,
                                   sizeof(g_softphone_ctx.call.to_tag)) >= 0) {
                softphone_send_ack();
            }
            LOG_DEBUG("[SOFTPHONE_RESPONSE] Transitioning to IDLE state");
            g_softphone_ctx.call.state = SOFTPHONE_STATE_IDLE;
            g_softphone_ctx.call.state_timestamp = time(NULL);
            break;

        default:
            // All error responses to INVITE require ACK (RFC 3261)
            LOG_WARN("[SOFTPHONE_RESPONSE] Error response code: %d", status_code);
            // Extract To tag and send ACK to complete transaction
            if (softphone_extract_to_tag(response, g_softphone_ctx.call.to_tag,
                                   sizeof(g_softphone_ctx.call.to_tag)) >= 0) {
                LOG_DEBUG("[SOFTPHONE_RESPONSE] Sending ACK for error response");
                softphone_send_ack();
            }
            LOG_DEBUG("[SOFTPHONE_RESPONSE] Resetting UAC to IDLE state after error response");
            g_softphone_ctx.call.state = SOFTPHONE_STATE_IDLE;
            g_softphone_ctx.call.state_timestamp = time(NULL);
            break;
    }

    return 0;
}

// Check for call timeout and force reset if needed
int softphone_check_timeout(void) {
    // No timeout check if UAC is idle
    if (g_softphone_ctx.call.state == SOFTPHONE_STATE_IDLE) {
        return 0;
    }

    time_t now = time(NULL);
    time_t elapsed = now - g_softphone_ctx.call.state_timestamp;

    // Check state-specific timeouts
    int should_reset = 0;
    const char *reason = NULL;

    switch (g_softphone_ctx.call.state) {
        case SOFTPHONE_STATE_CALLING:
            if (elapsed > SOFTPHONE_RESPONSE_TIMEOUT) {
                should_reset = 1;
                reason = "no response to INVITE";
            }
            break;

        case SOFTPHONE_STATE_RINGING:
            if (elapsed > SOFTPHONE_RINGING_TIMEOUT) {
                should_reset = 1;
                reason = "phone ringing too long";
            }
            break;

        case SOFTPHONE_STATE_ESTABLISHED:
            if (elapsed > SOFTPHONE_CALL_TIMEOUT) {
                should_reset = 1;
                reason = "call established but not terminated";
            }
            break;

        case SOFTPHONE_STATE_TERMINATING:
            if (elapsed > SOFTPHONE_RESPONSE_TIMEOUT) {
                should_reset = 1;
                reason = "no response to BYE/CANCEL";
            }
            break;

        case SOFTPHONE_STATE_TERMINATED:
            // Should already be reset, but just in case
            should_reset = 1;
            reason = "stuck in TERMINATED state";
            break;

        default:
            break;
    }

    if (should_reset) {
        LOG_WARN("[SOFTPHONE_TIMEOUT] Call timeout after %ld seconds in state %s (%s)",
                 elapsed, softphone_state_to_string(g_softphone_ctx.call.state), reason);
        softphone_reset_state();
        return 1;
    }

    return 0;
}
