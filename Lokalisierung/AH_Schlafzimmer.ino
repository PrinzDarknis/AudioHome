/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/

//BLE
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAddress.h>

#define MQTT_TOPIC "Lokalisierung/Schlafzimmer/"
#define ZIMMER "Schlafzimmer"

//WiFi & MQTT
#include <WiFi.h>
#include <PubSubClient.h>

//WiFi
const String server = "192.168.178.49"; 
const String ssid = "FRITZ!Box 7560 XZ";
const String password = "55878645949116258495";

//BLE
int scanTime = 1; //In sekunden
BLEAddress adresse("00:00:00:00:00:00");
BLEScan* pBLEScan;
int RSSI;

//MQTT
char rssi[4];
char room[50];

boolean success = false;

void callback(char* topic, byte* payload, unsigned int length);

WiFiClient wclient;
PubSubClient mqtt(server.c_str(), 1883, callback, wclient);


void callback(char* topic, byte* payload, unsigned int length) {
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.

  // Allocate the correct amount of memory for the payload copy
  byte* p = (byte*)malloc(length);
  // Copy the payload to the new buffer
  memcpy(p,payload,length);
  mqtt.publish("Callback", p, length);
  // Free the memory
  free(p);
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      //Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
      String name = advertisedDevice.getName().c_str();
      Serial.printf("onResult  name: %s \n", name);
      adresse = advertisedDevice.getAddress();
      Serial.printf("onResult  address: %s \n", adresse.toString().c_str());

      if (advertisedDevice.haveTXPower()){
        Serial.printf("onResult  TXPower vorhanden");
      }
      
      unsigned int TX = advertisedDevice.getTXPower();
      Serial.printf("onResult  TX Power: %d \n", TX);
         
      RSSI = advertisedDevice.getRSSI();   //kleine Werte --> weit weg  -80 ~ ausser Raum
      Serial.printf("onResult  RSSI: %d \n", RSSI);
     
    }
};

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  int timout = 0;
  Serial.print("...");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print("O");
    timout++;
    if  (timout > 20) // Wenn Anmeldung nicht möglich
    {
      Serial.println("");
      Serial.println("...Wlan verbindung fehlt");
      ESP.restart(); // ESP32 neu starten
    }
  }
  Serial.println("");
  Serial.print("...IP Addresse: ");
  Serial.println(WiFi.localIP());
}

void setup_ble()  {
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
}

void setup() {
  Serial.begin(115200);
  Serial.println("START_SETUP...");
  
  Serial.println("...WiFi start");
  setup_wifi();
  Serial.println("...WiFi connected");
  
  Serial.println("...BLE start");
  setup_ble();
  Serial.println("...BLE initialized");
  
  Serial.println("END_SETUP...");
}

void publish_mqtt(const char room[], const char msg[]) {
    success = false;
    Serial.println("send data via MQTT");
    if (mqtt.connect(ZIMMER)) {
    Serial.println("connected");
    Serial.println(room);
    Serial.println(msg);
    success = mqtt.publish(room, msg);
    if (success == true)  {
      Serial.println("successful published");
    }
    else Serial.println("failure in mqtt.publish()");
    }  
    mqtt.loop();
    delay(200);
    mqtt.disconnect();
    Serial.println("disconnected");
}

void loop() {
  Serial.println("-----------------------------------------");
  Serial.println("start BLE scan");
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);   //scannt 5 sek nach BLE Signalen
  Serial.print("BLE scan done - devices found: ");
  Serial.println(foundDevices.getCount());
    
  strcpy(room, MQTT_TOPIC);
  strcat(room, adresse.toString().c_str());
  Serial.println(room);
  sprintf(rssi, "%d", RSSI);
  Serial.println(rssi);

  publish_mqtt(room, rssi);
  
  pBLEScan->clearResults();   // lösche Ergebnisse von BLEScan -> Speicher freigeben
  delay(1000);        //Warte 1 sek bevor erneuter Scan
}
