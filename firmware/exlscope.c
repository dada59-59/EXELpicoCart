/*
 * EXLSCOPE -- le Pico en petit oscilloscope, pour le crayon optique : la ROM est
 * servie comme d'habitude (coeur 1, exlserve.h) et le coeur 0 enregistre la tension
 * du fil signal du crayon avec le convertisseur analogique du Pico.
 *
 * Branchement : pont de resistances du fil signal du crayon vers GP28 (ADC2) :
 *     signal crayon -- 10 kohms -- GP28 -- 20 kohms (ou 22 k) -- masse
 *
 * Version 3 : enregistrements LONGS (1 s = 50 images) pour faire ressortir, en les
 * superposant image par image apres coup, une petite impulsion qui revient a chaque
 * passage du spot (20 ms) et qui se perd dans le bruit sur une seule image. Le
 * convertisseur mesure toutes les 2 us (500 000 mesures/s, 12 bits) ; le coeur 0
 * fait la moyenne de 12 mesures (24 us : une periode du parasite de 43,5 kHz vu
 * sur le releve precedent) et garde ces moyennes. Trois enregistrements, a 15 s,
 * 35 s et 55 s apres la mise sous tension du Pico, ranges dans la flash (1,5 Mo).
 * Branche sur un PC (USB reconnu) : envoi a chaque 'd' recu, au format de la sonde
 * (lu et enregistre par EXLPICO, "Lire la sonde") :
 *   EXLSCOPE 3 ncap nval groupe periode_ns t1_ms t2_ms t3_ms facteur_pont_x1000 bits
 *   EXLSONDE 4 nmots ... puis les moyennes (x16, sur 16 bits, poids faible d'abord),
 *   2 par mot. (Versions 1 et 2 : 60 / 40 ms de mesures brutes.)
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/flash.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "pico/multicore.h"
#include "tusb.h"
#include "exlserve.h"

#define SYS_CLOCK_KHZ    200000u
#define ADC_PIN          28
#define ADC_INPUT        2
#define NCAP             3u
#define GROUP            12u                    /* mesures par moyenne (24 us) */
#define NVAL             41664u                 /* moyennes par enregistrement : ~1 s */
#define NRAW             (NVAL * GROUP + 128u)  /* mesures (les 128 premieres jetees) */
#define LOG_FLASH_OFFSET 0x00180000u            /* en-tete (4 Ko), puis les donnees */
#define CAP_BYTES        ((NVAL * 2u + 4095u) & ~4095u)
#define LOG_MAGIC        0x50435845u            /* "EXCP" */
#define RING_BITS        13u                    /* anneau DMA : 8 Ko = 4096 mesures */
static const uint32_t T_CAP_MS[NCAP] = { 15000u, 35000u, 55000u };

cell_t rom[ROM_SIZE] __attribute__((aligned(4)));
static uint16_t ring[1u << (RING_BITS - 1)] __attribute__((aligned(1u << RING_BITS)));
static uint16_t val[CAP_BYTES / 2] __attribute__((aligned(4)));
static uint32_t head[1024] __attribute__((aligned(4)));       /* 4 Ko */

static void core1_entry(void) {
    save_and_disable_interrupts();
    pins_interp_init();
    exl_serve(exl_read_cfg());
}

static void dump_mode(void) {
    const uint32_t *H = (const uint32_t *)(XIP_BASE + LOG_FLASH_OFFSET);
    for (;;) {
        int c = getchar_timeout_us(1000000);
        if (c != 'd' && c != 'D') continue;
#ifdef EXL_PINOUT_EPROM
        printf("EXLPINS eprom\n");
#else
        printf("EXLPINS lineaire\n");
#endif
        if (H[0] != LOG_MAGIC || H[1] == 0 || H[1] > NCAP) { printf("EXLSONDE VIDE\n"); continue; }
        const uint32_t nc = H[1];
        printf("EXLSCOPE 3 %lu %lu %lu %lu %lu %lu %lu %u 12\n", (unsigned long)nc, (unsigned long)H[2],
               (unsigned long)GROUP, (unsigned long)H[3], (unsigned long)H[4], (unsigned long)H[5],
               (unsigned long)H[6], 1500u);
        const uint32_t nw = nc * NVAL / 2u;
        printf("EXLSONDE 4 %lu 200000 00000000 0 0\n", (unsigned long)nw);
        uint32_t i = 0;
        for (uint32_t k = 0; k < nc; k++) {
            const uint8_t *D = (const uint8_t *)(XIP_BASE + LOG_FLASH_OFFSET + 4096u + k * CAP_BYTES);
            for (uint32_t j = 0; j < NVAL / 2u; j++, i++)
                printf("%02x%02x%02x%02x%c", D[4 * j], D[4 * j + 1], D[4 * j + 2], D[4 * j + 3], (i % 8 == 7) ? '\n' : ' ');
        }
        printf("\nFIN\n");
        stdio_flush();
    }
}

static void flash_save(uint32_t off, const void *src, uint32_t len) {
    uint32_t irq = save_and_disable_interrupts();       /* coeur 1 sert depuis la RAM */
    flash_range_erase(off, len);
    flash_range_program(off, (const uint8_t *)src, len);
    restore_interrupts(irq);
}

int main(void) {
    pins_init();
    img_load(rom, (const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET), 0, ROM_SIZE);
    pins_gpio_init();
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    multicore_launch_core1(core1_entry);
    /* USB toujours en marche : branche sur un PC, le Pico envoie le dernier
     * enregistrement ; dans l'EXL100, personne ne repond sur l'USB */
    stdio_usb_init();
    const uint64_t t0 = time_us_64();

    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(ADC_INPUT);
    adc_fifo_setup(true, true, 1, false, false);         /* FIFO, DMA, 12 bits */
    adc_set_clkdiv(0);                                   /* 500 000 mesures/s */
    const int ch = dma_claim_unused_channel(true);
    memset(head, 0, sizeof head);
    head[0] = LOG_MAGIC; head[2] = NVAL;
    for (uint32_t k = 0; k < NCAP; k++) {
        while (time_us_64() - t0 < (uint64_t)T_CAP_MS[k] * 1000u)
            if (tud_mounted()) dump_mode();
        dma_channel_config d = dma_channel_get_default_config(ch);
        channel_config_set_transfer_data_size(&d, DMA_SIZE_16);
        channel_config_set_read_increment(&d, false);
        channel_config_set_write_increment(&d, true);
        channel_config_set_ring(&d, true, RING_BITS);  /* anneau sur l'ecriture */
        channel_config_set_dreq(&d, DREQ_ADC);
        adc_fifo_drain();
        dma_channel_configure(ch, &d, ring, &adc_hw->fifo, NRAW, false);
        const uint64_t a = time_us_64();
        dma_channel_start(ch);
        adc_run(true);
        uint32_t rd = 0, n = 0, acc = 0, g = 0;          /* mesures lues, moyennes, somme */
        while (n < NVAL) {
            const uint32_t wr = NRAW - dma_hw->ch[ch].transfer_count;   /* mesures ecrites */
            if (wr - rd > (1u << (RING_BITS - 1)) - 64u) break;          /* retard : arret */
            while (rd < wr && n < NVAL) {
                const uint32_t s = ring[rd & ((1u << (RING_BITS - 1)) - 1u)] & 0x0FFFu;
                if (rd++ < 128u) continue;
                acc += s;
                if (++g == GROUP) { val[n++] = (uint16_t)((acc * 16u + GROUP / 2u) / GROUP); acc = 0; g = 0; }
            }
        }
        adc_run(false);
        dma_channel_abort(ch);
        const uint64_t b = time_us_64();
        adc_fifo_drain();
        if (k == 0) head[3] = (uint32_t)((b - a) * 1000u / (rd ? rd : 1u));   /* periode d'une mesure, ns */
        head[4 + k] = (uint32_t)(a / 1000u);
        head[1] = k + 1u;
        head[8 + k] = n;                                 /* moyennes obtenues (NVAL si tout va bien) */
        flash_save(LOG_FLASH_OFFSET + 4096u + k * CAP_BYTES, val, CAP_BYTES);
        flash_save(LOG_FLASH_OFFSET, head, 4096u);
    }
    for (;;) if (tud_mounted()) dump_mode();
}
