# Capteur Zigbee DIY — ESP32-C6-DEV-KIT-N8

LED embarquée pilotable + 3 contacts de porte, remontés dans Home Assistant via Zigbee2MQTT.

## Vue d'ensemble

| Endpoint | Fonction          | GPIO   | Cluster Zigbee        | Dans HA                |
|----------|-------------------|--------|-----------------------|------------------------|
| 1        | LED RGB embarquée | GPIO8  | On/Off (0x0006)       | `light` / `switch`     |
| 2        | Porte 1           | GPIO2  | IAS Zone (0x0500)     | `binary_sensor` contact|
| 3        | Porte 2           | GPIO3  | IAS Zone (0x0500)     | `binary_sensor` contact|
| 4        | Porte 3           | GPIO4  | IAS Zone (0x0500)     | `binary_sensor` contact|

Le C6 est configuré en **Routeur** (alimenté secteur) — il renforce le maillage.
Pour passer en End Device, mettre `DOOR_DEVICE_AS_ROUTER` à `false` dans `zb_door_sensors.h`.

## Câblage des reed switches

Chaque capteur de porte = un interrupteur magnétique (reed) à 2 fils :

```
   reed switch
 GPIOx ──/ ──── GND
```

- un fil au GPIO (2, 3 ou 4), l'autre à GND
- pull-up interne activé dans le firmware (pas de résistance externe nécessaire)
- aimant présent (porte fermée) → contact fermé → niveau BAS → "fermé"
- aimant éloigné (porte ouverte) → contact ouvert → pull-up → niveau HAUT → "ouvert"

Si un capteur s'affiche inversé (NF au lieu de NO), inverse la logique dans
`door_is_open()`.

## Prérequis

- ESP-IDF **v5.5.4** (version recommandée par l'esp-zigbee-sdk)
- Carte ESP32-C6-DEV-KIT-N8, câble USB-C

## Build & flash

```bash
# 1. Environnement ESP-IDF
. $HOME/esp/esp-idf/export.sh

# 2. Cible
idf.py set-target esp32c6

# 3. Récupère les dépendances (esp-zigbee-lib, led_strip) automatiquement
#    puis build
idf.py build

# 4. Flash + monitor (le port USB-JTAG natif apparaît en /dev/ttyACM0 sous Linux)
idf.py -p /dev/ttyACM0 flash monitor
```

Sous Windows, remplace `/dev/ttyACM0` par le port COM correspondant.
Le DEV-KIT-N8 utilise l'USB-JTAG-Serial natif : aucun pilote CP210x à installer.

## Intégration Zigbee2MQTT

1. Copie `c6_door_sensor.js` dans le dossier de config Z2MQTT
   (ex. `/config/zigbee2mqtt/` sous Home Assistant OS).
2. Dans `configuration.yaml` de Z2MQTT :
   ```yaml
   external_converters:
     - c6_door_sensor.js
   ```
3. Redémarre Z2MQTT.
4. Active l'appairage : interface Z2MQTT → "Permit join (All)".
5. Mets l'appareil sous tension. Il apparaît comme **DIYlab C6.DoorSensor.3x**
   avec : 1 interrupteur (LED) + 3 contacts (door1, door2, door3).

> Les versions récentes de Z2MQTT supportent l'appairage sans converter mais
> afficheront des entités génériques. Le converter donne des noms propres et
> le bon type `contact`.

## Pièges connus

- **Interférences 2.4 GHz** : si l'appairage est instable, éloigne la carte des
  ports USB 3.0 et utilise une rallonge USB.
- **IAS Zone enrollment** : à l'appairage, le coordinateur "enrôle" chaque zone.
  Si un contact reste bloqué sur un état, retire puis ré-appaire l'appareil.
- **Modèle non reconnu** : vérifie que `MODEL_IDENTIFIER` du firmware
  (`C6.DoorSensor.3x`) correspond exactement au `zigbeeModel` du converter.

## Vers la Phase 2 (Matter)

Le câblage et la logique de lecture GPIO sont réutilisables tels quels.
Seule la couche réseau (clusters IAS Zone → endpoints Contact Sensor Matter)
et le commissioning (permit join → QR code via le hub Aeotec) changeront.
