package com.nk.nkbuild

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.Spring
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.chrisbanes.haze.HazeState
import dev.chrisbanes.haze.hazeEffect
import dev.chrisbanes.haze.hazeSource

val Accent = Color(0xFF7C5CFF)
val Accent2 = Color(0xFFB39BFF)
val TextMain = Color(0xFFF2F4FF)
val TextSub = Color(0xFFA6ADCF)
val TextDim = Color(0xFF6C7396)

/** 真·液态玻璃卡片：hazeEffect 在 API31+ 用 RenderEffect 实时模糊背景 */
@Composable
fun GlassCard(
    haze: HazeState?,
    modifier: Modifier = Modifier,
    corner: Int = 22,
    content: @Composable ColumnScope.() -> Unit,
) {
    var m = modifier.clip(RoundedCornerShape(corner.dp))
    if (haze != null) m = m.hazeEffect(state = haze)
    Column(
        m
            .background(
                Brush.verticalGradient(
                    listOf(Color.White.copy(alpha = 0.16f), Color.White.copy(alpha = 0.06f))
                )
            )
            .border(
                width = 1.dp,
                brush = Brush.verticalGradient(
                    listOf(Color.White.copy(alpha = 0.30f), Color.White.copy(alpha = 0.08f))
                ),
                shape = RoundedCornerShape(corner.dp)
            )
            .padding(16.dp),
        content = content
    )
}

@Composable
fun SectionTitle(num: String, title: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        if (num.isNotEmpty()) {
            Text(num, color = TextMain, fontWeight = FontWeight.Bold, fontSize = 13.sp)
            Spacer(Modifier.width(6.dp))
        }
        Text(title, color = TextSub, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
    }
    Spacer(Modifier.height(10.dp))
}

/** 分段按钮：滑块用 spring 弹过去 */
@Composable
fun GlassSegmented(
    values: List<String>,
    index: Int,
    onSelect: (Int) -> Unit,
) {
    val shape = RoundedCornerShape(14.dp)
    Box(
        Modifier
            .fillMaxWidth()
            .height(44.dp)
            .clip(shape)
            .background(Color.Black.copy(alpha = 0.30f))
            .border(1.dp, Color.White.copy(alpha = 0.10f), shape)
            .padding(4.dp)
    ) {
        val target = index * (1f / values.size)
        val off by animateFloatAsState(
            targetValue = target,
            animationSpec = spring(dampingRatio = Spring.DampingRatioLowBouncy,
                stiffness = Spring.StiffnessMediumLow),
            label = "thumb"
        )
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val w = maxWidth
            Box(
                Modifier
                    .offset(x = w * off)
                    .width(w / values.size)
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(11.dp))
                    .background(Brush.horizontalGradient(listOf(Color(0xFF8B6BFF), Color(0xFF6A48FF))))
            )
        }
        Row(Modifier.fillMaxSize()) {
            values.forEachIndexed { i, v ->
                Box(Modifier.weight(1f).fillMaxHeight().clickable { onSelect(i) },
                    contentAlignment = Alignment.Center) {
                    Text(
                        v,
                        color = if (i == index) Color.White else TextSub,
                        fontSize = 13.sp,
                        fontWeight = if (i == index) FontWeight.Bold else FontWeight.Normal
                    )
                }
            }
        }
    }
}

/** 玻璃开关 */
@Composable
fun GlassSwitch(on: Boolean, onChange: (Boolean) -> Unit) {
    val pos by animateFloatAsState(
        if (on) 1f else 0f,
        spring(dampingRatio = Spring.DampingRatioLowBouncy, stiffness = Spring.StiffnessMediumLow),
        label = "knob"
    )
    val bg by animateColorAsState(
        if (on) Accent else Color.White.copy(alpha = 0.16f), label = "bg")
    Box(
        Modifier
            .width(54.dp).height(30.dp)
            .clip(RoundedCornerShape(15.dp))
            .background(bg)
            .clickable { onChange(!on) }
            .padding(3.dp)
    ) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val d = maxHeight
            Box(
                Modifier
                    .offset(x = (maxWidth - d) * pos)
                    .size(d)
                    .clip(RoundedCornerShape(d / 2))
                    .background(Color.White)
            )
        }
    }
}

/** 主按钮：按下缩、悬停弹（用 spring 近似） */
@Composable
fun PrimaryButton(text: String, enabled: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
    var pressed by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(
        if (pressed) 0.96f else 1f,
        spring(dampingRatio = Spring.DampingRatioMediumBouncy), label = "press")
    Box(
        modifier
            .scale(scale)
            .height(46.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(
                if (enabled) Brush.horizontalGradient(listOf(Color(0xFF8B6BFF), Color(0xFF6A48FF)))
                else Brush.horizontalGradient(listOf(Color(0xFF3A3F5C), Color(0xFF2A2E45)))
            )
            .clickable(enabled = enabled) {
                pressed = true; onClick()
            }
            .padding(horizontal = 18.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 14.sp)
    }
    LaunchedEffect(pressed) {
        if (pressed) {
            kotlinx.coroutines.delay(120)
            pressed = false
        }
    }
}
