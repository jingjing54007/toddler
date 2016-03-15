/*
 * Thread manager
 */


#include "common/include/data.h"
#include "common/include/memory.h"
#include "kernel/include/hal.h"
#include "kernel/include/mem.h"
#include "kernel/include/proc.h"


static int thread_salloc_id;


static ulong gen_thread_id(struct thread *t)
{
    ulong id = (ulong)t;
    return id;
}


struct thread *create_thread(
    struct process *p, ulong entry_point, ulong param,
    int pin_cpu_id, ulong stack_size, ulong tls_size
)
{
    // Allocate a thread struct
    struct thread *t = (struct thread *)salloc(thread_salloc_id);
    assert(t);
    
    // Assign a thread id
    t->thread_id = gen_thread_id(t);
    
    // Setup the thread
    t->proc_id = p->proc_id;
    t->proc = p;
    t->state = thread_enter;
    
    // Thread memory
    
    
    // Context
    
    // Scheduling
    t->pin_cpu_id = pin_cpu_id;
    
    // Done
    return t;
}


void init_thread()
{
    kprintf("Initializing thread manager\n");
    
    // Create salloc obj
    thread_salloc_id = salloc_create(sizeof(struct thread), 0, 0, NULL, NULL);
    
    // Create dummy kernel threads, one for each CPU
    int i;
    for (i = 0; i < hal->num_cpus; i++) {
        ulong param = i;
        create_thread(kernel_proc, &kernel_dummy_thread, param, i, 0, 0);
    }
    
    kprintf("\tThread salloc ID: %d\n", thread_salloc_id);
}
