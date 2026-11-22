# ci/vendor —— 兜底源码

有些项目会把 `.c` 直接 `#include` 进头文件（典型：`res/pz.h` 里 `#include "cJSON.c"`），
但上传时只带了 `.h`，于是编译直接死在 `fatal error: 'cJSON.c' file not found`。

这里放一份公开库的副本当兜底：`ci/build-lucifer.sh` 在编译前若发现某个
`#include "xxx.c"` 的目标缺失，会从本目录复制过去（只在缺失时补，不覆盖你的文件）。

| 文件 | 来源 | 许可 |
|---|---|---|
| `cJSON.c` | cJSON（Dave Gamble） | MIT |
