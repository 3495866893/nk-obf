# projects/hello —— 最小示例工程

一个单文件 C 程序，用来验证「上传 → 触发 → 混淆编译 → 取回产物」这条链路。
它的代码刻意写得「有东西可混淆」：字符串常量、魔数运算、多分支 if/else、一个 5 项字符串表。

## 用 GUI 跑一次

| 字段 | 值 |
|---|---|
| 源目录 | `projects/hello` |
| 编译单元 | `main.c`（自动扫描出来的就是它） |
| 工程名 | `hello` |
| 产物名 | `hello` |
| 反调试 | 想验证看门狗就打开 |

点「开始编译」，等 1–2 分钟，产物会存到 `Download/NKBuild/hello`（手机）或你设的输出目录（桌面）。

## 用命令行跑一次

```bash
gh workflow run build-obf.yml \
  -f src_dir=projects/hello \
  -f src_files="main.c" \
  -f out_name=hello \
  -f anti_debug=false
```

然后 `gh run list --workflow=build-obf.yml --limit 1` 看状态，
产物在 Release `nk-builds` 的 `hello` 资产里（或对应 run 的 artifact）。

## 期望输出

```
hello from NK · built by the cloud pipeline
mix  = 0x........
stage  5/5: deliver
path  A|B|C
```

## 自己的工程怎么接

1. 目录里只放**真正要编译**的 `.c/.cpp`（被别的文件 `#include` 进去的 `.c` 不要单独列，会 duplicate symbol）。
2. 头文件、资源一起放进来，扫描时会一并上传（单个文件 ≤ 100 MB，建议 ≤ 70 MB）。
3. 文件名别带空格和中文（CI 按空格切分编译单元列表）。
4. 编译单元超过 60 个、或总量超过 120 MB，容易撞限流 —— GUI 的「预检」页会提前警告。
