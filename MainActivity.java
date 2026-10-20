package dev.eff.probe;
import android.app.Activity;
import android.content.Intent;
import android.os.*;
import android.widget.*;
import org.json.*;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final java.util.concurrent.ExecutorService worker=Executors.newSingleThreadExecutor();
    private LinearLayout library;private TextView status,selection;private Button importer;private boolean importing;
    private String displayed="";
    private final Runnable refresh=new Runnable(){@Override public void run(){render();handler.postDelayed(this,1000);}};
    @Override public void onCreate(Bundle state){
        super.onCreate(state);LinearLayout page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);int pad=(int)(20*getResources().getDisplayMetrics().density);page.setPadding(pad,pad,pad,pad);
        TextView title=new TextView(this);title.setText("EFF Android · 四角色与形态键");title.setTextSize(26);page.addView(title);
        TextView intro=new TextView(this);intro.setText("勾选最多四个 Mod，点击应用后返回游戏。形态键调整会自动保存并生效，无需重新编译。升级模块后请重启游戏一次。\n");intro.setTextSize(16);page.addView(intro);
        status=new TextView(this);status.setTextSize(16);page.addView(status);
        importer=new Button(this);importer.setText("导入 EFF Mod 压缩包");importer.setOnClickListener(v->{Intent pick=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);startActivityForResult(pick,1);});page.addView(importer);
        selection=new TextView(this);selection.setTextSize(16);page.addView(selection);
        Button apply=new Button(this);apply.setText("应用所选 Mod");apply.setOnClickListener(v->{try{ModStore.applyDraft(this);render();}catch(Exception e){message(e.getMessage());}});page.addView(apply);
        Button off=new Button(this);off.setText("全部恢复原版");off.setOnClickListener(v->{try{ModStore.clear(this);displayed="";render();}catch(Exception e){message(e.getMessage());}});page.addView(off);
        library=new LinearLayout(this);library.setOrientation(LinearLayout.VERTICAL);page.addView(library);
        String[] packages={"com.gryphline.endfield.gp","com.hypergryph.endfield"};String[] names={"返回国际服","返回国服"};
        for(int i=0;i<packages.length;++i){String target=packages[i];Button launch=new Button(this);launch.setText(names[i]);launch.setOnClickListener(v->{Intent intent=getPackageManager().getLaunchIntentForPackage(target);if(intent!=null)startActivity(intent);else Toast.makeText(this,"未安装该客户端",Toast.LENGTH_SHORT).show();});page.addView(launch);}
        ScrollView scroll=new ScrollView(this);scroll.addView(page);setContentView(scroll);
    }
    private static java.util.Set<String> ids(JSONArray array)throws Exception{java.util.Set<String> set=new java.util.HashSet<>();for(int i=0;i<array.length();++i)set.add(array.getString(i));return set;}
    private void shapeControl(LinearLayout row,JSONObject spec,JSONObject values)throws Exception{
        double min=spec.getDouble("min"),max=spec.getDouble("max"),initial=values.optDouble(spec.getString("key"),spec.getDouble("default"));initial=Math.max(min,Math.min(max,initial));
        String label=spec.getString("label");TextView text=new TextView(this);text.setTextSize(16);text.setText(label+" · "+String.format(java.util.Locale.ROOT,"%.3f",initial));row.addView(text);
        SeekBar slider=new SeekBar(this);slider.setMax(1000);slider.setProgress((int)Math.round((initial-min)/(max-min)*1000));row.addView(slider);
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            @Override public void onProgressChanged(SeekBar bar,int progress,boolean user){text.setText(label+" · "+String.format(java.util.Locale.ROOT,"%.3f",min+(max-min)*progress/1000.0));}
            @Override public void onStartTrackingTouch(SeekBar bar){}
            @Override public void onStopTrackingTouch(SeekBar bar){try{ModStore.shape(MainActivity.this,spec,min+(max-min)*bar.getProgress()/1000.0);}catch(Exception e){message(e.getMessage());}}
        });
    }
    private void render(){try{
        JSONObject snapshot=ModStore.snapshot(this);if(!importing)status.setText(snapshot.getString("status")+"\n");
        java.util.Set<String> draft=ids(snapshot.getJSONArray("draft_ids")),active=ids(snapshot.getJSONArray("selected_ids"));selection.setText("已勾选 "+draft.size()+" / 4 · 已提交 "+active.size()+" 个 Mod");
        String data=snapshot.getJSONArray("library").toString()+snapshot.getJSONArray("draft_ids")+snapshot.getJSONArray("selected_ids");if(data.equals(displayed))return;displayed=data;library.removeAllViews();
        JSONArray entries=snapshot.getJSONArray("library");for(int i=0;i<entries.length();++i){JSONObject item=entries.getJSONObject(i);String id=item.getString("id");LinearLayout row=new LinearLayout(this);row.setOrientation(LinearLayout.VERTICAL);row.setPadding(0,12,0,24);
            CheckBox checkbox=new CheckBox(this);checkbox.setText(item.getString("name")+(active.contains(id)?" · 已提交":""));checkbox.setTextSize(18);checkbox.setChecked(draft.contains(id));
            checkbox.setOnCheckedChangeListener((button,checked)->{try{ModStore.stage(this,id,checked);displayed="";handler.post(this::render);}catch(Exception e){displayed="";handler.post(this::render);message(e.getMessage());}});row.addView(checkbox);
            if(item.has("controls")){JSONArray controls=item.getJSONArray("controls");if(controls.length()==0){TextView empty=new TextView(this);empty.setText("此 Mod 没有形态键");row.addView(empty);}else{
                for(int n=0;n<controls.length();++n)shapeControl(row,controls.getJSONObject(n),snapshot.getJSONObject("shape_values"));
                Button reset=new Button(this);reset.setText("恢复此 Mod 的形态默认值");reset.setOnClickListener(v->{try{for(int n=0;n<controls.length();++n){JSONObject spec=controls.getJSONObject(n);ModStore.shape(this,spec,spec.getDouble("default"));}displayed="";render();}catch(Exception e){message(e.getMessage());}});row.addView(reset);
            }}else{TextView hint=new TextView(this);hint.setText("首次应用并编译后显示形态键");row.addView(hint);}
            library.addView(row);
        }
    }catch(Exception e){status.setText("读取 Mod 库失败："+e.getMessage());}}
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request!=1||result!=RESULT_OK||data==null||data.getData()==null)return;
        importing=true;importer.setEnabled(false);status.setText("正在解压并校验 Mod…");worker.execute(()->{try{JSONObject item=ModStore.importZip(this,data.getData());runOnUiThread(()->message("已导入 "+item.optString("name")));}catch(Exception e){runOnUiThread(()->message("导入失败："+e.getMessage()));}finally{runOnUiThread(()->{importing=false;importer.setEnabled(true);displayed="";render();});}});
    }
    private void message(String text){new android.app.AlertDialog.Builder(this).setMessage(text).setPositiveButton("确定",null).show();}
    @Override protected void onResume(){super.onResume();displayed="";handler.post(refresh);}
    @Override protected void onPause(){handler.removeCallbacks(refresh);super.onPause();}
    @Override protected void onDestroy(){worker.shutdown();super.onDestroy();}
}
