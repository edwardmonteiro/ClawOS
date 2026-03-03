/*
 * ClawOS - Message Path Microbenchmarks
 *
 * Self-contained benchmarks for the three hottest paths:
 *   1. Message construction: stack vs malloc
 *   2. Socket round-trip: SEQPACKET message send/recv latency
 *   3. Topic dispatch: hash lookup vs linear scan
 *
 * Usage: ./bench_msg [iterations]
 *   Default: 1,000,000 iterations per benchmark
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>

#include "claw/types.h"

/* ------------------------------------------------------------------ */
/* Timing helpers                                                      */
/* ------------------------------------------------------------------ */

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

static void print_stats(const char *name, uint64_t *samples, int n)
{
    qsort(samples, n, sizeof(uint64_t), cmp_u64);

    uint64_t sum = 0;
    for (int i = 0; i < n; i++)
        sum += samples[i];

    printf("  %-30s  avg=%6lu ns  p50=%6lu  p95=%6lu  p99=%6lu  min=%6lu  max=%6lu\n",
           name,
           (unsigned long)(sum / n),
           (unsigned long)samples[n / 2],
           (unsigned long)samples[(int)(n * 0.95)],
           (unsigned long)samples[(int)(n * 0.99)],
           (unsigned long)samples[0],
           (unsigned long)samples[n - 1]);
}

/* ------------------------------------------------------------------ */
/* Bench 1: Message construction — stack vs malloc                     */
/* ------------------------------------------------------------------ */

/*
 * Simulates claw_ipc_emit() message construction with malloc+free.
 * This is what the old code did on every IPC call.
 */
static void bench_msg_construct_malloc(int iters)
{
    uint64_t *samples = malloc(iters * sizeof(uint64_t));

    for (int i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();

        /* Old path: malloc + memset + fill + free */
        size_t msg_size = sizeof(struct claw_msg) + 64;
        struct claw_msg *msg = malloc(msg_size);
        memset(msg, 0, sizeof(struct claw_msg));
        msg->id = i;
        msg->type = CLAW_MSG_EVENT;
        msg->src = 1;
        msg->dst = 0;
        strncpy(msg->topic, "bench.test", CLAW_MAX_NAME - 1);
        msg->len = 64;
        memset(msg->data, 0xAB, 64);
        /* Prevent compiler from optimizing away */
        asm volatile("" : : "r"(msg) : "memory");
        free(msg);

        samples[i] = now_ns() - t0;
    }

    print_stats("msg construct (malloc)", samples, iters);
    free(samples);
}

/*
 * Simulates the optimized path: stack-allocated message buffer.
 */
static void bench_msg_construct_stack(int iters)
{
    uint64_t *samples = malloc(iters * sizeof(uint64_t));

    for (int i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();

        /* New path: stack buffer + memset + fill */
        char buf[sizeof(struct claw_msg) + 4096];
        struct claw_msg *msg = (struct claw_msg *)buf;
        memset(msg, 0, sizeof(struct claw_msg));
        msg->id = i;
        msg->type = CLAW_MSG_EVENT;
        msg->src = 1;
        msg->dst = 0;
        strncpy(msg->topic, "bench.test", CLAW_MAX_NAME - 1);
        msg->len = 64;
        memset(msg->data, 0xAB, 64);
        asm volatile("" : : "r"(msg) : "memory");

        samples[i] = now_ns() - t0;
    }

    print_stats("msg construct (stack)", samples, iters);
    free(samples);
}

/* ------------------------------------------------------------------ */
/* Bench 2: SEQPACKET round-trip latency                               */
/* ------------------------------------------------------------------ */

/*
 * Measures send+recv latency over a SOCK_SEQPACKET socketpair.
 * This is the actual kernel<->agent path after our SEQPACKET change.
 *
 * Forks a child that echoes messages back. The parent measures
 * the round-trip time for each message.
 */
static void bench_seqpacket_rtt(int iters)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) < 0) {
        perror("socketpair");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Child: echo loop */
        close(sv[0]);
        char buf[CLAW_MAX_MSG_SIZE];
        while (1) {
            ssize_t n = recv(sv[1], buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            send(sv[1], buf, n, MSG_NOSIGNAL);
        }
        close(sv[1]);
        _exit(0);
    }

    /* Parent: measure round-trip */
    close(sv[1]);

    /* Build a typical message */
    char msg_buf[sizeof(struct claw_msg) + 64];
    struct claw_msg *msg = (struct claw_msg *)msg_buf;
    memset(msg, 0, sizeof(struct claw_msg));
    msg->type = CLAW_MSG_REQUEST;
    msg->src = 1;
    msg->dst = 2;
    strncpy(msg->topic, "resolve", CLAW_MAX_NAME - 1);
    msg->len = 64;
    memset(msg->data, 0, 64);
    size_t total = sizeof(struct claw_msg) + msg->len;

    /* Warmup */
    for (int i = 0; i < 1000; i++) {
        send(sv[0], msg, total, MSG_NOSIGNAL);
        recv(sv[0], msg_buf, sizeof(msg_buf), 0);
    }

    uint64_t *samples = malloc(iters * sizeof(uint64_t));

    for (int i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        send(sv[0], msg, total, MSG_NOSIGNAL);
        recv(sv[0], msg_buf, sizeof(msg_buf), 0);
        samples[i] = now_ns() - t0;
    }

    print_stats("SEQPACKET round-trip", samples, iters);
    free(samples);

    close(sv[0]);
    waitpid(pid, NULL, 0);
}

/*
 * Same test but with SOCK_STREAM for comparison.
 * Shows the overhead of stream sockets and the risk of
 * message boundary issues.
 */
static void bench_stream_rtt(int iters)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        perror("socketpair");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        close(sv[0]);
        char buf[CLAW_MAX_MSG_SIZE];
        while (1) {
            ssize_t n = recv(sv[1], buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            send(sv[1], buf, n, MSG_NOSIGNAL);
        }
        close(sv[1]);
        _exit(0);
    }

    close(sv[1]);

    char msg_buf[sizeof(struct claw_msg) + 64];
    struct claw_msg *msg = (struct claw_msg *)msg_buf;
    memset(msg, 0, sizeof(struct claw_msg));
    msg->type = CLAW_MSG_REQUEST;
    msg->src = 1;
    msg->dst = 2;
    strncpy(msg->topic, "resolve", CLAW_MAX_NAME - 1);
    msg->len = 64;
    memset(msg->data, 0, 64);
    size_t total = sizeof(struct claw_msg) + msg->len;

    for (int i = 0; i < 1000; i++) {
        send(sv[0], msg, total, MSG_NOSIGNAL);
        recv(sv[0], msg_buf, sizeof(msg_buf), 0);
    }

    uint64_t *samples = malloc(iters * sizeof(uint64_t));

    for (int i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        send(sv[0], msg, total, MSG_NOSIGNAL);
        recv(sv[0], msg_buf, sizeof(msg_buf), 0);
        samples[i] = now_ns() - t0;
    }

    print_stats("STREAM round-trip", samples, iters);
    free(samples);

    close(sv[0]);
    waitpid(pid, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Bench 3: Topic dispatch — hash vs linear scan                       */
/* ------------------------------------------------------------------ */

#define BENCH_MAX_SUBS  1024
#define BENCH_HASH_BUCKETS  256

struct bench_sub {
    char    topic[CLAW_MAX_NAME];
    int     active;
    int     next;   /* hash chain */
};

/*
 * Linear scan: O(n) per publish.
 * This is what the old bus did.
 */
static int linear_dispatch(struct bench_sub *subs, int count,
                           const char *topic)
{
    int matched = 0;
    for (int i = 0; i < count; i++) {
        if (!subs[i].active)
            continue;
        if (subs[i].topic[0] != '\0' &&
            strcmp(subs[i].topic, topic) != 0)
            continue;
        matched++;
    }
    return matched;
}

static unsigned int bench_topic_hash(const char *s)
{
    unsigned int h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h & (BENCH_HASH_BUCKETS - 1);
}

/*
 * Hash dispatch: O(chain_length) per publish.
 * This is what the optimized bus does.
 */
static int hash_dispatch(struct bench_sub *subs, int *heads,
                         const char *topic)
{
    int matched = 0;
    unsigned int h = bench_topic_hash(topic);
    int idx = heads[h];
    while (idx >= 0) {
        if (subs[idx].active && strcmp(subs[idx].topic, topic) == 0)
            matched++;
        idx = subs[idx].next;
    }
    return matched;
}

static void bench_topic_dispatch(int iters)
{
    /* Set up 1000 subscriptions across 50 topics */
    struct bench_sub subs[BENCH_MAX_SUBS];
    int heads[BENCH_HASH_BUCKETS];
    memset(subs, 0, sizeof(subs));
    for (int i = 0; i < BENCH_HASH_BUCKETS; i++)
        heads[i] = -1;

    int sub_count = 0;
    for (int t = 0; t < 50; t++) {
        char topic[CLAW_MAX_NAME];
        snprintf(topic, sizeof(topic), "agent.topic.%d", t);
        /* 20 subscribers per topic = 1000 total */
        for (int s = 0; s < 20 && sub_count < BENCH_MAX_SUBS; s++) {
            strncpy(subs[sub_count].topic, topic, CLAW_MAX_NAME - 1);
            subs[sub_count].active = 1;

            unsigned int h = bench_topic_hash(topic);
            subs[sub_count].next = heads[h];
            heads[h] = sub_count;

            sub_count++;
        }
    }

    const char *target = "agent.topic.25";  /* middle topic */
    uint64_t *samples = malloc(iters * sizeof(uint64_t));

    /* Bench linear scan */
    for (int i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        int m = linear_dispatch(subs, sub_count, target);
        asm volatile("" : : "r"(m) : "memory");
        samples[i] = now_ns() - t0;
    }
    print_stats("topic dispatch (linear)", samples, iters);

    /* Bench hash dispatch */
    for (int i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        int m = hash_dispatch(subs, heads, target);
        asm volatile("" : : "r"(m) : "memory");
        samples[i] = now_ns() - t0;
    }
    print_stats("topic dispatch (hash)", samples, iters);

    free(samples);
}

/* ------------------------------------------------------------------ */
/* Bench 4: Throughput — sustained message send rate                    */
/* ------------------------------------------------------------------ */

/*
 * Measures how many messages per second can be pushed through
 * a SEQPACKET socketpair. The child drains as fast as possible.
 * Parent sends for 1 second and counts messages.
 */
static void bench_throughput(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) < 0) {
        perror("socketpair");
        return;
    }

    /* Increase socket buffer sizes for throughput test */
    int bufsize = 1024 * 1024;  /* 1MB */
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Child: drain messages */
        close(sv[0]);
        char buf[CLAW_MAX_MSG_SIZE];
        while (recv(sv[1], buf, sizeof(buf), 0) > 0)
            ;
        close(sv[1]);
        _exit(0);
    }

    close(sv[1]);

    /* Build a typical small message (resolve request ~160 bytes) */
    char msg_buf[sizeof(struct claw_msg) + 8];
    struct claw_msg *msg = (struct claw_msg *)msg_buf;
    memset(msg, 0, sizeof(struct claw_msg));
    msg->type = CLAW_MSG_REQUEST;
    msg->src = 1;
    msg->dst = 0;
    strncpy(msg->topic, "resolve", CLAW_MAX_NAME - 1);
    msg->len = 8;
    memcpy(msg->data, "net.dns\0", 8);
    size_t total = sizeof(struct claw_msg) + msg->len;

    uint64_t start = now_ns();
    uint64_t deadline = start + 1000000000ULL;  /* 1 second */
    long count = 0;
    long dropped = 0;

    while (now_ns() < deadline) {
        ssize_t n = send(sv[0], msg, total, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n > 0) {
            count++;
        } else {
            dropped++;
            /* Brief yield when buffer is full */
            struct timespec ts = { .tv_nsec = 100 };
            nanosleep(&ts, NULL);
        }
    }

    uint64_t elapsed_ns = now_ns() - start;
    double secs = elapsed_ns / 1e9;
    double rate = count / secs;
    double bytes_per_sec = (count * total) / secs;

    printf("  %-30s  %ld msgs in %.2fs = %.0f msg/s (%.1f MB/s, %ld drops)\n",
           "SEQPACKET throughput",
           count, secs, rate,
           bytes_per_sec / (1024.0 * 1024.0),
           dropped);

    close(sv[0]);
    waitpid(pid, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int iters = 1000000;
    if (argc > 1)
        iters = atoi(argv[1]);
    if (iters < 1000)
        iters = 1000;

    printf("ClawOS Message Path Benchmarks (%d iterations)\n", iters);
    printf("================================================\n\n");

    printf("[1] Message construction\n");
    bench_msg_construct_malloc(iters);
    bench_msg_construct_stack(iters);
    printf("\n");

    printf("[2] Socket round-trip latency\n");
    bench_stream_rtt(iters);
    bench_seqpacket_rtt(iters);
    printf("\n");

    printf("[3] Topic dispatch (1000 subs, 50 topics)\n");
    bench_topic_dispatch(iters);
    printf("\n");

    printf("[4] Sustained throughput (1 second)\n");
    bench_throughput();
    printf("\n");

    return 0;
}
