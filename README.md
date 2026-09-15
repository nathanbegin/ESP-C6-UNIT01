# ESP-C6-UNIT01

Firmware ESP-IDF pour ESP32-C6 : trois contacts de porte, une LED WS2812 et une commande de ventilation par **trois relais SPST**. Fabricant Zigbee : `NathanSensors`; modèle : `ESP-C6-UNIT01`.

## Commandes de ventilation

Le circuit de commande entre **J13 et J14** est reproduit directement plutôt que de répliquer mécaniquement l'ancien sélecteur 2P3T :

- **Fan 1 / Low** : branche 21 kΩ;
- **Fan 2 / High** : branche 4 kΩ;
- **Échange extérieur** : liaison directe J13-J14, autorisée uniquement en Fan High.

| Commande Zigbee | ON | OFF |
| --- | --- | --- |
| **Fan 1** | sélectionne Low; coupe Fan 2 et l'échange | passe sur Off si Fan 1 est actif |
| **Fan 2** | sélectionne High; coupe Fan 1 | passe sur Off si Fan 2 est actif; coupe aussi l'échange |
| **Échange extérieur** | force d'abord Fan High puis active l'échange | désactive l'échange mais conserve Fan High |

Les états physiques permis sont donc seulement : **Off**, **Low**, **High**, **High + Exchange**. Le firmware interdit Low+High simultané et interdit l'échange sans High.

Les relais n'ont aucun endpoint individuel; les endpoints exposent les fonctions logiques. La LED et les trois contacts existants sont conservés.

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

## Relais, GPIO et câblage

| Relais | GPIO ESP32-C6 | Module relais | Fonction |
| --- | ---: | --- | --- |
| **K1** | **6** | IN1 | Fan Low / branche 21 kΩ |
| **K2** | **7** | IN2 | Fan High / branche 4 kΩ |
| **K3** | **10** | IN3 | Échange extérieur / liaison directe J13-J14 |

**GPIO11 est maintenant libre.** Si un module à quatre relais est utilisé, IN4/K4 n'est pas utilisé par la ventilation.

Utiliser des relais **SPST normalement ouverts à contacts secs**. Un relais SPDT peut aussi convenir en n'utilisant que COM et NO, mais le contact NC n'est pas requis. Voir [docs/RELAIS.md](docs/RELAIS.md) pour le schéma logique, la table d'états et la procédure de validation.

Les entrées du module doivent être compatibles 3,3 V; une bobine seule ne se branche pas directement à un GPIO. J13/J14 et les résistances du circuit commandé restent isolés des GPIO et de la masse ESP32.

Dans `idf.py menuconfig` > **ESP-C6-UNIT01 relay control** :

- `APP_RELAY_ACTIVE_LOW` : **activé par défaut**. LOW active une bobine; désactiver pour un module actif HIGH.
- `APP_RELAY_SETTLE_MS` : **30 ms par étape**, à adapter aux temps de commutation et de rebond du module.

Les trois bobines sont relâchées avant de démarrer Zigbee ou Wi-Fi : Off, échange désactivé. Les modes ne sont pas restaurés après une coupure. Le matériel doit imposer un état sûr pendant le reset, avant l'exécution du firmware.

Les changements de vitesse sont **break-before-make** : le firmware ouvre d'abord l'échange, ouvre la vitesse actuelle, ferme la nouvelle vitesse, puis ne referme l'échange qu'une fois Fan High réellement sélectionné. Les délais se déroulent dans une tâche dédiée. Les rapports décrivent l'état commandé des sorties; il n'y a pas de retour mécanique des contacts relais.

## Table d'états des relais

| État | K1 Low | K2 High | K3 Exchange |
| --- | ---: | ---: | ---: |
| Off | 0 | 0 | 0 |
| Fan Low | 1 | 0 | 0 |
| Fan High | 0 | 1 | 0 |
| Fan High + échange | 0 | 1 | 1 |

`Exchange ON` depuis Off ou Low force automatiquement **Fan High + échange**. `Fan Low ON` pendant un échange coupe d'abord K3 avant de quitter High.

## Mise à jour d'un appareil appairé

1. Vérifier le câblage K1/K2/K3, la polarité des entrées et les délais du module.
2. Compiler et flasher le firmware.
3. Remplacer le convertisseur externe dans Zigbee2MQTT par **un seul** fichier : `c6_door_sensor.mjs` (ESM, installations actuelles) ou `c6_door_sensor.js` (CommonJS, installations compatibles). Retirer l'ancien convertisseur actif pour ce modèle.
4. Refaire l'interview si nécessaire pour les endpoints 5, 6 et 7; vérifier ensuite les automatisations existantes.
5. Tester les contacts relais avec J13/J14 déconnectés de la machine, puis valider au multimètre les états décrits dans `docs/RELAIS.md` avant raccordement réel.

La [documentation Zigbee2MQTT](https://www.zigbee2mqtt.io/advanced/more/external_converters.html) indique le dossier `external_converters` à côté de `configuration.yaml`, une gestion depuis la console de développement, et `enable_external_js` pour les nouvelles installations à partir de 2.11.0. Ne pas charger les deux variantes simultanément.

Le convertisseur lit les états pendant sa configuration. Le firmware publie après les commandes, après la connexion au réseau et toutes les 30 secondes. Un message IAS sans statut valide ne modifie plus le contact.

Exemples vers `zigbee2mqtt/NOM_APPAREIL/set` :

```json
{"state_fan1":"ON"}
```

```json
{"state_fan2":"ON"}
```

L'échange peut être demandé directement; le firmware force alors High avant K3 :

```json
{"state_exchange":"ON"}
```

Pour arrêter la vitesse active, envoyer OFF sur l'endpoint correspondant. Si Fan High est actif avec échange, `state_fan2: OFF` coupe aussi l'échange.

## LED WS2812

La LED embarquée est une WS2812 sur **GPIO8**. La transmission RMT est exécutée dans une tâche FreeRTOS dédiée afin de ne pas bloquer le callback Zigbee. Le firmware vérifie les retours `led_strip`, réessaie jusqu'à trois fois lors d'une erreur de transmission et ne republie l'état Zigbee qu'après application physique réussie. La dernière valeur appliquée est resynchronisée après une reconnexion Zigbee.

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

`fan_set_mode(FAN_OFF / FAN_LOW / FAN_HIGH)` et `outdoor_exchange_set(bool)` déposent une requête dans la file de la tâche des relais. `ESP_OK` signifie que la requête est acceptée, pas déjà exécutée. `fan_control_set_switch()` traduit les interrupteurs en états sûrs et applique l'interverrouillage High/Exchange.

La logique indépendante du matériel est dans `main/fan_controller.c`; son adaptation GPIO/FreeRTOS est dans `main/fan_control.c`.

```bash
bash tests/run.sh
```

Les tests hôtes couvrent les **16 transitions entre les quatre états sûrs**, l'ordre break-before-make, l'exclusivité Low/High, l'interverrouillage High/Exchange, les commandes OFF périmées, les erreurs GPIO simulées et le convertisseur. Les imports Zigbee2MQTT sont simulés; les variantes CJS/ESM sont comparées.

Le moniteur série à 115200 bauds trace chaque commande reçue, son acceptation dans la file, l'état avant/après et chaque bobine commandée avec son GPIO et son niveau électrique. Exemple Low :

```text
I (...) ZB_DOOR: Commande On/Off reçue: endpoint=5, valeur=ON
I (...) FAN: Commande Zigbee acceptée: FAN 1 / BASSE -> ON
I (...) FAN: K1 GPIO6 -> bobine ACTIVEE (niveau=LOW)
I (...) FAN: Etat applique: mode=FAN 1 / BASSE, échange=OFF, K1(low)=1 K2(high)=0 K3(exchange)=0
```

Pour `Exchange ON` depuis Off, K2 est activé avant K3. Ces traces confirment les niveaux demandés aux GPIO; elles ne constituent pas un retour mécanique des contacts du relais.

Le workflow [Firmware validation](https://github.com/nathanbegin/ESP-C6-UNIT01/actions) teste la logique et compile dans ESP-IDF 5.5.4. Une compilation réussie fournit les binaires en artefact. Vérifier le résultat du commit concerné. Les essais électriques et Zigbee sur la carte ne sont pas remplacés par ces tests.

`build/` et `managed_components/` sont exclus. L'import initial provient de `ESP-C6-UNIT01_2026-09-14.zip`; [l'analyse d'origine](docs/ANALYSE.md) et [l'ancien README](docs/README-original.md) sont conservés à titre historique.
