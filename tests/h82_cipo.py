#!/usr/bin/env python3
"""H82 - o Cipó: CSS moderno, fontes TrueType, imagens novas e JavaScript.

Critérios:
  - no computador: o GIF (com transparência e entrelaçado) sai igual ao do
    Pillow, pixel a pixel; o WebP com perdas sai parecido com o do Pillow
    (sem o filtro de suavização dos blocos, D043: diferença média pequena);
  - uma página com CSS moderno (flex, grid, var(), gradiente, position,
    float, tabela, transform, CSS externo, @font-face com uma fonte TrueType,
    SVG e imagens GIF, WebP e PNG transparente), servida por HTTP local,
    aparece na janela igual, pixel a pixel, ao motor compilado no computador
    (o mesmo esquema da H62);
  - uma página com JavaScript (as verificações do teste de JavaScript do
    pull request #6, de Robson de Lima Barbosa, mais: toString em outras
    bases, cookies e fetch com POST) roda: console.log chega ao registro,
    document.title muda o título, o DOM muda o que aparece (a caixa fica
    verde), um timer roda depois do load, e um clique no botão roda o
    evento e a página é redesenhada (a caixa fica azul).
"""
import glob
import http.server
import os
import random
import shutil
import socketserver
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, RAIZ, SAIDA  # noqa: E402
from h62_navegador import compilar_referencia, BARRA, VISTA_L, VISTA_A  # noqa: E402
from PIL import Image, ImageChops  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
PASTA = os.path.join(SAIDA, "h82-paginas")
RENDER = os.path.join(SAIDA, "h82-teste_pagina")
TITULO = "Cipó"
pedidos = []


class Atendente(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=PASTA, **k)

    def end_headers(self):
        if self.path.startswith("/js.html"):
            self.send_header("Set-Cookie", "sabor=caju; Path=/")
        super().end_headers()

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        corpo = self.rfile.read(n).decode("utf-8", "replace")
        pedidos.append(("POST", self.path, corpo, self.headers.get("Cookie", ""), self.headers.get("X-Teste", "")))
        resposta = f"{self.command} [{corpo}] cookies={self.headers.get('Cookie', '')} x={self.headers.get('X-Teste', '')}"
        dados = resposta.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(dados)))
        self.end_headers()
        self.wfile.write(dados)

    def guess_type(self, path):
        if path.endswith(".html"):
            return "text/html; charset=utf-8"
        return super().guess_type(path)

    def log_message(self, *a):
        pass


def preparar():
    if os.path.exists(PASTA):
        shutil.rmtree(PASTA)
    shutil.copytree(os.path.join(RAIZ, "tests", "navegador", "cipo"), PASTA)
    os.makedirs(os.path.join(PASTA, "imagens"))
    # Uma fonte TrueType "da web": a DejaVu Serif Itálico com outro nome.
    shutil.copy(os.path.join(RAIZ, "rootfs", "fontes", "DejaVuSerif-Italic.ttf"), os.path.join(PASTA, "letra.ttf"))
    # GIF com paleta, transparência e entrelaçamento.
    gif = Image.new("P", (60, 40), 0)
    gif.putpalette([255, 0, 255] + [40, 160, 60] + [250, 220, 30] + [20, 40, 200] + [0, 0, 0] * 252)
    g = gif.load()
    for y in range(40):
        for x in range(60):
            g[x, y] = 0 if (x - 30) ** 2 + (y - 20) ** 2 > 18 ** 2 else 1 + (x // 10 + y // 10) % 3
    gif.save(os.path.join(PASTA, "imagens", "gif.gif"), transparency=0, interlace=True)
    # Foto em WebP com perdas.
    foto = Image.new("RGB", (96, 64))
    f = foto.load()
    for y in range(64):
        for x in range(96):
            f[x, y] = (x * 255 // 95, 80 + y * 2, 255 - (x + y) * 255 // 158)
    foto.save(os.path.join(PASTA, "imagens", "foto.webp"), quality=90)
    # PNG com transparência parcial sobre um fundo azul da página.
    tr = Image.new("RGBA", (50, 30), (0, 0, 0, 0))
    t = tr.load()
    for y in range(30):
        for x in range(50):
            t[x, y] = (200, 30, 30, x * 255 // 49)
    tr.save(os.path.join(PASTA, "imagens", "transparente.png"))
    r = compilar_referencia(RENDER)
    if r.returncode != 0:
        raise FalhaTeste("o desenho de referência não compilou: " + r.stderr[-1500:])


def testar_decodificadores():
    nav = os.path.join(RAIZ, "userland", "navegador")
    prog = os.path.join(SAIDA, "h82-teste_decodificar")
    r = subprocess.run(["gcc", "-m32", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover",
                        "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
                        "-I", os.path.join(RAIZ, "tests", "navegador", "simulado"), "-I", nav, "-o", prog,
                        os.path.join(RAIZ, "tests", "navegador", "teste_decodificar.c"),
                        os.path.join(nav, "imagens.c"), os.path.join(nav, "png.c"), os.path.join(nav, "jpeg.c"),
                        os.path.join(nav, "descompactar.c"), os.path.join(RAIZ, "terceiros", "libwebp-vp8", "vp8.c")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise FalhaTeste("teste dos decodificadores não compilou: " + r.stderr[-1500:])

    def decodificar(nome):
        cru = os.path.join(SAIDA, "h82-" + nome + ".cru")
        r = subprocess.run([prog, os.path.join(PASTA, "imagens", nome), cru], capture_output=True, text=True)
        verificar(r.returncode == 0, f"{nome} decodificado ({r.stdout.strip() or r.stderr[-300:]})")
        d = open(cru, "rb").read()
        l, a = struct.unpack("<II", d[:8])
        return Image.frombuffer("RGBA", (l, a), d[8:], "raw", "BGRA", 0, 1)

    nosso = decodificar("gif.gif")
    pillow = Image.open(os.path.join(PASTA, "imagens", "gif.gif")).convert("RGBA")
    # Transparente é transparente (a cor do pixel não importa); o resto, igual.
    a, b = nosso.load(), pillow.load()
    erros = sum(1 for y in range(pillow.size[1]) for x in range(pillow.size[0])
                if (a[x, y][3] == 0) != (b[x, y][3] == 0) or (b[x, y][3] and a[x, y] != b[x, y]))
    verificar(nosso.size == pillow.size and erros == 0, f"GIF entrelaçado e transparente igual ao do Pillow ({erros} pixels diferentes)")

    nosso = decodificar("foto.webp").convert("RGB")
    pillow = Image.open(os.path.join(PASTA, "imagens", "foto.webp")).convert("RGB")
    dif = ImageChops.difference(nosso, pillow)
    hist = dif.histogram()
    media = sum(i * hist[c * 256 + i] for c in range(3) for i in range(256)) / (3 * pillow.size[0] * pillow.size[1])
    verificar(nosso.size == pillow.size and media < 3.0, f"WebP parecido com o do Pillow (diferença média {media:.2f} de 255)")


def testar_robustez():
    """O motor (HTML, CSS, layout e pintura), compilado com verificação de
    memória, lê páginas cortadas, embaralhadas e lixo sem cair (como a H61
    faz com o leitor de HTML da 2.0)."""
    prog = os.path.join(SAIDA, "h82-teste_pagina-asan")
    # shift-base fica de fora: o código de cores do #6 desloca int com sinal
    # (255 << 24), que o gcc faz como esperado; o resto do UBSan vale.
    r = compilar_referencia(prog, ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize=shift-base",
                                   "-fno-sanitize-recover"])
    if r.returncode != 0:
        raise FalhaTeste("o motor com verificação de memória não compilou: " + r.stderr[-1500:])
    sorte = random.Random(82)
    bases = [open(p, "rb").read() for p in sorted(glob.glob(os.path.join(RAIZ, "tests", "navegador", "html", "*.html")))]
    bases += [open(os.path.join(PASTA, "css.html"), "rb").read(),
              open(os.path.join(RAIZ, "tests", "navegador", "paginas", "teste1.html"), "rb").read()]
    caminho = os.path.join(PASTA, "estragada.html")
    for k in range(120):
        b = bytearray(sorte.choice(bases))
        if k % 3 == 0:
            b = b[:sorte.randrange(len(b) + 1)]
        elif k % 3 == 1:
            for _ in range(sorte.randrange(1, 40)):
                i = sorte.randrange(len(b))
                b[i] = sorte.choice(b"<>/&;:{}()\"'=! \x00\xc3\xff#x%-")
        else:
            b = bytearray(sorte.randrange(256) for _ in range(sorte.randrange(1, 3000)))
        with open(caminho, "wb") as f:
            f.write(b)
        r = subprocess.run([prog, caminho, "500", caminho + ".cru"], capture_output=True, timeout=60,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        if r.returncode != 0:
            os.replace(caminho, os.path.join(SAIDA, f"h82-falhou-{k}.html"))
            raise FalhaTeste(f"página estragada {k} derrubou o motor: {r.stderr.decode(errors='replace')[-800:]}")
    verificar(True, "120 páginas estragadas diagramadas e pintadas sem erro de memória")


def referencia(nome):
    cru = os.path.join(SAIDA, f"h82-ref-{nome}.cru")
    r = subprocess.run([RENDER, os.path.join(PASTA, nome), str(VISTA_L), cru], capture_output=True, text=True,
                       encoding="latin-1")
    if r.returncode != 0:
        raise FalhaTeste("referência falhou: " + r.stdout + r.stderr)
    d = open(cru, "rb").read()
    l, a = struct.unpack("<II", d[:8])
    return Image.frombuffer("RGBX", (l, a), d[8:], "raw", "BGRX", 0, 1).convert("RGB"), r.stdout


def comparar(m, ref, j, nome):
    m.mouse_para(j["x"] + 300, j["y"] + BARRA + VISTA_A + 10)
    time.sleep(0.5)
    tela = Image.open(m.captura(nome)).convert("RGB")
    vista = tela.crop((j["x"], j["y"] + BARRA, j["x"] + VISTA_L, j["y"] + BARRA + VISTA_A))
    esperado = Image.new("RGB", (VISTA_L, VISTA_A), (255, 255, 255))
    esperado.paste(ref.crop((0, 0, VISTA_L, min(ref.size[1], VISTA_A))), (0, 0))
    corte = 768 - 6 - 56 - 30 - (j["y"] + BARRA)   # a doca fica por cima do pé da janela
    if 0 < corte < VISTA_A:
        vista = vista.crop((0, 0, VISTA_L, corte))
        esperado = esperado.crop((0, 0, VISTA_L, corte))
    caixa = ImageChops.difference(vista, esperado).getbbox()
    if caixa:
        vista.save(os.path.join(SAIDA, f"h82-vista-{nome}.png"))
        esperado.save(os.path.join(SAIDA, f"h82-esperado-{nome}.png"))
    verificar(caixa is None, f"{nome}: a janela mostra a página igual à referência, pixel a pixel (diferença em {caixa})")


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def cor_na_vista(m, j, cor, nome):
    """Quantos pixels da cor dada há na vista da página."""
    tela = Image.open(m.captura(nome)).convert("RGB")
    vista = tela.crop((j["x"], j["y"] + BARRA, j["x"] + VISTA_L, j["y"] + BARRA + 400))
    return sum(1 for p in vista.getdata() if p == cor)


def main():
    os.makedirs(SAIDA, exist_ok=True)
    preparar()
    testar_decodificadores()
    testar_robustez()
    ref, info = referencia("css.html")
    verificar("imagens: 4 (4 carregadas)" in info, "referência montada: " + info.replace("\n", ", "))
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    base = f"http://10.0.2.2:{porta}"
    try:
        with Maquina("h82", "ide", extras=REDE, memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(1)
            # CSS
            m.digitar(f"navegador {base}/css.html\n", 0.04)
            s = esperar(m, f"navegador: pronto {base}/css.html", 180)
            verificar("navegador: imagens prontas (4 de 4)" in s, "as imagens GIF, WebP e PNG chegaram (e o SVG da página)")
            verificar("navegador: titulo CSS do Cip" in s, "título da página com acento (UTF-8)")
            time.sleep(1)
            j = janela(s, TITULO)
            comparar(m, ref, j, "H82-1-css")
            # JavaScript
            antes = len(m.serial())
            m.clique(j["x"] + 300, j["y"] + 17)
            m.digitar(f"{base}/js.html\n", 0.03)
            s = esperar(m, f"navegador: pronto {base}/js.html", 180, antes)
            time.sleep(2)
            s = m.serial()[antes:]
            verificar("js: resultado fat=120 soma=50" in s, "funções, recursão, for, continue e textos")
            verificar("js: global 50" in s, "variáveis valem entre scripts")
            verificar("cipo: js 4 scripts, 1 parados" in s, "script com erro para sem derrubar a página")
            verificar("js: externo /js.html" in s, "script externo")
            verificar("js: t1 42 get st 9 23 57 1 TypeError!" in s, "classes, closures, desestruturação e exceções")
            verificar('js: t2 {"a":[1,{"b":"c"}],"d":null} x2y aBC 34 3' in s, "JSON, modelos e regex")
            verificar("js: t3 0.30 3.5 2 -2 123 undefined nn 2" in s, "números e operadores novos")
            verificar("js: t4 5" in s, "async e promessas")
            verificar("js: t5 1 app BODY" in s, "DOM e eventos de carga")
            verificar("js: t7 0,1,4,true, 2+4+6 1 7true" in s, "geradores: next, yield*, throw e return")
            verificar("js: t8 257 44 8 2 3 2 2 -56" in s, "ArrayBuffer, typed arrays, DataView e UTF-8")
            verificar("js: t9 ff 0.1 -12.2 0.002200 " in s, "toString em outras bases (o algoritmo do V8)")
            verificar("js: t11 cor=verde,sabor=caju" in s, "cookies: o do servidor (Set-Cookie) e o do script")
            verificar("js: t10 POST [oi do cipo] cookies=sabor=caju; cor=verde x=sim" in s
                      or "js: t10 POST [oi do cipo] cookies=cor=verde; sabor=caju x=sim" in s,
                      "fetch com POST leva o corpo, o cabeçalho do script e os cookies")
            verificar("navegador: titulo Depois" in s, "document.title muda o título")
            esperar(m, "js: t12 timer", 20, antes)
            verificar(True, "o timer rodou depois do load")
            verificar(cor_na_vista(m, j, (0x00, 0xAA, 0x44), "H82-2-js") > 5000, "o JavaScript pintou a caixa de verde")
            laranja = cor_na_vista(m, j, (0xFF, 0x88, 0x00), "H82-2-js")
            verificar(5000 < laranja < 8000, f"o Canvas 2D pintou o retângulo laranja com um círculo preto ({laranja} pixels laranja)")
            verificar("js: t13 webgl" in m.serial()[antes:], "a página ganhou um contexto WebGL")
            azul = sum(cor_na_vista(m, j, (0, g, 255), "H82-2-js") for g in (127, 128))
            verificar(azul > 4000, f"o WebGL (em software) limpou a tela com a cor pedida ({azul} pixels)")
            antes = len(m.serial())
            m.clique(j["x"] + 8 + 100, j["y"] + BARRA + 8 + 20)
            esperar(m, "js: t6 1", 20, antes)
            time.sleep(1.5)
            verificar(cor_na_vista(m, j, (0x22, 0x44, 0xCC), "H82-3-clique") > 5000, "o clique rodou o evento e a caixa ficou azul")
    finally:
        servidor.shutdown()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
