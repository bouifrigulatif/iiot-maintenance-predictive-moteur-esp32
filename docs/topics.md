# Contrat des topics MQTT

Projet IIoT : surveillance d'un moteur électrique (ESP32 simulé sur Wokwi).
Broker : EMQX Cloud Serverless, connexion TLS sur le port 8883.
Les identifiants (utilisateur et mot de passe) ne sont JAMAIS écrits dans ce dépôt.

## Tableau des topics

| Topic | Contenu du message | Publié par | Reçu par | QoS | Retained |
|---|---|---|---|---|---|
| `usine/ligne1/moteur/mesures` | JSON de mesures (voir ci-dessous), toutes les 2 s | ESP32 | Node-RED | 0 | non |
| `usine/ligne1/moteur/prediction` | JSON de maintenance prédictive (voir ci-dessous), toutes les 2 s | ESP32 | Node-RED | 0 | non |
| `usine/ligne1/moteur/statut` | texte : `MARCHE` ou `ARRET` | ESP32 | Node-RED | 0 | oui |
| `usine/ligne1/moteur/defaut` | texte : cause du défaut, ou `aucun` | ESP32 | Node-RED | 0 | oui |
| `usine/ligne1/alertes` | texte : `MOTEUR COUPE : <cause>`, `ATTENTION : <raison>` ou `RETOUR A LA NORMALE` | ESP32 | Node-RED | 0 | non |
| `usine/ligne1/moteur/cmd/relais` | texte : `ON` ou `OFF` | Node-RED | ESP32 | 1 | non |
| `usine/ligne1/esp32/etat` | texte : `online` ou `offline` | ESP32 (et Last Will du broker) | Node-RED | 1 (Last Will) | oui |

## JSON 1 : `mesures` (mesures brutes)

```json
{"ts": 1793500000, "temperature": 25.0, "vibration": 0.12, "statut": "MARCHE"}
```

- `ts` : heure réelle en secondes (format Unix). Vaut `0` si l'ESP32 n'a pas encore l'heure.
- `temperature` : en °C, une décimale. Vaut `null` si le capteur DHT22 donne une mesure invalide.
- `vibration` : en m/s², deux décimales (écart de l'accélération mesurée par rapport à 9,81).
- `statut` : `MARCHE` ou `ARRET`.

## JSON 2 : `prediction` (maintenance prédictive)

```json
{"ts": 1793500000, "moyenne": 61.2, "pente": 0.35, "restant": null, "etat": "NORMAL", "raison": ""}
```

- `moyenne` : moyenne mobile de la température sur 5 mesures (après filtre médian), en °C. `null` pendant les premières mesures.
- `pente` : vitesse de variation de la température en °C par minute, calculée par régression linéaire sur 15 mesures (30 s). `null` pendant les premières mesures.
- `restant` : temps estimé avant d'atteindre 75 °C, en secondes. `null` si la température ne monte pas (pente < 0,3 °C/min) ou si l'échéance dépasse 1 heure.
- `etat` : `INIT` (démarrage), `NORMAL`, `ATTENTION` ou `ARRET`.
- `raison` : cause de l'état `ATTENTION` (`temperature elevee`, `montee rapide`, `coupure proche`, `vibration anormale`), sinon vide.

## Règles de fonctionnement

- **Protection (décidée localement par l'ESP32, jamais par le réseau, priorité absolue)** : coupure à 75 °C ou vibration >= 3,0 ; redémarrage automatique sous 60 °C.
- **Maintenance prédictive (avertit, ne coupe jamais)** : `ATTENTION` si la moyenne atteint 70 °C (retour sous 68 °C), ou si la température monte d'au moins 5 °C/min au-dessus de 45 °C, ou si la coupure est estimée dans moins de 120 s, ou si la vibration dépasse un seuil adaptatif (moyenne apprise + 4 écarts-types, confirmé sur 2 mesures).
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

## Pour le tableau de bord (Node-RED)

- Courbes : `temperature` et `moyenne` sur le même graphique, `vibration` sur un second.
- Texte coloré : `etat`. Affichage : `restant` (en secondes, à convertir en minutes).
- Journal des alertes : enregistrer chaque message de `usine/ligne1/alertes` avec l'heure (`ts` ou heure du serveur).
- Historique : enregistrer `mesures` dans un fichier CSV (colonnes `ts,temperature,vibration,statut`) pour le script `data/analyse_anomalies.py`.
