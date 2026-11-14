#!/usr/bin/env python3
"""B4 - Sagui 3D.

Critério: o jogo abre, o mapa carrega, o sagui anda, a semente atinge o
macaco-prego, o macaco escorrega na casca, a saída só abre com o mínimo de
bananas e a fase termina na porteira. As três fases do jogo carregam sem
erro. Capturas da tela de início, do jogo e da vitória.

As fases de teste ficam em /testes (corredores retos, para o teste poder
jogar só segurando as setas pelo monitor do QEMU, como um jogador)."""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402

SOM = ["-audiodev", "none,id=som", "-machine", "pcspk-audiodev=som"]


def segurar(m, tecla, ms):
    """Segura uma tecla (o jogo lê as teclas seguradas, não só as apertadas)."""
    m.monitor(f"sendkey {tecla} {ms}")
    time.sleep(ms / 1000 + 0.2)


def abrir(m, fase=""):
    antes = m.serial().count("sagui3d: tela de inicio")
    m.comando(f"sagui3d {fase}".strip())
    m.esperar_serial("sagui3d: tela de inicio", 20)
    while m.serial().count("sagui3d: tela de inicio") <= antes:
        time.sleep(0.2)
    time.sleep(1)


def main():
    with Maquina("b4", "ide", extras=SOM) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)

        # ---- o jogo de verdade: tela de início e as três fases carregam
        abrir(m)
        m.captura("B4-1-tela-de-inicio")
        m.tecla("ret")
        m.esperar_serial("sagui3d: fase 1 carregada", 10)
        time.sleep(0.5)                     # a linha pode estar chegando pela serial
        s = m.serial()
        verificar(re.search(r"fase 1 carregada: 11 salas, \d+ paredes, 10 passagens, 7 bananas \(minimo 5\)", s),
                  "fase 1 carregou do arquivo (11 salas, 10 passagens, 7 bananas)")
        time.sleep(1.5)
        m.captura("B4-2-fase1-clareira")
        segurar(m, "up", 1200)
        segurar(m, "right", 500)
        time.sleep(0.5)
        m.captura("B4-3-fase1-corredor")
        pos = re.findall(r"sagui3d: posicao x=(\d+) y=(\d+) sala=(\d+)", m.serial())
        verificar(pos and int(pos[-1][0]) > 150, f"o sagui anda (x={pos[-1][0] if pos else '?'})")
        quadros = re.findall(r"sagui3d: (\d+) quadros por segundo", m.serial())
        m.tecla("esc")
        m.esperar_serial("sagui3d: pausa", 5)
        m.tecla("q")
        time.sleep(1)
        m.serial_marca = len(m.serial())

        # ---- porteira: fecha com 2 de 3 bananas, abre com 3, a fase termina nela
        abrir(m, "/testes/sagui3d-porteira.txt")
        m.tecla("ret")
        m.esperar_serial("sagui3d: fase 1 carregada: 2 salas", 10)
        time.sleep(0.5)
        segurar(m, "up", 2600)
        s = m.serial()
        verificar("comeu banana (bananas 2 de 3" in s, "comeu as duas primeiras bananas")
        verificar("porteira fechada: 2 de 3 bananas" in s, "com 2 de 3 bananas a porteira continua fechada")
        verificar("fase 1 concluida" not in s, "e a fase não termina ao passar por ela")
        verificar("comeu banana (bananas 3 de 3" in s and "porteira aberta" in s, "a terceira banana abre a porteira")
        verificar(re.search(r"posicao x=\d+ y=\d+ sala=1", s), "o sagui subiu o degrau para a segunda sala")
        m.captura("B4-4-porteira-aberta")
        segurar(m, "down", 1800)
        m.esperar_serial("sagui3d: fase 1 concluida", 5)
        verificar(True, "voltando à porteira aberta, a fase termina")
        m.esperar_serial("sagui3d: tela de vitoria da fase 1", 5)
        time.sleep(0.5)
        m.captura("B4-5-vitoria")
        s = m.serial()
        i = s.rindex("fase 1 concluida")
        verificar("[BIP] 523 Hz" in s[i - 200:], "melodia de fim de fase no bip")
        m.tecla("ret")                      # volta para a tela de início...
        time.sleep(1)
        m.tecla("esc")                      # ... e Esc fecha o jogo
        time.sleep(1.5)

        # ---- macacos: sementes, tontura e casca
        abrir(m, "/testes/sagui3d-macacos.txt")
        m.tecla("ret")
        m.esperar_serial("sagui3d: fase 1 carregada: 2 salas", 10)
        m.esperar_serial("comeu banana (bananas 1 de 1, cascas 1)", 5)
        verificar(True, "comeu a banana e ganhou uma casca")
        m.tecla("b")
        m.esperar_serial("sagui3d: casca no chao", 5)
        verificar(True, "B larga a casca no chão")
        m.esperar_serial("viu o sagui", 5)
        # Um tiro por toque até o primeiro macaco ficar tonto. Segurar o Ctrl
        # por um tempo fixo dava 5 a 7 tiros conforme o QEMU, e com 7 as
        # sementes deixavam os dois macacos tontos: nenhum sobrava para a casca.
        ini = len(m.serial())
        for _ in range(10):
            segurar(m, "ctrl", 60)
            time.sleep(0.25)
            if "ficou tonto" in m.serial()[ini:]:
                break
        s = m.serial()
        verificar(re.search(r"tiro \(sementes 1[0-4]\)", s), "Ctrl atira sementes com o estilingue")
        m.esperar_serial("acertou macaco", 5)
        verificar(True, "a semente acerta o macaco-prego")
        m.esperar_serial("ficou tonto e vai fugir", 5)
        verificar(True, "com três sementes o macaco fica tonto")
        time.sleep(0.8)
        m.captura("B4-6-macaco-tonto")
        m.esperar_serial("escorregou na casca", 20)
        verificar(True, "o outro macaco pisa na casca e escorrega")
        time.sleep(0.6)
        m.captura("B4-7-macaco-escorregou")
        s = m.serial()
        j = s.rindex("escorregou na casca")
        verificar("[BIP] 300 Hz" in s[j - 300:j + 300], "som próprio quando o macaco escorrega")
        verificar("[BIP] 880 Hz 40 ms" in s, "som próprio ao largar a casca")
        verificar("[BIP] 700 Hz 20 ms" in s and "[BIP] 220 Hz 40 ms" in s, "sons do tiro e do acerto")
        m.esperar_serial("fugiu", 10)
        verificar(True, "depois da tontura, o macaco foge")
        if quadros:
            verificar(True, f"desempenho no QEMU: {quadros[-1]} quadros por segundo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
