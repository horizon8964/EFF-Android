package dev.eff.probe;

import android.content.Context;
import android.net.Uri;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;

final class ModStore {
    static final String AUTHORITY="dev.eff.probe.mods";
    static final Uri URI=Uri.parse("content://"+AUTHORITY);
    static File root(Context c){return new File(c.getFilesDir(),"library");}
    static File child(File root,String relative)throws IOException{
        if(relative.isEmpty()||relative.indexOf('\\')>=0||relative.startsWith("/"))throw new IOException("非法资源路径");
        File file=new File(root,relative).getCanonicalFile();
        if(!file.getPath().startsWith(root.getCanonicalPath()+File.separator))throw new IOException("资源路径超出 Mod 目录");
        return file;
    }
    static void save(File file,String text)throws IOException{
        File temp=new File(file.getPath()+".tmp");
        try(FileOutputStream out=new FileOutputStream(temp)){out.write(text.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
        if(!temp.renameTo(file))throw new IOException("无法提交文件");
    }
    static String read(File file)throws IOException{
        if(file.length()>8*1024*1024)throw new IOException("索引过大");
        try(FileInputStream in=new FileInputStream(file);ByteArrayOutputStream out=new ByteArrayOutputStream()){
            byte[] buffer=new byte[16384];for(int n;(n=in.read(buffer))!=-1;)out.write(buffer,0,n);
            return out.toString(StandardCharsets.UTF_8.name());
        }
    }
    static String digest(File file)throws Exception{
        MessageDigest md=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(file)){byte[] b=new byte[1024*1024];for(int n;(n=in.read(b))!=-1;)md.update(b,0,n);}
        StringBuilder text=new StringBuilder();for(byte b:md.digest())text.append(String.format(Locale.ROOT,"%02x",b&255));return text.toString();
    }
    static JSONObject importZip(Context c,Uri uri)throws Exception{
        File work=new File(c.getCacheDir(),"import-"+UUID.randomUUID());if(!work.mkdirs())throw new IOException("无法创建导入目录");
        try{
            File archive=new File(work,"archive.zip");long copied=0;
            try(InputStream in=c.getContentResolver().openInputStream(uri);FileOutputStream out=new FileOutputStream(archive)){
                if(in==null)throw new IOException("无法打开压缩包");byte[] b=new byte[1024*1024];
                for(int n;(n=in.read(b))!=-1;){copied+=n;if(copied>2L*1024*1024*1024)throw new IOException("压缩包超过 2 GiB");out.write(b,0,n);}
            }
            String id=digest(archive);File destination=new File(root(c),id);
            if(new File(destination,"entry.json").isFile())return new JSONObject(read(new File(destination,"entry.json")));
            File unpacked=new File(work,"unpacked");if(!unpacked.mkdir())throw new IOException("无法解压");
            long expanded=0;int count=0;Set<String> names=new HashSet<>();List<File> inis=new ArrayList<>();
            try(ZipInputStream zip=new ZipInputStream(new BufferedInputStream(new FileInputStream(archive)))){
                for(ZipEntry entry;(entry=zip.getNextEntry())!=null;){
                    if(++count>50000)throw new IOException("文件数量过多");String name=entry.getName().replace('\\','/');
                    if(name.endsWith("/"))name=name.substring(0,name.length()-1);if(name.isEmpty())continue;
                    File file=child(unpacked,name);if(!names.add(file.getCanonicalPath()))throw new IOException("压缩包含重复路径");
                    if(entry.isDirectory()){if(!file.isDirectory()&&!file.mkdirs())throw new IOException("无法创建目录");continue;}
                    if(!file.getParentFile().isDirectory()&&!file.getParentFile().mkdirs())throw new IOException("无法创建资源目录");
                    try(FileOutputStream out=new FileOutputStream(file)){byte[] b=new byte[65536];for(int n;(n=zip.read(b))!=-1;){expanded+=n;if(expanded>4L*1024*1024*1024)throw new IOException("解压数据超过 4 GiB");out.write(b,0,n);}}
                    if(file.getName().equals("mod.ini"))inis.add(file);
                }
            }
            if(inis.size()!=1)throw new IOException("压缩包需包含一个 EFF mod.ini");
            File author=inis.get(0).getParentFile();if(!new File(author,"source-inputs.bin").isFile())throw new IOException("缺少 EFF source-inputs.bin，请使用完整原始 Mod 包");
            String label="EFF Mod";boolean mod=false;
            for(String line:read(inis.get(0)).split("\\r?\\n")){String value=line.trim();if(value.startsWith("["))mod=value.equalsIgnoreCase("[Mod]");if(mod&&value.startsWith("id="))label=value.substring(3).trim();}
            JSONArray files=new JSONArray();collect(author,author,files);
            if(!root(c).isDirectory()&&!root(c).mkdirs())throw new IOException("无法创建 Mod 库");
            File staging=new File(root(c),"staging-"+UUID.randomUUID());if(!staging.mkdir())throw new IOException("无法暂存 Mod");
            try{
                File raw=new File(staging,"raw");if(!author.renameTo(raw))throw new IOException("无法保存导入的 Mod");
                JSONObject metadata=new JSONObject().put("id",id).put("name",label).put("files",files).put("bytes",expanded);
                String index=metadata.toString();if(index.getBytes(StandardCharsets.UTF_8).length>8*1024*1024)throw new IOException("Mod 文件索引超过 8 MiB");
                save(new File(staging,"entry.json"),index);
                if(!staging.renameTo(destination))throw new IOException("无法发布 Mod");return metadata;
            }finally{remove(staging);}
        }finally{remove(work);}
    }
    private static void collect(File root,File folder,JSONArray list)throws Exception{
        File[] files=folder.listFiles();if(files==null)throw new IOException("无法读取 Mod");Arrays.sort(files);
        for(File file:files)if(file.isDirectory())collect(root,file,list);else list.put(new JSONObject().put("path",root.toPath().relativize(file.toPath()).toString().replace('\\','/')).put("bytes",file.length()).put("sha256",digest(file)));
    }
    static void remove(File file){if(file.isDirectory()){File[] children=file.listFiles();if(children!=null)for(File child:children)remove(child);}if(file.exists()&&!file.delete())android.util.Log.w("EFF","Cannot remove temporary file "+file.getName());}
    static synchronized JSONArray list(Context c)throws Exception{
        JSONArray list=new JSONArray();File[] folders=root(c).listFiles();if(folders!=null){Arrays.sort(folders);for(File f:folders)if(f.getName().matches("[0-9a-f]{64}")&&new File(f,"entry.json").isFile())list.put(entry(f));}return list;
    }
    private static JSONObject entry(File folder)throws Exception{
        JSONObject entry=new JSONObject(read(new File(folder,"entry.json")));File controls=new File(folder,"controls.json");
        if(controls.isFile())entry.put("controls",new JSONArray(read(controls)));return entry;
    }
    private static JSONArray selected(Context c)throws Exception{
        android.content.SharedPreferences p=c.getSharedPreferences("control",0);String value=p.getString("selected_ids",null);
        if(value!=null)return new JSONArray(value);JSONArray selected=new JSONArray();String legacy=p.getString("selected","");if(!legacy.isEmpty())selected.put(legacy);return selected;
    }
    static synchronized JSONObject snapshot(Context c)throws Exception{
        android.content.SharedPreferences p=c.getSharedPreferences("control",0);
        return new JSONObject().put("sequence",p.getLong("sequence",0)).put("selected_ids",selected(c)).put("draft_ids",new JSONArray(p.getString("draft_ids",selected(c).toString()))).put("shape_values",new JSONObject(p.getString("shape_values","{}"))).put("status",p.getString("status","等待游戏连接")).put("library",list(c));
    }
    static synchronized void stage(Context c,String id,boolean enabled)throws Exception{
        if(!id.matches("[0-9a-f]{64}")||!new File(root(c),id+"/entry.json").isFile())throw new IOException("Mod 不存在");
        android.content.SharedPreferences p=c.getSharedPreferences("control",0);JSONArray old=new JSONArray(p.getString("draft_ids",selected(c).toString()));java.util.TreeSet<String> ids=new java.util.TreeSet<>();for(int i=0;i<old.length();++i)ids.add(old.getString(i));
        if(enabled)ids.add(id);else ids.remove(id);if(ids.size()>4)throw new IOException("最多同时启用 4 个 Mod");JSONArray next=new JSONArray();for(String item:ids)next.put(item);
        if(!p.edit().putString("draft_ids",next.toString()).commit())throw new IOException("无法保存选择");
    }
    static synchronized void applyDraft(Context c)throws Exception{
        android.content.SharedPreferences p=c.getSharedPreferences("control",0);JSONArray ids=new JSONArray(p.getString("draft_ids",selected(c).toString()));
        if(ids.length()>4)throw new IOException("最多同时启用 4 个 Mod");for(int i=0;i<ids.length();++i)if(!new File(root(c),ids.getString(i)+"/entry.json").isFile())throw new IOException("所选 Mod 不存在");
        if(!p.edit().putString("selected_ids",ids.toString()).putLong("sequence",p.getLong("sequence",0)+1).putString("status","等待游戏合并所选 Mod").commit())throw new IOException("无法保存请求");
    }
    static synchronized void clear(Context c)throws Exception{
        android.content.SharedPreferences p=c.getSharedPreferences("control",0);
        if(!p.edit().putString("selected_ids","[]").putString("draft_ids","[]").putString("selected","").putLong("sequence",p.getLong("sequence",0)+1).putString("status","等待恢复原版").commit())throw new IOException("无法保存恢复请求");
    }
    static synchronized void shape(Context c,JSONObject spec,double value)throws Exception{
        double min=spec.getDouble("min"),max=spec.getDouble("max");if(!Double.isFinite(value)||min>=max)throw new IOException("形态值无效");value=Math.max(min,Math.min(max,value));
        android.content.SharedPreferences p=c.getSharedPreferences("control",0);JSONObject values=new JSONObject(p.getString("shape_values","{}"));values.put(spec.getString("key"),value);
        if(!p.edit().putString("shape_values",values.toString()).commit())throw new IOException("无法保存形态值");
    }
}
