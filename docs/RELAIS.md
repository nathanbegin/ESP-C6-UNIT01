# Commande J13/J14 avec trois relais SPST

Cette version remplace l'ancienne tentative de reproduire mécaniquement le sélecteur 2P3T. D'après le schéma de commande révisé :

- **P1 / Fan Low** correspond à la branche **21 kΩ** entre J13 et J14;
- **P2 / Fan High** correspond à la branche **4 kΩ** entre J13 et J14;
- **Échange extérieur / humidité** ferme une liaison directe J13-J14;
- la machine ne doit échanger avec l'extérieur **qu'en Fan High**.

Le firmware reproduit donc directement ces trois fonctions avec **trois relais SPST normalement ouverts à contacts secs**.

## Attribution des relais

| Relais | GPIO ESP32-C6 | Fonction | Contact commandé |
| --- | ---: | --- | --- |
| **K1** | **GPIO6** | Fan Low | J13 -> 21 kΩ -> J14 |
| **K2** | **GPIO7** | Fan High | J13 -> 4 kΩ -> J14 |
| **K3** | **GPIO10** | Échange extérieur | J13 -> contact direct -> J14 |

**GPIO11 n'est plus utilisé par la ventilation.** Si un module physique à quatre relais est installé, son quatrième canal doit rester inutilisé pour cette fonction.

Les contacts de puissance/commande des relais sont isolés de la logique ESP32. Ne jamais relier J13 ou J14 directement à un GPIO. Les entrées IN/VCC/GND du module relais doivent être câblées selon le module réel et être compatibles avec la logique 3,3 V de l'ESP32-C6.

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
                 Échange
```

Si les résistances 21 kΩ et 4 kΩ existent déjà dans le contrôleur d'origine, les relais doivent commuter les branches correspondantes plutôt que d'ajouter aveuglément de nouvelles résistances. Vérifier le câblage réel hors tension avant raccordement.

## États permis

`0` = relais relâché/contact ouvert. `1` = relais activé/contact fermé. La polarité électrique de l'entrée du module peut être active LOW; ce tableau décrit seulement l'état mécanique/logique du relais.

| État logique | K1 Low | K2 High | K3 Exchange | Équivalent J13-J14 |
| --- | ---: | ---: | ---: | --- |
| Off | 0 | 0 | 0 | ouvert |
| Fan Low | 1 | 0 | 0 | 21 kΩ |
| Fan High | 0 | 1 | 0 | 4 kΩ |
| Fan High + échange extérieur | 0 | 1 | 1 | proche de 0 Ω, avec la branche 4 kΩ toujours sélectionnée |

Les combinaisons suivantes sont **interdites par le firmware** :

- K1 et K2 fermés simultanément;
- K3 fermé lorsque K2 n'est pas fermé;
- échange extérieur en mode Off ou Fan Low.

## Logique des commandes Zigbee

Les endpoints restent les mêmes :

| Endpoint | Commande | Effet |
| ---: | --- | --- |
| 5 | Fan 1 / Low | sélectionne K1; coupe K2 et K3 |
| 6 | Fan 2 / High | sélectionne K2; coupe K1 |
| 7 | Échange extérieur | ON force d'abord Fan High/K2, puis ferme K3 |

Conséquences importantes :

- `Exchange ON` depuis Off ou Low passe automatiquement en **Fan High + Exchange**;
- `Exchange OFF` ouvre K3 mais **conserve Fan High**;
- `Fan Low ON` pendant un échange ouvre d'abord K3, quitte High, puis ferme K1;
- `Fan High OFF` lorsqu'il est actif arrête aussi l'échange et passe à Off;
- une commande OFF visant une vitesse qui n'est pas active ne coupe pas l'autre vitesse.

## Séquence break-before-make

Le contrôleur applique un délai configurable entre les opérations (`APP_RELAY_SETTLE_MS`, 30 ms par défaut).

Lors d'un changement de mode :

1. si K3 est fermé, ouvrir K3 et attendre;
2. ouvrir le relais de vitesse actuellement actif et attendre;
3. fermer le nouveau relais de vitesse et attendre;
4. si l'état cible demande l'échange, fermer K3 seulement après que K2/Fan High soit actif, puis attendre.

Cette séquence évite de mettre les branches 21 kΩ et 4 kΩ en parallèle et empêche la liaison directe J13-J14 d'être présente pendant un changement de vitesse.

## Démarrage et erreurs

Les trois relais sont commandés au repos avant le démarrage de Zigbee ou du portail Wi-Fi. Le firmware ne restaure pas un ancien mode après une coupure : l'état initial est **Off / échange OFF**.

Si une écriture GPIO échoue pendant une transition, le contrôleur tente d'ouvrir K3, K1 et K2. Si cette remise au repos réussit, l'état Off est rapporté. Si elle échoue, le contrôleur se marque en défaut et refuse les nouvelles commandes jusqu'au redémarrage. Il n'existe pas de retour mécanique permettant de détecter un relais physiquement collé.

## Validation électrique avant raccordement

Avant de connecter J13/J14 à l'échangeur :

1. tester le module relais sans la machine;
2. vérifier K1/K2/K3 au multimètre en mode continuité/ohmmètre;
3. confirmer environ 21 kΩ pour Low, 4 kΩ pour High et une liaison proche de 0 Ω uniquement pour High + Exchange;
4. vérifier qu'aucune transition ne ferme K1 et K2 en même temps;
5. vérifier que K3 ne ferme jamais sans K2;
6. confirmer la polarité active LOW/HIGH des entrées du module relais.

Les valeurs ci-dessus proviennent du schéma de commande fourni. Elles doivent être confirmées sur le contrôleur réel, débranché de la machine, avant mise en service.
