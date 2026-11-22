package com.nk.nkbuild

import android.content.Context
import android.media.SoundPool
import android.view.HapticFeedbackConstants
import android.view.View

/**
 * 按钮音效 + 震感。
 *
 * sound pool 的 maxStreams 固定为 1，所以连点也不会两句声音叠在一起；
 * 再叠一层 55ms 的节流，长按/连击时只响一次。
 */
object Sfx {
    const val CLICK = 0
    const val OK = 1
    const val DENY = 2
    const val TICK = 3

    private var pool: SoundPool? = null
    private val ids = IntArray(4)
    private var lastAt = 0L

    private fun warm(ctx: Context) {
        if (pool != null) return
        val p = SoundPool.Builder().setMaxStreams(1).build()
        ids[CLICK] = p.load(ctx, R.raw.sfx_click, 1)
        ids[OK] = p.load(ctx, R.raw.sfx_ok, 1)
        ids[DENY] = p.load(ctx, R.raw.sfx_deny, 1)
        ids[TICK] = p.load(ctx, R.raw.sfx_tick, 1)
        pool = p
    }

    fun play(ctx: Context, kind: Int = CLICK) {
        warm(ctx)
        val now = System.currentTimeMillis()
        if (now - lastAt < 55L) return
        lastAt = now
        val p = pool ?: return
        val vol = when (kind) {
            OK -> 0.75f
            DENY -> 0.60f
            TICK -> 0.45f
            else -> 0.60f
        }
        runCatching { p.play(ids[kind], vol, vol, 1, 0, 1f) }
    }

    /** 轻点 → KEYBOARD_TAP；成功 → CONFIRM；失败 → REJECT（低版本自动退化成 LONG_PRESS） */
    fun haptic(v: View?, kind: Int = CLICK) {
        v ?: return
        val c = when (kind) {
            OK -> if (android.os.Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.CONFIRM
            else HapticFeedbackConstants.LONG_PRESS

            DENY -> if (android.os.Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.REJECT
            else HapticFeedbackConstants.LONG_PRESS

            TICK -> HapticFeedbackConstants.CLOCK_TICK
            else -> HapticFeedbackConstants.KEYBOARD_TAP
        }
        runCatching { v.performHapticFeedback(c) }
    }

    fun tap(ctx: Context, v: View?, kind: Int = CLICK) {
        play(ctx, kind)
        haptic(v, kind)
    }
}
