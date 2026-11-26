/*
 * test_video.c -- essai sur PC du mode video (exlvideo.h + exlvcodec.h, carte lineaire) :
 *   gcc -O2 -o test_video test_video.c
 *   ./test_video lecteur.rom video.exv images.bin listes.bin [pas] [nb]
 * Decode la video avec le decodeur du firmware (vs_line), verifie chaque image
 * contre images.bin, et ecrit dans listes.bin les listes que le firmware enverrait
 * a l'EXL100 pour les images 0, pas, 2*pas... (nb listes) : pour chacune,
 * le numero d'image (4 octets), la longueur (4 octets), puis la liste.
 * test_badapple.py --listes les fait executer par le vrai lecteur.
 */
#include <stdio.h>
#include <stdlib.h>
#include "exlvideo.h"

static uint8_t *load(const char *f, long *len) {
    FILE *h = fopen(f, "rb");
    if (!h) { perror(f); exit(1); }
    fseek(h, 0, SEEK_END); *len = ftell(h); fseek(h, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)*len);
    if (fread(d, 1, (size_t)*len, h) != (size_t)*len) exit(1);
    fclose(h);
    return d;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage : test_video lecteur.rom video.exv images.bin listes.bin [pas] [nb]\n"); return 2; }
    long lr, lv, li;
    uint8_t *rom8 = load(argv[1], &lr), *exv = load(argv[2], &lv), *img = load(argv[3], &li);
    int pas = argc > 5 ? atoi(argv[5]) : 1, nb = argc > 6 ? atoi(argv[6]) : 50;
    int fails = 0;
#define OK(c, ...) do { int _c = (c); printf(_c ? "OK    " : "ECHEC "); printf(__VA_ARGS__); printf("\n"); fails += !_c; } while (0)

    static vzone_t z;
    OK(vz_find(rom8, (uint32_t)lr, &z) && z.fifo == 0x7F00, "zone EXLV trouvee en $%04X", (unsigned)z.fifo);
    const vhdr_t *h = (const vhdr_t *)exv;
    OK(vh_ok(h) && vh_sum_ok(h), "en-tete et somme de la video : %u images, %u octets", (unsigned)h->frames, (unsigned)h->size);

    const uint32_t np = vh_planes(h);
    const size_t FB = (size_t)VFRAME * np;
    static vstate_t s;
    static uint8_t P[3 * VFRAME], C[3 * VFRAME], g1[VFRAME], g2[VFRAME], sent[3 * VFRAME];
    s.P = P; s.C = C; s.chg = g1; s.chp = g2;
    vs_start(&s, exv + sizeof *h, np);

    /* place de la liste : celle du firmware sur la carte support EPROM (la plus petite) */
    static vbuild_t b;
    static cell_t buf[32768];
    b.z = &z; b.buf = buf; b.np = np; b.sent = sent;
    b.cap = np == 3 ? (131072u - 3u * 3u * VFRAME - 2u * VFRAME) / 2u : VID_STREAM_CAP;
    vb_reset(&b);
    printf("       %s, liste de %u octets au plus\n", np == 3 ? "8 couleurs" : "noir et blanc", (unsigned)b.cap);

    FILE *o = fopen(argv[4], "wb");
    uint32_t nf = h->frames < (uint32_t)(li / (long)FB) ? h->frames : (uint32_t)(li / (long)FB);
    int bad = 0, nsent = 0, parts = 0;
    uint32_t maxlen = 0;
    uint8_t text[80];
    for (uint32_t k = 0; k < nf; k++) {
        for (int y = 0; y < (int)VH; y++) vs_decode_line(&s, y);
        vs_end(&s);                                       /* P = image k */
        if (memcmp(s.P, img + (size_t)k * FB, FB)) { if (bad++ < 3) printf("   image %u decodee differente\n", (unsigned)k); }
        if (nsent < nb && k % (uint32_t)pas == 0) {
            vt_make(&z, text, h->title, k * 1000u / h->fps_milli, (uint32_t)(30 / pas));
            const uint8_t *t = text;
            do {                                          /* liste pleine : on continue, comme le firmware */
                uint32_t n = vb_build(&b, s.P, t);
                t = 0;
                if (n > maxlen) maxlen = n;
                uint32_t hd[2] = { k | (b.partial ? 0x80000000u : 0u), n };
                fwrite(hd, 4, 2, o); fwrite(buf, 1, n, o);
                parts += b.partial;
            } while (b.partial);
            if (memcmp(b.sent, s.P, FB)) { printf("   etat 'affiche' faux apres l'image %u\n", (unsigned)k); bad++; }
            nsent++;
        }
    }
    fclose(o);
    OK(bad == 0, "%u images decodees par le decodeur du firmware : toutes identiques a la video d'origine", (unsigned)nf);
    OK(maxlen <= b.cap, "%d images envoyees (pas de %d), la plus longue liste : %u octets (place : %u) ; %d listes completees par une suivante",
       nsent, pas, (unsigned)maxlen, (unsigned)b.cap, parts);

    /* pires cas : groupes isoles (1 sur 4 change), puis tout l'ecran ; en couleur, autant de
     * listes qu'il faut, et l'etat "affiche" doit finir egal a l'image */
    static uint8_t A[3 * VFRAME];
    uint32_t n1 = 0, n2 = 0, k1 = 0, k2 = 0;
    vb_reset(&b);
    for (uint32_t i = 0; i < FB; i++) A[i] = ((i / np) % 4 == 0) ? (uint8_t)(0x0F << (i % np)) : 0;
    do { uint32_t n = vb_build(&b, A, text); if (n > n1) n1 = n; k1++; } while (b.partial && k1 < 20);
    int ok1 = !memcmp(b.sent, A, FB);
    for (uint32_t i = 0; i < FB; i++) A[i] = (uint8_t)(0x55 + i % np);
    do { uint32_t n = vb_build(&b, A, 0); if (n > n2) n2 = n; k2++; } while (b.partial && k2 < 20);
    OK(ok1 && !memcmp(b.sent, A, FB) && n1 <= b.cap && n2 <= b.cap,
       "pires cas : groupes isoles %u listes (%u octets au plus), ecran entier %u listes (%u octets)",
       (unsigned)k1, (unsigned)n1, (unsigned)k2, (unsigned)n2);
    /* musique 1 bit : la console simulee a la microseconde (minuterie avec recharge au
     * debordement, interruption servie 25 a 70 us plus tard, parfois masquee 0,5 ms),
     * le coeur 1 (heure de S_MASK, entree suivante apres S_REL) et sb_read / sb_fill
     * appeles toutes les 150 us, avec des pauses de 3 ms (une liste a preparer). */
    const bhdr_t *bh = sb_find(h);
    if (bh) {
        OK(sb_sum_ok(bh), "musique 1 bit : %u evenements, %u ms, somme juste", (unsigned)bh->nev, (unsigned)bh->ms);
        static sbit_t S;
        static double want[1 << 22];
        uint32_t nw = 0; { uint32_t t = 0; const uint16_t *e = sb_ev(bh);
            for (uint32_t i = 0; i < bh->nev && nw < (1u << 22); i++) { t += e[i] & 0x7FFFu; if (e[i] & 0x8000u) want[nw++] = t; } }
        const struct { double unit; int delta; int masks; } cases[] = { { 13.0208, 0, 0 }, { 6.5104, 0, 0 }, { 13.0208, -1, 0 }, { 13.0208, 0, 1 } };
        for (int c = 0; c < 4; c++) {
            memset(&S, 0, sizeof S);
            S.h = bh; S.ev = sb_ev(bh);
            uint32_t sk = 1000;                           /* numero de l'interruption suivante */
            volatile uint32_t st[SB_Q];
            sb_reset(&S, sk);
            cell_t cm = S.qm[sk & (SB_Q - 1u)], cl = S.qr[sk & (SB_Q - 1u)];
            uint32_t sqn = S.qn;
            const double t0 = 5e5;                        /* origine du film : 0,5 s */
            double tu = t0 + 1000.0, now = 0;             /* premier debordement */
            uint32_t p2 = SB_CAL_REL;                     /* le lecteur : recharge 255 */
            double dint_until = 0, next_core0 = 0;
            uint32_t rnd = 12345, ntog = 0, wi = 0, bad = 0, lost = 0;
            double maxerr = 0, sumerr = 0; uint32_t nerr = 0;
            const double end = t0 + SB_DELAY_US + (double)bh->ms * 1000.0;
#define RND() (rnd = rnd * 1103515245u + 12345u, (rnd >> 8) & 0xFFFF)
            while (tu < end) {
                /* debordement a tu ; recharge = P2 actuel */
                const double next_u = tu + (p2 + 1 + cases[c].delta) * cases[c].unit;
                double ti = tu + 25.0 + 45.0 * RND() / 65536.0;          /* l'interruption */
                if (cases[c].masks && RND() % 400 == 0) dint_until = tu + 500.0;  /* parfois masquee */
                if (ti < dint_until) ti = dint_until + 5.0;
                if (ti >= next_u) { lost++; tu = next_u; continue; }      /* deux debordements : un seul appel */
                /* le coeur 0 tourne jusqu'a ti */
                while (next_core0 < ti) {
                    now = next_core0;
                    const uint32_t k = sk;
                    if ((int32_t)(k - S.seen) > (int32_t)(SB_Q - 8u)) S.seen = k;
                    while (S.seen != k) { sb_read(&S, S.seen, (int64_t)st[S.seen & (SB_Q - 1u)]); S.seen++; }
                    sb_fill(&S, k, (int64_t)now, (int64_t)t0);
                    sqn = S.qn;
                    next_core0 += (RND() % 64 == 0) ? 3000.0 : 150.0;
                }
                /* l'interruption : S_MASK puis S_REL (coeur 1) */
                st[sk & (SB_Q - 1u)] = (uint32_t)ti;
                if (cm & SB_TOGGLE) {
                    ntog++;
                    const double w = t0 + SB_DELAY_US;
                    while (wi < nw && w + want[wi] < ti - 2000.0) wi++;
                    if (wi < nw) {
                        double e = ti - (w + want[wi]); if (e < 0) e = -e;
                        if (e > 2000.0) bad++;
                        else { if (ti > t0 + 300000.0 && e > maxerr) maxerr = e; sumerr += e; nerr++; wi++; }
                    }
                }
                p2 = cl;
                sk++;
                if ((int32_t)(sqn - sk) > 0) { cm = S.qm[sk & (SB_Q - 1u)]; cl = S.qr[sk & (SB_Q - 1u)]; }
                else { cm = 0; cl = SB_CAL_REL; }
                tu = next_u;
            }
            OK(nerr + S.late + 2 >= nw && bad == 0 && (cases[c].masks || (S.resyncs == 0 && maxerr < 80.0)),
               "musique 1 bit, unite %.2f us, ecart = recharge + 1 %+d%s : %u inversions sur %u voulues (%u sautees au debut), "
               "ecart moyen %.1f us, maxi %.1f us ; %u interruptions perdues, %u recalages",
               cases[c].unit, S.delta, cases[c].masks ? ", interruptions parfois masquees" : "", (unsigned)nerr, (unsigned)nw,
               (unsigned)S.late, nerr ? sumerr / nerr : 0.0, maxerr, (unsigned)lost, (unsigned)S.resyncs);
        }
    }
    printf(fails ? "\n%d ECHEC(S)\n" : "\ntout est bon\n", fails);
    return fails != 0;
}
