// BLE-based WiFi Setup Manager library for Particle Gen 3 devices
#include "Particle.h"
#include "ble_wifi_constants.h"
#include <queue>

class BLEWiFiSetupManager {

    typedef enum {
        STATE_CONFIG_SETUP = 0,
        STATE_CONFIG_IDLE,
        STATE_CONFIG_PARSE_MSG,
    } ConfigState_t;

    typedef void (provisionCb_t)(void);

    public:
        BLEWiFiSetupManager();

        void setup();
        void loop();

        void wifi_scan_handler(WiFiAccessPoint* wap);
        void queue_msg(const uint8_t* rx_data, size_t len);

        void setProvisionCallback(provisionCb_t* cb);

    private:
        ConfigState_t config_state;
        ConfigState_t next_config_state;

        provisionCb_t *provisionCb;

        void parse_message();

        std::queue<WiFiAccessPoint> wifi_scan_response_queue;
        std::queue<char*> device_receive_msg_queue;

        BleCharacteristic *rxCharacteristic;
        BleCharacteristic *txCharacteristic;
};