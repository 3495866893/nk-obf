#pragma once
#include "xz.h"
#include "cJSON.h"
#include "cJSON.c"
#include "Encrypt.h"
using namespace std;

extern char *APPID;
extern const char *APPKEY;
extern const char *RC4KEY;
static time_t g_vip_time = 0;  // 全局到期时间戳，供主循环检测超时
extern const bool RC4_ON;
extern const char *LOCAL_VERSION;

static void clear_screen()
{
    printf("\033[0m");
    fflush(stdout);
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void wait_and_clear()
{
    fflush(stdout);
    sleep(1);
    clear_screen();
}

char *md5(const char *input)
{
    unsigned char md5buf[16];
    MD5_CTX md5c;
    MD5Init(&md5c);
    MD5Update(&md5c, (unsigned char *)input, strlen(input));
    MD5Final(&md5c, md5buf);

    char *result = new char[33];
    for (int i = 0; i < 16; ++i)
        sprintf(result + i * 2, "%02x", md5buf[i]);
    result[32] = '\0';
    return result;
}

 static bool read_text_file(const char *path, char *buf, size_t buf_size)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    memset(buf, 0, buf_size);
    bool ok = fscanf(fp, "%39s", buf) == 1 && strlen(buf) > 0;
    fclose(fp);
    return ok;
}

 static bool write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return false;
    fprintf(fp, "%s", text ? text : "");
    fclose(fp);
    return true;
}

 static void input_km(char *km, size_t km_size)
{
    printf("\033[33;1m请输入卡密：");
    fflush(stdout);
    memset(km, 0, km_size);
    scanf("%39s", km);
    if (strlen(km) == 0)
    {
        printf("\033[31;1m卡密不能为空\n");
        exit(1);
    }
}

 static void input_and_save_km(const char *km_luj, char *km, size_t km_size)
{
    input_km(km, km_size);
    if (!write_text_file(km_luj, km))
    {
        printf("\033[31;1m卡密保存失败：%s\n", km_luj);
        exit(1);
    }
}

 static void random_10_alnum(char *out)
{
    const char table[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    srand((unsigned int)(time(NULL) ^ getpid()));
    for (int i = 0; i < 10; ++i) out[i] = table[rand() % (sizeof(table) - 1)];
    out[10] = '\0';
}

 static void create_and_save_imei(const char *imei_luj, char *imei, size_t imei_size)
{
    char raw[11];
    random_10_alnum(raw);
    char *imei_md5 = md5(raw);
    memset(imei, 0, imei_size);
    strncpy(imei, imei_md5, imei_size - 1);
    delete[] imei_md5;
    if (!write_text_file(imei_luj, imei))
    {
        printf("\033[31;1m设备码保存失败：%s\n", imei_luj);
        exit(1);
    }
}


 static bool looks_like_json(const char *s)
{
    if (!s) return false;
    while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t') ++s;
    return *s == '{' || *s == '[';
}

 static bool looks_like_hex(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (n == 0 || (n % 2) != 0) return false;
    for (size_t i = 0; i < n; ++i)
    {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

 static void shell_quote(const char *in, char *out, size_t out_size)
{
    size_t j = 0;
    if (j < out_size) out[j++] = '\'';
    for (size_t i = 0; in && in[i] && j + 5 < out_size; ++i)
    {
        if (in[i] == '\'')
        {
            out[j++] = '\'';
            out[j++] = '\\';
            out[j++] = '\'';
            out[j++] = '\'';
        }
        else out[j++] = in[i];
    }
    if (j < out_size) out[j++] = '\'';
    if (j < out_size) out[j] = 0;
    else out[out_size - 1] = 0;
}

 static char *run_cmd_read(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    size_t cap = 4096, len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    int ch;
    while ((ch = fgetc(fp)) != EOF)
    {
        if (len + 1 >= cap)
        {
            cap *= 2;
            char *nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); pclose(fp); return NULL; }
            buf = nb;
        }
        buf[len++] = (char)ch;
    }
    pclose(fp);
    buf[len] = 0;
    if (len == 0) { free(buf); return NULL; }
    return buf;
}

 static char *httpget_compat(char *host, const char *url)
{
    char urlbuf[512];
    snprintf(urlbuf, sizeof(urlbuf), "%s", url);
    char *ret = httpget(host, urlbuf);
    if (ret && strlen(ret) > 0 && (looks_like_json(ret) || looks_like_hex(ret))) return ret;
    if (ret) free(ret);

    char full[768], qfull[900], cmd[1100];
    snprintf(full, sizeof(full), "https://%s/%s", host, url);
    shell_quote(full, qfull, sizeof(qfull));
    snprintf(cmd, sizeof(cmd), "curl -ksL --max-time 10 %s 2>/dev/null", qfull);
    ret = run_cmd_read(cmd);
    if (ret && strlen(ret) > 0) return ret;
    if (ret) free(ret);

    snprintf(full, sizeof(full), "http://%s/%s", host, url);
    shell_quote(full, qfull, sizeof(qfull));
    snprintf(cmd, sizeof(cmd), "curl -sL --max-time 10 %s 2>/dev/null", qfull);
    return run_cmd_read(cmd);
}

 static char *httppost_compat(char *host, const char *url, const char *post)
{
    char urlbuf[512];
    snprintf(urlbuf, sizeof(urlbuf), "%s", url);
    char *ret = httppost(host, urlbuf, (char*)post);
    if (ret && strlen(ret) > 0 && (looks_like_json(ret) || looks_like_hex(ret))) return ret;
    if (ret) free(ret);

    char full[768], qfull[900], qpost[1400], cmd[2600];
    snprintf(full, sizeof(full), "https://%s/%s", host, url);
    shell_quote(full, qfull, sizeof(qfull));
    shell_quote(post, qpost, sizeof(qpost));
    snprintf(cmd, sizeof(cmd), "curl -ksL --max-time 10 -X POST -H 'Content-Type: application/x-www-form-urlencoded' --data %s %s 2>/dev/null", qpost, qfull);
    ret = run_cmd_read(cmd);
    if (ret && strlen(ret) > 0) return ret;
    if (ret) free(ret);

    snprintf(full, sizeof(full), "http://%s/%s", host, url);
    shell_quote(full, qfull, sizeof(qfull));
    snprintf(cmd, sizeof(cmd), "curl -sL --max-time 10 -X POST -H 'Content-Type: application/x-www-form-urlencoded' --data %s %s 2>/dev/null", qpost, qfull);
    return run_cmd_read(cmd);
}

 static cJSON *parse_server_json(char *body, const char *rc4key, bool rc4_on)
{
    if (!body) return NULL;

    cJSON *json = NULL;
    if (rc4_on && looks_like_hex(body))
    {
        char *plain = Decrypt(body, rc4key);
        if (plain)
        {
            json = cJSON_Parse(plain);
            delete[] plain;
            if (json)
            {
                free(body);
                return json;
            }
        }
    }
    json = cJSON_Parse(body);
    free(body);
    return json;
}

static const char *json_str(cJSON *root, const char *key, const char *def = "")
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    return (item && item->valuestring) ? item->valuestring : def;
}

static long long json_int64(cJSON *root, const char *key, long long def = 0)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (!item) return def;
    if (cJSON_IsNumber(item)) return (long long)item->valuedouble;
    if (item->valuestring && item->valuestring[0]) return atoll(item->valuestring);
    return def;
}

static int json_int(cJSON *root, const char *key, int def = 0)
{
    return (int)json_int64(root, key, def);
}

static cJSON *json_find_recursive(cJSON *root, const char *key)
{
    if (!root || !key) return NULL;
    cJSON *direct = cJSON_GetObjectItem(root, key);
    if (direct) return direct;
    for (cJSON *child = root->child; child; child = child->next)
    {
        if (child->child)
        {
            cJSON *found = json_find_recursive(child, key);
            if (found) return found;
        }
    }
    return NULL;
}

static const char *json_str_any(cJSON *root, const char *key, const char *def = "")
{
    cJSON *item = json_find_recursive(root, key);
    if (!item) return def;
    if (item->valuestring) return item->valuestring;
    return def;
}

static long long json_int64_any(cJSON *root, const char *key, long long def = 0)
{
    cJSON *item = json_find_recursive(root, key);
    if (!item) return def;
    if (cJSON_IsNumber(item)) return (long long)item->valuedouble;
    if (item->valuestring && item->valuestring[0]) return atoll(item->valuestring);
    return def;
}

static bool json_any_to_text(cJSON *root, const char *key, char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    cJSON *item = json_find_recursive(root, key);
    if (!item) return false;
    if (cJSON_IsString(item) && item->valuestring)
    {
        snprintf(out, out_size, "%s", item->valuestring);
        return strlen(out) > 0;
    }
    if (cJSON_IsNumber(item))
    {
        long long v = (long long)item->valuedouble;
        snprintf(out, out_size, "%lld", v);
        return true;
    }
    if (cJSON_IsBool(item))
    {
        snprintf(out, out_size, "%s", cJSON_IsTrue(item) ? "true" : "false");
        return true;
    }
    return false;
}

static const char *find_use_count_text(cJSON *root, char *out, size_t out_size)
{
    const char *keys[] = {
        "api_total", "app_total", "app_use", "use_count", "usecount", "used_count",
        "total", "count", "num", "api_count", "start_count", "run_count", "app_count", NULL
    };
    for (int i = 0; keys[i]; ++i)
    {
        if (json_any_to_text(root, keys[i], out, out_size)) return out;
    }
    snprintf(out, out_size, "未返回");
    return out;
}

static long long first_valid_time_field(cJSON *root)
{
    const char *keys[] = {
        "vip", "endtime", "end_time", "expire", "expires", "expire_time",
        "viptime", "vip_time", "kami_time", "km_time", "dqtime", "enddate", "end_date", NULL
    };
    for (int i = 0; keys[i]; ++i)
    {
        long long v = json_int64_any(root, keys[i], 0);
        if (v > 0) return v;
    }
    return 0;
}

static time_t normalize_timestamp(long long ts)
{
    if (ts > 100000000000LL) ts /= 1000;
    return (time_t)ts;
}

static bool download_update_file(const char *url, const char *save_path)
{
    if (!url || !url[0] || !save_path || !save_path[0]) return false;
    char qurl[900], qsave[512], cmd[1800];
    shell_quote(url, qurl, sizeof(qurl));
    shell_quote(save_path, qsave, sizeof(qsave));
    snprintf(cmd, sizeof(cmd), "curl -ksL --max-time 30 --retry 2 %s -o %s 2>/dev/null", qurl, qsave);
    char *ret = run_cmd_read(cmd);
    if (ret) free(ret);
    FILE *fp = fopen(save_path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fclose(fp);
    return sz > 0;
}

static bool get_self_path(char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    memset(out, 0, out_size);
    ssize_t n = readlink("/proc/self/exe", out, out_size - 1);
    if (n <= 0) return false;
    out[n] = 0;
    return strlen(out) > 0;
}

static bool restart_self(const char *self_path)
{
    if (!self_path || !self_path[0]) return false;
    chmod(self_path, 0755);

    execl(self_path, self_path, (char*)NULL);
    char qself[700], cmd[1200];
    shell_quote(self_path, qself, sizeof(qself));
    snprintf(cmd, sizeof(cmd), "chmod 755 %s >/dev/null 2>&1; nohup %s >/dev/null 2>&1 &", qself, qself);
    return system(cmd) == 0;
}

static bool local_download_replace_self(const char *url)
{
    char self_path[512] = {0};
    if (!get_self_path(self_path, sizeof(self_path)))
    {
        printf("\033[31;1m获取当前文件路径失败\n");
        return false;
    }

    char tmp_path[640] = {0};
    char bak_path[640] = {0};
    snprintf(tmp_path, sizeof(tmp_path), "%s.update", self_path);
    snprintf(bak_path, sizeof(bak_path), "%s.bak", self_path);

    printf("\033[33;1m正在下载更新...\n");
    if (!download_update_file(url, tmp_path))
    {
        remove(tmp_path);
        printf("\033[31;1m本地下载失败\n");
        return false;
    }

    chmod(tmp_path, 0755);
    remove(bak_path);
    rename(self_path, bak_path);
    if (rename(tmp_path, self_path) != 0)
    {
        rename(bak_path, self_path);
        remove(tmp_path);
        printf("\033[31;1m替换当前文件失败，请检查存储权限\n");
        return false;
    }
    chmod(self_path, 0755);
    remove(bak_path);

    printf("\033[32;1m下载成功\n");
    printf("\033[32;1m更新完成\n");
    printf("\033[32;1m正在自动运行新版...\n");
    fflush(stdout);
    sleep(1);
    clear_screen();
    restart_self(self_path);
    printf("\033[31;1m自动运行失败，请手动重新打开\n");
    exit(0);
    return true;
}

static bool open_update_in_browser(const char *url)
{
    if (!url || !url[0]) return false;
    char qurl[900], cmd[1200];
    shell_quote(url, qurl, sizeof(qurl));
    snprintf(cmd, sizeof(cmd), "am start -a android.intent.action.VIEW -d %s >/dev/null 2>&1", qurl);
    int r = system(cmd);
    if (r != 0)
    {
        printf("\033[33;1m无法自动打开浏览器，请手动复制更新链接\n");
        return false;
    }
    printf("\033[32;1m已打开浏览器更新\n");
    return true;
}

static bool json_bool_any(cJSON *root, const char *key, bool def = false)
{
    cJSON *item = json_find_recursive(root, key);
    if (!item) return def;
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    if (cJSON_IsNumber(item)) return item->valuedouble != 0;
    if (item->valuestring && item->valuestring[0])
    {
        const char *v = item->valuestring;
        return strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "TRUE") == 0 ||
               strcmp(v, "yes") == 0 || strcmp(v, "YES") == 0 || strcmp(v, "y") == 0 || strcmp(v, "Y") == 0 ||
               strcmp(v, "on") == 0 || strcmp(v, "ON") == 0 || strcmp(v, "2") == 0 ||
               strcmp(v, "强制") == 0 || strcmp(v, "必须") == 0 || strcmp(v, "是") == 0;
    }
    return def;
}

static bool find_force_update(cJSON *root)
{
    const char *keys[] = {
        "app_update_must", "update_must", "must_update", "force_update", "is_force",
        "force", "must", "app_must", "mandatory", "app_update_force", "update_force", NULL
    };
    for (int i = 0; keys[i]; ++i)
        if (json_bool_any(root, keys[i], false)) return true;
    return false;
}

static int read_menu_choice()
{
    char line[64] = {0};
    while (1) {
        if (!fgets(line, sizeof(line), stdin)) {
            return -1;   // EOF
        }
        // 去除前导空白
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p == '\0') {
            // 空行，继续等待
            continue;
        }
        // 去掉末尾换行（可选）
        size_t len = strlen(p);
        if (len > 0 && p[len-1] == '\n') p[len-1] = '\0';
        return atoi(p);
    }
}
static void handle_update_method_menu(const char *update_url, bool force_update)
{
    printf("\033[36;1m请选择更新方式：\n1.浏览器更新\n2.本地更新\n输入：");
    int way = read_menu_choice();
    if (way == 1)
    {
        open_update_in_browser(update_url);
        exit(0);
    }
    if (way == 2)
    {
        local_download_replace_self(update_url);
        exit(0);
    }
    if (force_update) exit(0);
}

static void handle_update_menu(const char *update_url, bool force_update)
{
    if (!update_url || !update_url[0] || strcmp(update_url, "未提交URL") == 0)
    {
        printf("\033[31;1m更新链接未提交\n");
        if (force_update) exit(0);
        return;
    }

    if (force_update)
    {
        printf("\033[36;1m请选择：\n1.更新\n0.退出\n输入：");
        int op = read_menu_choice();
        if (op == 1)
        {
            handle_update_method_menu(update_url, true);
            exit(0);
        }
        exit(0);
    }

    printf("\033[36;1m请选择：\n1.更新\n0.暂不\n输入：");
    int op = read_menu_choice();
    if (op == 1)
    {
        handle_update_method_menu(update_url, false);
        return;
    }
}


struct StartupConfig
{
    bool ok;
    char use_count[64];
    char server_version[64];
    char update_show[512];
    char update_url[512];
    char notice[512];
    bool force_update;
};

static void copy_field(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    memset(dst, 0, dst_size);
    if (src) strncpy(dst, src, dst_size - 1);
}

static void trim_inplace(char *s)
{
    if (!s) return;
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = 0;
}

static bool is_empty_notice_text(const char *s)
{
    if (!s || !s[0]) return true;
    return strcmp(s, "null") == 0 || strcmp(s, "NULL") == 0 || strcmp(s, "未设置") == 0 || strcmp(s, "无") == 0;
}

static void init_startup_config(StartupConfig *cfg)
{
    memset(cfg, 0, sizeof(StartupConfig));
    cfg->ok = false;
    strcpy(cfg->use_count, "未返回");
}

static void fetch_notice_once(char *host, const char *APPID, const char *RC4KEY, bool rc4_on, StartupConfig *cfg)
{
    if (!cfg) return;
    char url[256];
    sprintf(url, "api.php?api=notice&app=%s", APPID);
    char *ret = httpget_compat(host, url);
    cJSON *json = parse_server_json(ret, RC4KEY, rc4_on);
    if (!json) return;

    int code = json_int(json, "code", 0);
    const char *msg = json_str(json, "msg", "");
    if (code != 0 && msg && msg[0])
    {
        cJSON_Delete(json);
        return;
    }

    const char *notice = json_str_any(json, "app_gg", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "notice", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "app_notice", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "gg", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "announcement", "");
    if (notice && notice[0])
    {
        copy_field(cfg->notice, sizeof(cfg->notice), notice);
        trim_inplace(cfg->notice);
        if (is_empty_notice_text(cfg->notice)) cfg->notice[0] = 0;
    }
    cJSON_Delete(json);
}

static void fetch_startup_config(char *host, const char *APPID, const char *RC4KEY, bool rc4_on, StartupConfig *cfg)
{
    init_startup_config(cfg);
    char url[256];
    sprintf(url, "api.php?api=ini&app=%s", APPID);
    char *ret = httpget_compat(host, url);
    cJSON *json = parse_server_json(ret, RC4KEY, rc4_on);
    if (!json) return;

    int code = json_int(json, "code", 0);
    const char *msg = json_str(json, "msg", "");
    if (code != 0 && strlen(msg) > 0)
    {
        cJSON_Delete(json);
        return;
    }

    char total_buf[64];
    copy_field(cfg->use_count, sizeof(cfg->use_count), find_use_count_text(json, total_buf, sizeof(total_buf)));
    copy_field(cfg->server_version, sizeof(cfg->server_version), json_str_any(json, "version", ""));
    copy_field(cfg->update_show, sizeof(cfg->update_show), json_str_any(json, "app_update_show", ""));
    copy_field(cfg->update_url, sizeof(cfg->update_url), json_str_any(json, "app_update_url", ""));

    const char *notice = json_str_any(json, "app_gg", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "notice", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "app_notice", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "gg", "");
    if (!notice || !notice[0]) notice = json_str_any(json, "announcement", "");
    copy_field(cfg->notice, sizeof(cfg->notice), notice);
    trim_inplace(cfg->notice);
    if (is_empty_notice_text(cfg->notice)) cfg->notice[0] = 0;

    cfg->force_update = find_force_update(json);
    cfg->ok = true;
    cJSON_Delete(json);
}

static void print_startup_header(const char *APPID, const StartupConfig *cfg)
{
    printf("\033[35;1m欢迎使用XL云验证[%s]\n", APPID);
    printf("\033[32;1m卡密购买地址:\n");
    printf("\033[32;1m本脚本使用：%s次\n", cfg && cfg->use_count[0] ? cfg->use_count : "未返回");
}

static void print_notice_if_any(const StartupConfig *cfg)
{
    if (cfg && cfg->notice[0])
    {
        printf("\033[36;1m公告：%s\n", cfg->notice);
        fflush(stdout);
    }
}

static void check_update_cached(const StartupConfig *cfg, const char *local_version)
{
    if (!cfg || !cfg->server_version[0])
    {
        return;
    }

    if (strcmp(cfg->server_version, local_version) != 0)
    {
        printf("\033[35;1m发现新版本：%s\n", cfg->server_version);
        if (cfg->update_show[0]) printf("更新内容：%s\n", cfg->update_show);
        handle_update_menu(cfg->update_url, cfg->force_update);
        return;
    }
}

static void show_cached_top(const char *APPID, const StartupConfig *cfg, const char *local_version)
{
    print_startup_header(APPID, cfg);
    check_update_cached(cfg, local_version);
    print_notice_if_any(cfg);
}

 static bool login_once(char *host, const char *APPID, const char *APPKEY, const char *RC4KEY, bool rc4_on,
                       const char *km_luj, char *km, size_t km_size, const char *imei)
{
    time_t now = time(NULL);
    int t = (int)now;

    char sign_src[512];
    sprintf(sign_src, "kami=%s&markcode=%s&t=%d&%s", km, imei, t, APPKEY);
    char *sign = md5(sign_src);

    char random_src[256];
    sprintf(random_src, "%d独九月", t);
    char *random_rc4 = Encrypt(random_src, RC4KEY);
    char random_md5_src[512];
    sprintf(random_md5_src, "%s%s%s", random_rc4 ? random_rc4 : "", APPKEY, imei);
    char *Random = md5(random_md5_src);
    if (random_rc4) delete[] random_rc4;

    char post_plain[512];
    sprintf(post_plain, "kami=%s&markcode=%s&t=%d&sign=%s", km, imei, t, sign);
    delete[] sign;

    char post_body[1024];
    if (rc4_on)
    {
        char *enc = Encrypt(post_plain, RC4KEY);
        if (!enc)
        {
            delete[] Random;
            printf("\033[31;1m请求数据加密失败\n");
            return false;
        }
        sprintf(post_body, "data=%s&value=%s", enc, Random);
        delete[] enc;
    }
    else
    {
        sprintf(post_body, "%s&value=%s", post_plain, Random);
    }

    char url[256];
    sprintf(url, "api.php?api=kmlogon&app=%s", APPID);
    char *ret = httppost_compat(host, url, post_body);
    cJSON *json = parse_server_json(ret, RC4KEY, rc4_on);
    if (!json)
    {
        delete[] Random;
        printf("\033[31;1m服务器返回数据解密/解析失败\n");
        return false;
    }

    int code = json_int(json, "code", 0);
    long long server_time_raw = json_int64(json, "time", 0);
    int server_time = (int)normalize_timestamp(server_time_raw);
    const char *msg = json_str(json, "msg", "验证失败");
    const char *check = json_str(json, "check", "");
    long long vip_raw = first_valid_time_field(json);

    if (code != 200)
    {
        printf("\033[31;1m%s\n", msg);
        write_text_file(km_luj, "");
        cJSON_Delete(json);
        delete[] Random;
        memset(km, 0, km_size);
        return false;
    }

    if (server_time - (int)time(NULL) > 10 || server_time - (int)time(NULL) < -10)
    {
        printf("\033[31;1m数据超时\n");
        cJSON_Delete(json);
        delete[] Random;
        return false;
    }

    char check_src[512];
    sprintf(check_src, "%d%s%s", server_time, APPKEY, Random);
    char *local_check = md5(check_src);
    bool ok = (strcmp(local_check, check) == 0);
    delete[] local_check;
    delete[] Random;

    if (!ok)
    {
        printf("\033[31;1m请不要修改数据，校验失败\n");
        cJSON_Delete(json);
        memset(km, 0, km_size);
        return false;
    }

    time_t vip_time = normalize_timestamp(vip_raw);
    g_vip_time = vip_time;
    char vipbuf[128] = {0};
    if (vip_time <= 0)
    {
        printf("\033[32;1m登录成功！\n");
        printf("到期时间：永久或未返回\n");
    }
    else
    {
        strftime(vipbuf, sizeof(vipbuf), "%Y年%m月%d日 %H时%M分%S秒", localtime(&vip_time));
        printf("\033[32;1m登录成功！\n");
        printf("到期时间：%s\n", vipbuf);
    }
    write_text_file(km_luj, km);
    cJSON_Delete(json);
    return true;
}

static void unbind_km(char *host, const char *APPID, const char *APPKEY, const char *RC4KEY, bool rc4_on,
                      const char *km, const char *imei)
{
    int t = (int)time(NULL);
    char sign_src[512];
    sprintf(sign_src, "kami=%s&markcode=%s&t=%d&%s", km, imei, t, APPKEY);
    char *sign = md5(sign_src);
    char plain[512];
    sprintf(plain, "kami=%s&markcode=%s&t=%d&sign=%s", km, imei, t, sign);
    delete[] sign;

    char post_body[1024];
    if (rc4_on)
    {
        char *enc = Encrypt(plain, RC4KEY);
        if (!enc)
        {
            printf("\033[31;1m解绑数据加密失败\n");
            return;
        }
        sprintf(post_body, "data=%s", enc);
        delete[] enc;
    }
    else sprintf(post_body, "%s", plain);

    char url[256];
    sprintf(url, "api.php?api=kmunmachine&app=%s", APPID);
    char *ret = httppost_compat(host, url, post_body);
    cJSON *json = parse_server_json(ret, RC4KEY, rc4_on);
    if (!json)
    {
        printf("\033[31;1m解绑接口返回解析失败\n");
        return;
    }
    printf("\033[35;1m%s\n", json_str(json, "msg", "解绑完成"));
    cJSON_Delete(json);
}

int wyz()
{


    clear_screen();
    char *host = (char *)"yz.dujiuyue.cn";
    char km_luj[128];
    sprintf(km_luj, "/data/local/tmp/.arlo_log/.key");
    char imei_luj[128];
    sprintf(imei_luj, "/storage/emulated/0/Android/%s.imei", APPID);

    StartupConfig cfg;
    fetch_startup_config(host, APPID, RC4KEY, RC4_ON, &cfg);
    fetch_notice_once(host, APPID, RC4KEY, RC4_ON, &cfg);
    show_cached_top(APPID, &cfg, LOCAL_VERSION);

    char km[40] = {0};
    bool has_saved_km = read_text_file(km_luj, km, sizeof(km));
    char imei[40];
    if (!read_text_file(imei_luj, imei, sizeof(imei)))
        create_and_save_imei(imei_luj, imei, sizeof(imei));

    if (has_saved_km && strlen(km) > 0)
    {
        if (login_once(host, APPID, APPKEY, RC4KEY, RC4_ON, km_luj, km, sizeof(km), imei))
            return 0;
        write_text_file(km_luj, "");
        memset(km, 0, sizeof(km));
        wait_and_clear();
    }

    return 1;
}
