package com.nk.nkbuild

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import dev.chrisbanes.haze.HazeState
import dev.chrisbanes.haze.hazeEffect
import dev.chrisbanes.haze.hazeSource

enum class Tab(val label: String, val glyph: String) {
    BUILD("构建", "⚡"),
    OBF("混淆", "◈"),
    PRE("预检", "◎"),
    OUT("产物", "▣"),
    SET("设置", "⚙"),
}

val TABS = listOf(Tab.BUILD, Tab.OBF, Tab.PRE, Tab.OUT, Tab.SET)

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                App()
            }
        }
    }
}

@Composable
fun App(vm: BuildViewModel = viewModel()) {
    val ctx = LocalContext.current
    val prefs = remember { Prefs(ctx) }
    val st by vm.state.collectAsStateWithLifecycle()

    var blurOn by remember { mutableStateOf(prefs.blur) }
    val haze: HazeState? = remember(blurOn) {
        if (blurOn && Build.VERSION.SDK_INT >= 31) HazeState() else null
    }
    var tab by remember { mutableStateOf(if (prefs.token.isBlank()) Tab.SET else Tab.BUILD) }

    LaunchedEffect(Unit) { vm.restore(prefs) }

    val pickTree = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        if (uri != null) {
            runCatching {
                ctx.contentResolver.takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            vm.scan(ctx, uri)
        }
    }

    Box(Modifier.fillMaxSize().background(Color(0xFF0A0B12))) {
        // ---- 背景光斑（真正被玻璃模糊的就是它）----
        val t by rememberInfiniteTransition(label = "bg").animateFloat(
            initialValue = 0f, targetValue = 1f,
            animationSpec = infiniteRepeatable(
                tween(18000, easing = LinearEasing), RepeatMode.Reverse), label = "t")
        Canvas(
            if (haze != null) Modifier.fillMaxSize().hazeSource(state = haze)
            else Modifier.fillMaxSize()
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

        // ---- 会游的蓝色大肥鱼（纯 Canvas 矢量，不占体积、任意分辨率都清晰）----
        if (prefs.fish) FishBackground(haze)

        Column(
            Modifier.fillMaxSize().statusBarsPadding().padding(horizontal = 16.dp)
        ) {
            Header(st)
            // 底部留出悬浮导航栏的高度（68dp 胶囊 + 12dp 外边距 + 余量），
            // 否则最后一张卡片会滑到玻璃条下面去
            Box(Modifier.weight(1f).padding(bottom = 96.dp)) {
                when (tab) {
                    Tab.BUILD -> BuildPage(vm, st, haze) { pickTree.launch(null) }
                    Tab.OBF -> ObfPage(vm, st, haze)
                    Tab.PRE -> PrecheckPage(st, haze)
                    Tab.OUT -> OutPage(vm, st, haze)
                    Tab.SET -> SetPage(prefs, blurOn, prefs.fish, haze,
                        onBlur = { on -> blurOn = on; prefs.blur = on },
                        onFish = { on -> prefs.fish = on })
                }
            }
        }

        LiquidNav(
            haze = haze,
            selected = tab,
            onSelect = { tab = it },
            modifier = Modifier.align(Alignment.BottomCenter).navigationBarsPadding()
                .padding(horizontal = 18.dp, vertical = 12.dp)
        )
    }
}

/* ------------------------------------------------------------------ 顶栏 */

@Composable
private fun Header(st: UiState) {
    Row(Modifier.fillMaxWidth().padding(vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Text("NKBUILD", color = Color.White, fontWeight = FontWeight.Black,
            fontSize = 22.sp, letterSpacing = 2.sp)
        Spacer(Modifier.width(8.dp))
        Text("源码 → 云端混淆", color = TextDim, fontSize = 11.sp)
        Spacer(Modifier.weight(1f))
        Box(
            Modifier.clip(RoundedCornerShape(14.dp))
                .background(Color.White.copy(alpha = 0.08f))
                .padding(horizontal = 10.dp, vertical = 6.dp)
        ) {
            Text(
                "● " + st.statusText +
                    when (st.phase) {
                        Phase.RUNNING -> " ${fmt(st.elapsed)}"
                        Phase.DONE -> " ${fmt(st.elapsed)}"
                        else -> ""
                    },
                color = when (st.phase) {
                    Phase.DONE -> Color(0xFF3DDC97)
                    Phase.ERROR -> Color(0xFFFF6B81)
                    else -> TextSub
                }, fontSize = 11.sp)
        }
    }
}

/* ------------------------------------------------------------- ① 构建页 */

@Composable
private fun BuildPage(vm: BuildViewModel, st: UiState, haze: HazeState?, onPick: () -> Unit) {
    val ctx = LocalContext.current
    val prefs = remember { Prefs(ctx) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        GlassCard(haze) {
            SectionTitle("①", "源码")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier.weight(1f).height(42.dp).clip(RoundedCornerShape(12.dp))
                        .background(Color.White.copy(alpha = 0.06f))
                        .padding(horizontal = 12.dp),
                    contentAlignment = Alignment.CenterStart
                ) {
                    val units = st.files.count {
                        it.rel.substringAfterLast('.', "").lowercase() in
                            setOf("c", "cc", "cpp", "cxx", "c++")
                    }
                    Text(
                        if (st.files.isEmpty()) "选择项目目录（含 .h/.hpp/资源）"
                        else "$units 个编译单元 · ${st.files.size} 个文件",
                        color = if (st.files.isEmpty()) TextDim else TextMain, fontSize = 13.sp)
                }
                PrimaryButton("选择", true) { onPick() }
            }
            if (st.files.isNotEmpty()) {
                Spacer(Modifier.height(8.dp))
                Text(st.files.take(6).joinToString("\n") { it.rel } +
                    if (st.files.size > 6) "\n…（共 ${st.files.size} 个）" else "",
                    color = TextSub, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }

        GlassCard(haze) {
            SectionTitle("②", "工程 / 产物")
            Field("工程名", st.project, vm::setProject)
            Field("产物名", st.outName, vm::setOutName)
        }

        GlassCard(haze) {
            SectionTitle("③", "反调试")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("IR 级反调试 + nk_ad 看门狗", color = TextMain, fontSize = 13.sp)
                    Spacer(Modifier.height(4.dp))
                    Text("TracerPid 探测 · 线程全查 · maps 差分 · libc 序言校验\n" +
                        "模拟器/虚拟机请关掉，否则会静默退出（exit 97）",
                        color = TextDim, fontSize = 10.sp)
                }
                GlassSwitch(st.adb) { vm.setAdb(it) }
            }
        }

        GlassCard(haze) {
            SectionTitle("④", "开始")
            val sel = st.passes.toMutableSet()
            if (!st.adb) sel.remove("anti_debugging")
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("${sel.size} 个 pass", color = TextSub, fontSize = 11.sp)
                Text("·", color = TextDim, fontSize = 11.sp)
                Text(sel.joinToString(" / ") { PassCatalog.labelOf(it) }
                    .take(40) + if (sel.size > 3) "…" else "",
                    color = TextDim, fontSize = 11.sp)
            }
            // 预检结论：一眼看见有没有会把这次构建搞崩的文件
            val risks = remember(st.files) { Precheck.scan(st.files) }
            val nb = risks.count { it.level == Risk.Level.BLOCK }
            val nh = risks.count { it.level == Risk.Level.HIGH }
            if (st.files.isNotEmpty()) {
                Spacer(Modifier.height(6.dp))
                Text(
                    if (nb > 0) "⚠ 预检：$nb 个必崩文件（点开始会被拦下）· 去「预检」页看清单"
                    else if (nh > 0) "⚠ 预检：$nh 个高风险（能编，可能慢）· 去「预检」页看清单"
                    else "✓ 预检通过：没有会崩的文件",
                    color = if (nb > 0) Color(0xFFFF8A8A)
                    else if (nh > 0) Color(0xFFFFC68A) else Color(0xFF3DDC97),
                    fontSize = 11.sp)
            }
            Spacer(Modifier.height(10.dp))
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
            }
            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                PrimaryButton(
                    if (st.phase == Phase.RUNNING) "编译中…" else "开始编译",
                    st.phase != Phase.RUNNING
                ) { vm.start(ctx, prefs) }
                if (st.phase == Phase.RUNNING) {
                    Box(Modifier.height(46.dp).clip(RoundedCornerShape(14.dp))
                        .background(Color.White.copy(alpha = 0.10f))
                        .clickable { vm.cancel() }.padding(horizontal = 16.dp),
                        contentAlignment = Alignment.Center
                    ) { Text("取消", color = TextMain, fontSize = 14.sp) }
                }
            }
            if (st.phase == Phase.RUNNING) {
                Spacer(Modifier.height(8.dp))
                Text("⏱ 已用 ${fmt(st.elapsed)} · 预计还剩 ${fmt(st.remain)}（总估 ${fmt(st.eta)}）",
                    color = TextSub, fontSize = 11.sp)
            }
            if (st.phase == Phase.DONE) {
                Spacer(Modifier.height(8.dp))
                Text("✅ 完成，用时 ${fmt(st.elapsed)}" +
                    (st.savedName?.let { " · 已存 Download/NKBuild/$it" } ?: ""),
                    color = Color(0xFF3DDC97), fontSize = 11.sp)
            }
            st.error?.let {
                Spacer(Modifier.height(8.dp))
                Text(it, color = Color(0xFFFF6B81), fontSize = 11.sp)
            }
        }

        GlassCard(haze) {
            SectionTitle("⑤", "日志")
            Column(
                Modifier.fillMaxWidth().heightIn(min = 120.dp, max = 260.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(Color.Black.copy(alpha = 0.35f))
                    .padding(10.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                Text(
                    st.logs.ifEmpty { listOf("日志：上传 → 触发 → 编译 → 下载 → 存 Download") }
                        .joinToString("\n"),
                    color = TextSub, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }
        Spacer(Modifier.height(96.dp))
    }
}

/* ------------------------------------------------------------- ② 混淆页 */

@Composable
private fun ObfPage(vm: BuildViewModel, st: UiState, haze: HazeState?) {
    var showToml by remember { mutableStateOf(false) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        GlassCard(haze) {
            SectionTitle("◈", "预设")
            GlassSegmented(listOf("满血", "标准", "轻量"),
                when {
                    st.passes == PassCatalog.FULL -> 0
                    st.passes == PassCatalog.DEFAULTS -> 1
                    st.passes == PassCatalog.LIGHT -> 2
                    else -> 1
                }
            ) { idx ->
                vm.applyPreset(
                    when (idx) {
                        0 -> PassCatalog.FULL
                        2 -> PassCatalog.LIGHT
                        else -> PassCatalog.DEFAULTS
                    })
            }
            Spacer(Modifier.height(8.dp))
            Text("满血=${PassCatalog.FULL.size} 个 pass（含未验证项，易崩）\n" +
                "标准=${PassCatalog.DEFAULTS.size} 个（推荐）· 轻量=${PassCatalog.LIGHT.size} 个（快）",
                color = TextDim, fontSize = 10.sp)
        }

        GlassCard(haze) {
            SectionTitle("核心", "必开项")
            PassCatalog.byGroup(0).forEach { p ->
                PassRow(p, st.passes.contains(p.key)) { vm.togglePass(p.key) }
            }
        }

        GlassCard(haze) {
            SectionTitle("进阶", "实验项（默认关）")
            PassCatalog.byGroup(1).forEach { p ->
                PassRow(p, st.passes.contains(p.key)) { vm.togglePass(p.key) }
            }
        }

        GlassCard(haze) {
            SectionTitle("禁用", "AArch64 不可用 / 已知崩")
            PassCatalog.byGroup(2).forEach { p ->
                PassRow(p, false) { }
            }
        }

        GlassCard(haze) {
            Row(verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.clickable { showToml = !showToml }) {
                SectionTitle("", "本地生成的 TOML")
                Spacer(Modifier.weight(1f))
                Text(if (showToml) "收起" else "展开", color = Accent2, fontSize = 11.sp)
            }
            Text("提交到仓库 ci/ui-<工程名>.toml 后随 dispatch 一起生效",
                color = TextDim, fontSize = 10.sp)
            if (showToml) {
                Spacer(Modifier.height(8.dp))
                Column(
                    Modifier.fillMaxWidth().heightIn(max = 420.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color.Black.copy(alpha = 0.40f))
                        .padding(10.dp)
                        .verticalScroll(rememberScrollState())
                ) {
                    Text(vm.tomlText(), color = TextSub, fontSize = 9.sp,
                        fontFamily = FontFamily.Monospace)
                }
            }
        }
        Spacer(Modifier.height(96.dp))
    }
}

@Composable
private fun PassRow(p: PassDef, on: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable(enabled = p.selectable) { onClick() }
            .padding(horizontal = 4.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            Modifier.size(22.dp).clip(RoundedCornerShape(7.dp))
                .background(if (on) Accent else Color.White.copy(alpha = 0.10f))
                .border(1.dp,
                    if (on) Color.Transparent else Color.White.copy(alpha = 0.22f),
                    RoundedCornerShape(7.dp)),
            contentAlignment = Alignment.Center
        ) { if (on) Text("✓", color = Color.White, fontSize = 12.sp) }
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text(
                p.label + if (!p.selectable) "（不可用）" else "",
                color = if (p.selectable) TextMain else TextDim,
                fontSize = 13.sp,
                fontWeight = if (on) FontWeight.Bold else FontWeight.Normal)
            Spacer(Modifier.height(2.dp))
            Text(p.desc, color = TextDim, fontSize = 10.sp)
        }
        Text(if (on) "ON" else "—",
            color = if (on) Color(0xFF3DDC97) else TextDim, fontSize = 10.sp)
    }
}

/* ------------------------------------------------------------- ③ 产物页 */

@Composable
private fun OutPage(vm: BuildViewModel, st: UiState, haze: HazeState?) {
    val ctx = LocalContext.current
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        GlassCard(haze) {
            SectionTitle("▣", "本次产物")
            if (st.savedName == null && st.artifactPath == null) {
                Text("还没有产物。去「构建」页跑一轮。", color = TextDim, fontSize = 12.sp)
            } else {
                Text(st.savedName ?: st.artifactPath!!.substringAfterLast('/'),
                    color = TextMain, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(4.dp))
                val size = st.artifactPath?.let { java.io.File(it).length() } ?: 0L
                Text("${size / 1024} KB", color = TextDim, fontSize = 11.sp)
                Spacer(Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    PrimaryButton("安装 / 打开", true) { vm.openOrInstall(ctx) }
                    Box(Modifier.height(46.dp).clip(RoundedCornerShape(14.dp))
                        .background(Color.White.copy(alpha = 0.10f))
                        .clickable {
                            st.artifactPath?.let { p ->
                                vm.exportToDownloads(ctx, java.io.File(p),
                                    java.io.File(p).name)
                            }
                        }.padding(horizontal = 16.dp),
                        contentAlignment = Alignment.Center
                    ) { Text("再存一份", color = TextMain, fontSize = 13.sp) }
                }
                Spacer(Modifier.height(8.dp))
                Text("公共目录：Download/NKBuild/", color = TextDim, fontSize = 10.sp)
            }
        }

        GlassCard(haze) {
            SectionTitle("", "运行信息")
            Text(
                "状态：${st.statusText}\n" +
                    "已用：${fmt(st.elapsed)}" +
                    (if (st.phase == Phase.RUNNING) "（还剩约 ${fmt(st.remain)}）" else "") + "\n" +
                    "产物路径：${st.artifactPath ?: "—"}",
                color = TextSub, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
            st.runUrl?.let { u ->
                Spacer(Modifier.height(8.dp))
                Text("查看运行日志 ↗", color = Accent2, fontSize = 12.sp,
                    modifier = Modifier.clickable {
                        runCatching {
                            ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(u))
                                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                        }
                    })
            }
        }

        GlassCard(haze) {
            SectionTitle("", "日志尾巴")
            Column(
                Modifier.fillMaxWidth().heightIn(min = 160.dp, max = 320.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(Color.Black.copy(alpha = 0.35f))
                    .padding(10.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                Text(st.logs.takeLast(60).joinToString("\n").ifEmpty { "—" },
                    color = TextSub, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }
        Spacer(Modifier.height(96.dp))
    }
}

/* ------------------------------------------------------------- ④ 设置页 */

@Composable
private fun SetPage(
    prefs: Prefs,
    blurOn: Boolean,
    fishOn: Boolean,
    haze: HazeState?,
    onBlur: (Boolean) -> Unit,
    onFish: (Boolean) -> Unit,
) {
    var token by remember { mutableStateOf(prefs.token) }
    var owner by remember { mutableStateOf(prefs.owner) }
    var repo by remember { mutableStateOf(prefs.repo) }
    var branch by remember { mutableStateOf(prefs.branch) }
    var wf by remember { mutableStateOf(prefs.workflow) }
    var autoExport by remember { mutableStateOf(prefs.autoExport) }
    var savedTip by remember { mutableStateOf("") }

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        GlassCard(haze) {
            SectionTitle("⚙", "仓库")
            Field("GitHub Token", token) { token = it }
            Field("用户名 / 组织", owner) { owner = it }
            Field("仓库", repo) { repo = it }
            Field("分支", branch) { branch = it }
            Field("工作流文件", wf) { wf = it }
            Text("Token 只存本机 SharedPreferences，不联网外发",
                color = TextDim, fontSize = 10.sp)
            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalAlignment = Alignment.CenterVertically) {
                PrimaryButton("保存", true) {
                    prefs.token = token.trim()
                    prefs.owner = owner.trim()
                    prefs.repo = repo.trim()
                    prefs.branch = branch.trim().ifBlank { "main" }
                    prefs.workflow = wf.trim().ifBlank { "build-obf.yml" }
                    savedTip = "已保存"
                }
                if (savedTip.isNotEmpty()) {
                    Text(savedTip, color = Color(0xFF3DDC97), fontSize = 12.sp)
                }
            }
        }

        GlassCard(haze) {
            SectionTitle("", "行为")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("产物自动存 Download/NKBuild", color = TextMain, fontSize = 13.sp)
                    Spacer(Modifier.height(4.dp))
                    Text("API29+ 走 MediaStore，不需要存储权限",
                        color = TextDim, fontSize = 10.sp)
                }
                GlassSwitch(autoExport) { autoExport = it; prefs.autoExport = it }
            }
            Spacer(Modifier.height(10.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("液态玻璃模糊（RenderEffect）", color = TextMain, fontSize = 13.sp)
                    Spacer(Modifier.height(4.dp))
                    Text("vivo/OPPO/一加/realme 部分机型 GPU 会崩，默认已关",
                        color = TextDim, fontSize = 10.sp)
                }
                GlassSwitch(blurOn) { onBlur(it) }
            }
            Spacer(Modifier.height(10.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("会游的蓝色大肥鱼背景", color = TextMain, fontSize = 13.sp)
                    Spacer(Modifier.height(4.dp))
                    Text("矢量绘制 + 摆尾/吐泡动画，0 素材体积", color = TextDim, fontSize = 10.sp)
                }
                GlassSwitch(fishOn) { onFish(it) }
            }
        }

        GlassCard(haze) {
            SectionTitle("", "关于")
            Text("NKBuild 手机版 v1.2（versionCode 3）\n" +
                "Ensia = LLVM-21 Pass 插件（IR 级），配合 nk_ad 运行时看门狗\n" +
                "构建：GitHub Actions · 产物：AArch64 PIE",
                color = TextSub, fontSize = 11.sp)
        }
        Spacer(Modifier.height(96.dp))
    }
}

/* ------------------------------------------------------- 液态玻璃导航条 */

@Composable
private fun LiquidNav(
    haze: HazeState?,
    selected: Tab,
    onSelect: (Tab) -> Unit,
    modifier: Modifier = Modifier,
) {
    val shape = RoundedCornerShape(28.dp)
    var m = modifier.fillMaxWidth().height(68.dp).clip(shape)
    if (haze != null) m = m.hazeEffect(state = haze)

    Box(
        m.background(Brush.verticalGradient(
            listOf(Color.White.copy(alpha = 0.22f), Color.White.copy(alpha = 0.09f))))
            .border(1.dp, Brush.verticalGradient(
                listOf(Color.White.copy(alpha = 0.40f), Color.White.copy(alpha = 0.10f))), shape)
            .padding(6.dp)
    ) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val w = maxWidth
            val idx = TABS.indexOf(selected).coerceAtLeast(0)
            val off by animateDpAsState(
                targetValue = w / TABS.size * idx,
                animationSpec = spring(dampingRatio = Spring.DampingRatioLowBouncy,
                    stiffness = Spring.StiffnessMediumLow), label = "pill")
            Box(
                Modifier.offset(x = off).width(w / TABS.size).fillMaxHeight()
                    .clip(RoundedCornerShape(22.dp))
                    .background(Brush.verticalGradient(
                        listOf(Color.White.copy(alpha = 0.30f),
                            Color.White.copy(alpha = 0.12f))))
                    .border(1.dp, Color.White.copy(alpha = 0.38f), RoundedCornerShape(22.dp))
            )
            Row(Modifier.fillMaxSize()) {
                TABS.forEach { t ->
                    val on = t == selected
                    Column(
                        Modifier.weight(1f).fillMaxHeight().clickable { onSelect(t) },
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        Text(t.glyph, color = if (on) Color.White else TextSub, fontSize = 15.sp)
                        Spacer(Modifier.height(1.dp))
                        Text(t.label, color = if (on) Color.White else TextSub,
                            fontSize = 10.sp,
                            fontWeight = if (on) FontWeight.Bold else FontWeight.Normal)
                    }
                }
            }
        }
    }
}

/* --------------------------------------------------------------- 小组件 */

@Composable
private fun Field(label: String, value: String, onChange: (String) -> Unit) {
    Text(label, color = TextDim, fontSize = 11.sp)
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

/* --------------------------------------------------- 背景：会游的蓝色大肥鱼 */

/**
 * 纯 Canvas 画的蓝色大肥鱼：横向慢慢游 + 上下浮动 + 摆尾 + 眨眼 + 吐泡泡。
 * 不用任何图片素材（0 KB），任意分辨率都清晰，也不会被 aapt 压缩坏。
 */
@Composable
private fun FishBackground(haze: HazeState?) {
    val tr = rememberInfiniteTransition(label = "fish")

    // 主鱼：一次完整的横向穿越 26 秒
    val swim by tr.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(26000, easing = LinearEasing),
            RepeatMode.Restart), label = "swim")
    // 上下浮动
    val bob by tr.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(3600, easing = LinearEasing),
            RepeatMode.Reverse), label = "bob")
    // 摆尾（快一点）
    val tail by tr.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(620, easing = LinearEasing),
            RepeatMode.Reverse), label = "tail")
    // 眨眼：周期 5.4 秒，只有最后 6% 是闭眼
    val blinkT by tr.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(5400, easing = LinearEasing),
            RepeatMode.Restart), label = "blink")
    // 小鱼苗更快，相位错开
    val swim2 by tr.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(15000, easing = LinearEasing),
            RepeatMode.Restart), label = "swim2")

    Canvas(
        if (haze != null) Modifier.fillMaxSize().hazeSource(state = haze)
        else Modifier.fillMaxSize()
    ) {
        val w = size.width
        val h = size.height
        val blink = if (blinkT > 0.94f) abs(0.94f - blinkT) / 0.06f else 1f

        // 主鱼：从右往左游（往右游更自然，这里用 0→1 从左边进）
        val fw = w * 0.52f
        val fx = -fw * 0.9f + (w + fw * 1.8f) * swim
        val fy = h * 0.68f + sin(bob * 2f * PI).toFloat() * h * 0.028f
        drawFatFish(fx, fy, fw, tail, blink, 0.42f)

        // 两条小鱼苗（远近感：更小、更淡、更快）
        val sw = w * 0.20f
        val sx = -sw + (w + sw * 2f) * swim2
        val sy = h * 0.30f + sin((bob * 1.7f) * 2f * PI).toFloat() * h * 0.02f
        drawFatFish(sx, sy, sw, (tail + 0.5f) % 1f, 1f, 0.22f)

        val sx2 = w + sw * 0.5f - ((w + sw * 2f) * swim2)
        drawFatFish(sx2, h * 0.86f + sin((bob * 2.3f) * 2f * PI).toFloat() * h * 0.02f,
            sw * 0.7f, tail, 1f, 0.16f)
    }
}

/** 一条胖鱼：椭圆身体 + 三角尾 + 背鳍 + 眼睛 + 嘴 + 吐泡泡。 */
private fun DrawScope.drawFatFish(
    cx: Float,
    cy: Float,
    w: Float,
    tailPhase: Float,
    blink: Float,
    alpha: Float,
) {
    val bodyTop = cy - w * 0.29f
    val bodyH = w * 0.58f
    val left = cx - w * 0.5f
    val bodyBrush = Brush.linearGradient(
        colors = listOf(Color(0xFF6FC0FF), Color(0xFF2E7BEE), Color(0xFF1B4FC4)),
        start = Offset(left, bodyTop),
        end = Offset(left + w, bodyTop + bodyH))
    val edge = Color(0xFF0E2F80).copy(alpha = alpha * 0.9f)

    // 尾巴（左右摆动）
    val wig = sin(tailPhase * 2f * PI).toFloat() * w * 0.10f
    val tailPath = Path().apply {
        moveTo(left + w * 0.06f, cy)
        lineTo(left - w * 0.26f, cy - w * 0.24f + wig)
        lineTo(left - w * 0.18f, cy)
        lineTo(left - w * 0.26f, cy + w * 0.24f + wig)
        close()
    }
    drawPath(tailPath, Color(0xFFFFB347).copy(alpha = alpha * 1.5f))
    drawPath(tailPath, edge, style = Stroke(width = w * 0.012f))

    // 背鳍
    val finPath = Path().apply {
        moveTo(cx - w * 0.10f, bodyTop + bodyH * 0.06f)
        quadTo(cx + w * 0.02f, bodyTop - w * 0.16f + wig * 0.4f,
            cx + w * 0.16f, bodyTop + bodyH * 0.10f)
        close()
    }
    drawPath(finPath, Color(0xFFFFCF6B).copy(alpha = alpha * 1.4f))

    // 身体
    drawOval(bodyBrush, topLeft = Offset(left, bodyTop), size = Size(w, bodyH))
    drawOval(edge.copy(alpha = alpha), topLeft = Offset(left, bodyTop),
        size = Size(w, bodyH), style = Stroke(width = w * 0.012f))

    // 肚皮高光
    drawOval(Color.White.copy(alpha = alpha * 0.35f),
        topLeft = Offset(left + w * 0.10f, bodyTop + bodyH * 0.52f),
        size = Size(w * 0.55f, bodyH * 0.32f))

    // 眼睛（blink = 0 时闭眼）
    val eyeX = cx + w * 0.24f
    val eyeY = cy - w * 0.07f
    val r = w * 0.085f
    drawCircle(Color.White.copy(alpha = alpha * 2.0f), r, Offset(eyeX, eyeY))
    if (blink > 0.15f) {
        drawCircle(Color(0xFF10205A).copy(alpha = alpha * 2.2f), r * 0.55f, Offset(eyeX, eyeY))
        drawCircle(Color.White.copy(alpha = alpha * 3f), r * 0.20f,
            Offset(eyeX - r * 0.22f, eyeY - r * 0.24f))
    } else {
        drawLine(edge, Offset(eyeX - r, eyeY), Offset(eyeX + r, eyeY),
            strokeWidth = w * 0.016f)
    }

    // 嘴 + 腮红
    drawArc(edge, startAngle = 20f, sweepAngle = 120f, useCenter = false,
        topLeft = Offset(cx + w * 0.34f, cy - w * 0.02f),
        size = Size(w * 0.12f, w * 0.10f), style = Stroke(width = w * 0.014f))
    drawCircle(Color(0xFFFF8FB0).copy(alpha = alpha * 0.9f), w * 0.045f,
        Offset(cx + w * 0.10f, cy + w * 0.10f))

    // 泡泡
    for (i in 0..2) {
        val p = ((tailPhase + i * 0.33f) % 1f)
        val br = w * (0.02f + 0.012f * i)
        drawCircle(Color.White.copy(alpha = alpha * 0.5f * (1f - p)), br,
            Offset(cx + w * 0.44f + w * 0.05f * i, cy - w * 0.05f - p * w * 0.45f),
            style = Stroke(width = w * 0.006f))
    }
}

/* --------------------------------------------------------------- 预检菜单 */

@Composable
private fun PrecheckPage(st: UiState, haze: HazeState?) {
    val risks = remember(st.files) { Precheck.scan(st.files) }
    val blocks = risks.count { it.level == Risk.Level.BLOCK }
    val highs = risks.count { it.level == Risk.Level.HIGH }
    val notes = risks.count { it.level == Risk.Level.NOTE }

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        GlassCard(haze) {
            SectionTitle("◎", "上传预检")
            Text(
                if (st.files.isEmpty()) "还没选源码：先在「构建」里选一次项目目录"
                else "${st.files.size} 个文件、共 ${Precheck.human(st.files.sumOf { it.size })}"
                        + " · ${st.files.count { it.rel.substringAfterLast('.', "").lowercase() in Precheck.compileExt }} 个编译单元",
                color = TextMain, fontSize = 13.sp)

            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                CountBadge("必崩", blocks, Color(0xFFFF5D5D))
                CountBadge("很可能崩", highs, Color(0xFFFFB04D))
                CountBadge("注意", notes, Color(0xFF6FB3FF))
            }
            Spacer(Modifier.height(10.dp))
            Text(
                if (blocks > 0) "① 有 $blocks 个必崩项：点「开始编译」会被直接拦下"
                else if (highs > 0) "② 没有必崩项，但有 $highs 个高风险：能跑，可能慢或超时"
                else "③ 没发现问题，可以放心编",
                color = if (blocks > 0) Color(0xFFFF8A8A)
                else if (highs > 0) Color(0xFFFFC68A) else Color(0xFF3DDC97),
                fontSize = 12.sp)
        }

        if (risks.isEmpty()) {
            GlassCard(haze) {
                SectionTitle("✓", "全部通过")
                Text("没有超过 1 MB 的文件、没有带空格的文件名、没有会用不上的大素材。",
                    color = TextDim, fontSize = 12.sp)
            }
        } else {
            GlassCard(haze) {
                SectionTitle("!", "逐条清单")
                risks.forEach { r ->
                    RiskRow(r)
                    Spacer(Modifier.height(10.dp))
                }
            }
        }

        GlassCard(haze) {
            SectionTitle("i", "判定规则")
            RuleLine("> 70 MB", "必崩 —— GitHub 单文件上限 100 MB，base64 后超了，会被跳过")
            RuleLine("> 45 MB", "很可能崩 —— 请求体太大，大概率上传超时")
            RuleLine("> 1 MB", "注意 —— 走 blob 接口，能传但慢")
            RuleLine("文件名带空格/中文", "必崩（编译单元）—— CI 按空格切 SRC_FILES")
            RuleLine("图片/字体/文档", "注意 —— 编译用不上，只拖时间")
            RuleLine("> 60 个编译单元", "很可能崩 —— 一次提交文件太多容易撞限流")
        }
    }
}

@Composable
private fun CountBadge(label: String, n: Int, color: Color) {
    Row(
        Modifier.background(color.copy(alpha = 0.14f), RoundedCornerShape(10.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text("$n", color = color, fontSize = 15.sp, fontWeight = FontWeight.Black)
        Spacer(Modifier.width(6.dp))
        Text(label, color = color.copy(alpha = 0.85f), fontSize = 11.sp)
    }
}

@Composable
private fun RiskRow(r: Risk) {
    val c = when (r.level) {
        Risk.Level.BLOCK -> Color(0xFFFF5D5D)
        Risk.Level.HIGH -> Color(0xFFFFB04D)
        Risk.Level.NOTE -> Color(0xFF6FB3FF)
    }
    val tag = when (r.level) {
        Risk.Level.BLOCK -> "必崩"
        Risk.Level.HIGH -> "很可能崩"
        Risk.Level.NOTE -> "注意"
    }
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
        Box(
            Modifier.padding(top = 2.dp)
                .background(c.copy(alpha = 0.16f), RoundedCornerShape(6.dp))
                .padding(horizontal = 6.dp, vertical = 2.dp)
        ) { Text(tag, color = c, fontSize = 10.sp, fontWeight = FontWeight.Bold) }
        Spacer(Modifier.width(8.dp))
        Column(Modifier.weight(1f)) {
            Text(r.rel, color = TextMain, fontSize = 12.sp)
            Spacer(Modifier.height(3.dp))
            Text("${Precheck.human(r.size)} · ${r.reason}", color = TextDim, fontSize = 10.sp)
        }
    }
}

@Composable
private fun RuleLine(k: String, v: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.Top) {
        Text(k, color = Accent2, fontSize = 11.sp, modifier = Modifier.width(112.dp))
        Text(v, color = TextDim, fontSize = 11.sp, modifier = Modifier.weight(1f))
    }
}
