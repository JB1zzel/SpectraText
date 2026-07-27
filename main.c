#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <spectranet.h>
#include <spdos.h>
#include <input.h>

/* Polls the keyboard directly instead of going through stdio, so the
   key pressed isn't echoed to the screen. */
static int wait_key(void)
{
    int k = 0;
    while (k == 0)
    {
        k = in_Inkey();
    }
    return k;
}

#define BIGBUF     4000
#define MAXLINES   300
#define PAGE_SIZE  21
#define MAXFILES   20
#define NAMELEN    32

static char filebuf[BIGBUF];
static int  line_start[MAXLINES];
static int  line_len[MAXLINES];
static int  num_lines = 0;

static char filenames[MAXFILES][NAMELEN];
static int  num_files = 0;

/* ---------- small helpers ---------- */

static void print_uint(unsigned int uv)
{
    char buf[8];
    int i = 0;

    if (uv == 0)
    {
        putchar('0');
        return;
    }
    while (uv > 0)
    {
        buf[i++] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    while (i > 0)
    {
        putchar(buf[--i]);
    }
}

/* true if name ends in ".txt" (case-sensitive, matches typical
   TNFS/XFS naming on this SD-card-style filesystem) */
static int has_txt_ext(const char *name)
{
    int len = (int)strlen(name);
    if (len < 4) return 0;
    return (strcmp(name + len - 4, ".txt") == 0);
}

/* ---------- directory listing / menu ---------- */

static void build_file_list(void)
{
    int dh;
    char rawbuf[64];

    num_files = 0;

    dh = opendir(".");
    if (dh < 0)
    {
        printf("Could not open directory.\n");
        return;
    }

    while (readdir(dh, rawbuf) == 0)
    {
        printf("RAW:[%s]\n", rawbuf);
        if (has_txt_ext(rawbuf) && num_files < MAXFILES)
        {
            strncpy(filenames[num_files], rawbuf, NAMELEN - 1);
            filenames[num_files][NAMELEN - 1] = '\0';
            num_files++;
        }
    }

    closedir(dh);

    printf("Press a key to continue...\n");
    wait_key();
}

static void show_menu(void)
{
    int i;

    printf("\n\nText files:\n\n");

    for (i = 0; i < num_files; i++)
    {
        print_uint((unsigned int)(i + 1));
        printf(". %s\n", filenames[i]);
    }

    printf("\nPress a number to read, Q to quit\n");
}

/* ---------- file reading / line splitting ---------- */

static int load_file(const char *name)
{
    int fd;
    int total = 0;
    ssize_t got;

    fd = open(name, O_RDONLY, 0);
    if (fd < 0)
    {
        return -1;
    }

    for (;;)
    {
        got = read(fd, filebuf + total, 32);
        if (got <= 0)
        {
            break;
        }
        total += (int)got;
        if (total + 32 > BIGBUF)
        {
            break;
        }
    }

    close(fd);
    return total;
}

/* split filebuf[0..n) into lines on '\r'; skip raw '\n'
   (Spectrum "cursor down" control code, not a line separator) */
static void split_lines(int n)
{
    int i;
    int start = 0;

    num_lines = 0;

    for (i = 0; i < n; i++)
    {
        if (filebuf[i] == '\r')
        {
            if (num_lines < MAXLINES)
            {
                line_start[num_lines] = start;
                line_len[num_lines] = i - start;
                num_lines++;
            }
            start = i + 1;
        }
        else if (filebuf[i] == '\n')
        {
            if (start == i)
            {
                start = i + 1;
            }
        }
    }

    if (start < n && num_lines < MAXLINES)
    {
        line_start[num_lines] = start;
        line_len[num_lines] = n - start;
        num_lines++;
    }
}

/* ---------- pager ---------- */

static void print_line(int idx)
{
    int i;
    int start = line_start[idx];
    int len = line_len[idx];

    for (i = 0; i < len; i++)
    {
        putchar(filebuf[start + i]);
    }
    putchar('\n');
}

static void show_page(int top)
{
    int i;
    int last = top + PAGE_SIZE;

    if (last > num_lines)
    {
        last = num_lines;
    }

    for (i = top; i < last; i++)
    {
        print_line(i);
    }

    printf("\na=down q=up p=quit  line ");
    print_uint((unsigned int)(top + 1));
    putchar('/');
    print_uint((unsigned int)num_lines);
    putchar('\n');
}

static void run_pager(const char *name)
{
    int total;
    int top;
    int k;

    total = load_file(name);
    if (total <= 0)
    {
        printf("Could not read %s\n", name);
        printf("Press a key...\n");
        wait_key();
        return;
    }

    split_lines(total);

    top = 0;
    for (;;)
    {
        show_page(top);

        k = wait_key();

        if (k == 'a' || k == 'A')
        {
            if (top + PAGE_SIZE < num_lines)
            {
                top += PAGE_SIZE;
            }
        }
        else if (k == 'q' || k == 'Q')
        {
            top -= PAGE_SIZE;
            if (top < 0)
            {
                top = 0;
            }
        }
        else if (k == 'p' || k == 'P')
        {
            return;  /* back to menu */
        }
    }
}

/* ---------- main ---------- */

int main()
{
    int k;
    int choice;

    pagein();

    for (;;)
    {
        build_file_list();

        if (num_files == 0)
        {
            printf("No .txt files found.\n");
            pageout();
            return 0;
        }

        show_menu();

        k = wait_key();

        if (k == 'q' || k == 'Q')
        {
            break;
        }

        if (k >= '1' && k <= '9')
        {
            choice = k - '1';
            if (choice < num_files)
            {
                run_pager(filenames[choice]);
            }
        }
        /* anything else: just redraw the menu */
    }

    pageout();
    return 0;
}