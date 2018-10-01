/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "BLEBeacon.h"
#include "BLEEddystoneTLM.h"
#include "BLEEddystoneURL.h"
#include "Settings_local.h"

BLEScan* pBLEScan;
int scanTime = 5; //In seconds
int waitTime = 15; //In seconds

uint16_t beconUUID = 0xFEAA;
#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00)>>8) + (((x)&0xFF)<<8))
#define base_topic "esp32" // No trailing slash

WiFiClient espClient;
PubSubClient client(espClient);

String getProximityUUIDString(BLEBeacon beacon) {
  std::string serviceData = beacon.getProximityUUID().toString().c_str();
  int serviceDataLength = serviceData.length();
  String returnedString = "";
  int i = serviceDataLength;
  while (i > 0)
  {
    if (serviceData[i-1] == '-') {
      i--;
    }
    char a = serviceData[i-1];
    char b = serviceData[i-2];
    returnedString += b;
    returnedString += a;
    
    i -= 2;
  }
  
  return returnedString;
}

float calculateDistance(int rssi, int txPower) {

  if (rssi == 0) {
      return -1.0;
  }

  if (!txPower) {
      // somewhat reasonable default value
      txPower = -59;
  }

  const float ratio = rssi * 1.0 / txPower;
  if (ratio < 1.0) {
      return pow(ratio, 10);
  } else {
      return (0.89976) * pow(ratio, 7.7095) + 0.111;
  }

}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", mqttUser, mqttPassword )) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  
    void onResult(BLEAdvertisedDevice advertisedDevice) {

      StaticJsonBuffer<500> JSONbuffer;
      JsonObject& JSONencoder = JSONbuffer.createObject();

      String mac_adress = advertisedDevice.getAddress().toString().c_str();
      mac_adress.replace(":","");
      mac_adress.toLowerCase();
      int rssi = advertisedDevice.getRSSI();
      
      JSONencoder["id"] = mac_adress;
      JSONencoder["rssi"] = rssi;

      if (advertisedDevice.haveName()){
        String nameBLE = advertisedDevice.getName().c_str();
        JSONencoder["name"] = nameBLE;
      }
      
      Serial.printf("\n\n");
      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
      std::string strServiceData = advertisedDevice.getServiceData();
       uint8_t cServiceData[100];
       strServiceData.copy((char *)cServiceData, strServiceData.length(), 0);

       if (advertisedDevice.getServiceDataUUID().equals(BLEUUID(beconUUID))==true) {  // found Eddystone UUID
        Serial.printf("is Eddystone: %d %s length %d\n", advertisedDevice.getServiceDataUUID().bitSize(), advertisedDevice.getServiceDataUUID().toString().c_str(),strServiceData.length());
        if (cServiceData[0]==0x10) {
           BLEEddystoneURL oBeacon = BLEEddystoneURL();
           oBeacon.setData(strServiceData);
           Serial.printf("Eddystone Frame Type (Eddystone-URL) ");
           Serial.printf(oBeacon.getDecodedURL().c_str());
           JSONencoder["url"] = oBeacon.getDecodedURL().c_str();
           
        } else if (cServiceData[0]==0x20) {
           BLEEddystoneTLM oBeacon = BLEEddystoneTLM();
           oBeacon.setData(strServiceData);
           Serial.printf("Eddystone Frame Type (Unencrypted Eddystone-TLM) \n");
           Serial.printf(oBeacon.toString().c_str());
        } else {
          for (int i=0;i<strServiceData.length();i++) {
            Serial.printf("[%X]",cServiceData[i]);
          }
        }
        Serial.printf("\n");

       } else {
        if (advertisedDevice.haveManufacturerData()==true) {
          std::string strManufacturerData = advertisedDevice.getManufacturerData();
          
          uint8_t cManufacturerData[100];
          strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length(), 0);
          
          if (strManufacturerData.length()==25 && cManufacturerData[0] == 0x4C  && cManufacturerData[1] == 0x00 ) {
            BLEBeacon oBeacon = BLEBeacon();
            oBeacon.setData(strManufacturerData);
            
            String proximityUUID = getProximityUUIDString(oBeacon);
            
            Serial.printf("iBeacon Frame\n");
            Serial.printf("ID: %04X Major: %d Minor: %d UUID: %s Power: %d\n",oBeacon.getManufacturerId(),ENDIAN_CHANGE_U16(oBeacon.getMajor()),ENDIAN_CHANGE_U16(oBeacon.getMinor()),proximityUUID.c_str(),oBeacon.getSignalPower());

            float distance = calculateDistance(rssi, oBeacon.getSignalPower());
            Serial.print("RSSI: ");
            Serial.print(rssi);
            Serial.print("\txPower: ");
            Serial.print(oBeacon.getSignalPower());
            Serial.print("\tDistance: ");
            Serial.println(distance);

            int major = ENDIAN_CHANGE_U16(oBeacon.getMajor());
            int minor = ENDIAN_CHANGE_U16(oBeacon.getMinor());

            JSONencoder["major"] = major;
            JSONencoder["minor"] = minor;

            JSONencoder["uuid"] = proximityUUID;
            JSONencoder["id"] = proximityUUID + "-" + String(major) + "-0";
            JSONencoder["txPower"] = oBeacon.getSignalPower();
            JSONencoder["distance"] = distance;
            
          } else {

            if (advertisedDevice.haveTXPower()) {
              float distance = calculateDistance(rssi, advertisedDevice.getTXPower());
              JSONencoder["distance"] = distance;
            } else {
              float distance = calculateDistance(rssi, -59);
              JSONencoder["distance"] = distance;
            }

            Serial.printf("strManufacturerData: %d \n",strManufacturerData.length());
            // TODO: parse manufacturer data
//            for (int i=0;i<strManufacturerData.length();i++) {
//              Serial.printf("[%X]",cManufacturerData[i]);
//            }
//            Serial.printf("\n");
          }
         } else {

          if (advertisedDevice.haveTXPower()) {
            float distance = calculateDistance(rssi, advertisedDevice.getTXPower());
            JSONencoder["distance"] = distance;
          } else {
            float distance = calculateDistance(rssi, -59);
            JSONencoder["distance"] = distance;
          }
                    
          Serial.printf("no Beacon Advertised ServiceDataUUID: %d %s \n", advertisedDevice.getServiceDataUUID().bitSize(), advertisedDevice.getServiceDataUUID().toString().c_str());
         }
        }

        char JSONmessageBuffer[500];
        JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
      
        String publishTopic = String(base_topic) + "/" + room;
        
        if (client.publish(publishTopic.c_str(), JSONmessageBuffer) == true) {
      //    Serial.print("Success sending message to topic: "); Serial.println(publishTopic);
      //    Serial.print("Message: "); Serial.println(JSONmessageBuffer);
        } else {
          Serial.print("Error sending message: "); Serial.println(publishTopic);
          Serial.print("Message: "); Serial.println(JSONmessageBuffer);
        }
    }
};

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi..");
  }

  Serial.println("Connected to the WiFi network");

  client.setServer(mqttServer, mqttPort);

  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");

    if (client.connect("ESP32Client", mqttUser, mqttPassword )) {

      Serial.println("connected");

    } else {

      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }

  if (client.publish(base_topic, "Hello from ESP32") == true) { //TODO base_topic + mac_address
    Serial.println("Success sending message to topic");
  } else {
    Serial.println("Error sending message");
  }
  
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
}

unsigned long last = 0;

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - last > (waitTime * 1000)) {
    Serial.println("Scanning...");
    BLEScanResults foundDevices = pBLEScan->start(scanTime);
    Serial.printf("\nScan done! Devices found: %d\n",foundDevices.getCount());
    last = millis();
  }
  delay(1);
}