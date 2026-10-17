/* gow_mlaa_hle.c -- HLE of GoW:A's SPU post-process taskset (MLAA).
 *
 * The 5 tasks of the embedded SPU ELF at VA 0x68F580 (image 14, taskset created
 * with cellSpursCreateTask2, argument {0x9BAC30, i}) wait for a signal from an
 * RsxSync WWS job, process the frame in the buffer named by the control block,
 * write the batch number into an RSX label (the RSX blocks on it with
 * SEMAPHORE_ACQUIRE) and decrement a per-frame batch counter; the decrement that
 * reaches zero kicks the counter's workload (an EDGE wid).
 *
 * Lifted, one batch takes ~1.7 s against a few ms on hardware, so the RsxSync
 * job posts the next batch into the single control block while the tasks are
 * still busy, a signal is eaten by a barrier wait and a batch is lost. Even an
 * instant task loses batches if it reads the control block after the job has
 * already posted the next one. So the batch is served AT THE SIGNAL: a title
 * hook sees the job's PUTLLC on the taskset's signalled bitset, and for every
 * task bit it set performs that task's externally visible work -- label write,
 * then its counter decrement (+ the kick when it reaches zero) -- and clears the
 * bit. The tasks themselves only park. Visible deviation: no anti-aliasing.
 * GOW_MLAA=lifted runs the real tasks instead.
 *
 * Control block (EA in TaskInfo.args word 0): {u32 batch, u32 labelEA,
 * u32 counterEA, u32 bufferEA}. Counter: (count<<16)|(wid<<8)|flags. */
#include "spu_context.h"
#include "spu_workload.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern uint8_t* vm_base;
extern void spu_lockline_lock(void);
extern void spu_lockline_unlock(void);
extern void (*g_spu_line_commit_title_hook)(uint32_t line, spu_context* ctx);

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void     wbe32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static volatile uint32_t s_taskset, s_ctrl, s_spurs, s_ntasks;

/* One task's share of a batch. Called with the lock-line lock held. */
static void mlaa_task_batch(uint32_t task)
{
    const uint32_t batch = be32(vm_base + s_ctrl + 0);
    const uint32_t label = be32(vm_base + s_ctrl + 4);
    const uint32_t cnt   = be32(vm_base + s_ctrl + 8);
    if (label) wbe32(vm_base + label, batch);
    uint32_t before = 0, after = 0;
    if (cnt) {
        before = be32(vm_base + cnt);
        uint32_t count = before >> 16;
        if (count) {
            count--;
            after = count ? (count << 16) | (before & 0xFFFFu) : 0x0000FF00u;
            wbe32(vm_base + cnt, after);
            if (!count && s_spurs) {                   /* kick the counter's workload */
                const uint32_t wid = (before >> 8) & 0xFFu;
                if (wid < 32) {
                    uint8_t* rc = vm_base + s_spurs + (wid < 16 ? wid : 0x10u + (wid - 16u));
                    if (*rc < 0xFF) (*rc)++;
                }
            }
        }
    }
    static volatile long s_n;
    if (InterlockedIncrement(&s_n) <= 60)
        fprintf(stderr, "[mlaa-hle] task %u batch %u label 0x%08X counter 0x%08X: %08X -> %08X\n",
                task, batch, label, cnt, before, after);
}

static void mlaa_line_hook(uint32_t line, spu_context* ctx)
{
    const uint32_t ts = s_taskset;
    if (!ts || line != ((ts + 0x40u) & ~127u) || ctx->image_id == 14) return;
    spu_lockline_lock();
    uint8_t* sig = vm_base + ts + 0x40;                /* signalled bitset, task 0 = MSB */
    for (uint32_t t = 0; t < s_ntasks && t < 8; t++) {
        const uint8_t m = (uint8_t)(0x80u >> t);
        if (sig[0] & m) { sig[0] &= (uint8_t)~m; mlaa_task_batch(t); }
    }
    spu_lockline_unlock();
}

/* The task entry: record the taskset/control block and park for good (the task
 * must stay enabled -- an exited task makes the job's SendSignal fail). */
static void gow_mlaa_hle_task(spu_context* ctx)
{
    const uint8_t* ls = ctx->ls;
    const uint32_t taskset = be32(ls + 0x27BC);   /* STC taskset ptr (be64 @0x27B8) */
    const uint32_t task_id = be32(ls + 0x27D4);   /* STC taskId */
    s_ctrl  = be32(ls + 0x2780);                  /* TaskInfo.args word 0 */
    s_spurs = be32(ls + 0x1C4);                   /* kernel ctx spurs EA (be64 @0x1C0) */
    if (task_id + 1 > s_ntasks) s_ntasks = task_id + 1;
    s_taskset = taskset;
    fprintf(stderr, "[mlaa-hle] task %u parked (taskset=0x%08X ctrl=0x%08X spurs=0x%08X)\n",
            task_id, taskset, s_ctrl, s_spurs);
    for (;;) Sleep(INFINITE);
}

void gow_mlaa_hle_register(void)
{
    const char* m = getenv("GOW_MLAA");
    if (m && !strcmp(m, "lifted")) return;
    g_spu_line_commit_title_hook = mlaa_line_hook;
    spu_workload_register_img(0xF4C2A58BDE92CE5EULL, gow_mlaa_hle_task, 14, "gowtask0-mlaa-hle");
    fprintf(stderr, "[mlaa-hle] MLAA taskset (fp 0xF4C2A58BDE92CE5E) served by HLE "
                    "(no anti-aliasing; GOW_MLAA=lifted runs the real tasks)\n");
}
