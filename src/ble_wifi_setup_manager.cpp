#include "ble_wifi_setup_manager.h"

Logger BLEWiFiSetupManagerLogger("app.BLEWiFiSetupManager");

static const char * const security_strings[] = {
    "Unsecured",
    "WEP",
    "WPA",
    "WPA2",
    "WPA Enterprise",
    "WPA2 Enterprise",
};

static void wifi_scan_callback(WiFiAccessPoint* wap, BLEWiFiSetupManager* self) {
    self->wifi_scan_handler(wap);
}

static void onDataReceived(const uint8_t* rx_data, size_t len, const BlePeerDevice& peer, void* self) {
    // Is this really the way to do this?
    ((BLEWiFiSetupManager*)self)->queue_msg(rx_data, len);
}

BLEWiFiSetupManager::BLEWiFiSetupManager() 
  : config_state(STATE_CONFIG_SETUP),
    next_config_state(STATE_CONFIG_SETUP)
{}

void BLEWiFiSetupManager::setup() {
    rxCharacteristic = new BleCharacteristic("rx", BleCharacteristicProperty::NOTIFY, readUUID, serviceUUID);
    txCharacteristic = new BleCharacteristic("tx", BleCharacteristicProperty::WRITE_WO_RSP, writeUUID, serviceUUID, onDataReceived, this);

    BLE.addCharacteristic(*rxCharacteristic);
    BLE.addCharacteristic(*txCharacteristic);

    // Advertise our custom configuration service UUID so the webapp can detect compatible devices
    BleAdvertisingData advData;
    advData.appendServiceUUID(serviceUUID);
    BLE.advertise(&advData);

    BLEWiFiSetupManagerLogger.trace("Bluetooth Address: %s", BLE.address().toString().c_str());
    BLE.on();
}

void BLEWiFiSetupManager::loop() {
    // Run state machine
    switch(config_state) {
        case STATE_CONFIG_SETUP: {
            next_config_state = STATE_CONFIG_IDLE;
            break;
        }

        case STATE_CONFIG_IDLE: {
            next_config_state = STATE_CONFIG_IDLE;
            break;
        }

        case STATE_CONFIG_PARSE_MSG: {
            parse_message();    
            free(msg_buf);
            next_config_state = STATE_CONFIG_IDLE;
            break;
        }
    }

    if (config_state != next_config_state) {
        BLEWiFiSetupManagerLogger.info("State Transition: %u -> %u", config_state, next_config_state);
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
    BLEWiFiSetupManagerLogger.info("String RX: %s", msg_buf);

    JSONValue outerObj = JSONValue::parse(msg_buf, msg_len);
    JSONObjectIterator iter(outerObj);
    while(iter.next()) {
        BLEWiFiSetupManagerLogger.info("key=%s value=%s", 
            (const char *) iter.name(), 
            (const char *) iter.value().toString());
        
        if (iter.name() == "msg_type") {
            // We've received a valid message!
            if (strcmp((const char *)iter.value().toString(), "scan") == 0) {
                WiFi.scan(wifi_scan_callback, this);
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
                        return;
                    }
                }

                if (!ssid.isEmpty() && !pass.isEmpty()) {
                    WiFi.setCredentials(ssid.data(), pass.data());
                    if (WiFi.ready() || WiFi.connecting()) {
                        WiFi.disconnect();
                    }
                    WiFi.connect();
                    // TODO: send status message
                } else {
                    BLEWiFiSetupManagerLogger.warn("Failure parsing WiFi credentials");
                }
            }
        }
    }
}

void BLEWiFiSetupManager::queue_msg(const uint8_t* rx_data, size_t len) {
    if( len > 0 ) {
        // The underlying BLE lib reuses the receive buffer, and will not terminate it properly for a string
        // Add some manual processing and properly terminate for string parsing
        msg_buf = (char*)malloc(len+1);
        memcpy(msg_buf, rx_data, len);
        msg_buf[len] = 0;
        msg_len = len+1;
        config_state = STATE_CONFIG_PARSE_MSG;
        return;
    }
}
