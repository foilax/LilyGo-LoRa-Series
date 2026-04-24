/*
   PROJET : Télémétrie LoRa / MQTT / LLM
   FICHIER : Recepteur_Passerelle_Intelligente.ino
   RÔLE : Passerelle centrale (Gateway). Reçoit les données LoRa, interroge un LLM
          via Wi-Fi, publie les résultats complets sur un broker MQTT WebSockets (WSS), 
          et renvoie une commande simplifiée (ON/OFF) au nœud distant.
   MATÉRIEL : LilyGo T-Beam Supreme (ESP32-S3) - Requis : Partition "Huge APP" (3MB)
*/

#include <RadioLib.h>
#include "LoRaBoards.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h> 
#include <Ticker.h> 
#include "mqtt_client.h" 
#include <esp_crt_bundle.h> // Requis pour la vérification SSL native

// =========================================================================
// ==================== CONFIGURATION UTILISATEUR ==========================
// =========================================================================

// --- CÂBLAGE & LORA ---
constexpr uint8_t LED_STATUS_PIN = 6; 
constexpr uint8_t LORA_SYNC_WORD = 0x12; 
constexpr float   CONFIG_RADIO_FREQ = 915.0;

// --- PARAMÈTRES DE CACHE ---
constexpr int SEUIL_TOLERANCE_CACHE = 50; 

// --- WI-FI ---
constexpr bool USE_WPA2_ENTERPRISE = true; // 'true' pour réseau scolaire/entreprise, 'false' pour Wi-Fi domestique

// Si USE_WPA2_ENTERPRISE = true 
const char* EAP_IDENTITY  = "VOTRE_IDENTIFIANT_ENTREPRISE";
const char* EAP_USERNAME  = "VOTRE_NOM_UTILISATEUR_ENTREPRISE";
const char* EAP_PASSWORD  = "VOTRE_MOT_DE_PASSE_ENTREPRISE";

// Si USE_WPA2_ENTERPRISE = false
const char* WIFI_SSID     = "VOTRE_NOM_DE_RESEAU_WIFI"; 
const char* WIFI_PASS     = "VOTRE_MOT_DE_PASSE_WIFI"; 

// --- API LLM (OPENROUTER) ---
const char* API_URL       = "https://openrouter.ai/api/v1/chat/completions";
const char* API_KEY       = "VOTRE_CLE_API_OPENROUTER"; // ex: "sk-or-v1-..."
const char* MODEL_NAME    = "nvidia/nemotron-3-super-120b-a12b:free"; 

// --- MQTT (WebSockets Sécurisés - WSS) ---
const char* MQTT_URI      = "wss://votre-serveur-mqtt.com:443/mqtt"; // L'adresse WebSockets
const char* MQTT_TOPIC    = "lora/llm/status"; 
const char* MQTT_USER     = "VOTRE_UTILISATEUR_MQTT";
const char* MQTT_PASS     = "VOTRE_MOT_DE_PASSE_MQTT";

// =========================================================================
// =========================================================================

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
WiFiClientSecure apiSecureClient;  
Ticker ledBlinker; 
esp_mqtt_client_handle_t mqtt_client; 

// --- VARIABLES GLOBALES ---
static volatile bool actionDoneFlag = false;
String payloadReçu = "--";
String reponseOLED = "OFF"; 
String repLLMCache = "";    
int lastProcessedPot = -999; 

String wifiStatus = "WIFI: WAIT";
String sysStatus  = "LORA: WAIT";
String rssi = "--";
String snr = "--";

// --- FONCTIONS ---
#if defined(ESP32) || defined(ESP8266)
IRAM_ATTR
#endif
void setFlag(void) { 
    actionDoneFlag = true; 
}

void basculerLED() { 
    digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN)); 
}

void drawMain() {
    if (disp) {
        disp->clearBuffer();
        disp->drawRFrame(0, 0, 128, 64, 3);
        
        disp->setFont(u8g2_font_pxplusibmvga8_mr); 
        disp->setCursor(5, 12); disp->print(wifiStatus);
        disp->setCursor(5, 26); disp->print(sysStatus);
        disp->setCursor(5, 40); disp->print("POT: " + payloadReçu); 
        disp->setCursor(5, 52); disp->print("LLM: " + reponseOLED);
        
        disp->setFont(u8g2_font_u8glib_4_tr); 
        disp->setCursor(5, 62); disp->print("Sig: " + snr + " | " + rssi);
        
        disp->sendBuffer();
    }
}

// --- Événements MQTT ---
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            Serial.println("[MQTT] Authentification réussie ! Connecté via WSS.");
            break;
        case MQTT_EVENT_DISCONNECTED:
            Serial.println("[MQTT] Déconnecté.");
            break;
        case MQTT_EVENT_ERROR:
            Serial.println("[MQTT] Erreur critique de connexion.");
            break;
        default:
            break;
    }
}

void setupWiFiAndMQTT() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    
    if (USE_WPA2_ENTERPRISE) { 
        WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD); 
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    
    int tentatives = 0;
    while (WiFi.status() != WL_CONNECTED && tentatives < 30) { 
        delay(500); 
        tentatives++; 
    }
    
    wifiStatus = (WiFi.status() == WL_CONNECTED) ? "IP: " + WiFi.localIP().toString() : "WIFI: ECHEC";
    
    apiSecureClient.setInsecure(); 
    drawMain();

    if (WiFi.status() == WL_CONNECTED) {
        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = MQTT_URI;
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach; 
        mqtt_cfg.credentials.username = MQTT_USER;
        mqtt_cfg.credentials.authentication.password = MQTT_PASS;

        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
        esp_mqtt_client_start(mqtt_client); 
    }
}

void publierMQTT(String fullJson) {
    if (WiFi.status() == WL_CONNECTED) {
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, fullJson.c_str(), 0, 0, 0);
    }
}

String callOpenRouter(int val) {
    if (WiFi.status() != WL_CONNECTED) {
        return "{\"decision\":\"OFF\",\"message\":\"No WiFi\"}";
    }
    
    HTTPClient http;
    http.setTimeout(15000);
    http.begin(apiSecureClient, API_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(API_KEY));

    // En-têtes recommandés par OpenRouter
    http.addHeader("HTTP-Referer", "https://votre-domaine.com"); 
    http.addHeader("X-Title", "Votre_Nom_Application");        

    String body = "{\"model\":\"" + String(MODEL_NAME) + "\",\"messages\":["
                  "{\"role\":\"system\",\"content\":\"Tu es un assistant auto. Analyse valeur_pot (0-4095). "
                  "1. Si > 2000, decision=ON, sinon OFF. "
                  "2. Si valeur < 1000, message='La poubelle est presque vide'. "
                  "Si valeur > 3000, message='La poubelle est pleinne'. "
                  "Sinon, message='la poubelle est a moitier pleine''fais un message humoristique'. "
                  "Reponds en JSON: {\\\"decision\\\":\\\"...\\\",\\\"message\\\":\\\"...\\\"}\"},"
                  "{\"role\":\"user\",\"content\":\"{\\\"valeur_pot\\\":" + String(val) + "}\"}]}";

    int code = http.POST(body);
    String result = "{\"decision\":\"ERR\",\"message\":\"HTTP " + String(code) + "\"}";
    
    if (code == 200) {
        String respRaw = http.getString();
        JsonDocument respDoc;
        deserializeJson(respDoc, respRaw);
        String content = respDoc["choices"][0]["message"]["content"].as<String>();
        
        int start = content.indexOf('{');
        int end = content.lastIndexOf('}');
        if (start >= 0 && end >= 0) {
            result = content.substring(start, end + 1);
        }
    }
    http.end();
    return result;
}

void setup() {
    setupBoards();
    delay(500);
    
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);
    
    for (int i = 0; i < 3; i++) { 
        digitalWrite(LED_STATUS_PIN, HIGH); 
        delay(100); 
        digitalWrite(LED_STATUS_PIN, LOW); 
        delay(100); 
    }

    setupWiFiAndMQTT();
    
    radio.begin();
    radio.setDio1Action(setFlag);
    radio.setFrequency(CONFIG_RADIO_FREQ);
    radio.setSyncWord(LORA_SYNC_WORD); 
    radio.startReceive();
}

void loop() {
    if (actionDoneFlag) {
        actionDoneFlag = false;
        String raw;
        
        if (radio.readData(raw) == RADIOLIB_ERR_NONE) {
            rssi = String(radio.getRSSI()) + "dBm";
            snr = String(radio.getSNR()) + "dB";
            
            JsonDocument d;
            if (!deserializeJson(d, raw) && d.containsKey("valeur_pot")) {
                int v = d["valeur_pot"];
                payloadReçu = String(v);
                
                if (abs(v - lastProcessedPot) > SEUIL_TOLERANCE_CACHE || lastProcessedPot == -999) {
                    lastProcessedPot = v;
                    sysStatus = "APPEL LLM...";
                    drawMain();
                    
                    ledBlinker.attach_ms(50, basculerLED); 
                    repLLMCache = callOpenRouter(v); 
                    ledBlinker.detach();
                    digitalWrite(LED_STATUS_PIN, LOW);
                    
                    JsonDocument resDoc;
                    deserializeJson(resDoc, repLLMCache);
                    reponseOLED = resDoc["decision"].as<String>(); 
                    
                    sysStatus = "MQTT PUBLISH...";
                    drawMain();
                    publierMQTT(repLLMCache); 
                    
                } else {
                    sysStatus = "CACHE UTILISEE";
                    drawMain();
                }
                
                sysStatus = "TX RETOUR";
                drawMain();
                String miniJson = "{\"decision\":\"" + reponseOLED + "\"}"; 
                radio.transmit(miniJson); 

                delay(10); 
                actionDoneFlag = false; 
            }
            
            sysStatus = "LORA: RX MODE";
            radio.startReceive();
            drawMain();
        } else {
            radio.startReceive();
        }
    }
}