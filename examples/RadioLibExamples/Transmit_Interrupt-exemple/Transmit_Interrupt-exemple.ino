/*
   PROJET : Télémétrie LoRa / MQTT / LLM
   FICHIER : Emetteur_Capteur_LoRa.ino
   RÔLE : Nœud capteur distant. Lit un potentiomètre, lisse la valeur, 
          l'envoie par LoRa toutes les 5 secondes et affiche la décision (ON/OFF)
          renvoyée par la passerelle intelligente.
   MATÉRIEL : LilyGo T-Beam Supreme (ESP32-S3)
*/

#include "LoRaBoards.h"
#include <RadioLib.h>
#include <ArduinoJson.h>

// --- CÂBLAGE ---
constexpr uint8_t POT_PIN = 3;        
constexpr uint8_t LED_ACTION_PIN = 6; 

// --- CONFIGURATION RADIO LORA ---
constexpr float   CONFIG_RADIO_FREQ         = 915.0; 
constexpr int8_t  CONFIG_RADIO_OUTPUT_POWER = 22;
constexpr float   CONFIG_RADIO_BW           = 125.0;
constexpr uint8_t LORA_SYNC_WORD            = 0x12; 

// --- PARAMÈTRES DE DÉLAI ET DE LISSAGE ---
constexpr unsigned long INTERVALLE_ENVOI_MS = 5000;
constexpr unsigned long TIMEOUT_TX_MS       = 1000;
constexpr unsigned long TIMEOUT_RX_MS       = 4000;
constexpr int           NBR_ECHANTILLONS    = 20;

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

// --- VARIABLES GLOBALES ---
static volatile bool actionDoneFlag = false;
String displayStatus = "INIT";
String decisionRecue = "--";
int smoothedPotValue = 0;
unsigned long dernierEnvoi = 0;

// --- FONCTIONS ---
#if defined(ESP32) || defined(ESP8266)
IRAM_ATTR
#endif
void setFlag(void) { 
    actionDoneFlag = true; 
}

void drawMain() {
    if (disp) {
        disp->clearBuffer();
        disp->drawRFrame(0, 0, 128, 64, 3);
        disp->setFont(u8g2_font_pxplusibmvga8_mr); 
        
        disp->setCursor(5, 15); disp->print("POT: " + String(smoothedPotValue)); 
        disp->setCursor(5, 35); disp->print("STAT: " + displayStatus);
        disp->setCursor(5, 55); disp->print("LLM : " + decisionRecue);
        
        disp->sendBuffer();
    }
}

void setup() {
    setupBoards();
    delay(1500);
    
    pinMode(LED_ACTION_PIN, OUTPUT);
    digitalWrite(LED_ACTION_PIN, LOW); 

    radio.begin();
    radio.setDio1Action(setFlag);
    radio.setFrequency(CONFIG_RADIO_FREQ);
    radio.setSyncWord(LORA_SYNC_WORD); 
    radio.startReceive();
}

void loop() {
    long sommePot = 0;
    for (int i = 0; i < NBR_ECHANTILLONS; i++) {
        sommePot += analogRead(POT_PIN);
        delay(5); 
    }
    smoothedPotValue = sommePot / NBR_ECHANTILLONS;
    
    drawMain(); 

    unsigned long tempsActuel = millis();
    if (tempsActuel - dernierEnvoi >= INTERVALLE_ENVOI_MS) {
        dernierEnvoi = tempsActuel;
        
        JsonDocument doc;
        doc["valeur_pot"] = smoothedPotValue;
        String msg;
        serializeJson(doc, msg);
        
        displayStatus = "TX EN COURS...";
        drawMain();
        
        actionDoneFlag = false;
        radio.startTransmit(msg);
        
        unsigned long t0 = millis();
        while (!actionDoneFlag && (millis() - t0 < TIMEOUT_TX_MS)) {
            yield(); 
        }
        
        displayStatus = "ATTENTE LLM";
        drawMain();
        radio.startReceive();
        
        unsigned long t1 = millis();
        bool recu = false;
        
        while (millis() - t1 < TIMEOUT_RX_MS) { 
            if (actionDoneFlag) {
                String rep;
                if (radio.readData(rep) == RADIOLIB_ERR_NONE) {
                    JsonDocument rxDoc;
                    if (!deserializeJson(rxDoc, rep)) {
                        decisionRecue = rxDoc["decision"].as<String>();
                        decisionRecue.trim();
                        
                        digitalWrite(LED_ACTION_PIN, (decisionRecue.indexOf("ON") >= 0));
                        recu = true;
                        break;
                    }
                }
            }
            yield(); 
        }
        
        if (!recu) {
            decisionRecue = "TIMEOUT";
        }
        displayStatus = "PRET";
    }
}