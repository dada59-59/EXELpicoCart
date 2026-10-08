; ============================================================================
; EXLMENU -- loader de la bibliotheque de jeux de la cartouche Pico (EXLCART)
; pour Exelvision EXL100 / EXELTEL, assembleur TMS7020.
;
; Assembler (avec 7020.equ, 3556.equ et mixt_api.asm dans le meme dossier) :
;     tasm -tEXL -a -b exlmenu.asm
;     obj2exl.exe exlmenu.obj exlmenu.rom -t:ROM -r:0x1000 -p
; puis donner exlmenu.rom a EXLPICO (carte "Bibliotheque") comme "loader".
;
; Fonctionnement :
;   - le Pico sert ce loader au demarrage, et y a INSERE la liste des jeux en
;     $7000-$7EFF (ne rien placer dans cette zone) ;
;   - fleches pour choisir, ESPACE ou ENTREE pour lancer (le lancement part
;     quand la touche est RELACHEE) ;
;   - le lancement est une simple LECTURE en $6F00+n (zone reservee elle
;     aussi) : le Pico charge le jeu n pendant que ce loader attend ; puis une
;     petite routine copiee en RAM lit $6FFF (le Pico bascule sur le jeu) et
;     execute BR @$7FFD, l'instruction de demarrage du pied du jeu, comme le
;     fait la ROM interne (pas de TRAP 0 : l'EXELTEL s'y arrete sur son ecran) ;
;   - un reset pendant une partie ramene a ce menu.
;
; Affichage repris de mixpix (valide sur machine reelle) : page mixte faite de
; 25 lignes TEXTE (25 x 10 lignes de balayage = l'ecran entier), police 5x7
; chargee par trap 19 dans BAGC3, espace ecrit comme le glyphe vide CH_BLANK.
; Clavier repris d'Exeltris : VALUE0, "aucune touche" = $04 ou $00.
; ============================================================================

#include "7020.equ"
#include "3556.equ"

#DEFINE Finit_vdp                       ; seule routine utilisee de mixt_api

; --- zones partagees avec le Pico (ne rien y assembler) ---------------------
CAT_COUNT       .equ    $7000           ; nombre de jeux (0..190)
CAT_NAMES       .equ    $7010           ; 20 caracteres par jeu, sans 0 final
NAME_LEN        .equ    20
SEL_HOTSPOT     .equ    $6F00           ; lire $6F00+n = lancer le jeu n
GO_HOTSPOT      .equ    $6FFF           ; lire $6FFF = le Pico bascule sur le jeu
TRAMP_RAM       .equ    $C510           ; routine de saut, copiee en RAM
TRAMP_LEN       .equ    6               ; ses 6 octets (voir tramp_src)

; --- ecran -----------------------------------------------------------------
TEXT_ATTR       .equ    FWHITE|$18|BBLACK   ; $18 = banc BAGC3
ATTR_SEL        .equ    FWHITE|$18|$04      ; fond BLEU : ligne choisie
ATTR_TITLE      .equ    FYELLOW|$18|BBLACK
ATTR_HELP       .equ    FCYAN|$18|BBLACK
BAGC3_MOSAIC    .equ    $08
FONT_BAGC3      .equ    $6800
FONT_BAGC3_M2   .equ    $67FE           ; FONT_BAGC3-2
CH_BLANK        .equ    $60             ; glyphe vide (au lieu de l'espace)
MIXT_NEXT_TEXT  .equ    $00
SCREEN_BASE     .equ    $0600           ; 25 lignes texte x 82 octets
SCREEN_BASE_M1  .equ    $05FF
NB_ROWS         .equ    25
PAGE_SIZE       .equ    20              ; jeux par page (lignes 3 a 22)
LIST_ROW        .equ    3
LIST_COL        .equ    6

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
        call    @init_vdp
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
        mov     %65,TEMP4-1
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

        call    @menu_init
menu_loop:
        call    @get_key
        call    @menu_key               ; ne revient pas si un jeu est lance
        br      @menu_loop

; --- etat initial et premier affichage (aussi appele seul par les tests) ----
menu_init:
        call    @build_text_page
        clr     A
        sta     @m_sel
        sta     @m_page
        lda     @CAT_COUNT
        cmp     %191,A                  ; liste illisible (cartouche sans Pico,
        jl      @mi_ok                  ; emulateur...) : aucun jeu
        clr     A
mi_ok:
        sta     @m_count
        call    @draw_static
        lda     @m_count
        jeq     @mi_empty
        call    @draw_list
        rets
mi_empty:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        mov     %12,A
        sta     @m_row
        mov     %5,A
        sta     @m_col
        call    @goto_rc
        movd    %s_empty,TEMP3
        call    @put_str
        rets

; ============================================================================
; CLAVIER
; ============================================================================
; get_key : attend le relachement de la touche precedente, puis une touche
; nouvelle lue deux fois identique a ~15 ms d'intervalle. A = code.
get_key:
gk_up:
        mov     VALUE0,A
        jeq     @gk_poll
        cmp     %$04,A
        jne     @gk_up
gk_poll:
        mov     VALUE0,A
        jeq     @gk_poll
        cmp     %$04,A
        jeq     @gk_poll
        sta     @m_key
        call    @delay_short
        mov     VALUE0,A
        cmpa    @m_key
        jne     @gk_poll
        lda     @m_key
        rets

; menu_key : A = touche. Deplace la selection et redessine, ou lance le jeu.
menu_key:
        sta     @m_key
        lda     @m_count
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
        jeq     @mk_go
        cmp     %KEY_ENTER,A
        jeq     @mk_go
mk_end:
        rets
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
        sta     @m_page
        call    @draw_page_info
        call    @draw_list
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
        ; Demarrage DIRECT du jeu, sans repasser par la ROM interne : apres un
        ; TRAP 0, l'EXELTEL prend son chemin de redemarrage "a chaud", affiche
        ; son ecran rouge et attend une touche. Le pied d'une cartouche est une
        ; instruction : AA/55 puis "8C hh ll" = BR @hhll ; la ROM le verifie puis
        ; execute $7FFD. On fait pareil, depuis la RAM, car la cartouche change
        ; de contenu sous nos pieds : LDA @$6FFF (le Pico bascule sur le jeu des
        ; l'acces suivant), puis BR @$7FFD (l'instruction de demarrage du jeu).
        ; Si le jeu n'etait pas encore charge, $7FFD est celui du menu : le menu
        ; redemarre, sans blocage. Un reset lance aussi le jeu choisi.
        mov     %TRAMP_LEN,B
mk_cp:  lda     @tramp_src-1(B)
        sta     @TRAMP_RAM-1(B)
        djnz    B,@mk_cp
        br      @TRAMP_RAM

tramp_src:
        .byte   $8A,GO_HOTSPOT>>8,GO_HOTSPOT&$FF        ; LDA @$6FFF
        .byte   $8C,$7F,$FD                             ; BR  @$7FFD

; --- attendre que toutes les touches soient relachees ------------------------
; 8 lectures consecutives "aucune touche" ($00 ou $04), espacees de ~15 ms ;
; une touche encore enfoncee fait tout recommencer.
wait_release:
        mov     %8,TEMP7
wrl_chk:
        mov     VALUE0,A
        jeq     @wrl_ok
        cmp     %$04,A
        jne     @wait_release
wrl_ok:
        call    @delay_short
        djnz    TEMP7,@wrl_chk
        rets

; ============================================================================
; AFFICHAGE
; ============================================================================
; --- page : 25 lignes texte vides --------------------------------------------
build_text_page:
        mov     %SCREEN_BASE>>8,A
        mov     %SCREEN_BASE&$FF,B
        call    @setAcmpxy
        mov     %NB_ROWS,TEMP5
btp_row:
        mov     %40,B
btp_col:
        movp    %TEXT_ATTR,P46
        movp    %CH_BLANK,P46
        djnz    B,@btp_col
        movp    %MIXT_NEXT_TEXT,P46     ; ligne suivante : texte
        movp    %0,P46
        djnz    TEMP5,@btp_row
        rets

; --- titre, nombre de jeux, aide ----------------------------------------------
draw_static:
        mov     %ATTR_TITLE,A
        sta     @m_attr
        clr     A
        sta     @m_row
        mov     %8,A
        sta     @m_col
        call    @goto_rc
        movd    %s_title,TEMP3
        call    @put_str
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
        call    @put_str
        lda     @m_count
        jeq     @ds_end
        call    @draw_page_info
ds_end:
        rets

; --- ligne 1 : "nnn JEUX" a gauche, "PAGE p/q" a droite -----------------------
draw_page_info:
        mov     %TEXT_ATTR,A
        sta     @m_attr
        mov     %1,A
        sta     @m_row
        mov     %LIST_COL,A
        sta     @m_col
        call    @goto_rc
        lda     @m_count
        call    @put_dec3
        movd    %s_games,TEMP3
        call    @put_str
        mov     %25,A
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

; --- la liste : PAGE_SIZE lignes a partir de m_page ---------------------------
draw_list:
        movd    %CAT_NAMES,TEMP3        ; TEMP3 = nom du premier jeu de la page
        lda     @m_page
        jeq     @dl_go
        mov     A,B
dl_skip:
        add     %NAME_LEN,TEMP3
        adc     %0,TEMP3-1
        djnz    B,@dl_skip
dl_go:
        lda     @m_page
        sta     @m_idx
        mov     %LIST_ROW,A
        sta     @m_row
        mov     %PAGE_SIZE,A
        sta     @m_lines
dl_line:
        mov     %LIST_COL,A
        sta     @m_col
        call    @goto_rc
        lda     @m_idx
        cmpa    @m_count
        jc      @dl_blank               ; jc : index >= nombre de jeux
        mov     %TEXT_ATTR,B            ; attribut : surbrillance si choisi
        cmpa    @m_sel
        jne     @dl_attr
        mov     %ATTR_SEL,B
dl_attr:
        mov     B,A
        sta     @m_attr
        mov     %$20,A
        call    @put_ch
        lda     @m_idx                  ; numero affiche : index + 1
        inc     A
        call    @put_dec3
        mov     %$20,A
        call    @put_ch
        mov     %$20,A
        call    @put_ch
        mov     %NAME_LEN,TEMP5         ; le nom, 20 caracteres
        call    @put_n
        mov     %$20,A
        call    @put_ch
        br      @dl_next
dl_blank:
        mov     %TEXT_ATTR,A            ; au-dela du dernier jeu : ligne vide
        sta     @m_attr
        mov     %27,B
dl_bl:
        mov     %$20,A
        push    B
        call    @put_ch
        pop     B
        djnz    B,@dl_bl
        add     %NAME_LEN,TEMP3
        adc     %0,TEMP3-1
dl_next:
        lda     @m_idx
        inc     A
        sta     @m_idx
        lda     @m_row
        inc     A
        sta     @m_row
        lda     @m_lines
        dec     A
        sta     @m_lines
        jne     @dl_line
        rets

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
; (minuscules -> majuscules, espace et caracteres inconnus -> CH_BLANK)
put_ch:
        cmp     %$61,A
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
        lda     @m_attr
        movp    A,P46
        movp    B,P46
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
s_title:   .byte   "EXLCART - MENU DES JEUX",0
s_games:   .byte   " JEUX",0
s_page:    .byte   "PAGE ",0
s_help1:   .byte   "HAUT BAS:CHOISIR  GAUCHE DROITE:PAGE",0
s_help2:   .byte   "ESPACE OU ENTREE : LANCER LE JEU",0
s_loading: .byte   "  CHARGEMENT DU JEU...                  ",0
s_empty:   .byte   "AUCUN JEU : UTILISER EXLPICO",0

; adresses VRAM des 25 lignes texte : SCREEN_BASE + 82 x ligne
row_hi:    .byte   $06,$06,$06,$06,$07,$07,$07,$08,$08,$08,$09,$09,$09,$0A,$0A,$0A,$0B,$0B,$0B,$0C,$0C,$0C,$0D,$0D,$0D
row_lo:    .byte   $00,$52,$A4,$F6,$48,$9A,$EC,$3E,$90,$E2,$34,$86,$D8,$2A,$7C,$CE,$20,$72,$C4,$16,$68,$BA,$0C,$5E,$B0

; police 5x7 de mixpix (10 octets par glyphe, ligne du BAS en premier),
; codes $20..$5F puis CH_BLANK ($60)
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

#include "mixt_api.asm"

        .end
