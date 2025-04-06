// gcc -o victim victim.c
// 54 4f 50 20 53 45 43 52 45 54 20 44 41 54 41 00  TOP SECRET DATA.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int hp = 80085;  // Known global memory content
u_int64_t money = 1337;

int main() {
    printf("PID: %d\n", getpid());

    // Store addresses in local variables to print the same ones later
    void* hp_addr = &hp;
    void* money_addr = &money;

    printf("Find the hp: %d at %p\n", hp, hp_addr);
    printf("Find the money: %ld at %p\n", money, money_addr);

    int counter = 0;  // Keep track of elapsed time in seconds

    while (1) {
        sleep(30);  // Wait for 30 seconds
        counter += 30;

        if (counter >= 60) {
            money++;  // Increase money every 60 seconds
            counter = 0;
        }

        printf("Printing every 30 seconds:\n");
        printf("Find the hp: %d at %p\n", hp, hp_addr);
        printf("Find the money: %ld at %p\n", money, money_addr);
    }

    return 0;
}