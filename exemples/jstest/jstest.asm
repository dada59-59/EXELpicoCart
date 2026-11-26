        .msfirst
; ============================================================================
; JSTEST.ASM -- test des manettes infrarouges de l'EXL100 / EXELTEL
; ============================================================================
; Affiche ce que la console recoit des manettes (et du clavier), tel que le
; moniteur le range : VALUE0 (R3) pour le clavier et la manette 1, VALUE1 (R4)
; pour la manette 2. Pour chacune :
;   - le code courant en hexadecimal et ce qu'il veut dire ;
;   - le nombre d'appels du moniteur (vecteurs BRJOY0 / BRJOY1) : un appel par
;     code recu, donc la cadence des repetitions quand on tient une direction ;
;   - les 8 derniers codes recus, du plus ancien au plus recent.
; Un caractere se deplace dans le cadre pour chaque manette : O pour la
; manette 1 (* quand le feu est appuye), X pour la manette 2 (+ avec le feu).
;
; Sert a comparer la vraie manette Exelvision et la manette DB9 envoyee par
; l'infrarouge de l'EXELpicoCart : les memes gestes doivent donner les memes
; codes, au meme rythme.
;
; Codes attendus (tables des jeux : Tennis, Virus, Guppy, Capitaine Menkar) :
;   $80 haut  $81 droite  $82 bas  $83 gauche
;   $4F haut-droite  $48 bas-droite  $3C bas-gauche  $54 haut-gauche
;   $20 feu  $5F feu + haut/bas  $4E feu + HG/BD  $47 feu + HD/BG
;   $04 relache
;
; ROM de 32 Ko, point d'entree $1000, sans zone d'echange : avec l'EXELpicoCart,
; l'infrarouge est donc actif. Fonctionne aussi sur une vraie EPROM.
; Assemblage : TASM + tasmEXL.TAB, ou asm7000.py d'EXLOUTILS :
;     python3 asm7000.py jstest.asm jstest.rom
; ============================================================================

TEMP2           .equ    16
TEMP3           .equ    18
TEMP4           .equ    20
TEMP7           .equ    26
TEMP8           .equ    28

VALUE0          .equ    3               ; R3 : clavier + manette 1 (rempli par le moniteur)
VALUE1          .equ    4               ; R4 : manette 2
BRJOY0          .equ    $C000           ; le moniteur appelle cette adresse a chaque code (R3)
BRJOY1          .equ    $C002           ; ... et celle-ci pour la manette 2 (R4)

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

; attributs : bits 7-5 = couleur (bleu, vert, rouge), $18 = banc BAGC3 (notre police)
A_WHITE         .equ    $F8
A_YELLOW        .equ    $78
A_CYAN          .equ    $D8
A_GREEN         .equ    $58
A_RED           .equ    $38
CH_BLANK        .equ    $60
NEXT_TEXT       .equ    $00

SCREEN_BASE     .equ    $0600
SCREEN_BASE_M1  .equ    $05FF
NB_ROWS         .equ    25
FONT_BAGC3      .equ    $7900
FONT_BAGC3_M2   .equ    $78FE

ROW_J1          .equ    3               ; lignes de la manette 1 (3, 4) et 2 (7, 8)
ROW_J2          .equ    7
FIELD_TOP       .equ    10              ; cadre : lignes 10 a 23, colonnes 0 a 39
FIELD_BOT       .equ    23
STEP_N          .equ    3               ; un pas toutes les 3 boucles (~80 ms)

; --- registres (le moniteur utilise R2-R35 ; la pile commence en R89)
C0H             .equ    36              ; appels BRJOY0, en BCD (4 chiffres)
C0L             .equ    37
C1H             .equ    38              ; appels BRJOY1
C1L             .equ    39
HP0             .equ    40              ; position du dernier code dans l'historique
HP1             .equ    41
X0              .equ    42              ; manette 1 : colonne, ligne
Y0              .equ    43
X1              .equ    44              ; manette 2
Y1              .equ    45
STEPC           .equ    46
PX              .equ    47              ; step : position, caracteres
PY              .equ    48
PCH             .equ    49
PFI             .equ    50
PATT            .equ    51
DX              .equ    52              ; dirof : deplacement, feu, libelle
DY              .equ    53
FI              .equ    54
LBL             .equ    55
AH              .equ    56              ; at : adresse VDP
AL              .equ    57
SROW            .equ    58              ; showj : ligne, compteur, historique
SCH             .equ    59
SCL             .equ    60
SHP             .equ    61
SBASE           .equ    62
SCODE           .equ    63
CNT             .equ    64
IDX             .equ    65
DL1             .equ    66
DL2             .equ    67
PH              .equ    68              ; puts : pointeur (PH = poids fort)
PL              .equ    69

HIST            .equ    $C500           ; 16 octets : manette 1 (0-7), manette 2 (8-15)

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

        call    @js_init
main:   call    @delay
        mov     VALUE0,A                ; manette 1
        mov     %ROW_J1,SROW
        mov     C0H,SCH
        mov     C0L,SCL
        mov     HP0,SHP
        clr     SBASE
        call    @showj
        mov     VALUE1,A                ; manette 2
        mov     %ROW_J2,SROW
        mov     C1H,SCH
        mov     C1L,SCL
        mov     HP1,SHP
        mov     %8,SBASE
        call    @showj
        djnz    STEPC,@main
        mov     %STEP_N,STEPC
        call    @moves
        jmp     @main

; ============================================================================
; js_init : ecran, variables, vecteurs (appele seul par les essais)
; ============================================================================
js_init:
        call    @build_page
        clr     C0H
        clr     C0L
        clr     C1H
        clr     C1L
        clr     HP0
        clr     HP1
        mov     %12,X0
        mov     %16,Y0
        mov     %27,X1
        mov     %16,Y1
        mov     %STEP_N,STEPC
        mov     %16,B                   ; historique a zero
        clr     A
ji_h:   sta     @HIST-1(B)
        djnz    B,@ji_h

        movd    %s_title,PL             ; textes fixes
        mov     %A_YELLOW,PATT
        clr     A
        clr     B
        call    @at
        call    @puts
        mov     %A_CYAN,PATT
        movd    %s_j1,PL
        mov     %ROW_J1-1,A
        clr     B
        call    @at
        call    @puts
        movd    %s_j2,PL
        mov     %ROW_J2-1,A
        clr     B
        call    @at
        call    @puts
        mov     %A_WHITE,PATT
        mov     %ROW_J1,A
        call    @labels
        mov     %ROW_J2,A
        call    @labels
        movd    %s_help,PL
        mov     %A_CYAN,PATT
        mov     %24,A
        clr     B
        call    @at
        call    @puts

        mov     %A_WHITE,PATT           ; le cadre
        mov     %FIELD_TOP,A
        call    @hline
        mov     %FIELD_BOT,A
        call    @hline
        mov     %FIELD_TOP+1,DL1
ji_v:   mov     DL1,A
        clr     B
        call    @at
        mov     %$21,A                  ; !
        call    @putc
        mov     DL1,A
        mov     %39,B
        call    @at
        mov     %$21,A
        call    @putc
        inc     DL1
        cmp     %FIELD_BOT,DL1
        jne     @ji_v

        dint                            ; les deux vecteurs du moniteur
        movd    %cb0,B                  ; A = poids fort, B = poids faible
        sta     @BRJOY0
        mov     B,A
        sta     @BRJOY0+1
        movd    %cb1,B
        sta     @BRJOY1
        mov     B,A
        sta     @BRJOY1+1
        eint
        call    @moves                  ; les deux caracteres a leur place
        rets

; "CODE $" et "RECUS" sur la ligne A, "DERNIERS :" sur la suivante
labels: mov     A,SROW
        mov     %1,B
        call    @at
        movd    %s_code,PL
        call    @puts
        mov     SROW,A
        mov     %24,B
        call    @at
        movd    %s_recus,PL
        call    @puts
        mov     SROW,A
        inc     A
        mov     %1,B
        call    @at
        movd    %s_last,PL
        call    @puts
        rets

; ligne de '-' sur toute la largeur (A = ligne)
hline:  clr     B
        call    @at
        mov     %40,CNT
hl_1:   mov     %$2D,A
        call    @putc
        djnz    CNT,@hl_1
        rets

; ============================================================================
; appels du moniteur : un code vient d'arriver (A et B sauvegardes par lui)
; ============================================================================
cb0:    clrc                            ; un appel de plus (BCD)
        dac     %1,C0L
        dac     %0,C0H
        mov     HP0,B                   ; historique : case suivante
        inc     B
        and     %7,B
        mov     B,HP0
        mov     VALUE0,A
        sta     @HIST(B)
        rets
cb1:    clrc
        dac     %1,C1L
        dac     %0,C1H
        mov     HP1,B
        inc     B
        and     %7,B
        mov     B,HP1
        or      %8,B
        mov     VALUE1,A
        sta     @HIST(B)
        rets

; ============================================================================
; showj : A = code courant ; SROW = ligne ; SCH:SCL = appels ; SHP = dernier
; code dans l'historique ; SBASE = 0 (manette 1) ou 8 (manette 2)
; ============================================================================
showj:  mov     A,SCODE
        mov     %A_WHITE,PATT
        mov     SROW,A                  ; le code, en hexadecimal
        mov     %7,B
        call    @at
        mov     SCODE,A
        call    @puthex
        mov     SCODE,A                 ; ce qu'il veut dire
        call    @dirof
        mov     SROW,A
        mov     %10,B
        call    @at
        mov     LBL,B
        lda     @lab_hi(B)
        mov     A,PH
        lda     @lab_lo(B)
        mov     A,PL
        call    @puts
        mov     SROW,A                  ; les appels
        mov     %31,B
        call    @at
        mov     SCH,A
        call    @puthex
        mov     SCL,A
        call    @puthex
        mov     SROW,A                  ; les 8 derniers codes
        inc     A
        mov     %12,B
        call    @at
        mov     SHP,IDX
        mov     %8,CNT
sj_h:   inc     IDX
        mov     IDX,A
        and     %7,A
        or      SBASE,A
        mov     A,B
        lda     @HIST(B)
        call    @puthex
        mov     %$20,A
        call    @putc
        djnz    CNT,@sj_h
        rets

; ============================================================================
; dirof : A = code -> DX, DY (-1 = $FF), FI (1 = feu), LBL (0 = inconnu)
; ============================================================================
dirof:  mov     %N_CODES,B
do_l:   cmpa    @t_code-1(B)
        jeq     @do_f
        djnz    B,@do_l
        clr     DX                      ; (B = 0 : code inconnu)
        clr     DY
        clr     FI
        clr     LBL
        rets
do_f:   mov     B,LBL
        lda     @t_dx-1(B)
        mov     A,DX
        lda     @t_dy-1(B)
        mov     A,DY
        lda     @t_fi-1(B)
        mov     A,FI
        rets

; ============================================================================
; moves : un pas pour chaque manette, dans la direction de son code
; ============================================================================
moves:  mov     VALUE0,A
        mov     X0,PX
        mov     Y0,PY
        mov     %$4F,PCH                ; O
        mov     %$2A,PFI                ; *
        mov     %A_GREEN,PATT
        call    @step
        mov     PX,X0
        mov     PY,Y0
        mov     VALUE1,A
        mov     X1,PX
        mov     Y1,PY
        mov     %$58,PCH                ; X
        mov     %$2B,PFI                ; +
        mov     %A_RED,PATT
        call    @step
        mov     PX,X1
        mov     PY,Y1
        rets

step:   call    @dirof
        mov     PY,A                    ; efface
        mov     PX,B
        call    @at
        mov     %CH_BLANK,A
        call    @putc
        mov     PX,A                    ; colonne : 1 a 38
        add     DX,A
        cmp     %1,A
        jl      @st_y
        cmp     %39,A
        jc      @st_y
        mov     A,PX
st_y:   mov     PY,A                    ; ligne : FIELD_TOP+1 a FIELD_BOT-1
        add     DY,A
        cmp     %FIELD_TOP+1,A
        jl      @st_d
        cmp     %FIELD_BOT,A
        jc      @st_d
        mov     A,PY
st_d:   mov     PY,A
        mov     PX,B
        call    @at
        mov     PCH,A
        btjz    %1,FI,@st_c
        mov     PFI,A
st_c:   call    @putc
        rets

; ============================================================================
; ecran texte : 25 lignes de 40 cases (attribut, caractere) + 2 octets
; ============================================================================
build_page:
        mov     %SCREEN_BASE>>8,A
        mov     %SCREEN_BASE&$FF,B
        movp    %$21,P45
        movp    B,P45
        movp    A,P45
        mov     %NB_ROWS,CNT
bp_r:   mov     %40,B
bp_c:   movp    %A_WHITE,P46
        movp    %CH_BLANK,P46
        djnz    B,@bp_c
        movp    %NEXT_TEXT,P46
        movp    %0,P46
        djnz    CNT,@bp_r
        rets

; at : A = ligne, B = colonne -> pointeur VDP sur la case
at:     push    B
        mov     A,B
        lda     @row_lo(B)
        mov     A,AL
        lda     @row_hi(B)
        mov     A,AH
        pop     A                       ; 2 octets par case
        mov     A,B
        add     B,A
        add     A,AL
        adc     %0,AH
        mov     AL,B
        mov     AH,A
        movp    %$21,P45
        movp    B,P45
        movp    A,P45
        rets

; putc : A = caractere, avec l'attribut PATT (A garde)
putc:   push    A
        mov     PATT,A
        movp    A,P46
        pop     A
        movp    A,P46
        rets

; puts : chaine terminee par 0 en PH:PL
puts:   lda     *PL
        jeq     @ps_e
        call    @putc
        add     %1,PL
        adc     %0,PH
        jmp     @puts
ps_e:   rets

; puthex : A en deux chiffres hexadecimaux
puthex: push    A
        rl      A
        rl      A
        rl      A
        rl      A
        call    @putnib
        pop     A
putnib: and     %$0F,A
        add     %$30,A
        cmp     %$3A,A
        jl      @pn_o
        add     %7,A
pn_o:   jmp     @putc

; ~10 ms (plus l'affichage : ~27 ms par tour)
delay:  mov     %14,DL1
dl_o:   mov     %255,DL2
dl_i:   djnz    DL2,@dl_i
        djnz    DL1,@dl_o
        rets

; ============================================================================
; donnees
; ============================================================================
N_CODES         .equ    14
t_code: .byte   $80, $81, $82, $83, $4F, $48, $3C, $54, $20, $5F, $4E, $47, $04, $00
t_dx:   .byte   $00, $01, $00, $FF, $01, $01, $FF, $FF, $00, $00, $00, $00, $00, $00
t_dy:   .byte   $FF, $00, $01, $00, $FF, $01, $01, $FF, $00, $00, $00, $00, $00, $00
t_fi:   .byte   $00, $00, $00, $00, $00, $00, $00, $00, $01, $01, $01, $01, $00, $00

lab_hi: .byte   l_aut>>8, l_h>>8, l_d>>8, l_b>>8, l_g>>8, l_hd>>8, l_bd>>8, l_bg>>8, l_hg>>8
        .byte   l_f>>8, l_fhb>>8, l_fhg>>8, l_fhd>>8, l_rel>>8, l_rien>>8
lab_lo: .byte   l_aut&$FF, l_h&$FF, l_d&$FF, l_b&$FF, l_g&$FF, l_hd&$FF, l_bd&$FF, l_bg&$FF, l_hg&$FF
        .byte   l_f&$FF, l_fhb&$FF, l_fhg&$FF, l_fhd&$FF, l_rel&$FF, l_rien&$FF

; libelles : 13 caracteres chacun (l'ancien est recouvert)
l_aut:  .text   "AUTRE TOUCHE "
        .byte   0
l_h:    .text   "HAUT         "
        .byte   0
l_d:    .text   "DROITE       "
        .byte   0
l_b:    .text   "BAS          "
        .byte   0
l_g:    .text   "GAUCHE       "
        .byte   0
l_hd:   .text   "HAUT-DROITE  "
        .byte   0
l_bd:   .text   "BAS-DROITE   "
        .byte   0
l_bg:   .text   "BAS-GAUCHE   "
        .byte   0
l_hg:   .text   "HAUT-GAUCHE  "
        .byte   0
l_f:    .text   "FEU          "
        .byte   0
l_fhb:  .text   "FEU+HAUT/BAS "
        .byte   0
l_fhg:  .text   "FEU+HG/BD    "
        .byte   0
l_fhd:  .text   "FEU+HD/BG    "
        .byte   0
l_rel:  .text   "RELACHE      "
        .byte   0
l_rien: .text   "RIEN         "
        .byte   0

s_title: .text  " TEST DES MANETTES  EXL100/EXELPICOCART"
        .byte   0
s_j1:   .text   "MANETTE 1 ET CLAVIER (R3)"
        .byte   0
s_j2:   .text   "MANETTE 2 (R4)"
        .byte   0
s_code: .text   "CODE $"
        .byte   0
s_recus: .text  "APPELS"
        .byte   0
s_last: .text   "DERNIERS :"
        .byte   0
s_help: .text   " MANETTE 1 : O   MANETTE 2 : X   FEU *+"
        .byte   0

; adresses des 25 lignes : SCREEN_BASE + 82 x ligne
row_hi: .byte   $06,$06,$06,$06,$07,$07,$07,$08,$08,$08,$09,$09,$09,$0A,$0A,$0A,$0B,$0B,$0B,$0C,$0C,$0C,$0D,$0D,$0D
row_lo: .byte   $00,$52,$A4,$F6,$48,$9A,$EC,$3E,$90,$E2,$34,$86,$D8,$2A,$7C,$CE,$20,$72,$C4,$16,$68,$BA,$0C,$5E,$B0

; police 5 x 7 (10 octets par glyphe, ligne du bas d'abord), codes $20 a $60
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

        .end
