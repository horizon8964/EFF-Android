package dev.eff.probe;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

final class LoaderFiles {
    private LoaderFiles() {}
    private static String hash(File file) throws Exception {
        MessageDigest hash = MessageDigest.getInstance("SHA-256");
        try (FileInputStream stream = new FileInputStream(file)) {
            byte[] buffer = new byte[1024 * 1024];
            for (int n; (n = stream.read(buffer)) != -1;) hash.update(buffer, 0, n);
        }
        StringBuilder text = new StringBuilder();
        for (byte b : hash.digest()) text.append(String.format(java.util.Locale.ROOT, "%02X", b & 255));
        return text.toString();
    }
    private static File child(File root, String relative) throws Exception {
        File file = new File(root, relative).getCanonicalFile();
        if (!file.getPath().startsWith(root.getCanonicalPath() + File.separator)) throw new SecurityException("package path escapes root");
        return file;
    }
    static String prepare(Context game, File diagnostics) throws Exception {
        File gameRoot = game.getExternalFilesDir(null);
        if (gameRoot == null) return "";
        File mod = new File(diagnostics, "mods/si");
        File manifestFile = new File(mod, "manifest.json");
        if (!manifestFile.isFile()) return "";
        if (manifestFile.length() > 1024 * 1024) throw new IllegalStateException("manifest too large");
        JSONObject manifest;
        try (FileInputStream stream = new FileInputStream(manifestFile)) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            for (int n; (n = stream.read(buffer)) != -1;) bytes.write(buffer, 0, n);
            manifest = new JSONObject(bytes.toString(StandardCharsets.UTF_8.name()));
        }
        if (manifest.getInt("schema") != 1 || !manifest.getString("mod").equals("si")) throw new IllegalStateException("unsupported Mod package");
        if (!manifest.getBoolean("enabled")) return "";
        JSONArray sources = manifest.getJSONArray("source_blcs");
        for (int i = 0; i < sources.length(); ++i) {
            JSONObject source = sources.getJSONObject(i);
            if (!hash(child(gameRoot, source.getString("file"))).equalsIgnoreCase(source.getString("sha256")))
                throw new IllegalStateException("game VFS changed; recompile si: " + source.getString("file"));
        }
        if (!hash(new File(mod, "routes.tsv")).equalsIgnoreCase(manifest.getString("routes_sha256"))) throw new IllegalStateException("routes digest differs");
        if (!hash(new File(mod, "prewarm.txt")).equalsIgnoreCase(manifest.getString("prewarm_sha256"))) throw new IllegalStateException("prewarm digest differs");
        JSONArray payloads = manifest.getJSONArray("payloads");
        for (int i = 0; i < payloads.length(); ++i) {
            JSONObject item = payloads.getJSONObject(i);
            File file = child(mod, item.getString("file"));
            if (file.length() != item.getLong("bytes") || !hash(file).equalsIgnoreCase(item.getString("sha256")))
                throw new IllegalStateException("si payload integrity failed: " + file.getName());
        }
        return mod.getCanonicalPath();
    }
}
