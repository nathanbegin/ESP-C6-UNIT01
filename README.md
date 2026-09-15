# ESP-C6-UNIT01

Firmware ESP-IDF pour ESP32-C6 : trois contacts de porte, une LED WS2812 et une commande de ventilation par **trois relais SPST**. Fabricant Zigbee : `NathanSensors`; modèle : `ESP-C6-UNIT01`.

## Commandes de ventilation

Le circuit de commande entre **J13 et J14** est reproduit directement :

- **Fan 1 / Low** : branche 21 kΩ;
- **Fan 2 / High** : branche 4 kΩ;
- **Échange extérieur** : liaison directe J13-J14, proche de 0 Ω.

Pour l'échange extérieur, **K3 seul** est nécessaire. Il n'est pas utile de conserver K2/4 kΩ fermé en même temps qu'un contact direct J13-J14.

| Commande Zigbee | ON | OFF |
| --- | --- | --- |
| **Fan 1** | sélectionne Low/K1; coupe High et Exchange | passe sur Off si Low est actif |
| **Fan 2** | sélectionne High/K2; coupe Low et Exchange | passe sur Off si High est actif |
| **Échange extérieur** | sélectionne Exchange/K3 seul; coupe Low et High | ouvre K3 et passe sur Off si Exchange est actif |

Les quatre états physiques permis sont donc : **Off**, **Low**, **High** et **Exchange**. Low, High et Exchange sont mutuellement exclusifs; un seul relais peut être fermé à la fois.

Les relais n'ont aucun endpoint individuel; les endpoints exposent les fonctions logiques.

| Endpoint | Fonction | GPIO / propriété Zigbee2MQTT |
| --- | --- | --- |
| 1 | LED blanche On/Off | GPIO8 / `state_led` |
| 2 | Porte 1 | GPIO2 / `contact_door1` |
| 3 | Porte 2 | GPIO3 / `contact_door2` |
| 4 | Porte 3 | GPIO4 / `contact_door3` |
| 5 | Fan 1, basse vitesse | `state_fan1` |
| 6 | Fan 2, haute vitesse | `state_fan2` |
| 7 | Échange extérieur | `state_exchange` |

Les contacts de porte valent `true` pour fermé, `false` pour ouvert. Les interrupteurs utilisent `ON` / `OFF`; les commandes On, Off et Toggle sont acceptées.

## Relais, GPIO et câblage

| Relais | GPIO ESP32-C6 | Module relais | Fonction |
| --- | ---: | --- | --- |
| **K1** | **6** | IN1 | Fan Low / branche 21 kΩ |
| **K2** | **7** | IN2 | Fan High / branche 4 kΩ |
| **K3** | **10** | IN3 | Échange extérieur / liaison directe J13-J14 |

**GPIO11 est libre.** Si un module à quatre relais est utilisé, IN4/K4 n'est pas utilisé par la ventilation.

Utiliser des relais **SPST normalement ouverts à contacts secs**. Un relais SPDT convient aussi si seuls COM et NO sont utilisés. Voir [docs/RELAIS.md](docs/RELAIS.md) pour le schéma logique, la table d'états et la validation au multimètre.

Les entrées du module doivent être compatibles 3,3 V; une bobine seule ne se branche pas directement à un GPIO. J13/J14 et le circuit commandé restent isolés des GPIO et de la masse ESP32.

Dans `idf.py menuconfig` > **ESP-C6-UNIT01 relay control** :

- `APP_RELAY_ACTIVE_LOW` : **activé par défaut**. LOW active une bobine; désactiver pour un module actif HIGH.
- `APP_RELAY_SETTLE_MS` : **30 ms par étape**, à adapter aux temps de commutation et de rebond du module.

Les trois bobines sont relâchées avant de démarrer Zigbee ou Wi-Fi. Les états ne sont pas restaurés après une coupure.

## Table d'états des relais

| État | K1 Low | K2 High | K3 Exchange | J13-J14 |
| --- | ---: | ---: | ---: | --- |
| Off | 0 | 0 | 0 | ouvert |
| Fan Low | 1 | 0 | 0 | 21 kΩ |
| Fan High | 0 | 1 | 0 | 4 kΩ |
| Échange extérieur | 0 | 0 | 1 | proche de 0 Ω |

**K1, K2 et K3 ne doivent jamais être actifs simultanément.**

## Transitions sûres

Les transitions sont **break-before-make**. Le firmware ouvre toujours le relais actif avant d'en fermer un autre.

Exemples :

```text
High -> Exchange : K2 OFF -> délai -> K3 ON
Exchange -> High : K3 OFF -> délai -> K2 ON
Low -> Exchange  : K1 OFF -> délai -> K3 ON
```

Ainsi, la branche 4 kΩ n'est pas conservée pendant Exchange et les branches résistives ne sont jamais mises en parallèle entre elles ou avec K3.

## Mise à jour d'un appareil appairé

1. Vérifier le câblage K1/K2/K3, la polarité des entrées et les délais du module.
2. Compiler et flasher le firmware.
3. Remplacer le convertisseur externe dans Zigbee2MQTT par **un seul** fichier : `c6_door_sensor.mjs` (ESM, installations actuelles) ou `c6_door_sensor.js` (CommonJS, installations compatibles).
4. Refaire l'interview si nécessaire pour les endpoints 5, 6 et 7.
5. Tester les contacts relais avec J13/J14 déconnectés de la machine, puis valider les résistances/continuités décrites dans `docs/RELAIS.md`.

Le convertisseur lit les états pendant sa configuration. Le firmware publie après les commandes, après la connexion au réseau et toutes les 30 secondes.

Exemples vers `zigbee2mqtt/NOM_APPAREIL/set` :

```json
{"state_fan1":"ON"}
```

```json
{"state_fan2":"ON"}
```

```json
{"state_exchange":"ON"}
```

`state_exchange: ON` coupe toute vitesse active puis ferme **K3 seul**. `state_exchange: OFF` ouvre K3 et revient à Off si Exchange est actif.

## LED WS2812

La LED embarquée est une WS2812 sur **GPIO8**. La transmission RMT est exécutée dans une tâche FreeRTOS dédiée afin de ne pas bloquer le callback Zigbee. Le firmware vérifie les retours `led_strip`, réessaie jusqu'à trois fois lors d'une erreur et ne republie l'état Zigbee qu'après application physique réussie.

## Portes et portail Wi-Fi

Chaque contact de porte se branche entre son GPIO et GND. Le pull-up interne est actif : contact fermé = porte fermée; contact ouvert ou non raccordé = porte ouverte. Les entrées sont échantillonnées toutes les 10 ms et validées après 50 ms de stabilité.

Maintenir BOOT (GPIO9) environ trois secondes en mode normal puis relâcher. L'appareil enregistre CONFIG en NVS et redémarre. Se connecter à `NathanSensors-Setup`, ouvrir `http://192.168.4.1`, puis utiliser le bouton de la page pour revenir au mode normal.

En CONFIG, les relais sont relâchés; Zigbee et la surveillance des portes ne démarrent pas.

## Compilation

Versions verrouillées : ESP-IDF **5.5.4**, `esp-zigbee-lib` **1.6.8**, `esp-zboss-lib` **1.6.4**, `led_strip` **3.0.3**. Cible `esp32c6`, flash **8 Mo**, partition applicative 5 Mio. Le rôle Zigbee reste **routeur**.

```bash
idf.py menuconfig
idf.py build
idf.py -p COM5 flash monitor
```

Adapter le port.

## Fonctions internes et tests

`fan_set_mode(FAN_OFF / FAN_LOW / FAN_HIGH)` et `outdoor_exchange_set(bool)` déposent une requête dans la file de la tâche des relais. Une commande de mode quitte toujours Exchange. `fan_control_set_switch()` applique les quatre états sûrs exclusifs.

La logique indépendante du matériel est dans `main/fan_controller.c`; son adaptation GPIO/FreeRTOS est dans `main/fan_control.c`.

```bash
bash tests/run.sh
```

Les tests hôtes couvrent les **16 transitions entre les quatre états sûrs**, le break-before-make, l'exclusivité K1/K2/K3, les commandes OFF périmées, les erreurs GPIO simulées et le convertisseur Zigbee2MQTT.

Le moniteur série à 115200 bauds trace chaque commande, l'état avant/après et chaque bobine commandée. Exemple Exchange depuis High :

```text
I (...) FAN: K2 GPIO7 -> bobine RELACHEE (...)
I (...) FAN: K3 GPIO10 -> bobine ACTIVEE (...)
I (...) FAN: Etat applique: mode=OFF, échange=ON, K1(low)=0 K2(high)=0 K3(exchange)=1
```

Le workflow [Firmware validation](https://github.com/nathanbegin/ESP-C6-UNIT01/actions) exécute les tests logiques et compile le firmware sous ESP-IDF 5.5.4. Une compilation réussie fournit les binaires en artefact. Les essais électriques réels restent nécessaires avant raccordement à la machine.

`build/` et `managed_components/` sont exclus. L'import initial provient de `ESP-C6-UNIT01_2026-09-14.zip`; [l'analyse d'origine](docs/ANALYSE.md) et [l'ancien README](docs/README-original.md) sont conservés à titre historique.
