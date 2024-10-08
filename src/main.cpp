#include "core/globals.h"

#include <EEPROM.h>
#include <iostream>
#include <functional>
#include <vector>
#include <string>
#include "esp32-hal-psram.h"

// Public Globals Variables
unsigned long previousMillis = millis();
int prog_handler;    // 0 - Flash, 1 - LittleFS, 3 - Download
int rotation;
int IrTx;
int IrRx;
int RfTx;
int RfRx;
int dimmerSet;
int bright=100;
int tmz=3;
bool sdcardMounted = false;
bool gpsConnected = false;
bool wifiConnected = false;
bool BLEConnected = false;
bool returnToMenu;
bool isSleeping = false;
bool isScreenOff = false;
bool dimmer = false;
char timeStr[10];
time_t localTime;
struct tm* timeInfo;
#if defined(HAS_RTC)
  cplus_RTC _rtc;
  bool clock_set = true;
#else
  ESP32Time rtc;
  bool clock_set = false;
#endif
JsonDocument settings;

String wui_usr="admin";
String wui_pwd="bruce";
String ssid;
String pwd;
std::vector<std::pair<std::string, std::function<void()>>> options;
const int bufSize = 4096;
uint8_t buff[4096] = {0};
// Protected global variables
#if defined(HAS_SCREEN)
	TFT_eSPI tft = TFT_eSPI();         // Invoke custom library
	TFT_eSprite sprite = TFT_eSprite(&tft);
	TFT_eSprite draw = TFT_eSprite(&tft);
#else
    SerialDisplayClass tft;
    SerialDisplayClass& sprite = tft;
    SerialDisplayClass& draw = tft;
#endif

#if defined(CARDPUTER)
  Keyboard_Class Keyboard = Keyboard_Class();
#elif defined (STICK_C_PLUS)
  AXP192 axp192;
#endif

#include "Wire.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include "core/main_menu.h"
#include "core/serialcmds.h"
#include "modules/others/audio.h"


/*********************************************************************
**  Function: setup_gpio
**  Setup GPIO pins
*********************************************************************/
void setup_gpio() {
  #if  defined(STICK_C_PLUS2)
    pinMode(UP_BTN, INPUT);   // Sets the power btn as an INPUT
    pinMode(SEL_BTN, INPUT);
    pinMode(DW_BTN, INPUT);
    pinMode(4, OUTPUT);     // Keeps the Stick alive after take off the USB cable
    digitalWrite(4,HIGH);   // Keeps the Stick alive after take off the USB cable    
  #elif defined(STICK_C_PLUS)
    pinMode(SEL_BTN, INPUT);
    pinMode(DW_BTN, INPUT);
    axp192.begin();           // Start the energy management of AXP192
  #elif defined(CARDPUTER)
    Keyboard.begin();
    pinMode(0, INPUT);
    pinMode(10, INPUT);     // Pin that reads the
  #elif ! defined(HAS_SCREEN)
    // do nothing
  #elif defined(M5STACK) // init must be done after tft, to make SDCard work
    // do nothing
  #else
    pinMode(UP_BTN, INPUT);   // Sets the power btn as an INPUT
    pinMode(SEL_BTN, INPUT);
    pinMode(DW_BTN, INPUT);
  #endif

  #if defined(BACKLIGHT)
  pinMode(BACKLIGHT, OUTPUT);
  #endif
}


/*********************************************************************
**  Function: begin_tft
**  Config tft
*********************************************************************/
void begin_tft(){
#if defined(HAS_SCREEN)
  tft.init();
#else
  tft.begin(); //115200, 240,320);
  tft.clear();
#endif
  rotation = gsetRotation();
  tft.setRotation(rotation);
  resetTftDisplay();
}


/*********************************************************************
**  Function: boot_screen
**  Draw boot screen
*********************************************************************/
void boot_screen() {
  tft.setTextColor(FGCOLOR, TFT_BLACK);
  tft.setTextSize(FM);
  tft.drawCentreString("Bruce", WIDTH / 2, 10, SMOOTH_FONT);
  tft.setTextSize(FP);
  tft.drawCentreString(BRUCE_VERSION, WIDTH / 2, 25, SMOOTH_FONT);
  tft.setTextSize(FM);

  int i = millis();
  while(millis()<i+7000) { // boot image lasts for 5 secs
    if((millis()-i>2000) && (millis()-i)<2200) tft.fillScreen(TFT_BLACK);
    if((millis()-i>2200) && (millis()-i)<2700) tft.drawRect(160,50,2,2,FGCOLOR);
    if((millis()-i>2700) && (millis()-i)<2900) tft.fillScreen(TFT_BLACK);
    if((millis()-i>2900) && (millis()-i)<3400) tft.drawXBitmap(130,45,bruce_small_bits, bruce_small_width, bruce_small_height,TFT_BLACK,FGCOLOR);
    if((millis()-i>3400) && (millis()-i)<3600) tft.fillScreen(TFT_BLACK);
    if((millis()-i>3600)) tft.drawXBitmap(1,1,bits, bits_width, bits_height,TFT_BLACK,FGCOLOR);

    if(checkAnyKeyPress())  // If any key or M5 key is pressed, it'll jump the boot screen
    {
      tft.fillScreen(TFT_BLACK);
      delay(10);
      return;
    }
  }

#if defined(BUZZ_PIN)
  // Bip M5 just because it can. Does not bip if splashscreen is bypassed
  _tone(5000, 50);
  delay(200);
  _tone(5000, 50);
/*  2fix: menu infinite loop */
#elif defined(HAS_NS4168_SPKR)
  // play a boot sound
  if(SD.exists("/boot.wav")) playAudioFile(&SD, "/boot.wav");
  else if(LittleFS.exists("/boot.wav")) playAudioFile(&LittleFS, "/boot.wav");
  setup_gpio(); // temp fix for menu inf. loop
#endif
}


/*********************************************************************
**  Function: load_eeprom
**  Load EEPROM data
*********************************************************************/
void load_eeprom() {
  EEPROM.begin(EEPROMSIZE); // open eeprom

  rotation = EEPROM.read(0);
  dimmerSet = EEPROM.read(1);
  bright = EEPROM.read(2);
  IrTx = EEPROM.read(6);
  IrRx = EEPROM.read(7);
  RfTx = EEPROM.read(8);
  RfRx = EEPROM.read(9);
  tmz = EEPROM.read(10);
  FGCOLOR = EEPROM.read(11) << 8 | EEPROM.read(12);

  log_i("\
  \n*-*EEPROM Settings*-* \
  \n- rotation  =%03d, \
  \n- dimmerSet =%03d, \
  \n- Brightness=%03d, \
  \n- IR Tx Pin =%03d, \
  \n- IR Rx Pin =%03d, \
  \n- RF Tx Pin =%03d, \
  \n- RF Rx Pin =%03d, \
  \n- Time Zone =%03d, \
  \n- FGColor   =0x%04X \
  \n*-*-*-*-*-*-*-*-*-*-*", rotation, dimmerSet, bright,IrTx, IrRx, RfTx, RfRx, tmz, FGCOLOR);
  if (rotation>3 || dimmerSet>60 || bright>100 || IrTx>100 || IrRx>100 || RfRx>100 || RfTx>100 || tmz>24) {
    rotation = ROTATION;
    dimmerSet=10;
    bright=100;
    IrTx=LED;
    IrRx=GROVE_SCL;
    RfTx=GROVE_SDA;
    RfRx=GROVE_SCL;
    FGCOLOR=0xA80F;
    tmz=0;

    EEPROM.write(0, rotation);
    EEPROM.write(1, dimmerSet);
    EEPROM.write(2, bright);
    EEPROM.write(6, IrTx);
    EEPROM.write(7, IrRx);
    EEPROM.write(8, RfTx);
    EEPROM.write(9, RfRx);
    EEPROM.write(10, tmz);
    EEPROM.write(11, int((FGCOLOR >> 8) & 0x00FF));
    EEPROM.write(12, int(FGCOLOR & 0x00FF));
    EEPROM.writeString(20,"");

    EEPROM.commit();      // Store data to EEPROM
    EEPROM.end();
    log_w("One of the eeprom values is invalid");
  }
  setBrightness(bright,false);
  EEPROM.end();
}

/*********************************************************************
**  Function: init_clock
**  Clock initialisation for propper display in menu
*********************************************************************/
void init_clock() {
  #if defined(HAS_RTC)
    RTC_TimeTypeDef _time;
    cplus_RTC _rtc;
    _rtc.begin();
    _rtc.GetBm8563Time();
    _rtc.GetTime(&_time);
  #endif
}

/*********************************************************************
**  Function: setup
**  Where the devices are started and variables set
*********************************************************************/
void setup() {
  Serial.begin(115200);

  log_d("Total heap: %d", ESP.getHeapSize());
  log_d("Free heap: %d", ESP.getFreeHeap());
  if(psramInit()) log_d("PSRAM Started");
  if(psramFound()) log_d("PSRAM Found");
  else log_d("PSRAM Not Found");
  log_d("Total PSRAM: %d", ESP.getPsramSize());
  log_d("Free PSRAM: %d", ESP.getFreePsram());

  // declare variables
  prog_handler=0;
  sdcardMounted=false;
  wifiConnected=false;
  BLEConnected=false;

  setup_gpio();
  begin_tft();
  #if defined(M5STACK)
  M5.begin(); // Begin after TFT, for SDCard to work
  #endif
  load_eeprom();
  init_clock();
  
  if(!LittleFS.begin(true)) { LittleFS.format(), LittleFS.begin();}
  
  boot_screen();
  
  #if ! defined(HAS_SCREEN)
    // start a task to handle serial commands while the webui is running
    startSerialCommandsHandlerTask();
  #endif
  
  delay(200);
  previousMillis = millis();
}

/**********************************************************************
**  Function: loop
**  Main loop
**********************************************************************/
#if defined(HAS_SCREEN)
void loop() {
  #if defined(HAS_RTC)
    RTC_TimeTypeDef _time;
  #endif
  bool redraw = true;
  int index = 0;
  int opt = 8;

  tft.fillRect(0,0,WIDTH,HEIGHT,BGCOLOR);
  setupSdCard();
  getConfigs();

  while(1){
    if (returnToMenu) {
      returnToMenu = false;
      tft.fillScreen(BGCOLOR); //fix any problem with the mainMenu screen when coming back from submenus or functions
      redraw=true;
    }

    if (redraw) {
      drawMainMenu(index);
      redraw = false;
      delay(200);
    }

    handleSerialCommands();
#ifdef CARDPUTER
    checkShortcutPress();  // shortctus to quickly start apps without navigating the menus
#endif

    if (checkPrevPress()) {
      checkReboot();
      if(index==0) index = opt - 1;
      else if(index>0) index--;
      redraw = true;
    }
    /* DW Btn to next item */
    if (checkNextPress()) {
      index++;
      if((index+1)>opt) index = 0;
      redraw = true;
    }

    /* Select and run function */
    if (checkSelPress()) {
      getMainMenuOptions(index);
      drawMainBorder(true);
      redraw=true;
    }

    if (clock_set) {
      #if defined(HAS_RTC)
        _rtc.GetTime(&_time);
        setTftDisplay(12, 12, FGCOLOR, 1, BGCOLOR);
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", _time.Hours, _time.Minutes);
        tft.print(timeStr);
      #else
        updateTimeStr(rtc.getTimeStruct());
        setTftDisplay(12, 12, FGCOLOR, 1, BGCOLOR);
        tft.print(timeStr);
      #endif
    }
    else {
      setTftDisplay(12, 12, FGCOLOR, 1, BGCOLOR);
      tft.print("BRUCE " + String(BRUCE_VERSION));
    }
  }
}
#else

// alternative loop function for headless boards
#include "core/wifi_common.h"
#include "modules/others/webInterface.h"

void loop() {
  setupSdCard();
  getConfigs();
  
  if(!wifiConnected) {
    Serial.println("wifiConnect");
    wifiConnect("",0,true);  // TODO: read mode from settings file
  }
  Serial.println("startWebUi");
  startWebUi(true);  // MEMO: will quit when checkEscPress
}
#endif
