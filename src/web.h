#ifndef web_h
#define web_h

// Captive provisioning portal (AP mode): form at /, POST /save persists to
// settings and reboots, any other GET 302-redirects to the form (catches
// captive-portal probes). Also starts the DNS hijack task answering every
// A query with 192.168.4.1. The /api/* server (STA mode) lands in M6.
void web_start_portal();

#endif
