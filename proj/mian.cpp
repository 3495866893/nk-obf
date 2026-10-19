#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>
#include "读写.h"

long int UE4 = 0;

#define CYAN   "\033[96m"
#define YELLOW "\033[93m"
#define GREEN  "\033[1;92m"
#define RED    "\033[1;91m"
#define WHITE  "\033[97m"
#define RESET  "\033[0m"

#define BASE_PATH     "/data/local/tmp/路西法和平美化"
#define PERSON_DIR    BASE_PATH "/人物美化"
#define WEAPON_DIR    BASE_PATH "/经典手持枪械配置"
#define CLASSIC_PATH  BASE_PATH "/经典全局遍历和平经典配置.json"
#define METRO_PATH    BASE_PATH "/地铁全局遍历/和平地铁配置.json"
#define VEHICLE_PATH  BASE_PATH "/载具美化/载具.json"
#define ICON_PATH     "/data/local/tmp/配置.h"

#define MAX_VEHICLE_MAPPINGS 1000
#define MAX_LINE_LEN 256

struct VehicleMapping { int from; int to; };
VehicleMapping vehicle_mappings[MAX_VEHICLE_MAPPINGS];
int vehicle_mapping_count = 0;

int load_vehicle_mappings(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;
    char line[MAX_LINE_LEN];
    vehicle_mapping_count = 0;
    while (fgets(line, sizeof(line), fp) && vehicle_mapping_count < MAX_VEHICLE_MAPPINGS) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (p[0] == '/' && p[1] == '/') continue;
        int from, to;
        if (sscanf(p, "%d-%d", &from, &to) == 2 ||
            sscanf(p, "%d %d", &from, &to) == 2) {
            vehicle_mappings[vehicle_mapping_count].from = from;
            vehicle_mappings[vehicle_mapping_count].to = to;
            vehicle_mapping_count++;
        }
    }
    fclose(fp);
    return vehicle_mapping_count;
}

int find_vehicle_target(int original_value) {
    for (int i = 0; i < vehicle_mapping_count; i++) {
        if (vehicle_mappings[i].from == original_value)
            return vehicle_mappings[i].to;
    }
    return 0;
}

struct ItemOffset {
    const char *name;
    long int offset;
    int type;
};

ItemOffset items[] = {
    {"脸部",     0x3C,  0}, {"面罩",     0xE0,  1}, {"面罩",     0xE4,  0},
    {"面罩",     0xF8,  1}, {"眼罩",     0x2A0, 1}, {"眼罩",     0x2A4, 0},
    {"眼罩",     0x2B8, 1}, {"头饰",     0xA8,  1}, {"头饰",     0xAC,  0},
    {"头饰",     0xC0,  1}, {"头发",     0x74,  0}, {"衣服",     0x11C, 0},
    {"裤子",     0x154, 0}, {"鞋子",     0x18C, 0}, {"头盔",     0x1FC, 0},
    {"背包",     0x1C4, 0}, {"护甲",     0x234, 0}, {"降落伞",   0x26C, 0},
    {"飞行器",   0x310, 1}, {"飞行器",   0x314, 0}, {"飞行器",   0x328, 1},
    {"背包挂件", 0x348, 1}, {"背包挂件", 0x34C, 0}, {"背包挂件", 0x360, 1}
};

struct SkinSet {
    std::string name;
    std::map<std::string, int> values;
    bool 判断头盔背包 = false;
    int 一级头 = 0, 二级头 = 0, 三级头 = 0;
    int 一级包 = 0, 二级包 = 0, 三级包 = 0;
};

struct WeaponSkin {
    int weaponId;
    int partId;
    std::string partStr;
    int skinId;
};

struct Slot { long offset; long offset2; };
std::vector<Slot> weaponSlots = {
    {0x18C, 0}, {0xE4,  0}, {0x4,   0}, {0x3C,  0}, {0x74,  0},
    {0xAC,  0}, {0x118, 0x11C}, {0x150, 0x154}, {0x1F8, 0x1FC}
};

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool is_all_digits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

static bool is_config_file(const std::string &name) {
    if (name.size() < 3) return false;
    std::string lower = name;
    for (char &c : lower) c = tolower(c);
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".json") return true;
    if (lower.size() >= 2 && lower.substr(lower.size() - 2) == ".h")  return true;
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".txt") return true;
    return false;
}

SkinSet parse_person_file(const char *path, const std::string &name) {
    SkinSet set;
    set.name = name;

    std::ifstream myfile(path);
    if (!myfile.is_open()) return set;

    std::string line;
    while (getline(myfile, line)) {
        size_t cmt = line.find('#');
        if (cmt != std::string::npos) line = line.substr(0, cmt);
        line = trim(line);
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string valStr = trim(line.substr(eq + 1));
        if (key.empty() || valStr.empty()) continue;

        int value = atoi(valStr.c_str());

        if (key == "头盔背包判断" || key == "是否开启判断头盔背包") {
            set.判断头盔背包 = (value != 0);
        }
        else if (key == "头盔1") set.一级头 = value;
        else if (key == "头盔2") set.二级头 = value;
        else if (key == "头盔3") set.三级头 = value;
        else if (key == "背包1") set.一级包 = value;
        else if (key == "背包2") set.二级包 = value;
        else if (key == "背包3") set.三级包 = value;
        else {
            std::string mapped = key;
            if (key == "脸型") mapped = "脸部";
            else if (key == "帽子") mapped = "头饰";
            else if (key == "胸甲") mapped = "护甲";
            set.values[mapped] = value;
        }
    }
    return set;
}

std::vector<SkinSet> load_person_dir(const char *dir_path) {
    std::vector<SkinSet> result;
    DIR *dir = opendir(dir_path);
    if (!dir) {
        printf("无法打开目录：%s\n", dir_path);
        return result;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string filename = entry->d_name;
        if (!is_config_file(filename)) continue;

        std::string fullpath = std::string(dir_path) + "/" + filename;
        std::string name = filename;
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) name = name.substr(0, dot);

        SkinSet set = parse_person_file(fullpath.c_str(), name);
        if (!set.values.empty() || set.判断头盔背包) {
            result.push_back(set);
        }
    }
    closedir(dir);
    std::sort(result.begin(), result.end(),
              [](const SkinSet &a, const SkinSet &b){ return a.name < b.name; });
    return result;
}

bool load_weapon_config(const char *filename, std::vector<WeaponSkin> &weaponSkins) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return true;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        std::string str_line = trim(line);
        if (str_line.empty()) continue;
        if (str_line[0] == '#' || (str_line.length() >= 2 && str_line[0] == '/' && str_line[1] == '/'))
            continue;
        if (str_line.find("手持=") == 0) continue;

        size_t dash1 = str_line.find('-');
        if (dash1 == std::string::npos) continue;
        size_t dash2 = str_line.find('-', dash1 + 1);
        if (dash2 == std::string::npos) continue;

        std::string weaponIdStr = trim(str_line.substr(0, dash1));
        std::string midPart = trim(str_line.substr(dash1 + 1, dash2 - dash1 - 1));
        std::string skinIdStr = trim(str_line.substr(dash2 + 1));

        size_t comment = skinIdStr.find("//");
        if (comment != std::string::npos) skinIdStr = skinIdStr.substr(0, comment);
        skinIdStr = trim(skinIdStr);
        if (weaponIdStr.empty() || midPart.empty() || skinIdStr.empty()) continue;

        WeaponSkin ws;
        ws.weaponId = atoi(weaponIdStr.c_str());
        ws.skinId = atoi(skinIdStr.c_str());
        if (is_all_digits(midPart)) { ws.partId = atoi(midPart.c_str()); ws.partStr.clear(); }
        else {
            ws.partId = -1; ws.partStr = midPart;
            if (ws.partStr == "击打特效") ws.partStr = "打击";
        }
        weaponSkins.push_back(ws);
    }
    fclose(fp);
    return true;
}

bool load_weapon_dir(const char *dir_path, std::vector<WeaponSkin> &weaponSkins) {
    weaponSkins.clear();
    DIR *dir = opendir(dir_path);
    if (!dir) return false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string filename = entry->d_name;
        if (!is_config_file(filename)) continue;
        std::string fullpath = std::string(dir_path) + "/" + filename;
        load_weapon_config(fullpath.c_str(), weaponSkins);
    }
    closedir(dir);
    return true;
}

static inline void 开启衣服call(pid_t pid, uint64_t libbase) {
    uint64_t 自身地址 = getPtr64(getPtr64(getPtr64(getPtr64(getPtr64(libbase + 0x1771CC38) + 0xc0) + 0x88) + 0x30) + 0x3540);
    if (!自身地址) return;
    if (ptraceAttach(pid) == -1) return;
    衣服call(pid, libbase, 自身地址, 1);
    ptraceDetach(pid);
}

static inline void 开启枪械call(uint64_t libbase) {
    uint64_t 自身 = getPtr64(getPtr64(getPtr64(libbase + 0x176ECA90) + 0x30) + 0x5D8);
    if (!自身) return;
    uint64_t 枪械实例 = getPtr64(自身 + 0x1170);
    if (!枪械实例) return;
    if (ptraceAttach(pid) == -1) return;
    枪械call(pid, libbase, 枪械实例, 1);
    ptraceDetach(pid);
}

void apply_skin(long int libbase, const SkinSet &set) {
    long int 自身地址 = getPtr64(getPtr64(getPtr64(getPtr64(getPtr64(libbase + 0x1771CC38) + 0xc0) + 0x88) + 0x30) + 0x3540);
    if (!自身地址) { printf("请确认在游戏对局内！\n"); return; }
    long int 衣服指针 = getPtr64(自身地址 + 0x5198);
    if (!衣服指针) { printf("获取衣服指针失败！\n"); return; }

    int 当前头盔ID = 0, 当前背包ID = 0;
    if (set.判断头盔背包) {
        当前头盔ID = getDword(衣服指针 + 0x1FC);
        当前背包ID = getDword(衣服指针 + 0x1C4);
    }

    for (int i = 0; i < (int)(sizeof(items)/sizeof(items[0])); i++) {
        const char *name = items[i].name;
        long int offset = items[i].offset;
        int type = items[i].type;

        if (strcmp(name, "头盔") == 0 && set.判断头盔背包) {
            int 目标头盔 = set.values.count("头盔") ? set.values.at("头盔") : 0;
            if (当前头盔ID == set.一级头) 目标头盔 = set.一级头;
            else if (当前头盔ID == set.二级头) 目标头盔 = set.二级头;
            else if (当前头盔ID == set.三级头) 目标头盔 = set.三级头;
            vm_writev(衣服指针 + offset, &目标头盔, 4);
        }
        else if (strcmp(name, "背包") == 0 && set.判断头盔背包) {
            int 目标背包 = set.values.count("背包") ? set.values.at("背包") : 0;
            if (当前背包ID == set.一级包) 目标背包 = set.一级包;
            else if (当前背包ID == set.二级包) 目标背包 = set.二级包;
            else if (当前背包ID == set.三级包) 目标背包 = set.三级包;
            vm_writev(衣服指针 + offset, &目标背包, 4);
        }
        else {
            auto it = set.values.find(name);
            if (it != set.values.end()) {
                int val = (type == 1) ? 1 : it->second;
                vm_writev(衣服指针 + offset, &val, 4);
            }
        }
    }
    开启衣服call(pid, libbase);
}

void apply_weapon_skins_force(long int libbase, const std::vector<WeaponSkin> &weaponSkins) {
    long int 自身 = getPtr64(getPtr64(getPtr64(getPtr64(getPtr64(libbase + 0x1771CC38) + 0xc0) + 0x88) + 0x30) + 0x3540);
    if (!自身) return;
    uint64_t 枪械实例 = getPtr64(自身 + 0x1170);
    if (!枪械实例) return;
    long int 手持 = getPtr64(枪械实例 + 0xFC0);
    if (!手持) return;

    int currentWeaponId = getDword(手持 + 0x18C);
    std::vector<int> slotValues;
    for (const auto &slot : weaponSlots) slotValues.push_back(getDword(手持 + slot.offset));

    std::vector<WeaponSkin> matchedConfigs;
    bool directMatched = false;

    for (const auto &ws : weaponSkins) {
        if (currentWeaponId != 0 && ws.weaponId == currentWeaponId) {
            directMatched = true; matchedConfigs.push_back(ws);
        }
    }
    if (!directMatched) {
        for (const auto &ws : weaponSkins) {
            if (ws.skinId == currentWeaponId && ws.weaponId != 0) {
                for (const auto &cfg : weaponSkins)
                    if (cfg.weaponId == ws.weaponId) matchedConfigs.push_back(cfg);
                break;
            }
        }
    }

    int appliedCount = 0;
    for (const auto &ws : matchedConfigs) {
        if (ws.partStr == "打击") {
            int cur1 = getDword(手持 + 0x150);
            int cur2 = getDword(手持 + 0x154);
            if (cur1 != ws.skinId) { vm_writev(手持 + 0x150, (void*)&ws.skinId, 4); appliedCount++; }
            if (cur2 != ws.skinId) { vm_writev(手持 + 0x154, (void*)&ws.skinId, 4); }
            continue;
        }
        if (ws.partStr == "枪械") {
            int cur = getDword(手持 + 0x18C);
            if (cur != ws.skinId) { vm_writev(手持 + 0x18C, (void*)&ws.skinId, 4); appliedCount++; }
            continue;
        }
        if (ws.partStr == "挂件") {
            int cur1 = getDword(手持 + 0x1F8);
            int cur2 = getDword(手持 + 0x1FC);
            if (cur1 != ws.skinId) { vm_writev(手持 + 0x1F8, (void*)&ws.skinId, 4); appliedCount++; }
            if (cur2 != ws.skinId) { vm_writev(手持 + 0x1FC, (void*)&ws.skinId, 4); }
            continue;
        }
        if (ws.partStr == "检视") {
            int cur1 = getDword(手持 + 0x118);
            int cur2 = getDword(手持 + 0x11C);
            if (cur1 != ws.skinId) { vm_writev(手持 + 0x118, (void*)&ws.skinId, 4); appliedCount++; }
            if (cur2 != ws.skinId) { vm_writev(手持 + 0x11C, (void*)&ws.skinId, 4); }
            continue;
        }
        if (ws.partId >= 0) {
            for (size_t i = 0; i < weaponSlots.size(); ++i) {
                int curVal = slotValues[i];
                if (curVal == ws.partId || curVal == ws.skinId) {
                    if (curVal != ws.skinId) {
                        vm_writev(手持 + weaponSlots[i].offset, (void*)&ws.skinId, 4);
                        if (weaponSlots[i].offset2 != 0)
                            vm_writev(手持 + weaponSlots[i].offset2, (void*)&ws.skinId, 4);
                        appliedCount++;
                    }
                }
            }
        }
    }
    if (appliedCount > 0) 开启枪械call(libbase);
}

std::atomic<bool> g_weaponEnabled{false};
std::thread g_weaponThread;
std::vector<WeaponSkin> g_weaponSkins;

void weapon_guard_loop() {
    while (g_weaponEnabled) {
        int cur_pid = getPID("com.tencent.tmgp.pubgmhd");
        if (cur_pid <= 0) { usleep(500000); continue; }
        long int cur_libbase = get_module_base(cur_pid, "libUE4.so");
        if (cur_libbase == 0) { usleep(500000); continue; }
        pid = cur_pid;
        apply_weapon_skins_force(cur_libbase, g_weaponSkins);
        usleep(100000);
    }
}

std::atomic<bool> g_vehicleEnabled{false};
std::thread g_vehicleThread;

void vehicle_guard_loop() {
    while (g_vehicleEnabled) {
        int cur_pid = getPID("com.tencent.tmgp.pubgmhd");
        if (cur_pid <= 0) { usleep(500000); continue; }
        long int cur_libbase = get_module_base(cur_pid, "libUE4.so");
        if (cur_libbase == 0) { usleep(500000); continue; }
        pid = cur_pid;

        uint64_t 自身 = getPtr64(getPtr64(getPtr64(cur_libbase + 0x176ECA90) + 0x30) + 0x5D8);
        if (!自身) { usleep(200000); continue; }

        uint64_t 上车 = getPtr64(自身 + 0x1310);
        if (!上车) { usleep(200000); continue; }

        uint64_t 载具组件 = getPtr64(上车 + 0x1ED8);
        if (!载具组件) { usleep(200000); continue; }

        uint64_t 载具数据 = getPtr64(载具组件 + 0x11E8);
        if (!载具数据) { usleep(200000); continue; }

        int 原始数值 = getDword(载具数据 + 0x4);
        if (原始数值 == 0) { usleep(200000); continue; }

        int 目标数值 = find_vehicle_target(原始数值);
        if (目标数值 == 0) { usleep(200000); continue; }

        writedword(载具数据 + 0x4, 目标数值);
        if (getDword(载具数据 + 0x1C) != 2)
            writedword(载具数据 + 0x1C, 2);

        if (ptraceAttach(pid) == 0) {
            载具call(pid, cur_libbase, 载具组件);
            ptraceDetach(pid);
        }

        usleep(200000);
    }
}

static void 执行金身() {
    uint64_t 控制器 = getPtr64(getPtr64(UE4 + 0x176ECA90) + 0x30);
    if (!控制器) { printf("\n  " YELLOW "未获取到控制器" RESET "\n"); return; }
    uint64_t 自身 = getPtr64(控制器 + 0x5D8);
    if (!自身) { printf("\n  " YELLOW "未获取到自身" RESET "\n"); return; }
    if (ptraceAttach(pid) == -1) return;
    金身(pid, UE4, 自身, 1);
    ptraceDetach(pid);
}

static void 执行下雨() {
    uint64_t 控制器 = getPtr64(getPtr64(UE4 + 0x176ECA90) + 0x30);
    if (!控制器) { printf("\n  " YELLOW "未获取到控制器" RESET "\n"); return; }
    if (ptraceAttach(pid) == -1) return;
    下雨(pid, UE4, 控制器, 1);
    ptraceDetach(pid);
}

static void 执行下雪() {
    uint64_t 控制器 = getPtr64(getPtr64(UE4 + 0x176ECA90) + 0x30);
    if (!控制器) { printf("\n  " YELLOW "未获取到控制器" RESET "\n"); return; }
    if (ptraceAttach(pid) == -1) return;
    下雪(pid, UE4, 控制器, 1);
    ptraceDetach(pid);
}

static void 执行暴雪() {
    uint64_t 控制器 = getPtr64(getPtr64(UE4 + 0x176ECA90) + 0x30);
    if (!控制器) { printf("\n  " YELLOW "未获取到控制器" RESET "\n"); return; }
    if (ptraceAttach(pid) == -1) return;
    暴雪(pid, UE4, 控制器, 1);
    ptraceDetach(pid);
}

void Firearms(long int UE4base, int 原ID, int 修改ID, int tableType = 0, bool iconDisplay = false) {
    int 表标志 = (tableType == 1) ? 9801001 : 1000;
    long int Firearm = getPtr64(getPtr64(UE4base + 0x16F9E168) + 0x50);
    if (!Firearm) return;

    for (int i = 0; i < 150; ++i) {
        long int temp = getPtr64(Firearm + i * 8);
        if (temp == 0) continue;
        long int Array = getPtr64(temp + 0x30);
        if (Array < 0x1000) continue;

        if (getDword(getPtr64(Array + 0x8)) == 表标志) {
            int SJ[3] = {0};
            if (iconDisplay) {
                for (int j = 0x8; j <= 0x100D30; j += 0x18) {
                    long int targetTable = getPtr64(Array + j);
                    if (!targetTable) continue;
                    if (getDword(targetTable) == 修改ID) {
                        char checkName[256] = {0};
                        getUTF8(checkName, getPtr64(targetTable + 0x8));
                        if (strlen(checkName) > 0) {
                            SJ[0] = getDword(targetTable + 0x48);
                            SJ[1] = getDword(targetTable + 0x50);
                            SJ[2] = getDword(targetTable + 0x80);
                            break;
                        }
                    }
                }
            }

            for (int id = 0x8; id <= 0x100D30; id += 0x18) {
                long int table = getPtr64(Array + id);
                if (table == 0) continue;
                if (getDword(table) == 原ID) {
                    char name[256] = {0};
                    getUTF8(name, getPtr64(table + 0x8));
                    if (strlen(name) > 0) {
                        writedword(table + 0x20, 修改ID);
                        if (iconDisplay) {
                            writedword(table + 0x48, SJ[0]);
                            writedword(table + 0x50, SJ[1]);
                            writedword(table + 0x80, SJ[2]);
                        }
                        return;
                    }
                }
            }
        }
    }
}

static void 执行配置文件(const char *path, int tableType, bool iconDisplay) {
    std::ifstream myfile(path);
    if (!myfile.is_open()) return;
    std::string line;
    while (getline(myfile, line)) {
        if (line.empty()) continue;
        size_t cmt = line.find("//");
        if (cmt != std::string::npos) line = line.substr(0, cmt);
        cmt = line.find('#');
        if (cmt != std::string::npos) line = line.substr(0, cmt);
        line = trim(line);
        if (line.empty()) continue;

        size_t sep = line.find(' ');
        if (sep == std::string::npos) sep = line.find('-');
        if (sep == std::string::npos) continue;

        std::string a = trim(line.substr(0, sep));
        std::string b = trim(line.substr(sep + 1));
        if (a.empty() || b.empty()) continue;
        if (!is_all_digits(a) || !is_all_digits(b)) continue;

        int data1 = atoi(a.c_str());
        int data2 = atoi(b.c_str());
        Firearms(UE4, data1, data2, tableType, iconDisplay);
    }
    myfile.close();
}

static void 导出物品ID(int 判断值, const char *模式名称) {
    if (!UE4) return;

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_c);
    char timeStr[100];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d-%H-%M", now_tm);

    char 输出文件路径[256];
    sprintf(输出文件路径, "/storage/emulated/0/和平精英-%s-%s输出.h", 模式名称, timeStr);

    FILE* fp = fopen(输出文件路径, "w");
    if (!fp) return;

    long int Firearm = getPtr64(getPtr64(UE4 + 0x16F9E168) + 0x50);
    long int Array = 0;
    bool 找到 = false;

    for (int i = 0; i < 150; ++i) {
        Array = getPtr64(getPtr64(Firearm + i * 8) + 0x30);
        if (Array == 0 || Array < 0x1000) continue;
        if (getDword(getPtr64(Array + 0x8)) == 判断值) {
            找到 = true;
            writedword(Array, 0);
            break;
        }
    }
    if (!找到) { fclose(fp); return; }

    for (int id = 0x8; id <= 0x201000; id += 0x18) {
        long int table = getPtr64(Array + id);
        if (table == 0 || table < 0x1000) continue;

        int art  = getDword(table);
        int art1 = getDword(table + 0x20);
        char Name[256] = {0};
        getUTF8Long(Name, getPtr64(table + 0x8));

        if (art != 0 || art1 != 0 || strlen(Name) > 0) {
            fprintf(fp, "〔{%s} -> {%d} -> {%d} -> {0x%X}〕\n", Name, art, art1, id);
        } else {
            break;
        }
    }
    fclose(fp);
}

static void print_banner() {
    printf("\033[2J\033[H");
    printf("\n");
    printf(CYAN "╔══════════════════════════════════════════════════╗\n");
    printf(CYAN "          路西法和平美化 适配1.38\n");
    printf(CYAN "╚══════════════════════════════════════════════════╝\n" RESET);
    printf(CYAN "开源者:沉默\n" RESET);
    //留个名字吧 别删了😥
}

int main() {

    pid = getPID("com.tencent.tmgp.pubgmhd");
    if (pid <= 0) {
        printf("路西法提醒你；你未打开游戏！\n");
        sleep(3);
        return -1;
    }
    UE4 = get_module_base(pid, "libUE4.so");
    if (UE4 == 0) {
        printf("获取模块基址失败！\n");
        sleep(3);
        return -1;
    }
    long int libbase = UE4;

    std::vector<SkinSet> skins = load_person_dir(PERSON_DIR);
    if (skins.empty())
        printf("人物美化目录为空或不存在：%s\n", PERSON_DIR);

    load_weapon_dir(WEAPON_DIR, g_weaponSkins);

    bool 局内美化 = false;
    bool 枪械美化 = false;
    bool 载具美化 = false;

    usleep(800000);

    char input[64];
    std::string choice;

    while (1) {
        print_banner();
        printf(CYAN "┌────────────────────────────────────┐\n");
        printf(WHITE "⇨    输入1 . 美化\n");
        printf(WHITE "⇨    输入2 . 函数功能\n");
        printf(WHITE "⇨    输入 y . 退出\n");
        printf(CYAN "└────────────────────────────────────┘\n" RESET);
        printf(WHITE "请输入选择 : " RESET);

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\r\n")] = 0;
        choice = trim(input);
        if (choice == "y" || choice == "Y") { printf("\n程序退出\n"); break; }
        int c = atoi(input);

        if (c == 1) {
            bool 返回主菜单 = false;
            while (!返回主菜单) {
                print_banner();
                printf(YELLOW "🐉 美化区\n" RESET);
                printf(CYAN "┌────────────────────────────────────┐\n");
                printf(WHITE "⇨    输入1 . 图标美化\n");
                printf(WHITE "⇨    输入2 . 遍历美化经典\n");
                printf(WHITE "⇨    输入3 . 遍历美化地铁\n");

                printf(WHITE "⇨    输入4 . 经典局内美化");
                if (局内美化) printf(GREEN " [已开启]" RESET "\n");
                else          printf(RED   " [未开启]" RESET "\n");

                printf(WHITE "⇨    输入5 . 枪械美化");
                if (枪械美化) printf(GREEN " [已开启]" RESET "\n");
                else          printf(RED   " [未开启]" RESET "\n");

                printf(WHITE "⇨    输入6 . 输出经典物品ID\n");
                printf(WHITE "⇨    输入7 . 输出地铁物品ID\n");

                printf(WHITE "⇨    输入8 . 载具美化");
                if (载具美化) printf(GREEN " [已开启]" RESET "\n");
                else          printf(RED   " [未开启]" RESET "\n");

                printf(WHITE "⇨    输入 y . 返回菜单\n");
                printf(CYAN "└────────────────────────────────────┘\n" RESET);
                printf(WHITE "请输入选择 : " RESET);

                if (!fgets(input, sizeof(input), stdin)) goto exit_program;
                input[strcspn(input, "\r\n")] = 0;
                choice = trim(input);
                if (choice == "y" || choice == "Y") { 返回主菜单 = true; break; }
                int sub = atoi(input);

                switch (sub) {
                    case 1: {
                        printf("\n  " CYAN "开始图标美化..." RESET "\n");
                        执行配置文件(ICON_PATH, 0, true);
                        printf("\n  " GREEN "图标美化完成！" RESET "\n");
                        usleep(1800000);
                        break;
                    }
                    case 2: {
                        printf("\n  " CYAN "开始遍历美化经典..." RESET "\n");
                        执行配置文件(CLASSIC_PATH, 0, false);
                        printf("\n  " GREEN "遍历美化经典完成！" RESET "\n");
                        usleep(1800000);
                        break;
                    }
                    case 3: {
                        printf("\n  " CYAN "开始遍历美化地铁..." RESET "\n");
                        执行配置文件(METRO_PATH, 1, false);
                        printf("\n  " GREEN "遍历美化地铁完成！" RESET "\n");
                        usleep(1800000);
                        break;
                    }
                    case 4: {
                        if (skins.empty()) {
                            printf("\n  " YELLOW "人物美化目录为空，请检查：" PERSON_DIR RESET "\n");
                            usleep(1500000);
                            break;
                        }
                        while (1) {
                            printf("\033[2J\033[H");
                            printf(YELLOW "===== 选择套装 =====\n\n" RESET);
                            for (size_t i = 0; i < skins.size(); i++)
                                printf(WHITE "  %d. %s\n" RESET, (int)(i+1), skins[i].name.c_str());
                            printf(WHITE "  0. 返回\n" RESET);
                            printf(WHITE "  请输入选择 (0-%d) : " RESET, (int)skins.size());
                            if (!fgets(input, sizeof(input), stdin)) goto exit_program;
                            int s = atoi(input);
                            if (s == 0) break;
                            else if (s >= 1 && s <= (int)skins.size()) {
                                int idx = s - 1;
                                printf("\033[2J\033[H");
                                printf(CYAN "正在更换 %s ...\n" RESET, skins[idx].name.c_str());
                                apply_skin(libbase, skins[idx]);
                                局内美化 = true;
                                printf(GREEN "换装完成！\n" RESET);
                                sleep(1);
                                break;
                            } else {
                                printf("\n  " YELLOW "无效选项，请重新选择" RESET "\n");
                                usleep(600000);
                            }
                        }
                        break;
                    }
                    case 5:
                        if (!枪械美化) {
                            if (g_weaponSkins.empty()) {
                                printf("\n  " YELLOW "未加载到枪械配置，请检查：" WEAPON_DIR RESET "\n");
                                usleep(1500000);
                            } else {
                                枪械美化 = true;
                                g_weaponEnabled = true;
                                if (g_weaponThread.joinable()) g_weaponThread.join();
                                g_weaponThread = std::thread(weapon_guard_loop);
                                printf("\n  " GREEN "枪械美化已开启" RESET "\n");
                                usleep(1000000);
                            }
                        } else {
                            枪械美化 = false;
                            g_weaponEnabled = false;
                            if (g_weaponThread.joinable()) g_weaponThread.join();
                            printf("\n  " RED "枪械美化已关闭" RESET "\n");
                            usleep(1000000);
                        }
                        break;
                    case 6:
                        导出物品ID(1000, "经典");
                        break;
                    case 7:
                        导出物品ID(9801001, "地铁");
                        break;
                    case 8:
                        if (!载具美化) {
                            if (load_vehicle_mappings(VEHICLE_PATH) <= 0) {
                                printf("\n  " YELLOW "载具配置为空或丢失：" VEHICLE_PATH RESET "\n");
                                usleep(1500000);
                            } else {
                                载具美化 = true;
                                g_vehicleEnabled = true;
                                if (g_vehicleThread.joinable()) g_vehicleThread.join();
                                g_vehicleThread = std::thread(vehicle_guard_loop);
                                printf("\n  " GREEN "载具美化已开启（%d 条映射）" RESET "\n", vehicle_mapping_count);
                                usleep(1000000);
                            }
                        } else {
                            载具美化 = false;
                            g_vehicleEnabled = false;
                            if (g_vehicleThread.joinable()) g_vehicleThread.join();
                            printf("\n  " RED "载具美化已关闭" RESET "\n");
                            usleep(1000000);
                        }
                        break;
                    default:
                        printf("\n  " YELLOW "无效选项，请重新选择" RESET "\n");
                        usleep(600000);
                        break;
                }
            }
        }

        else if (c == 2) {
            bool 返回主菜单 = false;
            while (!返回主菜单) {
                print_banner();
                printf(YELLOW "⚙ 函数功能区\n" RESET);
                printf(CYAN "┌────────────────────────────────────┐\n");
                printf(WHITE "⇨    输入1 . 金身\n");
                printf(WHITE "⇨    输入2 . 下雨\n");
                printf(WHITE "⇨    输入3 . 下雪\n");
                printf(WHITE "⇨    输入4 . 暴雪\n");
                printf(WHITE "⇨    输入 y . 返回菜单\n");
                printf(CYAN "└────────────────────────────────────┘\n" RESET);
                printf(WHITE "请输入选择 : " RESET);
                if (!fgets(input, sizeof(input), stdin)) goto exit_program;
                input[strcspn(input, "\r\n")] = 0;
                choice = trim(input);
                if (choice == "y" || choice == "Y") { 返回主菜单 = true; break; }
                int sub = atoi(input);
                switch (sub) {
                    case 1:
                        printf("\n  " CYAN "执行金身..." RESET "\n");
                        执行金身();
                        printf("\n  " GREEN "金身完成！" RESET "\n");
                        usleep(1200000);
                        break;
                    case 2:
                        printf("\n  " CYAN "执行下雨..." RESET "\n");
                        执行下雨();
                        printf("\n  " GREEN "下雨完成！" RESET "\n");
                        usleep(1200000);
                        break;
                    case 3:
                        printf("\n  " CYAN "执行下雪..." RESET "\n");
                        执行下雪();
                        printf("\n  " GREEN "下雪完成！" RESET "\n");
                        usleep(1200000);
                        break;
                    case 4:
                        printf("\n  " CYAN "执行暴雪..." RESET "\n");
                        执行暴雪();
                        printf("\n  " GREEN "暴雪完成！" RESET "\n");
                        usleep(1200000);
                        break;
                    default:
                        printf("\n  " YELLOW "无效选项，请重新选择" RESET "\n");
                        usleep(600000);
                        break;
                }
            }
        }

        else {
            printf("\n  " YELLOW "无效选项，请重新选择" RESET "\n");
            usleep(600000);
        }
    }

exit_program:
    g_weaponEnabled = false;
    g_vehicleEnabled = false;
    if (g_weaponThread.joinable()) g_weaponThread.join();
    if (g_vehicleThread.joinable()) g_vehicleThread.join();
    return 0;
}