# XL云验证 CPP 对接示例 —— 加密算法改为 AES-256-GCM（标准）改造说明

## 1. 本次改动清单

| 文件 | 改动 |
|---|---|
| `jni/res/Encrypt.h` | **整体重写**：RC4 → AES-256-GCM。纯 C++ 实现（AES-256 分组 + GHASH + SHA-256），不依赖 OpenSSL，保持原函数名 `Encrypt` / `Decrypt` / `ByteToHex` / `HexToByte`，新增 `EncryptDeterministic` |
| `jni/main.cpp` | 填入 `APPID=11054`、`APPKEY=y4sH2w11imYIx1K4`、`AESKEY=mN4bV7cX1zL9kJ2hG5fD8sA0qW3enknb`（32 字节 = 256bit）|
| `jni/res/pz.h` | `RC4KEY` → `AESKEY`、`RC4_ON` → `AES_ON`、`rc4_on` → `aes_on`；`random_rc4` 改用确定性加密；`post_body[1024]` → `[2048]`、`random_src[256]` → `[320]`（GCM 输出比 RC4 长 28 字节，需扩容）|
| `aesgcm_server_ref.py` | **服务端参考实现**（Python + cryptography，放工程根目录），必须与客户端配套上线 |

## 2. 协议约定（客户端 / 服务端必须完全一致）

```
密钥派生   len(AESKEY)==32 → key = AESKEY 原始 32 字节（当前就是这样）
           其它长度        → key = SHA256(AESKEY)
算法       AES-256-GCM，AAD = 空，TAG = 16 字节
线上格式   payload = hex( IV(12B) ‖ CIPHERTEXT(nB) ‖ TAG(16B) )   # 小写 hex
IV         普通加密      : 12 字节随机（/dev/urandom）
           确定性加密    : IV = SHA256(明文)[0..11]
```

请求体：

```
POST api.php?api=kmlogon&app=11054
data=<payload>&value=<random_md5>

POST api.php?api=kmunmachine&app=11054
data=<payload>
```

响应体：把 JSON 文本用同一算法加密后整串返回即可；客户端会先判断是否 hex 并解密，失败再按明文 JSON 解析（兼容不加密返回）。

### 为什么 `random_rc4` 要用确定性加密

原逻辑 `random_rc4 = RC4(t+"独九月")` 是**确定性**的，服务端可用同一个 `t` 复算出该值，再算 `md5(random_rc4 + APPKEY + markcode)` 校验 `value`。
AES-GCM 若用随机 IV 每次结果不同，服务端就无法复算。所以这里用 `EncryptDeterministic()`：`IV = SHA256(明文)[0:12]`，输出仍然唯一确定，服务端按同样规则即可复现。

## 3. 签名 / 校验逻辑未变

```
sign  = md5("kami=%s&markcode=%s&t=%d&" + APPKEY)      # 客户端，明文参与
value = md5(random_rc4 + APPKEY + markcode)            # 客户端
check = md5(server_time + APPKEY + Random)             # 服务端下发，客户端校验
```

## 4. 编译

```bash
cd "…/XL云验证cpp对接示例（CV优化）"
sh 一键打包.sh          # 需要 AIDE 自带 NDK，路径见脚本内 NDK= 变量
# 产物: libs/arm64-v8a/main
```

## 5. 已验证项（本机 g++ / x86_64 实测）

1. `g++ -std=c++14 -w` 编译整份工程（main.cpp + pz.h + cJSON + 新 Encrypt.h）**零报错**；
2. **60 组随机向量**（IV 长度 8/12/13/16，明文 0~200 字节，AAD 0/5/16/33 字节）与 Python `cryptography.hazmat…AESGCM` 输出**逐字节一致**；
3. 双向互操作：C++ 加密 → Python 解密、Python 加密 → C++ 解密，4 组用例（含中文 `独九月`、200 字节长文本）全部通过；
4. 确定性加密两侧输出**完全相同**；服务端 `verify_random()` 复算通过；
5. 篡改任意一位 hex，`Decrypt()` 返回 `NULL`（tag 校验失败），不会返回脏数据。

复测方法：`aesgcm_server_ref.py` 直接 `python3 aesgcm_server_ref.py` 自检；向量比对与双向互操作脚本已跑完删除（结论见上，需要我再生成一份随时说）。

## 6. 注意事项

- **服务端必须同步改造**，否则 `api.php` 返回的仍是 RC4 或明文，`data` 参数也解不出 —— 客户端会打印「服务器返回数据解密/解析失败」。参考实现见 `aesgcm_server_ref.py`。
- `AESKEY` 必须正好 32 字节才等于直接使用；写成其它长度会自动走 `SHA256`，此时服务端也必须走 `SHA256`。
- 明文越长 GCM 输出越长 28 字节（12 IV + 16 TAG），已顺手扩容 `post_body`；若后续卡密/字段再加长，注意 `sprintf` 缓冲区。
- `Encrypt()` 与 `Decrypt()` 都用 `new[]` 分配，调用方必须 `delete[]`（pz.h 里原本就是这样配对的，未改动）。
## 7. 线上实测对接结论（yz.dujiuyue.cn / app=11054，2026-11-06 抓包）

官方文档：https://yz.dujiuyue.cn/user/transport_crypto_mod.php （传输加密协议 v1.0）

### 7.1 服务端到底用的什么

| 项目 | 实测结果 |
|---|---|
| 响应编码 | **Base64( 12B IV ‖ AES-256-GCM 密文 ‖ 16B tag )**，不是 hex |
| 响应密钥 | 后台「AES密钥」= `mN4bV7cX1zL9kJ2hG5fD8sA0qW3enknb`（32B 原样使用）|
| 加密方式 | 后台「数据加密类型」= **AES-256-GCM(标准)** |
| check 字段 | `MD5(time + APPKEY + value)`，无 value 时 value="" —— 实测 True |
| 未绑定 appid | 直接返回明文 JSON，code=100「请绑定应用ID」|
| 提交方式 | POST `application/x-www-form-urlencoded`，`data=` + Base64 密文（+ / = 必须 URL 编码）|

**客户端「服务器返回数据解密/解析失败」的唯一原因**：原代码只认 hex，服务端吐的是 Base64，`looks_like_hex()` 判false → 根本没尝试解密。

### 7.2 抓包实测表

```
ini        GET 200 OK   {"code":200,...}          ← C++ 客户端已实测解密成功
notice     GET 200 OK   {"app_gg":"⚠️ 本软件仅供…"}
heartbeat  GET 200 OK   {"code":107,"msg":"数据为空"}
kmlogon    GET 200 OK   {"code":107,"msg":"数据为空"}
heartbeat  POST data=<加密空串>  200 OK  code107
heartbeat  POST data=<加密非空>  500 Internal Server Error
kmlogon    POST data=<加密非空>  500 Internal Server Error（QS / JSON / 明文 都一样）
```

### 7.3 请求侧 500 的判断

- 空串能解密成功并返回 107 → **密钥与算法都对**（tag 校验通过才会走到业务层）。
- 任何非空明文都 500 → 故障点在服务端 `kmlogon` / `heartbeat` 的业务处理，
  与客户端无关（`ini` / `notice` 同一把密钥、同一套 POST 完全正常）。
- 官方文档写明「解密或标签校验失败统一返回 **107**」，现在返回的是 500，属于未捕获异常。

处理建议（按优先级）：
1. 后台「传输安全」里核对该应用/该接口的加密类型与 AES 密钥（是否接口开了「独立配置」用了别的算法）；
2. 用后台自带的加解密工具，拿同一明文对比密文是否互通；
3. 临时把该应用切成「不加密」或「RC4加密-2」，确认 `kmlogon` 接口本身是否可通；
4. 以上都正常仍 500 → 平台侧 bug，把上面的实测表发给平台方即可。

复现脚本：`python3 server_probe.py --req`

---

## 8. 最终交付状态（2026-11-06 实测收工）

### 8.1 后台配置要求

| 后台项 | 必须值 | 说明 |
|---|---|---|
| 数据加密类型 | **不加密** | AES-256-GCM 的请求解密在平台侧有故障，详见 8.2 |
| 数据签名 | 开启 | 不开则服务端不校验 sign，但开了更安全 |
| 签名放 DATA 里 | 关闭 | 实测 sign 必须是独立参数，放进 data 会返回 104 签名为空 |
| 调试模式 | **关闭** | 开启后接口只输出签名原串，不走业务流程 |
| 时间差校验 | 20 秒 | 客户端 t 必须取服务器时间，本地时钟偏差不能超过窗口 |

### 8.2 客户端对应开关

`jni/main.cpp` 第 28 行：

```cpp
extern const bool AES_ON = false;   // false = 明文直传（当前唯一可用方式）
                                    // true  = AES-256-GCM（等平台修复请求解密后再开）
```

### 8.3 唯一可用的调用协议（已实测 code:200 + check PASS）

```
POST api.php?api=kmlogon&app=11054
Content-Type: application/x-www-form-urlencoded

kami=2iPMrvyhqH&user=&markcode=<MD5(CPU型号+制造商+序列号)>&t=<秒>&sign=<签名>&value=<纯数字>
```

```
sign  = MD5("kami=" + 卡密 + "&user=" + 账号 + "&markcode=" + 设备码 + "&t=" + 时间戳 + "&" + APPKEY)
check = MD5(服务端 time + APPKEY + value)      // 客户端本地复算，与服务端返回比对
```

三条铁律：

1. `sign` 必须是**独立参数**，塞进 data 里 -> `104 签名为空`
2. 签名原文里的 `&user=` **即使账号为空也必须存在**，少了 -> `106 签名有误`
3. `value` **不参与签名**，只参与 check

实测输出：

```
{"code":200,"msg":{"kami":"2iPMrvyhqH","vip":"4102243200"},"time":...,"check":"..."}
check 校验 PASS，时间差 0 秒
```

### 8.4 AES-256-GCM 为啥不能用（平台侧故障，客户端无法绕过）

同一服务端、同一把密钥、同一接口：

| 方向 | 结果 |
|---|---|
| 服务端响应（ini / notice） -> 客户端解密 | **正常**，`Base64(12B IV + 密文 + 16B tag)` 可完整解开 |
| 客户端请求 -> 服务端解密 | **任何非空密文 HTTP 500，响应体 0 字节** |

压力测试：`data` 里包装的明文只有 19 字节（`kami=2iPMrvyhqH`）也 500；`data` 为空则返回 107。
已排除：密钥、AAD（空 / markcode / appid / appkey / 接口名）、Base64 / hex / URL-safe / 不编码、GET / POST、
参数格式（含官方易语言 SDK 逐字构造）、时间戳、签名、调试模式。

**对照实验（决定性）**：把后台切成「不加密」，同一套参数 + 同一个签名公式 -> `code:200` 验证成功。

结论：加密/签名以外的业务链完全正常，故障点在平台的 AES-256-GCM **请求解密**实现
（官方文档称解密或 tag 失败应返回 107，实际抛未捕获异常返回 500）。

### 8.5 一键回归

```bash
python3 login_probe.py            # 完整登录链路（取时间 -> 签名 -> POST -> check 校验）
python3 server_probe.py           # 响应侧解密自检（ini / notice / heartbeat）
python3 aesgcm_server_ref.py      # AES-256-GCM 参考实现自检（PHP 后端移植用）
```

### 8.6 平台修好 AES 后怎么切回

1. `jni/main.cpp` -> `AES_ON = true`
2. 后台「数据加密类型」-> `AES-256-GCM(标准)`
3. 重新编译：`./一键打包.sh` 或 `ndk-build`

客户端 `pz.h` 的 `aes_on` 分支会自动改走 `data=<Base64 密文>`，签名逻辑不变。
**切回前先用 `python3 login_probe.py` 确认服务端已恢复正常（否则会 500）。**


---

## 9. 最终方案：SM4-CBC（动态IV）+ HMAC-SM3（当前生效，已端到端跑通）

### 9.1 为什么换成 SM4

| 方案 | 服务端实测 |
|---|---|
| AES-256-GCM（标准） | 响应能解，**请求侧任何非空密文整回 500**（平台解密实现故障）|
| 不加密 | 可用，但明文裸传，不安全 |
| **SM4-CBC(动态IV+SM3签名)** | **请求/响应双向正常，登录 code:200** |

### 9.2 协议格式（实测反推 + 官方文档一致）

```
待加密内容 = 8B 随机数 + 4B 秒级时间戳(大端) + 业务参数串
密文   = Base64( 16B IV + SM4-CBC(待加密内容) + 32B HMAC-SM3 )
校验值 = HMAC-SM3( SM4密钥, IV ‖ 密文 )     // 标准 HMAC，block=64
```

### 9.3 客户端实现（jni/res/gm.h，自实现，无第三方库）

| 组件 | 说明 | 验证 |
|---|---|---|
| SM4 | 32 轮 + S 盒 + 密钥扩展，解密用逆序轮密钥 | 标准向量 `681edf34d206965e86b3e94f536e4246` **一致** |
| SM3 | 压缩函数 + P0/P1 + 填充 | `SM3("abc") = 66c7f0f4...8f4ba8e0` **一致** |
| HMAC-SM3 | key 不足 64 字节补零，ipad/opad | 与 Python gmssl **逐字节一致** |
| GmWrap / GmUnwrap | 打包 / 解包（自动去 12 字节头） | C++ 加密 → Python 解密一致；C++ 加密 → 服务端 → **code:200** |

### 9.4 代码改动点

| 位置 | 改动 |
|---|---|
| `pz.h:5-7` | `#include "gm.h"` + `GM_KEY16 = "tYHhrtawMQIdR5Cx"`（后台 SM4 密钥）|
| `pz.h:242` | 响应解密 `Decrypt()` → `GmUnwrap()` |
| `pz.h:734` | 请求加密 `Encrypt()` → `GmWrap(plain+"&value="+Random)`，body 只提交 `data=` |
| `pz.h:840` | 解绑接口同步改为 `GmWrap` |
| `main.cpp:28` | `AES_ON = true`（此时表示传输加密开启，算法已是 SM4）|

### 9.5 后台必须这样配

| 后台项 | 值 |
|---|---|
| 应用安全配置 | 开 |
| 加密类型 | SM4-CBC加密(动态IV+SM3签名) |
| SM4密钥 | `tYHhrtawMQIdR5Cx`（16 字节，与 GM_KEY16 一致）|
| 数据签名 | 开 |
| 签名放DATA里 | 开（sign 与 value 都放进密文）|
| 时间差校验 | 20 秒（t 必须取服务器时间）|
| 调试模式 | 关 |

### 9.6 实测记录（2026-11-06）

```
C++ GmWrap 生成 data（300 字符）→ POST
HTTP 200  tag=OK
{"code":200,"msg":{"kami":"2iPMrvyhqH","vip":"4102243200"},"time":...,"check":"40eb6fa6..."}
```

一键回归：`python3 sm4_login.py`（需 `pip3 install --break-system-packages gmssl`）

