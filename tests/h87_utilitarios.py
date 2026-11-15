#!/usr/bin/env python3
"""H87: programas úteis novos.

Confere, numa máquina com pendrive (FAT32) e placa de som:
- comandos de texto: head, tail, sort, uniq, find, du e hexdump, comparados
  com os arquivos do rootfs lidos aqui no host;
- tar e zip: pacotes criados no SaguiOS, gravados no pendrive e abertos aqui
  com o tarfile e o zipfile do Python (o conteúdo tem que ser igual ao do
  rootfs); o unzip do SaguiOS extrai um .zip feito aqui e recusa um com o
  CRC estragado;
- less: abre, procura e sai;
- captura: a tecla Print Screen salva um PNG em /usb/Capturas, que é aberto
  aqui com o PIL e comparado com a tela do QEMU;
- visualizador: PNG, JPEG, WebP e BMP feitos aqui, com as cores conferidas na
  tela; o zoom e a passagem para a próxima imagem da pasta;
- música: um WAV de 440 Hz e um M4A (AAC) de 330 Hz tocam em seguida, e o
  .wav gravado pelo QEMU tem os dois tons;
- lixeira do Arquivos: apagar move para /.lixeira, restaurar devolve, e
  esvaziar apaga de vez.
"""
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import (Maquina, verificar, criar_pendrive, pendrive_qemu, mtools,  # noqa: E402
                  SAIDA, RAIZ)
from h40_som import ler_wav  # noqa: E402
from PIL import Image  # noqa: E402

ROOTFS = os.path.join(RAIZ, "rootfs")


def janela(serial, titulo):
    m = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)[-1]
    return dict(l=int(m[0]), a=int(m[1]), x=int(m[2]) + 2, y=int(m[3]) + 24, fx=int(m[2]), fy=int(m[3]))


def imagem_teste():
    """300x200: metade esquerda vermelha, metade direita azul."""
    im = Image.new("RGB", (300, 200), (220, 30, 30))
    im.paste((30, 60, 220), (150, 0, 300, 200))
    return im


def bytes_de(im, formato, **kw):
    b = io.BytesIO()
    im.save(b, formato, **kw)
    return b.getvalue()


def tom_wav(freq, segundos, taxa=22050):
    import math
    import struct
    n = int(taxa * segundos)
    dados = b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * freq * i / taxa))) for i in range(n))
    cab = b"RIFF" + struct.pack("<I", 36 + len(dados)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, taxa, taxa * 2, 2, 16)
    return cab + b"data" + struct.pack("<I", len(dados)) + dados


def janelas_com(amostras, taxa, freq):
    """Quantas janelas de 0,1 s com som estão perto de "freq" (cruzamentos de zero)."""
    jan = taxa // 10
    n = 0
    for k in range(0, len(amostras) - jan, jan):
        t = amostras[k:k + jan]
        if max(abs(a) for a in t) < 500:
            continue
        cruz = sum(1 for a, b in zip(t, t[1:]) if a < 0 <= b)
        if abs(cruz * 10 - freq) < 25:
            n += 1
    return n


def limpo(out, comando=None):
    """A saída de um comando sem as linhas do kernel ([SAGUI], [GUI]...), sem o
    prompt e sem o eco do próprio comando (que nem sempre vem primeiro)."""
    return [l for l in out.splitlines() if l.strip() and not l.lstrip().startswith("[") and "sagui:" not in l
            and not (comando and comando in l)]


def contagem(out):
    r = re.search(r"(\d+) linhas, (\d+) palavras, (\d+) letras", out)
    return r.groups() if r else None


def cor_perto(a, b, tol=40):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def main():
    im = imagem_teste()
    arquivos = {
        "fotos/foto.png": bytes_de(im, "PNG"),
        "fotos/foto.jpg": bytes_de(im, "JPEG", quality=92),
        "fotos/foto.webp": bytes_de(im, "WEBP", quality=90),
        "fotos/foto.bmp": bytes_de(im, "BMP"),
        "musica/a-tom.wav": tom_wav(440, 2),
    }
    tem_ffmpeg = shutil.which("ffmpeg") is not None
    if tem_ffmpeg:
        m4a = os.path.join(SAIDA, "h87-tom.m4a")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "sine=f=330:d=2",
                        "-c:a", "aac", "-ar", "44100", "-ac", "2", m4a], check=True)
        arquivos["musica/b-tom.m4a"] = open(m4a, "rb").read()
    # um .zip feito aqui (deflate e guardado) e outro com o CRC estragado
    z = io.BytesIO()
    with zipfile.ZipFile(z, "w") as zf:
        zf.write(os.path.join(ROOTFS, "historia", "linux.txt"), "doc/linux.txt", compress_type=zipfile.ZIP_DEFLATED)
        zf.writestr("doc/curto.txt", "sagui\n", compress_type=zipfile.ZIP_STORED)
    arquivos["host.zip"] = z.getvalue()
    ruim = bytearray(z.getvalue())
    k = ruim.index(b"sagui\n")
    ruim[k] = ord("S")   # o CRC do diretório central não confere mais
    arquivos["ruim.zip"] = bytes(ruim)
    pen = criar_pendrive("h87-pen", 64, "H87", arquivos=arquivos, pastas=("fotos", "musica"))

    wav = os.path.join(SAIDA, "h87-som.wav")
    som = ["-audiodev", f"wav,id=som,path={wav}", "-device", "intel-hda", "-device", "hda-duplex,audiodev=som"]
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen) + som
    linux = open(os.path.join(ROOTFS, "historia", "linux.txt"), "rb").read()
    # dentro do SaguiOS os .txt do rootfs ficam em Latin-1 (tools/mksgfs.py)
    linux_sagui = linux.decode("utf-8").encode("latin-1")
    sagui_txt = open(os.path.join(ROOTFS, "arte", "sagui.txt"), "rb").read().decode("utf-8").encode("latin-1")

    with Maquina("h87", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        terminal = janela(m.serial(), "Terminal")

        def focar_terminal():
            m.clique(terminal["fx"] + 60, terminal["fy"] + 10)
            time.sleep(0.4)

        out = m.comando("montar usb0")
        verificar("montado" in out, "pendrive montado em /usb")

        # ---------------------------------------------------- texto
        out = m.comando("head -n 2 /historia/linux.txt")
        primeira = linux.split(b"\n")[0].decode("utf-8")[:20]
        verificar(primeira[:12] in out, f"head mostra o começo do arquivo ({primeira[:12]!r})")
        out = m.comando("tail -n 1 /historia/linux.txt")
        ultima = [l for l in linux.split(b"\n") if l.strip()][-1].decode("utf-8")
        trecho = re.sub(r"[^A-Za-z ]", "", ultima)[:12]
        verificar(trecho in re.sub(r"[^A-Za-z ]", "", out), f"tail mostra a última linha ({trecho!r})")
        out = m.comando("ls /bin | sort -r | head -n 4")
        nomes = [l.split()[0] for l in limpo(out, "ls /bin | sort -r")]
        verificar(len(nomes) == 4 and nomes == sorted(nomes, reverse=True) and all(not n.startswith("[") for n in nomes),
                  f"sort -r e head em pipe ({nomes})")
        m.comando("echo b > /tmp/u.txt")
        m.comando("echo a >> /tmp/u.txt")
        m.comando("echo b >> /tmp/u.txt")
        out = m.comando("sort /tmp/u.txt | uniq -c")
        verificar(re.search(r"\b1 a\b", out) and re.search(r"\b2 b\b", out), "sort | uniq -c conta as repetidas")
        out = m.comando('find / -name "*.txt" -type f')
        verificar("/historia/linux.txt" in out and "/jogos/sagui3d/fase1.txt" in out and "/historia\n" not in out,
                  "find acha os .txt descendo nas pastas, sem as pastas")
        out = m.comando("du -s /fontes")
        soma = sum(os.path.getsize(os.path.join(ROOTFS, "fontes", f)) for f in os.listdir(os.path.join(ROOTFS, "fontes")))
        esperado = (soma + 1023) // 1024
        r = re.search(r"(\d+) KB\s+/fontes", out)
        verificar(r and int(r.group(1)) == esperado, f"du -s /fontes = {r.group(1) if r else '?'} KB (esperado {esperado})")
        out = m.comando("hexdump -n 16 /bin/ola")
        verificar("00000000  7f 45 4c 46 01 01 01" in out and "|.ELF" in out, "hexdump mostra o cabeçalho ELF")

        # ---------------------------------------------------- tar, zip e unzip
        out = m.comando("tar cf /usb/h.tar /historia /arte/sagui.txt")
        out = m.comando("tar tf /usb/h.tar")
        verificar("historia/linux.txt" in out and "arte/sagui.txt" in out, "tar tf lista o pacote")
        m.comando("mkdir /tmp/t")
        m.comando("cd /tmp/t")
        m.comando("tar xf /usb/h.tar")
        a = m.comando("wc /tmp/t/historia/linux.txt")
        b = m.comando("wc /historia/linux.txt")
        verificar(contagem(a) is not None and contagem(a) == contagem(b), f"tar xf extrai igual ao original ({contagem(a)})")
        m.comando("cd /")
        out = m.comando("zip -q /usb/h.zip /historia /arte/sagui.txt")
        verificar(re.search(r"/usb/h\.zip: \d+ itens", out), "zip cria o pacote")
        out = m.comando("unzip -l /usb/h.zip")
        verificar("historia/linux.txt" in out, "unzip -l lista o pacote do zip")
        out = m.comando("unzip /usb/h.zip -d /tmp/z")
        a = m.comando("wc /tmp/z/historia/linux.txt")
        verificar(contagem(a) is not None and contagem(a) == contagem(b), f"unzip extrai o que o zip comprimiu ({contagem(a)})")
        out = m.comando("unzip /usb/host.zip -d /tmp/hz")
        verificar("2 arquivos extraídos" in out or "2 arquivos extra" in out, "unzip extrai um .zip feito pelo Python (deflate e guardado)")
        out = m.comando("cat /tmp/hz/doc/curto.txt")
        verificar("sagui" in out, "o arquivo guardado (método 0) sai igual")
        out = m.comando("unzip /usb/ruim.zip -d /tmp/ruim")
        verificar("CRC não confere" in out or "CRC n" in out, "unzip recusa um arquivo com o CRC errado")

        # ---------------------------------------------------- less
        antes = len(m.serial())
        m.digitar("less /historia/linux.txt\n", 0.04)
        m.esperar_serial("less: /historia/linux.txt", 20)
        time.sleep(1)
        m.captura("H87-less")
        m.digitar("/Linux\n", 0.06)
        m.esperar_serial('less: procurou "Linux"', 20)
        m.tecla("spc")
        m.tecla("q")
        m.esperar_serial("less: saiu", 20)
        fim = time.time() + 20
        while m.serial()[antes:].count("sagui:/$") < 1 and time.time() < fim:
            time.sleep(0.2)
        verificar(m.serial()[antes:].count("sagui:/$") >= 1, "less abre, procura e sai de volta ao prompt")

        # ---------------------------------------------------- captura (Print Screen)
        time.sleep(1)
        tela = Image.open(m.captura("H87-print-screen", guardar=False)).convert("RGB")   # a tela antes da tecla
        antes = len(m.serial())
        m.tecla("print")
        m.esperar_serial("captura: salva /usb/Capturas/", 60)
        r = re.search(r"captura: salva (\S+) \((\d+)x(\d+), (\d+) bytes\)", m.serial()[antes:])
        verificar(r, "Print Screen salva a tela no pendrive")
        nome_cap = r.group(1)
        time.sleep(2.5)   # o aviso da captura fecha sozinho
        focar_terminal()

        # ---------------------------------------------------- visualizador
        antes = len(m.serial())
        m.digitar("visualizador /usb/fotos/foto.png\n", 0.04)
        m.esperar_serial("visualizador: abriu /usb/fotos/foto.png (300x200", 60)
        time.sleep(1)
        v = janela(m.serial(), "Visualizador")
        area_a = v["a"] - 34 - 24
        ox, oy = v["x"] + (v["l"] - 300) // 2, v["y"] + 34 + (area_a - 200) // 2

        def conferir(nome, tol):
            cap = Image.open(m.captura(f"H87-visualizador-{nome}", guardar=nome == "png")).convert("RGB")
            esq, dir_ = cap.getpixel((ox + 75, oy + 100)), cap.getpixel((ox + 225, oy + 100))
            verificar(cor_perto(esq, (220, 30, 30), tol) and cor_perto(dir_, (30, 60, 220), tol),
                      f"{nome}: cores no lugar ({esq}, {dir_})")

        conferir("png", 4)
        ordem = ["webp", "bmp", "jpg"]   # depois de foto.png, em ordem alfabética, dando a volta
        for ext in ordem:
            n0 = len(m.serial())
            m.tecla("right")
            fim = time.time() + 30
            while f"abriu /usb/fotos/foto.{ext} (300x200" not in m.serial()[n0:] and time.time() < fim:
                time.sleep(0.2)
            verificar(f"abriu /usb/fotos/foto.{ext} (300x200" in m.serial()[n0:], f"seta para a direita abre foto.{ext}")
            time.sleep(0.5)
            conferir(ext, 6 if ext == "bmp" else 40)
        m.tecla("shift-equal")
        m.esperar_serial("visualizador: zoom 125%", 10)
        verificar(True, "+ amplia para 125%")
        m.tecla("esc")
        m.esperar_serial("(visualizador) terminou com codigo 0", 20)
        focar_terminal()
        m.digitar(f"visualizador {nome_cap}\n", 0.04)
        m.esperar_serial(f"visualizador: abriu {nome_cap} ({r.group(2)}x{r.group(3)}", 60)
        verificar(True, "o PNG da captura abre no visualizador (decodificador do Cipó)")
        m.tecla("esc")
        m.esperar_serial("(visualizador) terminou com codigo 0", 20)
        focar_terminal()

        # ---------------------------------------------------- música
        if tem_ffmpeg:
            m.digitar("musica /usb/musica/a-tom.wav\n", 0.04)
            m.esperar_serial("musica: tocando /usb/musica/a-tom.wav (22050 Hz, wav, 2 s)", 60)
            m.esperar_serial("musica: tocando /usb/musica/b-tom.m4a (44100 Hz, aac, 2 s)", 30)
            time.sleep(1)
            m.captura("H87-musica")
            m.esperar_serial("musica: fim de b-tom.m4a", 30)
            verificar(True, "a música toca o WAV e passa sozinha para o M4A")
            m.tecla("esc")
            m.esperar_serial("(musica) terminou com codigo 0", 20)
            focar_terminal()
        else:
            print("  AVISO: sem ffmpeg: o M4A da música NÃO foi testado")

        # ---------------------------------------------------- lixeira do Arquivos
        m.comando("echo lixo > /tmp/lx.txt")
        m.comando("rm /tmp/u.txt")
        m.digitar("arquivos /tmp\n", 0.04)
        m.esperar_serial("arquivos: pasta /tmp (", 30)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        # /tmp tem hz/, ruim/, t/, z/ e lx.txt: lx.txt é o 5º (pastas primeiro)
        m.clique(a["x"] + 146 + 120, a["y"] + 70 + 2 + 4 * 20 + 8)
        m.tecla("delete")
        m.esperar_serial('"Apagar"', 10)
        time.sleep(0.5)
        d = janela(m.serial(), "Apagar")
        m.clique(d["x"] + d["l"] - 16 - 45, d["y"] + d["a"] - 40 + 13)
        m.esperar_serial("arquivos: apagado /tmp/lx.txt (para a lixeira: /.lixeira/lx.txt)", 10)
        verificar(True, "Apagar move para a lixeira")
        m.clique(a["x"] + 8 + 60, a["y"] + a["a"] - 24 - 38 + 14)   # botão Lixeira, no pé da lateral
        m.esperar_serial("arquivos: pasta /.lixeira (1 itens)", 10)
        time.sleep(0.5)
        m.captura("H87-lixeira")
        m.clique(a["x"] + 106 + 55, a["y"] + 7 + 13)   # Restaurar
        m.esperar_serial("arquivos: restaurado /.lixeira/lx.txt -> /tmp/lx.txt", 10)
        verificar(True, "Restaurar devolve o arquivo para /tmp")
        m.esperar_serial("arquivos: pasta /.lixeira (0 itens)", 10)
        m.clique(a["x"] + 8 + 45, a["y"] + 7 + 13)    # Voltar: /
        m.esperar_serial("arquivos: pasta / (", 10)
        raiz = int(re.findall(r"arquivos: pasta / \((\d+) itens\)", m.serial())[-1])
        m.clique(a["fx"] + a["l"] + 4 - 2 - 20 + 8, a["fy"] + 12)   # fecha o Arquivos
        time.sleep(1)
        focar_terminal()
        out = m.comando("ls /tmp")
        verificar("lx.txt" in out, "lx.txt de volta em /tmp")
        out = m.comando("ls /")
        itens = limpo(out, "ls /")
        verificar(".lixeira/" in itens and raiz == len(itens) - 1,
                  f"a lixeira existe mas fica escondida no Arquivos ({raiz} itens; o ls mostra {len(itens)})")
        # de novo para a lixeira, e então Esvaziar apaga de vez
        m.digitar("arquivos /tmp\n", 0.04)
        n0 = len(m.serial())
        m.esperar_serial("arquivos: pronto", 30)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        m.clique(a["x"] + 146 + 120, a["y"] + 70 + 2 + 4 * 20 + 8)
        m.tecla("delete")
        fim = time.time() + 10
        while m.serial()[n0:].count('"Apagar"') < 1 and time.time() < fim:
            time.sleep(0.2)
        time.sleep(0.5)
        d = janela(m.serial(), "Apagar")
        m.clique(d["x"] + d["l"] - 16 - 45, d["y"] + d["a"] - 40 + 13)
        fim = time.time() + 10
        while "para a lixeira: /.lixeira/lx.txt" not in m.serial()[n0:] and time.time() < fim:
            time.sleep(0.2)
        m.clique(a["x"] + 8 + 60, a["y"] + a["a"] - 24 - 38 + 14)   # Lixeira
        fim = time.time() + 10
        while "arquivos: pasta /.lixeira (1 itens)" not in m.serial()[n0:] and time.time() < fim:
            time.sleep(0.2)
        m.clique(a["x"] + 408 + 50, a["y"] + 7 + 13)                 # Esvaziar (H88: depois de "Apagar de vez", 150)
        m.esperar_serial('"Esvaziar lixeira"', 10)
        time.sleep(0.5)
        d = janela(m.serial(), "Esvaziar lixeira")
        m.clique(d["x"] + d["l"] - 16 - 45, d["y"] + d["a"] - 40 + 13)
        m.esperar_serial("arquivos: lixeira esvaziada /.lixeira (0)", 10)
        verificar("arquivos: pasta /.lixeira (0 itens)" in m.serial()[n0:], "Esvaziar apaga de vez o que estava na lixeira")
        m.clique(a["fx"] + a["l"] + 4 - 2 - 20 + 8, a["fy"] + 12)
        time.sleep(1)
        focar_terminal()
        out = m.comando("ls /.lixeira")
        verificar("lx.txt" not in out, "a lixeira ficou vazia")
        out = m.comando("desmontar usb0")
        time.sleep(1)

    # ---------------------------------------------------- conferências no host
    pasta = os.path.join(SAIDA, "h87-saida")
    shutil.rmtree(pasta, ignore_errors=True)
    os.makedirs(pasta)
    for nome in ("h.tar", "h.zip"):
        mtools(pen, "mcopy", "-o", f"p:/{nome}", os.path.join(pasta, nome))
    with tarfile.open(os.path.join(pasta, "h.tar")) as t:
        verificar(t.extractfile("historia/linux.txt").read() == linux_sagui, "o .tar do SaguiOS abre no tarfile do Python, igual ao original")
    with zipfile.ZipFile(os.path.join(pasta, "h.zip")) as zf:
        verificar(zf.testzip() is None, "o .zip do SaguiOS passa no testzip do Python (CRC de tudo)")
        verificar(zf.read("historia/linux.txt") == linux_sagui and zf.read("arte/sagui.txt") == sagui_txt,
                  "o conteúdo do .zip é igual ao rootfs")
        info = zf.getinfo("historia/linux.txt")
        verificar(info.compress_type == zipfile.ZIP_DEFLATED and info.compress_size < info.file_size,
                  f"o texto foi comprimido com deflate ({info.file_size} -> {info.compress_size} bytes)")
    cap_local = os.path.join(pasta, "captura.png")
    mtools(pen, "mcopy", "-o", "p:" + nome_cap[len("/usb"):], cap_local)
    png = Image.open(cap_local)
    png.load()   # decodifica tudo: confere o zlib e os CRC dos pedaços
    verificar(png.size == tela.size, f"o PNG da captura abre no PIL com o tamanho da tela {png.size}")
    a, b = png.convert("RGB"), tela
    iguais = sum(1 for x in range(0, a.size[0], 7) for y in range(0, a.size[1], 7)
                 if a.getpixel((x, y)) == b.getpixel((x, y)))
    total = len(range(0, a.size[0], 7)) * len(range(0, a.size[1], 7))
    verificar(iguais >= total * 0.97, f"a captura bate com a tela do QEMU ({iguais} de {total} pontos iguais)")

    if tem_ffmpeg:
        amostras, taxa = ler_wav(wav)
        j440, j330 = janelas_com(amostras, taxa, 440), janelas_com(amostras, taxa, 330)
        verificar(j440 >= 15 and j330 >= 15, f"o som gravado tem os dois tons: {j440} janelas de 440 Hz e {j330} de 330 Hz (0,1 s cada)")


if __name__ == "__main__":
    main()
