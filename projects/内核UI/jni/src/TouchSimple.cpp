#include "TouchSimple.h"

#include "AetherUI.h"
#include "imgui.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
struct TouchDev {
  int fd = -1;
  int slot = 0;
  float minX = 0, maxX = 0, minY = 0, maxY = 0;
  float absX[10]{};
  float absY[10]{};
  bool down[10]{};
};

std::vector<TouchDev> g_devs;
float g_w = 1, g_h = 1;
uint32_t g_ori = 0;
bool g_tracking = false;
float g_lastX = 0, g_lastY = 0;
float g_upHold = 0.f;

int g_uinput = -1;
bool g_grabbed = false;
bool g_blockGesture = false;
int g_injTracking = -1;

void MapToScreen(float ax, float ay, float minX, float maxX, float minY, float maxY, float& ox,
                 float& oy) {
  float nx = (maxX > minX) ? (ax - minX) / (maxX - minX) : 0.f;
  float ny = (maxY > minY) ? (ay - minY) / (maxY - minY) : 0.f;
  nx = nx < 0 ? 0 : (nx > 1 ? 1 : nx);
  ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
  switch (g_ori) {
  case 1:
    ox = ny * g_w;
    oy = (1.f - nx) * g_h;
    break;
  case 3:
    ox = (1.f - ny) * g_w;
    oy = nx * g_h;
    break;
  case 2:
    ox = (1.f - nx) * g_w;
    oy = (1.f - ny) * g_h;
    break;
  default:
    ox = nx * g_w;
    oy = ny * g_h;
    break;
  }
}

void ScreenToAbs(float sx, float sy, const TouchDev& d, int& ax, int& ay) {
  float nx = 0.f, ny = 0.f;
  switch (g_ori) {
  case 1:
    nx = 1.f - (sy / g_h);
    ny = sx / g_w;
    break;
  case 3:
    nx = sy / g_h;
    ny = 1.f - (sx / g_w);
    break;
  case 2:
    nx = 1.f - (sx / g_w);
    ny = 1.f - (sy / g_h);
    break;
  default:
    nx = sx / g_w;
    ny = sy / g_h;
    break;
  }
  if (nx < 0)
    nx = 0;
  if (nx > 1)
    nx = 1;
  if (ny < 0)
    ny = 0;
  if (ny > 1)
    ny = 1;
  ax = (int)(d.minX + nx * (d.maxX - d.minX) + 0.5f);
  ay = (int)(d.minY + ny * (d.maxY - d.minY) + 0.5f);
}

bool IsTouchDevice(int fd) {
  unsigned long absbits[(ABS_MAX + 1 + 63) / 64]{};
  if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0)
    return false;
  auto has = [&](int bit) { return (absbits[bit / 64] >> (bit % 64)) & 1ULL; };
  return has(ABS_MT_POSITION_X) && has(ABS_MT_POSITION_Y);
}

void ReadAbsRange(int fd, int code, float& mn, float& mx) {
  input_absinfo info{};
  if (ioctl(fd, EVIOCGABS(code), &info) == 0) {
    mn = (float)info.minimum;
    mx = (float)info.maximum;
  }
}

void Emit(int fd, uint16_t type, uint16_t code, int32_t value) {
  input_event ev{};
  ev.type = type;
  ev.code = code;
  ev.value = value;
  (void)write(fd, &ev, sizeof(ev));
}

bool CreateUinput(const TouchDev& ref) {
  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    return false;

  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
  ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
  ioctl(fd, UI_SET_EVBIT, EV_ABS);
  ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
  ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
  ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
  ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
  ioctl(fd, UI_SET_ABSBIT, ABS_X);
  ioctl(fd, UI_SET_ABSBIT, ABS_Y);

  uinput_user_dev ui{};
  snprintf(ui.name, UINPUT_MAX_NAME_SIZE, "ImGuiOverlay Virtual Touch");
  ui.id.bustype = BUS_VIRTUAL;
  ui.id.vendor = 0xCAE5;
  ui.id.product = 0x0001;
  ui.id.version = 1;

  auto setAbs = [&](int code, float mn, float mx) {
    ui.absmin[code] = (int)mn;
    ui.absmax[code] = (int)mx;
    ui.absfuzz[code] = 0;
    ui.absflat[code] = 0;
  };
  setAbs(ABS_MT_SLOT, 0, 9);
  setAbs(ABS_MT_TRACKING_ID, 0, 65535);
  setAbs(ABS_MT_POSITION_X, ref.minX, ref.maxX);
  setAbs(ABS_MT_POSITION_Y, ref.minY, ref.maxY);
  setAbs(ABS_X, ref.minX, ref.maxX);
  setAbs(ABS_Y, ref.minY, ref.maxY);

  if (write(fd, &ui, sizeof(ui)) != (ssize_t)sizeof(ui)) {
    close(fd);
    return false;
  }
  if (ioctl(fd, UI_DEV_CREATE) != 0) {
    close(fd);
    return false;
  }
  g_uinput = fd;
  return true;
}

void InjectFinger(bool down, float sx, float sy) {
  if (g_uinput < 0 || g_devs.empty())
    return;
  const TouchDev& d = g_devs[0];
  int ax = 0, ay = 0;
  ScreenToAbs(sx, sy, d, ax, ay);

  Emit(g_uinput, EV_ABS, ABS_MT_SLOT, 0);
  if (down) {
    if (g_injTracking < 0)
      g_injTracking = 1;
    Emit(g_uinput, EV_ABS, ABS_MT_TRACKING_ID, g_injTracking);
    Emit(g_uinput, EV_ABS, ABS_MT_POSITION_X, ax);
    Emit(g_uinput, EV_ABS, ABS_MT_POSITION_Y, ay);
    Emit(g_uinput, EV_KEY, BTN_TOUCH, 1);
    Emit(g_uinput, EV_KEY, BTN_TOOL_FINGER, 1);
  } else {
    Emit(g_uinput, EV_ABS, ABS_MT_TRACKING_ID, -1);
    Emit(g_uinput, EV_KEY, BTN_TOUCH, 0);
    Emit(g_uinput, EV_KEY, BTN_TOOL_FINGER, 0);
    g_injTracking = -1;
  }
  Emit(g_uinput, EV_SYN, SYN_REPORT, 0);
}

}

bool Touch_Init(int w, int h, uint32_t orientation, bool readOnly) {
  Touch_Close();
  g_w = (float)w;
  g_h = (float)h;
  g_ori = orientation;

  DIR* dir = opendir("/dev/input");
  if (!dir)
    return false;
  dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    if (strncmp(ent->d_name, "event", 5) != 0)
      continue;
    char path[256];
    snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
    int fd = open(path, readOnly ? (O_RDONLY | O_NONBLOCK) : (O_RDWR | O_NONBLOCK));
    if (fd < 0 && !readOnly)
      fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
      continue;
    if (!IsTouchDevice(fd)) {
      close(fd);
      continue;
    }
    TouchDev d;
    d.fd = fd;
    ReadAbsRange(fd, ABS_MT_POSITION_X, d.minX, d.maxX);
    ReadAbsRange(fd, ABS_MT_POSITION_Y, d.minY, d.maxY);
    if (d.maxX <= d.minX)
      d.maxX = d.minX + 1;
    if (d.maxY <= d.minY)
      d.maxY = d.minY + 1;
    if (!readOnly) {
      if (ioctl(fd, EVIOCGRAB, 1) == 0)
        g_grabbed = true;
    }
    g_devs.push_back(d);
  }
  closedir(dir);

  if (g_devs.empty())
    return false;

  if (!readOnly && g_grabbed) {
    if (!CreateUinput(g_devs[0])) {
      fprintf(stderr, "overlay touch: uinput failed\n");
      for (auto& d : g_devs)
        ioctl(d.fd, EVIOCGRAB, 0);
      g_grabbed = false;
    } else {
      fprintf(stderr, "overlay touch: grab ok\n");
    }
  }
  return true;
}

void UpdateScreenData(int w, int h, uint32_t orientation) {
  g_w = (float)w;
  g_h = (float)h;
  g_ori = orientation;
}
void Touch_PumpAll() {
  ImGuiIO& io = ImGui::GetIO();

  for (auto& d : g_devs) {
    input_event ev{};
    while (read(d.fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
      if (ev.type == EV_ABS) {
        if (ev.code == ABS_MT_SLOT) {
          d.slot = ev.value;
          if (d.slot < 0)
            d.slot = 0;
          if (d.slot > 9)
            d.slot = 9;
        } else if (ev.code == ABS_MT_TRACKING_ID) {
          d.down[d.slot] = (ev.value >= 0);
        } else if (ev.code == ABS_MT_POSITION_X) {
          d.absX[d.slot] = (float)ev.value;
        } else if (ev.code == ABS_MT_POSITION_Y) {
          d.absY[d.slot] = (float)ev.value;
        }
      }
    }
  }

  bool anyDown = false;
  float curX = g_lastX, curY = g_lastY;
  for (auto& d : g_devs) {
    for (int i = 0; i < 10; i++) {
      if (!d.down[i])
        continue;
      float sx, sy;
      MapToScreen(d.absX[i], d.absY[i], d.minX, d.maxX, d.minY, d.maxY, sx, sy);
      curX = sx;
      curY = sy;
      anyDown = true;
      break;
    }
    if (anyDown)
      break;
  }

  bool fingerDown = anyDown;
  if (fingerDown) {
    g_upHold = 0.f;
  } else if (g_tracking) {
    g_upHold += io.DeltaTime;
    if (g_upHold < 0.12f)
      fingerDown = true;
    else
      g_upHold = 0.f;
  }

  if (fingerDown && !g_tracking)
    g_blockGesture = AetherUI::PointBlocksTouch(curX, curY);
  if (!fingerDown)
    g_blockGesture = false;

  io.AddMousePosEvent(curX, curY);
  if (fingerDown != g_tracking) {
    io.AddMouseButtonEvent(0, fingerDown);
    g_tracking = fingerDown;
  }
  g_lastX = curX;
  g_lastY = curY;

  if (g_grabbed && g_uinput >= 0) {
    if (fingerDown && !g_blockGesture)
      InjectFinger(true, curX, curY);
    else if (!fingerDown && g_injTracking >= 0)
      InjectFinger(false, curX, curY);
  }
}

void Touch_Close() {
  if (g_uinput >= 0) {
    ioctl(g_uinput, UI_DEV_DESTROY);
    close(g_uinput);
    g_uinput = -1;
  }
  for (auto& d : g_devs) {
    if (d.fd >= 0) {
      if (g_grabbed)
        ioctl(d.fd, EVIOCGRAB, 0);
      close(d.fd);
    }
  }
  g_devs.clear();
  g_grabbed = false;
  g_tracking = false;
  g_blockGesture = false;
  g_injTracking = -1;
}