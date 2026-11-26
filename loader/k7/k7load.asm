; ============================================================================
; K7LOAD -- lecture des cassettes depuis le Pico (EXLCART), pour EXELBASIC et
; EXEL BASIC PLUS. Remplace le TRAP 14 en LECTURE : le BASIC y appelle
; "dint / trap 14 / eint" (3 octets) pour le bloc RAM puis pour le bloc VRAM
; d'un programme ; le Pico y met "call @k7_load" (3 octets) et sert cette
; routine dans une zone de RECOUVREMENT (K7_ORG, ~120 octets) :
;   - le Pico sert le BASIC d'origine ; quand la console lit le dernier octet
;     d'un de ces CALL, il bascule sur une copie du BASIC ou la zone porte
;     cette routine ; quand elle lit le RETS final, il revient au BASIC ;
;   - rien n'est donc retire du BASIC (la cassette marche toujours en SAVE).
; Le Pico se comporte comme une bande :
;   K7_NEXT  lecture = passer au bloc suivant (la fin du bloc en cours est sautee)
;   K7_STAT  $FF : il cherche ; 0 : bloc pret ; $20 : fin de bande (rembobinee)
;   K7_DATA  apres CHAQUE lecture, l'octet suivant du bloc : nom (4), offset,
;            fin (2), debut (2), donnees, somme
; Memes entrees / sorties que le TRAP 14 (voir la ROM du TMS7020, $FCA1) :
;   R32 (TEMP10) bit 1 : lecture, bit 2 : VRAM, bit 0 : adresses de la bande,
;   bit 3 : premier fichier venu ; R27:R28 debut, R29:R30 fin (TEMP8, TEMP9) ;
;   FILNAM ($C025-$C028) : le nom cherche ;
;   en sortie : R32 bit 7 : autre fichier (son nom en TEPNAM, $C01B), bit 6 :
;   somme fausse, bit 5 : pas de bande ; l'en-tete en $C01F-$C023, la somme
;   lue en $C024 ; B = R32 (EXELBASIC teste B apres le bloc VRAM).
;   L'offset VRAM (TEMP10-1) est toujours $7A dans le BASIC : non gere.
; Interruptions coupees pendant la routine (comme le TRAP 14).
; RETOUR (11/11/2026) : le TMS7000 lit l'octet qui suit une instruction d'un
; octet : pendant le EINT final, il lit deja l'adresse du RETS (K7_EXIT). Le
; Pico ne revient donc au BASIC qu'a l'acces a l'instruction qui suit le CALL,
; apres avoir vu K7_EXIT (exlk7.h) ; avant, il revenait des K7_EXIT, et le RETS
; etait lu dans le BASIC : Err 18 apres LOAD, ou ecran noir.
; ============================================================================
R_A             .equ    0               ; A et B vus comme registres (forme Rn,Rn)
R_B             .equ    1
T_CK            .equ    24              ; somme des donnees
T_PH            .equ    25              ; R25:R26 : pointeur
T_PL            .equ    26
T_DH            .equ    27              ; R27:R28 : debut (TEMP8)
T_DL            .equ    28
T_EH            .equ    29              ; R29:R30 : fin (TEMP9)
T_EL            .equ    30
T_10            .equ    32              ; TEMP10
T_1             .equ    14              ; TEMP1 (TRAP 8)
TEPNAM_M1       .equ    $C01A           ; nom trouve : $C01B-$C01E (adresse - 1)
HDR_M1          .equ    $C01E           ; offset, fin, debut : $C01F-$C023
FILNAM_M1       .equ    $C024           ; nom cherche : $C025-$C028 ; somme lue en $C024
REG_M1          .equ    $001A           ; R27-R31 (adresses de la bande, bit 0)

        .org    K7_ORG
k7_load:
        dint
        lda     @K7_NEXT                ; <- bloc suivant
kl_w:   lda     @K7_STAT
        cmp     %$FF,A
        jeq     @kl_w                   ; le Pico cherche
        tsta
        jeq     @kl_go
        or      R_A,T_10                ; $20 : pas de bande
        jmp     @kl_end
kl_go:  mov     %4,B                    ; le nom (de droite a gauche, comme la ROM)
kl_nm:  lda     @K7_DATA
        sta     @TEPNAM_M1(B)
        cmpa    @FILNAM_M1(B)
        jeq     @kl_n1
        btjo    %$08,T_10,@kl_n1        ; premier fichier venu
        or      %$80,T_10               ; autre fichier
kl_n1:  djnz    B,@kl_nm
        mov     %5,B                    ; offset, fin, debut
kl_hd:  lda     @K7_DATA
        sta     @HDR_M1(B)
        btjz    %$01,T_10,@kl_h1
        sta     @REG_M1(B)              ; adresses de la bande
kl_h1:  djnz    B,@kl_hd
        btjo    %$80,T_10,@kl_end       ; autre fichier : on s'arrete la
        clr     T_CK
        movd    T_DL,T_PL               ; pointeur = debut
        btjz    %$04,T_10,@kl_lp
        movd    T_PL,T_1
        trap    8                       ; pointeur d'ecriture du VDP
kl_lp:  lda     @K7_DATA
        add     R_A,T_CK
        btjz    %$04,T_10,@kl_ram
        movp    A,P46                   ; VRAM
        jmp     @kl_nx
kl_ram: sta     *T_PL                   ; RAM
kl_nx:  cmp     T_EH,T_PH               ; fin atteinte ? (comme la ROM, $FE10)
        jl      @kl_inc
        cmp     T_EL,T_PL
        jl      @kl_inc
        lda     @K7_DATA                ; la somme de la bande
        sta     @FILNAM_M1
        cmp     R_A,T_CK
        jeq     @kl_end
        or      %$40,T_10               ; somme fausse
kl_end: mov     T_10,B
        eint
K7_EXIT:
        rets                            ; <- le Pico revient au BASIC
kl_inc: inc     T_PL
        adc     %0,T_PH
        jmp     @kl_lp
K7_NEXT: .byte  0
K7_STAT: .byte  $FF
K7_DATA: .byte  0
k7_fin:
