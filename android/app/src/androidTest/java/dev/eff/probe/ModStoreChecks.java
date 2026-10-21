package dev.eff.probe;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Device checks use isolated fixtures, then restore the user's exact controls. */
public final class ModStoreChecks extends Instrumentation {
    private Class<?> store;
    private Object call(String name,Class<?>[] signature,Object... arguments)throws Exception{
        Method method=store.getDeclaredMethod(name,signature);method.setAccessible(true);return method.invoke(null,arguments);
    }
    private JSONObject snapshot(Context c)throws Exception{return (JSONObject)call("snapshot",new Class<?>[]{Context.class},c);}
    private void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    private void clean(File file)throws Exception{call("remove",new Class<?>[]{File.class},file);}
    private void restore(SharedPreferences prefs,Map<String,?> saved){
        SharedPreferences.Editor editor=prefs.edit().clear();for(Map.Entry<String,?> row:saved.entrySet()){
            Object value=row.getValue();String key=row.getKey();
            if(value instanceof String)editor.putString(key,(String)value);
            else if(value instanceof Long)editor.putLong(key,(Long)value);
            else if(value instanceof Integer)editor.putInt(key,(Integer)value);
            else if(value instanceof Float)editor.putFloat(key,(Float)value);
            else if(value instanceof Boolean)editor.putBoolean(key,(Boolean)value);
            else if(value instanceof Set){Set<String> copy=new HashSet<>();for(Object item:(Set<?>)value)copy.add((String)item);editor.putStringSet(key,copy);}
            else throw new AssertionError("Unexpected preference type");
        }check(editor.commit(),"Could not restore original controls");
    }
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    @Override public void onStart(){
        Context c=getTargetContext();SharedPreferences prefs=c.getSharedPreferences("control",0);Map<String,?> saved=new HashMap<>(prefs.getAll());List<File> fixtures=new ArrayList<>(),trash=new ArrayList<>();Bundle result=new Bundle();int code=Activity.RESULT_OK;
        try{
            store=c.getClassLoader().loadClass("dev.eff.probe.ModStore");File library=(File)call("root",new Class<?>[]{Context.class},c);
            call("heartbeat",new Class<?>[]{String.class,int.class},"com.gryphline.endfield.gp",12345);
            check(snapshot(c).getBoolean("connected")&&snapshot(c).getInt("game_pid")==12345,"Fresh game heartbeat not reflected");
            java.lang.reflect.Field heartbeat=store.getDeclaredField("lastHeartbeat");heartbeat.setAccessible(true);heartbeat.setLong(null,android.os.SystemClock.elapsedRealtime()-13000);
            check(!snapshot(c).getBoolean("connected"),"Expired heartbeat still reports working");
            call("heartbeat",new Class<?>[]{String.class,int.class},"",0);
            boolean spoofRejected=false;try{Bundle forged=new Bundle();forged.putInt("pid",12345);c.getContentResolver().call(android.net.Uri.parse("content://dev.eff.probe.mods"),"heartbeat","com.gryphline.endfield.gp",forged);}catch(Exception expected){spoofRejected=true;}check(spoofRejected,"Manager could impersonate a game heartbeat");
            List<String> ids=new ArrayList<>();String tag=UUID.randomUUID().toString();
            for(int i=0;i<5;++i){byte[] digest=java.security.MessageDigest.getInstance("SHA-256").digest((tag+i).getBytes(StandardCharsets.UTF_8));StringBuilder hex=new StringBuilder();for(byte b:digest)hex.append(String.format(Locale.ROOT,"%02x",b&255));String id=hex.toString();ids.add(id);
                File folder=new File(library,id);check(folder.mkdirs(),"Fixture path already exists");fixtures.add(folder);new File(folder,"raw").mkdirs();
                JSONObject entry=new JSONObject().put("id",id).put("name","Temporary test Mod "+i).put("files",new JSONArray()).put("bytes",0);
                try(FileOutputStream out=new FileOutputStream(new File(folder,"entry.json"))){out.write(entry.toString().getBytes(StandardCharsets.UTF_8));}
            }
            check(prefs.edit().putString("selected_ids","[]").putString("draft_ids","[]").putString("selected","").putString("shape_values","{}").commit(),"Could not isolate test selection");
            for(String id:ids)call("stage",new Class<?>[]{Context.class,String.class,boolean.class},c,id,true);
            check(snapshot(c).getJSONArray("draft_ids").length()==5,"Five Mods cannot be selected");
            call("applyDraft",new Class<?>[]{Context.class},c);JSONObject applied=snapshot(c);check(applied.getJSONArray("selected_ids").length()==5,"Five Mods cannot be applied");long sequence=applied.getLong("sequence");
            String deleted=ids.get(0),retained=ids.get(1);JSONObject values=new JSONObject().put(deleted+":$test",.5).put(retained+":$test",.25);check(prefs.edit().putString("shape_values",values.toString()).commit(),"Could not set fixture controls");
            File removed=(File)call("delete",new Class<?>[]{Context.class,String.class},c,deleted);trash.add(removed);JSONObject after=snapshot(c);
            check(after.getJSONArray("selected_ids").length()==4&&after.getJSONArray("draft_ids").length()==4,"Deletion did not prune both selections");
            check(after.getLong("sequence")==sequence+1,"Active deletion did not request reload");
            check(!after.getJSONObject("shape_values").has(deleted+":$test")&&after.getJSONObject("shape_values").getDouble(retained+":$test")==.25,"Deletion removed unrelated controls");
            check(!fixtures.get(0).exists()&&removed.isDirectory(),"Deleted Mod was not detached");
            call("stage",new Class<?>[]{Context.class,String.class,boolean.class},c,ids.get(4),false);call("applyDraft",new Class<?>[]{Context.class},c);long beforeInactive=snapshot(c).getLong("sequence");
            trash.add((File)call("delete",new Class<?>[]{Context.class,String.class},c,ids.get(4)));
            check(snapshot(c).getLong("sequence")==beforeInactive,"Inactive deletion unnecessarily reapplied Mods");
            boolean rejected=false;try{call("delete",new Class<?>[]{Context.class,String.class},c,"../outside");}catch(Exception expected){rejected=true;}check(rejected,"Invalid deletion path accepted");
            result.putString("stream","PASS: heartbeat freshness/timeout/owner checks; five-Mod selection/apply; active and inactive deletion; reload request; control isolation; invalid-path rejection. User state restored.\n");
        }catch(Throwable error){code=Activity.RESULT_CANCELED;result.putString("stream","FAIL: "+error+"\n");}
        finally{try{restore(prefs,saved);if(store!=null){call("heartbeat",new Class<?>[]{String.class,int.class},"",0);for(File folder:fixtures)clean(folder);for(File folder:trash)clean(folder);}}catch(Throwable error){code=Activity.RESULT_CANCELED;result.putString("stream","FAIL restoring fixtures: "+error+"\n");}}
        finish(code,result);
    }
}
