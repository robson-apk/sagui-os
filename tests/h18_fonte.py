#!/usr/bin/env python3
"""H18 - fonte própria e terminal gráfico.

Critério: captura mostra o terminal gráfico com o texto
`Ação, vovô, pão, ÇÃO, à é í ó ú` correto.
Também: edição da linha com as setas (ajuste do teste da fase 3),
texto de um programa em segundo plano não se mistura com a linha digitada, e
sem modo gráfico o sistema avisa e continua em modo texto.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, imagem_console  # noqa: E402

FRASE = "Ação, vovô, pão, ÇÃO, à é í ó ú"


def main():
    with Maquina("h18", "ide", imagem=imagem_console()) as m:
        m.esperar_serial("sagui:/$", 120)
        tela = m.tela_texto()
        verificar(len(tela) == 48 and len(tela[1]) > 100, "terminal gráfico de 128x48 caracteres (1024x768)")
        verificar(any("fonte própria 8x16 com acentos" in l for l in tela), "etapa do vídeo aparece com acentos na tela")
        m.comando("limpar")
        m.comando("echo " + FRASE)
        tela = m.tela_texto()
        verificar(any(l == FRASE for l in tela), f"a tela mostra exatamente: {FRASE}")
        m.comando("echo ÁÉÍÓÚ âêô ãõ Ç ü à")
        verificar(any(l == "ÁÉÍÓÚ âêô ãõ Ç ü à" for l in m.tela_texto()), "maiúsculas acentuadas, circunflexo, til, trema e crase")
        m.captura("H18-fonte-acentos")

        # Edição com as setas: "echo abc", 2x esquerda, X, Delete, End, "!"
        out = m.comando("echo abc", teclas_finais=("left", "left", "shift-x", "delete", "end", "shift-1", "ret"))
        verificar("aXc!" in out, "setas, Delete e End editam no meio da linha (echo aXc!)")
        out = m.comando("cho meio", teclas_finais=("home", "e", "ret"))
        verificar("meio" in out and "executar /bin/echo" in out, "Home leva ao começo da linha")

        # Programa em segundo plano escrevendo enquanto se digita.
        m.comando("contador 2 &")
        m.digitar("ech", 0.04)
        m.esperar_serial("2 de 2 segundos", 15)
        time.sleep(0.5)
        out = m.comando("o pronto")
        verificar("pronto" in out and "executar /bin/echo" in out, "a linha digitada continua inteira depois do contador escrever")
        tela = m.tela_texto()
        verificar(not any("ech[contador" in l or "ech[" in l for l in tela), "o texto do contador não se mistura com a linha digitada")
        verificar(any(l.startswith("sagui:/$ echo pronto") for l in tela), "o shell redesenhou o prompt com o que já estava digitado")
        m.captura("H18-edicao-e-segundo-plano")

    with Maquina("h18-texto", "ide", imagem=imagem_de_teste("")) as m:   # sem --vesa
        m.esperar_serial("sagui:/$", 120)
        tela = "\n".join(m.tela_texto())
        verificar("Modo gr" in tela and "indispon" in tela and "modo texto" in tela,
                  "sem VESA: aviso na tela e o sistema continua em modo texto")
        out = m.comando("echo ainda funciona")
        verificar("ainda funciona" in out, "o shell funciona no modo texto")
    return 0


if __name__ == "__main__":
    sys.exit(main())
