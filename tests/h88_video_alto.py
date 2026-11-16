#!/usr/bin/env python3
"""H88 - o tocador de vídeo decodifica H.264 perfil High (D048).

Os vídeos de teste do Eli (testes_eli/sample-5s-360p.mp4, -720p e o de
1080p) são H.264 High, nível 4.0, com quadros B e CABAC: usam a transformada
8x8 e a predição intra 8x8, que o decodificador do pull request #6 não tinha.

Critério, em duas partes:

1. No PC de build: userland/video/h264.c compila sem avisos (-DH264_HOST) e
   passa nas conferências das peças novas que não dependem de vídeo nenhum
   (a transformada 8x8 é ortogonal, o normAdjust8x8 compensa o tamanho das
   funções de base, a leitura das matrizes de escala segue as regras A e B, a
   predição intra 8x8 filtra as vizinhas...). Depois decodifica cada vídeo de
   teste que encontrar: saem tantos quadros quantas amostras o MP4 tem, sem
   nenhum erro, toda fatia CABAC termina exatamente no fim dos seus dados
   (um símbolo lido errado desalinharia tudo), o vídeo usa mesmo as peças do
   High (MBs intra 8x8 nos 9 modos, MBs inter com a transformada 8x8), os
   quadros vizinhos são parecidos e as imagens não têm "blocos" (o degrau
   médio nas bordas de bloco é como o do resto da imagem).

   NÃO há decodificador de referência neste PC (nem ffmpeg, nem libavcodec):
   a imagem não foi comparada amostra por amostra com a de outro
   decodificador. O resumo dos quadros de cada vídeo fica anotado em RESUMOS
   para avisar se uma mudança futura alterar a imagem.

2. No SaguiOS (QEMU): "video /usb/v.mp4" com o vídeo de 360p num pendrive
   abre a janela, decodifica todos os quadros sem erro, e a janela mostra um
   quadro do vídeo: a captura H88-1-video é comparada com os quadros
   decodificados no PC.

Os vídeos não estão no repositório (são do Eli). O teste procura em
testes_eli/ (também na pasta principal, quando roda num worktree) ou em
$SAGUI_VIDEO_ALTO; sem o de 360p, só as conferências do item 1 rodam, e o
teste avisa.
"""
import glob
import hashlib
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, SAIDA, RAIZ, criar_pendrive, pendrive_qemu  # noqa: E402

AQUI = os.path.dirname(os.path.abspath(__file__))
VIDEOS = {"sample-5s-360p.mp4": (640, 360), "sample-5s-720p.mp4": (1280, 720), "sample-5s.mp4": (1920, 1080)}

# sha256 do MP4 -> sha256 das linhas "q" (o resumo de cada quadro) que o
# video_pc imprime. Anotado quando o High foi escrito, com as imagens
# conferidas a olho; serve para notar se a imagem mudar. Se um dia a saída for
# comparada com a de um decodificador de referência e diferir, o errado é este
# número.
RESUMOS = {
    # sample-5s-360p.mp4
    "d385179acb9e43d7ff87d0e8fbff2074fb29bc2363b64206128383a17ca534a8": "9be3f92695f6fea25ee76cfff1d85525c8d6081c231b7b3cc12b49a67efb05a5",
    # sample-5s-720p.mp4
    "0da0cfa7ce3112631593fd38fdc6fff5ac1b27f89604370181c027ee1bf809ae": "cf43d2c424a2540b83277039e3e4511eb4776a7e00b09d79115959729011e7ec",
    # sample-5s.mp4 (1080p)
    "05bd857af7f70bf51b6aac1144046973bf3325c9101a554bc27dc9607dbbd8f5": "ca852483c7a3d5f199b8ccf04a33fa792da62812c415ebe7d570549c184e2e65",
}


def achar_video(nome):
    pastas = [os.environ.get("SAGUI_VIDEO_ALTO") or "", os.path.join(RAIZ, "testes_eli")]
    if "/.claude/worktrees/" in RAIZ:    # num worktree, os arquivos do Eli estão na pasta principal
        pastas.append(os.path.join(RAIZ.split("/.claude/worktrees/")[0], "testes_eli"))
    for p in pastas:
        if p and os.path.exists(os.path.join(p, nome)):
            return os.path.join(p, nome)
    return None


def compilar():
    exe = os.path.join(SAIDA, "video_pc")
    r = subprocess.run(["gcc", "-DH264_HOST", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I", os.path.join(RAIZ, "userland", "video"), "-o", exe,
                        os.path.join(AQUI, "video_pc", "principal.c"),
                        os.path.join(RAIZ, "userland", "video", "mp4.c"), "-lm"], capture_output=True, text=True)
    verificar(r.returncode == 0, "h264.c e mp4.c compilam no PC de build sem avisos" + ("" if r.returncode == 0 else ":\n" + r.stderr[:2000]))
    return exe


def luma(yuv, larg, alt):
    from PIL import Image
    with open(yuv, "rb") as f:
        return Image.frombytes("L", (larg, alt), f.read(larg * alt))


def blocagem(im):
    """Degrau médio entre colunas vizinhas nas bordas de bloco 8x8 dividido
    pelo degrau médio nas outras colunas. Numa imagem bem decodificada (e
    passada pelo filtro de desbloqueio) fica perto de 1; resíduo ou predição
    errados deixam os blocos à mostra e isso sobe."""
    larg, alt = im.size
    d = im.tobytes()
    borda = dentro = nb = nd = 0
    for y in range(0, alt, 4):
        linha = d[y * larg:(y + 1) * larg]
        for x in range(1, larg):
            v = abs(linha[x] - linha[x - 1])
            if x % 8 == 0:
                borda += v
                nb += 1
            else:
                dentro += v
                nd += 1
    return (borda / nb) / (dentro / nd)


def decodificar_no_pc(exe, mp4, nome, larg, alt, guardar, olhar=(0, 60, 120, 170)):
    """Roda o video_pc num MP4 e confere a saída; "guardar" são os quadros a
    gravar em build/testes e "olhar", os que têm a imagem examinada. Devolve
    o número de quadros."""
    prefixo = os.path.join(SAIDA, "h88-" + nome)
    r = subprocess.run([exe, mp4, prefixo] + [str(i) for i in guardar], capture_output=True, text=True, timeout=300)
    verificar(r.returncode == 0, f"{nome}: o decodificador aceita o vídeo" + ("" if r.returncode == 0 else f" ({r.stdout.strip()[-200:]})"))
    linhas = r.stdout.splitlines()
    fim = [int(v) for v in linhas[-2].split()[1:]]
    conta = [int(v) for v in linhas[-1].split()[1:]]
    quadros = [ln.split() for ln in linhas if ln.startswith("q ")]
    verificar(fim[0] == larg and fim[1] == alt, f"{nome}: {larg}x{alt}")
    verificar(fim[2] == fim[3] == len(quadros) and fim[2] > 100,
              f"{nome}: saíram {fim[3]} quadros, um por amostra do MP4 ({fim[2]})")
    verificar(fim[4] == 0, f"{nome}: nenhuma fatia com erro")
    verificar(conta[3] > 0 and conta[16] == 0, f"{nome}: as {conta[3]} fatias CABAC ({conta[6]} delas B) terminam exatamente no fim dos dados")
    verificar(conta[1] > 0 and conta[2] > 0 and min(conta[7:16]) > 0,
              f"{nome}: é High de verdade ({conta[1]} MBs intra 8x8, com os 9 modos; {conta[2]} MBs inter com a transformada 8x8)")
    difs = [int(q[3]) / 100 for q in quadros[1:]]
    verificar(max(difs) < 40 and sorted(difs)[len(difs) // 2] < 20,
              f"{nome}: quadros vizinhos são parecidos (diferença média de luma: mediana {sorted(difs)[len(difs) // 2]:.1f}, máxima {max(difs):.1f})")
    resumo = hashlib.sha256("\n".join(q[2] for q in quadros).encode()).hexdigest()
    arquivo = hashlib.sha256(open(mp4, "rb").read()).hexdigest()
    if arquivo in RESUMOS:
        verificar(RESUMOS[arquivo] == resumo, f"{nome}: a imagem de todos os quadros é a anotada no teste")
    else:
        print(f"  AVISO: {nome}: vídeo sem resumo anotado ({arquivo}: {resumo})")
    piores = []
    for i in olhar:
        yuv = f"{prefixo}-{i}.yuv"
        im = luma(yuv, larg, alt)
        ext = im.getextrema()
        verificar(ext[1] - ext[0] > 100, f"{nome}: o quadro {i} tem claros e escuros ({ext[0]} a {ext[1]})")
        piores.append(blocagem(im))
    if piores:
        verificar(max(piores) < 1.5, f"{nome}: sem blocos à mostra nos quadros {list(olhar)} (degrau nas bordas 8x8 / fora delas: até {max(piores):.2f})")
    print(f"  {nome}: {fim[3]} quadros em {fim[5]} ms no PC ({fim[3] * 1000 / max(fim[5], 1):.0f} quadros/s)")
    return fim[3]


def parte_pc():
    exe = compilar()
    r = subprocess.run([exe, "--conferir"], capture_output=True, text=True, timeout=120)
    oks = [ln for ln in r.stdout.splitlines() if ln.startswith("ok ")]
    erros = [ln for ln in r.stdout.splitlines() if not ln.startswith("ok ")]
    for ln in oks:
        print("  ok:", ln[3:])
    verificar(r.returncode == 0 and not erros and len(oks) >= 14, "as peças do High passam nas conferências" + ("" if not erros else ": " + "; ".join(erros)))
    quadros = {}
    for nome, (larg, alt) in VIDEOS.items():
        mp4 = achar_video(nome)
        if not mp4:
            print(f"  AVISO: {nome} não está em testes_eli/: não foi decodificado")
            continue
        # do de 360p ficam todos os quadros, para comparar com a captura da parte 2
        quadros[nome] = decodificar_no_pc(exe, mp4, nome[:-4], larg, alt, range(400) if nome == "sample-5s-360p.mp4" else (0, 60, 120, 170))
    return quadros


def quadro_mais_parecido(png, area, prefixo, n, larg, alt):
    """Compara a área do vídeo na captura com os n quadros decodificados no
    PC (luma reduzida a 160x90). Devolve (diferença média, índice)."""
    from PIL import Image, ImageChops
    cap = Image.open(png).convert("L").crop(area).resize((160, 90), Image.BOX)
    melhor = None
    for i in range(n):
        # a luma do vídeo vai de 16 a 235; a tela, de 0 a 255
        ref = luma(f"{prefixo}-{i}.yuv", larg, alt).resize((160, 90), Image.BOX).point(lambda p: max(0, min(255, (p - 16) * 255 // 219)))
        h = ImageChops.difference(cap, ref).histogram()
        d = sum(k * v for k, v in enumerate(h)) / (160 * 90)
        if melhor is None or d < melhor[0]:
            melhor = (d, i)
    return melhor


def main():
    os.makedirs(SAIDA, exist_ok=True)
    quadros = parte_pc()
    nome = "sample-5s-360p.mp4"
    mp4 = achar_video(nome)
    if not mp4:
        print("  AVISO: sem o vídeo de 360p, a decodificação do High NÃO foi testada (nem no PC, nem no SaguiOS)")
        return 0
    larg, alt = VIDEOS[nome]
    prefixo = os.path.join(SAIDA, "h88-" + nome[:-4])
    pen = criar_pendrive("h88-pen", mb=64, rotulo="DADOS", arquivos={"v.mp4": open(mp4, "rb").read()})
    with Maquina("h88", "ide", memoria="768M", extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("montar")
        m.digitar("video /usb/v.mp4\n", 0.04)
        m.esperar_serial(f"video: {larg}x{alt}", 120)
        m.esperar_serial('"v.mp4"', 60)       # a janela só abre se o H.264 foi aceito
        verificar("H.264 recusado" not in m.serial(), "o /bin/video aceita o vídeo High (antes: 'H.264 recusado (-2)')")
        time.sleep(4)
        png = m.captura("H88-1-video")
        j = janela(m.serial(), "v.mp4")
        m.esperar_serial("video: H.264:", 500)
        s = m.serial()
    f = re.search(r"video: fim; (\d+) quadros mostrados, (\d+) pulados, (\d+) ms.*decodificando (\d+) ms", s)
    d = re.search(r"video: H\.264: (\d+) de (\d+) quadros decodificados, (\d+) erros", s)
    verificar(f and d, "o tocador chegou ao fim")
    mostrados, pulados, total_ms, decod_ms = (int(v) for v in f.groups())
    verificar(int(d.group(1)) == int(d.group(2)) == quadros[nome],
              f"no SaguiOS saíram os {d.group(1)} quadros do vídeo ({mostrados} mostrados, {pulados} pulados por atraso)")
    verificar(int(d.group(3)) == 0, "nenhuma fatia com erro no SaguiOS")
    dif, qual = quadro_mais_parecido(png, (j["x"], j["y"], j["x"] + larg, j["y"] + alt), prefixo, quadros[nome], larg, alt)
    verificar(dif < 6, f"a janela mostra o vídeo: a captura H88-1-video é o quadro {qual} decodificado no PC (diferença média {dif:.1f} em 255)")
    print(f"  no QEMU (emulado, sem KVM): {quadros[nome]} quadros em {decod_ms} ms de decodificação "
          f"({quadros[nome] * 1000 / max(decod_ms, 1):.1f} quadros/s; o vídeo tem 30 por segundo)")
    for arq in glob.glob(prefixo + "-*.yuv"):
        os.remove(arq)
    return 0


if __name__ == "__main__":
    sys.exit(main())
