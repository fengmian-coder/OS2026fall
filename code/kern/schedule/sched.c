
#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}


// LAB4 function3 2411785
// Member C: 李文慧
// Select the next runnable process and perform scheduling.

void
schedule(void)
{
    bool intr_flag;
    list_entry_t *head = &proc_list;
    list_entry_t *le, *start;
    struct proc_struct *next = NULL;

    // Disable interrupts and save the previous interrupt state.
    local_intr_save(intr_flag);

    // Clear the rescheduling flag of the current process.
    current->need_resched = 0;

    // Start at the list head for idleproc,
    // otherwise start at the current process.
    start = (current == idleproc)
        ? head : &(current->list_link);

    le = start;

    // Search the process list in a circular manner.
    do {
        le = list_next(le);

        // The list head is not a process.
        if (le != head) {
            struct proc_struct *proc =
                le2proc(le, list_link);

            // Select the first runnable process.
            if (proc->state == PROC_RUNNABLE) {
                next = proc;
                break;
            }
        }
    } while (le != start);

    // Fall back to the idle process if no runnable
    // process is found.
    if (next == NULL) {
        next = idleproc;
    }

    // Record the number of times this process runs.
    next->runs++;

    // Switch to the selected process.
    if (next != current) {
        proc_run(next);
    }

    // Restore the previous interrupt state.
    local_intr_restore(intr_flag);
}
