#pragma once

#define WLED_USE_TM1637
#define PIN_MOTOR 12

#ifndef USED_STORAGE_FILESYSTEMS
  #ifdef WLED_USE_SD_SPI
    #define USED_STORAGE_FILESYSTEMS "SD SPI, LittleFS"
  #else
    #define USED_STORAGE_FILESYSTEMS "SD MMC, LittleFS"
  #endif
#endif

#ifndef SD_ADAPTER
  #if defined(WLED_USE_SD) || defined(WLED_USE_SD_SPI)
    #ifdef WLED_USE_SD_SPI
      #ifndef WLED_USE_SD
        #define WLED_USE_SD
      #endif
      #ifndef WLED_PIN_SCK
        #define WLED_PIN_SCK SCK
      #endif
      #ifndef WLED_PIN_MISO
        #define WLED_PIN_MISO MISO
      #endif
      #ifndef WLED_PIN_MOSI
        #define WLED_PIN_MOSI MOSI
      #endif
      #ifndef WLED_PIN_SS
        #define WLED_PIN_SS SS
      #endif
      #define SD_ADAPTER SD
    #else
      #define SD_ADAPTER SD_MMC
    #endif
  #endif
#endif

#ifdef WLED_USE_SD_SPI
  #ifndef SPI_PORT_DEFINED
    //inline SPIClass spiPort = SPIClass(VSPI);
    SPIClass spiPort = SPIClass(VSPI);
    #define SPI_PORT_DEFINED
  #endif
#endif

#include "wled.h"
#include "../usermods/FSEQ/fseq_player.h"
#include "../usermods/FSEQ/fseq_player.cpp"
#include "../usermods/FSEQ/sd_manager.h"
#include "../usermods/FSEQ/sd_manager.cpp"
#include "../usermods/FSEQ/web_ui_manager.h"
#include "../usermods/FSEQ/web_ui_manager.cpp"
#include "TM1637TinyDisplay.h"

// Usermod for FSEQ playback with UDP and web UI support
class UsermodFseq : public Usermod {
private:
  WebUIManager webUI; // Web UI Manager module (handles endpoints)
  static const char _name[]; // for storing usermod name in config
  TM1637TinyDisplay *display;
  uint8_t mIsPlaying = false;
  uint8_t err = false;

public:
  // Setup function called once at startup
  void setup() {
    DEBUG_PRINTF("[%s] Usermod loaded\n", FPSTR(_name));

    display = new TM1637TinyDisplay(27, 26);
    display->begin();
    display->setBrightness(BRIGHT_HIGH);
    display->clear();
    
    
    // Initialize SD card using SDManager
    SDManager sd;
    if (!sd.begin()) {
      DEBUG_PRINTF("[%s] SD initialization FAILED.\n", FPSTR(_name));
      display->showString("Erro");
      err = true;
    } else {
      DEBUG_PRINTF("[%s] SD initialization successful.\n", FPSTR(_name));
      display->showString("OLA");
    }
    
    // Register web endpoints defined in WebUIManager
    webUI.registerEndpoints();

    // Initial strip things
    strip.fill(0);
    strip.setTransition(0);
    strip.show();

    // Motor
    pinMode(PIN_MOTOR, OUTPUT);
    digitalWrite(PIN_MOTOR, HIGH);  // 4n25 is pulled high. (motor is ON when this pin is LOW)
  }
  
  // Loop function called continuously
  void loop() {
    // Process FSEQ playback (includes UDP sync commands)
    FSEQPlayer::handlePlayRecording();
    if(FSEQPlayer::isPlaying() != mIsPlaying){
      mIsPlaying = FSEQPlayer::isPlaying();
      if(mIsPlaying)
        display->showString("P", 1, 0, 0b10000000);
      else
        display->showString("S", 1, 0, 0b10000000);
    }
  }
  
  // Unique ID for the usermod
  uint16_t getId() override {
    return USERMOD_ID_SD_CARD;
  }

  // Add a link in the Info tab to your SD UI
  void addToJsonInfo(JsonObject &root) override {
    JsonObject user = root["u"];
    if (user.isNull()) {
      user = root.createNestedObject("u");
    }
    // Create an array with two items: label and value
    JsonArray arr = user.createNestedArray("Usermod FSEQ UI");
    
    String ip = WiFi.localIP().toString();
    arr.add("http://" + ip + "/fsequi"); // value
  }

  // Save your SPI pins to WLED config JSON
  void addToConfig(JsonObject &root) override {
  #ifdef WLED_USE_SD_SPI
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top["csPin"]  = configPinSourceSelect;
    top["sckPin"] = configPinSourceClock;
    top["misoPin"] = configPinPoci;
    top["mosiPin"] = configPinPico;
  #endif
  }

  // Read your SPI pins from WLED config JSON
  bool readFromConfig(JsonObject &root) override {
  #ifdef WLED_USE_SD_SPI
    JsonObject top = root[FPSTR(_name)];
    if (top.isNull()) return false;

    if (top["csPin"].is<int>())   configPinSourceSelect = top["csPin"].as<int>();
    if (top["sckPin"].is<int>())  configPinSourceClock  = top["sckPin"].as<int>();
    if (top["misoPin"].is<int>()) configPinPoci         = top["misoPin"].as<int>();
    if (top["mosiPin"].is<int>()) configPinPico         = top["mosiPin"].as<int>();

    reinit_SD_SPI(); // reinitialize SD with new pins
    return true;
  #else
    return false;
  #endif
  }

#ifdef WLED_USE_SD_SPI
  // Reinitialize SD SPI with updated pins
  void reinit_SD_SPI() {
    // Deinit SD if needed
    SD_ADAPTER.end();
    // Reallocate pins
    PinManager::deallocatePin(configPinSourceSelect, PinOwner::UM_SdCard);
    PinManager::deallocatePin(configPinSourceClock,  PinOwner::UM_SdCard);
    PinManager::deallocatePin(configPinPoci,         PinOwner::UM_SdCard);
    PinManager::deallocatePin(configPinPico,         PinOwner::UM_SdCard);

    PinManagerPinType pins[4] = {
      { configPinSourceSelect, true },
      { configPinSourceClock,  true },
      { configPinPoci,         false },
      { configPinPico,         true }
    };
    if (!PinManager::allocateMultiplePins(pins, 4, PinOwner::UM_SdCard)) {
      DEBUG_PRINTF("[%s] SPI pin allocation failed!\n", FPSTR(_name));
      return;
    }

    // Reinit SPI with new pins
    spiPort.begin(configPinSourceClock, configPinPoci, configPinPico, configPinSourceSelect);

    // Try to begin SD again
    if (!SD_ADAPTER.begin(configPinSourceSelect, spiPort)) {
      DEBUG_PRINTF("[%s] SPI begin failed!\n", FPSTR(_name));
    } else {
      DEBUG_PRINTF("[%s] SD SPI reinitialized with new pins\n", FPSTR(_name));
    }
  }

  // Getter methods and static variables for SD pins
  static int8_t getCsPin()   { return configPinSourceSelect; }
  static int8_t getSckPin()  { return configPinSourceClock; }
  static int8_t getMisoPin() { return configPinPoci; }
  static int8_t getMosiPin() { return configPinPico; }

  static int8_t configPinSourceSelect;
  static int8_t configPinSourceClock;
  static int8_t configPinPoci;
  static int8_t configPinPico;
#endif

bool handleButton(uint8_t b) override {
  yield();
  if (
       buttonType[b] == BTN_TYPE_NONE
    || buttonType[b] == BTN_TYPE_RESERVED
    || buttonType[b] == BTN_TYPE_PIR_SENSOR
    || buttonType[b] == BTN_TYPE_ANALOG
    || buttonType[b] == BTN_TYPE_ANALOG_INVERTED) {
    return false;
  }

  // Get first moment only
  if (isButtonPressed(b) && !buttonPressedBefore[b]) {
    buttonPressedBefore[b] = true;
    buttonPressedTime[b] = millis();
    return true;
  }
  else if(!isButtonPressed(b) && buttonPressedBefore[b] // released
      && (millis() - buttonPressedTime[b] > 200)) {     // debounce
    static char fileIndex = '0';
    
    if(b == 0) togglePlay(fileIndex);
    else if(b == 1) playNextFile(fileIndex);
    else if(b == 2) playPrevFile(fileIndex);
    
    buttonPressedBefore[b] = false;
    return false;
  }
  return true;
}

void togglePlay(char& fileIndex){
  if(FSEQPlayer::isPlaying()){
    FSEQPlayer::hardStop();
    DEBUG_PRINTF(">>>>> Stop\n");
    display->clear();
    display->showString("S", 1, 0, 0b10000000);
    display->showNumber(fileIndex - 48, 1, 2, 2);
    // Motor
    DEBUG_PRINTF(">>>>> Toggle Motor HIGH");
    digitalWrite(PIN_MOTOR, HIGH);
  }
  else {
    if(fileIndex == '0')
      fileIndex = '1';
    const String fileName = "/cue" + String(fileIndex) + ".fseq";
    const char* fileNameC = fileName.c_str();
    if(SD_ADAPTER.exists(fileNameC))
      FSEQPlayer::loadRecording(fileNameC, 0, uint16_t(-1), 0.0f); // 1.0f for looping
    
    DEBUG_PRINTF(">>>>> Play cue%c.fseq\n", fileIndex);
    display->clear();
    display->showString("P", 1, 0, 0b10000000);
    display->showNumber(fileIndex - 48, 1, 2, 2);
    // Motor
    
    if(fileIndex != '2'){
      DEBUG_PRINTF(">>>>> Toggle Motor LOW");
      digitalWrite(PIN_MOTOR, LOW);
    }
  }
}

void playNextFile(char& fileIndex){
  fileIndex++;
  const String fileName = "/cue" + String(fileIndex) + ".fseq";
  const char* fileNameC = fileName.c_str();
  
  if(!SD_ADAPTER.exists(fileNameC)) {
    fileIndex--;
    return;
  }
  
  // Play it
  FSEQPlayer::loadRecording(fileNameC, 0, uint16_t(-1), 0.0f); // 1.0f for looping
  DEBUG_PRINTF(">>>>> Playing next: cue%c.fseq\n", fileIndex);
  display->clear();
  display->showString("P", 1, 0, 0b10000000);
  display->showNumber(fileIndex - 48, 1, 2, 2);
  
  // Motor
  if(fileIndex == '2'){
    DEBUG_PRINTF(">>>>> Next Motor HIGH");
    digitalWrite(PIN_MOTOR, HIGH);
  }
  else{
    DEBUG_PRINTF(">>>>> Next Motor LOW");
    digitalWrite(PIN_MOTOR, LOW);
  }
  
  return;
}

void playPrevFile(char& fileIndex){
  if(fileIndex == '1')
    return;

  fileIndex--;
  const String fileName = "/cue" + String(fileIndex) + ".fseq";
  const char* fileNameC = fileName.c_str();

  if(!SD_ADAPTER.exists(fileNameC)) {
    fileIndex++;
    return;
  }
  
  // Play it
  FSEQPlayer::loadRecording(fileNameC, 0, uint16_t(-1), 0.0f); // 1.0f for looping
  DEBUG_PRINTF(">>>>> Playing prev: cue%c.fseq\n", fileIndex);
  
  display->clear();
  display->showString("P", 1, 0, 0b10000000);
  display->showNumber(fileIndex - 48, 1, 2, 2);
  
  // Motor
  if(fileIndex == '2'){
    DEBUG_PRINTF(">>>>> Prev Motor HIGH");
    digitalWrite(PIN_MOTOR, HIGH);
  }
  else{
    DEBUG_PRINTF(">>>>> Prev Motor LOW");
    digitalWrite(PIN_MOTOR, LOW);
  }
  return;
}

};



// Provide a usermod name for config storage
const char UsermodFseq::_name[] PROGMEM = "usermod FSEQ sd card";

#ifdef WLED_USE_SD_SPI
int8_t UsermodFseq::configPinSourceSelect = 19;
int8_t UsermodFseq::configPinSourceClock  = 4;
int8_t UsermodFseq::configPinPoci         = 5;
int8_t UsermodFseq::configPinPico         = 18;
#endif