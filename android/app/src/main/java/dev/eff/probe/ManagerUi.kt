package dev.eff.probe

import android.app.Activity
import android.os.Build
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowInsetsControllerCompat
import kotlinx.coroutines.launch
import top.yukonga.miuix.kmp.basic.*
import top.yukonga.miuix.kmp.theme.ColorSchemeMode
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.ThemeController
import top.yukonga.miuix.kmp.utils.PressFeedbackType
import top.yukonga.miuix.kmp.utils.overScrollVertical
import top.yukonga.miuix.kmp.window.WindowDialog
import java.util.Locale

private val pages = listOf("主页" to Icons.Rounded.Home, "模块" to Icons.Rounded.Extension, "设置" to Icons.Rounded.Settings)
private val pageSpring = spring<Float>(dampingRatio = .88f, stiffness = 420f)

@OptIn(ExperimentalComposeUiApi::class)
@Composable
internal fun EffManager(
    state: ManagerState,
    themeMode: Int,
    busy: String?,
    failure: String?,
    deleteTarget: ModItem?,
    onTheme: (Int) -> Unit,
    onImport: () -> Unit,
    onStage: (String, Boolean) -> Unit,
    onApply: () -> Unit,
    onRestore: () -> Unit,
    onDelete: (ModItem) -> Unit,
    onConfirmDelete: () -> Unit,
    onDismissDelete: () -> Unit,
    onDismissError: () -> Unit,
    onControl: (ModControl, Double) -> Unit,
    onReset: (ModItem) -> Unit,
    onGame: (String) -> Unit,
) {
    val controller = remember(themeMode) { ThemeController(when (themeMode) { 1 -> ColorSchemeMode.Light; 2 -> ColorSchemeMode.Dark; else -> ColorSchemeMode.System }) }
    val dark = themeMode == 2 || (themeMode == 0 && isSystemInDarkTheme())
    val context = LocalContext.current
    SideEffect { (context as? Activity)?.window?.let { WindowInsetsControllerCompat(it, it.decorView).apply { isAppearanceLightStatusBars = !dark; isAppearanceLightNavigationBars = !dark } } }
    MiuixTheme(controller = controller) {
        val pager = rememberPagerState { pages.size }
        val scope = rememberCoroutineScope()
        val wide = LocalConfiguration.current.screenWidthDp >= 600
        val scroll = MiuixScrollBehavior()
        val rail = rememberNavigationRailState()
        val select: (Int) -> Unit = { page -> scope.launch { pager.animateScrollToPage(page, animationSpec = pageSpring) } }

        Row(Modifier.fillMaxSize().background(MiuixTheme.colorScheme.surface).semantics { testTagsAsResourceId = true }) {
            if (wide) {
                NavigationRail(state = rail, header = {
                    IconButton(onClick = { rail.toggle() }, modifier = Modifier.padding(top = 8.dp)) { Icon(Icons.Rounded.Menu, "展开导航") }
                }) {
                    pages.forEachIndexed { index, page -> NavigationRailItem(pager.targetPage == index, { select(index) }, page.second, page.first, Modifier.testTag("tab-$index")) }
                }
            }
            Scaffold(
                modifier = Modifier.weight(1f),
                topBar = {
                    TopAppBar(title = pages[pager.currentPage].first, scrollBehavior = scroll, actions = {
                        if (pager.currentPage == 1) TextButton("应用", onApply, enabled = busy == null && state.mods.isNotEmpty(), modifier = Modifier.testTag("apply-mods"))
                        Text("v${BuildConfig.VERSION_NAME}", fontSize = 12.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary, modifier = Modifier.padding(horizontal = 16.dp))
                    })
                },
                bottomBar = {
                    if (!wide) NavigationBar(showDivider = false) {
                        pages.forEachIndexed { index, page -> NavigationBarItem(pager.targetPage == index, { select(index) }, page.second, page.first, Modifier.weight(1f).testTag("tab-$index")) }
                    }
                },
                floatingActionButton = {
                    AnimatedVisibility(pager.currentPage == 1 && busy == null, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
                        FloatingActionButton(onImport, modifier = Modifier.padding(end = 8.dp, bottom = 12.dp).testTag("import-mod"), shape = RoundedCornerShape(20.dp)) {
                            Row(Modifier.padding(horizontal = 20.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Icon(Icons.Rounded.Add, null, tint = MiuixTheme.colorScheme.onPrimary)
                                Text("导入", color = MiuixTheme.colorScheme.onPrimary, fontWeight = FontWeight.Medium)
                            }
                        }
                    }
                },
            ) { padding ->
                HorizontalPager(pager, modifier = Modifier.fillMaxSize().padding(padding), beyondViewportPageCount = 1) { page ->
                    when (page) {
                        0 -> HomePage(state, dark, scroll, onGame, onRestore)
                        1 -> ModulesPage(state, scroll, onImport, onStage, onDelete, onControl, onReset)
                        else -> SettingsPage(themeMode, scroll, onTheme)
                    }
                }
            }
        }
        WindowDialog(show = deleteTarget != null, title = "删除模块？", summary = deleteTarget?.name, onDismissRequest = onDismissDelete) {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                TextButton("取消", onDismissDelete, Modifier.weight(1f))
                TextButton("删除", onConfirmDelete, Modifier.weight(1f), colors = ButtonDefaults.textButtonColorsPrimary())
            }
        }
        WindowDialog(show = failure != null, title = "操作失败", summary = failure, onDismissRequest = onDismissError) {
            TextButton("确定", onDismissError, Modifier.fillMaxWidth())
        }
        WindowDialog(show = busy != null, title = busy, onDismissRequest = null) {
            Box(Modifier.fillMaxWidth().padding(16.dp), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
        }
    }
}

@Composable
private fun HomePage(state: ManagerState, dark: Boolean, scroll: ScrollBehavior, onGame: (String) -> Unit, onRestore: () -> Unit) {
    val context = LocalContext.current
    val clients = remember(context) { listOf("com.gryphline.endfield.gp" to "国际服", "com.hypergryph.endfield" to "国服").filter { context.packageManager.getLaunchIntentForPackage(it.first) != null } }
    val connected = state.connected
    val accent = if (connected) Color(0xFF2FA35B) else MiuixTheme.colorScheme.primary
    val cardColor = if (connected) { if (dark) Color(0xFF193C29) else Color(0xFFDFFAE4) } else MiuixTheme.colorScheme.surfaceContainerHigh
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(Modifier.widthIn(max = 820.dp).fillMaxSize().nestedScroll(scroll.nestedScrollConnection).overScrollVertical().testTag("home-page"), contentPadding = PaddingValues(16.dp, 12.dp, 16.dp, 28.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            item {
                Card(Modifier.fillMaxWidth(), colors = CardDefaults.defaultColors(color = cardColor), insideMargin = PaddingValues(0.dp), pressFeedbackType = PressFeedbackType.Tilt, onClick = { clients.firstOrNull()?.let { onGame(it.first) } }) {
                    Box(Modifier.fillMaxWidth().height(148.dp)) {
                        Icon(if (connected) Icons.Rounded.CheckCircleOutline else Icons.Rounded.PowerSettingsNew, null, tint = if (connected) Color(0xFF36D167) else accent.copy(alpha = .22f), modifier = Modifier.size(120.dp).align(Alignment.BottomEnd).offset(20.dp, 24.dp))
                        Column(Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            AnimatedContent(if (connected) "工作中" else "等待游戏", transitionSpec = { fadeIn() togetherWith fadeOut() }, label = "connection") { title ->
                                Text(title, fontSize = 25.sp, fontWeight = FontWeight.SemiBold)
                            }
                            Text("EFF Android · v${BuildConfig.VERSION_NAME}", fontSize = 14.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary)
                            Text(if (connected) clients.firstOrNull { it.first == state.gamePackage }?.second ?: "已连接" else "未连接", fontSize = 13.sp, color = accent)
                        }
                    }
                }
            }
            item {
                Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                    CounterCard("模块", state.mods.size, Icons.Rounded.Extension, Modifier.weight(1f))
                    CounterCard("已启用", state.applied.size, Icons.Rounded.CheckCircleOutline, Modifier.weight(1f))
                }
            }
            item {
                Card(Modifier.fillMaxWidth(), insideMargin = PaddingValues(18.dp)) {
                    InfoRow("状态", state.status, Icons.Rounded.Tune)
                    HorizontalDivider(Modifier.padding(vertical = 12.dp))
                    InfoRow("系统", "Android ${Build.VERSION.RELEASE} · ARM64", Icons.Rounded.PhoneAndroid)
                }
            }
            item {
                Card(Modifier.fillMaxWidth(), insideMargin = PaddingValues(18.dp)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        if (clients.isEmpty()) Text("未检测到客户端", color = MiuixTheme.colorScheme.onSurfaceVariantSummary)
                        clients.forEach { client -> TextButton(if (clients.size == 1) "打开游戏" else client.second, { onGame(client.first) }, Modifier.weight(1f), colors = ButtonDefaults.textButtonColorsPrimary()) }
                    }
                    if (state.applied.isNotEmpty()) TextButton("恢复原版", onRestore, Modifier.fillMaxWidth().padding(top = 8.dp))
                }
            }
        }
    }
}

@Composable
private fun CounterCard(label: String, number: Int, icon: ImageVector, modifier: Modifier) {
    Card(modifier, insideMargin = PaddingValues(18.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(label, fontSize = 13.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary)
                AnimatedContent(number, transitionSpec = { fadeIn() togetherWith fadeOut() }, label = label) { value -> Text(value.toString(), fontSize = 28.sp, fontWeight = FontWeight.SemiBold) }
            }
            Icon(icon, null, tint = MiuixTheme.colorScheme.primary.copy(alpha = .7f), modifier = Modifier.size(28.dp))
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String, icon: ImageVector) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Icon(icon, null, modifier = Modifier.size(22.dp), tint = MiuixTheme.colorScheme.onSurfaceVariantSummary)
        Text(label, fontSize = 15.sp)
        Spacer(Modifier.weight(1f))
        Text(value, fontSize = 14.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary, maxLines = 2, overflow = TextOverflow.Ellipsis, modifier = Modifier.widthIn(max = 440.dp).weight(2f, fill = false))
    }
}

@Composable
private fun ModulesPage(state: ManagerState, scroll: ScrollBehavior, onImport: () -> Unit, onStage: (String, Boolean) -> Unit, onDelete: (ModItem) -> Unit, onControl: (ModControl, Double) -> Unit, onReset: (ModItem) -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        if (state.mods.isEmpty()) {
            Column(Modifier.align(Alignment.Center).padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(18.dp)) {
                Icon(Icons.Rounded.Extension, null, Modifier.size(60.dp), tint = MiuixTheme.colorScheme.onSurfaceVariantSummary.copy(alpha = .45f))
                Text("暂无模块", fontSize = 18.sp)
                TextButton("导入 Mod", onImport, colors = ButtonDefaults.textButtonColorsPrimary())
            }
        } else LazyColumn(Modifier.widthIn(max = 820.dp).fillMaxSize().nestedScroll(scroll.nestedScrollConnection).overScrollVertical().testTag("modules-page"), contentPadding = PaddingValues(16.dp, 12.dp, 16.dp, 108.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            items(state.mods, key = { it.id }) { mod ->
                ModCard(mod, state, Modifier.animateItem(), onStage, onDelete, onControl, onReset)
            }
        }
    }
}

@Composable
private fun ModCard(mod: ModItem, state: ManagerState, modifier: Modifier, onStage: (String, Boolean) -> Unit, onDelete: (ModItem) -> Unit, onControl: (ModControl, Double) -> Unit, onReset: (ModItem) -> Unit) {
    var expanded by rememberSaveable(mod.id) { mutableStateOf(false) }
    val checked = mod.id in state.draft
    val applied = mod.id in state.applied
    val pending = checked != applied
    val arrow by animateFloatAsState(if (expanded) 180f else 0f, spring(stiffness = Spring.StiffnessMediumLow), label = "controls-arrow")
    // Only the title reacts to presses; controls stay outside its transformed subtree.
    Card(modifier.fillMaxWidth().testTag("mod-${mod.id}"), insideMargin = PaddingValues(18.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            Box(Modifier.weight(1f).testTag("title-${mod.id}").clickable(interactionSource = remember { MutableInteractionSource() }, indication = null, enabled = mod.controls.isNotEmpty()) { expanded = !expanded }) {
              Card(Modifier.fillMaxWidth(), insideMargin = PaddingValues(0.dp), pressFeedbackType = PressFeedbackType.Tilt) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                    Box(Modifier.size(44.dp).background(MiuixTheme.colorScheme.surfaceContainerHigh, RoundedCornerShape(13.dp)), contentAlignment = Alignment.Center) {
                        Icon(Icons.Rounded.Extension, null, tint = MiuixTheme.colorScheme.primary, modifier = Modifier.size(25.dp))
                    }
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                        Text(mod.name, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Text("${formatBytes(mod.bytes)} · ${if (pending) "待应用" else if (applied) "已启用" else "未启用"}", fontSize = 12.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary)
                    }
                }
              }
            }
            Switch(checked = checked, onCheckedChange = { onStage(mod.id, it) }, modifier = Modifier.testTag("enable-${mod.id}"))
        }
        Row(Modifier.fillMaxWidth().padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            if (mod.controls.isNotEmpty()) {
                TextButton("控制", { expanded = !expanded }, minHeight = 36.dp, modifier = Modifier.testTag("controls-${mod.id}"))
                Icon(Icons.Rounded.KeyboardArrowDown, null, modifier = Modifier.size(18.dp).graphicsLayer { rotationZ = arrow }, tint = MiuixTheme.colorScheme.onSurfaceVariantSummary)
            }
            Spacer(Modifier.weight(1f))
            IconButton(onClick = { onDelete(mod) }, modifier = Modifier.testTag("delete-${mod.id}")) { Icon(Icons.Rounded.DeleteOutline, "删除 ${mod.name}", modifier = Modifier.size(21.dp), tint = MiuixTheme.colorScheme.onSurfaceVariantSummary) }
        }
        AnimatedVisibility(expanded && mod.controls.isNotEmpty(), enter = expandVertically(spring(dampingRatio = .9f, stiffness = 420f)) + fadeIn(), exit = shrinkVertically() + fadeOut()) {
            Column(Modifier.fillMaxWidth().padding(top = 10.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                HorizontalDivider()
                mod.controls.forEach { control -> key(control.key) { ControlItem(control, state.values[control.key] ?: control.default, onControl) } }
                TextButton("重置", { onReset(mod) }, Modifier.fillMaxWidth())
            }
        }
    }
}

@Composable
private fun ControlItem(control: ModControl, value: Double, onControl: (ModControl, Double) -> Unit) {
    if (control.cycle.isNotEmpty()) {
        val index = control.cycle.indexOf(value).coerceAtLeast(0)
        TextButton("${control.label} · 形态 ${index + 1} / ${control.cycle.size}", { onControl(control, control.cycle[(index + 1) % control.cycle.size]) }, Modifier.fillMaxWidth(), colors = ButtonDefaults.textButtonColorsPrimary())
    } else {
        var current by remember(control.key) { mutableFloatStateOf(value.toFloat()) }
        var dragging by remember(control.key) { mutableStateOf(false) }
        LaunchedEffect(value) { if (!dragging) current = value.toFloat() }
        Column {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(control.label, fontSize = 14.sp, modifier = Modifier.weight(1f))
                Text(String.format(Locale.ROOT, "%.2f", current), fontSize = 13.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary)
            }
            Slider(current.coerceIn(control.minimum.toFloat(), control.maximum.toFloat()), onValueChange = { current = it; dragging = true }, valueRange = control.minimum.toFloat()..control.maximum.toFloat(), onValueChangeFinished = { dragging = false; onControl(control, current.toDouble()) }, modifier = Modifier.fillMaxWidth().testTag("slider-${control.key}"))
        }
    }
}

@Composable
private fun SettingsPage(themeMode: Int, scroll: ScrollBehavior, onTheme: (Int) -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(Modifier.widthIn(max = 820.dp).fillMaxSize().nestedScroll(scroll.nestedScrollConnection).overScrollVertical().testTag("settings-page"), contentPadding = PaddingValues(16.dp, 12.dp, 16.dp, 32.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            item { SectionLabel("外观") }
            item {
                Card(Modifier.fillMaxWidth(), insideMargin = PaddingValues(18.dp)) {
                    Text("主题", fontWeight = FontWeight.Medium, modifier = Modifier.padding(bottom = 14.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf("跟随系统", "浅色", "深色").forEachIndexed { index, label ->
                            TextButton(label, { onTheme(index) }, Modifier.weight(1f).testTag("theme-$index"), colors = if (themeMode == index) ButtonDefaults.textButtonColorsPrimary() else ButtonDefaults.textButtonColors(), insideMargin = PaddingValues(horizontal = 6.dp, vertical = 12.dp))
                        }
                    }
                }
            }
            item { SectionLabel("画面") }
            item {
                Card(Modifier.fillMaxWidth(), insideMargin = PaddingValues(18.dp)) {
                    FutureSetting("隐藏 UID", Icons.Rounded.VisibilityOff)
                    HorizontalDivider(Modifier.padding(vertical = 14.dp))
                    FutureSetting("隐藏 HUD", Icons.Rounded.Fullscreen)
                }
            }
            item {
                Text("EFF Android · v${BuildConfig.VERSION_NAME}", fontSize = 12.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary, modifier = Modifier.padding(horizontal = 6.dp, vertical = 8.dp))
            }
        }
    }
}

@Composable
private fun SectionLabel(text: String) { Text(text, fontSize = 13.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary, modifier = Modifier.padding(start = 6.dp, top = 4.dp)) }

@Composable
private fun FutureSetting(label: String, icon: ImageVector) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
        Icon(icon, null, modifier = Modifier.size(24.dp), tint = MiuixTheme.colorScheme.onSurfaceVariantSummary)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(label, fontSize = 16.sp)
            Text("待开发", fontSize = 12.sp, color = MiuixTheme.colorScheme.onSurfaceVariantSummary)
        }
        Switch(checked = false, onCheckedChange = {}, enabled = false)
    }
}

private fun formatBytes(bytes: Long): String = when {
    bytes >= 1024L * 1024 * 1024 -> String.format(Locale.ROOT, "%.1f GB", bytes / (1024.0 * 1024 * 1024))
    bytes >= 1024L * 1024 -> String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024))
    bytes >= 1024 -> "${bytes / 1024} KB"
    else -> "$bytes B"
}
