// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

#ifdef UB_ON_WRITE
struct page_ref {
  void *pa;
  int ref_count;
};

struct page_ref pages[NPAGE];
struct spinlock pages_lock;
#endif

// Must hold pages_lock
void print_number_of_free_pages() {
  int cnt = 0;
  for (int i = 0; i < NPAGE; ++i) {
    if (pages[i].ref_count == 0) cnt++;
  }
  printf("Number of free pages is %d\n", cnt);
}


void kinit() {
  char *p = (char *)PGROUNDUP((uint64)end);
  bd_init(p, (void *)PHYSTOP);
  #ifdef UB_ON_WRITE
  initlock(&pages_lock, "pages_lock");
  acquire(&pages_lock);
  print_number_of_free_pages();
  release(&pages_lock);
  #endif
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {

  #ifndef UB_ON_WRITE
  bd_free(pa);
  return;
  #else
  acquire(&pages_lock);
  for (int i = 0; i < NPAGE; ++i) {
    if (pages[i].pa == pa) {
      if (pages[i].ref_count <= 0) panic("kfree ref count");
      pages[i].ref_count--;
      if (pages[i].ref_count == 0) {
        bd_free(pa);
        pages[i].pa = 0;
        // printf("[kfree] ");
        // print_number_of_free_pages();
        release(&pages_lock);
        return;
      }
    }
  }
  panic("kfree invalid arg");
  #endif
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) { 
  void *cur_pa = bd_malloc(PGSIZE);

  #ifndef UB_ON_WRITE
  return cur_pa;
  #else
  acquire(&pages_lock);
  if (!cur_pa) return cur_pa;
  for (int i = 0; i < NPAGE; ++i) {
    if (!pages[i].pa) {
      pages[i].pa = cur_pa;
      pages[i].ref_count = 1;
      // printf("[kalloc] ");
      // print_number_of_free_pages();
      release(&pages_lock);
      return cur_pa;
    }
  }
  // printf("kalloc pages > NPAGE\n");
  release(&pages_lock);
  return 0;
  #endif
}


void kup(void *pa) {
  #ifdef UB_ON_WRITE
  acquire(&pages_lock);
  for (int i = 0; i < NPAGE; ++i) {
    if (pages[i].pa == pa) {
      pages[i].ref_count++;
      release(&pages_lock);
      return;
    }
  }
  #endif
  panic("kup invalid arg");
}
