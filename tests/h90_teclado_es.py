#!/usr/bin/env python3
"""H90 - layout espanhol do teclado e troca de layout.

Critério: `teclado` mostra o layout (abnt2 por padrão); `teclado es` grava
teclado.layout = es e, a partir daí, as teclas seguem o teclado espanhol ISO
(ñ à direita do L, ç à direita do ´, AltGr+2 = @, AltGr+4 = ~ morto, ´ ¨ ` ^
mortos, ¿ ¡, º ª); `teclado abnt2` volta ao ABNT2.

O QEMU nomeia as teclas pela posição física no teclado americano, por isso o
teste manda os nomes crus (a tecla "semicolon" é o ñ no espanhol e o ç no
ABNT2).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_console, FalhaTeste, janela  # noqa: E402

# (teclas do QEMU no layout espanhol, o que deve sair, descrição)
ROTEIRO_ES = [
    (["semicolon", "a", "n", "d", "apostrophe", "u"], "ñandú", "ñ (0x27) e ´ + u"),
    (["spc", "shift-semicolon"], " Ñ", "Shift + ñ"),
    (["spc", "backslash", "shift-backslash"], " çÇ", "ç Ç (0x2B)"),
    (["spc", "alt_r-2", "alt_r-3"], " @#", "AltGr+2 = @, AltGr+3 = #"),
    (["spc", "alt_r-4", "a", "alt_r-4", "spc"], " ã~", "AltGr+4 = ~ morto (+ a, + espaço)"),
    (["spc", "bracket_left", "a", "shift-bracket_left", "e", "shift-apostrophe", "u"], " àêü", "` ^ ¨ mortos"),
    (["spc", "apostrophe", "spc"], " ´", "´ + espaço = ´"),
    (["spc", "shift-equal", "equal"], " ¿¡", "¿ ¡ (0x0D)"),
    (["spc", "grave_accent", "shift-grave_accent", "shift-3"], " ºª·", "º ª (0x29) e Shift+3 = ·"),
    (["spc", "shift-7", "slash", "shift-0"], " /-=", "/ - = nas posições espanholas"),
    (["spc", "alt_r-bracket_left", "alt_r-bracket_right", "alt_r-apostrophe", "alt_r-backslash"], " []{}", "AltGr: [ ] { }"),
    (["spc", "caps_lock", "semicolon", "b", "caps_lock", "semicolon"], " ÑBñ", "Caps Lock vale para o ñ"),
]


def comando_cru(m, teclas, tempo=60):
    """Como Maquina.comando, mas mandando nomes de teclas do QEMU."""
    antes = m.serial()
    n = antes.count("sagui:")
    for t in teclas:
        m.tecla(t, 0.05)
    m.tecla("ret", 0.05)
    fim = time.time() + tempo
    while m.serial().count("sagui:") <= n:
        if time.time() > fim:
            raise FalhaTeste("o comando não voltou ao prompt")
        time.sleep(0.2)
    return m.serial()[len(antes):].replace("\r", "")


def main():
    with Maquina("h90", "ide", imagem=imagem_console()) as m:
        m.esperar_serial("[SAGUI] shell iniciado: processo", 120)
        m.esperar_serial("sagui:/$", 30)

        out = m.comando("teclado")
        verificar("Layout do teclado: abnt2" in out, "teclado sem argumento: abnt2 é o padrão")
        out = m.comando("teclado xx")
        verificar("uso: teclado" in out, "layout desconhecido mostra o uso")
        out = m.comando("ajuda teclado")
        verificar("Uso: teclado" in out, "ajuda teclado")

        out = m.comando("teclado es")
        verificar("Layout do teclado: es" in out, "teclado es troca o layout")
        verificar("[CONFIG] teclado.layout = es" in m.serial(), "a escolha vai para a configuração teclado.layout")
        out = m.comando("config teclado.layout")
        verificar("teclado.layout = es" in out, "config teclado.layout = es")

        teclas = ["e", "c", "h", "o", "spc"]
        esperado = ""
        for t, s, _ in ROTEIRO_ES:
            teclas += t
            esperado += s
        out = comando_cru(m, teclas)
        m.captura("H90-teclado-es")
        # A saída do echo é a linha logo depois do aviso do kernel de que ele começou
        # (antes vem o eco do que foi digitado).
        linhas = out.split("\n")
        saida = ""
        for i, l in enumerate(linhas[:-1]):
            if "executar /bin/echo" in l:
                saida = linhas[i + 1]
        pos = 0
        for _, s, desc in ROTEIRO_ES:
            verificar(saida[pos:pos + len(s)] == s, f"{desc}: {s!r} (obtido {saida[pos:pos + len(s)]!r})")
            pos += len(s)
        verificar(saida == esperado, f"linha completa no layout espanhol (obtido {saida!r})")

        out = m.comando("teclado")
        verificar("Layout do teclado: es" in out, "teclado mostra o layout atual (es)")

        # Volta ao ABNT2: o harness digita pelo mapa ABNT2 de novo.
        out = m.comando("teclado abnt2")
        verificar("Layout do teclado: abnt2" in out, "teclado abnt2 volta ao ABNT2")
        out = m.comando("echo ação @ é /")
        verificar(re.search(r"^ação @ é /$", out, re.M), "ABNT2 de novo: ç, ~, @, ´ e / nas posições brasileiras")

    # A aba Teclado das Configurações, na área de trabalho.
    with Maquina("h90-janela", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("configuracoes teclado")
        m.esperar_serial("configuracoes: aberto na aba Teclado", 15)
        time.sleep(0.6)
        j = janela(m.serial(), "Configurações")
        x0, y0 = j["x"], j["y"]
        m.clique(x0 + 190 + 150, y0 + 116 + 14)            # botão "Espanhol (ES)"
        m.esperar_serial("configuracoes: teclado es", 10)
        verificar("[CONFIG] teclado.layout = es" in m.serial(), "o botão Espanhol grava teclado.layout = es")
        time.sleep(0.5)
        m.captura("H90-configuracoes-teclado")
        m.clique(x0 + 190 + 150, y0 + 80 + 14)             # botão "Português do Brasil (ABNT2)"
        m.esperar_serial("configuracoes: teclado abnt2", 10)
        verificar("[CONFIG] teclado.layout = abnt2" in m.serial(), "o botão ABNT2 volta para teclado.layout = abnt2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
