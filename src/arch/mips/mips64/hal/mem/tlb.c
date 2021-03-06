#include "common/include/data.h"
#include "common/include/memory.h"
#include "hal/include/print.h"
#include "hal/include/lib.h"
#include "hal/include/cpu.h"
#include "hal/include/int.h"
#include "hal/include/mem.h"


static int tlb_entry_count = 0;
static int reserved_tlb_entry_count = 0;

dec_per_cpu(struct page_frame *, cur_page_dir);


void write_tlb_entry(int index, ulong hi, ulong pm, ulong lo0, ulong lo1)
{
    u32 entry_index = index;
    
    // Find a random entry
    if (index < 0) {
        read_cp0_random(entry_index);
    }
    
    // Write to TLB
    write_cp0_entry_hi(hi);
    write_cp0_page_mask(pm);
    write_cp0_entry_lo0(lo0);
    write_cp0_entry_lo1(lo1);
    write_cp0_index(entry_index);
    
    // Apply the changes
    __asm__ __volatile__ (
        "ehb;"                  // clear hazard barrier
        "tlbwi;"                // write indexed entry
        :
        :
    );
}

void map_tlb_entry_user(
    int index, ulong asid, ulong vaddr,
    int valid0, ulong pfn0, int write0,
    int valid1, ulong pfn1, int write1)
{
    struct tlb_entry tlb;
//     struct cp0_entry_hi hi;
    
//     __asm__ __volatile__ (
//         "mfc0   %0, $10;"   // hi
//         : "=r" (hi.value)
//         :
//     );
    
    tlb.hi.value = 0;
    tlb.hi.asid = asid;
    tlb.hi.vpn2 = vaddr >> 13;
    
    tlb.pm.value = 0;
    tlb.pm.mask_ex = USER_PAGE_MASK_EX;
    tlb.pm.mask = USER_PAGE_MASK;
    
    tlb.lo0.value = 0;
    tlb.lo0.valid = valid0;
    tlb.lo0.pfn = pfn0;
    tlb.lo0.dirty = write0;
    tlb.lo0.coherent = 0x6;
    
    tlb.lo1.value = 0;
    tlb.lo1.valid = valid1;
    tlb.lo1.pfn = pfn1;
    tlb.lo1.dirty = write1;
    tlb.lo1.coherent = 0x6;
    
//     kprintf("To map user TLB entry, asid: %lx, vaddr @ %lx"
//             ", valid0: %d, write0: %d, pfn0 @ %lx"
//             ", valid1: %d, write1: %d, pfn1 @ %lx\n",
//             asid, vaddr, valid0, write0, pfn0, valid1, write1, pfn1);
    
    write_tlb_entry(index, tlb.hi.value, tlb.pm.value, tlb.lo0.value, tlb.lo1.value);
    
//     // Restore current ASID
//     __asm__ __volatile__ (
//         "mtc0   %0, $10;"   // hi
//         :
//         : "r" (hi.value)
//     );
}

static void map_tlb_entry_kernel(ulong addr)
{
    ulong mask = (KERNEL_PAGE_MASK << 12) | (KERNEL_PAGE_MASK_EX << 10) | 0x3ff;
    ulong physical_pfn = addr & ~mask;
    
    struct tlb_entry tlb;
    
    tlb.hi.value = 0;
    tlb.hi.vpn2 = addr >> 13;
    
    tlb.pm.value = 0;
    tlb.pm.mask_ex = KERNEL_PAGE_MASK_EX;
    tlb.pm.mask = KERNEL_PAGE_MASK;
    
    tlb.lo0.value = 0;
    tlb.lo0.pfn = physical_pfn;
    tlb.lo0.valid = 1;
    tlb.lo0.dirty = 1;
    tlb.lo0.coherent = 0x6;
    
    tlb.lo1.value = 0;
    tlb.lo1.pfn = physical_pfn | (mask + 0x1);
    tlb.lo1.valid = 1;
    tlb.lo1.dirty = 1;
    tlb.lo1.coherent = 0x6;
    
    write_tlb_entry(-1, tlb.hi.value, tlb.pm.value, tlb.lo0.value, tlb.lo1.value);
}

static int tlb_probe_kernel(ulong addr)
{
    int index = -1;
    struct cp0_entry_hi hi;
    
    hi.value = 0;
    hi.vpn2 = addr >> (PAGE_BITS + 1);
    
    __asm__ __volatile__ (
        "mtc0   %1, $10;"   // hi
        "ehb;"
        "tlbp;"
        "mfc0   %0, $0;"    // index
        : "=r" (index)
        : "r" (hi.value)
    );
    
    return index;
}

static ulong read_asid()
{
    struct cp0_entry_hi hi;
    read_cp0_entry_hi(hi.value);
    return hi.asid;
}

static void set_asid(ulong asid)
{
    struct cp0_entry_hi hi;
    
    read_cp0_entry_hi(hi.value);
    hi.asid = asid;
    write_cp0_entry_hi(hi.value);
}

int tlb_refill_kernel(ulong addr)
{
    kprintf("Kernel TLB refill @ %lx\n", addr);
    map_tlb_entry_kernel(addr);
    kprintf("TLB done\n");
    return 0;
}

// Note that we are running this function in kernel mode, ASID is already set to be 0
int tlb_refill_user(ulong addr)
{
    kprintf("User TLB refill @ %lx ... ", addr);
    
    // User's ASID
    ulong user_asid = read_asid();
    
    // Align the addr to double-page boundary
    addr &= ~((PAGE_SIZE << 1) - 1);
    
    // Obtain page table
    struct page_frame **cur_page_dir = get_per_cpu(struct page_frame *, cur_page_dir);
    volatile struct page_frame *page = *cur_page_dir;
    page = (void *)PHYS_TO_KDATA((ulong)page);
    
    kprintf("Page @ %p\n", page);
    
    // Page table lookup result
    int valid0 = 0, valid1 = 0;
    ulong pfn0 = 0, pfn1 = 0;
    int write0 = 0, write1 = 0;
    
    // Look up the page table
    int i;
    int shift = PAGE_BITS + PAGE_ENTRY_BITS * (PAGE_LEVELS - 1);
    int index = (addr >> shift) & (PAGE_ENTRY_COUNT - 1);
    int block_shift = PAGE_BITS;
    
    for (i = 0; i < PAGE_LEVELS - 1; i++) {
        // Check if mapping is valid
        if (!page->entries[index].value) {
            panic("Doesn't support page fault yet!");
            return 0;
        }
        
        // Move to next level
        if (page->entries[index].has_next_level) {
            page = (struct page_frame *)PHYS_TO_KDATA(PFN_TO_ADDR((ulong)page->entries[index].pfn));
            shift -= PAGE_ENTRY_BITS;
            index = (addr >> shift) & (PAGE_ENTRY_COUNT - 1);
            block_shift += PAGE_ENTRY_BITS;
        }
        
        // Done at this level
        else {
            valid0 = valid1 = 1;
            pfn0 = pfn1 = (ulong)page->entries[index].pfn;
            write0 = write1 = (ulong)page->entries[index].write_allow;
            goto __do_write;
        }
    }
    
    // The final level
    if (page->entries[index].present) {
        assert(!page->entries[index].has_next_level);
        valid0 = 1;
        pfn0 = (ulong)page->entries[index].pfn;
        write0 = page->entries[index].write_allow;
    }
    
    if (page->entries[index + 1].present) {
        assert(!page->entries[index + 1].has_next_level);
        valid1 = 1;
        pfn1 = (ulong)page->entries[index + 1].pfn;
        write1 = page->entries[index + 1].write_allow;
    }
    
__do_write:
    map_tlb_entry_user(-1, user_asid, addr, valid0, pfn0, write0, valid1, pfn1, write1);
    set_asid(user_asid);
    
    return 0;
    
    
//     ulong user_asid = read_asid();
//     set_asid(0);
//     
//     struct page_frame **cur_page_dir = get_per_cpu(struct page_frame *, cur_page_dir);
//     struct page_frame *page = *cur_page_dir;
//     u32 page_addr = (u32)page;
//     struct tlb_entry tlb;
//     
//     // First map page dir
//     int dir_index = tlb_probe_kernel(page_addr);
//     if (dir_index < 0) {
//         map_tlb_entry_kernel(page_addr);
//     }
//     dir_index = tlb_probe_kernel(page_addr);
//     assert(dir_index >= 0);
//     
//     // Access the page dir
//     u32 table_pfn = page->value_pde[GET_PDE_INDEX(addr)].pfn;
//     page_addr = PFN_TO_ADDR(table_pfn);
//     page = (struct page_frame *)page_addr;
//     
//     // Next map page table
//     int table_index = tlb_probe_kernel(page_addr);
//     if (table_index < 0) {
//         map_tlb_entry_kernel(page_addr);
//     }
//     dir_index = tlb_probe_kernel(page_addr);
//     assert(dir_index >= 0);
//     
//     // Access the page table
//     u32 pte_index0 = GET_PTE_INDEX(addr) & ~0x1;
//     u32 page_pfn0 = page->value_pte[pte_index0].pfn;
//     int write0 = page->value_pte[pte_index0].write_allow;
//     
//     u32 pte_index1 = pte_index0 | 0x1;
//     u32 page_pfn1 = page->value_pte[pte_index1].pfn;
//     int write1 = page->value_pte[pte_index1].write_allow;
//     
//     // Finally map the actual page
//     map_tlb_entry_user(dir_index, user_asid, addr, page_pfn0, write0, page_pfn1, write1);
//     
//     // Restore ASID
//     set_asid(user_asid);
//     
//     kprintf("done!\n");
    
    return 0;
}

static no_opt void invalidate_tlb(ulong asid, ulong vaddr)
{
    kprintf("TLB shootdown @ %lx, ASID: %lx ...", vaddr, asid);
    
    int index = -1;
    struct cp0_entry_hi hi;
    ulong hi_old = 0;
    
    // Read the old value of HI
    __asm__ __volatile__ (
        "mfc0   %0, $10;"   // hi
        : "=r" (hi.value)
        :
    );
    hi_old = hi.value;
    
    // Set ASID and Vaddr
    hi.asid = asid;
    hi.vpn2 = vaddr >> 13;
    
    // Write HI and do a TLB probe
    __asm__ __volatile__ (
        "mtc0   %1, $10;"   // hi
        "ehb;"
        "tlbp;"
        "mfc0   %0, $0;"    // index
        : "=r" (index)
        : "r" (hi.value)
    );
    
//     kprintf(" index @ %x ...", index);
    
    // If there's no match then there's no need to do an invalidation
    if (index >= 0) {
        // Write to TLB to do an invalidation
        __asm__ __volatile__ (
            "mtc0   $0, $10;"       // hi
            "mtc0   $0, $5;"        // pm
            "mtc0   $0, $2;"        // lo0
            "mtc0   $0, $3;"        // lo1
            "mtc0   %0, $0;"        // index
            "ehb;"                  // clear hazard barrier
            "tlbwi;"                // write indexed entry
            :
            : "r" (index)
            : "memory"
        );
    }
    
    // Restore old HI
    __asm__ __volatile__ (
        "mtc0   %0, $10;"   // hi
        :
        : "r" (hi_old)
    );
    
//     kprintf(" done");
}

void invalidate_tlb_array(ulong asid, ulong vaddr, size_t size)
{
    ulong vstart = ALIGN_DOWN(vaddr, PAGE_SIZE);
    ulong vend = ALIGN_UP(vaddr + size, PAGE_SIZE);
    ulong page_count = (vend - vstart) >> PAGE_BITS;
    
    ulong i;
    ulong vcur = vstart;
    for (i = 0; i < page_count; i++) {
        invalidate_tlb(asid, vcur);
        vcur += PAGE_SIZE;
    }
}


void init_tlb()
{
    int i;
    
    // Get TLB entry count
    __asm__ __volatile__ (
        "mfc0   %0, $16, 1;"    // Read config reg - CP0 reg 16 sel 1
        : "=r" (tlb_entry_count)
        :
    );
    
    tlb_entry_count >>= 25;
    tlb_entry_count &= 0x3F;
    tlb_entry_count += 1;
    
    // Set wired limit to zero
    __asm__ __volatile__ (
        "mtc0   $0, $6;"        // wired - 0
        :
        :
    );
    
    // Zero all TLB entries
    for (i = 0; i < tlb_entry_count; i++) {
        write_tlb_entry(i, 0, 0, 0, 0);
    }
    
    // Set up page grain config
    struct cp0_page_grain grain;
    read_cp0_page_mask(grain.value);
    grain.value = 0;
    grain.iec = 1;
    grain.elpa = 1;
    grain.xie = 1;
    grain.rie = 1;
    write_cp0_page_mask(grain.value);
    
    kprintf("TLB initialized, entry count: %d\n", tlb_entry_count);
}
