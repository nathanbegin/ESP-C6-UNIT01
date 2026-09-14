# ESP-C6-UNIT01

Firmware ESP-IDF pour un ESP32-C6 : **3 contacts de porte Zigbee + LED embarquée On/Off + portail Wi-Fi de maintenance**.

Fabricant déclaré : `NathanSensors`. Modèle attendu : `ESP-C6-UNIT01`.

Ce dépôt a été préparé le 14 septembre 2026 à partir de `ESP-C6-UNIT01_2026-09-14.zip`. Les sources du firmware, le convertisseur et les configurations sont conservés sans modification. La documentation a été actualisée à partir du code. Les problèmes repérés sont décrits dans [docs/ANALYSE.md](docs/ANALYSE.md). L'ancien README est conservé comme document historique dans [docs/README-original.md](docs/README-original.md).

## Fonctions et câblage

| Fonction | GPIO | Endpoint Zigbee | Type | Propriété du convertisseur |
| --- | --- | --- | --- | --- |
| LED RGB embarquée | 8 | 1 | On/Off, cluster `0x0006` | `state_led` |
| Contact porte 1 | 2 | 2 | IAS Zone, cluster `0x0500` | `contact_door1` |
| Contact porte 2 | 3 | 3 | IAS Zone, cluster `0x0500` | `contact_door2` |
| Contact porte 3 | 4 | 4 | IAS Zone, cluster `0x0500` | `contact_door3` |
| Bouton BOOT | 9 | — | Entrée locale, appui long | — |

Chaque contact magnétique se branche entre son GPIO et GND. Le firmware active une résistance de rappel interne vers le haut.

- Contact fermé vers GND : niveau bas, porte considérée fermée.
- Contact ouvert : niveau haut, porte considérée ouverte.
- Entrée non raccordée : également considérée ouverte.
- Cette correspondance suppose un contact fermé lorsque la porte est fermée. Un capteur de logique inverse nécessite une adaptation.

Utiliser des contacts secs sur ces entrées, sans y injecter une tension externe. Les broches correspondent à la carte décrite dans les sources; vérifier le brochage de la carte réellement utilisée.

La LED est RGB physiquement, mais le firmware ne propose que deux états : éteinte ou blanc doux avec RGB `(24, 24, 24)`. Il ne contient aucune commande de relais.

## Fonctionnement normal

1. Initialisation de la mémoire persistante NVS et lecture du mode de démarrage.
2. En mode normal, initialisation de la LED, des entrées de porte et de la surveillance du bouton BOOT.
3. Démarrage de Zigbee en **routeur** : `DOOR_DEVICE_AS_ROUTER=true`, capacité configurée de dix enfants.
4. Si l'appareil est neuf pour le réseau, recherche d'un réseau ouvert à l'appairage. En cas d'échec du steering, un nouvel essai est programmé après une seconde.
5. Chaque changement électrique déclenche une interruption. Une tâche FreeRTOS applique un anti-rebond de 50 ms, relit le GPIO et transmet les changements d'état.
6. Les commandes Zigbee On/Off de l'endpoint 1 pilotent la LED.

Une ouverture produit `ZoneStatus=1`; une fermeture produit `ZoneStatus=0`. La notification est envoyée directement au coordinateur `0x0000`, endpoint `1`. Le convertisseur traduit cela en `contact_doorN=false` pour ouvert et `true` pour fermé.

L'appareil utilise Zigbee pour communiquer avec le coordinateur. Dans l'intégration prévue, Zigbee2MQTT convertit les messages puis les publie vers MQTT pour Home Assistant. Le firmware n'est pas lui-même un client MQTT ou un client du Wi-Fi domestique.

Attention : le code actuel ne retransmet pas automatiquement les trois états après la connexion au réseau. Voir l'analyse avant de considérer les états au redémarrage comme fiables.

## Portail Wi-Fi

Pendant le fonctionnement normal, maintenir BOOT pendant environ trois secondes, puis relâcher le bouton. Le firmware enregistre le mode CONFIG dans NVS et redémarre.

1. Se connecter au réseau ouvert `NathanSensors-Setup`.
2. Ouvrir `http://192.168.4.1` dans un navigateur.
3. Cliquer sur « Terminer et revenir au Zigbee » pour enregistrer le mode normal et redémarrer.

Le point d'accès utilise le canal 1 et accepte au maximum quatre clients. Zigbee et la surveillance des portes ne sont pas démarrés dans ce mode. Un simple redémarrage conserve le mode CONFIG : il faut utiliser le bouton de la page pour revenir au fonctionnement normal.

Le portail actuel contient uniquement ce bouton de retour. Il ne permet pas de changer les GPIO, les noms, les paramètres Wi-Fi ou d'effectuer un factory reset. Il ne fournit pas de mise à jour OTA. La redirection des erreurs HTTP 404 vers `/` ne constitue pas un service DNS captif; ouvrir directement l'adresse IP reste la procédure prévue.

## Compilation et programmation

Versions relevées dans `dependencies.lock` :

| Dépendance | Version |
| --- | --- |
| ESP-IDF | 5.5.4 |
| espressif/esp-zigbee-lib | 1.6.8 |
| espressif/esp-zboss-lib | 1.6.4 |
| espressif/led_strip | 3.0.3 |

Le manifeste autorise ESP-IDF `>=5.5.0`; la version 5.5.4 est celle enregistrée dans l'archive, sans constituer une validation de toutes les versions plus récentes.

Depuis un terminal ESP-IDF configuré, à la racine du projet :

```bash
idf.py build
idf.py -p COM5 flash monitor
```

Remplacer `COM5` par le port de la carte. Sous Linux, utiliser le port réel, par exemple `/dev/ttyACM0` pour une connexion USB série native.

Le `sdkconfig` fourni sélectionne déjà `esp32c6`, une flash de **8 Mo**, la pile Zigbee ZCZR et la table de partitions personnalisée. Il est versionné volontairement. La partition applicative fait 5 Mio : cette table ne convient pas à une flash de 4 Mo.

Si la configuration est régénérée ou la cible réinitialisée, vérifier la taille de flash dans `idf.py menuconfig` avant de compiler : `sdkconfig.defaults` ne contient pas encore le réglage de flash 8 Mo. Aucun outil ESP-IDF n'était disponible dans l'environnement d'analyse; la compilation et le flash n'ont pas été exécutés.

## Zigbee2MQTT et Home Assistant

Le convertisseur fourni, `c6_door_sensor.js`, expose trois contacts et un interrupteur pour la LED. Il utilise CommonJS (`require`, `module.exports`). Sa compatibilité avec la version de Zigbee2MQTT installée doit être vérifiée.

La [documentation actuelle des convertisseurs externes](https://www.zigbee2mqtt.io/advanced/more/external_converters.html) indique un dossier `external_converters` à côté de `configuration.yaml`, ou une gestion depuis Settings > Dev console > External converters. Les nouvelles installations à partir de 2.11.0 désactivent ces scripts par défaut; vérifier `enable_external_js`. Les exemples actuels utilisent des modules `.mjs`. Ne pas appliquer aveuglément les anciennes instructions YAML du README historique.

Pour l'appairage, activer l'autorisation d'appairage du coordinateur et démarrer la carte. Le modèle du firmware et celui du convertisseur correspondent : `ESP-C6-UNIT01`. La découverte MQTT de Home Assistant doit être configurée côté Zigbee2MQTT/Home Assistant pour y retrouver les entités.

## Structure

| Fichier | Responsabilité |
| --- | --- |
| `main/zb_door_sensors.c` | Démarrage, GPIO, anti-rebond, endpoints, notifications et LED |
| `main/zb_door_sensors.h` | Broches, rôle Zigbee et identifiants |
| `main/app_config.c` | Bouton BOOT, NVS, point d'accès et serveur HTTP |
| `main/app_config.h` | Paramètres du portail et du bouton |
| `c6_door_sensor.js` | Traduction Zigbee2MQTT des contacts et de la LED |
| `sdkconfig`, `sdkconfig.defaults` | Configuration ESP-IDF complète et valeurs de base |
| `partitions.csv` | Organisation de la mémoire flash |
| `dependencies.lock` | Versions des composants de l'archive |
| `docs/ANALYSE.md` | Résultats de la revue et limites |

Les dossiers `build/` et `managed_components/` sont exclus : ils contiennent les résultats de compilation et les dépendances récupérables. `sdkconfig.old` n'est pas importé. Aucune licence de redistribution nouvelle n'a été ajoutée.

## Dépôt GitHub

[nathanbegin/ESP-C6-UNIT01](https://github.com/nathanbegin/ESP-C6-UNIT01)
