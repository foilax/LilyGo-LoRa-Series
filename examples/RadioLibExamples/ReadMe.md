# 📡 Projet : Télémétrie LoRa / MQTT / LLM (Passerelle Intelligente)

Ce projet implémente un système de télémétrie de bout en bout utilisant des microcontrôleurs ESP32-S3. Il permet à un capteur distant (simulant le niveau de remplissage d'une poubelle) d'envoyer ses données par fréquence radio **LoRa** à une passerelle (Gateway). La passerelle interroge ensuite une **Intelligence Artificielle (LLM)** via Wi-Fi pour prendre une décision, publie les résultats sur un serveur **MQTT sécurisé (WSS)**, et renvoie un ordre d'action au capteur distant.

---

## 🏗️ Architecture du Système

1. **Nœud Capteur (Émetteur LoRa) :**
   - Lit la valeur d'un potentiomètre (simulant un niveau de remplissage).
   - Lisse la donnée pour éviter le bruit.
   - Envoie la valeur via LoRa à la passerelle toutes les 5 secondes.
   - Attend la réponse et allume/éteint une LED en fonction de la décision de l'IA.

2. **Passerelle Intelligente (Récepteur / Gateway) :**
   - Reçoit le paquet LoRa.
   - Se connecte au réseau Wi-Fi (Supporte WPA2-Enterprise pour les campus/entreprises ou WPA2-Personal).
   - Utilise un système de cache pour éviter de spammer l'API (n'appelle l'IA que si la valeur a significativement changé).
   - Interroge l'API **OpenRouter** (Modèle Nemotron 3 Super) avec un *prompt* système pour analyser la donnée et générer une réponse humoristique.
   - Publie la réponse complète (Décision + Message) sur un Broker **MQTT via WebSockets Sécurisés (WSS)**.
   - Renvoie une décision simplifiée (`ON` ou `OFF`) au nœud capteur par LoRa.

---

## 🛠️ Matériel Requis

* **2x** Cartes LilyGo T-Beam Supreme (ESP32-S3 avec puce SX1262 LoRa).
* **1x** Potentiomètre (connecté à la broche 3 du Nœud Capteur).
* **2x** LEDs avec résistances (Broche 6 sur chaque carte pour le statut/action).
* Câbles Dupont et Breadboards.

---

## 💻 Prérequis Logiciels

### Bibliothèques Arduino IDE
Assurez-vous d'installer les bibliothèques suivantes via le Gestionnaire de bibliothèques Arduino :
* `RadioLib` (Pour la communication LoRa SX1262)
* `ArduinoJson` (Pour le formatage et parsing des données)
* `WiFiClientSecure` & `HTTPClient` (Incluses avec le core ESP32)
* Dépendances spécifiques LilyGo (`LoRaBoards.h`, gestion de l'écran OLED)

### ⚠️ Paramètres de Compilation CRITIQUES (Passerelle)
La passerelle embarque des bibliothèques lourdes (SSL/TLS natif, WebSockets, requêtes HTTP volumineuses). **Vous devez absolument modifier le schéma de partitionnement dans l'IDE Arduino :**
* **Outils > Partition Scheme > Huge APP (3MB No OTA / 1MB SPIFFS)**

---

## ⚙️ Configuration & Installation

### 1. Configuration du Nœud Capteur (`Emetteur_Capteur_LoRa.ino`)
Aucune configuration réseau n'est requise. Vérifiez simplement que :
* `CONFIG_RADIO_FREQ` correspond à la fréquence de votre région (ex: `915.0` pour l'Amérique du Nord, `868.0` pour l'Europe).
* `LORA_SYNC_WORD` (`0x12`) est identique à la passerelle pour créer un réseau privé.

### 2. Configuration de la Passerelle (`Recepteur_Passerelle_Intelligente.ino`)
Ouvrez le fichier et modifiez la section `=== CONFIGURATION UTILISATEUR ===` :

**A. Wi-Fi :**
* Mettez `USE_WPA2_ENTERPRISE` à `true` ou `false` selon votre environnement.
* Remplissez vos identifiants (`EAP_IDENTITY`, `EAP_USERNAME`, `EAP_PASSWORD` pour les campus, ou `WIFI_SSID`, `WIFI_PASS` pour le domicile).

**B. API OpenRouter (LLM) :**
* Obtenez une clé API sur [OpenRouter.ai](https://openrouter.ai/).
* Remplacez `VOTRE_CLE_API_OPENROUTER` par votre clé.
* Vous pouvez modifier `MODEL_NAME` (ex: `nvidia/nemotron-3-super-120b-a12b:free` ou `openai/gpt-3.5-turbo`).

**C. MQTT :**
* Renseignez `MQTT_URI` (ex: `wss://mqtt.votre-serveur.com:443/mqtt`).
* Ajoutez vos identifiants (`MQTT_USER`, `MQTT_PASS`).

---

## 🧠 Fonctionnement de l'Intelligence Artificielle

Le système utilise un prompt structuré injecté dynamiquement dans la requête JSON vers OpenRouter :

1. L'IA reçoit la valeur analogique (`0-4095`).
2. Si la valeur est `> 2000`, la décision est `ON` (Poubelle pleine -> Allume la LED du capteur). Sinon `OFF`.
3. L'IA génère un message texte personnalisé et humoristique (ex: "Je déborde, au secours !").
4. Le système filtre la réponse pour n'extraire que le format JSON valide afin d'éviter les erreurs de parsing causées par le texte d'enrobage (hallucinations du LLM).

---

## 🐛 Dépannage (Troubleshooting)

* **L'ESP32 redémarre en boucle (Guru Meditation Error / Watchdog) :**
  Assurez-vous que l'interruption `setFlag()` utilise bien la directive `IRAM_ATTR`. Vérifiez également que les boucles `while` d'attente contiennent l'instruction `yield();`.
* **La passerelle ne se connecte pas au Wi-Fi Enterprise :**
  Les réseaux WPA2-Enterprise peuvent nécessiter des certificats spécifiques. Assurez-vous que le signal est suffisamment fort.
* **Erreur `HTTP -1` (Connection Refused) avec l'API :**
  Vérifiez votre clé API et assurez-vous que `apiSecureClient.setInsecure();` est bien appelé avant la requête (ou utilisez le `esp_crt_bundle_attach` pour une vraie vérification SSL).
* **Communication LoRa instable :**
  Assurez-vous que l'antenne est bien branchée **avant** d'alimenter les cartes pour ne pas griller l'amplificateur radio.

---
*Projet réalisé dans le cadre d'une intégration Télémétrie/IoT/IA.*