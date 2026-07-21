#pragma once
#include "esp_event.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
extern esp_event_handler_instance_t inst_any, inst_got_ip;


// Initialise WiFi and block until connected.
// Returns 0 on success, -1 on failure.
int scaife_wifi_init(const char *ssid, const char *pass);

// passage XML from:
//   https://scaife.perseus.org/library/<full_urn>/cts-api-xml/
// Writes raw XML into buf (null-terminated).
// Returns number of bytes written (>0) or -1 on error.
int scaife_get_passage_xml(const char *full_urn,
                           char *buf, int buf_size);

// Fetch the passage JSON from:
//   https://scaife.perseus.org/library/passage/<full_urn>/json/
// Writes raw JSON into buf (null-terminated).
// Returns number of bytes written (>0) or -1 on error.
int scaife_get_passage_json(const char *full_urn,
                            char *buf, int buf_size, bool link_h_only=0);
int load_authors_json();
#ifdef __cplusplus
}
#endif