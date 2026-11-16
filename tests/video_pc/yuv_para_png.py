#!/usr/bin/env python3
"""Converte um quadro I420 gravado pelo video_pc (teste H88) em PNG, para
uma pessoa olhar: python3 yuv_para_png.py quadro.yuv largura altura saida.png

A conta é a do /bin/video (BT.601, faixa limitada), com o Pillow."""
import sys

from PIL import Image


def converter(yuv, larg, alt, png):
    d = open(yuv, "rb").read()
    lc, ac = larg // 2, alt // 2
    y = Image.frombytes("L", (larg, alt), d[:larg * alt])
    u = Image.frombytes("L", (lc, ac), d[larg * alt:larg * alt + lc * ac]).resize((larg, alt), Image.NEAREST)
    v = Image.frombytes("L", (lc, ac), d[larg * alt + lc * ac:larg * alt + 2 * lc * ac]).resize((larg, alt), Image.NEAREST)
    # o "YCbCr" do Pillow é de faixa completa: estica o Y de 16..235 e a croma de 16..240
    y = y.point(lambda p: max(0, min(255, (p - 16) * 255 // 219)))
    u = u.point(lambda p: max(0, min(255, (p - 128) * 255 // 224 + 128)))
    v = v.point(lambda p: max(0, min(255, (p - 128) * 255 // 224 + 128)))
    Image.merge("YCbCr", (y, u, v)).convert("RGB").save(png)


if __name__ == "__main__":
    converter(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4])
