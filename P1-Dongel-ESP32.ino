/*
***************************************************************************  
**  Program  : P1-Dongel-ESP32
**  Copyright (c) 2023 Martijn Hendriks / based on DSMR Api Willem Aandewiel
**
**  TERMS OF USE: MIT License. See bottom of file.                                                            
***************************************************************************      
*      
TODO
- detailgegevens voor korte tijd opslaan in werkgeheugen (eens per 10s voor bv 1 uur)
- feature: nieuwe meter = beginstand op 0
- front-end: splitsen dashboard / eenmalige instellingen bv fases 
- front-end: functie toevoegen die de beschikbare versies toont (incl release notes) en je dan de keuze geeft welke te flashen. (Erik)
- verbruik - teruglevering lijn door maandgrafiek (Erik)
- issue met reconnect dns name mqtt (Eric)
- auto switch 3 - 1 fase max fuse
- issue met basic auth afscherming rng bestanden
- temparatuur ook opnemen in grafieken (A van Dijken)
- websockets voor de communicatie tussen client / dongle ( P. van Bennekom )
- 90 dagen opslaan van uur gegevens ( R de Grijs )
- Roberto: P1 H2O watersensor gegevens apart versturen (MQTT) van P1 
- Sluipverbruik bijhouden
- Modbus TCP (a3) Vrij slave adres. Sommige systemen kennen alleen uniek slave adres/id. Maken geen onderscheid in IP adres omdat soms meer RS485/RTU devices achter 1 TCP naar RTU converter (bijvoorbeeld Moxa M-gate) zitten.
- Modbus Registers van de "Actueel" pagina lijkt me in eerste instantie voldoende. Wel mis ik zo iets als m3 (of liters) gas per uur. Belangrijk bij hybride warmteopwekking (ketel en warmtepomp), waarbij elke liter gas er één te veel is :-). Is natuurlijk ook softwarematig te maken.
- optie in settings om te blijven proberen om de connectie met de router te maken (geen hotspot) (Wim Zwart)
- toevoegen van mdns aanmelding na 1 minuut
- update Warmtelink url en mechanisme isoleren (Henry de J)
- ondersteuning ISTA devices (868MHz) - Jan Winder
- Interface HomeKit ivm triggeren op basis van energieverbruik/teruglevering (Thijs v Z)
- #18 water en gas ook in de enkele json string (mqtt)
- spanning bij houden igv teruglevering (+% teruglevering weergeven van de dag/uur/maand)
- loadbalancing bijhouden over de fases
- mqtt client in task proces

  • grafische weergave als standaardoptie weergave en cijferlijsten als tweede keuze. Nu is het andersom. 
  • Consistentie tijd-assen, links oud, rechts nieuw
    • in Actueel staat de laatste meting rechts en de oudste meting links
    • in de uurstaat loopt de tijd van rechts (oudst) naar links (laatste uurmeting)


4.8.7
√ use autoreconnect from wifimanager
√ bugfix mqtt issue #23
√ mbus integration (ID=1)
√ HA auto detect missing ... fixed

4.8.8
- issue met wifi connection lost ...  (h van Akker, P Brand)
- lengte credentials vermelden  (Rob G)

4.9.0
- RNG files continu open
- uren RNG files vergroten bv naar 14 dagen (nu 48h -> 336h) (Broes)
- teruglevering dashboard verkeerde verhoudingen ( Pieter ) 
- localisation frontend (resource files) https://phrase.com/blog/posts/step-step-guide-javascript-localization/
- RNGDays 31 days
- eigen NTP kunnen opgeven of juist niet (stopt pollen)
- support https / http mqtt link extern
- issue: wegvallen wifi geen reconnect / reconnect mqtt


************************************************************************************
Arduino-IDE settings for P1 Dongle hardware ESP32:
  - Board: "ESP32 Dev Module"
  - Flash mode: "QIO"
  - Flash size: "4MB (32Mb)"
  - CorenDebug Level: "None"
  - Flash Frequency: "80MHz"
  - CPU Frequency: "240MHz"
  - Upload Speed: "961600"                                                                                                                                                                                                                                                 
  - Port: <select correct port>
*/
/******************** compiler options  ********************************************/
//#define SHOW_PASSWRDS   // well .. show the PSK key and MQTT password, what else?     
//#define SE_VERSION
//#define ETHERNET
//#define STUB            //test only
//#define HEATLINK        //first draft
//#define INSIGHT         
//#define AP_ONLY
//#define MBUS
//#define MQTT_DISABLE
//#define NO_STORAGE
//#define VOLTAGE_MON
//#define EID

/******************** don't change anything below this comment **********************/
#include "DSMRloggerAPI.h"

//===========================================================================================
void setup() 
{
  SerialOut.begin(115200); //debug stream  
  USBSerial.begin(115200); //cdc stream

  Debug("\n\n ----> BOOTING....[" _VERSION "] <-----\n\n");
  DebugTln("The board name is: " ARDUINO_BOARD);

  P1StatusBegin(); //leest laatste opgeslagen status & rebootcounter + 1
  pinMode(DTR_IO, OUTPUT);
  pinMode(LED, OUTPUT);
  
  SetConfig();
  
  if ( IOWater != -1 ) pinMode(IOWater, INPUT_PULLUP); //Setconfig first
  
  if ( P1Status.dev_type == PRO_BRIDGE ) {
    pinMode(PRT_LED, OUTPUT);
    digitalWrite(PRT_LED, true); //default on
  }

  lastReset = getResetReason();
  DebugT(F("Last reset reason: ")); Debugln(lastReset);
  
//================ File System =====================================
  if (LittleFS.begin(true)) 
  {
    DebugTln(F("File System Mount succesfull\r"));
    FSmounted = true;     
  } else DebugTln(F("!!!! File System Mount failed\r"));   // Serious problem with File System 
  
//================ Status update ===================================
  actT = epoch(actTimestamp, strlen(actTimestamp), true); // set the time to actTimestamp!
  P1StatusWrite();
  LogFile("",false); // write reboot status to file
  readSettings(true);
//=============start Networkstuff ==================================
#ifndef ETHERNET
  #ifndef AP_ONLY
    startWiFi(settingHostname, 240);  // timeout 4 minuten
  #else 
    startAP();
  #endif
#else
  startETH();
#endif
  delay(100);
  startTelnet();
#ifndef AP_ONLY
  startMDNS(settingHostname);
  startNTP();
#ifndef MQTT_DISABLE 
  MQTTclient.setBufferSize(MQTT_BUFF_MAX);
#endif

#endif
//================ Check necessary files ============================
  if (!DSMRfileExist(settingIndexPage, false) ) {
    DebugTln(F("Oeps! Index file not pressent, try to download it!\r"));
    GetFile(settingIndexPage); //download file from cdn
    if (!DSMRfileExist(settingIndexPage, false) ) { //check again
      DebugTln(F("Index file still not pressent!\r"));
      FSNotPopulated = true;
      }
  }
  if (!FSNotPopulated) {
    DebugTln(F("FS correct populated -> normal operation!\r"));
    httpServer.serveStatic("/", LittleFS, settingIndexPage);
  }
 
  if (!DSMRfileExist("/Frontend.json", false) ) {
    DebugTln(F("Frontend.json not pressent, try to download it!"));
    GetFile("/Frontend.json");
  }
  setupFSexplorer();
 
  esp_register_shutdown_handler(ShutDownHandler);

  setupWater();
  setupAuxButton(); //esp32c3 only

  if (EnableHistory) CheckRingExists();

//================ Start Slimme Meter ===============================
  
  SetupSMRport();
  
//create a task that will be executed in the fP1Reader() function, with priority 2
  if( xTaskCreate( fP1Reader, "p1-reader", 30000, NULL, 2, &tP1Reader ) == pdPASS ) DebugTln(F("Task tP1Reader succesfully created"));

  DebugTf("Startup complete! actTimestamp[%s]\r\n", actTimestamp);  

#ifdef MBUS
  mbusSetup();
#endif  
} // setup()


//P1 reader task
void fP1Reader( void * pvParameters ){
    DebugTln(F("Enable slimme meter..."));
    slimmeMeter.enable(false);
    while(true) {
      handleSlimmemeter();
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void loop () { 
        
        httpServer.handleClient();
        handleMQTT();   
        yield();
      
       if ( DUE(StatusTimer) && (telegramCount > 2) ) { 
          P1StatusWrite();
          MQTTSentStaticInfo();
          CHANGE_INTERVAL_MIN(StatusTimer, 30);
       }
       
       handleKeyInput();
       handleRemoteUpdate();
       handleButton();
       handleWater();
       handleEnergyID();
  
} // loop()


/***************************************************************************
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to permit
* persons to whom the Software is furnished to do so, subject to the
* following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
* OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
* THE USE OR OTHER DEALINGS IN THE SOFTWARE.
* 
***************************************************************************/
