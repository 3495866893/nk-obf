package com.nk.nkbuild

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.core.content.FileProvider
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.zip.ZipInputStream

data class SourceFile(val rel: String, val uri: Uri, val size: Long)

enum class Phase { IDLE, RUNNING, DONE, ERROR }

data class UiState(
    val phase: Phase = Phase.IDLE,
    val statusText: String = "空闲",
    val elapsed: Long = 0L,
    val eta: Long = 0L,
    val progress: Float = 0f,
    val logs: List<String> = emptyList(),
    val files: List<SourceFile> = emptyList(),
    val project: String = "myproj",
    val outName: String = "lucifer",
    val passes: Set<String> = PassCatalog.DEFAULTS,
    val adb: Boolean = true,
    val artifactPath: String? = null,
    val savedUri: String? = null,
    val savedName: String? = null,
    val runUrl: String? = null,
    val error: String? = null,
) {
    /** 还差多少秒（粗略；跑完或没开始就是 0） */
    val remain: Long get() = if (phase == Phase.RUNNING && eta > elapsed) eta - elapsed else 0L
}

class BuildViewModel : ViewModel() {

    private val _s = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _s.asStateFlow()

    private var job: Job? = null
    private var startedAt = 0L

    private val compileExt = setOf("c", "cc", "cpp", "cxx", "c++")

    fun log(msg: String) {
        val t = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
            .format(java.util.Date())
        _s.value = _s.value.copy(logs = (_s.value.logs + "[$t] $msg").takeLast(400))
    }

    fun setProject(v: String) { _s.value = _s.value.copy(project = v) }
    fun setOutName(v: String) { _s.value = _s.value.copy(outName = v) }
    fun setAdb(v: Boolean) { _s.value = _s.value.copy(adb = v) }

    fun applyPreset(set: Set<String>) { _s.value = _s.value.copy(passes = set) }

    fun togglePass(key: String) {
        val p = PassCatalog.SPEC.firstOrNull { it.key == key } ?: return
        if (!p.selectable) return
        val cur = _s.value.passes.toMutableSet()
        if (!cur.add(key)) cur.remove(key)
        _s.value = _s.value.copy(passes = cur)
    }

    fun restore(prefs: Prefs) {
        _s.value = _s.value.copy(passes = prefs.passes, adb = prefs.antiDebug)
    }

    fun tomlText(): String {
        val sel = _s.value.passes.toMutableSet()
        if (!_s.value.adb) sel.remove("anti_debugging")
        return PassCatalog.toml(sel)
    }

    /** 扫描 SAF 目录：编译单元与待上传文件分开统计（.h/.hpp/资源也要传） */
    fun scan(ctx: Context, tree: Uri) {
        viewModelScope.launch(Dispatchers.IO) {
            val root = DocumentFile.fromTreeUri(ctx, tree) ?: run {
                log("无法读取该目录"); return@launch
            }
            val skipDir = setOf("build", "obj", "out", ".git", ".gradle", "node_modules",
                "intermediates", "cmake-build-debug", "cmake-build-release", "__pycache__",
                ".idea", ".cxx")
            val skipFile = setOf("defense.c", "defense.h", "defense_str.h")
            val skipExt = setOf("o", "a", "so", "dll", "exe", "apk", "zip", "jar", "class")
            val found = mutableListOf<SourceFile>()

            fun walk(dir: DocumentFile, prefix: String) {
                for (f in dir.listFiles()) {
                    val name = f.name ?: continue
                    if (f.isDirectory) {
                        if (name.lowercase() in skipDir) continue
                        walk(f, if (prefix.isEmpty()) name else "$prefix/$name")
                    } else {
                        if (name in skipFile) continue
                        if (f.length() > 100L * 1024 * 1024) continue
                        val ext = name.substringAfterLast('.', "").lowercase()
                        if (ext.isNotEmpty() && ext in skipExt) continue
                        found.add(SourceFile(
                            if (prefix.isEmpty()) name else "$prefix/$name",
                            f.uri, f.length()))
                    }
                }
            }
            walk(root, "")

            val units = found.count { it.rel.substringAfterLast('.', "").lowercase() in compileExt }
            val headers = found.count {
                val e = it.rel.substringAfterLast('.', "").lowercase()
                e in setOf("h", "hpp", "hh", "hxx", "inc", "inl")
            }
            val proj = (root.name ?: "myproj").replace(" ", "_")
            _s.value = _s.value.copy(files = found, project = proj)
            log("扫描到 $units 个编译单元、${found.size} 个待上传文件（含 $headers 个头文件）")
        }
    }

    fun start(ctx: Context, prefs: Prefs) {
        if (_s.value.phase == Phase.RUNNING) return
        val files = _s.value.files
        val units = files.filter { it.rel.substringAfterLast('.', "").lowercase() in compileExt }
        if (units.isEmpty()) {
            _s.value = _s.value.copy(phase = Phase.ERROR, error = "先选目录：里面没有 .c/.cpp")
            return
        }
        if (prefs.token.isBlank()) {
            _s.value = _s.value.copy(phase = Phase.ERROR, error = "还没填 Token，去「设置」页")
            return
        }

        val mb = files.sumOf { it.size } / 1048576.0
        val npass = _s.value.passes.size + if (_s.value.adb) 1 else 0
        val eta = (120 + 20 * npass + minOf(900L, (20 * mb).toLong())).toLong()

        _s.value = _s.value.copy(
            phase = Phase.RUNNING, statusText = "校验中", progress = 0.02f,
            logs = emptyList(), error = null, artifactPath = null,
            savedUri = null, savedName = null, runUrl = null,
            eta = eta, elapsed = 0L)
        startedAt = System.currentTimeMillis()

        job = viewModelScope.launch(Dispatchers.IO) {
            val ticker = launch {
                while (true) {
                    delay(500)
                    if (_s.value.phase == Phase.RUNNING) {
                        _s.value = _s.value.copy(
                            elapsed = (System.currentTimeMillis() - startedAt) / 1000)
                    } else break
                }
            }
            try {
                val gh = Github(prefs.token)
                val o = prefs.owner
                val r = prefs.repo
                val br = prefs.branch
                val wf = prefs.workflow
                val projDir = "projects/${_s.value.project}"

                log("登录为 ${gh.me()}")
                log("预计用时 ${fmt(eta)}（${npass} 个 pass，${"%.1f".format(mb)} MB）")
                setPhase("上传中", 0.05f)

                // ---- ① 本地生成 TOML 并提交到仓库 ----
                val sel = _s.value.passes.toMutableSet()
                if (!_s.value.adb) sel.remove("anti_debugging")
                val cfgName = "ui-${_s.value.project}"
                val cfgPath = "ci/$cfgName.toml"
                gh.putFile(o, r, br, cfgPath, PassCatalog.toml(sel),
                    "NKBuild-Android: 生成 $cfgPath（${sel.size} 个 pass）")
                log("已上传配置 $cfgPath（${sel.size} 个 pass）")

                // ---- ② 上传源码（含头文件/资源） ----
                val head = gh.headSha(o, r, br)
                val baseTree = gh.treeSha(o, r, head)
                val entries = JSONArray()
                var total = 0L
                for (f in files) {
                    val data: ByteArray? = try {
                        ctx.contentResolver.openInputStream(f.uri)?.use { it.readBytes() }
                    } catch (e: Exception) {
                        null
                    }
                    if (data == null) { log("读不到，跳过：${f.rel}"); continue }
                    val sha = gh.blob(o, r, data)
                    entries.put(JSONObject().put("path", "$projDir/${f.rel}")
                        .put("mode", "100644").put("type", "blob").put("sha", sha))
                    total += data.size
                }
                if (entries.length() == 0) throw RuntimeException("没有可上传的文件")
                val tree = gh.tree(o, r, baseTree, entries)
                val commit = gh.commit(o, r, "NKBuild-Android: ${_s.value.project}", tree, head)
                gh.moveRef(o, r, br, commit)
                log("已提交 ${commit.take(8)}（${entries.length()} 个文件 / ${total / 1024} KB）")

                // ---- ③ 触发 ----
                setPhase("触发中", 0.2f)
                val inputs = mapOf(
                    "src_dir" to projDir,
                    "src_files" to units.joinToString(" ") { it.rel },
                    "presets" to cfgName,
                    "config_file" to cfgPath,
                    "cfg_only" to cfgPath,
                    "out_name" to _s.value.outName.ifBlank { "lucifer" },
                    "defense_level" to "0",
                    "anti_debug" to if (_s.value.adb) "true" else "false",
                    "skip_smoke" to "true",
                )
                val before = gh.latestRunId(o, r, wf)
                gh.dispatch(o, r, wf, br, inputs)
                log("已触发：${units.size} 个编译单元 / $cfgPath / 反调试=${_s.value.adb}")

                var runId = 0L
                for (i in 0 until 30) {
                    delay(4000)
                    val cur = gh.latestRunId(o, r, wf)
                    if (cur > before && cur != 0L) { runId = cur; break }
                }
                if (runId == 0L) throw RuntimeException("没等到新的 run（workflow 名字对吗？）")
                log("run id = $runId")

                // ---- ④ 轮询 ----
                setPhase("编译中", 0.3f)
                var printed = ""
                var info: JSONObject? = null
                val hardLimit = maxOf(240L, eta) * 2
                var waited = 0L
                while (waited < hardLimit) {
                    info = gh.run(o, r, runId)
                    val st = info.optString("status", "unknown")
                    if (st != printed) { log("状态 $st"); printed = st }
                    if (st == "completed") break
                    val frac = (waited.toFloat() / eta.toFloat()).coerceIn(0f, 1f)
                    _s.value = _s.value.copy(progress = 0.3f + 0.55f * frac)
                    delay(10000)
                    waited += 10
                }
                val concl = info?.optString("conclusion", "") ?: ""
                val url = info?.optString("html_url", "") ?: ""
                _s.value = _s.value.copy(runUrl = url.ifEmpty { null })
                if (concl != "success") {
                    val steps = try { gh.failedSteps(o, r, runId) } catch (e: Exception) { emptyList() }
                    throw RuntimeException("编译失败：$concl" +
                        (if (steps.isNotEmpty()) "\n失败步骤：" + steps.take(4).joinToString(" | ") else "") +
                        (if (url.isNotEmpty()) "\n日志：$url" else ""))
                }

                // ---- ⑤ 下载 ----
                setPhase("下载中", 0.88f)
                val dir = File(ctx.filesDir, "out/${_s.value.project}")
                dir.mkdirs()
                var saved: File? = null
                val want = _s.value.outName.ifBlank { "lucifer" }
                for ((aid, name, expired) in gh.artifacts(o, r, runId)) {
                    if (expired) { log("$name 已过期，跳过"); continue }
                    if (name != "lucifer-obf" && name != "nk-toolchain") continue
                    val bytes = gh.downloadArtifact(o, r, aid)
                    ZipInputStream(bytes.inputStream()).use { z ->
                        while (true) {
                            val e = z.nextEntry ?: break
                            if (e.isDirectory) continue
                            val base = e.name.substringAfterLast('/')
                            if (base.isBlank()) continue
                            val out = File(dir, base)
                            out.outputStream().use { o2 -> z.copyTo(o2) }
                            log("下载 $base（${out.length()} 字节）")
                            if (base == want || base.startsWith(want) || base == "lucifer") saved = out
                        }
                    }
                }
                val art = saved ?: throw RuntimeException("没有下载到产物")

                // ---- ⑥ 自动导出到公共 Download/NKBuild ----
                var uri: Uri? = null
                if (prefs.autoExport) {
                    uri = exportToDownloads(ctx, art, art.name)
                }
                _s.value = _s.value.copy(
                    phase = Phase.DONE, statusText = "完成", progress = 1f,
                    artifactPath = art.absolutePath,
                    savedUri = uri?.toString(), savedName = art.name,
                    elapsed = (System.currentTimeMillis() - startedAt) / 1000)
                log("⏱ 总用时 ${fmt(_s.value.elapsed)}")
                log("产物：${art.absolutePath}")
                if (uri != null) log("已存到 Download/NKBuild/${art.name}")
            } catch (e: Exception) {
                val msg = e.message ?: e.toString()
                _s.value = _s.value.copy(phase = Phase.ERROR, statusText = "失败", error = msg,
                    elapsed = (System.currentTimeMillis() - startedAt) / 1000)
                log("!! $msg")
            } finally {
                ticker.cancel()
            }
        }
    }

    fun cancel() {
        job?.cancel()
        _s.value = _s.value.copy(phase = Phase.IDLE, statusText = "已取消", progress = 0f)
        log("已取消")
    }

    /** 复制到公共 Download/NKBuild（API29+ 走 MediaStore，不用存储权限） */
    fun exportToDownloads(ctx: Context, src: File, name: String): Uri? {
        return try {
            if (Build.VERSION.SDK_INT >= 29) {
                val cv = ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, name)
                    put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream")
                    put(MediaStore.MediaColumns.RELATIVE_PATH,
                        Environment.DIRECTORY_DOWNLOADS + "/NKBuild")
                }
                val uri = ctx.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
                if (uri == null) return null
                ctx.contentResolver.openOutputStream(uri)?.use { o ->
                    src.inputStream().use { i -> i.copyTo(o) }
                } ?: return null
                uri
            } else {
                @Suppress("DEPRECATION")
                val dir = File(Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_DOWNLOADS), "NKBuild")
                dir.mkdirs()
                val out = File(dir, name)
                src.copyTo(out, overwrite = true)
                FileProvider.getUriForFile(ctx, "${ctx.packageName}.fileprovider", out)
            }
        } catch (e: Exception) {
            log("存到 Download 失败：${e.message}")
            null
        }
    }

    /** 产物是 .apk 就直接调安装器，其它类型用系统「打开方式」 */
    fun openOrInstall(ctx: Context) {
        val uri = _s.value.savedUri?.let { Uri.parse(it) }
        val name = _s.value.savedName ?: ""
        val path = _s.value.artifactPath
        val target: Uri? = uri ?: path?.let {
            runCatching {
                FileProvider.getUriForFile(ctx, "${ctx.packageName}.fileprovider", File(it))
            }.getOrNull()
        }
        if (target == null) { log("还没有产物"); return }
        val mime = if (name.endsWith(".apk", true)) "application/vnd.android.package-archive"
                   else "application/octet-stream"
        val i = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(target, mime)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        try {
            ctx.startActivity(i)
        } catch (e: Exception) {
            log("系统没有能处理这个类型的应用：${e.message}")
        }
    }

    private fun setPhase(text: String, p: Float) {
        _s.value = _s.value.copy(statusText = text, progress = p)
        log(text)
    }

    override fun onCleared() {
        job?.cancel()
        super.onCleared()
    }
}

fun fmt(sec: Long): String {
    val h = sec / 3600
    val m = (sec % 3600) / 60
    val s = sec % 60
    return if (h > 0) "%d:%02d:%02d".format(h, m, s) else "%d:%02d".format(m, s)
}
