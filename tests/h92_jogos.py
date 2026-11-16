#!/usr/bin/env python3
"""H92 - o item "Jogos" do menu do sagui e o lançador de jogos.

Critérios:
  - o menu do sagui tem o item "Jogos" (6º item, no lugar de Banana e Sagui
    3D) e ele abre a janela "Jogos" (/bin/jogos);
  - o lançador mostra os jogos que existem em /bin, cada um com o ícone;
  - um duplo clique num jogo abre o jogo (cada um escreve a sua linha no log);
  - as setas e o Enter também abrem um jogo;
  - os ícones de Banana e Sagui 3D continuam na área de trabalho.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, RAIZ, FalhaTeste  # noqa: E402

# A ordem da tabela de userland/programas/jogos.c: programa e a linha que o
# jogo escreve no log quando a janela fica pronta.
JOGOS = [
    ("banana", 'criada por', "Sagui Pega Banana"),
    ("sagui3d", 'criada por', "Sagui 3D"),
    ("cacabananas", "cacabananas: pronto", "Caça-Bananas"),
    ("paciencia", "paciencia: pronto", "Paciência Banana"),
    ("chip8", "chip8: lista pronta", "CHIP-8"),
    ("jclic", "jclic: lista pronta", "Atividades JClic"),
]
COLUNAS, CELULA_L, CELULA_A, MARGEM, TOPO = 3, 168, 124, 12, 44


def esperar(m, texto, tempo, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        s = m.serial()[desde:]
        if texto in s:
            return s
        time.sleep(0.3)
    raise FalhaTeste(f"não apareceu no log: {texto!r}")


def main():
    presentes = [j for j in JOGOS if os.path.exists(os.path.join(RAIZ, "build", "userland", "bin", j[0]))]
    with Maquina("h92", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        antes = len(m.serial())
        m.clique(40, 12)
        esperar(m, "menu aberto", 10, antes)
        time.sleep(0.5)
        m.captura("H92-1-menu-jogos")
        m.digitar("jogos\n", 0.08)                 # a busca do menu Iniciar; Enter abre o primeiro
        s = esperar(m, "jogos: pronto", 20, antes)
        verificar("[GUI] menu: Jogos" in s, "o menu Iniciar acha e abre \"Jogos\"")
        verificar(f"jogos: pronto, {len(presentes)} jogos" in s, f"o lançador mostra os {len(presentes)} jogos de /bin")
        time.sleep(1)
        jan = janela(m.serial(), "Jogos")
        m.captura("H92-2-lancador")

        def celula(i):
            return (jan["x"] + MARGEM + (i % COLUNAS) * CELULA_L + 80,
                    jan["y"] + TOPO + 4 + (i // COLUNAS) * CELULA_A + 40)

        for i, (prog, linha, titulo) in enumerate(presentes):
            antes = len(m.serial())
            m.clique(*celula(i), duplo=True)
            s = esperar(m, f"jogos: abrir /bin/{prog}", 15, antes)
            esperar(m, f'criada por', 20, antes)
            s = esperar(m, linha, 30, antes)
            verificar(f'"{titulo}"' in m.serial()[antes:], f"duplo clique no lançador abre {titulo}")
            time.sleep(1.5)
            # Fecha o jogo pelo X da moldura para o próximo não abrir por cima.
            jg = janela(m.serial(), titulo)
            m.clique(jg["fx"] + jg["l"] + 4 - 2 - 20 + 8, jg["fy"] + 12)
            time.sleep(1)

        # Pelo teclado: clicar no primeiro, seta para a direita e Enter abre o segundo.
        antes = len(m.serial())
        m.clique(*celula(0))
        m.tecla("right")
        m.tecla("ret")
        esperar(m, f"jogos: abrir /bin/{presentes[1][0]}", 15, antes)
        verificar(True, "seta e Enter abrem o jogo escolhido")
    return 0


if __name__ == "__main__":
    sys.exit(main())
