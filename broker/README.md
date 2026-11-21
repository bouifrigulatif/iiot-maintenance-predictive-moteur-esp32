# Broker MQTT

## Choix du broker

- Service : **EMQX Cloud Serverless** (broker MQTT cloud gratuit), déploiement `iiot-moteur`.
- Région : Europe (AWS, Francfort).
## Paramètres de connexion

| Paramètre | Valeur |
|---|---|
| Adresse | `vc6f0d00.ala.eu-central-1.emqxsl.com` |
| Port MQTT sur TLS | `8883` |
| Port WebSocket sur TLS | `8084` |
| Certificat CA | `emqxsl-ca.crt` (dans ce dossier, DigiCert Global Root G2) |
| Expiration du certificat | 2031 |

Le port 1883 (non chiffré) n'est pas utilisé.

## Utilisateurs (Access Control → Authentication)

| Utilisateur | Rôle | Client ID |
|---|---|---|
| `esp32` | publie les mesures, reçoit les commandes | `esp32-moteur-1` |
| `nodered` | tableau de bord : lit les mesures, envoie les commandes | choisi par Node-RED, différent de celui de l'ESP32 |

Les mots de passe ne sont **jamais** écrits dans le dépôt. Ils sont transmis en privé, et dans le firmware ils sont dans `secrets.h` (exclu du dépôt).

## Sécurité mise en place

- **Authentification obligatoire** : aucun accès anonyme. Un test avec un mauvais mot de passe est refusé (`Bad User Name or Password`). Cela protège contre l'usurpation d'identité (un inconnu qui publierait de fausses mesures ou enverrait un faux ordre OFF).
- **Chiffrement TLS** (port 8883) : les identifiants et les messages ne circulent pas en clair.
- **Vérification du certificat du serveur** : l'ESP32 contrôle l'identité du broker avec le certificat CA, ce qui empêche un faux broker de se faire passer pour le vrai. Un certificat volontairement altéré fait échouer la connexion (code `-2`).
- **Un utilisateur par rôle** : si un mot de passe fuit, on le change sans toucher à l'autre.

## Fiabilité

- **Keep-alive** : 15 s (valeur par défaut de PubSubClient). Le broker considère le client perdu après environ 1,5 fois cette durée.
- **Last Will** : sur `usine/ligne1/esp32/etat`, message `offline`, QoS 1, retained. Le broker le publie lui-même si l'ESP32 disparaît.
- **QoS** : voir `docs/topics.md` pour le détail et la justification.
- **Client ID unique** : deux clients avec le même identifiant se déconnectent mutuellement.


## Procédure de changement de mot de passe

1. Console EMQX → Access Control → Authentication.
2. Supprimer l'utilisateur, le recréer avec un nouveau mot de passe.
3. Mettre à jour `secrets.h` (ESP32) et la configuration Node-RED.
