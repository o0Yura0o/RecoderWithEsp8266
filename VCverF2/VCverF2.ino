#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <FS.h>
#include <LittleFS.h>
#include "src/AudioTimer.h"

const char* ssid = "SOC-AX150-AP2.4";
const char* password = "1004SOClab";
const char* serverIP = "140.120.14.42"; // Django server IP
const uint16_t serverPort = 8000;       // Django server port (default: 8000)

const char* filePath = "/data.bin";
const int buffSize = 1024;
const int SampleRate = 16000;

volatile boolean buffFull[2] = {false,false}, whichBuff = false, a;
volatile byte buffer[2][buffSize];
volatile int buffCount = 0;
uint8_t buffer2[buffSize];

unsigned long dataSize = 0, pre = 0;
uint16_t pcmValue = 0;
int touchSensor = 12;
int sensorValue = 0;

File f;
AudioTimer* timer = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  pinMode(A0, INPUT);
  pinMode(touchSensor, INPUT);
  setupFS(LittleFS);
}

void loop() {
  // put your main code here, to run repeatedly:

  sensorValue = digitalRead(touchSensor);
  if(sensorValue){
    startR();
    
    while(sensorValue){
      yield();  //avoid soft wdt reboot
      if(buffFull[whichBuff] && buffFull[!whichBuff]) Serial.println("data loss");
      if(buffFull[!whichBuff]){
        a = !whichBuff;
        if (!f) {
          Serial.printf("Unable to open file for writing, aborting\n");
          return;
        }
        timer->stop();
        f.write((byte*)buffer[a], buffSize);
        timer->start();
        
        buffFull[a] = false;
        
      }
      sensorValue = digitalRead(touchSensor);
    }
    stopR();
    
    Serial.println("Listing files...");
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
      Serial.print("File: ");
      Serial.print(dir.fileName());
      Serial.print(" | Size: ");
      dataSize = dir.fileSize();
      Serial.println(dataSize);
    }
    sendData();
  }
}

void setupFS(FS fs) {
  if (!fs.format()) {
    Serial.printf("Unable to format(), aborting\n");
    return;
  }
  if (!fs.begin()) {
    Serial.printf("Unable to begin(), aborting\n");
    return;
  }
  Serial.println("FS setup success");
}

void IRAM_ATTR sampleAndBuffer() {
  system_adc_read_fast(&pcmValue, 1, 8);
  
  buffer[whichBuff][buffCount++] = (pcmValue>>2) & 0xFF;
  if(buffCount >= buffSize){
    buffCount = 0;
    buffFull[whichBuff] = true;
    whichBuff = !whichBuff;
    
  }
}

void startR() {
  if (LittleFS.exists(filePath)) {
    LittleFS.remove(filePath);
    Serial.println("Existing file removed!");
  }
  f = LittleFS.open(filePath, "w");
  if (!f) {
    Serial.printf("Unable to open file for writing, aborting\n");
    return;
  }
  if(!timer) {
    timer = new AudioTimer();
    timer->setup(SampleRate, sampleAndBuffer);  //setup(tick, ISR_func)
  }
  Serial.println("Start Recording");
  pre = micros();
	timer->start();
}

void stopR() {
  timer->stop();
  pre = micros()-pre;
  Serial.println("Time stop");
  f.write((byte*)buffer[whichBuff], buffCount);
  f.close();
}

void sendData() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  WiFiClient client;
  if (client.connect(serverIP, serverPort)){
    client.println("POST /upload_pcm HTTP/1.1");
    client.println("Host: " + String(serverIP));
    client.println("Content-Type: application/octet-stream");
    client.print("Content-Length: ");
    client.println(dataSize);
    client.print("Batch-Time: ");  // Add the sample rate as a custom header
    client.println(pre);
    client.println();
    f = LittleFS.open(filePath, "r");
    for(int i=0;i<(dataSize/buffSize);i++){
      f.read(buffer2, buffSize);
      client.write(buffer2, buffSize);
    }
    client.stop();
  }
  Serial.println("All Done!!!");
}