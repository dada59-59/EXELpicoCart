# EXLCART — cartouche multi-jeux pour Exelvision EXL100 / EXELTEL sur Raspberry Pi Pico

EXLCART remplace l'EPROM d'une cartouche EXL100 par un **Raspberry Pi Pico (RP2040)**. Le Pico répond au bus de la console exactement comme une EPROM de 32 Ko. Le même firmware sait servir :

- **une ROM unique**, comme une cartouche classique ;
- **une bibliothèque** : un menu au démarrage, jusqu'à **190 jeux**, lancement aux flèches et à ENTRÉE, retour au menu par **reset** ;
- **une ROM à banques** : 16 Ko fixes et des banques de 16 Ko que le programme fait défiler en `$4000–$7FFF`, pour les jeux de plus de 32 Ko ; ou **deux images complètes de 32 Ko** (cartouches de 64 Ko comme le logiciel EXELTEL pour EXL100) ;
- **une manette DB9** de type Atari, branchée sur le Pico : envoyée à la console **par infrarouge, comme la manette Exelvision d'origine**, pour les jeux du commerce non modifiés ; ou lue directement dans la ROM par les jeux qui le prévoient, avec **une souris USB** ;
- **un clavier USB** (AZERTY) sur le port USB du Pico, envoyé par la même diode **comme le clavier Exelvision** ; et **une manette USB** (HID générique : manettes SNES USB, DualShock 4… ; ou Xbox 360, One, Series et compatibles XInput), qui s'ajoute à la DB9 ;
- **une vidéo** de 320 × 240 en noir et blanc ou en 8 couleurs, avec une **musique 1 bit** jouée par la sortie cassette : *Bad Apple!!* et *Big Buck Bunny* en exemples. Deux lecteurs : l'un où le Pico décode la vidéo, l'autre (« version disque ») où le Pico ne fait que servir un fichier et où l'EXL100 fait tout. La version disque se lance aussi depuis la bibliothèque.

Aucune ROM n'est compilée dans le firmware. Les jeux, la bibliothèque et les vidéos sont rangés dans la flash du Pico par des fichiers `.uf2` que fabrique l'outil web **EXLPICO** (`outils/exlpico.html`). Changer de jeu ne demande jamais de recompiler.

Deux câblages du Pico sont pris en charge, à la même vitesse : la carte **linéaire** et la carte **support EPROM** (le Pico soudé sur l'empreinte d'une 27C256). Le circuit imprimé **EXELpicoCart** (archive à part) reprend le câblage linéaire, avec la prise DB9 et l'émetteur infrarouge.

## État

| Fonction | Sur la machine |
|---|---|
| ROM unique | validée, EXL100 et EXELTEL, deux cartes, 19 ROM |
| Bibliothèque (menu, lancement, reset → menu) | validée, EXL100 et EXELTEL |
| ROM à banques (BANKTEST) | validée sur EXELTEL | 
| Deux images de 32 Ko (cartouche EXELTEL 64k pour EXL100) | validé sur EXL100 |
| Manette DB9 par infrarouge, jeux non modifiés | validée (carte linéaire) : Wizord d'origine, et mêmes codes que la vraie manette (`jstest`) | 
| Clavier USB par infrarouge | validé |
| Manette USB (HID générique) | validée | 
| Manette Xbox / XInput | validée | 
| Manette DB9 dans la zone d'échange | validée avec Wizord patché (carte linéaire) |
| Souris USB | Exelnoid : fonctionne (un plantage vu une fois, non reproduit) | 
| Vidéo, le Pico décode (Bad Apple, Big Buck Bunny) | validé |
| Musique 1 bit | validée sur EXELTEL (son par le téléviseur) |
| Version disque seule | validée sur EXELTEL, carte support EPROM, 15 à 30 images/s |
| Version disque lancée depuis la bibliothèque | validé | 
| Chargement de l'Exelmémoire depuis la bibliothèque (`.cram`, `.BKP`) | v4 sur EXELTEL : copies justes, Pindo et CB5QUAD démarrent |
| Sauvegarde de l'Exelmémoire dans le Pico, relue par EXLPICO en `.cram` / `.BKP` (v5.1) | **oui** (10/11/2026 : BURGER sauvé, relu, relancé par `CALL EXEC(32768)` dans un émulateur) |
| Cassettes `.k7` dans la bibliothèque (EXELBASIC, EXEL BASIC PLUS) : `LOAD "1"` depuis le Pico | validés | 


---

## Sommaire

1. [Contenu de l'archive](#1-contenu-de-larchive)
2. [Matériel et câblage](#2-matériel-et-câblage)
3. [Démarrage rapide](#3-démarrage-rapide)
4. [La bibliothèque et son loader](#4-la-bibliothèque-et-son-loader)
5. [Les ROM à banques (plus de 32 Ko)](#5-les-rom-à-banques-plus-de-32-ko)
6. [Manettes et souris](#6-manettes-et-souris)
7. [Vidéo et musique](#7-vidéo-et-musique)
8. [L'outil EXLPICO](#8-loutil-exlpico)
9. [Compiler le firmware du Pico](#9-compiler-le-firmware-du-pico)
10. [Fonctionnement technique](#10-fonctionnement-technique)
11. [La sonde (diagnostic)](#11-la-sonde-diagnostic)
12. [Dépannage](#12-dépannage)
13. [Limites et pistes](#13-limites-et-pistes)

---

## 1. Contenu de l'archive

```
EXLCART/
├── LISEZMOI.md                     ce document
├── firmware/                       sources du firmware du Pico (C, Pico SDK)
│   ├── CMakeLists.txt              projet : cibles exlcart, exlsonde, exlcart_eprom, exlsonde_eprom (+ variantes d'essai)
│   ├── pico_sdk_import.cmake       recherche du Pico SDK (fichier standard du SDK)
│   ├── exlcart.c                   firmware de la cartouche : choix du mode au démarrage
│   ├── exlpins.h                   brochage des deux cartes, rangement des images en RAM
│   ├── exlserve.h                  service du bus, mode ROM unique
│   ├── exllib.h                    bibliothèque : flash, menu, chargement des jeux
│   ├── exlsave.h                   sauvegardes de l'Exelmémoire dans la flash du Pico (menu v5.1)
│   ├── exlk7.h                     cassettes .k7 : le BASIC, sa routine de lecture et la bande
│   ├── exlusbdev.c                 Pico seul sur un PC : port série USB pour relire les sauvegardes
│   ├── exlmulti.h                  machine d'états de la bibliothèque (menu / lancement / jeu)
│   ├── exlbanks.h                  ROM à banques de 16 Ko, réserve d'images en RAM
│   ├── exljoy.h                    zone d'échange : manette DB9 (anti-rebond), compteurs de souris
│   ├── exlmouse.c                  souris, clavier et manette USB : pile TinyUSB en hôte sur le port USB du Pico
│   ├── tusb_config.h               configuration de TinyUSB (hôte : HID, petit hub ; périphérique : port série)
│   ├── exlir.h                     la DB9 envoyée par infrarouge (codes et trames du MC14497)
│   ├── exlir.pio                   la porteuse et les trames infrarouges (machine PIO)
│   ├── exlkbd.h                    clavier USB → codes du clavier EXL100 (AZERTY), en séquence
│   ├── exlpad.h                    manette USB : lecture de son descripteur de rapport, directions, feu, touches
│   ├── exlxinput.h                 manettes Xbox 360 / One / Series et XInput : reconnaissance, mise en marche, rapports
│   ├── exlvideo.h                  vidéo : service par V_FIFO (cœur 1), décodage, listes et musique (cœur 0)
│   ├── exlvcodec.h                 codec vidéo, noir et blanc ou 8 couleurs (décodeur ; encodeur pour le PC)
│   ├── exlvenc.c                   encodeur vidéo sur PC (le même que exlvideo.py, en C)
│   ├── exlflux.h                   version disque : le Pico sert deux fichiers octet par octet
│   ├── exlfluxlib.h                le lecteur disque parmi les jeux de la bibliothèque
│   ├── exlsonde.c                  firmware sonde : cartouche + relevé des accès, lu par USB
│   ├── exloscillo.pio              programme PIO de la sonde (« oscilloscope » 20 ns)
│   ├── test_multi.c                test sur PC de la machine d'états
│   ├── test_pins.c                 test sur PC du brochage et du rangement des images
│   ├── test_io.c                   test sur PC de la zone d'échange et des compteurs de souris
│   ├── test_kbd.c                  test sur PC du clavier USB : correspondances et séquences envoyées
│   ├── test_pad.c                  test sur PC de la manette USB : descripteurs de vraies manettes, rapports
│   ├── test_xinput.c               test sur PC des manettes XInput : descripteurs, rapports, paquets envoyés
│   ├── test_video.c                test sur PC du décodeur, des listes et de la musique 1 bit
│   └── uf2/
│       ├── firmware_seul_exlcart_SANS_ROM.uf2          cartouche, carte linéaire
│       ├── firmware_seul_exlcart_eprom_SANS_ROM.uf2    cartouche, carte support EPROM
│       ├── firmware_seul_exlsonde_SANS_ROM.uf2         sonde, carte linéaire
│       └── firmware_seul_exlsonde_eprom_SANS_ROM.uf2   sonde, carte support EPROM
├── loader/
│   ├── exlmenu.asm                 le menu de la bibliothèque (assembleur TMS7020)
│   └── k7/                         k7load.asm : la lecture cassette depuis le Pico (EXLPICO en contient le code assemblé)
├── exemples/
│   ├── jstest/                     test des manettes infrarouges (§6.5)
│   │   ├── jstest.asm              codes reçus, cadence, un caractère qui se déplace
│   │   ├── jstest.rom              le même, assemblé
│   │   └── jstest_carte_lineaire.uf2, jstest_carte_eprom.uf2   firmware + ROM
│   ├── joyexl.asm                  routine à inclure : manettes infrarouges → octet au format de la DB9
│   ├── banktest.asm, bank1.asm     test des banques : ROM principale, et la banque 1 (§5)
│   ├── exelnoid_exlcart.diff       Exelnoid : souris USB et manette DB9 (différences avec l'original)
│   ├── wizord/, tennis/, virus/, guppy/, menkar/, pindo/
│   │                               patchs « zone d'échange » pour la DB9 (il faut votre ROM d'origine)
│   ├── exelpaint/                  patch d'ExelPaint pour la souris USB (il faut votre ROM)
│   ├── badapple/                   vidéo noir et blanc avec musique 1 bit, le Pico décode (§7.1)
│   │   ├── badapple.asm, badapple.rom    le lecteur vidéo (zone EXLV), le même pour toutes les vidéos
│   │   ├── badapple.exv            Bad Apple!! sans musique : 6 955 images, 1,3 Mo
│   │   ├── badapple_1bit.exv       avec la musique 1 bit : 1,8 Mo
│   │   └── badapple_carte_lineaire.uf2, badapple_carte_eprom.uf2   firmware + lecteur + vidéo
│   ├── bigbuckbunny/               vidéo en 8 couleurs (§7.1)
│   │   ├── CREDITS.txt             Big Buck Bunny, (c) Blender Foundation, licence CC BY 3.0
│   │   ├── bigbuckbunny.exv        la première minute : 1 442 images, 0,8 Mo
│   │   └── bigbuckbunny_carte_lineaire.uf2, bigbuckbunny_carte_eprom.uf2
│   └── badflux/                    version disque : le Pico sert, l'EXL100 fait tout (§7.3)
│       ├── badflux.asm, badflux.rom      le lecteur disque (zone EXLD)
│       └── badflux.exf             le film de Bad Apple pour ce lecteur (10 Mo) ; les .uf2 (21 Mo) sont à part
└── outils/
    ├── exlpico.html                outil web : ROM, bibliothèque, banques, vidéo, version disque, sonde → .uf2
    ├── exlvideo.py                 n'importe quelle vidéo → .exv (lecteur vidéo), avec ffmpeg
    ├── exlflux.py                  n'importe quelle vidéo → .exf (version disque), avec ffmpeg
    ├── integrer_firmware.py        intègre un firmware recompilé dans exlpico.html
    └── bkp2cram.py                 sauvegarde Exelmémoire .BKP → image .cram (émulateurs) ; --inverse : .cram → .BKP
```

Les quatre firmwares de `firmware/uf2/` sont **ceux qui sont intégrés dans `exlpico.html`**. EXLPICO met lui-même le bon dans les fichiers qu'il fabrique, d'après la carte choisie.


---

## 2. Matériel et câblage

### 2.1 La carte Pico

Il faut une carte RP2040 qui **sort la broche GP23** : c'est elle qui reçoit `/ROM_CS`. Sur le Raspberry Pi Pico officiel, GP23 n'est pas accessible (elle pilote son alimentation). La carte utilisée est un **« Purple Pico » YD-RP2040, 16 Mo de flash**. Sur cette carte, GP23 commande aussi la LED RGB : elle change parfois de couleur pendant le jeu, sans conséquence. GP25 commande la LED bleue, qui sert aussi de témoin à l'émetteur infrarouge.

Place dans la flash : une bibliothèque demande au moins 4 Mo ; les vidéos et la version disque, la carte de 16 Mo.

### 2.2 Trois façons de câbler

| | Carte linéaire | Carte support EPROM | Circuit imprimé EXELpicoCart |
|---|---|---|---|
| Principe | fils directs, A0–A14 sur GP0–GP14, D0–D7 sur GP15–GP22 | le Pico soudé presque directement sur l'empreinte d'une 27C256, quelques lignes reroutées | carte cartouche complète, Pico violet sur supports, DB9 et émetteur IR |
| Firmware | `exlcart` | `exlcart_eprom` | `exlcart` (le câblage linéaire) |
| Feu de la DB9 | GP24 (aussi le bouton USR) | GP17 | GP24 |
| État | validée | validée, du premier coup | commandée |

Dans tous les cas : **/ROM_CS → GP23**, **+5 V → VBUS**, **masse → GND**. Sur la cartouche, /ROM_CS est relié à la fois à /CE et à /OE de l'EPROM. Tout est branché **en direct**, sans pont diviseur ni résistance.

**Un firmware installé sur la mauvaise carte ne sert rien de cohérent** : la console ne voit pas de cartouche. EXLPICO demande la carte en premier.

**Carte support EPROM** :

| Broche EPROM | Signal | Pico | | Broche EPROM | Signal | Pico |
|---|---|---|---|---|---|---|
| 1 | VPP | — | | 15 | D3 | GP18 |
| 2 | A12 | GP6 | | 16 | D4 | GP19 |
| 3 | A7 | GP7 | | 17 | D5 | GP20 |
| 4 | A6 | GP8 | | 18 | D6 | GP21 |
| 5 | A5 | GP9 | | 19 | D7 | GP22 |
| 6 | A4 | GP10 | | 20 | /CE (/ROM_CS) | GP23 |
| 7 | A3 | GP11 | | 21 | A10 | GP24 |
| 8 | A2 | GP12 | | 22 | /OE | (reliée à /CE sur la cartouche) |
| 9 | A1 | GP0 | | 23 | A11 | GP3 |
| 10 | A0 | GP13 | | 24 | A9 | GP2 |
| 11 | D0 | GP14 | | 25 | A8 | GP1 |
| 12 | D1 | GP15 | | 26 | A13 | GP4 |
| 13 | D2 | GP16 | | 27 | A14 | GP5 |
| 14 | GND | GND | | 28 | VCC | VBUS |

GP17 et GP25 restent libres (feu de la DB9 et diode infrarouge). Sur la plupart des YD-RP2040, GP24 est aussi relié au bouton **USR** : sur cette carte, ne pas l'appuyer quand la cartouche est dans la console, il mettrait A10 à la masse.


Le RP2040 n'est pas officiellement tolérant au 5 V, mais ses broches numériques non-ADC le supportent en pratique (même choix que A8PicoCart pour l'Atari 8 bits). Les broches ADC (GP26–GP29) ne reçoivent jamais de 5 V : elles ne servent qu'à la DB9, en entrées 3,3 V.

### 2.3 La manette DB9

La manette ne fait que relier ses contacts à la masse. Le Pico met ses résistances de tirage internes en 3,3 V : **la manette ne reçoit jamais de 5 V**.

| DB9 (manette) | Signal | Carte linéaire, EXELpicoCart | Carte support EPROM |
|---|---|---|---|
| 1 | haut | GP26 | GP26 |
| 2 | bas | GP27 | GP27 |
| 3 | gauche | GP28 | GP28 |
| 4 | droite | GP29 | GP29 |
| 6 | feu | **GP24** | **GP17** |
| 8 | masse | GND | GND |
| 5, 7, 9 | — | **non reliées** | **non reliées** |

- Côté cartouche, une **prise DB9 mâle**. Vue de face, broches vers soi : 1 à 5 sur la rangée du haut, de gauche à droite ; 6 à 9 en dessous. Côté soudure, l'ordre est inversé.
- **Ne jamais relier la broche 7 (+5 V Atari).** Une manette passive n'en a pas besoin.
- Conseillé sur un montage à fils : une résistance de 1 kΩ en série sur chaque ligne (1, 2, 3, 4, 6), contre les courts-circuits et l'électricité statique.
- Manettes à éviter : Mega Drive (elles ont besoin du +5 V et d'une ligne de sélection), et tout ce qui demande la broche 7 ou 5.

### 2.4 L'émetteur infrarouge (facultatif)

Une diode infrarouge sur **GP25** envoie la DB9 à la console comme la manette Exelvision d'origine ([§6.2](#62-la-db9-par-infrarouge--les-jeux-non-modifiés)). Deux montages, sur toutes les cartes, avec le même firmware.

**Avec un transistor** (celui du circuit imprimé) : la diode est alimentée par le 5 V, avec de la marge, comme une télécommande.

```
                +5 V (VBUS)
                   │
                  ┌┴┐
                  │ │ 68 Ω
                  └┬┘
                   │  anode (patte longue)
                  ─┴─
                  ╲ ╱  diode infrarouge 940 nm  ⇉ vers la fenêtre infrarouge de la console
                  ─┬─
                   │  cathode (côté plat)
                   │ C
   GP25 ──[ 1 kΩ ]─┤ B   transistor NPN (2N3904, PN2222A, BC547, 2N2222…)
            │      │ E
         [ 10 kΩ ] │
            │      │
   GND ─────┴──────┘
```

- GP25 ne fournit que 2–3 mA : c'est le transistor qui commande la diode. Courant dans la diode : environ **50 mA** en impulsions, pendant la porteuse seulement. 47 Ω pour un peu plus de portée.
- La 10 kΩ tient la diode éteinte au démarrage, avant que le firmware ne prenne GP25.

**En direct, sans transistor** : quand la diode est **à quelques centimètres** du récepteur (6 cm environ sur une cartouche), GP25 peut la commander seul.

```
   GP25 ──[ 150 Ω ]──►|── GND        (ou GP25 ──►|──[ 150 Ω ]── GND : c'est pareil)
                    anode  cathode (côté plat, patte courte)
```

- Le firmware règle GP25 à **12 mA**, le maximum d'une broche du RP2040 : environ 10 mA dans la diode, 5 fois moins qu'avec le transistor, donc à peu près deux fois moins de portée. **Ne pas descendre sous 150 Ω.**
- La diode doit **viser la fenêtre** du récepteur. Si une console ne réagit pas ou seulement de près, passer au montage avec transistor.
- Au démarrage, GP25 est une entrée : la diode reste éteinte sans résistance de rappel.
- Une diode 5 mm ordinaire (TSAL6200, SFH 4544, ou celle d'une télécommande), **au bout d'un fil ou les pattes pliées**, pointée vers la fenêtre infrarouge de la console (celle que vise le clavier). Le récepteur de l'EXL100 / EXELTEL est à droite du port cartouche.
- **L'ordre des pattes du transistor dépend du modèle.** Face plate vers soi, pattes en bas : 2N3904 et PN2222A = E B C ; BC547 et P2N2222A = C B E. En cas de doute, le repérer au multimètre (position diode : la base donne environ 0,65 V vers les deux autres pattes).
- **Pour vérifier le montage**, mettre un moment une diode verte à la place de la diode infrarouge : éteinte au repos, elle doit clignoter quand on bouge la manette. Allumée en permanence : transistor défectueux ou mal câblé (vu une fois). Pour voir la diode infrarouge elle-même, filmer avec la caméra **avant** d'un téléphone (les caméras arrière filtrent souvent l'infrarouge).

### 2.5 La souris, le clavier et la manette USB

- Une souris, un clavier et/ou une manette USB ordinaires sur le **port USB du Pico**, avec un **adaptateur OTG** (USB-C ou micro-USB mâle vers USB-A femelle, selon la carte) ; plusieurs ensemble par un petit hub, ou un clavier avec pavé tactile. Aucune broche n'est utilisée.
- Le clavier passe par l'émetteur infrarouge ([§6.3](#63-un-clavier-usb-par-infrarouge)) : il faut la diode sur GP25. La manette va où va la DB9 : par l'émetteur, ou dans la zone d'échange ([§6.4](#64-une-manette-usb)).
- Elle est alimentée par le 5 V de la cartouche (20 à 100 mA pour une souris courante). Une souris derrière un petit hub USB est acceptée.
- Elle est reconnue environ une demi-seconde après la mise sous tension ; la cartouche, elle, est servie immédiatement.
- La souris n'est lue que par les jeux qui la prévoient ([§6.6](#66-la-zone-déchange--les-jeux-patchés-et-les-nouveaux-jeux)) ; le clavier marche partout, sauf en vidéo et en version disque.

### 2.6 Précautions

- **Ne jamais brancher l'USB à un PC ni à un chargeur quand le Pico est dans la console.** Les deux alimentations 5 V se retrouveraient reliées. (Le clavier, la manette et la souris USB, par l'adaptateur OTG, sont prévus : c'est le Pico qui les alimente.) Le menu le rappelle sous le titre depuis la v5.2.
- Insérer et retirer la cartouche **console éteinte**.
- **Vérifier le sens avant d'insérer** : la carte entre aussi à l'envers, et le Pico reçoit alors son alimentation inversée (un Pico perdu ainsi). Marquer la face du dessus.
- Sur l'EXL100, préférer un **Pico soudé** à un Pico sur supports femelles (voir [§12](#12-dépannage)).
- Pour programmer le Pico : le sortir de la console, débrancher la souris, maintenir **BOOT** (BOOTSEL), brancher l'USB, relâcher. Un lecteur `RPI-RP2` apparaît : y copier le fichier `.uf2`. Le Pico redémarre tout seul à la fin de la copie.

---

## 3. Démarrage rapide

Ouvrir `outils/exlpico.html` dans un navigateur (Chrome ou Edge recommandés : la lecture de la sonde utilise Web Serial). Tout fonctionne hors ligne.

**Choisir d'abord la carte**, en haut de la page (carte **Carte (brochage)**) : linéaire (aussi pour EXELpicoCart) ou support EPROM. Le choix est mémorisé. Les fichiers qui contiennent un firmware prennent celui de cette carte, et leur nom se termine par `_eprom` pour la carte support EPROM.

Chaque fichier « complet » contient le firmware : **après une mise à jour d'EXLPICO, refaire un fichier complet** au moins une fois.

### Une ROM unique

1. Carte **ROM** : **Choisir la ROM…** EXLPICO l'analyse (pied, point d'entrée, zones particulières).
2. Carte **Fichier .uf2** : **UF2 complet (firmware + ROM)** la première fois ; **UF2 ROM seule** pour changer de jeu ensuite.
3. Copier le `.uf2` sur le Pico, insérer, allumer.

Avec la DB9 et l'émetteur infrarouge, un jeu du commerce reçoit la manette comme la manette Exelvision 1, sans rien régler.

### Une bibliothèque

1. Assembler le loader ([§4](#4-la-bibliothèque-et-son-loader)) : on obtient `exlmenu.rom`.
2. Carte **Bibliothèque de jeux (menu)** : **Loader (menu)…** = `exlmenu.rom`, puis **Ajouter des jeux…** (plusieurs à la fois). Les noms viennent des noms de fichiers (20 caractères, majuscules) : on peut les modifier, réordonner les jeux (↑ ↓), en retirer (×), ou **Trier par nom**.
3. Pour mettre aussi Bad Apple : ajouter `exemples/badflux/badflux.rom` aux jeux, et choisir `exemples/badflux/badflux.exf` dans la carte **Version disque**. Le film est rangé après les jeux.
4. **UF2 bibliothèque complète (firmware + menu + jeux)**, ou **UF2 bibliothèque seule** pour changer seulement la liste.
5. Copier sur le Pico, insérer, allumer : le menu s'affiche.

| Clavier / manette infrarouge | Manette DB9 ou USB | Souris USB | Action |
|---|---|---|---|
| ↑ ↓ | haut / bas | glisser vers le haut / le bas | jeu précédent / suivant (la liste boucle) |
| ← → | gauche / droite | — | page précédente / suivante (15 jeux par page) |
| ESPACE ou ENTRÉE (feu de la manette IR) | feu | un clic | lancer le jeu (il démarre au **relâchement**) |
| **Reset** pendant une partie | | | retour au menu |

Si un jeu ne démarrait pas après sa sélection, un **reset** le lance quand même : le Pico est déjà basculé sur lui.

### Une ROM à banques

1. Carte **ROM** : la ROM principale (zone fixe + banque 0).
2. Carte **Banques de 16 Ko** : **Ajouter des banques…** ; elles prennent les numéros 1, 2, 3… dans l'ordre de la liste.
3. **UF2 complet**, ou **UF2 ROM seule**. Le nom du fichier indique le nombre de banques.

### Une cartouche de 64 Ko (deux images de 32 Ko)

Carte **ROM** : choisir le fichier de 64 Ko. S'il porte un pied (`AA 8C` ou `55 8C`) en `$7FFC` de chaque moitié, EXLPICO passe tout seul en mode **2 × 32 Ko** et l'indique dans la carte des banques. **UF2 complet** ou **UF2 ROM seule** ; le nom du fichier finit par `_2x32ko`. Voir [§5.1](#51-deux-images-de-32-ko).

### Une vidéo

- **Le plus simple** : copier sur le Pico un fichier tout fait, `exemples/badapple/badapple_carte_lineaire.uf2` (ou `_carte_eprom`), ou celui de Big Buck Bunny.
- **Avec EXLPICO** : la ROM `badapple.rom`, puis la vidéo `.exv` dans la carte **Vidéo**, et **UF2 complet + vidéo**.
- **La version disque** : la ROM `badflux.rom`, puis le film `badflux.exf` dans la carte **Version disque**, et **UF2 complet + disque** (21 Mo, plusieurs minutes de copie). Ou dans une bibliothèque, comme ci-dessus.

Il faut la carte de 16 Mo. Détails : [§7](#7-vidéo-et-musique).

---
## 4. La bibliothèque et son loader

`loader/exlmenu.asm` est le menu affiché par la bibliothèque. C'est un programme de cartouche ordinaire, assemblé comme un jeu.

### Compilation

Mettre dans le même dossier que `exlmenu.asm` les fichiers de votre kit de développement EXL100 : **`7020.equ`** et **`3556.equ`** (ils ne sont pas fournis ici ; `mixt_api.asm` n’est plus nécessaire depuis la v3.4 du menu, qui contient elle-même les quatre instructions de son `init_vdp`). Puis :

```
tasm -tEXL -a -b exlmenu.asm
obj2exl.exe exlmenu.obj exlmenu.rom -t:ROM -r:0x1000 -p
```

Donner ensuite `exlmenu.rom` à EXLPICO comme **loader**.

### Règles à respecter si vous modifiez le loader

- **Ne rien assembler entre `$6F00` et `$7EFF`.** C'est la zone d'échange avec le Pico ; EXLPICO refuse un loader qui l'utilise.
  - `$7000` : nombre de jeux, puis à partir de `$7010`, 20 caractères par jeu. Le Pico **insère** cette liste dans l'image du menu au démarrage.
  - `$6F00 + n` : lire cette adresse **choisit le jeu n**. Le Pico le charge en arrière-plan (environ 1 ms).
  - `$6FFF` : lire cette adresse **bascule la cartouche sur le jeu** : l'accès suivant lit déjà le jeu.
- **Exelmémoire (v4) : ne rien assembler non plus de `$2800` à `$67FF`, ni de `$6DFC` à `$6EFF`** (le code du menu doit finir avant `$2800` ; il finit vers `$2350`).
  - `$6E00 + n` : lire cette adresse demande l'**image Exelmémoire n** ; le Pico met `$6DFF` à 0, copie les 16 Ko dans la fenêtre `$2800`–`$67FF` **du menu**, puis écrit `$A5` en `$6DFF`. Le menu reste servi.
  - Les images Exelmémoire de la liste ont le **bit 7 du premier caractère** de leur nom à 1 (posé par EXLPICO).
  - `$6DFE` (v4.1) : « l'image démarre » ; `$6DFD` : « sans cartouche » ; `$6DFC` : annulation. Le cœur de service ne fait que les **noter** (`g_hot`, et `g_boots` pour un pied lu après une pause) ; le cœur 0 agit (`cram_step`, `exlmulti.h`). La boucle de service compile exactement comme avant.
- **Sauvegarde de l'Exelmémoire (v5.1) : ne rien assembler de `$6BF0` à `$6CFF`** (le code finit vers `$2350`) :
  - `$6BF0` : début ; `$6C00 + v` : **envoie l'octet v** (la console ne peut pas écrire dans la cartouche : le Pico lit l'octet sur le bus d'adresses) ; `$6BF1` : fin ;
  - réponse du Pico en `$6BF2` (`$A5` rangée, `$E1` transmission fausse, `$E2` flash), emplacement (1–8) en `$6BF3` ; [détails](#sauvegarder-lexelmémoire-dans-le-pico-v51).
- Variables du menu en RAM externe à `$C500`–`$C516`, routine de lancement copiée en `$C7F9`, tampons de la ligne bitmap en `$C520`–`$C597`. Registres du dessin bitmap : R44–R57 (la ROM n'utilise que R2–R40).
- Le loader porte sa propre zone d'échange (`EXLP`, voir [§6](#6-manette-db9-et-souris-usb)) : manette DB9 et souris USB. `poll_level` lit le clavier, puis la manette DB9 traduite en flèches / ESPACE, puis les boutons de la souris ; `mouse_step` transforme 6 unités de déplacement vertical en une ligne. `wait_release` attend aussi que le feu et les boutons soient relâchés : le jeu ne démarre pas feu appuyé.
- Clavier repris d'Exeltris (`VALUE0`).

### Écran (v5, 10/11/2026)

Une **page mixte** du TMS3556, qui commence en VRAM `$0600` et se termine en `$6B5B` (la police du menu, BAGC3, est en `$7600` ; celle de la ROM reste en `$7000`) :

| Lignes | Mode | Contenu |
|---|---|---|
| 0 | texte | haut du **bandeau cyan**, vide (le mode mixte doit commencer par une ligne de texte) |
| bitmap 0–49 | **bitmap** (à partir de `$0652`) | suite du bandeau : le **papillon d'Exelvision** en haut à gauche, « EXELpicoCart » en noir (lignes 10–19), puis la **mise en garde USB** (v5.2) : « USB : CLAVIER, MANETTE, SOURIS » en noir (lignes 24–33) et « JAMAIS DE PC NI DE CHARGEUR ! » en rouge (lignes 34–43) |
| bitmap 50–199 | **bitmap** (à partir de `$1E26`) | la **liste** sur fond noir, 15 lignes de 10 points sur toute la largeur ; images Exelmémoire en cyan, cassettes K7 en jaune (v5.2), jeu choisi sur une barre bleue. EXLPICO marque les noms dans le catalogue : bit 7 du 1er caractère pour une image Exelmémoire, du 2e pour une cassette (vérifié : `EXLOUTILS/simulateur_tms7020/test_menu_couleurs.py`) |
| bitmap 200–209 | bitmap | une ligne vide |
| 22–24 (`$6A66`, `$6AB8`, `$6B0A`) | texte | la **ligne d'état**, noir sur cyan : nombre de jeux et page, puis l'aide ou les messages (police 5×7 de mixpix dans BAGC3, plus les minuscules p i c o a r t du titre, codes 1 à 7 de la chaîne) |

- **Marges** : l'octet de contrôle de chaque ligne (121e en bitmap, 81e en texte) donne aussi la couleur des marges de la ligne suivante, et celui de la dernière ligne, les marges du haut et du bas. Elles sont cyan autour du bandeau et de la ligne d'état, noires le long de la liste.
- **La liste et le titre sont dessinés en bitmap** avec la même police. Chaque point de chaque plan (bleu, vert, rouge) vaut `(glyphe ET a) OU-EXCLUSIF x`, où `a` et `x` dépendent de la couleur du texte et du fond. Une ligne de 40 cases prend environ 30 ms. Un déplacement dans la page ne redessine que l'ancienne et la nouvelle ligne choisie. Un changement de page redessine les 15 lignes, environ une demi-seconde.
- Jusqu'à la v4.1 : titre et aide en texte, liste à gauche (28 colonnes, 20 jeux par page), panneau cyan à droite avec le papillon.
- **Le papillon est rangé dans le loader** (`logo_bmp`, 792 octets) depuis la v3, ce qui donne le même rendu sur EXL100 et sur EXELTEL. C'est celui de l'écran d'allumage de l'EXL100 : sur cette machine, le processeur d'E/S (7041) l'envoie à la ROM (commande `$0D`, données en `$FC18` de sa ROM), qui le dessine avec son `trap 11`. L'EXELTEL ne l'a pas (vérifié dans un émulateur). Le dessin a donc été extrait de ces données, sans le mot « exelvision », et converti en bitmap : 44 lignes de 6 groupes de 8 points, fond cyan compris. Le menu le copie simplement dans la mémoire vidéo.
- **BAMTF** (registre 15 du VDP) est mise à `$7FFF` au démarrage, tout en haut de la mémoire vidéo (v3.3 ; `$7800` est un des deux suspects de la corruption de Wizord).

### Séquence de lancement

1. Lecture de `$6F00 + n` : le Pico charge le jeu n.
2. Attente du **relâchement de toutes les touches** : 8 lectures consécutives « aucune touche », espacées d'environ 15 ms. Un rebond relance l'attente. Puis encore environ 150 ms.
3. Remise de la machine dans l'état où la ROM laisse un jeu après un reset (v3.3) : interruptions coupées, affichage éteint (registre 6 = 0), RAM `$C100`–`$C7F8` et registres R2–R127 effacés, pile à 0. Le menu laisse sinon des restes de son dessin bitmap (R44–R57, `$C520`…), l’autre suspect de la corruption de Wizord.
4. Copie en RAM (`$C7F9`) d'une routine de 7 octets, interruptions remises, puis saut vers elle :
   ```
   $8A $6F $FF    LDA @$6FFF   ; le Pico bascule sur le jeu
   $B5            CLR A        ; A = 0, comme la ROM
   $8C $7F $FD    BR  @$7FFD   ; exécute l'instruction de démarrage du jeu
   ```
5. Le jeu démarre exactement comme si la ROM interne l'avait lancé. Pourquoi pas `TRAP 0` : [§10.5](#105-démarrage-des-jeux--pourquoi-pas-trap-0).

### Sauvegarder l'Exelmémoire dans le Pico (v5.1)

Le chemin inverse : le contenu de l'Exelmémoire branchée sur la console part dans la flash du Pico, et EXLPICO le relit ensuite par USB, comme le relevé de la sonde.

1. **Dans le menu**, si une Exelmémoire répond, la ligne d'aide devient « ENTREE/FEU:LANCER M:SAUVER EXELMEMOIRE ». Le menu le vérifie au démarrage en écrivant `$55` puis `$AA` en `$BFFF`, relus chacun, puis il y remet l'octet d'origine.
2. **M**, puis ENTRÉE (ou feu) : « SAUVEGARDE DE L'EXELMEMOIRE... », moins d'une seconde, puis « SAUVEE DANS LE PICO, EMPLACEMENT n ». Le Pico garde les **8 dernières** sauvegardes, numérotées ; la 9e remplace la plus ancienne.
3. **Pico sorti de la machine**, branché seul sur le PC (sans BOOT), avec son firmware **cartouche** habituel ou la sonde : attendre 5 secondes, puis **Lire les sauvegardes (USB)** dans EXLPICO (carte « Sauvegardes de l'Exelmémoire », Chrome ou Edge), choisir le port du Pico.
4. Pour chaque sauvegarde : **.cram** (pour les émulateurs), **.BKP** (même format que les sauvegardes de l'Exelmémoire, relu par `bkpToCram` et `bkp2cram.py`), ou **→ bibliothèque** : elle s'ajoute à la liste, pour être recopiée plus tard dans une Exelmémoire. **Effacer les sauvegardes du Pico** les supprime toutes.

Comment ça marche :
- **L'envoi** : la console ne peut pas écrire dans la cartouche ; le menu envoie donc chaque octet v en **lisant** l'adresse `$6C00 + v`, et le Pico lit l'octet sur le bus d'adresses. Interruptions coupées, après la lecture de `P49` et `P50` : `$6BF0` (début), les 16 384 octets de `$BFFF` à `$8000` (l'ordre des `.BKP` : le TMS7020 n'a pas d'`INCD`), leur somme sur 16 bits, `$6BF1` (fin). L'Exelmémoire n'est que lue.
- **Le Pico** : le cœur de service range l'octet de poids faible de chaque adresse `$6C00`–`$6CFF` (état MENU seulement) ; le cœur 0 contrôle le nombre d'octets et la somme, écrit l'image dans la flash à `0x190000` (8 emplacements de 20 Ko : un en-tête `EXLM`, puis l'image dans l'ordre `.cram`), la relit, puis répond en `$6BF2` / `$6BF3`. Le menu attend la réponse 4 s au plus : « LE PICO NE REPOND PAS (FIRMWARE ?) » avec un ancien firmware.
- **La boucle de service** n'a pas changé : l'attente et l'accès sont désormais écrits en assembleur, instruction pour instruction comme en v4.1, pour que le code ajouté après l'accès ne puisse plus y déplacer un registre (vérifié en désassemblant les quatre firmwares).
- **Sur un PC** : un firmware cartouche en mode bibliothèque qui ne voit **aucun démarrage de la console en 3 s** fait de son port USB un **port série** (au lieu de l'hôte USB de la souris et des manettes) ; EXLPICO y envoie `m` (les sauvegardes : `EXLMEM 1`, puis pour chacune `SLOT k numéro somme` et 16 Ko en hexadécimal, puis `FIN`) ou `x` (tout effacer). Le firmware sonde répond aux mêmes commandes. Si la console démarre quand même (Pico en retard sur elle à l'allumage), le Pico redémarre aussitôt et ne cherche plus le PC : faire alors un reset de la console.
- **Les fichiers de la bibliothèque** n'y touchent pas : un UF2 de bibliothèque ou de ROM n'écrit pas à `0x190000`, et les sauvegardes ne touchent ni la bibliothèque ni le relevé de la sonde (vérifié).
- **Vérifié** : le menu dans le simulateur TMS7020 avec un modèle du Pico (`EXLOUTILS/simulateur_tms7020/test_menu_save.py`, 23 contrôles : détection, ordre et somme, messages, Pico muet, octet altéré, sans Exelmémoire) ; le vrai firmware dans rp2040js, deux cartes (`EXLOUTILS/pico/run_save.js`) : 9 sauvegardes, transmission fausse refusée, puis le Pico seul sur un PC, son port série énuméré, le texte lu par EXLPICO, `.cram` et `.BKP` justes, effacement ; `test_multi` (la machine d'états). **Essayé sur la machine** (10/11/2026) : BURGER sauvé, relu par EXLPICO, l'image relancée par `CALL EXEC(32768)` depuis le BASIC dans un émulateur ; elle ne diffère de l'image chargée au départ que de 3 octets que le jeu et le CROS avaient modifiés en tournant.
- **Ligne d'état** : sur la machine, la première version cassait la ligne 24 après la sauvegarde (l'octet de contrôle de la ligne 23 recevait le bit « ligne suivante bitmap » : une ligne de points jusqu'au reset). Le menu réécrit maintenant les octets de contrôle des lignes 22-24 après chaque message (`status_ctl`) et range le pointeur du VDP hors de la page ; corrigé sur la machine. La cause exacte n'est pas trouvée (jexel-debug ne la montre pas).

### Ce que la bibliothèque sait lancer

| Jeu | Comportement |
|---|---|
| ROM ordinaire | servie comme en ROM unique ; avec la DB9 et l'émetteur, la manette arrive par infrarouge |
| jeu avec zone d'échange (`EXLJ` / `EXLP`) | reçoit la DB9 et la souris dans sa zone, comme en ROM unique (infrarouge coupé) |
| lecteur **disque** `badflux.rom` (zone `EXLD`) | avec le film choisi dans la carte **Version disque**, rangé après les jeux : le Pico passe au service disque au lancement, et revient au menu au reset. Sans film, le lecteur affiche « pas de film » |
| lecteur **vidéo** `badapple.rom` (zone `EXLV`) | **non** : EXLPICO le signale ; mettre la version disque à la place |
| ROM à banques | non : les banques ne se servent qu'en ROM unique |
| **image Exelmémoire** (`.cram` ou `.BKP`) | recopiée dans l'Exelmémoire branchée sur la console, puis démarrée si elle a le démarrage automatique ([ci-dessous](#lexelmémoire-depuis-la-bibliothèque-v4)) |
| **cassette** `.k7` (programme BASIC) | lance le BASIC (une autre entrée : la ROM EXELBASIC ou EXEL BASIC PLUS) avec la cassette « dans le lecteur » : `LOAD "1"` charge aussitôt ([ci-dessous](#cassettes-k7-basic)) |

EXLPICO indique dans la colonne « état » les jeux qui portent une zone, et la place prise dans la flash (film compris).

### Cassettes K7 (BASIC)

Les programmes BASIC sur cassette (fichiers `.k7` des émulateurs, comme DCEXEL) se lancent depuis le menu ; le Pico joue la cassette.

1. Dans EXLPICO, ajouter à la bibliothèque **la ROM du BASIC** (EXELBASIC et/ou EXEL BASIC PLUS : EXLPICO les reconnaît, « sert aussi aux cassettes ») et les **cassettes `.k7`**. Chaque cassette indique ses programmes (par exemple « ANA1, ANA2 ») et le BASIC qu'elle utilisera ; le choix « BASIC : auto » prend BASIC PLUS s'il est là, sinon EXELBASIC.
2. Dans le menu, choisir la cassette (les cassettes sont en **jaune** dans la liste, depuis le menu v5.2) : le BASIC démarre, la cassette est rembobinée.
3. `LOAD "1"` charge le premier programme en une fraction de seconde (l'EXELBASIC demande d'abord **ESC**, « ESC -> Lect. », comme avec un magnétophone ; BASIC PLUS lit tout de suite), puis `RUN`. `LOAD "1.ANA2"` cherche ce programme ; les programmes qui chargent leur suite eux-mêmes, ou qui demandent de taper `LOAD "1"` pour la suite, marchent comme sur une vraie bande. Un nom absent donne l'erreur de fin de bande après un tour complet.
4. Le BASIC lancé seul depuis le menu reste le BASIC d'origine (le magnétophone).

Comment ça marche :
- La lecture cassette est le **TRAP 14** de la ROM interne du TMS7020 (impossible à corriger). Le BASIC l'appelle à deux endroits pour `LOAD` (bloc RAM, puis bloc VRAM), par `dint / trap 14 / eint` : EXELBASIC en `$7C62` et `$7C9B`, BASIC PLUS en `$7B0E` et `$7B47`. Le Pico y met `call` vers une routine de 129 octets (`loader/k7/k7load.asm`), qui fait la même chose que le TRAP 14 (comparaison des noms, `TEMP10`, `TEPNAM`, somme) en lisant les octets dans le Pico. **Retour au BASIC** (corrigé le 11/11/2026) : le TMS7000 lit l'octet qui suit une instruction d'un octet ; pendant le `eint` final de la routine, il lit donc déjà l'adresse du `rets`. Le Pico revenait au BASIC dès cet accès, et le vrai `rets` était lu dans le BASIC (son code de `SAVE`), exécuté à la place : registres non restaurés, écritures parasites au début du programme, d'où `Err 18` après `LOAD` avec BASIC PLUS, un écran noir avec EXELBASIC, ou un chargement réussi par chance. Le Pico ne revient maintenant au BASIC qu'à l'accès à l'instruction qui suit le `call`, après avoir vu le `rets` (relevé fait sur l'EXELTEL ; l'émulateur ne fait pas ces lectures).
- Le BASIC n'a pas 129 octets libres : la routine est servie dans une **image de recouvrement**. Le Pico sert le BASIC ; quand la console lit le dernier octet d'un de ces `call`, il sert une copie du BASIC qui porte la routine en `$7CDD` (EXELBASIC) ou `$7B8B` (BASIC PLUS) ; quand elle lit le `rets` final, le BASIC revient. Rien n'est retiré au BASIC : `SAVE` écrit toujours sur une vraie cassette.
- La bande : `K7_NEXT` (bloc suivant), `K7_STAT` (`$FF` : le Pico cherche, 0 : prêt, `$20` : fin de bande), `K7_DATA` (l'octet suivant à chaque lecture). Le cœur 0 recopie le bloc de la flash dans un anneau en RAM, en avance ; la boucle de service est la même qu'ailleurs (même assembleur).
- Entrée K7 dans la flash (son emplacement de 32 Ko) : un en-tête `EXK7` (numéro du BASIC, adresses, routine, début des blocs), puis la bande : ce qui suit l'octet `$70` de chaque bloc. Limites : 32 256 octets de bande, 64 blocs (CITE D'OR : 31 160 octets).
- Firmware sonde : une cassette y lance son BASIC seul (pas de lecture depuis le Pico).
- **Reset sur l'EXELTEL avec BASIC PLUS** : la ROM de l'EXELTEL fait une *reprise à chaud* quand un programme a posé son adresse de reprise (`$C006-$C008` égal à `$C7FD-$C7FF`) : elle y repart sans relire le pied de la cartouche, et le Pico ne voyait pas le reset (on restait dans le BASIC ; l'EXELBASIC et l'EXL100 ne sont pas concernés). Au chargement d'un jeu, le firmware cherche le code qui pose cette adresse W (`movd %W,B / sta @$C007 / sta @$C7FE` ; BASIC PLUS : `$031D`, qui ne sert qu'à cette reprise), y met `br @$7FFD`, et met `$7F $FD` après W dans le menu (zone libre sous `$1000`). Après un reset, la console lit W, le Pico passe au menu (seulement après une pause de plus de 2 ms, retour au menu armé), et le `BR` du pied du menu démarre le menu. Vérifié dans rp2040js et `test_multi` ; **pas encore essayé sur la machine**.
- **Vérifié** : la routine avec les vrais EXELBASIC et BASIC PLUS dans jexel-debug, le Pico étant joué par des points d'arrêt (`EXLOUTILS/k7/e2e_k7.py`) : PINBALL, ANACONDA (ANA1, puis `LOAD "1.ANA2"` qui saute ANA1), CITE D'OR chargés et lancés, nom absent → `Err 0,255`. Le vrai firmware dans rp2040js, deux cartes, deux BASIC (`EXLOUTILS/pico/run_k7.js`, 21 contrôles) : routine servie au bon moment, blocs justes, recherche, fin de bande, retour au menu, BASIC seul intact. **Pas encore essayé sur la machine.**

#### Déroulement du lancement d'une cassette

Du choix dans le menu jusqu'à la fin du chargement (adresses de BASIC PLUS ; EXELBASIC plus bas) :

```
 CONSOLE (TMS7020/7040)                    PICO
 ──────────────────────                    ────

 A. AVANT : fabrication de la bibliotheque (EXLPICO, sur le PC)
    L'entree "cassette" en flash contient :
      - un en-tete EXK7 : numero du BASIC a utiliser, adresses des 2 appels
        cassette de ce BASIC, routine de lecture (129 octets) et adresses
        de ses points reperes (K7_NEXT, K7_STAT, K7_DATA, K7_EXIT)
      - la bande, bloc par bloc

 B. CHOIX DANS LE MENU
    Le menu tourne (image "menu")
    ENTREE sur PINBALL
    lit l'adresse "choix n" ────────────►  coeur 1 : note le choix
                                           coeur 0 : lit l'en-tete EXK7
                                             1. copie le BASIC en RAM (image 1)
                                             2. y remplace les 2 "dint/trap 14/
                                                eint" par "call routine"
                                             3. fait une 2e copie (image 2) avec
                                                la routine a la place du code
                                                SAVE du BASIC ($7B8B...)
                                             4. rembobine : bloc 0, K7_STAT=$FF
    attend "pret" ◄──────────────────────  repond "pret"
    saute en RAM, puis relance
    la cartouche ($7FFC) ───────────────►  coeur 1 : sert l'image 1
    BASIC+ demarre                           (BASIC modifie)

 C. LOAD "1"   (tape par l'utilisateur)
    BASIC+ : affiche "Read", sauve ses
    registres, prepare le nom cherche
    call $7B8B (a la place du trap 14)
      lit le 3e octet du call ──────────►  bascule sur l'image 2 (routine)

 D. LA ROUTINE (image 2), pour chaque bloc de la bande
    dint
    lit K7_NEXT ────────────────────────►  "bloc suivant" : K7_STAT=$FF
                                           coeur 0 : remplit le tampon avec le
                                             bloc (4096 octets, 3800 d'avance),
                                             pose le 1er octet dans K7_DATA,
                                             puis K7_STAT=0 (pret)
                                             ou $20 (fin de bande)
    lit K7_STAT jusqu'a <> $FF
      $20 : "pas de bande" -> sortie
    lit K7_DATA x4 : le nom ────────────►  a chaque lecture : octet suivant
      compare au nom cherche                 dans K7_DATA
    lit K7_DATA x5 : en-tete du bloc
      (debut, fin, type RAM/VRAM)
    mauvais nom -> sortie (le BASIC
      rappellera : bloc suivant)
    lit K7_DATA xN : les donnees
      bloc RAM  : ecrit en RAM
      bloc VRAM : trap 8 puis ecrit
                  en memoire video (P46)
      additionne chaque octet
    lit K7_DATA : la somme, compare
    resultat dans R32 (TEMP10) et B
    eint  (lit deja l'adresse du rets) ─►  K7_EXIT vu : drapeau "fin"
    rets  (K7_EXIT) ────────────────────►  K7_EXIT vu : drapeau "fin"

 E. RETOUR AU BASIC
    lit l'octet qui suit le call ───────►  drapeau leve : rebascule sur
                                             l'image 1 (BASIC)
    BASIC+ teste R32 : nom, somme, bande

 F. UN PROGRAMME = 2 BLOCS, donc C-D-E deux fois
    1er appel ($7B0E) : bloc RAM  (18 octets : pointeurs du programme)
      BASIC+ affiche le nom trouve ("PINB"), calcule ou placer le programme
    2e appel  ($7B47) : bloc VRAM (le programme, 18 250 octets pour PINBALL)
      BASIC+ restaure ses registres, remet le bandeau, rend la main
      (ou lance le programme s'il est a demarrage automatique)

 G. ENSUITE
    Le Pico continue de servir le BASIC ; un nouveau LOAD lit le bloc suivant
    de la bande (ANACONDA : ANA1 puis ANA2).
    Reset de la console ────────────────►  retour a l'image "menu"
```

Avec **EXELBASIC**, même déroulement, à trois différences près : les deux appels sont en `$7C62` et `$7C9B` et la routine en `$7CDD` ; EXELBASIC affiche « ESC -> Lect. » et attend ESC avant le premier appel ; il ne lance pas tout seul les programmes à démarrage automatique.

Deux principes du montage :
- le Pico ne fait que servir des octets et regarder les adresses lues ; la console ne lui écrit rien : toutes les « commandes » sont des lectures à des adresses convenues ;
- les deux bascules d'image se font sur des octets identiques dans les deux images (le 3e octet du `call` à l'aller, l'octet qui suit le `call` au retour). Le TMS7000 lit en avance l'octet qui suit une instruction d'un octet : une bascule sur l'adresse du `rets` lui-même (avant le 11/11/2026) faisait lire le vrai `rets` dans la mauvaise image.

### L'Exelmémoire depuis la bibliothèque (v4)

L'Exelmémoire est la mémoire sauvegardée par pile qui se branche à l'arrière de la console : 16 Ko vus par le processeur en `$8000`–`$BFFF`. Le menu peut y **recopier une image** rangée dans la bibliothèque :

- **Les fichiers acceptés** par EXLPICO (« Ajouter des jeux ou des Exelmémoires ») :
  - `.cram` : l'image de 16 384 octets des émulateurs (DCEXEL, Jexel), `$8000` → `$BFFF` dans l'ordre ;
  - `.BKP` : la sauvegarde de l'Exelmémoire elle-même. Format reconnu sur les fichiers fournis : **512 octets d'en-tête**, puis **64 blocs** de `01 00` + 256 octets, la mémoire étant lue **de `$BFFF` vers `$8000`** ; le reste du fichier est du bourrage. `outils/bkp2cram.py` fait la même conversion sur PC (pour un émulateur). Vérifié : CB5QUAD converti démarre bien sur le « CMOS RAM OPERATING SYSTEM V1.5 » dans jexel-debug.
- **Dans le menu**, ces entrées sont en **cyan**. ENTRÉE (ou feu) sur l'une d'elles :
  1. confirmation : « EXELMEMOIRE : SON CONTENU SERA EFFACE », ENTRÉE ou feu pour charger, une autre touche pour revenir ;
  2. lecture de `$6E00 + n`, attente de `$A5` en `$6DFF` (2 s au plus ; sinon « LE PICO NE REPOND PAS (FIRMWARE ?) » : firmware antérieur au 09/11/2026) ;
  3. **copie** de la fenêtre `$2800`–`$67FF` vers `$8000`–`$BFFF`, interruptions coupées, après une lecture de `P49` et `P50` (ce qui valide le connecteur ; c'est ce que fait Exelnoid pour lire ses niveaux dans l'Exelmémoire, vérifié sur la machine), puis **relecture** des 16 Ko (« RELECTURE FAUSSE : EXELMEMOIRE ? » si un octet diffère : Exelmémoire absente) ;
  4. ensuite, deux façons de démarrer :
     - **ENTRÉE ou feu** (image à **démarrage automatique** : `$AA` en `$8000`, suivi de `8C hh ll`) : démarrage **direct**. Lecture de `$6DFE` (le Pico relaie alors la manette par infrarouge, comme pour un jeu sans zone d'échange, jusqu'au prochain reset), même ménage de la machine qu'avant un jeu, `$C006` = `BR @$8001` comme le laisse la ROM, puis `BR @$8001`. Marche pour Pindo et CB5QUAD (CROS).
     - **S** (et ENTRÉE ou feu pour une image **sans** démarrage automatique, comme BURGER) : **sans cartouche**. Lecture de `$6DFD` : le Pico met `$FF` dans le pied du menu, la cartouche « disparaît ». Le menu affiche « APPUYEZ SUR RESET : DEMARRAGE SANS CARTOUCHE » (une autre touche annule, `$6DFC`). Au reset, la console démarre **comme sans cartouche** : l'image à démarrage automatique par la ROM elle-même, ou le menu de la console (Exelquad, d'où on lance BURGER), avec la manette relayée par infrarouge. Environ 1 s après ce démarrage, le Pico remet le pied du menu : **le reset suivant ramène au menu**.
- **Un reset ramène au menu** : au démarrage, la ROM interne donne la priorité à la cartouche sur l'Exelmémoire (vérifié dans jexel-debug).
- **Quizzy** : son démarrage automatique saute dans le CROS en `$E4B0`, un sous-programme qui finit par `RETS` ; en démarrage direct, l'EXELTEL affiche son écran rouge (essai de David, 09/11). Essayer **S** (sans cartouche) ; si l'écran rouge reste, Quizzy ne démarre sans doute pas non plus sur l'EXELTEL sans la cartouche (fichier `quizzy_exl100`).
- **Rien n'est fait par le Pico du côté de l'Exelmémoire** : il ne fait que remplir une fenêtre de l'image du menu ; c'est le menu, sur la console, qui écrit l'Exelmémoire. Il faut le **loader v4** et un **firmware du 09/11/2026 ou plus récent** ; EXLPICO refuse une bibliothèque avec des images Exelmémoire et un ancien loader.
- **À savoir** :
  - avec une Exelmémoire de 64 Ko (paginée), l'image va dans la page active ;
  - **sur la machine** (EXELTEL, 09/11/2026, v4) : copies justes ; Pindo et CB5QUAD (CROS) démarrent ; Quizzy : écran rouge ; pas de relais infrarouge (corrigé en v4.1) ; BURGER ne se lançait qu'en redémarrant sans la cartouche (d'où « sans cartouche », v4.1, pas encore essayé sur la machine) ;
  - dans jexel-debug (cœur de Jester, Exelmémoire émulée), 20 contrôles : Pindo démarré direct puis sans cartouche (par la ROM), CB5QUAD et son CROS, BURGER chargé puis la console démarrée sans cartouche, annulation, firmware absent, Exelmémoire absente (`EXLOUTILS/jexel/e2e_exelmemoire.py`). Le vrai firmware dans rp2040js, deux cartes : la fenêtre, « sans cartouche » et l'annulation (`run_libcram.js`, 27 contrôles), et le relais infrarouge pendant une image (`run_cramir.js`).

---

## 5. Les ROM à banques (plus de 32 Ko)

La console ne voit que 32 Ko de cartouche. Une ROM à banques garde son programme dans la moitié basse, et fait défiler des pages de 16 Ko dans la moitié haute. Le Pico reconnaît la demande de changement **en surveillant les adresses lues** : ni registre ni écriture, une simple lecture suffit.

### Organisation vue par la console

| Adresses | Contenu |
|---|---|
| `$0000–$3FFF` | **zone fixe** : le programme, toujours visible |
| `$3FF0–$3FFF` | **signal** : une lecture en `$3FF0 + n` sélectionne la banque n. **Ne rien assembler dans ces 16 octets** |
| `$4000–$7FFF` | **fenêtre** : la banque courante, 16 Ko |
| `$7FFC–$7FFF` | le pied : celui de la ROM principale, recopié par EXLPICO dans **chaque** banque |

C'est la même convention que l'image EPROM produite par EXLBANK.

- La **banque 0** est la moitié haute de la ROM principale. Sans banques, celle-ci démarre telle quelle.
- Les **banques 1, 2…** sont des fichiers à part : 16 Ko bruts, ou une ROM de 32 Ko dont seule la moitié haute est prise.
- **Une banque assemblée avec TASM + obj2exl doit commencer par un repère en `$1000`** (`.org $1000` / `.byte $FF`, puis `.org $4000` et les données). Le binaire de TASM commence à la plus petite adresse assemblée et obj2exl le pose à l'adresse de `-r` : une banque avec un `.org $4000` seul serait posée en `$1000`, et sa fenêtre serait vide. EXLPICO refuse une telle banque, avec ce diagnostic. Voir `exemples/bank1.asm`.
- Le changement a lieu **juste après** la lecture du signal : la lecture suivante de la fenêtre voit déjà la nouvelle banque.

```
        mov     %2,B
        lda     @$3FF0(B)       ; lecture de $3FF2 : le Pico passe sur la banque 2
        lda     @$4000          ; premier octet de la banque 2
```

Trois règles pour le programme :

- **Choisir sa banque au démarrage.** Le Pico ne voit pas le reset de la console, seulement les lectures : il ne revient pas de lui-même à la banque 0.
- **Ne pas changer de banque depuis du code placé dans la fenêtre** : l'instruction suivante serait lue dans l'autre banque.
- **Laisser `$3FF0–$3FFF` libres** : une lecture de ces octets, même par le processeur qui exécute du code, change de banque.

### Capacité

Chaque banque est gardée en RAM sous forme d'image complète, pour servir à la même vitesse qu'une ROM unique ([§10.8](#108-le-service-avec-banques)) :

| Firmware | Carte linéaire | Carte support EPROM |
|---|---|---|
| cartouche (EXLCART) | **6 banques** : 16 Ko fixes + 6 × 16 Ko = 112 Ko | **3 banques** : 64 Ko |
| sonde (EXLSONDE) | 3 banques | 2 banques |

Une banque au-delà de la capacité est ignorée. EXLPICO prévient quand la liste dépasse la capacité de la carte choisie.

### Exemple : BANKTEST

`exemples/banktest.asm` affiche le texte lu en `$4000`, et **ESPACE** passe à la banque suivante (0, 1, 0, 1…) : « BONJOUR PLAGE 1 » (banque 0, fin de `banktest.asm`), « AU REVOIR PLAGE 2 » (banque 1, `bank1.asm`). La ligne **OCTETS** montre les 8 premiers octets de la fenêtre : `FF FF FF …` veut dire une banque vide.

```
tasm -tEXL -a -b banktest.asm
obj2exl.exe banktest.obj banktest.rom -t:ROM -r:0x1000 -p
tasm -tEXL -a -b bank1.asm
obj2exl.exe bank1.obj bank1.rom -t:ROM -r:0x1000 -p
```

Puis dans EXLPICO : **ROM** = `banktest.rom`, **Banques** = `bank1.rom`, **UF2 complet**.

### 5.1 Deux images de 32 Ko

Certaines cartouches d'origine font 64 Ko : deux images complètes de 32 Ko, qu'un circuit logique (74LS…) fait basculer. C'est le cas du logiciel **EXELTEL pour EXL100** . Le programme recopie en RAM deux petites routines, `LDA @>7FF0 / RETS` et `LDA @>7FF8 / RETS` :

| Lecture | Effet |
|---|---|
| `$7FF0` | l'image 1 (2ᵉ moitié du fichier) est servie |
| `$7FF8` | l'image 0 (1ʳᵉ moitié) est servie |

- **Adresses exactes.** Le pied n'agit pas : le programme saute en `$7FFD`, où chaque image a son propre `BR` (`8C 02 F3` dans l'image 0, `8C 59 C7` dans l'image 1). Une bascule sur tout `$7FF8–$7FFF` le casserait.
- Au démarrage du Pico, l'image 0. Comme pour les banques de 16 Ko, le Pico ne voit pas le reset de la console : un reset pendant que l'image 1 est servie démarre par le pied de l'image 1 (`$59C7`), qui sait redémarrer le logiciel : le programme change d'image environ 3 000 fois par seconde (un tiers du temps dans l'image 1), et des resets répétés sur EXL100 ramènent chaque fois au menu.
- Chaque image est servie **telle quelle**, pied compris : rien n'est recopié de l'une à l'autre.
- Flash : la 1ʳᵉ moitié à 1 Mo (ROM principale), l'en-tête `EXLK` **version 2** (2 images) à `0x10F000`, la 2ᵉ moitié (32 Ko) à `0x110000`.
- Exclusif des banques de 16 Ko, et **pas dans une bibliothèque** (EXLPICO refuse un fichier de 64 Ko dans la liste des jeux). Une image de la réserve suffit : toutes les cartes et les sondes le servent.

---

## 6. Manettes et souris

### 6.1 Deux façons d'arriver jusqu'au jeu

La manette DB9 branchée sur le Pico peut parvenir au jeu de deux façons :

| | Par infrarouge | Par la zone d'échange |
|---|---|---|
| Pour quels jeux | **tous les jeux qui acceptent la manette Exelvision 1**, sans modification | les jeux qui réservent une zone `EXLJ` / `EXLP` : patchés (Wizord, Tennis…), Exelnoid, le menu, les nouveaux jeux |
| Matériel | la DB9 + l'émetteur infrarouge sur GP25 ([§2.4](#24-lémetteur-infrarouge-facultatif)) | la DB9 seule |
| Ce que reçoit le jeu | les codes de la manette d'origine, par le TMS7041 et le moniteur | un octet direct : haut, bas, gauche, droite, feu |
| Souris USB | non | oui (zone `EXLP`) |
| Latence | celle de l'infrarouge (trames de 11 ms, répétées toutes les 100 ms) | 1 ms, sans infrarouge |

Le firmware choisit seul : **la manette ne passe pas par l'infrarouge quand l'image servie a sa zone d'échange**, pour qu'un jeu ne la reçoive pas deux fois (de même dans le menu de la bibliothèque). Une manette USB suit le même chemin que la DB9 ([§6.4](#64-une-manette-usb)). Le clavier USB, lui, passe toujours par l'infrarouge ([§6.3](#63-un-clavier-usb-par-infrarouge)). En vidéo et en version disque, l'émetteur ne sert pas.

### 6.2 La DB9 par infrarouge : les jeux non modifiés

Les jeux du commerce lisent leurs manettes par l'infrarouge : la manette Exelvision émet, comme le clavier, avec un circuit **MC14497**, et le TMS7041 de la console décode. EXLCART fait la même chose. Le jeu ne voit pas la différence.

**Ce qui est envoyé** : le comportement de la manette 1 d'origine.

| DB9 | code infrarouge | ce que le jeu reçoit (`VALUE0`, R3) | touche du clavier au même code |
|---|---|---|---|
| haut, droite, bas, gauche | 1, 2, 3, 4 | `$80`, `$81`, `$82`, `$83` | les flèches |
| haut-droite, bas-droite, bas-gauche, haut-gauche | 33, 34, 35, 36 | `$4F`, `$48`, `$3C`, `$54` | O, H, `<`, T |
| feu seul, ou avec gauche / droite | 6 | `$20` | espace |
| feu + haut, feu + bas | 7 | `$5F` | `_` |
| feu + haut-gauche, feu + bas-droite | 38 | `$4E` | N |
| feu + haut-droite, feu + bas-gauche | 39 | `$47` | G |
| relâché | 62 (fin) | `$04` | |

**Pourquoi ces valeurs** : la manette n'a pas de codes à elle. Elle envoie des **positions de la matrice du clavier** (code = ligne × 16 + colonne), et le TMS7041 les traduit comme des touches : d'où des caractères ASCII (`$4F` = « O », `$5F` = « _ »…). Le codage est régulier :
- la **colonne** donne la direction (1 haut, 2 droite, 3 bas, 4 gauche) ou le tir (6 : feu seul ou avec gauche / droite ; 7 : feu avec haut / bas) ;
- **+ 32** (ligne M2 au lieu de M0) veut dire « diagonale » : chaque diagonale prend la direction qui la précède dans le sens des aiguilles d'une montre (haut-droite = haut + 32 = 33…), et de même pour le tir (6 + 32 = 38, 7 + 32 = 39).

Les touches O, H, `<`, T, N et G se trouvent simplement sous ces positions de la matrice : sur le clavier filaire de l'EXELTEL, les taper donne exactement les valeurs de la manette (vérifié sur la machine).

Ces codes ont été **relevés sur la machine** avec le programme `jstest` ([§6.5](#65-le-programme-de-test-jstest)), identiques avec la vraie manette et avec la DB9. Ce sont aussi ceux des tables de Tennis, Virus, Guppy et Capitaine Menkar. La documentation technique Exelvision donne `$4F` pour haut-gauche et `$45` pour haut-droite : ce sont, gauche et droite inversées, les valeurs du **clavier mou** quand on tient deux flèches (haut + droite `$4F`, haut + gauche `$45`, d'après les relevés de Jester sur un EXL100). Le clavier y arrive par le croisement de deux contacts de sa matrice, et il diffère de la manette pour haut-gauche. Les jeux faits pour la manette attendent donc `$54` : Wizord ne réagit pas à haut + gauche au clavier mou, mais bien à la manette et à la DB9.

**Les trames** :
- porteuse 455 kHz / 16 = **28,4375 kHz** ; 20 cases de 16 périodes (562,6 µs) : une case de porteuse, 5 de silence, le bit de départ « 1 », puis les 6 bits du code en biphase, poids faible d'abord (1 = porteuse puis silence) ; une trame dure 11,25 ms. Durées du MC14497, du modèle de MAME et du montage PS/2 → infrarouge d'aotta ;
- **position tenue** : la trame est répétée 90 ms après la précédente ; **relâché** : le code 62 part aussitôt (au moins 8 ms après la fin de la trame précédente) ; **changement sans relâcher** (haut → haut-droite) : le nouveau code part 8 ms après la fin de la trame en cours. Ces 8 ms de silence entre deux trames restent loin des 2,8 ms de silence du début de trame ;
- haut et bas ensemble (ou gauche et droite) s'annulent ; un contact n'est pris en compte qu'après 3 ms stables : la première trame part environ 2 ms après l'appui ;
- la porteuse et les trames sont fabriquées par une **machine PIO** (`exlir.pio`) : le processeur ne pousse qu'un mot de 32 bits par trame. Toutes les périodes font exactement 36 cycles PIO, y compris d'une case à l'autre.

**Quel cœur s'en occupe** :
- **ROM unique, ROM à banques** : le cœur 0 sert le bus exactement comme avant ; le cœur 1, inutilisé dans ces modes, lit la DB9 et pilote la diode ;
- **bibliothèque** : le cœur 0, entre deux chargements, pendant un jeu sans zone d'échange.

**Vérifié** : dans l'émulateur RP2040, avec le vrai firmware et la machine PIO à sa vraie vitesse, sur les deux cartes (`EXLOUTILS/pico/run_ir.js`) : porteuse à 28 437,6 Hz sans irrégularité, les 17 positions de la manette, la fin de transmission, la répétition, le changement sans relâcher, l'anti-rebond ; et en bibliothèque, l'infrarouge seulement pendant un jeu sans zone (`run_libio.js`). **Sur la machine** (carte linéaire) : Wizord d'origine se joue à la DB9, et `jstest` reçoit exactement les mêmes codes de la DB9 et de la vraie manette 1.

**Limites** : seule la **manette 1** est émise (la manette 2 utilise une autre porteuse, à 20 kHz, et un autre récepteur du TMS7041) ; le pavé numérique de la manette d'origine n'existe pas sur une DB9 (une manette USB en donne une partie par ses boutons, [§6.4](#64-une-manette-usb)) ; si l'on tape au clavier en même temps, les deux émissions peuvent se brouiller, comme avec deux télécommandes.

### 6.3 Un clavier USB par infrarouge

Un clavier USB branché sur le Pico est envoyé à la console par l'émetteur infrarouge, **comme le clavier Exelvision** : la console ne voit pas la différence (`exlkbd.h`).

**Comment le clavier EXL100 fonctionne** : il n'envoie qu'**un code à la fois** (le TMS7020 ne reçoit qu'une valeur courante par canal). Shift, Ctl et Fct sont des touches à part, qui agissent sur la touche **suivante** (le TMS7041 les retient) ; Caps bascule les minuscules. Une touche avec modificateur part donc en séquence : le modificateur, la fin de transmission, puis la touche, répétée tant qu'elle est tenue, et la fin au relâchement. Les touches tapées vite sont mises en file : aucune ne se perd, et dans l'ordre.

**Correspondance** (clavier PC **AZERTY** français) :

| Sur le PC | Sur l'EXL100 |
|---|---|
| lettre | la même lettre : **sans Maj en majuscule** (le défaut de l'EXL100), avec Maj en minuscule (son Shift) ; Verr. Maj = Caps |
| chiffre ou signe | le **caractère tapé sur le PC** (Maj et AltGr compris), avec ou sans le Shift de l'EXL100 selon le cas : `1` → 1, `&` → Shift 1, `é` → Shift 2, `@` (AltGr à) → Shift %, `,` → Shift ?, `:` → Shift / … |
| Entrée, Retour arrière ou Suppr, Tab, Échap, espace, flèches, Début | Entrée, Del, Tab, Esc, espace, flèches, Home |
| Ctrl + touche, Alt + touche | Ctl, Fct devant la touche |
| F1 à F10 | Fct puis 1 à 0 |
| pavé numérique | chiffres et signes |

Les caractères que le clavier EXL100 n'a pas (accolades, barre verticale, accent grave, ², °, £, ù…) sont ignorés. Les seconds caractères des touches de l'EXL100 viennent de la ROM du TMS7041 : 1 &, 2 é, 3 ", 4 ', 5 (, 6 $, 7 è, 8 !, 9 ç, 0 à, `#` `)`, `_` `-`, `~` `^`, `<` `[`, `>` `]`, `%` `@`, `?` `,`, `.` `;`, `/` `:`, `+` `=`, `*` `\`.

**Plusieurs touches tenues.** Les claviers d'origine ne se comportent pas pareil (relevés sur la machine) :

| Situation | Clavier filaire de l'EXELTEL | Clavier mou infrarouge | Clavier USB d'EXLCART |
|---|---|---|---|
| X tenu, puis Y | Y | Y | Y |
| Y relâché, X encore tenu | **Y continue** tant que X est tenu | **X revient** | **X revient** |
| gauche ou droite tenu, puis espace | espace (`$20`) | espace (`$20`) | espace (`$20`), comme feu + gauche/droite |
| haut ou bas tenu, puis espace | espace | **`$5F`**, comme feu + haut/bas à la manette | **`$5F`** |
| deux flèches | impossible (une touche à la fois) | haut + droite `$4F`, bas + droite `$48`, bas + gauche `$3C`, mais **haut + gauche `$45`** | la diagonale de la **manette** : haut + gauche **`$54`** |
| haut + bas, gauche + droite | — | `$82`, `$20` (croisements de contacts) | la dernière enfoncée l'emporte |
| deux touches ordinaires | — | des valeurs imprévisibles (A + Z → `$04`, Y + U → `$8B`…) | la dernière enfoncée |

Choix d'EXLCART, et pourquoi :
- **La dernière touche enfoncée compte ; si on la relâche, la plus récente encore tenue reprend**, comme sur le clavier mou. Le clavier filaire, qui garde la touche relâchée, est déroutant pour jouer.
- **Les flèches et l'espace forment une manette**, comme le clavier mou l'a été conçu pour (page 11 de la documentation technique) : deux flèches donnent la diagonale, une flèche et l'espace le tir combiné, avec les **codes de la vraie manette**. Haut + gauche donne donc `$54` et non le `$45` accidentel du clavier mou : les jeux faits pour la manette (Wizord, dont la table attend `$54`) se jouent aussi au clavier USB, diagonales comprises. Deux flèches opposées : la dernière enfoncée l'emporte, plutôt que de s'annuler.
- **Avec Ctrl ou Alt**, une flèche ou l'espace redevient une touche ordinaire (Ctl ou Fct devant la touche).
- **Les combinaisons du clavier mou ne sont pas reproduites** : elles viennent des croisements de contacts de la matrice du MC14497 de la télécommande, pas du TMS7041. Le 7041 ne reçoit jamais qu'un code par trame, et EXLCART lui envoie toujours le code d'une touche seule (ou de la manette).
- **Un préfixe ne s'applique qu'à une nouvelle touche** : quand une séquence avec Shift, Ctl ou Fct suit une touche encore tenue (Maj a tenu, puis Maj b), la fin de transmission part d'abord, puis le préfixe, la fin et la touche. Les relevés de Jester confirment la règle : « Shift relâché, puis A » donne `a`, et chaque préfixe ne sert qu'à une touche ; Shift et A tenus *ensemble* ne donnent rien (`$04`).

Ces relevés du clavier mou sont ceux de Jester (EXL100, octobre 2026) ; ceux du clavier filaire et de la manette ont été faits sur EXELTEL et EXL100 avec `jstest`.

- **Le clavier passe avant la manette** tant qu'une touche est tenue ou qu'une séquence est en cours ; la manette reprend ensuite.
- **Il n'est jamais coupé par une zone d'échange** (elle ne porte pas de clavier) : il marche aussi dans le menu de la bibliothèque et dans les jeux patchés. Seuls la vidéo et la version disque n'ont pas l'émetteur.
- **Débit** : une trame dure 11,25 ms, suivie de 8 ms de silence ; une lettre (touche, puis fin) part en 40 ms environ, une minuscule ou un signe avec Shift en 80 ms.

**Vérifié** : sur PC, 88 contrôles (`test_kbd.c` : chaque correspondance, et les séquences envoyées : touche tapée, tenue, avec Shift ou Ctl, touches tapées en quelques ms, plusieurs touches tenues, retour à la touche encore tenue, flèches et espace en manette, diagonales, flèches opposées, Ctrl + flèche) ; dans l'émulateur RP2040, avec le vrai firmware et la machine PIO, sur les deux cartes (`run_ir.js`) : les trames de A, de a (Shift, fin, A), de &, de LOL tapé en 5 ms, d'Entrée tenue, d'une touche tapée pendant que la manette est tenue, de haut + gauche (36) et de gauche + espace puis gauche à nouveau. **Pas encore essayé sur la machine** : en particulier la séquence « Shift, fin, touche », d'après la ROM du TMS7041. Avec `jstest` : `a` doit afficher `$41` (A), et Maj + `a` la suite `$86` (Shift), `$04`, `$61` (a).

### 6.4 Une manette USB

Une manette USB **HID** branchée sur le Pico s'ajoute à la DB9 (`exlpad.h`) : manettes « SNES USB » et autres manettes premier prix, DualShock 4, DualSense, 8BitDo en mode D-input… Ses directions et son feu vont **là où va la DB9** : par infrarouge pour les jeux non modifiés, dans la zone d'échange pour les jeux qui en ont une, et dans le menu de la bibliothèque. La DB9 et la manette USB marchent ensemble (leurs contacts s'additionnent).

| Sur la manette | Sur l'EXL100 |
|---|---|
| croix, ou stick gauche (au-delà du quart de sa course) | haut, bas, gauche, droite et diagonales |
| boutons 1 à 4 (les boutons de face : A B X Y, croix rond carré triangle) | feu |
| L1, R1, L2, R2 (boutons 5 à 8) | touches 1, 2, 3, 4 |
| Select, Share (bouton 9) | ESPACE |
| Start, Options (bouton 10) | ENTRÉE |
| boutons 11 à 16 (clics des sticks, PS…) | touches 5, 6, 7, 8, 9, 0 |

- **Les touches** partent par l'émetteur infrarouge, comme celles du clavier USB ([§6.3](#63-un-clavier-usb-par-infrarouge)) : elles marchent aussi dans les jeux à zone d'échange et dans le menu. De quoi choisir le nombre de joueurs ou lancer une partie sans clavier.
- **La numérotation des boutons** est celle des manettes SNES USB, des DualShock et de la plupart des manettes HID ; elle varie un peu d'une marque à l'autre. La table est `PAD_KEYS`, dans `exlpad.h`.
- **Comment** : une manette n'a pas de format de rapport fixe, elle le décrit dans son « descripteur de rapport ». Le Pico le lit au branchement et y retient les axes X et Y, la croix (chapeau, ou usages D-pad) et les boutons. Une interface qui n'est pas une manette (les touches multimédia d'un clavier, par exemple) est ignorée. Un axe trouvé loin du centre au branchement (stick tenu, ou octet fixe de certaines manettes SNES USB) est ignoré jusqu'à son passage au centre.
- **Limites** : une seule manette à la fois (la première branchée, HID ou Xbox). La manette Switch Pro n'est pas prise (elle demande un dialogue à elle).

**Les manettes Xbox et XInput** (`exlxinput.h`) : Xbox 360 filaire, Xbox One, Xbox Series (Elite comprises), et les manettes « compatibles XInput » (8BitDo en mode X, PowerA, PDP, Logitech F310 / F710 en position X, et la plupart des manettes PC récentes). Le **récepteur sans fil Xbox 360 pour PC** est pris aussi (la première manette jumelée). **Rien à régler** : le Pico les reconnaît seul, à leur interface USB, sans liste de modèles. Il les met en marche si besoin (les Xbox One et Series restent muettes sans cela), et allume le voyant 1 des manettes 360.

| Sur la manette Xbox | Sur l'EXL100 |
|---|---|
| croix, ou stick gauche | directions et diagonales |
| A, B, X, Y | feu |
| LB, RB, LT, RT | touches 1, 2, 3, 4 |
| Back / View | ESPACE |
| Start / Menu | ENTRÉE |
| clic du stick gauche, du stick droit, bouton Xbox | touches 5, 6, 7 |

- **Par câble USB seulement** : une manette One ou Series sans fil doit être branchée par son câble ; ni Bluetooth, ni adaptateur sans fil Xbox (celui des One, Wi-Fi). Le câble « Play & Charge » des manettes 360 sans fil ne fait que charger : il faut le récepteur sans fil pour PC.
- **Alimentation** : une manette Xbox filaire consomme 100 à 200 mA sur le 5 V de la cartouche (pas de vibration).
- **Comment** : ce ne sont pas des manettes HID. Le Pico lit le descripteur de configuration de chaque appareil branché ; s'il y trouve une interface « XInput » (classe 0xFF, sous-classe 0x5D protocole 0x01 ou 0x81, ou sous-classe 0x47 protocole 0xD0), il ouvre lui-même ses points d'accès et lit ses rapports, au format fixe. Paquets de mise en marche et formats : ceux du pilote `xpad` de Linux, y compris les paquets propres aux manettes PDP, PowerA et Elite 2.

**Vérifié** : sur PC, 47 contrôles (`test_pad.c`) avec les descripteurs d'une manette SNES USB (DragonRise), d'une DualShock 4, d'un appareil composite (touches multimédia et manette à axes 16 bits), d'une croix à 4 directions, et un descripteur sans manette ; plus 600 000 descripteurs et rapports aléatoires, sans débordement. Pour les manettes Xbox, 48 contrôles (`test_xinput.c`) : reconnaissance dans les descripteurs de configuration d'une 360 filaire, d'une One S, du récepteur sans fil 360 et d'une manette HID ; chaque bouton des rapports 360, One et sans fil ; bouton Xbox et son accusé de réception ; paquets de mise en marche selon le modèle ; plus 500 000 descripteurs aléatoires. Dans l'émulateur RP2040, avec le vrai firmware sur les deux cartes (`run_ir.js`) : les trames de la croix, du feu, de Start (ENTRÉE), de L (1), la croix USB à gauche avec la DB9 en haut (haut-gauche), puis les mêmes avec des rapports de Xbox One. La partie USB elle-même (reconnaissance, mise en marche, lecture) ne s'émule pas. **Pas encore essayé sur la machine.**

### 6.5 Le programme de test `jstest`

`exemples/jstest/` (source, ROM, et un UF2 complet par carte) affiche ce que la console reçoit, pour la manette 1 avec le clavier (`R3`, vecteur `BRJOY0`) et pour la manette 2 (`R4`, vecteur `BRJOY1`) :

- le code courant en hexadécimal et sa signification (haut, haut-droite, feu + haut/bas, relâché, autre touche…) ;
- le nombre d'appels du moniteur : un par code reçu, ce qui montre la cadence des répétitions quand on tient une direction ;
- les 8 derniers codes reçus, du plus ancien au plus récent ;
- un caractère qui se déplace dans un cadre : `O` pour la manette 1 (`*` avec le feu), `X` pour la manette 2 (`+`).

On fait les mêmes gestes avec la vraie manette et avec la DB9 : les codes, leur ordre et la vitesse du compteur doivent être les mêmes. Le programme n'a pas de zone d'échange, donc l'infrarouge de la cartouche est actif. Il marche aussi sur une vraie EPROM. La source s'assemble seule, sans fichiers `.equ` : TASM + tasmEXL.TAB, ou `asm7000.py` d'EXLOUTILS. Vérifié dans le simulateur avec le moniteur de l'EXL100 (`test_jstest.py`, 28 contrôles), puis sur la machine.

Avec une vraie manette 2, rien n'a encore été reçu sur la machine essayée (deux manettes essayées) : cause non établie (récepteur du canal 2 de la console, ou manettes du canal 1). Tennis d'origine à deux joueurs permet de trancher.

### 6.6 La zone d'échange : les jeux patchés et les nouveaux jeux

Le jeu réserve dans sa ROM, **où il veut**, une zone de 8 ou 16 octets. Le Pico y écrit l'état de la DB9 et de la souris ; sur une vraie EPROM, la zone lit simplement « rien d'appuyé, pas de souris ».

| Octet | Contenu |
|---|---|
| +0 | manette : bit 0 haut, bit 1 bas, bit 2 gauche, bit 3 droite, bit 4 feu (1 = appuyé) |
| +1 | seconde manette (pas encore câblée : `$00`) |
| +2 | `$A5` quand EXLCART gère la zone |
| +3 | `$00` |
| +4…+7 | signature : **`EXLJ`** (manette seule, zone de 8 octets) ou **`EXLP`** (manette + souris, 16 octets) |
| +8 | souris : compteur X (8 bits, tourne en boucle ; vers la droite = +) |
| +9 | souris : compteur Y (vers le bas = +) |
| +10 | souris : boutons, bit 0 gauche, bit 1 droit, bit 2 milieu (1 = appuyé) |
| +11 | souris : `$A5` quand une souris est branchée |
| +12…+15 | réservés (`$00`) |

```
exl_io:
exl_joy0:  .byte $00            ; manette
exl_joy1:  .byte $00
exl_jpres: .byte $00
           .byte $00
           .byte "EXLP"         ; ou "EXLJ" (alors s'arrêter là : 8 octets)
exl_mx:    .byte $00            ; souris
exl_my:    .byte $00
exl_mbtn:  .byte $00
exl_mpres: .byte $00
           .byte $00,$00,$00,$00
```

- **Au démarrage, le firmware cherche le motif** `00 00 00 00 "EXLJ"`, ou `00 00 00 00 "EXLP"` suivi de 8 zéros. Avec des banques, seulement dans la zone fixe. **Sans motif, la ROM est servie telle quelle.**
- **Avec le motif**, le service du bus passe sur le cœur 1 (même boucle, même vitesse). Le cœur 0 lit la manette toutes les millisecondes (anti-rebond de 3 ms) et fait tourner la pile USB. Chaque octet est réécrit d'un coup dans chaque image servie, banques comprises : la console lit toujours une valeur entière.
- **La souris donne des compteurs, pas une position.** Le jeu calcule le déplacement par différence avec sa lecture précédente : sur 8 bits, `nouveau − ancien` donne directement un déplacement signé de −128 à +127. Rien à remettre à zéro, rien ne se perd. Il suffit de lire au moins une fois par image. **Sensibilité** : réglage d'EXLPICO (carte *Réglage du service*), de 1/4 à 1/64, 1/16 par défaut.
- **En bibliothèque**, le menu a sa zone (le loader actuel en porte une), et le firmware cherche celle de chaque jeu chargé.

Pour un nouveau jeu, le plus simple est de combiner les sources par un OU : clavier et manettes infrarouges (`exemples/joyexl.asm`, qui rend le même format de bits) et DB9.

```
        lda     @joy_state0     ; infrarouge et clavier (joyexl.asm)
        mov     A,B
        lda     @exl_joy0       ; manette DB9 ($00 sans EXLCART)
        or      B,A             ; A = les deux
```

Pour la souris :

```
        lda     @exl_mx         ; compteur X
        mov     A,B
        lda     @mx_prec        ; lecture précédente (en RAM)
        push    B
        sub     R0,R1           ; B = nouveau - précédent = déplacement signé
        pop     A
        sta     @mx_prec
```

`joyexl.asm` branche les vecteurs `BRJOY0` / `BRJOY1` du moniteur et traduit chaque code reçu en octet normalisé (`joy_state0`, `joy_state1`). Sa table des diagonales suit les codes relevés sur la machine.

### 6.7 Les jeux patchés

Avec l'émetteur infrarouge, ces patchs ne sont plus nécessaires pour jouer à la DB9. Ils restent utiles **sans émetteur**, pour une réponse sans latence, ou pour la souris (Exelnoid, ExelPaint). Chaque script vérifie qu'il s'agit bien de la version analysée du jeu, puis produit la ROM patchée ; dans EXLPICO, la ROM s'affiche avec sa zone d'échange.

| Jeu | Patch | Ce qu'il fait | Vérifié |
|---|---|---|---|
| **Exelnoid** | `exelnoid_exlcart.diff` (sources) | souris USB au format ExelMouse (`read_mouse_usb`, `mouse_mag`), activée d'office au menu ; DB9 pour la raquette, le tir, la pause ; zone `EXLP` | sur la machine (souris) |
| **Wizord** | `wizord/make_wizord_db9.py` | 2 octets changés (`$61AF`) : une routine en `$5000` traduit `$3FE0` en code du 7041 et appelle le gestionnaire de Wizord ; image de 32 Ko | sur la machine (carte linéaire) |
| **Tennis** | `tennis/make_tennis_db9.py` | `BRTIME` → `$7F60` (2 octets) : la DB9 est le joystick 1, 8 directions, menus au clavier | simulateur, 29 contrôles |
| **Virus** | `virus/make_virus_db9.py` | ses deux routines minuterie redirigées (6 octets) ; image de 32 Ko | simulateur, 24 contrôles |
| **Guppy** | `guppy/make_guppy_db9.py` | `BRTIME` → `$7F00` (2 octets) ; image de 32 Ko | simulateur, 19 contrôles |
| **Capitaine Menkar** | `menkar/make_menkar_db9.py` | ROM pleine : patch et zone **sous le jeu**, en `$3F00` / `$3FE0` (2 octets changés) | simulateur, 19 contrôles |
| **PINDO** | `pindo/make_pindo_db9.py` | le chargeur pose une autre routine minuterie (2 octets) ; le patch range le code dans `R3` ; patch et zone en `$3F00` / `$3FE0` | exécution de la ROM, 16 contrôles |
| **ExelPaint** | `exelpaint/make_exelpaint_usb.py` | pilote de souris USB au format ExelMouse, en `$7E00` (7 octets changés) ; zone `EXLP` en `$7F00` | simulateur, 32 contrôles |

Exemple : `python3 make_tennis_db9.py tennis_exl100.rom tennis_db9.rom`. Les scripts assemblent eux-mêmes leur patch : TASM n'est pas nécessaire. Les détails de chaque patch (adresses, choix des diagonales, octets de RAM utilisés) sont en tête des fichiers `*_patch.asm`.

Particularités :
- **Exelnoid** : sur une vraie EPROM, ou sans manette ni souris, il se comporte exactement comme avant. Assembler la version adaptée comme d'habitude, en vérifiant qu'elle tient dans les 32 Ko (environ 140 octets ajoutés). Une fois, sur la machine, une partie de l'écran s'est brouillée puis le jeu s'est planté ; non reproduit depuis ([§12](#12-dépannage)).
- **Wizord** : le menu (choix du niveau) reste au clavier ; la carte support EPROM (feu sur GP17) n'a pas été essayée avec lui.
- **Tennis** : le pavé du joystick infrarouge n'existe pas sur une DB9 : la touche `0` (service droit) se tape au clavier.
- **Virus, Guppy, Wizord** : les scripts produisent une image de 32 Ko, le jeu à sa place. Une ROM de 16 ou 8 Ko recopiée dans tout l'espace ferait trouver la zone dans une copie que le jeu ne lit pas (EXLPICO évite aussi ce piège, [§8](#8-loutil-exlpico)).
- **ExelPaint** : la souris USB marche même si l'écran de configuration a conclu « pas de souris », et peut être branchée en cours de route. La sensibilité d'EXLPICO donne directement la vitesse du curseur.

---
## 7. Vidéo et musique

L'EXL100 affiche une vidéo rangée dans la flash du Pico : **320 × 240 pixels, en noir et blanc ou en 8 couleurs**, à la vitesse du film, avec une **musique 1 bit** par la sortie cassette, reliée au son du téléviseur. Deux lecteurs existent :

| | Lecteur vidéo (`badapple.rom`, zone `EXLV`) | Version disque (`badflux.rom`, zone `EXLD`) |
|---|---|---|
| Qui calcule | **le Pico** décode la vidéo et prépare, à chaque image, les seuls morceaux d'écran qui changent | **personne pendant la lecture** : tout est préparé sur PC ; le Pico sert un fichier octet par octet, l'EXL100 fait le reste |
| Fichier | `.exv` (codec compressé) : Bad Apple 1,8 Mo avec la musique | `.exf` (listes non compressées) : Bad Apple 10 Mo |
| Place dans la flash | à 8 Mo | à 2 Mo (seule), ou après les jeux d'une bibliothèque |
| Images par seconde (Bad Apple) | 23 à 31 sur la machine | 15 à 30 sur la machine |
| Couleurs | noir et blanc ou 8 couleurs | noir et blanc |
| Depuis la bibliothèque | non | **oui** |

Les deux demandent la carte de 16 Mo.

### 7.1 Le lecteur vidéo (le Pico décode)

**Installer Bad Apple** : copier `exemples/badapple/badapple_carte_lineaire.uf2` (ou `_carte_eprom.uf2`) sur le Pico : firmware, lecteur, vidéo et musique (3,7 Mo, environ une minute de copie). Insérer, allumer : la vidéo démarre en moins d'une seconde.

- **En haut de l'écran**, une ligne de texte : le titre, la position dans le film (m:ss) et le nombre d'images affichées dans la dernière seconde.
- **La vitesse du film est respectée** (29,97 images par seconde, d'après l'horloge du Pico) : quand l'EXL100 n'a pas le temps d'afficher une image, il la saute ; le film ne ralentit pas. La musique suit la même horloge.
- **À la fin**, la dernière image reste 2 secondes, puis le film recommence. **Reset** : le film repart du début.
- Sans EXELpicoCart (vraie EPROM) : « IL FAUT LA CARTOUCHE EXELPICOCART ». Sans vidéo valide dans la flash : « PAS DE VIDEO DANS LE PICO (EXLVIDEO) ».

**En couleurs : Big Buck Bunny.** `exemples/bigbuckbunny/bigbuckbunny_carte_lineaire.uf2` (ou `_carte_eprom`) : la première minute du dessin animé libre de la Blender Foundation (licence CC BY 3.0, voir `CREDITS.txt`), en 320 × 180 avec des bandes noires, dans les 8 couleurs du TMS3556.

**Une autre vidéo** :

```sh
python3 outils/exlvideo.py film.mp4 film.exv --titre "MON FILM"                      # noir et blanc
python3 outils/exlvideo.py film.mp4 film.exv --couleur --titre "MON FILM"            # 8 couleurs
python3 outils/exlvideo.py film.mp4 film.exv --titre "MON FILM" --midi partition.mid # avec musique
```

Il faut **Python 3**, **numpy** et **ffmpeg** (dans le PATH). Le script réduit la vidéo à 320 × 240 (bandes noires si elle n'est pas en 4/3), la code, et écrit `film.exv`, à charger dans EXLPICO (carte **Vidéo** : **UF2 complet + vidéo**, ou **vidéo seule** pour changer de vidéo ensuite). Avec `--uf2`, il écrit aussi `film_video_seule.uf2`.

- Options : `--debut S`, `--duree S`, `--ips N` (30 au plus), `--seuil B,H` (noir sous B, blanc à partir de H ; entre les deux, le pixel garde sa couleur précédente), `--inverse` ; en couleur, `--contraste`, `--saturation`, `--hysteresis`.
- **Ce qui marche bien** : des formes nettes et contrastées, comme les silhouettes de Bad Apple. Une vidéo grise ou bruitée change partout d'une image à l'autre : l'EXL100 affiche alors moins d'images par seconde.
- **En couleur**, la conversion renforce contraste et saturation, puis fait un **tramage ordonné 4 × 4** sur chaque composante (le même motif d'une image à l'autre, sans scintillement). L'**hystérésis** garde sa couleur à un point proche de son seuil : beaucoup moins de points changent, l'EXL100 a moins de travail (environ 14 → 21 images par seconde).
- Compter environ une minute de calcul par minute de film.

**Comment ça marche.**
- **La vidéo dans la flash** (`0x800000`) : un en-tête de 64 octets (signature `EXLV`, taille, nombre d'images, images par seconde, somme de contrôle, titre), puis les données. Chaque image est codée par rapport à la précédente, avec un codeur arithmétique (celui de LZMA) qui prédit chaque pixel d'après ses voisins et l'image d'avant (`exlvcodec.h`). En couleur (version 2 du format), chaque image a 3 plans, codés avec le contexte des plans déjà connus. Les 3 min 52 de Bad Apple (6 955 images) tiennent en 1,3 Mo, la minute de Big Buck Bunny en 0,8 Mo.
- **Le Pico** : le cœur 1 sert le bus, avec exactement les mêmes instructions pendant l'accès que la ROM unique ; le cœur 0 décode le film au rythme de ses images (4 à 10 ms par image en noir et blanc, jusqu'à 23 ms en couleur) et, à chaque demande de l'EXL100, compare la dernière image décodée avec ce que l'EXL100 affiche, et prépare la liste des modifications.
- **Le lecteur EXL100** (`badapple.asm`, 1,6 Ko) : mode mixte du TMS3556 (une ligne de texte, puis 240 lignes bitmap). Dans sa zone `EXLV` (en `$7F00`), **une seule adresse sert de tuyau** : à chaque lecture de `V_FIFO`, le Pico y présente l'octet suivant de la liste, comme les signaux des banques.

| Commande de la liste | Effet | Coût pour l'EXL100 |
|---|---|---|
| ADR lo hi | place le pointeur d'écriture du VDP | ~120 cycles |
| GROUPES n | n groupes de 8 pixels (1 à 27) : chaque octet écrit 3 fois (plans bleu, vert, rouge) | **41 cycles par groupe** |
| BRUT n | n octets écrits tels quels (ligne de texte, groupes en couleur) | 21 cycles par octet |
| FIN | image terminée | — |

Le premier octet de chaque commande est directement un déplacement dans une table de sauts (`br @JT(B)`), et les groupes sont écrits par un bloc déroulé dans lequel on entre au bon endroit. Une image de Bad Apple change en moyenne d'environ 600 groupes, soit 27 ms à 2,455 MHz.

| Adresse | Rôle |
|---|---|
| `V_PRES` (`$7F02`) | `$A5` : vidéo présente ; `$5A` : pas de vidéo valide ; 0 sur une EPROM |
| `V_RST` (`$7F09`) | lecture : (re)démarrage, le Pico repart du début |
| `V_SYNC` (`$7F03`) | lecture : image affichée, la suivante s'il vous plaît |
| `V_STAT` (`$7F01`) | 0 tant que la liste n'est pas prête, puis 1 |
| `V_FIFO` (`$7F00`) | la liste, octet par octet |

Le descripteur qui suit la signature (`$7F04`) donne au firmware l'adresse de la table de sauts, les déplacements des commandes et la géométrie de l'écran : un autre lecteur peut s'en servir sans changer le firmware.

**Vérifié** : les images décodées par le code du firmware sont identiques aux images de départ, et les codeurs Python et C produisent les mêmes octets ; le vrai lecteur, exécuté octet par octet dans un émulateur EXL100, affiche chaque image exacte au pixel près ; le vrai firmware dans l'émulateur RP2040, sur les deux cartes, reconstruit 60 images exactes ; la simulation du film entier donne 27 images par seconde en moyenne (Bad Apple) et 21 (Big Buck Bunny). **Sur la console** (EXELTEL, carte support EPROM) : Bad Apple de 23 à 31 images par seconde, Big Buck Bunny de 11 à 24, rendu conforme à l'émulateur.

### 7.2 La musique 1 bit

La sortie cassette de l'EXL100 (bit 3 du port B du TMS7020) est reliée au son du téléviseur : un seul bit, que le processeur inverse, comme le haut-parleur d'un ZX Spectrum. La musique vient d'une **partition MIDI**, jouée à la manière des musiques « beeper » des micros 8 bits :

- **la mélodie** et **la basse**, en impulsions étroites à la fréquence de chaque note : les deux voix s'entendent en même temps ;
- **la batterie** : la grosse caisse en « bip » qui descend, la caisse claire en salves de bruit, les cymbales en bruit qui s'éteint, un clic pour le charleston.

Il n'y a ni synthé vocal ni TMS7041 dans la boucle. (Première version abandonnée : la bande son recodée pour le synthétiseur vocal TMS5220 ; beaucoup de parasites sur la console.)

**Utilisation** :
- `badapple_1bit.exv` (dans les UF2 tout faits) a la musique ; `badapple.exv` est la version muette, avec le même firmware et le même lecteur.
- `--midi partition.mid` : le script affiche les pistes et choisit la mélodie, la basse et la batterie ; on peut les imposer avec `--midi-melodie 6,5` (dans l'ordre de priorité), `--midi-basse 9`, `--midi-batterie 2`.
- `--midi-decalage S` : l'instant du film où commence la partition. Sans cette option, le script le trouve seul : d'abord là où la bande son n'est pas silencieuse, puis à 5 ms près, grosses caisses de la partition contre attaques graves de la bande son.
- **Changer seulement la musique** d'une vidéo déjà codée : `python3 outils/exlvideo.py film.exv nouveau.exv --midi partition.mid`.
- Le script écrit aussi `film_1bit.wav` : la sortie 1 bit telle que le téléviseur la jouera.

**La partition de Bad Apple** est « badapple!!midifull.mid » de PGFLIMSXD ([github.com/Handhule90/badapple-midi](https://github.com/Handhule90/badapple-midi)), non fournie ici faute de licence. Pistes : mélodie 6 puis 5, basse 9, batterie 2 ; décalage trouvé : 1,32 s. La musique *Bad Apple!!* appartient à ses auteurs (ZUN, Alstroemeria Records).

**Comment ça marche** (lecteur vidéo) :
- le lecteur (zone `EXLV` version 3) lit `V_SON` (`$7F1F`) ; si le Pico y a mis 1, il range l'adresse de sa routine dans `BRTIME` (`$C004`), que le moniteur appelle à chaque interruption de la minuterie 1, et démarre la minuterie (une unité = 13 µs sur l'EXL100) ;
- la routine fait 4 instructions : `lda @S_MASK ; xorp A,P6` (inverser la sortie, ou rien), `lda @S_REL ; movp A,P2` (la recharge) ;
- **la minuterie compte seule** : le décompteur se recharge à chaque débordement avec la valeur écrite *avant*. Le temps de réponse de l'interruption ne s'accumule donc pas ;
- **le Pico** date chaque lecture de `S_MASK` et présente l'entrée suivante après celle de `S_REL`. Au départ, il **mesure la minuterie** de la console (16 écarts à la recharge 255, puis 48 à la recharge 63) : peu importe sa vitesse (l'EXELTEL a un autre quartz). Ensuite, il place chaque inversion à l'heure de la partition, 20 ms après l'image, et se recale en permanence sur les lectures datées ;
- coût : environ 150 cycles par interruption, 900 à 1 400 interruptions par seconde, soit 5 % du temps de l'EXL100 ;
- dans le `.exv`, la musique est un bloc `EXL1` après la vidéo : un en-tête de 20 octets, puis un mot de 16 bits par événement (écart en µs, et un bit « inverser »). Bad Apple compte environ 228 000 inversions.

**Vérifié** : dans les ROM du moniteur, l'interruption de la minuterie appelle bien l'adresse rangée en `$C004`, sur l'EXL100 (TMS7020) comme sur l'EXELTEL (TMS7040) ; dans l'émulateur EXL100, les 5 773 inversions de 6 s de film sortent toutes, à 6 µs près en médiane ; toute la chanson avec une console simulée à la microseconde (écart moyen 16 µs), y compris avec une minuterie deux fois plus rapide et des interruptions perdues ; le vrai firmware dans l'émulateur RP2040. **Sur la console** : le son sort bien par le téléviseur de l'EXELTEL.

### 7.3 La version disque : le Pico ne calcule rien

Ici, le Pico ne fait que le travail d'un **contrôleur de disquette**. Il sert deux fichiers rangés dans sa flash, chacun **octet par octet à une seule adresse** : `D_DAT0` pour le film, `D_DAT1` pour le son. Une lecture de `D_RST` les ramène tous deux au début. Tout le calcul est fait à l'avance sur PC, comme pour les démos des micros 8 bits qui lisent leur film sur disquette.

**Installer**, au choix :
- **le fichier tout fait** : `badflux_carte_lineaire.uf2` ou `badflux_carte_eprom.uf2` (livrés à part, 21 Mo : plusieurs minutes de copie) : firmware, lecteur et film ;
- **avec EXLPICO** : la ROM `exemples/badflux/badflux.rom` en haut de la page, le film `exemples/badflux/badflux.exf` dans la carte **Version disque**, puis **UF2 complet + disque**. **UF2 film seul** change ensuite de film sans toucher au reste ;
- **dans une bibliothèque**, avec vos jeux : ajouter `badflux.rom` aux jeux et choisir le film dans la carte **Version disque** ; le film est rangé après les jeux, et le lecteur se lance depuis le menu comme un jeu. Un **reset** ramène au menu ; relancé, le film repart du début.

Seule, la version disque prend la place de la bibliothèque (à 2 Mo) : installer l'une efface l'autre. Dans une bibliothèque, film et jeux tiennent ensemble dans les 16 Mo : avec Bad Apple, il reste la place d'une centaine de jeux.

**Ce que fait l'EXL100** (`badflux.asm`, 1,9 Ko) :
- **le film** : une suite de listes, avec les mêmes commandes que le lecteur vidéo (ADR, GROUPES, BRUT, FIN), chacune précédée de son heure. Le lecteur attend cette heure, puis exécute la liste ;
- **l'horloge** : la minuterie 1 (une unité = 13,02 µs). Au démarrage, le lecteur mesure lui-même sa vitesse et choisit le prédiviseur : 3 sur l'EXL100, 7 si elle va deux fois plus vite ;
- **le texte** : le titre arrive avec la première liste ; chaque seconde, l'EXL100 écrit le temps écoulé (`m:ss`) et le nombre d'images affichées ;
- **le son** : à chaque débordement de la minuterie, l'interruption lit deux octets de `D_DAT1` : l'inversion (`$08` ou 0), puis la recharge. Elle ajoute aussi l'écart écoulé à l'horloge. Environ 190 cycles par interruption, soit 6 à 8 % du temps de l'EXL100.

**Ce que fait le Pico** : le cœur 1 sert le bus (mêmes instructions pendant l'accès que la ROM unique) et, après chaque lecture de `D_DAT0` ou `D_DAT1`, présente l'octet suivant, pris dans un tampon en RAM ; le cœur 0 ne fait que recopier la flash dans ces tampons, quelques milliers d'octets en avance. Après la fin d'un fichier, il sert `$FF`. `D_PRES` vaut `$A5` si les fichiers sont là, `$5A` sinon (le lecteur affiche alors « pas de film »).

**Fabriquer un autre film** :

```sh
python3 outils/exlflux.py film.mp4 film.exf --titre "MON FILM" --midi partition.mid
```

Il faut `exlvideo.py` à côté du script ; les options de la musique sont les mêmes. Avec `--uf2 badflux.rom firmware.uf2 [firmware_eprom.uf2]`, le script écrit aussi les fichiers complets. Pour le film, le script simule l'EXL100 : le coût de chaque liste en cycles, plus le temps pris par les interruptions du son. À chaque liste, il prend l'image du moment où la console sera prête : quand une image change beaucoup, la suivante est prise un peu plus loin, et le film ne prend pas de retard. Il écrit aussi `film_1bit.wav`.

*Essai abandonné* : faire fabriquer la musique par l'EXL100 lui-même, à partir de la partition. Cela coûtait environ 550 cycles par interruption (20 à 30 % du temps de la console), et des interruptions se perdaient pendant les roulements de batterie.

| Bad Apple, version disque | |
|---|---|
| Images | 5 688 listes, 24,5 images par seconde en moyenne (27 avec le lecteur vidéo) |
| Film | 10,1 Mo de listes non compressées (1,3 Mo avec le codec du Pico) |
| Son | 0,5 Mo |
| Conteneur `EXLF` | 10,5 Mo ; au-delà de 14 Mo, il ne tient plus dans la flash (un film aussi agité ne pourrait durer qu'environ 5 minutes) |

**Le conteneur `EXLF`** : un en-tête de 64 octets, petit-boutiste (signature `EXLF`, version 1, deux fichiers ; pour chacun sa position, sa taille et sa somme de contrôle ; le titre sur 28 octets ; la somme des 60 premiers octets), puis les deux fichiers. Chaque liste du film commence par son heure (3 octets, poids fort d'abord, en pas de 256 unités, soit 3,33 ms) ; `$FFFFFF` marque la fin : le lecteur revient au début des deux fichiers.

**Vérifié** :
- **émulateur EXL100, film entier** : le vrai lecteur, avec la minuterie et l'interruption qui passe par le vrai code du moniteur : les 5 687 listes donnent chacune exactement l'image prévue, et finissent au plus tard à l'heure prévue ; les 227 298 inversions tombent à moins de 13 µs de l'heure voulue ; 24,3 images par seconde. Même résultat avec une minuterie deux fois plus rapide ;
- **vrai firmware dans l'émulateur RP2040**, deux cartes : `$5A` sans film ou avec un en-tête abîmé, le reste de la ROM servi tel quel, 40 000 lectures mélangées des deux fichiers toutes justes, retour au début compris ; aussi avec les fichiers faits par EXLPICO ;
- **dans une bibliothèque** (`run_libflux.js`, deux cartes, bibliothèque faite par EXLPICO avec le vrai lecteur et le vrai film) : lancement depuis le menu, film et son justes, `D_RST`, reset → menu, relance, puis un jeu ordinaire servi comme avant ; et sans film, `$5A` ;
- **sur la console** (EXELTEL, carte support EPROM, version seule) : de 15 à 30 images par seconde, avec quelques saccades comme avec le lecteur vidéo ; la musique sort bien. Le lancement depuis la bibliothèque n'a pas encore été essayé sur la machine.

---

## 8. L'outil EXLPICO

`outils/exlpico.html` est une page autonome. Tout tourne dans le navigateur, sans serveur.

| Carte | Rôle |
|---|---|
| **Carte (brochage)** | linéaire (et EXELpicoCart) ou support EPROM : choisit le firmware mis dans les fichiers |
| **ROM** | charge une ROM et l'analyse : organisation, pied `AA/55 8C`, point d'entrée, somme de contrôle, zones (`EXLJ` / `EXLP` : manette et souris ; `EXLV` : lecteur vidéo ; `EXLD` : lecteur disque ; aucune : la DB9 passera par l'infrarouge) |
| **Banques de 16 Ko** | banques 1, 2… d'une ROM à banques : aperçu du début de chaque banque, capacité, contrôle de `$3FF0–$3FFF` |
| **Vidéo** | une vidéo `.exv` : titre, durée, couleur, musique, somme de contrôle ; UF2 complet + vidéo, ou vidéo seule |
| **Version disque** | un film `.exf` : titre, tailles, sommes de contrôle ; UF2 complet + disque, ou film seul ; le même film sert à la bibliothèque |
| **Réglage du service** | délai et maintien « façon EPROM » (**laisser 0 / 0** : réponse immédiate, validée sur les deux machines) ; sensibilité de la souris USB |
| **Fichier .uf2** | UF2 complet (firmware + ROM) ou UF2 ROM seule, avec les banques s'il y en a |
| **Bibliothèque de jeux (menu)** | loader, liste des jeux, des images Exelmémoire `.cram` / `.BKP` et des cassettes `.k7` (état de chaque entrée, BASIC de chaque cassette, place dans la flash), UF2 bibliothèque complète ou seule, avec le film d'un lecteur disque |
| **Sauvegardes de l'Exelmémoire** | lecture par USB des sauvegardes faites depuis le menu (touche M), téléchargement en `.cram` ou `.BKP`, ajout à la bibliothèque, effacement ([§4](#sauvegarder-lexelmémoire-dans-le-pico-v51)) |
| **Sonde (diagnostic)** | UF2 sonde, lecture du relevé par USB, fichier de test de la flash ([§11](#11-la-sonde-diagnostic)) |
| **Installer sur le Pico** | rappel de la procédure |

### ROM acceptées

EXLPICO repère l'organisation du fichier d'après la position du pied `AA 8C` ou `55 8C` :

| Fichier | Traitement |
|---|---|
| 32 768 octets, image depuis `$0000` | placé tel quel |
| 32 256 octets, image depuis `$0200` | décalé à sa place |
| 16 Ko ou 8 Ko | recopié dans tout l'espace (A13 et A14 sont ignorées par une petite EPROM) ; **sauf s'il porte une zone d'échange** : laissé seul à sa place, sinon le firmware trouverait la zone dans une copie que le jeu ne lit pas |
| sans pied reconnu | refusé, avec un diagnostic (positions trouvées, 8 derniers octets) |

Deux signatures existent : **`AA 8C`** pour les cartouches officielles, **`55 8C`** pour les ROM produites par obj2exl. Les deux sont acceptées partout.

### Ce qu'écrit chaque fichier

- Les UF2 « ROM unique » écrivent un bloc de zéros sur l'en-tête de la bibliothèque (et sur celui des banques quand la liste est vide) : le firmware sert alors la ROM. Les jeux restent dans la flash, mais ne sont plus utilisés tant qu'une bibliothèque n'est pas réinstallée.
- Un UF2 de version disque seule écrit son film à 2 Mo, à la place de la bibliothèque ; un UF2 de bibliothèque écrit son en-tête à 2 Mo, à la place d'un film seul.
- Une vidéo (`.exv`, à 8 Mo) n'efface ni la ROM ni la bibliothèque.
- Un UF2 « firmware seul » (`firmware/uf2/`) ne touche à rien d'autre.

---

## 9. Compiler le firmware du Pico

Inutile si vous ne modifiez pas les sources : les firmwares précompilés sont dans `firmware/uf2/` et déjà intégrés dans EXLPICO.

### Prérequis

- **Pico SDK 1.5.1** : version utilisée pour les binaires livrés. Une version 2.x devrait fonctionner, mais elle n'a pas été testée.
- **arm-none-eabi-gcc** (testé avec 13.2), **CMake** ≥ 3.13, **make** ou **ninja**, **Python 3**.

### Linux / macOS

```sh
# une seule fois : le SDK
git clone -b 1.5.1 https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk && git submodule update --init && cd ..
export PICO_SDK_PATH=$PWD/pico-sdk

# la compilation (les deux cartes)
cd EXLCART/firmware
mkdir build && cd build
cmake ..
make exlcart exlsonde exlcart_eprom exlsonde_eprom
```

Sous Debian ou Ubuntu : `sudo apt install gcc-arm-none-eabi cmake build-essential`.

### Windows

Le plus simple est l'extension **Raspberry Pi Pico** de VS Code, qui installe le SDK et le compilateur (ou l'installateur « Pico setup for Windows »). Dans le terminal fourni :

```bat
cd EXLCART\firmware
mkdir build
cd build
cmake -G "NMake Makefiles" ..
nmake exlcart exlsonde exlcart_eprom exlsonde_eprom
```

Avec Ninja : `cmake -G Ninja ..` puis `ninja exlcart exlsonde exlcart_eprom exlsonde_eprom`.

### Résultat

| Fichier | Contenu |
|---|---|
| `build/exlcart.uf2` | cartouche, carte linéaire (= `firmware_seul_exlcart_SANS_ROM.uf2`) |
| `build/exlcart_eprom.uf2` | cartouche, carte support EPROM |
| `build/exlsonde.uf2` | sonde, carte linéaire |
| `build/exlsonde_eprom.uf2` | sonde, carte support EPROM |

Le brochage d'une carte est entièrement décrit dans `exlpins.h` ; la variante support EPROM est compilée avec `EXL_PINOUT_EPROM`. La carte cible est la carte `pico` du SDK : son chargeur de démarrage (boot2 W25Q080) convient au Purple Pico 16 Mo, et l'accès à la flash au-delà de 2 Mo est vérifié ([§11](#11-la-sonde-diagnostic)).

Les autres cibles sont des variantes d'essai pour l'émulateur RP2040 (EXLOUTILS), qui n'a qu'un cœur ; elles ne sont pas faites pour la console :

| Cible (et `_eprom`) | Ce qu'elle fait |
|---|---|
| `exlcart_test` | tous les modes, servis par un seul cœur (le travail du cœur 0 entre deux accès) |
| `exlcart_libio_test` | bibliothèque et périphériques, sans service du bus |
| `exlcart_ir_test` | l'émetteur infrarouge seul |
| `exlcart_libflux_test` | bibliothèque avec un lecteur disque ou une cassette K7, servie par un seul cœur |
| `exlcart_usbdev_test` | bibliothèque, Pico seul sur un PC : port série USB et sauvegardes de l'Exelmémoire |

### Utiliser un firmware recompilé

- **soit** installer `build/exlcart.uf2` sur le Pico : il ne touche ni aux ROM ni à la bibliothèque ;
- **soit** l'intégrer dans EXLPICO, pour que les boutons « complet » l'utilisent :
  ```sh
  python3 outils/integrer_firmware.py firmware/build/exlcart.uf2 firmware/build/exlsonde.uf2 \
      firmware/build/exlcart_eprom.uf2 firmware/build/exlsonde_eprom.uf2
  ```
  Chaque fichier est reconnu à son nom ; on peut n'en donner que certains. Une sauvegarde `exlpico.html.bak` est faite.

Les fichiers tout faits des exemples (Bad Apple, Big Buck Bunny, `jstest`, badflux) contiennent le firmware du moment où ils ont été faits : après une mise à jour, les refaire avec EXLPICO, ou réinstaller ensuite un firmware seul.

### Tests sur PC

```sh
cd firmware
gcc -O2 -Wall -o test_multi test_multi.c && ./test_multi
gcc -O2 -Wall -o test_pins test_pins.c && ./test_pins
gcc -O2 -Wall -o test_io test_io.c && ./test_io
gcc -O2 -Wall -o test_kbd test_kbd.c && ./test_kbd
gcc -O2 -Wall -o test_pad test_pad.c && ./test_pad
gcc -O2 -Wall -o test_xinput test_xinput.c && ./test_xinput
# les memes pour la carte support EPROM : ajouter -DEXL_PINOUT_EPROM
gcc -O2 -Wall -o exlvenc exlvenc.c && gcc -O2 -Wall -o test_video test_video.c
./test_video ../exemples/badapple/badapple.rom ../exemples/badapple/badapple_1bit.exv images.bin listes.bin 3 400
```

- `test_multi` : 69 scénarios de la machine d'états (choix d'un jeu, lancement par `$6FFF` et par reset, retour au menu, jeu pas encore chargé, reset trop tôt, image Exelmémoire copiée dans la fenêtre du menu, image démarrée, sans cartouche, sauvegarde reçue, reprise à chaud de l'EXELTEL…), dont l'état vu par le cœur 0.
- `test_pins` : le brochage, la correspondance adresse ↔ case pour les 32 768 adresses, puis 4 × 32 768 accès simulés avec du bruit sur toutes les broches qui ne sont pas des adresses.
- `test_kbd` : le clavier USB, 88 contrôles : chaque correspondance AZERTY → EXL100, les séquences envoyées par l'émetteur, plusieurs touches tenues, flèches et espace en manette.
- `test_pad` : la manette USB, 47 contrôles : les descripteurs de rapport de vraies manettes, le décodage de leurs rapports, et les trames envoyées pour leurs directions et leurs boutons.
- `test_xinput` : les manettes Xbox, 48 contrôles : reconnaissance, rapports 360, One / Series et sans fil, paquets de mise en marche et accusés, trames envoyées.
- `test_io` : la recherche de la zone d'échange et les compteurs de souris (200 000 rapports aléatoires : le cumul lu par le jeu reste exactement égal au total divisé par la sensibilité).
- `test_video` : toute la vidéo décodée par le décodeur du firmware et comparée aux images de départ (`exlvideo.py --images`), les listes que le firmware enverrait, et toute la musique jouée avec une console simulée à la microseconde (quatre cas, dont une minuterie deux fois plus rapide). `exlvenc` code un fichier d'images en `.exv` et le relit aussitôt.

Les essais du vrai firmware dans l'émulateur RP2040 et du code EXL100 dans le simulateur TMS7020 sont dans l'archive EXLOUTILS.

---
## 10. Fonctionnement technique

### 10.1 Carte de la flash du Pico

| Adresse flash | Contenu |
|---|---|
| `0x000000` | firmware (80 Ko pour la cartouche, 84 Ko carte support EPROM ; 47 / 49 Ko pour la sonde) |
| `0x100000` | ROM unique (32 Ko) |
| `0x108000` | bloc de réglages `EXLC` (délai, maintien, sensibilité de la souris) |
| `0x10F000` | en-tête des banques `EXLK` |
| `0x110000 + (k − 1) × 0x4000` | la banque k (k = 1 à 15), 16 Ko |
| `0x180000` | relevé de la sonde (64 Ko) |
| `0x190000 + k × 0x5000` | sauvegarde de l'Exelmémoire k (k = 0 à 7, menu v5.1) : en-tête `EXLM` (`0x4D4C5845`, version, numéro, taille, somme), puis l'image à +`0x1000` |
| `0x200000` | en-tête de bibliothèque `EXLB`, puis à +256 la liste des jeux (3 840 octets) ; **ou** le conteneur `EXLF` de la version disque seule (jusqu'à 14 Mo) |
| `0x201000` | le menu (loader), 32 Ko |
| `0x209000 + n × 0x8000` | le jeu n (n = 0 à 189) |
| après le dernier jeu, aligné sur 64 Ko | le conteneur `EXLF` d'un lecteur disque de la bibliothèque |
| `0x800000` | la vidéo du lecteur vidéo : en-tête `EXLV` de 64 octets, puis les données (jusqu'à 8 Mo) |

Tous les en-têtes sont petit-boutistes.
- **Banques** : `EXLK` (`0x4B4C5845`), version 1 (16 bits), nombre de banques **y compris la banque 0** (16 bits, 2 à 16).
- **Bibliothèque** : `EXLB` (`0x424C5845`), version (16 bits), nombre de jeux (16 bits), puis les décalages du menu, du premier jeu, le pas entre deux jeux, et (version 2) l'emplacement du film d'un lecteur disque, 0 s'il n'y en a pas (32 bits chacun).

### 10.2 Les fichiers UF2 sont écrits secteur par secteur

La ROM de démarrage du RP2040 efface la flash par secteurs de 4 Ko, et choisit le secteur à effacer d'après le **numéro du bloc** dans le fichier UF2, pas d'après son adresse. Un fichier qui mélange plusieurs zones non alignées voit donc certaines de ses données effacées par le bloc suivant. EXLPICO émet toujours des **secteurs complets** de 16 blocs de 256 octets, complétés par `FF`. Si vous fabriquez vos propres UF2, respectez cette règle.

### 10.3 Le service du bus

La console met `/ROM_CS` à 0 pendant environ **400 ns** par accès, et lit l'octet environ **150 ns** après le début. Mesures faites avec la sonde :

| Boucle | Octet sur le bus après le début de l'accès |
|---|---|
| ROM unique, ROM à banques, bibliothèque, deux cartes (et les boucles vidéo et disque, qui ont les mêmes instructions pendant l'accès) | **85 à 125 ns** : fonctionne |
| première version de la bibliothèque (contrôles d'état **avant** l'octet) | 205–265 ns : la console ne reconnaît pas le pied |

Chaque boucle de service tourne en RAM, à 200 MHz, interruptions coupées, et a le même chemin critique :

- **au repos** : lire les broches, tester `/ROM_CS`, recommencer (3 instructions) ;
- **pendant l'accès** : l'adresse, l'octet en RAM, le décalage, l'écriture, l'activation des sorties (6 instructions) ;
- **après l'accès** (bus relâché, au moins 400 ns avant le suivant) : tout le reste. Signaux de la bibliothèque et des banques, octet suivant de la vidéo ou du disque, mesure des pauses au minuteur matériel.

Le code compilé de chaque boucle a été vérifié : repos et accès identiques à ceux de la ROM unique, instruction par instruction, sur les deux cartes.

Les sorties de données sont réglées à **12 mA**, le maximum du RP2040 (8 mA avant le 30/10/2026). Sur une EXL100, avec le Pico sur supports, la ligne D5 n'arrivait pas toujours à redescendre à 0 à 8 mA, alors que la même carte marchait sur l'EXELTEL et qu'un Pico soudé marchait partout.

| Mode | Cœur qui sert le bus | L'autre cœur |
|---|---|---|
| ROM unique, ROM à banques | cœur 0 | cœur 1 : la DB9, le clavier et la manette USB, l'émetteur infrarouge |
| ROM avec zone d'échange | cœur 1 | cœur 0 : la DB9, la manette et la souris USB (zone), le clavier USB et les boutons de la manette (infrarouge) |
| bibliothèque | cœur 1 | cœur 0 : chargement des jeux, zones d'échange, USB, infrarouge, film d'un lecteur disque |
| vidéo | cœur 1 | cœur 0 : décodage, listes, musique |
| version disque | cœur 1 | cœur 0 : recopie de la flash dans les tampons |

Le cœur qui sert le bus ne lit jamais la flash : un chargement ou un décodage ne ralentit pas le service.

### 10.4 La machine d'états de la bibliothèque

| État | Image servie | Passage à l'état suivant |
|---|---|---|
| **MENU** | le menu, liste insérée en `$7000` | lecture de `$6F00 + n` (n < nombre de jeux) → le cœur 0 charge le jeu n → **LAUNCH** |
| **LAUNCH** | le menu | lecture de `$6FFF` **ou** du pied `$7FFC`, jeu chargé → **GAME** |
| **GAME** | le jeu | lecture du pied `$7FFC` après une pause de plus de 2 ms, une fois le jeu lancé (au moins 1 000 accès) → **MENU** |

Les adresses de signal ne sont actives que dans l'état concerné : un jeu peut utiliser librement toute la plage `$0000–$7FFF`.

**Le pied est préparé d'avance.** Aucune décision n'est prise pendant un accès : la boucle sert toujours l'image courante.
- Dès que le jeu est chargé, le cœur 0 écrit **sa** signature (`AA` ou `55`) dans le pied du menu. Si la console redémarre, elle lit donc celle du jeu.
- Pendant la partie, la signature **du menu** est écrite dans le pied du jeu. Après un reset, la console lit donc celle du menu.

**Un lecteur disque parmi les jeux** (`exlfluxlib.h`). Au chargement d'un jeu qui porte une zone `EXLD`, le cœur 0 prépare le début du film (rangé après les jeux) dans l'image du jeu, et met `D_PRES` à `$A5`. Au passage en **GAME**, la boucle de la bibliothèque laisse la place à la boucle disque : mêmes instructions pendant l'accès, l'octet suivant du film ou du son après l'accès, et la même règle de retour au menu. Au reset, la boucle de la bibliothèque reprend.

### 10.5 Démarrage des jeux : pourquoi pas TRAP 0

Le pied d'une cartouche est une **instruction** : `55 8C 10 00`, c'est la signature `55` suivie de `8C 10 00`, soit `BR @$1000`. Au démarrage, la ROM interne vérifie la signature en `$7FFC`, puis exécute l'instruction en `$7FFD`.

`TRAP 0` relance la ROM interne sans faire un vrai reset de la machine. Sur l'EXELTEL, la ROM prend alors son chemin de redémarrage « à chaud » : elle affiche son **écran rouge** et attend une touche. Le loader démarre donc le jeu **lui-même**, comme le ferait la ROM : signal `$6FFF`, puis `BR @$7FFD`, depuis la RAM.

Conséquence : le jeu démarre dans l'état laissé par le menu (mode vidéo, police, interruptions actives), et non dans celui d'un démarrage à froid. Les jeux qui initialisent eux-mêmes leur affichage n'y voient aucune différence ; les 19 ROM testées démarrent ainsi.

### 10.6 Le choix du mode au démarrage

Le firmware copie d'abord la ROM rangée à 1 Mo en RAM, puis :

1. **en-tête `EXLB` valide à 2 Mo** → bibliothèque ;
2. sinon, la ROM réserve une **zone vidéo** `EXLV` → lecteur vidéo ;
3. une **zone disque** `EXLD` → version disque (conteneur `EXLF` à 2 Mo) ;
4. une **zone d'échange** `EXLJ` / `EXLP` → service sur le cœur 1, DB9 et souris sur le cœur 0, avec ou sans banques ;
5. un **en-tête `EXLK`** → ROM à banques ;
6. sinon → ROM unique.

Dans les cas 5 et 6, le cœur 1 lit la DB9, le clavier et la manette USB, et pilote l'émetteur infrarouge. Les réglages délai et maintien (bloc `EXLC`) ne servent qu'aux essais.

### 10.7 La carte support EPROM : même vitesse

Sur cette carte, les lignes ne sont plus dans l'ordre : A10 est sur GP24, et il y a un trou en GP17 au milieu des données. Plutôt que de remettre les bits dans l'ordre à chaque accès, on **range l'image en RAM dans l'ordre des broches**. Tout le travail est fait une seule fois, à la copie flash → RAM (`img_load` dans `exlpins.h`).

- **L'adresse** : l'interpolateur du cœur qui sert est réglé ainsi : voie 0 = GP0–GP13 ; voie 1, qui lit la même valeur, = GP24 placé en bit 14 ; BASE2 = adresse de l'image / 2. On écrit l'état des broches dans l'interpolateur, on relit son résultat, et une seule instruction `ldrh rX, [rh, rh]` lit la case.
- **La donnée** : chaque case fait 16 bits et contient déjà les bits à sortir sur GP14–GP22 (GP17 à 0). La sortie reste un simple décalage.

| | Carte linéaire | Carte support EPROM |
|---|---|---|
| Accès (après la détection) | `lsls`, `lsrs`, `ldrb`, `lsls`, `str`, `str` | `str`, `ldr`, `ldrh`, `lsls`, `str`, `str` |
| Cycles | identiques | identiques |
| Taille d'une image en RAM | 32 Ko | 64 Ko |
| RAM utilisée (cartouche / sonde) | 225 Ko / 166 Ko | 235 Ko / 200 Ko, sur 264 Ko |

Les adresses de signal sont comparées après l'accès, une fois l'index remis dans l'ordre. Les ROM, les fichiers UF2, la bibliothèque et le loader sont les mêmes pour les deux cartes : seule la copie en RAM change. Elle rallonge un peu le démarrage (quelques dizaines de ms en bibliothèque) et le chargement d'un jeu (quelques ms), loin des 300 ms après lesquelles la console lit le pied.

Un relevé de la sonde sur EXL100, avec Exeltris, confirme la vitesse : l'octet est sur le bus en **85 à 125 ns** (la console le lit vers 150 ns), `/ROM_CS` reste à 0 environ 425 ns par accès, et la somme de l'image servie est identique à celle de la ROM.

### 10.8 Le service avec banques

Au démarrage, `bank_prepare()` (`exlbanks.h`) construit **une image complète de 32 Ko par banque** : la zone fixe, la fenêtre de la banque, et le pied de la ROM principale. Changer de banque revient à changer le pointeur de l'image servie, comme la bascule menu → jeu de la bibliothèque. Le test du signal se fait **après** l'accès :

```c
if ((a & 0x7FF0) == 0x3FF0 && (a & 15) < nb) img = bank_img[a & 15];
```

Les images viennent d'une réserve, `img_pool`, qui sert aussi de tampon « jeu » à la bibliothèque et de mémoire de travail à la vidéo : ces modes s'excluent. Sa taille fixe la capacité : 5 images en plus de la ROM sur la carte linéaire, 2 sur la carte support EPROM, une de moins pour la sonde.

**Validation** : dans l'émulateur RP2040, sur les deux cartes, et sur la console avec BANKTEST (y compris un reset pendant que la banque 1 est active).

**Deux images de 32 Ko** (en-tête version 2) : `bank_prepare()` recopie la 2ᵉ moitié telle quelle dans `img_pool[0]`, et `serve_banks()` passe à `serve_full()`, même chemin critique, avec ce test après l'accès :

```c
if ((a & 0x7FF7) == 0x7FF0) img = (a & 8) ? img0 : img1;   /* $7FF0 -> image 1, $7FF8 -> image 0 */
```

La sonde ajoute la version à sa ligne `EXLBANKS` (`v2`), et EXLPICO l'affiche.

### 10.9 L'émetteur infrarouge

`exlir.h` lit la DB9 toutes les millisecondes, garde un état après 3 ms stables, le traduit en code de la manette d'origine, et décide des trames : tout de suite pour une nouvelle position (8 ms après la fin de la trame précédente), 90 ms après la fin de la précédente pour une position tenue, le code 62 au relâchement. Le clavier USB passe avant : les rapports du clavier (TinyUSB, `exlmouse.c`) donnent la touche du moment (`g_kbd`, `exlkbd.h`), et chaque changement de ce qu'il faut envoyer ajoute à une file sa séquence (modificateurs suivis de la fin, puis la touche ; la fin d'abord si une touche était tenue) ; un relâchement ajoute la fin. `kb_report` garde les touches tenues dans l'ordre d'appui : la plus récente compte, et si c'est une flèche ou l'espace, toutes les flèches et l'espace tenus forment un état de manette, traduit par la même fonction que la DB9 (`kb_joy_code`). Une manette USB (`exlpad.h`, ou `exlxinput.h` pour une Xbox) donne un octet au format de la DB9 (`g_pad`), ajouté à celui des broches avant l'anti-rebond, ici comme dans la zone d'échange ; ses boutons de touches passent par `g_kbd`, comme le clavier. Il pousse alors un mot de 32 bits à la machine PIO : bit k = case k de la trame (1 = porteuse).

La machine PIO (`exlir.pio`) tourne à 28 437,5 × 36 = 1,02375 MHz (diviseur calculé d'après l'horloge du système) : 36 cycles par période de porteuse, 16 allumés et 20 éteints, 16 périodes par case. Le passage d'une case à la suivante prend 5 cycles, qui remplacent le « `nop [4]` » des deux boucles : toutes les périodes font 36 cycles. Entre deux trames, la broche reste à 0.

La manette passe par l'infrarouge quand l'image servie n'a pas de zone d'échange : dans la bibliothèque, le cœur 1 indique au cœur 0 quand il sert le jeu (`g_mstate`). Le clavier passe toujours.

---

## 11. La sonde (diagnostic)

Le firmware **EXLSONDE** sert la cartouche comme EXLCART (mêmes ROM, même bibliothèque, même boucle). En plus, il **enregistre les accès** de la console :

- à chaque descente de `/ROM_CS`, un programme PIO prend **64 instantanés de toutes les broches**, toutes les **20 ns** ;
- le relevé est enregistré dans la flash après environ 1 s de calme ;
- on le relit plus tard par USB, Pico sorti de la console.

La sonde ne gère ni la DB9, ni la souris, ni l'infrarouge, ni la vidéo. En mode bibliothèque, elle reçoit aussi les sauvegardes de l'Exelmémoire faites depuis le menu, et les rend comme le firmware cartouche (commande `m`, carte « Sauvegardes de l'Exelmémoire » d'EXLPICO).

### Utilisation

1. Installer `firmware_seul_exlsonde_SANS_ROM.uf2` (carte linéaire) ou `firmware_seul_exlsonde_eprom_SANS_ROM.uf2` (carte support EPROM) : il ne touche ni à la ROM, ni à la bibliothèque. Ou **UF2 sonde (firmware sonde + ROM)** dans EXLPICO.
2. Insérer le Pico, allumer la console, faire l'essai, éteindre.
3. Pico sorti de la console : le brancher en USB **sans** BOOT, attendre 5 secondes, puis **Lire le relevé (USB)** dans EXLPICO (Chrome ou Edge). **Télécharger le relevé (.txt)** l'enregistre.
4. Pour revenir à la cartouche : réinstaller le firmware cartouche de votre carte.

### Lignes du relevé

| Ligne | Contenu |
|---|---|
| `EXLPINS` | le brochage du firmware sonde : `lineaire` ou `eprom`. EXLPICO s'en sert pour décoder le relevé, et prévient s'il ne correspond pas à la carte choisie |
| `EXLROM` | somme de la ROM unique dans la flash, son pied, les 8 octets au point d'entrée |
| `EXLLIB` | en-tête de bibliothèque (signature, version et nombre, emplacements), identifiant JEDEC de la puce flash |
| `EXLSR` | registres d'état de la puce flash ; EXLPICO signale une éventuelle protection en écriture |
| `EXLMAP` | les repères du fichier de test de la flash |
| `EXLBANKS` | nombre de banques dans la flash, nombre que cette sonde peut servir, 8 premiers octets de la banque 1 |
| `EXLMENU` | le menu tel qu'il est dans la flash : somme, pied, point d'entrée (si une bibliothèque est présente) |
| `EXLSONDE 4 n …` | en-tête du relevé (nombre de mots, horloge, somme de l'image **servie**, réglages), puis 64 mots par accès |

Chaque mot contient l'état des broches GP0–GP31 ; EXLPICO le décode selon la ligne `EXLPINS` : adresses lues, octet présenté et à quel instant, durée de l'accès. La **somme de l'image servie** dit quel mode était actif (en bibliothèque : la somme du loader, plus celle de la liste insérée).

À savoir pour lire un relevé :
- **Des accès peuvent manquer** : après chaque descente de `/ROM_CS`, la sonde enregistre pendant environ 1,3 µs, et un accès qui commence dans cette fenêtre n'est pas capturé (par exemple `$7FFE` entre `$7FFD` et `$7FFF`). On le reconnaît à l'octet de l'accès manquant, encore présent sur le bus au début de l'accès suivant.
- **GP25** passe à 1 en même temps que `/ROM_CS` remonte, probablement par couplage avec la LED bleue. Sans effet.

### Fichier de test de la flash

**UF2 test de la flash** écrit un repère (`EXT0` à `EXT4`) à 1,94 Mo, 2 Mo, 4 Mo, 8 Mo et 15 Mo. On installe ensuite le firmware sonde : la ligne `EXLMAP` montre quels repères sont arrivés. **Attention** : ce test écrit par-dessus la bibliothèque.

---

## 12. Dépannage

| Symptôme | Cause probable | Solution |
|---|---|---|
| Écran de la ROM interne : la cartouche n'est pas vue | Pico mal inséré, ROM sans pied valide, ou **firmware de l'autre carte** | vérifier l'insertion, l'analyse de la ROM et le choix de la carte en haut d'EXLPICO ; réinstaller un fichier « complet » ; faire un relevé avec la sonde |
| EXL100 seulement (l'EXELTEL marche avec la même carte) : la cartouche est vue, mais saute au mauvais endroit, écran noir ; la sonde montre les bons octets servis | une ligne de données mal tenue à 0 côté EXL100 (vu sur D5 : `00` lu `20`), à travers les **supports** du Pico | **souder le Pico** directement (ou supports tulipe) ; firmware récent : sorties de données à **12 mA** (au lieu de 8 mA). La ROM de test `testbus` avec la sonde dit quel bit est faux ([§11](#11-la-sonde-diagnostic)) |
| Menu affiché, mais « AUCUN JEU : UTILISER EXLPICO » | le loader est installé comme ROM unique | installer un UF2 bibliothèque |
| Après le choix d'un jeu, le menu revient | firmware trop ancien, qui ne connaît pas le signal `$6FFF` | installer l'**UF2 bibliothèque complète** |
| Après le choix d'un jeu : écran rouge (EXELTEL) ou noir (EXL100), puis un reset lance le jeu | loader ancien, qui redémarre par `TRAP 0` | réassembler `exlmenu.asm` et réinstaller la bibliothèque |
| La bibliothèque a disparu | une ROM seule, une version disque seule, ou le test de la flash, a été installé après elle | réinstaller la bibliothèque |
| ROM à banques : « BANQUE ACTIVE : 1 » mais octets `FF FF …` | banque assemblée avec un `.org $4000` seul : obj2exl l'a posée en `$1000` | ajouter le repère `.org $1000` / `.byte $FF` ([§5](#5-les-rom-à-banques-plus-de-32-ko)), refaire l'UF2 |
| ROM à banques : la fenêtre ne change pas, ou montre `FF` | banque au-delà de la capacité, liste des banques oubliée, firmware ancien | vérifier la carte **Banques** et la ligne `EXLBANKS` de la sonde ; **UF2 complet** |
| ROM à banques : affichage faux après un reset | le programme lit la fenêtre sans avoir choisi sa banque | lire `$3FF0 + n` au démarrage |
| Manette DB9 sans effet dans un jeu du commerce (infrarouge) | ROM avec zone d'échange (infrarouge coupé), firmware ancien, montage de l'émetteur | la **LED bleue** du Pico doit clignoter quand on bouge la manette ; sinon, vérifier la ROM (EXLPICO ne doit pas afficher de zone d'échange) et faire un **UF2 complet**. Si elle clignote : test de la diode verte ([§2.4](#24-lémetteur-infrarouge-facultatif)) ; sens de la diode (patte longue vers la résistance) et ordre des pattes du transistor ; rapprocher la diode de la fenêtre infrarouge, essayer 47 Ω |
| Diode verte d'essai allumée en permanence | transistor défectueux, ou cathode reliée à la masse | retirer le transistor : si la diode reste allumée, c'est le câblage ; sinon, changer le transistor (vu une fois) |
| Manette DB9 ou souris sans effet dans un jeu patché | ROM sans zone d'échange, firmware ancien ou sonde installée, broche 8 (masse) non reliée | EXLPICO doit afficher « Zone d'échange EXLJ / EXLP » ; **UF2 complet** ; vérifier le câblage |
| Manette 2 infrarouge jamais reçue (`jstest`) | récepteur du canal 2 de la console, ou manette du canal 1 | essayer Tennis d'origine à deux joueurs avec deux vraies manettes |
| Clavier USB sans effet | pas d'émetteur infrarouge, adaptateur sans OTG, clavier « non boot », vidéo ou version disque | la LED bleue doit clignoter à chaque touche ; essayer un autre clavier ou un hub ; un clavier sans fil avec dongle USB marche s'il parle le protocole « boot » |
| Clavier USB : flèches + espace, ou deux flèches, ne donnent pas les mêmes valeurs qu'au clavier mou (haut + gauche : `$54` au lieu de `$45`) | choix d'EXLCART : les flèches et l'espace imitent la **manette** ([§6.3](#63-un-clavier-usb-par-infrarouge)) | pour une touche ordinaire, tenir Ctrl ou Alt (préfixe Ctl ou Fct) |
| Clavier USB : minuscules et majuscules inversées par rapport au PC | c'est la règle de l'EXL100 (majuscules par défaut) | Maj pour une minuscule, ou Verr. Maj (Caps de l'EXL100) |
| Souris USB : la raquette va trop vite ou trop lentement | sensibilité inadaptée à la souris | changer la sensibilité dans EXLPICO, refaire un UF2 (ROM seule suffit) |
| Exelnoid avec souris USB : écran en partie brouillé, puis plantage (vu une fois) | cause non établie : lecture de cartouche ratée, pile d'Exelnoid, alimentation de la souris | firmware et Exelnoid à jour ; noter les circonstances (geste rapide, branchement de la souris, aussi sans souris ?) |
| Manette USB sans effet | manette Switch Pro, manette Xbox sans fil (sans son câble), adaptateur sans OTG, vidéo ou version disque | brancher la manette par son câble ; la LED bleue doit clignoter quand on appuie sur la croix (jeu sans zone d'échange) ; une manette à deux modes (D-input / X) peut essayer l'autre |
| Manette Xbox 360 : le voyant ne s'arrête pas sur « 1 » | la manette n'a pas été reconnue | essayer sans hub, ou avec un hub alimenté ; noter la marque et le modèle |
| Manette USB : les boutons ne donnent pas les touches attendues | numérotation des boutons propre à la marque | voir la table `PAD_KEYS` d'`exlpad.h` ([§6.4](#64-une-manette-usb)) |
| Souris USB non reconnue | adaptateur sans fonction OTG, souris trop gourmande, ou souris « non boot » | autre adaptateur, autre souris, ou hub alimenté |
| Vidéo : « IL FAUT LA CARTOUCHE EXELPICOCART » | firmware sans le mode vidéo (ancien, ou la sonde) | installer `badapple_carte_….uf2`, ou un UF2 « complet + vidéo » |
| Vidéo : « PAS DE VIDEO DANS LE PICO » | pas de vidéo à 8 Mo, copie interrompue, carte de moins de 16 Mo | recopier le fichier complet ou la vidéo seule |
| Version disque : « PAS DE FILM DANS LE PICO » | film absent, abîmé, ou effacé par une bibliothèque ; dans une bibliothèque, film non choisi dans la carte Version disque | refaire l'UF2 avec le film ; EXLPICO prévient quand une bibliothèque contient un lecteur sans film |
| Vidéo : image fausse ou figée | lecture de `V_FIFO` ratée ou doublée | relevé de la sonde avec le lecteur |
| Vidéo : pas de musique | `.exv` sans musique, ou ancien lecteur | vérifier dans EXLPICO « avec musique 1 bit » |
| Vidéo : musique hachée ou fausse | lectures de `S_MASK` / `S_REL` ratées | relevé de la sonde (lectures en `$7F20`-`$7F21`) |
| Le lecteur `RPI-RP2` n'apparaît pas | BOOT pas maintenu au branchement, câble « charge seule » | maintenir BOOT en branchant ; autre câble |
| La sonde ne répond pas | navigateur sans Web Serial, ou Pico branché avec BOOT | Chrome ou Edge ; brancher sans BOOT et attendre 5 s |

En cas de doute, la sonde reste l'outil de référence : elle dit quel mode est actif, ce que contient la flash, et ce que la console lit réellement, à 20 ns près.

---

## 13. Limites et pistes

- **Banques** : 6 au plus (112 Ko de ROM) sur la carte linéaire, 3 (64 Ko) sur la carte support EPROM, parce que chaque banque est une image complète en RAM. Un RP2350 (Pico 2, 520 Ko de RAM) en servirait environ deux fois plus. Pas de banques dans la bibliothèque.
- **Lecture seule** : pas d'émulation de RAM de cartouche.
- **Infrarouge** : seule la manette 1 est émise (la manette 2 utilise une autre porteuse, à 20 kHz) ; pas de pavé numérique. **Clavier USB** : disposition AZERTY française seulement ; les modificateurs partent en séquence, comme sur l'EXL100 (pas de combinaisons simultanées). **Manette USB** : une seule à la fois, par câble ; pas de Bluetooth, ni de manette Switch Pro.
- **Vidéo** : 320 × 240, 8 couleurs au plus, carte de 16 Mo. Dans la bibliothèque, seulement la version disque (noir et blanc). Le lecteur vidéo (le Pico décode) reste en ROM unique.
- **Version disque** : un film à la fois ; 14 Mo au plus (environ 5 minutes d'un film aussi agité que Bad Apple), moins la place des jeux dans une bibliothèque.
- **190 jeux** au maximum : c'est la limite de la liste de la zone `$7000–$7EFF` et du signal `$6F00 + n`.
- Les jeux démarrent dans l'état laissé par le menu ([§10.5](#105-démarrage-des-jeux--pourquoi-pas-trap-0)). Si un jeu en dépendait, un reset après la sélection le lance à froid.

