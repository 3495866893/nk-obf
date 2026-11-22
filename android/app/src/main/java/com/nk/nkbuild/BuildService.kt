package com.nk.nkbuild

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder

/**
 * 只为「把进程优先级抬起来」而存在的前台服务：
 * 上传/编译期间挂一条常驻通知，切出去之后系统不轻易把进程回收，
 * 就算真被杀了，回来后 BuildViewModel.resume() 会按 run id 接上。
 */
class BuildService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val text = intent?.getStringExtra("text") ?: "正在上传并编译…"
        ensureChannel()
        val n: Notification = Notification.Builder(this, CH)
            .setContentTitle("NKBuild")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setOngoing(true)
            .build()
        startForeground(1001, n)
        return START_NOT_STICKY
    }

    private fun ensureChannel() {
        if (Build.VERSION.SDK_INT < 26) return
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (nm.getNotificationChannel(CH) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CH, "编译任务", NotificationManager.IMPORTANCE_LOW)
            )
        }
    }

    companion object {
        private const val CH = "nk_build"

        fun start(ctx: Context, text: String) {
            val i = Intent(ctx, BuildService::class.java).putExtra("text", text)
            try {
                if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
            } catch (_: Exception) {
            }
        }

        fun stop(ctx: Context) {
            try {
                ctx.stopService(Intent(ctx, BuildService::class.java))
            } catch (_: Exception) {
            }
        }
    }
}
