package com.nk.nkbuild

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.Crossfade
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
import androidx.compose.foundation.Image
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
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
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
            MaterialTheme(colorScheme = lightColorScheme(
                primary = Color(0xFFD2557F), secondary = Color(0xFFF08FB4),
                background = Color(0xFFF6EFF4), surface = Color(0xFFFFFFFF),
                onPrimary = Color.White, onBackground = Color(0xFF2B2230),
                onSurface = Color(0xFF2B2230))) {
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

    LaunchedEffect(Unit) {
        vm.restore(prefs)
        // 上次切后台/被杀掉的任务：按 run id 接上
        vm.resume(ctx, prefs)
    }

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

    Box(Modifier.fillMaxSize().background(Color(0xFFF6EFF4))) {
        // ---- 皮肤背景：每切一页换一张（素材取自 KernelSU 那套粉色皮肤）----
        SkinBackground(tab, prefs.skin, haze)

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
                    Tab.SET -> SetPage(prefs, blurOn, prefs.skin, haze,
                        onBlur = { on -> blurOn = on; prefs.blur = on },
                        onSkin = { on -> prefs.skin = on })
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
        Mascot()
        Spacer(Modifier.width(10.dp))
        Column {
            Text("NKBUILD", color = TextMain, fontWeight = FontWeight.Black,
                fontSize = 20.sp, letterSpacing = 1.5.sp)
            Text("源码 → 云端混淆", color = TextSub, fontSize = 10.sp)
        }
        Spacer(Modifier.weight(1f))
        Box(
            Modifier.clip(RoundedCornerShape(14.dp))
                .background(Color(0xCCFFFFFF))
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
                    .background(Color(0x22000000))) {
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
                        .background(Color(0x1F000000))
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
                .background(if (on) Accent else Color(0xFFE2DCE6))
                .border(1.dp,
                    if (on) Color.Transparent else Color(0xFFC9C2CE),
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
                        .background(Color(0x14000000))
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
    skinOn: Boolean,
    haze: HazeState?,
    onBlur: (Boolean) -> Unit,
    onSkin: (Boolean) -> Unit,
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
                    Text("皮肤背景（每页一张）", color = TextMain, fontSize = 13.sp)
                    Spacer(Modifier.height(4.dp))
                    Text("5 张开源皮肤图，切页交叉淡入", color = TextDim, fontSize = 10.sp)
                }
                GlassSwitch(skinOn) { onSkin(it) }
            }
        }

        GlassCard(haze) {
            SectionTitle("", "关于")
            Text("NKBuild 手机版 v1.4（versionCode 6）\n" +
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
        m.background(
            Brush.verticalGradient(listOf(Color(0xF2FFFFFF), Color(0xE8FFF7FB)))
        )
            .border(1.dp, Color(0x40FFFFFF), shape)
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
                    .background(Brush.verticalGradient(listOf(Accent2, Accent)))
                    .border(1.dp, Color(0x66FFFFFF), RoundedCornerShape(22.dp))
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
            .background(Color(0x14000000))
            .padding(horizontal = 10.dp, vertical = 10.dp),
        cursorBrush = Brush.verticalGradient(listOf(Accent, Accent))
    )
    Spacer(Modifier.height(10.dp))
}

/* ------------------------------------------------------------ 皮肤背景 */

/** 每个页面一张图，切页交叉淡入。素材：KernelSU 那套粉色皮肤（开源） */
@Composable
private fun SkinBackground(tab: Tab, on: Boolean, haze: HazeState?) {
    val src = Modifier.fillMaxSize().let { if (haze != null) it.hazeSource(haze) else it }
    Box(Modifier.fillMaxSize()) {
        if (on) {
            Crossfade(targetState = tab, animationSpec = tween(420), label = "skin") { t ->
                Image(
                    painter = painterResource(
                        when (t) {
                            Tab.BUILD -> R.drawable.bg_home
                            Tab.OBF -> R.drawable.bg_kernel
                            Tab.PRE -> R.drawable.bg_settings
                            Tab.OUT -> R.drawable.bg_module
                            Tab.SET -> R.drawable.bg_superuser
                        }
                    ),
                    contentDescription = null,
                    modifier = src,
                    contentScale = ContentScale.Crop
                )
            }
        } else {
            Box(src.background(Brush.verticalGradient(
                listOf(Color(0xFFF8EFF5), Color(0xFFEFE7F3)))))
        }
        // 薄白纱：保证任何一张图上卡片和文字都清楚
        Box(Modifier.fillMaxSize().background(Color(0x4DFFFFFF)))
    }
}

/** 小圆头像（卡通形象），放在顶栏标题左边 */
@Composable
private fun Mascot(size: Int = 38) {
    Box(
        Modifier.size(size.dp).clip(RoundedCornerShape((size / 2).dp))
            .border(1.5.dp, Color(0x66FFFFFF), RoundedCornerShape((size / 2).dp))
    ) {
        Image(
            painter = painterResource(R.drawable.mascot),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Crop
        )
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
            if (st.files.isEmpty()) {
                Text("还没选源码：先去「构建」页选一次项目目录，回来这里就能看到逐条风险清单。",
                    color = TextMain, fontSize = 13.sp)
                Spacer(Modifier.height(8.dp))
                Text("判定规则在下面，可以先看一眼什么样的文件会把这次构建搞崩。",
                    color = TextDim, fontSize = 11.sp)
            } else {
                Text("${st.files.size} 个文件、共 ${Precheck.human(st.files.sumOf { it.size })}"
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
        }

        if (st.files.isNotEmpty()) {
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
