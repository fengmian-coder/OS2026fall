#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>
#include <sbi.h>

#define TICK_NUM 100
#define SHUTDOWN_AFTER 10

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void) {

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

//lab3 function2 2412074


void interrupt_handler(struct trapframe *tf) {
    // 去掉 scause 的最高位，得到中断原因编号
    uintptr_t cause = tf->cause & (((uintptr_t)-1) >> 1);

    switch (cause) {
        case IRQ_S_TIMER:
    // 设置下一次时钟中断
    clock_set_next_event();

    // 时钟中断计数加一
    ticks++;

    // 每累计100次时钟中断打印一次
    if (ticks % TICK_NUM == 0) {
        print_ticks();

        // 累计输出10次后关闭系统
        if (ticks / TICK_NUM >= SHUTDOWN_AFTER) {
            cprintf("Timer test finished, shutting down...\n");
            sbi_shutdown();
        }
    }
    break;

        case IRQ_S_SOFT:
            // 处理Supervisor软件中断
            cprintf("Supervisor software interrupt\n");
            break;


        default:
             // 未知中断：输出原因编号
             cprintf("Unknown interrupt: cause %d\n", (int)cause);

             // 输出完整的TrapFrame用于调试
             print_trapframe(tf);
             break;

    }
}



/* lab3 function2: exception_handler
 * 异常处理
 */
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {


case CAUSE_BREAKPOINT: {
    // 输出断点异常发生的位置
    cprintf("Breakpoint at 0x%08x\n", tf->epc);

    // 读取断点指令的低16位
    uint16_t insn = *(const uint16_t *)(uintptr_t)tf->epc;

    // 根据指令长度更新异常返回地址
    // 压缩指令为2字节，普通指令为4字节
    if ((insn & 0x3) == 0x3) {
        tf->epc += 4;
    } else {
        tf->epc += 2;
    }
    break;
}


    case CAUSE_ILLEGAL_INSTRUCTION: {
    cprintf("Illegal instruction at 0x%08x\n", tf->epc);

    uint16_t insn = *(const uint16_t *)(uintptr_t)tf->epc;

    if ((insn & 0x3) == 0x3) {
        tf->epc += 4;
    } else {
        tf->epc += 2;
    }
    break;
}

    default:
        // 未知异常：输出完整TrapFrame便于调试
        print_trapframe(tf);
        break;
    }
}



static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}
