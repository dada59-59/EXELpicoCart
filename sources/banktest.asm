; ============================================================================
; BANKTEST -- test du changement de banque de 16 Ko sur la cartouche EXLCART
; (Pico) pour Exelvision EXL100 / EXELTEL, assembleur TMS7020.
;
; Organisation vue par la console (convention commune a EXLCART et EXLBANK) :
;   $0000-$3FFF  zone FIXE : ce programme (point d'entree $1000)
;   $3FF0-$3FFF  SIGNAL : LIRE $3FF0+n selectionne la banque n (n = 0..15) ;
;                ne rien assembler dans ces 16 octets
;   $4000-$7FFF  FENETRE : la banque courante (16 Ko) ; le pied $7FFC-$7FFF
;                est recopie dans chaque banque par EXLPICO
;
; Banque 0 = la moitie haute de CETTE ROM ($4000-$7FFF, voir la fin du fichier).
; Banque 1 = bank1.asm (assemble a part), ajoutee dans EXLPICO, carte "Banques".
;
; ESPACE : passe a la banque suivante (0, 1, 0, 1...), puis relit et affiche le
; texte trouve en $4000. Le Pico bascule apres l'acces au signal : la lecture
; suivante de la fenetre voit deja la nouvelle banque.
;
; Assembler (avec 7020.equ, 3556.equ et mixt_api.asm dans le meme dossier) :
;     tasm -tEXL -a -b banktest.asm
;     obj2exl.exe banktest.obj banktest.rom -t:ROM -r:0x1000 -p
; ============================================================================

#include "7020.equ"
#include "3556.equ"

#DEFINE Finit_vdp                       ; seule routine utilisee de mixt_api

; --- banques -----------------------------------------------------------------
BANK_SEL        .equ    $3FF0           ; lire $3FF0+n = banque n
BANK_WIN        .equ    $4000           ; debut de la fenetre
NB_BANKS        .equ    2               ; nombre de banques du test
TXT_LEN         .equ    30              ; caracteres lus et affiches

; --- ecran (repris d'EXLMENU / mixpix) ---------------------------------------
TEXT_ATTR       .equ    FWHITE|$18|BBLACK   ; $18 = banc BAGC3
ATTR_TITLE      .equ    FYELLOW|$18|BBLACK
ATTR_HELP       .equ    FCYAN|$18|BBLACK
ATTR_DATA       .equ    FWHITE|$18|$04      ; fond bleu : texte lu
BAGC3_MOSAIC    .equ    $08
FONT_BAGC3      .equ    $6800
FONT_BAGC3_M2   .equ    $67FE           ; FONT_BAGC3-2
CH_BLANK        .equ    $60
MIXT_NEXT_TEXT  .equ    $00
SCREEN_BASE     .equ    $0600
SCREEN_BASE_M1  .equ    $05FF
NB_ROWS         .equ    25
KEY_SPACE       .equ    $20

; --- variables (RAM externe) -------------------------------------------------
m_row           .equ    $C500
m_col           .equ    $C501
m_attr          .equ    $C502
m_key           .equ    $C503
b_bank          .equ    $C504           ; banque courante

        .org    $1000

; ============================================================================
; DEMARRAGE (identique a EXLMENU)
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

        call    @bt_init
bt_loop:
        call    @get_key
        cmp     %KEY_SPACE,A
        jne     @bt_loop
        lda     @b_bank                 ; banque suivante, en boucle
        inc     A
        cmp     %NB_BANKS,A
        jl      @bt_set
        clr     A
bt_set:
        sta     @b_bank
        call    @show_bank
        br      @bt_loop

; --- ecran fixe, puis banque 0 (aussi appele seul par les tests) -------------
bt_init:
        call    @build_text_page
        mov     %ATTR_TITLE,A
        sta     @m_attr
        mov     %2,A
        sta     @m_row
        mov     %6,A
        sta     @m_col
        call    @goto_rc
        movd    %s_title,TEMP3
        call    @put_str
        mov     %ATTR_HELP,A
        sta     @m_attr
        mov     %5,A
        sta     @m_row
        mov     %6,A
        sta     @m_col
        call    @goto_rc
        movd    %s_help,TEMP3
        call    @put_str
        mov     %TEXT_ATTR,A
        sta     @m_attr
        mov     %13,A
        sta     @m_row
        mov     %4,A
        sta     @m_col
        call    @goto_rc
        movd    %s_read,TEMP3
        call    @put_str
        clr     A
        sta     @b_bank
        ; (suite dans show_bank)

; --- show_bank : selectionne b_bank, puis affiche son numero et son texte ----
show_bank:
        lda     @b_bank
        mov     A,B
        lda     @BANK_SEL(B)            ; <- le signal : le Pico bascule
        mov     %TEXT_ATTR,A            ; "BANQUE n"
        sta     @m_attr
        mov     %10,A
        sta     @m_row
        mov     %4,A
        sta     @m_col
        call    @goto_rc
        movd    %s_bank,TEMP3
        call    @put_str
        lda     @b_bank
        add     %$30,A
        call    @put_ch
        mov     %ATTR_DATA,A            ; le texte lu dans la fenetre
        sta     @m_attr
        mov     %15,A
        sta     @m_row
        mov     %5,A
        sta     @m_col
        call    @goto_rc
        movd    %BANK_WIN,TEMP3
        mov     %TXT_LEN,TEMP5
sb_ch:
        lda     *TEMP3                  ; lecture dans la banque courante
        jeq     @sb_pad                 ; 0 : fin du texte
        call    @put_ch
        inc     TEMP3
        djnz    TEMP5,@sb_ch
        jmp     @sb_hex
sb_pad:                                 ; complete par des blancs
        mov     %$20,A
        call    @put_ch
        djnz    TEMP5,@sb_pad
sb_hex:                                 ; diagnostic : les 8 premiers octets
        mov     %TEXT_ATTR,A            ; de la fenetre, en hexadecimal
        sta     @m_attr                 ; (FF FF ... = banque vide)
        mov     %17,A
        sta     @m_row
        mov     %4,A
        sta     @m_col
        call    @goto_rc
        movd    %s_hex,TEMP3
        call    @put_str
        movd    %BANK_WIN,TEMP3
        mov     %8,TEMP5
sb_hx:
        lda     *TEMP3
        call    @put_hex
        mov     %$20,A
        call    @put_ch
        inc     TEMP3
        djnz    TEMP5,@sb_hx
        rets

; --- put_hex : A en deux chiffres hexadecimaux ------------------------------
put_hex:
        push    A
        swap    A                       ; quartet haut d'abord
        call    @put_nib
        pop     A
put_nib:
        and     %$0F,A
        add     %$30,A                  ; '0'..'9'
        cmp     %$3A,A
        jl      @pn_out
        add     %7,A                    ; 'A'..'F'
pn_out:
        br      @put_ch                 ; put_ch fait le RETS

; ============================================================================
; CLAVIER, AFFICHAGE (repris d'EXLMENU)
; ============================================================================
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
s_title:   .byte   "TEST DES BANQUES EXLCART",0
s_help:    .byte   "ESPACE : CHANGER DE BANQUE",0
s_bank:    .byte   "BANQUE ACTIVE : ",0
s_read:    .byte   "TEXTE LU EN $4000 :",0
s_hex:     .byte   "OCTETS : ",0

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

; ============================================================================
; BANQUE 0 : moitie haute de la ROM, lue a travers la fenetre $4000-$7FFF
; ============================================================================
        .org    BANK_WIN
        .byte   "BONJOUR PLAGE 1",0

        .end
