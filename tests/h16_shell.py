#!/usr/bin/env python3
"""H16 - shell e comandos como programas.

Critério: roteiro executa todos os comandos e apelidos, incluindo
`echo ola > tmp/a.txt`, `cat tmp/a.txt`, `cp tmp/a.txt tmp/b.txt`,
`ls bin | conta` e `contador` rodando junto com outro comando; `falha`
derruba só o próprio processo e o shell continua; `ps` mostra os processos;
`ajuda` lista tudo. Captura de tela de cada caso em docs/capturas/.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, RAIZ, imagem_console  # noqa: E402

PROGRAMAS = sorted([f[:-2] for f in os.listdir(os.path.join(RAIZ, "userland", "programas")) if f.endswith(".c")] + ["sagui3d", "navegador", "cipo", "video", "saguicode", "cc", "visualizador", "musica", "unzip", "tocador", "mp3info"])   # programas de várias partes, em pastas próprias (cipo e video: H82; visualizador, musica e unzip: H87; tocador e mp3info: H86 do upstream)


def main():
    with Maquina("h16", "ide", imagem=imagem_console()) as m:
        m.esperar_serial("[SAGUI] shell iniciado: processo", 120)
        m.esperar_serial("sagui:/$", 30)
        s = m.serial()
        # As tarefas do kernel (USB, rede) também têm número de processo e nascem
        # antes; o sagsh tem que ser o primeiro programa do usuário a rodar.
        execs = re.findall(r"\[SAGUI\] executar (\S+) -> processo", s)
        verificar(execs and execs[0] == "/bin/sagsh", "o kernel inicia /bin/sagsh como primeiro programa (ring 3)")
        verificar("Terminal provis" not in s, "o terminal provisório do kernel foi removido")

        out = m.comando("ajuda")
        for c in ("ls/listar", "cd/entrar", "pwd/onde", "cat/ver", "echo/eco", "cp/copiar", "mv/mover", "rm/apagar",
                  "mkdir/criar-pasta", "rmdir/remover-pasta", "wc/conta", "ps/processos", "kill/matar", "clear/limpar",
                  "ajuda", "mem", "data", "sobre", "ola", "contador", "falha", "reiniciar", "sair"):
            verificar(c in out, f"ajuda lista {c}")
        m.captura("H16-ajuda")
        out = m.comando("ajuda conta")
        verificar("Uso: wc" in out and "Apelido" in out, "ajuda <comando> explica o comando")

        m.comando("limpar")
        out = m.comando("ls")
        verificar("bin/" in out and "leia-me.txt" in out, "ls")
        out = m.comando("listar /bin")
        verificar("sagsh" in out and "programa" in out, "listar (apelido de ls) /bin")
        out = m.comando("ls bin | conta")
        verificar(f"{len(PROGRAMAS)} linhas" in out, f"ls bin | conta = {len(PROGRAMAS)} linhas (um por programa)")
        out = m.comando("listar bin|wc")
        verificar(f"{len(PROGRAMAS)} linhas" in out, "pipe colado nas palavras (listar bin|wc)")

        m.comando("echo ola > tmp/a.txt")
        out = m.comando("cat tmp/a.txt")
        verificar(re.search(r"^ola$", out, re.M), "echo ola > tmp/a.txt; cat tmp/a.txt")
        m.comando("cp tmp/a.txt tmp/b.txt")
        out = m.comando("ver tmp/b.txt")
        verificar(re.search(r"^ola$", out, re.M), "cp tmp/a.txt tmp/b.txt; ver (apelido de cat) tmp/b.txt")
        m.comando("eco \"mais uma linha\" >> tmp/a.txt")
        out = m.comando("conta < tmp/a.txt")
        verificar("2 linhas, 4 palavras" in out, "eco com aspas e >>; conta < arquivo (redirecionar a entrada)")
        m.comando("copiar tmp/a.txt /tmp/c.txt")
        m.comando("mv tmp/b.txt tmp/d.txt")
        m.comando("mover tmp/c.txt tmp/e.txt")
        out = m.comando("ls tmp")
        verificar("a.txt" in out and "d.txt" in out and "e.txt" in out and "b.txt" not in out and "c.txt" not in out,
                  "copiar, mv e mover")
        m.comando("rm tmp/d.txt")
        m.comando("apagar tmp/e.txt")
        m.comando("mkdir tmp/pasta1")
        m.comando("criar-pasta tmp/pasta2")
        out = m.comando("ls tmp")
        verificar("pasta1/" in out and "pasta2/" in out and "d.txt" not in out and "e.txt" not in out,
                  "rm, apagar, mkdir, criar-pasta")
        m.comando("rmdir tmp/pasta1")
        m.comando("remover-pasta tmp/pasta2")
        out = m.comando("ls tmp")
        verificar("pasta" not in out, "rmdir, remover-pasta")
        m.captura("H16-arquivos-redirecionamento-pipe")

        m.comando("cd historia")
        out = m.comando("pwd")
        verificar("/historia" in out, "cd historia; pwd")
        m.comando("entrar ..")
        out = m.comando("onde")
        verificar(re.search(r"^/$", out, re.M), "entrar .. (apelido de cd); onde (apelido de pwd)")
        out = m.comando("cd /nao-existe")
        verificar("não existe" in out, "cd para pasta inexistente avisa")
        out = m.comando("xyz")
        verificar("comando não encontrado" in out, "comando desconhecido avisa")

        # Multitarefa: contador em segundo plano junto com outros comandos.
        out = m.comando("contador 4 &")
        pid = int(re.search(r"\[(\d+)\] contador em segundo plano", out).group(1))
        out = m.comando("ps")
        verificar(re.search(rf"^\s*{pid}\s+\d+\s+\w+.*contador", out, re.M), f"ps mostra o contador (PID {pid}) rodando junto")
        verificar("sagsh" in out and "ocioso" in out and "ps" in out, "ps mostra sagsh, ocioso e o próprio ps")
        m.captura("H16-contador-e-ps")
        out = m.comando("processos")
        verificar("contador" in out, "processos (apelido de ps)")
        m.esperar_serial(f"[contador {pid}] 4 de 4 segundos", 30)
        time.sleep(1)   # deixa o shell redesenhar o prompt (D018) antes de digitar
        out = m.comando("ola de novo")
        verificar(f"[{pid}] terminou" in out, "o shell avisa quando o contador termina")
        verificar('argv[1] = "de"' in out, "ola recebe os argumentos")

        out = m.comando("contador 60 &")
        pid = int(re.search(r"\[(\d+)\] contador em segundo plano", out).group(1))
        out = m.comando(f"kill {pid}")
        verificar(f"Processo {pid} encerrado" in out, "kill encerra o contador")
        m.comando("contador 60 &")
        out = m.comando("ps")
        pid2 = int(re.findall(r"^\s*(\d+)\s+\d+\s+\w+.*contador", out, re.M)[0])
        out = m.comando(f"matar {pid2}")
        verificar(f"Processo {pid2} encerrado" in out, "matar (apelido de kill)")
        out = m.comando("ps")
        verificar("contador" not in out, "nenhum contador sobrou")

        # Proteção: falha derruba só o próprio processo.
        for tipo, esperado in (("", "Falta de p"), ("kernel", "Falta de p"), ("instrucao", "Prote"), ("divisao", "Divis")):
            out = m.comando(f"falha {tipo}".strip())
            verificar("encerrado" in out and esperado in out, f"falha {tipo or '(ponteiro nulo)'}: só o processo foi encerrado")
        out = m.comando("echo o shell continua vivo")
        verificar("o shell continua vivo" in out, "o shell continua funcionando depois das falhas")
        m.captura("H16-falha-protecao")

        out = m.comando("mem")
        verificar("Livre:" in out and "MB" in out, "mem")
        out = m.comando("data")
        verificar(re.search(r"\d\d/\d\d/20\d\d \d\d:\d\d:\d\d", out), "data")
        out = m.comando("sobre")
        verificar("SaguiOS 2.1" in out and "processos" in out, "sobre")

        # Histórico (seta para cima) e completar com Tab.
        out = m.comando("", teclas_finais=("up", "ret"))
        verificar("SaguiOS 2.1" in out, "seta para cima repete o último comando (sobre)")
        out = m.comando("cat /his", teclas_finais=("tab", "l", "i", "tab", "ret"))
        verificar("cat /historia/linux.txt" in out and "Minix" in out, "Tab completa /his -> /historia/ e li -> linux.txt")
        out = m.comando("sob", teclas_finais=("tab", "ret"))
        verificar("executar /bin/sobre" in out and "SaguiOS 2.1" in out, "Tab completa nomes de comandos (sob -> sobre)")
        out = m.comando("regis", teclas_finais=("tab", "ret"))
        verificar("executar /bin/registro" in out, "Tab completa programas de /bin fora da tabela do shell (regis -> registro)")
        out = m.comando("conta", teclas_finais=("d", "tab", "1", "ret"))
        verificar("1 de 1 segundos" in out, "Tab completa contad -> contador (\"con\" é ambíguo com conta)")
        m.comando("limpar")
        m.comando("cat /historia/saguios.txt | conta")
        m.captura("H16-tab-historico")

        # sair: o kernel abre um shell novo.
        m.digitar("sair\n", 0.04)
        m.esperar_serial("O shell foi fechado", 20)
        m.esperar_serial("shell iniciado: processo", 20)
        time.sleep(1)
        out = m.comando("pwd")
        verificar(re.search(r"^/$", out, re.M), "sair fecha o shell e o sistema abre outro")

        m.digitar("reiniciar\n", 0.04)
        m.esperar_serial("[SAGUI] reiniciando", 20)
        try:
            m.proc.wait(timeout=30)
            reiniciou = True
        except Exception:
            reiniciou = False
        verificar(reiniciou, "reiniciar reinicia a máquina")
    return 0


if __name__ == "__main__":
    sys.exit(main())
