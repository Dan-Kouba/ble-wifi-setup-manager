#include "Particle.h"
#include "ble_wifi_setup_manager.h"

// This example does not require the cloud so you can run it in manual mode or
// normal cloud-connected mode
SYSTEM_MODE(MANUAL);

SerialLogHandler logHandler(LOG_LEVEL_TRACE, {
    {"app", LOG_LEVEL_ALL}
});

BLEWiFiSetupManager wifi_manager;

void setup() {
	wifi_manager.setup();
    WiFi.on();
}

void loop() {
	wifi_manager.loop();
}