package com.nk.nkbuild

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import dev.chrisbanes.haze.HazeState
import dev.chrisbanes.haze.hazeSource

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { App() }
    }
}

@Composable
fun App(vm: BuildViewModel = viewModel()) {
    val ctx = LocalContext.current
    val prefs = remember { Prefs(ctx) }
    val st by vm.state.collectAsStateWithLifecycle()
    val haze = remember { HazeState() }

    var showSettings by remember { mutableStateOf(prefs.token.isBlank()) }
    var srcUri by remember { mutableStateOf<Uri?>(null) }
    var saveLauncher by remember { mutableStateOf<((Uri) -> Unit)?>(null) }

    val pickTree = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        if (uri != null) {
            ctx.contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            srcUri = uri
            vm.scan(ctx, uri)
        }
    }

    val createDoc = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { out ->
        if (out != null) {
            val path = st.artifactPath
            if (path != null) {
                runCatching {
                    ctx.contentResolver.openOutputStream(out)!!.use { o ->
                        java.io.File(path).inputStream().use { i -> i.copyTo(o) }
                    }
                    vm.log("已保存到所选位置")
                }.onFailure { vm.log("保存失败：${it.message}") }
            }
        }
    }

    Box(Modifier.fillMaxSize().background(Color(0xFF0A0B12))) {
        // ---- 背景：光斑 + 网格（真正被玻璃模糊的就是它）----
        val t by rememberInfiniteTransition(label = "bg").animateFloat(
            initialValue = 0f, targetValue = 1f,
            animationSpec = infiniteRepeatable(tween(18000, easing = LinearEasing),
                RepeatMode.Reverse), label = "t")
        androidx.compose.foundation.Canvas(
            Modifier.fillMaxSize().hazeSource(state = haze)
        ) {
            drawRect(Brush.radialGradient(
                colors = listOf(Color(0xCC7C5CFF), Color(0x33241B60), Color.Transparent),
                center = Offset(size.width * (0.2f + 0.08f * t), size.height * 0.12f),
                radius = size.width * 0.85f))
            drawRect(Brush.radialGradient(
                colors = listOf(Color(0x992F6BFF), Color(0x2210206A), Color.Transparent),
                center = Offset(size.width * (0.95f - 0.06f * t), size.height * 0.32f),
                radius = size.width * 0.8f))
            drawRect(Brush.radialGradient(
                colors = listOf(Color(0x66FF5EA8), Color(0x228A1F5A), Color.Transparent),
                center = Offset(size.width * 0.55f, size.height * (1.05f - 0.05f * t)),
                radius = size.width * 0.9f))
        }

        Column(
            Modifier
                .fillMaxSize()
                .statusBarsPadding()
                .navigationBarsPadding()
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // ---- 顶栏 ----
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("NKBUILD", color = Color.White, fontWeight = FontWeight.Black,
                    fontSize = 24.sp, letterSpacing = 2.sp)
                Spacer(Modifier.width(10.dp))
                Text("源码 → 云端混淆编译", color = TextDim, fontSize = 12.sp)
                Spacer(Modifier.weight(1f))
                Box(
                    Modifier.clip(RoundedCornerShape(14.dp))
                        .background(Color.White.copy(alpha = 0.08f))
                        .padding(horizontal = 12.dp, vertical = 6.dp)
                ) {
                    Text(
                        "● ${st.statusText}" + if (st.phase == Phase.RUNNING) " ${fmt(st.elapsed)}" else "",
                        color = when (st.phase) {
                            Phase.DONE -> Color(0xFF3DDC97)
                            Phase.ERROR -> Color(0xFFFF6B81)
                            else -> TextSub
                        }, fontSize = 12.sp
                    )
                }
                Spacer(Modifier.width(8.dp))
                Box(
                    Modifier.clip(RoundedCornerShape(12.dp))
                        .background(Color.White.copy(alpha = 0.10f))
                        .clickable { showSettings = true }
                        .padding(horizontal = 14.dp, vertical = 8.dp)
                ) { Text("设置", color = TextMain, fontSize = 13.sp) }
            }

            // ---- ① 源码 ----
            GlassCard(haze) {
                SectionTitle("①", "源码")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Box(
                        Modifier.weight(1f).height(42.dp).clip(RoundedCornerShape(12.dp))
                            .background(Color.White.copy(alpha = 0.06f))
                            .padding(horizontal = 12.dp),
                        contentAlignment = Alignment.CenterStart
                    ) {
                        Text(
                            if (st.files.isEmpty()) "选择项目目录（SAF）"
                            else "已选 ${st.files.size} 个文件",
                            color = if (st.files.isEmpty()) TextDim else TextMain, fontSize = 13.sp
                        )
                    }
                    PrimaryButton("选择", true) { pickTree.launch(null) }
                }
                if (st.files.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    Text(st.files.take(6).joinToString("\n") { it.rel } +
                        if (st.files.size > 6) "\n…（共 ${st.files.size} 个）" else "",
                        color = TextSub, fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
            }

            // ---- ② 混淆强度 ----
            GlassCard(haze) {
                SectionTitle("②", "混淆强度")
                GlassSegmented(listOf("标准", "轻量"),
                    if (st.strong) 0 else 1) { idx -> vm.setStrong(idx == 0) }
                Spacer(Modifier.height(6.dp))
                Text("标准=满血（含反调试另开）｜轻量=只有字符串加密+替换+BCF+分割",
                    color = TextDim, fontSize = 11.sp)
                Spacer(Modifier.height(6.dp))
                Text("源码总量 > 20 MB 自动切轻量", color = TextDim, fontSize = 11.sp)
            }

            // ---- ③ 反调试 ----
            GlassCard(haze) {
                SectionTitle("③", "Ensia 反调试")
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("IR 级反调试（adb）", color = TextMain, fontSize = 13.sp)
                        Spacer(Modifier.height(4.dp))
                        Text("ptrace 自附加 · TracerPid 轮询 · 反 Hook\n虚拟机请关掉，否则会静默退出",
                            color = TextDim, fontSize = 11.sp)
                    }
                    GlassSwitch(st.adb) { vm.setAdb(it) }
                }
            }

            // ---- 产物 ----
            GlassCard(haze) {
                SectionTitle("", "产物")
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("工程名", color = TextDim, fontSize = 12.sp, modifier = Modifier.width(56.dp))
                    androidx.compose.foundation.text.BasicTextField(
                        value = st.project, onValueChange = vm::setProject,
                        singleLine = true, textStyle = androidx.compose.ui.text.TextStyle(
                            color = TextMain, fontSize = 13.sp),
                        modifier = Modifier.weight(1f)
                            .clip(RoundedCornerShape(10.dp))
                            .background(Color.White.copy(alpha = 0.06f))
                            .padding(horizontal = 10.dp, vertical = 10.dp),
                        cursorBrush = Brush.verticalGradient(listOf(Accent, Accent))
                    )
                }
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("产物名", color = TextDim, fontSize = 12.sp, modifier = Modifier.width(56.dp))
                    androidx.compose.foundation.text.BasicTextField(
                        value = st.outName, onValueChange = vm::setOutName,
                        singleLine = true, textStyle = androidx.compose.ui.text.TextStyle(
                            color = TextMain, fontSize = 13.sp),
                        modifier = Modifier.weight(1f)
                            .clip(RoundedCornerShape(10.dp))
                            .background(Color.White.copy(alpha = 0.06f))
                            .padding(horizontal = 10.dp, vertical = 10.dp),
                        cursorBrush = Brush.verticalGradient(listOf(Accent, Accent))
                    )
                }
            }

            // ---- ④ 日志 ----
            GlassCard(haze) {
                SectionTitle("④", "日志")
                Column(
                    Modifier.fillMaxWidth().heightIn(min = 140.dp, max = 300.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color.Black.copy(alpha = 0.35f))
                        .padding(10.dp)
                        .verticalScroll(rememberScrollState())
                ) {
                    Text(
                        st.logs.ifEmpty { listOf("日志会实时显示：上传 → 触发 → 编译 → 下载") }
                            .joinToString("\n"),
                        color = TextSub, fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }

            // ---- 底部：进度 + 开始 ----
            GlassCard(haze) {
                Row(verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Box(Modifier.weight(1f).height(10.dp).clip(RoundedCornerShape(5.dp))
                        .background(Color.White.copy(alpha = 0.10f))) {
                        Box(Modifier.fillMaxHeight()
                            .fillMaxWidth(st.progress.coerceIn(0f, 1f))
                            .clip(RoundedCornerShape(5.dp))
                            .background(Brush.horizontalGradient(
                                listOf(Color(0xFF6A48FF), Accent2, Color(0xFF6A48FF)))))
                    }
                    Text("${(st.progress * 100).toInt()}%", color = TextSub, fontSize = 12.sp)
                    PrimaryButton(if (st.phase == Phase.RUNNING) "编译中…" else "开始编译",
                        st.phase != Phase.RUNNING) { vm.start(ctx, prefs) }
                }
                if (st.phase == Phase.DONE) {
                    Spacer(Modifier.height(8.dp))
                    Text("⏱ 总用时 ${fmt(st.elapsed)}", color = Color(0xFF3DDC97), fontSize = 12.sp)
                    if (st.artifactPath != null) {
                        Spacer(Modifier.height(6.dp))
                        PrimaryButton("另存产物", true) {
                            createDoc.launch(st.artifactPath!!.substringAfterLast('/'))
                        }
                    }
                }
                if (st.error != null) {
                    Spacer(Modifier.height(8.dp))
                    Text(st.error!!, color = Color(0xFFFF6B81), fontSize = 12.sp)
                }
            }

            Spacer(Modifier.height(20.dp))
        }

        if (showSettings) {
            SettingsSheet(
                prefs = prefs,
                onClose = {
                    showSettings = false
                    vm.log("设置已保存")
                })
        }
    }
}

@Composable
fun SettingsSheet(prefs: Prefs, onClose: () -> Unit) {
    var token by remember { mutableStateOf(prefs.token) }
    var owner by remember { mutableStateOf(prefs.owner) }
    var repo by remember { mutableStateOf(prefs.repo) }
    var branch by remember { mutableStateOf(prefs.branch) }
    var wf by remember { mutableStateOf(prefs.workflow) }

    Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.55f))
        .clickable { onClose() }, contentAlignment = Alignment.Center) {
        Column(
            Modifier.padding(20.dp).fillMaxWidth()
                .clip(RoundedCornerShape(22.dp))
                .background(Color(0xF2141726))
                .clickable(enabled = false) {}
                .padding(20.dp)
        ) {
            Text("设置", color = TextMain, fontWeight = FontWeight.Bold, fontSize = 17.sp)
            Spacer(Modifier.height(12.dp))
            Field("GitHub Token", token) { token = it }
            Field("用户名/组织", owner) { owner = it }
            Field("仓库", repo) { repo = it }
            Field("分支", branch) { branch = it }
            Field("工作流文件", wf) { wf = it }
            Text("Token 只存本机 SharedPreferences", color = TextDim, fontSize = 11.sp)
            Spacer(Modifier.height(14.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                PrimaryButton("保存", true) {
                    prefs.token = token.trim()
                    prefs.owner = owner.trim()
                    prefs.repo = repo.trim()
                    prefs.branch = branch.trim().ifBlank { "main" }
                    prefs.workflow = wf.trim().ifBlank { "build-obf.yml" }
                    onClose()
                }
                Box(Modifier.height(46.dp).clip(RoundedCornerShape(14.dp))
                    .background(Color.White.copy(alpha = 0.10f))
                    .clickable { onClose() }.padding(horizontal = 18.dp),
                    contentAlignment = Alignment.Center
                ) { Text("取消", color = TextMain, fontSize = 14.sp) }
            }
        }
    }
}

@Composable
private fun Field(label: String, value: String, onChange: (String) -> Unit) {
    Text(label, color = TextDim, fontSize = 12.sp)
    Spacer(Modifier.height(4.dp))
    androidx.compose.foundation.text.BasicTextField(
        value = value, onValueChange = onChange, singleLine = true,
        textStyle = androidx.compose.ui.text.TextStyle(color = TextMain, fontSize = 13.sp),
        modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp))
            .background(Color.White.copy(alpha = 0.08f))
            .padding(horizontal = 10.dp, vertical = 10.dp),
        cursorBrush = Brush.verticalGradient(listOf(Accent, Accent))
    )
    Spacer(Modifier.height(10.dp))
}
