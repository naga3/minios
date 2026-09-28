// シグナル（最小限）: 既定の動作（終了する、または何もしない）と、無視（SIG_IGN）だけ。ハンドラは扱わない
// （minios のプログラムはハンドラを使わない）。PID 1 には届かない（Linux と同じ）
#include "kernel.h"

static int default_ignored(int sig) { return sig == 17 /* SIGCHLD */ || sig == 23 /* SIGURG */ || sig == 28 /* SIGWINCH */; }

void signal_send(struct proc *p, int sig) {
    if (sig <= 0 || sig >= 64 || p->pid == 1) return;
    if ((p->sig_ignored >> sig & 1) || default_ignored(sig)) return;
    p->sig_pending |= 1UL << sig;
    if (p->state == SLEEPING) {  // 眠っていれば起こす（読み込みなどは EINTR で戻る）
        p->state = RUNNABLE;
        p->wake_tick = 0;
    }
}

void signal_pgrp(long pgrp, int sig) {
    for (int i = 0; i < NPROC; i++)
        if (procs[i].state != UNUSED && procs[i].state != ZOMBIE && procs[i].pgid == pgrp) signal_send(&procs[i], sig);
}

int signal_pending(struct proc *p) { return p->sig_pending != 0; }

// ユーザーモードに戻る前に呼ぶ。届いているシグナルがあれば、既定の動作（終了）をする
void signal_check(void) {
    if (!cur->sig_pending) return;
    for (int sig = 1; sig < 64; sig++)
        if (cur->sig_pending >> sig & 1) process_exit_status(sig);  // wait4 の形: 下位 7 ビットがシグナル
}

long sys_rt_sigaction(long sig, u64 act_va, u64 old_va) {
    if (sig <= 0 || sig >= 64 || sig == 9 || sig == 19) return -22;
    if (old_va) {
        u64 old[4] = {cur->sig_ignored >> sig & 1, 0, 0, 0};
        if (copy_out(old_va, old, sizeof old) < 0) return -14;
    }
    if (act_va) {
        u64 handler;
        if (copy_in(&handler, act_va, 8) < 0) return -14;
        if (handler == 1) cur->sig_ignored |= 1UL << sig;  // SIG_IGN
        else cur->sig_ignored &= ~(1UL << sig);            // SIG_DFL（ハンドラは既定の動作として扱う）
    }
    return 0;
}

long sys_kill(long pid, long sig) {
    if (sig < 0 || sig >= 64) return -22;
    int found = 0;
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &procs[i];
        if (p->state == UNUSED || p->state == ZOMBIE) continue;
        if (pid > 0 ? p->pid == pid : pid == 0 ? p->pgid == cur->pgid : pid < -1 ? p->pgid == -pid : p->pid != 1) {
            found = 1;
            if (sig) signal_send(p, sig);
        }
    }
    return found ? 0 : -3;  // ESRCH
}
