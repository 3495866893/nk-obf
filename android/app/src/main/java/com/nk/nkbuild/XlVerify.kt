package com.nk.nkbuild

import android.content.Context
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest
import java.security.SecureRandom
import kotlin.experimental.and
import kotlin.experimental.xor

/**
 * XL 云验证（与 res/xl_verify.h 同协议的 Kotlin 实现）
 *
 * 请求：plain = kami/user/markcode/t/sign/value
 *      sign  = md5("kami=" + km + "&user=" + user + "&markcode=" + imei + "&t=" + t + "&" + APPKEY)
 *      value = "%d%u" (1000 + t%99000, t)
 *      data  = Base64( IV(16) + SM4-CBC(GMKEY, IV, 8B随机‖4B时间戳BE‖plain) + HMAC-SM3(GMKEY, IV‖ct) )
 * 响应：同样 Base64 解包 → 校验 HMAC → 解出后丢掉前 12 字节 → JSON
 *      code==200 且 |time-now|<=30 且 check==md5(time+APPKEY+value)
 */
object XlVerify {

    const val APPID = "11054"
    private const val APPKEY = "y4sH2w11imYIx1K4"
    private const val GMKEY = "tYHhrtawMQIdR5Cx"
    private val HOSTS = listOf("yz.dujiuyue.cn", "xl-relay.vercel.app")

    data class Result(
        val ok: Boolean,
        val msg: String,
        val vip: Long = 0L,
        val expiry: String = "",
    )

    // ------------------------------------------------------------ 对外
    fun deviceCode(ctx: Context): String {
        val f = File(ctx.filesDir, "11054.imei")
        if (f.exists()) {
            val t = runCatching { f.readText().trim() }.getOrDefault("")
            if (t.length >= 16) return t
        }
        val rnd = SecureRandom()
        val sb = StringBuilder()
        repeat(10) { sb.append(rnd.nextInt(10)) }
        val code = md5Hex(sb.toString().toByteArray())
        runCatching { f.writeText(code) }
        return code
    }

    fun login(ctx: Context, kami: String, user: String = ""): Result {
        val km = kami.trim()
        if (km.isEmpty()) return Result(false, "卡密不能为空")
        val t = System.currentTimeMillis() / 1000L
        val imei = deviceCode(ctx)
        val sign = md5Hex("kami=$km&user=$user&markcode=$imei&t=$t&$APPKEY".toByteArray())
        // 与 xl_verify.h 的 sprintf("%d%u") 等价：1000+t%99000 接无符号 t
        val value = (1000L + t % 99000L).toString() + java.lang.Long.toUnsignedString(t)
        val plain = "kami=$km&user=$user&markcode=$imei&t=$t&sign=$sign&value=$value"

        val rnd = SecureRandom()
        val pre = ByteArray(12)
        rnd.nextBytes(pre)
        pre[8] = ((t ushr 24) and 0xFF).toByte()
        pre[9] = ((t ushr 16) and 0xFF).toByte()
        pre[10] = ((t ushr 8) and 0xFF).toByte()
        pre[11] = (t and 0xFF).toByte()
        val body = gmWrap(pre + plain.toByteArray(Charsets.UTF_8))

        val resp = post("/api.php?api=kmlogon&app=$APPID", "data=" + URLEncoder.encode(body, "UTF-8"))
            ?: return Result(false, "连不上验证服务器（网络不通 / 被拦）")
        val json = gmUnwrapToJson(resp) ?: return Result(false, "响应校验失败（密文或签名不对）")

        val code = json.optInt("code", -1)
        if (code != 200) {
            val m = json.optString("msg", "").ifBlank { "code $code" }
            return Result(false, "卡密无效：$m")
        }
        val st = json.optLong("time", 0L)
        if (st > 0 && kotlin.math.abs(st - t) > 30) return Result(false, "服务器时间偏差过大")
        val check = json.optString("check", "")
        val want = md5Hex((st.toString() + APPKEY + value).toByteArray())
        if (check.isNotEmpty() && !check.equals(want, ignoreCase = true)) {
            return Result(false, "响应校验不通过（check 不符）")
        }
        var vip = json.optLong("vip", 0L)
        if (vip <= 0) vip = json.optLong("endtime", 0L)
        if (vip <= 0) vip = json.optLong("expire", 0L)
        if (vip <= 0) vip = json.optLong("expiretime", 0L)
        if (vip > 0 && vip < t) return Result(false, "卡密已过期", vip, fmtDate(vip))
        return Result(true, if (vip > 0) "已授权 · ${fmtDate(vip)}" else "已授权 · 永久", vip,
            if (vip > 0) fmtDate(vip) else "")
    }

    fun unbind(ctx: Context, kami: String): Result {
        val km = kami.trim()
        if (km.isEmpty()) return Result(false, "卡密不能为空")
        val t = System.currentTimeMillis() / 1000L
        val imei = deviceCode(ctx)
        val core = "kami=$km&user=&markcode=$imei&t=$t"
        // 解绑也必须带 sign，否则服务器直接回 104「签名为空」
        val plain = core + "&sign=" + md5Hex((core + "&" + APPKEY).toByteArray())
        val rnd = SecureRandom()
        val pre = ByteArray(12)
        rnd.nextBytes(pre)
        pre[8] = ((t ushr 24) and 0xFF).toByte()
        pre[9] = ((t ushr 16) and 0xFF).toByte()
        pre[10] = ((t ushr 8) and 0xFF).toByte()
        pre[11] = (t and 0xFF).toByte()
        val body = gmWrap(pre + plain.toByteArray(Charsets.UTF_8))
        val resp = post("/api.php?api=kmunmachine&app=$APPID", "data=" + URLEncoder.encode(body, "UTF-8"))
            ?: return Result(false, "连不上验证服务器")
        val json = gmUnwrapToJson(resp) ?: return Result(false, "响应校验失败")
        return if (json.optInt("code", -1) == 200) Result(true, "解绑完成")
        else Result(false, "解绑失败：" + json.optString("msg", "code " + json.optInt("code", -1)))
    }


    /* ---------------------------------------- 账号 / 公告 / 云端版本（v1.7） */

    /** 账号体系：userreg / userlogon 的字段是 user、password、markcode、t，签名同上 */
    data class Acct(
        val ok: Boolean,
        val msg: String,
        val user: String = "",
        val activated: Boolean = false,
        val vip: Long = 0L,
    )

    /** 云端配置：api=ini 的 msg.version / version_info / app_update_url / app_name */
    data class Cloud(val version: String, val info: String, val url: String, val name: String)

    fun account(ctx: Context, reg: Boolean, user: String, password: String): Acct {
        val u = user.trim()
        val p = password.trim()
        if (u.isEmpty()) return Acct(false, "账号不能为空")
        if (p.isEmpty()) return Acct(false, "密码不能为空")
        val t = System.currentTimeMillis() / 1000L
        val core = "user=$u&password=$p&markcode=" + deviceCode(ctx) + "&t=$t"
        val sign = md5Hex((core + "&" + APPKEY).toByteArray())
        val api = if (reg) "userreg" else "userlogon"
        val json = call(api, core + "&sign=" + sign)
            ?: return Acct(false, "连不上验证服务器（网络不通 / 被拦）")
        val code = json.optInt("code", -1)
        val nested = json.optJSONObject("msg")
        val text = if (nested != null) nested.optString("msg", "") else json.optString("msg", "")
        if (code != 200) return Acct(false, if (text.isBlank()) "code $code" else text)
        val act = if (nested != null) nested.optString("activated", "n").equals("y", true) else true
        val vip = if (nested != null) nested.optLong("vip", 0L) else json.optLong("vip", 0L)
        val name = if (nested != null) nested.optString("user", u) else u
        return Acct(true, if (text.isBlank()) "成功" else text, name, act, vip)
    }

    /** 卡密给账号加时：kmlogon 带 user=账号，服务器按卡密时长续期 */
    fun recharge(ctx: Context, kami: String, user: String): Result {
        val r = login(ctx, kami, user)
        return if (r.ok) Result(true, "充值成功 · " + r.msg, r.vip, r.expiry) else r
    }

    /** 公告：api=notice → msg.app_gg */
    fun notice(): String? {
        val m = getJson("notice")?.optJSONObject("msg") ?: return null
        val s = m.optString("app_gg", "")
        return if (s.isBlank()) null else s
    }

    /** 云端配置（版本号来源） */
    fun cloud(): Cloud? {
        val m = getJson("ini")?.optJSONObject("msg") ?: return null
        return Cloud(
            m.optString("version", ""),
            m.optString("version_info", ""),
            m.optString("app_update_url", ""),
            m.optString("app_name", ""),
        )
    }

    private fun call(api: String, plain: String): org.json.JSONObject? {
        val body = "data=" + URLEncoder.encode(wrapStamp(plain), "UTF-8")
        val resp = post("/api.php?api=$api&app=$APPID", body) ?: return null
        return gmUnwrapToJson(resp)
    }

    private fun wrapStamp(plain: String): String {
        val t = System.currentTimeMillis() / 1000L
        val pre = ByteArray(12)
        SecureRandom().nextBytes(pre)
        pre[8] = ((t ushr 24) and 0xFF).toByte()
        pre[9] = ((t ushr 16) and 0xFF).toByte()
        pre[10] = ((t ushr 8) and 0xFF).toByte()
        pre[11] = (t and 0xFF).toByte()
        return gmWrap(pre + plain.toByteArray(Charsets.UTF_8))
    }

    private fun getJson(api: String): org.json.JSONObject? {
        for (h in HOSTS) {
            for (scheme in listOf("https", "http")) {
                val url = "$scheme://$h/api.php?api=$api&app=$APPID"
                try {
                    val c = URL(url).openConnection() as HttpURLConnection
                    c.connectTimeout = 8000
                    c.readTimeout = 12000
                    c.requestMethod = "GET"
                    c.setRequestProperty("User-Agent", "NKBuild")
                    if (c.responseCode == 200) {
                        val s = c.inputStream.bufferedReader().readText().trim()
                        if (s.isNotEmpty() && !s.startsWith("<")) {
                            val j = gmUnwrapToJson(s)
                            if (j != null) {
                                c.disconnect()
                                return j
                            }
                        }
                    }
                    c.disconnect()
                } catch (_: Exception) {
                }
            }
        }
        return null
    }

    private fun fmtDate(sec: Long): String {
        val f = java.text.SimpleDateFormat("yyyy-MM-dd", java.util.Locale.US)
        return f.format(java.util.Date(sec * 1000L))
    }

    // ------------------------------------------------------------ 网络
    private fun post(path: String, form: String): String? {
        for (h in HOSTS) {
            for (scheme in listOf("https", "http")) {
                val text = runCatching { postOnce("$scheme://$h$path", form) }.getOrNull()
                if (!text.isNullOrBlank()) return text
            }
        }
        return null
    }

    private fun postOnce(url: String, form: String): String {
        val c = URL(url).openConnection() as HttpURLConnection
        c.requestMethod = "POST"
        c.connectTimeout = 12000
        c.readTimeout = 15000
        c.doOutput = true
        c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
        c.setRequestProperty("User-Agent", "NKBuild-android/1.6")
        c.outputStream.use { it.write(form.toByteArray(Charsets.UTF_8)) }
        val code = c.responseCode
        val s = (if (code in 200..299) c.inputStream else c.errorStream)
            ?.bufferedReader(Charsets.UTF_8)?.use { it.readText() } ?: ""
        c.disconnect()
        return s.trim()
    }

    // ------------------------------------------------------------ 信封
    private fun gmWrap(data: ByteArray): String {
        val key = GMKEY.toByteArray(Charsets.UTF_8)
        val iv = ByteArray(16).also { SecureRandom().nextBytes(it) }
        val ct = sm4CbcEncrypt(key, iv, data)
        val tag = hmacSm3(key, iv + ct)
        return b64(iv + ct + tag)
    }

    private fun gmUnwrapToJson(text: String): org.json.JSONObject? = runCatching {
        val raw = unB64(text.trim())
        if (raw.size < 16 + 16 + 32) return null
        val iv = raw.copyOfRange(0, 16)
        val tag = raw.copyOfRange(raw.size - 32, raw.size)
        val ct = raw.copyOfRange(16, raw.size - 32)
        val want = hmacSm3(GMKEY.toByteArray(Charsets.UTF_8), iv + ct)
        if (!MessageDigest.isEqual(tag, want)) return null
        val plain = sm4CbcDecrypt(GMKEY.toByteArray(Charsets.UTF_8), iv, ct)
        if (plain.size <= 12) return null
        val jsonText = String(plain, 12, plain.size - 12, Charsets.UTF_8).trim()
        org.json.JSONObject(jsonText)
    }.getOrNull()

    private fun b64(b: ByteArray): String =
        android.util.Base64.encodeToString(b, android.util.Base64.NO_WRAP)

    private fun unB64(s: String): ByteArray =
        android.util.Base64.decode(s.replace("\n", "").replace("\r", ""), android.util.Base64.DEFAULT)

    // ------------------------------------------------------------ SM4
    private val SBOX = intArrayOf(
        0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7, 0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
        0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3, 0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
        0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a, 0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
        0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95, 0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
        0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba, 0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
        0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b, 0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
        0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2, 0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
        0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52, 0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
        0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5, 0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
        0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55, 0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
        0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60, 0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
        0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f, 0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
        0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f, 0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
        0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd, 0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
        0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e, 0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
        0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20, 0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48,
    )
    private val FK = intArrayOf(0xa3b1bac6.toInt(), 0x56aa3350, 0x677d9197, 0xb27022dc.toInt())

    private fun rotl(x: Int, n: Int): Int = (x shl n) or (x ushr (32 - n))

    private fun tau(a: Int): Int =
        (SBOX[(a ushr 24) and 0xFF] shl 24) or
            (SBOX[(a ushr 16) and 0xFF] shl 16) or
            (SBOX[(a ushr 8) and 0xFF] shl 8) or
            SBOX[a and 0xFF]

    private fun lT(b: Int): Int = b xor rotl(b, 2) xor rotl(b, 10) xor rotl(b, 18) xor rotl(b, 24)
    private fun lTp(b: Int): Int = b xor rotl(b, 13) xor rotl(b, 23)

    private fun keySchedule(key: ByteArray): IntArray {
        require(key.size == 16) { "SM4 key must be 16 bytes" }
        val k = IntArray(4) { i ->
            ((key[i * 4].toInt() and 0xFF) shl 24) or ((key[i * 4 + 1].toInt() and 0xFF) shl 16) or
                ((key[i * 4 + 2].toInt() and 0xFF) shl 8) or (key[i * 4 + 3].toInt() and 0xFF)
        }
        for (i in 0..3) k[i] = k[i] xor FK[i]
        val ck = IntArray(32)
        for (i in 0..31) {
            var v = 0
            for (j in 0..3) v = (v shl 8) or (((4 * i + j) * 7) and 0xFF)
            ck[i] = v
        }
        val rk = IntArray(32)
        val x = IntArray(4) { k[it] }
        for (i in 0..31) {
            val t = x[(i + 1) % 4] xor x[(i + 2) % 4] xor x[(i + 3) % 4] xor ck[i]
            val v = x[i % 4] xor lTp(tau(t))
            x[i % 4] = v
            rk[i] = v
        }
        return rk
    }

    private fun cryptBlock(block: ByteArray, off: Int, rk: IntArray, out: ByteArray, outOff: Int) {
        var x0 = ((block[off].toInt() and 0xFF) shl 24) or ((block[off + 1].toInt() and 0xFF) shl 16) or
            ((block[off + 2].toInt() and 0xFF) shl 8) or (block[off + 3].toInt() and 0xFF)
        var x1 = ((block[off + 4].toInt() and 0xFF) shl 24) or ((block[off + 5].toInt() and 0xFF) shl 16) or
            ((block[off + 6].toInt() and 0xFF) shl 8) or (block[off + 7].toInt() and 0xFF)
        var x2 = ((block[off + 8].toInt() and 0xFF) shl 24) or ((block[off + 9].toInt() and 0xFF) shl 16) or
            ((block[off + 10].toInt() and 0xFF) shl 8) or (block[off + 11].toInt() and 0xFF)
        var x3 = ((block[off + 12].toInt() and 0xFF) shl 24) or ((block[off + 13].toInt() and 0xFF) shl 16) or
            ((block[off + 14].toInt() and 0xFF) shl 8) or (block[off + 15].toInt() and 0xFF)
        for (i in 0..31) {
            val t = x1 xor x2 xor x3 xor rk[i]
            val v = x0 xor lT(tau(t))
            x0 = x1; x1 = x2; x2 = x3; x3 = v
        }
        val w = intArrayOf(x3, x2, x1, x0)
        for (i in 0..3) {
            out[outOff + i * 4] = ((w[i] ushr 24) and 0xFF).toByte()
            out[outOff + i * 4 + 1] = ((w[i] ushr 16) and 0xFF).toByte()
            out[outOff + i * 4 + 2] = ((w[i] ushr 8) and 0xFF).toByte()
            out[outOff + i * 4 + 3] = (w[i] and 0xFF).toByte()
        }
    }

    /** PKCS#7 补位 + CBC 加密 */
    fun sm4CbcEncrypt(key: ByteArray, iv: ByteArray, data: ByteArray): ByteArray {
        val rk = keySchedule(key)
        val pad = 16 - data.size % 16
        val buf = data + ByteArray(pad) { pad.toByte() }
        val out = ByteArray(buf.size)
        var prev = iv.copyOf()
        val blk = ByteArray(16)
        var i = 0
        while (i < buf.size) {
            for (j in 0..15) blk[j] = (buf[i + j] xor prev[j])
            cryptBlock(blk, 0, rk, out, i)
            prev = out.copyOfRange(i, i + 16)
            i += 16
        }
        return out
    }

    fun sm4CbcDecrypt(key: ByteArray, iv: ByteArray, ct: ByteArray): ByteArray {
        val rk = keySchedule(key)
        val out = ByteArray(ct.size)
        var prev = iv.copyOf()
        var i = 0
        while (i < ct.size) {
            // 解密用逆序轮密钥
            cryptBlock(ct, i, rk.reversedArray(), out, i)
            for (j in 0..15) out[i + j] = (out[i + j] xor prev[j])
            prev = ct.copyOfRange(i, i + 16)
            i += 16
        }
        val pad = out.last().toInt() and 0xFF
        if (pad in 1..16 && out.size >= pad) return out.copyOfRange(0, out.size - pad)
        return out
    }

    // ------------------------------------------------------------ SM3
    private fun sm3Compress(v: IntArray, block: ByteArray, off: Int) {
        val w = IntArray(68)
        for (j in 0..15) {
            w[j] = ((block[off + j * 4].toInt() and 0xFF) shl 24) or
                ((block[off + j * 4 + 1].toInt() and 0xFF) shl 16) or
                ((block[off + j * 4 + 2].toInt() and 0xFF) shl 8) or
                (block[off + j * 4 + 3].toInt() and 0xFF)
        }
        for (j in 16..67) {
            val a = w[j - 16] xor w[j - 9] xor rotl(w[j - 3], 15)
            val b = rotl(a, 15) xor a  // P1 = x ^ rotl(x,15) ^ rotl(x,23)
            val p1 = b xor rotl(a, 23)
            w[j] = p1 xor rotl(w[j - 13], 7) xor w[j - 6]
        }
        val w1 = IntArray(64) { w[it] xor w[it + 4] }
        var a = v[0]; var b = v[1]; var c = v[2]; var d = v[3]
        var e = v[4]; var f = v[5]; var g = v[6]; var h = v[7]
        for (j in 0..63) {
            val tj = if (j < 16) rotl(0x79cc4519, j % 32) else rotl(0x7a879d8a, j % 32)
            val ss1 = rotl(rotl(a, 12) + e + tj, 7)
            val ss2 = ss1 xor rotl(a, 12)
            val ff = if (j < 16) (a xor b xor c) else ((a and b) or (a and c) or (b and c))
            val gg = if (j < 16) (e xor f xor g) else ((e and f) or (e.inv() and g))
            val tt1 = ff + d + ss2 + w1[j]
            val tt2 = gg + h + ss1 + w[j]
            d = c
            c = rotl(b, 9)
            b = a
            a = tt1
            h = g
            g = rotl(f, 19)
            f = e
            val p0 = tt2 xor rotl(tt2, 9) xor rotl(tt2, 17)
            e = p0
        }
        v[0] = v[0] xor a; v[1] = v[1] xor b; v[2] = v[2] xor c; v[3] = v[3] xor d
        v[4] = v[4] xor e; v[5] = v[5] xor f; v[6] = v[6] xor g; v[7] = v[7] xor h
    }

    fun sm3(data: ByteArray): ByteArray {
        val v = intArrayOf(
            0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600.toInt(),
            0xa96f30bc.toInt(), 0x163138aa, 0xe38dee4d.toInt(), 0xb0fb0e4e.toInt(),
        )
        val bitLen = data.size.toLong() * 8
        val padLen = ((data.size + 9 + 63) / 64) * 64
        val buf = ByteArray(padLen)
        System.arraycopy(data, 0, buf, 0, data.size)
        buf[data.size] = 0x80.toByte()
        for (i in 0..7) buf[padLen - 1 - i] = ((bitLen ushr (8 * i)) and 0xFF).toByte()
        var off = 0
        while (off < buf.size) {
            sm3Compress(v, buf, off)
            off += 64
        }
        val out = ByteArray(32)
        for (i in 0..7) {
            out[i * 4] = ((v[i] ushr 24) and 0xFF).toByte()
            out[i * 4 + 1] = ((v[i] ushr 16) and 0xFF).toByte()
            out[i * 4 + 2] = ((v[i] ushr 8) and 0xFF).toByte()
            out[i * 4 + 3] = (v[i] and 0xFF).toByte()
        }
        return out
    }

    fun hmacSm3(key: ByteArray, data: ByteArray): ByteArray {
        var k = if (key.size > 64) sm3(key) else key
        if (k.size < 64) k = k + ByteArray(64 - k.size)
        val ipad = ByteArray(64) { (k[it] xor 0x36.toByte()) }
        val opad = ByteArray(64) { (k[it] xor 0x5c.toByte()) }
        return sm3(opad + sm3(ipad + data))
    }

    private fun md5Hex(b: ByteArray): String {
        val d = MessageDigest.getInstance("MD5").digest(b)
        val sb = StringBuilder()
        for (x in d) sb.append(String.format("%02x", x.toInt() and 0xFF))
        return sb.toString()
    }
}
