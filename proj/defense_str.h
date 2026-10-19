/* ============================================================================
 * defense_str.h —— 字符串隐藏（编译期密文 + 运行期解码）
 * ----------------------------------------------------------------------------
 * 用法：
 *   1. 用 tools/gen_ds.py 生成密文数组（把明文换成密文，strings 就搜不到）：
 *        python tools/gen_ds.py "/data/local/tmp/路西法和平美化" --key 0x5A
 *   2. 把生成的数组粘进源码，用 DS_DEC(数组名, key) 取明文：
 *        printf("%s", DS_DEC(s_path_1, 0x5A));
 *
 * 注意：
 *   * 返回的指针指向线程局部静态缓冲（4 个轮转），只读、不要 free；
 *     同一条表达式里连写两次 DS_DEC 会互相覆盖，先取变量再用。
 *   * 只对「静态字符串」有意义；运行时拼出来的字符串本来就搜不到。
 * ========================================================================== */
#ifndef DEFENSE_STR_H
#define DEFENSE_STR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 把 blob（len 字节）用 key 异或解码，返回以 \0 结尾的线程局部缓冲 */
const char *defense_str_dec(const unsigned char *blob, size_t len, unsigned char key);

#ifdef __cplusplus
}
#endif

/* 数组形式的密文（unsigned char[]） */
#define DS_DEC(name, key) \
    defense_str_dec((const unsigned char *)(name), sizeof(name), (unsigned char)(key))

#endif /* DEFENSE_STR_H */
