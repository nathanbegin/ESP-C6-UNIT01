# Commande J13/J14 avec trois relais SPST

Le firmware commande directement les trois états électriques utiles entre **J13** et **J14**, sans reproduire mécaniquement l'ancien sélecteur 2P3T.

D'après le schéma et l'interprétation retenue :

- **Fan Low** = branche **21 kΩ** entre J13 et J14;
- **Fan High** = branche **4 kΩ** entre J13 et J14;
- **Échange extérieur** = liaison directe J13-J14, donc **proche de 0 Ω**.

Le dernier état n'a pas besoin de conserver la branche 4 kΩ : un contact direct J13-J14 court-circuite électriquement cette résistance. Le firmware commande donc **K3 seul** pour l'échange extérieur.

## Attribution des relais

| Relais | GPIO ESP32-C6 | Fonction | Contact commandé |
| --- | ---: | --- | --- |
| **K1** | **GPIO6** | Fan Low | J13 -> 21 kΩ -> J14 |
| **K2** | **GPIO7** | Fan High | J13 -> 4 kΩ -> J14 |
| **K3** | **GPIO10** | Échange extérieur | J13 -> contact direct -> J14 |

**GPIO11 n'est pas utilisé par la ventilation.** Si un module physique à quatre relais est installé, son quatrième canal doit rester inutilisé pour cette fonction.

Utiliser des contacts secs **SPST normalement ouverts**. Un relais SPDT convient aussi si seuls COM et NO sont utilisés. J13/J14 ne doivent jamais être reliés directement aux GPIO ou à la masse logique de l'ESP32.

## Schéma logique

```text
                 K1 SPST NO
J13 ---------- o/ o -------- 21 kΩ --------+
                                           |
                 K2 SPST NO                |
J13 ---------- o/ o --------  4 kΩ --------+---- J14
                                           |
                 K3 SPST NO                |
J13 ---------- o/ o -----------------------+
                 Échange extérieur
```

Si les résistances 21 kΩ et 4 kΩ existent déjà dans le contrôleur d'origine, les relais doivent simplement commuter les branches correspondantes. Ne pas ajouter de résistances en parallèle sans avoir vérifié le circuit réel.

## États permis

`0` = relais relâché/contact ouvert. `1` = relais activé/contact fermé.

| État | K1 Low | K2 High | K3 Exchange | Équivalent J13-J14 |
| --- | ---: | ---: | ---: | --- |
| Off | 0 | 0 | 0 | ouvert |
| Fan Low | 1 | 0 | 0 | 21 kΩ |
| Fan High | 0 | 1 | 0 | 4 kΩ |
| Échange extérieur | 0 | 0 | 1 | proche de 0 Ω |

Les trois fonctions sont **mutuellement exclusives**. Il ne doit jamais y avoir plus d'un relais fermé à la fois.

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
- `Exchange ON` ouvre d'abord K1 ou K2 s'il y en a un d'actif, puis ferme **K3 seulement**;
- `Exchange OFF` ouvre K3 et retourne à Off;
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

Au démarrage, K1, K2 et K3 sont tous relâchés avant le lancement de Zigbee ou du portail Wi-Fi. L'état initial est donc **Off**.

Si une écriture GPIO échoue pendant une transition, le contrôleur tente d'ouvrir les trois relais. Si cette remise au repos réussit, Off est rapporté. Sinon, le contrôleur passe en défaut et refuse les nouvelles commandes jusqu'au redémarrage.

Le firmware commande les GPIO mais ne possède aucun retour de position mécanique : un relais physiquement collé ne peut pas être détecté par logiciel.

## Validation électrique avant raccordement

Avant de connecter J13/J14 à l'échangeur :

1. tester le module relais sans la machine;
2. confirmer que **K1 = GPIO6**, **K2 = GPIO7**, **K3 = GPIO10**;
3. vérifier au multimètre environ **21 kΩ** avec K1 seul;
4. vérifier environ **4 kΩ** avec K2 seul;
5. vérifier une liaison proche de **0 Ω** avec **K3 seul**;
6. vérifier qu'un seul relais peut être fermé à la fois, y compris pendant les transitions;
7. confirmer la polarité active LOW/HIGH du module relais.

Les valeurs 21 kΩ, 4 kΩ et 0 Ω proviennent du schéma de commande interprété. Elles doivent être confirmées sur le contrôleur réel, débranché de la machine, avant mise en service.
