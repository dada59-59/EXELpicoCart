        .msfirst                        ; mots poids fort d'abord (.word)
; ============================================================================
; BADAPPLE.ASM -- lecteur video noir et blanc pour EXL100 / EXELTEL
;                 avec la cartouche EXELpicoCart (EXLCART, firmware video)
; ============================================================================
; Le Pico fait tout le travail : il decode la video rangee dans sa flash,
; compare l'image du moment avec ce que l'EXL100 affiche deja, et prepare la
; liste des seules modifications, deja au format de la memoire video. L'EXL100
; ne calcule rien : il lit cette liste octet par octet A UNE SEULE ADRESSE de
; la cartouche (V_FIFO : le Pico y presente l'octet suivant apres chaque
; lecture) et recopie dans le VDP.
;
; Ecran : mode mixte du TMS3556, comme Lemmings (valide sur EXL100 et
; EXELTEL) : 1 ligne de texte en haut (titre, minuteur, images par seconde,
; ecrits par le Pico) puis 240 lignes bitmap de 320 pixels. 320 x 240 : le
; format 4/3 de la video.
;
; Protocole (adresses de la zone d'echange, en $7F00) :
;   lecture de V_RST  : (re)demarrage, l'ecran est vide ; le Pico repart du debut
;   lecture de V_SYNC : "image affichee, j'attends la suivante"
;   V_STAT            : 0 tant que la suivante n'est pas prete, puis 1
;   V_FIFO            : la liste, octet par octet. Le premier octet de chaque
;                       commande est un deplacement dans la table JT :
;     0   ADR    lo, hi : pointeur d'ecriture du VDP (ACMPxy) sur hi:lo
;     3   FIN    image terminee : retour a V_SYNC
;     6   BRUT   n puis n octets ecrits tels quels (texte, octets de controle)
;     9.. GROUPES : entree dans le bloc deroule, pour N groupes de 8 pixels
;                   (N = 1 a 27) : N octets, chacun ecrit 3 fois (plans bleu,
;                   vert, rouge : noir ou blanc). Deplacement = 9 + 9*(27-N).
;   Le firmware lit la geometrie et la table dans le descripteur EXLV.
;
; Musique 1 bit (si V_SON = 1) : la sortie cassette (bit 3 du port B, P6)
; est reliee au son du televiseur. La minuterie 1 tourne seule (prediviseur
; S_PS) ; a chaque debordement, le moniteur appelle notre interruption (son
; adresse en BRTIME, $C004), qui lit deux octets preparees par le Pico :
;   S_MASK : $08 = inverser la sortie, 0 = rien (simple reveil)
;   S_REL  : la recharge de la minuterie (prise au debordement SUIVANT)
; Le Pico date ces lectures, mesure la vitesse de la minuterie et place
; chaque inversion a l'heure de la partition, calee sur la video.
;
; Assemblage : TASM + tasmEXL.TAB, puis obj2exl ... -t:ROM -r:0x1000 -p
; (ou asm7000.py d'EXLOUTILS, qui donne les memes octets).
;
; Cout pour l'EXL100 : 41 cycles par groupe (lda 11 + 3 x movp 10), environ
; 120 cycles par saut d'adresse. Une image de Bad Apple change en moyenne
; d'environ 600 groupes : ~65 000 cycles, soit ~27 ms a 2,455 MHz.
;
; Sur une EPROM (sans EXELpicoCart), V_PRES reste a 0 : message et arret.
; ============================================================================

; --- registres du moniteur (comme EXL100.equ)
TEMP2           .equ    16
TEMP3           .equ    18
TEMP4           .equ    20
TEMP7           .equ    26
TEMP8           .equ    28

; --- VDP TMS3556 (valeurs de Lemmings / EXL100.equ)
COL             .equ    1
ROW             .equ    2
TIMEBASE        .equ    4
DECODER         .equ    5
SCRMODE         .equ    6
BORDER          .equ    7
REGBAPA         .equ    10
REGBAGC3        .equ    14
CM1_VAL         .equ    $10             ; CM1MASK
CM2_VAL         .equ    $C8             ; CM2MASK|LINEZERO|BAGC3 alphamosaique
CM3_VAL         .equ    $C8             ; CM3MASK|MIXTMODE : mode mixte
CM4_VAL         .equ    $00             ; marge noire

TEXT_ATTR       .equ    $F8             ; blanc, banc BAGC3 ($18), fond noir
CH_BLANK        .equ    $60             ; glyphe vide (jamais $20 : delimiteur)
NEXT_TEXT       .equ    $00             ; octet de controle : suivante = texte
NEXT_GRAPH      .equ    $10             ; C/G = 1 : suivante = bitmap

; --- memoire video (32 Ko) :
;   $0000-$04FF  police ROM (trap 13)
;   $0600-$78B1  la page : 1 ligne de texte (82 octets) + 240 lignes bitmap
;                (122 octets : 120 de pixels + 2 de controle)
;   $7900-$7DFF  notre police (BAGC3)
SCREEN_BASE     .equ    $0600
SCREEN_BASE_M1  .equ    $05FF           ; BAPA charge COL/ROW + 1
GRAPH_BASE      .equ    $0652           ; SCREEN_BASE + 82
GRAPH_LINES     .equ    240
LINE_LEN        .equ    122
FONT_BAGC3      .equ    $7900
FONT_BAGC3_M2   .equ    $78FE           ; les bases de generateur chargent COL/ROW + 2

; --- musique 1 bit : minuterie 1, sortie cassette
BRTIME          .equ    $C004           ; le moniteur appelle cette adresse a chaque
                                        ; interruption de la minuterie (si non nulle)
S_PS            .equ    3               ; prediviseur : une unite = 4 x 16 periodes
                                        ; d'horloge (13 us sur l'EXL100)
S_INIT          .equ    255             ; premiere recharge (comme le Pico a vide)

        .org    $1000

; ============================================================================
start:
        dint
        mov     %$58,B
        ldsp
        movp    P40,A                   ; comme Lemmings : P40/P36, puis
        movp    P36,A
        movp    P40,A                   ; init_vdp de mixt_api (P40/P36, CM1)
        movp    P36,A
        movp    %TIMEBASE,P45
        movp    %CM1_VAL,P45
        eint
        movd    %$0000,TEMP8            ; police ROM en $0000 (comme Lemmings :
        trap    13                      ; appel conserve tel quel)

        movp    %TIMEBASE,P45           ; CM1 a CM4 : ceux de Lemmings
        movp    %CM1_VAL,P45
        movp    %DECODER,P45
        movp    %CM2_VAL,P45
        movp    %SCRMODE,P45
        movp    %CM3_VAL,P45
        movp    %BORDER,P45
        movp    %CM4_VAL,P45

        movp    %COL,P45                ; BAGC3 -> FONT_BAGC3
        movp    %FONT_BAGC3_M2&$FF,P45
        movp    %ROW,P45
        movp    %FONT_BAGC3_M2>>8,P45
        movp    %REGBAGC3,P45
        movp    %0,P45
        movp    %0,P45

        movd    %font_chr,TEMP3         ; codes $20..$60
        movd    %FONT_BAGC3,TEMP2
        mov     %$20,TEMP7
        mov     %65,TEMP4-1
        trap    19
        movd    %font_chr,TEMP3         ; code $00 : vide aussi
        movd    %FONT_BAGC3,TEMP2
        clr     A
        mov     A,TEMP7
        mov     %1,TEMP4-1
        trap    19

        mov     %SCREEN_BASE_M1>>8,A    ; BAPA = SCREEN_BASE
        mov     %SCREEN_BASE_M1&$FF,B
        movp    %COL,P45
        movp    B,P45
        movp    %ROW,P45
        movp    A,P45
        movp    %REGBAPA,P45
        movp    %0,P45
        movp    %0,P45

        call    @build_page             ; texte vide + bitmap noir

        ; EXELpicoCart avec le firmware video ? Au demarrage, le Pico verifie
        ; la video rangee dans sa flash (quelques dizaines de ms) puis met
        ; V_PRES a $A5 (video presente) ou $5A (pas de video valide). On
        ; l'attend un peu plus d'une seconde ; sur une EPROM, V_PRES reste a 0.
        clr     R30
pw1:    clr     R31
pw2:    lda     @V_PRES
        cmp     %$A5,A
        jeq     @go
        cmp     %$5A,A
        jeq     @novid
        djnz    R31,@pw2
        djnz    R30,@pw1
        movd    %msg_pico,R33           ; pas d'EXELpicoCart
        jmp     @stop
novid:  movd    %msg_video,R33          ; EXELpicoCart sans video
stop:   call    @message
halt:   jmp     @halt

go:     lda     @V_RST                  ; ecran vide : le Pico repart du debut
        lda     @V_SYNC                 ; la premiere image
g_wait: lda     @V_STAT
        jeq     @g_wait
        lda     @V_SON                  ; une musique ? (le Pico met V_SON a 1)
        jeq     @g_go
        dint
        mov     %snd_irq>>8,A           ; notre interruption pour la minuterie
        sta     @BRTIME
        mov     %snd_irq&$FF,A
        sta     @BRTIME+1
        movp    %S_INIT,P2              ; recharge
        movp    %$80+S_PS,P3            ; minuterie 1 en marche, horloge interne
        movp    P0,A                    ; INT2 (minuterie) permise, son drapeau
        and     %$D5,A                  ; efface ; les autres drapeaux ne sont
        or      %$0C,A                  ; pas touches
        movp    A,P0
        eint
g_go:   br      @next                   ; la liste de la premiere image

; --- interruption de la minuterie (appelee par le moniteur, A et B sauves) ---
snd_irq:
        lda     @S_MASK                 ; $08 : inverser la sortie cassette
        xorp    A,P6
        lda     @S_REL                  ; recharge pour le debordement suivant
        movp    A,P2
        rets

; ============================================================================
; boucle des images
; ============================================================================
play:   lda     @V_SYNC                 ; image terminee, la suivante svp
wait:   lda     @V_STAT
        jeq     @wait
        lda     @V_FIFO
        mov     A,B
        br      @JT(B)

; --- table des commandes : le deplacement est lu dans la liste -----------
JT:     br      @c_addr                 ; 0  ADR
        br      @play                   ; 3  FIN
        br      @c_raw                  ; 6  BRUT
G_FIRST:                                ; 9  GROUPES : 27 x 9 octets
        lda     @V_FIFO                 ; groupe 27 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 26 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 25 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 24 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 23 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 22 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 21 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 20 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 19 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 18 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 17 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 16 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 15 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 14 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 13 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 12 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 11 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 10 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 9 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 8 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 7 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 6 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 5 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 4 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 3 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 2 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @V_FIFO                 ; groupe 1 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
next:   lda     @V_FIFO                 ; JT+252 : commande suivante
        mov     A,B
        br      @JT(B)

c_addr: movp    %$21,P45                ; COL/ROW en rafale : ACMPxy
        lda     @V_FIFO
        movp    A,P45                   ; poids faible
        lda     @V_FIFO
        movp    A,P45                   ; poids fort
        lda     @V_FIFO
        mov     A,B
        br      @JT(B)

c_raw:  lda     @V_FIFO                 ; deplacement dans RT : 5*(50-n)
        mov     A,B
        br      @RT(B)
RT:
        lda     @V_FIFO                 ; octet 50 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 49 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 48 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 47 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 46 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 45 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 44 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 43 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 42 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 41 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 40 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 39 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 38 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 37 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 36 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 35 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 34 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 33 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 32 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 31 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 30 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 29 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 28 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 27 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 26 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 25 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 24 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 23 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 22 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 21 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 20 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 19 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 18 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 17 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 16 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 15 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 14 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 13 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 12 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 11 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 10 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 9 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 8 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 7 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 6 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 5 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 4 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 3 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 2 avant la fin
        movp    A,P46
        lda     @V_FIFO                 ; octet 1 avant la fin
        movp    A,P46
        lda     @V_FIFO
        mov     A,B
        br      @JT(B)

; ============================================================================
; build_page -- la page mixte entiere (comme build_mixed_page de Lemmings) :
; 1 ligne texte vide, puis 240 lignes bitmap noires, chacune suivie de ses
; 2 octets de controle (type de la ligne SUIVANTE, puis 0).
; ============================================================================
build_page:
        mov     %SCREEN_BASE>>8,A
        mov     %SCREEN_BASE&$FF,B
        movp    %$21,P45
        movp    B,P45
        movp    A,P45
        mov     %40,B
bp_txt: movp    %TEXT_ATTR,P46
        movp    %CH_BLANK,P46
        djnz    B,@bp_txt
        movp    %NEXT_GRAPH,P46
        movp    %0,P46
        mov     %GRAPH_LINES,A
bp_gr:  mov     %120,B
bp_px:  movp    %0,P46
        djnz    B,@bp_px
        cmp     %1,A
        jne     @bp_g
        movp    %NEXT_TEXT,P46          ; derniere ligne : la suivante (la
        jmp     @bp_c                   ; premiere de l'ecran) est du texte
bp_g:   movp    %NEXT_GRAPH,P46
bp_c:   movp    %0,P46
        djnz    A,@bp_gr
        rets

; --- message de 40 caracteres (adresse en R32:R33) sur la ligne de texte -
message:
        mov     %SCREEN_BASE>>8,A
        mov     %SCREEN_BASE&$FF,B
        movp    %$21,P45
        movp    B,P45
        movp    A,P45
        mov     %40,B
ms_lp:  lda     *R33
        movp    %TEXT_ATTR,P46
        movp    A,P46
        add     %1,R33
        adc     %0,R32
        djnz    B,@ms_lp
        rets

msg_pico:  .text "```IL`FAUT`LA`CARTOUCHE`EXELPICOCART````"
msg_video: .text "``PAS`DE`VIDEO`DANS`LE`PICO`(EXLVIDEO)``"

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

; ============================================================================
; zone d'echange video (cherchee par le firmware : 4 x 00 puis "EXLV")
; ============================================================================
        .org    $7F00
V_FIFO: .byte   0                       ; +0  la liste, octet par octet
V_STAT: .byte   0                       ; +1  0 = pas prete, 1 = prete
V_PRES: .byte   0                       ; +2  $A5 quand le Pico gere la video
V_SYNC: .byte   0                       ; +3  lecture = image suivante svp
        .text   "EXLV"                  ; +4
        .byte   3                       ; +8  version du protocole (3 : musique 1 bit)
V_RST:  .byte   0                       ; +9  lecture = (re)demarrage
        .word   JT                      ; +10 table des commandes
        .byte   0, 3, 6                 ; +12 ADR, FIN, BRUT
        .byte   G_FIRST-JT, 27, 9       ; +15 groupes : premier, nombre, taille
        .word   RT                      ; +18 table des octets bruts
        .byte   50, 5                   ; +20 nombre, taille
        .word   SCREEN_BASE             ; +22 la ligne de texte en VRAM
        .word   GRAPH_BASE              ; +24 la premiere ligne bitmap
        .byte   LINE_LEN                ; +26 octets par ligne bitmap
        .byte   GRAPH_LINES             ; +27 lignes bitmap
        .byte   40                      ; +28 groupes par ligne
        .byte   TEXT_ATTR               ; +29 attribut du texte
        .byte   CH_BLANK                ; +30 glyphe vide
V_SON:  .byte   0                       ; +31 1 quand le Pico a une musique
S_MASK: .byte   0                       ; +32 lu par l'interruption : $08 = inverser
S_REL:  .byte   S_INIT                  ; +33 lu par l'interruption : recharge
        .byte   1                       ; +34 type de musique : 1 bit (sortie cassette)
        .byte   S_PS                    ; +35 prediviseur de la minuterie


        .end
