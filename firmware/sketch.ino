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

// ---------- Seuils de protection (priorite absolue) ----------
const float TEMP_COUPURE = 75.0;
const float TEMP_REDEMARRE = 60.0;
const float VIB_COUPURE = 3.0;          // coupure si vibration >= 3,0 m/s2
const float VIB_REDEMARRE = 2.5;        // redemarrage seulement sous 2,5 m/s2 (hysteresis)
const float VIB_ATTENTION = 2.0;        // avertissement entre 2,0 et 3,0 m/s2 (zone orange du dashboard)

// ---------- Logique du relais ----------
const int RELAIS_ON = HIGH;
const int RELAIS_OFF = LOW;

// ---------- Topics MQTT ----------
const char* T_MES   = "usine/ligne1/moteur/mesures";      // JSON : ts + temperature + vibration + statut
const char* T_PRED  = "usine/ligne1/moteur/prediction";   // JSON : ts + moyenne + pente + restant + etat + raison
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
char derniereCause[40] = "aucun";      // cause du dernier arret (pour la republier apres une coupure reseau)
unsigned long derniereMesure = 0;
unsigned long dernierEssaiMqtt = 0;
const unsigned long PERIODE_MESURE = 2000;
const unsigned long PERIODE_RECONNEXION = 5000;

// =====================================================================
//  MAINTENANCE PREDICTIVE (n'agit JAMAIS sur le relais : elle avertit)
// =====================================================================
const int   N_FEN = 15;                 // fenetre d'analyse : 15 mesures = 30 s
const int   N_MOY = 5;                  // la moyenne mobile porte sur 5 mesures
const int   N_MIN_PENTE = 8;            // il faut 8 mesures pour calculer une pente
const float T_ATTENTION = 70.0;         // avertissement si la moyenne depasse 70 C
const float T_ATTENTION_FIN = 68.0;     // retour a la normale sous 68 C (hysteresis)
const float T_PENTE_MINI = 45.0;        // la pente n'alerte qu'au-dessus de 45 C
const float PENTE_ATTENTION = 5.0;      // montee rapide : 5 C par minute ou plus
const float PENTE_MIN_RESTANT = 0.3;    // sous 0,3 C/min on ne calcule pas de temps restant
const float RESTANT_ATTENTION = 120.0;  // avertissement si coupure estimee dans moins de 120 s

float fenT[N_FEN];                      // fenetre circulaire des temperatures filtrees
int   fenIdx = 0, fenNb = 0;
float brut[3];                          // les 3 dernieres temperatures brutes
int   brutNb = 0;
int   vibConsec = 0;                    // mesures de suite dans la zone orange (2,0 a 3,0)
bool  attentionActive = false;

struct Prediction {
  bool moyenneOk, penteOk, restantOk;
  float moyenne, pente, restant;
  const char* etat;
  char raison[32];
};

float mediane3(float a, float b, float c) {
  if ((a >= b && a <= c) || (a <= b && a >= c)) return a;
  if ((b >= a && b <= c) || (b <= a && b >= c)) return b;
  return c;
}

float valeurFenetre(int i) {            // i = 0 : la plus ancienne ... fenNb-1 : la plus recente
  return fenT[(fenIdx - fenNb + i + N_FEN) % N_FEN];
}

void ajouterTemperature(float t) {
  // 1) filtre median sur 3 points : un pic isole est elimine
  if (brutNb < 3) brutNb++;
  brut[2] = brut[1]; brut[1] = brut[0]; brut[0] = t;
  float filtree = (brutNb >= 3) ? mediane3(brut[0], brut[1], brut[2]) : t;
  // 2) rangement dans la fenetre circulaire
  fenT[fenIdx] = filtree;
  fenIdx = (fenIdx + 1) % N_FEN;
  if (fenNb < N_FEN) fenNb++;
}

float moyenneMobile() {
  int n = (fenNb < N_MOY) ? fenNb : N_MOY;
  float s = 0;
  for (int i = fenNb - n; i < fenNb; i++) s += valeurFenetre(i);
  return s / n;
}

// pente par regression lineaire (moindres carres), en degres C par minute
float penteRegression() {
  int n = fenNb;
  double sx = 0, sy = 0, sxy = 0, sxx = 0;
  for (int i = 0; i < n; i++) {
    double y = valeurFenetre(i);
    sx += i; sy += y; sxy += i * y; sxx += (double)i * i;
  }
  double den = n * sxx - sx * sx;
  if (den == 0) return 0;
  double parMesure = (n * sxy - sx * sy) / den;      // degres C par mesure
  return (float)(parMesure * 60000.0 / PERIODE_MESURE);
}

// vibration : avertissement si elle reste entre 2,0 et 3,0 m/s2 (au-dela de 3,0, le moteur est coupe)
bool analyseVibration(float vib) {
  bool zoneOrange = (vib >= VIB_ATTENTION) && (vib < VIB_COUPURE);
  vibConsec = zoneOrange ? vibConsec + 1 : 0;
  return vibConsec >= 2;                              // 2 mesures de suite : pas de fausse alerte
}

Prediction predire(bool tempOk, float t, bool vibOk, float vib, bool marche) {
  Prediction p;
  p.moyenneOk = p.penteOk = p.restantOk = false;
  p.moyenne = p.pente = p.restant = 0;
  p.raison[0] = '\0';

  if (tempOk) ajouterTemperature(t);
  bool vibAnormale = vibOk ? analyseVibration(vib) : false;

  if (fenNb >= N_MOY) { p.moyenne = moyenneMobile(); p.moyenneOk = true; }
  if (fenNb >= N_MIN_PENTE) { p.pente = penteRegression(); p.penteOk = true; }
  if (p.moyenneOk && p.penteOk && p.pente >= PENTE_MIN_RESTANT) {
    float r = (TEMP_COUPURE - p.moyenne) / p.pente * 60.0f;   // secondes avant 75 C
    if (r < 0) r = 0;
    if (r <= 3600) { p.restant = r; p.restantOk = true; }
  }

  if (!marche) {
    p.etat = "ARRET";
    attentionActive = false;
  } else if (!p.moyenneOk) {
    p.etat = "INIT";
  } else {
    float seuil = attentionActive ? T_ATTENTION_FIN : T_ATTENTION;
    bool chaud   = p.moyenne >= seuil;
    bool rapide  = p.penteOk && p.pente >= PENTE_ATTENTION && p.moyenne >= T_PENTE_MINI;
    bool bientot = p.restantOk && p.restant <= RESTANT_ATTENTION;
    if (chaud || rapide || bientot || vibAnormale) {
      p.etat = "ATTENTION";
      if (chaud) strcpy(p.raison, "temperature elevee");
      else if (rapide) strcpy(p.raison, "montee rapide");
      else if (bientot) strcpy(p.raison, "coupure proche");
      else strcpy(p.raison, "vibration anormale");
    } else {
      p.etat = "NORMAL";
    }
    attentionActive = (strcmp(p.etat, "ATTENTION") == 0);
  }
  return p;
}

const char* etatPrecedent = "INIT";

// Ecrit un nombre ou "null" dans un tampon (JSON valide meme sans valeur)
void nombre(char* buf, size_t taille, bool ok, float v, int decimales) {
  if (ok) snprintf(buf, taille, "%.*f", decimales, v);
  else strcpy(buf, "null");
}

// Heure reelle en secondes (0 si l'heure n'est pas encore connue)
long horodatage() {
  time_t n = time(nullptr);
  return (n > 1700000000) ? (long)n : 0;
}

// ---------- Publication (seulement si connecte) ----------
void publier(const char* topic, const char* message, bool retenu = false) {
  if (mqtt.connected()) mqtt.publish(topic, message, retenu);
}

// ---------- Actions sur le moteur ----------
void couperMoteur(const char* cause) {
  moteurMarche = false;
  strncpy(derniereCause, cause, sizeof(derniereCause) - 1);
  derniereCause[sizeof(derniereCause) - 1] = '\0';
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
  strcpy(derniereCause, "aucun");
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
    publier(T_DEF, moteurMarche ? "aucun" : derniereCause, true);   // republie aussi la cause
  } else {
    Serial.print("echec, code = ");
    Serial.println(mqtt.state());
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

  // LED rouge : clignote pendant un avertissement (moteur encore en marche)
  if (moteurMarche) {
    bool clignote = attentionActive && ((millis() / 300) % 2);
    digitalWrite(PIN_LED_ROUGE, clignote ? HIGH : LOW);
  }

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
  bool tempOk = !(isnan(t) || t < -40 || t > 80);
  bool vibOk  = !(isnan(vib) || vib < 0);          // pas un nombre, ou negatif (impossible)
  bool mesureValide = tempOk && vibOk;               // une seule mesure fausse suffit a invalider

  // 3) Decision locale (priorite absolue, inchangee)
  if (!mesureValide) {
    if (moteurMarche) couperMoteur("mesure invalide");
  } else if (moteurMarche) {
    if (t >= TEMP_COUPURE) couperMoteur("temperature trop elevee");
    else if (vib >= VIB_COUPURE) couperMoteur("vibration trop forte");
  } else {
    if (!arretDistant && t < TEMP_REDEMARRE && vib < VIB_REDEMARRE) redemarrerMoteur();
  }

  // 4) Maintenance predictive (apres la decision : elle avertit, elle ne decide pas)
  Prediction p = predire(tempOk, t, vibOk, vib, moteurMarche);
  if (strcmp(p.etat, etatPrecedent) != 0) {            // message seulement quand l'etat change
    if (strcmp(p.etat, "ATTENTION") == 0) {
      char msg[64];
      snprintf(msg, sizeof(msg), "ATTENTION : %s", p.raison);
      publier(T_ALERT, msg);
      Serial.println(msg);
    } else if (strcmp(etatPrecedent, "ATTENTION") == 0 && strcmp(p.etat, "NORMAL") == 0) {
      publier(T_ALERT, "RETOUR A LA NORMALE");
      Serial.println("RETOUR A LA NORMALE");
    }
    etatPrecedent = p.etat;
  }

  // 5) Publication : JSON 1 = mesures brutes, JSON 2 = prediction
  long ts = horodatage();
  char tbuf[16];
  char vbuf[16];
  nombre(tbuf, sizeof(tbuf), tempOk, t, 1);
  nombre(vbuf, sizeof(vbuf), vibOk, vib, 2);
  char json[200];
  snprintf(json, sizeof(json),
           "{\"ts\":%ld,\"temperature\":%s,\"vibration\":%s,\"statut\":\"%s\"}",
           ts, tbuf, vbuf, moteurMarche ? "MARCHE" : "ARRET");
  publier(T_MES, json);

  char mbuf[16], pbuf[16], rbuf[16];
  nombre(mbuf, sizeof(mbuf), p.moyenneOk, p.moyenne, 1);
  nombre(pbuf, sizeof(pbuf), p.penteOk, p.pente, 2);
  nombre(rbuf, sizeof(rbuf), p.restantOk, p.restant, 0);
  char jsonPred[220];
  snprintf(jsonPred, sizeof(jsonPred),
           "{\"ts\":%ld,\"moyenne\":%s,\"pente\":%s,\"restant\":%s,\"etat\":\"%s\",\"raison\":\"%s\"}",
           ts, mbuf, pbuf, rbuf, p.etat, p.raison);
  publier(T_PRED, jsonPred);

  // 6) Affichage local
  Serial.println(json);
  Serial.println(jsonPred);
  Serial.print("MQTT : ");
  Serial.println(mqtt.connected() ? "connecte" : "hors ligne");
}
