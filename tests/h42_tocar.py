#!/usr/bin/env python3
"""H42 - tocador de WAV e sons do sistema.

Critério: "tocar" toca um WAV de 22050 Hz, 16 bits, mono, feito no PC (o
.wav gravado pelo QEMU tem 300 Hz); o canto do sagui do sistema toca; com a
placa de som, o bip (e os sons dos jogos, que usam o bip) sai por ela; os
sons do sistema tocam no início e nos avisos.
"""
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA, criar_pendrive, pendrive_qemu  # noqa: E402
from h40_som import som_qemu  # noqa: E402
from h41_mistura import energia  # noqa: E402
from h40_som import ler_wav  # noqa: E402


def wav_teste(freq, taxa=22050, dur=1.5):
    n = int(taxa * dur)
    dados = b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * freq * i / taxa))) for i in range(n))
    return (b"RIFF" + struct.pack("<I", 36 + len(dados)) + b"WAVE" +
            b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, taxa, taxa * 2, 2, 16) +
            b"data" + struct.pack("<I", len(dados)) + dados)


def main():
    wav = os.path.join(SAIDA, "h42.wav")
    pen = criar_pendrive("h42-pen", rotulo="DADOS", arquivos={"teste300.wav": wav_teste(300),
                                                                     "longo500.wav": wav_teste(500, 44100, 2.5)})
    with Maquina("h42", "ide", extras=som_qemu(wav) + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        verificar("[SOM] som do sistema 0" in m.serial(), "som de início do sistema tocou")
        time.sleep(1)
        m.comando("montar")
        verificar("[SOM] som do sistema 1" in m.serial(), "som de aviso tocou com a notificação do pendrive")
        out = m.comando("tocar /usb/teste300.wav", tempo=30)
        verificar("22050 Hz, 16 bits, mono" in out, "tocar lê o cabeçalho do WAV")
        time.sleep(0.5)
        # Mais de 65536 amostras (2,5 s a 44,1 kHz): a posição em 32 bits dava a
        # volta e a música nunca terminava (G470).
        out = m.comando("tocar /usb/longo500.wav", tempo=30)
        verificar("44100 Hz" in out, "tocar termina uma música de 2,5 s a 44,1 kHz (mais de 65536 amostras)")
        out = m.comando("tocar /sons/sagui.wav", tempo=30)
        verificar("Tocando /sons/sagui.wav" in out, "o canto do sagui toca")
        m.comando("bip 1000 600")
        time.sleep(1)
        verificar("[SOM] nota 1000 Hz 600 ms" in m.serial(), "o bip vai para a placa de som")
        m.captura("H42-tocar")
    amostras, taxa = ler_wav(wav)
    jan = taxa // 10
    janelas = [amostras[k:k + jan] for k in range(0, len(amostras) - jan, jan)]
    com_300 = sum(1 for t in janelas if energia(t, taxa, 300) > 800)
    com_1000 = sum(1 for t in janelas if energia(t, taxa, 1000) > 500)
    verificar(com_300 >= 10, f"o WAV de 300 Hz foi tocado e convertido para 48 kHz ({com_300} pedaços de 0,1 s)")
    verificar(com_1000 >= 4, f"o bip de 1000 Hz saiu pela placa ({com_1000} pedaços)")
    com_500 = sum(1 for t in janelas if energia(t, taxa, 500) > 800)
    verificar(com_500 >= 20, f"o WAV longo de 500 Hz tocou inteiro ({com_500} pedaços de 0,1 s de 25)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
