#!/usr/bin/env python3
"""
mkarte.py - gera os ícones do SaguiOS (64x64 na área de trabalho, 40x40 na doca).

Uso: mkarte.py <saida.c>

Escrito pelo Robson de Lima Barbosa (pull request #1) e trazido para a 2.0
(H81), com o segundo tamanho e os ícones novos da 2.0 (Configurações, Cipó,
programa instalado).

Cada ícone é descrito aqui com formas simples (retângulo arredondado, círculo,
polígono, traço), sempre numa grade de 64x64. O script calcula a cobertura de
cada pixel com 4x4 amostras, o que suaviza as bordas, acrescenta uma sombra e
escreve vetores ARGB em C. Para o tamanho menor, as mesmas formas são
amostradas numa grade mais grossa (cada pixel cobre 64/40 da grade).
Nenhuma imagem pronta entra no projeto: os desenhos são estes números.

O padrão para um ícone novo: uma função que desenha na grade de 64, com uma
margem de uns 4 pixels (a sombra precisa dela), cores com um degradê de cima
para baixo e um contorno claro só onde ajuda a ler; depois, uma linha em
ICONES. O C gerado tem arte_px_<nome> (64) e arte_px_<nome>_40.
"""
import math
import sys

GRADE = 64     # os desenhos são feitos nesta grade
T = 64         # tamanho da saída (muda para 40 na segunda passada)
AMOSTRAS = [(i + 0.5) / 4 for i in range(4)]


def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def vertical(c0, c1, y0, y1):
    a, b = rgb(c0), rgb(c1)

    def cor(_x, y):
        t = min(1, max(0, (y - y0) / (y1 - y0)))
        return tuple(a[k] + (b[k] - a[k]) * t for k in range(3))
    return cor


class Tela:
    def __init__(self):
        self.p = [[(0.0, 0.0, 0.0, 0.0)] * T for _ in range(T)]

    def forma(self, dentro, caixa, cor, opacidade=1.0):
        if isinstance(cor, int):
            fixa = rgb(cor)

            def cor(_x, _y, fixa=fixa):
                return fixa
        e = GRADE / T                      # quanto da grade cada pixel da saída cobre
        x0, y0, x1, y1 = (v / e for v in caixa)
        for y in range(max(0, int(y0) - 1), min(T, int(y1) + 2)):
            for x in range(max(0, int(x0) - 1), min(T, int(x1) + 2)):
                n = sum(dentro((x + i) * e, (y + j) * e) for i in AMOSTRAS for j in AMOSTRAS)
                if not n:
                    continue
                a = n / 16 * opacidade
                c = cor((x + 0.5) * e, (y + 0.5) * e)
                r, g, b, f = self.p[y][x]
                saida = a + f * (1 - a)
                self.p[y][x] = tuple((c[k] * a + (r, g, b)[k] * f * (1 - a)) / saida for k in range(3)) + (saida,)

    def ret(self, x0, y0, x1, y1, raio, cor, opacidade=1.0):
        def dentro(x, y):
            dx = max(x0 + raio - x, 0, x - (x1 - raio))
            dy = max(y0 + raio - y, 0, y - (y1 - raio))
            return x0 <= x <= x1 and y0 <= y <= y1 and dx * dx + dy * dy <= raio * raio
        self.forma(dentro, (x0, y0, x1, y1), cor, opacidade)

    def disco(self, cx, cy, r, cor, opacidade=1.0, ry=None):
        ry = ry or r
        self.forma(lambda x, y: ((x - cx) / r) ** 2 + ((y - cy) / ry) ** 2 <= 1,
                   (cx - r, cy - ry, cx + r, cy + ry), cor, opacidade)

    def poligono(self, pts, cor, opacidade=1.0):
        def dentro(x, y):
            d = False
            for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]):
                if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
                    d = not d
            return d
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.forma(dentro, (min(xs), min(ys), max(xs), max(ys)), cor, opacidade)

    def traco(self, pts, largura, cor, opacidade=1.0):
        r = largura / 2

        def dentro(x, y):
            for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                dx, dy = bx - ax, by - ay
                t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy or 1)))
                if (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2 <= r * r:
                    return True
            return False
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.forma(dentro, (min(xs) - r, min(ys) - r, max(xs) + r, max(ys) + r), cor, opacidade)

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


def arquivos(t):
    t.ret(5, 12, 30, 28, 4, 0xC98A12)
    t.ret(5, 18, 59, 52, 5, 0xD9A11E)
    t.ret(11, 15, 53, 34, 2, 0xFFFDF2)
    t.ret(5, 24, 59, 54, 5, vertical(0xFFE066, 0xF2B21C, 24, 54))
    t.ret(5, 24, 59, 25.2, 1, 0xFFF2B0)


def terminal(t):
    t.ret(5, 9, 59, 47, 6, vertical(0x56616E, 0x323A44, 9, 47))
    t.ret(9, 13, 55, 43, 3, vertical(0x10261C, 0x0B1712, 13, 43))
    t.traco([(15, 20), (24, 28), (15, 36)], 3.6, 0x42E584)
    t.traco([(29, 37), (42, 37)], 3.6, 0xFFDF4A)
    t.ret(27, 47, 37, 53, 0, 0x3C4550)
    t.ret(17, 53, 47, 58, 2.5, vertical(0x6A7582, 0x444D58, 53, 58))


def editor(t):
    t.poligono([(13, 5), (41, 5), (52, 16), (52, 59), (13, 59)], vertical(0xFFFFFF, 0xE6EAEE, 5, 59))
    t.poligono([(41, 5), (52, 16), (41, 16)], 0xB9C2CB)
    for k, fim in enumerate((37, 44, 44, 34, 40)):
        t.traco([(20, 24 + k * 6.5), (fim, 24 + k * 6.5)], 2.2, 0x8795A3 if k else 0x0B7A3E)
    t.traco([(34, 51), (55, 28)], 8, 0xF2B21C)
    t.traco([(51, 32.5), (55, 28)], 8, 0xD8564B)
    t.poligono([(28.5, 57), (31, 48), (37.5, 54)], 0xF4D9B0)
    t.poligono([(28.5, 57), (29.5, 53.3), (32.5, 56)], 0x2A2F35)


def calculadora(t):
    t.ret(11, 4, 53, 60, 7, vertical(0x4B5764, 0x2B333C, 4, 60))
    t.ret(16, 10, 48, 23, 3, vertical(0xB7F5CC, 0x7FE2A4, 10, 23))
    t.ret(36, 15, 45, 18, 1, 0x0B5C30)
    cores = [0xDDE3E8, 0xDDE3E8, 0xDDE3E8, 0xF2B21C]
    for lin in range(4):
        for col in range(4):
            x, y = 16 + col * 8.3, 28 + lin * 7.7
            c = 0x2A6FD8 if lin == 3 and col == 3 else cores[col]
            t.ret(x, y, x + 6.6, y + 6, 1.6, c)


def banana(t):
    def curva(r, a0, a1, n=28):
        return [(46 - r * math.cos(a), 11 + r * math.sin(a)) for a in
                (a0 + (a1 - a0) * k / n for k in range(n + 1))]
    externa = curva(44, -0.12, 1.5)
    interna = curva(29, 1.42, 0.02)
    t.poligono(externa + interna, vertical(0xFFE35A, 0xF2B21C, 10, 56))
    t.traco(curva(35, 0.02, 1.42), 1.6, 0xD99A10, 0.75)
    t.traco(curva(30, 0.12, 1.36), 1.8, 0xFFF3A8, 0.8)
    t.traco([(9, 7), (14, 14)], 6, 0x7A5A1E)
    t.traco([(42, 54.5), (47, 53)], 4, 0x4C3A18)


def sagui3d(t):
    t.ret(5, 5, 59, 59, 7, vertical(0x8FD8F0, 0x56B3D8, 5, 34))
    t.poligono([(5, 59), (59, 59), (38, 34), (26, 34)], vertical(0x1B9A52, 0x0B6B38, 34, 59))
    t.poligono([(5, 8), (26, 22), (26, 34), (5, 59)], vertical(0x8D887A, 0x5D594F, 8, 59))
    t.poligono([(59, 8), (38, 22), (38, 34), (59, 59)], vertical(0x777267, 0x4F4B43, 8, 59))
    t.ret(26, 22, 38, 34, 0, 0x2C2A26)
    t.traco([(19, 51), (25, 54), (31, 52)], 3, 0xFFD52E)
    t.traco([(32, 43), (32, 31)], 2.2, 0x6B4A2A)
    t.traco([(26, 26), (32, 32), (38, 26)], 2.2, 0x6B4A2A)
    t.ret(5, 5, 59, 59, 7, 0xFFFFFF, 0.06)


def sobre(t):
    t.disco(13, 27, 11, 0xF5F5F0)
    t.disco(51, 27, 11, 0xF5F5F0)
    t.disco(32, 31, 23, vertical(0x4A382B, 0x2D211A, 8, 54))
    t.disco(32, 12, 6, 0xF5F5F0, ry=4.5)
    t.disco(32, 35, 16, vertical(0xC08E78, 0x9D6E5A, 19, 51), ry=14)
    t.disco(25, 32, 4.2, 0x17120F)
    t.disco(39, 32, 4.2, 0x17120F)
    t.disco(26.3, 30.6, 1.3, 0xFFFFFF)
    t.disco(40.3, 30.6, 1.3, 0xFFFFFF)
    t.disco(32, 39, 2.2, 0x3C2D23, ry=1.5)
    t.traco([(27, 44), (32, 46), (37, 44)], 1.8, 0x3C2D23)


def pendrive(t):
    t.ret(23, 3, 41, 18, 2, vertical(0xE6EAEE, 0xAEB7C0, 3, 18))
    t.ret(27, 7, 31, 11, 0, 0x4B5560)
    t.ret(33, 7, 37, 11, 0, 0x4B5560)
    t.ret(17, 16, 47, 60, 6, vertical(0x3E8BF0, 0x1A4FA8, 16, 60))
    t.ret(21, 20, 25, 56, 2, 0xFFFFFF, 0.18)
    t.disco(32, 42, 7, 0xFFDF4A)
    t.disco(32, 42, 3, 0x1A4FA8)


def configuracoes(t):
    """Engrenagem: um disco com oito dentes e um furo, em cinza azulado."""
    cor = vertical(0x9AA6B4, 0x5A6674, 6, 58)
    for k in range(8):
        a = k * math.pi / 4
        c, s_ = math.cos(a), math.sin(a)
        # cada dente é um quadrilátero que sai do centro
        pts = [(32 + c * 18 - s_ * 5, 32 + s_ * 18 + c * 5), (32 + c * 27 - s_ * 4, 32 + s_ * 27 + c * 4),
               (32 + c * 27 + s_ * 4, 32 + s_ * 27 - c * 4), (32 + c * 18 + s_ * 5, 32 + s_ * 18 - c * 5)]
        t.poligono(pts, cor)
    t.disco(32, 32, 21, cor)
    t.disco(32, 32, 15, vertical(0xC4CDD6, 0x8794A2, 17, 47))
    t.disco(32, 32, 7, 0x2B333C)
    t.disco(32, 32, 7, 0xFFFFFF, 0.08)


def cipo(t):
    """O navegador Cipó: um globo azul abraçado por um cipó verde com folhas."""
    t.disco(32, 32, 25, vertical(0x5FB6F2, 0x1C5FB8, 7, 57))
    for dy in (-12, 0, 12):                              # paralelos
        r = math.sqrt(max(0, 25 * 25 - dy * dy))
        t.traco([(32 - r + 2, 32 + dy), (32 + r - 2, 32 + dy)], 1.4, 0xFFFFFF, 0.35)
    t.traco([(32 + 11 * math.sin(a / 10), 7 + a * 50 / 31) for a in range(32)], 1.4, 0xFFFFFF, 0.35)
    t.traco([(32 - 11 * math.sin(a / 10), 7 + a * 50 / 31) for a in range(32)], 1.4, 0xFFFFFF, 0.35)
    # o cipó: uma curva que dá a volta e três folhas
    pts = [(6 + k * 52 / 30, 48 - 30 * math.sin(k * math.pi / 30) + 6 * math.sin(k / 3)) for k in range(31)]
    t.traco(pts, 3.4, 0x2E8B3A)
    for (fx, fy, ang) in ((16, 30, -0.9), (34, 20, 0.2), (50, 30, 0.9)):
        c, s_ = math.cos(ang), math.sin(ang)
        folha = [(fx + c * u * 9 - s_ * v * 4, fy + s_ * u * 9 + c * v * 4) for (u, v) in
                 ((0, 0), (0.3, -0.8), (0.7, -0.7), (1, 0), (0.7, 0.7), (0.3, 0.8))]
        t.poligono(folha, vertical(0x6BD66B, 0x2E8B3A, fy - 8, fy + 8))


def instalado(t):
    """Programa instalado: uma janela clara com uma estrela amarela."""
    t.ret(6, 9, 58, 55, 6, vertical(0xF4F6F8, 0xD8DEE4, 9, 55))
    t.ret(6, 9, 58, 20, 6, vertical(0x18A65A, 0x0A6E38, 9, 20))
    t.ret(6, 15, 58, 20, 0, 0x0A6E38)
    t.disco(50, 14.5, 2.6, 0xFFFFFF, 0.85)
    estrela = []
    for k in range(10):
        r = 13 if k % 2 == 0 else 5.5
        a = -math.pi / 2 + k * math.pi / 5
        estrela.append((32 + r * math.cos(a), 38 + r * math.sin(a)))
    t.poligono(estrela, vertical(0xFFE35A, 0xF2B21C, 25, 51))


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


ICONES = [("arquivos", arquivos), ("terminal", terminal), ("editor", editor), ("calculadora", calculadora),
          ("banana", banana), ("sagui3d", sagui3d), ("sobre", sobre), ("pendrive", pendrive),
          ("configuracoes", configuracoes), ("cipo", cipo), ("instalado", instalado),
          ("monitor", monitor), ("visualizador", visualizador), ("musica", musica), ("energia", energia)]
TAMANHOS = [(64, ""), (40, "_40"), (20, "_20")]
# Os programas recebem só o tamanho 20 (libsagui, icones.h): os ícones dos
# programas e os da interface, com o prefixo icone_.
INTERFACE = [("relogio", ui_relogio), ("aparencia", ui_aparencia), ("som", ui_som), ("rede", ui_rede),
             ("programas", ui_programas), ("info", ui_info), ("ia", ui_ia), ("lixeira", ui_lixeira),
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
