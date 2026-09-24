/* ==========================================================================
 * main.cpp  --  menu and interactive shell.
 *
 * Part 1 of the Cache Coherence assignment: a MESI snooping cache coherence
 * simulator for 4 processors, each with a 64 KB Opteron-shaped L1, hanging
 * off one shared MemBus with a round-robin bus arbiter in front of it.
 *
 * usage:  mesi_sim [--ascii] [--no-color] [--auto]
 * ========================================================================== */
#include "sim.h"
#include "tests.h"
#include "mesi.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  interactive shell                                                  */
/* ------------------------------------------------------------------ */

static void shell_help(void)
{
    ui_clear_screen();
    ui_banner(std::string(C_BOLD) + "INTERACTIVE SHELL" + C_RESET, "commands");

    ui_box_top("ISSUING REQUESTS");
    ui_box_row("p<n> r <addr>              processor n reads that address");
    ui_box_row("p<n> w <addr> [value]      processor n writes (value defaults to a");
    ui_box_row("                           generated 0xC0DE.... pattern)");
    ui_box_row("");
    ui_box_row("Addresses and values are hex, with or without the 0x.");
    ui_box_row("Put a semicolon between requests to fire them in the SAME cycle and");
    ui_box_row("make the bus arbiter choose an order:");
    ui_box_row("");
    ui_box_row(std::string(C_BGRN) + "    p0 r 1040 ; p1 r 1040 ; p2 w 1040" + C_RESET);
    ui_box_bottom();

    ui_box_top("LOOKING AROUND");
    ui_box_row("state / s                  redraw the dashboard");
    ui_box_row("dump <n>                   list the valid lines of P<n>'s cache");
    ui_box_row("mem <addr>                 what main memory currently holds there");
    ui_box_row("cov                        transition coverage report");
    ui_box_row("stats                      full statistics");
    ui_box_row("diag                       the two state diagrams");
    ui_box_row("cfg                        parameters and assumptions");
    ui_box_row("reset                      wipe the machine and start over");
    ui_box_row("help / ?                   this screen");
    ui_box_row("quit / q                   back to the main menu");
    ui_box_bottom();

    ui_box_top("HANDY ADDRESSES (they collide in set 65, so they force evictions)");
    ui_box_row(ui_fmt("A = %06X    B = %06X    C = %06X    D = %06X",
                      ADDR_A, ADDR_B, ADDR_C, ADDR_D));
    ui_box_bottom();

    ui_pause("\n   press ENTER to go back to the shell ");
}

/* turn "p2 w 1040 deadbeef" into a Request.  returns 1 if it parsed. */
static int parse_request(char *text, Request *out, int seq)
{
    char cpu_tok[32], op_tok[32], addr_tok[64], val_tok[64];
    int  got;

    val_tok[0] = 0;
    got = sscanf(text, "%31s %31s %63s %63s", cpu_tok, op_tok, addr_tok, val_tok);
    if (got < 3)
        return 0;

    if (cpu_tok[0] != 'p' && cpu_tok[0] != 'P')
        return 0;
    int cpu = atoi(cpu_tok + 1);
    if (cpu < 0 || cpu >= NUM_CPUS) {
        printf(" %sthere is no P%d, this machine has %d processors%s\n",
               C_BRED, cpu, NUM_CPUS, C_RESET);
        return 0;
    }

    int op;
    if (op_tok[0] == 'r' || op_tok[0] == 'R')      op = OP_READ;
    else if (op_tok[0] == 'w' || op_tok[0] == 'W') op = OP_WRITE;
    else                                            return 0;

    unsigned addr = (unsigned)strtoul(addr_tok, 0, 16);
    if (addr >= MEM_SIZE_BYTES) {
        printf(" %s0x%X is outside the %d KB address space%s\n",
               C_BRED, addr, MEM_SIZE_BYTES / 1024, C_RESET);
        return 0;
    }

    unsigned value = 0;
    if (op == OP_WRITE) {
        if (got >= 4 && val_tok[0])
            value = (unsigned)strtoul(val_tok, 0, 16);
        else
            value = 0xC0DE0000u | ((unsigned)cpu << 12) | (unsigned)(seq & 0xFFF);
    }

    out->cpu   = cpu;
    out->op    = op;
    out->addr  = addr;
    out->value = value;
    return 1;
}

static void run_shell(Sim *s)
{
    char line[512];
    int  seq = 1;

    s->headline = "interactive shell";
    ui_clear_screen();
    sim_render(s);

    for (;;) {
        printf("\n %s%smesi>%s ", C_BOLD, C_BGRN, C_RESET);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            return;

        /* strip the newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (len == 0)
            continue;

        /* --- plain commands --- */
        if (!strcmp(line, "q") || !strcmp(line, "quit") || !strcmp(line, "exit"))
            return;
        if (!strcmp(line, "help") || !strcmp(line, "?")) {
            shell_help();
            ui_clear_screen();
            sim_render(s);
            continue;
        }
        if (!strcmp(line, "state") || !strcmp(line, "s")) {
            ui_clear_screen();
            sim_render(s);
            continue;
        }
        if (!strcmp(line, "cov")) {
            sim_show_coverage(s);
            ui_clear_screen();
            sim_render(s);
            continue;
        }
        if (!strcmp(line, "stats")) {
            sim_show_stats(s);
            ui_clear_screen();
            sim_render(s);
            continue;
        }
        if (!strcmp(line, "diag")) {
            sim_show_diagrams();
            ui_clear_screen();
            sim_render(s);
            continue;
        }
        if (!strcmp(line, "cfg")) {
            sim_show_config(s);
            ui_clear_screen();
            sim_render(s);
            continue;
        }
        if (!strcmp(line, "reset")) {
            sim_reset(s);
            s->headline = "interactive shell";
            ui_clear_screen();
            sim_render(s);
            printf(" %smachine reset%s\n", C_BYEL, C_RESET);
            continue;
        }
        if (!strncmp(line, "dump", 4)) {
            int n = atoi(line + 4);
            if (n < 0 || n >= NUM_CPUS) n = 0;
            ui_clear_screen();
            sim_dump_cache(s, n);
            ui_pause("\n press ENTER ");
            ui_clear_screen();
            sim_render(s);
            continue;
        }
        if (!strncmp(line, "mem", 3)) {
            unsigned a = (unsigned)strtoul(line + 3, 0, 16);
            if (a >= MEM_SIZE_BYTES) {
                printf(" %saddress out of range%s\n", C_BRED, C_RESET);
                continue;
            }
            printf(" memory[0x%06X] = 0x%08X    coherent value = 0x%08X%s\n",
                   a, mem_peek_word(&s->mem, a), s->golden[a >> 2],
                   mem_peek_word(&s->mem, a) == s->golden[a >> 2]
                       ? "" : "   <- memory is stale, a cache holds the block in M");
            continue;
        }

        /* --- otherwise it should be one or more requests --- */
        Request reqs[NUM_CPUS];
        int     n = 0;
        int     bad = 0;
        char   *save = line;
        char   *part = strtok(save, ";+");
        int     used_cpu[NUM_CPUS] = {0, 0, 0, 0};

        while (part && n < NUM_CPUS) {
            Request r;
            if (!parse_request(part, &r, seq++)) {
                bad = 1;
                break;
            }
            if (used_cpu[r.cpu]) {
                printf(" %sP%d can only issue one request per cycle%s\n",
                       C_BRED, r.cpu, C_RESET);
                bad = 1;
                break;
            }
            used_cpu[r.cpu] = 1;
            reqs[n++] = r;
            part = strtok(0, ";+");
        }

        if (bad || n == 0) {
            if (!bad)
                printf(" %sdid not understand that. type help%s\n", C_DIM, C_RESET);
            else
                printf(" %stry something like:  p0 r 1040    or   p0 w 1040 deadbeef%s\n",
                       C_DIM, C_RESET);
            continue;
        }

        s->headline = "interactive shell";
        sim_log(s, "======== shell command ========");
        sim_step(s, reqs, n);
        ui_clear_screen();
        sim_render(s);
    }
}

/* ------------------------------------------------------------------ */
/*  main menu                                                          */
/* ------------------------------------------------------------------ */

static void draw_menu(Sim *s)
{
    ui_clear_screen();
    ui_banner(std::string(C_BOLD) + "CACHE COHERENCE SIMULATOR  -  PART 1  -  MESI SNOOPING" + C_RESET,
              ui_fmt("%d CPUs   %d KB L1 each   %d MB shared memory",
                     NUM_CPUS, L1_SIZE_BYTES / 1024, MEM_SIZE_BYTES / (1024 * 1024)));

    ui_box_top("MENU");
    ui_box_row("");
    ui_box_row(std::string("  ") + C_BGRN + "1" + C_RESET + "   run the test vector suite, one step at a time");
    ui_box_row(std::string("  ") + C_BGRN + "2" + C_RESET + "   run the test vector suite straight through");
    ui_box_row(std::string("  ") + C_BGRN + "3" + C_RESET + "   interactive shell  (type your own reads and writes)");
    ui_box_row("");
    ui_box_row(std::string("  ") + C_BCYN + "4" + C_RESET + "   architecture, parameters and assumptions");
    ui_box_row(std::string("  ") + C_BCYN + "5" + C_RESET + "   the two MESI state diagrams");
    ui_box_row(std::string("  ") + C_BCYN + "6" + C_RESET + "   test vector table");
    ui_box_row("");
    ui_box_row(std::string("  ") + C_BYEL + "7" + C_RESET + "   transition coverage report");
    ui_box_row(std::string("  ") + C_BYEL + "8" + C_RESET + "   statistics");
    ui_box_row(std::string("  ") + C_BYEL + "9" + C_RESET + "   reset the machine");
    ui_box_row("");
    ui_box_row(std::string("  ") + C_DIM + "0   quit" + C_RESET);
    ui_box_row("");
    ui_box_bottom();

    int lc = cov_covered(DIAG_LOCAL),  lt = cov_total(DIAG_LOCAL);
    int rc = cov_covered(DIAG_REMOTE), rt = cov_total(DIAG_REMOTE);
    printf("  %scycle %ld   steps run %ld   coverage local %d/%d remote %d/%d   "
           "data checks %ld/%ld ok%s\n",
           C_DIM, s->cycle, s->step_no, lc, lt, rc, rt,
           s->checks - s->check_fails, s->checks, C_RESET);
    printf("  %sthe full event transcript is being written to run_log.txt%s\n",
           C_DIM, C_RESET);
}

int main(int argc, char **argv)
{
    int use_color   = 1;
    int use_unicode = 1;
    int auto_run    = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ascii"))         use_unicode = 0;
        else if (!strcmp(argv[i], "--no-color")) use_color   = 0;
        else if (!strcmp(argv[i], "--auto"))     auto_run    = 1;
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: %s [--ascii] [--no-color] [--auto]\n", argv[0]);
            printf("  --ascii     plain ASCII instead of box drawing characters\n");
            printf("  --no-color  no ANSI colour\n");
            printf("  --auto      run the test vector suite and exit (for capturing output)\n");
            return 0;
        }
    }

    ui_init(use_color, use_unicode);

    Sim *s = new Sim;
    sim_init(s);

    if (auto_run) {
        tests_run(s, 0);
        sim_show_coverage(s);
        sim_show_stats(s);
        sim_free(s);
        delete s;
        return 0;
    }

    char line[64];
    for (;;) {
        draw_menu(s);
        printf("\n  %sselect>%s ", C_BOLD, C_RESET);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        switch (line[0]) {
            case '1': tests_run(s, 1);        break;
            case '2': tests_run(s, 0);        break;
            case '3': run_shell(s);           break;
            case '4': sim_show_config(s);     break;
            case '5': sim_show_diagrams();    break;
            case '6': tests_show_table();     break;
            case '7': sim_show_coverage(s);   break;
            case '8': sim_show_stats(s);      break;
            case '9': sim_reset(s);           break;
            case '0':
            case 'q':
                ui_clear_screen();
                ui_show_cursor();
                printf("bye. the transcript of this session is in run_log.txt\n");
                sim_free(s);
                delete s;
                return 0;
            default:
                break;
        }
    }

    ui_show_cursor();
    sim_free(s);
    delete s;
    return 0;
}
