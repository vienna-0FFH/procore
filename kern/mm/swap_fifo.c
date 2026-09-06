#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_fifo.h>
#include <list.h>
#include <error.h>

/* [wikipedia]The simplest Page Replacement Algorithm(PRA) is a FIFO algorithm. The first-in, first-out
 * page replacement algorithm is a low-overhead algorithm that requires little book-keeping on
 * the part of the operating system. The idea is obvious from the name - the operating system
 * keeps track of all the pages in memory in a queue, with the most recent arrival at the back,
 * and the earliest arrival in front. When a page needs to be replaced, the page at the front
 * of the queue (the oldest page) is selected. While FIFO is cheap and intuitive, it performs
 * poorly in practical application. Thus, it is rarely used in its unmodified form. This
 * algorithm experiences Belady's anomaly.
 *
 * Details of FIFO PRA: each address space owns a queue of resident pages,
 * ordered by insertion time. A per-address-space lock keeps queue updates
 * independent on SMP.
 */

static list_entry_t pra_list_head;
static spinlock_t fifo_lock;
/*
 * (2) _fifo_init_mm: attach the address space to the global replacement
 *     queue. A Page has one replacement link and may be shared after fork().
 */
static int
_fifo_init_mm(struct mm_struct *mm)
{
     mm->sm_priv = &pra_list_head;
     return 0;
}

static void
_fifo_cleanup_mm(struct mm_struct *mm)
{
     (void)mm;
}

static void
_fifo_untrack_page(struct Page *page)
{
    if (page == NULL) {
        return;
    }
    spin_lock(&fifo_lock);
    if (!list_empty(&(page->pra_page_link))) {
        list_del_init(&(page->pra_page_link));
    }
    page->pra_mm = NULL;
    page->pra_vaddr = 0;
    spin_unlock(&fifo_lock);
}
/*
 * (3)_fifo_map_swappable: link the most recent arrival page at the back of
 *     this address space's FIFO queue.
 */
static int
_fifo_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head = mm != NULL ? mm->sm_priv : NULL;
    list_entry_t *entry;
 
    if (head == NULL || page == NULL) {
        return -E_INVAL;
    }
    entry = &(page->pra_page_link);
    spin_lock(&fifo_lock);
    if (list_empty(entry)) {
        list_add(head, entry);
        page->pra_mm = mm;
        page->pra_vaddr = addr;
    }
    spin_unlock(&fifo_lock);
    return 0;
}
/*
 *  (4)_fifo_swap_out_victim: unlink the oldest page from this queue and
 *                            return it to the caller.
 */
static int
_fifo_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
     list_entry_t *head = mm != NULL ? mm->sm_priv : NULL;
     if (head == NULL || ptr_page == NULL || in_tick != 0) {
         return -E_INVAL;
     }
     /* Select the oldest entry (the tail). */
     spin_lock(&fifo_lock);
     list_entry_t *le = head->prev;
    if (head == le) {
        spin_unlock(&fifo_lock);
         return -E_NO_MEM;
     }
     struct Page *p = le2page(le, pra_page_link);
     list_del_init(le);
     spin_unlock(&fifo_lock);
     if (p == NULL) {
         return -E_INVAL;
     }
     *ptr_page = p;
     return 0;
}

static int
_fifo_check_swap(void) {
    cprintf("write Virt Page c in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==4);
    cprintf("write Virt Page a in fifo_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==4);
    cprintf("write Virt Page d in fifo_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==4);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==4);
    cprintf("write Virt Page e in fifo_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==5);
    cprintf("write Virt Page a in fifo_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==6);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==7);
    cprintf("write Virt Page c in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==8);
    cprintf("write Virt Page d in fifo_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==9);
    cprintf("write Virt Page e in fifo_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==10);
    cprintf("write Virt Page a in fifo_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==11);
    return 0;
}


static int
_fifo_init(void)
{
    list_init(&pra_list_head);
    spin_init(&fifo_lock);
    return 0;
}

static int
_fifo_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    pte_t *ptep;
    if (mm == NULL) {
        return -E_INVAL;
    }
    ptep = get_pte(mm->pgdir, addr, 0);
    if (ptep != NULL && (*ptep & PTE_P)) {
        struct Page *page = pte2page(*ptep);
        if (!list_empty(&(page->pra_page_link))) {
            spin_lock(&fifo_lock);
            list_del_init(&(page->pra_page_link));
            page->pra_mm = NULL;
            page->pra_vaddr = 0;
            spin_unlock(&fifo_lock);
        }
    }
    return 0;
}

static int
_fifo_tick_event(struct mm_struct *mm)
{ return 0; }


struct swap_manager swap_manager_fifo =
{
     .name            = "fifo swap manager",
     .init            = &_fifo_init,
     .init_mm         = &_fifo_init_mm,
     .cleanup_mm      = &_fifo_cleanup_mm,
     .untrack_page    = &_fifo_untrack_page,
     .tick_event      = &_fifo_tick_event,
     .map_swappable   = &_fifo_map_swappable,
     .set_unswappable = &_fifo_set_unswappable,
     .swap_out_victim = &_fifo_swap_out_victim,
     .check_swap      = &_fifo_check_swap,
};
