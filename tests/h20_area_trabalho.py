#!/usr/bin/env python3
"""H20 - área de trabalho.

Critério: captura da área de trabalho em docs/capturas/ (fundo com degradê
em cores do Brasil, o mascote sagui em pixel art, barra superior com o nome
do sistema e o relógio CMOS).
"""
import datetime
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, cor_do_pixel  # noqa: E402


def main():
    for modo in ("ide", "usb"):
        with Maquina("h20", modo) as m:
            m.esperar_serial("area de trabalho no ar", 120)
            m.esperar_serial("[GUI] relogio:", 20)
            png = m.captura("H20-area-de-trabalho", guardar=(modo == "ide"))
            topo, meio, base = cor_do_pixel(png, 1000, 40), cor_do_pixel(png, 1000, 384), cor_do_pixel(png, 1000, 760)   # à direita do Terminal, que abre no boot
            verificar(topo[1] > topo[0] and topo[1] > topo[2], f"[{modo}] topo verde {topo}")
            verificar(meio[0] > 180 and meio[1] > 150 and meio[2] < 90, f"[{modo}] meio amarelo {meio}")
            verificar(base[2] > base[0] and base[2] > base[1], f"[{modo}] base azul {base}")
            verificar(sum(cor_do_pixel(png, 300, 2)) < 200, f"[{modo}] barra superior escura (vidro escuro, H74)")
            verificar(cor_do_pixel(png, 975, 600) == (245, 245, 240), f"[{modo}] tufo branco do sagui no canto (o da direita, longe da sombra do Terminal)")
            pasta = cor_do_pixel(png, 68, 72)   # centro do ícone da pasta (64x64, desenhado em x=36, y=40): amarelo
            verificar(pasta[0] > 200 and pasta[1] > 140 and pasta[2] < 130, f"[{modo}] ícone da pasta (Arquivos) {pasta}")
            hora = re.findall(r"\[GUI\] relogio: (\S+) (\d\d/\d\d/\d{4})\s+(\d\d):(\d\d)", m.serial())[-1]
            agora = datetime.datetime.now(datetime.timezone.utc)
            verificar(hora[1] == agora.strftime("%d/%m/%Y"), f"[{modo}] relógio da barra com a data do CMOS: {' '.join(hora)}")
            verificar("Terminal provis" not in m.serial(), f"[{modo}] sem terminal de tela cheia")
    return 0


if __name__ == "__main__":
    sys.exit(main())
