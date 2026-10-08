# EXLCART — cartouche multi-jeux pour Exelvision EXL100 / EXELTEL sur Raspberry Pi Pico

EXLCART remplace l'EPROM d'une cartouche EXL100 par un **Raspberry Pi Pico (RP2040)**. Le Pico répond au bus de la console exactement comme une EPROM de 32 Ko, et peut servir :

- **une ROM unique**, comme une cartouche classique ;
- **une bibliothèque** : un menu au démarrage, jusqu'à **190 jeux**, lancement par les flèches et ENTRÉE, retour au menu par **reset**.

Deux cartes sont prises en charge, avec la même vitesse de service, et toutes deux validées sur **EXL100** et **EXELTEL** :

- la carte **linéaire** d'origine, testée avec 19 ROM, dont Exelnoid et Exeltris ;
- la carte **support EPROM**, où le Pico est soudé presque directement sur l'empreinte d'une 27C256 (voir [§2](#2-matériel-et-câblage)). Elle a fonctionné du premier coup, et la sonde mesure le même temps de réponse que sur la carte linéaire (voir [§7.7](#77-la-carte-support-eprom--même-vitesse)).

Aucune ROM n'est compilée dans le firmware : les jeux sont rangés dans la flash du Pico par des fichiers `.uf2` produits par l'outil web **EXLPICO**. Changer de jeu ou de bibliothèque ne demande jamais de recompiler.

---

## Sommaire

1. [Contenu de l'archive](#1-contenu-de-larchive)
2. [Matériel et câblage](#2-matériel-et-câblage)
3. [Démarrage rapide](#3-démarrage-rapide)
4. [Le loader EXLMENU](#4-le-loader-exlmenu)
5. [L'outil EXLPICO](#5-loutil-exlpico)
6. [Compiler le firmware du Pico](#6-compiler-le-firmware-du-pico)
7. [Fonctionnement technique](#7-fonctionnement-technique)
8. [La sonde (diagnostic)](#8-la-sonde-diagnostic)
9. [Dépannage](#9-dépannage)
10. [Limites et pistes](#10-limites-et-pistes)

---

## 1. Contenu de l'archive

```
EXLCART/
├── LISEZMOI.md                     ce document
├── firmware/                       sources du firmware du Pico (C, Pico SDK)
│   ├── CMakeLists.txt              projet : cibles exlcart, exlsonde, exlcart_eprom, exlsonde_eprom
│   ├── pico_sdk_import.cmake       recherche du Pico SDK (fichier standard du SDK)
│   ├── exlcart.c                   firmware de la cartouche (point d'entrée)
│   ├── exlpins.h                   brochage des deux cartes, rangement des images en RAM
│   ├── exlserve.h                  service du bus, mode ROM unique
│   ├── exllib.h                    mode bibliothèque : flash, menu, chargement des jeux
│   ├── exlmulti.h                  machine d'états de la bibliothèque (menu / lancement / jeu)
│   ├── exlsonde.c                  firmware sonde : cartouche + relevé des accès, lu par USB
│   ├── exloscillo.pio              programme PIO de la sonde (« oscilloscope » 20 ns)
│   ├── test_multi.c                test sur PC de la machine d'états (gcc)
│   ├── test_pins.c                 test sur PC du brochage et du rangement des images (gcc)
│   └── uf2/
│       ├── firmware_seul_exlcart_SANS_ROM.uf2          cartouche, carte linéaire
│       ├── firmware_seul_exlsonde_SANS_ROM.uf2         sonde, carte linéaire
│       ├── firmware_seul_exlcart_eprom_SANS_ROM.uf2    cartouche, carte support EPROM
│       └── firmware_seul_exlsonde_eprom_SANS_ROM.uf2   sonde, carte support EPROM
├── loader/
│   └── exlmenu.asm                 le menu de la bibliothèque (assembleur TMS7020)
└── outils/
    ├── exlpico.html                outil web : ROM → .uf2, bibliothèque, sonde
    └── integrer_firmware.py        intègre un firmware recompilé dans exlpico.html
```

Les quatre firmwares précompilés sont **ceux qui sont intégrés dans `exlpico.html`**. EXLPICO choisit lui-même le bon d'après la carte sélectionnée.

---

## 2. Matériel et câblage

### Carte

Il faut une carte RP2040 qui **sort la broche GP23** : c'est elle qui reçoit `/ROM_CS`. Sur le Raspberry Pi Pico officiel, GP23 n'est pas accessible (elle pilote son alimentation). La carte utilisée est un **« Purple Pico » YD-RP2040, 16 Mo de flash**. Sur cette carte, GP23 commande aussi la LED RGB : elle change parfois de couleur pendant le jeu, sans aucune conséquence.

Le mode bibliothèque range les jeux **à partir de 2 Mo** dans la flash : il faut une carte d'au moins 4 Mo. Avec 16 Mo, les 190 jeux tiennent largement (ils s'arrêtent vers 8 Mo).

### Brochage : deux cartes possibles

Tout est branché **en direct**, sans pont diviseur ni résistance. Aucune broche ADC (GP26–GP29) n'est utilisée. Dans les deux cas : **/ROM_CS → GP23**, **+5 V → VBUS**, **masse → GND**. Sur la cartouche, /ROM_CS est relié à la fois à /CE et à /OE de l'EPROM.

**Carte linéaire** (la carte d'origine) :

| Signal | Pico |
|---|---|
| A0 … A14 | GP0 … GP14 |
| D0 … D7 | GP15 … GP22 |

**Carte support EPROM** : le Pico est soudé presque directement sur l'empreinte d'une 27C256 (28 broches). Seules quelques broches sont reroutées. Cette carte est validée sur EXL100 et EXELTEL.

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

GP17 et GP25 (LED bleue) restent **non connectées**. Sur la plupart des YD-RP2040, GP24 est aussi relié au bouton **USR** : ne pas l'appuyer quand la cartouche est dans la console, il mettrait A10 à la masse.

Le firmware se compile pour l'une ou l'autre carte, et EXLPICO met le bon dans les fichiers qu'il produit. **Un firmware installé sur la mauvaise carte ne sert rien de cohérent** : la console ne voit alors pas de cartouche. Comment la carte support EPROM garde la même vitesse : voir [§7.7](#77-la-carte-support-eprom--même-vitesse).

Le RP2040 n'est pas officiellement tolérant au 5 V, mais ses broches numériques non-ADC le supportent en pratique. Le même choix est fait par d'autres cartouches à base de Pico (A8PicoCart pour l'Atari 8 bits). Les données 3,3 V du Pico sont lues correctement par la console : c'est vérifié à l'oscilloscope de la sonde.

### Précautions

- **Ne jamais brancher l'USB quand le Pico est dans la console.** Les deux alimentations 5 V se retrouveraient reliées.
- Insérer et retirer la cartouche **console éteinte**.
- Pour programmer le Pico : le sortir de la console, maintenir **BOOT** (BOOTSEL), brancher l'USB, relâcher. Un lecteur `RPI-RP2` apparaît : on y copie le fichier `.uf2`. Le Pico redémarre tout seul à la fin de la copie. On peut alors le débrancher et le remettre dans la console.

---

## 3. Démarrage rapide

Ouvrir `outils/exlpico.html` dans un navigateur (Chrome ou Edge recommandés : la lecture de la sonde utilise Web Serial). Tout fonctionne hors ligne.

**Commencer par choisir la carte**, en haut de la page (carte **Carte (brochage)**) : linéaire ou support EPROM. Le choix est mémorisé par le navigateur. Les fichiers qui contiennent un firmware (« complet », « sonde ») prennent alors celui de cette carte, et leur nom se termine par `_eprom` pour la carte support EPROM.

### Une ROM unique

1. Carte **ROM** : **Choisir la ROM…**
2. Carte **Fichier .uf2** :
   - **UF2 complet (firmware + ROM)** la première fois, ou pour remettre le firmware ;
   - **UF2 ROM seule** pour changer de jeu ensuite (installation plus rapide).
3. Copier le `.uf2` sur le Pico (BOOT + USB), débrancher, insérer, allumer.

### Une bibliothèque

1. Assembler le loader (voir [§4](#4-le-loader-exlmenu)) : on obtient `exlmenu.rom`.
2. Carte **Bibliothèque de jeux (menu)** :
   - **Loader (menu)…** : choisir `exlmenu.rom` ;
   - **Ajouter des jeux…** : choisir les ROM (plusieurs à la fois).
   - Les noms sont tirés des noms de fichiers (20 caractères, majuscules). On peut les modifier, réordonner les jeux avec ↑ ↓, en retirer avec ×, ou **Trier par nom**.
3. **UF2 bibliothèque complète (firmware + menu + jeux)** : à utiliser après toute mise à jour du firmware ou d'EXLPICO. **UF2 bibliothèque seule** : pour changer seulement la liste des jeux.
4. Copier sur le Pico, insérer, allumer : le menu s'affiche.

### Utilisation du menu

| Touche | Action |
|---|---|
| ↑ ↓ | jeu précédent / suivant (la liste boucle) |
| ← → | page précédente / suivante (20 jeux par page) |
| ESPACE ou ENTRÉE | lancer le jeu (il démarre au **relâchement** de la touche) |
| **Reset** pendant une partie | retour au menu |

Si un jeu ne démarrait pas après sa sélection, un **reset** le lance quand même : le Pico est déjà basculé sur lui.

---

## 4. Le loader EXLMENU

`loader/exlmenu.asm` est le menu affiché par la bibliothèque. C'est un programme de cartouche ordinaire, assemblé comme un jeu.

### Compilation

Mettre dans le même dossier que `exlmenu.asm` les fichiers de votre kit de développement EXL100 : **`7020.equ`**, **`3556.equ`** et **`mixt_api.asm`**. Ils ne sont pas fournis dans cette archive. Puis :

```
tasm -tEXL -a -b exlmenu.asm
obj2exl.exe exlmenu.obj exlmenu.rom -t:ROM -r:0x1000 -p
```

Donner ensuite `exlmenu.rom` à EXLPICO comme **loader**.

### Règles à respecter si vous modifiez le loader

- **Ne rien assembler entre `$6F00` et `$7EFF`.** C'est la zone d'échange avec le Pico. EXLPICO refuse un loader qui l'utilise.
  - `$7000` : nombre de jeux, puis à partir de `$7010`, 20 caractères par jeu. Le Pico **insère** cette liste dans l'image du menu au démarrage.
  - `$6F00 + n` : lire cette adresse **choisit le jeu n**. Le Pico le charge en arrière-plan (environ 1 ms).
  - `$6FFF` : lire cette adresse **bascule la cartouche sur le jeu** : l'accès suivant lit déjà le jeu.
- Variables du menu en RAM externe à `$C500`, routine de lancement copiée en `$C510`.
- Affichage repris de mixpix : 25 lignes de texte, police 5×7 chargée dans BAGC3. Clavier repris d'Exeltris (`VALUE0`).

### Séquence de lancement

1. Lecture de `$6F00 + n` : le Pico charge le jeu n.
2. Attente du **relâchement de toutes les touches** : 8 lectures consécutives « aucune touche », espacées d'environ 15 ms. Un rebond relance l'attente. Puis encore environ 150 ms.
3. Copie en RAM (`$C510`) d'une routine de 6 octets, puis saut vers elle :
   ```
   $8A $6F $FF    LDA @$6FFF   ; le Pico bascule sur le jeu
   $8C $7F $FD    BR  @$7FFD   ; exécute l'instruction de démarrage du jeu
   ```
4. Le jeu démarre exactement comme si la ROM interne l'avait lancé.

Pourquoi ce démarrage direct plutôt qu'un `TRAP 0` : voir [§7.5](#75-démarrage-des-jeux--pourquoi-pas-trap-0).

---

## 5. L'outil EXLPICO

`outils/exlpico.html` est une page autonome. Toutes ses fonctions tournent dans le navigateur, sans serveur.

| Carte | Rôle |
|---|---|
| **Carte (brochage)** | linéaire ou support EPROM : choisit le firmware mis dans les fichiers « complet » et « sonde » |
| **ROM** | charge une ROM et l'analyse : organisation, pied `AA/55 8C`, point d'entrée, somme de contrôle |
| **Réglage du service (essais)** | délai et maintien « façon EPROM ». **Laisser 0 / 0** : c'est la réponse immédiate, validée sur les deux machines |
| **Fichier .uf2** | UF2 complet (firmware + ROM) ou UF2 ROM seule |
| **Bibliothèque de jeux (menu)** | loader, liste des jeux, UF2 bibliothèque complète ou seule |
| **Sonde (diagnostic)** | UF2 sonde, lecture du relevé par USB, fichier de test de la flash (voir [§8](#8-la-sonde-diagnostic)) |
| **Installer sur le Pico** | rappel de la procédure |

### ROM acceptées

EXLPICO repère l'organisation du fichier d'après la position du pied `AA 8C` ou `55 8C` :

| Fichier | Traitement |
|---|---|
| 32 768 octets, image depuis `$0000` | placé tel quel |
| 32 256 octets, image depuis `$0200` | décalé à sa place |
| 16 Ko ou 8 Ko | recopié dans tout l'espace (A13 et A14 sont ignorées par une petite EPROM) |
| sans pied reconnu | refusé, avec un diagnostic (positions trouvées, 8 derniers octets) |

Deux signatures existent : **`AA 8C`** pour les cartouches officielles, **`55 8C`** pour les ROM produites par obj2exl. Les deux sont acceptées partout.

### Installer une ROM seule après une bibliothèque

Les UF2 « ROM unique » écrivent aussi un bloc de zéros sur l'en-tête de la bibliothèque. Le firmware sert alors la ROM unique. Les jeux de la bibliothèque restent dans la flash, mais ne sont plus utilisés tant qu'une bibliothèque n'est pas réinstallée.

---

## 6. Compiler le firmware du Pico

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

Sous Debian ou Ubuntu, le compilateur s'installe avec `sudo apt install gcc-arm-none-eabi cmake build-essential`.

### Windows

Le plus simple est l'extension **Raspberry Pi Pico** de VS Code, qui installe le SDK et le compilateur. On peut aussi utiliser l'installateur « Pico setup for Windows ». Dans le terminal fourni (« Developer Command Prompt » ou « Pico – Developer PowerShell ») :

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
| `build/exlcart.uf2` | cartouche, carte linéaire (équivalent de `firmware_seul_exlcart_SANS_ROM.uf2`) |
| `build/exlsonde.uf2` | sonde, carte linéaire |
| `build/exlcart_eprom.uf2` | cartouche, carte support EPROM |
| `build/exlsonde_eprom.uf2` | sonde, carte support EPROM |

Le brochage d'une carte est entièrement décrit dans `exlpins.h`. La variante support EPROM est compilée avec la définition `EXL_PINOUT_EPROM`.

La carte cible est la carte `pico` par défaut du SDK. Elle convient au Purple Pico 16 Mo : son chargeur de démarrage (boot2 W25Q080) fonctionne avec la puce de cette carte, et l'accès à la flash au-delà de 2 Mo est vérifié (test de la flash, [§8](#8-la-sonde-diagnostic)).

Les cibles `exlcart_test` et `exlcart_eprom_test` sont des variantes à un seul cœur, réservées aux essais en émulateur (rp2040js). Elles ne sont pas faites pour la console.

### Utiliser un firmware recompilé

Un `.uf2` de firmware seul ne touche pas aux ROM ni à la bibliothèque déjà dans la flash. On peut donc :

- **soit** installer `build/exlcart.uf2` sur le Pico, puis, si besoin, un « UF2 ROM seule » ou « UF2 bibliothèque seule » ;
- **soit** intégrer le nouveau firmware dans EXLPICO, pour que les boutons « complet » l'utilisent :
  ```sh
  python3 outils/integrer_firmware.py firmware/build/exlcart.uf2 firmware/build/exlsonde.uf2 \
      firmware/build/exlcart_eprom.uf2 firmware/build/exlsonde_eprom.uf2
  ```
  Chaque fichier est reconnu à son nom. On peut n'en donner que certains.
  Une sauvegarde `exlpico.html.bak` est faite automatiquement.

### Tests sur PC

```sh
cd firmware
gcc -O2 -Wall -o test_multi test_multi.c && ./test_multi
gcc -O2 -Wall -o test_pins test_pins.c && ./test_pins
# les memes pour la carte support EPROM :
gcc -O2 -Wall -DEXL_PINOUT_EPROM -o test_multi_eprom test_multi.c && ./test_multi_eprom
gcc -O2 -Wall -DEXL_PINOUT_EPROM -o test_pins_eprom test_pins.c && ./test_pins_eprom
```

- `test_multi` : 24 scénarios de la machine d'états (choix d'un jeu, lancement par `$6FFF` et par reset, retour au menu, jeu pas encore chargé, reset trop tôt…).
- `test_pins` : le brochage (chaque signal une fois, aucune broche interdite), la correspondance adresse ↔ case pour les 32 768 adresses, puis 4 × 32 768 accès simulés comme dans la boucle de service. Ces accès se font avec du bruit sur toutes les broches qui ne sont pas des adresses : la console doit toujours lire le bon octet, et rien ne doit être écrit hors des 8 broches de données.

---

## 7. Fonctionnement technique

### 7.1 Carte de la flash du Pico

| Adresse flash | Contenu |
|---|---|
| `0x000000` | firmware (environ 12 Ko pour la cartouche, 37 Ko pour la sonde) |
| `0x100000` | ROM unique (32 Ko) |
| `0x108000` | bloc de réglages `EXLC` (délai, maintien) |
| `0x180000` | relevé de la sonde (64 Ko) |
| `0x200000` | en-tête de bibliothèque `EXLB`, puis à +256 la liste des jeux (3 840 octets) |
| `0x201000` | le menu (loader), 32 Ko |
| `0x209000 + n × 0x8000` | le jeu n (n = 0 à 189) |

En-tête de bibliothèque, petit-boutiste : signature `EXLB` (`0x424C5845`), version (16 bits), nombre de jeux (16 bits), puis les décalages du menu, du premier jeu, et le pas entre deux jeux (32 bits chacun).

### 7.2 Les fichiers UF2 sont écrits secteur par secteur

La ROM de démarrage du RP2040 efface la flash par secteurs de 4 Ko, et elle choisit le secteur à effacer d'après le **numéro du bloc** dans le fichier UF2, pas d'après son adresse. Un fichier qui mélange plusieurs zones non alignées voit donc certaines de ses données effacées par le bloc suivant. EXLPICO émet donc toujours des **secteurs complets** de 16 blocs de 256 octets, complétés par `FF`. Si vous fabriquez vos propres UF2, respectez cette règle.

### 7.3 Le service du bus

La console met `/ROM_CS` à 0 pendant environ **400 ns** par accès, et lit l'octet environ **150 ns** après le début. Mesures faites avec la sonde :

| Boucle | Octet sur le bus après le début de l'accès |
|---|---|
| ROM unique, et bibliothèque actuelle | **~105 ns** : fonctionne |
| première version de la bibliothèque (contrôles d'état **avant** l'octet) | 205–265 ns : la console ne reconnaît pas le pied |

La boucle de service tourne en RAM, à 200 MHz, interruptions coupées :

- **au repos** : lire les broches, tester `/ROM_CS`, recommencer (3 instructions) ;
- **pendant l'accès** : masquer l'adresse, lire l'octet en RAM, le décaler, l'écrire, activer les sorties (6 instructions) ;
- **après l'accès** (bus relâché, au moins 400 ns avant le suivant) : toute la logique de la bibliothèque, avec la durée des pauses mesurée par le minuteur matériel.

En mode bibliothèque, le **cœur 1** sert le bus en permanence, et le **cœur 0** copie le jeu choisi de la flash vers la RAM. Le cœur 1 ne lit jamais la flash : le chargement ne ralentit donc pas le service.

### 7.4 La machine d'états de la bibliothèque

| État | Image servie | Passage à l'état suivant |
|---|---|---|
| **MENU** | le menu, liste insérée en `$7000` | lecture de `$6F00 + n` (n < nombre de jeux) → le cœur 0 charge le jeu n → **LAUNCH** |
| **LAUNCH** | le menu | lecture de `$6FFF` **ou** du pied `$7FFC`, jeu chargé → **GAME** |
| **GAME** | le jeu | lecture du pied `$7FFC` après une pause de plus de 2 ms, une fois le jeu lancé (au moins 1 000 accès) → **MENU** |

Les adresses de signal ne sont actives que dans l'état concerné. Un jeu peut donc utiliser librement toute la plage `$0000–$7FFF`.

**Le pied est préparé d'avance.** Aucune décision n'est prise pendant un accès : la boucle sert toujours l'image courante.
- Dès que le jeu est chargé, le cœur 0 écrit **sa** signature (`AA` ou `55`) dans le pied du menu. Si la console redémarre, elle lit donc la signature du jeu.
- Pendant la partie, la signature **du menu** est écrite dans le pied du jeu. Après un reset, la console lit donc celle du menu.

### 7.5 Démarrage des jeux : pourquoi pas TRAP 0

Le pied d'une cartouche est une **instruction** : `55 8C 10 00`, c'est la signature `55` suivie de `8C 10 00`, soit `BR @$1000`. Au démarrage, la ROM interne vérifie la signature en `$7FFC`, puis exécute l'instruction en `$7FFD`.

`TRAP 0` relance la ROM interne sans faire un vrai reset de la machine. Sur l'EXELTEL, la ROM prend alors son chemin de redémarrage « à chaud » : elle affiche son **écran rouge** et attend une touche avant de démarrer la cartouche. Le loader démarre donc le jeu **lui-même**, comme le ferait la ROM : signal `$6FFF`, puis `BR @$7FFD`, depuis la RAM.

Conséquence : le jeu démarre dans l'état laissé par le menu (mode vidéo, police, interruptions actives), et non dans celui d'un démarrage à froid. Les jeux qui initialisent eux-mêmes leur affichage n'y voient aucune différence. Les 19 ROM testées démarrent ainsi.

### 7.6 Mode ROM unique

Sans en-tête `EXLB` valide à 2 Mo, le firmware copie la ROM rangée à 1 Mo en RAM, puis la sert avec la boucle rapide, sur le cœur 0. Les réglages délai et maintien (bloc `EXLC`) ne servent qu'aux essais.

### 7.7 La carte support EPROM : même vitesse

Sur cette carte, les lignes ne sont plus dans l'ordre : A10 est sur GP24, et il y a un trou en GP17 au milieu des données. Plutôt que de remettre les bits dans l'ordre à chaque accès, ce qui coûterait du temps, on **range l'image en RAM dans l'ordre des broches**. Tout le travail est fait une seule fois, à la copie flash → RAM (`img_load` dans `exlpins.h`).

- **L'adresse** : chaque cœur du RP2040 a deux **interpolateurs**, des circuits faits pour combiner des champs de bits. Celui du cœur qui sert est réglé ainsi :
  - voie 0 = GP0–GP13 ;
  - voie 1, qui lit la même valeur (« entrée croisée »), = GP24 placé en bit 14 ;
  - BASE2 = adresse de l'image / 2.

  On écrit l'état des broches dans l'interpolateur, on relit son résultat, et une seule instruction `ldrh rX, [rh, rh]` lit la case. Ça remplace les deux décalages de la carte linéaire.
- **La donnée** : chaque case fait 16 bits et contient déjà les bits à sortir sur GP14–GP22 (GP17 à 0). La sortie reste un simple décalage.

| | Carte linéaire | Carte support EPROM |
|---|---|---|
| Accès (après la détection) | `lsls`, `lsrs`, `ldrb`, `lsls`, `str`, `str` | `str`, `ldr`, `ldrh`, `lsls`, `str`, `str` |
| Cycles | identiques | identiques |
| Taille d'une image en RAM | 32 Ko | 64 Ko |
| RAM utilisée (cartouche / sonde) | 69 Ko / 137 Ko | 137 Ko / 205 Ko, sur 264 Ko |

Les adresses de signal (`$6F00+n`, `$6FFF`, `$7FFC`) sont comparées après l'accès, une fois l'index remis dans l'ordre par deux petites tables. Les ROM, les fichiers UF2, la bibliothèque et le loader sont les mêmes pour les deux cartes : seule la copie en RAM est rangée différemment.

La copie permutée rallonge un peu le démarrage (quelques dizaines de ms en bibliothèque) et le chargement d'un jeu (quelques ms). La console ne lit le pied qu'environ 300 ms après l'allumage, et le loader attend au moins 270 ms avant de lancer le jeu : la marge reste large.

**Validation.** La carte support EPROM a d'abord été testée dans l'émulateur RP2040, avec le vrai firmware compilé et l'interpolateur simulé, dans les deux modes. Elle a ensuite fonctionné du premier coup sur EXL100 et sur EXELTEL.

Un relevé de la sonde sur EXL100, avec Exeltris en ROM unique, confirme la vitesse :

| Accès | Octet | Stable dès |
|---|---|---|
| `$7FFC` (pied) | `$55` | 105 ns |
| `$7FFD` | `$8C` | 85 ns |
| `$7FFF` | `$00` | 85 ns |
| `$1000` (entrée) | `$06` | 105 ns |
| `$1001` | `$52` | 85 ns |
| `$1003` | `$0D` | 125 ns |

- **L'octet est sur le bus en 85 à 125 ns**, comme les ~105 ns de la carte linéaire. L'écart d'un accès à l'autre vient de la résolution de 20 ns de la sonde, et du moment où l'accès tombe dans la boucle d'attente. La console lit l'octet vers 150 ns, et un service trop lent n'échouait qu'au-delà d'environ 200 ns : la marge est confortable.
- **`/ROM_CS` reste à 0 environ 425 ns** par accès, comme sur la carte linéaire.
- **La somme de l'image servie** est identique à celle de la ROM dans la flash. La copie permutée en RAM, une fois redécodée, est donc exactement la ROM.

---

## 8. La sonde (diagnostic)

Le firmware **EXLSONDE** sert la cartouche exactement comme EXLCART : mêmes ROM, même bibliothèque, même boucle. En plus, il **enregistre les accès** de la console :

- à chaque descente de `/ROM_CS`, un programme PIO prend **64 instantanés de toutes les broches**, toutes les **20 ns** ;
- le relevé est enregistré dans la flash après environ 1 s de calme ;
- on le relit plus tard par USB, Pico sorti de la console.

### Utilisation

1. Installer `firmware_seul_exlsonde_SANS_ROM.uf2` (carte linéaire) ou `firmware_seul_exlsonde_eprom_SANS_ROM.uf2` (carte support EPROM). Il ne touche ni à la ROM, ni à la bibliothèque. Autre possibilité : **UF2 sonde (firmware sonde + ROM)** dans EXLPICO, avec la bonne carte choisie.
2. Insérer le Pico, allumer la console, faire l'essai, éteindre.
3. Pico sorti de la console : le brancher en USB **sans** BOOT, attendre 5 secondes, puis cliquer **Lire le relevé (USB)** dans EXLPICO (Chrome ou Edge). On peut l'enregistrer avec **Télécharger le relevé (.txt)**.
4. Pour revenir à la cartouche normale : réinstaller le firmware cartouche de votre carte (`firmware_seul_exlcart_SANS_ROM.uf2` ou `firmware_seul_exlcart_eprom_SANS_ROM.uf2`).

### Lignes du relevé

| Ligne | Contenu |
|---|---|
| `EXLPINS` | le brochage du firmware sonde : `lineaire` ou `eprom`. EXLPICO s'en sert pour décoder le relevé, et prévient s'il ne correspond pas à la carte choisie |
| `EXLROM` | somme de la ROM unique dans la flash, son pied, les 8 octets au point d'entrée |
| `EXLLIB` | en-tête de bibliothèque (signature, version et nombre, emplacements), identifiant JEDEC de la puce flash |
| `EXLSR` | registres d'état de la puce flash. EXLPICO signale une éventuelle protection en écriture (bits BP/CMP) |
| `EXLMAP` | les repères du fichier de test de la flash |
| `EXLMENU` | le menu tel qu'il est dans la flash : somme, pied, point d'entrée (seulement si une bibliothèque est présente) |
| `EXLSONDE 4 n …` | en-tête du relevé (nombre de mots, horloge, somme de l'image **servie**, réglages), puis 64 mots par accès |

Chaque mot de l'oscilloscope contient l'état des broches GP0–GP31. Avec la carte linéaire : l'adresse en bits 0–14, la donnée en bits 15–22, et `/ROM_CS` en bit 23. Avec la carte support EPROM, chaque ligne est à sa broche (voir [§2](#2-matériel-et-câblage)). EXLPICO décode tout ça en clair, selon la ligne `EXLPINS` : adresses lues, octet présenté et à quel instant, durée de l'accès.

La **somme de l'image servie** permet de savoir quel mode était actif. En mode bibliothèque, elle vaut la somme du loader, plus celle de la liste insérée.

Deux particularités à connaître pour lire un relevé :

- **Des accès peuvent manquer.** Après chaque descente de `/ROM_CS`, la sonde enregistre pendant environ 1,3 µs. Un accès qui commence dans cette fenêtre n'est pas capturé : c'est le cas, par exemple, de `$7FFE` entre `$7FFD` et `$7FFF`. On le reconnaît à l'octet de l'accès manquant, encore présent sur le bus au début de l'accès suivant.
- **GP25 (LED bleue du Purple Pico, non connectée)** passe à 1 en même temps que `/ROM_CS` remonte, probablement par couplage avec la LED. Le firmware ne lit pas cette broche : c'est sans effet.

### Fichier de test de la flash

**UF2 test de la flash** écrit un repère (`EXT0` à `EXT4`) à 1,94 Mo, 2 Mo, 4 Mo, 8 Mo et 15 Mo. On installe ensuite le firmware sonde, et la ligne `EXLMAP` montre quels repères sont arrivés : c'est un contrôle de toute la flash.

**Attention** : ce test écrit par-dessus la bibliothèque. Réinstallez-la ensuite.

---

## 9. Dépannage

| Symptôme | Cause probable | Solution |
|---|---|---|
| Écran de la ROM interne : la cartouche n'est pas vue | Pico mal inséré, ROM sans pied valide, ou **firmware de l'autre carte** | vérifier l'insertion, l'analyse de la ROM et le choix de la carte en haut d'EXLPICO ; réinstaller un fichier « complet » ; faire un relevé avec la sonde |
| Menu affiché, mais « AUCUN JEU : UTILISER EXLPICO » | le loader est installé comme ROM unique, sans bibliothèque | installer un UF2 bibliothèque |
| Après le choix d'un jeu, le menu revient | firmware trop ancien, qui ne connaît pas le signal `$6FFF` | installer l'**UF2 bibliothèque complète** (il contient le firmware) |
| Après le choix d'un jeu : écran rouge (EXELTEL) ou noir (EXL100), puis un reset lance le jeu | loader ancien, qui redémarre par `TRAP 0` | réassembler `exlmenu.asm` (version actuelle) et réinstaller la bibliothèque |
| La bibliothèque a disparu | une ROM seule, ou le test de la flash, a été installé après elle | réinstaller la bibliothèque |
| Le lecteur `RPI-RP2` n'apparaît pas | BOOT pas maintenu au branchement, ou câble USB « charge seule » | maintenir BOOT en branchant ; essayer un autre câble |
| La sonde ne répond pas | pas de port série (navigateur sans Web Serial), ou Pico branché avec BOOT | utiliser Chrome ou Edge ; brancher sans BOOT et attendre 5 s |

En cas de doute, la sonde reste l'outil de référence : elle dit quel mode est actif, ce que contient la flash, et ce que la console lit réellement, à 20 ns près.

---

## 10. Limites et pistes

- **Jeux de 32 Ko au maximum**, sans changement de banque. Le RP2040 a assez de RAM (264 Ko) pour en ajouter un dans le firmware plus tard.
- **Lecture seule** : pas d'émulation de RAM de cartouche.
- **190 jeux** au maximum : c'est la limite de la liste de la zone `$7000–$7EFF` et du signal `$6F00 + n`.
- Les jeux démarrent dans l'état laissé par le menu ([§7.5](#75-démarrage-des-jeux--pourquoi-pas-trap-0)). Si un jeu en dépendait, un reset après la sélection le lance à froid.
