#include <Adafruit_MCP23X17.h>

#include <FastLED.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// Current firmware version
#define CURRENT_FIRMWARE_VERSION "1.2"

bool shakerPresent; // <<< AJOUT >>>
bool ignoreShakerSecurity = false; // <<< NOUVEAU : Mémorise le mode Force

// WiFi credentials
#define WIFI_SSID "Distributeur Shake'it"
#define WIFI_PASSWORD "Shakeit@2026"

// Variables for Receiving Version and Update URL
String Update_Version = "";
String Firmware_Update_URL = "";
int tentatives_co = 0;

#define RX_PIN 16
#define TX_PIN 17

#define CONTACT_PIN 25
bool contactPrevState = HIGH;

//LEDs
#define LED_PIN     4    // Pin de données (IO2 sur l’ESP32) 04
#define NUM_LEDS    75   // Nombre de LEDs
//#define LED_PIN_RETRO     8    // Pin de données // gpio08 ne fonctionne pas
//#define NUM_LEDS_RETRO    70   // Nombre de LEDs
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB

// Couleur fixe en RGB (0–255)
#define LED_R  0 //55
#define LED_G  255 //255
#define LED_B  55  //55
// Couleur fixe en RGB (0–255)
/*#define LED_R  55 //128
#define LED_G  255
#define LED_B  55*/
/*#define LED_R  0 //128
#define LED_G  255
#define LED_B  55*/

// Couleur Boisson
int LED_R_Boisson = 0;
int LED_G_Boisson = 255;
int LED_B_Boisson = 55;

// Couleur Fraise
#define LED_R_FRAISE  255
#define LED_G_FRAISE  0
#define LED_B_FRAISE  7

// Couleur Rétro-éclairage
#define LED_R_RETRO  255
#define LED_G_RETRO  255
#define LED_B_RETRO  255

// Couleur Pomme
#define LED_R_POMME  180
#define LED_G_POMME  255
#define LED_B_POMME  0

// Couleur Citron
#define LED_R_CITRON  255
#define LED_G_CITRON 255
#define LED_B_CITRON  2

// Couleur Orange
#define LED_R_ORANGE  255
#define LED_G_ORANGE  110
#define LED_B_ORANGE  0

// Couleur Eau
#define LED_R_EAU  0
#define LED_G_EAU  200
#define LED_B_EAU  255

// Ultrason
#define TRIG_PIN 26  //26
#define ECHO_PIN 27  //27

HardwareSerial UART(1); // UART2

volatile unsigned long echoStart = 0;
volatile unsigned long echoEnd = 0;
volatile bool newDistanceAvailable = false;
float distance = 0.0;

// --- Variables pour la génération non bloquante du pulse ---
enum TrigState { IDLE, PULSE_LOW, PULSE_HIGH };
TrigState trigState = IDLE;
unsigned long trigTimer = 0;

void IRAM_ATTR echoISR() {
  if (digitalRead(ECHO_PIN) == HIGH) {
    echoStart = micros();
  } else {
    echoEnd = micros();
    unsigned long duration = echoEnd - echoStart;
    distance = duration * 0.0343 / 2.0; // distance en cm
    newDistanceAvailable = true;
  }
}

// --- Adresses I2C ---
#define MCP_POMPE_ADDR 0x21
#define MCP_AUTRES_ADDR 0x20

// --- Electroaimants sur MCP 0x20 ---
#define ELECTRO1_PIN 6   // GPA6
#define ELECTRO2_PIN 7   // GPA7

CRGB leds[NUM_LEDS];
//CRGB leds_retro[NUM_LEDS_RETRO];

Adafruit_MCP23X17 mcpPompes;
Adafruit_MCP23X17 mcpAutres;

int DUREE_BOISSON = 12000; // 10s
int shaker_update = 0;
int boisson_running = 0;
int boisson = 0; 
/*  0 : Couleur Shake'it
    1 : Couleur Orange
    2 : Couleur fraise
    3 : Couleur citron
    4 : Couleur eau */

// Gestion électroaimants
bool electroDeactivated = false;
unsigned long electroTimer = 0;

// --- Mapping des pompes sur le MCP23017 (0x21) ---
const int pompePins[] = {
  -1,   // index 0 inutilisé
  15,   // Pompe 1 -> GPB7
  13,   // Pompe 2 -> GPB5
  11,   // Pompe 3 -> GPB3
  9,    // Pompe 4 -> GPB1
  7,    // Pompe 5 -> GPA7
  5,    // Pompe 6 -> GPA5
  3,    // Pompe 7 -> GPA3
  1     // Pompe 8 -> GPA1
};

// Structure pour suivre l’état de chaque pompe
struct PompeState {
  bool active;
  unsigned long startTime;
  unsigned long duration;
};

PompeState pompes[9]; // index 1..8

// --- Progress bar ---
bool progressActive = false;
unsigned long progressStart = 0;

// --- Shaker ---
const int SHAKER1_PIN = 8;  // GPB0 = 8

void setup() {
  delay(2000);
  UART.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN); // 9600 bauds, 8 bits, pas de parité, 1 stop bit
  Serial.begin(9600);

  // >>> AJOUT INDISPENSABLE ICI <<<
  //UART.setTimeout(20);
  //Serial.setTimeout(20);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(40);
  fill_solid(leds, NUM_LEDS, CRGB(LED_R, LED_G, LED_B)); // Allumées au démarrage
  FastLED.show();

  // Connect to Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      tentatives_co = tentatives_co + 1;
      if(tentatives_co > 20){
        break;
      }
      delay(1000);
      Serial.println("{CONNEXION_WIFI}");
      fill_solid(leds, NUM_LEDS, CRGB(LED_R_ORANGE, LED_G_ORANGE, LED_B_ORANGE)); // Allumées au démarrage
      FastLED.show();
    }
    Serial.println("{WIFI_CONNECTE}");
    fill_solid(leds, NUM_LEDS, CRGB(LED_R, LED_G, LED_B)); // Allumées au démarrage
    FastLED.show();

  if (!mcpPompes.begin_I2C(MCP_POMPE_ADDR)) {
    Serial.println("{error; 'Cannot find MCP23017 for pumps'}");
    fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0)); // Allumées au démarrage
    FastLED.show();
    while (1);
  }

  if (!mcpAutres.begin_I2C(MCP_AUTRES_ADDR)) {
    Serial.println("{error; 'Cannot find MCP23017 for others'}");
    fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0)); // Allumées au démarrage
    FastLED.show();
    while (1);
  }

  for (int i = 1; i <= 8; i++) {
    mcpPompes.pinMode(pompePins[i], OUTPUT);
    mcpPompes.digitalWrite(pompePins[i], LOW);
    pompes[i] = {false, 0, 0};
  }

  mcpAutres.pinMode(SHAKER1_PIN, INPUT);
    pinMode(CONTACT_PIN, INPUT);


  mcpAutres.pinMode(ELECTRO1_PIN, OUTPUT);
  mcpAutres.pinMode(ELECTRO2_PIN, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoISR, CHANGE);

  mcpAutres.digitalWrite(ELECTRO1_PIN, HIGH);
  mcpAutres.digitalWrite(ELECTRO2_PIN, HIGH);

  Serial.println("{success; 'Setup complete'}");
}

void loop() {

  //Check Shaker
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();

    // --- Détection contact sec ---
  bool contactState = digitalRead(CONTACT_PIN);
  if (contactState == LOW && contactPrevState == HIGH) {
    UART.println("{BADGE:1}");
    Serial.println("{BADGE:1}");
  }
  contactPrevState = contactState;

  // ====================================================
  // 1. D'ABORD : ON LIT L'ULTRASON (Via Interruption)
  // ====================================================
  // --- Gestion non bloquante du trigger ---
  switch (trigState) {
    case IDLE:
      static unsigned long lastTrigger = 0;
      if (nowMs - lastTrigger >= 100) {  
        lastTrigger = nowMs;
        digitalWrite(TRIG_PIN, LOW);
        trigTimer = nowUs;
        trigState = PULSE_LOW;
      }
      break;

    case PULSE_LOW:
      if (nowUs - trigTimer >= 2) {
        digitalWrite(TRIG_PIN, HIGH);
        trigTimer = nowUs;
        trigState = PULSE_HIGH;
      }
      break;

    case PULSE_HIGH:
      if (nowUs - trigTimer >= 10) {
        digitalWrite(TRIG_PIN, LOW);
        trigState = IDLE;
      }
      break;
  }

  if (newDistanceAvailable) {
    newDistanceAvailable = false;
    Serial.println(distance); // Optionnel : pour vérifier dans le moniteur
    
    if(distance >= 35){
      shakerPresent = false;
    }
    else if(distance < 35){
      shakerPresent = true;
    }
  }

  // ====================================================
  // 2. ENSUITE : LA SÉCURITÉ PREND SA DÉCISION
  // ====================================================
  // Coupe les pompes SEULEMENT si le shaker est absent ET qu'on n'est pas en mode Force
  if (!shakerPresent && !ignoreShakerSecurity) {
    for (int i = 1; i <= 8; i++) {
      if (pompes[i].active) {
        mcpPompes.digitalWrite(pompePins[i], LOW);
        pompes[i].active = false;
      }
    }
    progressActive = false;
    
    // >>> CORRECTION ICI : Le FastLED est ENFIN à l'intérieur ! <<<
    if(shaker_update == 0){
        fill_solid(leds, NUM_LEDS, CRGB(LED_R, LED_G, LED_B)); 
        FastLED.show(); // Ne s'exécute qu'une SEULE fois !
        
        Serial.println("{NO_SHAKER}");
        UART.println("{NO_SHAKER}");
        shaker_update = 1;
    }
  }

  // Gestion commande série (Lecture intelligente façon 2en1)
  String cmd = "";
  
  if (UART.available()) {
    char c = UART.peek();
    if (c == '{') {
      cmd = UART.readStringUntil('}');
    } else {
      UART.read(); // Jette les parasites (\n, \r, etc.) sans bloquer
    }
  } 
  else if (Serial.available()) {
    char c = Serial.peek();
    if (c == '{') {
      cmd = Serial.readStringUntil('}');
    } else {
      Serial.read(); // Jette les parasites
    }
  }

  if (cmd.length() > 0) {
    cmd.trim();

    Serial.print("Commande brute reçue : ");
    Serial.println(cmd);

    if (cmd.startsWith("{DOOR_OPEN")) {
      mcpAutres.digitalWrite(ELECTRO1_PIN, LOW);
      mcpAutres.digitalWrite(ELECTRO2_PIN, LOW);
      electroDeactivated = true;
      electroTimer = millis();
      Serial.println("{success; 'Door unlocked for 15s'}");
      UART.println("{success; 'Door unlocked for 15s'}");
    }

    // --- GESTION DIRECTE ELECTROVANNE {E:0;T:y} ---
    else if (cmd.startsWith("{E:") && cmd.indexOf(";T:") > 0) {
      
      // 1. Extraction du numéro
      int eIndex = cmd.indexOf(":") + 1;
      int tIndex = cmd.indexOf(";T:");
      int vanneNum = cmd.substring(eIndex, tIndex).toInt();
      
      // 2. Extraction de la durée
      int duree = cmd.substring(tIndex + 3, cmd.indexOf("}")).toInt();
      
      // Sécurité anti-inondation : max 15 secondes
      if (duree > 15000) duree = 15000; 

      // 3. Activation stricte de l'électrovanne (E:0)
      if (vanneNum == 0) {
          shaker_update = 0; // Bloque les messages NO_SHAKER intempestifs
          activerPompe(6, duree); 
          
          Serial.printf("{success; 'ELECTROVANNE ON pour %dms'}\n", duree);
          UART.printf("{success; 'ELECTROVANNE ON pour %dms'}\n", duree);
      } 
      else {
          Serial.println("{error; 'Utilisez E:0 pour l electrovanne. (Utilisez P: pour les aromes)'}");
          UART.println("{error; 'Utilisez E:0 pour l electrovanne. (Utilisez P: pour les aromes)'}");
      }
    }

    else if (cmd.startsWith("{UPDATE:\"") && cmd.endsWith("\"")) {  // Il faut envoyer une commande du style {UPDATE:"url_serveur"}
      int startIdx = cmd.indexOf("\"") + 1;
      int endIdx = cmd.lastIndexOf("\"");
      Firmware_Update_URL = cmd.substring(startIdx, endIdx);

      UpdateFirmware();  // Lance la mise à jour immédiatement
    }

    /*else if (cmd.startsWith("{START")) {
      Serial.println("{success; 'Restarting ESP32'}");
      UART.println("{success; 'Restarting ESP32'}");
      ESP.restart();   // <<< AJOUT : reset de l'ESP32 >>>
    }*/

    else if (cmd.startsWith("{CHECK")) {
      Serial.print("{READY}");
      UART.print("{READY}");
      //ESP.restart();   // <<< AJOUT : reset de l'ESP32 >>>
    }
    // 
    else if (cmd.startsWith("{") && cmd.indexOf("P:") > 0 && cmd.indexOf(";T:") > 0) {
      int pIndex = cmd.indexOf("P:") + 2;
      int tIndex = cmd.indexOf(";T:") + 3;

      int pompeNum = cmd.substring(pIndex, cmd.indexOf(";T:")).toInt();
      int tempsMs;
      int eauMs = DUREE_BOISSON;   // durée par défaut si E:w non présent
      String cValue = "";
      
      // 1. Extraction du paramètre Force (F:0 ou F:1)
      int forceMode = 0; // Par défaut, on ne force pas (sécurité activée)
      if (cmd.indexOf(";F:") > 0) {
        forceMode = cmd.substring(cmd.indexOf(";F:") + 3).toInt();
      }

// >>> AJOUT : On applique le laissez-passer à toute la machine <<<
      if (forceMode == 1) {
        ignoreShakerSecurity = true;
      } else {
        ignoreShakerSecurity = false;
      }

      // 2. Extraction Eau (E:)
      if (cmd.indexOf(";E:") > 0) {
        int eStart = cmd.indexOf(";E:") + 3;
        int eEnd = cmd.indexOf(";", eStart); // Recherche le prochain paramètre
        if (eEnd == -1) eEnd = cmd.length();
        eauMs = cmd.substring(eStart, eEnd).toInt();
        DUREE_BOISSON = eauMs;
      }

      // 3. Extraction Couleur (C:) et Temps (T:)
      if (cmd.indexOf(";C:") > 0) {
        tempsMs = cmd.substring(tIndex, cmd.indexOf(";C:")).toInt();
        int cStart = cmd.indexOf(";C:") + 3;
        // On s'arrête avant le ;F: s'il existe, sinon on va jusqu'au bout
        int cEnd = (cmd.indexOf(";F:") > 0) ? cmd.indexOf(";F:") : cmd.length();
        cValue = cmd.substring(cStart, cEnd);
      } else if (cmd.indexOf(";F:") > 0) {
        tempsMs = cmd.substring(tIndex, cmd.indexOf(";F:")).toInt();
      } else {
        tempsMs = cmd.substring(tIndex).toInt();
      }

      // >>> CORRECTION ICI : Le chrono global s'aligne sur l'action la plus longue <<<
      if (tempsMs > eauMs) {
        DUREE_BOISSON = tempsMs;
      } else {
        DUREE_BOISSON = eauMs;
      }

      // --- Mapping boisson en fonction de C:z ---
      if (cValue == "ICE_TEA"){
        boisson = 1;
        LED_R_Boisson = LED_R_ORANGE;
        LED_G_Boisson = LED_G_ORANGE;
        LED_B_Boisson = LED_B_ORANGE;
      } 
      else if (cValue == "MULTIFRUIT"){
        boisson = 1;
        LED_R_Boisson = LED_R_ORANGE;
        LED_G_Boisson = LED_G_ORANGE;
        LED_B_Boisson = LED_B_ORANGE;
      } 
      else if (cValue == "APPLE"){
        boisson = 1;
        LED_R_Boisson = LED_R_POMME;
        LED_G_Boisson = LED_G_POMME;
        LED_B_Boisson = LED_B_POMME;
      } 
      else if (cValue == "RED_FRUIT"){
        boisson = 2;
        LED_R_Boisson = LED_R_FRAISE;
        LED_G_Boisson = LED_G_FRAISE;
        LED_B_Boisson = LED_B_FRAISE;
      } 
      else if (cValue == "LEMON"){
        boisson = 3;
        LED_R_Boisson = LED_R_CITRON;
        LED_G_Boisson = LED_G_CITRON;
        LED_B_Boisson = LED_B_CITRON;
      } 
      else if (cValue == "WATER"){
        boisson = 4;
        LED_R_Boisson = LED_R_EAU;
        LED_G_Boisson = LED_G_EAU;
        LED_B_Boisson = LED_B_EAU;
      } 
      else{
        boisson = 0;
        LED_R_Boisson = LED_R;
        LED_G_Boisson = LED_G;
        LED_B_Boisson = LED_B;
      }

      // --- 4. LANCEMENT AVEC LE BYPASS (F:1) ---
      // Si le shaker n'est pas là ET qu'on n'a pas forcé le démarrage
      if (!shakerPresent && forceMode == 0) {
        Serial.println("{NO_SHAKER}");
        UART.println("{NO_SHAKER}");
      } 
      else if (pompeNum >= 1 && pompeNum <= 8 && tempsMs > 0) {
        shaker_update = 0;
        activerPompe(pompeNum, tempsMs);

        if (pompeNum >= 1 && pompeNum <= 5) {
          activerPompe(6, eauMs);   // Utilisation de la valeur E:w

          progressActive = true;
          progressStart = millis();
          fill_solid(leds, NUM_LEDS, CRGB::Black); 
          FastLED.show();
        }

        Serial.printf("{success; 'Pump %d activated for %d seconds, C:%s, Eau:%dms => boisson=%d, Force:%d'}\n", pompeNum, tempsMs / 1000, cValue.c_str(), eauMs, boisson, forceMode);
        UART.printf("{success; 'Pump %d activated for %d seconds, C:%s, Eau:%dms => boisson=%d, Force:%d'}\n", pompeNum, tempsMs / 1000, cValue.c_str(), eauMs, boisson, forceMode);
      } 
      else if (pompeNum == 0 && eauMs > 0) {
        shaker_update = 0;

        activerPompe(6, eauMs);   // Utilisation de la valeur E:w

        progressActive = true;
        progressStart = millis();
        fill_solid(leds, NUM_LEDS, CRGB::Black); 
        FastLED.show();
      }
      else {
        Serial.println("{error; 'Invalid command received. Please check your input'}");
        UART.println("{error; 'Invalid command received. Please check your input'}");
      }
    }
    else if (cmd.length() > 0) {
      Serial.println("{error; 'Invalid command received'}");
      UART.println("{error; 'Invalid command received'}");
    }

    //fill_solid(leds, NUM_LEDS, CRGB(LED_R, LED_G, LED_B)); // Allumées au démarrage
    //FastLED.show();
  }
  /*else if(!Serial.available()){
    fill_solid(leds, NUM_LEDS, CRGB(LED_R_FRAISE, LED_G_FRAISE, LED_B_FRAISE)); 
    FastLED.show();
  }*/

  if (electroDeactivated && millis() - electroTimer >= 5000) {
    mcpAutres.digitalWrite(ELECTRO1_PIN, HIGH);
    mcpAutres.digitalWrite(ELECTRO2_PIN, HIGH);
    electroDeactivated = false;
    Serial.println("{success; 'Door locked again'}");
    UART.println("{success; 'Door locked again'}");
  }

  unsigned long now = millis();
  for (int i = 1; i <= 8; i++) {
    if (pompes[i].active && (now - pompes[i].startTime >= pompes[i].duration)) {
      mcpPompes.digitalWrite(pompePins[i], LOW);
      pompes[i].active = false;
    }
  }

  if (progressActive) {
    unsigned long elapsed = millis() - progressStart;
    if (elapsed >= DUREE_BOISSON) {
      progressActive = false;
      ignoreShakerSecurity = false; // <<< AJOUT : Fin du mode Force, on remet la sécurité !
      fill_solid(leds, NUM_LEDS, CRGB(LED_R, LED_G, LED_B)); 
      FastLED.show();
    } else {
      float progress = (float)elapsed / DUREE_BOISSON;
      int ledsComplete = (int)(progress * NUM_LEDS);
      float fraction = (progress * NUM_LEDS) - ledsComplete;

      fill_solid(leds, NUM_LEDS, CRGB::Black);

      for (int i = 0; i < ledsComplete; i++) {
        leds[i] = CRGB(LED_R_Boisson, LED_G_Boisson, LED_B_Boisson);
      }

      int nextLed = ledsComplete;
      if (nextLed < NUM_LEDS) {
        leds[nextLed] = CRGB(LED_R_Boisson * fraction, LED_G_Boisson * fraction, LED_B_Boisson * fraction);
      }

      FastLED.show();
    }
  }
}

void activerPompe(int num, unsigned long duree) {
  mcpPompes.digitalWrite(pompePins[num], HIGH);
  pompes[num].active = true;
  pompes[num].startTime = millis();
  pompes[num].duration = duree;
}

void UpdateFirmware(){

    Serial.println("{LANCEMENT_UPDATE}");
    UART.println("{LANCEMENT_UPDATE}");

    // --- MISE À JOUR SÉCURISÉE (HTTPS) ---
    WiFiClientSecure clientSecurise;
    clientSecurise.setInsecure(); // Indispensable pour le HTTPS (ex: GitHub)
    
    HTTPClient http;
    http.begin(clientSecurise, Firmware_Update_URL);
    
    // On oblige l'ESP32 à suivre la redirection (Erreur 302) vers les serveurs de stockage
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // -------------------------------------

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
      WiFiClient& streamClient = http.getStream();
      int firmwareSize = http.getSize();
      Serial.print("Firmware Size: ");
      Serial.println(firmwareSize);
      UART.print("Firmware Size: ");
      UART.println(firmwareSize);

      if (Update.begin(firmwareSize))
      {
          size_t written = Update.writeStream(streamClient); // Le téléchargement se fait ici

          if (Update.size() == written)
          {
              Serial.println("{SUCCESS: MAJ}");
              UART.println("{SUCCESS: MAJ}");

              if (Update.end())
              {
                  Serial.println("RESTART");
                  UART.println("RESTART");
                  ESP.restart();
              } 
              else 
              {
                  Serial.print("{ERREUR: }");
                  Serial.println(Update.errorString());
                  UART.print("{ERREUR: }");
                  UART.println(Update.errorString());
              }
          }
          else
          {
              Serial.println("{NO_SPACE}");  // Erreur pas assez d'espace
              UART.println("{NO_SPACE}");  
          }
      } 
        else
        {
            Serial.println("UPDATE_ERROR");
            UART.println("UPDATE_ERROR");
        }
    }
    else
    {
        Serial.print("Failed to download firmware. HTTP code: ");
        Serial.println(httpCode);
        UART.print("Failed to download firmware. HTTP code: ");
        UART.println(httpCode);
    }

    http.end();
}
