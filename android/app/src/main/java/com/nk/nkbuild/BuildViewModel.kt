package com.nk.nkbuild

import android.content.Context
import android.net.Uri
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
import java.io.ByteArrayOutputStream
import java.util.zip.ZipInputStream

data class SourceFile(val rel: String, val uri: Uri, val size: Long)

enum class Phase { IDLE, RUNNING, DONE, ERROR }

data class UiState(
    val phase: Phase = Phase.IDLE,
    val statusText: String = "空闲",
    val elapsed: Long = 0L,
    val progress: Float = 0f,
    val logs: List<String> = emptyList(),
    val files: List<SourceFile> = emptyList(),
    val project: String = "myproj",
    val outName: String = "lucifer",
    val strong: Boolean = false,
    val adb: Boolean = true,
    val artifactPath: String? = null,
    val error: String? = null,
)

class BuildViewModel : ViewModel() {

    private val _s = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _s.asStateFlow()

    private var job: Job? = null
    private var startedAt = 0L

    fun log(msg: String) {
        val t = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
            .format(java.util.Date())
        _s.value = _s.value.copy(logs = (_s.value.logs + "[$t] $msg").takeLast(400))
    }

    fun setProject(v: String) { _s.value = _s.value.copy(project = v) }
    fun setOutName(v: String) { _s.value = _s.value.copy(outName = v) }
    fun setStrong(v: Boolean) { _s.value = _s.value.copy(strong = v) }
    fun setAdb(v: Boolean) { _s.value = _s.value.copy(adb = v) }

    /** 扫描 SAF 目录，收集 .c/.cpp/.h（跳过 build/obj/out 与 defense.*） */
    fun scan(ctx: Context, tree: Uri) {
        viewModelScope.launch(Dispatchers.IO) {
            val root = DocumentFile.fromTreeUri(ctx, tree) ?: run {
                log("无法读取该目录"); return@launch
            }
            val skipDir = setOf("build", "obj", "out", ".git", ".gradle", "node_modules",
                "intermediates", "cmake-build-debug", "cmake-build-release", "__pycache__")
            val skipFile = setOf("defense.c", "defense.h", "defense_str.h")
            val exts = setOf("c", "cc", "cpp", "cxx", "c++", "h", "hpp", "hh")
            val found = mutableListOf<SourceFile>()

            fun walk(dir: DocumentFile, prefix: String) {
                for (f in dir.listFiles()) {
                    val name = f.name ?: continue
                    if (f.isDirectory) {
                        if (name.lowercase() in skipDir) continue
                        walk(f, if (prefix.isEmpty()) name else "$prefix/$name")
                    } else {
                        if (name in skipFile) continue
                        val ext = name.substringAfterLast('.', "").lowercase()
                        if (ext in exts) {
                            found.add(SourceFile(
                                if (prefix.isEmpty()) name else "$prefix/$name",
                                f.uri, f.length()))
                        }
                    }
                }
            }
            walk(root, "")
            val proj = root.name?.replace(" ", "_") ?: "myproj"
            _s.value = _s.value.copy(files = found, project = proj)
            log("扫到 ${found.size} 个源文件（工程名 $proj）")
        }
    }

    fun start(ctx: Context, prefs: Prefs) {
        if (_s.value.phase == Phase.RUNNING) return
        val files = _s.value.files
        if (files.isEmpty()) { log("先选目录并扫描源码"); return }
        if (prefs.token.isBlank()) {
            _s.value = _s.value.copy(phase = Phase.ERROR, error = "还没填 Token，点右上角设置")
            return
        }

        _s.value = _s.value.copy(phase = Phase.RUNNING, statusText = "校验中",
            progress = 0.02f, logs = emptyList(), error = null, artifactPath = null)
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
                val o = prefs.owner; val r = prefs.repo
                val br = prefs.branch; val wf = prefs.workflow

                log("登录为 ${gh.me()}")
                setPhase("上传中", 0.05f)

                // 体积统计 + 只上传可编译源文件
                val compileExt = setOf("c", "cc", "cpp", "cxx", "c++")
                val srcs = files.filter { it.rel.substringAfterLast('.', "").lowercase() in compileExt }
                if (srcs.isEmpty()) throw RuntimeException("没有可编译的 .c/.cpp")
                val big = files.sumOf { it.size }
                // 规则与桌面版一致：选「轻量」或总量 > 20MB 都用轻量配置
                val useLight = !_s.value.strong || big > 20L * 1024 * 1024
                if (!_s.value.strong) log("已选「轻量」配置")
                else if (big > 20L * 1024 * 1024)
                    log("源码总量 ${big / 1048576} MB，自动用「轻量」配置")

                val head = gh.headSha(o, r, br)
                val baseTree = gh.treeSha(o, r, head)
                val entries = JSONArray()
                val projDir = "projects/${_s.value.project}"
                var total = 0L
                for (f in files) {
                    val data = ctx.contentResolver.openInputStream(f.uri)!!.use { it.readBytes() }
                    if (data.size > 100 * 1024 * 1024) { log("跳过 >100MB：${f.rel}"); continue }
                    val sha = gh.blob(o, r, data)
                    entries.put(JSONObject().put("path", "$projDir/${f.rel}")
                        .put("mode", "100644").put("type", "blob").put("sha", sha))
                    total += data.size
                    log("+ ${f.rel}（${data.size / 1024} B）")
                }
                if (entries.length() == 0) throw RuntimeException("没有可上传的文件")
                val tree = gh.tree(o, r, baseTree, entries)
                val commit = gh.commit(o, r, "NKBuild-Android: ${_s.value.project}", tree, head)
                gh.moveRef(o, r, br, commit)
                log("已提交 ${commit.take(8)}（共 ${total / 1024} KB）")

                setPhase("触发中", 0.2f)
                val cfg = if (useLight) "ci/fast.toml"
                          else if (_s.value.adb) "ci/max-real.toml" else "ci/max-vm.toml"
                val inputs = mapOf(
                    "src_dir" to projDir,
                    "src_files" to srcs.joinToString(" ") { it.rel },
                    "presets" to "none",
                    "out_name" to (_s.value.outName.ifBlank { "lucifer" }),
                    "defense_level" to "0",
                    "config_file" to cfg,
                    "cfg_only" to cfg,
                    "skip_smoke" to "true",
                )
                val before = gh.latestRunId(o, r, wf)
                gh.dispatch(o, r, wf, br, inputs)
                log("已触发 配置=$cfg 反调试=${_s.value.adb}")

                var runId = 0L
                for (i in 0 until 30) {
                    delay(4000)
                    val cur = gh.latestRunId(o, r, wf)
                    if (cur > before && cur != 0L) { runId = cur; break }
                }
                if (runId == 0L) throw RuntimeException("没等到新的 run（workflow 名字对吗？）")
                log("run id = $runId")

                setPhase("编译中", 0.3f)
                var printed = ""
                var info: JSONObject? = null
                for (i in 0 until 360) {
                    info = gh.run(o, r, runId)
                    val st = info.optString("status", "unknown")
                    if (st != printed) { log("状态 $st"); printed = st }
                    if (st == "completed") break
                    val p = 0.3f + 0.5f * (i / 60f).coerceAtMost(1f)
                    _s.value = _s.value.copy(progress = p)
                    delay(10000)
                }
                val concl = info?.optString("conclusion", "") ?: ""
                if (concl != "success") {
                    val steps = try { gh.failedSteps(o, r, runId) } catch (e: Exception) { emptyList() }
                    val url = info?.optString("html_url", "") ?: ""
                    throw RuntimeException("编译失败：$concl" +
                        (if (steps.isNotEmpty()) "\n失败步骤：" + steps.take(4).joinToString(" | ") else "") +
                        (if (url.isNotEmpty()) "\n日志：$url" else ""))
                }

                setPhase("下载中", 0.88f)
                val dir = java.io.File(ctx.filesDir, "out/${_s.value.project}")
                dir.mkdirs()
                var saved: String? = null
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
                            val out = java.io.File(dir, base)
                            out.outputStream().use { o2 -> z.copyTo(o2) }
                            log("下载 $base（${out.length()} 字节）")
                            if (base.startsWith("lucifer")) saved = out.absolutePath
                        }
                    }
                }
                if (saved == null) throw RuntimeException("没有下载到产物")

                _s.value = _s.value.copy(phase = Phase.DONE, statusText = "完成",
                    progress = 1f, artifactPath = saved,
                    elapsed = (System.currentTimeMillis() - startedAt) / 1000)
                log("⏱ 总用时 ${fmt(_s.value.elapsed)}")
                log("产物：$saved")
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
    val h = sec / 3600; val m = (sec % 3600) / 60; val s = sec % 60
    return if (h > 0) "%d:%02d:%02d".format(h, m, s) else "%d:%02d".format(m, s)
}
