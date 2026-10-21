package dev.eff.probe;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.*;
import java.io.*;
import java.util.List;

public final class ModProvider extends ContentProvider {
    @Override public boolean onCreate(){return true;}
    private void check(){
        if(Binder.getCallingUid()==android.os.Process.myUid())return;
        String[] packages=getContext().getPackageManager().getPackagesForUid(Binder.getCallingUid());
        if(packages!=null)for(String p:packages)if(p.equals("com.gryphline.endfield.gp")||p.equals("com.hypergryph.endfield"))return;
        throw new SecurityException("仅允许所选游戏读取 Mod");
    }
    @Override public Bundle call(String method,String arg,Bundle extras){
        check();try{
            Bundle out=new Bundle();
            if(method.equals("snapshot"))out.putString("json",ModStore.snapshot(getContext()).toString());
            else if(method.equals("heartbeat")){
                if(arg==null||(!arg.equals("com.gryphline.endfield.gp")&&!arg.equals("com.hypergryph.endfield"))||extras==null||extras.getInt("pid")<=0)throw new IllegalArgumentException("游戏连接无效");
                String[] owners=getContext().getPackageManager().getPackagesForUid(Binder.getCallingUid());boolean owns=false;if(owners!=null)for(String owner:owners)if(arg.equals(owner))owns=true;
                if(!owns)throw new SecurityException("游戏连接来源不符");ModStore.heartbeat(arg,extras.getInt("pid"));
            }
            else if(method.equals("status"))getContext().getSharedPreferences("control",0).edit().putString("status",arg).apply();
            else if(method.equals("controls")){
                if(arg==null||!arg.matches("[0-9a-f]{64}")||extras==null)throw new IllegalArgumentException("Mod 元数据无效");
                File folder=new File(ModStore.root(getContext()),arg);if(!new File(folder,"entry.json").isFile())throw new IllegalArgumentException("Mod 不存在");
                String value=extras.getString("json");if(value==null||value.length()>1024*1024)throw new IllegalArgumentException("形态索引过大");
                org.json.JSONArray controls=new org.json.JSONArray(value);ModStore.save(new File(folder,"controls.json"),controls.toString());
            }
            else throw new IllegalArgumentException("未知请求");return out;
        }catch(Exception e){throw new IllegalStateException(e);}
    }
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode)throws FileNotFoundException{
        check();try{
            List<String> parts=uri.getPathSegments();if(!mode.equals("r")||parts.size()<2||!parts.get(0).matches("[0-9a-f]{64}"))throw new FileNotFoundException();
            File root=new File(ModStore.root(getContext()),parts.get(0)+"/raw");
            StringBuilder relative=new StringBuilder();for(int i=1;i<parts.size();++i){if(i>1)relative.append('/');relative.append(parts.get(i));}
            return ParcelFileDescriptor.open(ModStore.child(root,relative.toString()),ParcelFileDescriptor.MODE_READ_ONLY);
        }catch(IOException e){throw new FileNotFoundException(e.getMessage());}
    }
    @Override public Cursor query(Uri u,String[] p,String s,String[] a,String order){throw new UnsupportedOperationException();}
    @Override public String getType(Uri u){return "application/octet-stream";}
    @Override public Uri insert(Uri u,ContentValues v){throw new UnsupportedOperationException();}
    @Override public int delete(Uri u,String s,String[] a){throw new UnsupportedOperationException();}
    @Override public int update(Uri u,ContentValues v,String s,String[] a){throw new UnsupportedOperationException();}
}
