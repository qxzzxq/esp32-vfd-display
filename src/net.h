#ifndef net_h
#define net_h

#include <stdint.h>

enum class NetState : uint8_t { Portal, Connecting, Connected };

// Brings up WiFi — STA from stored credentials, or the SoftAP provisioning
// portal when force_portal is set or no credentials exist — plus SNTP (STA
// mode) and the worker task that polls sensors (weather joins at M5).
void net_init(bool force_portal);

NetState net_state();
void net_get_ip(char out[16]);       // "0.0.0.0" until connected
void net_get_ap_ssid(char out[16]);  // portal AP name, e.g. "VFD-A1B2"
void net_reset_credentials();        // erase ssid/pass and restart into portal

#endif
