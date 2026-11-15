#!/usr/bin/env python3
"""Ferramentas de apoio do teste do MP3 (H86), do lado do PC.

O decodificador de MP3 do SaguiOS (userland/tocador/mp3.c) foi escrito do
zero, pela norma. Para saber se ele está certo, o teste precisa de duas coisas
que NÃO fazem parte do sistema (são só ferramentas de teste, como o QEMU):

  - um codificador, para fabricar arquivos MP3 de teste: a libmp3lame do PC;
  - um decodificador de referência, para comparar o resultado: a libmpg123.

As duas são usadas pelo ctypes (o PC de build tem as bibliotecas, mas não os
comandos "lame" e "mpg123"). Se faltarem, as funções devolvem None e o teste
avisa que a decodificação de verdade não foi conferida.

Aqui também fica a conferência das tabelas de Huffman de mp3_tabelas.h.
"""
import ctypes
import ctypes.util
import math
import os
import re
import struct
import subprocess
from fractions import Fraction

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(AQUI)


def _biblioteca(nomes):
    for n in nomes:
        try:
            return ctypes.CDLL(n)
        except OSError:
            pass
    return None


def lame():
    return _biblioteca(["libmp3lame.so.0", ctypes.util.find_library("mp3lame") or "libmp3lame.so"])


def mpg123():
    return _biblioteca(["libmpg123.so.0", ctypes.util.find_library("mpg123") or "libmpg123.so"])


def codificar(amostras, taxa, canais, kbps=128, vbr=None, modo=None, qualidade=5, reserva=True, xing=True, crc=False):
    """Codifica PCM de 16 bits (lista intercalada se estéreo) em MP3 com a
    libmp3lame. vbr: None (taxa fixa) ou a qualidade de 0 a 9. modo: 0
    estéreo, 1 estéreo conjunto, 3 mono. Devolve os bytes, com o quadro
    Xing/Info no começo (como o comando lame faz), ou None sem a biblioteca.
    reserva=False desliga a reserva de bits (cada quadro fica independente);
    xing=False tira o quadro Xing/Info; crc=True põe o CRC em cada quadro."""
    L = lame()
    if L is None:
        return None
    L.lame_init.restype = ctypes.c_void_p
    g = ctypes.c_void_p(L.lame_init())
    L.lame_set_in_samplerate(g, taxa)
    L.lame_set_out_samplerate(g, taxa)      # sem isso o lame baixa a taxa nas taxas de bits pequenas
    L.lame_set_num_channels(g, canais)
    L.lame_set_quality(g, qualidade)
    if modo is None:
        modo = 3 if canais == 1 else 1
    L.lame_set_mode(g, modo)
    if vbr is None:
        L.lame_set_VBR(g, 0)
        L.lame_set_brate(g, kbps)
    else:
        L.lame_set_VBR(g, 4)            # vbr_mtrh, o "-V" do lame
        L.lame_set_VBR_q(g, vbr)
    L.lame_set_bWriteVbrTag(g, 1 if xing else 0)
    L.lame_set_disable_reservoir(g, 0 if reserva else 1)
    L.lame_set_error_protection(g, 1 if crc else 0)
    if L.lame_init_params(g) < 0:
        raise RuntimeError("lame_init_params falhou")
    n = len(amostras) // canais
    pcm = (ctypes.c_short * len(amostras))(*amostras)
    cap = int(1.25 * n) + 7200
    buf = (ctypes.c_ubyte * cap)()
    if canais == 2:
        t = L.lame_encode_buffer_interleaved(g, pcm, n, buf, cap)
    else:
        t = L.lame_encode_buffer(g, pcm, pcm, n, buf, cap)
    if t < 0:
        raise RuntimeError("lame_encode_buffer falhou")
    saida = bytes(buf[:t])
    t = L.lame_encode_flush(g, buf, cap)
    saida += bytes(buf[:t])
    # O quadro Xing/Info: o lame reserva o primeiro quadro e o preenche no fim.
    L.lame_get_lametag_frame.restype = ctypes.c_size_t
    t = L.lame_get_lametag_frame(g, buf, ctypes.c_size_t(cap))
    if xing and 0 < t <= len(saida):
        saida = bytes(buf[:t]) + saida[t:]
    L.lame_close(g)
    return saida


def decodificar_referencia(mp3):
    """Decodifica com a libmpg123 (sem cortar o atraso do começo, para dar as
    mesmas amostras que um decodificador simples). Devolve (taxa, canais,
    amostras de 16 bits intercaladas) ou None sem a biblioteca."""
    M = mpg123()
    if M is None:
        return None
    M.mpg123_init()
    M.mpg123_new.restype = ctypes.c_void_p
    h = ctypes.c_void_p(M.mpg123_new(None, None))
    M.mpg123_param(h, 2, 0x40, ctypes.c_double(0))       # MPG123_REMOVE_FLAGS, MPG123_GAPLESS
    M.mpg123_param(h, 1, 0x20, ctypes.c_double(0))       # MPG123_ADD_FLAGS, MPG123_QUIET
    M.mpg123_format_none(h)
    for t in (8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000):
        M.mpg123_format(h, ctypes.c_long(t), 3, 0xD0)    # mono ou estéreo, 16 bits com sinal
    M.mpg123_open_feed(h)
    saida = bytearray()
    buf = (ctypes.c_ubyte * (1 << 20))()
    feito = ctypes.c_size_t(0)
    r = M.mpg123_decode(h, mp3, ctypes.c_size_t(len(mp3)), buf, ctypes.c_size_t(len(buf)), ctypes.byref(feito))
    taxa, canais, cod = ctypes.c_long(0), ctypes.c_int(0), ctypes.c_int(0)
    while True:
        saida += bytes(buf[:feito.value])
        if r == -11:                                     # MPG123_NEW_FORMAT
            M.mpg123_getformat(h, ctypes.byref(taxa), ctypes.byref(canais), ctypes.byref(cod))
        elif r != 0:                                     # MPG123_NEED_MORE (-10), fim ou erro
            break
        r = M.mpg123_decode(h, None, ctypes.c_size_t(0), buf, ctypes.c_size_t(len(buf)), ctypes.byref(feito))
    M.mpg123_delete(h)
    return taxa.value, canais.value, list(struct.unpack("<%dh" % (len(saida) // 2), bytes(saida[:len(saida) // 2 * 2])))


def id3v2(tamanho=1000):
    """Uma etiqueta ID3v2.3 com um título e enchimento, para pôr na frente."""
    texto = b"\x00Teste do SaguiOS"
    quadro = b"TIT2" + struct.pack(">I", len(texto)) + b"\x00\x00" + texto
    corpo = quadro + bytes(tamanho - len(quadro))
    t = len(corpo)
    seguro = bytes([(t >> 21) & 127, (t >> 14) & 127, (t >> 7) & 127, t & 127])
    return b"ID3\x03\x00\x00" + seguro + corpo


def id3v1():
    return (b"TAG" + b"Teste".ljust(30, b"\x00") + bytes(95))[:128]


def seno(freq, taxa, dur, amp=12000):
    return [int(amp * math.sin(2 * math.pi * freq * i / taxa)) for i in range(int(taxa * dur))]


def musica(taxa, dur, semente=7):
    """Um sinal estéreo "parecido com música": notas com harmônicos e ataques
    bruscos (que fazem o codificador usar blocos curtos), um pouco diferentes
    em cada lado, e ruído. Devolve a lista intercalada."""
    estado = semente

    def acaso():
        nonlocal estado
        estado = (estado * 1103515245 + 12345) & 0x7FFFFFFF
        return estado / 0x7FFFFFFF - 0.5

    notas = [262, 330, 392, 523, 440, 349, 294, 494, 587, 196]
    n = int(taxa * dur)
    saida = []
    passo = taxa // 5
    for i in range(n):
        k = i // passo
        t = (i % passo) / taxa
        f = notas[k % len(notas)]
        env = math.exp(-6 * t)
        base = env * (math.sin(2 * math.pi * f * i / taxa) + 0.5 * math.sin(2 * math.pi * 2 * f * i / taxa)
                      + 0.3 * math.sin(2 * math.pi * 3.01 * f * i / taxa))
        estalo = acaso() * 2.5 * math.exp(-300 * t)                # o "ataque" de cada nota
        agudo = 0.2 * math.sin(2 * math.pi * (f * 7.3) * i / taxa) * env
        chiado = 0.05 * acaso()
        e = base + estalo + chiado + (agudo if k % 2 else 0)
        d = 0.8 * base + estalo * (0.5 if k % 3 else -1) + chiado + (0 if k % 2 else agudo)
        saida.append(max(-32768, min(32767, int(9000 * e))))
        saida.append(max(-32768, min(32767, int(9000 * d))))
    return saida


class _Bits:
    """Leitor e escritor de bits (o bit mais alto primeiro), para montar_intensidade."""

    def __init__(self, dados=b""):
        self.d, self.pos, self.saida = dados, 0, []

    def ler(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.pos >> 3] >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def por(self, valor, n):
        self.saida.extend((valor >> (n - 1 - i)) & 1 for i in range(n))

    def bytes(self, tamanho):
        bits = self.saida + [0] * (tamanho * 8 - len(self.saida))
        return bytes(sum(bits[i + k] << (7 - k) for k in range(8)) for i in range(0, tamanho * 8, 8))


def montar_intensidade(mono, com_ms=False, escala=0):
    """Fabrica um MP3 com ESTÉREO DE INTENSIDADE, que a libmp3lame não sabe
    fazer. Parte de um MP3 mono sem reserva de bits e sem quadro Xing: cada
    quadro vira um quadro de estéreo conjunto com o dobro da taxa de bits, em
    que o canal esquerdo é o mono original, bit a bit, e o direito não tem
    nenhum valor (big_values = 0), só "fatores de escala" de 3 bits, que no
    estéreo de intensidade são as posições (is_pos) de cada faixa: aqui, uma
    sequência variada de 0 a 7 (7 é a posição "ilegal"). Serve para o MPEG-1 e
    para o MPEG-2 (onde escala é o bit intensity_scale)."""
    saida = bytearray()
    p, semente = 0, 12345
    while p + 4 <= len(mono):
        cab = mono[p:p + 4]
        if cab[0] != 0xFF or (cab[1] & 0xE0) != 0xE0:
            p += 1
            continue
        v1 = (cab[1] >> 3) & 3 == 3
        ib, it = cab[2] >> 4, (cab[2] >> 2) & 3
        taxa = [44100, 48000, 32000][it] // (1 if v1 else 2)
        tab = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320] if v1 else \
              [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]
        tam = (144000 if v1 else 72000) * tab[ib] // taxa + ((cab[2] >> 1) & 1)
        assert cab[3] >> 6 == 3 and cab[1] & 1, "precisa ser mono e sem CRC"
        lat = _Bits(mono[p + 4:p + 4 + (17 if v1 else 9)])
        assert lat.ler(9 if v1 else 8) == 0, "o MP3 de origem tem reserva de bits"
        lat.ler(5 if v1 else 1)
        scfsi = lat.ler(4) if v1 else 0
        dados = _Bits(mono[p + 4 + (17 if v1 else 9):p + tam])
        novo, corpo = _Bits(), _Bits()
        novo.por(0, 9 if v1 else 8)
        novo.por(0, 3 if v1 else 2)
        if v1:
            novo.por(scfsi, 4)
            novo.por(0, 4)
        for _ in range(2 if v1 else 1):
            bits, grandes, ganho = lat.ler(12), lat.ler(9), lat.ler(8)
            compr = lat.ler(4 if v1 else 9)
            janela = lat.ler(23)                    # window_switching_flag e os 22 bits seguintes
            resto = lat.ler(3 if v1 else 2)
            curto = (janela >> 22) & 1 and (janela >> 20) & 3 == 2
            for campo, n in ((bits, 12), (grandes, 9), (ganho, 8), (compr, 4 if v1 else 9), (janela, 23), (resto, 3 if v1 else 2)):
                novo.por(campo, n)
            corpo.por(dados.ler(bits), bits) if bits else None
            # O canal direito: 21 posições (blocos longos) ou 36 (curtos), de 3 bits.
            n_pos = 36 if curto else 21
            novo.por(n_pos * 3, 12)
            novo.por(0, 9)
            novo.por(0, 8)
            if v1:
                novo.por(13, 4)                     # slen1 = slen2 = 3
            else:
                novo.por((3 * 36 + 3 * 6 + 3) * 2 + escala, 9)      # slen1 = slen2 = slen3 = 3
            novo.por(janela, 23)
            novo.por(0, 3 if v1 else 2)
            for _ in range(n_pos):
                semente = (semente * 1103515245 + 12345) & 0x7FFFFFFF
                corpo.por((semente >> 16) & 7, 3)
        ib2 = tab.index(tab[ib] * 2)
        tam2 = (144000 if v1 else 72000) * tab[ib2] // taxa
        saida += bytes([cab[0], cab[1], (ib2 << 4) | (it << 2), 0x40 | (0x30 if com_ms else 0x10) | (cab[3] & 15)])
        saida += novo.bytes(32 if v1 else 17)
        saida += corpo.bytes(tam2 - 4 - (32 if v1 else 17))
        p += tam
    return bytes(saida)


def conferir_huffman(caminho=None):
    """Confere as tabelas de Huffman de mp3_tabelas.h: cada uma tem de ser um
    código de prefixo completo. Devolve a lista de problemas (vazia = certo)."""
    fonte = open(caminho or os.path.join(RAIZ, "userland", "tocador", "mp3_tabelas.h"), encoding="utf-8").read()

    def lista(nome):
        m = re.search(r"\b%s\[\d+\]\s*=\s*\{([^;]*)\};" % nome, fonte, re.S)
        return [int(v) for v in re.findall(r"-?\d+", m.group(1))]

    problemas = []
    for t in ["1", "2", "3", "5", "6", "7", "8", "9", "10", "11", "12", "13", "15", "16", "24", "A", "B"]:
        cod, tam = lista("hc" + t), lista("ht" + t)
        if len(cod) != len(tam):
            problemas.append(f"tabela {t}: {len(cod)} códigos e {len(tam)} tamanhos")
            continue
        if any(c >= (1 << b) for c, b in zip(cod, tam)):
            problemas.append(f"tabela {t}: código que não cabe no tamanho")
            continue
        if sum(Fraction(1, 2 ** b) for b in tam) != 1:
            problemas.append(f"tabela {t}: a soma de 2^-tamanho não dá 1")
        textos = [format(c, "0%db" % b) for c, b in zip(cod, tam)]
        conj = set(textos)
        if len(conj) != len(textos):
            problemas.append(f"tabela {t}: códigos repetidos")
        if any(x[:j] in conj for x in textos for j in range(1, len(x))):
            problemas.append(f"tabela {t}: um código é começo de outro")
    # A janela de síntese: o filtro por trás dela é uma curva suave; um número
    # digitado errado aparece como um "dente".
    d = lista("janela_d")
    if len(d) != 257:
        problemas.append(f"janela_d tem {len(d)} valores")
    else:
        h = [d[i] * (-1) ** (i // 64) for i in range(256)] + [d[256]]
        h = h + h[-2::-1]                                   # o filtro inteiro, simétrico
        for k in range(4):                                  # a 4ª diferença de uma curva suave é quase zero
            h = [b - a for a, b in zip(h, h[1:])]
        for i, v in enumerate(h):
            if abs(v) > 8:
                problemas.append(f"janela_d perto de {i + 2} destoa dos vizinhos")
    return problemas


def compilar_no_pc(destino):
    """Compila mp3.c no PC (com um sagui.h de mentira que usa o malloc) junto
    do programinha tests/mp3_pc/principal.c, que decodifica um arquivo e
    grava o PCM. Devolve o caminho do executável, ou None se não compilar."""
    exe = os.path.join(destino, "mp3_pc")
    os.makedirs(destino, exist_ok=True)
    pasta = os.path.join(AQUI, "mp3_pc")
    r = subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-I", pasta, "-I", os.path.join(RAIZ, "userland", "tocador"),
                        "-o", exe, os.path.join(pasta, "principal.c"),
                        os.path.join(RAIZ, "userland", "tocador", "mp3.c")], capture_output=True, text=True)
    if r.returncode != 0 or r.stderr.strip():
        print(r.stderr)
        return None
    return exe


def comparar(a, b):
    """Compara duas listas de amostras: (diferença máxima, média, SNR em dB)."""
    n = min(len(a), len(b))
    if n == 0:
        return 99999, 99999.0, -99.0
    maior, soma, erro2, sinal2 = 0, 0, 0, 0
    for x, y in zip(a[:n], b[:n]):
        d = abs(x - y)
        maior = max(maior, d)
        soma += d
        erro2 += d * d
        sinal2 += y * y
    snr = 999.0 if erro2 == 0 else 10 * math.log10(max(sinal2, 1) / erro2)
    return maior, soma / n, snr


if __name__ == "__main__":
    p = conferir_huffman()
    print("tabelas:", p or "certas")
