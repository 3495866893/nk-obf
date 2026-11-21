package com.nk.nkbuild

/**
 * Ensia pass 目录（与桌面版 NKBuild 的 PASS_SPEC 保持一致）。
 * 手机端不再只给「轻量 / 标准」两档，而是 16 个 pass 逐个勾选，
 * 勾完在本地生成 TOML，上传到仓库 ci/ui-<工程名>.toml 后再 dispatch。
 */
data class PassDef(
    val key: String,
    val label: String,
    val desc: String,
    val defaultOn: Boolean,
    val selectable: Boolean,
    val group: Int,          // 0=核心安全 1=进阶实验 2=不可用（仅 AArch64 无意义/已知崩）
)

object PassCatalog {

    val SPEC: List<PassDef> = listOf(
        PassDef("string_encryption", "字符串加密", "字面量运行时解密，用完清零（anti_dump）", true, true, 0),
        PassDef("constant_encryption", "常量加密", "常量 globalize + XOR，feistel 永久关", true, true, 0),
        PassDef("substitution", "指令替换", "等价指令序列替换", true, true, 0),
        PassDef("bcf", "虚假控制流", "不透明谓词 + 假分支", true, true, 0),
        PassDef("split_blocks", "基本块分割", "每个函数切 3 段", true, true, 0),
        PassDef("indirect_branch", "间接跳转", "栈上跳转表 + 跳转目标加密", true, true, 0),
        PassDef("flattening", "控制流平坦化", "状态机 + 分发器", true, true, 0),
        PassDef("anti_debugging", "反调试（IR 级）", "TracerPid 探测 + 时间差，需顶部开关同开", true, true, 0),

        PassDef("mba", "MBA 混淆", "位运算恒等式替换，体积大、耗时长", false, true, 1),
        PassDef("chaos_state_machine", "混沌状态机", "未充分验证，可能崩", false, true, 1),
        PassDef("vector_obfuscation", "向量混淆", "体积暴涨、收益低", false, true, 1),
        PassDef("function_wrapper", "函数包装", "可能破坏结构（尤其虚表）", false, true, 1),
        PassDef("function_call_obfuscate", "函数调用混淆", "崩溃后极难排查", false, true, 1),

        PassDef("anti_hooking", "反 Hook", "完整性基线永远对不上，已知会崩 → 禁用", false, false, 2),
        PassDef("anti_class_dump", "类 dump 防护", "仅 Objective-C，AArch64 无意义", false, false, 2),
        PassDef("objc", "ObjC 混淆", "仅 Objective-C，AArch64 无意义", false, false, 2),
    )

    val KEYS: List<String> = SPEC.map { it.key }
    val DEFAULTS: Set<String> = SPEC.filter { it.defaultOn && it.selectable }.map { it.key }.toSet()
    val LIGHT: Set<String> =
        setOf("string_encryption", "substitution", "bcf", "split_blocks")
    val FULL: Set<String> = SPEC.filter { it.selectable }.map { it.key }.toSet()

    fun labelOf(key: String): String = SPEC.firstOrNull { it.key == key }?.label ?: key

    fun byGroup(g: Int): List<PassDef> = SPEC.filter { it.group == g }

    /** 与桌面版 build_toml() 完全同构的输出 */
    fun toml(selected: Set<String>): String {
        val sb = StringBuilder()
        sb.append("[global]\n")
        sb.append("preset = \"\"\n")
        sb.append("seed = \"0xDEADBEEF\"\n")
        sb.append("verbose = false\n\n")
        for (p in SPEC) {
            val on = p.selectable && selected.contains(p.key)
            sb.append("[passes.").append(p.key).append("]\n")
            sb.append("enabled = ").append(if (on) "true" else "false").append('\n')
            if (on) when (p.key) {
                "string_encryption" ->
                    sb.append("probability = 100\nanti_dump = true\nforce_content = [\".*\"]\n")
                "constant_encryption" ->
                    sb.append("iterations = 2\nshare_count = 6\nfeistel = false\n")
                        .append("globalize = true\nglobalize_prob = 100\n")
                        .append("substitute_xor = true\nsubstitute_xor_prob = 100\n")
                        .append("skip_value = [\"^0x0$\"]\n")
                "substitution" ->
                    sb.append("probability = 60\niterations = 1\n")
                "bcf" ->
                    sb.append("probability = 60\niterations = 1\ncomplexity = 6\n")
                        .append("entropy_chain = false\njunk_asm = false\n")
                        .append("nested = false\ncreate_func = false\n")
                "split_blocks" ->
                    sb.append("splits = 3\nstack_confusion = false\n")
                "indirect_branch" ->
                    sb.append("use_stack = true\nenc_jump_target = true\n")
                else ->
                    sb.append("probability = 100\n")
            }
            sb.append('\n')
        }
        return sb.toString()
    }
}
