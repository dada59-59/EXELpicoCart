/*
 * EXLBROCHES -- test des lignes de DONNEES de la carte, sans l'EXL100.
 * Le Pico, branche sur l'USB d'un PC (cartouche HORS de l'EXL100), pilote D0-D7
 * (selon le brochage de la carte, exlpins.h) avec un motif fixe qui change toutes
 * les 5 s ; la LED du Pico (GP25) est allumee quand les donnees sont a 1 :
 *    LED eteinte : D0-D7 a 0  (on doit lire ~0 V sur chaque doigt de donnee)
 *    LED allumee : D0-D7 a 1  (on doit lire ~3,3 V sur chaque doigt de donnee)
 * Mesure au voltmetre entre chaque doigt de donnee et la masse (doigt a7).
 * Verifie tout le chemin : sortie du Pico, contact dans le support, piste, doigt.
 */
#include "pico/stdlib.h"
#include "hardware/structs/sio.h"
#include "exlpins.h"

int main(void) {
    pins_gpio_init();
    uint32_t m = 0;
    for (int j = 0; j < 8; j++) m |= 1u << PIN_D[j];
    gpio_init(25); gpio_set_dir(25, GPIO_OUT);
    sio_hw->gpio_oe_set = m;
    for (int k = 0;; k ^= 1) {
        if (k) sio_hw->gpio_set = m; else sio_hw->gpio_clr = m;
        gpio_put(25, k);
        sleep_ms(5000);
    }
}
