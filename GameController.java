package dev.eff.probe;
import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import org.json.*;
import java.io.*;
import java.util.concurrent.atomic.AtomicBoolean;

final class GameController {
    private static final AtomicBoolean running=new AtomicBoolean();
    static void start(Context game,File diagnostics){
        if(!running.compareAndSet(false,true))return;
        Thread worker=new Thread(()->run(game,diagnostics),"EFF-ModController");worker.setDaemon(true);worker.start();
    }
    private static void status(Context game,String value){try{game.getContentResolver().call(ModStore.URI,"status",value,null);}catch(Exception ignored){android.util.Log.w("EFF","Status provider unavailable");}}
    private static String vfsKey(Context game)throws Exception{
        File vfs=new File(game.getExternalFilesDir(null),"VFS");java.util.List<File> blcs=new java.util.ArrayList<>();findBlcs(vfs,blcs);java.util.Collections.sort(blcs);
        if(blcs.isEmpty())throw new IOException("游戏 VFS 元数据不存在");java.security.MessageDigest digest=java.security.MessageDigest.getInstance("SHA-256");
        for(File f:blcs){digest.update(vfs.toPath().relativize(f.toPath()).toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));digest.update(ModStore.digest(f).getBytes(java.nio.charset.StandardCharsets.UTF_8));}
        StringBuilder text=new StringBuilder();for(byte b:digest.digest())text.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return text.toString();
    }
    private static void findBlcs(File folder,java.util.List<File> out){File[] files=folder.listFiles();if(files!=null)for(File f:files){if(f.isDirectory())findBlcs(f,out);else if(f.getName().endsWith(".blc"))out.add(f);}}
    private static void restoreApkChunks(Context game,File output)throws Exception{
        java.util.List<String> apks=new java.util.ArrayList<>();android.content.pm.ApplicationInfo info=game.getApplicationInfo();apks.add(info.sourceDir);if(info.splitSourceDirs!=null)java.util.Collections.addAll(apks,info.splitSourceDirs);
        File overlay=new File(game.getExternalFilesDir(null),"eff-android/vfs-chunks");
        for(String row:ModStore.read(new File(output,"required-chunks.tsv")).split("\\n")){
            if(row.isEmpty())continue;String[] fields=row.split("\\t");if(fields.length!=2||!fields[0].matches("[0-9A-Fa-f]{8}/[0-9A-Fa-f]{32}\\.chk")||!fields[1].matches("[0-9A-Fa-f]{32}"))throw new IOException("APK 补全清单无效");
            File target=ModStore.child(overlay,fields[0]);boolean found=false;
            for(String apk:apks){try(java.util.zip.ZipFile zip=new java.util.zip.ZipFile(apk)){
                java.util.Enumeration<? extends java.util.zip.ZipEntry> entries=zip.entries();
                while(entries.hasMoreElements()){java.util.zip.ZipEntry entry=entries.nextElement();String name=entry.getName().replace('\\','/');
                    if(!name.endsWith("/VFS/"+fields[0]))continue;
                    status(game,"正在从游戏 APK 补全内置资源 "+fields[0]);
                    if(!target.getParentFile().isDirectory()&&!target.getParentFile().mkdirs())throw new IOException("无法创建 APK 资源补全目录");File temp=new File(target.getPath()+".tmp");java.security.MessageDigest md5=java.security.MessageDigest.getInstance("MD5");
                    try(InputStream in=zip.getInputStream(entry);FileOutputStream out=new FileOutputStream(temp)){byte[] b=new byte[1024*1024];long size=0;for(int n;(n=in.read(b))!=-1;){size+=n;if(size>2L*1024*1024*1024)throw new IOException("APK CHK 过大");md5.update(b,0,n);out.write(b,0,n);}}
                    StringBuilder digest=new StringBuilder();for(byte b:md5.digest())digest.append(String.format(java.util.Locale.ROOT,"%02X",b&255));
                    if(!digest.toString().equalsIgnoreCase(fields[1])){ModStore.remove(temp);throw new IOException("APK 内置资源与当前 VFS 版本不符");}
                    if(!temp.renameTo(target))throw new IOException("无法保存 APK 内置资源");ModStore.save(new File(target.getPath()+".md5"),fields[1]);found=true;break;
                }
            }if(found)break;}
            if(!found)throw new IOException("缺少游戏资源 "+fields[0]+"，请先下载对应游戏内容");
        }
    }
    private static File copyAuthor(Context game,JSONObject entry)throws Exception{
        String id=entry.getString("id");File raw=new File(game.getFilesDir(),"eff-author/"+id);
        if(new File(game.getFilesDir(),"eff-author/"+id+".ready").isFile())return raw;
        status(game,"正在向游戏传递 "+entry.getString("name"));if(!raw.isDirectory()&&!raw.mkdirs())throw new IOException("无法创建 Mod 目录");
        JSONArray files=entry.getJSONArray("files");for(int i=0;i<files.length();++i){JSONObject file=files.getJSONObject(i);String path=file.getString("path");File target=ModStore.child(raw,path);
            if(!target.getParentFile().isDirectory()&&!target.getParentFile().mkdirs())throw new IOException("无法创建资源目录");
            Uri uri=ModStore.URI.buildUpon().appendPath(id).build();for(String part:path.split("/"))uri=uri.buildUpon().appendPath(part).build();
            try(InputStream in=game.getContentResolver().openInputStream(uri);FileOutputStream out=new FileOutputStream(target)){
                if(in==null)throw new IOException("资源无法读取");long bytes=0;byte[] buffer=new byte[1024*1024];for(int n;(n=in.read(buffer))!=-1;){bytes+=n;if(bytes>file.getLong("bytes"))throw new IOException("资源大小不符");out.write(buffer,0,n);}if(bytes!=file.getLong("bytes"))throw new IOException("资源被截断");
            }
            if(!ModStore.digest(target).equals(file.getString("sha256")))throw new IOException("资源哈希不符");
        }
        ModStore.save(new File(game.getFilesDir(),"eff-author/"+id+".ready"),id);return raw;
    }
    private static String hashText(String text)throws Exception{
        byte[] digest=java.security.MessageDigest.getInstance("SHA-256").digest(text.getBytes(java.nio.charset.StandardCharsets.UTF_8));StringBuilder out=new StringBuilder();for(byte b:digest)out.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return out.toString();
    }
    private static void publishControls(Context game,File output,java.util.Set<String> ids)throws Exception{
        JSONArray all=new JSONObject(ModStore.read(new File(output,"controls.json"))).getJSONArray("controls");
        for(String id:ids){JSONArray list=new JSONArray();for(int i=0;i<all.length();++i)if(all.getJSONObject(i).getString("owner").equals(id))list.put(all.getJSONObject(i));Bundle extras=new Bundle();extras.putString("json",list.toString());game.getContentResolver().call(ModStore.URI,"controls",id,extras);}
    }
    private static void run(Context game,File diagnostics){
        long applied=-1;String last="",lastShapes="";
        while(true){long processing=-1;try{
            Bundle bundle=game.getContentResolver().call(ModStore.URI,"snapshot",null,null);if(bundle==null)throw new IOException("Mod 库未连接");JSONObject control=new JSONObject(bundle.getString("json"));long sequence=control.getLong("sequence");
            JSONObject values=control.getJSONObject("shape_values");String shapes=values.toString();if(!shapes.equals(lastShapes)){
                StringBuilder command=new StringBuilder();java.util.Iterator<String> keys=values.keys();while(keys.hasNext()){String key=keys.next();command.append(key).append('\t').append(values.getDouble(key)).append('\n');}NativeBridge.controls(command.toString());lastShapes=shapes;
            }
            if(applied!=sequence){
                processing=sequence;JSONArray selection=control.getJSONArray("selected_ids");if(selection.length()>4)throw new IOException("最多启用 4 个 Mod");
                if(selection.length()==0){NativeBridge.request("",sequence);applied=sequence;}
                else{
                    java.util.TreeSet<String> ids=new java.util.TreeSet<>();for(int i=0;i<selection.length();++i)if(!ids.add(selection.getString(i)))throw new IOException("Mod 选择重复");
                    java.util.Map<String,JSONObject> entries=new java.util.HashMap<>();JSONArray library=control.getJSONArray("library");for(int i=0;i<library.length();++i){JSONObject entry=library.getJSONObject(i);entries.put(entry.getString("id"),entry);}
                    StringBuilder authors=new StringBuilder(),identity=new StringBuilder();for(String id:ids){JSONObject entry=entries.get(id);if(entry==null)throw new IOException("所选 Mod 不存在");if(authors.length()>0)authors.append('\n');authors.append(copyAuthor(game,entry).getPath());identity.append(id).append('\n');}
                    String vfs=vfsKey(game);File output=new File(game.getFilesDir(),"eff-compiled/"+hashText(identity.toString())+"-v3-"+vfs);
                    if(!new File(output,"hot.tsv").isFile()){
                        if(!output.isDirectory()&&!output.mkdirs())throw new IOException("无法创建编译目录");status(game,"正在合并编译 "+ids.size()+" 个 Mod");
                        AtomicBoolean compiling=new AtomicBoolean(true);Thread progress=new Thread(()->{while(compiling.get()){try{File file=new File(output,"progress.txt");if(file.isFile())status(game,"编译："+ModStore.read(file));Thread.sleep(1000);}catch(Exception ignored){break;}}},"EFF-CompileProgress");progress.setDaemon(true);progress.start();
                        String error;try{error=NativeBridge.compile(authors.toString(),game.getExternalFilesDir(null).getPath(),output.getPath());if(error.equals("APK_CHUNKS_REQUIRED")){restoreApkChunks(game,output);error=NativeBridge.compile(authors.toString(),game.getExternalFilesDir(null).getPath(),output.getPath());}}finally{compiling.set(false);}
                        if(!error.isEmpty())throw new IOException(error.contains("conflicting Mesh")||error.contains("duplicate/composed Mod id")?"所选 Mod 冲突："+error:error);
                    }
                    if(!vfs.equals(vfsKey(game)))throw new IOException("游戏资源已更新，请重新应用");publishControls(game,output,ids);
                    Bundle latest=game.getContentResolver().call(ModStore.URI,"snapshot",null,null);
                    if(latest!=null&&new JSONObject(latest.getString("json")).getLong("sequence")==sequence)NativeBridge.request(output.getPath(),sequence);
                    applied=sequence;
                }
            }
            File runtime=new File(diagnostics,"hot-status.txt");if(runtime.isFile()){String text=ModStore.read(runtime);if(!text.equals(last)){last=text;status(game,text);}}
        }catch(Throwable e){if(processing>=0)applied=processing;status(game,"加载失败："+e.getMessage()+"；修正后重新点击应用");}
        try{Thread.sleep(1000);}catch(InterruptedException e){Thread.currentThread().interrupt();return;}
        }
    }
}
