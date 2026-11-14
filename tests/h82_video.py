#!/usr/bin/env python3
"""H82 - o tocador de vídeo (/bin/video), do pull request #6.

Critério (adaptado do teste H46 do #6, de Robson de Lima Barbosa): o tocador
baixa um MP4 (H.264 Baseline + AAC-LC) por HTTP, mostra os quadros e toca o
som pela placa HD Audio da 2.0 (48 kHz), e a frequência no .wav gravado pelo
QEMU é o tom de 440 Hz do arquivo. O MP4 é gerado pelo ffmpeg da máquina dos
testes; SEM ffmpeg, só se confere que o programa existe e recusa um arquivo
que não existe (a decodificação NÃO é testada nesse caso, e o teste diz isso).
"""
import functools
import http.server
import os
import re
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA  # noqa: E402
from h40_som import ler_wav  # noqa: E402

SEGUNDOS = 6
REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]


def main():
    os.makedirs(SAIDA, exist_ok=True)
    tem_ffmpeg = shutil.which("ffmpeg") is not None
    pasta = os.path.join(SAIDA, "h82v-www")
    os.makedirs(pasta, exist_ok=True)
    if tem_ffmpeg:
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y",
                        "-f", "lavfi", "-i", f"testsrc2=size=320x240:rate=25:d={SEGUNDOS}",
                        "-f", "lavfi", "-i", f"sine=f=440:d={SEGUNDOS}",
                        "-c:v", "libx264", "-profile:v", "baseline", "-pix_fmt", "yuv420p",
                        "-c:a", "aac", "-ar", "44100", "-ac", "2", "-movflags", "+faststart",
                        os.path.join(pasta, "teste.mp4")], check=True)
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    porta = srv.server_address[1]
    wav = os.path.join(SAIDA, "h82-video.wav")
    som = ["-audiodev", f"wav,id=som,path={wav}", "-device", "intel-hda", "-device", "hda-duplex,audiodev=som"]
    try:
        with Maquina("h82v", "ide", extras=REDE + som, memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.digitar("video /nao/existe.mp4\n", 0.04)
            m.esperar_serial("video: nao abriu /nao/existe.mp4", 30)
            verificar(True, "o programa /bin/video existe, roda e recusa um arquivo que não existe")
            if not tem_ffmpeg:
                print("  AVISO: sem ffmpeg nesta máquina: a decodificação de H.264/AAC e o som NÃO foram testados")
                return 0
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            m.digitar(f"video http://10.0.2.2:{porta}/teste.mp4\n", 0.04)
            m.esperar_serial("video: 320x240", 120)
            time.sleep(SEGUNDOS / 2)
            m.captura("H82-video")
            m.esperar_serial("video: fim", 120)
            s = m.serial()
    finally:
        srv.shutdown()
    f = re.search(r"video: fim; (\d+) quadros mostrados, (\d+) pulados, (\d+) ms", s)
    verificar(f, "o tocador chegou ao fim")
    mostrados, pulados = int(f.group(1)), int(f.group(2))
    total = SEGUNDOS * 25
    verificar(mostrados + pulados == total, f"decodificou todos os quadros ({mostrados} mostrados, {pulados} pulados)")
    verificar(mostrados >= total // 2, "mostrou pelo menos metade dos quadros")
    # O .wav começa no boot e inclui o som de início do sistema (523, 659,
    # 784 e 1047 Hz). Medir do primeiro ao último trecho com som misturava
    # aquelas notas com o tom do vídeo e dava 302 Hz. Cada janela de 0,1 s é
    # medida sozinha; só as que têm som e cerca de 440 Hz contam.
    amostras, taxa = ler_wav(wav)
    jan = taxa // 10
    com_som = com_440 = 0
    for k in range(0, len(amostras) - jan, jan):
        trecho = amostras[k:k + jan]
        if max(abs(a) for a in trecho) < 500:
            continue
        com_som += 1
        # Cruzamentos de zero subindo: um por ciclo da onda.
        cruz = sum(1 for a, b in zip(trecho, trecho[1:]) if a < 0 <= b)
        if 400 < cruz * taxa / jan < 480:
            com_440 += 1
    minimo = SEGUNDOS * 8   # 80% do vídeo, em janelas de 0,1 s
    verificar(com_som >= minimo, f"tocou quase todo o áudio ({com_som / 10:.1f} s)")
    verificar(com_440 >= minimo,
              f"o som é o tom de 440 Hz do arquivo ({com_440} de {com_som} janelas de 0,1 s com som, convertido para 48 kHz)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
