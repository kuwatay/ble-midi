/*
    BLE-MIDI Matrix switch, 5x5 version
    2019/7/22 by morecat_lab
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer* pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define MIDI_SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

const int ledPin = 13; // led pin

#define DEBUG

#define NOTE_OFFSET 48 // start from C3

// Matrix switch control
#define SCAN_ROW_SIZE 5
#define SCAN_COL_SIZE 5
#define MAX_KEYS (SCAN_ROW_SIZE * SCAN_COL_SIZE)
#define SCAN_COUNT_TH 500  /* read key every SCAN_COUNT_TH loop */

/* Pin Assignment for LED/SW matrix */
int scanCol [SCAN_COL_SIZE] = {18, 19, 21, 22, 23};
int scanRow [SCAN_ROW_SIZE] = {15,  4, 16, 17,  5};

static uint8_t scan_col = 0;
static uint16_t scan_wait_count = 1;

/* key status */
static uint8_t keyCountButton[MAX_KEYS];
static uint8_t buttonStatus[MAX_KEYS];

/* matrix LED status */
static uint8_t matrixLedStatus[MAX_KEYS];


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

void matrixInit() {
  uint8_t i;
  for (i = 0 ; i < MAX_KEYS ; i++) {
    keyCountButton[i] = 0;
    buttonStatus[i] = 1;
#ifdef LED_TEST
    // for cheaker pattern
    uint8_t x = i / 8;
    uint8_t y = i % 8;
    matrixLedStatus[i] = (x + y) % 2;     /* LED on/off */
#else
    matrixLedStatus[i] = 0;     /* LED off */
#endif

  }
}

void matrixOut(uint8_t note, uint8_t state) {
  uint8_t no = note - NOTE_OFFSET;
  if ((no >= 0) && (no < MAX_KEYS)) {
    matrixLedStatus[no] = state;
  }
}

void scanMatrix() {

  if ((--scan_wait_count) == 0) {
    /* make pre-column to Hi-Z */
    uint8_t preCol = (scan_col == 0) ? (SCAN_COL_SIZE - 1) : (scan_col - 1);
    pinMode(scanCol[preCol], INPUT_PULLUP);

/* =============KEY INPUT ===================== */
    /* make all row-pin to Hi-Z */
    for (uint8_t row = 0 ; row < SCAN_ROW_SIZE ; row++) {
      pinMode(scanRow[row], INPUT_PULLUP);
    }
    /* change COL to low and read key status */
    digitalWrite(scanCol[scan_col], LOW);
    pinMode(scanCol[scan_col], OUTPUT);

    for (uint8_t row = 0 ; row < SCAN_ROW_SIZE ; row++) {
      uint8_t note = (scan_col * SCAN_ROW_SIZE) + row;
      ets_delay_us(9);
      uint8_t x = digitalRead(scanRow[row]);     /* READ PORT */
      /* bounce canceler */
      if (x != buttonStatus[note]) {
        if (++keyCountButton[note] > 1) { /* KEY change detected */
          if (x == 0) {
            // send note-on message
#ifdef DEBUG
            Serial.print("Key ON ");
            Serial.println( note+ NOTE_OFFSET );
#endif
            midiPacket[2] = 0x90;  // channel = 1
            midiPacket[3] = note + NOTE_OFFSET;
            midiPacket[4] = 127;
            pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
            pCharacteristic->notify();           
          } else {
            // send note-off message
#ifdef DEBUG
            Serial.print("Key OFF ");
            Serial.println( note+ NOTE_OFFSET );
#endif
            midiPacket[2] = 0x90; // channel = 1
            midiPacket[3] = note+ NOTE_OFFSET;
            midiPacket[4] = 0;
            pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
            pCharacteristic->notify();
          }
          keyCountButton[note] = 0;
          buttonStatus[note] = x;
        }
      } else {
        keyCountButton[note] = 0;
      }
    }

    /* change COL to Hi-Z */
    digitalWrite(scanCol[scan_col], HIGH);
    pinMode(scanCol[scan_col], INPUT_PULLUP);

    /* =============LED OCONTROL ===================== */
    for (uint8_t row = 0 ; row < SCAN_ROW_SIZE ; row++) {
      /* set row(j) to H(OUT) or L(OUT) */
      if (matrixLedStatus[(scan_col * SCAN_ROW_SIZE) + row] != 0) {
        /* LED ON */
        pinMode(scanRow[row], OUTPUT);
        digitalWrite(scanRow[row], LOW);
      } else {
        /* LED OFF */
        pinMode(scanRow[row], INPUT_PULLUP);
      }
    }

    /* set col(i) to H */
    pinMode(scanCol[scan_col], OUTPUT);
    digitalWrite(scanCol[scan_col], HIGH);

    /* set pointer to next column */
    scan_col ++;
    if (scan_col >= SCAN_COL_SIZE) {
      scan_col = 0;
    }

    /* set counter */
    scan_wait_count = SCAN_COUNT_TH; /* RESET COUNTER */
  }

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
              // set matrix value
              switch (midi_status & 0xf0) {
                case 0x80:  // note off
                  matrixOut(midi_data1, 0);
#ifdef DEBUG
                Serial.print("BLE: Note OFF ");
                Serial.println( midi_data1 );
#endif                 
                  break;
                case 0x90:  // note on
                  matrixOut(midi_data1, (midi_data2 == 0) ? 0 : 1);
#ifdef DEBUG
                if (midi_data2 == 0) {
                   Serial.print("BLE:Note OFF ");
                } else {
                   Serial.print("BLE:Note ON ");
                }
                Serial.println( midi_data1 );
#endif                  break;
              }

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


void setup() {
  
#ifdef DEBUG
  Serial.begin(115200);
#endif
  // initialize the LED pin as an output:
  pinMode(ledPin, OUTPUT);

  // Create the BLE Device
  BLEDevice::init("MIDI-MATRIX-5x5");

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
 
  // maintain matrix and read key
  scanMatrix();
  
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
