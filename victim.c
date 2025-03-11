#include <stdio.h>
int main() {
    char secret_data[] = "TOP SECRET DATA";
    printf("Secret data is stored at %p\n", (void *)secret_data);
    getchar();
    return 0;
}
