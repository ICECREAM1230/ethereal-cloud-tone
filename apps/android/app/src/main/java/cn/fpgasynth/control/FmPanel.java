package cn.fpgasynth.control;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.widget.*;
import android.os.SystemClock;
import cn.fpgasynth.control.core.*;
import cn.fpgasynth.control.generated.FmContract;
import java.util.*;

/** Controls come only from the verified generated catalog and the current native layout. */
final class FmPanel extends LinearLayout {
    private static final int INK=Color.rgb(235,242,239),MINT=Color.rgb(159,225,199),MUTED=Color.rgb(158,181,186);
    private final Activity activity;private final AppController session;
    private final TextView status;private final LinearLayout controls;
    private final List<Row> rows=new ArrayList<>();
    private AppController.View current;private Button preset;
    private String identity="";private int voice,patchOperator,voiceOperator;
    FmPanel(Activity a,AppController s){super(a);activity=a;session=s;setOrientation(VERTICAL);setPadding(dp(12),dp(12),dp(12),dp(12));
        status=text("FM —",14,MINT);addView(status);controls=new LinearLayout(a);controls.setOrientation(VERTICAL);addView(controls);}
    private int dp(int v){return Math.round(v*getResources().getDisplayMetrics().density);}
    private TextView text(String value,int size,int color){TextView t=new TextView(activity);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setPadding(0,dp(5),0,dp(5));return t;}
    private Button button(String value,Runnable action){Button b=new Button(activity);b.setText(value);b.setTextColor(MINT);b.setAllCaps(false);b.setOnClickListener(v->action.run());return b;}
    private boolean unchanged(long epoch){return session.view().inputEpoch==epoch;}
    private static String sessionIdentity(AppController.View v){
        if(v.v2==null||v.catalog==null)return "";
        FmApi.Fm f=v.v2.fm;return v.token+":"+v.v2.context+":"+f.session+":"+f.epoch+":"+f.topology+":"+v.catalog.profile.id+":"+Arrays.toString(f.operators);
    }
    private boolean sameSession(String identity){return !identity.isEmpty()&&identity.equals(sessionIdentity(session.view()));}
    void update(AppController.View v){
        current=v;
        if(v.v2==null){status.setText("FM —");controls.setVisibility(GONE);return;}
        FmApi.Fm f=v.v2.fm;
        status.setText(f.profile==null||v.catalog==null?"FM 未就绪":"FM · "+f.voiceCount+" 声部 / "+f.patchCount+" 算子");
        if(v.catalog==null){controls.setVisibility(GONE);return;}
        controls.setVisibility(VISIBLE);
        String key=v.catalog.profile.id+":"+v.v2.context+":"+f.session+":"+f.topology+":"+Arrays.toString(f.operators);
        if(!identity.equals(key)){identity=key;voice=patchOperator=voiceOperator=0;rebuild();}
        if(preset!=null)preset.setEnabled(v.fmEditable&&f.hasFeature(FmContract.FM_FEATURE_APPLY_PRESET));
        for(Row r:rows)r.update(v);
    }
    private void rebuild(){
        controls.removeAllViews();rows.clear();if(current==null||current.catalog==null)return;
        FmApi.Fm f=current.v2.fm;FmApi.Catalog cat=current.catalog;
        if(f.voiceCount>0)controls.addView(button("声部 "+(voice+1),()->{
            String[] labels=new String[f.voiceCount];for(int i=0;i<labels.length;i++)labels[i]="声部 "+(i+1)+" · "+f.operators[i]+" 算子";
            String stamp=sessionIdentity(current);new AlertDialog.Builder(activity).setTitle("声部").setItems(labels,(d,i)->{if(sameSession(stamp)){voice=i;voiceOperator=0;rebuild();}}).show();
        }));
        if(f.patchCount>0)controls.addView(operatorButton("Patch 算子",f.patchCount,true));
        boolean voiceScoped=false;for(FmContract.Parameter p:cat.parameters)if(p.scope_id==FmContract.FM_SCOPE_VOICE_OPERATOR)voiceScoped=true;
        if(voiceScoped&&f.voiceCount>0)controls.addView(operatorButton("声部算子",f.operators[voice],false));
        preset=button("选择 FM 预设",()->{
            String[] labels=new String[cat.presets.size()];for(int i=0;i<labels.length;i++)labels[i]=cat.presets.get(i).name;
            String stamp=sessionIdentity(current);new AlertDialog.Builder(activity).setTitle("FM 预设").setItems(labels,(d,i)->{if(sameSession(stamp)&&!session.fmPreset(cat.presets.get(i).id))Toast.makeText(activity,"等待设备就绪",Toast.LENGTH_SHORT).show();}).show();
        });preset.setEnabled(current.fmEditable&&f.hasFeature(FmContract.FM_FEATURE_APPLY_PRESET));controls.addView(preset);
        for(FmContract.Parameter p:cat.parameters){Row row=new Row(p);rows.add(row);controls.addView(row.box);row.update(current);}
    }
    private Button operatorButton(String label,int count,boolean patch){
        FmContract.Profile p=current.catalog.profile;int chosen=patch?patchOperator:voiceOperator;
        return button(label+" · "+p.operatorLabel(chosen),()->{
            String[] labels=new String[count];for(int i=0;i<count;i++)labels[i]=p.operatorLabel(i);
            String stamp=sessionIdentity(current);new AlertDialog.Builder(activity).setTitle(label).setItems(labels,(d,i)->{if(sameSession(stamp)){if(patch)patchOperator=i;else voiceOperator=i;rebuild();}}).show();
        });
    }
    private final class Row {
        final FmContract.Parameter parameter;final LinearLayout box;final TextView value;final SeekBar slider;
        boolean editing;long touchEpoch,protectedUntil;int touchVoice,touchOperator;
        Row(FmContract.Parameter p){parameter=p;box=new LinearLayout(activity);box.setOrientation(VERTICAL);
            String scope=p.scope_id==FmContract.FM_SCOPE_PATCH?"Patch":p.scope_id==FmContract.FM_SCOPE_PATCH_OPERATOR?"Patch 算子":p.scope_id==FmContract.FM_SCOPE_VOICE?"声部":"声部算子";
            box.addView(text(p.name+" · "+scope+" · "+p.minimum+"—"+p.maximum,14,INK));value=text("—",12,MINT);box.addView(value);
            slider=new SeekBar(activity);slider.setMax((int)(p.maximum-p.minimum));slider.setProgressTintList(ColorStateList.valueOf(MINT));slider.setContentDescription(p.name+" chip_code");box.addView(slider,new LinearLayout.LayoutParams(-1,dp(48)));
            slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
                public void onStartTrackingTouch(SeekBar s){editing=true;touchEpoch=current.inputEpoch;touchVoice=voice;touchOperator=operator();}
                public void onStopTrackingTouch(SeekBar s){editing=false;if(unchanged(touchEpoch)){protectedUntil=SystemClock.elapsedRealtime()+500;boolean ok=session.fmParameter(p.id,touchVoice,touchOperator,s.getProgress()+p.minimum);value.setText(ok?"等待采用":"当前不可调");}}
                public void onProgressChanged(SeekBar s,int n,boolean user){if(user)value.setText("请求 "+(p.minimum+n));}
            });
        }
        int operator(){return parameter.scope_id==FmContract.FM_SCOPE_PATCH_OPERATOR?patchOperator:voiceOperator;}
        long target(){long v=(parameter.scope_id==FmContract.FM_SCOPE_VOICE||parameter.scope_id==FmContract.FM_SCOPE_VOICE_OPERATOR)?voice:FmContract.FM_TARGET_NONE;
            long o=(parameter.scope_id==FmContract.FM_SCOPE_PATCH_OPERATOR||parameter.scope_id==FmContract.FM_SCOPE_VOICE_OPERATOR)?operator():FmContract.FM_TARGET_NONE;return FmContract.packTarget(v,o);}
        void update(AppController.View v){slider.setEnabled(v.fmEditable);if(editing||SystemClock.elapsedRealtime()<protectedUntil)return;
            Map<String,Object> matched=null;long target=target();for(Map<String,Object> item:v.v2.fm.parameterValues)if(FmApi.u32(item.get("parameter_id"))==parameter.id&&FmApi.u32(item.get("target_word"))==target)matched=item;
            if(matched==null){value.setText("—");return;}
            long n=FmApi.u32(matched.get("value_u32"));slider.setProgress((int)(n-parameter.minimum));value.setText(Long.toString(n));
        }
    }
}
