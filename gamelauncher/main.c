#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_FOUND 256
#define EXE_LIST_CAPACITY 4096

static char exe_list[MAX_FOUND][MAX_PATH];
static char dir_list[MAX_FOUND][MAX_PATH];
static int found_count = 0;
static char self_path[MAX_PATH];

static int is_self(const char *path)
{
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(self_path, '\\');
    a = a ? a + 1 : path;
    b = b ? b + 1 : self_path;
    return _stricmp(a, b) == 0;
}

static void add_candidate(const char *dir, const char *name)
{
    char full[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (found_count >= MAX_FOUND)
        return;
    if (is_self(name))
        return;
    snprintf(full, MAX_PATH, "%s\\%s", dir, name);
    if (!GetFileAttributesExA(full, GetFileExInfoStandard, &fad))
        return;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return;
    if (fad.nFileSizeHigh == 0 && fad.nFileSizeLow == 0)
        return;

    snprintf(exe_list[found_count], MAX_PATH, "%s", name);
    snprintf(dir_list[found_count], MAX_PATH, "%s", dir);
    found_count++;
}

static void scan_dir(const char *dir)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    snprintf(pattern, MAX_PATH, "%s\\*.exe", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            add_candidate(dir, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    snprintf(pattern, MAX_PATH, "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        char sub[MAX_PATH];
        char subexe[MAX_PATH];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        snprintf(sub, MAX_PATH, "%s\\%s", dir, fd.cFileName);
        snprintf(subexe, MAX_PATH, "%s\\*.exe", sub);
        {
            WIN32_FIND_DATAA fd2;
            HANDLE h2 = FindFirstFileA(subexe, &fd2);
            if (h2 == INVALID_HANDLE_VALUE)
                continue;
            do {
                add_candidate(sub, fd2.cFileName);
            } while (FindNextFileA(h2, &fd2));
            FindClose(h2);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void launch(const char *dir, const char *name)
{
    char full[MAX_PATH];
    char cmdline[2 * MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    snprintf(full, MAX_PATH, "%s\\%s", dir, name);
    snprintf(cmdline, sizeof(cmdline), "\"%s\"", full);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("Launch failed (error %lu): %s\n", GetLastError(), full);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("Launched: %s\n", full);
}

static void read_ini(char *emu, size_t emu_size)
{
    char ini[MAX_PATH];
    const char *slash;

    snprintf(ini, MAX_PATH, "%s", self_path);
    slash = strrchr(ini, '\\');
    if (slash)
        *(slash + 1) = '\0';
    strncat(ini, "launcher.ini", MAX_PATH - strlen(ini) - 1);

    emu[0] = '\0';
    {
        FILE *f = fopen(ini, "r");
        if (!f)
            return;
        if (fgets(emu, (int)emu_size, f)) {
            size_t n = strlen(emu);
            while (n > 0 && (emu[n - 1] == '\n' || emu[n - 1] == '\r'))
                emu[--n] = '\0';
        }
        fclose(f);
    }
}

static void write_ini(const char *emu)
{
    char ini[MAX_PATH];
    const char *slash;
    FILE *f;

    snprintf(ini, MAX_PATH, "%s", self_path);
    slash = strrchr(ini, '\\');
    if (slash)
        *(slash + 1) = '\0';
    strncat(ini, "launcher.ini", MAX_PATH - strlen(ini) - 1);

    f = fopen(ini, "w");
    if (f) {
        fprintf(f, "%s\n", emu);
        fclose(f);
    }
}

static void pick_file(const char *filter, const char *title, char *out, size_t out_size)
{
    OPENFILENAMEA ofn;
    char file[MAX_PATH];

    file[0] = '\0';
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

    printf("Opening file dialog: %s\n", title);
    if (!GetOpenFileNameA(&ofn) || file[0] == '\0') {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s", file);
}

static void run_emulator_mode(void)
{
    char emu[MAX_PATH];
    char game[MAX_PATH];
    char dir[MAX_PATH];
    char cmdline[3 * MAX_PATH];
    char input[8];
    char *slash;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    read_ini(emu, sizeof(emu));

    if (emu[0] != '\0') {
        printf("Saved emulator:\n  %s\n", emu);
        printf("Use it? [Y/n]: ");
        if (fgets(input, sizeof(input), stdin) &&
            (input[0] == 'n' || input[0] == 'N'))
            emu[0] = '\0';
    }

    if (emu[0] == '\0') {
        pick_file("Emulators (*.exe)\0*.exe\0All files (*.*)\0*.*\0",
                  "Select emulator executable", emu, sizeof(emu));
        if (emu[0] == '\0') {
            printf("No emulator selected.\n");
            return;
        }
        write_ini(emu);
        printf("Saved for next time in launcher.ini\n");
    }

    pick_file("All files (*.*)\0*.*\0", "Select game file to open", game, sizeof(game));
    if (game[0] == '\0') {
        printf("No game file selected.\n");
        return;
    }

    snprintf(dir, MAX_PATH, "%s", emu);
    slash = strrchr(dir, '\\');
    if (slash)
        *slash = '\0';

    snprintf(cmdline, sizeof(cmdline), "\"%s\" \"%s\"", emu, game);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("Launch failed (error %lu): %s %s\n", GetLastError(), emu, game);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("Launched emulator: %s\nGame file: %s\n", emu, game);
}

int main(int argc, char **argv)
{
    char dir[MAX_PATH];
    char choice[32];
    const char *target;

    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    if (argc > 1 && strcmp(argv[1], "/emu") == 0) {
        printf("GameLauncher - emulator mode\n");
        printf("Runs a game (disk image, ROM, dump) through an emulator.\n\n");
        run_emulator_mode();
        return 0;
    }

    if (argc > 1)
        target = argv[1];
    else {
        DWORD len = GetCurrentDirectoryA(MAX_PATH, dir);
        target = dir;
        (void)len;
    }

    printf("GameLauncher - scans a folder for game executables\n");
    printf("Folder: %s\n\n", target);

    scan_dir(target);

    if (found_count == 0) {
        printf("No .exe files found in this folder or its subfolders.\n");
        printf("Usage:\n");
        printf("  gamelauncher.exe [path-to-game-folder]  - scan folder and run an .exe\n");
        printf("  gamelauncher.exe /emu                   - run a game file via an emulator\n");
        return 1;
    }

    printf("Found %d executable(s):\n\n", found_count);
    for (int i = 0; i < found_count; i++) {
        const char *show = dir_list[i];
        printf("  [%3d] %-40s  (%s)\n", i + 1, exe_list[i], show);
    }

    printf("\nEnter number to launch (or press Enter to quit): ");
    if (fgets(choice, sizeof(choice), stdin) == NULL)
        return 0;
    {
        int n = atoi(choice);
        if (n >= 1 && n <= found_count)
            launch(dir_list[n - 1], exe_list[n - 1]);
        else
            printf("Nothing launched.\n");
    }
    return 0;
}
