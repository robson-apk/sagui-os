#!/usr/bin/env python3
"""Vistoria visual: captura a área de trabalho, o menu e cada programa com janela."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina

PROGS = ["configuracoes relogio", "configuracoes aparencia", "configuracoes som", "configuracoes rede",
         "configuracoes programas", "configuracoes sobre", "configuracoes ia", "arquivos", "calculadora",
         "editor", "sagui-monitor", "visualizador", "musica", "sobre-o-sistema", "energia", "navegador"]

def main():
    os.makedirs("/tmp/vistoria", exist_ok=True)
    so = sys.argv[1:] 
    if not so or "base" in so:
        with Maquina("vis", "ide", memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120); time.sleep(2)
            m.captura("H99-00-area")
            m.clique(40, 12); time.sleep(1); m.captura("H99-01-menu")
    for i, p in enumerate(PROGS):
        nome = p.replace(" ", "-")
        if so and nome not in so: continue
        with Maquina("vis", "ide", memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120); time.sleep(1)
            m.digitar(p + "\n", 0.03)
            time.sleep(5)
            m.captura(f"H99-{i+2:02d}-{nome}")

if __name__ == "__main__":
    main()
