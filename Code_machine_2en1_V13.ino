#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// Current firmware version
#define CURRENT_FIRMWARE_VERSION "1.9"

// WiFi credentials
#define WIFI_SSID "Distributeur Shake'it"
#define WIFI_PASSWORD "Shakeit@!2026"

// Variables for Receiving Version and Update URL
String Update_Version = "";
String Firmware_Update_URL = "";
int tentatives_co = 0;

// --- CONFIGURATION ULTRASONS ---
const int US1_TRIG = 27;
const int US1_ECHO = 26;

const int US2_TRIG = 25;
const int US2_ECHO = 33;

unsigned long lastUltraTime = 0; 
const unsigned long TIMEOUT_SAFE = 25000; 

// --- GESTION SHAKER ---
const float SHAKER_THRESHOLD_C = 14.0; 
bool shakerPresent_C = false;   
bool shakerPresent_B = false;  
// --- AJOUT : Tolérance pour éviter les fausses coupures ---
int compteurPerteC = 0;
int compteurPerteB = 0;    

// -------------------------------

// --- GESTION PORTE ---
bool doorUnlocked = false;       
unsigned long doorUnlockTime = 0; 
const unsigned long DOOR_DELAY = 10000; 
// -------------------------------

// --- VARIABLES LEDS & COULEURS ---
int LED_R_Boisson = 0;
int LED_G_Boisson = 255;
int LED_B_Boisson = 55;

#define LED_R_FRAISE  255
#define LED_G_FRAISE  0
#define LED_B_FRAISE  7

#define LED_R_POMME   110
#define LED_G_POMME   255
#define LED_B_POMME   0

#define LED_R_CITRON  255
#define LED_G_CITRON  255
#define LED_B_CITRON  2

#define LED_R_ORANGE  255
#define LED_G_ORANGE  110
#define LED_B_ORANGE  0

#define LED_R_EAU     0
#define LED_G_EAU     200
#define LED_B_EAU     255

// Chocolat : Un marron profond (orange très assombri)
#define LED_R_CHOCOLATE 90
#define LED_G_CHOCOLATE 40
#define LED_B_CHOCOLATE 0

// Coco : Un blanc pur et percutant
#define LED_R_COCO      255
#define LED_G_COCO      255
#define LED_B_COCO      255

// Cookie : Un doré/biscuit (plus clair et chaud que le chocolat)
#define LED_R_COOKIE    210
#define LED_G_COOKIE    120
#define LED_B_COOKIE    20

// Vanille : Un blanc cassé crémeux avec une touche de jaune
#define LED_R_VANILLA   255
#define LED_G_VANILLA   210
#define LED_B_VANILLA   80

// Variables Progress Bar
bool progressActive_Boisson = false;
unsigned long progressStart_Boisson = 0;
bool progressActive_Complement = false;
unsigned long progressStart_Complement = 0;
int DUREE_BOISSON = 0; 

// --- VARIABLES STOCKAGE COMMANDE COMPLEXE ---
int nb_bacs_seq = 0;
int seq_b1=0, seq_t1=0; // Bac 1 + Temps
int seq_b2=0, seq_t2=0;
int seq_b3=0, seq_t3=0;
int seq_b4=0, seq_t4=0;
int last_position_chariot;  // 0 = home, 3 = passage centre, 4 = attente avant service, 5 = service

// Infos Liquide Séquence
int seq_pompe=0; int seq_sirop=0; int seq_eau=0; String seq_col="";
int seq_force=0;

// --- NOUVEL AJOUT : Tolérance de perte de shaker ---
unsigned long timeShakerLost = 0; // Enregistre le moment exact où on perd le shaker
bool ignoreShakerSecurity = false; // <-- AJOUT : Laissez-passer pour le mode Force

// --- AJOUT : NETTOYAGE AUTOMATIQUE POST-COMPLEXE ---
bool attenteNettoyage1Min = false;
bool attenteNettoyage10Min = false;
unsigned long chronoFinServiceComplexe = 0;
// ---------------------------------------------------

// Déclarations MCP
Adafruit_MCP23X17 mcpPompes;
Adafruit_MCP23X17 mcpBacs;
Adafruit_MCP23X17 mcpAutres;

// Adresses I2C
#define MCP_POMPES_ADDR 0x20
#define MCP_BACS_ADDR   0x21
#define MCP_AUTRES_ADDR 0x23

// LEDs
#define LED_TYPE WS2812
#define COLOR_ORDER GRB
#define LED_STRIP_1 15
#define LED_STRIP_2 2
#define LED_STRIP_3 4
#define NUM_LEDS_1 5  // Autre
#define NUM_LEDS_2 31  // Compléments 31 leds
#define NUM_LEDS_3 25 // Boisson 25 leds

// Couleurs Repos
#define LED_R  55
#define LED_G  255
#define LED_B  55

CRGB leds1[NUM_LEDS_1];
CRGB leds2[NUM_LEDS_2];
CRGB leds3[NUM_LEDS_3];

// ---------------------------
// PINS MCP
// ---------------------------
// POMPES (MCP 0x20)
#define POMPE1 15
#define CHECKPUMP1 14
#define POMPE2 13
#define CHECKPUMP2 12
#define POMPE3 11
#define CHECKPUMP3 10
#define POMPE4 9
#define CHECKPUMP4 8
#define POMPE5 7
#define CHECKPUMP5 6
#define POMPE6 5
#define CHECKPUMP6 4
#define POMPE7 3
#define CHECKPUMP7 2
#define POMPE8 1
#define CHECKPUMP8 0

// VANNES (MCP 0x21)
#define VANNE1        2  
#define VANNE2        1  
#define VANNE3        0  
#define POMPE10 7
#define CHECKPUMP10 6  // pompe 10 sur mcpBacs et pompe 9 sur McpAutre
#define CLAPET_BAC1 5  // GPA5 sur mcpBacs (0x21)
#define CLAPET_BAC2 4  // GPA4 sur mcpBacs (0x21)

// AUTRES (MCP 0x23)
#define ELECTRO2   6
#define ELECTRO1   7
#define ENDSTOP1 8 
#define ENDSTOP2 10 
#define ENDSTOP3 9 
#define ENDSTOP4 11 // GPB3
#define ENDSTOP_PORTE_OUVERTE 14 // <-- AJOUT : GPB6 (Nouveau capteur)
#define POMPE9 0
#define CHECKPUMP9 1

// UART
#define RXD2 16
#define TXD2 17
HardwareSerial &UART = Serial2; 

#define MOTOR_SLAVE_ADDR 0x27
bool homingActive = false;
int lastStateS1 = -1;
int lastStateS2 = -1;
int lastStateS3 = -1;
bool homingPorteActive = false;
int lastStateS4 = -1;
bool memoirePorteOuverte = false; // Mémorise si la porte a été ouverte

// --- AJOUT : HOMING AUTOMATIQUE ---
unsigned long lastAutoHomeTime = 0; 
const unsigned long AUTO_HOME_INTERVAL = 3600000; // 3 600 000 ms = 1 Heure

// --- GESTION ETATS POMPES ---
struct OutputState {
  bool active;
  unsigned long startTime;
  unsigned long duration;
  bool isPaused;               // <-- AJOUT : Mémorise si la pompe est en pause
  unsigned long timeRemaining; // <-- AJOUT : Garde en mémoire le temps qu'il reste à couler
};

// Index 1-5 = Pompes aromes,
OutputState outputState[15]; // On agrandit pour stocker jusqu'à l'index 13

// Mapping des Pins Pompes 
const int pompePinsMap[] = { -1, POMPE1, POMPE2, POMPE3, POMPE4, POMPE5, POMPE6, POMPE7, POMPE8 };

// --- GESTION ETATS BACS (POUDRES) ---
struct BacMotor {
  uint8_t pin;
  bool actif;
  unsigned long startTime;
  unsigned long duree;
};

// Tableau de 8 moteurs (Index 0 = Bac 1, Index 7 = Bac 8)
BacMotor moteursBacs[8];
const int PIN_MOTEUR_BAC[] = { -1, 15, 14, 13, 12, 11, 10, 9, 8 };

void sendToSlave(String cmd) {
  Wire.beginTransmission(MOTOR_SLAVE_ADDR);
  Wire.print(cmd);
  Wire.endTransmission();
  Serial.print("-> Slave : "); Serial.println(cmd);
}

// --- GESTION TÂCHE LED (MULTITASKING ESP32) ---
TaskHandle_t TaskLEDHandle;
bool ledBreathingActive = false; // L'interrupteur

// Cette fonction tourne en boucle infinie sur le 2ème cœur de l'ESP32
void TaskLEDCode(void * pvParameters) {
  for(;;) { 
    if (ledBreathingActive) {
      
      // 1. Calcul de l'onde
      uint8_t ratio = beatsin8(15, 0, 255); 
      
      // --- CORRECTIF ANTI-ROUGE ---
      // Si la luminosité est très faible (< 12 sur 255), on force le noir complet.
      // Cela évite le moment où le vert s'éteint mais le rouge reste allumé.
      // Vous pouvez ajuster "12" : Si c'est encore rouge, montez à 15 ou 20.
      if (ratio < 12) {
        ratio = 0;
      }
      // ----------------------------

      // 2. On repart de la couleur originale
      CRGB couleurFinale = CRGB(LED_R_Boisson, LED_G_Boisson, LED_B_Boisson);
      
      // 3. Application de la luminosité
      couleurFinale.nscale8(ratio); 
      
      // 4. Affichage
      fill_solid(leds2, NUM_LEDS_2, couleurFinale);
      FastLED.show();
      
      vTaskDelay(30 / portTICK_PERIOD_MS); 
    } 
    else {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

// ----------------------------------------------------
// INITIALISATION
// ----------------------------------------------------
void init_machine() {
  mcpPompes.begin_I2C(MCP_POMPES_ADDR);
  mcpBacs.begin_I2C(MCP_BACS_ADDR);
  mcpAutres.begin_I2C(MCP_AUTRES_ADDR);

  const uint8_t pompesPins[] = { 15, 13, 11, 9, 7, 5, 3, 1 };
  for (uint8_t i = 0; i < 8; i++) {
    mcpPompes.pinMode(pompesPins[i], OUTPUT);
    mcpPompes.digitalWrite(pompesPins[i], LOW);
  }

  const uint8_t bacsPins[] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
  for (uint8_t i = 0; i < 16; i++) {
    mcpBacs.pinMode(bacsPins[i], OUTPUT);
    mcpBacs.digitalWrite(bacsPins[i], LOW);
  }

  const uint8_t autresPins[] = { 0, 1, 2, 3, 6, 7 };
  for (uint8_t i = 0; i < 6; i++) {
    mcpAutres.pinMode(autresPins[i], OUTPUT);
    if (autresPins[i] == ELECTRO1 || autresPins[i] == ELECTRO2) {
      mcpAutres.digitalWrite(autresPins[i], HIGH);
    } else {
      mcpAutres.digitalWrite(autresPins[i], LOW);
    }
  }
  for (int pin = 8; pin <= 15; pin++) mcpAutres.pinMode(pin, INPUT_PULLUP);

  for(int i=0; i<15; i++) outputState[i] = {false, 0, 0, false, 0};

  // Init Etats Bacs à 0
  for(int i=0; i<8; i++) moteursBacs[i] = {0, false, 0, 0};
}

float getDistance(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, TIMEOUT_SAFE);
  if (duration == 0) return 0.0;
  float d = duration * 0.0343 / 2.0;
  return (d > 30.0) ? 0.0 : d;
}

void activerSortie(int index, unsigned long duree) {
  
  // --- AJOUT : INVERSION LOGIQUE DES POMPES (1-5 <-> 6-10) ---
  if (index >= 1 && index <= 5) {
    index = index + 5; // Exemple : 1 devient 6
  } 
  else if (index >= 6 && index <= 10) {
    index = index - 5; // Exemple : 6 devient 1
  }
  // -----------------------------------------------------------

  // La suite reste identique, mais utilise le nouvel "index" inversé
  if (index >= 1 && index <= 8) {
    mcpPompes.digitalWrite(pompePinsMap[index], HIGH);
  } 
  else if (index == 9) {
    mcpAutres.digitalWrite(POMPE9, HIGH); 
  }
  else if (index == 10) {
    mcpBacs.digitalWrite(POMPE10, HIGH);
  }
  else if (index == 11) {
    mcpBacs.digitalWrite(VANNE1, HIGH);
  }
  else if (index == 12) {
    mcpBacs.digitalWrite(VANNE2, HIGH);
  }
  else if (index == 13) {
    mcpBacs.digitalWrite(VANNE3, HIGH);
  }
  
  outputState[index].active = true;
  outputState[index].startTime = millis();
  outputState[index].duration = duree;
  outputState[index].isPaused = false;
}

void bouger_porte(int position) {
  String cmd = "{DOOR:" + String(position) + "}";
  sendToSlave(cmd);
  // Optionnel : attendreFinMouvement(); // Décommente si tu veux que ce soit bloquant
}

void home_porte() {
  sendToSlave("{HOME_PORTE}");
  homingPorteActive = true;
  lastStateS4 = -1;
}

/*depuis testbacs*/

// ----------------------------------------------------
// FONCTIONS DE GESTION DES BACS
// ----------------------------------------------------
void activerBac(uint8_t numero, unsigned long dureeMs) {
  if (numero < 1 || numero > 8) return;
  
  uint8_t pin = PIN_MOTEUR_BAC[numero];

  moteursBacs[numero - 1] = { pin, true, millis(), dureeMs };
  mcpBacs.digitalWrite(pin, HIGH);

  // --- NOUVEAU : OUVERTURE DU CLAPET ---
  if (numero == 2) {
      mcpBacs.digitalWrite(CLAPET_BAC1, HIGH);
      Serial.println("Clapet Bac 1 OUVERT");
  } else if (numero == 3) {
      mcpBacs.digitalWrite(CLAPET_BAC2, HIGH);
      Serial.println("Clapet Bac 2 OUVERT");
  }
  // -------------------------------------

  Serial.printf("Bac %d activé pendant %lu ms\n", numero, dureeMs);
  
  attendre(dureeMs); // Attente non-bloquante pour les autres sorties
  
  mcpBacs.digitalWrite(pin, LOW);
  
  // --- NOUVEAU : FERMETURE DU CLAPET A LA FIN ---
  if (numero == 2) {
      mcpBacs.digitalWrite(CLAPET_BAC1, LOW);
      Serial.println("Clapet Bac 1 FERME");
  } else if (numero == 3) {
      mcpBacs.digitalWrite(CLAPET_BAC2, LOW);
      Serial.println("Clapet Bac 2 FERME");
  }
  // ----------------------------------------------
}

void updateBacs() {
  unsigned long now = millis();

  for (int i = 0; i < 8; i++) {
    if (moteursBacs[i].actif &&
        (now - moteursBacs[i].startTime >= moteursBacs[i].duree)) {
      moteursBacs[i].actif = false;
      mcpBacs.digitalWrite(moteursBacs[i].pin, LOW);
      
      // --- NOUVEAU : FERMETURE DU CLAPET (Sécurité timer) ---
      if (i == 1) { // Index 0 = Bac 1
          mcpBacs.digitalWrite(CLAPET_BAC1, LOW);
      } else if (i == 2) { // Index 1 = Bac 2
          mcpBacs.digitalWrite(CLAPET_BAC2, LOW);
      }
      // ------------------------------------------------------
      
      Serial.printf("Bac %d arrêté\n", i + 1);
    }
  }
}

// ---------------------------
// MOTEURS & COORDONNÉES
// ---------------------------

/*COORDONNES ROBOT
MAX THEORIQUE: {X:-92000;Y-26000}
MAX REEL : {X:-92000;Y-26000}
CENTRE SERVICE : {X:-47000;Y:-17000} (Passer par centre passage d'abord pour éviter de taper la porte)
CENTRE PASSAGE : {X:-47000;Y:-500}
CENTRE REPOS : {X:-47000;Y:-13000}

BAC 1 : {X:-92000;Y:-0}
BAC 2 : {X:-82000;Y:-0}
BAC 3 : {X:-64000;Y:-0}
BAC 4 : {X:-52000;Y:-0} 
BAC 5 : {X:-38500;Y:-0}
BAC 6 : {X:-24000;Y:-0}
BAC 7 : {X:-11500;Y:-0}
BAC 8 : {X:-200;Y:-0} ->  {HOME}
*/

// --- ÉTATS POSSIBLES DU ROBOT ---
enum EtatRobot {
  IDLE,               // Au repos, prêt à recevoir un ordre
  EN_ROUTE_PASSAGE,   // En train d'aller au point de passage (étape 1 vers service)
  EN_ROUTE_SERVICE,   // En train d'aller au service (étape 2)
  EN_ROUTE_SIMPLE     // En déplacement vers un bac ou autre (sans étape)
};

EtatRobot etatActuel = IDLE; // État de départ

// Timer pour ne pas spammer le bus I2C
unsigned long dernierCheckI2C = 0;

void fermerPorteActive() {
  Serial.println("Fermeture Porte (Active)...");
  
  // --- SÉCURITÉ 1 : La porte est-elle déjà fermée ? ---
  int etatInitial = !mcpAutres.digitalRead(ENDSTOP4);
  if (etatInitial == 1) {
      Serial.println("La porte est déjà fermée. Annulation du mouvement.");
      sendToSlave("{E:4;S:1}"); 
      delay(15);
      sendToSlave("{E:4;S:1}");
      return; // On quitte la fonction !
  }
  // ----------------------------------------------------

  // 1. On lance le moteur vers le capteur
  sendToSlave("{HOME_PORTE}");
  
  unsigned long debut = millis();
  
  // 2. Boucle de surveillance locale
  while (true) {
    verifierArretSorties(); 
    
    int s4 = !mcpAutres.digitalRead(ENDSTOP4); 
    
    // Si touché (1)
    if (s4 == 1) {
       // --- SÉCURITÉ 2 : On SPAMME l'ordre pour contrer les pertes I2C ---
       sendToSlave("{E:4;S:1}"); // STOP
       delay(15); 
       sendToSlave("{E:4;S:1}"); // Répétition 1
       delay(15);
       sendToSlave("{E:4;S:1}"); // Répétition 2
       Serial.println("Porte fermée OK.");
       break; // On sort de la boucle !
    }
    
    // --- SÉCURITÉ 3 : Timeout réduit à 6s pour sauver le TB6600 ---
    if (millis() - debut > 6000) { 
       sendToSlave("{E:4;S:1}");
       delay(15);
       sendToSlave("{E:4;S:1}");
       Serial.println("ERREUR: Timeout Porte. Moteur stoppé de force.");
       break;
    }
    
    delay(10); // Petite pause pour stabilité
  }
}

// On ajoute 'int forceMode' à la fin des arguments
void serviceBoisson(int nbBacs, int b1, int t1, int b2, int t2, int b3, int t3, int b4, int t4, int pId, int tSirop, int tEau, String couleur, int forceMode) {

  // Allumage de la bande led
  couleur.trim(); 

  if (couleur == "ICE_TEA" || couleur == "MULTIFRUIT" || couleur == "ORANGE") {
       LED_R_Boisson = LED_R_ORANGE; LED_G_Boisson = LED_G_ORANGE; LED_B_Boisson = LED_B_ORANGE;
  } else if (couleur == "APPLE") {
       LED_R_Boisson = LED_R_POMME; LED_G_Boisson = LED_G_POMME; LED_B_Boisson = LED_B_POMME;
  } else if (couleur == "RED_FRUIT") {
       LED_R_Boisson = LED_R_FRAISE; LED_G_Boisson = LED_G_FRAISE; LED_B_Boisson = LED_B_FRAISE;
  } else if (couleur == "LEMON") {
       LED_R_Boisson = LED_R_CITRON; LED_G_Boisson = LED_G_CITRON; LED_B_Boisson = LED_B_CITRON;
  } else if (couleur == "WATER") {
       LED_R_Boisson = LED_R_EAU; LED_G_Boisson = LED_G_EAU; LED_B_Boisson = LED_B_EAU;
  }
  else if (couleur == "CHOCOLATE") {
       LED_R_Boisson = LED_R; LED_G_Boisson = LED_G; LED_B_Boisson = LED_B;
  }
  else if (couleur == "VANILLA") {
       LED_R_Boisson = LED_R_VANILLA; LED_G_Boisson = LED_G_VANILLA; LED_B_Boisson = LED_B_VANILLA;
  } 
  else if (couleur == "COCO") {
       LED_R_Boisson = LED_R_COCO; LED_G_Boisson = LED_G_COCO; LED_B_Boisson = LED_B_COCO;
  }
  else if (couleur == "COOKIE") {
       LED_R_Boisson = LED_R_VANILLA; LED_G_Boisson = LED_G_VANILLA; LED_B_Boisson = LED_B_VANILLA;
  }else {
       // Défaut (Vert repos ou autre)
       LED_R_Boisson = LED_R; LED_G_Boisson = LED_G; LED_B_Boisson = LED_B;
  }

  ledBreathingActive = true;
  // On allume la bande LED 2
  fill_solid(leds2, NUM_LEDS_2, CRGB(LED_R_Boisson, LED_G_Boisson, LED_B_Boisson));
  FastLED.show();

  // On prévient l'app du lancement du process
  Serial.println("{OUVERTURE}");
  UART.println("{OUVERTURE}");

  // Début du service
  verrouiller_moteurs();

// --- CORRECTION : OUVERTURE ROBUSTE DE LA PORTE ---
  position_chariot(3);  // Passage centre
  
  if (forceMode == 0) {
      // En mode normal, on se met en attente et on ouvre
      position_chariot(4);  
      bouger_porte(-36500); 
      attendreFinMouvement();
  }
  
  // Et on avance en position de service (Porte déjà ouverte si F:1)
  position_chariot(5);  
  last_position_chariot = 5;
/*
  // Début du service
  verrouiller_moteurs();

  if(last_position_chariot == 1 || last_position_chariot == 0){ // Accepte les deux au cas où
    position_chariot(3);  // Passage centre
    last_position_chariot = 3;
    position_chariot(4);  // Attente ouverture porte
    last_position_chariot = 4;
    bouger_porte(-36500);
    attendreFinMouvement();
    position_chariot(5);  // Position service
    last_position_chariot = 5;
  }
  else{
    if(last_position_chariot == 4){
        bouger_porte(-36500);
        attendreFinMouvement();
        position_chariot(5);  // Position service
        last_position_chariot = 5;
    }
  }*/

  // --- LOGIQUE DETECTION MODIFIEE ---
  bool shakerTrouve = false;

  if (forceMode == 1) {
      // SI FORCE ACTIVÉ : On simule une détection réussie immédiatement
      shakerTrouve = true;
      shakerPresent_C = true; // Important pour l'animation LED plus bas
      attendre(1000);
      Serial.println("{SHAKER_DETECTE}");
      UART.println("{SHAKER_DETECTE}");
  } 
  else {
      // SINON : Comportement normal avec Ultrasons
      unsigned long startDetection = millis();
      while (millis() - startDetection < 12000) {
        float d1 = getDistance(US1_TRIG, US1_ECHO);
        if (d1 > 0.1 && d1 < 14) {
           shakerPresent_C = true;
           shakerTrouve = true;
           break; 
        }
        attendre(100); 
      }
  }

  // Si échec (seulement si forceMode était 0)
  if (!shakerTrouve) {
    
    ledBreathingActive = false;
    delay(50);
    fill_solid(leds2, NUM_LEDS_2, CRGB(LED_R, LED_G, LED_B)); 
    FastLED.show();

    // On recule le chariot
    position_chariot(4);  // Attente ouverture porte
    last_position_chariot = 4;
    attendre(600);
    fermerPorteActive();
    Serial.println("{NO_SHAKER}");
    UART.println("{NO_SHAKER}");
    return; // On annule tout
  }

  // --- SUCCES ---
  Serial.println("{SHAKER_DETECTE}");
  UART.println("{SHAKER_DETECTE}");
  attendre(1000);

  //Retour en arrière avant d'aller aux bacs
  position_chariot(4);
  last_position_chariot = 4;

  // Fermeture de la porte
  fermerPorteActive();
  Serial.println("{BOISSON_LANCEE}");
  UART.println("{BOISSON_LANCEE}");

  Serial.println("temps pompe: ");
  Serial.println(tSirop);
  Serial.println("numero pompe: ");
  Serial.println(pId);
  activerSortie(pId, tSirop); // Arome
  activerSortie(12, tEau);    // Eau (Vanne 2)
  
  attendre(tEau+1000);

  Serial.println(nbBacs);
  Serial.println(b1);
  Serial.println(t1);

  //attendre ici pour pas voir l'intérieur de la machine ?
  position_chariot(3);
  last_position_chariot = 3;

  switch (nbBacs) {
    case 1:
      chariot_bac(b1);
      attendreFinMouvement();
      activerBac(b1, t1); 
      break;
    case 2:
      chariot_bac(b1);
      attendreFinMouvement();
      activerBac(b1, t1);
      
      chariot_bac(b2);
      attendreFinMouvement();
      activerBac(b2, t2);
      break;
    case 3:
      chariot_bac(b1);
      attendreFinMouvement();
      activerBac(b1, t1);

      chariot_bac(b2);
      attendreFinMouvement();
      activerBac(b2, t2);
      
      chariot_bac(b3);
      attendreFinMouvement();
      activerBac(b3, t3);
      break;
    case 4:
      chariot_bac(b1);
      attendreFinMouvement();
      activerBac(b1, t1);

      chariot_bac(b2);
      attendreFinMouvement();
      activerBac(b2, t2);

      chariot_bac(b3);
      attendreFinMouvement();
      activerBac(b3, t3);

      chariot_bac(b4);
      attendreFinMouvement();
      activerBac(b4, t4);
      break;
    
    default:
      break;
  }

  position_chariot(3);  // Passage centre
  last_position_chariot = 3;
  position_chariot(4);  // Attente ouverture porte
  last_position_chariot = 4;
  bouger_porte(-36500);
  attendreFinMouvement();
  position_chariot(5);  // Position service
  last_position_chariot = 5;

  ledBreathingActive = false; 
  delay(50); 

  // Remise led standard
  CRGB repos = CRGB(LED_R, LED_G, LED_B);
  fill_solid(leds2, NUM_LEDS_2, repos);
  FastLED.show();

  Serial.println("{BOISSON_TERMINEE}");
  UART.println("{BOISSON_TERMINEE}");

  // --- SÉCURITÉ RETRAIT DU SHAKER ---
  if (forceMode == 1) {
      // 1. CAS BYPASS (F:1) : On ignore complètement le capteur
      Serial.println("Mode Force : Bypass de l'attente de retrait.");
      attendre(8000); // Petite pause de courtoisie de 8 secondes avant de refermer
      shakerPresent_C = false;
  } 
  else {
      // 2. CAS NORMAL : On attend le retrait, mais avec un chronomètre de 10s max
      unsigned long debutRetrait = millis();
      
      while (true) {
        float d1 = getDistance(US1_TRIG, US1_ECHO);
        
        // Si la distance est grande (> 14cm), le shaker est bien retiré
        if (d1 > 14 || d1 == 0) {
          shakerPresent_C = false;
          Serial.println("Shaker retiré naturellement.");
          UART.println("Shaker retiré naturellement.");
          break; 
        }

        // TIMEOUT DE SÉCURITÉ : Si on attend depuis plus de 10 secondes, on coupe
        if (millis() - debutRetrait > 10000) {
            Serial.println("ERREUR: Timeout Retrait. Fermeture automatique de sécurité.");
            UART.println("ERREUR: Timeout Retrait. Fermeture automatique de sécurité.");
            shakerPresent_C = false;
            break; // On sort de la boucle et on force la fermeture
        }
        
        attendre(200); 
      }
  }

  // --- FERMETURE ---
  attendre(2500); 

  position_chariot(4); 
  last_position_chariot = 4;
  
  fermerPorteActive();

  // Home pour être sur de sa position ?
  position_chariot(3);  // Passage centre
  last_position_chariot = 3;
  homerChariotActif();

  // --- AJOUT : DÉMARRAGE DES CHRONOS DE NETTOYAGE ---
  chronoFinServiceComplexe = millis();
  attenteNettoyage1Min = true;
  attenteNettoyage10Min = true;
  Serial.println("Chronos de nettoyage auto (1min et 10min) démarrés.");
  // --------------------------------------------------
}

void verrouiller_moteurs() {
  sendToSlave("{LOCK_CHARIOT}");
}

void deverrouiller_moteurs() {
  sendToSlave("{UNLOCK_CHARIOT}");
}


void homerChariotActif() {
  Serial.println("Recalibration Chariot (Active)...");
  sendToSlave("{HOME}");
  
  int locS1 = -1; int locS2 = -1; int locS3 = -1;
  unsigned long debut = millis();
  
  while (true) {
    verifierArretSorties(); 
    
    int s1 = !mcpAutres.digitalRead(ENDSTOP1);
    int s2 = !mcpAutres.digitalRead(ENDSTOP2);
    int s3 = !mcpAutres.digitalRead(ENDSTOP3);

    if (s1 != locS1) { if (s1 == 1) sendToSlave("{E:1;S:1}"); locS1 = s1; }
    if (s2 != locS2) { if (s2 == 1) sendToSlave("{E:2;S:1}"); locS2 = s2; }
    if (s3 != locS3) { if (s3 == 1) sendToSlave("{E:3;S:1}"); locS3 = s3; }

    if (s1 == 1 && s2 == 1 && s3 == 1) {
       Serial.println("Recalibration terminee.");
       break;
    }
    if (millis() - debut > 25000) { 
       sendToSlave("{E:1;S:1}"); sendToSlave("{E:2;S:1}"); sendToSlave("{E:3;S:1}");
       Serial.println("ERREUR: Timeout Recalibration");
       break;
    }
    delay(10);
  }

  // --- NOUVEL AJOUT : PLACEMENT AU CENTRE APRÈS LE HOMING ---
  // On recule un peu pour se dégager des capteurs
  position_chariot(3);
  position_chariot(4);
  // ---------------------------------------------------------

  last_position_chariot = 4; 
  homingActive = false; 
}
// ----------------------

void home_chariot(){
  sendToSlave("{HOME}");
  homingActive = true;
  lastStateS1 = -1; lastStateS2 = -1; lastStateS3 = -1;
}

void chariot_bac(int numero_bac) {
  numero_bac = 9 - numero_bac;
  switch (numero_bac) {
    case 1:
      sendToSlave("{X:-92000;Y:-0}");
      break;
    case 2:
      sendToSlave("{X:-82000;Y:-0}");
      break;
    case 3:
      sendToSlave("{X:-68000;Y:-0}");
      break;
    case 4:
      sendToSlave("{X:-54500;Y:-0}");
      break;
    case 5:
      sendToSlave("{X:-39500;Y:-0}");
      break;
    case 6:
      sendToSlave("{X:-25000;Y:-0}");
      break;
    case 7:
      sendToSlave("{X:-11500;Y:-0}");
      break;
    case 8:
      sendToSlave("{X:-50;Y:-0}");
      break;
    default:
      // Optionnel : Message d'erreur ou comportement par défaut
      break;
  }
}

void position_chariot(int position) {
  switch (position) {
    case 1: // CENTRE REPOS
      sendToSlave("{X:-47000;Y:-13000}");
      attendreFinMouvement();
      break;

    case 2: // CENTRE SERVICE
      // ÉTAPE 1 : Aller au point de passage pour éviter la collision
      sendToSlave("{X:-47000;Y:-500}");
      attendreFinMouvement();

      // ÉTAPE 2 : Attendre que le mouvement soit fini
      // IMPORTANT : Vous devez avoir une fonction pour attendre ou vérifier que le slave a fini.
      // Exemple simple (bloquant) : delay(3000); 
      // Exemple propre : while(isSlaveMoving()) { ... }
      //delay(3000); // À AJUSTER selon le temps de trajet réel ou remplacer par une vérification active

      // ÉTAPE 3 : Aller au service une fois au point de passage
      sendToSlave("{X:-47000;Y:-17000}");
      attendreFinMouvement();
      break;

    case 3: // CENTRE PASSAGE
      sendToSlave("{X:-47000;Y:-500}");
      attendreFinMouvement();
      break;

    case 4: // ATTENTE OUVERTURE PORTE
      sendToSlave("{X:-47000;Y:-15000}");
      attendreFinMouvement();
      break;

    case 5: // POSITION SERVICE
      sendToSlave("{X:-47000;Y:-17000}");
      attendreFinMouvement();
      break;

    default:
      break;
  }
}

// --- NOUVELLES FONCTIONS : PAUSE ET REPRISE ---
void pauseLiquides() {
  unsigned long now = millis();
  for (int i = 1; i <= 13; i++) {
    if (outputState[i].active && !outputState[i].isPaused) {
      // 1. Calcul du temps restant
      unsigned long elapsed = now - outputState[i].startTime;
      if (elapsed < outputState[i].duration) {
         outputState[i].timeRemaining = outputState[i].duration - elapsed;
      } else {
         outputState[i].timeRemaining = 0;
      }
      
      // 2. Coupure physique
      if (i <= 8) mcpPompes.digitalWrite(pompePinsMap[i], LOW);
      else if (i == 9) mcpAutres.digitalWrite(POMPE9, LOW);
      else if (i == 10) mcpBacs.digitalWrite(POMPE10, LOW);
      else if (i == 11) mcpBacs.digitalWrite(VANNE1, LOW);
      else if (i == 12) mcpBacs.digitalWrite(VANNE2, LOW);
      else if (i == 13) mcpBacs.digitalWrite(VANNE3, LOW);
      
      outputState[i].isPaused = true;
      Serial.printf("Pompe %d en PAUSE. Reste %lu ms\n", i, outputState[i].timeRemaining);
    }
  }
}

void reprendreLiquides() {
  for (int i = 1; i <= 13; i++) {
    if (outputState[i].active && outputState[i].isPaused) {
      // 1. On rallume physiquement
      if (i <= 8) mcpPompes.digitalWrite(pompePinsMap[i], HIGH);
      else if (i == 9) mcpAutres.digitalWrite(POMPE9, HIGH);
      else if (i == 10) mcpBacs.digitalWrite(POMPE10, HIGH);
      else if (i == 11) mcpBacs.digitalWrite(VANNE1, HIGH);
      else if (i == 12) mcpBacs.digitalWrite(VANNE2, HIGH);
      else if (i == 13) mcpBacs.digitalWrite(VANNE3, HIGH);
      
      // 2. On relance le chrono avec le temps restant
      outputState[i].startTime = millis();
      outputState[i].duration = outputState[i].timeRemaining;
      outputState[i].isPaused = false;
      Serial.printf("Pompe %d REPRISE pour %lu ms\n", i, outputState[i].duration);
    }
  }
}
// ----------------------------------------------

void stopAllService() {
  for (int i = 1; i <= 13; i++) { // On va jusqu'à 13
     if (outputState[i].active) {
       // On éteint selon l'index
       if (i <= 8) mcpPompes.digitalWrite(pompePinsMap[i], LOW);
       else if (i == 9) mcpAutres.digitalWrite(POMPE9, LOW);
       else if (i == 10) mcpBacs.digitalWrite(POMPE10, LOW);
       else if (i == 11) mcpBacs.digitalWrite(VANNE1, LOW);
       else if (i == 12) mcpBacs.digitalWrite(VANNE2, LOW);
       else if (i == 13) mcpBacs.digitalWrite(VANNE3, LOW);
       
       outputState[i].active = false;
       // --- CORRECTIF CRITIQUE INONDATION ---
       outputState[i].isPaused = false; // On purge la mémoire fantôme !
     }
  }
  
  if (progressActive_Boisson) {
    progressActive_Boisson = false;
    ignoreShakerSecurity = false; // <-- AJOUT : Désactivation du bypass d'urgence
    CRGB repos = CRGB(LED_R, LED_G, LED_B);
    fill_solid(leds1, NUM_LEDS_1, repos);
    fill_solid(leds2, NUM_LEDS_2, repos);
    fill_solid(leds3, NUM_LEDS_3, repos);
    FastLED.show();
    Serial.println("{NO_SHAKER}");
    UART.println("{NO_SHAKER}");
    // --- AJOUT : NOTIFICATION FIN APRÈS RETRAIT ---
    Serial.println("{BOISSON_TERMINEE}");
    UART.println("{BOISSON_TERMINEE}");
    // ----------------------------------------------
  }
}

// --- FONCTIONS CLÉS POUR GÉRER L'ATTENTE SANS BLOQUER LES POMPES ---

// 1. Cette fonction vérifie si une pompe doit s'éteindre
void verifierArretSorties() {
  unsigned long now = millis();
  for (int i = 1; i <= 13; i++) { 
    // AJOUT : On ne vérifie que si c'est actif ET NON PAUSÉ
    if (outputState[i].active && !outputState[i].isPaused && (now - outputState[i].startTime >= outputState[i].duration)) {
      
      // Extinction physique
      if (i <= 8) mcpPompes.digitalWrite(pompePinsMap[i], LOW);
      else if (i == 9) mcpAutres.digitalWrite(POMPE9, LOW);
      else if (i == 10) mcpBacs.digitalWrite(POMPE10, LOW);
      else if (i == 11) mcpBacs.digitalWrite(VANNE1, LOW);
      else if (i == 12) mcpBacs.digitalWrite(VANNE2, LOW);
      else if (i == 13) mcpBacs.digitalWrite(VANNE3, LOW);
      
      outputState[i].active = false;
      Serial.printf("Fin sortie %d (Timer)\n", i);
    }
  }
}

// 2. Cette fonction remplace delay(). Elle attend MAIS continue de vérifier les pompes.
void attendre(unsigned long duree) {
  unsigned long debut = millis();
  while (millis() - debut < duree) {
    verifierArretSorties(); // On surveille les pompes pendant qu'on attend
    delay(1); // Petite pause pour la stabilité
  }
}

/*void attendreFinMouvement() {
  // On laisse 50ms au slave pour recevoir l'ordre et démarrer le moteur
  //delay(2000); 
  attendre(2000);

  while (true) {
    // 1. On continue de surveiller les sécurités (pompes, shakers) pendant qu'on attend
    verifierArretSorties(); 
    
    // 2. On demande au Slave (0x27) : "Est-ce que tu bouges ?"
    Wire.requestFrom(MOTOR_SLAVE_ADDR, 1); 
    
    if (Wire.available()) {
      int status = Wire.read(); // 1 = Je bouge, 0 = J'ai fini
      if (status == 0) {
        break; // Le mouvement est fini, on sort de la boucle !
      }
    }
    
    delay(50); // Petite pause pour ne pas saturer le processeur
  }
}*/

void attendreFinMouvement() {
  // 1. Petit délai de sécurité au démarrage (laisse le temps à l'ordre d'arriver)
  attendre(100); 

  int compteurConfirmation = 0; // Compteur de sécurité

  while (true) {
    // On surveille les pompes en permanence
    verifierArretSorties(); 
    
    // On interroge l'esclave
    Wire.requestFrom(MOTOR_SLAVE_ADDR, 1); 
    
    int status = 1; // Par défaut, on considère qu'il est occupé (sécurité)
    
    if (Wire.available()) {
      status = Wire.read(); 
    }

    if (status == 1) {
      // L'esclave dit : "Je bouge"
      compteurConfirmation = 0; // On remet le compteur à zéro
    } 
    else {
      // L'esclave dit : "J'ai fini" (0)
      // ON NE LE CROIT PAS TOUT DE SUITE !
      compteurConfirmation++;
      
      // On attend qu'il nous dise "J'ai fini" 5 fois de suite (5 x 20ms = 100ms stables)
      // Cela permet de passer par dessus le petit "trou" au démarrage
      if (compteurConfirmation >= 5) {
        break; // Là on est sûr, on sort !
      }
    }
    
    delay(20); // Petite pause entre chaque question
  }
}

void UpdateFirmware(){

    Serial.println("{LANCEMENT_MAJ}");
    UART.println("{LANCEMENT_MAJ}");

    WiFiClientSecure clientSecurise;
    clientSecurise.setInsecure(); // Indispensable pour le HTTPS de GitHub
    
    HTTPClient http;
    http.begin(clientSecurise, Firmware_Update_URL);
    
    // --- LE CORRECTIF POUR GITHUB (Erreur 302) ---
    // On oblige l'ESP32 à suivre la redirection vers les serveurs de stockage
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // ---------------------------------------------

    int httpCode = http.GET();

    // Sur certaines versions de l'ESP32, la redirection renvoie le code final, 
    // ou s'arrête en considérant la redirection réussie. 
    // HTTP_CODE_OK = 200 (Tout est parfait)
    if (httpCode == HTTP_CODE_OK)
    {
      WiFiClient& streamClient = http.getStream(); 
      int firmwareSize = http.getSize();
      Serial.print("Firmware Size: ");
      Serial.println(firmwareSize);
      UART.print("Firmware Size: ");
      UART.println(firmwareSize);
      Serial.println("{LANCEMENT_MAJ}");
      UART.println("{LANCEMENT_MAJ}");

      if (Update.begin(firmwareSize))
      {
          size_t written = Update.writeStream(streamClient); // Le téléchargement se fait ici

          if (Update.size() == written)
          {
              Serial.println("{UPDATE_SUCCEED}");
              UART.println("{UPDATE_SUCCEED}");

              if (Update.end())
              {
                  Serial.println("{UPDATE_SUCCEED}");
                  UART.println("{UPDATE_SUCCEED}");
                  ESP.restart(); // La machine redémarre toute seule avec le nouveau code !
              } 
              else 
              {
                  Serial.println("{ERREUR_MAJ}");
                  Serial.println(Update.errorString());
                  UART.println("{ERREUR_MAJ}");
                  UART.println(Update.errorString());
              }
          }
          else
          {
              Serial.println("{NO_SPACE}");  
              UART.println("{NO_SPACE}");
              Serial.println("{ERREUR_MAJ}");
              UART.println("{ERREUR_MAJ}");  
          }
      } 
        else
        {
            Serial.println("UPDATE_ERROR");
            UART.println("UPDATE_ERROR");
            Serial.println("{ERREUR_MAJ}");
            UART.println("{ERREUR_MAJ}");
        }
    }
    else
    {
        Serial.print("Failed to download firmware. HTTP code: ");
        Serial.println(httpCode);
        UART.print("Failed to download firmware. HTTP code: ");
        UART.println(httpCode);
        Serial.println("{ERREUR_MAJ}");
        UART.println("{ERREUR_MAJ}");
    }

    http.end();
}

// ----------------------------------------------------
// FONCTION DE TRAITEMENT DES COMMANDES (COMMUNE)
// ----------------------------------------------------
void traiterCommande(String cmd) {
    Serial.print("Recu : "); Serial.println(cmd);

    // ============================================================
    // CAS 1 : COMMANDE COMPLEXE (Séquence Complète) -> Commence par [
      // Exemple cmd nettoyée : "[{B:3;T:2000},{B:6;T:1500},{P:1;T:3000;Q:5000;C:RED_FRUIT}]"
      //[{B:a;T:b},{B:c;T:d},{B:e;T:f},{B:g;T:h},{P:x;T:y;Q:z;C:w}]
    // ============================================================
    // ============================================================
    // PARSING COMMANDE COMPLEXE (Commence par [ )
    // ============================================================
    if (cmd.startsWith("[")) {
        
        Serial.println(">>> Parsing Sequence Complexe...");

        // 1. Réinitialisation des variables
        nb_bacs_seq = 0;
        seq_b1=0; seq_t1=0; seq_b2=0; seq_t2=0; 
        seq_b3=0; seq_t3=0; seq_b4=0; seq_t4=0;
        seq_pompe=0; seq_sirop=0; seq_eau=0; seq_col="";
        seq_force=0; // Reset Force

        // 2. Nettoyage de la chaine
        String workStr = cmd;
        workStr.replace("[", ""); 
        workStr.replace("]", "");

        // 3. Boucle de lecture
        int curseur = 0;
        while (curseur < workStr.length()) {
            int debut = workStr.indexOf('{', curseur);
            int fin = workStr.indexOf('}', debut);
            if (debut == -1 || fin == -1) break;

            String s = workStr.substring(debut + 1, fin);

            // --- CAS A : C'est un Bac (B:) ---
            if (s.indexOf("B:") >= 0) {
                nb_bacs_seq++; 
                int idxB = s.indexOf("B:") + 2;
                int valB = s.substring(idxB, s.indexOf(";", idxB)).toInt();
                
                int idxT = s.indexOf("T:") + 2;
                int valT = s.substring(idxT).toInt();
                
                if (nb_bacs_seq == 1) { seq_b1 = valB; seq_t1 = valT; }
                else if (nb_bacs_seq == 2) { seq_b2 = valB; seq_t2 = valT; }
                else if (nb_bacs_seq == 3) { seq_b3 = valB; seq_t3 = valT; }
                else if (nb_bacs_seq == 4) { seq_b4 = valB; seq_t4 = valT; }
            }
            
            // --- CAS B : C'est le Liquide (P:) ---
            else if (s.indexOf("P:") >= 0) {
                // 1. Extraction Force (F) si présent
                if (s.indexOf("F:") >= 0) {
                    int idxF = s.indexOf("F:") + 2;
                    int endF = s.indexOf(";", idxF); 
                    if(endF == -1) endF = s.length();
                    seq_force = s.substring(idxF, endF).toInt();
                }

                // Extraction P
                int idxP = s.indexOf("P:") + 2;
                seq_pompe = s.substring(idxP, s.indexOf(";", idxP)).toInt();
                
                // Extraction T
                int idxT = s.indexOf("T:") + 2;
                seq_sirop = s.substring(idxT, s.indexOf(";", idxT)).toInt();
                
                // Extraction Q
                int idxQ = s.indexOf("Q:") + 2;
                seq_eau = s.substring(idxQ, s.indexOf(";", idxQ)).toInt();
                if (seq_eau > 15000) seq_eau = 15000; // <-- SECURITE EAU
                
                // Extraction C (Attention au F éventuel)
                int idxC = s.indexOf("C:") + 2;
                int endC = s.length();
                if (s.indexOf("F:") > 0) {
                    endC = s.indexOf("F:") - 1; // On s'arrête au point-virgule avant F
                }
                seq_col = s.substring(idxC, endC);
            }
            
            curseur = fin + 1;
        }

        Serial.printf("Parsing Fini. Bacs: %d, F=%d\n", nb_bacs_seq, seq_force);
        
        // APPEL AVEC LE NOUVEL ARGUMENT seq_force
        serviceBoisson(nb_bacs_seq, seq_b1, seq_t1, seq_b2, seq_t2, seq_b3, seq_t3, seq_b4, seq_t4, seq_pompe, seq_sirop, seq_eau, seq_col, seq_force);
        
        return; 
    }

    // ============================================================
    // CAS 2 : COMMANDES SIMPLES (Votre code existant)
    // ============================================================

    // --- Parsing Boisson Simple {P:1;T:2000...} ---
    // --- Parsing Boisson Simple {P:1;T:1000;E:12000;C:ICE_TEA;F:0} ---
    if (cmd.indexOf("P:") > 0 && cmd.indexOf(";T:") > 0) {
       
       // 1. Extraction du mode Force (F)
       // On cherche ";F:". Si absent, forceMode reste à 0.
       int forceMode = 0;
       if (cmd.indexOf(";F:") > 0) {
          int fIndex = cmd.indexOf(";F:");
          // On lit le chiffre entre "F:" et l'accolade de fin "}"
          forceMode = cmd.substring(fIndex + 3, cmd.indexOf("}", fIndex)).toInt();
       }
        // 2. Vérification Shaker (Avec délai de 5 secondes)
       bool shakerTrouveSimple = false;

       if (forceMode == 1) {
           // Mode Force : on valide tout de suite
           shakerTrouveSimple = true;
           ignoreShakerSecurity = true; // <-- AJOUT : On active le Bypass
           Serial.println("{BOISSON_LANCEE}");
           UART.println("{BOISSON_LANCEE}");
           attendre(1000);
       } 
       else {
           // On attend jusqu'à 5 secondes que le shaker B soit placé ET STABLE
           ignoreShakerSecurity = false; // <-- AJOUT : Mode normal
           unsigned long startDetectionSimple = millis();
           int confirmationCountB = 0; // Compteur de stabilité

           while (millis() - startDetectionSimple < 5000) {
               float d2 = getDistance(US2_TRIG, US2_ECHO);
               
               if (d2 > 0.1 && d2 < 11) { // 11 cm de marge
                   confirmationCountB++;
                   // Il faut le voir 4 fois de suite (400ms) sans erreur pour valider
                   if (confirmationCountB >= 4) { 
                       shakerPresent_B = true;
                       shakerTrouveSimple = true;
                       break; // Shaker bien posé, on sort !
                   }
               } else {
                   confirmationCountB = 0; // Il a bougé (ex: main devant), on recommence la validation
               }
               attendre(100); 
           }

           if (shakerTrouveSimple) {
               Serial.println("{SHAKER_DETECTE}");
               UART.println("{SHAKER_DETECTE}");
               Serial.println("{BOISSON_LANCEE}");
               UART.println("{BOISSON_LANCEE}");
               attendre(1000);
           }
       }

       // Si après 5 secondes il n'y a toujours rien, on annule
       if (!shakerTrouveSimple) {
           Serial.println("{NO_SHAKER}");
           UART.println("{NO_SHAKER}");
       } 
       else {
           // ---> LE RESTE DU CODE (Extraction des données, etc.) CONTINUE ICI <---

           // 3. Extraction des données
           int pIndex = cmd.indexOf("P:") + 2;
           int tIndex = cmd.indexOf(";T:") + 3;
           
           int pompeNum = cmd.substring(pIndex, cmd.indexOf(";T:")).toInt();
           
           // Lecture Temps (s'arrête au prochain point-virgule, peu importe si c'est E ou C)
           int tEnd = cmd.indexOf(";", tIndex); 
           int tempsMs = cmd.substring(tIndex, tEnd).toInt();
           
           int eauMs = 0; 
           String cValue = "";

           // Lecture Eau (E)
           if (cmd.indexOf(";E:") > 0) {
              int eStart = cmd.indexOf(";E:") + 3;
              int eEnd = cmd.indexOf(";", eStart); // S'arrête au prochain ; (donc C) ou }
              if (eEnd == -1) eEnd = cmd.indexOf("}");
              
              eauMs = cmd.substring(eStart, eEnd).toInt();
              if (eauMs > 15000) eauMs = 15000; // <-- SECURITE EAU
              DUREE_BOISSON = eauMs; 
           } else {
              DUREE_BOISSON = 10000; 
           }

           // Lecture Couleur (C)
           if (cmd.indexOf(";C:") > 0) {
              int cStart = cmd.indexOf(";C:") + 3;
              int cEnd;
              
              // C'est ICI que ça change : Si on a F, on s'arrête avant F. Sinon avant }
              if (cmd.indexOf(";F:") > 0) {
                  cEnd = cmd.indexOf(";F:");
              } else {
                  cEnd = cmd.indexOf("}");
              }
              cValue = cmd.substring(cStart, cEnd);
           }

           // 4. Mapping Couleurs
           if (cValue == "ICE_TEA" || cValue == "MULTIFRUIT" || cValue == "ORANGE") {
               LED_R_Boisson = LED_R_ORANGE; LED_G_Boisson = LED_G_ORANGE; LED_B_Boisson = LED_B_ORANGE;
           } else if (cValue == "APPLE") {
               LED_R_Boisson = LED_R_POMME; LED_G_Boisson = LED_G_POMME; LED_B_Boisson = LED_B_POMME;
           } else if (cValue == "RED_FRUIT") {
               LED_R_Boisson = LED_R_FRAISE; LED_G_Boisson = LED_G_FRAISE; LED_B_Boisson = LED_B_FRAISE;
           } else if (cValue == "LEMON") {
               LED_R_Boisson = LED_R_CITRON; LED_G_Boisson = LED_G_CITRON; LED_B_Boisson = LED_B_CITRON;
           } else if (cValue == "WATER") {
               LED_R_Boisson = LED_R_EAU; LED_G_Boisson = LED_G_EAU; LED_B_Boisson = LED_B_EAU;
           } else {
               LED_R_Boisson = LED_R; LED_G_Boisson = LED_G; LED_B_Boisson = LED_B;
           }

           // 5. Lancement (Pompe 0 autorisée)
           if (pompeNum >= 0 && pompeNum <= 10) {
               activerSortie(pompeNum, tempsMs); // Arome
               activerSortie(11, eauMs);         // Eau (Vanne 1)
               
               progressActive_Boisson = true;
               progressStart_Boisson = millis();
               fill_solid(leds3, NUM_LEDS_3, CRGB::Black);
               FastLED.show();
               
               Serial.printf("Boisson lancée: P%d, Eau(%dms), F=%d\n", pompeNum, eauMs, forceMode);
               UART.printf("{SUCCESS: P%d START}\n", pompeNum);

           }
       }
    }
    // --- GESTION BACS SIMPLE {B:x;T:y} ---
    else if (cmd.indexOf("B:") > 0 && cmd.indexOf(";T:") > 0) {
        
        int bIndex = cmd.indexOf("B:") + 2;
        int tIndex = cmd.indexOf(";T:") + 3;
        
        int bacNum = cmd.substring(bIndex, cmd.indexOf(";T:")).toInt();
        
        // Trouver la fin de la commande
        int endCmd = cmd.indexOf("}"); 
        if(endCmd == -1) endCmd = cmd.length();

        int tempsMs = cmd.substring(tIndex, endCmd).toInt();

        // Appel direct sans vérification
        activerBac(bacNum, tempsMs);
    }
    
    else if (cmd == "{DOOR_OPEN}") {
       Serial.println("Ouverture Porte (10s)...");
       UART.println("{DOOR_OPEN_OK}");
       mcpAutres.digitalWrite(ELECTRO1, LOW);
       mcpAutres.digitalWrite(ELECTRO2, LOW);
       doorUnlocked = true;
       doorUnlockTime = millis();
       deverrouiller_moteurs();
    }
    else if (cmd == "{HOME}") {
      home_chariot();
    }
    else if (cmd.startsWith("{X:")) {
      sendToSlave(cmd);
    }
    else if (cmd == "{CHECK}") {
      Serial.printf("READY");
      UART.printf("READY");
    }
    else if (cmd == "{HOME_PORTE}") {
       home_porte();
    }
    else if (cmd == "{UNLOCK_CHARIOT}") {
       deverrouiller_moteurs();
    }
    else if (cmd == "{LOCK_CHARIOT}") {
       verrouiller_moteurs();
    }
    
    else if (cmd.startsWith("{DOOR:")) {
       // Format attendu : {DOOR:15000}
       int pIndex = cmd.indexOf(":") + 1;
       int endIndex = cmd.indexOf("}");
       int pos = cmd.substring(pIndex, endIndex).toInt();
       
       bouger_porte(pos);
    }

    // --- GESTION MANUELLE PORTE COULISSANTE ---
    else if (cmd == "{OPEN_DOOR}") {
        position_chariot(3);
        position_chariot(4);
        Serial.println("Ouverture manuelle Porte Coulissante...");
        UART.println("Ouverture Porte");
        bouger_porte(-36500); // Envoie la porte à la position ouverte
    } 
    else if (cmd == "{CLOSE_DOOR}") {
        Serial.println("Fermeture manuelle Porte Coulissante...");
        UART.println("Fermeture Porte");
        fermerPorteActive(); // Lance le homing sécurisé de la porte
    }
    
    // --- GESTION NETTOYAGE {NETTOYAGE;T:x} ---
    else if (cmd.indexOf("NETTOYAGE") > 0 && cmd.indexOf(";T:") > 0) {
        
      int tIndex = cmd.indexOf(";T:") + 3;  // 1. On repère où commence la valeur du temps (juste après ";T:")
      int endCmd = cmd.indexOf("}");  // 2. On repère la fin de la commande (l'accolade fermante)
        
      int temps_nettoyage = cmd.substring(tIndex, endCmd).toInt();
      activerSortie(13, temps_nettoyage);    // Gicleurs (Vanne 3)
      Serial.printf("RINCAGE_ACTIVE");
      UART.printf("RINCAGE_ACTIVE");
    }

    // --- GESTION DIRECTE ELECTROVANNES {E:x;T:y} ---
    // x = 1, 2 ou 3 (Numéro Vanne)
    // y = Temps en ms
    else if (cmd.startsWith("{E:") && cmd.indexOf(";T:") > 0) {
        
        // 1. Extraction du numéro de vanne
        int eIndex = cmd.indexOf(":") + 1;
        int tIndex = cmd.indexOf(";T:");
        int vanneNum = cmd.substring(eIndex, tIndex).toInt();
        
        // 2. Extraction de la durée
        int duree = cmd.substring(tIndex + 3, cmd.indexOf("}")).toInt();
        if (duree > 15000) duree = 15000; // <-- SECURITE EAU

        // 3. Mapping vers les ID internes (11, 12, 13)
        int idInterne = 0;
        if (vanneNum == 1) idInterne = 11;      // Vanne 1
        else if (vanneNum == 2) idInterne = 12; // Vanne 2
        else if (vanneNum == 3) idInterne = 13; // Vanne 3 (ou Gicleurs)

        // 4. Activation Non-Bloquante
        if (idInterne != 0) {
            activerSortie(idInterne, duree);
            Serial.printf("VANNE %d ON pour %dms\n", vanneNum, duree);
            UART.printf("VANNE %d ON", vanneNum);
        } else {
            Serial.println("Erreur: Numero Vanne Inconnu (1-3 uniquement)");
        }
    }

    // --- GESTION INTERNET {INTERNET:x} ---
    else if (cmd.startsWith("{INTERNET:")) {
        // On récupère la valeur après les deux points (0 ou 1)
        int status = cmd.substring(cmd.indexOf(":") + 1, cmd.indexOf("}")).toInt();

        if (status == 0) {
            // x=0 : Pas d'internet -> ORANGE (Alerte)
            // On utilise les définitions ORANGE déjà présentes dans ton code
            fill_solid(leds2, NUM_LEDS_2, CRGB(LED_R_ORANGE, LED_G_ORANGE, LED_B_ORANGE));
            Serial.printf("NO_INTERNET");
            UART.printf("NO_INTERNET");
        } else {
            // x=1 : Internet OK -> Retour couleur standard (Vert repos)
            // On utilise les définitions LED_R/G/B du repos
            fill_solid(leds2, NUM_LEDS_2, CRGB(LED_R, LED_G, LED_B));
            Serial.printf("INTERNET_CONNECTE");
            UART.printf("INTERNET_CONNECTE");
        }
        FastLED.show();
    }
    // MISE A JOUR A DISTANCE
    else if (cmd.startsWith("{UPDATE:") && cmd.endsWith("}")) {  // Format attendu : {UPDATE:url}
      int startIdx = cmd.indexOf(":") + 1;       // On commence juste après les deux-points
      int endIdx = cmd.lastIndexOf("}");         // On s'arrête juste avant l'accolade de fin
      Firmware_Update_URL = cmd.substring(startIdx, endIdx);

      UpdateFirmware();  // Lance la mise à jour immédiatement
    }
}

// ----------------------------------------------------
// SETUP
// ----------------------------------------------------
void setup() {
  Serial.begin(9600);
  delay(1000);
  UART.begin(9600, SERIAL_8N1, RXD2, TXD2); 
  delay(1000);

  pinMode(US1_TRIG, OUTPUT); pinMode(US1_ECHO, INPUT);
  pinMode(US2_TRIG, OUTPUT); pinMode(US2_ECHO, INPUT);

  Wire.begin();
  init_machine();
  Serial.print("FIRMWARE V");
  Serial.println(CURRENT_FIRMWARE_VERSION);

  FastLED.addLeds<LED_TYPE, LED_STRIP_1, COLOR_ORDER>(leds1, NUM_LEDS_1);
  FastLED.addLeds<LED_TYPE, LED_STRIP_2, COLOR_ORDER>(leds2, NUM_LEDS_2);
  FastLED.addLeds<LED_TYPE, LED_STRIP_3, COLOR_ORDER>(leds3, NUM_LEDS_3);
  FastLED.setBrightness(40);
  FastLED.setCorrection(TypicalLEDStrip);
  
  CRGB repos = CRGB(LED_R, LED_G, LED_B);
  fill_solid(leds1, NUM_LEDS_1, repos);
  fill_solid(leds3, NUM_LEDS_3, repos);
  FastLED.show();

  // Connect to Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      tentatives_co = tentatives_co + 1;
      if(tentatives_co > 90){
        break;
      }
      delay(1000);
      Serial.println("{CONNEXION_WIFI}");
      fill_solid(leds2, NUM_LEDS_2, CRGB(LED_R_ORANGE, LED_G_ORANGE, LED_B_ORANGE)); // Allumées au démarrage
      FastLED.show();
    }
    Serial.println("{WIFI_CONNECTE}");
    fill_solid(leds2, NUM_LEDS_2, repos);
    //fill_solid(leds2, NUM_LEDS_2, CRGB(LED_R_CHOCOLATE, LED_G_CHOCOLATE, LED_B_CHOCOLATE));
    FastLED.show();

  // --- AJOUT : DEMARRAGE CHRONO HOMING AUTO ---
  lastAutoHomeTime = millis();

  // --- AJOUT : LANCEMENT DE LA TÂCHE LED ---
  // Crée la tâche sur le cœur 0 (Arduino tourne par défaut sur le cœur 1)
  xTaskCreatePinnedToCore(
      TaskLEDCode,   // Fonction de la tâche
      "TaskLED",     // Nom (pour debug)
      10000,         // Taille de la pile (Stack size)
      NULL,          // Paramètres
      1,             // Priorité
      &TaskLEDHandle,// Handle
      0              // Numéro du cœur (0 ou 1)
  );

  home_chariot();
  home_porte();
  Serial.println("ESP maître prêt !");
}

// ----------------------------------------------------
// LOOP
// ----------------------------------------------------
void loop() {

  // Gestion Arrêt Bacs (Timer)
  //updateBacs();

  // 2. GESTION DE LA MACHINE À ÉTATS DU ROBOT
  //gererDeplacementRobot();
  
  // 1. Ultrasons et détection Shaker
  if (millis() - lastUltraTime >= 200) {
    lastUltraTime = millis();
    float d1 = getDistance(US1_TRIG, US1_ECHO); // Complément
    delay(2);
    float d2 = getDistance(US2_TRIG, US2_ECHO);  // Boisson

    // --- LIGNES DE DEBUG À AJOUTER ICI ---
    //Serial.printf("Capteur C (Complement): %.2f cm | Capteur B (Boisson): %.2f cm\n", d1, d2);
    
    // --- CAPTEUR C (Complément) -> INTACT ---
    if (d1 > 0.1 && d1 < 14) {  // Distance en dessous de 14cm
      shakerPresent_C = true;
    } else {
      shakerPresent_C = false;
    }

    // --- CAPTEUR B (Boisson) -> MODIFIÉ AVEC TOLÉRANCE ---
    if (d2 > 0.1 && d2 < 11) {  // Distance en dessous de 11cm (plus large)
      shakerPresent_B = true;
      compteurPerteB = 0; // Il est là, on remet à zéro
    } else {
      compteurPerteB++;
      // Tolérance : Il faut 3 lectures ratées de suite (3 x 200ms = 0.6 sec) pour couper
      if (compteurPerteB >=3) {
          shakerPresent_B = false;
      }
    }
  }

// --- SÉCURITÉ RETRAIT SHAKER (PAUSE + TOLÉRANCE 5 SECONDES) ---
  if (progressActive_Boisson && !ignoreShakerSecurity) {
      if (!shakerPresent_B) {
          // 1. Le shaker vient d'être enlevé : on met en pause les liquides
          if (timeShakerLost == 0) {
              timeShakerLost = millis();
              Serial.println("Shaker perdu ! PAUSE des liquides. Attente de 5s...");
              pauseLiquides(); // <-- Coupe l'eau et le sirop
          } 
          // 2. Le chrono tourne : on vérifie si les 5 secondes sont écoulées
          else if (millis() - timeShakerLost >= 5000) {
              Serial.println("Délai de 5s dépassé. Arrêt d'urgence définitif.");
              stopAllService();
              timeShakerLost = 0; // On reset
          }
      } 
      else {
          // 3. Le shaker est de retour !
          if (timeShakerLost != 0) {
              Serial.println("Shaker retrouvé ! REPRISE du service.");
              // Pour la barre LED : on décale son point de départ pour compenser le temps passé en pause
              progressStart_Boisson += (millis() - timeShakerLost); 
              reprendreLiquides(); // <-- Rallume l'eau et le sirop
              timeShakerLost = 0;
          }
      }
  } else {
      timeShakerLost = 0;
  }
  // -----------------------------------------------------------

  // 4. Commandes (Lecture Intelligente)
  if (Serial.available()) {
    char c = Serial.peek(); // On regarde le premier caractère sans l'enlever
    String cmd = ""; 
    
    if (c == '[') {
        // C'est une SEQUENCE -> On lit jusqu'au crochet fermant ]
        cmd = Serial.readStringUntil(']');
        cmd += "]"; // On remet le crochet pour que le parser le reconnaisse
    } 
    else if (c == '{') {
        // C'est une COMMANDE SIMPLE -> On lit jusqu'à l'accolade fermante }
        cmd = Serial.readStringUntil('}');
        cmd += "}";
    }
    else {
        // Caractère parasite (\n, espace...) -> On vide
        Serial.read();
    }

    cmd.trim();
    if (cmd.length() > 2) traiterCommande(cmd);
  }

  // Pareil pour l'UART (Ecran)
  if (UART.available()) {
    char c = UART.peek();
    String cmd = "";
    
    if (c == '[') {
        cmd = UART.readStringUntil(']');
        cmd += "]";
    } 
    else if (c == '{') {
        cmd = UART.readStringUntil('}');
        cmd += "}";
    }
    else {
        UART.read();
    }

    cmd.trim();
    if (cmd.length() > 2) traiterCommande(cmd);
  }

  // 2. LECTURE DES COMMANDES (Serial ET UART)
  
  /*// A. USB Serial
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('}'); 
    cmd += "}"; 
    cmd.trim(); 
    if (cmd.startsWith("{") || cmd.startsWith("[")) traiterCommande(cmd);
  }

  // B. UART (Serial2)
  if (UART.available()) {
    String cmd = UART.readStringUntil('}'); 
    cmd += "}"; 
    cmd.trim(); 
    if (cmd.startsWith("{") || cmd.startsWith("[")) traiterCommande(cmd);
  }*/

  // 3. Gestion Porte Auto-Lock
  if (doorUnlocked && (millis() - doorUnlockTime >= DOOR_DELAY)) {
    mcpAutres.digitalWrite(ELECTRO1, HIGH);
    mcpAutres.digitalWrite(ELECTRO2, HIGH);
    doorUnlocked = false;
    Serial.println("Porte reverrouillée auto.");
  }

  // 4. Gestion Arret Pompes/Vannes
  unsigned long now = millis();
  bool auMoinsUnePompeActive = false; // <-- AJOUT pour le juge de paix

  for (int i = 1; i <= 13; i++) { // Jusqu'à 13
    // --- CORRECTIF : On ignore les pompes qui sont en pause ---
    if (outputState[i].active && !outputState[i].isPaused && (now - outputState[i].startTime >= outputState[i].duration)) {
      
      // Extinction physique
      if (i <= 8) mcpPompes.digitalWrite(pompePinsMap[i], LOW);
      else if (i == 9) mcpAutres.digitalWrite(POMPE9, LOW);
      else if (i == 10) mcpBacs.digitalWrite(POMPE10, LOW);
      else if (i == 11) mcpBacs.digitalWrite(VANNE1, LOW);
      else if (i == 12) mcpBacs.digitalWrite(VANNE2, LOW);
      else if (i == 13) mcpBacs.digitalWrite(VANNE3, LOW);
      
      outputState[i].active = false;
      Serial.printf("Fin sortie %d\n", i);
    }

    // On regarde s'il reste encore au moins un liquide en train de couler (ou en pause)
    if (outputState[i].active) {
        auMoinsUnePompeActive = true;
    }
  }

  // --- LE JUGE DE PAIX (Fermeture propre de la commande simple) ---
  // Si la machine pensait servir une boisson, MAIS qu'il n'y a plus aucune pompe active,
  // C'EST QUE LE SERVICE EST VRAIMENT FINI ET SÉCURISÉ.
  if (progressActive_Boisson && !auMoinsUnePompeActive) {
      
      progressActive_Boisson = false; // On coupe les sécurités anti-retrait
      ignoreShakerSecurity = false;   // <-- AJOUT : Désactivation du bypass naturel
      
      // On remet les LEDs au repos
      CRGB repos = CRGB(LED_R, LED_G, LED_B);
      fill_solid(leds1, NUM_LEDS_1, repos);
      fill_solid(leds2, NUM_LEDS_2, repos);
      fill_solid(leds3, NUM_LEDS_3, repos);
      FastLED.show();

      // On notifie l'application que c'est une fin naturelle
      Serial.println("{BOISSON_TERMINEE}");
      UART.println("{BOISSON_TERMINEE}");
  }
  // ----------------------------------------------------------------

  // 5. Animation LED Progress Bar -- Boisson
  if (progressActive_Boisson && shakerPresent_B) {
    unsigned long elapsed = millis() - progressStart_Boisson;
    
    // Si le temps de base est écoulé, on fige simplement la barre à 100%
    if (elapsed >= DUREE_BOISSON) {
        fill_solid(leds3, NUM_LEDS_3, CRGB(LED_R_Boisson, LED_G_Boisson, LED_B_Boisson));
        FastLED.show();
    } else {
      float progress = (float)elapsed / DUREE_BOISSON;
      
      auto updateStrip = [&](CRGB* strip, int numLeds) {
         int ledsComplete = (int)(progress * numLeds);
         float fraction = (progress * numLeds) - ledsComplete;
         fill_solid(strip, numLeds, CRGB::Black); 
         for(int i=0; i<ledsComplete; i++) {
            strip[i] = CRGB(LED_R_Boisson, LED_G_Boisson, LED_B_Boisson);
         }
         if (ledsComplete < numLeds) {
            strip[ledsComplete] = CRGB(
              LED_R_Boisson * fraction, 
              LED_G_Boisson * fraction, 
              LED_B_Boisson * fraction
            );
         }
      };

      updateStrip(leds3, NUM_LEDS_3);
      FastLED.show();
    }
  }

  // 5. Animation LED Progress Bar -- Complement
  if (progressActive_Complement && shakerPresent_C) {
    unsigned long elapsed = millis() - progressStart_Complement;
    if (elapsed >= DUREE_BOISSON) {
      progressActive_Complement = false;
      CRGB repos = CRGB(LED_R, LED_G, LED_B);
      fill_solid(leds1, NUM_LEDS_1, repos);
      fill_solid(leds2, NUM_LEDS_2, repos);
      fill_solid(leds3, NUM_LEDS_3, repos);
      FastLED.show();
    } else {
      float progress = (float)elapsed / DUREE_BOISSON;
      
      auto updateStrip = [&](CRGB* strip, int numLeds) {
         int ledsComplete = (int)(progress * numLeds);
         float fraction = (progress * numLeds) - ledsComplete;
         fill_solid(strip, numLeds, CRGB::Black); 
         for(int i=0; i<ledsComplete; i++) {
            strip[i] = CRGB(LED_R_Boisson, LED_G_Boisson, LED_B_Boisson);
         }
         if (ledsComplete < numLeds) {
            strip[ledsComplete] = CRGB(
              LED_R_Boisson * fraction, 
              LED_G_Boisson * fraction, 
              LED_B_Boisson * fraction
            );
         }
      };

      //updateStrip(leds1, NUM_LEDS_1);
      updateStrip(leds2, NUM_LEDS_2);
      //updateStrip(leds3, NUM_LEDS_3);
      FastLED.show();
    }
  }

  // 6. Gestion Endstops
  if (homingActive) {
    int s1 = !mcpAutres.digitalRead(ENDSTOP1); // X
    int s2 = !mcpAutres.digitalRead(ENDSTOP2); // Y1
    int s3 = !mcpAutres.digitalRead(ENDSTOP3);  // Y2

    if (s1 != lastStateS1) { if (s1 == 1) sendToSlave("{E:1;S:1}"); lastStateS1 = s1; }
    if (s2 != lastStateS2) { if (s2 == 1) sendToSlave("{E:2;S:1}"); lastStateS2 = s2; }
    if (s3 != lastStateS3) { if (s3 == 1) sendToSlave("{E:3;S:1}"); lastStateS3 = s3; }

    if (lastStateS1 == 1 && lastStateS2 == 1 && lastStateS3 == 1) {
      homingActive = false;
      Serial.println("Homing terminé.");
    }
    delay(10);
    // --- NOUVEL AJOUT : PLACEMENT AU CENTRE APRÈS LE HOMING ---
      //position_chariot(2);
      //last_position_chariot = 1; // 1 correspond à ta position CENTRE REPOS
      // ---------------------------------------------------------
  }

  // 7. Gestion Homing Porte (Indépendant)
  if (homingPorteActive) {
    int s4 = !mcpAutres.digitalRead(ENDSTOP4); // Lecture Endstop 4 (Porte)

    if (s4 != lastStateS4) { 
      if (s4 == 1) { 
        sendToSlave("{E:4;S:1}"); // On dit à l'esclave STOP (E:4)
        homingPorteActive = false; // On arrête la surveillance
        Serial.println("Homing Porte terminé.");
      } 
      lastStateS4 = s4; 
    }
    delay(10);
  }

  // --- 8. GESTION HOMING AUTOMATIQUE (MAINTENANCE) ---
  if (millis() - lastAutoHomeTime >= AUTO_HOME_INTERVAL) {
      
      // Sécurité : On ne lance le homing auto QUE si la machine est au repos complet
      if (!progressActive_Boisson && !progressActive_Complement && !doorUnlocked && !homingActive && !homingPorteActive) {
          
          Serial.println("--- Lancement du Homing Automatique de maintenance (1h) ---");
          
          verrouiller_moteurs(); // Par sécurité
          attendre(100);
          
          // On utilise la fonction bloquante pour être sûr que tout se recale bien
          homerChariotActif(); 
          
          deverrouiller_moteurs(); // On relâche les moteurs une fois fini
          
          // On réinitialise le chrono pour la prochaine heure
          lastAutoHomeTime = millis(); 
      }
  }

  // --- 9. GESTION DU NOUVEL ENDSTOP (PORTE) & HOMING ---
  // Logique inversée (!read) : 1 = Capteur Écrasé, 0 = Capteur Relâché
  int etatPorte = !mcpAutres.digitalRead(ENDSTOP_PORTE_OUVERTE);

  // 1. La porte S'OUVRE -> Elle vient écraser le capteur (1)
  if (etatPorte == 0 && !memoirePorteOuverte) {
      memoirePorteOuverte = true; // On mémorise l'ouverture
      Serial.println("Porte détectée OUVERTE (Capteur écrasé).");
      deverrouiller_moteurs();
  }

  // 2. La porte SE FERME -> Elle libère le capteur qui repasse à 0
  if (memoirePorteOuverte && etatPorte == 1) {
      Serial.println("Porte détectée REFERMÉE. Reverrouillage et Homing...");
      
      verrouiller_moteurs(); // 1. On redonne de la force aux moteurs
      attendre(100);         // 2. Petite pause de sécurité
      homerChariotActif();   // 3. On lance le homing du chariot
      
      memoirePorteOuverte = false; // 4. On réinitialise la mémoire
  }

  // --- 10. GESTION DU NETTOYAGE AUTOMATIQUE POST-COMPLEXE ---
  // A. Nettoyage après 1 minute (60 000 millisecondes)
  if (attenteNettoyage1Min && (millis() - chronoFinServiceComplexe >= 60000)) {
      attenteNettoyage1Min = false; // On coupe ce chrono
      activerSortie(13, 20000);     // Active Vanne 3 pendant 20s
      Serial.println("{NETTOYAGE_AUTO_1MIN_LANCE}");
      UART.println("{NETTOYAGE_AUTO_1MIN_LANCE}");
  }
  
  // B. Nettoyage après 10 minutes (600 000 millisecondes)
  if (attenteNettoyage10Min && (millis() - chronoFinServiceComplexe >= 600000)) {
      attenteNettoyage10Min = false; // On coupe ce chrono
      activerSortie(13, 20000);      // Active Vanne 3 pendant 20s
      Serial.println("{NETTOYAGE_AUTO_10MIN_LANCE}");
      UART.println("{NETTOYAGE_AUTO_10MIN_LANCE}");
  }
  // ----------------------------------------------------------

}