#!/usr/bin/env python3
"""png_icones.py - recorta a folha de ícones (arte/icones-folha.png) e gera
tools/icones_imagem.py, que o mkarte.py usa no lugar dos desenhos.

Uso: png_icones.py arte/icones-folha.png tools/icones_imagem.py [nome=arte/icone-nome.png ...]
(os avulsos têm um ícone só, também no fundo magenta)
(precisa do Pillow; não roda no build: o build só lê icones_imagem.py, com a
biblioteca padrão do Python.)

A folha tem 5 x 5 células, fundo magenta #FF00FF. O alfa sai da distância
de cada pixel ao magenta (borda suave), e a cor da borda é corrigida para
tirar o tom de magenta que vazou. Cada ícone é cortado pela própria caixa e
reduzido (Lanczos) para 64, 40 e 20 pixels, com margem para a sombra que o
mkarte.py acrescenta.
"""
import base64
import sys
import zlib

from PIL import Image

NOMES = ["arquivos", "terminal", "editor", "calculadora", "banana",
         "sagui3d", "sobre", "pendrive", "configuracoes", "cipo",
         "instalado", "monitor", "visualizador", "musica", "energia",
         "lupa", "chuva", "triangulos", "codigo", "componentes",
         "tocador"]
TAMANHOS = (64, 40, 20)


def sem_magenta(img):
    img = img.convert("RGB")
    l, a = img.size
    src = img.load()
    out = Image.new("RGBA", img.size)
    dst = out.load()
    for y in range(a):
        for x in range(l):
            r, g, b = src[x, y]
            # magenta: r e b altos, g baixo. "m" mede quanto do pixel é fundo.
            m = min(r, b) - g
            m = max(0, min(255, m))
            alfa = 255 - m
            if alfa < 8:
                dst[x, y] = (0, 0, 0, 0)
                continue
            if alfa < 250:      # tira o magenta misturado: cor = (pixel - fundo*(1-a)) / a
                k = alfa / 255
                r = (r - 255 * (1 - k)) / k
                b = (b - 255 * (1 - k)) / k
                g = g / k
                r, g, b = (max(0, min(255, round(v))) for v in (r, g, b))
            dst[x, y] = (r, g, b, alfa)
    return out


def main():
    folha = Image.open(sys.argv[1])
    L = folha.size[0] / 5
    dados = {}
    pedacos = [(nome, folha.crop((round(k % 5 * L), round(k // 5 * L), round((k % 5 + 1) * L), round((k // 5 + 1) * L))))
               for k, nome in enumerate(NOMES)]
    for extra in sys.argv[3:]:
        nome, cam = extra.split("=", 1)
        pedacos.append((nome, Image.open(cam)))
    for nome, cel in pedacos:
        ic = sem_magenta(cel)
        ic = ic.crop(ic.getbbox())
        dados[nome] = {}
        for t in TAMANHOS:
            lado = round(t * 0.875)               # margem de 1/16 em cada lado (sombra)
            red = ic.resize((lado, lado), Image.LANCZOS)
            tela = Image.new("RGBA", (t, t), (0, 0, 0, 0))
            m = (t - lado) // 2
            tela.paste(red, (m, m - (1 if t >= 40 else 0)))
            dados[nome][t] = base64.b64encode(zlib.compress(tela.tobytes(), 9)).decode()
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write('"""Gerado por tools/png_icones.py a partir de arte/icones-folha.png. Não edite.\n\n')
        f.write('Pixels RGBA (zlib + base64) de cada ícone, em 64, 40 e 20 pixels."""\n\n')
        f.write("ICONES = {\n")
        for nome, tams in dados.items():
            f.write(f"    {nome!r}: {{\n")
            for t, s in tams.items():
                f.write(f"        {t}: {s!r},\n")
            f.write("    },\n")
        f.write("}\n")


if __name__ == "__main__":
    main()
