package dev.eff.probe;
final class NativeBridge {
    private NativeBridge() {}
    static native boolean frame();
    static native void controls(String values);
    static native String compile(String mod,String game,String output);
    static native void request(String output,long sequence);
}
