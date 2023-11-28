#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc_list.h"
#include "proc.h"


void
proc_lst_init(struct proc_list *lst)
{
  lst->next = lst;
  lst->prev = lst;
  struct proc *cur_proc = bd_malloc(sizeof(struct proc));
  lst->cur_proc = cur_proc;
  initlock(&cur_proc->lock, "head lock");
}

int
proc_lst_empty(struct proc_list *lst) {
  return lst->next == lst;
}

void
proc_lst_remove(struct proc_list *e) {
  e->prev->next = e->next;
  e->next->prev = e->prev;
}

void
proc_lst_push(struct proc_list *lst, struct proc_list *p)
{
  p->next = lst->next;
  p->prev = lst;
  lst->next->prev = p;
  lst->next = p;
}

void
proc_lst_print(struct proc_list *lst)
{
  for (struct proc_list *p = lst->next; p != lst; p = p->next) {
    printf(" %p with pid %d", p, p->cur_proc->pid);
  }
  printf("\n");
}


int proc_lst_size(struct proc_list *lst)
{
  int size = 1;
  for (struct proc_list *p = lst->next; p != lst; p = p->next) {
    size++;
  }
  return size;
}

struct proc_list* proc_lst_pop(struct proc_list *lst) {
  if(lst->next == lst)
    panic("lst_pop");
  struct proc_list *p = lst->next;
  proc_lst_remove(p);
  return p;
}

struct proc_list *get_proc_list_by_proc(struct proc *cur_proc, struct proc_list* head)
{
  for (struct proc_list *p = head->next; p != head; p = p->next) {
    if (p->cur_proc == cur_proc) return p;
  }
  return 0;
}
