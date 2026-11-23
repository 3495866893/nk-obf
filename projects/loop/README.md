# loop（运行时回归样例）

活 12 秒、3 个线程、反复 malloc/free，用来验证运行时看门狗在「长时间运行 + 多线程 + 频繁堆操作」下不误杀。

```bash
gh workflow run build-obf.yml \
  -f src_dir=projects/loop -f src_files="main.c" \
  -f cfg_only=ci/ui-jni.toml -f out_name=loop -f anti_debug=true
```

期望：本地直接跑（不接调试器）12 秒内 12 行 `tick`，最后 `loop done sink=…`，退出码 0；
被 `projects/tracer` 这类父进程 `PTRACE_CONT` 牵引时立刻 `exit 97`。
