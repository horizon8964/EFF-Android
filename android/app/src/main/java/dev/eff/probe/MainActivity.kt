package dev.eff.probe

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

class MainActivity : ComponentActivity() {
    private val worker = Executors.newSingleThreadExecutor()
    private val handler = Handler(Looper.getMainLooper())
    private val refreshing = AtomicBoolean(false)
    private var state by mutableStateOf(ManagerState())
    private var busy by mutableStateOf<String?>(null)
    private var failure by mutableStateOf<String?>(null)
    private var deleteTarget by mutableStateOf<ModItem?>(null)
    private var themeMode by mutableIntStateOf(0)
    private val ticker = object : Runnable {
        override fun run() { refresh(); handler.postDelayed(this, 1000) }
    }
    private val importer = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importMod(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        themeMode = getSharedPreferences("ui", MODE_PRIVATE).getInt("theme", 0).coerceIn(0, 2)
        setContent {
            EffManager(state, themeMode, busy, failure, deleteTarget,
                onTheme = { mode -> themeMode = mode; getSharedPreferences("ui", MODE_PRIVATE).edit().putInt("theme", mode).apply() },
                onImport = { if (busy == null) importer.launch(arrayOf("application/zip", "application/octet-stream", "application/x-zip-compressed")) },
                onStage = { id, enabled -> action { ModStore.stage(this, id, enabled) } },
                onApply = { action { ModStore.applyDraft(this) } },
                onRestore = { action { ModStore.clear(this) } },
                onDelete = { deleteTarget = it },
                onConfirmDelete = { deleteTarget?.let(::deleteMod) },
                onDismissDelete = { deleteTarget = null },
                onDismissError = { failure = null },
                onControl = { control, value -> action { ModStore.shape(this, JSONObject(control.document), value) } },
                onReset = { mod -> action { mod.controls.forEach { ModStore.shape(this, JSONObject(it.document), it.default) } } },
                onGame = ::launchGame,
            )
        }
    }

    private fun action(task: () -> Unit) {
        try { task(); refresh() } catch (error: Exception) { failure = error.message ?: "操作失败" }
    }

    private fun refresh() {
        if (!refreshing.compareAndSet(false, true)) return
        worker.execute {
            try {
                val next = ManagerState.read(ModStore.snapshot(this))
                runOnUiThread { state = next }
            } catch (error: Exception) {
                runOnUiThread { failure = error.message ?: "无法读取模块" }
            } finally { refreshing.set(false) }
        }
    }

    private fun importMod(uri: Uri) {
        busy = "导入中"
        worker.execute {
            try {
                ModStore.importZip(this, uri)
                val next = ManagerState.read(ModStore.snapshot(this))
                runOnUiThread { state = next }
            } catch (error: Exception) {
                runOnUiThread { failure = error.message ?: "导入失败" }
            } finally { runOnUiThread { busy = null } }
        }
    }

    private fun deleteMod(mod: ModItem) {
        deleteTarget = null
        busy = "删除中"
        worker.execute {
            try {
                val trash = ModStore.delete(this, mod.id)
                val next = ManagerState.read(ModStore.snapshot(this))
                runOnUiThread { state = next; busy = null }
                ModStore.remove(trash)
            } catch (error: Exception) {
                runOnUiThread { busy = null; failure = error.message ?: "删除失败" }
            }
        }
    }

    private fun launchGame(packageName: String) {
        val intent: Intent? = packageManager.getLaunchIntentForPackage(packageName)
        if (intent != null) startActivity(intent) else Toast.makeText(this, "未安装此客户端", Toast.LENGTH_SHORT).show()
    }

    override fun onResume() { super.onResume(); handler.removeCallbacks(ticker); handler.post(ticker) }
    override fun onPause() { handler.removeCallbacks(ticker); super.onPause() }
    override fun onDestroy() { handler.removeCallbacks(ticker); worker.shutdown(); super.onDestroy() }
}
