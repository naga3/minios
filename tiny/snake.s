# スネーク: 矢印キー（か WASD）でヘビを動かし、エサを食べて伸ばす。壁か自分の体に当たると終わり。
# Enter でもう一度、Esc でシェルに戻る
#
# 盤面は 8 × 8 点の升目で 40 × 24 マス（画面の 2 行目から）。1 行目は得点
    .code64
    .set GW, 40                         # 盤面の幅（マス）
    .set GH, 24                         # 盤面の高さ（マス）
    .set MAXLEN, GW * GH
    .set KEY_UP,    0x80
    .set KEY_DOWN,  0x81
    .set KEY_LEFT,  0x82
    .set KEY_RIGHT, 0x83
    .set C_BG,    0                     # 黒
    .set C_WALL,  8                     # 暗い灰色
    .set C_BODY,  2                     # 緑
    .set C_HEAD,  10                    # 明るい緑
    .set C_FOOD,  12                    # 明るい赤
    .set SPEED,   8                     # 何回のタイマー（10ms）ごとに 1 マス進むか

    .text
    .global snake_main

snake_main:
    push %rbx
    push %r12
    push %r13
    xor %eax, %eax                      # カーソルを隠す
    call con_cursor
    rdtsc                               # 乱数の種: CPU の時計
    shl $32, %rdx
    or %rdx, %rax
    or $1, %rax
    mov %rax, rand_state
new_game:
    call setup
play:
    call read_keys                      # キーを読んで、向きを変える（Esc なら 1）
    test %eax, %eax
    jnz quit
    mov ticks, %rax
    cmp next_step, %rax
    jae 1f
    hlt                                 # 次に進む時刻まで、CPU を休める
    jmp play
1:  add $SPEED, %rax
    mov %rax, next_step
    call step                           # 1 マス進む（ぶつかったら 1）
    test %eax, %eax
    jz play
    call game_over                      # Enter なら 0、Esc なら 1
    test %eax, %eax
    jz new_game
quit:
    mov $1, %al
    call con_cursor
    mov $0x0107, %ax                    # シェルの色に戻して消す
    call con_color
    call con_clear
    pop %r13
    pop %r12
    pop %rbx
    ret

# setup: 盤面を描き、ヘビを真ん中に置き、エサを置く
setup:
    mov $C_BG, %r8d                     # 画面を黒に
    call clear
    mov $0x000f, %ax                    # 得点の行: 黒地に白
    call con_color
    xor %eax, %eax
    call con_goto
    mov $str_title, %rsi
    call con_puts
    xor %ebx, %ebx                      # 壁: 上と下の行
1:  mov %ebx, %edi
    xor %esi, %esi
    mov $C_WALL, %r8d
    call draw_cell
    mov $GH - 1, %esi
    call draw_cell
    inc %ebx
    cmp $GW, %ebx
    jne 1b
    xor %ebx, %ebx                      # 壁: 左と右の列
2:  xor %edi, %edi
    mov %ebx, %esi
    mov $C_WALL, %r8d
    call draw_cell
    mov $GW - 1, %edi
    call draw_cell
    inc %ebx
    cmp $GH, %ebx
    jne 2b
    movq $3, length                     # ヘビ: 長さ 3、右向き
    movb $20, body_x
    movb $12, body_y
    movb $19, body_x + 1
    movb $12, body_y + 1
    movb $18, body_x + 2
    movb $12, body_y + 2
    movb $1, dir_x
    movb $0, dir_y
    movb $1, next_dx
    movb $0, next_dy
    movq $0, score
    xor %ebx, %ebx
3:  movzbl body_x(%rbx), %edi
    movzbl body_y(%rbx), %esi
    mov $C_BODY, %r8d
    test %ebx, %ebx
    jnz 4f
    mov $C_HEAD, %r8d
4:  call draw_cell
    inc %ebx
    cmp length, %rbx
    jne 3b
    call place_food
    call show_score
    mov ticks, %rax
    add $SPEED, %rax
    mov %rax, next_step
    ret

# read_keys: 貯まったキーを全部読む。矢印か WASD で次の向きを決める（逆向きには曲がれない）。Esc なら eax = 1
read_keys:
1:  call getc_nowait
    test %al, %al
    jz 9f
    cmp $27, %al
    je 8f
    xor %ecx, %ecx                      # cl = dx、ch = dy
    cmp $KEY_UP, %al
    je 2f
    cmp $'w', %al
    je 2f
    cmp $KEY_DOWN, %al
    je 3f
    cmp $'s', %al
    je 3f
    cmp $KEY_LEFT, %al
    je 4f
    cmp $'a', %al
    je 4f
    cmp $KEY_RIGHT, %al
    je 5f
    cmp $'d', %al
    je 5f
    jmp 1b
2:  mov $-1, %ch
    jmp 6f
3:  mov $1, %ch
    jmp 6f
4:  mov $-1, %cl
    jmp 6f
5:  mov $1, %cl
6:  mov dir_x, %al                      # 今の向きの逆（dx + 新しい dx = 0 かつ dy + 新しい dy = 0）には曲がれない
    add %cl, %al
    mov dir_y, %ah
    add %ch, %ah
    test %ax, %ax
    jz 1b
    mov %cl, next_dx
    mov %ch, next_dy
    jmp 1b
8:  mov $1, %eax
    ret
9:  xor %eax, %eax
    ret

# step: 1 マス進む。壁か体に当たったら eax = 1
step:
    mov next_dx, %al                    # 向きを確定
    mov %al, dir_x
    mov next_dy, %al
    mov %al, dir_y
    movzbl body_x, %r12d                # 新しい頭の位置
    movsbl dir_x, %eax
    add %eax, %r12d
    movzbl body_y, %r13d
    movsbl dir_y, %eax
    add %eax, %r13d
    cmp $1, %r12d                       # 壁（盤面の縁）
    jl 8f
    cmp $GW - 2, %r12d
    jg 8f
    cmp $1, %r13d
    jl 8f
    cmp $GH - 2, %r13d
    jg 8f
    xor %ebx, %ebx                      # エサを食べるか
    cmp food_x, %r12b
    jne 1f
    cmp food_y, %r13b
    jne 1f
    mov $1, %ebx
1:  mov length, %rcx                    # 体に当たるか（伸びないときは、尻尾はこの手で動くので数えない）
    test %ebx, %ebx
    jnz 2f
    dec %rcx
2:  xor %edx, %edx
3:  cmp %rcx, %rdx
    jae 5f
    cmp body_x(%rdx), %r12b
    jne 4f
    cmp body_y(%rdx), %r13b
    je 8f
4:  inc %rdx
    jmp 3b
5:  test %ebx, %ebx                     # 伸びないなら、尻尾のマスを消す
    jnz 6f
    mov length, %rdx
    dec %rdx
    movzbl body_x(%rdx), %edi
    movzbl body_y(%rdx), %esi
    mov $C_BG, %r8d
    call draw_cell
    jmp 7f
6:  cmpq $MAXLEN, length                # 伸びる
    jae 7f
    incq length
    incq score
7:  mov length, %rcx                    # 体を 1 つずつ後ろへずらし、先頭に新しい頭
    dec %rcx
9:  test %rcx, %rcx
    jz 10f
    mov body_x - 1(%rcx), %al
    mov %al, body_x(%rcx)
    mov body_y - 1(%rcx), %al
    mov %al, body_y(%rcx)
    dec %rcx
    jmp 9b
10: movzbl body_x, %edi                 # 前の頭を体の色に
    movzbl body_y, %esi
    mov $C_BODY, %r8d
    call draw_cell
    mov %r12b, body_x
    mov %r13b, body_y
    mov %r12d, %edi
    mov %r13d, %esi
    mov $C_HEAD, %r8d
    call draw_cell
    test %ebx, %ebx
    jz 11f
    call place_food
    call show_score
11: call present
    xor %eax, %eax
    ret
8:  mov $1, %eax
    ret

# place_food: 体と重ならない所に、でたらめにエサを置く
place_food:
1:  call random
    xor %edx, %edx
    mov $GW - 2, %ecx
    div %rcx
    lea 1(%rdx), %r12d                  # 1〜GW-2
    call random
    xor %edx, %edx
    mov $GH - 2, %ecx
    div %rcx
    lea 1(%rdx), %r13d                  # 1〜GH-2
    xor %edx, %edx
2:  cmp length, %rdx
    jae 3f
    cmp body_x(%rdx), %r12b
    jne 4f
    cmp body_y(%rdx), %r13b
    je 1b                               # 体の上だった。やり直す
4:  inc %rdx
    jmp 2b
3:  mov %r12b, food_x
    mov %r13b, food_y
    mov %r12d, %edi
    mov %r13d, %esi
    mov $C_FOOD, %r8d
    call draw_cell
    ret

# show_score: 1 行目の右に得点を出す
show_score:
    mov $0x000f, %ax
    call con_color
    mov $30, %al
    mov $0, %ah
    call con_goto
    mov $str_score, %rsi
    call con_puts
    mov score, %rax
    call con_print_u
    mov $str_pad, %rsi
    call con_puts
    ret

# game_over: GAME OVER を出して、Enter（eax = 0）か Esc（eax = 1）を待つ
game_over:
    mov $0x040f, %ax                    # 赤地に白
    call con_color
    mov $15, %al
    mov $11, %ah
    call con_goto
    mov $str_over, %rsi
    call con_puts
    mov $0x000f, %ax
    call con_color
    mov $8, %al
    mov $13, %ah
    call con_goto
    mov $str_again, %rsi
    call con_puts
1:  call getc
    cmp $'\n', %al
    je 2f
    cmp $27, %al
    jne 1b
    mov $1, %eax
    ret
2:  xor %eax, %eax
    ret

# draw_cell: マス (rdi, rsi) を r8b の色で塗る（盤面は画面の 2 行目から）
draw_cell:
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    shl $3, %edi
    add $1, %esi
    shl $3, %esi
    mov $8, %edx
    mov $8, %ecx
    call fill_rect
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    ret

# random: でたらめな数を rax に（xorshift64）
random:
    mov rand_state, %rax
    mov %rax, %rdx
    shl $13, %rdx
    xor %rdx, %rax
    mov %rax, %rdx
    shr $7, %rdx
    xor %rdx, %rax
    mov %rax, %rdx
    shl $17, %rdx
    xor %rdx, %rax
    mov %rax, rand_state
    ret

    .section .rodata
str_title: .asciz "SNAKE  arrows/WASD  Esc:quit"
str_score: .asciz "SCORE "
str_pad:   .asciz "  "
str_over:  .asciz " GAME OVER "
str_again: .asciz "Enter: again   Esc: quit"

    .section .bss
    .align 8
length:    .skip 8
score:     .skip 8
next_step: .skip 8
rand_state:.skip 8
body_x:    .skip MAXLEN                 # 体の各マス（0 番が頭）
body_y:    .skip MAXLEN
dir_x:     .skip 1                      # 今の向き（-1・0・1）
dir_y:     .skip 1
next_dx:   .skip 1                      # 次の手で曲がる向き
next_dy:   .skip 1
food_x:    .skip 1
food_y:    .skip 1

    .section .note.GNU-stack, "", @progbits
