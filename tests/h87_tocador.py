#!/usr/bin/env python3
"""H87 - o Tocador de músicas.

Critérios:
  - no Arquivos, dois cliques num .wav abrem o Tocador com a música (abrir pela
    extensão), e o botão direito oferece "Abrir no Tocador";
  - o Tocador toca o WAV inteiro (2 músicas na pasta: a lista mostra as duas
    e, no fim da primeira, passa para a segunda);
  - pausar para de entregar som; o som gravado pelo QEMU tem as duas
    frequências (440 Hz e 660 Hz).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, SAIDA, criar_pendrive, pendrive_qemu  # noqa: E402
from h40_som import som_qemu, ler_wav  # noqa: E402
from h41_mistura import energia  # noqa: E402
from h42_tocar import wav_teste  # noqa: E402


def esperar(m, texto, tempo=30, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def main():
    wav = os.path.join(SAIDA, "h87.wav")
    pen = criar_pendrive("h87-pen", rotulo="DADOS", pastas=("musicas",),
                         arquivos={"musicas/a-la440.wav": wav_teste(440, 22050, 2.0),
                                   "musicas/b-mi660.wav": wav_teste(660, 44100, 2.0),
                                   "musicas/c-seno440.mp3": open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                              "recursos", "h86_seno440.mp3"), "rb").read(),
                                   "musicas/z-pagina.html": b"<html><body>oi</body></html>",
                                   "musicas/z-texto.txt": b"oi"})
    with Maquina("h87", "ide", memoria="768M", extras=som_qemu(wav) + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")
        # Arquivos na pasta das músicas; dois cliques na primeira (a lista é ordenada).
        antes = len(m.serial())
        m.digitar("arquivos /usb/musicas\n", 0.04)
        esperar(m, "arquivos: pronto", 20, antes)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        # A lista começa em (LATERAL + 6, 70); cada item tem 20 de altura.
        xi, yi = a["x"] + 170 + 6 + 60, a["y"] + 70 + 2 + 10
        m.clique(xi, yi, botao=2)                         # botão direito: o menu diz "Abrir no Tocador"
        s = esperar(m, "menu aberto (tipo 3", 10, antes)
        png = m.captura("H87-1-menu-abrir-no-tocador")
        # Ícones por tipo: a nota verde da música no 1º item, a
        # faixa azul da página no 4º, e o texto comum (sem verde) no 5º.
        from qemu import cor_do_pixel as cor
        ix = a["x"] + 140 + 6 + 6   # a lista começa em LATERAL (140) + 6; o ícone, 6 depois

        def cores_do_icone(item):
            y0 = a["y"] + 70 + 2 + item * 20 + 2
            return {cor(png, ix + i, y0 + k) for i in range(16) for k in range(16)}
        verificar((0x0B, 0x7A, 0x3E) in cores_do_icone(0), "o .wav tem o ícone de música (nota verde)")
        verificar((0x0B, 0x7A, 0x3E) in cores_do_icone(2), "o .mp3 também")
        verificar((0x2A, 0x6F, 0xD8) in cores_do_icone(3), "o .html tem o ícone de página (faixa azul)")
        verificar((0x0B, 0x7A, 0x3E) not in cores_do_icone(4), "o .txt continua com o ícone de texto")
        m.clique(a["x"] + 400, a["y"] + 20)               # fecha o menu sem escolher
        antes = len(m.serial())
        m.clique(xi, yi, duplo=True)
        s = esperar(m, "abrindo /usb/musicas/a-la440.wav com /bin/tocador", 15, antes)
        verificar(True, "dois cliques num .wav abrem o Tocador (pela extensão)")
        s = esperar(m, "tocador: pronto (3 m", 20, antes)   # só as músicas: a página e o texto ficam de fora
        verificar(True, "o Tocador lista as 3 músicas da pasta (2 WAV e 1 MP3)")
        time.sleep(1.5)
        m.captura("H87-2-tocador")
        s = esperar(m, "tocador: fim de a-la440.wav", 20, antes)
        esperar(m, "tocador: /usb/musicas/b-mi660.wav (44100 Hz", 10, antes)
        verificar(True, "no fim da primeira, toca a segunda")
        esperar(m, "tocador: fim de b-mi660.wav", 20, antes)
        esperar(m, "tocador: /usb/musicas/c-seno440.mp3 (44100 Hz", 10, antes)
        esperar(m, "tocador: fim de c-seno440.mp3", 20, antes)
        verificar(True, "o MP3 (decodificador do zero) tocou até o fim")
        # Pausar: Espaço com a janela em foco; depois Espaço de novo.
        t = janela(m.serial(), "Tocador")
        antes = len(m.serial())
        m.clique(t["x"] + 96 + 40, t["y"] + 132 + 14)     # Tocar (recomeça a última aberta)
        esperar(m, "tocador: tocando", 10, antes)
        m.clique(t["x"] + 96 + 40, t["y"] + 132 + 14)     # Pausar
        esperar(m, "tocador: pausado", 10, antes)
        verificar(True, "o botão alterna tocar e pausar")
    amostras, taxa = ler_wav(wav)
    jan = taxa // 10
    janelas = [amostras[k:k + jan] for k in range(0, len(amostras) - jan, jan)]
    com_440 = sum(1 for x in janelas if energia(x, taxa, 440) > 800)
    com_660 = sum(1 for x in janelas if energia(x, taxa, 660) > 800)
    verificar(com_440 >= 12, f"a música de 440 Hz tocou inteira ({com_440} pedaços de 0,1 s de 20)")
    verificar(com_660 >= 12, f"a de 660 Hz (44,1 kHz) tocou depois ({com_660} pedaços)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
