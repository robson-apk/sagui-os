; rebate.asm - Rebate: duas raquetes e uma bolinha (exemplo do SaguiOS, H92).
;
; Programa de CHIP-8 escrito para o SaguiOS. Você é a raquete da esquerda
; (teclas W e S, ou as setas para cima e para baixo); o computador é a da
; direita e segue a bolinha, mas só anda a cada dois quadros. Quem deixar a
; bolinha passar dá um ponto ao outro; o placar volta a zero no 10.
;
; Registros: v6 = raquete do jogador (y), v7 = do computador, v8, v9 =
; bolinha, va, vb = passo da bolinha (1 ou 255, que somado dá -1), vc =
; pontos do jogador, vd = do computador, ve = conta quadros; v0 a v5 = contas.

ALTURA = 6                  ; tamanho das raquetes
X_JOGADOR = 2
X_COMPUTADOR = 61

inicio:
    cls
    ld vc, 0
    ld vd, 0
    call placar

nova_bola:
    ld v6, 13
    ld v7, 13
    ld i, raquete
    ld v0, X_JOGADOR
    drw v0, v6, ALTURA
    ld v0, X_COMPUTADOR
    drw v0, v7, ALTURA
    ld v8, 32
    rnd v9, 15
    add v9, 8
    ld va, 1                ; sai para um lado sorteado...
    rnd v0, 1
    se v0, 0
    ld va, 255
    ld vb, 1                ; ...subindo ou descendo
    rnd v0, 1
    se v0, 0
    ld vb, 255
    ld i, bola
    drw v8, v9, 1

laco:
    ld v0, 1                ; um passo por quadro
    ld dt, v0
espera:
    ld v0, dt
    se v0, 0
    jp espera

    ld v0, 5                ; tecla 5 (W): sobe
    sknp v0
    call sobe
    ld v0, 8                ; tecla 8 (S): desce
    sknp v0
    call desce
    add ve, 1               ; o computador anda nos quadros ímpares
    ld v0, 1
    and v0, ve
    se v0, 0
    call computador

    ld i, bola              ; apaga a bolinha, anda e confere as batidas
    drw v8, v9, 1
    add v8, va
    add v9, vb
    sne v9, 0
    ld vb, 1
    sne v9, 31
    ld vb, 255
    sne v8, 3
    call toca_jogador
    sne v8, 60
    call toca_computador
    sne v8, 0
    jp ponto_computador
    sne v8, 63
    jp ponto_jogador
    ld i, bola
    drw v8, v9, 1
    jp laco

; ---- a raquete do jogador
sobe:
    sne v6, 0               ; já está no alto: não sobe
    ret
    ld i, raquete
    ld v0, X_JOGADOR
    drw v0, v6, ALTURA
    add v6, 255
    drw v0, v6, ALTURA
    ret
desce:
    sne v6, 26              ; 32 - ALTURA
    ret
    ld i, raquete
    ld v0, X_JOGADOR
    drw v0, v6, ALTURA
    add v6, 1
    drw v0, v6, ALTURA
    ret

; ---- a raquete do computador: vai na direção da bolinha
computador:
    ld v1, v7
    add v1, 2               ; o meio da raquete
    ld v0, v9
    sub v0, v1              ; bolinha - meio; VF = 1 se a bolinha está no meio ou abaixo
    se vf, 0
    jp computador_desce
    sne v7, 0
    ret
    ld i, raquete
    ld v0, X_COMPUTADOR
    drw v0, v7, ALTURA
    add v7, 255
    drw v0, v7, ALTURA
    ret
computador_desce:
    ld v1, 2                ; só desce se a bolinha estiver 2 ou mais abaixo
    sub v0, v1
    se vf, 1
    ret
    sne v7, 26
    ret
    ld i, raquete
    ld v0, X_COMPUTADOR
    drw v0, v7, ALTURA
    add v7, 1
    drw v0, v7, ALTURA
    ret

; ---- a bolinha encosta numa raquete?
toca_jogador:
    se va, 255              ; só quando vem da direita
    ret
    ld v0, v9
    sub v0, v6              ; bolinha - topo da raquete
    se vf, 1
    ret                     ; passou por cima
    ld v1, 5
    sub v1, v0              ; VF = 1 se a diferença é de 0 a 5
    se vf, 1
    ret
    ld va, 1
    ld v0, 2
    ld st, v0               ; bip
    ret
toca_computador:
    se va, 1
    ret
    ld v0, v9
    sub v0, v7
    se vf, 1
    ret
    ld v1, 5
    sub v1, v0
    se vf, 1
    ret
    ld va, 255
    ld v0, 2
    ld st, v0
    ret

; ---- pontos
ponto_computador:
    add vd, 1
    jp ponto
ponto_jogador:
    add vc, 1
ponto:
    ld v0, 12
    ld st, v0               ; um bip mais longo
    sne vc, 10
    call zerar
    sne vd, 10
    call zerar
    cls
    call placar
    jp nova_bola
zerar:
    ld vc, 0
    ld vd, 0
    ret

placar:
    ld v0, 24
    ld v1, 1
    ld f, vc
    drw v0, v1, 5
    ld v0, 36
    ld f, vd
    drw v0, v1, 5
    ret

raquete:
    db %#......., %#......., %#......., %#......., %#......., %#.......
bola:
    db %#.......
