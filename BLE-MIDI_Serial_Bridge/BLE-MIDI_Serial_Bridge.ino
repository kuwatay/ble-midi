/*
    BLE-MIDI <-> Serial-MIDI Bridge
    2019/7/17 by morecat_lab
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <MIDI.h>

MIDI_CREATE_DEFAULT_INSTANCE();

BLEServer* pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define MIDI_SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

const int ledPin = 4; // monitor led pin

// for send BLE midi packet
uint8_t midiPacket[] = {
   0x80,  // timestampHigh (not implemented)
   0x80,  // timestampLow ( not implemented)
   0x00,  // status
   0x3c,  // 0x3c == 60 == middle c
   0x00   // velocity
};

// for send SERIAL midi
//uint8_t serialData[3];  // only deal with 3byte data


byte midi_status, midi_data1, midi_data2;

void serial_send() {
  MIDI.send(midi::MidiType(midi_status & 0xf0), 
            midi_data1, 
            midi_data2,
            (midi_status & 0xf) +1  // channel
           ); 
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};


class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
         int state = 0;
         digitalWrite(ledPin, HIGH);  // turn LED on
        // parse BLE-MIDI Message
        for (int i = 0; i < rxValue.length(); i++) {
          switch (state) {
            case 0:  // read timestampHigh
              state = 1; // ignore  (timestamp is not implemented)
              break;
              
            case 1:  // read timestampLow
              if ((rxValue[i] & 0x80) == 0) {  //SysEx continue packet
                state = 10;
              } else {  // normal packet
                state = 2;  // ignore  (timestamp is not implemented)
              }
              break;  
              
            case 2:  // read statuas
              state = 3;
              midi_status = rxValue[i];
              if ((midi_status & 0xf0) == 0xff) {
                if (midi_status == 0xf0) {  // SysEx 
                  state = 10;
                } else if (midi_status == 0xf8) { // Real-time message
                  state = 0; // just ignore it
                }
              }
              break;
              
            case 3:  // read data1
              state = 4;
              midi_data1 =  rxValue[i];
              break;
              
            case 4:  // read data2
              state = 5;
              midi_data2 =  rxValue[i];
              // send midi data to serial
              serial_send();
              break;
              
            case 5:  // after 1st midi message ..
              if ((rxValue[i] & 0x80) != 0) { // we get timestampLow
                state = 2;
              } else {  // running status message
                midi_data1 = rxValue[i];
                state = 4;
              }
              break;

            case 10: // SysEx receiving
             if ((rxValue[i] & 0x80) != 0) { // we get timestampLow
                state = 11;
             } else {
              // ignore sysEx message, just now
             }
             break;
             
           case 11:
            if (rxValue[i] == 0xf7) { // we get end of SysEx
                state = 0;  
            }
            break;
          } // switch
        }  // for

        digitalWrite(ledPin, LOW);  // turn LED off
      }
    }
};

// Serial MIDI handler (Serial-MIDI -> BLE-MIDI)
void handleNoteOn(byte channel, byte pitch, byte velocity) {
  midiPacket[2] = 0x90; // note on, channel 0
  midiPacket[3] = pitch;
  midiPacket[4] = velocity;  // velocity
  
  pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
  pCharacteristic->notify();
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  midiPacket[2] = 0x80; // note off, channel 0
  midiPacket[3] = pitch;
  midiPacket[4] = velocity;  // velocity
  
  pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
  pCharacteristic->notify();  
}

void setup() {

  // initialize the LED pin as an output:
  pinMode(ledPin, OUTPUT);

  // setup MIDI
  MIDI.setHandleNoteOn(handleNoteOn);  
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.begin(MIDI_CHANNEL_OMNI);

  // Create the BLE Device
  BLEDevice::init("MIDI-BRIDGE");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEDevice::setEncryptionLevel((esp_ble_sec_act_t)ESP_LE_AUTH_REQ_SC_BOND);

  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(MIDI_SERVICE_UUID));
  
  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      BLEUUID(MIDI_CHARACTERISTIC_UUID),
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE_NR  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
                    
  pCharacteristic->setCallbacks(new MyCallbacks());

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
//  Serial.println("start advertising");

}

void loop() {
  // read Serial MIDI data
  MIDI.read();
  
  // notify changed value
  if (deviceConnected) {
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
      // do stuff here on connecting
      oldDeviceConnected = deviceConnected;
  }
}
