// パイプ: 4KB の輪のバッファ。空なら読む側が、満杯なら書く側が眠る。書く側が全員閉じたら、読む側は 0（終わり）を受け取る
#include "kernel.h"

struct pipe {
    u8 buf[4096];
    u64 r, w;
    int readers, writers;
};

long sys_pipe(u64 fds_va) {
    struct pipe *p = kmalloc(sizeof *p);
    struct file *rf = file_new(F_PIPE_R), *wf = file_new(F_PIPE_W);
    if (!p || !rf || !wf) return -12;
    p->readers = p->writers = 1;
    rf->pipe = wf->pipe = p;
    wf->flags = 1;  // O_WRONLY
    int fds[2] = {-1, -1};
    for (int i = 0, k = 0; i < NFD && k < 2; i++)
        if (!cur->fds[i]) {
            cur->fds[i] = k ? wf : rf;
            fds[k++] = i;
        }
    if (fds[1] < 0) {
        if (fds[0] >= 0) cur->fds[fds[0]] = 0;
        kfree(rf), kfree(wf), kfree(p);
        return -24;  // EMFILE
    }
    return copy_out(fds_va, fds, sizeof fds);
}

long pipe_read(struct pipe *p, u64 buf, u64 n) {
    while (p->r == p->w) {
        if (p->writers == 0) return 0;
        sleep_on(p);
        if (signal_pending(cur)) return -4;
    }
    u64 i = 0;
    for (; i < n && p->r != p->w; i++, p->r++)
        if (copy_out(buf + i, &p->buf[p->r % sizeof p->buf], 1) < 0) return -14;
    wakeup(p);
    return i;
}

long pipe_write(struct pipe *p, u64 buf, u64 n) {
    u64 i = 0;
    while (i < n) {
        if (p->readers == 0) {
            signal_send(cur, 13);  // SIGPIPE
            return i ? (long)i : -32;  // EPIPE
        }
        if (p->w - p->r == sizeof p->buf) {
            wakeup(p);
            sleep_on(p);
            if (signal_pending(cur)) return i ? (long)i : -4;
            continue;
        }
        if (copy_in(&p->buf[p->w % sizeof p->buf], buf + i, 1) < 0) return -14;
        p->w++, i++;
    }
    wakeup(p);
    return n;
}

void pipe_close(struct pipe *p, int writer) {
    if (writer) p->writers--;
    else p->readers--;
    wakeup(p);
    if (p->readers == 0 && p->writers == 0) kfree(p);
}
