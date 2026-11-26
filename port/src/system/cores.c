/*
 * The game thread on a hybrid CPU's fast cores (cores.h, xg-hyd0).
 *
 * Measured on an i9-12900K (8 P-cores at 5.1-5.2 GHz, 8 E-cores at
 * 3.9 GHz) under scx_lavd, which sees every core's cpu_capacity as 1024: the
 * player's game thread sat on the E-cores 5 s of 30 s, and in Lahan with the
 * free camera turning (a real window, vblanks 560-900) 45 of 341 vblanks came
 * late with the thread held on the P-cores, 110 on the E-cores, 101 left to
 * the scheduler; the room's drawing took 7.5 ms (median) a picture on a
 * P-core and 10.4 on an E-core.
 *
 * Which cores are fast, from sysfs: Intel's hybrid parts list their big cores
 * in /sys/devices/cpu_core/cpus (and the small ones in cpu_atom); otherwise
 * the cores whose cpu_capacity is the largest, when the capacities differ
 * (ARM's clusters); otherwise the cores whose top clock is within 85% of the
 * fastest, when some are below that (two core designs at different clocks).
 * A machine whose cores are all alike is left alone, as is a game already
 * kept to fewer cores (taskset, a cgroup) when none of those is fast.
 */

#define _GNU_SOURCE 1 /* sched_getaffinity, CPU_AND, CPU_COUNT */

#include "cores.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <errno.h>
#include <sched.h>
#include <sys/resource.h>

static cpu_set_t startMask; /* the game thread's cores before the pin */
static cpu_set_t fastMask;  /* and after it */
static int pinnedToFast;

/* A sysfs CPU list ("0-15,18,20-21") into set; 0 when unreadable or empty. */
static int readCpuList(const char* path, cpu_set_t* set) {
    char buf[1024];
    FILE* f = fopen(path, "r");
    size_t n;
    int count = 0;
    const char* p;
    CPU_ZERO(set);
    if (f == NULL) return 0;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    for (p = buf; *p != '\0' && *p != '\n';) {
        char* end;
        long lo = strtol(p, &end, 10), hi;
        if (end == p) return 0;
        hi = lo;
        p = end;
        if (*p == '-') {
            hi = strtol(p + 1, &end, 10);
            if (end == p + 1) return 0;
            p = end;
        }
        for (; lo <= hi && lo < CPU_SETSIZE; lo++) {
            if (lo >= 0) {
                CPU_SET((int)lo, set);
                count++;
            }
        }
        if (*p == ',') p++;
    }
    return count;
}

static long readCpuValue(int cpu, const char* leaf) {
    char path[128];
    long v = -1;
    FILE* f;
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/%s", cpu, leaf);
    f = fopen(path, "r");
    if (f != NULL) {
        if (fscanf(f, "%ld", &v) != 1) v = -1;
        fclose(f);
    }
    return v;
}

/* The allowed cores whose leaf value is at least percent% of the largest, when
 * some allowed core is below that; 0 (fast empty) when they are all alike or
 * a value is missing. */
static int fastByValue(const cpu_set_t* allowed, const char* leaf, int percent, cpu_set_t* fast) {
    long top = 0, v;
    int cpu, slow = 0, count = 0;
    CPU_ZERO(fast);
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (!CPU_ISSET(cpu, allowed)) continue;
        if ((v = readCpuValue(cpu, leaf)) <= 0) return 0;
        if (v > top) top = v;
    }
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (!CPU_ISSET(cpu, allowed)) continue;
        if (readCpuValue(cpu, leaf) * 100 >= top * percent) {
            CPU_SET(cpu, fast);
            count++;
        } else {
            slow++;
        }
    }
    return slow > 0 ? count : 0;
}

/* The fast cores among allowed; 0 when there is nothing to choose. */
static int fastCores(const cpu_set_t* allowed, cpu_set_t* fast, const char** how) {
    cpu_set_t big, small;
    int count;
    if (readCpuList("/sys/devices/cpu_core/cpus", &big) > 0 && readCpuList("/sys/devices/cpu_atom/cpus", &small) > 0) {
        CPU_AND(fast, &big, allowed);
        *how = "Intel hybrid: cpu_core";
        count = CPU_COUNT(fast);
        return count < CPU_COUNT(allowed) ? count : 0;
    }
    if ((count = fastByValue(allowed, "cpu_capacity", 90, fast)) > 0) {
        *how = "cpu_capacity";
        return count;
    }
    if ((count = fastByValue(allowed, "cpufreq/cpuinfo_max_freq", 85, fast)) > 0) {
        *how = "top clock";
        return count;
    }
    return 0;
}

static void describeSet(const cpu_set_t* set, char* out, size_t size) {
    size_t len = 0;
    int cpu = 0;
    out[0] = '\0';
    while (cpu < CPU_SETSIZE && len + 16 < size) {
        int end;
        if (!CPU_ISSET(cpu, set)) {
            cpu++;
            continue;
        }
        for (end = cpu; end + 1 < CPU_SETSIZE && CPU_ISSET(end + 1, set); end++) {
        }
        len += (size_t)snprintf(out + len, size - len, end > cpu ? "%s%d-%d" : "%s%d", len ? "," : "", cpu, end);
        cpu = end + 1;
    }
}

void coresPinGameThread(void) {
    cpu_set_t fast;
    const char* how = NULL;
    const char* v;
    char list[256];
    int count;
    settingsDescribe("fast_cores", "fast_cores: auto (default: on a CPU with fast and slow cores, such as Intel's P- and "
                                   "E-cores, the game thread stays on the fast ones) or off (wherever the system's "
                                   "scheduler puts it); XG_FAST_CORES overrides for one run");
    v = settingsGet("fast_cores");
    if (pinnedToFast || (v != NULL && strcmp(v, "off") == 0)) return;
    /* No one watches a headless run (the tests, agents' runs beside the player's game), and one
     * started niced (tools/yield.sh) has asked to give way: kept to the fast cores either would
     * wait there behind the machine's other work instead of taking an idle slow core (the 4x
     * battle's real-clock test ran its sound device dry more often, xg-hyd0). ananicy may renice
     * a xenogears process to -5 later; XG_HEADLESS still tells. */
    const char* headless = getenv("XG_HEADLESS");
    if (headless != NULL && headless[0] != '\0' && headless[0] != '0') return;
    errno = 0;
    int nice = getpriority(PRIO_PROCESS, 0);
    if (errno == 0 && nice > 0) return;
    if (sched_getaffinity(0, sizeof(startMask), &startMask) != 0) return;
    if ((count = fastCores(&startMask, &fast, &how)) <= 0) return;
    if (sched_setaffinity(0, sizeof(fast), &fast) != 0) {
        fprintf(stderr, "cores: could not keep the game thread on the fast cores\n");
        return;
    }
    fastMask = fast;
    pinnedToFast = 1;
    describeSet(&fast, list, sizeof(list));
    fprintf(stderr, "cores: the game thread on the fast cores %s (%s; %d of %d)\n", list, how, count, CPU_COUNT(&startMask));
}

void coresPlace(int fast) {
    if (pinnedToFast) {
        sched_setaffinity(0, sizeof(cpu_set_t), fast ? &fastMask : &startMask);
    }
}

#else

void coresPinGameThread(void) {
}

#endif
