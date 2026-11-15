#!/usr/bin/env python3
"""H86 - decodificador de MP3 escrito do zero (userland/tocador/mp3.c).

Critério, em duas partes:

1. No PC de build: as tabelas da norma em mp3_tabelas.h são coerentes; mp3.c
   compila sem avisos; e o som que ele decodifica é igual (a menos de 1 ou 2
   unidades de arredondamento em 16 bits) ao de um decodificador de
   referência (libmpg123), em MP3 feitos na hora com a libmp3lame: mono e
   estéreo, estéreo conjunto, 32/44,1/48 kHz, as taxas baixas do MPEG-2, taxa
   de bits variável com quadro Xing, CRC, etiquetas ID3 e, montados à mão,
   quadros com estéreo de intensidade (que a libmp3lame não faz). O pulo
   (mp3_ir_para_ms) também é comparado. Sem as duas bibliotecas, esta parte
   avisa que não conferiu a decodificação.

2. No SaguiOS (QEMU): "mp3info" recusa um arquivo que não existe e um que
   não é MP3; mostra a taxa, os canais e a duração de um MP3 do pendrive;
   decodifica o mesmo número de quadros que no PC; e "mp3info -t" toca um
   MP3 de 440 Hz, que aparece com 440 Hz no .wav gravado pelo QEMU.

O MP3 de 440 Hz fica guardado em tests/recursos/h86_seno440.mp3 (um seno
feito por este teste e codificado com a libmp3lame), para a parte 2 valer
também num PC sem codificador.
"""
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, SAIDA, criar_pendrive, pendrive_qemu  # noqa: E402
from h40_som import som_qemu, ler_wav  # noqa: E402
from h41_mistura import energia  # noqa: E402
import mp3_ref as R  # noqa: E402

RECURSO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recursos", "h86_seno440.mp3")


def decodificar_no_pc(exe, mp3, nome, pular=None):
    """Roda o mp3.c compilado no PC. Devolve (taxa, canais, duração em ms,
    quadros, amostras intercaladas) ou None se ele disse que não é MP3."""
    arq = os.path.join(SAIDA, "h86-" + nome + ".mp3")
    with open(arq, "wb") as f:
        f.write(mp3)
    r = subprocess.run([exe, arq, arq + ".pcm"] + ([str(pular)] if pular is not None else []),
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return None
    taxa, canais, dur, quadros = (int(v) for v in r.stdout.split())
    bruto = open(arq + ".pcm", "rb").read()
    return taxa, canais, dur, quadros, list(struct.unpack("<%dh" % (len(bruto) // 2), bruto))


def casos_de_teste():
    """Nome -> (descrição, bytes do MP3). Só com a libmp3lame."""
    m44, m48, m32 = R.musica(44100, 3.0), R.musica(48000, 3.0), R.musica(32000, 3.0)
    m22, m16 = R.musica(22050, 3.0), R.musica(16000, 3.0)
    ruido = [((i * 2654435761) >> 7) % 60000 - 30000 for i in range(44100 * 2)]
    mono_1 = R.codificar(m44[::2], 44100, 1, kbps=64, reserva=False, xing=False)
    mono_2 = R.codificar(m22[::2], 22050, 1, kbps=32, reserva=False, xing=False)
    return {
        "seno": ("seno de 440 Hz, mono, 44,1 kHz, 128 kbps", R.codificar(R.seno(440, 44100, 2.0), 44100, 1, kbps=128)),
        "conjunto": ("estéreo conjunto, 44,1 kHz, 128 kbps, com ID3v2 e ID3v1",
                     R.id3v2() + R.codificar(m44, 44100, 2, kbps=128, modo=1) + R.id3v1()),
        "48k": ("estéreo simples, 48 kHz, 192 kbps", R.codificar(m48, 48000, 2, kbps=192, modo=0)),
        "32k": ("estéreo conjunto, 32 kHz, 96 kbps", R.codificar(m32, 32000, 2, kbps=96, modo=1)),
        "vbr": ("taxa de bits variável (Xing), 44,1 kHz", R.codificar(m44, 44100, 2, vbr=4)),
        "320": ("ruído forte, 320 kbps (valores grandes, linbits)", R.codificar(ruido, 44100, 1, kbps=320)),
        "64": ("estéreo conjunto, 44,1 kHz, 64 kbps (muita reserva de bits)", R.codificar(m44, 44100, 2, kbps=64, modo=1)),
        "crc": ("quadros com CRC", R.codificar(m44, 44100, 2, kbps=128, crc=True)),
        "22k": ("MPEG-2, 22,05 kHz, estéreo conjunto, 64 kbps", R.codificar(m22, 22050, 2, kbps=64, modo=1)),
        "16k": ("MPEG-2, 16 kHz, mono, 32 kbps", R.codificar(m16[::2], 16000, 1, kbps=32)),
        "24k": ("MPEG-2, 24 kHz, estéreo, 64 kbps", R.codificar(R.musica(24000, 3.0), 24000, 2, kbps=64, modo=1)),
        "int1": ("estéreo de intensidade, MPEG-1 (montado à mão)", R.montar_intensidade(mono_1)),
        "int1ms": ("intensidade + meio/lado, MPEG-1 (montado à mão)", R.montar_intensidade(mono_1, com_ms=True)),
        "int2": ("estéreo de intensidade, MPEG-2 (montado à mão)", R.montar_intensidade(mono_2)),
        "int2ms": ("intensidade (escala 1) + meio/lado, MPEG-2 (montado à mão)",
                   R.montar_intensidade(mono_2, com_ms=True, escala=1)),
    }


def parte_pc():
    """Devolve (executável, casos ou None)."""
    problemas = R.conferir_huffman()
    verificar(not problemas, "as tabelas de Huffman são códigos de prefixo completos e a janela de síntese é suave"
              + ("" if not problemas else ": " + "; ".join(problemas)))
    exe = R.compilar_no_pc(SAIDA)
    verificar(exe is not None, "mp3.c compila no PC de build sem avisos")
    lixo = bytes((i * 7919 + 13) & 0xFF for i in range(20000))
    verificar(decodificar_no_pc(exe, lixo, "lixo") is None, "um arquivo que não é MP3 é recusado")

    if R.lame() is None or R.mpg123() is None:
        print("  AVISO: sem libmp3lame e libmpg123 neste PC; a decodificação NÃO foi comparada com uma referência")
        return exe, None
    casos = casos_de_teste()
    for nome, (descricao, mp3) in casos.items():
        meu = decodificar_no_pc(exe, mp3, nome)
        verificar(meu is not None, f"{nome}: o arquivo abre")
        taxa, canais, pcm = R.decodificar_referencia(mp3)
        verificar(meu[0] == taxa and meu[1] == canais, f"{nome}: {taxa} Hz, {canais} canal(is)")
        amostras = meu[4] if canais == 2 else meu[4][::2]
        verificar(len(amostras) == len(pcm) and len(pcm) > 20000,
                  f"{nome}: mesmo número de amostras da referência ({len(pcm) // canais} quadros)")
        verificar(abs(meu[2] - meu[3] * 1000 // taxa) <= 60, f"{nome}: duração informada {meu[2]} ms")
        maior, media, snr = R.comparar(amostras, pcm)
        verificar(maior <= 2 and snr >= 80,
                  f"{descricao}: igual à referência (diferença máxima {maior}, média {media:.4f}, SNR {snr:.1f} dB)")
    # O pulo: o som depois de mp3_ir_para_ms tem de ser o da referência a partir
    # do começo do quadro em que o instante cai.
    for nome, ms in (("vbr", 1700), ("64", 2200), ("22k", 1500)):
        mp3 = casos[nome][1]
        meu = decodificar_no_pc(exe, mp3, nome + "-pulo", pular=ms)
        taxa, canais, pcm = R.decodificar_referencia(mp3)
        por_quadro = 1152 if taxa >= 32000 else 576
        inicio = (ms * taxa // 1000) // por_quadro * por_quadro * canais
        maior, media, snr = R.comparar(meu[4], pcm[inicio:])
        verificar(len(meu[4]) == len(pcm) - inicio and maior <= 2,
                  f"pulo para {ms} ms em '{nome}': igual à referência dali em diante (diferença máxima {maior}, SNR {snr:.1f} dB)")
    return exe, casos


def main():
    exe, casos = parte_pc()
    if not os.path.exists(RECURSO) and casos:
        with open(RECURSO, "wb") as f:
            f.write(R.id3v2(300) + R.codificar(R.seno(440, 44100, 2.0), 44100, 1, kbps=64))
    arquivos = {"lixo.mp3": bytes((i * 7919 + 13) & 0xFF for i in range(20000))}
    seno = musica = None
    if os.path.exists(RECURSO):
        arquivos["teste.mp3"] = open(RECURSO, "rb").read()
        seno = decodificar_no_pc(exe, arquivos["teste.mp3"], "recurso")
    if casos:
        arquivos["musica.mp3"] = casos["conjunto"][1]
        musica = decodificar_no_pc(exe, arquivos["musica.mp3"], "musica")

    wav = os.path.join(SAIDA, "h86.wav")
    pen = criar_pendrive("h86-pen", rotulo="DADOS", arquivos=arquivos)
    with Maquina("h86", "ide", extras=som_qemu(wav) + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")
        out = m.comando("mp3info /usb/falta.mp3")
        verificar("mp3info: /usb/falta.mp3:" in out, "mp3info avisa que o arquivo não existe")
        out = m.comando("mp3info /usb/lixo.mp3")
        verificar("não é um arquivo MP3" in out, "mp3info recusa um arquivo que não é MP3")
        if musica:
            out = m.comando("mp3info /usb/musica.mp3", tempo=180)
            verificar(f"{musica[0]} Hz, estéreo, {musica[2] // 1000}.{musica[2] % 1000 // 100} s" in out,
                      "mp3info mostra a taxa, os canais e a duração do MP3 estéreo (com ID3v2)")
            verificar(f"Decodificados {musica[3]} quadros" in out,
                      f"o SaguiOS decodifica os mesmos {musica[3]} quadros que o PC")
            print("  ", [l for l in out.split("\n") if "Decodificados" in l][0].strip())
        if seno:
            out = m.comando("mp3info -t /usb/teste.mp3", tempo=180)
            verificar(f"{seno[0]} Hz, mono, {seno[2] // 1000}.{seno[2] % 1000 // 100} s" in out,
                      "mp3info -t mostra os dados do MP3 de 440 Hz")
            verificar(f"Tocados {seno[3]} quadros" in out, f"o MP3 de 440 Hz foi tocado inteiro ({seno[3]} quadros)")
            time.sleep(0.5)
        m.captura("H86-mp3info")
    if not seno:
        print("  AVISO: sem MP3 de teste; no SaguiOS só os casos de erro do mp3info foram conferidos")
        return 0
    amostras, taxa = ler_wav(wav)
    jan = taxa // 10
    com_440 = 0
    for k in range(0, len(amostras) - jan, jan):
        trecho = amostras[k:k + jan]
        e = energia(trecho, taxa, 440)
        if e > 2000 and energia(trecho, taxa, 480) < e / 5 and energia(trecho, taxa, 400) < e / 5:
            com_440 += 1
    verificar(com_440 >= 15, f"o tom de 440 Hz do MP3 saiu pela placa de som ({com_440} pedaços de 0,1 s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
