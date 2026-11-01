#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "secrets.h"

// ---------- Broches ----------
const int PIN_DHT = 15;
const int PIN_RELAIS = 26;
const int PIN_LED_ROUGE = 27;
const int PIN_BUZZER = 14;

// ---------- Seuils ----------
const float TEMP_COUPURE = 75.0;
const float TEMP_REDEMARRE = 60.0;
const float VIB_COUPURE = 3.0;

// ---------- Logique du relais ----------
const int RELAIS_ON = HIGH;
const int RELAIS_OFF = LOW;

// ---------- Topics MQTT ----------
const char* T_MES   = "usine/ligne1/moteur/mesures";      // JSON : temperature + vibration + statut
const char* T_STAT  = "usine/ligne1/moteur/statut";
const char* T_DEF   = "usine/ligne1/moteur/defaut";
const char* T_CMD   = "usine/ligne1/moteur/cmd/relais";
const char* T_ALERT = "usine/ligne1/alertes";
const char* T_ETAT  = "usine/ligne1/esp32/etat";
const char* CLIENT_ID = "esp32-moteur-1";

// ---------- Objets ----------
DHT dht(PIN_DHT, DHT22);
Adafruit_MPU6050 mpu;
WiFiClientSecure clientTls;
PubSubClient mqtt(clientTls);

// ---------- Etat ----------
bool moteurMarche = true;
bool arretDistant = false;
bool ntpLance = false;
unsigned long derniereMesure = 0;
unsigned long dernierEssaiMqtt = 0;
const unsigned long PERIODE_MESURE = 2000;
const unsigned long PERIODE_RECONNEXION = 5000;

// ---------- Publication (seulement si connecte) ----------
void publier(const char* topic, const char* message, bool retenu = false) {
  if (mqtt.connected()) mqtt.publish(topic, message, retenu);
}

// ---------- Actions sur le moteur ----------
void couperMoteur(const char* cause) {
  moteurMarche = false;
  digitalWrite(PIN_RELAIS, RELAIS_OFF);
  digitalWrite(PIN_LED_ROUGE, HIGH);
  tone(PIN_BUZZER, 1000, 500);
  Serial.print("MOTEUR COUPE - cause : ");
  Serial.println(cause);

  char msg[96];
  snprintf(msg, sizeof(msg), "MOTEUR COUPE : %s", cause);
  publier(T_STAT, "ARRET", true);
  publier(T_DEF, cause, true);
  publier(T_ALERT, msg);
}

void redemarrerMoteur() {
  moteurMarche = true;
  digitalWrite(PIN_RELAIS, RELAIS_ON);
  digitalWrite(PIN_LED_ROUGE, LOW);
  Serial.println("MOTEUR REDEMARRE");
  publier(T_STAT, "MARCHE", true);
  publier(T_DEF, "aucun", true);
}

// ---------- Reception d'une commande ----------
void rappel(char* topic, byte* payload, unsigned int longueur) {
  String msg = "";
  for (unsigned int i = 0; i < longueur; i++) msg += (char)payload[i];
  msg.trim();
  Serial.print("Commande recue : ");
  Serial.println(msg);

  if (msg == "OFF") {
    arretDistant = true;
    if (moteurMarche) couperMoteur("ordre distant");
  } else if (msg == "ON") {
    arretDistant = false;
  } else {
    Serial.println("Commande ignoree (valeur inconnue)");
  }
}

// ---------- Gestion du reseau (sans jamais bloquer) ----------
void gererReseau() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;
  if (millis() - dernierEssaiMqtt < PERIODE_RECONNEXION) return;
  dernierEssaiMqtt = millis();

  // Pour verifier le certificat, l'ESP32 doit connaitre la date reelle
  if (time(nullptr) < 1700000000) {
    if (!ntpLance) {
      configTime(0, 0, "pool.ntp.org");
      ntpLance = true;
      Serial.println("Synchronisation de l'heure demandee");
    }
    return;
  }

  Serial.print("Connexion MQTT... ");
  if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS, T_ETAT, 1, true, "offline")) {
    Serial.println("OK");
    mqtt.subscribe(T_CMD, 1);
    publier(T_ETAT, "online", true);
    publier(T_STAT, moteurMarche ? "MARCHE" : "ARRET", true);
  } else {
    Serial.print("echec, code = ");
    Serial.println(mqtt.state()); //Cette fonction ne contient aucune boucle d’attente : l’ESP32 reste réactif (sécurité locale active) même quand le réseau est tombé.
  }
}

// ---------- Commandes de test au clavier (moniteur serie) ----------
void commandesSerie() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'c') {
    Serial.println("TEST : coupure du reseau");
    WiFi.disconnect();
  } else if (c == 'r') {
    Serial.println("TEST : retour du reseau");
    WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAIS, OUTPUT);
  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  dht.begin();
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("MPU6050 introuvable");
    while (true) delay(10);
  }

  digitalWrite(PIN_RELAIS, RELAIS_ON);
  digitalWrite(PIN_LED_ROUGE, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  Serial.print("Wi-Fi");
  unsigned long debut = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - debut < 10000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" OK, IP = ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" pas de reseau : mode local seul");
  }

  clientTls.setCACert(CA_CERT);        // verifie l'identite du serveur (certificat DigiCert)
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);             // agrandit la memoire des messages (256 octets par defaut)
  mqtt.setCallback(rappel);
  mqtt.setSocketTimeout(5);
  Serial.println("Systeme pret");
}

void loop() {
  commandesSerie();
  gererReseau();
  mqtt.loop();

  if (millis() - derniereMesure < PERIODE_MESURE) return;
  derniereMesure = millis();

  // 1) Mesures
  float t = dht.readTemperature();
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float norme = sqrt(a.acceleration.x * a.acceleration.x +
                     a.acceleration.y * a.acceleration.y +
                     a.acceleration.z * a.acceleration.z);
  float vib = fabs(norme - 9.81);

  // 2) Validation
  bool mesureValide = !(isnan(t) || t < -40 || t > 80);

  // 3) Decision locale (priorite absolue)
  if (!mesureValide) {
    if (moteurMarche) couperMoteur("mesure invalide");
  } else if (moteurMarche) {
    if (t >= TEMP_COUPURE) couperMoteur("temperature trop elevee");
    else if (vib >= VIB_COUPURE) couperMoteur("vibration trop forte");
  } else {
    if (!arretDistant && t < TEMP_REDEMARRE && vib < VIB_COUPURE) redemarrerMoteur();
  }

  // 4) Publication : un seul message JSON avec toutes les mesures
  char tbuf[16];
  if (mesureValide) snprintf(tbuf, sizeof(tbuf), "%.1f", t);
  else strcpy(tbuf, "null");                 // JSON valide meme si la mesure est invalide
  char json[200];
  snprintf(json, sizeof(json),
           "{\"temperature\":%s,\"vibration\":%.2f,\"statut\":\"%s\"}",
           tbuf, vib, moteurMarche ? "MARCHE" : "ARRET");
  publier(T_MES, json);

  // 5) Affichage local
  Serial.println(json);
  Serial.print("MQTT : ");
  Serial.println(mqtt.connected() ? "connecte" : "hors ligne");
}
