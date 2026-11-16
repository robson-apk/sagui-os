#!/usr/bin/env python3
"""H78 - redimensionar e maximizar janelas.

Critérios: arrastar o canto do Terminal muda o tamanho da janela e o número
de linhas e colunas; o botão de maximizar ocupa a tela entre a barra de cima
e a doca, e clicar de novo volta ao tamanho de antes; duplo clique na barra
de título também maximiza; o Navegador refaz o layout na largura nova; uma
janela de tamanho fixo (Calculadora) não tem o botão. Capturas de cada passo.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, cor_do_pixel  # noqa: E402


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def moldura(m, titulo):
    """Posição e tamanho atuais (o log da última mudança, ou o da criação)."""
    j = janela(m.serial(), titulo)
    wid = re.findall(rf'janela (\d+) criada por \d+: "{re.escape(titulo)}"', m.serial())[-1]
    for linha in m.serial().splitlines():
        r = re.search(rf"janela {wid} redimensionada para (\d+)x(\d+) em \((-?\d+),(-?\d+)\)", linha)
        if r:
            j = dict(l=int(r[1]), a=int(r[2]), fx=int(r[3]), fy=int(r[4]))
        r = re.search(rf"janela {wid} movida para \((-?\d+),(-?\d+)\)", linha)
        if r:
            j = dict(j, fx=int(r[1]), fy=int(r[2]))
    j["x"], j["y"] = j["fx"] + 2, j["fy"] + 24
    j["id"] = wid
    return j


def botao_max(j):
    return j["fx"] + j["l"] + 4 - 2 - 20 - 48 + 8, j["fy"] + 12


def main():
    with Maquina("h78", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        t = moldura(m, "Terminal")
        verificar(f"janela {t['id']} redimensionavel" in m.serial(), "o Terminal se declarou redimensionável")
        # Arrastar o canto de baixo à direita: 160 pixels para a esquerda e 96 para cima.
        canto_x, canto_y = t["fx"] + t["l"] + 4 + 2, t["fy"] + t["a"] + 26 + 2
        antes = len(m.serial())
        m.arrastar(canto_x, canto_y, canto_x - 160, canto_y - 96)
        s = esperar(m, "terminal: tamanho", 20, antes)
        col = re.search(r"terminal: tamanho \d+x\d+ \((\d+) colunas, (\d+) linhas\)", s)
        verificar(col and int(col[1]) == 94 - 20 and int(col[2]) == 28 - 6,
                  f"arrastar o canto encolheu o Terminal para {col[1] if col else '?'} colunas e {col[2] if col else '?'} linhas")
        time.sleep(1)
        m.captura("H78-1-terminal-menor")
        out = m.comando("echo cabe aqui", tempo=20)
        verificar("cabe aqui" in out, "o shell continua funcionando depois de mudar o tamanho")
        # Maximizar pelo botão.
        t = moldura(m, "Terminal")
        antes = len(m.serial())
        m.clique(*botao_max(t))
        s = esperar(m, "maximizada", 20, antes)
        col = re.search(r"terminal: tamanho \d+x\d+ \((\d+) colunas, (\d+) linhas\)", esperar(m, "terminal: tamanho", 20, antes))
        verificar(col and int(col[1]) > 94 and int(col[2]) > 28, f"maximizado: {col[1]} colunas e {col[2]} linhas")
        time.sleep(1)
        m.captura("H78-2-terminal-maximizado")
        # Clicar de novo: volta ao tamanho de antes.
        t2 = moldura(m, "Terminal")
        antes = len(m.serial())
        m.clique(*botao_max(t2))
        esperar(m, "voltou ao tamanho de antes", 20, antes)
        t3 = moldura(m, "Terminal")
        verificar((t3["l"], t3["a"]) == (t["l"], t["a"]), f"voltou a {t3['l']}x{t3['a']}")
        # Duplo clique na barra de título também maximiza.
        antes = len(m.serial())
        m.clique(t3["fx"] + 100, t3["fy"] + 12, duplo=True)
        esperar(m, "maximizada", 20, antes)
        verificar(True, "duplo clique na barra de título maximiza")
        m.clique(t3["fx"] + 100, 24 + 12, duplo=True)
        time.sleep(1)
        # Navegador: maximiza e refaz o layout.
        antes = len(m.serial())
        m.comando("navegador", tempo=30)
        esperar(m, "navegador: pronto sagui:inicio", 30, antes)
        time.sleep(1)
        n = moldura(m, "Cipó")   # o nome do navegador desde a H82
        antes = len(m.serial())
        m.clique(*botao_max(n))
        s = esperar(m, "navegador: tamanho", 20, antes)
        verificar("maximizada" in s, "o Navegador maximizou e refez a página na largura nova")
        time.sleep(1)
        m.captura("H78-3-navegador-maximizado")
        # Calculadora: tamanho fixo, sem o botão de maximizar.
        antes = len(m.serial())
        m.clique(40, 12)                                    # menu do sagui > Calculadora
        time.sleep(0.5)
        m.clique(44 + 3 * 108 + 54, 134 + 0 * 92 + 30)   # H89: grade do menu Iniciar, item 3
        esperar(m, '"Calculadora"', 20, antes)
        time.sleep(1.5)
        c = moldura(m, "Calculadora")
        verificar(f"janela {c['id']} redimensionavel" not in m.serial(), "a Calculadora continua com tamanho fixo")
        antes = len(m.serial())
        m.clique(*botao_max(c))
        time.sleep(1)
        verificar("maximizada" not in m.serial()[antes:], "sem botão de maximizar na Calculadora")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
