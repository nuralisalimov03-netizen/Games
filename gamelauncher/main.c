#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    snprintf(full, MAX_PATH, "%s\\%s", dir, name);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(full, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("Launch failed (error %lu): %s\n", GetLastError(), full);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("Launched: %s\n", full);
}

int main(int argc, char **argv)
{
    char dir[MAX_PATH];
    char choice[32];
    const char *target;

    GetModuleFileNameA(NULL, self_path, MAX_PATH);

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
        printf("Usage: gamelauncher.exe [path-to-game-folder]\n");
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
