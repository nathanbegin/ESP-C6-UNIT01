# Réplication de SW1 (2P3T) et SW2 (SPST)

Position 1 = Off, position 2 = basse vitesse, position 3 = haute vitesse. Les noms ci-dessous viennent du schéma fourni. PA2 et PA3 étant reliés, trois relais SPDT suffisent pour SW1; K4 remplace SW2.

## Contacts

Déconnecter les anciens interrupteurs si le module les remplace. Conserver les résistances, la diode et les autres connexions du schéma.

| Relais | COM | NC | NO |
| --- | --- | --- | --- |
| K1 | CA, ancienne borne 2 | PA1, ancienne borne 1 | PA2–PA3, anciennes bornes 8 et 7 |
| K2 | CB, ancienne borne 6 | PB1, ancienne borne 5 | COM de K3 |
| K3 | NO de K2 | PB2, ancienne borne 4 | PB3, ancienne borne 3 |
| K4 | CB | Non raccordé | PA2–PA3 |

COM/NC/NO sont des contacts isolés. Ne pas relier ces communs à la masse ESP32. Les raccordements IN/VCC/GND dépendent du module réel.

## États des bobines

0 = relâchée, 1 = activée. La polarité électrique est inversée avec le réglage par défaut actif LOW.

| Mode | Échange | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- |
| Off | OFF | 0 | 0 | 0 | 0 |
| Off | ON | 0 | 0 | 0 | 1 |
| Fan 1 : basse | OFF | 1 | 1 | 0 | 0 |
| Fan 1 : basse | ON | 1 | 1 | 0 | 1 |
| Fan 2 : haute | OFF | 1 | 1 | 1 | 0 |
| Fan 2 : haute | ON | 1 | 1 | 1 | 1 |

GPIO6/GPIO7/GPIO10/GPIO11 commandent respectivement K1/K2/K3/K4.

## Transitions de vitesse

1. Relâcher K4, puis attendre.
2. Relâcher K1 et K2, puis attendre : position 1, K3 isolé de CB.
3. Positionner K3 selon la vitesse cible, puis attendre.
4. Activer K2 si la cible est une vitesse, puis attendre.
5. Activer K1 si la cible est une vitesse, puis attendre.
6. Rétablir K4 selon la demande d'échange, puis attendre.

Le délai entre étapes est réglable : six fois 30 ms par défaut, environ 180 ms hors ordonnancement. Un changement d'échange seul ne modifie pas K1/K2/K3. Une commande identique ne commute rien. Les transitoires ne reproduisent pas nécessairement ceux de l'ancien sélecteur mécanique.

Une erreur GPIO déclenche une tentative de relâcher toutes les bobines. Si elle réussit, Off/échange OFF est rapporté. Sinon, le contrôleur passe en défaut, refuse les nouvelles commandes et ne publie pas de nouvel état prétendument appliqué. Consulter les journaux et corriger la cause avant de redémarrer. Aucun retour matériel ne détecte un relais collé.

## Points à vérifier sur le circuit réel

Sur le dessin, D1 est contournée en position 1 puisque CA et son côté gauche partagent le même nœud. Les résistances équivalentes théoriques entre J14 et J13 sont :

| Position | SW2 ouvert | SW2 fermé |
| --- | --- | --- |
| 1 / Off | 21 kΩ | 21 kΩ |
| 2 / Basse | 10 kΩ | Proche de 0 Ω |
| 3 / Haute | 4 kΩ | Proche de 0 Ω |

Ce sont les conséquences du dessin, pas des mesures de l'appareil. SW2 fermé produit la même liaison directe en positions 2 et 3. Valider ce résultat sur la commande débranchée. Le logiciel reproduit ces connexions, sans interpréter la réponse du moteur aux résistances.

Vérifier la compatibilité 3,3 V, la polarité active, les délais et l'état des entrées pendant un reset. Le firmware impose Off dès son initialisation, mais ne contrôle pas la période qui précède son exécution.
