# Commande J13/J14 avec quatre relais exclusifs

Documentation du comportement déjà présent dans le firmware; l'ajout OTA ne change pas cette logique.

| Relais | GPIO | Branche entre J13 et J14 |
| --- | --- | --- |
| K1 Low | 6 | 10 kΩ |
| K2 High | 7 | 4 kΩ |
| K3 Exchange | 10 | Liaison directe, environ 0 Ω |
| K4 OFF | 11 | 21 kΩ |

Chaque branche comporte un contact sec normalement ouvert en série. Les quatre branches sont en parallèle entre J13 et J14, mais **un seul contact est fermé à la fois**. Un relais SPDT peut utiliser COM et NO. Ne pas relier J13/J14 aux GPIO ou à la masse de l'ESP32.

OFF ferme K4 seul; Low ferme K1 seul; High ferme K2 seul; Exchange ferme K3 seul. Au démarrage, y compris en mode portail Wi-Fi, le firmware applique OFF/K4. Pendant un reset, avant l'exécution du firmware, l'état dépend du matériel.

## Logique des commandes Zigbee

Les endpoints restent :

| Endpoint | Commande | Effet physique |
| ---: | --- | --- |
| 5 | Fan 1 / Low | K1 seul |
| 6 | Fan 2 / High | K2 seul |
| 7 | Échange extérieur | K3 seul |

Conséquences :

- `Fan Low ON` ouvre d'abord tout état actif puis ferme K1;
- `Fan High ON` ouvre d'abord tout état actif puis ferme K2;
- `Exchange ON` ouvre d'abord le relais actif (K1, K2 ou K4), puis ferme **K3 seulement**;
- `Exchange OFF` ouvre K3 et retourne à Off/K4;
- une commande OFF visant une fonction qui n'est pas active ne modifie pas l'état courant.

Le firmware publie les trois états logiques après la transition. Quand `Exchange` est ON, `Fan Low` et `Fan High` sont tous les deux rapportés OFF.

## Séquence break-before-make

Le délai entre opérations est configurable avec `APP_RELAY_SETTLE_MS` (30 ms par défaut).

Pour passer d'un état actif à un autre :

1. ouvrir le relais actuellement actif;
2. attendre le délai de stabilisation;
3. fermer le relais correspondant au nouvel état;
4. attendre de nouveau.

Exemples :

```text
High -> Exchange : K2 OFF -> délai -> K3 ON
Exchange -> High : K3 OFF -> délai -> K2 ON
Low -> Exchange  : K1 OFF -> délai -> K3 ON
```

Cette stratégie évite de mettre momentanément 21 kΩ et 4 kΩ en parallèle et évite également qu'une branche résistive reste fermée en même temps que le court-circuit K3.

## Démarrage et erreurs

Au démarrage, K1, K2 et K3 sont tous relâchés avant le lancement de Zigbee ou du portail Wi-Fi. K4 est fermé seul : l'état initial est **Off / 21 kΩ**.

Si une écriture GPIO échoue pendant une transition, le contrôleur tente d'ouvrir les quatre relais puis de fermer K4 seul. Si ce retour vers OFF/K4 réussit, Off est rapporté. Sinon, le contrôleur passe en défaut et refuse les nouvelles commandes jusqu'au redémarrage.

Le firmware commande les GPIO mais ne possède aucun retour de position mécanique : un relais physiquement collé ne peut pas être détecté par logiciel.

## Validation électrique avant raccordement

Avant de connecter J13/J14 à l'échangeur :

1. tester le module relais sans la machine;
2. confirmer que **K1 = GPIO6**, **K2 = GPIO7**, **K3 = GPIO10**, **K4 = GPIO11**;
3. vérifier au multimètre environ **10 kΩ** avec K1 seul;
4. vérifier environ **4 kΩ** avec K2 seul;
5. vérifier une liaison proche de **0 Ω** avec **K3 seul**;
6. vérifier **21 kΩ** avec K4 seul, puis qu'un seul relais peut être fermé à la fois, y compris pendant les transitions;
7. confirmer la polarité active LOW/HIGH du module relais.

Les valeurs 21 kΩ, 10 kΩ, 4 kΩ et 0 Ω proviennent du schéma de commande interprété. Elles doivent être confirmées sur le contrôleur réel, débranché de la machine, avant mise en service.
