/*
Based on:
    BLE_MIDI Example by neilbags 
    https://github.com/neilbags/arduino-esp32-BLE-MIDI
    
    Based on BLE_notify example by Evandro Copercini.

    5 button keyboard
    2019/1/11 by morecat_lab
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

#define MIDI_SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

struct buttonConfigTag {
  int gpioNo;   // gpio pin number
  int noteNo;   // note number
  int pinState; // keep pin status
} buttonConfig[5] = {
  32, 60, HIGH, // middle c
  33, 61, HIGH,
  34, 62, HIGH,
  35, 63, HIGH,
  39, 64, HIGH,
};

const int ledPin = 4; // monitor led pin

uint8_t midiPacket[] = {
   0x80,  // header
   0x80,  // timestamp, not implemented 
   0x00,  // status
   0x3c,  // 0x3c == 60 == middle c
   0x00   // velocity
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

void setup() {
  Serial.begin(115200);

  // initialize the LED pin as an output:
  pinMode(ledPin, OUTPUT);

  // initialize the pushbutton pin as an input:
  for (int i = 0 ; i < 5 ; i++) {
    pinMode(buttonConfig[i].gpioNo, INPUT_PULLUP);
  }

  BLEDevice::init("MIDI-BUTTON5");

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEDevice::setEncryptionLevel((esp_ble_sec_act_t)ESP_LE_AUTH_REQ_SC_BOND);

  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(MIDI_SERVICE_UUID));
  
  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      BLEUUID(MIDI_CHARACTERISTIC_UUID),
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_WRITE_NR
                    );
  pCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  pServer->getAdvertising()->addServiceUUID(MIDI_SERVICE_UUID);
  pServer->getAdvertising()->start();

}

void loop() {
  int v;
  if (deviceConnected) {
    for (int i = 0 ; i < 5 ; i++) {
      if ((v = digitalRead(buttonConfig[i].gpioNo)) != buttonConfig[i].pinState) {
        /* new state is detected */
        buttonConfig[i].pinState = v;
        if (v == 0) { /* key down */
          digitalWrite(ledPin, HIGH);  // turn LED on
          midiPacket[2] = 0x90; // note on, channel 0
          midiPacket[3] = buttonConfig[i].noteNo;
          midiPacket[4] = 127;  // velocity
        } else { /* key up */
          digitalWrite(ledPin, LOW);   // turn LED off
          midiPacket[2] = 0x80; // note off, channel 0
          midiPacket[3] = buttonConfig[i].noteNo;
          midiPacket[4] = 0;    // velocity
        }
        pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
        pCharacteristic->notify();

        printf("%d %s\n", buttonConfig[i].noteNo, (v == 0) ? "ON": "OFF");
        delay(10);
      }
    } /* for */
  }
}
