package com.nk.nkbuild

import android.content.Context
import android.content.SharedPreferences

class Prefs(ctx: Context) {
    private val sp: SharedPreferences =
        ctx.getSharedPreferences("nkbuild", Context.MODE_PRIVATE)

    var token: String
        get() = sp.getString("token", "") ?: ""
        set(v) = sp.edit().putString("token", v).apply()

    var owner: String
        get() = sp.getString("owner", "3495866893") ?: "3495866893"
        set(v) = sp.edit().putString("owner", v).apply()

    var repo: String
        get() = sp.getString("repo", "nk-plugin") ?: "nk-plugin"
        set(v) = sp.edit().putString("repo", v).apply()

    var branch: String
        get() = sp.getString("branch", "main") ?: "main"
        set(v) = sp.edit().putString("branch", v).apply()

    var workflow: String
        get() = sp.getString("workflow", "build-obf.yml") ?: "build-obf.yml"
        set(v) = sp.edit().putString("workflow", v).apply()

    var lastLog: String
        get() = sp.getString("lastLog", "") ?: ""
        set(v) = sp.edit().putString("lastLog", v).apply()

    /** 勾选的 pass（逗号分隔）。空 = 首次启动，用默认 8 个 */
    var passes: Set<String>
        get() {
            val raw = sp.getString("passes", null) ?: return PassCatalog.DEFAULTS
            if (raw.isBlank()) return PassCatalog.DEFAULTS
            return raw.split(',').filter { it.isNotBlank() }.toSet()
        }
        set(v) = sp.edit().putString("passes", v.joinToString(",")).apply()

    /** Nk IR 级反调试 + nk_ad 运行时看门狗总开关 */
    var antiDebug: Boolean
        get() = sp.getBoolean("antiDebug", true)
        set(v) = sp.edit().putBoolean("antiDebug", v).apply()

    /** 液态玻璃模糊：部分机型（vivo/OPPO/一加等）的 GPU/Shader 会导致崩，默认给它们关掉 */
    var blur: Boolean
        get() = sp.getBoolean("blur", defaultBlur())
        set(v) = sp.edit().putBoolean("blur", v).apply()

    /** 产物自动存公共 Download/NKBuild */
    var autoExport: Boolean
        get() = sp.getBoolean("autoExport", true)
        set(v) = sp.edit().putBoolean("autoExport", v).apply()

    /* ---- 后台任务的断点：切出去/被系统杀掉之后回来还能接上 ---- */

    /** 已触发的 run id；0 = 还没触发 */
    var runId: Long
        get() = sp.getLong("run_id", 0L)
        set(v) = sp.edit().putLong("run_id", v).apply()

    var jobProj: String
        get() = sp.getString("job_proj", "") ?: ""
        set(v) = sp.edit().putString("job_proj", v).apply()

    var jobOut: String
        get() = sp.getString("job_out", "") ?: ""
        set(v) = sp.edit().putString("job_out", v).apply()

    var jobEta: Long
        get() = sp.getLong("job_eta", 0L)
        set(v) = sp.edit().putLong("job_eta", v).apply()

    var jobStart: Long
        get() = sp.getLong("job_start", 0L)
        set(v) = sp.edit().putLong("job_start", v).apply()

    /** 开始上传前先落一个时间戳（此时还没有 run id） */
    fun markJob(proj: String, out: String, eta: Long) {
        sp.edit().putLong("run_id", 0L).putString("job_proj", proj)
            .putString("job_out", out).putLong("job_eta", eta)
            .putLong("job_start", System.currentTimeMillis()).apply()
    }

    fun clearJob() {
        sp.edit().remove("run_id").remove("job_proj").remove("job_out")
            .remove("job_eta").remove("job_start").apply()
    }

    /* ---- 任务历史：每次派发都落一条，产物下载被中断时可以点着重下 ---- */

    var jobsJson: String
        get() = sp.getString("jobs", "[]") ?: "[]"
        set(v) = sp.edit().putString("jobs", v).apply()

    fun jobs(): List<JobRecord> = runCatching {
        val arr = org.json.JSONArray(jobsJson)
        (0 until arr.length()).mapNotNull { i ->
            val o = arr.optJSONObject(i) ?: return@mapNotNull null
            JobRecord(o.optLong("run", 0L), o.optString("proj", "myproj"),
                o.optString("out", "lucifer"), o.optLong("start", 0L),
                o.optString("file", ""), o.optBoolean("done", false))
        }
    }.getOrDefault(emptyList())

    fun saveJobs(list: List<JobRecord>) {
        val arr = org.json.JSONArray()
        list.take(30).forEach { j ->
            arr.put(org.json.JSONObject().put("run", j.runId).put("proj", j.proj)
                .put("out", j.out).put("start", j.startMs).put("file", j.file).put("done", j.done))
        }
        jobsJson = arr.toString()
    }

    /** 派发成功、拿到 run id 时记一条（同 run 去重，最新的排最前） */
    fun addJob(runId: Long, proj: String, out: String, startMs: Long) {
        val rest = jobs().filter { it.runId != runId }
        saveJobs(listOf(JobRecord(runId, proj, out, startMs)) + rest)
    }

    fun markJobDone(runId: Long, file: String) {
        saveJobs(jobs().map { if (it.runId == runId) it.copy(file = file, done = true) else it })
    }

    fun removeJob(runId: Long) {
        saveJobs(jobs().filter { it.runId != runId })
    }

    /** XL 云验证卡密（登录成功后落盘，下次启动自动复验） */
    var kami: String
        get() = sp.getString("kami", "") ?: ""
        set(v) = sp.edit().putString("kami", v).apply()

    /** 上次验证通过的到期秒（0 = 永久），只用来做本地提示 */
    var licVip: Long
        get() = sp.getLong("lic_vip", 0L)
        set(v) = sp.edit().putLong("lic_vip", v).apply()

    /* ---- 账号（v1.7）：登录成功后本地记住，方便下次自动登录 ---- */

    var acctUser: String
        get() = sp.getString("acct_user", "") ?: ""
        set(v) = sp.edit().putString("acct_user", v).apply()

    var acctPass: String
        get() = sp.getString("acct_pass", "") ?: ""
        set(v) = sp.edit().putString("acct_pass", v).apply()

    var noticeText: String
        get() = sp.getString("notice", "") ?: ""
        set(v) = sp.edit().putString("notice", v).apply()

    var cloudVer: String
        get() = sp.getString("cloud_ver", "") ?: ""
        set(v) = sp.edit().putString("cloud_ver", v).apply()

    var cloudInfo: String
        get() = sp.getString("cloud_info", "") ?: ""
        set(v) = sp.edit().putString("cloud_info", v).apply()

    var cloudUrl: String
        get() = sp.getString("cloud_url", "") ?: ""
        set(v) = sp.edit().putString("cloud_url", v).apply()

    /** 皮肤背景：每切一页换一张图（默认开） */
    var skin: Boolean
        get() = sp.getBoolean("skin", true)
        set(v) = sp.edit().putBoolean("skin", v).apply()

    private fun defaultBlur(): Boolean {
        if (android.os.Build.VERSION.SDK_INT < 31) return false
        val m = (android.os.Build.MANUFACTURER + android.os.Build.BRAND).lowercase()
        val risky = listOf("vivo", "oppo", "bbk", "realme", "oneplus", "iqoo")
        return risky.none { m.contains(it) }
    }
}

/** 一条任务历史：run id、工程名、产物名、派发时间、下载到的文件名 */
data class JobRecord(
    val runId: Long,
    val proj: String,
    val out: String,
    val startMs: Long,
    val file: String = "",
    val done: Boolean = false,
)
