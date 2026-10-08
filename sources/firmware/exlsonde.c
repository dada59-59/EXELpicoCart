/*
 * EXLSONDE v4 "oscilloscope" -- diagnostic de la cartouche emulee EXL100/EXELTEL.
 *
 * A chaque front descendant de /ROM_CS, un automate PIO enregistre 64 instantanes de
 * toutes les broches (A0-A14, D0-D7, /ROM_CS), un toutes les 20 ns : on voit quand
 * l'octet apparait sur le bus, s'il est perturbe, et ce que devient le bus apres la
 * remontee de /ROM_CS. Un canal DMA range les instantanes en RAM.
 *   - le coeur 1 sert la ROM (exlserve.h, avec les reglages lus dans la flash) ;
 *   - le coeur 0 surveille, sauvegarde le releve dans la flash (1,5 Mo) apres 1 s de
 *     calme ou tampon plein (255 acces), puis l'envoie par USB sur un PC ('d').
 * Format : "EXLPINS lineaire|eprom" (le brochage, voir exlpins.h), puis
 * "EXLSONDE 4 n horloge somme delai maintien", puis n mots (64 par acces) : l'etat
 * des broches GP0-GP31, 1 mot toutes les 20 ns ; adresse, donnee et /ROM_CS (GP23)
 * se lisent selon le brochage annonce (lineaire : A0-A14 = bits 0-14, D0-D7 = bits
 * 15-22). La somme est celle des octets servis, comme celle de la ROM en flash.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "exloscillo.pio.h"
#include "exlserve.h"
#include "exllib.h"


#define LOG_FLASH_OFFSET 0x00180000u            /* releve : 1,5 Mo (64 Ko)     */
#define LOG_TOTAL_WORDS  16384u                 /* 64 Ko                       */
#define LOG_HEAD_WORDS   64u                    /* en-tete (256 octets)        */
#define LOG_CAP          (LOG_TOTAL_WORDS - LOG_HEAD_WORDS)
#define LOG_MAGIC        0x534C5845u            /* "EXLS"                      */
#define PC_IDLE_LOOPS    (75u * 1000u * 1000u)   /* ~3 s sans rien : sur un PC     */
#define QUIET_LOOPS      (25u * 1000u * 1000u)   /* ~1 s de calme : sauvegarder    */
#define SYS_CLOCK_KHZ    200000u
#define NO_ACCESS        0xFFFFFFFFu

cell_t   rom[ROM_SIZE] __attribute__((aligned(4)));
static exl_cfg_t cfg;               /* reglages du service (bloc EXLC) */
static uint32_t logbuf[LOG_TOTAL_WORDS];

/* mode lecture (sur un PC) : envoie le releve de la flash a chaque 'd' recu */
static void dump_mode(void) {
    stdio_usb_init();
    const uint32_t *L = (const uint32_t *)(XIP_BASE + LOG_FLASH_OFFSET);
    for (;;) {
        int c = getchar_timeout_us(1000000);
        if (c != 'd' && c != 'D') continue;
        /* la ROM ACTUELLEMENT dans la flash : somme, pied, octets au point d'entree */
#ifdef EXL_PINOUT_EPROM
        printf("EXLPINS eprom\n");
#else
        printf("EXLPINS lineaire\n");
#endif
        {
            const uint8_t *R = (const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET);
            uint32_t s = 0;
            for (uint32_t i = 0; i < ROM_SIZE; i++) s += R[i];
            uint32_t e = ((uint32_t)R[0x7FFE] << 8 | R[0x7FFF]) & 0x7FFFu;
            printf("EXLROM %08lx %02x %02x %02x %02x", (unsigned long)s, R[0x7FFC], R[0x7FFD], R[0x7FFE], R[0x7FFF]);
            for (uint32_t k = 0; k < 8; k++) printf(" %02x", R[(e + k) & 0x7FFFu]);
            printf("\n");
        }
        /* la bibliotheque trouvee dans la flash, et la taille reelle de la puce */
        {
            const uint32_t *B = (const uint32_t *)(XIP_BASE + LIB_FLASH_OFFSET);
            uint8_t tx[4] = {0x9F, 0, 0, 0}, rx[4] = {0, 0, 0, 0};
            uint32_t irq = save_and_disable_interrupts();
            flash_do_cmd(tx, rx, 4);                       /* identification JEDEC */
            restore_interrupts(irq);
            printf("EXLLIB %08lx %08lx %08lx %08lx %08lx %02x %02x %02x\n",
                   (unsigned long)B[0], (unsigned long)B[1], (unsigned long)B[2],
                   (unsigned long)B[3], (unsigned long)B[4], rx[1], rx[2], rx[3]);
        }
        /* registres d'etat de la puce flash (bits de protection BP/TB/SEC/CMP)
         * et carte des reperes du fichier de test (EXLPICO, "UF2 test flash") */
        {
            uint8_t sr[3];
            const uint8_t cmds[3] = {0x05, 0x35, 0x15};
            for (int k = 0; k < 3; k++) {
                uint8_t tx[2] = {cmds[k], 0}, rx[2] = {0, 0};
                uint32_t irq = save_and_disable_interrupts();
                flash_do_cmd(tx, rx, 2);
                restore_interrupts(irq);
                sr[k] = rx[1];
            }
            printf("EXLSR %02x %02x %02x\n", sr[0], sr[1], sr[2]);
            static const uint32_t probe_at[6] = {0x100000, 0x1F0000, 0x200000, 0x400000, 0x800000, 0xF00000};
            printf("EXLMAP");
            for (int k = 0; k < 6; k++)
                printf(" %06lx:%08lx", (unsigned long)probe_at[k],
                       (unsigned long)*(const uint32_t *)(XIP_BASE + probe_at[k]));
            printf("\n");
        }
        /* le menu tel qu'il est dans la flash (0x201000), si une bibliotheque existe */
        {
            const uint32_t *B = (const uint32_t *)(XIP_BASE + LIB_FLASH_OFFSET);
            if (B[0] == LIB_MAGIC) {
                const uint8_t *R = (const uint8_t *)(XIP_BASE + B[2]);
                uint32_t s = 0;
                for (uint32_t i = 0; i < ROM_SIZE; i++) s += R[i];
                uint32_t e = ((uint32_t)R[0x7FFE] << 8 | R[0x7FFF]) & 0x7FFFu;
                printf("EXLMENU %08lx %02x %02x %02x %02x", (unsigned long)s, R[0x7FFC], R[0x7FFD], R[0x7FFE], R[0x7FFF]);
                for (uint32_t k = 0; k < 8; k++) printf(" %02x", R[(e + k) & 0x7FFFu]);
                printf("\n");
            }
        }
        if (L[0] != LOG_MAGIC) { printf("EXLSONDE VIDE\n"); continue; }
        uint32_t n = L[1] > LOG_CAP ? LOG_CAP : L[1];
        printf("EXLSONDE 4 %lu %lu %08lx %u %u\n", (unsigned long)n, (unsigned long)L[2], (unsigned long)L[3],
               (unsigned)cfg.delai, (unsigned)cfg.maintien);
        for (uint32_t i = 0; i < n; i++)
            printf("%08lx%c", (unsigned long)L[LOG_HEAD_WORDS + i], (i % 8 == 7) ? '\n' : ' ');
        printf("\nFIN\n");
        stdio_flush();
    }
}

static void core1_entry(void) {
    save_and_disable_interrupts();
    pins_interp_init();             /* interpolateur du coeur 1 (support EPROM) */
    exl_serve(cfg);                 /* service du bus, memes reglages qu'EXLCART */
}

static void save_log(uint32_t n, uint32_t sum) {
    logbuf[0] = LOG_MAGIC; logbuf[1] = n; logbuf[2] = SYS_CLOCK_KHZ; logbuf[3] = sum;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(LOG_FLASH_OFFSET, sizeof logbuf);      /* coeur 1 tourne en RAM */
    flash_range_program(LOG_FLASH_OFFSET, (const uint8_t *)logbuf, sizeof logbuf);
    restore_interrupts(irq);
}

int main(void) {
    pins_init();
    img_load(rom, (const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET), 0, ROM_SIZE);

    pins_gpio_init();
    gpio_pull_up(CS_PIN);              /* sur un PC, /ROM_CS reste a 1 */

    cfg = exl_read_cfg();
    const lib_hdr_t *LIB = lib_find();     /* bibliotheque : servie comme EXLCART */
    if (LIB) lib_prepare(LIB);
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    multicore_launch_core1(LIB ? core1_multi : core1_entry);   /* servir au plus tot */

    uint32_t sum = img_sum(rom);       /* somme des octets servis (decodes) */

    /* automate PIO "oscilloscope" : lit les broches sans en prendre le controle */
    PIO pio = pio0; uint sm = 0;
    uint off = pio_add_program(pio, &oscillo_program);
    pio_sm_config c = oscillo_program_get_default_config(off);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, false, true, 32);     /* autopush a 32 bits */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(pio, sm, off, &c);

    int ch = dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_dreq(&d, pio_get_dreq(pio, sm, false));
    const uint32_t cap = (LOG_CAP / 64u) * 64u;      /* acces entiers */
    dma_channel_configure(ch, &d, &logbuf[LOG_HEAD_WORDS], &pio->rxf[sm], cap, true);
    pio_sm_set_enabled(pio, sm, true);

    /* surveillance : sauvegarde apres 1 s de calme ou tampon plein */
    uint64_t t_boot = time_us_64(), t_change = t_boot;
    uint32_t prev = 0, saved = 0;
    for (;;) {
        uint32_t n = ((cap - dma_hw->ch[ch].transfer_count) / 64u) * 64u;   /* acces complets */
        uint64_t now = time_us_64();
        if (LIB) lib_poll(LIB);                                         /* jeu demande */
        if (n != prev) { prev = n; t_change = now; }
        if (n == 0 && now - t_boot > 3000000u) break;                     /* sur un PC */
        bool full = !dma_channel_is_busy(ch);
        if (n != saved && (full || now - t_change > 1000000u)) { save_log(n, sum); saved = n; }
        if (full && n == saved) { for (;;) __wfi(); }
    }
    pio_sm_set_enabled(pio, sm, false);
    dump_mode();                                   /* sur un PC : lecture du releve */
}
