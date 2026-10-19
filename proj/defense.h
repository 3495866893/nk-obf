/* ============================================================================
 * defense.h —— 运行时自保护（反调试 / 反注入 / 反篡改）
 * ----------------------------------------------------------------------------
 * 纯 C，可被 C++ 直接 include（内部已加 extern "C"）。
 * 默认由 defense.c 里的 __attribute__((constructor)) 自动启动，不需要改 main。
 *
 * 强度等级（编译宏 DEFENSE_LEVEL，默认 3）：
 *   0 = 全关
 *   1 = 基础：PR_SET_DUMPABLE=0、RLIMIT_CORE=0、PTRACE_TRACEME
 *   2 = 标准：+ TracerPid 轮询（含各线程）、特征库扫描（frida/xposed/...）
 *   3 = 拉满：+ 代码段 CRC 运行时校验、libc 序言 inline-hook 检测、时序异常检测
 *
 * 另外：
 *   DEFENSE_PROCESS_NAME   伪装进程名（默认 "media.codec"），设为 "" 关闭
 *   DEFENSE_INTERVAL_MS    巡检间隔，默认 2000
 *   DEFENSE_LOG            1 = 命中时打印原因（调试用），默认 0（静默退出）
 * ========================================================================== */
#ifndef DEFENSE_H
#define DEFENSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 返回 0 表示初始化成功；非 0 表示部分能力不可用（不影响程序继续运行） */
int defense_init(void);

/* 手动单次巡检（一般不用调；watchdog 线程会周期调用） */
int defense_check_once(void);

/* 打印当前生效的防御层（调试用） */
void defense_status(void);

/* 主动退出（静默） */
void defense_panic(void);

#ifdef __cplusplus
}
#endif

#endif /* DEFENSE_H */
