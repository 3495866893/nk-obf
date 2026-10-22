package com.nk.nkbuild

import android.app.Application
import android.os.Build
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter

class NkApp : Application() {
    override fun onCreate() {
        super.onCreate()
        val def = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            runCatching {
                val sw = StringWriter()
                e.printStackTrace(PrintWriter(sw))
                val dir = File(getExternalFilesDir(null) ?: filesDir, "crash")
                dir.mkdirs()
                val f = File(dir, "crash-${System.currentTimeMillis()}.txt")
                f.writeText(
                    "device=${Build.MANUFACTURER} ${Build.MODEL}\n" +
                    "android=${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})\n" +
                    "version=1.1\n\n" + sw.toString()
                )
            }
            def?.uncaughtException(t, e)
        }
    }
}
