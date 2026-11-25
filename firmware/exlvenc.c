/*
 * exlvenc.c -- encodeur video EXLCART (PC) : images 320x240 1 bit -> fichier .exv
 *   gcc -O2 -o exlvenc exlvenc.c
 *   exlvenc [-c] images.bin video.exv [ips_x1000] ["TITRE"]
 * images.bin : les images a la suite, 9600 octets chacune (40 octets par ligne,
 * bit 7 = pixel de gauche, 1 = blanc) ; avec -c (8 couleurs), 28800 octets
 * chacune : pour chaque groupe de 8 pixels, les octets bleu, vert, rouge. Le fichier produit est relu et decode
 * aussitot : le programme verifie que chaque image decodee est identique.
 */
#include <stdio.h>
#include <stdlib.h>
#define EXLV_ENCODER
#include "exlvcodec.h"

int main(int argc, char **argv) {
    uint32_t np = 1;
    if (argc > 1 && !strcmp(argv[1], "-c")) { np = 3; argv++; argc--; }
    if (argc < 3) { fprintf(stderr, "usage : exlvenc [-c] images.bin video.exv [ips_x1000] [titre]\n"); return 2; }
    const size_t FB = (size_t)VFRAME * np;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint32_t n = (uint32_t)(len / (long)FB);
    uint8_t *img = malloc((size_t)n * FB);
    if (fread(img, FB, n, f) != n) { fprintf(stderr, "lecture\n"); return 1; }
    fclose(f);

    static vstate_t s;
    static uint8_t P[3 * VFRAME], C[3 * VFRAME], chg[VFRAME], chp[VFRAME];
    s.P = P; s.C = C; s.chg = chg; s.chp = chp;
    venc_t e; ve_init(&e);
    vs_start(&s, (const uint8_t *)"\0\0\0\0\0", np);    /* modele et images remis a zero */
    for (uint32_t i = 0; i < n; i++) {
        memcpy(s.C, img + (size_t)i * FB, FB);
        if (np == 3) ve_frame3(&e, &s); else ve_frame(&e, &s);
        vs_end(&s);
    }
    ve_flush(&e);

    vhdr_t h; memset(&h, 0, sizeof h);
    h.magic = VMAGIC; h.version = (uint16_t)(np == 3 ? VVERSION_COUL : VVERSION); h.width = VW; h.height = VH;
    h.fps_milli = (uint16_t)(argc > 3 ? atoi(argv[3]) : 29970);
    h.frames = n; h.size = (uint32_t)e.n;
    for (size_t i = 0; i < e.n; i++) h.sum += e.out[i];
    memset(h.title, ' ', sizeof h.title);
    if (argc > 4) memcpy(h.title, argv[4], strlen(argv[4]) < 40 ? strlen(argv[4]) : 40);
    FILE *o = fopen(argv[2], "wb");
    fwrite(&h, sizeof h, 1, o); fwrite(e.out, 1, e.n, o); fclose(o);
    printf("%u images, %zu octets (%.1f par image)\n", n, e.n, (double)e.n / n);

    /* relecture */
    static vstate_t r;
    static uint8_t P2[3 * VFRAME], C2[3 * VFRAME], g2[VFRAME], p2[VFRAME];
    r.P = P2; r.C = C2; r.chg = g2; r.chp = p2;
    vs_start(&r, e.out, np);
    for (uint32_t i = 0; i < n; i++) {
        for (int y = 0; y < (int)VH; y++) vs_decode_line(&r, y);
        if (memcmp(r.C, img + (size_t)i * FB, FB)) { printf("ECHEC image %u\n", i); return 1; }
        vs_end(&r);
    }
    printf("relecture : %u images identiques, %ld octets lus sur %zu\n", n, (long)(r.d.in - e.out), e.n);
    return 0;
}
