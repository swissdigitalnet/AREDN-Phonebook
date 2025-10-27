// softphone.h - SIP User Agent Client Library
#ifndef SOFTPHONE_H
#define SOFTPHONE_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

// Softphone Configuration
#define SOFTPHONE_SIP_PORT 5070
#define SOFTPHONE_PHONE_NUMBER "999900"

// Call States
typedef enum {
    SOFTPHONE_STATE_IDLE,           // No call
    SOFTPHONE_STATE_CALLING,        // INVITE sent, waiting for response
    SOFTPHONE_STATE_RINGING,        // 180 Ringing received
    SOFTPHONE_STATE_ESTABLISHED,    // 200 OK received, ACK sent
    SOFTPHONE_STATE_TERMINATING,    // BYE sent, waiting for 200 OK
    SOFTPHONE_STATE_TERMINATED      // Call ended
} softphone_call_state_t;

// Call Context (single call)
typedef struct {
    softphone_call_state_t state;
    char call_id[256];        // Unique Call-ID
    char from_tag[64];        // From tag
    char to_tag[64];          // To tag (from 200 OK)
    char via_branch[64];      // Via branch (must be same for INVITE and CANCEL)
    char target_number[32];   // Called number (e.g., "441530")
    int cseq;                 // CSeq counter
    struct sockaddr_in server_addr;  // SIP server address (5060)
    time_t state_timestamp;   // When current state was entered (for timeout detection)
} softphone_call_t;

// Softphone Context
typedef struct {
    int sockfd;               // Softphone socket (bound to port 5070)
    int local_port;           // Softphone port (5070)
    char local_ip[64];        // Softphone IP address
    softphone_call_t call;    // Single call
} softphone_context_t;

// Public API

/**
 * Initialize softphone module
 * @param local_ip IP address to bind softphone socket to
 * @return 0 on success, -1 on failure
 */
int softphone_init(const char *local_ip);

/**
 * Shutdown softphone module
 */
void softphone_shutdown(void);

/**
 * Get softphone socket file descriptor (for select loop)
 * @return Softphone socket fd
 */
int softphone_get_sockfd(void);

/**
 * Make a call to a target phone number
 * @param target_number Phone number to call (e.g., "441530")
 * @param server_ip SIP server IP address
 * @return 0 on success, -1 on failure
 */
int softphone_make_call(const char *target_number, const char *server_ip);

/**
 * Cancel a ringing call (sends CANCEL)
 * @return 0 on success, -1 on failure
 */
int softphone_cancel_call(void);

/**
 * Hang up current call (sends BYE)
 * @return 0 on success, -1 on failure
 */
int softphone_hang_up(void);

/**
 * Process incoming SIP response
 * @param response SIP response message
 * @param response_len Length of response
 * @return 0 on success, -1 on failure
 */
int softphone_process_response(const char *response, size_t response_len);

/**
 * Get current call state
 * @return Current softphone call state
 */
softphone_call_state_t softphone_get_state(void);

/**
 * Get call state as string
 * @param state Call state enum
 * @return String representation of state
 */
const char* softphone_state_to_string(softphone_call_state_t state);

/**
 * Reset softphone to IDLE state (clears any stuck call state)
 * Useful for error recovery and testing
 */
void softphone_reset_state(void);

/**
 * Check if softphone call has timed out and force reset if needed
 * Should be called periodically from main loop
 * @return 1 if timeout occurred and state was reset, 0 otherwise
 */
int softphone_check_timeout(void);

// Internal helper functions (used by SIP builder/parser)
int softphone_build_invite(char *buffer, size_t buffer_size, softphone_call_t *call,
                           const char *local_ip, int local_port);
int softphone_build_ack(char *buffer, size_t buffer_size, softphone_call_t *call,
                        const char *local_ip, int local_port);
int softphone_build_bye(char *buffer, size_t buffer_size, softphone_call_t *call,
                        const char *local_ip, int local_port);
int softphone_extract_to_tag(const char *response, char *to_tag_out, size_t out_size);

#endif // SOFTPHONE_H
