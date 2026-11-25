/*
 * exlsave.h -- sauvegarde de l'Exelmemoire dans la flash du Pico (menu v5.1), relue
 * ensuite par USB dans EXLPICO (fichiers .cram et .BKP). Commun a EXLCART et EXLSONDE
 * (inclus par exllib.h).
 *
 * La console ne peut pas ECRIRE dans la cartouche : elle envoie chaque octet v en
 * LISANT l'adresse $6C00+v, et le coeur 1 note l'octet de poids faible de l'adresse
 * (multi_after, exlmulti.h). Le menu, interruptions coupees :
 *   - lit $6BF0 : debut (compteur a zero, $6BF2 = 0) ;
 *   - lit $6C00+v pour chacun des 16 384 octets, de $BFFF a $8000 (l'ordre des
 *     .BKP : TMS7020 sans INCD), puis les deux octets de leur somme (16 bits, poids
 *     fort d'abord) ;
 *   - lit $6BF1 : fin ; puis attend la reponse en $6BF2.
 * Le coeur 0 (save_poll) controle le nombre d'octets et la somme, range l'image dans
 * la flash, la relit, puis ecrit l'emplacement (1-8) en $6BF3 et la reponse en $6BF2 :
 * SAVE_OK ($A5), SAVE_BAD ($E1 : transmission fausse), SAVE_FLASH ($E2 : flash).
 *
 * Flash : SAVE_SLOTS emplacements de 20 Ko a partir de 0x190000 (apres le releve de
 * la sonde, 0x180000-0x18FFFF) : +0 un en-tete de 256 octets, +0x1000 l'image dans
 * l'ordre des .cram ($8000 d'abord). Un emplacement libre d'abord, sinon le plus
 * ancien ; chaque sauvegarde porte un numero (le plus grand + 1).
 *
 * Lecture par USB (save_dump) : "EXLMEM 1 <emplacements>" puis, pour chaque
 * sauvegarde, "SLOT <k> <numero> <somme 32 bits>" et 16 384 octets en hexadecimal
 * (32 par ligne), enfin "FIN". save_erase efface tous les emplacements.
 */
#ifndef EXLSAVE_H
#define EXLSAVE_H
#include <stdio.h>
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

#define SAVE_FLASH_OFFSET 0x00190000u
#define SAVE_SLOT_SIZE    0x5000u                 /* en-tete (4 Ko) + image (16 Ko) */
#define SAVE_SLOTS        8u
#define SAVE_DATA_OFF     0x1000u
#define SAVE_MAGIC        0x4D4C5845u             /* "EXLM" */
#define SAVE_OK           0xA5u
#define SAVE_BAD          0xE1u
#define SAVE_FLASH        0xE2u
typedef struct { uint32_t magic, version, seq, size, sum, nsum; } save_hdr_t;

static inline const save_hdr_t *save_hdr(uint32_t k) {
    return (const save_hdr_t *)(XIP_BASE + SAVE_FLASH_OFFSET + k * SAVE_SLOT_SIZE);
}
static inline int save_valid(const save_hdr_t *h) {
    return h->magic == SAVE_MAGIC && h->version == 1u && h->size == CRAM_SIZE;
}
/* range l'image (ordre .cram) ; renvoie l'emplacement (0-7), ou -1 si la relecture est fausse */
static int save_write(const uint8_t *img, uint32_t sum, uint32_t nsum) {
    uint32_t k = SAVE_SLOTS, seq = 0, old = 0, oldseq = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < SAVE_SLOTS; i++) {
        const save_hdr_t *h = save_hdr(i);
        if (!save_valid(h)) { if (k == SAVE_SLOTS) k = i; continue; }
        if (h->seq > seq) seq = h->seq;
        if (h->seq < oldseq) { oldseq = h->seq; old = i; }
    }
    if (k == SAVE_SLOTS) k = old;                         /* tout est pris : le plus ancien */
    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page, 0xFF, sizeof page);
    const save_hdr_t h = { SAVE_MAGIC, 1u, seq + 1u, CRAM_SIZE, sum, nsum };
    memcpy(page, &h, sizeof h);
    const uint32_t off = SAVE_FLASH_OFFSET + k * SAVE_SLOT_SIZE;
    uint32_t irq = save_and_disable_interrupts();         /* le coeur 1 sert depuis la RAM */
    flash_range_erase(off, SAVE_SLOT_SIZE);
    flash_range_program(off + SAVE_DATA_OFF, img, CRAM_SIZE);
    flash_range_program(off, page, FLASH_PAGE_SIZE);      /* l'en-tete en dernier */
    restore_interrupts(irq);
    const uint8_t *f = (const uint8_t *)(XIP_BASE + off);
    if (memcmp(f + SAVE_DATA_OFF, img, CRAM_SIZE) || memcmp(f, page, sizeof h)) return -1;
    return (int)k;
}
/* coeur 0 : une sauvegarde est arrivee ? (menu = l'image servie, pour la reponse) */
static void save_poll(cell_t *menu, uint8_t *buf) {
    const uint32_t d = g_sv_done;
    if (!d) return;
    g_sv_done = 0;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < CRAM_SIZE; i++) sum += buf[i];
    uint8_t st = SAVE_BAD, slot = 0;
    const uint32_t nsum = (uint32_t)buf[CRAM_SIZE] << 8 | buf[CRAM_SIZE + 1u];
    if (d - 1u == SAVE_LEN && (sum & 0xFFFFu) == nsum) {
        for (uint32_t i = 0; i < CRAM_SIZE / 2u; i++) {   /* $BFFF..$8000 -> ordre .cram */
            const uint8_t t = buf[i]; buf[i] = buf[CRAM_SIZE - 1u - i]; buf[CRAM_SIZE - 1u - i] = t;
        }
        const int k = save_write(buf, sum, nsum);
        if (k < 0) st = SAVE_FLASH;
        else { st = SAVE_OK; slot = (uint8_t)(k + 1); }
    }
    menu[SAVE_SLOT_IDX] = ENC(slot);
    __dmb();                                              /* l'emplacement d'abord */
    menu[SAVE_STAT_IDX] = ENC(st);
}
/* lecture par USB : out(texte, longueur) */
static void save_dump(void (*out)(const char *s, uint32_t n)) {
    char line[80];
    uint32_t n = 0;
    for (uint32_t k = 0; k < SAVE_SLOTS; k++) n += (uint32_t)save_valid(save_hdr(k));
    out(line, (uint32_t)snprintf(line, sizeof line, "EXLMEM 1 %lu %lu\n", (unsigned long)n, (unsigned long)SAVE_SLOTS));
    static const char hx[] = "0123456789abcdef";
    for (uint32_t k = 0; k < SAVE_SLOTS; k++) {
        const save_hdr_t *h = save_hdr(k);
        if (!save_valid(h)) continue;
        out(line, (uint32_t)snprintf(line, sizeof line, "SLOT %lu %lu %08lx\n", (unsigned long)(k + 1u),
                                     (unsigned long)h->seq, (unsigned long)h->sum));
        const uint8_t *d = (const uint8_t *)h + SAVE_DATA_OFF;
        for (uint32_t i = 0; i < CRAM_SIZE; i += 32u) {
            for (uint32_t j = 0; j < 32u; j++) { line[2 * j] = hx[d[i + j] >> 4]; line[2 * j + 1] = hx[d[i + j] & 15u]; }
            line[64] = '\n';
            out(line, 65u);
        }
    }
    out("FIN\n", 4u);
}
/* efface toutes les sauvegardes (commande 'x' par USB) */
static void save_erase(void) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SAVE_FLASH_OFFSET, SAVE_SLOTS * SAVE_SLOT_SIZE);
    restore_interrupts(irq);
}
#endif
