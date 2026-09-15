# ESP-C6-UNIT01

Firmware ESP-IDF pour ESP32-C6 : trois contacts de porte, une LED et une commande de ventilation par quatre relais. Fabricant Zigbee : `NathanSensors`; modèle : `ESP-C6-UNIT01`.

## Trois commandes de ventilation

| Interrupteur | ON | OFF |
| --- | --- | --- |
| **Fan 1** | Position 2, basse vitesse; désactive Fan 2 | Passe sur Off si Fan 1 est actif |
| **Fan 2** | Position 3, haute vitesse; désactive Fan 1 | Passe sur Off si Fan 2 est actif |
| **Échange extérieur** | Ferme SW2 avec K4 | Ouvre SW2 avec K4 |

**Off = Fan 1 et Fan 2 désactivés**, position 1 du sélecteur. Éteindre une vitesse déjà inactive ne coupe pas l'autre vitesse. La demande d'échange est indépendante et conservée pendant les changements de mode, y compris Off; selon le schéma fourni, elle n'a pas d'effet électrique en position 1.

Les relais K1..K4 n'ont aucun endpoint individuel. La LED et les trois contacts existants sont conservés.

| Endpoint | Fonction | GPIO / propriété Zigbee2MQTT |
| --- | --- | --- |
| 1 | LED blanche On/Off | GPIO8 / `state_led` |
| 2 | Porte 1 | GPIO2 / `contact_door1` |
| 3 | Porte 2 | GPIO3 / `contact_door2` |
| 4 | Porte 3 | GPIO4 / `contact_door3` |
| 5 | Fan 1, basse vitesse | `state_fan1` |
| 6 | Fan 2, haute vitesse | `state_fan2` |
| 7 | Échange extérieur | `state_exchange` |

Les contacts valent `true` pour fermé, `false` pour ouvert. Les interrupteurs utilisent `ON` / `OFF`; les commandes On, Off et Toggle sont acceptées.

## Raccordement et paramètres

<img width="1536" height="1024" alt="5473d712-a4ce-4df6-86d3-064a21f32113" src="https://github.com/user-attachments/assets/98015bfb-0120-482f-81f8-1e02185d40b9" />


| GPIO ESP32-C6 | Module relais |
| --- | --- |
| 6 | IN1 / K1 |
| 7 | IN2 / K2 |
| 10 | IN3 / K3 |
| 11 | IN4 / K4 |

Utiliser quatre relais **SPDT à contacts secs COM/NC/NO**, selon [docs/RELAIS.md](docs/RELAIS.md). Les broches supposent une DevKitC avec ces GPIO accessibles. Les entrées du module doivent être compatibles 3,3 V; une bobine seule ne se branche pas directement à un GPIO. L'alimentation du module et sa masse logique suivent son schéma réel. Les contacts du circuit commandé restent isolés des GPIO et de la masse ESP32.

Dans `idf.py menuconfig` > **ESP-C6-UNIT01 relay control** :

- `APP_RELAY_ACTIVE_LOW` : **activé par défaut**. LOW active une bobine; désactiver pour un module actif HIGH.
- `APP_RELAY_SETTLE_MS` : **30 ms par étape**, à adapter aux temps de commutation et de rebond du module.

Les quatre bobines sont relâchées avant de démarrer Zigbee ou Wi-Fi : Off, échange désactivé. Les modes ne sont pas restaurés après une coupure. Le driver matériel doit imposer le bon état pendant le reset, avant l'exécution du firmware.

Les transitions de vitesse ouvrent K4, passent par la position 1, isolent K3 avant de le commuter, sélectionnent la vitesse puis rétablissent la demande d'échange. Les délais se déroulent dans une tâche dédiée. Les rapports décrivent l'état commandé des sorties : il n'y a pas de retour de position mécanique des relais.

## Mise à jour d'un appareil appairé

1. Vérifier le câblage, la polarité des entrées et les délais du module.
2. Compiler et flasher le firmware.
3. Remplacer le convertisseur externe dans Zigbee2MQTT par **un seul** fichier : `c6_door_sensor.mjs` (ESM, installations actuelles) ou `c6_door_sensor.js` (CommonJS, installations compatibles). Retirer l'ancien convertisseur actif pour ce modèle.
4. Refaire l'interview pour découvrir les endpoints 5, 6 et 7. Si nécessaire, retirer puis réappairer; vérifier ensuite les automatisations existantes.
5. Tester les états des contacts relais avec le circuit commandé déconnecté, puis vérifier le comportement réel.

La [documentation Zigbee2MQTT](https://www.zigbee2mqtt.io/advanced/more/external_converters.html) indique le dossier `external_converters` à côté de `configuration.yaml`, une gestion depuis la console de développement, et `enable_external_js` pour les nouvelles installations à partir de 2.11.0. Ne pas charger les deux variantes simultanément.

Le convertisseur lit les états pendant sa configuration. Le firmware publie après les commandes, après la connexion au réseau et toutes les 30 secondes. Un message IAS sans statut valide ne modifie plus le contact.

Messages vers `zigbee2mqtt/NOM_APPAREIL/set` :

```json
{"state_fan1":"ON"}
```

```json
{"state_fan2":"ON","state_exchange":"ON"}
```

Pour arrêter quelle que soit la vitesse :

```json
{"state_fan1":"OFF","state_fan2":"OFF"}
```

## Portes et portail Wi-Fi

Chaque contact de porte se branche entre son GPIO et GND. Le pull-up interne est actif : contact fermé = porte fermée; contact ouvert ou non raccordé = porte ouverte. Les entrées sont échantillonnées toutes les 10 ms et validées après 50 ms de stabilité. Les rapports initiaux et périodiques resynchronisent les états.

Maintenir BOOT (GPIO9) environ trois secondes en mode normal puis relâcher. L'appareil enregistre CONFIG en NVS et redémarre. Se connecter à `NathanSensors-Setup`, ouvrir `http://192.168.4.1`, puis utiliser le bouton de la page pour revenir au mode normal.

En CONFIG, les relais sont relâchés; Zigbee et la surveillance des portes ne démarrent pas. Ce mode persiste après une coupure. Le portail ne propose pas encore l'affectation des GPIO, le factory reset ou l'OTA.

## Compilation

Versions verrouillées : ESP-IDF **5.5.4**, `esp-zigbee-lib` **1.6.8**, `esp-zboss-lib` **1.6.4**, `led_strip` **3.0.3**. Cible `esp32c6`, flash **8 Mo**, partition applicative 5 Mio. Le rôle Zigbee reste **routeur**.

Dans un terminal ESP-IDF configuré :

```bash
idf.py menuconfig
idf.py build
idf.py -p COM5 flash monitor
```

Adapter le port. Le `sdkconfig` complet reste versionné; les defaults imposent également 8 Mo pour une configuration régénérée.

## Fonctions internes et tests

`fan_set_mode(FAN_OFF / FAN_LOW / FAN_HIGH)` et `outdoor_exchange_set(bool)` déposent une requête dans la file de la tâche des relais. `ESP_OK` signifie que la requête est acceptée, pas déjà exécutée. `fan_control_set_switch()` traduit les interrupteurs en modes exclusifs.

La logique indépendante du matériel est dans `main/fan_controller.c`; son adaptation GPIO/FreeRTOS est dans `main/fan_control.c`.

```bash
bash tests/run.sh
```

Les tests hôtes couvrent les 36 transitions, l'ordre de commutation, les modes exclusifs, les commandes OFF périmées, les erreurs GPIO simulées et le convertisseur. Les imports Zigbee2MQTT sont simulés; les variantes CJS/ESM sont comparées.

Le moniteur série à 115200 bauds trace chaque commande reçue, son acceptation dans la file, l'état avant/après et chaque bobine commandée avec son GPIO et son niveau électrique. Par exemple :

```text
I (...) ZB_DOOR: Commande On/Off reçue: endpoint=5, valeur=ON
I (...) FAN: Commande Zigbee acceptée: FAN 1 / BASSE -> ON
I (...) FAN: K1 GPIO6 -> bobine ACTIVEE (niveau=LOW)
I (...) FAN: K2 GPIO7 -> bobine ACTIVEE (niveau=LOW)
I (...) FAN: Etat applique: mode=FAN 1 / BASSE, échange=OFF, K1=1 K2=1 K3=0 K4=0
```

Ces traces confirment les niveaux demandés aux GPIO. Elles ne constituent pas un retour mécanique des contacts du relais.

Le workflow [Firmware validation](https://github.com/nathanbegin/ESP-C6-UNIT01/actions) teste la logique et compile dans ESP-IDF 5.5.4. Une compilation réussie fournit les binaires en artefact. Vérifier le résultat du commit concerné. Les essais électriques et Zigbee sur la carte ne sont pas remplacés par ces tests.

`build/` et `managed_components/` sont exclus. L'import initial provient de `ESP-C6-UNIT01_2026-09-14.zip`; [l'analyse d'origine](docs/ANALYSE.md) et [l'ancien README](docs/README-original.md) sont conservés à titre historique.
