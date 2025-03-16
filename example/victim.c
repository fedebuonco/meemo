// gcc -o victim victim.c
// 54 4f 50 20 53 45 43 52 45 54 20 44 41 54 41 00  TOP SECRET DATA.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int hp = 80085;  // Known global    memory content
u_int64_t  money = 1337;
char* secret = "FEDEBUONCO";

int main() {
  printf("PID: %d\n", getpid());
  printf("Find the hp: %d at %p at \n", hp, &hp);
  printf("Find the money: %ld at %p at \n", money, &money);
  printf("Find the secret: %s at %p \n", secret, &secret);

  while (1) {
    sleep(1);
  }
  return 0;
}