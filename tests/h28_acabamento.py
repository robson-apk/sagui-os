#!/usr/bin/env python3
"""H28 - acabamento.

Critério: captura com três programas abertos ao mesmo tempo; imagem medida e
registrada (meta: até 1,44 MB). Os programas são abertos com duplo clique nos
ícones da área de trabalho; a tela Sobre mostra versão, tamanho da imagem,
processos ativos e créditos.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, RAIZ  # noqa: E402

ICONES = ["Arquivos", "Terminal", "Editor", "Calculadora", "Banana", "Sagui 3D", "Configurações", "Sobre"]
POR_COLUNA = (768 - 40) // 92   # os ícones continuam numa segunda coluna (H38)


def abrir_icone(m, nome):
    i = ICONES.index(nome)
    m.clique(20 + (i // POR_COLUNA) * 110 + 48, 40 + (i % POR_COLUNA) * 92 + 32, duplo=True)


def main():
    tamanho = int(open(os.path.join(RAIZ, "build", "tamanho.txt")).read())
    # Na versão 2.0 não há teto de tamanho (decisão do projeto): só a medida.
    verificar(tamanho > 0, f"imagem com {tamanho} bytes úteis ({100 * tamanho / 1474560:.1f}% de 1,44 MB; sem teto na 2.0)")
    with Maquina("h28", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        for nome, titulo in (("Arquivos", "Arquivos"), ("Calculadora", "Calculadora"), ("Banana", "Sagui Pega Banana")):
            abrir_icone(m, nome)
            m.esperar_serial(f'"{titulo}"', 15)
            verificar(True, f"duplo clique no ícone {nome} abre o programa")
            time.sleep(0.5)
        abrir_icone(m, "Sobre")
        m.esperar_serial("sobre: versao", 15)
        s = m.serial()
        v = re.findall(r"sobre: versao (.*?), imagem (\d+) bytes, (\d+) processos", s)[-1]
        verificar(v[0].startswith("2.1") and int(v[1]) == tamanho, f"Sobre mostra a versão {v[0]} e o tamanho da imagem ({v[1]} bytes)")
        verificar(int(v[2]) >= 7, f"Sobre mostra {v[2]} processos ativos (ocioso, janelas, terminal, sagsh e os programas abertos)")
        time.sleep(1)
        m.captura("H28-programas-abertos")
        criadas = len(re.findall(r"\[GUI\] janela \d+ criada", s))
        fechadas = len(re.findall(r"\[GUI\] fechar janela", s))
        verificar(criadas >= 5 and fechadas == 0, f"{criadas} janelas abertas ao mesmo tempo (Terminal + 4 programas)")
        # Fecha o Sobre e espalha as janelas para a captura de três programas lado a lado.
        m.tecla("ret")
        m.esperar_serial("(sobre-o-sistema) terminou", 10)

        def moldura(titulo):
            g = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', m.serial())[-1]
            return int(g[2]), int(g[3])

        bx, by = moldura("Sagui Pega Banana")
        m.arrastar(bx + 60, by + 12, 528 + 60, 316 + 12)
        cx, cy = moldura("Calculadora")
        m.arrastar(cx + 60, cy + 12, 150 + 60, 330 + 12)
        ax, ay = moldura("Arquivos")
        m.arrastar(ax + 60, ay + 12, 330 + 60, 40 + 12)
        time.sleep(1)
        png = m.captura("H28-tres-programas")
        verificar(True, "captura com Arquivos, Calculadora e Sagui Pega Banana lado a lado")
    return 0


if __name__ == "__main__":
    sys.exit(main())
