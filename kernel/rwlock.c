#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "rwlock.h"
#include "defs.h"

void initrwlock(struct rwlock *lk, char *name) {
    lk->read_counter = 0;
    initlock(&lk->counter_lock, name);
    initlock(&lk->write_lock, name);
}

void acquire_write(struct rwlock* lk) {
    if (holding(&lk->write_lock)) {
        while (1) {
            acquire(&lk->counter_lock);
            if (lk->read_counter == 0) {
                acquire(&lk->write_lock);
                release(&lk->counter_lock);
                return;
            }
            release(&lk->counter_lock);
        }
    }
    acquire(&lk->write_lock);
}

void release_write(struct rwlock* lk) {
    release(&lk->write_lock);
}

void acquire_read(struct rwlock* lk) {
    acquire(&lk->counter_lock);
    lk->read_counter++;
    if (lk->read_counter == 1) {
        acquire(&lk->write_lock);
        pop_off();
    }
    release(&lk->counter_lock);
}

void release_read(struct rwlock* lk) {
    acquire(&lk->counter_lock);
    lk->read_counter--;
    if (lk->read_counter == 0) {
        lk->write_lock.cpu = mycpu();
        push_off();
        release(&lk->write_lock);
    }
    release(&lk->counter_lock);
}
