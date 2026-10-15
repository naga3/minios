// Pi 版の土台（hal.c）の、シェル（main.c）向けの宣言。ゲームと音は ../tiny/tiny.h と sound.h を使う
#pragma once

void hal_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex(unsigned long v);
void uart_putdec(unsigned long v);
unsigned char getc_wait(void);           // 文字が来るまで待つ
void audio_report(void);                 // 最後に演奏した音の、サンプル数と FNV-1a のハッシュを UART に出す（PC の波形と比べる試験用）
void con_putc_raw(char c);               // 1 文字を書いて画面へ写す
