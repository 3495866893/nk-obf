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
}
