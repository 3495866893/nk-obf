package com.nk.nkbuild

import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/** GitHub REST 客户端：只做 NKBuild 需要的那几件事 */
class Github(private val token: String) {

    private val http = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(180, TimeUnit.SECONDS)
        .writeTimeout(180, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    private val json = "application/json".toMediaType()

    private fun req(path: String, method: String = "GET", body: String? = null): String {
        val url = if (path.startsWith("http")) path else "https://api.github.com$path"
        val b = Request.Builder().url(url)
            .header("Authorization", "Bearer $token")
            .header("Accept", "application/vnd.github+json")
            .header("User-Agent", "NKBuild-Android")
        when (method) {
            "POST" -> b.post((body ?: "{}").toRequestBody(json))
            "PATCH" -> b.patch((body ?: "{}").toRequestBody(json))
            "PUT" -> b.put((body ?: "{}").toRequestBody(json))
        }
        http.newCall(b.build()).execute().use { r ->
            val t = r.body?.string() ?: ""
            if (!r.isSuccessful) {
                val msg = when (r.code) {
                    401 -> "Token 无效或已被撤销（HTTP 401），去设置里重填"
                    403 -> "权限不足或被限流（HTTP 403），确认 token 勾了 repo + workflow"
                    else -> "HTTP ${r.code}：${t.take(200)}"
                }
                throw RuntimeException(msg)
            }
            return t
        }
    }

    fun me(): String = JSONObject(req("/user")).optString("login", "?")

    fun headSha(o: String, r: String, br: String): String {
        val j = JSONObject(req("/repos/$o/$r/git/ref/heads/$br"))
        return j.getJSONObject("object").getString("sha")
    }

    fun treeSha(o: String, r: String, commit: String): String {
        val j = JSONObject(req("/repos/$o/$r/git/commits/$commit"))
        return j.getJSONObject("tree").getString("sha")
    }

    fun blob(o: String, r: String, data: ByteArray): String {
        val body = JSONObject()
            .put("content", android.util.Base64.encodeToString(data, android.util.Base64.NO_WRAP))
            .put("encoding", "base64")
            .toString()
        return JSONObject(req("/repos/$o/$r/git/blobs", "POST", body)).getString("sha")
    }

    fun tree(o: String, r: String, base: String, entries: JSONArray): String {
        val body = JSONObject().put("base_tree", base).put("tree", entries).toString()
        return JSONObject(req("/repos/$o/$r/git/trees", "POST", body)).getString("sha")
    }

    fun commit(o: String, r: String, msg: String, tree: String, parent: String): String {
        val body = JSONObject().put("message", msg).put("tree", tree)
            .put("parents", JSONArray().put(parent)).toString()
        return JSONObject(req("/repos/$o/$r/git/commits", "POST", body)).getString("sha")
    }

    fun moveRef(o: String, r: String, br: String, sha: String) {
        val body = JSONObject().put("sha", sha).put("force", false).toString()
        req("/repos/$o/$r/git/refs/heads/$br", "PATCH", body)
    }

    fun dispatch(o: String, r: String, wf: String, ref: String, inputs: Map<String, String>) {
        val i = JSONObject()
        inputs.forEach { (k, v) -> i.put(k, v) }
        val body = JSONObject().put("ref", ref).put("inputs", i).toString()
        req("/repos/$o/$r/actions/workflows/$wf/dispatches", "POST", body)
    }

    fun latestRunId(o: String, r: String, wf: String): Long {
        return try {
            val j = JSONObject(req("/repos/$o/$r/actions/workflows/$wf/runs?per_page=5"))
            val arr = j.optJSONArray("workflow_runs") ?: return 0L
            if (arr.length() == 0) 0L else arr.getJSONObject(0).getLong("id")
        } catch (e: Exception) {
            val j = JSONObject(req("/repos/$o/$r/actions/runs?per_page=10"))
            val arr = j.optJSONArray("workflow_runs") ?: return 0L
            if (arr.length() == 0) 0L else arr.getJSONObject(0).getLong("id")
        }
    }

    fun run(o: String, r: String, id: Long): JSONObject =
        JSONObject(req("/repos/$o/$r/actions/runs/$id"))

    fun failedSteps(o: String, r: String, id: Long): List<String> {
        val out = mutableListOf<String>()
        val j = JSONObject(req("/repos/$o/$r/actions/runs/$id/jobs"))
        val jobs = j.optJSONArray("jobs") ?: return out
        for (i in 0 until jobs.length()) {
            val steps = jobs.getJSONObject(i).optJSONArray("steps") ?: continue
            for (k in 0 until steps.length()) {
                val s = steps.getJSONObject(k)
                if (s.optString("conclusion") == "failure") out.add(s.optString("name"))
            }
        }
        return out
    }

    fun artifacts(o: String, r: String, id: Long): List<Triple<Long, String, Boolean>> {
        val out = mutableListOf<Triple<Long, String, Boolean>>()
        val j = JSONObject(req("/repos/$o/$r/actions/runs/$id/artifacts"))
        val arr = j.optJSONArray("artifacts") ?: return out
        for (i in 0 until arr.length()) {
            val a = arr.getJSONObject(i)
            out.add(Triple(a.getLong("id"), a.getString("name"), a.optBoolean("expired")))
        }
        return out
    }

    fun downloadArtifact(o: String, r: String, aid: Long): ByteArray {
        val url = "https://api.github.com/repos/$o/$r/actions/artifacts/$aid/zip"
        val b = Request.Builder().url(url)
            .header("Authorization", "Bearer $token")
            .header("Accept", "application/vnd.github+json")
            .header("User-Agent", "NKBuild-Android")
            .build()
        http.newCall(b.build()).execute().use { resp ->
            if (!resp.isSuccessful) throw RuntimeException("下载产物失败：HTTP ${resp.code}")
            return resp.body?.bytes() ?: ByteArray(0)
        }
    }
}
