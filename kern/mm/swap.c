#include <swap.h>
#include <swapfs.h>
#include <swap_fifo.h>
#include <stdio.h>
#include <string.h>
#include <memlayout.h>
#include <pmm.h>
#include <mmu.h>
#include <default_pmm.h>
#include <kdebug.h>
#include <error.h>
#include <kmalloc.h>
#include <sync.h>

// the valid vaddr for check is between 0~CHECK_VALID_VADDR-1
#define CHECK_VALID_VIR_PAGE_NUM 5
#define BEING_CHECK_VALID_VADDR 0X1000
#define CHECK_VALID_VADDR (CHECK_VALID_VIR_PAGE_NUM+1)*0x1000
// the max number of valid physical page for check
#define CHECK_VALID_PHY_PAGE_NUM 4
// the max access seq number
#define MAX_SEQ_NO 10

static struct swap_manager *sm;
size_t max_swap_offset;

volatile int swap_init_ok = 0;

/* Swap slots are independent of virtual addresses.  A reference count lets
 * fork() share a non-present entry until one address space faults it in. */
static uint16_t *swap_slot_refs;
static size_t swap_slot_count;
static spinlock_t swap_slot_lock;

static bool
swap_entry_valid(swap_entry_t entry, size_t *slot_store) {
     size_t slot = (size_t)(entry >> 8);
     if (slot == 0 || slot >= swap_slot_count || swap_slot_refs == NULL) {
          return 0;
     }
     if (slot_store != NULL) {
          *slot_store = slot;
     }
     return 1;
}

static int
swap_alloc_entry(swap_entry_t *entry_store) {
     size_t slot;
     bool intr_flag;
     if (entry_store == NULL || swap_slot_refs == NULL) {
          return -E_NO_MEM;
     }
     local_intr_save(intr_flag);
     spin_lock(&swap_slot_lock);
     for (slot = 1; slot < swap_slot_count; slot++) {
          if (swap_slot_refs[slot] == 0) {
               swap_slot_refs[slot] = 1;
               *entry_store = (swap_entry_t)(slot << 8);
               spin_unlock(&swap_slot_lock);
               local_intr_restore(intr_flag);
               return 0;
          }
     }
     spin_unlock(&swap_slot_lock);
     local_intr_restore(intr_flag);
     return -E_NO_MEM;
}

int
swap_duplicate_entry(swap_entry_t entry) {
     size_t slot;
     bool intr_flag;
     if (!swap_entry_valid(entry, &slot)) {
          return -E_INVAL;
     }
     local_intr_save(intr_flag);
     spin_lock(&swap_slot_lock);
     if (swap_slot_refs[slot] == 0 || swap_slot_refs[slot] == 0xffff) {
          spin_unlock(&swap_slot_lock);
          local_intr_restore(intr_flag);
          return -E_INVAL;
     }
     swap_slot_refs[slot]++;
     spin_unlock(&swap_slot_lock);
     local_intr_restore(intr_flag);
     return 0;
}

void
swap_release_entry(swap_entry_t entry) {
     size_t slot;
     bool intr_flag;
     if (!swap_entry_valid(entry, &slot)) {
          return;
     }
     local_intr_save(intr_flag);
     spin_lock(&swap_slot_lock);
     if (swap_slot_refs[slot] != 0) {
          swap_slot_refs[slot]--;
     }
     spin_unlock(&swap_slot_lock);
     local_intr_restore(intr_flag);
}

unsigned int swap_page[CHECK_VALID_VIR_PAGE_NUM];

unsigned int swap_in_seq_no[MAX_SEQ_NO],swap_out_seq_no[MAX_SEQ_NO];

static void check_swap(void);

int
swap_init(void)
{
     swapfs_init();

     if (!(1024 <= max_swap_offset && max_swap_offset < MAX_SWAP_OFFSET_LIMIT))
     {
          panic("bad max_swap_offset %08x.\n", max_swap_offset);
     }

     swap_slot_count = max_swap_offset;
     swap_slot_refs = kmalloc(swap_slot_count * sizeof(*swap_slot_refs));
     if (swap_slot_refs == NULL) {
          panic("swap: cannot allocate slot table (%d entries).\n",
                swap_slot_count);
     }
     memset(swap_slot_refs, 0,
            swap_slot_count * sizeof(*swap_slot_refs));
     spin_init(&swap_slot_lock);
     

     sm = &swap_manager_fifo;
     int r = sm->init();
     
     if (r == 0)
     {
          swap_init_ok = 1;
          cprintf("SWAP: manager = %s\n", sm->name);
          check_swap();
     }

     return r;
}

int
swap_init_mm(struct mm_struct *mm)
{
     return (sm != NULL && sm->init_mm != NULL) ? sm->init_mm(mm) : -E_INVAL;
}

void
swap_cleanup_mm(struct mm_struct *mm)
{
     if (sm != NULL && sm->cleanup_mm != NULL) {
          sm->cleanup_mm(mm);
     }
}

void
swap_untrack_page(struct Page *page)
{
     if (sm != NULL && sm->untrack_page != NULL && page != NULL) {
          sm->untrack_page(page);
     }
}

int
swap_tick_event(struct mm_struct *mm)
{
     return (sm != NULL && sm->tick_event != NULL) ? sm->tick_event(mm) : -E_INVAL;
}

int
swap_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
     return (sm != NULL && sm->map_swappable != NULL) ?
            sm->map_swappable(mm, addr, page, swap_in) : -E_INVAL;
}

int
swap_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
     return (sm != NULL && sm->set_unswappable != NULL) ?
            sm->set_unswappable(mm, addr) : -E_INVAL;
}

volatile unsigned int swap_out_num=0;

int
swap_out(struct mm_struct *mm, int n, int in_tick)
{
     int swapped = 0, scanned = 0;
     if (mm == NULL || n <= 0) {
          return 0;
     }
     lock_mm(mm);
     while (swapped < n && scanned < SWAP_VICTIM_SCAN_LIMIT)
     {
          uintptr_t v;
          //struct Page **ptr_page=NULL;
          struct Page *page;
          // cprintf("i %d, SWAP: call swap_out_victim\n",i);
          int r = (sm != NULL && sm->swap_out_victim != NULL) ?
                  sm->swap_out_victim(mm, &page, in_tick) : -E_INVAL;
          scanned++;
          if (r != 0) {
                  break;
          }          
          //assert(!PageReserved(page));

          //cprintf("SWAP: choose victim page 0x%08x\n", page);
          
          struct mm_struct *owner = page->pra_mm;
          if (owner == NULL || page_ref(page) != 1) {
                    continue;
          }
          bool owner_locked = 0;
          if (owner != mm) {
                    if (!try_lock_mm(owner)) {
                         sm->map_swappable(owner, page->pra_vaddr, page, 0);
                         continue;
                    }
                    owner_locked = 1;
          }
          v = page->pra_vaddr;
          pte_t *ptep = get_pte(owner->pgdir, v, 0);
          if (ptep == NULL || !(*ptep & PTE_P) ||
              pte2page(*ptep) != page) {
                    sm->map_swappable(owner, v, page, 0);
                    if (owner_locked) {
                         unlock_mm(owner);
                    }
                    continue;
          }

          swap_entry_t entry;
          if (swap_alloc_entry(&entry) != 0) {
                    sm->map_swappable(owner, v, page, 0);
                    if (owner_locked) {
                         unlock_mm(owner);
                    }
                    break;
          }

          if (swapfs_write(entry, page) != 0) {
                    swap_release_entry(entry);
                    cprintf("SWAP: failed to save\n");
                    sm->map_swappable(owner, v, page, 0);
                    if (owner_locked) {
                         unlock_mm(owner);
                    }
                    continue;
          }
          else {
#if SWAP_DEBUG
                    cprintf("swap_out: page vaddr 0x%x -> slot %d\n",
                            v, entry >> 8);
#endif
                    *ptep = entry;
                    if (page_ref_dec(page) == 0) {
                         free_page(page);
                    }
                    swapped++;
          }
          
          tlb_invalidate(owner->pgdir, v);
          if (owner_locked) {
               unlock_mm(owner);
          }
     }
     unlock_mm(mm);
     return swapped;
}

int
swap_in(struct mm_struct *mm, uintptr_t addr, struct Page **ptr_result)
{
     struct Page *result = alloc_page();
     if (result == NULL || mm == NULL || ptr_result == NULL) {
          return -E_NO_MEM;
     }

     pte_t *ptep = get_pte(mm->pgdir, addr, 0);
     // cprintf("SWAP: load ptep %x swap entry %d to vaddr 0x%08x, page %x, No %d\n", ptep, (*ptep)>>8, addr, result, (result-pages));
    
     if (ptep == NULL || *ptep == 0 || (*ptep & PTE_P) ||
         !swap_entry_valid(*ptep, NULL)) {
         free_page(result);
         return -E_INVAL;
     }
     int r;
     if ((r = swapfs_read((*ptep), result)) != 0)
     {
        free_page(result);
        return r;
     }
#if SWAP_DEBUG
     cprintf("swap_in: load disk swap entry %d with swap_page in vadr 0x%x\n", (*ptep)>>8, addr);
#endif
     *ptr_result=result;
     return 0;
}



static inline void
check_content_set(void)
{
     *(unsigned char *)0x1000 = 0x0a;
     assert(pgfault_num==1);
     *(unsigned char *)0x1010 = 0x0a;
     assert(pgfault_num==1);
     *(unsigned char *)0x2000 = 0x0b;
     assert(pgfault_num==2);
     *(unsigned char *)0x2010 = 0x0b;
     assert(pgfault_num==2);
     *(unsigned char *)0x3000 = 0x0c;
     assert(pgfault_num==3);
     *(unsigned char *)0x3010 = 0x0c;
     assert(pgfault_num==3);
     *(unsigned char *)0x4000 = 0x0d;
     assert(pgfault_num==4);
     *(unsigned char *)0x4010 = 0x0d;
     assert(pgfault_num==4);
}

static inline int
check_content_access(void)
{
    int ret = sm->check_swap();
    return ret;
}

struct Page * check_rp[CHECK_VALID_PHY_PAGE_NUM];
pte_t * check_ptep[CHECK_VALID_PHY_PAGE_NUM];
unsigned int check_swap_addr[CHECK_VALID_VIR_PAGE_NUM];

extern free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void
check_swap(void)
{
    //backup mem env
     int ret, count = 0, total = 0, i;
     list_entry_t *le = &free_list;
     while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
     }
     assert(total == nr_free_pages());
     cprintf("BEGIN check_swap: count %d, total %d\n",count,total);
     
     //now we set the phy pages env     
     struct mm_struct *mm = mm_create();
     assert(mm != NULL);

     extern struct mm_struct *check_mm_struct;
     assert(check_mm_struct == NULL);

     check_mm_struct = mm;

     pde_t *pgdir = mm->pgdir = boot_pgdir;
     assert(pgdir[0] == 0);

     struct vma_struct *vma = vma_create(BEING_CHECK_VALID_VADDR, CHECK_VALID_VADDR, VM_WRITE | VM_READ);
     assert(vma != NULL);

     insert_vma_struct(mm, vma);

     //setup the temp Page Table vaddr 0~4MB
     cprintf("setup Page Table for vaddr 0X1000, so alloc a page\n");
     pte_t *temp_ptep=NULL;
     temp_ptep = get_pte(mm->pgdir, BEING_CHECK_VALID_VADDR, 1);
     assert(temp_ptep!= NULL);
     cprintf("setup Page Table vaddr 0~4MB OVER!\n");
     
     for (i=0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
          check_rp[i] = alloc_page();
          assert(check_rp[i] != NULL );
          assert(!PageProperty(check_rp[i]));
     }
     list_entry_t free_list_store = free_list;
     list_init(&free_list);
     assert(list_empty(&free_list));
     
     //assert(alloc_page() == NULL);
     
     unsigned int nr_free_store = nr_free;
     nr_free = 0;
     for (i=0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
        free_pages(check_rp[i],1);
     }
     assert(nr_free==CHECK_VALID_PHY_PAGE_NUM);
     
     cprintf("set up init env for check_swap begin!\n");
     //setup initial vir_page<->phy_page environment for page relpacement algorithm 

     
     pgfault_num=0;
     
     check_content_set();
     assert( nr_free == 0);         
     for(i = 0; i<MAX_SEQ_NO ; i++) 
         swap_out_seq_no[i]=swap_in_seq_no[i]=-1;
     
     for (i= 0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
         check_ptep[i]=0;
         check_ptep[i] = get_pte(pgdir, (i+1)*0x1000, 0);
         //cprintf("i %d, check_ptep addr %x, value %x\n", i, check_ptep[i], *check_ptep[i]);
         assert(check_ptep[i] != NULL);
         assert(pte2page(*check_ptep[i]) == check_rp[i]);
         assert((*check_ptep[i] & PTE_P));          
     }
     cprintf("set up init env for check_swap over!\n");
     // now access the virt pages to test  page relpacement algorithm 
     ret=check_content_access();
     assert(ret==0);
     
     //restore kernel mem env
     for (i=0;i<CHECK_VALID_PHY_PAGE_NUM;i++) {
         free_pages(check_rp[i],1);
     } 

     //free_page(pte2page(*temp_ptep));
    free_page(pde2page(pgdir[0]));
     pgdir[0] = 0;
     mm->pgdir = NULL;
     mm_destroy(mm);
     check_mm_struct = NULL;
     
     nr_free = nr_free_store;
     free_list = free_list_store;

     
     le = &free_list;
     while ((le = list_next(le)) != &free_list) {
         struct Page *p = le2page(le, page_link);
         count --, total -= p->property;
     }
     cprintf("count is %d, total is %d\n",count,total);
     //assert(count == 0);
     
     cprintf("check_swap() succeeded!\n");
}
