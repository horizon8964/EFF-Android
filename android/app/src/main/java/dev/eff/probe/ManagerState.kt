package dev.eff.probe

import org.json.JSONArray
import org.json.JSONObject

internal data class ModControl(
    val key: String,
    val label: String,
    val minimum: Double,
    val maximum: Double,
    val default: Double,
    val cycle: List<Double>,
    val document: String,
)

internal data class ModItem(
    val id: String,
    val name: String,
    val bytes: Long,
    val controls: List<ModControl>,
)

internal data class ManagerState(
    val mods: List<ModItem> = emptyList(),
    val draft: Set<String> = emptySet(),
    val applied: Set<String> = emptySet(),
    val values: Map<String, Double> = emptyMap(),
    val connected: Boolean = false,
    val gamePackage: String = "",
    val gamePid: Int = 0,
    val status: String = "等待游戏",
) {
    companion object {
        private fun ids(array: JSONArray) = List(array.length()) { array.getString(it) }.toSet()
        fun read(json: JSONObject): ManagerState {
            val library = json.getJSONArray("library")
            val mods = List(library.length()) { index ->
                val entry = library.getJSONObject(index)
                val controls = entry.optJSONArray("controls") ?: JSONArray()
                ModItem(entry.getString("id"), entry.getString("name"), entry.optLong("bytes"), List(controls.length()) { n ->
                    val spec = controls.getJSONObject(n)
                    val states = spec.optJSONArray("states") ?: JSONArray()
                    ModControl(spec.getString("key"), spec.getString("label"), spec.getDouble("min"), spec.getDouble("max"), spec.getDouble("default"),
                        if (spec.optString("kind") == "cycle") List(states.length()) { states.getDouble(it) } else emptyList(), spec.toString())
                })
            }
            val values = json.getJSONObject("shape_values")
            return ManagerState(mods, ids(json.getJSONArray("draft_ids")), ids(json.getJSONArray("selected_ids")), values.keys().asSequence().associateWith { values.getDouble(it) },
                json.optBoolean("connected"), json.optString("game_package"), json.optInt("game_pid"), compactStatus(json.getString("status")))
        }
        fun compactStatus(text: String): String = when {
            text.startsWith("Mod 已应用") || text == "已应用" -> "已应用"
            text.startsWith("已恢复原版") -> "已恢复原版"
            text.startsWith("编译：") || text.startsWith("正在合并编译") -> "编译中"
            text.startsWith("等待进入大世界") -> "等待进入大世界"
            text.startsWith("等待") -> "等待游戏"
            text.startsWith("正在") -> "加载中"
            else -> text
        }
    }
}
