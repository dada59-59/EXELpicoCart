; WZ_PATCH -- Wizord : manette DB9 d'EXLCART en plus du joystick infrarouge
; Appele 50 fois/s a la place de la routine timer de jeu de Wizord ($740F) :
; lit l'octet manette $3FE0 (0 sur une vraie EPROM), le traduit en code 7041,
; et, s'il a change, le donne au gestionnaire joystick de Wizord ($639D),
; exactement comme le ferait le moniteur pour un code infrarouge.
JOYP    .equ    $3FE0           ; b0 haut b1 bas b2 gauche b3 droite b4 feu
JLAST   .equ    $C7F0           ; dernier code envoye (RAM libre pour Wizord)
        .org    $5000
wz_tick:
        lda     @JOYP
        and     %$1F,A
        mov     A,B
        lda     @wz_tab(B)      ; code 7041 correspondant
        cmpa    @JLAST
        jeq     @wz_done        ; pas de changement : rien a faire
        sta     @JLAST
        mov     A,B             ; B = code
        lda     @$C000          ; gestionnaire de jeu actif ? (BRJOY0 = $639D)
        cmp     %$63,A
        jne     @wz_done
        lda     @$C001
        cmp     %$9D,A
        jne     @wz_done
        mov     B,R3            ; comme le moniteur : code dans VALUE0
        call    @$639D
wz_done:
        br      @$740F          ; la routine timer d'origine
wz_tab:
        .byte   $04, $80, $82, $80, $83, $83, $83, $83
        .byte   $81, $81, $81, $81, $81, $81, $81, $81
        .byte   $20, $20, $20, $20, $20, $20, $20, $20
        .byte   $20, $20, $20, $20, $20, $20, $20, $20
        .end
