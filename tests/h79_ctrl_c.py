#!/usr/bin/env python3
"""H79 (correções do G470) - Ctrl+C encerra o programa que está rodando.

No G470, o `tocar` ficou parado em "Tocando..." (a placa de som não andava) e
o Ctrl+C não fazia nada: só o shell lia a tecla, e só quando esperava uma
linha. Critério: com o `contador 100` rodando no Terminal, Ctrl+C mostra ^C,
volta ao prompt e o contador some da lista de processos; no prompt, Ctrl+C
continua só descartando a linha. Backspace no fim da linha apaga a letra
na tela (antes só voltava o cursor e a letra ficava).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, cor_do_pixel  # noqa: E402


def main():
    with Maquina("h79c", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        # Backspace: "sagui:/$ " ocupa 9 colunas da linha 1 (a 0 é a saudação).
        j = janela(m.serial(), "Terminal")
        m.digitar("zzzz", 0.05)
        m.tecla("backspace")
        m.tecla("backspace")
        time.sleep(0.8)
        png = m.captura("H79-backspace", guardar=False)

        def celula(lin, col):
            x0, y0 = j["x"] + 4 + col * 8, j["y"] + 24 + 4 + lin * 16
            return {cor_do_pixel(png, x0 + i, y0 + k) for i in range(8) for k in range(16)}
        fundo = celula(1, 60)
        verificar(len(celula(1, 10)) > 1, "as letras que ficaram continuam na tela")
        verificar(celula(1, 12) == fundo, "Backspace apagou a última letra na tela (não só voltou o cursor)")
        m.tecla("ctrl-c")
        time.sleep(0.5)
        antes = m.serial().count("sagui:")
        m.digitar("contador 100", 0.04)
        m.tecla("ret")
        m.esperar_serial("de 100 segundos", 30)
        m.tecla("ctrl-c")
        fim = time.time() + 15
        while m.serial().count("sagui:") <= antes and time.time() < fim:
            time.sleep(0.2)
        verificar(m.serial().count("sagui:") > antes, "Ctrl+C devolve o prompt com o programa rodando")
        verificar("Ctrl+C encerrou contador" in m.serial(), "o terminal encerrou o contador")
        out = m.comando("ps")
        verificar("contador" not in out, "o contador não está mais na lista de processos")
        # No prompt, Ctrl+C só descarta a linha (o shell continua vivo).
        m.digitar("abc", 0.04)
        m.tecla("ctrl-c")
        out = m.comando("ola")
        verificar("Ol" in out, "o shell segue funcionando depois do Ctrl+C")
        m.captura("H79-ctrl-c")
        # clear/limpar apaga a tela do Terminal em janela (antes mandava só
        # ESC[J, que apaga do cursor para baixo, e nada sumia).
        m.comando("ls bin")
        m.comando("limpar")
        time.sleep(0.8)
        png = m.captura("H79-limpar", guardar=False)
        fundo = celula(20, 60)
        vazias = all(celula(l, c) == fundo for l in (3, 8, 15) for c in (0, 5, 20))
        verificar(vazias, "limpar apaga a tela do Terminal (as linhas de baixo ficaram vazias)")
        verificar(len(celula(0, 2)) > 1, "o prompt volta para a primeira linha")
    return 0


if __name__ == "__main__":
    sys.exit(main())
