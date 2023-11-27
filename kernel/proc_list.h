struct proc_list{
  struct proc_list *next;
  struct proc_list *prev;
  struct proc *cur_proc;
};
