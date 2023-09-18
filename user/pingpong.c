#include "kernel/types.h"
#include "user/user.h"

#define BUFSIZE 100

int main(int argc, char *argv[])
{
    int p1[2]; int p2[2];
    char ping[] = "ping";
    char pong[] = "pong";
    int fork_pid;

    if (pipe(p1) == -1 || pipe(p2) == -1){
        fprintf(2, "Pipe failed.");
        exit(0);
    }

    fork_pid = fork();

    if (fork_pid < 0){
        fprintf(2, "Fork faield.");
        exit(0);
    }

    // parent process
    if (fork_pid > 0){
        // close read end of first pipe
        close(p1[0]);
        // close write end of second pipe
        close(p2[1]);


        write(p1[1], ping, BUFSIZE);
        close(p1[1]);

        char inbuf[BUFSIZE];
        read(p2[0], inbuf, BUFSIZE);
        close(p2[0]);

        printf("%d: got %s.\n", getpid(), inbuf);
        wait(0);
    } else { // child process
        // close read end of second pipe
        close(p2[0]);
        // close write end of first pipe
        close(p1[1]);

        char inbuf[BUFSIZE];
        read(p1[0], inbuf, BUFSIZE);
        close(p1[0]);

        printf("%d: got %s.\n", getpid(), inbuf);

        write(p2[1], pong, BUFSIZE);
        close(p2[1]);
    }

    exit(0);
}
