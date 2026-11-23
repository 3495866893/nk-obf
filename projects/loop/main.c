/* loop 样例：故意活 12 秒、多线程、反复分配，用来验证运行时看门狗在
 * 「长时间运行 + 多线程 + 频繁堆操作」下不误杀（真机误杀回归样本）。 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile long g_sink = 0;

static void *worker(void *p) {
    long id = (long)p;
    for (int i = 0; i < 24; i++) {
        char *b = (char *)malloc(4096);
        if (!b) break;
        memset(b, (int)(id + i), 4096);
        g_sink += b[i * 7 % 4096];
        free(b);
        usleep(120 * 1000);
    }
    return NULL;
}

int main(void) {
    printf("loop start\n");
    fflush(stdout);
    pthread_t t[3];
    for (long i = 0; i < 3; i++) pthread_create(&t[i], NULL, worker, (void *)i);
    for (int s = 0; s < 12; s++) {
        printf("tick %d\n", s + 1);
        fflush(stdout);
        usleep(1000 * 1000);
    }
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);
    printf("loop done sink=%ld\n", g_sink);
    return 0;
}
