# Contrat des topics MQTT

Projet IIoT : surveillance d'un moteur électrique (ESP32 simulé sur Wokwi).
Broker : EMQX Cloud Serverless, connexion TLS sur le port 8883.
Les identifiants (utilisateur et mot de passe) ne sont JAMAIS écrits dans ce dépôt.

## Tableau des topics

| Topic | Contenu du message | Publié par | Reçu par | QoS | Retained |
|---|---|---|---|---|---|
| `usine/ligne1/moteur/mesures` | JSON (voir ci-dessous), toutes les 2 s | ESP32 | Node-RED | 0 | non |
| `usine/ligne1/moteur/statut` | texte : `MARCHE` ou `ARRET` | ESP32 | Node-RED | 0 | oui |
| `usine/ligne1/moteur/defaut` | texte : cause du défaut, ou `aucun` | ESP32 | Node-RED | 0 | oui |
| `usine/ligne1/alertes` | texte : `MOTEUR COUPE : <cause>` | ESP32 | Node-RED | 0 | non |
| `usine/ligne1/moteur/cmd/relais` | texte : `ON` ou `OFF` | Node-RED | ESP32 | 1 | non |
| `usine/ligne1/esp32/etat` | texte : `online` ou `offline` | ESP32 (et Last Will du broker) | Node-RED | 1 (Last Will) | oui |

## Format du message `mesures`

```json
{"temperature": 25.0, "vibration": 0.12, "statut": "MARCHE"}
```

- `temperature` : en °C, une décimale. Vaut `null` si le capteur DHT22 donne une mesure invalide.
- `vibration` : en m/s², deux décimales (écart de l'accélération mesurée par rapport à 9,81).
- `statut` : `MARCHE` ou `ARRET`.

## Règles de fonctionnement

- Seuils (décidés localement par l'ESP32, jamais par le réseau) : coupure à 75 °C ou vibration >= 3,0 ; redémarrage automatique sous 60 °C.
- `OFF` : coupe le moteur et le verrouille. `ON` : enlève seulement le verrou, le moteur ne redémarre que si la température est sous 60 °C et la vibration sous 3,0.
- Les commandes ne sont pas retained : un OFF ancien ne doit jamais être rejoué au redémarrage.
- Toute autre valeur que `ON` ou `OFF` est ignorée.
- Client ID de l'ESP32 : `esp32-moteur-1`. Il doit être unique : un client Node-RED ou EMQX Web doit utiliser un autre identifiant.
- Abonnement conseillé pour Node-RED : `usine/ligne1/#`.

## Utilisateurs MQTT (créés dans EMQX)

| Utilisateur | Utilisé par |
|---|---|
| `esp32` | firmware de l'ESP32 |
| `nodered` | tableau de bord Node-RED |
