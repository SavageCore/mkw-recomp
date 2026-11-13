// LD_PRELOAD over ioctl (Linux): per thread name and request code, calls, total ms and the longest, printed every 2 s
// (deltas) to stderr, into the run's log. Why: perf showed the replay thread half in the kernel during the boot with
// no symbols; this said which driver calls (amdgpu GEM_VA 0xc0406448 for sparse binds, CS 0xc0186444, GEM_CREATE
// 0xc0206440) and which thread (carga-io2, 14/11/2026). Build: cc -O2 -shared -fPIC -o build/ioctl_shim.so
// tools/analisis/linux/ioctl_shim.c -ldl -lpthread; run: LIVE_WRAP="env LD_PRELOAD=$PWD/build/ioctl_shim.so" tools/run/live.sh NAME
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
typedef int (*ioctl_fn)(int, unsigned long, ...);
static ioctl_fn real;
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
#define N 512
static struct { char name[16]; unsigned long req; uint64_t n, ns, max; } tab[N];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static __thread char tname[17];
static void *reporter(void *a) {
    (void)a;
    static uint64_t pn[N], pns[N];
    for (;;) {
        sleep(2);
        pthread_mutex_lock(&mu);
        fprintf(stderr, "ioctl_shim: t=%.1f\n", now() / 1e9);
        for (int i = 0; i < N; i++) {
            if (!tab[i].n || tab[i].n == pn[i]) continue;
            fprintf(stderr, "ioctl_shim:   %-15s req 0x%08lx calls %6llu  %8.1f ms  max %6.1f ms\n", tab[i].name, tab[i].req,
                    (unsigned long long)(tab[i].n - pn[i]), (tab[i].ns - pns[i]) / 1e6, tab[i].max / 1e6);
            pn[i] = tab[i].n, pns[i] = tab[i].ns, tab[i].max = 0;
        }
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}
__attribute__((constructor)) static void init(void) {
    real = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    pthread_t t; pthread_create(&t, NULL, reporter, NULL); pthread_detach(t);
}
int ioctl(int fd, unsigned long req, ...) {
    va_list ap; va_start(ap, req); void *arg = va_arg(ap, void *); va_end(ap);
    uint64_t t0 = now();
    int r = real(fd, req, arg);
    uint64_t d = now() - t0;
    if (!tname[0]) prctl(PR_GET_NAME, tname);
    pthread_mutex_lock(&mu);
    int i = 0;
    for (; i < N && tab[i].n; i++) if (tab[i].req == req && !strcmp(tab[i].name, tname)) break;
    if (i < N) {
        if (!tab[i].n) { strcpy(tab[i].name, tname); tab[i].req = req; }
        tab[i].n++, tab[i].ns += d;
        if (d > tab[i].max) tab[i].max = d;
    }
    pthread_mutex_unlock(&mu);
    return r;
}
