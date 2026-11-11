#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char name[23];
    write(1, "Enter you name :", 17);
    scanf("%22c", name);
    write(1, "Helllo ubuntu\n", 13);
    return 0;
}