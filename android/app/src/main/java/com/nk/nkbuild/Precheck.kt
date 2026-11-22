package com.nk.nkbuild

/**
 * 上传预检：上传前就告诉你哪些文件会把这次构建搞崩。
 *
 * 判定依据（都是实测过的）：
 *  - GitHub 的 git/blob 接口单文件上限 100 MB，contents 接口 1 MB（我们只拿它传 toml）
 *  - 超过 70 MB 的文件在手机上 base64 后约 93 MB，请求体过大，直接跳过
 *  - 编译单元文件名里带空格：CI 用 SRC_FILES="a.cpp b.cpp" 按空格切，会被切成两截 → 编译失败
 *  - 中文/特殊字符文件名：GitHub 能存，但 CI 那边 shell 处理容易出岔子
 *  - 图片/字体/文档等：编译用不上，只会把上传时间拉长
 */
data class Risk(
    val rel: String,
    val size: Long,
    val level: Level,
    val reason: String,
) {
    enum class Level { BLOCK, HIGH, NOTE }
}

object Precheck {

    private const val MB = 1024L * 1024L

    /** 编译绝对用不上的扩展名：传上去只是拖时间 */
    private val junkExt = setOf(
        "png", "jpg", "jpeg", "webp", "gif", "bmp", "ico", "svg",
        "ttf", "otf", "woff", "woff2", "ttc",
        "mp4", "mp3", "wav", "avi", "mkv", "mov",
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx",
        "psd", "ai", "blend", "fbx", "obj", "glb", "gltf",
        "7z", "rar", "tar", "gz", "xz", "iso", "bin",
        "pyc", "pdb", "ilk", "exp", "lib", "dylib",
    )

    val compileExt = setOf("c", "cc", "cpp", "cxx", "c++", "s", "asm")

    /** 人类可读的大小 */
    fun human(bytes: Long): String = when {
        bytes >= 1024L * 1024L -> String.format("%.1f MB", bytes / 1048576.0)
        bytes >= 1024L -> String.format("%.0f KB", bytes / 1024.0)
        else -> "$bytes B"
    }

    fun scan(files: List<SourceFile>): List<Risk> {
        val out = mutableListOf<Risk>()
        val total = files.sumOf { it.size }
        val units = files.count { it.rel.substringAfterLast('.', "").lowercase() in compileExt }

        for (f in files) {
            val name = f.rel.substringAfterLast('/')
            val ext = name.substringAfterLast('.', "").lowercase()
            val isUnit = ext in compileExt

            when {
                f.size > 70 * MB -> out.add(Risk(f.rel, f.size, Risk.Level.BLOCK,
                    "超过 70 MB：GitHub 不收，本次会被跳过"))

                isUnit && name.contains(' ') -> out.add(Risk(f.rel, f.size, Risk.Level.BLOCK,
                    "编译单元文件名带空格：CI 按空格切 SRC_FILES，会编译失败"))

                isUnit && name.any { it.code > 127 } -> out.add(Risk(f.rel, f.size, Risk.Level.BLOCK,
                    "编译单元文件名含中文/特殊字符：CI 侧 shell 会认不出来"))

                f.size > 45 * MB -> out.add(Risk(f.rel, f.size, Risk.Level.HIGH,
                    "超大文件：base64 后约 ${f.size * 4 / 3 / MB} MB，上传很可能超时"))

                f.size > 1 * MB && ext in junkExt -> out.add(Risk(f.rel, f.size, Risk.Level.HIGH,
                    "大体积非源码（$ext）：编译用不上，白等上传"))

                f.size > 1 * MB -> out.add(Risk(f.rel, f.size, Risk.Level.NOTE,
                    "超过 1 MB：走 blob 接口（能传，但慢）"))

                ext in junkExt -> out.add(Risk(f.rel, f.size, Risk.Level.NOTE,
                    "非源码（$ext）：会被一起上传"))
            }
        }

        if (units > 60) out.add(Risk("（整个工程）", total, Risk.Level.HIGH,
            "$units 个编译单元：一次提交文件太多，容易撞 GitHub 限流"))
        if (total > 120 * MB) out.add(Risk("（整个工程）", total, Risk.Level.HIGH,
            "总大小 ${total / MB} MB：CI 拉取 + 上传都很慢"))

        return out.sortedBy { it.level.ordinal }
    }

    fun blocking(risks: List<Risk>) = risks.filter { it.level == Risk.Level.BLOCK }
}
