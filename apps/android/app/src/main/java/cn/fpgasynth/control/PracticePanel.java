package cn.fpgasynth.control;

import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import cn.fpgasynth.control.core.PracticeSession;
import cn.fpgasynth.control.core.PracticeSong;
import java.util.List;

/** Visual practice observes accepted input; it has no output port or note callback. */
final class PracticePanel extends LinearLayout {
    interface Listener {void guides(int current,int next);void expanded(boolean open);int heldMask();}
    private final Listener listener;
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final PracticeSession model=new PracticeSession();
    private final List<PracticeSong> songs=PracticeSong.builtIns();
    private final LinearLayout body;
    private final Button expand,song,start,pause,end,quickPause,quickEnd;
    private final TextView tempo,status,legend;
    private final SeekBar speed;
    private final Strip strip;
    private boolean open,eligible,closed;
    private int rangeMask=(1<<25)-1,lastGuide=-1,lastNext=-1,route=-1;
    private long epoch=Long.MIN_VALUE;
    private final Runnable tick=()->update();

    PracticePanel(Context c,Listener listener){
        super(c);this.listener=listener;setOrientation(VERTICAL);setPadding(dp(10),dp(4),dp(10),dp(8));
        setBackground(ConsoleTheme.panel(ConsoleTheme.PANEL,ConsoleTheme.BORDER,dp(8)));
        model.select(songs.get(0));
        expand=button("节奏跟练",this::toggle);
        int headlineHeight=dp(getResources().getConfiguration().fontScale>=1.3f?72:56);
        LinearLayout headline=line();headline.addView(expand,new LayoutParams(0,headlineHeight,1));quickPause=button("暂停",()->{if(model.scene(SystemClock.elapsedRealtime()).phase==PracticeSession.Phase.PAUSED)begin();else{model.pause(SystemClock.elapsedRealtime());update();}});quickEnd=button("停止",this::stop);headline.addView(quickPause,new LayoutParams(dp(64),headlineHeight));headline.addView(quickEnd,new LayoutParams(dp(64),headlineHeight));addView(headline);
        body=new LinearLayout(c);body.setOrientation(VERTICAL);body.setVisibility(GONE);addView(body);
        song=button(model.song().title,this::chooseSong);body.addView(song,new LayoutParams(-1,-2));
        LinearLayout timing=line();tempo=text("",13,ConsoleTheme.INK);timing.addView(tempo,new LayoutParams(dp(84),-2));
        speed=new SeekBar(c);speed.setMax(140);speed.setProgress(model.bpm()-40);speed.setContentDescription("跟练速度，40到180 BPM");timing.addView(speed,new LayoutParams(0,dp(48),1));body.addView(timing);
        speed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){}public void onProgressChanged(SeekBar b,int n,boolean user){if(user){model.tempo(n+40);update();}}});
        LinearLayout actions=line();start=button("开始",this::begin);
        pause=button("暂停",()->{model.pause(SystemClock.elapsedRealtime());update();});end=button("停止",this::stop);
        for(Button b:new Button[]{start,pause,end}){LayoutParams lp=new LayoutParams(0,dp(48),1);lp.setMargins(dp(2),0,dp(2),0);actions.addView(b,lp);}body.addView(actions);
        status=text("连接ESP32后开始",13,ConsoleTheme.INK);status.setMinHeight(dp(46));status.setAccessibilityLiveRegion(ACCESSIBILITY_LIVE_REGION_POLITE);body.addView(status);
        strip=new Strip(c);body.addView(strip,new LayoutParams(-1,dp(86)));
        legend=text("紫色「弹」=引导 · 琥珀=触控 · 薄荷=发送/采用",11,ConsoleTheme.MUTED);body.addView(legend);
        update();
    }
    private int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private TextView text(String value,int size,int color){TextView t=new TextView(getContext());t.setText(value);t.setTextSize(size);t.setTextColor(color);return t;}
    private LinearLayout line(){LinearLayout l=new LinearLayout(getContext());l.setOrientation(HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    private Button button(String label,Runnable action){Button b=new Button(getContext());b.setText(label);b.setTextSize(14);b.setAllCaps(false);b.setTextColor(ConsoleTheme.GUIDE);b.setMinHeight(dp(48));b.setMinimumWidth(0);b.setPadding(dp(6),dp(4),dp(6),dp(4));b.setBackground(ConsoleTheme.button(ConsoleTheme.WELL,dp(6)));b.setOnClickListener(v->action.run());return b;}
    private void chooseSong(){String[] names=new String[songs.size()];for(int i=0;i<names.length;i++)names[i]=songs.get(i).title;
        new AlertDialog.Builder(getContext()).setTitle("节奏练习曲 · 内置离线").setItems(names,(d,index)->{model.select(songs.get(index));song.setText(model.song().title);speed.setProgress(model.bpm()-40);update();}).show();}
    boolean expanded(){return open;}
    private void toggle(){open=!open;body.setVisibility(open?VISIBLE:GONE);expand.setText(open?"收起跟练":"节奏跟练");expand.setContentDescription(open?"收起节奏跟练":"展开节奏跟练");listener.expanded(open);}
    private void begin(){if(!canStart())return;long now=SystemClock.elapsedRealtime();if(model.scene(now).phase==PracticeSession.Phase.PAUSED)model.resume(now);else model.start(now);model.baseline(listener.heldMask());open=false;body.setVisibility(GONE);listener.expanded(false);update();}
    private boolean supportsRange(){for(PracticeSong.Note n:model.song().notes)if(n.midi>=0&&(rangeMask&(1<<(n.midi-48)))==0)return false;return true;}
    private boolean canStart(){return eligible&&!closed&&supportsRange();}
    void state(boolean enabled,long inputEpoch,int performanceRoute,int availableMask){
        boolean changed=epoch!=inputEpoch||route!=performanceRoute||rangeMask!=availableMask;
        if(changed||!enabled){model.stop();ui.removeCallbacks(tick);}
        epoch=inputEpoch;route=performanceRoute;rangeMask=availableMask;
        boolean redraw=changed||eligible!=enabled;eligible=enabled;if(redraw)update();
    }
    void input(int mask,long now){if(eligible){model.input(mask,now);update();}}
    void stop(){model.stop();ui.removeCallbacks(tick);if(strip!=null)update();}
    void close(){closed=true;stop();}
    private void label(TextView v,String value){if(!value.contentEquals(v.getText()))v.setText(value);}
    private void update(){
        ui.removeCallbacks(tick);PracticeSession.Scene s=model.scene(SystemClock.elapsedRealtime());
        int current=eligible?s.guideMask:0,next=eligible?s.nextMask:0;
        if(lastGuide!=current||lastNext!=next){lastGuide=current;lastNext=next;listener.guides(current,next);}
        if(status==null)return;
        boolean active=s.running||s.phase==PracticeSession.Phase.PAUSED;quickPause.setVisibility(active?VISIBLE:GONE);quickEnd.setVisibility(active?VISIBLE:GONE);quickPause.setText(s.phase==PracticeSession.Phase.PAUSED?"继续":"暂停");
        String summary=open?"收起跟练":s.phase==PracticeSession.Phase.PAUSED?"已暂停 · 跟练设置":s.phase==PracticeSession.Phase.COUNT_IN?"预备 · 第 "+s.beat+" 拍":s.phase==PracticeSession.Phase.PLAYING?"第 "+s.beat+" 拍 · "+(current==0?"休止":"弹 "+cn.fpgasynth.control.core.PerformanceFeedback.notes(current)):s.phase==PracticeSession.Phase.FINISHED?"完成 · 命中 "+s.hits:"节奏跟练";
        if(!open&&s.running&&s.lastErrorMs!=null)summary+="\n"+(s.lastErrorMs<0?"提前 ":s.lastErrorMs>0?"稍迟 ":"合拍 ")+Math.abs(s.lastErrorMs)+" ms";
        label(expand,summary);
        expand.setContentDescription(expand.getText()+"，点击"+(open?"收起":"展开")+"跟练设置");
        label(tempo,model.bpm()+" BPM");start.setEnabled(canStart()&&!s.running);pause.setEnabled(s.running);end.setEnabled(s.phase!=PracticeSession.Phase.STOPPED);
        speed.setEnabled(s.phase==PracticeSession.Phase.STOPPED||s.phase==PracticeSession.Phase.FINISHED);song.setEnabled(!s.running);start.setText(s.phase==PracticeSession.Phase.PAUSED?"继续":"开始");
        String message;
        if(!eligible)message="连接后在演奏页开始；模拟模式可预览";
        else if(!supportsRange())message="曲目超出当前Pad音域，请切换键盘";
        else if(s.phase==PracticeSession.Phase.COUNT_IN)message="预备 · 第 "+s.beat+" 拍 / "+model.song().beatsPerBar;
        else if(s.phase==PracticeSession.Phase.PLAYING)message="第 "+s.bar+" 小节 · 第 "+s.beat+" 拍 · "+(current==0?"休止":"弹 "+cn.fpgasynth.control.core.PerformanceFeedback.notes(current));
        else if(s.phase==PracticeSession.Phase.PAUSED)message="已暂停 · 点继续保持进度";
        else if(s.phase==PracticeSession.Phase.FINISHED)message="完成 · 命中 "+s.hits+" / 漏音 "+s.misses+" / 错音 "+s.wrong;
        else message="1=C · "+model.song().beatsPerBar+"/4 · 预备一小节";
        if(s.lastErrorMs!=null)message+="\n触键 "+(s.lastErrorMs>0?"+":"")+s.lastErrorMs+" ms · 命中 "+s.hits;
        else if(s.wrong>0&&s.running)message+="\n错音 "+s.wrong+" · 漏音 "+s.misses;
        label(status,message);strip.scene=s;strip.invalidate();
        strip.setContentDescription("简谱，"+model.song().title+"，"+message);
        if(s.running&&!closed)ui.postDelayed(tick,32);
    }
    private final class Strip extends View {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);PracticeSession.Scene scene;
        Strip(Context c){super(c);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);}
        @Override protected void onDraw(Canvas canvas){
            if(scene==null)return;PracticeSong tune=model.song();int current=Math.max(0,scene.currentIndex),first=current/4*4;
            float width=getWidth()/4f;paint.setTypeface(Typeface.DEFAULT);paint.setTextAlign(Paint.Align.CENTER);
            for(int i=0;i<4&&first+i<tune.notes.size();i++){
                PracticeSong.Note note=tune.notes.get(first+i);float x=i*width;boolean active=scene.phase==PracticeSession.Phase.PLAYING&&first+i==scene.currentIndex;
                paint.setColor(active?0xff3a3055:ConsoleTheme.WELL);canvas.drawRoundRect(x+dp(2),dp(3),x+width-dp(2),getHeight()-dp(4),dp(6),dp(6),paint);
                paint.setColor(active?ConsoleTheme.GUIDE:ConsoleTheme.INK);paint.setTextSize(dp(21));canvas.drawText(PracticeSong.degree(note.midi),x+width/2,dp(34),paint);
                paint.setTextSize(dp(11));String length=note.durationTicks==240?"½拍":note.durationTicks==480?"1拍":note.durationTicks==720?"1½拍":(note.durationTicks/480.0)+"拍";
                canvas.drawText(active?(note.midi<0?"休止":"弹"):length,x+width/2,dp(57),paint);
                if(active){paint.setColor(ConsoleTheme.GUIDE);float fraction=(float)scene.beatFraction;canvas.drawRect(x+dp(5),getHeight()-dp(10),x+dp(5)+(width-dp(10))*fraction,getHeight()-dp(6),paint);}
            }
        }
    }
}
