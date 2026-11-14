#!/usr/bin/env python3
"""H59 e H60 - descompactação, PNG e JPEG do Sagui Navegador.

Critérios:
  H59: um conjunto de PNGs gerados aqui (todas as profundidades e tipos de
       cor, paleta com transparência, cor-chave, 16 bits, entrelaçado) é
       decodificado igual, pixel a pixel, à referência; gzip e zlib de textos
       grandes voltam idênticos.
  H60: JPEGs sequenciais e progressivos, em cinza e em cores, com várias
       amostragens de cor e intervalos de reinício, ficam dentro da tolerância
       em relação à decodificação de referência. A norma (ITU T.81, anexo A)
       pede que a transformada inversa erre no máximo 1 por amostra; depois
       vêm a conversão de cores e o arredondamento, e com cores de meia
       resolução cada decodificador interpola do seu jeito. Limites: com cores
       em resolução inteira (4:4:4 e cinza), erro médio < 0,5 e máximo <= 4
       por canal; com cores em meia resolução, médio < 1 e máximo <= 8.

Os decodificadores (userland/navegador) são compilados aqui com um sagui.h
simulado (tests/navegador/simulado) e rodam no computador; o Pillow, o
ImageMagick e o zlib do Python só fazem as imagens e a referência. O teste
dentro do SaguiOS (a janela do navegador mostrando as imagens) fica na H62.
"""
import gzip
import os
import random
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import FalhaTeste, RAIZ, SAIDA  # noqa: E402
from PIL import Image  # noqa: E402

PASTA = os.path.join(SAIDA, "h59-imagens")
PROGRAMA = os.path.join(PASTA, "teste_imagens")
NAV = os.path.join(RAIZ, "userland", "navegador")
sorte = random.Random(59)


def verificar(condicao, mensagem, detalhe=""):
    if not condicao:
        raise FalhaTeste(f"{mensagem}: {detalhe}" if detalhe else mensagem)
    print("  ok:", mensagem)


def compilar():
    os.makedirs(PASTA, exist_ok=True)
    fontes = [os.path.join(RAIZ, "tests", "navegador", "teste_imagens.c")] + \
             [os.path.join(NAV, f) for f in ("png.c", "jpeg.c", "descompactar.c")]
    r = subprocess.run(["gcc", "-m32", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                        "-I", os.path.join(RAIZ, "tests", "navegador", "simulado"), "-o", PROGRAMA] + fontes,
                       capture_output=True, text=True)
    verificar(r.returncode == 0, "decodificadores compilam no computador", r.stderr[-2000:])


def decodificar(caminho):
    saida = caminho + ".cru"
    r = subprocess.run([PROGRAMA, caminho, saida], capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        raise FalhaTeste(f"{os.path.basename(caminho)}: {r.stdout.strip()} {r.stderr.strip()}")
    with open(saida, "rb") as f:
        dados = f.read()
    larg, alt = struct.unpack("<II", dados[:8])
    px = struct.unpack(f"<{larg * alt}I", dados[8:])
    return larg, alt, px


def referencia(caminho):
    """Pixels de referência como 0xAARRGGBB (Pillow converte tudo para RGBA de 8 bits)."""
    im = Image.open(caminho)
    im.load()
    if im.mode in ("I;16", "I;16B", "I"):
        # Cinza de 16 bits: o SaguiOS reduz para 8 com v*255/65535.
        larg, alt = im.size
        v = list(im.getdata())
        return larg, alt, [0xFF000000 | ((x * 255 // 65535) * 0x010101) for x in v]
    rgba = im.convert("RGBA")
    larg, alt = rgba.size
    return larg, alt, [(a << 24) | (r << 16) | (g << 8) | b for r, g, b, a in rgba.getdata()]


def imagem_teste(larg, alt, modo):
    """Uma imagem com degradês, formas e ruído (para não ser fácil demais de comprimir)."""
    im = Image.new("RGBA", (larg, alt))
    p = im.load()
    for y in range(alt):
        for x in range(larg):
            r = (x * 255) // max(1, larg - 1)
            g = (y * 255) // max(1, alt - 1)
            b = ((x ^ y) * 7) & 255
            if (x - larg // 2) ** 2 + (y - alt // 2) ** 2 < (min(larg, alt) // 3) ** 2:
                r, g, b = 250 - r // 2, 200, 40
            a = 255 if x < larg // 2 else (y * 255) // max(1, alt - 1)
            if sorte.random() < 0.05:
                r, g, b = sorte.randrange(256), sorte.randrange(256), sorte.randrange(256)
            p[x, y] = (r, g, b, a)
    return im.convert(modo) if modo != "RGBA" else im


def comparar_exato(nome, caminho):
    l1, a1, p1 = decodificar(caminho)
    l2, a2, p2 = referencia(caminho)
    verificar((l1, a1) == (l2, a2), f"{nome}: tamanho {l1}x{a1}", f"esperado {l2}x{a2}")
    dif = [i for i in range(len(p1)) if p1[i] != p2[i]]
    verificar(not dif, f"{nome}: {len(p1)} pixels iguais à referência",
              f"{len(dif)} diferentes; primeiro em {dif[0] if dif else 0}: "
              f"{p1[dif[0]] if dif else 0:08x} x {p2[dif[0]] if dif else 0:08x}")


def testar_png():
    pasta = os.path.join(PASTA, "png")
    os.makedirs(pasta, exist_ok=True)
    casos = []
    base = imagem_teste(97, 61, "RGBA")
    # Pillow: tipos de cor 0 (1, 8 e 16 bits), 2, 3 (paleta com 1/2/4/8 bits e tRNS), 4 e 6.
    for modo in ("1", "L", "RGB", "RGBA", "LA"):
        casos.append((f"modo-{modo}", base.convert(modo), {}))
    pal = base.convert("RGB").quantize(colors=200)
    casos.append(("paleta-8bits", pal, {}))
    for bits, cores in ((1, 2), (2, 4), (4, 16)):
        casos.append((f"paleta-{bits}bits", base.convert("RGB").quantize(colors=cores), {"bits": bits}))
    pal_t = base.convert("RGB").quantize(colors=16)
    casos.append(("paleta-transparente", pal_t, {"transparency": bytes([0, 128, 255] + [255] * 13)}))
    casos.append(("cinza-chave", base.convert("L"), {"transparency": 128}))
    casos.append(("rgb-chave", base.convert("RGB"), {"transparency": (250, 200, 40)}))
    i16 = Image.new("I;16", (53, 29))
    i16.putdata([(x * 1237 + y * 4099) & 0xFFFF for y in range(29) for x in range(53)])
    casos.append(("cinza-16bits", i16, {}))
    for compressao in (0, 1, 9):
        casos.append((f"compressao-{compressao}", base, {"compress_level": compressao}))
    for nome, im, opcoes in casos:
        caminho = os.path.join(pasta, nome + ".png")
        im.save(caminho, **opcoes)
        comparar_exato(f"PNG {nome}", caminho)
    # Profundidades que o Pillow não grava: o ImageMagick faz cinza de 2 e 4
    # bits, RGB e RGBA de 16 bits, e as versões entrelaçadas (Adam7).
    fonte = os.path.join(pasta, "fonte.png")
    base.save(fonte)
    magick = [
        ("cinza-2bits", ["-colorspace", "Gray", "-depth", "2", "PNG8:"]),
        ("cinza-4bits", ["-colorspace", "Gray", "-depth", "4"]),
        ("rgb-16bits", ["-alpha", "off", "-depth", "16", "PNG48:"]),
        ("rgba-16bits", ["-depth", "16", "PNG64:"]),
        ("entrelacado-rgba", ["-interlace", "PNG", "PNG32:"]),
        ("entrelacado-paleta", ["-interlace", "PNG", "-colors", "60", "PNG8:"]),
        ("entrelacado-cinza", ["-interlace", "PNG", "-colorspace", "Gray"]),
    ]
    for nome, args in magick:
        caminho = os.path.join(pasta, nome + ".png")
        prefixo = args[-1] if args[-1].endswith(":") else ""
        opcoes = args[:-1] if prefixo else args
        r = subprocess.run(["convert", fonte] + opcoes + [prefixo + caminho], capture_output=True, text=True)
        verificar(r.returncode == 0, f"ImageMagick fez {nome}", r.stderr)
        comparar_exato(f"PNG {nome}", caminho)
    # Uma imagem 1x1 e uma bem comprida.
    for nome, tam in (("1x1", (1, 1)), ("comprida", (700, 3))):
        caminho = os.path.join(pasta, nome + ".png")
        imagem_teste(*tam, "RGBA").save(caminho)
        comparar_exato(f"PNG {nome}", caminho)


def testar_descompactar():
    """zlib e gzip direto, sem PNG: textos repetitivos e bytes aleatórios."""
    pasta = os.path.join(PASTA, "z")
    os.makedirs(pasta, exist_ok=True)
    # O programa de teste só sabe de imagens; um PNG em tons de cinza 8 bits de
    # 1 pixel de altura é um "embrulho" que passa dados arbitrários pelo
    # inflar_zlib. Para o gzip, comparamos via o PNG também não dá; então o
    # gzip é conferido no SaguiOS (H62, páginas com Content-Encoding).
    for nome, dados in (("texto", ("O sagui pula de galho em galho. " * 900).encode()),
                        ("aleatorio", bytes(sorte.randrange(256) for _ in range(20000)))):
        for nivel in (0, 1, 6, 9):
            L = 4000
            dados_p = dados + bytes(-len(dados) % L)
            alt = len(dados_p) // L
            linha = b"".join(b"\x00" + dados_p[i * L:(i + 1) * L] for i in range(alt))
            z = zlib.compress(linha, nivel)
            png = b"\x89PNG\r\n\x1a\n"
            def pedaco(tipo, c):
                return struct.pack(">I", len(c)) + tipo + c + struct.pack(">I", zlib.crc32(tipo + c) & 0xFFFFFFFF)
            png += pedaco(b"IHDR", struct.pack(">IIBBBBB", L, alt, 8, 0, 0, 0, 0))
            # Vários IDAT, cortados em lugares quaisquer, como faz quem grava em fluxo.
            k = 0
            while k < len(z):
                t = sorte.randrange(1, 5000)
                png += pedaco(b"IDAT", z[k:k + t])
                k += t
            png += pedaco(b"IEND", b"")
            caminho = os.path.join(pasta, f"{nome}-{nivel}.png")
            with open(caminho, "wb") as f:
                f.write(png)
            larg, alt, px = decodificar(caminho)
            verificar(bytes(p & 0xFF for p in px) == dados_p, f"zlib nível {nivel} ({nome}, {len(dados)} bytes) volta igual")
    _ = gzip  # gzip: conferido na H62


def testar_jpeg():
    pasta = os.path.join(PASTA, "jpeg")
    os.makedirs(pasta, exist_ok=True)
    foto = imagem_teste(131, 87, "RGB")
    casos = [
        ("seq-444", foto, {"quality": 90, "subsampling": 0}, True),
        ("seq-422", foto, {"quality": 90, "subsampling": 1}, False),
        ("seq-420", foto, {"quality": 75, "subsampling": 2}, False),
        ("seq-cinza", foto.convert("L"), {"quality": 85}, True),
        ("seq-qualidade-100", foto, {"quality": 100, "subsampling": 0}, True),
        ("seq-qualidade-10", foto, {"quality": 10, "subsampling": 0}, True),
        ("prog-444", foto, {"quality": 90, "subsampling": 0, "progressive": True}, True),
        ("prog-420", foto, {"quality": 80, "subsampling": 2, "progressive": True}, False),
        ("prog-cinza", foto.convert("L"), {"quality": 80, "progressive": True}, True),
        ("seq-otimizado", foto, {"quality": 85, "subsampling": 0, "optimize": True}, True),
        ("pequena", imagem_teste(5, 3, "RGB"), {"quality": 90, "subsampling": 0}, True),
        ("reinicio-444", foto, {"quality": 85, "subsampling": 0, "restart_marker_blocks": 3}, True),
        ("reinicio-420", foto, {"quality": 85, "subsampling": 2, "restart_marker_blocks": 5}, False),
        ("reinicio-prog", foto, {"quality": 85, "subsampling": 2, "progressive": True, "restart_marker_blocks": 2}, False),
    ]
    for nome, im, opcoes, inteira in casos:
        caminho = os.path.join(pasta, nome + ".jpg")
        im.save(caminho, **opcoes)
        comparar_jpeg(nome, caminho, inteira)
    # Intervalo de reinício (marcadores RST) e progressivo do ImageMagick.
    fonte = os.path.join(pasta, "fonte.png")
    foto.save(fonte)
    for nome, args, inteira in (
            ("magick-prog", ["-interlace", "JPEG", "-sampling-factor", "2x2", "-quality", "70"], False),):
        caminho = os.path.join(pasta, nome + ".jpg")
        r = subprocess.run(["convert", fonte] + args + [caminho], capture_output=True, text=True)
        verificar(r.returncode == 0, f"ImageMagick fez {nome}", r.stderr)
        comparar_jpeg(nome, caminho, inteira)
    for nome in ("reinicio-444", "reinicio-420", "reinicio-prog"):
        with open(os.path.join(pasta, nome + ".jpg"), "rb") as f:
            verificar(b"\xff\xdd" in f.read(), f"o JPEG {nome} tem mesmo o marcador DRI")


def comparar_jpeg(nome, caminho, inteira):
    l1, a1, p1 = decodificar(caminho)
    l2, a2, p2 = referencia(caminho)
    verificar((l1, a1) == (l2, a2), f"JPEG {nome}: tamanho {l1}x{a1}", f"esperado {l2}x{a2}")
    soma = maximo = 0
    for x, y in zip(p1, p2):
        for d in (16, 8, 0):
            e = abs(((x >> d) & 255) - ((y >> d) & 255))
            soma += e
            maximo = max(maximo, e)
    media = soma / (3 * len(p1))
    if inteira:
        ok = media < 0.5 and maximo <= 4
    else:
        ok = media < 1.0 and maximo <= 8
    verificar(ok, f"JPEG {nome}: erro médio {media:.2f}, máximo {maximo}", "fora da tolerância")


def main():
    compilar()
    testar_png()
    testar_descompactar()
    testar_jpeg()
    print("  ok: PNG e JPEG conferidos")


if __name__ == "__main__":
    try:
        main()
        print("PASSOU")
    except FalhaTeste as e:
        print(f"FALHOU: {e}")
        sys.exit(1)
