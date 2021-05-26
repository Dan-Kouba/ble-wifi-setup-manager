#include "Particle.h"
#include "ble_wifi_setup_manager.h"

// This example does not require the cloud so you can run it in manual mode or
// normal cloud-connected mode
SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_WARN, {
    {"app", LOG_LEVEL_ALL},
    {"system.ctrl.ble", LOG_LEVEL_ALL},
    {"wiring.ble", LOG_LEVEL_ALL},
});

typedef enum {
    STATE_IDLE = 0,
    STATE_PROVISIONED
} ProvisionStates_t;

ProvisionStates_t provision_state, next_provision_state;

void provisionCb() {
    provision_state = STATE_PROVISIONED;
}

void setup() {
	provision_state      = STATE_IDLE;
    next_provision_state = STATE_IDLE;
    
    BLEWiFiSetupManager::instance().setup();
    BLEWiFiSetupManager::instance().setProvisionCallback(provisionCb);
}

void loop() {
	BLEWiFiSetupManager::instance().loop();

    // State machine to handle WiFi provisioning states
    switch(provision_state) {
        case STATE_IDLE: {
            next_provision_state = STATE_IDLE;
            break;
        }

        case STATE_PROVISIONED: {
            // Cycle WiFi state if needed (eg. bad password was entered and it's trying to connect)
            if (WiFi.ready() || WiFi.connecting()) {
                WiFi.disconnect();
            }
            
            // Then connect to cloud
            Particle.connect();
            
            next_provision_state = STATE_IDLE;
            break;
        }
    }

    if (provision_state != next_provision_state) {
        Log.info("Provision State Transition: %u -> %u", provision_state, next_provision_state);
        provision_state = next_provision_state;
    }
}