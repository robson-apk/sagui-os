#!/usr/bin/env python3
"""H79 - correções depois do teste no G470 (13/11/2026).

Critérios:
  - `registro` mostra o diário do kernel, com filtro (`registro VIDEO`), e a
    memória de vídeo está com escrita combinada (PAT);
  - um programa de texto compilado pelo cc roda no Terminal com `./teste`;
  - instalado no pendrive, o ícone dele abre um Terminal rodando o programa
    (antes rodava escondido e "nada acontecia");
  - botão direito num ícone abre o menu (Abrir, Renomear...); Renomear troca
    o nome do programa no pendrive;
  - o diário do som registra os pinos do codec e o estado do fluxo.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, criar_pendrive, pendrive_qemu, mtools  # noqa: E402

PROGRAMA = '#include "sagui.h"\n\nint principal(int argc, char **argv)\n{\n    imprimir("oi do G470\\n");\n    return 0;\n}\n'


def esperar(m, texto, tempo=30, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-400:]!r}")


def main():
    pen = criar_pendrive("h79-pen", pastas=(".saguios", "programas"), arquivos={".saguios/configuracoes.txt": "som.volume = 80\n"})
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen) + ["-audiodev", "wav,id=som,path=" + os.path.join(os.path.dirname(pen), "h79.wav"), "-device", "intel-hda", "-device", "hda-duplex,audiodev=som"]
    with Maquina("h79", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(4)                                      # o diagnóstico do som sai 3 s depois da partida
        out = m.comando("registro VIDEO", tempo=20)
        verificar("[VIDEO]" in out and "escrita combinada (PAT/WC)" in out, "registro mostra o diário, com filtro; vídeo com escrita combinada")
        out = m.comando("registro HDA", tempo=20)
        verificar("[HDA] pino" in out and "[HDA] fluxo:" in out and "andando" in out,
                  "o diário do som tem os pinos do codec e o fluxo andando")
    # Segunda volta, com o programa já no pendrive.
    fonte = os.path.join(os.path.dirname(pen), "teste79.c")
    with open(fonte, "w") as f:
        f.write(PROGRAMA)
    mtools(pen, "mmd", "p:/fontes")
    mtools(pen, "mcopy", "-o", fonte, "p:/fontes/teste.c")
    with Maquina("h79b", "ide", extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        m.comando("cd /tmp", tempo=10)
        out = m.comando("cc /usb/fontes/teste.c -o teste", tempo=90)
        verificar("erro" not in out, "cc compilou o programa de texto")
        out = m.comando("./teste", tempo=30)
        verificar("oi do G470" in out, "./teste roda na pasta atual e escreve no Terminal")
        out = m.comando("cc /usb/fontes/teste.c -o /usb/programas/teste", tempo=90)
        antes = len(m.serial())
        esperar(m, "programas instalados no pendrive: 1", 30, 0)
        time.sleep(3)
        # Ícones: 8 do sistema + o pendrive + o instalado (10º): coluna 2, linha 3.
        x, y = 20 + 110 + 48, 24 + 16 + 2 * 92 + 30
        m.captura("H79-1-icone-instalado")
        antes = len(m.serial())
        m.clique(x, y, duplo=True)
        s = esperar(m, "icone instalado teste -> terminal -e /usb/programas/teste", 20, antes)
        esperar(m, "terminal: programa da aba teste terminou (codigo 0)", 30, antes)
        verificar(True, "o ícone de um programa de texto abre um Terminal rodando o programa")
        time.sleep(1)
        m.captura("H79-2-programa-no-terminal")
        m.tecla("ret", 0.3)                                # fecha a janela do programa
        time.sleep(1)
        # Botão direito > Renomear.
        antes = len(m.serial())
        m.clique(x, y, botao=2)
        esperar(m, "menu do icone", 10, antes)
        time.sleep(0.5)
        m.captura("H79-3-menu-do-icone")
        m.clique(x + 20, y + 4 + 26 + 12)                  # o segundo item: Renomear...
        esperar(m, '"Renomear"', 20, antes)
        time.sleep(0.8)
        for _ in range(10):
            m.tecla("backspace", 0.03)
        m.digitar("saudacao\n", 0.05)
        esperar(m, "icone: /usb/programas/teste -> /usb/programas/saudacao (0)", 20, antes)
        verificar(True, "botão direito > Renomear trocou o nome do programa no pendrive")
        esperar(m, "programas instalados no pendrive", 30, antes)
        time.sleep(1)
        m.captura("H79-4-renomeado")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
