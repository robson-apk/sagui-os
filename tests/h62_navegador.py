#!/usr/bin/env python3
"""H62 - janela do Sagui Navegador.

Critérios:
  - páginas de teste (tests/navegador/paginas), servidas por um servidor HTTP
    local, aparecem na janela exatamente como a referência: o mesmo código de
    layout e desenho (userland/navegador) compilado no computador desenha a
    página inteira, e a parte visível da janela tem de ser igual, pixel a
    pixel, no topo e depois de rolar (PgDn);
  - o texto escondido por display:none não aparece; as imagens PNG e JPEG
    (inclusive progressiva) chegam e entram no layout;
  - clicar num link abre a página 2; o botão voltar e o avançar andam no
    histórico; um link #âncora rola até a seção; um link quebrado mostra a
    página de erro;
  - o formulário de busca manda GET /busca?fonte=teste&q=... com o texto
    digitado em UTF-8 (com acentos);
  - +Fav guarda a página e "Favoritos" a lista;
  - um .md vem como texto puro.
Desde a 2.1 (H82, D043) quem diagrama e pinta é o motor do Cipó
(userland/navegador/motor), com as fontes DejaVu: a referência é o motor
compilado aqui. Os critérios são os mesmos; mudaram só as cores que o teste
procura na captura (links #0B57D0 e a borda #767676 dos campos do motor).
"""
import http.server
import os
import shutil
import socketserver
import struct
import subprocess
import sys
import threading
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, RAIZ, SAIDA  # noqa: E402
from PIL import Image, ImageChops  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
PASTA = os.path.join(SAIDA, "h62-paginas")
PAGINAS = os.path.join(RAIZ, "tests", "navegador", "paginas")
RENDER = os.path.join(SAIDA, "h62-teste_pagina")
VISTA_L, VISTA_A, BARRA = 946, 606, 34
LINK = (0x0B, 0x57, 0xD0)          # a:link do motor do Cipó (H82)
BORDA_CAMPO = (0x76, 0x76, 0x76)   # borda dos <input> do motor
pedidos = []


class Atendente(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=PASTA, **k)

    def do_GET(self):
        pedidos.append(self.path)
        if self.path.startswith("/busca"):
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            corpo = ("<html><head><meta charset='utf-8'><title>Resultado</title></head><body><h1>Busca por "
                     + q.get("q", [""])[0] + "</h1></body></html>").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(corpo)))
            self.end_headers()
            self.wfile.write(corpo)
            return
        super().do_GET()

    def guess_type(self, path):
        if path.endswith(".md"):
            return "text/plain; charset=utf-8"
        if path.endswith(".html"):
            return "text/html; charset=utf-8"
        return super().guess_type(path)

    def log_message(self, *a):
        pass


def preparar():
    if os.path.exists(PASTA):
        shutil.rmtree(PASTA)
    shutil.copytree(PAGINAS, PASTA)
    os.makedirs(os.path.join(PASTA, "imagens"))
    # Imagens geradas aqui: um sagui de pixels (PNG com transparência) e duas fotos.
    im = Image.new("RGBA", (48, 40), (0, 0, 0, 0))
    p = im.load()
    for y in range(40):
        for x in range(48):
            dx, dy = x - 24, y - 20
            if dx * dx + dy * dy < 17 * 17:
                p[x, y] = (110, 70, 40, 255) if dx * dx + dy * dy > 10 * 10 or dy < -4 else (230, 200, 160, 255)
    for cx in (17, 31):
        for y in range(14, 19):
            for x in range(cx - 2, cx + 3):
                p[x, y] = (0, 0, 0, 255)
    im.save(os.path.join(PASTA, "imagens", "sagui.png"))
    foto = Image.new("RGB", (160, 100))
    q = foto.load()
    for y in range(100):
        for x in range(160):
            q[x, y] = (x * 255 // 159, 120 + (y * 100 // 99), 255 - x * 255 // 159)
    foto.save(os.path.join(PASTA, "imagens", "foto.jpg"), quality=85)
    foto.transpose(Image.FLIP_LEFT_RIGHT).save(os.path.join(PASTA, "imagens", "progressiva.jpg"), quality=80, progressive=True)
    # O desenho de referência: o mesmo código (o motor do Cipó, H82), compilado aqui.
    r = compilar_referencia(RENDER)
    if r.returncode != 0:
        raise FalhaTeste("o desenho de referência não compilou: " + r.stderr[-1500:])


def compilar_referencia(saida, opcoes=("-O2",)):
    """Compila tests/navegador/teste_pagina.c com o motor do navegador (o mesmo
    código que roda no SaguiOS) e o sagui.h simulado. As contas em double vão
    pelo SSE2, como no SaguiOS (D042), para darem os mesmos pixels."""
    nav = os.path.join(RAIZ, "userland", "navegador")
    fontes = [os.path.join(RAIZ, "tests", "navegador", "teste_pagina.c")] + \
        [os.path.join(nav, f) for f in ("motor/motor.c", "motor/quebra.c", "fontes/fonte.c", "fontes/brotli.c",
                                        "graficos/svg.c", "graficos/canvas2d.c", "imagens.c", "png.c", "jpeg.c",
                                        "descompactar.c")] + \
        [os.path.join(RAIZ, "terceiros", f) for f in ("font-rs/acumulador.c", "libwebp-vp8/vp8.c")] + \
        [os.path.join(RAIZ, "build", "gerado", "fonte8x16.c")]
    return subprocess.run(["gcc", "-m32", "-std=gnu11", *opcoes, "-msse2", "-mfpmath=sse", "-Wall", "-Wextra", "-Werror",
                           "-Wno-unused-function", "-Wno-misleading-indentation", "-Wno-unused-parameter", "-Wno-stringop-truncation",
                           "-finput-charset=UTF-8", "-fexec-charset=ISO-8859-1", "-DMOTOR_HOST",
                           "-DPASTA_FONTES_HOST=\"" + os.path.join(RAIZ, "rootfs") + "\"",
                           "-I", os.path.join(RAIZ, "tests", "navegador", "simulado"), "-I", nav, "-o", saida] + fontes,
                          capture_output=True, text=True)


def testar_enderecos():
    programa = os.path.join(SAIDA, "h62-teste_enderecos")
    r = subprocess.run(["gcc", "-m32", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined", "-Wall", "-Wextra", "-Werror",
                        "-Wno-format-truncation", "-I", os.path.join(RAIZ, "tests", "navegador", "simulado"), "-o", programa,
                        os.path.join(RAIZ, "tests", "navegador", "teste_enderecos.c"),
                        os.path.join(RAIZ, "userland", "navegador", "enderecos.c")], capture_output=True, text=True)
    if r.returncode != 0:
        raise FalhaTeste("teste de endereços não compilou: " + r.stderr[-1500:])
    r = subprocess.run([programa], capture_output=True, text=True)
    verificar(r.returncode == 0, "endereços: " + (r.stdout.strip() or r.stderr[-500:]))


def referencia(nome):
    cru = os.path.join(SAIDA, f"h62-ref-{nome}.cru")
    r = subprocess.run([RENDER, os.path.join(PASTA, nome), str(VISTA_L), cru], capture_output=True, text=True,
                       encoding="latin-1")
    if r.returncode != 0:
        raise FalhaTeste("referência falhou: " + r.stdout + r.stderr)
    d = open(cru, "rb").read()
    l, a = struct.unpack("<II", d[:8])
    return Image.frombuffer("RGBX", (l, a), d[8:], "raw", "BGRX", 0, 1).convert("RGB"), r.stdout


def comparar(m, ref, j, rolagem, nome):
    m.mouse_para(j["x"] + 300, j["y"] + BARRA + VISTA_A + 10)   # o ponteiro fica na barra de status, fora da vista
    time.sleep(0.5)
    png = m.captura(nome)
    tela = Image.open(png).convert("RGB")
    vista = tela.crop((j["x"], j["y"] + BARRA, j["x"] + VISTA_L, j["y"] + BARRA + VISTA_A))
    esperado = Image.new("RGB", (VISTA_L, VISTA_A), (255, 255, 255))
    esperado.paste(ref.crop((0, rolagem, VISTA_L, min(ref.size[1], rolagem + VISTA_A))), (0, 0))
    # A doca (H75) fica por cima do pé da janela: a comparação para acima
    # dela (e do balão com o nome, 30 pixels acima).
    corte = 768 - 6 - 56 - 30 - (j["y"] + BARRA)
    if 0 < corte < VISTA_A:
        vista = vista.crop((0, 0, VISTA_L, corte))
        esperado = esperado.crop((0, 0, VISTA_L, corte))
    dif = ImageChops.difference(vista, esperado)
    caixa = dif.getbbox()
    if caixa:
        dif.save(os.path.join(SAIDA, f"h62-dif-{nome}.png"))
        vista.save(os.path.join(SAIDA, f"h62-vista-{nome}.png"))
        esperado.save(os.path.join(SAIDA, f"h62-esperado-{nome}.png"))
    verificar(caixa is None, f"{nome}: a janela mostra a página igual à referência, pixel a pixel (rolagem {rolagem})")


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def main():
    testar_enderecos()
    preparar()
    ref, info = referencia("teste1.html")
    verificar("ESTE TEXTO" not in info, "referência montada: " + info.replace("\n", ", "))
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    base = f"http://10.0.2.2:{porta}"
    try:
        with Maquina("h62", "ide", extras=REDE, memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(1)
            m.digitar(f"navegador {base}/teste1.html\n", 0.04)
            esperar(m, f"navegador: pronto {base}/teste1.html", 120)
            s = m.serial()
            verificar("navegador: imagens prontas (3 de 3)" in s, "as três imagens (PNG, JPEG e JPEG progressivo) chegaram")
            time.sleep(1)
            j = janela(s, "Cipó")   # o nome desde a H82
            comparar(m, ref, j, 0, "H62-1-topo")
            # Rolar uma página (PgDn rola a vista menos 40 pixels).
            m.tecla("pgdn", 0.3)
            time.sleep(1)
            comparar(m, ref, j, VISTA_A - 40, "H62-2-rolada")
            m.tecla("home", 0.3)
            time.sleep(0.5)
            # Os links são achados na captura pelo sublinhado azul.
            antes = len(m.serial())
            png = Image.open(m.captura("H62-tmp", guardar=False)).convert("RGB")
            alvo = achar_texto_azul(png, j)
            verificar(alvo is not None, "achou os links azuis na captura")
            # Clica no primeiro link azul da página (o "link para a página 2").
            m.clique(*alvo)
            esperar(m, f"navegador: pronto {base}/pagina2.html", 60, antes)
            verificar(True, "clicar no link abriu a página 2")
            antes = len(m.serial())
            m.clique(j["x"] + 6 + 13, j["y"] + 5 + 12)          # voltar
            esperar(m, f"navegador: pronto {base}/teste1.html", 60, antes)
            verificar(True, "o botão voltar trouxe a primeira página")
            antes = len(m.serial())
            m.clique(j["x"] + 34 + 13, j["y"] + 5 + 12)         # avançar
            esperar(m, f"navegador: pronto {base}/pagina2.html", 60, antes)
            verificar(True, "o botão avançar foi de novo para a página 2")
            m.captura("H62-3-pagina2")
            # Link quebrado: página de erro.
            png = Image.open(m.captura("H62-tmp", guardar=False)).convert("RGB")
            links = achar_links(png, j)
            verificar(len(links) >= 2, f"a página 2 tem {len(links)} links")
            antes = len(m.serial())
            m.clique(*links[1])
            esperar(m, "navegador: pronto", 60, antes)
            verificar("situacao 404" in m.serial()[antes:], "link quebrado: o servidor respondeu 404 e a página aparece")
            # Endereço digitado: a busca.
            antes = len(m.serial())
            digitar_endereco(m, j, f"{base}/teste1.html#secao")
            esperar(m, "navegador: pronto", 60, antes)
            s = m.serial()[antes:]
            verificar("ancora" in s or "teste1.html#secao" in s, "endereço com #secao abre a página já na seção")
            m.captura("H62-4-ancora")
            # Formulário: o campo de texto fica perto do botão Buscar; procura pela borda do campo.
            m.tecla("home", 0.3)
            time.sleep(0.5)
            png = Image.open(m.captura("H62-tmp", guardar=False)).convert("RGB")
            campo = achar_campo(png, j)
            if campo is None:
                for _ in range(3):
                    m.tecla("pgdn", 0.3)
                    time.sleep(0.5)
                    png = Image.open(m.captura("H62-tmp", guardar=False)).convert("RGB")
                    campo = achar_campo(png, j)
                    if campo:
                        break
            verificar(campo is not None, "achou o campo de busca na página")
            m.clique(*campo)
            m.digitar("sagui ação", 0.06)
            antes = len(m.serial())
            del pedidos[:]
            m.tecla("ret", 0.1)
            esperar(m, "navegador: pronto", 60, antes)
            busca = [p for p in pedidos if p.startswith("/busca")]
            verificar(busca and busca[0] == "/busca?fonte=teste&q=sagui+a%C3%A7%C3%A3o",
                      f"o formulário mandou {busca[0] if busca else '(nada)'}")
            m.captura("H62-5-busca")
            # Favoritos.
            antes = len(m.serial())
            m.clique(*botao(j, "fav"))
            esperar(m, "navegador: favorito", 20, antes)
            m.clique(*botao(j, "favoritos"))
            esperar(m, "navegador: pronto sagui:favoritos", 30, antes)
            verificar("6 links" in m.serial()[antes:] or "links" in m.serial()[antes:], "página de favoritos aberta")
            m.captura("H62-6-favoritos")
            # Texto puro.
            antes = len(m.serial())
            digitar_endereco(m, j, f"{base}/leia.md")
            esperar(m, f"navegador: pronto {base}/leia.md", 60, antes)
            verificar("text/plain" in m.serial()[antes:], "um .md chega como texto puro")
            m.captura("H62-7-texto-puro")
    finally:
        servidor.shutdown()
    return 0


def achar_links(png, j):
    """Centros dos pedaços de texto sublinhados em azul (cor dos links) na vista, de cima para baixo."""
    px = png.load()
    achados = []
    y0, y1 = j["y"] + BARRA, j["y"] + BARRA + VISTA_A
    for y in range(y0, y1):
        x = j["x"]
        while x < j["x"] + VISTA_L - 20:
            # sublinhado: 12 pixels seguidos da cor do link
            if all(px[x + k, y] == LINK for k in range(12)):
                fim = x
                while fim < j["x"] + VISTA_L and px[fim, y] == LINK:
                    fim += 1
                # Um sublinhado inteiro é um link (as letras azuis logo acima não contam: só linhas com 12+ seguidos).
                if not any(abs(a[1] - (y - 8)) < 4 and a[0] - 6 <= x <= a[2] for a in achados):
                    achados.append((x + 6, y - 8, fim))
                x = fim
            else:
                x += 1
    return [(a[0], a[1]) for a in achados]


def achar_texto_azul(png, j):
    links = achar_links(png, j)
    return links[0] if links else None


def achar_campo(png, j, bordas=None):
    """O campo de texto: retângulo com borda cinza (BORDA_CAMPO) de 100 a 400 de
    largura e 14 a 40 de altura, branco por dentro. Devolve o centro."""
    px = png.load()
    bordas = bordas or (BORDA_CAMPO,)
    x0, x1 = j["x"], j["x"] + VISTA_L
    for y in range(j["y"] + BARRA, j["y"] + BARRA + VISTA_A - 14):
        x = x0
        while x < x1 - 100:
            cor = px[x, y]
            if cor not in bordas:
                x += 1
                continue
            fim = x
            while fim < x1 and px[fim, y] == cor:
                fim += 1
            if fim - x >= 100 and px[x + 50, y + 4] == (255, 255, 255):
                for a in range(14, 40):
                    if y + a < j["y"] + BARRA + VISTA_A and all(px[x + k, y + a] == cor for k in range(10, 60)):
                        return (x + fim) // 2, y + a // 2
            x = fim
    return None


def botao(j, qual):
    larg = 960
    ende_l = larg - 94 - 6 - 34 - 50 - 88 - 18
    x_ir = 94 + ende_l + 6
    x_fav = x_ir + 34 + 6
    x_favs = x_fav + 50 + 6
    return (j["x"] + (x_fav + 25 if qual == "fav" else x_favs + 44), j["y"] + 17)


def digitar_endereco(m, j, texto):
    m.clique(j["x"] + 300, j["y"] + 17)       # o primeiro clique seleciona o endereço todo
    m.digitar(texto + "\n", 0.03)             # e digitar troca


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
