struct rwlock{
    struct spinlock write_lock;
    struct spinlock counter_lock;
    int read_counter;
};
