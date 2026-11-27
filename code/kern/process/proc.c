#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <riscv.h>
#include <kmonitor.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));

    size_t len = 0;
    while (len < PROC_NAME_LEN && name[len] != '\0') {
        len++;
    }

    memcpy(proc->name, name, len);
    return proc->name;
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}


// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}


// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
static void release_current_mm(void);

// lab5 function4 2412074
int do_exit(int error_code)
{
    if (current == idleproc || current == initproc) {
        panic("idleproc or initproc cannot exit.\n");
    }

    // Release the user address space before becoming a zombie.
    release_current_mm();

    bool intr_flag;
    local_intr_save(intr_flag);

    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;
    current->wait_state = 0;

    struct proc_struct *parent = current->parent;

    // Wake up the parent if it is waiting for a child.
    if (parent != NULL &&
        parent->state == PROC_SLEEPING &&
        parent->wait_state == WT_CHILD) {
        wakeup_proc(parent);
    }

    // Reparent all remaining children to initproc.
    while (current->cptr != NULL) {
        struct proc_struct *child = current->cptr;
        current->cptr = child->optr;

        if (current->cptr != NULL) {
            current->cptr->yptr = NULL;
        }

        child->parent = initproc;
        child->yptr = NULL;
        child->optr = initproc->cptr;

        if (initproc->cptr != NULL) {
            initproc->cptr->yptr = child;
        }

        initproc->cptr = child;

        if (child->state == PROC_ZOMBIE &&
            initproc->state == PROC_SLEEPING &&
            initproc->wait_state == WT_CHILD) {
            wakeup_proc(initproc);
        }
    }

    local_intr_restore(intr_flag);

    // A zombie process must never execute again.
    schedule();
    panic("do_exit returned unexpectedly.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)

// lab5 function2 2412074: init_main, user_main, kernel_execve, do_execve, load_icode, do_wait.
// lab5 function4 2412074: do_exit, do_yield, do_kill.

static int kernel_execve(const char *name,
                         unsigned char *binary, size_t size);

// lab5 function2 2412074
// Select the user ELF embedded in the kernel image.
static int
user_main(void *arg)
{
    (void)arg;

#ifdef TEST
    return KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    return KERNEL_EXECVE(hello);
#endif
}

// lab5 function2 2412074
// Start user processes and reclaim all terminated children.
static int
init_main(void *arg)
{
    (void)arg;

    size_t free_pages_before = nr_free_pages();

    int pid = kernel_thread(user_main, NULL, 0);

    if (pid <= 0) {
        panic("cannot create user_main.\n");
    }

    // Reclaim both the original user process and adopted orphans.
    while (do_wait(0, NULL) == 0) {
        schedule();
    }

    cprintf("all user-mode processes have quit.\n");

    assert(initproc->cptr == NULL);
    assert(nr_process == 2);

    size_t free_pages_after = nr_free_pages();

    // Check whether all allocated physical pages are reclaimed.
    if (free_pages_before == free_pages_after) {
        cprintf("init check memory pass.\n");
    } else {
        cprintf("init memory check mismatch: pages %lu -> %lu\n",
                free_pages_before, free_pages_after);
    }

#ifdef DEBUG_GRADE
    // The grading tool observes the kernel monitor entry.
    kmonitor(NULL);
#endif

    // initproc must not exit.
    bool intr_flag;
    local_intr_save(intr_flag);
    current->state = PROC_SLEEPING;
    current->wait_state = 0;
    local_intr_restore(intr_flag);

    while (1) {
        schedule();
    }
}

//LAB4 function2 2412074

// Allocate and initialize a process control block.
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc == NULL) {
        return NULL;
    }

    memset(proc, 0, sizeof(struct proc_struct));

    proc->state = PROC_UNINIT;
    proc->pid = -1;
    proc->runs = 0;
    proc->kstack = 0;
    proc->need_resched = 0;
    proc->parent = NULL;
    proc->mm = NULL;
    proc->tf = NULL;
    proc->pgdir = boot_pgdir_pa;
    proc->flags = 0;

    // lab5 function2 2412074
    // Initialize process exit status and family links.
    proc->exit_code = 0;
    proc->wait_state = 0;
    proc->cptr = NULL;
    proc->yptr = NULL;
    proc->optr = NULL;

    list_init(&(proc->list_link));
    list_init(&(proc->hash_link));

    return proc;
}




// Initialize process management and create the first kernel thread.
void
proc_init(void)
{
    int i;
    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i++) {
        list_init(&hash_list[i]);
    }

    // The idle process uses the existing boot kernel stack.
    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    idleproc->pgdir = boot_pgdir_pa;

    nr_process = 1;
    current = idleproc;

    set_proc_name(idleproc, "idle");

    // Create the first ordinary kernel thread.
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0) {
        panic("cannot create initproc.\n");
    }

    initproc = find_proc(pid);
    assert(initproc != NULL && initproc->pid == 1);

    set_proc_name(initproc, "init");
    assert(nr_process == 2);
}


// Create a kernel thread with the specified entry function and argument.
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));

    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.epc = (uintptr_t)kernel_thread_entry;

    // Return to supervisor mode with interrupts enabled.
    tf.status = read_csr(sstatus);
    tf.status |= SSTATUS_SPP | SSTATUS_SPIE;
    tf.status &= ~SSTATUS_SIE;

    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}


// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

// lab5 function4 2412074
// Detach the current address space and release its last reference.
static void
release_current_mm(void)
{
    struct mm_struct *mm = current->mm;

    if (mm == NULL) {
        return;
    }

    // Switch away from the old page table before freeing it.
    current->pgdir = boot_pgdir_pa;
    write_csr(satp, 0x8000000000000000UL |
                    (boot_pgdir_pa >> RISCV_PGSHIFT));
    asm volatile("sfence.vma" ::: "memory");

    current->mm = NULL;

    if (mm_count_dec(mm) == 0) {
        exit_mmap(mm);
        put_pgdir(mm);
        mm_destroy(mm);
    }
}


// lab5 function2 2412074
// Load an ELF image into a newly created user address space.
static int
load_icode(unsigned char *binary, size_t size)
{
    assert(current->mm == NULL);
    assert(current->tf != NULL);

    int ret = -E_INVAL_ELF;

    // Check ELF header and program-header table boundaries.
    if (binary == NULL || size < sizeof(struct elfhdr)) {
        return ret;
    }

    struct elfhdr *elf = (struct elfhdr *)binary;

    if (elf->e_magic != ELF_MAGIC ||
        elf->e_phentsize != sizeof(struct proghdr) ||
        elf->e_phoff > size ||
        elf->e_phnum == 0 ||
        elf->e_phnum >
            (size - elf->e_phoff) / sizeof(struct proghdr)) {
        return ret;
    }

    struct mm_struct *mm = mm_create();

    if (mm == NULL) {
        return -E_NO_MEM;
    }

    if ((ret = setup_pgdir(mm)) != 0) {
        goto bad_mm;
    }

    struct proghdr *ph = (struct proghdr *)
        (binary + elf->e_phoff);

    // Load every PT_LOAD segment.
    for (size_t i = 0; i < elf->e_phnum; i++, ph++) {
        if (ph->p_type != ELF_PT_LOAD) {
            continue;
        }

        ret = -E_INVAL_ELF;

        if (ph->p_filesz > ph->p_memsz ||
            ph->p_offset > size ||
            ph->p_filesz > size - ph->p_offset) {
            goto bad_mmap;
        }

        if (ph->p_memsz == 0) {
            continue;
        }

        uintptr_t start = ph->p_va;
        uintptr_t stack_bottom = USTACKTOP - USTACKSIZE;

        // Reject overflow and overlap with the reserved user stack.
        if (start < USERBASE ||
            start >= stack_bottom ||
            ph->p_memsz > stack_bottom - start) {
            goto bad_mmap;
        }

        uintptr_t end = start + ph->p_memsz;
        uintptr_t file_end = start + ph->p_filesz;

        uint32_t vm_flags = 0;
        uint32_t perm = PTE_U | PTE_A;

        if (ph->p_flags & ELF_PF_R) {
            vm_flags |= VM_READ;
            perm |= PTE_R;
        }

        if (ph->p_flags & ELF_PF_W) {
            // RISC-V does not permit writable, unreadable leaf PTEs.
            vm_flags |= VM_READ | VM_WRITE;
            perm |= PTE_R | PTE_W | PTE_D;
        }

        if (ph->p_flags & ELF_PF_X) {
            vm_flags |= VM_EXEC;
            perm |= PTE_X;
        }

        if (vm_flags == 0) {
            goto bad_mmap;
        }

        ret = mm_map(mm, start, ph->p_memsz, vm_flags, NULL);

        if (ret != 0) {
            goto bad_mmap;
        }

        // Allocate each page and initialize it before copying file data.
        for (uintptr_t va = ROUNDDOWN(start, PGSIZE);
             va < ROUNDUP(end, PGSIZE);
             va += PGSIZE) {

            struct Page *page =
                pgdir_alloc_page(mm->pgdir, va, perm);

            if (page == NULL) {
                ret = -E_NO_MEM;
                goto bad_mmap;
            }

            unsigned char *dst = page2kva(page);

            // Zero the page, including BSS and unused bytes.
            memset(dst, 0, PGSIZE);

            uintptr_t copy_start = va > start ? va : start;
            uintptr_t page_end = va + PGSIZE;
            uintptr_t copy_end =
                page_end < file_end ? page_end : file_end;

            if (copy_start < copy_end) {
                memcpy(dst + (copy_start - va),
                       binary + ph->p_offset +
                           (copy_start - start),
                       copy_end - copy_start);
            }
        }
    }

    // The entry point must be inside an executable user mapping.
    ret = -E_INVAL_ELF;

    if (!USER_ACCESS(elf->e_entry, elf->e_entry + 1)) {
        goto bad_mmap;
    }

    pte_t *entry_pte = get_pte(mm->pgdir, elf->e_entry, 0);

    if (entry_pte == NULL ||
        (*entry_pte & (PTE_V | PTE_U | PTE_X)) !=
            (PTE_V | PTE_U | PTE_X)) {
        goto bad_mmap;
    }

    // Create the user stack VMA.
    ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE,
                 VM_READ | VM_WRITE | VM_STACK, NULL);

    if (ret != 0) {
        goto bad_mmap;
    }

    // Allocate the stack eagerly, leaving the lowest page as a guard.
    for (uintptr_t va = USTACKTOP - USTACKSIZE + PGSIZE;
         va < USTACKTOP; va += PGSIZE) {

        struct Page *page = pgdir_alloc_page(
            mm->pgdir, va,
            PTE_U | PTE_R | PTE_W | PTE_A | PTE_D);

        if (page == NULL) {
            ret = -E_NO_MEM;
            goto bad_mmap;
        }

        memset(page2kva(page), 0, PGSIZE);
    }

    // Install the new address space.
    mm_count_inc(mm);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);

    write_csr(satp, 0x8000000000000000UL |
                    (current->pgdir >> RISCV_PGSHIFT));

    asm volatile("sfence.vma" ::: "memory");
    // FENCE.I: synchronize instruction fetch after loading ELF code.
    // Use its encoding because the current assembler target omits Zifencei.
    asm volatile(".word 0x0000100f" ::: "memory");

    // Prepare the initial U-mode context.
    struct trapframe *tf = current->tf;

    uintptr_t status = read_csr(sstatus);

    memset(tf, 0, sizeof(*tf));

    tf->gpr.sp = USTACKTOP;
    tf->epc = elf->e_entry;

    // sret must return to U-mode with interrupts enabled.
    tf->status = (status & ~(SSTATUS_SPP | SSTATUS_SIE))
                 | SSTATUS_SPIE;

    return 0;

bad_mmap:
    exit_mmap(mm);
    put_pgdir(mm);

bad_mm:
    mm_destroy(mm);
    return ret;
}


// Copy a mapped user buffer through kernel page aliases.
// This avoids directly dereferencing an untrusted user pointer.
static bool
exec_copy_user(struct mm_struct *mm, void *dst,
               const void *src, size_t len)
{
    uintptr_t addr = (uintptr_t)src;

    if (len == 0) {
        return 1;
    }

    if (!user_mem_check(mm, addr, len, 0)) {
        return 0;
    }

    unsigned char *out = dst;

    while (len != 0) {
        pte_t *ptep = get_pte(mm->pgdir, addr, 0);
        uint32_t need = PTE_V | PTE_U | PTE_R;

        if (ptep == NULL || (*ptep & need) != need) {
            return 0;
        }

        size_t offset = addr % PGSIZE;
        size_t n = PGSIZE - offset;

        if (n > len) {
            n = len;
        }

        unsigned char *page =
            page2kva(pte2page(*ptep));

        memcpy(out, page + offset, n);

        out += n;
        addr += n;
        len -= n;
    }

    return 1;
}


// lab5 function2 2412074
// Replace the current process image with a new ELF program.
int
do_execve(const char *name, size_t len,
          unsigned char *binary, size_t size)
{
    if (name == NULL || binary == NULL ||
        size < sizeof(struct elfhdr) ||
        current->tf == NULL) {
        return -E_INVAL;
    }

    struct mm_struct *mm = current->mm;
    char local_name[PROC_NAME_LEN + 1];

    memset(local_name, 0, sizeof(local_name));

    if (len > PROC_NAME_LEN) {
        len = PROC_NAME_LEN;
    }

    struct Page *snapshot = NULL;
    size_t snapshot_pages = 0;
    unsigned char *image = binary;

    if (mm != NULL) {
        // Save the name and ELF data before destroying the old mm.
        if (!exec_copy_user(mm, local_name, name, len) ||
            !user_mem_check(mm, (uintptr_t)binary, size, 0)) {
            return -E_INVAL;
        }

        snapshot_pages = ROUNDUP(size, PGSIZE) / PGSIZE;
        snapshot = alloc_pages(snapshot_pages);

        if (snapshot == NULL) {
            return -E_NO_MEM;
        }

        image = page2kva(snapshot);

        if (!exec_copy_user(mm, image, binary, size)) {
            free_pages(snapshot, snapshot_pages);
            return -E_INVAL;
        }
    } else {
        // The first user ELF is embedded in the kernel image.
        if ((len != 0 &&
             !user_mem_check(NULL, (uintptr_t)name, len, 0)) ||
            !user_mem_check(NULL, (uintptr_t)binary, size, 0)) {
            return -E_INVAL;
        }

        memcpy(local_name, name, len);
    }

    release_current_mm();

    int ret = load_icode(image, size);

    if (snapshot != NULL) {
        free_pages(snapshot, snapshot_pages);
    }

    if (ret != 0) {
        // The old image is already gone, so it cannot be resumed.
        do_exit(ret);
        return ret;
    }

    set_proc_name(current, local_name);

    return 0;
}


// lab5 function2 2412074
// Enter the common trap-return path for the first user program.
static int
kernel_execve(const char *name,
              unsigned char *binary, size_t size)
{
    struct trapframe tf;
    struct trapframe *oldtf = current->tf;

    memset(&tf, 0, sizeof(tf));
    current->tf = &tf;

    int ret = do_execve(name, strlen(name), binary, size);

    if (ret != 0) {
        current->tf = oldtf;
        return ret;
    }

    // Place the final trapframe at the canonical kernel-stack top.
    struct trapframe *top = (struct trapframe *)
        (current->kstack + KSTACKSIZE - sizeof(tf));

    bool intr_flag;
    local_intr_save(intr_flag);
    (void)intr_flag;

    current->tf = top;

    // Copy backwards and jump without returning through the old
    // C stack frames, which may overlap the destination trapframe.
    asm volatile(
        "mv t0, %0\n"
        "mv t1, %1\n"
        "mv t2, %2\n"
        "add t0, t0, t2\n"
        "add t1, t1, t2\n"
        "1:\n"
        "addi t0, t0, -8\n"
        "addi t1, t1, -8\n"
        "ld t3, 0(t1)\n"
        "sd t3, 0(t0)\n"
        "addi t2, t2, -8\n"
        "bnez t2, 1b\n"
        "mv sp, %0\n"
        "j __trapret\n"
        :
        : "r"(top), "r"(&tf), "r"(sizeof(tf))
        : "t0", "t1", "t2", "t3", "memory"
    );

    __builtin_unreachable();
}


// lab5 function3 2412074: extend copy_mm and do_fork for user processes.
// Copy or share the parent's address space.
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    struct mm_struct *mm, *oldmm = current->mm;
    int ret = -E_NO_MEM;

    // Kernel threads do not have a user address space.
    if (oldmm == NULL) {
        return 0;
    }

    // Threads with CLONE_VM share the same address space.
    if (clone_flags & CLONE_VM) {
        mm = oldmm;
        goto good_mm;
    }

    // Ordinary fork creates an independent address space.
    if ((mm = mm_create()) == NULL) {
        return ret;
    }

    if ((ret = setup_pgdir(mm)) != 0) {
        goto bad_mm;
    }

    // The caller must hold the source mm lock.
    lock_mm(oldmm);
    ret = dup_mmap(mm, oldmm);
    unlock_mm(oldmm);

    if (ret != 0) {
        // dup_mmap already rolls back copied mappings and VMAs.
        put_pgdir(mm);
        goto bad_mm;
    }

good_mm:
    mm_count_inc(mm);
    proc->mm = mm;
    proc->pgdir = PADDR(mm->pgdir);
    return 0;

bad_mm:
    mm_destroy(mm);
    return ret;
}


// Prepare the initial trapframe and context of a new thread.
static void
copy_thread(struct proc_struct *proc, uintptr_t esp,
            struct trapframe *tf)
{
    // Place the trapframe at the top of the new kernel stack.
    proc->tf = (struct trapframe *)
        (proc->kstack + KSTACKSIZE - sizeof(struct trapframe));

    *(proc->tf) = *tf;

    // A child process initially returns zero from fork.
    proc->tf->gpr.a0 = 0;

    // lab5 function3 2412074
    // Select the initial stack according to the privilege level.
    if (tf->status & SSTATUS_SPP) {
        proc->tf->gpr.sp = (esp != 0) ? esp :
            (proc->kstack + KSTACKSIZE);
    } else {
        proc->tf->gpr.sp = (esp != 0) ? esp : tf->gpr.sp;
    }

    // The first context switch enters forkret.
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}


// Create a new kernel thread and register it in the process system.
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;

    if (nr_process >= MAX_PROCESS) {
        return ret;
    }

    ret = -E_NO_MEM;

    // Allocate and initialize a PCB.
    if ((proc = alloc_proc()) == NULL) {
        return ret;
    }

    proc->parent = current;

    // Allocate the kernel stack.
    if (setup_kstack(proc) != 0) {
        goto bad_fork_cleanup_proc;
    }

    // Set up the memory management information.
    if ((ret = copy_mm(clone_flags, proc)) != 0) {
        goto bad_fork_cleanup_kstack;
    }

    // Prepare the first execution context.
    copy_thread(proc, stack, tf);

    // Protect PID allocation and process-list updates.
    bool intr_flag;
    local_intr_save(intr_flag);

    proc->pid = get_pid();
    hash_proc(proc);

    // lab5 function3 2412074
    // Register the process and establish family links.
    set_links(proc);

    // Make the new thread runnable before leaving the critical section.
    wakeup_proc(proc);

    local_intr_restore(intr_flag);

    return proc->pid;

bad_fork_cleanup_kstack:
    put_kstack(proc);

bad_fork_cleanup_proc:
    kfree(proc);
    return ret;
}


// lab5 function2 2412074
// Check that the exit-code destination is writable and mapped.
static bool
wait_store_valid(struct mm_struct *mm, int *store)
{
    if (store == NULL) {
        return 1;
    }

    uintptr_t start = (uintptr_t)store;

    if (!user_mem_check(mm, start, sizeof(int), 1)) {
        return 0;
    }

    if (mm != NULL) {
        // Check every page touched by the exit-code destination.
        uintptr_t end = start + sizeof(int);
        uintptr_t addr = ROUNDDOWN(start, PGSIZE);

        for (; addr < end; addr += PGSIZE) {
            pte_t *ptep = get_pte(mm->pgdir, addr, 0);
            uint32_t need = PTE_V | PTE_U | PTE_W;

            if (ptep == NULL || (*ptep & need) != need) {
                return 0;
            }
        }
    }

    return 1;
}

// Wait for a specific child, or any child when pid is zero.
int
do_wait(int pid, int *code_store)
{
    if (pid < 0) {
        return -E_BAD_PROC;
    }

    struct mm_struct *mm = current->mm;

    if (!wait_store_valid(mm, code_store)) {
        return -E_INVAL;
    }

    while (1) {
        struct proc_struct *proc = NULL;
        struct proc_struct *child;
        bool has_child = 0;
        bool intr_flag;

        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }

        local_intr_save(intr_flag);

        if (pid != 0) {
            child = find_proc(pid);

            if (child != NULL && child->parent == current) {
                has_child = 1;

                if (child->state == PROC_ZOMBIE) {
                    proc = child;
                }
            }
        } else {
            // Search all children through the sibling links.
            for (child = current->cptr;
                 child != NULL;
                 child = child->optr) {

                has_child = 1;

                if (child->state == PROC_ZOMBIE) {
                    proc = child;
                    break;
                }
            }
        }

        if (proc != NULL) {
            assert(proc != idleproc && proc != initproc);
            assert(proc->mm == NULL);

            if (code_store != NULL) {
                if (!wait_store_valid(mm, code_store)) {
                    local_intr_restore(intr_flag);
                    return -E_INVAL;
                }

                int code = proc->exit_code;

                if (mm != NULL) {
                    // Permit controlled S-mode access to user memory.
                    uintptr_t status = set_csr(sstatus, SSTATUS_SUM);
                    asm volatile("" ::: "memory");

                    bool ok = copy_to_user(mm, code_store,
                                           &code, sizeof(code));

                    asm volatile("" ::: "memory");
                    if (!(status & SSTATUS_SUM)) {
                        clear_csr(sstatus, SSTATUS_SUM);
                    }

                    if (!ok) {
                        local_intr_restore(intr_flag);
                        return -E_INVAL;
                    }
                } else {
                    *code_store = code;
                }
            }

            // The parent performs final reclamation.
            unhash_proc(proc);
            remove_links(proc);
            local_intr_restore(intr_flag);

            put_kstack(proc);
            kfree(proc);

            return 0;
        }

        if (!has_child) {
            local_intr_restore(intr_flag);
            return -E_BAD_PROC;
        }

        // A matching child exists but has not exited yet.
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;

        local_intr_restore(intr_flag);
        schedule();

        current->wait_state = 0;

        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }
    }
}

// lab5 function4 2412074
int
do_yield(void)
{
    current->need_resched = 1;
    return 0;
}

// lab5 function4 2412074
int
do_kill(int pid)
{
    if (pid <= 0 || pid >= MAX_PID) {
        return -E_INVAL;
    }

    bool intr_flag;
    int ret = -E_BAD_PROC;

    local_intr_save(intr_flag);

    struct proc_struct *proc = find_proc(pid);

    if (proc != NULL &&
        proc != idleproc &&
        proc != initproc &&
        proc->state != PROC_ZOMBIE) {

        if (proc->flags & PF_EXITING) {
            ret = -E_KILLED;
        } else {
            proc->flags |= PF_EXITING;

            if (proc->state == PROC_SLEEPING &&
                (proc->wait_state & WT_INTERRUPTED)) {
                wakeup_proc(proc);
            }

            ret = 0;
        }
    }

    local_intr_restore(intr_flag);
    return ret;
}


// LAB4 function3 2411785
// Member C: 李文慧

// Switch to the specified process.
void
proc_run(struct proc_struct *proc)
{
    if (proc != current) {
        bool intr_flag;
        struct proc_struct *prev = current;

        // Disable interrupts before switching.
        local_intr_save(intr_flag);

        {
            // Update the current process.
            current = proc;

            // All Lab4 kernel threads share one page directory. Only switch SATP
            // and flush the TLB if a future process uses a different page table.
            if (prev->pgdir != proc->pgdir) {
                write_csr(satp,
                    0x8000000000000000 |
                    (proc->pgdir >> RISCV_PGSHIFT));
                asm volatile("sfence.vma" ::: "memory");
            }

            // Switch CPU execution contexts.
            switch_to(&(prev->context), &(proc->context));
        }

        // Restore the previous interrupt state.
        local_intr_restore(intr_flag);
    }
}

// LAB4 function3 2411785
// Member C: 李文慧
// Idle process loop.

void
cpu_idle(void)
{
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}
