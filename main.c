#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <spectranet.h>
#include <spdos.h>
#include <input.h>
#include <conio.h>
#include <spectrum.h>

#define PAGE_SIZE   21
#define SCREEN_COLS 62
#define MAXFILES    20
#define NAMELEN     32
#define MAXPAGES    200
#define PAGEBUF_SZ  1500

static char pagebuf[PAGEBUF_SZ];
static int  line_start[PAGE_SIZE];
static int  line_len[PAGE_SIZE];
static int  num_lines_this_page = 0;
static int  at_eof_after_page = 0;

static long page_offsets[MAXPAGES];
static int  highest_visited_page = 0;

static char filenames[MAXFILES][NAMELEN];
static int  num_files = 0;

/* ---------- helpers ---------- */
static int wait_key(void)
{
    int k = 0;
    int stable;
    while (k == 0)
        k = in_Inkey();
    stable = 0;
    while (stable < 500) {
        if (in_Inkey() == 0)
            stable++;
        else
            stable = 0;
    }
    return k;
}

static void print_uint(unsigned int uv)
{
    char buf[8];
    int i = 0;
    if (uv == 0) { putchar('0'); return; }
    while (uv > 0) {
        buf[i++] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    while (i > 0) putchar(buf[--i]);
}

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
    char rawbuf[256];
    num_files = 0;
    dh = opendir(".");
    if (dh < 0) return;
    while (readdir(dh, rawbuf) == 0) {
        if (has_txt_ext(rawbuf) && num_files < MAXFILES) {
            strncpy(filenames[num_files], rawbuf, NAMELEN - 1);
            filenames[num_files][NAMELEN - 1] = '\0';
            num_files++;
        }
    }
    closedir(dh);
}

static void show_menu(void)
{
    int i, pad;
    textbackground(BLUE);
    textcolor(WHITE);
    clrscr();
    pad = (SCREEN_COLS - 11) / 2;
    gotoxy(pad, 0);
    printf("SpectraText");
    gotoxy(0, 1);
    for (i = 0; i < SCREEN_COLS; i++) putchar('_');
    gotoxy(0, 3);
    for (i = 0; i < num_files; i++) {
        char *p;
        print_uint((unsigned int)(i + 1));
        printf(". ");
        p = filenames[i];
        while (*p) {
            if (p == filenames[i] + strlen(filenames[i]) - 4) break;
            putchar(*p);
            p++;
        }
        printf("\n");
    }
    gotoxy(0, 22);
    printf("Press a number to read");
    gotoxy(0, 23);
    printf("Q to quit");
}

/* ---------- lseek-based pager (minimal memory) ---------- */

static int read_raw_line(int fd, char *buf, int maxlen)
{
    int i = 0;
    while (i < maxlen - 1) {
        unsigned char c;
        int got = read(fd, &c, 1);
        if (got <= 0) {
            if (i == 0) return -1;  /* EOF */
            break;                   /* partial line at EOF */
        }
        if (c == '\r') {
            got = read(fd, &c, 1);
            if (got > 0 && c != '\n') {
                /* bare CR - we consumed a byte of next line.
                   Since lseek works, we can back up. */
                lseek(fd, -1, SEEK_CUR);
            }
            break;
        } else if (c == '\n') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static int load_one_page(int fd, int *reached_eof)
{
    int buf_fill = 0;
    *reached_eof = 0;
    num_lines_this_page = 0;

    while (num_lines_this_page < PAGE_SIZE) {
        if (buf_fill + 64 > PAGEBUF_SZ) break;
        int len = read_raw_line(fd, pagebuf + buf_fill, 64);
        if (len < 0) {
            *reached_eof = 1;
            break;
        }
        line_start[num_lines_this_page] = buf_fill;
        line_len[num_lines_this_page] = len;
        buf_fill += len;
        num_lines_this_page++;
    }

    at_eof_after_page = (*reached_eof && num_lines_this_page == 0) ? 1 : *reached_eof;
    return num_lines_this_page;
}

static void print_line(int idx)
{
    int start = line_start[idx];
    int len   = line_len[idx];
    for (int i = 0; i < len; i++)
        putchar(pagebuf[start + i]);
    putchar('\n');
}

static void show_page(int page_num)
{
    int i;
    textbackground(BLUE);
    textcolor(WHITE);
    clrscr();
    for (i = 0; i < num_lines_this_page; i++)
        print_line(i);

    gotoxy(0, 23);
    textbackground(BLACK);
    textcolor(WHITE);
    for (i = 0; i < SCREEN_COLS; i++) putchar(' ');
    gotoxy(0, 23);
    printf("a=down q=up p=quit  page ");
    print_uint((unsigned int)(page_num + 1));
    if (at_eof_after_page) printf(" (end)");
}

static void run_pager(const char *name)
{
    int fd = open(name, O_RDONLY, 0);
    if (fd < 0) {
        printf("Could not open %s\n", name);
        printf("Press a key...\n");
        wait_key();
        return;
    }

    int current_page = 0;
    int k;

    page_offsets[0] = 0;
    highest_visited_page = 0;

    load_one_page(fd, &at_eof_after_page);
    show_page(current_page);

    for (;;) {
        k = wait_key();

        if (k == 'a' || k == 'A') {
            if (!at_eof_after_page && current_page + 1 < MAXPAGES) {
                current_page++;
                if (current_page > highest_visited_page) {
                    page_offsets[current_page] = lseek(fd, 0, SEEK_CUR);
                    highest_visited_page = current_page;
                }
                load_one_page(fd, &at_eof_after_page);
                show_page(current_page);
            }
        } else if (k == 'q' || k == 'Q') {
            if (current_page > 0) {
                current_page--;
                lseek(fd, page_offsets[current_page], SEEK_SET);
                load_one_page(fd, &at_eof_after_page);
                show_page(current_page);
            }
        } else if (k == 'p' || k == 'P') {
            break;
        }
    }

    close(fd);
}

/* ---------- main ---------- */
int main()
{
    int k, choice;
    pagein();
    zx_border(BLUE);
    for (;;) {
        build_file_list();
        if (num_files == 0) {
            printf("No .txt files found.\n");
            pageout();
            return 0;
        }
        show_menu();
        k = wait_key();
        if (k == 'q' || k == 'Q') break;
        if (k >= '1' && k <= '9') {
            choice = k - '1';
            if (choice < num_files)
                run_pager(filenames[choice]);
        }
    }
    pageout();
    return 0;
}