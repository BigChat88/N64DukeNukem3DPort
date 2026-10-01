/*
 * N64 system glue: early init (debug output, ROM filesystem, memory check)
 * and the fatal error screen used by Error().
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <libdragon.h>

#include "platform.h"
#include "n64_platform.h"

/*
 * POSIX bits that newlib does not provide on the N64.
 */

int access(const char *path, int mode)
{
    // Only existence can be checked: the ROM filesystem is read-only.
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return -1;
    close(fd);
    return 0;
}

int32_t filelength(int fd)
{
    off_t current = lseek(fd, 0, SEEK_CUR);
    off_t size = lseek(fd, 0, SEEK_END);

    lseek(fd, current, SEEK_SET);
    return size;
}

/*
 * There is no command line on a console. For testing, one can be baked into
 * the ROM at build time: `libdragon make ARGS="-v1 -l1 -s2"`.
 */
void n64_get_args(int *argc, char ***argv)
{
    static char *args[32] = { "duke3d" };
#ifdef N64_ARGS
    static char cmdline[] = N64_ARGS;
    char *token;
    int n = 1;

    for (token = strtok(cmdline, " "); token && n < 32; token = strtok(NULL, " "))
        args[n++] = token;
    *argc = n;
#else
    *argc = 1;
#endif
    *argv = args;
}

/*
 * The expansion pack of an add-on ROM. The compiled game is the same for
 * every ROM: tools/pack_rom.py packs the add-on in rom:/addon/ and names it in
 * rom:/addon/ADDON.TXT (GRP on the first line, optional CON on the second).
 */
static char addon_grp[32], addon_con[32];
static int addon_state = 0;             // 0: not read yet, 1: add-on ROM, -1: base game

static void addon_read(void)
{
    FILE *f;

    if (addon_state)
        return;
    addon_state = -1;
    if (!(f = fopen("rom:/addon/ADDON.TXT", "r")))
        return;
    if (fgets(addon_grp, sizeof(addon_grp), f))
    {
        addon_grp[strcspn(addon_grp, "\r\n")] = 0;
        if (fgets(addon_con, sizeof(addon_con), f))
            addon_con[strcspn(addon_con, "\r\n")] = 0;
        if (addon_grp[0])
            addon_state = 1;
    }
    fclose(f);
}

const char *n64_addon_grp(void)
{
    addon_read();
    return addon_state > 0 ? addon_grp : NULL;
}

const char *n64_addon_con(void)
{
    addon_read();
    return addon_state > 0 && addon_con[0] ? addon_con : NULL;
}

void n64_log_memory(const char *label)
{
    heap_stats_t st;
    sys_get_heap_stats(&st);
    debugf("memory %s: heap used %d KB, free %d KB\n", label, st.used / 1024, st.free / 1024);
}

void n64_boot_mark(const char *phase)
{
    debugf("boot: %6lu ms  %s\n", (unsigned long)get_ticks_ms(), phase);
}

void n64_early_init(void)
{
    debug_init_isviewer();
    debug_init_usblog();
    // The game reports its progress with printf(), but libdragon only sends
    // stderr to the debug channels (emulator log / USB).
    stdout = stderr;
#ifdef N64_PROFILE
    // libdragon's own profiler (mixer, music, RSP queue): dumped every 5 s.
    profile_init(NULL);
    {
        extern void __mixer_profile_init(void);     // (not in a public header)
        __mixer_profile_init();
    }
#endif

    if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS)
        n64_fatal("Could not open the ROM filesystem.\n"
                  "Rebuild the ROM with DUKE3D.GRP\n"
                  "in the filesystem/ folder.");

    // BUILD's tile cache, the map arrays and the game state do not fit in
    // the base 4 MB of RDRAM.
    if (!is_memory_expanded())
        n64_fatal("Duke Nukem 3D needs the\n"
                  "Expansion Pak (8 MB of RAM).");

    n64_savefs_init();
    n64_dragon_intro();
}

#define FATAL_FONT_ID 1

void n64_fatal(const char *fmt, ...)
{
    char msg[1024];
    va_list args;

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    debugf("FATAL: %s\n", msg);

    if (n64_video_ready())
    {
        rspq_wait();
        display_close();
    }
    else
    {
        rdpq_init();
    }
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_text_register_font(FATAL_FONT_ID, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    while (1)
    {
        rdpq_attach_clear(display_get(), NULL);
        rdpq_text_print(NULL, FATAL_FONT_ID, 16, 24, "DUKE NUKEM 3D - FATAL ERROR");
        rdpq_text_print(&(rdpq_textparms_t){ .width = 288, .wrap = WRAP_WORD },
                        FATAL_FONT_ID, 16, 48, msg);
        rdpq_detach_show();
    }
}
