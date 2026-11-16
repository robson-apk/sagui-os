#!/usr/bin/env python3
"""
mkarte.py - gera os ícones do SaguiOS (64x64 na área de trabalho, 40x40 na doca).

Uso: mkarte.py <saida.c>

Escrito pelo Robson de Lima Barbosa (pull request #1) e trazido para a 2.0
(H81), com o segundo tamanho e os ícones novos da 2.0. O estilo atual dos
ícones (um quadrado de cantos redondos com um degradê de cima para baixo, um
brilho suave no alto e um símbolo claro no meio) foi criado por Robson de Lima
Barbosa no pull request #26; aqui ele é refeito só com código, para que
qualquer pessoa (ou IA) consiga desenhar o ícone de um programa novo sem
depender de um gerador de imagens.

Como funciona: cada ícone é descrito com formas simples (retângulo
arredondado, círculo, polígono, traço, curva de Bézier, arco), sempre numa
grade de 64x64. O script calcula a cobertura de cada pixel com 4x4 amostras,
o que suaviza as bordas, acrescenta uma sombra e escreve vetores ARGB em C.
Para o tamanho menor, as mesmas formas são amostradas numa grade mais grossa
(cada pixel cobre 64/40 da grade). As curvas de Bézier seguem a definição
de sempre (a soma ponderada dos pontos de controle pelos polinômios de
Bernstein); nada aqui veio de outro programa de desenho.

O PADRÃO PARA UM ÍCONE NOVO
  1. Escreva uma função  def meu_programa(t):  que começa com
         estilo_app(t, COR_DE_CIMA, COR_DE_BAIXO)
     (o fundo: sempre a mesma caixa, centrada, com o degradê, o brilho e os
     filetes; a cor de cima é mais clara que a de baixo);
  2. desenhe o símbolo por cima, em branco (ou numa cor do tema do programa),
     com t.ret, t.disco, t.poligono, t.traco, t.anel e as curvas bezier(),
     centrado em (32, 31) e dentro de uns 36x36 pixels (de 14 a 50): a caixa
     vai de 9,5 a 54,5; traços de uns 3 a 4 pixels de largura ficam legíveis
     também na doca, em 40 pixels;
  3. acrescente uma linha ("meu_programa", meu_programa) em ICONES.
  O C gerado tem arte_px_<nome> (64) e arte_px_<nome>_40.
"""
import math
import os
import sys

GRADE = 64     # os desenhos são feitos nesta grade
T = 64         # tamanho da saída (muda para 40 na segunda passada)
AMOSTRAS = [(i + 0.5) / 4 for i in range(4)]

# A caixa de todos os ícones de programa (medida nos ícones do pull request
# #26: uns 45 pixels de lado e cantos de raio 10 a 11), centrada na grade e
# um pouco para cima, porque a sombra cai 2 pixels para baixo.
CAIXA = (9.5, 8.5, 54.5, 53.5)
RAIO = 10.5
CX, CY = 32, 31          # o centro da caixa: o símbolo fica em volta dele


def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def misturar(c0, c1, t):
    """A cor c0 levada uma fração t na direção de c1 (0 = c0, 1 = c1)."""
    a, b = rgb(c0), rgb(c1)
    v = [round(a[k] + (b[k] - a[k]) * t) for k in range(3)]
    return (v[0] << 16) | (v[1] << 8) | v[2]


def vertical(c0, c1, y0, y1):
    a, b = rgb(c0), rgb(c1)

    def cor(_x, y):
        t = min(1, max(0, (y - y0) / (y1 - y0)))
        return tuple(a[k] + (b[k] - a[k]) * t for k in range(3))
    return cor


def diagonal(c0, c1, x0, y0, x1, y1):
    """Degradê ao longo da direção (x0, y0) -> (x1, y1)."""
    a, b = rgb(c0), rgb(c1)
    dx, dy = x1 - x0, y1 - y0
    d2 = dx * dx + dy * dy

    def cor(x, y):
        t = min(1, max(0, ((x - x0) * dx + (y - y0) * dy) / d2))
        return tuple(a[k] + (b[k] - a[k]) * t for k in range(3))
    return cor


def bezier(*pts, n=18):
    """Pontos de uma curva de Bézier (quadrática com 3 pontos, cúbica com 4):
    B(s) = soma de C(g, i) * (1 - s)^(g - i) * s^i * P_i, com s de 0 a 1."""
    g = len(pts) - 1
    saida = []
    for k in range(n + 1):
        s = k / n
        x = y = 0.0
        for i, (px, py) in enumerate(pts):
            b = math.comb(g, i) * (1 - s) ** (g - i) * s ** i
            x += b * px
            y += b * py
        saida.append((x, y))
    return saida


def girar(pts, cx, cy, ang, escala=1.0, dx=0.0, dy=0.0):
    """Gira os pontos em volta de (cx, cy) por ang radianos (no sentido do
    relógio, porque o y cresce para baixo), muda a escala e desloca."""
    c, s = math.cos(ang), math.sin(ang)
    return [(cx + dx + escala * ((x - cx) * c - (y - cy) * s),
             cy + dy + escala * ((x - cx) * s + (y - cy) * c)) for x, y in pts]


def dentro_poligono(pts):
    """Teste par-ímpar: um raio para a direita cruza a borda quantas vezes?"""
    arestas = list(zip(pts, pts[1:] + pts[:1]))

    def dentro(x, y):
        d = False
        for (ax, ay), (bx, by) in arestas:
            if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
                d = not d
        return d
    return dentro


def dentro_ret(x0, y0, x1, y1, raio):
    def dentro(x, y):
        dx = max(x0 + raio - x, 0, x - (x1 - raio))
        dy = max(y0 + raio - y, 0, y - (y1 - raio))
        return x0 <= x <= x1 and y0 <= y <= y1 and dx * dx + dy * dy <= raio * raio
    return dentro


class Tela:
    def __init__(self):
        self.p = [[(0.0, 0.0, 0.0, 0.0)] * T for _ in range(T)]
        self.recorte = None       # se tiver um teste, só pinta dentro dele

    def forma(self, dentro, caixa, cor, opacidade=1.0):
        if isinstance(cor, int):
            fixa = rgb(cor)

            def cor(_x, _y, fixa=fixa):
                return fixa
        if self.recorte:
            corte, original = self.recorte, dentro

            def dentro(x, y):
                return original(x, y) and corte(x, y)
        e = GRADE / T                      # quanto da grade cada pixel da saída cobre
        x0, y0, x1, y1 = (v / e for v in caixa)
        for y in range(max(0, int(y0) - 1), min(T, int(y1) + 2)):
            for x in range(max(0, int(x0) - 1), min(T, int(x1) + 2)):
                n = sum(dentro((x + i) * e, (y + j) * e) for i in AMOSTRAS for j in AMOSTRAS)
                if not n:
                    continue
                gx, gy = (x + 0.5) * e, (y + 0.5) * e
                a = n / 16 * (opacidade(gx, gy) if callable(opacidade) else opacidade)
                if a <= 0:
                    continue
                c = cor(gx, gy)
                r, g, b, f = self.p[y][x]
                saida = a + f * (1 - a)
                self.p[y][x] = tuple((c[k] * a + (r, g, b)[k] * f * (1 - a)) / saida for k in range(3)) + (saida,)

    def ret(self, x0, y0, x1, y1, raio, cor, opacidade=1.0):
        self.forma(dentro_ret(x0, y0, x1, y1, raio), (x0, y0, x1, y1), cor, opacidade)

    def disco(self, cx, cy, r, cor, opacidade=1.0, ry=None):
        ry = ry or r
        self.forma(lambda x, y: ((x - cx) / r) ** 2 + ((y - cy) / ry) ** 2 <= 1,
                   (cx - r, cy - ry, cx + r, cy + ry), cor, opacidade)

    def poligono(self, pts, cor, opacidade=1.0):
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.forma(dentro_poligono(pts), (min(xs), min(ys), max(xs), max(ys)), cor, opacidade)

    def traco(self, pts, largura, cor, opacidade=1.0):
        """Uma linha grossa de pontas redondas passando pelos pontos."""
        r = largura / 2
        segs = [(ax, ay, bx - ax, by - ay) for (ax, ay), (bx, by) in zip(pts, pts[1:])]

        def dentro(x, y):
            for ax, ay, dx, dy in segs:
                t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy or 1)))
                if (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2 <= r * r:
                    return True
            return False
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.forma(dentro, (min(xs) - r, min(ys) - r, max(xs) + r, max(ys) + r), cor, opacidade)

    def anel(self, cx, cy, r, largura, cor, a0=0.0, a1=2 * math.pi, opacidade=1.0, ry=None):
        """Um arco (ou o círculo inteiro) traçado com a largura dada. Os
        ângulos são em radianos, a partir da direita, no sentido do relógio."""
        ry = ry or r
        n = max(8, int(abs(a1 - a0) * max(r, ry) / 1.5))
        pts = [(cx + r * math.cos(a0 + (a1 - a0) * k / n), cy + ry * math.sin(a0 + (a1 - a0) * k / n))
               for k in range(n + 1)]
        self.traco(pts, largura, cor, opacidade)

    def com_sombra(self):
        """Sombra macia: o alfa do desenho, deslocado para baixo e borrado."""
        alfa = [[self.p[y][x][3] for x in range(T)] for y in range(T)]
        saida = []
        for y in range(T):
            for x in range(T):
                soma = 0
                d = 2 if T >= 64 else 1        # sombra proporcional ao tamanho
                for j in range(-2, 3):
                    for i in range(-2, 3):
                        xx, yy = x + i, y + j - d
                        if 0 <= xx < T and 0 <= yy < T:
                            soma += alfa[yy][xx]
                s = soma / 25 * (0.42 if T >= 40 else 0.22)
                r, g, b, f = self.p[y][x]
                a = f + s * (1 - f)
                if a <= 0:
                    saida.append(0)
                    continue
                k = f / a
                saida.append((round(a * 255) << 24) | (round(r * k) << 16) | (round(g * k) << 8) | round(b * k))
        return saida


# ---------------------------------------------------------------------------
# O fundo comum a todos os ícones de programa
# ---------------------------------------------------------------------------

def estilo_app(t, cima, baixo):
    """O fundo no estilo do pull request #26: a caixa de cantos redondos com o
    degradê de cima (claro) para baixo (escuro), um filete claro na borda de
    cima, um filete mais escuro embaixo (dá volume) e um brilho branco que
    some até o meio da caixa. Devolve o degradê, para quem precisar pintar
    "buracos" com a cor do fundo (o furo da engrenagem, por exemplo)."""
    x0, y0, x1, y1 = CAIXA
    fundo = vertical(cima, baixo, y0, y1)
    t.ret(x0, y0, x1, y1, RAIO, misturar(baixo, 0x000000, 0.28))
    t.ret(x0, y0, x1, y1 - 0.9, RAIO, misturar(cima, 0xFFFFFF, 0.45))
    t.ret(x0, y0 + 0.8, x1, y1 - 0.9, RAIO - 0.4, fundo)
    t.ret(x0, y0, x1, y1, RAIO, 0xFFFFFF, lambda _x, y: 0.20 * max(0.0, 1 - (y - y0) / 17))
    return fundo


def na_caixa(t):
    """Faz os próximos desenhos ficarem só dentro da caixa (para cenas que
    vão até a borda, como a do Sagui 3D)."""
    x0, y0, x1, y1 = CAIXA
    t.recorte = dentro_ret(x0, y0 + 0.8, x1, y1 - 0.9, RAIO - 0.4)


# ---------------------------------------------------------------------------
# Peças que mais de um ícone usa
# ---------------------------------------------------------------------------

# A banana "de referência", desenhada para o ícone do jogo Banana (centrada
# em CX, CY). As outras usam a mesma forma girada e encolhida.
BANANA_FORA = bezier((44.6, 22.5), (51.5, 36), (43, 51.5), (21, 49.5), n=24)     # a barriga (lado de baixo)
BANANA_DENTRO = bezier((17.5, 42.2), (27.5, 40.5), (37.5, 33.5), (39.8, 23), n=24)   # o lado de cima, côncavo
BANANA_PONTA = [(18, 48.8), (15.6, 47.4), (14.4, 45.4), (15.2, 43.4)]
BANANA = BANANA_FORA + BANANA_PONTA + BANANA_DENTRO + [(40.8, 21.4), (43.4, 21.2)]
BANANA_MEIO = bezier((16.5, 45.5), (31, 46.5), (43, 38), (42.6, 22.5), n=24)      # a quina do meio


def desenhar_banana(t, ang=0.0, escala=1.0, dx=0.0, dy=0.0, cabo=True):
    """Banana cheia e curva: amarela, com a barriga mais escura, um brilho na
    parte de cima, o cabinho marrom e a pontinha escura."""
    def m(pts):
        return girar(pts, CX, CY, ang, escala, dx, dy)
    corpo = m(BANANA)
    ys = [p[1] for p in corpo]
    t.poligono(corpo, vertical(0xFFE45C, 0xF5B921, min(ys), max(ys)))
    antes, t.recorte = t.recorte, dentro_poligono(corpo)
    # a metade de baixo, mais madura (laranja)
    t.traco(m(BANANA_FORA), 9 * escala, 0xE89A12, 0.55)
    t.traco(m(BANANA_FORA), 3.2 * escala, 0xC97A0A, 0.45)
    # o brilho de cima
    t.traco(m(bezier((21, 44), (30.5, 42.8), (39.4, 35.5), (41.4, 26), n=20)), 2.8 * escala, 0xFFF7C2, 0.85)
    t.recorte = antes
    t.traco(m(BANANA_MEIO[2:12]), 0.9 * escala, 0xD69410, 0.35)
    if cabo:
        t.traco(m([(42.3, 22.2), (43.2, 16.6)]), 4.2 * escala, 0x8A6526)
        t.disco(*m([(43.3, 16.4)])[0], 2.3 * escala, 0x4A3416)
    (px, py), = m([(15.6, 45.6)])
    t.disco(px, py, 1.9 * escala, 0x4A3416)


def cara_sagui(t, cx, cy, e=1.0):
    """A cara do sagui-de-tufo-branco: tufos brancos dos lados, cabeça marrom,
    a mancha branca na testa, a máscara clara, olhos grandes e brilhantes."""
    tufo = vertical(0xFFFFFF, 0xD8E0DA, cy - 10 * e, cy + 10 * e)
    for lado in (-1, 1):
        # o tufo: um leque de mechas brancas saindo de trás da cabeça
        t.disco(cx + lado * 8.5 * e, cy, 6.5 * e, tufo, ry=8.5 * e)
        for k in range(9):
            a = math.radians(-56 + k * 14)
            r = (15.5 if k % 2 else 14.2) * e
            t.traco([(cx + lado * 6 * e, cy), (cx + lado * r * math.cos(a), cy + r * math.sin(a))], 3.6 * e, tufo)
    t.disco(cx, cy, 10.5 * e, vertical(0x6E5747, 0x3B2C22, cy - 10 * e, cy + 10 * e), ry=11 * e)
    t.disco(cx, cy - 8.2 * e, 2.4 * e, 0xF4F1EA, ry=1.8 * e)                     # a mancha da testa
    t.disco(cx, cy + 2.6 * e, 7.0 * e, vertical(0xE2C3A6, 0xC09474, cy - 4 * e, cy + 9 * e), ry=6.3 * e)
    for lado in (-1, 1):
        t.disco(cx + lado * 3.9 * e, cy - 0.6 * e, 2.3 * e, 0x17120F)
        t.disco(cx + lado * 3.9 * e + 0.8 * e, cy - 1.4 * e, 0.8 * e, 0xFFFFFF)
    t.disco(cx, cy + 3.2 * e, 1.6 * e, 0x3C2D23, ry=1.1 * e)
    t.traco([(cx - 2.2 * e, cy + 5.6 * e), (cx, cy + 6.4 * e), (cx + 2.2 * e, cy + 5.6 * e)], 1.0 * e, 0x5A4334)


def estrela(cx, cy, r_fora, r_dentro, pontas=5, giro=-math.pi / 2):
    return [(cx + (r_fora if k % 2 == 0 else r_dentro) * math.cos(giro + k * math.pi / pontas),
             cy + (r_fora if k % 2 == 0 else r_dentro) * math.sin(giro + k * math.pi / pontas))
            for k in range(2 * pontas)]


BRANCO = vertical(0xFFFFFF, 0xE6EEF6, 13, 50)       # o branco dos símbolos, levemente sombreado embaixo


# ---------------------------------------------------------------------------
# Os ícones
# ---------------------------------------------------------------------------

def arquivos(t):
    """Arquivos: uma pasta amarela clara na caixa amarela."""
    estilo_app(t, 0xF8C83C, 0xEFA405)
    t.ret(15, 17.5, 30, 26, 3, 0xE59600)                               # a aba de trás
    t.ret(15, 21, 49, 45, 3.5, 0xEBA10A)                               # o fundo da pasta
    t.ret(18.5, 23.5, 45.5, 34, 1.5, 0xFFFDF4)                          # uma folha aparecendo
    t.ret(15, 27, 49, 46, 3.5, vertical(0xFFEB8A, 0xFFCC3A, 27, 46))     # a frente
    t.ret(15, 27, 49, 28.2, 1.2, 0xFFF6C8)


def terminal(t):
    """Terminal: o ">_" verde no quadro escuro."""
    estilo_app(t, 0x545C64, 0x1A1D22)
    t.traco([(18, 21.5), (27, 30), (18, 38.5)], 4.2, 0x3BE35A)
    t.traco([(31, 39), (45, 39)], 4.2, 0x3BE35A)


def editor(t):
    """Editor: uma folha com linhas de texto e um lápis, na caixa azul."""
    estilo_app(t, 0x45B0FB, 0x0569E8)
    folha = [(16.5, 14.5), (36, 14.5), (43.5, 22), (43.5, 47.5), (16.5, 47.5)]
    t.poligono(girar(folha, CX, CY, 0, 1, 1.5, 3), 0x0A4FB0, 0.35)       # sombra da folha
    t.poligono(folha, vertical(0xFFFFFF, 0xE4EFFB, 14, 48))
    t.poligono([(36, 14.5), (43.5, 22), (36, 22)], 0xB8D2F0)
    for k, fim in enumerate((31, 38, 38, 33)):
        t.traco([(21, 21 + k * 5.5 + (2 if k else 0)), (fim, 21 + k * 5.5 + (2 if k else 0))], 2.0,
                0x2F86E6 if k == 0 else 0x9DBFE6)
    # o lápis, na diagonal: corpo amarelo, borracha, ponta de madeira
    def m(pts):
        return girar(pts, 0, 0, math.radians(-50), 1, 38, 38)
    t.poligono(m([(-14, -3), (8, -3), (8, 3), (-14, 3)]), 0x0A4FB0, 0.3)
    t.poligono(m([(-12, -3.4), (8, -3.4), (8, 3.4), (-12, 3.4)]), vertical(0xFFD24A, 0xF29A12, 30, 46))
    t.poligono(m([(-12, -3.4), (8, -3.4), (8, -1.1), (-12, -1.1)]), 0xFFE58A)
    t.poligono(m([(8, -3.4), (10.5, -3.4), (10.5, 3.4), (8, 3.4)]), 0xC9D2DC)
    t.poligono(m([(10.5, -3.4), (13.5, -3.4), (14.5, -2.4), (14.5, 2.4), (13.5, 3.4), (10.5, 3.4)]), 0xF07A8A)
    t.poligono(m([(-12, -3.4), (-12, 3.4), (-19, 0)]), 0xF6D9B0)
    t.poligono(m([(-16.4, -1.3), (-16.4, 1.3), (-19, 0)]), 0x2A2F35)


def calculadora(t):
    """Calculadora: o corpo branco com o visor e as teclas (a de "=" em
    laranja), na caixa azul."""
    estilo_app(t, 0x6CB0E6, 0x2464A8)
    t.ret(19, 14.5, 45, 50.5, 5, 0x123E70, 0.3)
    t.ret(18.5, 13, 45.5, 49, 5, vertical(0xFFFFFF, 0xDDE9F5, 13, 49))
    t.ret(22, 16.5, 42, 23.5, 2, vertical(0x1E4F86, 0x163A66, 16, 24))   # o visor
    t.ret(33, 18.5, 39.5, 21.5, 0.8, 0x9ED8FF)                           # os números do visor
    t.ret(28.5, 18.5, 31.5, 21.5, 0.8, 0x9ED8FF, 0.6)
    w, h, gx, gy = 4.3, 3.9, 1.6, 1.5
    for lin in range(4):
        for col in range(4):
            x, y = 22 + col * (w + gx), 26.5 + lin * (h + gy)
            if col == 3 and lin >= 2:
                if lin == 2:                                              # o "=" alto, laranja
                    t.ret(x, y, x + w, y + 2 * h + gy, 1.3, vertical(0xFFB54A, 0xF27A12, y, y + 2 * h))
                continue
            t.ret(x, y, x + w, y + h, 1.3, 0x4E8FD6 if col < 3 else 0x2E6DB8)


def banana(t):
    """O jogo Banana: a banana cheia e curva na caixa verde."""
    estilo_app(t, 0x50D44E, 0x0C9C0C)
    desenhar_banana(t)


def sagui3d(t):
    """O Sagui 3D: um corredor na mata visto em primeira pessoa (as paredes
    de folhagem se fecham lá no fundo), a trilha e uma banana no fim dela."""
    estilo_app(t, 0x8FDCFF, 0x3E9E3A)
    na_caixa(t)
    fx0, fx1, fy0, fy1 = 28, 36, 25, 33                     # o fim do corredor
    t.ret(fx0 - 1, fy0 - 2, fx1 + 1, fy1, 0, vertical(0xE6F8C0, 0xA8DC78, fy0 - 2, fy1))
    # o chão: grama dos lados e a trilha de terra no meio, que afina no fundo
    t.poligono([(5, 56), (59, 56), (fx1, fy1), (fx0, fy1)], vertical(0x7AD24E, 0x3A9A2E, fy1, 56))
    t.poligono([(21, 56), (43, 56), (33.4, fy1), (30.6, fy1)], vertical(0xEED79A, 0xC49452, fy1, 56))
    # as paredes de mata: verde, com a copa ondulada no alto e poucos troncos
    for lado in (-1, 1):
        borda = 5 if lado < 0 else 59
        fim = fx0 if lado < 0 else fx1
        topo0, base0 = 11, 50
        cor = vertical(0x34A23A, 0x14622A, 10, 50) if lado < 0 else vertical(0x2A8E34, 0x0F5420, 10, 50)
        t.poligono([(borda, topo0), (fim, fy0), (fim, fy1), (borda, base0)], cor)
        for s in (0.22, 0.55, 0.8):                         # troncos, mais finos lá no fundo
            x = borda + (fim - borda) * s
            topo = topo0 + (fy0 - topo0) * s
            base = base0 + (fy1 - base0) * s
            t.traco([(x, topo + 6 * (1 - s)), (x, base)], 2.4 * (1 - s * 0.7), 0x5A3A1C, 0.8)
        for k in range(7):                                  # a copa, em bolas que diminuem
            s = k / 6
            x = borda + (fim - borda) * s
            y = topo0 + (fy0 - topo0) * s
            t.disco(x, y + 1, 5.0 * (1 - s * 0.72), 0x52C04A if lado < 0 else 0x40A842)
    t.recorte = None
    desenhar_banana(t, math.radians(-8), 0.36, -1.0, 6.5)


def sobre(t):
    """Sobre o SaguiOS: a cara do sagui na caixa verde."""
    estilo_app(t, 0x58DA56, 0x0AA60A)
    cara_sagui(t, CX, CY + 0.5, 1.12)


def pendrive(t):
    """Pendrive: o pendrive branco na diagonal, com o símbolo do USB, na
    caixa azul."""
    estilo_app(t, 0x2DB0FC, 0x0270E4)

    def m(pts):                                            # eixo do pendrive: de baixo-esquerda para cima-direita
        return girar(pts, 0, 0, math.radians(-45), 1, CX, CY)
    corpo = [(-17, -6.8), (4, -6.8), (4, 6.8), (-17, 6.8)]
    t.poligono(m([(x + 1, y + 1.5) for x, y in corpo]), 0x0A3C9A, 0.35)
    t.poligono(m(corpo), BRANCO)
    t.traco(m([(-14.5, -4.3), (-14.5, 4.3)]), 3.4, 0xFFFFFF)         # cantos arredondados
    t.poligono(m([(4, -5.2), (16.5, -5.2), (16.5, 5.2), (4, 5.2)]), 0xD7E4F2)   # o conector
    t.traco(m([(4, -5.2), (16.5, -5.2), (16.5, 5.2), (4, 5.2)]), 1.4, 0xFFFFFF)
    for v in (-2.6, 2.6):                                  # os furinhos
        t.poligono(m([(9, v - 1.3), (12.5, v - 1.3), (12.5, v + 1.3), (9, v + 1.3)]), 0x1A62C8)
    # o símbolo do USB (um tridente) no corpo
    azul = 0x1676E6
    t.traco(m([(-14, 0), (0, 0)]), 1.5, azul)
    t.poligono(m([(0.8, 0), (-1.6, -1.7), (-1.6, 1.7)]), azul)
    t.traco(m([(-10, 0), (-7.5, -3.2), (-4, -3.2)]), 1.3, azul)
    t.traco(m([(-8.5, 0), (-6, 3.2), (-3, 3.2)]), 1.3, azul)
    (x, y), = m([(-3.4, -3.2)])
    t.disco(x, y, 1.3, azul)
    (x, y), = m([(-2.4, 3.2)])
    t.ret(x - 1.2, y - 1.2, x + 1.2, y + 1.2, 0, azul)
    (x, y), = m([(-14.5, 0)])
    t.disco(x, y, 1.6, azul)


def configuracoes(t):
    """Configurações: uma engrenagem branca na caixa azul-acinzentada."""
    fundo = estilo_app(t, 0x5B94C8, 0x275C92)
    cor = vertical(0xFFFFFF, 0xD5E2EF, 14, 48)
    for k in range(8):
        a = k * math.pi / 4 + math.pi / 8
        dente = [(-3.6, 10), (3.6, 10), (2.6, 16.2), (-2.6, 16.2)]
        t.poligono([(CX + x * math.cos(a) - y * math.sin(a), CY + x * math.sin(a) + y * math.cos(a))
                    for x, y in dente], cor)
    t.disco(CX, CY, 12, cor)
    t.disco(CX, CY, 5.2, fundo)
    t.anel(CX, CY, 5.2, 1.0, 0x1B4A78, opacidade=0.35)


def cipo(t):
    """O navegador Cipó: um globo branco de linhas e um cipó verde com uma
    folha, na caixa azul."""
    estilo_app(t, 0x2A9BFA, 0x015ADC)
    gx, gy, r = CX - 1, CY - 2, 14
    t.anel(gx, gy, r, 2.2, 0xFFFFFF)
    t.traco([(gx, gy - r), (gx, gy + r)], 1.8, 0xFFFFFF)
    t.anel(gx, gy, 6.5, 1.8, 0xFFFFFF, ry=r)
    for dy in (-7, 0, 7):                                  # paralelos
        w = math.sqrt(r * r - dy * dy)
        t.traco([(gx - w + 0.8, gy + dy), (gx + w - 0.8, gy + dy)], 1.8, 0xFFFFFF)
    # o cipó: sobe pela frente do globo, de baixo-esquerda para a direita
    ramo = bezier((12.5, 49), (22, 40), (35, 47.5), (46, 37), n=24)
    t.traco(ramo, 4.2, 0x1E7A1E)
    t.traco(ramo, 2.6, 0x86E83A)
    t.traco(bezier((21, 45), (22, 40.5), (25, 39.5)), 1.6, 0x86E83A)   # uma gavinha
    # a folha: duas curvas que se encontram nas pontas
    base, ponta = (45, 38), (51.5, 24.5)
    folha = bezier(base, (38.5, 30), (44, 23), ponta, n=14) + bezier(ponta, (53.5, 31), (51, 37), base, n=14)[1:]
    t.poligono(folha, diagonal(0xB4F25A, 0x3AAE2A, 51, 24, 44, 38))
    t.traco(bezier((45.5, 36.5), (47.5, 31), (51, 25.5)), 1.1, 0x2E8A22, 0.8)


def instalado(t):
    """Programa instalado: a caixa de papelão aberta com uma estrela saindo
    dela, na caixa laranja."""
    estilo_app(t, 0xFFAD55, 0xEE7A1C)
    papel = vertical(0xFFF3DE, 0xF0D2A2, 26, 48)
    t.poligono([(17.5, 26.5), (46.5, 26.5), (44.5, 48), (19.5, 48)], 0xA85A12, 0.3)
    t.poligono([(19, 23.5), (45, 23.5), (45, 28), (19, 28)], 0xB9763A)       # o fundo, de dentro
    t.poligono(estrela(CX, 19.5, 8.5, 3.6), vertical(0xFFF2A0, 0xFFC928, 11, 27))
    t.poligono([(19, 27), (45, 27), (43.5, 47), (20.5, 47)], papel)          # a frente
    t.poligono([(19, 27), (12.5, 33.5), (19.5, 34.5)], 0xF6DDB2)              # as abas abertas
    t.poligono([(45, 27), (51.5, 33.5), (44.5, 34.5)], 0xEBC892)
    t.ret(28.5, 27, 35.5, 31.5, 0, 0xD6A86A)                                  # a fita
    t.traco([(19.2, 27), (44.8, 27)], 1.2, 0xFFFFFF, 0.7)


# ---- H88: ícones novos (programas do H87 e o desligar) ----

def monitor(t):
    """Sagui Monitor: um painel escuro com o gráfico da CPU subindo."""
    t.ret(5, 9, 59, 51, 6, vertical(0x3A4552, 0x1E252E, 9, 51))
    t.ret(9, 13, 55, 47, 3, vertical(0x13202A, 0x0C141B, 13, 47))
    for k in range(4):
        t.traco([(12, 20 + k * 8), (52, 20 + k * 8)], 0.8, 0xFFFFFF, 0.12)
    t.poligono([(12, 44), (12, 36), (21, 30), (29, 34), (38, 22), (46, 26), (52, 18), (52, 44)],
               vertical(0x42E584, 0x0B7A3E, 18, 44), 0.35)
    t.traco([(12, 36), (21, 30), (29, 34), (38, 22), (46, 26), (52, 18)], 2.8, 0x42E584)
    t.ret(27, 51, 37, 55, 0, 0x3C4550)
    t.ret(19, 55, 45, 59, 2, vertical(0x6A7582, 0x444D58, 55, 59))


def visualizador(t):
    """Visualizador de imagens: uma foto com céu, sol e montanhas."""
    t.ret(5, 10, 59, 54, 6, vertical(0xFFFFFF, 0xDDE3E8, 10, 54))
    t.ret(9, 14, 55, 50, 3, vertical(0x7CC8F4, 0xC8ECFF, 14, 50))
    t.disco(43, 24, 5, 0xFFD23E)
    t.poligono([(9, 50), (9, 42), (23, 27), (36, 41), (42, 35), (55, 47), (55, 50)], vertical(0x2E9E5B, 0x16683A, 27, 50))
    t.poligono([(23, 27), (27.5, 31.6), (24.5, 33), (21, 30.6)], 0xFFFFFF, 0.85)


def musica(t):
    """Música: um disco com uma nota musical."""
    t.disco(32, 32, 26, vertical(0xFF7A59, 0xC2185B, 6, 58))
    t.disco(32, 32, 26, 0xFFFFFF, 0.08)
    t.traco([(27, 44), (27, 18), (44, 14), (44, 39)], 3.6, 0xFFFFFF)
    t.traco([(27, 22.5), (44, 18.5)], 3.6, 0xFFFFFF)
    t.disco(22.5, 44, 5.5, 0xFFFFFF, ry=4.6)
    t.disco(39.5, 39.5, 5.5, 0xFFFFFF, ry=4.6)


def energia(t):
    """Desligar: o símbolo de liga/desliga num disco vermelho."""
    t.disco(32, 32, 26, vertical(0xF0645A, 0xB82820, 6, 58))
    pts = [(32 + 14 * math.cos(a), 33 + 14 * math.sin(a)) for a in
           (-math.pi / 2 + 0.75 + k * (2 * math.pi - 1.5) / 30 for k in range(31))]
    t.traco(pts, 4.2, 0xFFFFFF)
    t.traco([(32, 13), (32, 30)], 4.2, 0xFFFFFF)


# ---- H88: ícones da interface (só no tamanho pequeno, para os programas) ----

def ui_relogio(t):
    t.disco(32, 32, 26, vertical(0x5AA9F0, 0x1F5FB8, 6, 58))
    t.disco(32, 32, 20, 0xFFFFFF)
    for k in range(12):
        a = k * math.pi / 6
        t.traco([(32 + 15 * math.cos(a), 32 + 15 * math.sin(a)), (32 + 18 * math.cos(a), 32 + 18 * math.sin(a))], 2, 0x7A8794)
    t.traco([(32, 32), (32, 18)], 3.4, 0x1E2328)
    t.traco([(32, 32), (42, 37)], 3.4, 0x1E2328)
    t.disco(32, 32, 3, 0xD8564B)


def ui_aparencia(t):
    t.disco(32, 32, 26, vertical(0xF4E2C4, 0xD9B98A, 6, 58))
    t.disco(40, 42, 7, 0xFFFFFF, 0.9)
    for (x, y, c) in ((20, 24, 0xE0483E), (32, 16, 0xF2B21C), (45, 22, 0x2E9E5B), (19, 39, 0x2A6FD8)):
        t.disco(x, y, 5.5, c)


def ui_som(t):
    t.poligono([(8, 24), (19, 24), (32, 12), (32, 52), (19, 40), (8, 40)], vertical(0x5A6674, 0x2B333C, 12, 52))
    for r in (11, 19):
        pts = [(30 + r * math.cos(a), 32 + r * math.sin(a)) for a in (-0.8 + k * 1.6 / 16 for k in range(17))]
        t.traco(pts, 3.6, 0x2A6FD8)


def ui_rede(t):
    t.disco(32, 32, 26, vertical(0x4FC27E, 0x13854A, 6, 58))
    t.traco([(9, 32), (55, 32)], 2.4, 0xFFFFFF, 0.85)
    t.traco([(32 + 10 * math.sin(k / 10 * math.pi), 7 + k * 2.5) for k in range(21)], 2.4, 0xFFFFFF, 0.85)
    t.traco([(32 - 10 * math.sin(k / 10 * math.pi), 7 + k * 2.5) for k in range(21)], 2.4, 0xFFFFFF, 0.85)
    t.traco([(32, 7), (32, 57)], 2.4, 0xFFFFFF, 0.85)
    t.traco([(13, 19), (51, 19)], 2.2, 0xFFFFFF, 0.6)
    t.traco([(13, 45), (51, 45)], 2.2, 0xFFFFFF, 0.6)


def ui_programas(t):
    cores = (0x2A6FD8, 0x2E9E5B, 0xF2B21C, 0xE0483E)
    for k, c in enumerate(cores):
        x, y = 7 + (k % 2) * 27, 7 + (k // 2) * 27
        t.ret(x, y, x + 23, y + 23, 6, c)


def ui_info(t):
    t.disco(32, 32, 26, vertical(0x5AA9F0, 0x1F5FB8, 6, 58))
    t.disco(32, 19, 4, 0xFFFFFF)
    t.ret(28, 27, 36, 48, 2.5, 0xFFFFFF)


def ui_teclado(t):
    t.ret(4, 16, 60, 50, 7, vertical(0x8A96A6, 0x4E5A6A, 16, 50))
    for linha, (y, n) in enumerate([(21, 7), (30, 7), (39, 6)]):
        x0 = 9 + linha * 2
        for k in range(n):
            t.ret(x0 + k * 7, y, x0 + k * 7 + 5, y + 6, 1.5, 0xFFFFFF)
    t.ret(17, 48, 47, 54, 2, 0xFFFFFF)


def ui_ia(t):
    def brilho(cx, cy, r, cor):
        pts = []
        for k in range(8):
            rr = r if k % 2 == 0 else r * 0.28
            a = -math.pi / 2 + k * math.pi / 4
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        t.poligono(pts, cor)
    brilho(28, 36, 22, vertical(0xB57BFF, 0x6A3BD8, 14, 58))
    brilho(48, 15, 10, vertical(0xFFD23E, 0xF2A21C, 5, 25))


def ui_pendrive(t):
    pendrive(t)


def ui_pasta(t):
    arquivos(t)


def ui_lixeira(t):
    t.ret(12, 12, 52, 19, 2.5, vertical(0x8794A2, 0x5A6674, 12, 19))
    t.ret(25, 6, 39, 13, 2, 0x5A6674)
    t.poligono([(15, 21), (49, 21), (46, 58), (18, 58)], vertical(0xB9C2CB, 0x7A8794, 21, 58))
    for x in (25, 32, 39):
        t.traco([(x, 27), (x, 52)], 2.6, 0x4B5560, 0.7)


def ui_voltar(t):
    t.traco([(48, 32), (16, 32)], 6, 0x3A4450)
    t.traco([(30, 17), (15, 32), (30, 47)], 6, 0x3A4450)


def ui_nova_pasta(t):
    arquivos(t)
    t.disco(47, 47, 12, 0x2E9E5B)
    t.traco([(47, 40), (47, 54)], 3.4, 0xFFFFFF)
    t.traco([(40, 47), (54, 47)], 3.4, 0xFFFFFF)


def ui_apagar(t):
    t.disco(32, 32, 26, vertical(0xF0645A, 0xB82820, 6, 58))
    t.traco([(22, 22), (42, 42)], 5, 0xFFFFFF)
    t.traco([(42, 22), (22, 42)], 5, 0xFFFFFF)


def ui_restaurar(t):
    pts = [(32 + 19 * math.cos(a), 34 + 19 * math.sin(a)) for a in
           (-math.pi * 0.95 + k * math.pi * 1.6 / 24 for k in range(25))]
    t.traco(pts, 5.5, 0x2E9E5B)
    t.poligono([(4, 26), (24, 26), (12, 42)], 0x2E9E5B)


def ui_tocar(t):
    t.poligono([(18, 10), (54, 32), (18, 54)], 0x2B333C)


def ui_pausar(t):
    t.ret(16, 10, 28, 54, 3, 0x2B333C)
    t.ret(36, 10, 48, 54, 3, 0x2B333C)


def ui_anterior(t):
    t.ret(10, 12, 17, 52, 2, 0x2B333C)
    t.poligono([(54, 12), (20, 32), (54, 52)], 0x2B333C)


def ui_proxima(t):
    t.ret(47, 12, 54, 52, 2, 0x2B333C)
    t.poligono([(10, 12), (44, 32), (10, 52)], 0x2B333C)


def ui_teste(t):
    t.disco(32, 32, 26, vertical(0x4FC27E, 0x13854A, 6, 58))
    t.traco([(19, 33), (28, 42), (45, 22)], 5.5, 0xFFFFFF)


def ui_relogio_rede(t):
    ui_rede(t)


# ---- H89: ícones do menu Iniciar ----

def lupa(t):
    """Busca: uma lupa branca (o menu pinta por cima do vidro escuro)."""
    t.traco([(27 + 12.5 * math.cos(a), 27 + 12.5 * math.sin(a)) for a in (k * 2 * math.pi / 40 for k in range(41))], 6, 0xFFFFFF)
    t.traco([(40, 40), (54, 54)], 8, 0xFFFFFF)


def chuva(t):
    """Chuva de letras: colunas verdes caindo numa tela preta."""
    t.ret(6, 6, 58, 58, 8, vertical(0x16201A, 0x050806, 6, 58))
    for k, (x, y0, y1) in enumerate([(16, 12, 40), (26, 18, 52), (36, 10, 30), (46, 20, 48)]):
        t.traco([(x, y0), (x, y1)], 3.4, 0x2FD566, 0.55)
        t.disco(x, y1, 2.6, 0xB8FFD0)


def triangulos(t):
    """Triângulos 3D: dois triângulos coloridos sobrepostos."""
    t.poligono([(8, 52), (30, 10), (52, 52)], vertical(0x5AA9F0, 0x1F5FB8, 10, 52))
    t.poligono([(24, 56), (42, 22), (58, 56)], vertical(0xFFB547, 0xE06A1B, 22, 56), 0.92)
    t.poligono([(30, 10), (36, 21), (24, 21)], 0xFFFFFF, 0.35)


def codigo(t):
    """Sagui Code: chaves de código num cartão roxo."""
    t.ret(5, 8, 59, 56, 10, vertical(0x8A6CF0, 0x4B2BB8, 8, 56))
    t.traco([(24, 20), (14, 32), (24, 44)], 4.2, 0xFFFFFF)
    t.traco([(40, 20), (50, 32), (40, 44)], 4.2, 0xFFFFFF)
    t.traco([(35, 18), (29, 46)], 3.4, 0xFFE07A)


def componentes(t):
    """Componentes: peças de interface (botão, caixa e marcação)."""
    t.ret(5, 8, 59, 56, 8, vertical(0xF4F6F8, 0xD6DDE3, 8, 56))
    t.ret(11, 14, 53, 24, 4, 0x0B7A3E)
    t.ret(11, 29, 53, 39, 3, 0xFFFFFF)
    t.ret(11, 29, 53, 39, 3, 0x8896A4, 0.25)
    t.ret(11, 44, 21, 52, 2, 0x5AA9F0)
    t.traco([(25, 48), (45, 48)], 3, 0x8896A4)


def tocador(t):
    """O Tocador: o botão de tocar e as ondas do som, em branco na caixa verde."""
    estilo_app(t, 0x55D654, 0x0E980C)
    tri = [(18, 20.5), (18, 41.5), (34, 31)]
    t.poligono(tri, BRANCO)
    t.traco(tri + tri[:1], 3, BRANCO)                     # arredonda as pontas
    for k, r in enumerate((6.5, 11, 15.5)):
        t.anel(30, CY, r, 3.0, BRANCO, -0.85 + k * 0.08, 0.85 - k * 0.08)


def saguicode(t):
    """O Sagui Code: o "</>" do código, em branco na caixa azul-anil, com um
    brilho amarelo no canto."""
    estilo_app(t, 0x4E8EF9, 0x1212CC)
    t.traco([(23, 23), (15, 31.5), (23, 40)], 4.4, BRANCO)          # <
    t.traco([(36, 20), (28, 43)], 4.4, BRANCO)                      # /
    t.traco([(41, 23), (49, 31.5), (41, 40)], 4.4, BRANCO)          # >
    t.poligono(estrela(47.5, 16, 4.4, 1.3, 4, 0), 0xFFDF4A)


def jogos(t):
    """Os Jogos: um controle de videogame branco, com a cruz e os botões, na
    caixa roxa."""
    fundo = estilo_app(t, 0x8A6CF5, 0x3A1E9E)
    sombra = 0x1E0E66
    corpo = [(16, 24)] + bezier((16, 24), (32, 21.5), (32, 21.5), (48, 24), n=8)[1:] + \
        bezier((48, 24), (54, 25), (55, 41), (50, 43), n=12)[1:] + \
        bezier((50, 43), (46, 45), (42, 38), (32, 38), n=10)[1:] + \
        bezier((32, 38), (22, 38), (18, 45), (14, 43), n=10)[1:] + \
        bezier((14, 43), (9, 41), (10, 25), (16, 24), n=12)[1:]
    t.poligono([(x + 0.5, y + 2) for x, y in corpo], sombra, 0.3)
    t.poligono(corpo, BRANCO)
    t.ret(17.5, 29.5, 27.5, 33, 1, fundo)                  # a cruz
    t.ret(20.75, 26.25, 24.25, 36.25, 1, fundo)
    t.disco(41.5, 28.5, 2.4, 0xFFC928)                     # os botões
    t.disco(45.5, 32, 2.4, fundo)
    t.disco(37.5, 32, 2.4, fundo)
    t.disco(41.5, 35.5, 2.4, fundo)


def cacabananas(t):
    """O Caça-Bananas: uma banana-bomba, com o pavio aceso, na caixa vermelha
    (de perigo; um vermelho mais fechado que o dos Jogos)."""
    estilo_app(t, 0xF0564A, 0xA8121E)
    desenhar_banana(t, 0, 0.78, -2, 2)
    # o pavio sai do cabinho (que, nesta escala, fica em 38,8 x 21,6)
    pavio = bezier((38.8, 21.4), (39.5, 16), (43, 19.5), (44.5, 16.5), n=10)
    t.traco(pavio, 1.8, 0x3A2A16)
    # a faísca: uma estrela laranja com o miolo claro
    fx, fy = pavio[-1]
    t.poligono(estrela(fx, fy, 6, 2.3, 8), 0xFFB21E)
    t.poligono(estrela(fx, fy, 4, 1.6, 4, 0.3), 0xFFE65A)
    t.disco(fx, fy, 1.3, 0xFFFFFF)


def paciencia(t):
    """A Paciência Banana: duas cartas, a da frente com um espadas, na caixa
    azul."""
    estilo_app(t, 0x4FA6F8, 0x1A56C0)
    atras = girar([(16, 14), (36, 14), (36, 44), (16, 44)], 26, 29, math.radians(-14))
    t.poligono(atras, 0xD9E6F4)
    t.traco(atras + atras[:1], 2.4, 0xD9E6F4)
    dentro = girar([(18.5, 16.5), (33.5, 16.5), (33.5, 41.5), (18.5, 41.5)], 26, 29, math.radians(-14))
    t.poligono(dentro, vertical(0x3E88E0, 0x2A62B8, 14, 44), 0.55)

    def m(pts):
        return girar(pts, 37, 33, math.radians(8))
    frente = m([(26, 17), (48, 17), (48, 49), (26, 49)])
    t.poligono(m([(27, 19), (49, 19), (49, 50.5), (27, 50.5)]), 0x0E3A80, 0.35)
    t.poligono(frente, 0xFFFFFF)
    t.traco(frente + frente[:1], 2.4, 0xFFFFFF)
    # espadas: um coração de cabeça para baixo e o pezinho
    preto = 0x1E2430
    ex, ey = 37.5, 31.5
    t.poligono(m([(ex, ey - 7.5), (ex - 6.2, ey + 0.5), (ex + 6.2, ey + 0.5)]), preto)
    for lado in (-1, 1):
        (x, y), = m([(ex + lado * 3.2, ey + 1.2)])
        t.disco(x, y, 3.4, preto)
    t.poligono(m([(ex, ey + 1), (ex - 3, ey + 7.5), (ex + 3, ey + 7.5)]), preto)


def chip8(t):
    """O emulador de CHIP-8: um monitor antigo de fósforo verde, com um
    foguete de pixels grandes na tela, na caixa preta."""
    estilo_app(t, 0x2A2A2A, 0x050505)
    verde = 0x3CFF6E
    # o monitor: a moldura verde, a tela escura dentro, o pescoço e a base
    t.ret(13.5, 13.5, 50.5, 42, 5, verde)
    t.ret(16, 16, 48, 39.5, 3, vertical(0x0C2414, 0x04100A, 16, 40))
    t.ret(29, 42, 35, 45.5, 0, misturar(verde, 0x000000, 0.25))
    t.ret(22, 45, 42, 48.5, 1.6, verde)
    # o foguete (5 x 7 pixels): bico, corpo com janela, aletas e o fogo
    desenho = ["..#..", ".###.", ".#.#.", ".###.", ".###.", "#####", "#.#.#"]
    passo, lado = 3.0, 2.6
    x0 = CX - (5 * passo - (passo - lado)) / 2
    y0 = 27.75 - (7 * passo - (passo - lado)) / 2
    for lin, fila in enumerate(desenho):
        for col, c in enumerate(fila):
            if c == "#":
                x, y = x0 + col * passo, y0 + lin * passo
                t.ret(x - 0.8, y - 0.8, x + lado + 0.8, y + lado + 0.8, 1, verde, 0.22)   # o brilho do fósforo
                t.ret(x, y, x + lado, y + lado, 0.3, verde)
    for k in range(12):                                    # as linhas da varredura, bem de leve
        y = 17 + k * 2
        t.traco([(16.5, y), (47.5, y)], 0.6, 0x000000, 0.22)


def jclic(t):
    """As atividades JClic: uma peça de quebra-cabeça branca na caixa laranja."""
    fundo = estilo_app(t, 0xFFA24A, 0xE9600F)
    x0, y0, x1, y1 = 15.5, 22.5, 40.5, 47.5
    t.ret(x0 + 0.8, y0 + 1.5, x1 + 0.8, y1 + 1.5, 2.5, 0x8A3200, 0.3)
    t.ret(x0, y0, x1, y1, 2.5, BRANCO)
    t.disco((x0 + x1) / 2, y0 - 2.6, 4.6, BRANCO)          # o encaixe de cima, para fora
    t.disco(x1 + 2.6, (y0 + y1) / 2, 4.6, BRANCO)          # o da direita, para fora
    t.disco(x0, (y0 + y1) / 2, 4.2, fundo)                 # o da esquerda, para dentro
    t.disco((x0 + x1) / 2, y1, 4.2, fundo)                 # o de baixo, para dentro


ICONES = [("arquivos", arquivos), ("terminal", terminal), ("editor", editor), ("calculadora", calculadora),
          ("banana", banana), ("sagui3d", sagui3d), ("sobre", sobre), ("pendrive", pendrive),
          ("configuracoes", configuracoes), ("cipo", cipo), ("instalado", instalado), ("tocador", tocador),
          ("saguicode", saguicode), ("jogos", jogos), ("cacabananas", cacabananas), ("paciencia", paciencia),
          ("chip8", chip8), ("jclic", jclic),
          ("monitor", monitor), ("visualizador", visualizador), ("musica", musica), ("energia", energia),
          ("lupa", lupa), ("chuva", chuva), ("triangulos", triangulos), ("codigo", codigo),
          ("componentes", componentes)]
TAMANHOS = [(64, ""), (40, "_40"), (20, "_20")]
# Os programas recebem só o tamanho 20 (libsagui, icones.h): os ícones dos
# programas e os da interface, com o prefixo icone_.
INTERFACE = [("relogio", ui_relogio), ("aparencia", ui_aparencia), ("som", ui_som), ("rede", ui_rede),
             ("programas", ui_programas), ("info", ui_info), ("ia", ui_ia), ("teclado", ui_teclado), ("lixeira", ui_lixeira),
             ("voltar", ui_voltar), ("nova_pasta", ui_nova_pasta), ("apagar", ui_apagar),
             ("restaurar", ui_restaurar), ("tocar", ui_tocar), ("pausar", ui_pausar),
             ("anterior", ui_anterior), ("proxima", ui_proxima), ("teste", ui_teste)]



def escrever(f, nome, desenhar):
    t = Tela()
    desenhar(t)
    px = t.com_sombra()
    f.write(f"const uint32_t {nome}[{T * T}] = {{\n")
    for k in range(0, len(px), 8):
        f.write("    " + ", ".join(f"0x{v:08X}" for v in px[k:k + 8]) + ",\n")
    f.write("};\n\n")


def gerar(desenhar, tam):
    """Desenha um ícone no tamanho dado e devolve os pixels ARGB (com sombra)."""
    global T
    T = tam
    t = Tela()
    desenhar(t)
    return t.com_sombra()


def main():
    global T
    if len(sys.argv) == 3 and sys.argv[1] == "--programas":
        T = 20
        with open(sys.argv[2], "w", encoding="utf-8") as f:
            f.write("/* Gerado por tools/mkarte.py --programas. Não edite. */\n#include <stdint.h>\n\n")
            for nome, desenhar in ICONES + INTERFACE:
                escrever(f, "icone_" + nome, desenhar)
        return
    if len(sys.argv) != 2:
        sys.exit("uso: mkarte.py <saida.c> | mkarte.py --programas <saida.c>")
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        f.write("/* Gerado por tools/mkarte.py. Não edite. */\n#include \"arte.h\"\n\n")
        for tam, sufixo in TAMANHOS:
            T = tam
            for nome, desenhar in ICONES:
                escrever(f, f"arte_px_{nome}{sufixo}", desenhar)


if __name__ == "__main__":
    main()
