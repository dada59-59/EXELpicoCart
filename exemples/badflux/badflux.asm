        .msfirst
; ============================================================================
; BADFLUX.ASM -- lecteur video "disque" pour EXL100 / EXELTEL et EXELpicoCart
; ============================================================================
; Ici le Pico ne calcule rien : il fait le disque. Deux fichiers rangés dans
; sa flash (le film et la partition), chacun lu octet par octet a une seule
; adresse (D_DAT0, D_DAT1), comme le registre de donnees d'un controleur de
; disquette ; une lecture de D_RST les ramene au debut.
;
; L'EXL100 fait tout le reste :
; - LE FILM : une suite de listes preparees sur PC (ADR, GROUPES, BRUT, FIN,
;   comme le lecteur video), chacune precedee de son heure (24 bits, en unites
;   de 13,02 us). Le lecteur attend cette heure, puis l'execute. Si une liste
;   est en retard, il enchaine.
; - L'HORLOGE : la minuterie 1 du TMS7020, une unite = 13,02 us (prediviseur
;   choisi au demarrage : 3 sur l'EXL100, 7 si la minuterie va deux fois plus
;   vite, comme sur l'EXELTEL d'apres MAME).
; - LE TEXTE : minutes:secondes et images par seconde, chaque seconde.
; - LA MUSIQUE 1 BIT : la sortie cassette (bit 3 du port B) est reliee au son
;   du televiseur. La minuterie tourne seule ; a chaque debordement, son
;   interruption lit deux octets du fichier 1 (D_DAT1), prepares sur PC comme
;   les listes d'image : $08 = inverser la sortie (0 : rien), puis la recharge
;   de la minuterie. Le decompteur se recharge seul au debordement avec la
;   valeur ecrite AVANT : celle lue a l'interruption n fixe l'ecart entre les
;   debordements n+1 et n+2 ; le temps de reponse ne s'accumule donc pas.
;   C'est aussi l'horloge du film : chaque interruption ajoute son ecart.
;
; Assemblage : TASM + tasmEXL.TAB, ou asm7000.py d'EXLOUTILS.
; ============================================================================

TEMP2           .equ    16
TEMP3           .equ    18
TEMP4           .equ    20
TEMP7           .equ    26
TEMP8           .equ    28

COL             .equ    1
ROW             .equ    2
TIMEBASE        .equ    4
DECODER         .equ    5
SCRMODE         .equ    6
BORDER          .equ    7
REGBAPA         .equ    10
REGBAGC3        .equ    14
CM1_VAL         .equ    $10
CM2_VAL         .equ    $C8
CM3_VAL         .equ    $C8
CM4_VAL         .equ    $00

TEXT_ATTR       .equ    $F8             ; blanc, banc BAGC3
CH_BLANK        .equ    $60
NEXT_TEXT       .equ    $00
NEXT_GRAPH      .equ    $10

SCREEN_BASE     .equ    $0600
SCREEN_BASE_M1  .equ    $05FF
GRAPH_LINES     .equ    240
FONT_BAGC3      .equ    $7900
FONT_BAGC3_M2   .equ    $78FE
CLOCK_TXT       .equ    SCREEN_BASE+2*27 ; m:ss nn IM/S en colonnes 27-38

BRTIME          .equ    $C004           ; le moniteur appelle cette adresse a
                                        ; chaque interruption de la minuterie
ONE_SEC         .equ    300             ; 1 s = 76 800 unites = 300 x 256

; --- registres (le moniteur utilise R2-R35 ; la pile commence en R89)
PSV             .equ    36              ; prediviseur choisi
DM1             .equ    40              ; ecart en cours - 1 (la derniere recharge)
TMP             .equ    42
TH              .equ    43              ; heure : debordements de 256 unites (3,33 ms),
TM              .equ    44              ; 24 bits, + TF la fraction
TL              .equ    45
T2              .equ    48              ; calculs du programme principal
TSH             .equ    75              ; heure de la liste suivante
TSM             .equ    76
TSL             .equ    77
NSH             .equ    78              ; heure de la seconde suivante
NSM             .equ    79
NSL             .equ    80
SECB            .equ    81              ; secondes (BCD)
MINB            .equ    82              ; minutes (BCD)
FPSC            .equ    83              ; images de la seconde en cours (BCD)
FPSD            .equ    84              ; ... de la seconde passee
TF              .equ    85
REG_FIRST       .equ    37
REG_LAST        .equ    85

        .org    $1000

start:
        dint
        mov     %$58,B
        ldsp
        movp    P40,A
        movp    P36,A
        movp    P40,A
        movp    P36,A
        movp    %TIMEBASE,P45
        movp    %CM1_VAL,P45
        eint
        movd    %$0000,TEMP8
        trap    13

        movp    %TIMEBASE,P45
        movp    %CM1_VAL,P45
        movp    %DECODER,P45
        movp    %CM2_VAL,P45
        movp    %SCRMODE,P45
        movp    %CM3_VAL,P45
        movp    %BORDER,P45
        movp    %CM4_VAL,P45

        movp    %COL,P45
        movp    %FONT_BAGC3_M2&$FF,P45
        movp    %ROW,P45
        movp    %FONT_BAGC3_M2>>8,P45
        movp    %REGBAGC3,P45
        movp    %0,P45
        movp    %0,P45

        movd    %font_chr,TEMP3
        movd    %FONT_BAGC3,TEMP2
        mov     %$20,TEMP7
        mov     %65,TEMP4-1
        trap    19
        movd    %font_chr,TEMP3
        movd    %FONT_BAGC3,TEMP2
        clr     A
        mov     A,TEMP7
        mov     %1,TEMP4-1
        trap    19

        mov     %SCREEN_BASE_M1>>8,A
        mov     %SCREEN_BASE_M1&$FF,B
        movp    %COL,P45
        movp    B,P45
        movp    %ROW,P45
        movp    A,P45
        movp    %REGBAPA,P45
        movp    %0,P45
        movp    %0,P45

        call    @build_page

        ; EXELpicoCart en mode disque ? D_PRES = $A5 : les deux fichiers sont
        ; la ; $5A : pas de film ; 0 (EPROM) au bout d'une seconde : message.
        clr     R30
pw1:    clr     R31
pw2:    lda     @D_PRES
        cmp     %$A5,A
        jeq     @go
        cmp     %$5A,A
        jeq     @nofilm
        djnz    R31,@pw2
        djnz    R30,@pw1
        movd    %msg_pico,R33
        jmp     @stop
nofilm: movd    %msg_film,R33
stop:   call    @message
halt:   jmp     @halt

go:     call    @measure                ; vitesse de la minuterie -> PSV
        call    @rewind                 ; debut des fichiers, horloge a 0
        jmp     @nextl

; ============================================================================
; les listes : heure (3 octets), puis les commandes ; FIN -> la suivante
; ============================================================================
done:   clrc                            ; une image de plus cette seconde
        dac     %1,FPSC
nextl:  lda     @D_DAT0                 ; heure de la liste (24 bits, poids fort d'abord,
        mov     A,TSH                   ; en pas de 256 unites = 3,33 ms)
        lda     @D_DAT0
        mov     A,TSM
        lda     @D_DAT0
        mov     A,TSL
        and     TSM,A                   ; $FFFFFF : fin du film
        and     TSH,A
        cmp     %$FF,A
        jne     @wl
        call    @rewind
        jmp     @nextl
wl:     call    @tick                   ; la seconde est passee ?
        dint                            ; heure actuelle (copiee d'un coup)
        mov     TL,A
        mov     TM,B
        mov     TH,T2
        eint
        sub     TSL,A                   ; - heure de la liste
        sbb     TSM,B
        sbb     TSH,T2
        jn      @wl                     ; pas encore
        lda     @D_DAT0
        mov     A,B
        br      @JT(B)

JT:     br      @c_addr                 ; 0  ADR
        br      @done                   ; 3  FIN
        br      @c_raw                  ; 6  BRUT
G_FIRST:
        lda     @D_DAT0                 ; groupe 27 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 26 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 25 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 24 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 23 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 22 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 21 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 20 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 19 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 18 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 17 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 16 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 15 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 14 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 13 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 12 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 11 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 10 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 9 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 8 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 7 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 6 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 5 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 4 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 3 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 2 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
        lda     @D_DAT0                 ; groupe 1 avant la fin
        movp    A,P46                   ; plan bleu
        movp    A,P46                   ; plan vert
        movp    A,P46                   ; plan rouge
next:   lda     @D_DAT0
        mov     A,B
        br      @JT(B)

c_addr: movp    %$21,P45
        lda     @D_DAT0
        movp    A,P45
        lda     @D_DAT0
        movp    A,P45
        lda     @D_DAT0
        mov     A,B
        br      @JT(B)

c_raw:  lda     @D_DAT0
        mov     A,B
        br      @RT(B)
RT:
        lda     @D_DAT0                 ; octet 50 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 49 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 48 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 47 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 46 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 45 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 44 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 43 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 42 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 41 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 40 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 39 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 38 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 37 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 36 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 35 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 34 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 33 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 32 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 31 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 30 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 29 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 28 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 27 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 26 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 25 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 24 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 23 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 22 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 21 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 20 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 19 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 18 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 17 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 16 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 15 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 14 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 13 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 12 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 11 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 10 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 9 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 8 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 7 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 6 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 5 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 4 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 3 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 2 avant la fin
        movp    A,P46
        lda     @D_DAT0                 ; octet 1 avant la fin
        movp    A,P46
        lda     @D_DAT0
        mov     A,B
        br      @JT(B)

; ============================================================================
; tick : chaque seconde, m:ss et images par seconde (colonnes 27-33)
; ============================================================================
tick:   dint
        mov     TL,A
        mov     TM,B
        mov     TH,T2
        eint
        sub     NSL,A
        sbb     NSM,B
        sbb     NSH,T2
        jn      @tk_r
        add     %ONE_SEC&$FF,NSL
        adc     %ONE_SEC>>8,NSM
        adc     %0,NSH
        clrc
        dac     %1,SECB
        cmp     %$60,SECB
        jne     @tk_1
        clr     SECB
        clrc
        dac     %1,MINB
tk_1:   mov     FPSC,FPSD
        clr     FPSC
        call    @show
tk_r:   rets

show:   movp    %$21,P45                ; pointeur VDP : colonne 27 de la ligne de texte
        movp    %CLOCK_TXT&$FF,P45
        movp    %CLOCK_TXT>>8,P45
        mov     MINB,A
        and     %$0F,A
        or      %$30,A
        movp    %TEXT_ATTR,P46
        movp    A,P46
        movp    %TEXT_ATTR,P46
        movp    %$3A,P46                ; :
        mov     SECB,A
        call    @hi_dig
        movp    %TEXT_ATTR,P46
        movp    A,P46
        mov     SECB,A
        and     %$0F,A
        or      %$30,A
        movp    %TEXT_ATTR,P46
        movp    A,P46
        movp    %TEXT_ATTR,P46
        movp    %CH_BLANK,P46
        mov     FPSD,A                  ; dizaines (blanc si 0)
        call    @hi_dig
        cmp     %$30,A
        jne     @sh_1
        mov     %CH_BLANK,A
sh_1:   movp    %TEXT_ATTR,P46
        movp    A,P46
        mov     FPSD,A
        and     %$0F,A
        or      %$30,A
        movp    %TEXT_ATTR,P46
        movp    A,P46
        movp    %TEXT_ATTR,P46
        movp    %CH_BLANK,P46
        movp    %TEXT_ATTR,P46          ; IM/S
        movp    %$49,P46
        movp    %TEXT_ATTR,P46
        movp    %$4D,P46
        movp    %TEXT_ATTR,P46
        movp    %$2F,P46
        movp    %TEXT_ATTR,P46
        movp    %$53,P46
        rets

hi_dig: rr      A                       ; chiffre des dizaines d'un BCD -> '0'..'9'
        rr      A
        rr      A
        rr      A
        and     %$0F,A
        or      %$30,A
        rets

; ============================================================================
; measure : la minuterie va-t-elle a 16 periodes d'horloge (EXL100) ou plus
; vite ? On la laisse tourner ~800 cycles et on compte. Une unite de musique
; doit valoir 13,02 us : prediviseur 3 a 8 cycles par periode, 7 a 4 cycles.
; ============================================================================
measure:
        dint
        movp    %0,P3                   ; arretee
        movp    %255,P2
        movp    %$80,P3                 ; en marche, prediviseur 0
        movp    P2,A
        mov     A,TMP
        mov     %72,B                   ; ~800 cycles
ms_l:   djnz    B,@ms_l
        movp    P2,A
        mov     A,B
        mov     TMP,A
        sub     B,A                     ; periodes ecoulees
        movp    %0,P3
        mov     %3,PSV
        cmp     %150,A
        jnc     @ms_r                   ; moins de 150 : 8 cycles par periode
        mov     %7,PSV
ms_r:   eint
        rets

; ============================================================================
; rewind : fichiers au debut, ecran vide, horloge et musique a zero
; ============================================================================
rewind: dint
        movp    %0,P3                   ; minuterie arretee
        lda     @D_RST                  ; le Pico revient au debut des fichiers
rw_w:   lda     @D_PRES
        cmp     %$A5,A
        jne     @rw_w
        call    @build_page
        mov     %REG_FIRST,B            ; tous nos registres a 0
        clr     A
rw_c:   sta     *B                      ; (adresse = registre B)
        inc     B
        cmp     %REG_LAST+1,B
        jne     @rw_c
        mov     %ONE_SEC>>8,NSM
        mov     %ONE_SEC&$FF,NSL
        call    @show                   ; 0:00
        mov     %snd_irq>>8,A
        sta     @BRTIME
        mov     %snd_irq&$FF,A
        sta     @BRTIME+1
        mov     %15,DM1                 ; deux premiers ecarts : 16 unites
        movp    %15,P2
        mov     PSV,A
        or      %$80,A
        movp    A,P3                    ; en marche
        movp    P0,A                    ; INT2 permise, son drapeau efface
        and     %$D5,A
        or      %$0C,A
        movp    A,P0
        eint
        rets

; ============================================================================
; snd_irq : interruption de la minuterie (appelee par le moniteur, A et B
; sauves) : l'inversion prevue pour CE debordement, l'heure, puis la recharge
; qui fixera l'ecart d'apres le prochain debordement. ~85 cycles (plus ~100
; pour le moniteur).
; ============================================================================
snd_irq:
        lda     @D_DAT1                 ; $08 : inverser la sortie cassette
        and     %8,A                    ; (rien d'autre : apres la fin, $FF)
        xorp    A,P6
        setc                            ; heure : + ecart en cours (DM1 + 1)
        adc     DM1,TF
        jnc     @t_ok
        inc     TL
        jnc     @t_ok
        inc     TM
        jnc     @t_ok
        inc     TH
t_ok:   lda     @D_DAT1                 ; recharge, prise au debordement suivant
        mov     A,DM1
        movp    A,P2
        rets

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
        movp    %NEXT_TEXT,P46
        jmp     @bp_c
bp_g:   movp    %NEXT_GRAPH,P46
bp_c:   movp    %0,P46
        djnz    A,@bp_gr
        rets

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
msg_film:  .text "```PAS`DE`FILM`DANS`LE`PICO`(EXLFLUX)```"

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
; zone d'echange (cherchee par le firmware : 4 x 00 puis "EXLD")
; ============================================================================
        .org    $7F00
D_DAT0: .byte   0                       ; +0 fichier 0 (le film), octet par octet
D_DAT1: .byte   0                       ; +1 fichier 1 (la partition), octet par octet
D_PRES: .byte   0                       ; +2 $A5 : fichiers presents ; $5A : non
D_RST:  .byte   0                       ; +3 lecture = retour au debut des deux fichiers
        .text   "EXLD"                  ; +4
        .byte   1                       ; +8 version

        .end
