#!/usr/bin/env python3
"""H95 - atalhos de teclado do sistema.

Critérios (pelo log do kernel; as teclas vão pelo sendkey do QEMU):
  - Windows+Esquerda e Windows+Direita encaixam a janela em foco na metade;
  - Windows+Cima maximiza; Windows+Baixo volta ao tamanho de antes;
  - Alt+Tab troca a janela da frente;
  - Alt+F4 fecha a janela em foco;
  - a tecla Windows sozinha abre o menu Iniciar.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar  # noqa: E402


def esperar(m, texto, tempo=10, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def tecla(m, k):
    n = len(m.serial())
    m.monitor(f"sendkey {k}")
    time.sleep(0.6)
    return n


def main():
    with Maquina("h95", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n0 = len(m.serial())
        m.digitar("arquivos\n", 0.03)
        esperar(m, '"Arquivos"', 30, n0)
        time.sleep(1.5)
        jid = re.findall(r'\[GUI\] janela (\d+) criada por \d+: "Arquivos"', m.serial())[-1]

        n = tecla(m, "meta_l-left")
        esperar(m, f"janela {jid} encaixada na zona 2", 10, n)
        verificar(True, "Windows+Esquerda encaixa na metade esquerda")
        n = tecla(m, "meta_l-right")
        esperar(m, f"janela {jid} encaixada na zona 3", 10, n)
        verificar(True, "Windows+Direita encaixa na metade direita")
        m.captura("H95-1-direita")
        n = tecla(m, "meta_l-up")
        esperar(m, f"janela {jid} maximizada", 10, n)
        verificar(True, "Windows+Cima maximiza")
        n = tecla(m, "meta_l-down")
        esperar(m, f"janela {jid} voltou ao tamanho de antes", 10, n)
        verificar(True, "Windows+Baixo volta ao tamanho de antes")

        n = tecla(m, "alt-tab")
        esperar(m, "[GUI] alt+tab: janela", 10, n)
        outra = re.findall(r"alt\+tab: janela (\d+)", m.serial()[n:])[-1]
        verificar(outra != jid, f"Alt+Tab traz outra janela para a frente ({outra})")
        time.sleep(0.5)
        n = tecla(m, "alt-tab")
        esperar(m, f"[GUI] alt+tab: janela {jid}", 10, n)
        verificar(True, "Alt+Tab de novo volta ao Arquivos")

        n = tecla(m, "alt-f4")
        esperar(m, f"alt+f4: fechar janela {jid}", 10, n)
        verificar(True, "Alt+F4 fecha a janela em foco")
        time.sleep(1)

        n = tecla(m, "meta_l")
        esperar(m, "[GUI] menu aberto", 10, n)
        verificar(True, "a tecla Windows sozinha abre o menu Iniciar")
        m.captura("H95-2-menu")


if __name__ == "__main__":
    main()
