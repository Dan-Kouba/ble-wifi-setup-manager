#include "ble_wifi_setup_manager.h"

#if PLATFORM_ID != PLATFORM_ARGON && PLATFORM_ID != PLATFORM_P2 // Argon or P2 only
    #error "This library only works on the Argon or P2!"
#else

Logger BLEWiFiSetupManagerLogger("app.BLEWiFiSetupManager");

BLEWiFiSetupManager *BLEWiFiSetupManager::_instance = nullptr;

static const char * const security_strings[] = {
    "Unsecured",
    "WEP",
    "WPA",
    "WPA2",
    "WPA Enterprise",
    "WPA2 Enterprise",
};

static inline void wifi_scan_callback(WiFiAccessPoint* wap, void* data) {
    BLEWiFiSetupManager::instance().wifi_scan_handler(wap);
}

static inline void onDataReceived(const uint8_t* rx_data, size_t len, const BlePeerDevice& peer, void* self) {
    // Is this really the way to do this?
    BLEWiFiSetupManager::instance().ble_data_rx_handler(rx_data, len);
}

BLEWiFiSetupManager::BLEWiFiSetupManager() 
  : config_state(STATE_CONFIG_SETUP),
    next_config_state(STATE_CONFIG_SETUP),
    provisionCb(nullptr)
{}

void BLEWiFiSetupManager::setup() {
    rxCharacteristic = new BleCharacteristic("rx", BleCharacteristicProperty::NOTIFY, readUUID, serviceUUID);
    txCharacteristic = new BleCharacteristic("tx", BleCharacteristicProperty::WRITE_WO_RSP, writeUUID, serviceUUID, onDataReceived, nullptr);

    BLE.addCharacteristic(*rxCharacteristic);
    BLE.addCharacteristic(*txCharacteristic);

    // Advertise our custom configuration service UUID so the webapp can detect compatible devices
    BleAdvertisingData advData;
    advData.appendServiceUUID(serviceUUID);
    BLE.advertise(&advData);

    BLEWiFiSetupManagerLogger.trace("Bluetooth Address: %s", BLE.address().toString().c_str());
    BLE.on();

    // WiFi must be on for this library to work
    WiFi.on();
}

void BLEWiFiSetupManager::loop() {
    // Run state machine
    switch(config_state) {
        case STATE_CONFIG_SETUP: {
            next_config_state = STATE_CONFIG_IDLE;
            break;
        }

        case STATE_CONFIG_IDLE: {
            if (device_receive_msg_queue.empty()) {
                next_config_state = STATE_CONFIG_IDLE;
            } else {
                next_config_state = STATE_CONFIG_PARSE_MSG;
            }
            break;
        }

        case STATE_CONFIG_PARSE_MSG: {
            parse_message();
            next_config_state = STATE_CONFIG_IDLE;
            break;
        }
    }

    if (config_state != next_config_state) {
        BLEWiFiSetupManagerLogger.trace("State Transition: %u -> %u", config_state, next_config_state);
        config_state = next_config_state;
    }

    // Push out any WiFi AP updates to the device
    // TODO: use the JSONWriter class
    char tmp_buf[150];  // Need: ~64 chars + SSID length + null terminator
    while (!wifi_scan_response_queue.empty()) {
        WiFiAccessPoint ap = wifi_scan_response_queue.front();
        int len = sprintf(tmp_buf, 
            "{\"msg_t\":\"scan_resp\", \"ssid\":\"%s\", \"sec\":\"%s\", \"ch\":%d, \"rssi\":%d}", 
            ap.ssid, security_strings[(int)ap.security], (int)ap.channel, ap.rssi
        );
        rxCharacteristic->setValue((uint8_t*)tmp_buf, len);
        wifi_scan_response_queue.pop();
    }
}

void BLEWiFiSetupManager::wifi_scan_handler(WiFiAccessPoint* wap) {
    wifi_scan_response_queue.push(*wap);
}

void BLEWiFiSetupManager::parse_message() {
    // Pull our message off the queue, copy it locally, and free the original message
    // Probbaly not ideal since we don't really need to copy, but since we parse and have conditional
    // actions later, this may prevent accidental memory leaks with the addition of other conditional paths in this code 
    // e.g. returning somewhere down below before the free() call.
    char *msg_buf = device_receive_msg_queue.front();
    BLEWiFiSetupManagerLogger.trace("String RX: %s", msg_buf);
    JSONValue outerObj = JSONValue::parseCopy(msg_buf);
    device_receive_msg_queue.pop();
    free(msg_buf);

    // Process our received message
    JSONObjectIterator iter(outerObj);
    while(iter.next()) {
        BLEWiFiSetupManagerLogger.info("key=%s value=%s", 
            (const char *) iter.name(), 
            (const char *) iter.value().toString());
        
        if (iter.name() == "msg_type") {
            // We've received a valid message!
            if (strcmp((const char *)iter.value().toString(), "scan") == 0) {
                WiFi.scan(wifi_scan_callback);
                BLEWiFiSetupManagerLogger.info("WiFi Scan Complete");
                // TODO: send status message
            }
            else
            if (strcmp((const char *)iter.value().toString(), "set_creds") == 0) {
                JSONString ssid, pass;
                while(iter.next()) {
                    if (iter.name() == "ssid") {
                        ssid = iter.value().toString();
                        BLEWiFiSetupManagerLogger.info("Set WiFi SSID: %s", ssid.data());
                    }
                    else if (iter.name() == "password") {
                        pass = iter.value().toString();
                        BLEWiFiSetupManagerLogger.info("Set WiFi Password: %s", pass.data());
                    }
                    else {
                        BLEWiFiSetupManagerLogger.warn("Unrecognized key while parsing WiFi credentials: %s", (const char *)iter.name());
                    }
                }

                if (!ssid.isEmpty() && !pass.isEmpty()) {
                    WiFi.setCredentials(ssid.data(), pass.data());
                    if (provisionCb != nullptr) {
                        provisionCb();
                    }
                    BLEWiFiSetupManagerLogger.info("WiFi credentials set");
                    // if (WiFi.ready() || WiFi.connecting()) {
                    //     WiFi.disconnect();
                    // }
                    // WiFi.connect();
                    // TODO: send status message
                } else {
                    BLEWiFiSetupManagerLogger.warn("Failure parsing WiFi credentials");
                }
            }
        }
    }
}

void BLEWiFiSetupManager::ble_data_rx_handler(const uint8_t* rx_data, size_t len) {
    if( len > 0 ) {
        // The underlying BLE lib reuses the receive buffer, and will not terminate it properly for a string
        // Add some manual processing and properly terminate for string parsing
        char *msg_buf = (char*)malloc(len+1);
        memcpy(msg_buf, rx_data, len);
        msg_buf[len] = 0;   // Null-terminate string
        device_receive_msg_queue.push(msg_buf);
        BLEWiFiSetupManagerLogger.trace("Added message to the queue: %s", msg_buf);
        return;
    }
}

void BLEWiFiSetupManager::setProvisionCallback(provisionCb_t* cb) {
    provisionCb = cb;
}

#endif  // PLATFORM_ID == 12