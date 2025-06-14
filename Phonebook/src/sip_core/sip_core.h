#ifndef SIP_CORE_H
#define SIP_CORE_H

#include "../common.h"
#include "../user_manager/user_manager.h" 
#include "../call-sessions/call_sessions.h" // ADAPTED: Path changed from call_manager to call-sessions

int extract_sip_header(const char *msg, const char *hdr, char *buf, size_t len);
int parse_user_id_from_uri(const char *uri, char *buf, size_t len);
int extract_uri_from_header(const char *header_value, char *buf, size_t len);
int extract_tag_from_header(const char *header_value, char *buf, size_t len);
int extract_port_from_uri(const char *uri);
int extract_ip_from_uri(const char *uri, char *ip_buf, size_t len);
void get_first_line(const char *msg, char *buf, size_t len);
void get_sip_method(const char *msg, char *buf, size_t len);
void reconstruct_invite_message(const char *original_msg, const char *new_request_line_uri, char *output_buffer, size_t output_buffer_size);

void send_sip_response(int sockfd, const struct sockaddr_in *dest_addr, socklen_t dest_len, const char *status_line, const char *call_id, const char *cseq, const char *from_hdr, const char *to_hdr, const char *via_hdr, const char *contact_hdr, const char *extra_headers, const char *body);
void send_sip_message(int sockfd, const struct sockaddr_in *dest_addr, socklen_t dest_len, const char *msg);
void send_response_to_registered(int sockfd, const char *user_id, const struct sockaddr_in *cliaddr, socklen_t cli_len, const char *status_line, const char *call_id, const char *cseq, const char *from_hdr, const char *to_hdr, const char *via_hdr, const char *contact_hdr_for_response, const char *extra_hdrs, const char *body);

void process_incoming_sip_message(int sockfd, const char *buffer, ssize_t n,
                                  const struct sockaddr_in *cliaddr, socklen_t cli_len);

#endif