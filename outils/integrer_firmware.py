#!/usr/bin/env python3
"""
integrer_firmware.py -- remplace les firmwares integres dans exlpico.html par
ceux que vous venez de compiler, pour que les boutons "UF2 complet",
"UF2 bibliotheque complete" et "UF2 sonde" les utilisent.

Utilisation (depuis le dossier EXLCART) :
    python3 outils/integrer_firmware.py firmware/build/exlcart.uf2 firmware/build/exlsonde.uf2 \
        firmware/build/exlcart_eprom.uf2 firmware/build/exlsonde_eprom.uf2

N'importe lesquels de ces fichiers peuvent etre donnes : on reconnait chacun a son
nom (exlcart / exlsonde : carte lineaire ; exlcart_eprom / exlsonde_eprom : carte
support EPROM).
Une copie de sauvegarde exlpico.html.bak est faite avant modification.
"""
import base64, os, re, shutil, sys

HTML = os.path.join(os.path.dirname(os.path.abspath(__file__)), "exlpico.html")
# du plus precis au plus general : "exlcart_eprom" contient "exlcart"
VARS = [("exlcart_eprom", "FIRMWARE_EPROM_UF2_B64"), ("exlsonde_eprom", "SONDE_EPROM_UF2_B64"),
        ("exlcart", "FIRMWARE_UF2_B64"), ("exlsonde", "SONDE_UF2_B64")]

def main(paths):
    if not paths:
        print(__doc__); return 1
    s = open(HTML, encoding="utf-8").read()
    for p in paths:
        nom = os.path.basename(p).lower()
        var = next((v for k, v in VARS if k in nom), None)
        if not var:
            print("nom non reconnu (exlcart, exlsonde, exlcart_eprom ou exlsonde_eprom attendu) :", p); return 1
        data = open(p, "rb").read()
        if len(data) % 512 or data[:4] != b"UF2\n":
            print("ce n'est pas un fichier .uf2 :", p); return 1
        b64 = base64.b64encode(data).decode()
        s, n = re.subn(r'const ' + var + r' = "[A-Za-z0-9+/=]*";', 'const ' + var + ' = "' + b64 + '";', s)
        if n != 1:
            print("constante", var, "introuvable dans exlpico.html"); return 1
        print("%-17s <- %s (%d octets)" % (var, p, len(data)))
    shutil.copyfile(HTML, HTML + ".bak")
    open(HTML, "w", encoding="utf-8").write(s)
    print("exlpico.html mis a jour (sauvegarde : exlpico.html.bak)")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
