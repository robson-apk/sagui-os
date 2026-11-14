#!/usr/bin/env python3
"""H86: Sagui Monitor (pull request #7) em janela.

Abre /bin/sagui-monitor e confere:
- a primeira amostra: processos listados, CPU e memória entre 0 e 100%;
- a CPU total é o que não é do processo ocioso (com o sistema parado, baixa);
- as teclas M, N e P reordenam a lista (por memória, nome e CPU);
- a escolha segue o processo ao reordenar, e a seta desce uma linha;
- a janela é pintada com as cores do monitor;
- Q fecha o programa com código 0;
- o ajuda do shell lista o programa, e o menu do sagui o abre.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste  # noqa: E402
from PIL import Image  # noqa: E402


def main():
    img = imagem_de_teste("/bin/sagui-monitor", ["--vesa"])
    with Maquina("h86", "ide", imagem=img) as m:
        m.esperar_serial("sagui-monitor: ", 120)
        s = m.serial()
        r = re.search(r"sagui-monitor: (\d+) processos, CPU (\d+)\.(\d)%, memória (\d+)\.(\d)%", s)
        verificar(r is not None, "primeira amostra registrada")
        n, cpu, mem = int(r.group(1)), int(r.group(2)), int(r.group(4))
        verificar(n >= 3, f"lista com os processos do sistema ({n})")
        verificar(0 <= cpu <= 100 and 0 <= mem <= 100, f"CPU {cpu}% e memória {mem}% entre 0 e 100")

        # dá tempo para uma amostra de verdade (a primeira compara com 300 ms)
        time.sleep(3)

        def apertar(tecla, padrao):
            antes = len(re.findall(padrao, m.serial()))
            m.tecla(tecla)
            fim = time.time() + 15
            while len(re.findall(padrao, m.serial())) <= antes and time.time() < fim:
                time.sleep(0.2)
            achados = re.findall(padrao, m.serial())
            return achados[-1] if len(achados) > antes else None

        x = apertar("m", r"sagui-monitor: ordem MEM, primeiro (\S+)")
        verificar(x == "sagui-monitor", f"M: maior memória primeiro ({x})")
        x = apertar("n", r"sagui-monitor: ordem NOME, primeiro (\S+)")
        verificar(x == "janelas", f"N: ordem alfabética ({x})")
        x = apertar("down", r"sagui-monitor: escolhido PID \d+ \((\S+)\)")
        # a escolha segue o processo: começa no ocioso (1º por CPU), que na ordem
        # por nome (janelas, ocioso, rede, sagui-monitor) é o 2º; a seta desce para rede
        verificar(x == "rede", f"seta para baixo: a escolha segue o processo e desce uma linha ({x})")
        x = apertar("p", r"sagui-monitor: ordem CPU, primeiro (\S+)")
        verificar(x == "ocioso", f"P: maior CPU primeiro, o ocioso com o sistema parado ({x})")

        jan = re.search(r'criada por \d+: "Sagui Monitor" (\d+)x(\d+) em \((\d+),(\d+)\)', m.serial())
        verificar(jan is not None, "janela Sagui Monitor criada")
        jl, ja, jx, jy = (int(v) for v in jan.groups())
        im = Image.open(m.captura("H86-sagui-monitor")).convert("RGB")
        fundo = sum(1 for xx in range(jx, jx + jl, 8) for yy in range(jy, jy + ja, 8)
                    if im.getpixel((xx, yy)) == (0x1E, 0x1E, 0x2E))
        rosa = sum(1 for xx in range(jx, jx + jl, 2) for yy in range(jy, jy + 120, 2)
                   if im.getpixel((xx, yy)) == (0xF3, 0x8B, 0xA8))
        verificar(fundo > 2000, f"fundo escuro do monitor pintado ({fundo})")
        verificar(rosa > 3, f"barra ou histórico da CPU pintados ({rosa})")

        m.tecla("q")
        m.esperar_serial("(sagui-monitor) terminou com codigo 0", 20)
        verificar(True, "Q fecha com código 0")

    # Sistema normal: o ajuda e o menu do sagui (o 9º item, depois de Configurações).
    with Maquina("h86-menu", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("ajuda")
        verificar("sagui-monitor" in out, "o ajuda lista o sagui-monitor")
        out = m.comando("ajuda sagui-monitor")
        verificar("CPU, memória e processos" in out, "ajuda sagui-monitor explica o programa")
        antes = len(m.serial())
        m.clique(40, 12)
        fim = time.time() + 10
        while "menu aberto" not in m.serial()[antes:] and time.time() < fim:
            time.sleep(0.2)
        time.sleep(0.5)
        m.captura("H86-menu")
        m.clique(40, 24 + 4 + 4 + 8 * 26 + 12)
        fim = time.time() + 30
        while 'criada por' not in m.serial()[antes:] or '"Sagui Monitor"' not in m.serial()[antes:]:
            if time.time() > fim:
                break
            time.sleep(0.3)
        verificar("abrir /bin/sagui-monitor" in m.serial()[antes:], "o menu do sagui abre /bin/sagui-monitor")
        verificar('"Sagui Monitor"' in m.serial()[antes:], "a janela Sagui Monitor aparece")


if __name__ == "__main__":
    main()
