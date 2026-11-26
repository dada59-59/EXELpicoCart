; ============================================================================
; BANK1 -- banque 1 du test BANKTEST (16 Ko, vue en $4000-$7FFF)
;
; Une banque ne contient que des DONNEES (ou du code) pour la fenetre : pas de
; point d'entree, et ses 4 derniers octets ($7FFC-$7FFF) sont remplaces par le
; pied de la ROM principale dans EXLPICO.
;
; Assembler, avec EXACTEMENT les memes commandes que la ROM principale :
;     tasm -tEXL -a -b bank1.asm
;     obj2exl.exe bank1.obj bank1.rom -t:ROM -r:0x1000 -p
; puis ajouter bank1.rom dans EXLPICO, carte "Banques" : EXLPICO en prend la
; moitie haute ($4000-$7FFF).
;
; POURQUOI LE .org $1000 CI-DESSOUS : le binaire de TASM (-b) commence a la
; plus petite adresse assemblee, et obj2exl le pose a l'adresse donnee par -r.
; Sans ce repere, le binaire commencerait en $4000, obj2exl le poserait en
; $1000, et la fenetre $4000-$7FFF de la banque serait vide ($FF).
; ============================================================================

        .org    $1000
        .byte   $FF                     ; repere : le binaire commence en $1000

        .org    $4000
        .byte   "AU REVOIR PLAGE 2",0

        .end
