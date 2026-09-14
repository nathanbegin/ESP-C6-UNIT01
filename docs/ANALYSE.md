# Analyse historique du firmware ESP-C6-UNIT01 importé

Cette analyse décrit la version initiale `6e2bedb`, avant l’ajout des relais. La version actuelle corrige la synchronisation initiale, les états absents dans le convertisseur et le réglage de flash dans les defaults. Voir le README et [RELAIS.md](RELAIS.md) pour le fonctionnement actuel.

Analyse statique du 14 septembre 2026. Sources : fichiers contenus dans l'archive fournie. Les fichiers de programme et de configuration ont été conservés octet pour octet. Les constats ne prouvent pas que le firmware actuellement flashé sur la carte est identique à ces sources.

## Architecture

Le firmware C repose sur ESP-IDF et FreeRTOS. Trois tâches applicatives interviennent en fonctionnement normal : `door_task` lit les changements des contacts, `cfg_btn` surveille BOOT et `Zigbee_main` fait tourner la pile radio.

Les interruptions GPIO alimentent une file de dix événements. La tâche des portes attend 50 ms pour stabiliser une lecture; elle transmet uniquement les changements par rapport à `last_open`. La mise à jour du cluster et la notification prennent le verrou Zigbee. Les notifications ciblent directement l'adresse courte `0x0000` et l'endpoint `1`.

Le rôle réellement sélectionné est routeur, malgré le nom de macro `ESP_ZB_ZED_CONFIG()`. Il n'y a pas de veille profonde ni de stratégie d'économie de batterie dans l'application. Le dispositif est prévu pour rester alimenté.

Le mode de démarrage est enregistré dans l'espace NVS `appcfg`, clé `boot_mode`. En mode CONFIG, `app_main()` lance le portail et quitte la branche normale avant d'initialiser les contacts, la LED ou Zigbee. Les deux modes sont donc exclusifs dans cette implémentation. Il ne s'agit pas d'une preuve d'impossibilité générale de coexistence Wi-Fi/Zigbee sur le C6.

## Constats prioritaires

### 1. L'état initial des portes peut être faux ou absent

**Emplacement :** `door_task()`, `door_report()`, `build_endpoints()` et `esp_zb_app_signal_handler()` dans `main/zb_door_sensors.c`.

La tâche des portes démarre avant Zigbee. Elle lit les trois entrées, mais `door_report()` retourne sans mettre à jour les attributs si `s_zb_started` est faux. Les endpoints IAS sont ensuite créés avec `zone_status=0`, soit fermé.

Aucun nouvel envoi des trois lectures n'est déclenché après un démarrage ou un appairage réussi. Une porte déjà ouverte peut donc rester déclarée fermée, ou conserver un ancien état côté domotique, jusqu'au prochain changement électrique. Le convertisseur ne possède pas non plus de procédure `configure` qui initialise explicitement toutes les valeurs.

**Correction proposée :** après confirmation de connexion, relire les trois GPIO et synchroniser leurs attributs et notifications. Prévoir également une synchronisation après reconnexion.

### 2. « Pile démarrée » ne veut pas dire « réseau rejoint »

**Emplacement :** `esp_zb_app_signal_handler()`.

`s_zb_started=true` est posé dès `DEVICE_FIRST_START` ou `DEVICE_REBOOT`, avant que le steering soit terminé pour un appareil neuf. Un événement de porte peut donc déclencher un envoi alors que l'appareil n'a pas encore rejoint le réseau.

Les valeurs de retour de la mise à jour d'attribut et de la demande d'envoi ne sont pas vérifiées. Le journal indique « notif envoyée » sans confirmer sa réception. L'application n'implémente ni file de rattrapage des états ni contrôle de livraison; cela ne préjuge pas des mécanismes internes de retransmission Zigbee.

**Correction proposée :** distinguer initialisation de la pile et disponibilité du réseau, vérifier les erreurs et retransmettre l'état courant à la reconnexion.

### 3. Le convertisseur interprète une valeur absente comme « fermé »

**Emplacement :** `fzLocal.multi_ias_contact.convert()` dans `c6_door_sensor.js`.

Le code accepte les notifications, les rapports et les réponses de lecture IAS. Si le message n'a ni `zonestatus` ni `zoneStatus`, il remplace la valeur par `0` puis publie `contact_doorN=true`.

Un rapport IAS concernant uniquement un autre attribut pourrait donc forcer un état fermé injustifié. Le comportement est reproduit par un test isolé du convertisseur.

**Correction proposée :** ne rien publier lorsque l'attribut de statut manque, et vérifier son type.

## Compatibilité et robustesse

| Sujet | Constat et conséquence |
| --- | --- |
| Enrôlement IAS | Les zones démarrent non enrôlées avec une adresse CIE nulle. Le code applicatif n'implémente pas explicitement tout le dialogue d'enrôlement. Vérifier ce que gère la pile 1.6.8 et le coordinateur au cours d'un appairage réel. |
| Destinataire IAS | Adresse du coordinateur et endpoint 1 codés en dur. Les notifications ne suivent pas explicitement l'adresse CIE ou un destinataire configuré. Cela limite la portabilité vers d'autres coordinateurs. |
| File et anti-rebond | File limitée à dix événements, traitement séquentiel avec attente de 50 ms. En cas de rafale, une insertion peut échouer sans journalisation. Aucun balayage périodique ne recale ensuite l'état. |
| Rapport périodique | L'application émet sur changement; aucun heartbeat d'état des trois portes n'est programmé. |
| NVS | Plusieurs retours d'écriture et de commit ne sont pas vérifiés. Une erreur pourrait empêcher la bascule de mode malgré le redémarrage. |
| Portail | AP ouvert, sans authentification HTTP. Une personne connectée à cet AP peut demander le retour Zigbee. Aucune fonction de modification des GPIO n'est actuellement exposée. |
| Persistance du mode CONFIG | Une coupure de courant ne revient pas automatiquement au mode normal. Aucun délai de sortie automatique n'est prévu. |
| Portail captif | Redirection HTTP des 404 seulement; aucun serveur DNS captif dans les sources. |
| Flash | Le sdkconfig complet sélectionne 8 Mo; les defaults n'imposent pas cette taille malgré une partition factory de 5 Mio. Une configuration régénérée doit être vérifiée. |
| Mise à jour | Aucune gestion OTA applicative et aucune paire de partitions OTA dans `partitions.csv`. |
| Documentation d'origine | Elle contient d'anciens noms `DIYlab` et `C6.DoorSensor.3x`, absents de la définition actuelle du convertisseur. |
| Zigbee2MQTT | Convertisseur CommonJS non testé dans un serveur réel. Les instructions historiques d'installation doivent être adaptées à la version utilisée. |

La [documentation Zigbee2MQTT actuelle](https://www.zigbee2mqtt.io/advanced/more/external_converters.html), consultée le 14 septembre 2026, indique le dossier `external_converters`, la gestion depuis la console de développement et l'activation `enable_external_js` pour les nouvelles installations 2.11.0 et suivantes. Les exemples utilisent `.mjs`. Cette référence ne valide pas à elle seule la compatibilité du fichier CommonJS fourni.

## Fonctions absentes

Le code ne comporte pas de relais, commande de moteur, sortie analogique, liaison Matter, connexion au Wi-Fi domestique, client MQTT, serveur de contrôle des portes en fonctionnement Zigbee, choix de GPIO par interface, factory reset par interface ou mise à jour OTA.

Le portail et la logique de GPIO pourraient servir de base à des extensions, mais ces fonctions ne doivent pas être considérées comme déjà présentes.

## Vérifications effectuées et limites

- Lecture de tous les fichiers applicatifs, du convertisseur, des CMake, du manifeste, du verrou de dépendances, des configurations et de la table de partitions.
- Contrôle de syntaxe JavaScript avec Node.js.
- Vérification isolée des conversions ouvert/fermé pour les endpoints 2, 3 et 4 et des deux variantes de nom de champ. Les dépendances Zigbee2MQTT sont simulées : cela ne teste pas le chargement réel du convertisseur.
- Reproduction de la publication « fermé » lorsque le statut est absent.
- Vérification des chaînes ZCL : `ESP-C6-UNIT01` et `NathanSensors` font chacun 13 caractères. Leurs préfixes `0x0D` sont corrects, et le modèle correspond au convertisseur.
- Comparaison octet pour octet des fichiers source et de configuration importés avec l'archive.
- Recherche ciblée de clés privées et de formats courants de jetons ou mots de passe : aucun secret de ces catégories détecté dans les fichiers retenus. Ce contrôle n'est pas un audit exhaustif.

**Non effectué :** compilation ESP-IDF, flash, essai de la carte, test électrique, appairage Zigbee, intégration dans Zigbee2MQTT/Home Assistant et réception réelle des notifications. Les binaires et journaux présents dans l'archive ne prouvent pas une compilation réussie des sources exactes examinées.

## Ordre de correction conseillé

1. Synchroniser les états à la connexion et à la reconnexion; distinguer réseau disponible et pile démarrée.
2. Ignorer les messages sans statut dans le convertisseur.
3. Valider l'appairage et l'enrôlement IAS avec la version réelle de Zigbee2MQTT.
4. Compléter les defaults de compilation, vérifier les erreurs d'envoi/NVS et envisager une resynchronisation périodique.

Ces points sont documentés sans modification du comportement du firmware dans le livrable actuel.
