#pragma once
#include <dirent.h>
extern bool BallSwitch, MemuSwitch;
#include <linux/input.h>
#include <sys/select.h>
#include <sys/system_properties.h>

// 音量键隐藏悬浮窗：音量+ 显示，音量- 隐藏（IsBall -> BallSwitch）
static int vk_event_count() {
    DIR *dir = opendir("/dev/input/");
    if (!dir) return -1;
    int count = 0;
    struct dirent *ptr;
    while ((ptr = readdir(dir)) != NULL)
        if (strstr(ptr->d_name, "event")) count++;
    closedir(dir);
    return count > 0 ? count : -1;
}

static void vk_handle(struct input_event ev) {
    if (ev.type != EV_KEY || ev.value != 1) return;
    if (ev.code == KEY_VOLUMEUP) { BallSwitch = true; MemuSwitch = true; }
    else if (ev.code == 114) { BallSwitch = false; }
}

static int vk_thread() {
    int n = vk_event_count();
    if (n < 0) return -1;
    int *fd = (int *)malloc(n * sizeof(int));
    fd_set fds; struct timeval tv; int maxfd = 0;
    for (int i = 0; i < n; i++) {
        char tmp[128]; snprintf(tmp, sizeof(tmp), "/dev/input/event%d", i);
        fd[i] = open(tmp, O_RDONLY | O_NONBLOCK);
        if (fd[i] > maxfd) maxfd = fd[i];
    }
    struct input_event ev;
    while (1) {
        FD_ZERO(&fds);
        for (int i = 0; i < n; i++) if (fd[i] >= 0) FD_SET(fd[i], &fds);
        tv.tv_sec = 5; tv.tv_usec = 0;
        int ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) { usleep(5000); continue; }
        for (int i = 0; i < n; i++) {
            if (FD_ISSET(fd[i], &fds) && read(fd[i], &ev, sizeof(ev)) == sizeof(ev))
                vk_handle(ev);
        }
        usleep(5000);
    }
    for (int i = 0; i < n; i++) if (fd[i] >= 0) close(fd[i]);
    free(fd);
    return 0;
}

static void vk_start() {
    new std::thread(vk_thread);
}

static int FuckMi8() {
    const char* Mi8[] = { "M1803E6A","M1803E6B","M1803E6C","M1807E8T","M1807E8A",
                          "M1805D1SE","M1805D1S","M1908F1XE" };
    char model[PROP_VALUE_MAX];
    if (__system_property_get("ro.product.model", model) <= 0) return 0;
    for (unsigned i = 0; i < sizeof(Mi8)/sizeof(Mi8[0]); i++)
        if (strcmp(model, Mi8[i]) == 0) exit(1);
    return 0;
}