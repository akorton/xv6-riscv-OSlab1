#include "kernel/types.h"
#include "user/user.h"

#define SMALL_NUMBER  10
#define HUGE_NUMBER  1000

int children_pids_small[SMALL_NUMBER];
int proc_pids_small[SMALL_NUMBER];

int children_pids_huge[HUGE_NUMBER];
int proc_pids_huge[HUGE_NUMBER];

int soft_equals(int* pids1, int* pids2, int num)
{
    for (int i = 0; i < num; ++i) {
        int found = 0;
        for (int j = 0; j < num; j++) {
            if (pids1[i] == pids2[j]) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;
    }
    return 1;
}

void print_buf(int* buf, int num) {
    for (int i = 0; i < num; ++i) {
        printf("%d ", buf[i]);
    }
    printf("\n");
}

void print_proc_list(int pid)
{
    int l, r;
    int l_st, r_st;
    printf("%d ", pid);
    neighbors(pid, &l, &r, &l_st, &r_st);
    while (l != 0 || r != 0) {
        if (l != 0) {
            if(l_st != 0) printf("%d ", l);
            neighbors(l, &l, 0, &l_st, 0);
        }
        if (r != 0) {
            if (r_st != 0) printf("%d ", r);
            neighbors(r, 0, &r, 0, &r_st);
        }
    }
    printf("\n");
}

void fill_proc_pids(int pid, int* buf)
{
    int idx = 0;
    int l, r;
    int l_st, r_st;
    neighbors(pid, &l, &r, &l_st, &r_st);
    while (l != 0 || r != 0) {
        if (l != 0) {
            if (l > pid && l_st != 0) buf[idx++] = l;
            neighbors(l, &l, 0, &l_st, 0);
        }
        if (r != 0) {
            if (r > pid && r_st != 0) buf[idx++] = r;
            neighbors(r, 0, &r, 0, &r_st);
        }
    }
}

void
small_test(void)
{
    int n, pid;

    printf("Small test\n\n");

    // First of all lets fork a bit
    for(n = 0; n < SMALL_NUMBER; n++){
        pid = fork();
        children_pids_small[n] = pid;
        if(pid < 0) {
            printf("Fork failed\n");
            exit(1);
        }
        // In children we are just sleeping
        if(pid == 0) {
            // Wait to be killed
            sleep(1000);
            printf("Something went wrong...\n");
            exit(-1);
        }
    }
    printf("We are done forking\n\n");
    // Now we are doing staff
    int initial_pid = getpid();
    printf("Before killing anybody:\n");
    print_proc_list(initial_pid);
    printf("\n");

    printf("Killing pids ");
    for (int i = 0; i < SMALL_NUMBER / 3; i++) {
        kill(children_pids_small[3 * i]);
        printf("%d ", children_pids_small[3 * i]);
        children_pids_small[3 * i] = 0;
        n--;
        wait(0);
        if (i % 3 >= 1) {
            kill(children_pids_small[3 * i + 1]);
            printf("%d ", children_pids_small[3 * i + 1]);
            children_pids_small[3 * i + 1] = 0;
            n--;
            wait(0);
        }
        if (i % 3 == 2) {
            kill(children_pids_small[3 * i + 2]);
            printf("%d ", children_pids_small[3 * i + 2]);
            children_pids_small[3 * i + 2] = 0;
            n--;
            wait(0);
        }
    }
    printf("\n\n");
    
    printf("After killing spree:\n");
    print_proc_list(initial_pid);
    printf("\n");
    fill_proc_pids(initial_pid, proc_pids_small);
    // Kill everybody else
    for (int i = 0; i < SMALL_NUMBER; ++i) {
        if (children_pids_small[i]) kill(children_pids_small[i]);
    }
    printf("Killed everybody else sucessfully\n\n");
    
    // Just checking that everything is in order
    for(; n > 0; n--){
        if(wait(0) < 0){
            printf("wait stopped early\n");
            exit(1);
        }
    }

    printf("Waited for all of them\n\n");
    if(wait(0) != -1){
        printf("wait got too many\n");
        exit(1);
    }
    print_buf(proc_pids_small, SMALL_NUMBER);
    print_buf(children_pids_small, SMALL_NUMBER);
    if (soft_equals(proc_pids_small, children_pids_small, SMALL_NUMBER) && soft_equals(children_pids_small, proc_pids_small, SMALL_NUMBER))
        printf("Small test OK\n");
    else {
        printf("Small test FAILED\n");
        exit(-1);
    }
}

void
huge_test(void)
{
    int n, pid;

    printf("Huge test ");

    // First of all lets fork a bit
    for(n = 0; n < HUGE_NUMBER; n++){
        pid = fork();
        children_pids_huge[n] = pid;
        if(pid < 0) {
            printf("Fork failed\n");
            exit(1);
        }
        // In children we are just sleeping
        if(pid == 0) {
            // Wait to be killed
            sleep(1000);
            printf("Something went wrong...");
            exit(-1);
        }
    }
    // Now we are doing staff
    int initial_pid = getpid();

    for (int i = 0; i < HUGE_NUMBER / 3; i++) {
        kill(children_pids_huge[3 * i]);
        children_pids_huge[3 * i] = 0;
        n--;
        wait(0);
        if (i % 3 >= 1) {
            kill(children_pids_huge[3 * i + 1]);
            children_pids_huge[3 * i + 1] = 0;
            n--;
            wait(0);
        }
        if (i % 3 == 2) {
            kill(children_pids_huge[3 * i + 2]);
            children_pids_huge[3 * i + 2] = 0;
            n--;
            wait(0);
        }
    }
    
    fill_proc_pids(initial_pid, proc_pids_huge);
    // Kill everybody else
    for (int i = 0; i < HUGE_NUMBER; ++i) {
        if (children_pids_huge[i]) kill(children_pids_huge[i]);
    }
    
    // Just checking that everything is in order
    for(; n > 0; n--){
        if(wait(0) < 0){
            printf("wait stopped early\n");
            exit(1);
        }
    }
    if(wait(0) != -1){
        printf("wait got too many\n");
        exit(1);
    }
    if (soft_equals(proc_pids_huge, children_pids_huge, HUGE_NUMBER) && soft_equals(children_pids_huge, proc_pids_huge, HUGE_NUMBER))
        printf("OK\n");
    else {
        printf("FAILED\n");
        exit(-1);
    }
}

int
main(void)
{
  small_test();
  huge_test();
  exit(0);
}
