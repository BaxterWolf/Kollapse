/* compat.h - POSIX versions of the Windows calls the shared code uses. Paths use SEP / SEPS. */
#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SEP  '\\'
#define SEPS "\\"
static inline int is_abs_path(const char *p) { return (p[0] && p[1] == ':') || p[0] == '\\' || p[0] == '/'; }
static inline int is_root_path(const char *p) { return strlen(p) <= 3; }   /* "C:\" */

#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>

#define SEP  '/'
#define SEPS "/"
static inline int is_abs_path(const char *p) { return p[0] == '/'; }
static inline int is_root_path(const char *p) { return !strcmp(p, "/"); }

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif
typedef uint32_t DWORD;
typedef int BOOL;
typedef void *LPVOID;
typedef void *HANDLE;
#define WINAPI
#define TRUE 1
#define FALSE 0

#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#define _strdup   strdup

/* locks and threads */
typedef pthread_mutex_t CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *m)
{
    pthread_mutexattr_t a;                                 /* recursive, like a Windows critical section */
    pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a); pthread_mutexattr_destroy(&a);
}
#define EnterCriticalSection(m) pthread_mutex_lock(m)
#define LeaveCriticalSection(m) pthread_mutex_unlock(m)

typedef DWORD (*compat_thread_fn)(LPVOID);
struct compat_thread { compat_thread_fn fn; LPVOID arg; };
static inline void *compat_thread_start(void *p)
{
    struct compat_thread t = *(struct compat_thread *)p;
    free(p);
    t.fn(t.arg);
    return NULL;
}
static inline HANDLE CreateThread(void *sec, size_t stack, compat_thread_fn fn, LPVOID arg, DWORD flags, DWORD *id)
{
    pthread_t th;
    struct compat_thread *t = malloc(sizeof *t);
    (void)sec; (void)stack; (void)flags; (void)id;
    if (!t) return NULL;
    t->fn = fn; t->arg = arg;
    if (pthread_create(&th, NULL, compat_thread_start, t)) { free(t); return NULL; }
    pthread_detach(th);
    return (HANDLE)1;
}
static inline BOOL CloseHandle(HANDLE h) { (void)h; return TRUE; }

static inline DWORD GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}
static inline void Sleep(DWORD ms) { struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }

/* environment: APPDATA is the config folder, USERPROFILE the home folder */
static inline DWORD GetEnvironmentVariableA(const char *name, char *out, DWORD n)
{
    const char *v = NULL, *home = getenv("HOME");
    char buf[MAX_PATH];
    if (!strcmp(name, "APPDATA")) {
        if ((v = getenv("XDG_CONFIG_HOME")) == NULL || !*v) {
            if (!home) return 0;
            snprintf(buf, sizeof buf, "%s/.config", home); v = buf;
        }
    } else if (!strcmp(name, "USERPROFILE")) v = home;
    else v = getenv(name);
    if (!v || strlen(v) + 1 > n) return 0;
    strcpy(out, v);
    return (DWORD)strlen(out);
}

/* files and folders */
#define INVALID_FILE_ATTRIBUTES  0xFFFFFFFFu
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_HIDDEN    0x02
#define FILE_ATTRIBUTE_SYSTEM    0x04
#define MOVEFILE_REPLACE_EXISTING 1
static inline DWORD GetFileAttributesA(const char *p)
{
    struct stat st;
    if (stat(p, &st)) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0x80;
}
typedef struct { unsigned long long t; } FILETIME;
typedef struct { DWORD dwFileAttributes; FILETIME ftLastWriteTime; } WIN32_FILE_ATTRIBUTE_DATA;
enum { GetFileExInfoStandard };
static inline BOOL GetFileAttributesExA(const char *p, int lvl, WIN32_FILE_ATTRIBUTE_DATA *d)
{
    struct stat st;
    (void)lvl;
    if (stat(p, &st)) return FALSE;
    d->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0x80;
    d->ftLastWriteTime.t = (unsigned long long)st.st_mtime;
    return TRUE;
}
static inline long CompareFileTime(const FILETIME *a, const FILETIME *b) { return a->t < b->t ? -1 : a->t > b->t; }
static inline BOOL CreateDirectoryA(const char *p, void *sec) { (void)sec; return mkdir(p, 0755) == 0; }
static inline BOOL DeleteFileA(const char *p) { return unlink(p) == 0; }
static inline BOOL MoveFileExA(const char *a, const char *b, DWORD f) { (void)f; return rename(a, b) == 0; }
static inline BOOL SetCurrentDirectoryA(const char *p) { return chdir(p) == 0; }
static inline DWORD GetCurrentDirectoryA(DWORD n, char *out) { return getcwd(out, n) ? (DWORD)strlen(out) : 0; }
static inline DWORD GetFullPathNameA(const char *p, DWORD n, char *out, char **file)
{
    char cwd[MAX_PATH], tmp[MAX_PATH * 2], *seg[256];
    int ns = 0;
    if (p[0] == '/') snprintf(tmp, sizeof tmp, "%s", p);
    else if (getcwd(cwd, sizeof cwd)) snprintf(tmp, sizeof tmp, "%s/%s", cwd, p);
    else snprintf(tmp, sizeof tmp, "%s", p);
    for (char *s = strtok(tmp, "/"); s && ns < 256; s = strtok(NULL, "/")) {   /* drop "." and resolve ".." like Windows */
        if (!strcmp(s, ".")) continue;
        if (!strcmp(s, "..")) { if (ns) ns--; continue; }
        seg[ns++] = s;
    }
    size_t o = 0;
    out[0] = 0;
    for (int i = 0; i < ns && o + 1 < n; i++) o += (size_t)snprintf(out + o, n - o, "/%s", seg[i]);
    if (!ns) snprintf(out, n, "/");
    if (file) { char *s = strrchr(out, '/'); *file = s && s[1] ? s + 1 : NULL; }
    return (DWORD)strlen(out);
}
static inline DWORD GetTempPathA(DWORD n, char *out)
{
    const char *t = getenv("TMPDIR");
    snprintf(out, n, "%s/", t && *t ? t : "/tmp");
    return (DWORD)strlen(out);
}
static inline unsigned GetTempFileNameA(const char *dir, const char *prefix, unsigned u, char *out)
{
    (void)u;
    snprintf(out, MAX_PATH, "%s%sXXXXXX", dir, prefix);
    int fd = mkstemp(out);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

/* directory listing: FindFirstFileA of "*" or "dir" + "/" + "*" */
typedef struct { DWORD dwFileAttributes; char cFileName[MAX_PATH]; } WIN32_FIND_DATAA;
#define INVALID_HANDLE_VALUE ((HANDLE)0)
struct compat_find { DIR *d; char dir[MAX_PATH]; };
static inline BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd)
{
    struct compat_find *f = h;
    struct dirent *e;
    while ((e = readdir(f->d))) {
        char full[MAX_PATH * 2];
        snprintf(full, sizeof full, "%s/%s", f->dir, e->d_name);
        snprintf(fd->cFileName, sizeof fd->cFileName, "%s", e->d_name);
        DWORD a = GetFileAttributesA(full);
        fd->dwFileAttributes = (a == INVALID_FILE_ATTRIBUTES ? 0 : a) | (e->d_name[0] == '.' ? FILE_ATTRIBUTE_HIDDEN : 0);
        return TRUE;
    }
    return FALSE;
}
static inline HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd)
{
    struct compat_find *f = calloc(1, sizeof *f);
    if (!f) return INVALID_HANDLE_VALUE;
    snprintf(f->dir, sizeof f->dir, "%s", pattern);
    char *star = strrchr(f->dir, '*');
    if (star) *star = 0;
    size_t l = strlen(f->dir);
    if (l > 1 && f->dir[l - 1] == '/') f->dir[l - 1] = 0;
    if (!f->dir[0]) strcpy(f->dir, ".");
    if (!(f->d = opendir(f->dir)) || !FindNextFileA(f, fd)) { if (f->d) closedir(f->d); free(f); return INVALID_HANDLE_VALUE; }
    return f;
}
static inline BOOL FindClose(HANDLE h) { struct compat_find *f = h; closedir(f->d); free(f); return TRUE; }

/* kollapse.ini: [section] key=value, written back in place */
static inline DWORD GetPrivateProfileStringA(const char *sec, const char *key, const char *def, char *out, DWORD n, const char *file)
{
    FILE *f = fopen(file, "r");
    char line[2048], cur[128] = "";
    snprintf(out, n, "%s", def);
    if (!f) return (DWORD)strlen(out);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { char *e = strchr(line, ']'); if (e) *e = 0; snprintf(cur, sizeof cur, "%s", line + 1); continue; }
        char *eq = strchr(line, '=');
        if (!eq || strcasecmp(cur, sec)) continue;
        *eq = 0;
        if (!strcasecmp(line, key)) { snprintf(out, n, "%s", eq + 1); break; }
    }
    fclose(f);
    return (DWORD)strlen(out);
}
static inline unsigned GetPrivateProfileIntA(const char *sec, const char *key, int def, const char *file)
{
    char b[64], d[32];
    snprintf(d, sizeof d, "%d", def);
    GetPrivateProfileStringA(sec, key, d, b, sizeof b, file);
    return (unsigned)atoi(b);
}
static inline BOOL WritePrivateProfileStringA(const char *sec, const char *key, const char *val, const char *file)
{
    FILE *f = fopen(file, "r");
    char *text = NULL, line[2048], cur[128] = "";
    size_t len = 0, cap = 0;
    int done = 0, have_sec = 0;
    #define COMPAT_ADD(s) do { size_t l_ = strlen(s); if (len + l_ + 1 > cap) { cap = (len + l_ + 1) * 2; text = realloc(text, cap); } memcpy(text + len, s, l_ + 1); len += l_; } while (0)
    COMPAT_ADD("");
    while (f && fgets(line, sizeof line, f)) {
        char copy[2048]; snprintf(copy, sizeof copy, "%s", line);
        copy[strcspn(copy, "\r\n")] = 0;
        if (copy[0] == '[') {
            if (!done && have_sec) { char kv[2304]; snprintf(kv, sizeof kv, "%s=%s\n", key, val); COMPAT_ADD(kv); done = 1; }
            char *e = strchr(copy, ']'); if (e) *e = 0;
            snprintf(cur, sizeof cur, "%s", copy + 1);
            have_sec = !strcasecmp(cur, sec);
        } else if (have_sec && !done) {
            char *eq = strchr(copy, '=');
            if (eq) { *eq = 0; if (!strcasecmp(copy, key)) { char kv[2304]; snprintf(kv, sizeof kv, "%s=%s\n", key, val); COMPAT_ADD(kv); done = 1; continue; } }
        }
        COMPAT_ADD(line);
        if (len && text[len - 1] != '\n') COMPAT_ADD("\n");
    }
    if (f) fclose(f);
    if (!done) {
        char kv[2304];
        if (!have_sec) { snprintf(kv, sizeof kv, "[%s]\n", sec); COMPAT_ADD(kv); }
        snprintf(kv, sizeof kv, "%s=%s\n", key, val); COMPAT_ADD(kv);
    }
    #undef COMPAT_ADD
    f = fopen(file, "w");
    int ok = f && fputs(text, f) >= 0;
    if (f && fclose(f)) ok = 0;
    free(text);
    return ok;
}
#endif
#endif
