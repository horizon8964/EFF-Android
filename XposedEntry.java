package dev.eff.probe;

import android.app.Application;
import android.content.Context;
import android.system.Os;
import android.util.Log;
import io.github.libxposed.api.XposedModule;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/** Framework entry for verified startup replacements and runtime diagnostics. */
public final class XposedEntry extends XposedModule {
    private String processName;
    private final AtomicBoolean attached = new AtomicBoolean();
    private final AtomicBoolean loading = new AtomicBoolean();
    private final AtomicBoolean complete = new AtomicBoolean();
    private final AtomicInteger attempts = new AtomicInteger();
    private final CopyOnWriteArrayList<HookHandle> frames = new CopyOnWriteArrayList<>();
    private volatile File diagnostics;
    private volatile String loaderRoot = "";
    private volatile boolean nativeLoaded;
    private volatile boolean frameDone;

    @Override public void onModuleLoaded(ModuleLoadedParam param) {
        processName = param.getProcessName();
        report("module loaded in " + processName);
    }

    @android.annotation.SuppressLint("DiscouragedPrivateApi")
    @Override public void onPackageReady(PackageReadyParam param) {
        String target = param.getPackageName();
        if (!("com.gryphline.endfield.gp".equals(target) || "com.hypergryph.endfield".equals(target))
                || !target.equals(processName) || !attached.compareAndSet(false, true)) return;
        try {
            hook(Application.class.getDeclaredMethod("attach", Context.class)).intercept(chain -> {
                Object result = chain.proceed();
                Context game = (Context) chain.getArg(0);
                try {
                    File external = game.getExternalFilesDir(null);
                    diagnostics = new File(external != null ? external : game.getFilesDir(), "eff-android");
                    if (!diagnostics.isDirectory() && !diagnostics.mkdirs()) throw new IllegalStateException("diagnostics directory unavailable");
                    report("application attached; pid=" + android.os.Process.myPid() + "; package=" + target);
                    Thread worker = new Thread(() -> installFrames(game, param.getClassLoader()), "EFF-FrameSetup");
                    worker.setDaemon(true);
                    worker.start();
                } catch (Throwable failure) { report("attach failed: " + failure); }
                return result;
            });
            report("application attach hook installed");
        } catch (Throwable failure) { report("entry failed: " + failure); }
    }

    private void installFrames(Context game, ClassLoader loader) {
        try {
            try {
                loaderRoot = ""; // Live generations keep the game cache pristine.
                report(loaderRoot.isEmpty() ? "si not enabled; diagnostics only" : "si Android package and VFS proofs verified");
            } catch (Exception invalid) {
                report("si preflight rejected: " + invalid);
                loaderRoot = "";
            }
            Class<?> unity = null;
            for (int i = 0; i < 120 && unity == null; i++) {
                try { unity = Class.forName("com.unity3d.player.UnityPlayer", false, loader); }
                catch (ClassNotFoundException missing) { Thread.sleep(100); }
            }
            if (unity == null) throw new ClassNotFoundException("UnityPlayer unavailable after 12 seconds");
            final Class<?> caller = unity;
            for (Method method : unity.getDeclaredMethods()) {
                if (!method.getName().equals("nativeRender") || method.getReturnType() != boolean.class) continue;
                HookHandle handle = hook(method).intercept(chain -> {
                    Object result = chain.proceed();
                    if (Boolean.TRUE.equals(result)) {
                        loadAfterFrame(game, caller);
                        if (nativeLoaded && !frameDone) try {
                            if (NativeBridge.frame()) { frameDone = true; frames.forEach(HookHandle::unhook); }
                        } catch (Throwable error) {
                            frameDone = true; report("Unity frame bridge failed: " + error); frames.forEach(HookHandle::unhook);
                        }
                    }
                    return result;
                });
                frames.add(handle);
                if (complete.get() && !nativeLoaded) handle.unhook();
            }
            if (frames.isEmpty()) throw new NoSuchMethodException("UnityPlayer.nativeRender");
            report("waiting for successful Unity render; hooks=" + frames.size());
        } catch (Throwable failure) {
            frames.forEach(HookHandle::unhook);
            report("frame setup failed: " + failure);
        }
    }

    // Framework-injected entry only. nativeLoad selects Unity's linker namespace;
    // ordinary System.load() uses the module namespace. Failures stop after 3 tries.
    @android.annotation.SuppressLint({"BlockedPrivateApi", "DiscouragedPrivateApi"})
    private void loadAfterFrame(Context game, Class<?> caller) {
        if (complete.get() || !loading.compareAndSet(false, true)) return;
        try {
            if (complete.get()) return;
            int attempt = attempts.incrementAndGet();
            report("first-frame native load attempt " + attempt + "; thread=" + Thread.currentThread().getName());
            Context module = game.createPackageContext("dev.eff.probe", Context.CONTEXT_IGNORE_SECURITY);
            File library = new File(module.getApplicationInfo().nativeLibraryDir, "libeff_android_probe.so");
            if (!library.isFile()) throw new IllegalStateException("native library missing: " + library);
            Os.setenv("EFF_ANDROID_DIAGNOSTICS", diagnostics.getAbsolutePath(), true);
            Os.setenv("EFF_ANDROID_MOD_ROOT", loaderRoot, true);
            Method load;
            Object[] args;
            try {
                load = Runtime.class.getDeclaredMethod("nativeLoad", String.class, ClassLoader.class, Class.class);
                args = new Object[]{library.getAbsolutePath(), caller.getClassLoader(), caller};
            } catch (NoSuchMethodException absent) {
                load = Runtime.class.getDeclaredMethod("nativeLoad", String.class, ClassLoader.class);
                args = new Object[]{library.getAbsolutePath(), caller.getClassLoader()};
            }
            load.setAccessible(true);
            Thread thread = Thread.currentThread();
            ClassLoader previous = thread.getContextClassLoader();
            Object error;
            try {
                thread.setContextClassLoader(NativeBridge.class.getClassLoader());
                error = load.invoke(null, args);
            } finally { thread.setContextClassLoader(previous); }
            if (error != null) throw new UnsatisfiedLinkError(error.toString());
            nativeLoaded = true;
            GameController.start(game, diagnostics);
            complete.set(true);
            report("native library loaded in Unity classloader namespace; runtime probe started");
        } catch (Throwable failure) {
            report("native load failed: " + failure + "; cause=" + failure.getCause());
            if (attempts.get() >= 3) complete.set(true);
        } finally {
            if (complete.get() && !nativeLoaded) frames.forEach(HookHandle::unhook);
            loading.set(false);
        }
    }

    private synchronized void report(String message) {
        log(Log.INFO, "EFFAndroid", message);
        if (diagnostics == null) return;
        try (FileOutputStream stream = new FileOutputStream(new File(diagnostics, "bootstrap.log"), true)) {
            stream.write((System.currentTimeMillis() + " " + message + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (Exception failure) { Log.w("EFFAndroid", "cannot write bootstrap log", failure); }
    }
}
