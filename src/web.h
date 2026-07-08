#ifndef web_h
#define web_h

// Captive provisioning portal (AP mode): form at /, POST /save persists to
// settings and reboots, any other GET 302-redirects to the form (catches
// captive-portal probes). Also starts the DNS hijack task answering every
// A query with 192.168.4.1.
void web_start_portal();

// STA-mode JSON API on port 80: POST/GET /api/message, GET /api/status.
// Idempotent — safe to call on every GOT_IP (reconnects included). Never
// runs concurrently with the portal (different boot paths).
void web_start_api();

// Copy-out getter for the custom message (owned by this module, RAM-only —
// deliberately not persisted: it is ephemeral and NVS wear is avoided).
void web_get_message(char out[65]);

#endif
