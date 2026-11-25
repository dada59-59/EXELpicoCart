; ============================================================================
; EXLMENU -- loader de la bibliotheque de jeux de la cartouche Pico (EXLCART)
; pour Exelvision EXL100 / EXELTEL, assembleur TMS7020.
;
; Assembler (avec 7020.equ et 3556.equ dans le meme dossier ; mixt_api.asm
; n'est plus necessaire depuis la v3.4, son init_vdp est recopie dans start) :
;     tasm -tEXL -a -b exlmenu.asm
;     obj2exl.exe exlmenu.obj exlmenu.rom -t:ROM -r:0x1000 -p
; puis donner exlmenu.rom a EXLPICO (carte "Bibliotheque") comme "loader".
;
; Fonctionnement :
;   - le Pico sert ce loader au demarrage, et y a INSERE la liste des jeux en
;     $7000-$7EFF (ne rien placer dans cette zone) ;
;   - fleches pour choisir, ESPACE ou ENTREE pour lancer (le lancement part
;     quand la touche est RELACHEE) ; aussi a la manette infrarouge (memes
;     codes que les fleches), a la manette DB9 et a la souris USB d'EXLCART
;     (zone d'echange "EXLP" plus bas : haut/bas/gauche/droite/feu, souris
;     haut/bas pour choisir, un clic pour lancer) ;
;   - le lancement est une simple LECTURE en $6F00+n (zone reservee elle
;     aussi) : le Pico charge le jeu n pendant que ce loader attend ; puis une
;     petite routine copiee en RAM lit $6FFF (le Pico bascule sur le jeu) et
;     execute BR @$7FFD, l'instruction de demarrage du pied du jeu, comme le
;     fait la ROM interne (pas de TRAP 0 : l'EXELTEL s'y arrete sur son ecran) ;
;   - un reset pendant une partie ramene a ce menu.
;
; EXELMEMOIRE (v4, 09/11/2026) : la liste peut aussi contenir des IMAGES
; EXELMEMOIRE (16 Ko, $8000-$BFFF : fichiers .cram des emulateurs ou .BKP de
; l'Exelmemoire, convertis par EXLPICO). Elles sont marquees par le bit 7 du
; 1er caractere de leur nom et affichees en CYAN. ENTREE sur l'une d'elles :
;   - confirmation (le contenu de l'Exelmemoire sera efface) ;
;   - lecture en $6E00+n : le Pico copie l'image dans la fenetre $2800-$67FF de
;     CE loader, puis ecrit $A5 en $6DFF (ne rien placer de $2800 a $67FF ni en
;     $6DFF-$6EFF) ;
;   - le loader recopie la fenetre en $8000-$BFFF (interruptions coupees,
;     connecteur valide par une lecture de P49 et P50, comme Exelnoid), relit
;     tout, et propose de demarrer l'image si elle a le demarrage automatique
;     ($AA en $8000) : meme menage qu'avant un jeu, puis BR @$8001, comme la
;     ROM interne. Un reset ramene ensuite a ce menu (la cartouche passe avant
;     l'Exelmemoire au demarrage). Lecture de $6DFE juste avant : le Pico relaie
;     alors la manette par infrarouge (comme pour un jeu sans zone d'echange) ;
;   - ou "S" (et ENTREE si l'image n'a pas le demarrage automatique) : SANS
;     CARTOUCHE. Lecture de $6DFD : le Pico met $FF dans le pied ; l'utilisateur
;     appuie sur RESET, la console demarre comme sans cartouche (image a demarrage
;     automatique, ou menu de la console : Exelquad...), manette relayee en IR ;
;     le reset suivant ramene a ce menu. Une touche avant le reset : $6DFC, annule.
;     (v4.1, 09/11/2026 : Quizzy, Burger Time, relais infrarouge)
;
; Affichage (v5, 10/11/2026) : page MIXTE, de haut en bas
;   - un BANDEAU CYAN : une ligne de TEXTE vide (le mode mixte doit commencer
;     par du texte) puis 50 lignes BITMAP, le PAPILLON d'Exelvision en haut a
;     gauche et "EXELpicoCart" en noir, a peu pres centre ; marges cyan ;
;   - la LISTE des jeux sur fond noir : 15 lignes de 10 points en BITMAP, sur
;     toute la largeur (40 colonnes), dessinees avec la police 5x7 ;
;   - une ligne vide (10 lignes bitmap noires) ;
;   - la LIGNE D'ETAT : 3 lignes de TEXTE noir sur fond cyan (nombre de jeux et
;     page, puis aide ou messages), police 5x7 chargee par trap 19 dans BAGC3
;     comme dans mixpix (valide sur machine).
;   (v2-v4.1 : titre et aide en texte, liste a gauche, panneau cyan a droite)
;   - Le papillon (logo_bmp, en fin de fichier) est celui de l'ecran d'allumage
;     de l'EXL100 : sur cette machine, le processeur d'E/S (7041) l'envoie a la
;     ROM (commande $0D), qui le dessine (trap 11). L'EXELTEL ne l'a pas : le
;     dessin est donc range ICI, deja converti en bitmap, et simplement copie
;     dans la memoire video -- meme rendu sur EXL100 et sur EXELTEL (v3).
; SAUVEGARDE DE L'EXELMEMOIRE (v5.1, 10/11/2026) : si une Exelmemoire repond
; (essai en $BFFF : $55 puis $AA ecrits et relus, l'octet remis), l'aide propose
; M. M, puis ENTREE : le menu envoie les 16 Ko au Pico, qui les range dans sa
; flash (8 emplacements) ; EXLPICO les relit ensuite par USB, en .cram et .BKP.
; La console ne peut pas ecrire dans la cartouche : chaque octet v est envoye
; par une LECTURE en $6C00+v (le Pico lit l'octet sur le bus d'adresses) ;
; $6BF0 = debut, $6BF1 = fin ; 16 384 octets de $BFFF a $8000, puis leur somme
; (16 bits) ; reponse du Pico en $6BF2 ($A5 rangee, $E1 transmission fausse,
; $E2 flash), emplacement en $6BF3. (Ne rien placer de $6BF0 a $6CFF.)
; CASSETTES K7 EN JAUNE (v5.2) : EXLPICO met le bit 7 du 2e caractere du nom
; des entrees cassette (le 1er marque les images Exelmemoire, en cyan).
; MISE EN GARDE USB (v5.2, 10/11/2026) : deux lignes sous le titre, dans le
; bandeau : "USB : CLAVIER, MANETTE, SOURIS" (noir) et "JAMAIS DE PC NI DE
; CHARGEUR !" (rouge). Cartouche en place, un cable USB qui alimente le Pico
; (PC, chargeur) se bat avec le 5 V de la console.
; Clavier repris d'Exeltris : VALUE0, "aucune touche" = $04 ou $00.
; ============================================================================

#include "7020.equ"
#include "3556.equ"

; --- zones partagees avec le Pico (ne rien y assembler) ---------------------
CAT_COUNT       .equ    $7000           ; nombre de jeux (0..190)
CAT_NAMES       .equ    $7010           ; 20 caracteres par jeu, sans 0 final
NAME_LEN        .equ    20
SEL_HOTSPOT     .equ    $6F00           ; lire $6F00+n = lancer le jeu n
GO_HOTSPOT      .equ    $6FFF           ; lire $6FFF = le Pico bascule sur le jeu
CRAM_REQ        .equ    $6E00           ; lire $6E00+n = le Pico copie l'image Exelmemoire n
CRAM_STAT       .equ    $6DFF           ; ... dans la fenetre, puis y ecrit CRAM_READY
CRAM_READY      .equ    $A5
CRAM_GO         .equ    $6DFE           ; lire : "image demarree" (le Pico relaie la manette en IR)
NOCART          .equ    $6DFD           ; lire : le Pico cache la cartouche jusqu'au prochain reset
NOCART_BACK     .equ    $6DFC           ; lire : annulation, la cartouche revient
CRAM_WIN        .equ    $2800           ; fenetre de 16 Ko : $2800-$67FF (le code finit avant $2800)
CRAM_BASE       .equ    $8000           ; l'Exelmemoire : $8000-$BFFF
CRAM_BASE_HI    .equ    $80
CRAM_FLAG       .equ    $80             ; bit 7 du 1er caractere du nom : image Exelmemoire
K7_FLAG         .equ    $80             ; bit 7 du 2e caractere du nom : cassette K7 (v5.2)
SAVE_GO         .equ    $6BF0           ; lire : debut de la sauvegarde (v5.1)
SAVE_END        .equ    $6BF1           ; lire : fin
SAVE_STAT       .equ    $6BF2           ; reponse du Pico
SAVE_SLOT       .equ    $6BF3           ; emplacement utilise (1-8)
SAVE_WIN        .equ    $6C00           ; lire $6C00+v : envoie l'octet v
SAVE_OK         .equ    $A5             ; rangee dans la flash
SAVE_BAD        .equ    $E1             ; transmission fausse (nombre, somme)
SAVE_FLASH      .equ    $E2             ; ecriture de la flash fausse
CRAM_TOP        .equ    $BFFF           ; dernier octet de l'Exelmemoire
TRAMP_RAM       .equ    $C7F9           ; routine de saut, copiee en fin de RAM
TRAMP_LEN       .equ    7               ; ses 7 octets (voir tramp_src)

; --- ecran -----------------------------------------------------------------
STATUS_ATTR     .equ    FBLACK|$18|BCYAN    ; $18 = banc BAGC3 : noir sur cyan
TEXT_ATTR       .equ    STATUS_ATTR         ; (v5 : tout le texte est sur la
ATTR_TITLE      .equ    STATUS_ATTR         ;  ligne d'etat, noir sur cyan)
ATTR_HELP       .equ    STATUS_ATTR
BAGC3_MOSAIC    .equ    $08
FONT_BAGC3      .equ    $7600           ; v5 : la page va jusqu'a $6B5B (police
FONT_BAGC3_M2   .equ    $75FE           ; FONT_BAGC3-2 ; avant : $6800) ; ROM : $7000
CH_BLANK        .equ    $60             ; glyphe vide (au lieu de l'espace)
MIXT_NEXT_TEXT  .equ    $00
SCREEN_BASE     .equ    $0600           ; page mixte : 1 ligne texte, 210 lignes
SCREEN_BASE_M1  .equ    $05FF           ; bitmap, 3 lignes texte (fin en $6B5B)
BMP_BASE        .equ    $0652           ; premiere ligne bitmap (= $0600 + 82)
BMP_LINE        .equ    122             ; ligne bitmap : 40 x 3 octets + 2 de controle
BAN_LINES       .equ    50              ; bandeau : 50 lignes bitmap cyan
BMP_LINES       .equ    210             ; bandeau 50 + liste 150 + ligne vide 10
LIST_BASE       .equ    $1E26           ; liste : BMP_BASE + 50 x 122
TITLE_VADDR     .equ    $0B16           ; titre : BMP_BASE + 10 x 122 (ligne 10 ; v5.1 : 16)
MIXT_NEXT_BMP   .equ    $10             ; controle : la ligne suivante est bitmap
CTL_CYAN        .equ    MCYAN           ; controle : marges de la ligne suivante
PAGE_SIZE       .equ    15              ; jeux par page
LIST_COL        .equ    6               ; " nnn  NOM..." a partir de la colonne 7
LIST_CELLS      .equ    40              ; la liste : toute la largeur
ATTR_PANEL      .equ    $18|$06         ; case texte du bandeau : fond CYAN
; couleurs (3 bits : bleu=4, vert=2, rouge=1)
C_BLACK         .equ    0
C_RED           .equ    1
C_YELLOW        .equ    3
C_BLUE          .equ    4
C_CYAN          .equ    6
C_WHITE         .equ    7
C_CRAM          .equ    6               ; images Exelmemoire : en CYAN dans la liste
C_K7            .equ    3               ; cassettes K7 : en JAUNE dans la liste (v5.2)
; logo : le papillon, 6 groupes de 8 points (colonnes 1-6) x 44 lignes, en
; haut a gauche du bandeau (lignes bitmap 0-43, sous la ligne de texte vide)
LOGO_GROUP      .equ    1               ; premier groupe (x = 8)
LOGO_GROUPS     .equ    6
LOGO_Y          .equ    0
LOGO_ROWS       .equ    44
; TASM calcule les expressions de GAUCHE A DROITE, sans priorite des
; operateurs : BMP_BASE+3*LOGO_GROUP donnerait (BMP_BASE+3)*1. D'ou les
; valeurs ecrites en clair : $0652 + 3 x 1 = $0655 ; de meme LIST_BASE et
; TITLE_VADDR plus haut
LOGO_VADDR      .equ    $0655
TITLE_C0        .equ    8               ; titre : cases 8-39 (le logo occupe 1-6)
TITLE_COL       .equ    14              ; "EXELpicoCart" (12) : colonnes 14-25
TITLE_VA8       .equ    $0B2E           ; = TITLE_VADDR + 3 x TITLE_C0
; v5.2 : mise en garde USB sous le titre, lignes bitmap 24-33 et 34-43 (le bas
; du papillon), cases 8-39 ; ligne 1 noire, ligne 2 rouge (BMP_BASE + 122 x
; ligne + 3 x 8) ; le titre remonte de la ligne 16 a la ligne 10
WARN_VA1        .equ    $11DA           ; ligne 24
WARN_VA2        .equ    $169E           ; ligne 34
WARN_COL1       .equ    9               ; 30 caracteres : colonnes 9-38
WARN_COL2       .equ    10              ; 29 caracteres : colonnes 10-38

; --- clavier (valeurs d'Exeltris ; fleche haut $80 NON VERIFIEE) ------------
KEY_ARR_LEFT    .equ    $83
KEY_ARR_RIGHT   .equ    $81
KEY_ARR_DOWN    .equ    $82
KEY_ARR_UP      .equ    $80
KEY_SPACE       .equ    $20
KEY_ENTER       .equ    $0D

; --- variables (RAM externe, comme mixpix) ----------------------------------
m_sel           .equ    $C500           ; jeu choisi
m_page          .equ    $C501           ; premier jeu de la page affichee
m_count         .equ    $C502           ; nombre de jeux
m_row           .equ    $C503
m_col           .equ    $C504
m_attr          .equ    $C505           ; attribut d'ecriture courant
m_key           .equ    $C506
m_idx           .equ    $C507
m_lines         .equ    $C508
m_mlast         .equ    $C509           ; souris : dernier compteur Y lu
m_macc          .equ    $C50A           ; souris : deplacement Y cumule (signe)
m_osel          .equ    $C50B           ; jeu choisi avant la touche
m_out           .equ    $C50C           ; put_ch : 0 = ecran texte, 1 = tampon
m_bi            .equ    $C50D           ; tampon : case suivante
m_fg            .equ    $C50E           ; couleurs de la ligne bitmap
m_bg            .equ    $C50F
m_wait          .equ    $C510           ; Exelmemoire : attente du Pico
m_tramp         .equ    $C511           ; lancement : 0 = jeu, 1 = Exelmemoire
m_sh            .equ    $C512           ; say : chaine a ecrire
m_sl            .equ    $C513
m_auto          .equ    $C514           ; Exelmemoire : demarrage automatique ($AA en $8000)
m_c0            .equ    $C515           ; render_at : premiere case dessinee
m_xm            .equ    $C516           ; 1 : une Exelmemoire repond (touche M)
m_buf           .equ    $C520           ; tampon : 40 cases (index de glyphe)
m_gplo          .equ    $C548           ; adresse du glyphe de chaque case
m_gphi          .equ    $C570
; registres du dessin bitmap (libres : la ROM n'utilise que R2-R40)
RG_AB           .equ    44              ; masques ET / OU-exclusif des 3 plans
RG_XB           .equ    45
RG_AG           .equ    46
RG_XG           .equ    47
RG_AR           .equ    48
RG_XR           .equ    49
RG_O            .equ    50              ; ligne du glyphe (9 = haut)
RG_P            .equ    52              ; paire R51:R52 : octet du glyphe
RG_G            .equ    53              ; octet du glyphe
RG_T            .equ    54              ; compteur
RG_V            .equ    56              ; paire R55:R56 : adresse VRAM
RG_C            .equ    57              ; case courante
R_B             .equ    1               ; le registre B vu comme R1 (xm_send)
MSTEP           .equ    6               ; souris : unites par ligne de menu
MSNEG           .equ    251             ; = 256 - MSTEP + 1 (cumul <= -MSTEP)

        .org    $1000

; ============================================================================
; DEMARRAGE
; ============================================================================
start:
        dint
        mov     %$58,B
        ldsp
        movp    P40,A
        movp    P36,A
        ; ce que faisait init_vdp de mixt_api.asm (recopie telle quelle, v3.4) :
        ; encore deux lectures du VDP, puis registre 4 = $10
        movp    P40,A
        movp    P36,A
        movp    %$04,P45
        movp    %$10,P45
        eint

        movd    %$0000,TEMP8
        trap    13                      ; police ROM (necessaire, cf. mixpix)

        movp    %TIMEBASE,P45           ; registres de mode : ceux de mixpix
        movp    %CM1MASK,P45
        movp    %DECODER,P45
        movp    %CM2MASK|LINEZERO|BAGC3_MOSAIC,P45
        movp    %SCRMODE,P45
        movp    %CM3MASK|MIXTMODE,P45
        movp    %BORDER,P45
        movp    %CM4MASK|MBLACK,P45

        movp    %COL,P45                ; base de BAGC3, puis notre police
        movp    %FONT_BAGC3_M2&$FF,P45
        movp    %ROW,P45
        movp    %FONT_BAGC3_M2>>8,P45
        movp    %$0E,P45
        movp    %0,P45
        movp    %0,P45
        movd    %font_chr,TEMP3         ; codes $20..$60
        movd    %FONT_BAGC3,TEMP2
        mov     %$20,TEMP7
        mov     %72,TEMP4-1             ; $20-$60 + p i c o a r t minuscules ($61-$67)
        trap    19
        movd    %font_chr,TEMP3         ; code $00 : vide aussi
        movd    %FONT_BAGC3,TEMP2
        clr     A
        mov     A,TEMP7
        mov     %1,TEMP4-1
        trap    19

        mov     %SCREEN_BASE_M1>>8,A    ; BAPA = notre page
        mov     %SCREEN_BASE_M1&$FF,B
        movp    %COL,P45
        movp    B,P45
        movp    %ROW,P45
        movp    A,P45
        movp    %REGBAPA,P45
        movp    %0,P45
        movp    %0,P45

        ; BAMTF (registre 15) : fin de la memoire video vue par les pointeurs.
        ; Notre page (bande bitmap comprise) va jusqu'a $66EA. On la met tout en
        ; haut, $7FFF (valeur prise = COL/ROW + 1) : aucun retour au debut avant
        ; la fin de la memoire video (v3.3 : $7800 est suspect dans la corruption de Wizord).
        movp    %COL,P45
        movp    %$FE,P45
        movp    %ROW,P45
        movp    %$7F,P45
        movp    %$0F,P45
        movp    %0,P45
        movp    %0,P45

        call    @menu_init
menu_loop:
        call    @get_key
        call    @menu_key               ; ne revient pas si un jeu est lance
        br      @menu_loop

; --- etat initial et premier affichage (aussi appele seul par les tests) ----
menu_init:
        call    @build_screen
        call    @draw_logo
        clr     A
        sta     @m_out
        lda     @exl_my                 ; souris : partir de la position actuelle
        sta     @m_mlast
        clr     A
        sta     @m_macc
        sta     @m_sel
        sta     @m_osel
        sta     @m_page
        lda     @CAT_COUNT
        cmp     %191,A                  ; liste illisible (cartouche sans Pico,
        jl      @mi_ok                  ; emulateur...) : aucun jeu
        clr     A
mi_ok:
        sta     @m_count
        call    @xm_probe               ; une Exelmemoire ? (aide : M)
        sta     @m_xm
        call    @draw_static
        lda     @m_count
        jeq     @mi_empty
        call    @draw_list
        rets
mi_empty:                               ; message au milieu de la liste
        call    @buf_clear
        mov     %8,A
        sta     @m_bi
        movd    %s_empty,TEMP3
        call    @put_str
        mov     %C_YELLOW,A
        sta     @m_fg
        mov     %C_BLACK,A
        sta     @m_bg
        mov     %7,A
        sta     @m_row
        call    @render_list_row
        clr     A
        sta     @m_out
        rets

; ============================================================================
; CLAVIER
; ============================================================================
; get_key : attend le relachement de la touche precedente, puis une touche
; nouvelle lue deux fois identique a ~15 ms d'intervalle. A = code.
; Sources : clavier et manettes infrarouges (VALUE0), manette DB9 et boutons de
; la souris (poll_level, meme regle d'appui / relachement), deplacements de la
; souris (mouse_step, pris tout de suite : ce sont des impulsions).
get_key:
gk_up:
        call    @poll_level
        cmp     %$04,A
        jne     @gk_up
gk_poll:
        call    @mouse_step             ; souris : une ligne vers le haut / le bas
        cmp     %$04,A
        jne     @gk_ret
        call    @poll_level
        cmp     %$04,A
        jeq     @gk_poll
        sta     @m_key
        call    @delay_short
        call    @poll_level
        cmpa    @m_key
        jne     @gk_poll
        lda     @m_key
gk_ret:
        rets

; poll_level : A = touche enfoncee ($04 = aucune). Le clavier prime ; puis la
; manette DB9 (traduite en fleches / ESPACE) ; puis un bouton de souris (ESPACE).
poll_level:
        mov     VALUE0,A
        jeq     @pl_db9                 ; $00 : rien au clavier
        cmp     %$04,A
        jne     @pl_done                ; une touche : elle prime
pl_db9:
        lda     @exl_joy0               ; manette DB9 ($00 sans EXLCART)
        and     %$1F,A
        mov     A,B
        lda     @db9_keys(B)
        cmp     %$04,A
        jne     @pl_done
        lda     @exl_mbtn               ; boutons de la souris
        and     %$07,A
        jeq     @pl_none
        mov     %KEY_SPACE,A
        rets
pl_none:
        mov     %$04,A
pl_done:
        rets

; mouse_step : A = fleche haut/bas quand la souris a parcouru MSTEP unites
; verticalement depuis la derniere ligne, sinon $04
mouse_step:
        lda     @exl_mpres
        cmp     %$A5,A
        jne     @ms_none                ; pas de souris
        lda     @m_mlast
        mov     A,B
        lda     @exl_my
        sta     @m_mlast
        sub     B,A                     ; A = deplacement Y (signe)
        cmp     %$80,A                  ; borne a +-16 (le cumul ne deborde pas)
        jhs     @ms_dneg
        cmp     %17,A
        jl      @ms_add
        mov     %16,A
        jmp     @ms_add
ms_dneg:
        cmp     %$F0,A
        jhs     @ms_add
        mov     %$F0,A
ms_add:
        mov     A,B
        lda     @m_macc
        add     B,A
        sta     @m_macc
        cmp     %$80,A
        jhs     @ms_neg
        cmp     %MSTEP,A                ; vers le bas
        jl      @ms_none
        sub     %MSTEP,A
        sta     @m_macc
        mov     %KEY_ARR_DOWN,A
        rets
ms_neg:
        cmp     %MSNEG,A                ; vers le haut
        jhs     @ms_none
        add     %MSTEP,A
        sta     @m_macc
        mov     %KEY_ARR_UP,A
        rets
ms_none:
        mov     %$04,A
        rets

; manette DB9 -> touche (feu > haut > bas > gauche > droite) ; index = etat
db9_keys:
        .byte   $04,$80,$82,$80,$83,$80,$82,$80
        .byte   $81,$80,$82,$80,$83,$80,$82,$80
        .byte   $20,$20,$20,$20,$20,$20,$20,$20
        .byte   $20,$20,$20,$20,$20,$20,$20,$20

; menu_key : A = touche. Deplace la selection et redessine, ou lance le jeu.
menu_key:
        sta     @m_key
        and     %$DF,A                  ; M ou m : sauver l'Exelmemoire
        cmp     %$4D,A                  ; (meme sans jeu dans la liste)
        jne     @mk_nm
        br      @xm_save
mk_nm:  lda     @m_count
        jeq     @mk_end                 ; aucun jeu : rien a faire
        lda     @m_key
        cmp     %KEY_ARR_DOWN,A
        jeq     @mk_down
        cmp     %KEY_ARR_UP,A
        jeq     @mk_up
        cmp     %KEY_ARR_RIGHT,A
        jeq     @mk_next
        cmp     %KEY_ARR_LEFT,A
        jeq     @mk_prev
        cmp     %KEY_SPACE,A
        jeq     @mk_sel0
        cmp     %KEY_ENTER,A
        jeq     @mk_sel0
mk_end:
        rets
mk_sel0:
        br      @mk_sel
mk_down:                                ; suivant, puis retour au premier
        lda     @m_sel
        inc     A
        cmpa    @m_count
        jl      @mk_set
        clr     A
        br      @mk_set
mk_up:                                  ; precedent, puis le dernier
        lda     @m_sel
        jne     @mk_up1
        lda     @m_count
mk_up1:
        dec     A
        br      @mk_set
mk_next:                                ; page suivante (ou le dernier)
        lda     @m_sel
        add     %PAGE_SIZE,A
        jc      @mk_last
        cmpa    @m_count
        jl      @mk_set
mk_last:
        lda     @m_count
        dec     A
        br      @mk_set
mk_prev:                                ; page precedente (ou le premier)
        lda     @m_sel
        cmp     %PAGE_SIZE,A
        jl      @mk_first
        sub     %PAGE_SIZE,A
        br      @mk_set
mk_first:
        clr     A
mk_set:
        push    A
        lda     @m_sel                  ; garder l'ancien choix
        sta     @m_osel
        pop     A
        sta     @m_sel
        clr     B                       ; page = multiple de PAGE_SIZE <= sel
mk_pg:
        mov     B,A
        add     %PAGE_SIZE,A
        mov     A,TEMP5
        lda     @m_sel
        cmp     TEMP5,A
        jl      @mk_pgok
        mov     TEMP5,B
        br      @mk_pg
mk_pgok:
        mov     B,A
        cmpa    @m_page
        jeq     @mk_same                ; meme page : 2 lignes a redessiner
        sta     @m_page
        call    @draw_page_info
        call    @draw_list
        rets
mk_same:                                ; (une ligne bitmap prend ~20 ms)
        lda     @m_osel
        call    @draw_one
        lda     @m_sel
        call    @draw_one
        rets

; --- ESPACE / ENTREE : un jeu, ou une image Exelmemoire -----------------------
mk_sel:
        call    @sel_flag
        jeq     @mk_go                  ; un jeu
        call    @cram_menu              ; une image Exelmemoire
        rets

; --- lancement : la lecture en $6F00+n designe le jeu n au Pico -------------
mk_go:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        mov     %24,A
        sta     @m_row
        clr     A
        sta     @m_col
        call    @goto_rc
        movd    %s_loading,TEMP3
        call    @put_str
        lda     @m_sel
        mov     A,B
        lda     @SEL_HOTSPOT(B)         ; <- le signal : le Pico charge le jeu
        call    @wait_release           ; toutes les touches relachees (~120 ms)
        call    @delay_long             ; clavier au repos ; le jeu est charge
                                        ; depuis longtemps (~1 ms suffit)
        clr     A
        sta     @m_tramp                ; un jeu : BR @$7FFD
mk_jump:                                ; (m_tramp = 1 : Exelmemoire, BR @$8001)
        ; Demarrage DIRECT du jeu, sans repasser par la ROM interne : apres un
        ; TRAP 0, l'EXELTEL prend son chemin de redemarrage "a chaud", affiche
        ; son ecran rouge et attend une touche. Le pied d'une cartouche est une
        ; instruction : AA/55 puis "8C hh ll" = BR @hhll ; la ROM le verifie puis
        ; execute $7FFD. On fait pareil, depuis la RAM, car la cartouche change
        ; de contenu sous nos pieds : LDA @$6FFF (le Pico bascule sur le jeu des
        ; l'acces suivant), puis BR @$7FFD (l'instruction de demarrage du jeu).
        ; Si le jeu n'etait pas encore charge, $7FFD est celui du menu : le menu
        ; redemarre, sans blocage. Un reset lance aussi le jeu choisi.
        ;
        ; v3.3 : le jeu doit trouver la machine comme apres un reset. La ROM, avant
        ; de sauter a la cartouche, eteint l'affichage (registre 6 = 0), efface la
        ; RAM $C100-$C7FF et les registres R2-R127, puis met la pile a 0. Le menu
        ; laissait des valeurs dans R44-R57 et $C520-$C597 (dessin bitmap) :
        ; suspect dans la corruption de Wizord. On refait donc ce menage ici, sans
        ; interruption (l'interruption de la ROM lit R2, R5:R6...), puis on saute.
        dint
        mov     %TRAMP_LEN,B
mk_cp:  lda     @tramp_src-1(B)
        sta     @TRAMP_RAM-1(B)
        djnz    B,@mk_cp
        lda     @m_tramp
        jeq     @mk_clr0
        mov     %$B5,A                  ; Exelmemoire : CLR A, BR @$8001
        sta     @TRAMP_RAM
        mov     %$8C,A
        sta     @TRAMP_RAM+1
        mov     %CRAM_BASE>>8,A
        sta     @TRAMP_RAM+2
        mov     %$01,A
        sta     @TRAMP_RAM+3
        mov     %CRAM_BASE>>8,A         ; et, comme la ROM, BR @$8001 en $C006
        sta     @$C007
        mov     %$01,A
        sta     @$C008
mk_clr0:
        movp    %$06,P45                ; affichage eteint (comme la ROM en $FEB1)
        movp    %$00,P45
        movd    %TRAMP_RAM-1,R45        ; RAM $C100 a $C7F8 (pas la routine)
        clr     A
mk_clr: sta     *R45
        decd    R45
        cmp     %$C0,R44
        jne     @mk_clr
        mov     %$7E,B                  ; registres R2 a R127 (pile comprise :
mk_reg: sta     @$0001(B)               ; on ne revient plus)
        djnz    B,@mk_reg
        ldsp                            ; B = 0 : pile a 0, comme la ROM
        eint
        br      @TRAMP_RAM

tramp_src:
        .byte   $8A,GO_HOTSPOT>>8,GO_HOTSPOT&$FF        ; LDA @$6FFF
        .byte   $B5                                     ; CLR A (A = 0, comme la ROM)
        .byte   $8C,$7F,$FD                             ; BR  @$7FFD

; ============================================================================
; EXELMEMOIRE (v4)
; ============================================================================
; sel_flag : A = CRAM_FLAG si le choix est une image Exelmemoire, sinon 0 (et Z)
sel_flag:
        lda     @m_sel
        mpy     %NAME_LEN,A
        add     %CAT_NAMES&$FF,B
        adc     %CAT_NAMES>>8,A
        mov     A,TEMP3-1
        mov     B,TEMP3
        lda     *TEMP3
        and     %CRAM_FLAG,A
        rets

; cram_menu : confirmation, chargement, relecture, demarrage eventuel
cram_menu:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        movd    %s_cr_ask1,TEMP3
        call    @say23
        mov     %ATTR_HELP,A
        sta     @m_attr
        movd    %s_cr_ask2,TEMP3
        call    @say24
        call    @get_key
        cmp     %KEY_ENTER,A
        jeq     @cm_yes
        cmp     %KEY_SPACE,A            ; (feu de la manette, clic)
        jeq     @cm_yes
        br      @cm_back
cm_yes:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        movd    %s_none,TEMP3
        call    @say23
        movd    %s_cr_load,TEMP3
        call    @say24
        lda     @m_sel
        mov     A,B
        lda     @CRAM_REQ(B)            ; <- le signal : le Pico copie l'image n
        mov     %140,A                  ; puis ~2 s au plus pour CRAM_READY
        sta     @m_wait
cm_wt:  lda     @CRAM_STAT
        cmp     %CRAM_READY,A
        jeq     @cm_copy
        call    @delay_short
        lda     @m_wait
        dec     A
        sta     @m_wait
        jne     @cm_wt
        movd    %s_cr_nopico,TEMP3      ; firmware trop ancien, ou pas de Pico
        br      @cm_err
cm_copy:
        call    @cram_copy
        jeq     @cm_ok
        movd    %s_cr_bad,TEMP3         ; relecture fausse
cm_err:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        call    @say23
        mov     %ATTR_HELP,A
        sta     @m_attr
        movd    %s_cr_key,TEMP3
        call    @say24
        call    @get_key
        br      @cm_back
cm_ok:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        call    @cram_auto              ; demarrage automatique ?
        sta     @m_auto
        jeq     @cm_na
        movd    %s_cr_ok_s,TEMP3        ; avec : ENTREE demarre, S sans cartouche
        call    @say23
        mov     %ATTR_HELP,A
        sta     @m_attr
        movd    %s_cr_go,TEMP3
        call    @say24
        call    @get_key
        cmp     %KEY_ENTER,A
        jeq     @cm_go
        cmp     %KEY_SPACE,A
        jeq     @cm_go
        jmp     @cm_s
cm_na:                                  ; sans : ENTREE ou S, sans cartouche
        movd    %s_cr_ok_na,TEMP3
        call    @say23
        mov     %ATTR_HELP,A
        sta     @m_attr
        movd    %s_cr_go_na,TEMP3
        call    @say24
        call    @get_key
        cmp     %KEY_ENTER,A
        jeq     @cm_nocart
        cmp     %KEY_SPACE,A
        jeq     @cm_nocart
cm_s:   and     %$DF,A                  ; S ou s
        cmp     %$53,A
        jeq     @cm_nocart
cm_back:                                ; lignes 23-24 : l'aide, comme avant
        movd    %s_none,TEMP3
        call    @say23
        movd    %s_none,TEMP3
        call    @say24
        call    @draw_static
        rets
; sans cartouche : le Pico met $FF dans le pied ; au prochain reset, la console
; demarre comme si la cartouche n'etait pas la (Exelmemoire a demarrage
; automatique, ou menu de la console) ; le reset d'apres ramene a ce menu.
cm_nocart:
        lda     @NOCART                 ; <- le Pico cache la cartouche (le menu reste servi)
        mov     %ATTR_TITLE,A
        sta     @m_attr
        movd    %s_cr_rst1,TEMP3
        call    @say23
        mov     %ATTR_HELP,A
        sta     @m_attr
        movd    %s_cr_rst2,TEMP3
        call    @say24
        call    @get_key                ; (ou RESET)
        lda     @NOCART_BACK            ; annulation : la cartouche revient
        jmp     @cm_back
cm_go:
        call    @wait_release           ; toutes les touches relachees
        lda     @CRAM_GO                ; le Pico relaie maintenant la manette par infrarouge
        mov     %1,A
        sta     @m_tramp
        br      @mk_jump                ; menage de la machine, puis BR @$8001

; cram_copy : fenetre $2800-$67FF -> $8000-$BFFF, puis relecture.
; Interruptions coupees ; le connecteur est valide par une lecture de P49 et
; P50, comme dans Exelnoid (lecture de l'Exelmemoire verifiee sur la machine).
; Sortie : A = nombre d'octets faux (borne a 255), Z si tout est bon.
cram_copy:
        dint
        movp    P49,A
        movp    P50,A
        movd    %CRAM_WIN+$3FFF,RG_P    ; R51:R52 : source (la cartouche), en descendant
        movd    %CRAM_BASE+$3FFF,RG_V   ; R55:R56 : destination ($BFFF -> $8000)
cc_cp:  lda     *RG_P
        sta     *RG_V
        decd    RG_P
        decd    RG_V
        cmp     %CRAM_BASE_HI-1,RG_V-1
        jne     @cc_cp
        movd    %CRAM_WIN+$3FFF,RG_P    ; relecture
        movd    %CRAM_BASE+$3FFF,RG_V
        clr     B
cc_vf:  lda     *RG_P
        mov     A,RG_T
        lda     *RG_V
        cmp     RG_T,A
        jeq     @cc_vok
        inc     B                       ; un octet faux (borne a 255)
        jne     @cc_vok
        dec     B
cc_vok: decd    RG_P
        decd    RG_V
        cmp     %CRAM_BASE_HI-1,RG_V-1
        jne     @cc_vf
        eint
        mov     B,A
        rets

; cram_auto : A <> 0 (NZ) si l'Exelmemoire a le demarrage automatique ($AA en $8000)
cram_auto:
        dint
        movp    P49,A
        movp    P50,A
        lda     @CRAM_BASE
        eint
        cmp     %$AA,A
        jeq     @ca_yes
        clr     A
        rets
ca_yes: mov     %1,A
        rets

; ============================================================================
; SAUVEGARDE DE L'EXELMEMOIRE DANS LE PICO (v5.1)
; ============================================================================
; xm_probe : A = 1 (NZ) si une Exelmemoire repond en $BFFF, sinon 0 (Z). On y
; ecrit $55 puis $AA, relus chacun, puis l'octet d'origine est remis (sans
; Exelmemoire, la lecture rend le dernier octet vu sur le bus : $FF, l'adresse
; de l'instruction LDA). Interruptions coupees ; connecteur valide par P49/P50.
xm_probe:
        dint
        movp    P49,A
        movp    P50,A
        lda     @CRAM_TOP
        mov     A,RG_T                  ; l'octet d'origine
        mov     %$55,A
        sta     @CRAM_TOP
        lda     @CRAM_TOP
        mov     A,B
        mov     %$AA,A
        sta     @CRAM_TOP
        lda     @CRAM_TOP
        mov     A,RG_C
        mov     RG_T,A
        sta     @CRAM_TOP               ; remis
        eint
        cmp     %$55,B
        jne     @xp_no
        cmp     %$AA,RG_C
        jne     @xp_no
        mov     %1,A
        rets
xp_no:  clr     A
        rets

; xm_save : touche M. Confirmation, envoi, reponse du Pico, message.
xm_save:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        lda     @m_xm
        jne     @xv_ask
        movd    %s_xm_none,TEMP3        ; pas d'Exelmemoire
        br      @cm_err
xv_ask: movd    %s_xm_ask1,TEMP3
        call    @say23
        movd    %s_xm_ask2,TEMP3
        call    @say24
        call    @get_key
        cmp     %KEY_ENTER,A
        jeq     @xv_yes
        cmp     %KEY_SPACE,A            ; (feu de la manette, clic)
        jeq     @xv_yes
        br      @cm_back
xv_yes: movd    %s_xm_send,TEMP3
        call    @say23
        movd    %s_none,TEMP3
        call    @say24
        call    @xm_send
        mov     %255,A                  ; ~4 s au plus : le Pico ecrit sa flash
        sta     @m_wait
xv_wt:  lda     @SAVE_STAT
        cmp     %SAVE_OK,A
        jeq     @xv_ok
        cmp     %SAVE_BAD,A
        jeq     @xv_bad
        cmp     %SAVE_FLASH,A
        jeq     @xv_fl
        call    @delay_short
        lda     @m_wait
        dec     A
        sta     @m_wait
        jne     @xv_wt
        movd    %s_cr_nopico,TEMP3      ; firmware trop ancien, ou pas de Pico
        br      @cm_err
xv_bad: movd    %s_xm_bad,TEMP3
        br      @cm_err
xv_fl:  movd    %s_xm_flash,TEMP3
        br      @cm_err
xv_ok:  movd    %s_xm_ok,TEMP3
        call    @say23
        mov     %34,A                   ; le numero, a la suite du texte (say
        sta     @m_col                  ; laisse le pointeur hors de la page)
        call    @goto_rc
        lda     @SAVE_SLOT
        and     %$0F,A
        add     %$30,A
        call    @put_ch
        movd    %s_xm_ok2,TEMP3
        call    @say24
        call    @get_key
        br      @cm_back

; xm_send : envoie $BFFF..$8000 (ordre des .BKP : le TMS7020 n'a pas d'INCD),
; puis la somme des octets (16 bits, poids fort d'abord), par des lectures en
; $6C00+v, entre $6BF0 (debut) et $6BF1 (fin). Interruptions coupees (~0,5 s).
xm_send:
        dint
        movp    P49,A
        movp    P50,A
        lda     @SAVE_GO                ; <- le Pico remet son compteur a zero
        movd    %CRAM_TOP,RG_V          ; R55:R56 : l'Exelmemoire, en descendant
        movd    %0,RG_P                 ; R51:R52 : la somme
xs_lp:  lda     *RG_V
        mov     A,B
        lda     @SAVE_WIN(B)            ; <- l'octet, vu par le Pico sur le bus d'adresses
        add     R_B,RG_P                ; l'ADD pose la retenue (B = R1 : forme Rn,Rn,
        adc     %0,RG_P-1               ; TASM refuse ADD B,Rn) ; l'ADC la consomme aussitot
        decd    RG_V
        cmp     %CRAM_BASE_HI-1,RG_V-1
        jne     @xs_lp
        mov     RG_P-1,B                ; la somme
        lda     @SAVE_WIN(B)
        mov     RG_P,B
        lda     @SAVE_WIN(B)
        lda     @SAVE_END               ; <- fin : le Pico controle et range
        eint
        rets

; say23 / say24 : efface la ligne de texte 23 / 24, puis y ecrit la chaine
; TEMP3 depuis la colonne 0, avec l'attribut m_attr
say23:  mov     %23,A
        jmp     @say
say24:  mov     %24,A
say:    sta     @m_row
        mov     TEMP3-1,A
        sta     @m_sh
        mov     TEMP3,A
        sta     @m_sl
        clr     A
        sta     @m_col
        call    @goto_rc
        movd    %s_blank40,TEMP3
        call    @put_str
        lda     @m_sh
        mov     A,TEMP3-1
        lda     @m_sl
        mov     A,TEMP3
        lda     *TEMP3                  ; chaine vide : on ne repositionne PAS le
        jeq     @say_end                ; pointeur (un pointeur pose sans ecriture
        clr     A                       ; derriere fausse le suivant : vu dans
        sta     @m_col                  ; jexel-debug)
        call    @goto_rc
        call    @put_str
say_end:
        br      @status_ctl             ; octets de controle remis, pointeur range

; status_ctl (v5.1) : reecrit l'octet de controle des lignes texte 22, 23 et 24
; (texte suivant, marges cyan). Sur la machine, apres la sauvegarde, celui de
; la ligne 23 s'est retrouve avec le bit "ligne suivante bitmap" (une ligne de
; points a la place de la ligne 24, jusqu'au reset) : il est donc remis apres
; chaque message. Le pointeur VDP finit hors de la page ($6B5C).
status_ctl:
        mov     %22,B
sc_lp:  push    B
        lda     @row_lo(B)
        mov     A,RG_T                  ; R53:R54 = debut de la ligne
        lda     @row_hi(B)
        mov     A,RG_T-1
        add     %80,RG_T                ; + 80 : le controle (81e octet) ; l'ADD
        adc     %0,RG_T-1               ; pose la retenue, l'ADC la consomme
        mov     RG_T-1,A
        mov     RG_T,B
        call    @setAcmpxy
        movp    %MIXT_NEXT_TEXT|CTL_CYAN,P46
        movp    %0,P46
        pop     B
        inc     B
        cmp     %25,B
        jne     @sc_lp
        rets

; --- attendre que toutes les touches soient relachees ------------------------
; 8 lectures consecutives "aucune touche" ($00 ou $04), espacees de ~15 ms
; (clavier, manette DB9 et boutons de souris : le jeu ne demarre pas feu appuye) ;
; une touche encore enfoncee fait tout recommencer.
wait_release:
        mov     %8,TEMP7
wrl_chk:
        call    @poll_level             ; clavier, manette DB9, boutons de souris
        cmp     %$04,A
        jne     @wait_release
        call    @delay_short
        djnz    TEMP7,@wrl_chk
        rets

; ============================================================================
; AFFICHAGE
; ============================================================================
; --- la page : 1 ligne texte, la bande bitmap, 3 lignes texte --------------
; Chaque ligne commence par un nouveau pointeur (sans compter sur l'avance
; automatique d'une ligne a l'autre). L'octet de controle d'une ligne (121e
; en bitmap, 81e en texte) donne le type ET la couleur des marges de la
; ligne suivante ; celui de la derniere ligne, les marges du haut, du bas et
; de la premiere ligne : cyan autour du bandeau et de la ligne d'etat.
build_screen:
        clr     B                       ; ligne texte 0 : le haut du bandeau
        call    @goto_row
        mov     %40,B
bs_top:
        movp    %ATTR_PANEL,P46
        movp    %CH_BLANK,P46
        djnz    B,@bs_top
        movp    %MIXT_NEXT_BMP|CTL_CYAN,P46
        movp    %0,P46
        movd    %BMP_BASE,RG_V          ; la bande : bandeau cyan, puis noir
        clr     RG_T                    ; ligne de la bande (0-209)
bs_bmp:
        mov     RG_V-1,A
        mov     RG_V,B
        call    @setAcmpxy
        mov     %40,B
        cmp     %BAN_LINES,RG_T
        jhs     @bs_blk
bs_cyan:
        movp    %$FF,P46                ; bleu
        movp    %$FF,P46                ; vert
        movp    %0,P46                  ; rouge
        djnz    B,@bs_cyan
        jmp     @bs_ctl
bs_blk:
        movp    %0,P46
        movp    %0,P46
        movp    %0,P46
        djnz    B,@bs_blk
bs_ctl:
        mov     %MIXT_NEXT_BMP|CTL_CYAN,A   ; dans le bandeau : marges cyan
        cmp     %BAN_LINES-1,RG_T
        jl      @bs_c2
        mov     %MIXT_NEXT_BMP,A        ; la liste : marges noires
        cmp     %BMP_LINES-1,RG_T
        jne     @bs_c2
        mov     %MIXT_NEXT_TEXT|CTL_CYAN,A  ; derniere : la ligne d'etat suit
bs_c2:
        movp    A,P46
        movp    %0,P46
        add     %BMP_LINE,RG_V
        adc     %0,RG_V-1
        inc     RG_T
        cmp     %BMP_LINES,RG_T
        jne     @bs_bmp
        mov     %22,B                   ; lignes texte 22, 23, 24 : la ligne d'etat
bs_bot:
        push    B
        call    @goto_row
        mov     %40,B
bs_bc:
        movp    %STATUS_ATTR,P46
        movp    %CH_BLANK,P46
        djnz    B,@bs_bc
        movp    %MIXT_NEXT_TEXT|CTL_CYAN,P46
        movp    %0,P46
        pop     B
        inc     B
        cmp     %25,B
        jne     @bs_bot
        rets
; goto_row : pointeur VDP au debut de la ligne texte B
goto_row:
        lda     @row_lo(B)
        mov     A,RG_V
        lda     @row_hi(B)
        mov     RG_V,B
        call    @setAcmpxy
        rets

; --- le papillon, en haut a gauche du bandeau -----------------------------------
; logo_bmp : 44 lignes de 18 octets (6 groupes bleu, vert, rouge), fond cyan
; compris ; chaque ligne est copiee telle quelle a sa place dans la bande.
draw_logo:
        movd    %logo_bmp,RG_P
        mov     %LOGO_Y,RG_C
        mov     %LOGO_ROWS,RG_T
lg_row:
        mov     RG_C,A
        mpy     %BMP_LINE,A             ; A:B = 122 x ligne
        add     %LOGO_VADDR&$FF,B       ; + adresse de la ligne 0, colonne 1
        adc     %LOGO_VADDR>>8,A
        call    @setAcmpxy
        mov     %3*LOGO_GROUPS,B
lg_byte:
        lda     *RG_P
        movp    A,P46
        inc     RG_P
        jne     @lg_nc
        inc     RG_P-1
lg_nc:
        djnz    B,@lg_byte
        inc     RG_C
        djnz    RG_T,@lg_row
        rets

; --- titre (dans le bandeau), nombre de jeux, aide -----------------------------
draw_static:
        call    @buf_clear              ; "EXELpicoCart" noir sur cyan, dessine
        mov     %TITLE_COL,A            ; en bitmap a cote du papillon
        sta     @m_bi
        movd    %s_title,TEMP3
        call    @put_str
        mov     %C_BLACK,A
        sta     @m_fg
        mov     %C_CYAN,A
        sta     @m_bg
        mov     %TITLE_C0,A             ; cases 8-39 : le papillon reste
        sta     @m_c0
        movd    %TITLE_VA8,RG_V
        call    @render_at
        call    @buf_clear              ; v5.2 : mise en garde USB (noir, puis rouge)
        mov     %WARN_COL1,A
        sta     @m_bi
        movd    %s_warn1,TEMP3
        call    @put_str
        movd    %WARN_VA1,RG_V
        call    @render_at
        call    @buf_clear
        mov     %WARN_COL2,A
        sta     @m_bi
        movd    %s_warn2,TEMP3
        call    @put_str
        mov     %C_RED,A
        sta     @m_fg
        movd    %WARN_VA2,RG_V
        call    @render_at
        clr     A
        sta     @m_out
        mov     %ATTR_HELP,A
        sta     @m_attr
        mov     %23,A
        sta     @m_row
        mov     %2,A
        sta     @m_col
        call    @goto_rc
        movd    %s_help1,TEMP3
        call    @put_str
        mov     %24,A
        sta     @m_row
        mov     %2,A
        sta     @m_col
        call    @goto_rc
        movd    %s_help2,TEMP3
        lda     @m_xm                   ; avec une Exelmemoire : M pour la sauver
        jeq     @ds_h2
        movd    %s_help2m,TEMP3
ds_h2:  call    @put_str
        lda     @m_count
        jeq     @ds_end
        call    @draw_page_info
ds_end:
        br      @status_ctl

; --- ligne d'etat 22 : "nnn JEUX" a gauche, "PAGE p/q" a droite ---------------
draw_page_info:
        mov     %TEXT_ATTR,A
        sta     @m_attr
        mov     %22,A
        sta     @m_row
        mov     %1,A
        sta     @m_col
        call    @goto_rc
        lda     @m_count
        call    @put_dec3
        movd    %s_games,TEMP3
        call    @put_str
        mov     %29,A
        sta     @m_col
        call    @goto_rc
        movd    %s_page,TEMP3
        call    @put_str
        lda     @m_page                 ; numero de page = page/20 + 1
        clr     B
dpi_div:
        cmp     %PAGE_SIZE,A
        jl      @dpi_d1
        sub     %PAGE_SIZE,A
        inc     B
        br      @dpi_div
dpi_d1:
        mov     B,A
        inc     A
        call    @put_dec2
        mov     %$2F,A                  ; '/'
        call    @put_ch
        lda     @m_count                ; nombre de pages = (n+19)/20
        add     %PAGE_SIZE-1,A
        clr     B
dpi_div2:
        cmp     %PAGE_SIZE,A
        jl      @dpi_d2
        sub     %PAGE_SIZE,A
        inc     B
        br      @dpi_div2
dpi_d2:
        mov     B,A
        call    @put_dec2
        rets

; --- la liste : PAGE_SIZE lignes a partir de m_page (bande bitmap) ----------
draw_list:
        lda     @m_page
        sta     @m_idx
        clr     A
        sta     @m_row                  ; ligne de la liste (0-14)
dl_line:
        call    @draw_line
        lda     @m_idx
        inc     A
        sta     @m_idx
        lda     @m_row
        inc     A
        sta     @m_row
        cmp     %PAGE_SIZE,A
        jne     @dl_line
        rets

; draw_one : A = jeu ; le redessine s'il est sur la page affichee
draw_one:
        sta     @m_idx
        lda     @m_page
        mov     A,B
        lda     @m_idx
        sub     B,A                     ; A = jeu - premier de la page
        jl      @do_end                 ; avant la page
        cmp     %PAGE_SIZE,A
        jhs     @do_end                 ; apres la page
        sta     @m_row
        call    @draw_line
do_end:
        rets

; draw_line : la ligne du jeu m_idx sur la ligne m_row de la liste :
; " nnn  NOM-EN-20-CARACTERES " a partir de la colonne 7, sur les 40 cases
; (la barre bleue du jeu choisi fait toute la largeur) ; vide apres le dernier
draw_line:
        call    @buf_clear
        mov     %C_WHITE,A
        sta     @m_fg
        mov     %C_BLACK,A
        sta     @m_bg
        lda     @m_idx
        cmpa    @m_count
        jc      @dln_draw               ; jc : index >= nombre de jeux (vide)
        cmpa    @m_sel
        jne     @dln_txt
        mov     %C_BLUE,A               ; ligne choisie : fond bleu
        sta     @m_bg
dln_txt:
        lda     @m_idx                  ; TEMP3 = CAT_NAMES + 20 x index
        mpy     %NAME_LEN,A
        add     %CAT_NAMES&$FF,B
        adc     %CAT_NAMES>>8,A
        mov     A,TEMP3-1
        mov     B,TEMP3
        lda     *TEMP3                  ; image Exelmemoire : en cyan
        and     %CRAM_FLAG,A
        jeq     @dln_k7
        mov     %C_CRAM,A
        sta     @m_fg
        jmp     @dln_col
dln_k7: movd    TEMP3,RG_P              ; cassette K7 (bit 7 du 2e caractere) :
        inc     RG_P                    ; en jaune
        jne     @dln_k7a
        inc     RG_P-1
dln_k7a:
        lda     *RG_P
        and     %K7_FLAG,A
        jeq     @dln_col
        mov     %C_K7,A
        sta     @m_fg
dln_col:
        mov     %LIST_COL+1,A
        sta     @m_bi
        lda     @m_idx                  ; numero affiche : index + 1
        inc     A
        call    @put_dec3
        mov     %$20,A
        call    @put_ch
        mov     %$20,A
        call    @put_ch
        mov     %NAME_LEN,TEMP5         ; le nom, 20 caracteres
        call    @put_n
dln_draw:
        call    @render_list_row
        clr     A
        sta     @m_out
        rets

; buf_clear : 40 cases vides, et put_ch ecrit desormais dans le tampon
buf_clear:
        mov     %40,B
        mov     %CH_BLANK-$20,A
bc_l:   sta     @m_buf-1(B)
        djnz    B,@bc_l
        mov     %1,A
        sta     @m_out
        clr     A
        sta     @m_bi
        rets

; ============================================================================
; DESSIN BITMAP : cases du tampon -> 10 lignes de la bande
; ============================================================================
; render_list_row : les 40 cases, ligne m_row de la liste.
; render_at : cases m_c0 a 39, a partir de l'adresse VRAM RG_V (ligne du haut,
; case m_c0) ; couleurs m_fg / m_bg.
; Pour chaque ligne de 10 points, chaque case donne 3 octets (plans bleu, vert,
; rouge ; bit 7 = point de gauche). Avec p = octet du glyphe, un plan vaut
; (p ET a) OU-EXCLUSIF x, ou a et x valent $00 ou $FF selon que la couleur du
; texte et celle du fond contiennent ce plan : les deux -> $FF ; le texte
; seul -> p ; le fond seul -> NON p ; aucun -> 0.
render_list_row:
        clr     A
        sta     @m_c0
        lda     @m_row                  ; adresse VRAM : LIST_BASE + 122 x 10 x ligne
        mpy     %10,A
        mpy     %BMP_LINE,B
        add     %LIST_BASE&$FF,B
        adc     %LIST_BASE>>8,A
        mov     A,RG_V-1
        mov     B,RG_V
render_at:
        lda     @m_fg                   ; masques des 3 plans
        mov     A,RG_G
        lda     @m_bg
        mov     A,RG_O
        xor     RG_O,RG_G               ; texte OU-EXCLUSIF fond
        mov     RG_G,B
        lda     @pl_b(B)
        mov     A,RG_AB
        lda     @pl_g(B)
        mov     A,RG_AG
        lda     @pl_r(B)
        mov     A,RG_AR
        mov     RG_O,B
        lda     @pl_b(B)
        mov     A,RG_XB
        lda     @pl_g(B)
        mov     A,RG_XG
        lda     @pl_r(B)
        mov     A,RG_XR
        lda     @m_c0                   ; adresse du glyphe de chaque case
        mov     A,RG_C
rl_gp:
        mov     RG_C,B
        lda     @m_buf(B)
        mpy     %10,A                   ; A:B = 10 x index
        add     %font_chr&$FF,B
        adc     %font_chr>>8,A
        mov     A,RG_G
        mov     B,A
        mov     RG_C,B
        sta     @m_gplo(B)
        mov     RG_G,A
        sta     @m_gphi(B)
        inc     RG_C
        cmp     %LIST_CELLS,RG_C
        jne     @rl_gp
        mov     %9,RG_O                 ; le glyphe est range ligne du BAS en premier
rl_line:
        mov     RG_V-1,A
        mov     RG_V,B
        call    @setAcmpxy
        lda     @m_c0
        mov     A,RG_C
rl_cell:
        mov     RG_C,B
        lda     @m_gphi(B)
        mov     A,RG_P-1
        lda     @m_gplo(B)
        add     RG_O,A                  ; l'ADD pose la retenue,
        adc     %0,RG_P-1               ; l'ADC la consomme aussitot
        mov     A,RG_P
        lda     *RG_P
        mov     A,RG_G
        and     RG_AB,A                 ; plan bleu
        xor     RG_XB,A
        movp    A,P46
        mov     RG_G,A                  ; plan vert
        and     RG_AG,A
        xor     RG_XG,A
        movp    A,P46
        mov     RG_G,A                  ; plan rouge
        and     RG_AR,A
        xor     RG_XR,A
        movp    A,P46
        inc     RG_C
        cmp     %LIST_CELLS,RG_C
        jne     @rl_cell
        add     %BMP_LINE,RG_V
        adc     %0,RG_V-1
        dec     RG_O
        jpz     @rl_line                ; lignes 9 a 0 du glyphe
        rets

; plan bleu / vert / rouge present dans la couleur (index 0-7) : $FF ou $00
pl_b:      .byte   $00,$00,$00,$00,$FF,$FF,$FF,$FF
pl_g:      .byte   $00,$00,$FF,$FF,$00,$00,$FF,$FF
pl_r:      .byte   $00,$FF,$00,$FF,$00,$FF,$00,$FF

; ============================================================================
; ECRITURE DE TEXTE
; ============================================================================
; goto_rc : positionne le pointeur VDP sur (m_row, m_col)
goto_rc:
        lda     @m_row
        mov     A,B
        lda     @row_lo(B)
        mov     A,TEMP6
        lda     @row_hi(B)
        mov     A,TEMP6-1
        lda     @m_col
        mov     A,B
        add     B,A                     ; 2 octets par case
        mov     A,TEMP7
        add     TEMP7,TEMP6             ; l'ADD pose la retenue,
        adc     %0,TEMP6-1              ; l'ADC la consomme aussitot
        mov     TEMP6-1,A
        mov     TEMP6,B
        call    @setAcmpxy
        rets

; put_ch : A = caractere ASCII -> attribut m_attr + glyphe, a la suite
; (minuscules -> majuscules, espace et caracteres inconnus -> CH_BLANK ;
; codes 1-7 -> glyphes p i c o a r t minuscules $61-$67, pour le titre) ;
; si m_out = 1 : dans le tampon de la ligne bitmap (case m_bi)
put_ch:
        and     %$7F,A                  ; (bit 7 : marque des images Exelmemoire)
        cmp     %$08,A                  ; codes 1-7 : p i c o a r t minuscules
        jc      @pc_lx                  ; (titre "EXELpicoCart" seulement)
        tsta
        jeq     @pc_blank
        add     %$60,A                  ; -> glyphes $61-$64
        jmp     @pc_out
pc_lx:  cmp     %$61,A
        jl      @pc_upper
        cmp     %$7B,A
        jc      @pc_blank               ; jc : A >= $7B
        sub     %$20,A
pc_upper:
        cmp     %$21,A
        jl      @pc_blank               ; espace et codes de controle
        cmp     %$60,A
        jl      @pc_out
pc_blank:
        mov     %CH_BLANK,A
pc_out:
        mov     A,B
        lda     @m_out
        jne     @pc_buf
        lda     @m_attr
        movp    A,P46
        movp    B,P46
        rets
pc_buf:                                 ; tampon de la ligne bitmap :
        mov     B,A                     ; index du glyphe = code - $20
        sub     %$20,A
        push    A
        lda     @m_bi
        mov     A,B
        pop     A
        cmp     %40,B
        jhs     @pc_full
        sta     @m_buf(B)
        inc     B
        mov     B,A
        sta     @m_bi
pc_full:
        rets

; put_str : chaine terminee par 0, TEMP3 = adresse
put_str:
        lda     *TEMP3
        jeq     @pst_end
        call    @put_ch
        inc     TEMP3
        jne     @put_str
        inc     TEMP3-1
        br      @put_str
pst_end:
        rets

; put_n : TEMP5 caracteres a partir de TEMP3 (TEMP3 avance d'autant)
put_n:
        lda     *TEMP3
        call    @put_ch
        inc     TEMP3
        jne     @pn_nc
        inc     TEMP3-1
pn_nc:
        djnz    TEMP5,@put_n
        rets

; put_dec3 / put_dec2 : A en decimal sur 3 / 2 caracteres, zeros de tete
; ecrits en espaces. TEMP5 = centaines (0 : la dizaine nulle devient espace).
; ATTENTION : put_ch ecrase B -- les chiffres sont donc gardes AVANT l'appel.
put_dec3:
        clr     B
pd3_c:
        cmp     %100,A
        jl      @pd3_o
        sub     %100,A
        inc     B
        br      @pd3_c
pd3_o:
        mov     B,TEMP5                 ; centaines, gardees pour les zeros
        push    A                       ; reste (dizaines et unites)
        mov     B,A
        jeq     @pd3_sp
        add     %$30,A
        br      @pd3_w
pd3_sp:
        mov     %$20,A
pd3_w:
        call    @put_ch
        pop     A
        br      @pd_tens
put_dec2:
        clr     B
        mov     B,TEMP5
pd_tens:
        clr     B
pd2_c:
        cmp     %10,A
        jl      @pd2_o
        sub     %10,A
        inc     B
        br      @pd2_c
pd2_o:
        push    A                       ; unites
        mov     B,A                     ; dizaines
        or      TEMP5,A                 ; dizaine nulle et pas de centaine :
        jeq     @pd2_sp                 ; espace
        mov     B,A
        add     %$30,A
        br      @pd2_w
pd2_sp:
        mov     %$20,A
pd2_w:
        call    @put_ch
        pop     A
        add     %$30,A
        call    @put_ch
        rets

; --- setAcmpxy : repris de mixpix (vdp.asm) : pointeur VDP = A:B ------------
setAcmpxy:
        movp    %$21,P45
        movp    B,P45
        movp    A,P45
        rets

; --- attentes (TMS7020 : ~2,9 us par DJNZ) ------------------------------------
delay_short:                            ; ~15 ms
        mov     %20,TEMP5
dsh_o:  mov     %255,TEMP6
dsh_i:  djnz    TEMP6,@dsh_i
        djnz    TEMP5,@dsh_o
        rets
delay_long:                             ; ~150 ms
        mov     %200,TEMP5
dlg_o:  mov     %255,TEMP6
dlg_i:  djnz    TEMP6,@dlg_i
        djnz    TEMP5,@dlg_o
        rets

; ============================================================================
; DONNEES
; ============================================================================
s_title:   .byte   "EXEL",1,2,3,4,"C",5,6,7,0   ; 1-7 = p i c o a r t minuscules
s_warn1:   .byte   "USB : CLAVIER, MANETTE, SOURIS",0     ; v5.2 : sous le titre
s_warn2:   .byte   "JAMAIS DE PC NI DE CHARGEUR !",0      ; (en rouge)
s_games:   .byte   " JEUX",0
s_page:    .byte   "PAGE ",0
s_help1:   .byte   "HAUT BAS:CHOISIR  GAUCHE DROITE:PAGE",0
s_help2:   .byte   "ESPACE, ENTREE OU FEU : LANCER LE JEU",0
s_loading: .byte   "  CHARGEMENT DU JEU...                  ",0
s_empty:   .byte   "AUCUN JEU - VOIR EXLPICO",0
s_none:    .byte   0
s_blank40: .byte   "                                        ",0
s_cr_ask1: .byte   " EXELMEMOIRE : SON CONTENU SERA EFFACE",0
s_cr_ask2: .byte   " ENTREE OU FEU : CHARGER  AUTRE : NON",0
s_cr_load: .byte   " CHARGEMENT DE L'EXELMEMOIRE...",0
s_cr_ok_s: .byte   " CHARGEE.  S : SANS CARTOUCHE (RESET)",0
s_cr_go:   .byte   " ENTREE OU FEU : DEMARRER  AUTRE : MENU",0
s_cr_ok_na: .byte  " CHARGEE, SANS DEMARRAGE AUTOMATIQUE",0
s_cr_go_na: .byte  " ENTREE/FEU/S:SANS CARTOUCHE AUTRE:MENU",0
s_cr_rst1: .byte   " APPUYEZ SUR RESET : DEMARRAGE SANS",0
s_cr_rst2: .byte   " CARTOUCHE.  UNE TOUCHE : ANNULER",0
s_cr_nopico: .byte " LE PICO NE REPOND PAS (FIRMWARE ?)",0
s_cr_bad:  .byte   " RELECTURE FAUSSE : EXELMEMOIRE ?",0
s_cr_key:  .byte   " UNE TOUCHE POUR REVENIR AU MENU",0
s_help2m:  .byte   "ENTREE/FEU:LANCER M:SAUVER EXELMEMOIRE",0
s_xm_ask1: .byte   " SAUVER L'EXELMEMOIRE DANS LE PICO ?",0
s_xm_ask2: .byte   " ENTREE OU FEU : OUI  AUTRE : NON",0
s_xm_send: .byte   " SAUVEGARDE DE L'EXELMEMOIRE...",0
s_xm_ok:   .byte   " SAUVEE DANS LE PICO, EMPLACEMENT ",0
s_xm_ok2:  .byte   " A RELIRE AVEC EXLPICO (USB). UNE TOUCHE",0
s_xm_bad:  .byte   " TRANSMISSION FAUSSE : RECOMMENCER",0
s_xm_flash: .byte  " ERREUR D'ECRITURE DANS LE PICO",0
s_xm_none: .byte   " AUCUNE EXELMEMOIRE DETECTEE",0

; zone d'echange EXLCART (16 octets) : manette DB9 et souris USB, ecrites par le
; Pico ; $00 sans EXLCART (le menu se pilote alors au clavier seulement)
exl_io:
exl_joy0:  .byte   $00                  ; b0 haut b1 bas b2 gauche b3 droite b4 feu
exl_joy1:  .byte   $00
exl_jpres: .byte   $00
           .byte   $00
           .byte   "EXLP"
exl_mx:    .byte   $00
exl_my:    .byte   $00
exl_mbtn:  .byte   $00
exl_mpres: .byte   $00
           .byte   $00,$00,$00,$00

; adresses VRAM des lignes TEXTE : 0 (le haut du bandeau, SCREEN_BASE) et
; 22-24, la ligne d'etat (BMP_BASE + 210 x 122 = $6A66, puis + 82) ; les
; lignes 1-21 sont la bande bitmap (pas de texte)
row_hi:    .byte   $06,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$6A,$6A,$6B
row_lo:    .byte   $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$66,$B8,$0A

;
; le papillon d'Exelvision : extrait des donnees que le 7041 de l'EXL100 envoie
; a la ROM pour l'ecran d'allumage ($FC18 dans sa ROM, commande $0D), dessine
; comme le fait le trap 11 de la ROM puis converti en bitmap (sans le mot
; "exelvision"). 44 lignes x 6 groupes (bleu, vert, rouge ; bit 7 = gauche) ;
; couleurs : 0 noir, 1 rouge, 2 vert, 3 jaune, 4 bleu, 5 magenta, 6 cyan.
logo_bmp:
        .byte   $FF,$FF,$00,$F9,$F9,$00,$FF,$FF,$00,$FF,$FF,$00,$9F,$9F,$00,$FF,$FF,$00  ; ligne 0
        .byte   $FF,$FF,$00,$F9,$F9,$00,$FF,$FF,$00,$FF,$FF,$00,$9F,$9F,$00,$FF,$FF,$00  ; ligne 1
        .byte   $FF,$FF,$00,$FE,$FE,$00,$7F,$7F,$00,$FE,$FE,$00,$7F,$7F,$00,$FF,$FF,$00  ; ligne 2
        .byte   $FF,$FF,$00,$FE,$FE,$00,$7F,$7F,$00,$FE,$FE,$00,$7F,$7F,$00,$FF,$FF,$00  ; ligne 3
        .byte   $FF,$FF,$00,$FF,$FF,$00,$9F,$9F,$00,$F9,$F9,$00,$FF,$FF,$00,$FF,$FF,$00  ; ligne 4
        .byte   $FF,$FF,$00,$FF,$FF,$00,$9F,$9F,$00,$F9,$F9,$00,$FF,$FF,$00,$FF,$FF,$00  ; ligne 5
        .byte   $F8,$F8,$00,$01,$01,$00,$E7,$E7,$00,$E7,$E7,$00,$80,$80,$00,$1F,$1F,$00  ; ligne 6
        .byte   $F8,$F8,$00,$01,$01,$00,$E7,$E7,$00,$E7,$E7,$00,$80,$80,$00,$1F,$1F,$00  ; ligne 7
        .byte   $F8,$F9,$01,$00,$F8,$F8,$79,$79,$00,$9E,$9E,$00,$00,$1F,$1F,$1F,$9F,$80  ; ligne 8
        .byte   $F8,$F9,$01,$00,$F8,$F8,$79,$79,$00,$9E,$9E,$00,$00,$1F,$1F,$1F,$9F,$80  ; ligne 9
        .byte   $E0,$E1,$01,$00,$FE,$FE,$1E,$1E,$00,$78,$78,$00,$00,$7F,$7F,$07,$87,$80  ; ligne 10
        .byte   $E0,$E1,$01,$00,$FE,$FE,$1E,$1E,$00,$78,$78,$00,$00,$7F,$7F,$07,$87,$80  ; ligne 11
        .byte   $E0,$E7,$07,$00,$FF,$FF,$06,$86,$80,$60,$61,$01,$00,$FF,$FF,$07,$E7,$E0  ; ligne 12
        .byte   $E0,$E7,$07,$00,$FF,$FF,$06,$86,$80,$60,$61,$01,$00,$FF,$FF,$07,$E7,$E0  ; ligne 13
        .byte   $80,$87,$07,$00,$FF,$FF,$00,$E0,$E0,$00,$07,$07,$00,$FF,$FF,$01,$E1,$E0  ; ligne 14
        .byte   $87,$80,$07,$FF,$00,$FF,$E0,$00,$E0,$07,$00,$07,$FF,$00,$FF,$E1,$01,$E0  ; ligne 15
        .byte   $80,$9F,$1F,$00,$FF,$FF,$00,$FE,$FE,$00,$7F,$7F,$00,$FF,$FF,$01,$F9,$F8  ; ligne 16
        .byte   $80,$9F,$1F,$00,$FF,$FF,$00,$FE,$FE,$00,$7F,$7F,$00,$FF,$FF,$01,$F9,$F8  ; ligne 17
        .byte   $87,$80,$07,$FF,$00,$FF,$FE,$00,$FE,$7F,$00,$7F,$FF,$00,$FF,$E1,$01,$E0  ; ligne 18
        .byte   $80,$87,$07,$00,$FF,$FF,$00,$FE,$FE,$00,$7F,$7F,$00,$FF,$FF,$01,$E1,$E0  ; ligne 19
        .byte   $E1,$E0,$01,$FF,$00,$FF,$FE,$00,$FE,$7F,$00,$7F,$FF,$00,$FF,$87,$07,$80  ; ligne 20
        .byte   $E1,$E0,$01,$FF,$00,$FF,$FE,$00,$FE,$7F,$00,$7F,$FF,$00,$FF,$87,$07,$80  ; ligne 21
        .byte   $F8,$F8,$00,$00,$00,$1F,$00,$00,$FE,$00,$00,$7F,$00,$00,$F8,$1F,$1F,$00  ; ligne 22
        .byte   $F8,$F8,$00,$1F,$00,$1F,$FE,$00,$FE,$7F,$00,$7F,$F8,$00,$F8,$1F,$1F,$00  ; ligne 23
        .byte   $FF,$FF,$00,$80,$80,$00,$00,$00,$7E,$00,$00,$7E,$01,$01,$00,$FF,$FF,$00  ; ligne 24
        .byte   $FF,$FF,$00,$80,$80,$00,$7E,$00,$7E,$7E,$00,$7E,$01,$01,$00,$FF,$FF,$00  ; ligne 25
        .byte   $FF,$FF,$00,$FE,$FE,$00,$00,$00,$7E,$00,$00,$7E,$7F,$7F,$00,$FF,$FF,$00  ; ligne 26
        .byte   $FF,$FF,$00,$FE,$FE,$00,$00,$00,$7E,$00,$00,$7E,$7F,$7F,$00,$FF,$FF,$00  ; ligne 27
        .byte   $FF,$FF,$00,$F8,$F8,$00,$00,$00,$06,$00,$00,$60,$1F,$1F,$00,$FF,$FF,$00  ; ligne 28
        .byte   $FF,$FF,$00,$F8,$F8,$00,$00,$00,$06,$00,$00,$60,$1F,$1F,$00,$FF,$FF,$00  ; ligne 29
        .byte   $FF,$FF,$00,$E0,$E1,$00,$00,$E0,$00,$00,$07,$00,$07,$87,$00,$FF,$FF,$00  ; ligne 30
        .byte   $FF,$FF,$00,$E0,$E1,$00,$00,$E0,$00,$00,$07,$00,$07,$87,$00,$FF,$FF,$00  ; ligne 31
        .byte   $FF,$FF,$00,$80,$87,$00,$01,$F9,$00,$80,$9F,$00,$01,$E1,$00,$FF,$FF,$00  ; ligne 32
        .byte   $FF,$FF,$00,$80,$87,$00,$01,$F9,$00,$80,$9F,$00,$01,$E1,$00,$FF,$FF,$00  ; ligne 33
        .byte   $FE,$FE,$00,$1F,$00,$00,$E1,$01,$00,$87,$80,$00,$F8,$00,$00,$7F,$7F,$00  ; ligne 34
        .byte   $FE,$FE,$00,$00,$1F,$00,$01,$E1,$00,$80,$87,$00,$00,$F8,$00,$7F,$7F,$00  ; ligne 35
        .byte   $FE,$FE,$00,$00,$7F,$00,$07,$E7,$00,$E0,$E7,$00,$00,$FE,$00,$7F,$7F,$00  ; ligne 36
        .byte   $FE,$FE,$00,$7F,$00,$00,$E7,$07,$00,$E7,$E0,$00,$FE,$00,$00,$7F,$7F,$00  ; ligne 37
        .byte   $FE,$FE,$00,$00,$1F,$00,$07,$87,$00,$E0,$E1,$00,$00,$F8,$00,$7F,$7F,$00  ; ligne 38
        .byte   $FE,$FE,$00,$1F,$00,$00,$87,$07,$00,$E1,$E0,$00,$F8,$00,$00,$7F,$7F,$00  ; ligne 39
        .byte   $FF,$FF,$00,$87,$80,$00,$9F,$1F,$00,$F9,$F8,$00,$E1,$01,$00,$FF,$FF,$00  ; ligne 40
        .byte   $FF,$FF,$00,$87,$80,$00,$9F,$1F,$00,$F9,$F8,$00,$E1,$01,$00,$FF,$FF,$00  ; ligne 41
        .byte   $FF,$FF,$00,$E0,$E0,$00,$1F,$1F,$00,$F8,$F8,$00,$07,$07,$00,$FF,$FF,$00  ; ligne 42
        .byte   $FF,$FF,$00,$E0,$E0,$00,$1F,$1F,$00,$F8,$F8,$00,$07,$07,$00,$FF,$FF,$00  ; ligne 43

; police 5x7 de mixpix (10 octets par glyphe, ligne du BAS en premier),
; codes $20..$5F, CH_BLANK ($60), puis p i c o a r t minuscules ($61-$67)
font_chr:
        .byte   $00,$00,$00,$00,$00,$00,$00,$00,$00,$00  ; $20 espace (vide)
        .byte   $00,$00,$20,$00,$00,$20,$20,$20,$20,$00  ; $21 !
        .byte   $00,$00,$00,$00,$00,$00,$50,$50,$50,$00  ; $22 "
        .byte   $00,$00,$50,$50,$F8,$50,$F8,$50,$50,$00  ; $23 #
        .byte   $00,$00,$20,$F0,$28,$70,$A0,$78,$20,$00  ; $24 $
        .byte   $00,$00,$18,$98,$40,$20,$10,$C8,$C0,$00  ; $25 %
        .byte   $00,$00,$68,$90,$A8,$40,$A0,$90,$60,$00  ; $26 &
        .byte   $00,$00,$00,$00,$00,$00,$40,$20,$60,$00  ; $27 '
        .byte   $00,$00,$10,$20,$40,$40,$40,$20,$10,$00  ; $28 (
        .byte   $00,$00,$40,$20,$10,$10,$10,$20,$40,$00  ; $29 )
        .byte   $00,$00,$00,$20,$A8,$70,$A8,$20,$00,$00  ; $2A *
        .byte   $00,$00,$00,$20,$20,$F8,$20,$20,$00,$00  ; $2B +
        .byte   $00,$00,$40,$20,$60,$00,$00,$00,$00,$00  ; $2C ,
        .byte   $00,$00,$00,$00,$00,$F8,$00,$00,$00,$00  ; $2D -
        .byte   $00,$00,$60,$60,$00,$00,$00,$00,$00,$00  ; $2E .
        .byte   $00,$00,$00,$80,$40,$20,$10,$08,$00,$00  ; $2F /
        .byte   $00,$00,$70,$88,$C8,$A8,$98,$88,$70,$00  ; $30 0
        .byte   $00,$00,$70,$20,$20,$20,$20,$60,$20,$00  ; $31 1
        .byte   $00,$00,$F8,$40,$20,$10,$08,$88,$70,$00  ; $32 2
        .byte   $00,$00,$70,$88,$08,$10,$20,$10,$F8,$00  ; $33 3
        .byte   $00,$00,$10,$10,$F8,$90,$50,$30,$10,$00  ; $34 4
        .byte   $00,$00,$70,$88,$08,$08,$F0,$80,$F8,$00  ; $35 5
        .byte   $00,$00,$70,$88,$88,$F0,$80,$40,$30,$00  ; $36 6
        .byte   $00,$00,$40,$40,$40,$20,$10,$08,$F8,$00  ; $37 7
        .byte   $00,$00,$70,$88,$88,$70,$88,$88,$70,$00  ; $38 8
        .byte   $00,$00,$60,$10,$08,$78,$88,$88,$70,$00  ; $39 9
        .byte   $00,$00,$00,$30,$30,$00,$30,$30,$00,$00  ; $3A :
        .byte   $00,$00,$40,$20,$60,$00,$60,$60,$00,$00  ; $3B ;
        .byte   $00,$00,$10,$20,$40,$80,$40,$20,$10,$00  ; $3C <
        .byte   $00,$00,$00,$00,$F8,$00,$F8,$00,$00,$00  ; $3D =
        .byte   $00,$00,$40,$20,$10,$08,$10,$20,$40,$00  ; $3E >
        .byte   $00,$00,$20,$00,$20,$10,$08,$88,$70,$00  ; $3F ?
        .byte   $00,$00,$70,$A8,$A8,$68,$08,$88,$70,$00  ; $40 @
        .byte   $00,$00,$88,$88,$F8,$88,$88,$88,$70,$00  ; $41 A
        .byte   $00,$00,$F0,$88,$88,$F0,$88,$88,$F0,$00  ; $42 B
        .byte   $00,$00,$70,$88,$80,$80,$80,$88,$70,$00  ; $43 C
        .byte   $00,$00,$E0,$90,$88,$88,$88,$90,$E0,$00  ; $44 D
        .byte   $00,$00,$F8,$80,$80,$F0,$80,$80,$F8,$00  ; $45 E
        .byte   $00,$00,$80,$80,$80,$F0,$80,$80,$F8,$00  ; $46 F
        .byte   $00,$00,$78,$88,$88,$B8,$80,$88,$70,$00  ; $47 G
        .byte   $00,$00,$88,$88,$88,$F8,$88,$88,$88,$00  ; $48 H
        .byte   $00,$00,$70,$20,$20,$20,$20,$20,$70,$00  ; $49 I
        .byte   $00,$00,$60,$90,$10,$10,$10,$10,$38,$00  ; $4A J
        .byte   $00,$00,$88,$90,$A0,$C0,$A0,$90,$88,$00  ; $4B K
        .byte   $00,$00,$F8,$80,$80,$80,$80,$80,$80,$00  ; $4C L
        .byte   $00,$00,$88,$88,$88,$A8,$A8,$D8,$88,$00  ; $4D M
        .byte   $00,$00,$88,$88,$98,$A8,$C8,$88,$88,$00  ; $4E N
        .byte   $00,$00,$70,$88,$88,$88,$88,$88,$70,$00  ; $4F O
        .byte   $00,$00,$80,$80,$80,$F0,$88,$88,$F0,$00  ; $50 P
        .byte   $00,$00,$68,$90,$A8,$88,$88,$88,$70,$00  ; $51 Q
        .byte   $00,$00,$88,$90,$A0,$F0,$88,$88,$F0,$00  ; $52 R
        .byte   $00,$00,$F0,$08,$08,$70,$80,$80,$78,$00  ; $53 S
        .byte   $00,$00,$20,$20,$20,$20,$20,$20,$F8,$00  ; $54 T
        .byte   $00,$00,$70,$88,$88,$88,$88,$88,$88,$00  ; $55 U
        .byte   $00,$00,$20,$50,$88,$88,$88,$88,$88,$00  ; $56 V
        .byte   $00,$00,$50,$A8,$A8,$A8,$88,$88,$88,$00  ; $57 W
        .byte   $00,$00,$88,$88,$50,$20,$50,$88,$88,$00  ; $58 X
        .byte   $00,$00,$20,$20,$20,$50,$88,$88,$88,$00  ; $59 Y
        .byte   $00,$00,$F8,$80,$40,$20,$10,$08,$F8,$00  ; $5A Z
        .byte   $00,$00,$70,$40,$40,$40,$40,$40,$70,$00  ; $5B [
        .byte   $00,$00,$00,$08,$10,$20,$40,$80,$00,$00  ; $5C \
        .byte   $00,$00,$70,$10,$10,$10,$10,$10,$70,$00  ; $5D ]
        .byte   $00,$00,$00,$00,$00,$00,$88,$50,$20,$00  ; $5E ^
        .byte   $00,$00,$F8,$00,$00,$00,$00,$00,$00,$00  ; $5F _
        .byte   $00,$00,$00,$00,$00,$00,$00,$00,$00,$00  ; $60 CH_BLANK (vide)
        .byte   $80,$80,$F0,$88,$88,$88,$F0,$00,$00,$00  ; $61 p minuscule (code 1)
        .byte   $00,$00,$70,$20,$20,$20,$60,$00,$20,$00  ; $62 i minuscule (code 2)
        .byte   $00,$00,$78,$80,$80,$80,$78,$00,$00,$00  ; $63 c minuscule (code 3)
        .byte   $00,$00,$70,$88,$88,$88,$70,$00,$00,$00  ; $64 o minuscule (code 4)
        .byte   $00,$00,$78,$88,$78,$08,$70,$00,$00,$00  ; $65 a minuscule (code 5)
        .byte   $00,$00,$80,$80,$80,$C8,$B0,$00,$00,$00  ; $66 r minuscule (code 6)
        .byte   $00,$00,$30,$48,$40,$40,$F0,$40,$00,$00  ; $67 t minuscule (code 7)

        .end
