#include <stdio.h>
#include <fcntl.h>
#include <spectranet.h>
#include <input.h>

int main()
{
    pagein();

    int fd = open("jb.txt", O_RDONLY, 0);
    if (fd < 0) {
        printf("open error\n");
        pageout();
        return 1;
    }

    long pos = lseek(fd, 10, SEEK_SET);
    if (pos < 0) {
        printf("lseek error\n");
        close(fd);
        pageout();
        return 1;
    }

    unsigned char buf[6];
    int n = read(fd, buf, 5);
    buf[n] = '\0';

    printf("lseek returned: %ld\n", pos);
    printf("read %d bytes: '%s'\n", n, buf);
    printf("Press any key...\n");

    fgetc_cons();   /* waits for a keypress */

    close(fd);
    pageout();
    return 0;
}