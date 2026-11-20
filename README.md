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
(à compléter en phase 7)
