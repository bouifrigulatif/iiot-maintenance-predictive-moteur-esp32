# iiot-moteur
Mini-projet IIoT : surveillance d’un moteur électrique
 
ENSA Béni Mellal, TDI, 2026-2027.
Binôme : Latifa BOUIFRIGU et Sara OUALLA.
 
## Objectif
Chaîne IIoT simulée : ESP32 (Wokwi) -> broker MQTT (EMQX Cloud,
rôle de la DMZ) -> Node-RED -> décision locale et commande distante.
 
## Contenu du dépôt
- firmware/ : programme de l'ESP32 et câblage Wokwi
- broker/ : comptes du broker, liste des topics
- dashboard/ : flux Node-RED
- data/ : journal horodaté d'exemple
 
## Ce qui n'est pas dans le dépôt
Les mots de passe (fichier secrets.h). Le modèle est firmware/secrets.example.h.
 
## Lancer le projet


### Prérequis
- Un compte [Wokwi](https://wokwi.com) pour simuler l'ESP32 (ou l'extension Wokwi de VS Code).
- Un compte EMQX Cloud (offre gratuite suffisante pour un test).
- Node-RED installé, avec la palette du tableau de bord utilisée dans `dashboard/` (à installer depuis « Gérer la palette »).

### 1. Créer le broker
1. Crée un déploiement EMQX Cloud et note l'adresse et le port TLS (8883).
2. Crée deux utilisateurs : `esp32` et `nodered`, chacun avec son propre mot de passe.
3. Applique les règles d'accès décrites dans `broker/` : `esp32` publie ses topics et lit seulement `cmd/relais` ; `nodered` lit `usine/ligne1/#` et publie seulement `cmd/relais` ; tout le reste est refusé.

### 2. Configurer l'ESP32
1. Copie `firmware/secrets.example.h` en `firmware/secrets.h`.
2. Remplace les valeurs fictives par ton adresse de broker et les identifiants de `esp32`.
3. Ne publie jamais `secrets.h` (il est dans `.gitignore`).
4. Ouvre le montage dans Wokwi (voir `firmware/`) et lance la simulation.
   Le moniteur série doit afficher la connexion Wi-Fi, puis la connexion MQTT.

### 3. Lancer le tableau de bord
1. Dans Node-RED, importe le flux `dashboard/flows.json`.
2. Ouvre le nœud de connexion au broker et renseigne l'adresse, le port 8883 (TLS activé) et les identifiants de `nodered`.
3. Déploie le flux et ouvre l'adresse du tableau de bord.

### 4. Vérifier
- Les mesures arrivent toutes les 2 secondes.
- À 75 °C ou 3,0 m/s², le moteur est coupé localement.
- Le tableau de bord affiche l'état (NORMAL, ATTENTION, ARRET) et la raison.
