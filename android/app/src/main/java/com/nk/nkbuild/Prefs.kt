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
        get() = sp.getString("repo", "ensia-plugin") ?: "ensia-plugin"
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

    /** Ensia IR 级反调试 + nk_ad 运行时看门狗总开关 */
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

    private fun defaultBlur(): Boolean {
        if (android.os.Build.VERSION.SDK_INT < 31) return false
        val m = (android.os.Build.MANUFACTURER + android.os.Build.BRAND).lowercase()
        val risky = listOf("vivo", "oppo", "bbk", "realme", "oneplus", "iqoo")
        return risky.none { m.contains(it) }
    }
}
