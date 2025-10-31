#include "sip_core.h"
#include "../common.h" // This now includes all necessary headers and types
#include "../user_manager/user_manager.h" // For RegisteredUser, find_registered_user, etc.
#include "../call-sessions/call_sessions.h" // For CallSession, create_call_session, find_call_session_by_callid, etc.

#define MODULE_NAME "SIP"

// Note: Global extern declarations are now in common.h


int extract_sip_header(const char *msg, const char *hdr, char *buf, size_t len) {
    char *start = strstr(msg, hdr);
    if (!start) { buf[0] = '\0'; return 0; }
    start += strlen(hdr);
    while (*start == ' ' || *start == '\t') start++;
    char *end = strstr(start, "\r\n");
    if (!end) end = strstr(start, "\n");
    size_t l = end ? (size_t)(end - start) : strlen(start);
    if (l >= len) l = len - 1;
    memcpy(buf, start, l);
    buf[l] = '\0';
    return 1;
}

int parse_user_id_from_uri(const char *uri, char *buf, size_t len) {
    const char *start = uri;
    const char *lt = strchr(start, '<');
    if (lt) start = lt + 1;
    while (*start == ' ' || *start == '\t' || *start == '"') start++;
    if (strncasecmp(start, "sip:", 4) == 0) start += 4;
    const char *at = strchr(start, '@');
    const char *end_of_id = at ? at : start + strcspn(start, ":;");
    size_t ulen = end_of_id - start;
    if (ulen <= 0) { buf[0] = '\0'; return 0; }
    if (ulen >= len) ulen = len - 1;
    memcpy(buf, start, ulen);
    buf[ulen] = '\0';
    return 1;
}

int extract_uri_from_header(const char *header_value, char *buf, size_t len) {
    const char *start = strchr(header_value, '<');
    if (!start) start = header_value; else start++;
    const char *end = strchr(start, '>');
    if (!end) {
        end = strstr(start, ";");
        if (!end) end = start + strlen(start);
    }
    size_t uri_len = end - start;
    if (uri_len >= len) uri_len = len - 1;
    memcpy(buf, start, uri_len);
    buf[uri_len] = '\0';
    return 1;
}

int extract_tag_from_header(const char *header_value, char *buf, size_t len) {
    const char *tag_start = strstr(header_value, ";tag=");
    if (!tag_start) { buf[0] = '\0'; return 0; }
    tag_start += 5;
    const char *tag_end = strstr(tag_start, ";");
    if (!tag_end) tag_end = tag_start + strlen(tag_start);
    size_t tag_len = tag_end - tag_start;
    if (tag_len >= len) tag_len = len - 1;
    memcpy(buf, tag_start, tag_len);
    buf[tag_len] = '\0';
    return 1;
}

// Extract display name from SIP From/To header
// Handles formats like: "John Doe" <sip:...> or John Doe <sip:...>
// Falls back to user_id if no display name is present
void extract_display_name_from_header(const char *header_value, const char *user_id,
                                       char *display_name, size_t display_name_len) {
    const char *quote_start = strchr(header_value, '"');
    if (quote_start) {
        quote_start++;
        const char *quote_end = strchr(quote_start, '"');
        if (quote_end) {
            size_t name_len = quote_end - quote_start;
            if (name_len >= display_name_len) name_len = display_name_len - 1;
            memcpy(display_name, quote_start, name_len);
            display_name[name_len] = '\0';
            return;
        }
    }
    // No display name found, use user_id as fallback
    strncpy(display_name, user_id, display_name_len - 1);
    display_name[display_name_len - 1] = '\0';
}

// Extract codec from SDP body
// Parses m=audio line and a=rtpmap lines to determine codec name
// Example SDP: m=audio 8000 RTP/AVP 0 8 -> codec 0 -> PCMU
void extract_codec_from_sdp(const char *sip_message, char *codec_out, size_t codec_out_len) {
    // Find SDP body (after "\r\n\r\n")
    const char *body = strstr(sip_message, "\r\n\r\n");
    if (!body) {
        codec_out[0] = '\0';
        return;
    }
    body += 4; // Skip past "\r\n\r\n"

    // Find m=audio line
    const char *m_audio = strstr(body, "m=audio");
    if (!m_audio) {
        codec_out[0] = '\0';
        return;
    }

    // Extract first payload type number from m=audio line
    // Format: m=audio <port> RTP/AVP <payload_types...>
    const char *rtp_avp = strstr(m_audio, "RTP/AVP");
    if (!rtp_avp) {
        codec_out[0] = '\0';
        return;
    }
    rtp_avp += 7; // Skip "RTP/AVP"

    // Skip whitespace
    while (*rtp_avp == ' ' || *rtp_avp == '\t') rtp_avp++;

    // Extract first payload type number
    int payload_type = atoi(rtp_avp);

    // Common static payload types (RFC 3551)
    const char *codec_name = NULL;
    switch (payload_type) {
        case 0:  codec_name = "PCMU"; break;    // G.711 μ-law
        case 3:  codec_name = "GSM"; break;
        case 4:  codec_name = "G723"; break;
        case 8:  codec_name = "PCMA"; break;    // G.711 A-law
        case 9:  codec_name = "G722"; break;
        case 18: codec_name = "G729"; break;
        default:
            // Try to find a=rtpmap line for dynamic payload types
            {
                char search_pattern[32];
                snprintf(search_pattern, sizeof(search_pattern), "a=rtpmap:%d ", payload_type);
                const char *rtpmap = strstr(body, search_pattern);
                if (rtpmap) {
                    rtpmap += strlen(search_pattern);
                    // Extract codec name until '/' or whitespace
                    const char *codec_end = rtpmap;
                    while (*codec_end && *codec_end != '/' && *codec_end != '\r' &&
                           *codec_end != '\n' && *codec_end != ' ') {
                        codec_end++;
                    }
                    size_t codec_len = codec_end - rtpmap;
                    if (codec_len > 0 && codec_len < codec_out_len) {
                        memcpy(codec_out, rtpmap, codec_len);
                        codec_out[codec_len] = '\0';
                        return;
                    }
                }
                // Unknown payload type
                snprintf(codec_out, codec_out_len, "PT%d", payload_type);
                return;
            }
    }

    if (codec_name) {
        strncpy(codec_out, codec_name, codec_out_len - 1);
        codec_out[codec_out_len - 1] = '\0';
    } else {
        codec_out[0] = '\0';
    }
}

// These functions (extract_port_from_uri, extract_ip_from_uri) are no longer strictly needed
// for routing *dynamic* registrations if we always use DNS and SIP_PORT.
// However, they might still be useful for parsing incoming SIP messages or for other purposes.
// I'll keep them but comment out their direct use in SIP REGISTER/INVITE if removed from struct.
int extract_port_from_uri(const char *uri) {
    const char *colon_pos = strrchr(uri, ':');
    if (colon_pos && *(colon_pos + 1) != '/' && isdigit((unsigned char)*(colon_pos + 1))) {
        return atoi(colon_pos + 1);
    }
    return -1;
}

int extract_ip_from_uri(const char *uri, char *ip_buf, size_t len) {
    const char *start = strstr(uri, "@");
    if (!start) {
        start = strstr(uri, "sip:");
        if (start) start += 4;
        else start = uri;
    } else {
        start++;
    }
    const char *colon_pos = strchr(start, ':');
    const char *end = strchr(start, ';');
    if (!end && colon_pos) end = colon_pos;
    if (!end) end = start + strlen(start);
    size_t ip_len = end - start;
    if (ip_len <= 0) { ip_buf[0] = '\0'; return 0; }
    if (ip_len >= len) ip_buf[len - 1] = '\0';
    memcpy(ip_buf, start, ip_len);
    ip_buf[ip_len] = '\0';
    return 1;
}

void get_first_line(const char *msg, char *buf, size_t len) {
    const char *end = strstr(msg, "\r\n");
    if (end) {
        size_t line_len = end - msg;
        if (line_len >= len) line_len = len - 1;
        memcpy(buf, msg, line_len);
        buf[line_len] = '\0';
    } else {
        strncpy(buf, msg, len - 1);
        buf[len - 1] = '\0';
    }
}

void get_sip_method(const char *msg, char *buf, size_t len) {
    const char *end = strchr(msg, ' ');
    if (end) {
        size_t method_len = end - msg;
        if (method_len >= len) method_len = len - 1;
        memcpy(buf, msg, method_len);
        buf[method_len] = '\0';
    } else {
        buf[0] = '\0';
    }
}

// Helper function to add Record-Route header to a SIP response
void add_record_route_to_response(const char *original_msg, char *output_buffer, size_t output_buffer_size) {
    if (g_server_ip[0] == '\0') {
        // No server IP available, just copy original
        strncpy(output_buffer, original_msg, output_buffer_size - 1);
        output_buffer[output_buffer_size - 1] = '\0';
        return;
    }

    // Find the first line (status line)
    const char *first_line_end = strstr(original_msg, "\r\n");
    if (!first_line_end) {
        strncpy(output_buffer, original_msg, output_buffer_size - 1);
        output_buffer[output_buffer_size - 1] = '\0';
        return;
    }

    // Copy status line
    size_t first_line_len = first_line_end - original_msg + 2; // Include \r\n
    if (first_line_len >= output_buffer_size) {
        LOG_ERROR("Buffer too small for response status line");
        return;
    }
    memcpy(output_buffer, original_msg, first_line_len);
    output_buffer[first_line_len] = '\0';
    int written = first_line_len;

    // Add Record-Route header right after status line
    int result = snprintf(output_buffer + written, output_buffer_size - written,
                          "Record-Route: <sip:%s:%d;lr>\r\n", g_server_ip, SIP_PORT);
    if (result < 0 || result >= (int)(output_buffer_size - written)) {
        LOG_ERROR("Buffer overflow adding Record-Route to response");
        return;
    }
    written += result;

    // Copy the rest of the message (headers + body)
    const char *rest = first_line_end + 2; // Skip past status line's \r\n
    size_t rest_len = strlen(rest);
    if (written + rest_len >= output_buffer_size) {
        LOG_ERROR("Buffer overflow copying rest of response");
        return;
    }
    memcpy(output_buffer + written, rest, rest_len);
    output_buffer[written + rest_len] = '\0';

    LOG_DEBUG("Added Record-Route to response");
}

void reconstruct_invite_message(const char *original_msg, const char *new_request_line_uri,
                                char *output_buffer, size_t output_buffer_size) {
    char method[32];
    char original_first_line[MAX_SIP_MSG_LEN];
    get_first_line(original_msg, original_first_line, sizeof(original_first_line));
    get_sip_method(original_first_line, method, sizeof(method));
    const char *sip_version_str = strstr(original_first_line, "SIP/2.0");
    const char *version = sip_version_str ? sip_version_str : "SIP/2.0";

    int written = 0;
    int result = snprintf(output_buffer + written, output_buffer_size - written,
                           "%s %s %s\r\n", method, new_request_line_uri, version);
    if (result < 0 || result >= (int)(output_buffer_size - written)) {
        LOG_ERROR("SIP: reconstruct_invite_message: Initial line buffer overflow.");
        output_buffer[output_buffer_size - 1] = '\0';
        return;
    }
    written += result;

    const char *headers_start = strstr(original_msg, "\r\n");
    if (headers_start) {
        headers_start += 2;

        const char *body_start = strstr(headers_start, "\r\n\r\n");
        int content_length = 0;
        if (body_start) {
            body_start += 4;
            content_length = strlen(body_start);
        }

        const char *current_pos = headers_start;

        while (current_pos && strncmp(current_pos, "\r\n", 2) != 0 && written < (int)output_buffer_size) {
            const char *line_end = strstr(current_pos, "\r\n");
            if (!line_end) line_end = current_pos + strlen(current_pos);

            if (strncasecmp(current_pos, "Content-Length:", 15) == 0) {
                 current_pos = line_end + 2;
                 continue;
            }

            size_t copy_len = line_end - current_pos;
            if (written + copy_len + 2 < output_buffer_size) {
                memcpy(output_buffer + written, current_pos, copy_len);
                written += copy_len;
                memcpy(output_buffer + written, "\r\n", 2);
                written += 2;
                output_buffer[written] = '\0';
            } else {
                LOG_WARN("SIP: reconstruct_invite_message: Output buffer overflow during header copy.");
                written = (int)output_buffer_size - 1;
                output_buffer[written] = '\0';
                break;
            }
            current_pos = line_end + 2;
        }

        // Add Record-Route header to force all in-dialog messages (including BYE) through proxy
        if (g_server_ip[0] != '\0') {
            if (written + 100 >= (int)output_buffer_size) {
                LOG_WARN("SIP: reconstruct_invite_message: Not enough space for Record-Route header.");
                output_buffer[output_buffer_size - 1] = '\0';
                return;
            }
            result = snprintf(output_buffer + written, output_buffer_size - written,
                              "Record-Route: <sip:%s:%d;lr>\r\n", g_server_ip, SIP_PORT);
            if (result < 0 || result >= (int)(output_buffer_size - written)) {
                LOG_ERROR("SIP: reconstruct_invite_message: Record-Route header buffer overflow.");
                output_buffer[output_buffer_size - 1] = '\0';
                return;
            }
            written += result;
            LOG_DEBUG("Added Record-Route: <sip:%s:%d;lr>", g_server_ip, SIP_PORT);
        }

        if (written + 20 >= (int)output_buffer_size) {
             LOG_WARN("SIP: reconstruct_invite_message: Not enough space for Content-Length header or final CRLF.");
             output_buffer[output_buffer_size - 1] = '\0';
             return;
        }

        result = snprintf(output_buffer + written, output_buffer_size - written,
                            "Content-Length: %d\r\n", content_length);
        if (result < 0 || result >= (int)(output_buffer_size - written)) {
            LOG_ERROR("SIP: reconstruct_invite_message: Content-Length header buffer overflow.");
            output_buffer[output_buffer_size - 1] = '\0';
            return;
        }
        written += result;

        result = snprintf(output_buffer + written, output_buffer_size - written, "\r\n");
        if (result < 0 || result >= (int)(output_buffer_size - written)) {
            LOG_ERROR("SIP: reconstruct_invite_message: Final CRLF buffer overflow.");
            output_buffer[output_buffer_size - 1] = '\0';
            return;
        }
        written += result;


        if (body_start && *body_start && written < (int)output_buffer_size) {
            size_t to_copy = strlen(body_start);
            if (written + to_copy >= (int)output_buffer_size) {
                to_copy = output_buffer_size - written - 1;
            }
            memcpy(output_buffer + written, body_start, to_copy);
            output_buffer[written + to_copy] = '\0';
        } else if (written < (int)output_buffer_size) {
            output_buffer[written] = '\0';
        } else {
            LOG_ERROR("SIP: reconstruct_invite_message buffer overflow at end; truncating.");
            output_buffer[output_buffer_size - 1] = '\0';
        }
    } else {
        output_buffer[written] = '\0';
    }
}


void send_sip_response(int sockfd,
                       const struct sockaddr_in *dest_addr,
                       socklen_t dest_len,
                       const char *status_line,
                       const char *call_id,
                       const char *cseq,
                       const char *from_hdr,
                       const char *to_hdr,
                       const char *via_hdr,
                       const char *contact_hdr, // Still pass contact_hdr to allow responses to contain it if needed (e.g. 200 OK for REGISTER)
                       const char *extra_headers,
                       const char *body)
{
    char response_buffer[MAX_SIP_MSG_LEN];
    int len = 0;
    int result;

    result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "%s\r\n", status_line);
    if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
    len += result;

    if (*via_hdr) {
        result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "Via: %s\r\n", via_hdr);
        if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
        len += result;
    }
    if (*from_hdr) {
        result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "From: %s\r\n", from_hdr);
        if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
        len += result;
    }
    if (*to_hdr) {
        result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "To: %s\r\n", to_hdr);
        if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
        len += result;
    }
    if (*call_id) {
        result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "Call-ID: %s\r\n", call_id);
        if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
        len += result;
    }
    if (*cseq) {
        result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "CSeq: %s\r\n", cseq);
        if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
        len += result;
    }
    if (contact_hdr && *contact_hdr) // Only include Contact if provided by caller (e.g., from REGISTER message)
    {
        result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "Contact: %s\r\n", contact_hdr);
        if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
        len += result;
    }
    if (extra_headers && *extra_headers)
    {
        result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "%s\r\n", extra_headers);
        if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
        len += result;
    }

    int content_length = (body && *body) ? strlen(body) : 0;
    result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "Content-Length: %d\r\n", content_length);
    if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
    len += result;

    result = snprintf(response_buffer + len, MAX_SIP_MSG_LEN - len, "\r\n");
    if (result < 0 || result >= (int)(MAX_SIP_MSG_LEN - len)) { goto buffer_overflow; }
    len += result;

    if (body && *body) {
        if ((size_t)len + strlen(body) + 1 > MAX_SIP_MSG_LEN) {
            LOG_ERROR("SIP: SIP response body buffer overflowed; body truncated.");
            strncpy(response_buffer + len, body, MAX_SIP_MSG_LEN - len - 1);
            len = MAX_SIP_MSG_LEN - 1;
            response_buffer[len] = '\0';
        } else {
            strcat(response_buffer, body);
            len += strlen(body);
        }
    }

    goto end_send;

buffer_overflow:
    LOG_ERROR("SIP: SIP response buffer overflowed during snprintf; message truncated.");
    len = MAX_SIP_MSG_LEN - 1;
    response_buffer[len] = '\0';

end_send:;
    ssize_t sent_bytes = sendto(sockfd, response_buffer, len, 0,
                                 (const struct sockaddr*)dest_addr, dest_len);
    if (sent_bytes < 0) {
        LOG_ERROR("SIP: Error sending SIP response to %s:%d.",
                    sockaddr_to_ip_str(dest_addr),
                    ntohs(dest_addr->sin_port));
    } else {
        LOG_DEBUG("SIP: Sent SIP response to %s:%d (bytes: %zd):\n%s",
                    sockaddr_to_ip_str(dest_addr),
                    ntohs(dest_addr->sin_port),
                    sent_bytes, response_buffer);
    }
}

void send_sip_message(int sockfd,
                      const struct sockaddr_in *dest_addr,
                      socklen_t dest_len,
                      const char *msg)
{
    ssize_t sent_bytes = sendto(sockfd, msg, strlen(msg), 0,
                                 (const struct sockaddr*)dest_addr, dest_len);
    if (sent_bytes < 0) {
        LOG_ERROR("SIP: Error proxying SIP message to %s:%d.",
                    sockaddr_to_ip_str(dest_addr),
                    ntohs(dest_addr->sin_port));
    } else {
        LOG_DEBUG("Proxied SIP message to %s:%d (bytes: %zd):\n%s",
                    sockaddr_to_ip_str(dest_addr),
                    ntohs(dest_addr->sin_port),
                    sent_bytes, msg);
    }
}

void send_response_to_registered(int sockfd,
    const char *user_id,
    const struct sockaddr_in *cliaddr, socklen_t cli_len,
    const char *status_line,
    const char *call_id, const char *cseq,
    const char *from_hdr, const char *to_hdr,
    const char *via_hdr,
    const char *contact_hdr_for_response, // This can still be passed if the original message had one (e.g., REGISTER's Contact)
    const char *extra_hdrs, const char *body)
{
    // In the simplified model, we always resolve via DNS for call routing.
    // However, for responses to the *originating client* (cliaddr), we use cliaddr directly.
    // The contact_hdr_for_response is still used for 200 OK for REGISTER.

    // No need to find registered user here to get their IP/port from struct,
    // as routing is now based on DNS for INVITEs, and cliaddr for responses to original sender.

    send_sip_response(sockfd,
                      cliaddr, cli_len, // Always send response back to the client that sent the request
                      status_line,
                      call_id, cseq,
                      from_hdr, to_hdr, via_hdr,
                      contact_hdr_for_response, // Use original Contact header for response if applicable (e.g. 200 OK for REGISTER)
                      extra_hdrs, body);
}

void process_incoming_sip_message(int sockfd, const char *buffer, ssize_t n,
                                  const struct sockaddr_in *cliaddr, socklen_t cli_len) {
    char first_line[MAX_SIP_MSG_LEN];
    get_first_line(buffer, first_line, sizeof(first_line));

    if (n < 10 || strlen(first_line) == 0) {
        return;
    }

    char via_hdr[MAX_CONTACT_URI_LEN]     = "";
    char from_hdr[MAX_CONTACT_URI_LEN]    = "";
    char to_hdr[MAX_CONTACT_URI_LEN]      = "";
    char call_id_hdr[MAX_CONTACT_URI_LEN] = "";
    char cseq_hdr[MAX_CONTACT_URI_LEN]    = "";
    char contact_hdr[MAX_CONTACT_URI_LEN] = ""; // Still extract for parsing REGISTER/INVITE

    extract_sip_header(buffer, "Via:", via_hdr, sizeof(via_hdr));
    extract_sip_header(buffer, "From:", from_hdr, sizeof(from_hdr));
    extract_sip_header(buffer, "To:", to_hdr, sizeof(to_hdr));
    extract_sip_header(buffer, "Call-ID:", call_id_hdr, sizeof(call_id_hdr));
    extract_sip_header(buffer, "CSeq:", cseq_hdr, sizeof(cseq_hdr));
    extract_sip_header(buffer, "Contact:", contact_hdr, sizeof(contact_hdr));


    if (strncmp(first_line, "SIP/2.0", 7) == 0) {
        LOG_INFO("Received SIP Response: %s", first_line);

        CallSession *session = find_call_session_by_callid(call_id_hdr);
        if (session) {
            LOG_DEBUG("Matching session found for response: %s", session->call_id);

            // For INVITE responses, add Record-Route header before forwarding
            if (strstr(cseq_hdr, "INVITE")) {
                char modified_response[MAX_SIP_MSG_LEN];
                add_record_route_to_response(buffer, modified_response, sizeof(modified_response));
                send_sip_message(sockfd, &session->original_caller_addr, sizeof(session->original_caller_addr), modified_response);
                LOG_DEBUG("Proxied INVITE response with Record-Route for Call-ID %s to original caller (%s:%d).",
                            session->call_id, sockaddr_to_ip_str(&session->original_caller_addr),
                            ntohs(session->original_caller_addr.sin_port));
            } else {
                // For non-INVITE responses, forward as-is
                send_sip_message(sockfd, &session->original_caller_addr, sizeof(session->original_caller_addr), buffer);
                LOG_DEBUG("Proxied response for Call-ID %s to original caller (%s:%d).",
                            session->call_id, sockaddr_to_ip_str(&session->original_caller_addr),
                            ntohs(session->original_caller_addr.sin_port));
            }

            if (strstr(first_line, "200 OK") && strstr(cseq_hdr, "INVITE")) {
                session->state = CALL_STATE_ESTABLISHED;
                LOG_INFO("Call-ID %s state changed to ESTABLISHED.", session->call_id);
                export_active_calls_json();
            } else if (strstr(first_line, "4") == first_line + 8 || strstr(first_line, "5") == first_line + 8 || strstr(first_line, "6") == first_line + 8) {
                LOG_WARN("Received error response for Call-ID %s: %s", session->call_id, first_line);
                terminate_call_session(session);
                export_active_calls_json();
            } else if (strstr(first_line, "180 Ringing") || strstr(first_line, "183 Session Progress")) {
                session->state = CALL_STATE_RINGING;
                LOG_INFO("Call-ID %s state changed to RINGING.", session->call_id);
                export_active_calls_json();
            }
        } else {
            LOG_WARN("SIP response received with no matching call session: %s", call_id_hdr);
        }
    } else {
        char method[32];
        get_sip_method(first_line, method, sizeof(method));
        if (!*method) {
            LOG_DEBUG("Received invalid SIP request format from %s:%d. Ignoring.",
                        sockaddr_to_ip_str(cliaddr),
                        ntohs(cliaddr->sin_port));
            return;
        }
        LOG_DEBUG("Identified incoming as SIP Request: %s.", method);

        char from_uri[MAX_CONTACT_URI_LEN] = "";
        char from_user_id[MAX_USER_ID_LEN] = "";
        char from_tag[64] = "";
        char to_uri[MAX_CONTACT_URI_LEN] = "";
        char to_user_id[MAX_USER_ID_LEN] = "";

        extract_uri_from_header(from_hdr, from_uri, sizeof(from_uri));
        parse_user_id_from_uri(from_uri, from_user_id, sizeof(from_user_id));
        extract_tag_from_header(from_hdr, from_tag, sizeof(from_tag));

        extract_uri_from_header(to_hdr, to_uri, sizeof(to_uri));
        parse_user_id_from_uri(to_uri, to_user_id, sizeof(to_user_id));


        if (strcmp(method, "REGISTER") == 0) {
            char expires_hdr[32] = "";
            extract_sip_header(buffer, "Expires:", expires_hdr,
                               sizeof(expires_hdr));
            int expires = atoi(expires_hdr);

            char display_name[MAX_DISPLAY_NAME_LEN] = "";
            extract_display_name_from_header(from_hdr, from_user_id, display_name, sizeof(display_name));

            // Call simplified add_or_update_registered_user
            add_or_update_registered_user(from_user_id, display_name, expires);

            send_response_to_registered(sockfd,
                                        from_user_id,
                                        cliaddr, cli_len,
                                        "SIP/2.0 200 OK",
                                        call_id_hdr, cseq_hdr,
                                        from_hdr, to_hdr, via_hdr,
                                        contact_hdr, // Respond with client's Contact
                                        "Expires: 3600", NULL); // Default expiry is fine
            LOG_INFO("REGISTER processed for user %s from %s:%d. Expires: %d.",
                        from_user_id, sockaddr_to_ip_str(cliaddr),
                        ntohs(cliaddr->sin_port), expires);

        } else if (strcmp(method, "INVITE") == 0) {
            LOG_INFO("Received INVITE for %s from %s.", to_user_id, from_user_id);

            // Route based on DNS resolution (works for both local and remote phones)
            struct sockaddr_in resolved_callee_addr;
            memset(&resolved_callee_addr, 0, sizeof(resolved_callee_addr));
            resolved_callee_addr.sin_family = AF_INET;

            char hostname_to_resolve[MAX_USER_ID_LEN + sizeof(AREDN_MESH_DOMAIN) + 1];
            snprintf(hostname_to_resolve, sizeof(hostname_to_resolve), "%s.%s", to_user_id, AREDN_MESH_DOMAIN);

            struct addrinfo hints, *res;
            int status;
            bool resolved = false;

            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;

            if ((status = getaddrinfo(hostname_to_resolve, NULL, &hints, &res)) != 0) {
                LOG_ERROR("getaddrinfo for %s failed: %s", hostname_to_resolve, gai_strerror(status));
            } else {
                struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
                if (inet_pton(AF_INET, sockaddr_to_ip_str(ipv4), &resolved_callee_addr.sin_addr) > 0) {
                    resolved = true;
                    LOG_INFO("Resolved callee '%s' (%s) to IP %s", to_user_id, hostname_to_resolve, sockaddr_to_ip_str(ipv4));
                }
                freeaddrinfo(res);
            }

            if (!resolved) {
                LOG_INFO("INVITE failed: Callee %s hostname '%s' could not be resolved.", to_user_id, hostname_to_resolve);
                send_response_to_registered(sockfd, from_user_id, cliaddr, cli_len,
                                            "SIP/2.0 404 Not Found", call_id_hdr, cseq_hdr,
                                            from_hdr, to_hdr, via_hdr, NULL, NULL, NULL);
                return;
            }
            resolved_callee_addr.sin_port = htons(SIP_PORT);

            CallSession *session = create_call_session();
            if (!session) {
                LOG_INFO("INVITE failed: Max call sessions reached.");
                send_response_to_registered(sockfd,
                                            from_user_id,
                                            cliaddr, cli_len,
                                            "SIP/2.0 503 Service Unavailable",
                                            call_id_hdr, cseq_hdr,
                                            from_hdr, to_hdr, via_hdr,
                                            NULL, NULL, NULL);
                return;
            }
            strncpy(session->call_id, call_id_hdr, sizeof(session->call_id) - 1);
            session->call_id[sizeof(session->call_id) - 1] = '\0';
            strncpy(session->cseq, cseq_hdr, sizeof(session->cseq) - 1);
            session->cseq[sizeof(session->cseq) - 1] = '\0';
            strncpy(session->from_tag, from_tag, sizeof(session->from_tag) - 1);
            session->from_tag[sizeof(session->from_tag) - 1] = '\0';

            memcpy(&session->original_caller_addr, cliaddr, cli_len);
            memcpy(&session->callee_addr, &resolved_callee_addr, sizeof(resolved_callee_addr));

            // Populate call details for dashboard display
            strncpy(session->caller_user_id, from_user_id, sizeof(session->caller_user_id) - 1);
            session->caller_user_id[sizeof(session->caller_user_id) - 1] = '\0';
            strncpy(session->callee_user_id, to_user_id, sizeof(session->callee_user_id) - 1);
            session->callee_user_id[sizeof(session->callee_user_id) - 1] = '\0';

            // Extract display names from From and To headers
            extract_display_name_from_header(from_hdr, from_user_id,
                                             session->caller_display_name,
                                             sizeof(session->caller_display_name));
            extract_display_name_from_header(to_hdr, to_user_id,
                                             session->callee_display_name,
                                             sizeof(session->callee_display_name));

            // Extract codec from SDP body
            extract_codec_from_sdp(buffer, session->codec, sizeof(session->codec));

            // Store callee hostname for traceroute
            strncpy(session->callee_hostname, hostname_to_resolve, sizeof(session->callee_hostname) - 1);
            session->callee_hostname[sizeof(session->callee_hostname) - 1] = '\0';

            LOG_DEBUG("Callee '%s' target: %s:%d",
                        to_user_id, sockaddr_to_ip_str(&session->callee_addr), ntohs(session->callee_addr.sin_port));

            send_response_to_registered(sockfd,
                                        from_user_id,
                                        cliaddr, cli_len,
                                        "SIP/2.0 100 Trying",
                                        call_id_hdr, cseq_hdr,
                                        from_hdr, to_hdr, via_hdr,
                                        NULL,
                                        NULL, NULL);
            LOG_INFO("Sent 100 Trying for Call-ID %s.", session->call_id);
            session->state = CALL_STATE_INVITE_SENT;
            export_active_calls_json();

            char new_request_line_uri[MAX_CONTACT_URI_LEN];
            snprintf(new_request_line_uri, sizeof(new_request_line_uri),
                     "sip:%s@%s:%d", to_user_id, sockaddr_to_ip_str(&resolved_callee_addr), SIP_PORT);

            char proxied_invite[MAX_SIP_MSG_LEN];
            reconstruct_invite_message(buffer, new_request_line_uri, proxied_invite, sizeof(proxied_invite));

            send_sip_message(sockfd, &session->callee_addr, sizeof(session->callee_addr), proxied_invite);
            LOG_INFO("Proxied INVITE for Call-ID %s from %s to %s.",
                        session->call_id, from_user_id, to_user_id);

        } else if (strcmp(method, "BYE") == 0) {
            LOG_INFO("Received BYE for Call-ID %s.", call_id_hdr);
            CallSession *session = find_call_session_by_callid(call_id_hdr);
            if (session) {
                session->state = CALL_STATE_TERMINATING;

                struct sockaddr_in other_party_addr;
                bool is_caller_sending_bye = (strcmp(sockaddr_to_ip_str(&session->original_caller_addr), sockaddr_to_ip_str(cliaddr)) == 0 &&
                                              ntohs(session->original_caller_addr.sin_port) == ntohs(cliaddr->sin_port));

                if (is_caller_sending_bye) {
                    memcpy(&other_party_addr, &session->callee_addr, sizeof(other_party_addr));
                    LOG_DEBUG("Caller (%s:%d) sent BYE. Proxying to callee (%s:%d).",
                                sockaddr_to_ip_str(cliaddr), ntohs(cliaddr->sin_port),
                                sockaddr_to_ip_str(&other_party_addr), ntohs(other_party_addr.sin_port));
                } else {
                    memcpy(&other_party_addr, &session->original_caller_addr, sizeof(other_party_addr));
                    LOG_DEBUG("Callee (%s:%d) sent BYE. Proxying to caller (%s:%d).",
                                sockaddr_to_ip_str(cliaddr), ntohs(cliaddr->sin_port),
                                sockaddr_to_ip_str(&other_party_addr), ntohs(other_party_addr.sin_port));
                }

                send_sip_message(sockfd, &other_party_addr, sizeof(other_party_addr), buffer);

                send_response_to_registered(sockfd,
                                            from_user_id,
                                            cliaddr, cli_len,
                                            "SIP/2.0 200 OK",
                                            call_id_hdr, cseq_hdr,
                                            from_hdr, to_hdr, via_hdr,
                                            NULL, NULL, NULL);
                LOG_INFO("BYE processed and session %s terminated.", session->call_id);
                terminate_call_session(session);
                export_active_calls_json();
            } else {
                LOG_INFO("BYE failed: No matching call session for Call-ID %s.", call_id_hdr);
                send_response_to_registered(sockfd,
                                            from_user_id,
                                            cliaddr, cli_len,
                                            "SIP/2.0 481 Call/Transaction Does Not Exist",
                                            call_id_hdr, cseq_hdr,
                                            from_hdr, to_hdr, via_hdr,
                                            NULL, NULL, NULL);
            }

        } else if (strcmp(method, "CANCEL") == 0) {
            LOG_INFO("Received CANCEL for Call-ID %s.", call_id_hdr);
            CallSession *session = find_call_session_by_callid(call_id_hdr);
            if (session &&
               (session->state == CALL_STATE_INVITE_SENT ||
                session->state == CALL_STATE_RINGING)) {

                send_sip_message(sockfd, &session->callee_addr, sizeof(session->callee_addr), buffer);
                LOG_DEBUG("Proxied CANCEL for Call-ID %s to callee (%s:%d).",
                            session->call_id, sockaddr_to_ip_str(&session->callee_addr),
                            ntohs(session->callee_addr.sin_port));

                send_response_to_registered(sockfd,
                                            from_user_id,
                                            cliaddr, cli_len,
                                            "SIP/2.0 200 OK",
                                            call_id_hdr, cseq_hdr,
                                            from_hdr, to_hdr, via_hdr,
                                            NULL, NULL, NULL);
                LOG_INFO("CANCEL processed and session %s terminated.", session->call_id);
                terminate_call_session(session);
                export_active_calls_json();
            } else {
                LOG_INFO("CANCEL failed: No matching call session or invalid state for Call-ID %s.", call_id_hdr);
                send_response_to_registered(sockfd,
                                            from_user_id,
                                            cliaddr, cli_len,
                                            "SIP/2.0 481 Call/Transaction Does Not Exist",
                                            call_id_hdr, cseq_hdr,
                                            from_hdr, to_hdr, via_hdr,
                                            NULL, NULL, NULL);
            }

        } else if (strcmp(method, "OPTIONS") == 0) {
            LOG_INFO("Received OPTIONS from %s:%d. Responding 200 OK.", sockaddr_to_ip_str(cliaddr), ntohs(cliaddr->sin_port));
            send_response_to_registered(sockfd,
                                        from_user_id,
                                        cliaddr, cli_len,
                                        "SIP/2.0 200 OK",
                                        call_id_hdr, cseq_hdr,
                                        from_hdr, to_hdr, via_hdr,
                                        NULL, // No specific contact URI to echo back for OPTIONS
                                        "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REGISTER, SUBSCRIBE, NOTIFY, REFER, INFO, MESSAGE, UPDATE",
                                        NULL);

        } else if (strcmp(method, "ACK") == 0) {
            LOG_INFO("Received ACK for Call-ID %s.", call_id_hdr);
            CallSession *session = find_call_session_by_callid(call_id_hdr);
            if (session && session->state == CALL_STATE_ESTABLISHED) {
                send_sip_message(sockfd, &session->callee_addr, sizeof(session->callee_addr), buffer);
                LOG_DEBUG("Proxied ACK for Call-ID %s to callee.", session->call_id);
            } else {
                LOG_WARN("Received ACK for no matching session or invalid state: Call-ID %s.", call_id_hdr);
            }
        }
        else {
            LOG_WARN("Received unhandled SIP method: %s from %s:%d. Responding 501 Not Implemented.",
                        method, sockaddr_to_ip_str(cliaddr), ntohs(cliaddr->sin_port));
            send_response_to_registered(sockfd,
                                        from_user_id,
                                        cliaddr, cli_len,
                                        "SIP/2.0 501 Not Implemented",
                                        call_id_hdr, cseq_hdr,
                                        from_hdr, to_hdr, via_hdr,
                                        NULL, NULL, NULL);
        }
    }
}
