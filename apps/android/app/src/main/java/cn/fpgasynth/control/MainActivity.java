package cn.fpgasynth.control;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.pm.PackageManager;
import android.os.Build;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.text.TextUtils;
import cn.fpgasynth.control.core.ControlGesture;
import cn.fpgasynth.control.core.PerformanceFeedback;
import cn.fpgasynth.control.core.MotionInstrument;
import cn.fpgasynth.control.core.MidiMessage;
import cn.fpgasynth.control.core.MidiOutput;
import cn.fpgasynth.control.core.SensorMidiMapper;
import cn.fpgasynth.control.core.MidiControlAdapter;
import cn.fpgasynth.control.core.AppController;
import cn.fpgasynth.control.core.ControlPort;
import cn.fpgasynth.control.core.ControlPorts;
import cn.fpgasynth.control.core.BleMidiPort;
import cn.fpgasynth.control.core.FmApi;
import cn.fpgasynth.control.core.DemoDevice;
import cn.fpgasynth.control.core.DeviceState;
import cn.fpgasynth.control.core.PatchControls;
import cn.fpgasynth.control.core.Protocol;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.LinkedHashMap;
import java.io.IOException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Native views and native multitouch piano. No WebView, cloud service or audio simulation. */
public final class MainActivity extends Activity {
    private static final int BG=ConsoleTheme.BG,CARD=ConsoleTheme.PANEL,INK=ConsoleTheme.INK,
            MINT=ConsoleTheme.TEAL,MUTED=ConsoleTheme.MUTED,AMBER=ConsoleTheme.AMBER;
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ScheduledExecutorService io=Executors.newSingleThreadScheduledExecutor();
    private final List<ParameterRow> rows=new ArrayList<>();
    private AppController session;
    private ConnectivityManager cm;
    private ConnectivityManager.NetworkCallback wifiCallback;
    private LinearLayout root,play,sound,connection,header,pianoCard;
    private FrameLayout content;
    private ScrollView[] pages;
    private TextView phase,feedback,banner,engineInfo,decayText,raw,soundHint;
    private Button dds,ks,fm,stop,connect,disconnect;
    private FmPanel fmPanel;
    private LinearLayout ddsSound;
    private SeekBar decay;
    private EditText address;
    private KeyboardView keyboard;
    private int velocity=100,selected=0;
    private volatile boolean destroyed;
    private boolean decayEditing,decaySync;
    private final Button[] tabs=new Button[4];
    private PracticePanel practicePanel;
    private LinearLayout ksControls;
    private ScoreLibraryPanel scoreLibrary;
    private volatile boolean demo,foreground;
    private volatile WifiTransport deviceTransport;
    private volatile BleGattLink bleLink;
    private ControlPorts ports;
    private volatile ControlPort.Mode modeChoice=ControlPort.Mode.WIFI;
    private final AtomicLong connectionIntent=new AtomicLong();
    private final BleDeviceScanner scanner=new BleDeviceScanner();
    private boolean connectionBusy;
    private LinearLayout wifiSection,bluetoothSection;
    private Button wifiMode,bluetoothMode;
    private EditText bluetoothAddress;
    private TextView portNotice;
    private AlertDialog scanDialog;
    private String permissionAction;
    private long permissionIntent;
    private int permissionOutcome;
    private static final int BLE_PERMISSIONS=701,BLE_ENABLE=702;
    private final PerformanceFeedback feedbackModel=new PerformanceFeedback();
    private final List<SynthSketch> sketches=new ArrayList<>();
    private final List<LinearLayout> racks=new ArrayList<>();
    private PerformanceView performance;
    private PerformanceView ksPerformance;
    private PluckedView plucked;
    private LinearLayout ksRack,ksWood,masterConsole;
    private Button strumUp,strumDown,ksKeyboardButton;
    private SeekBar strumSpacing,ksVelocity,velocityControl;
    private TextView strumText,ksVelocityText;
    private boolean ksDockVisible,ksLayoutActive;
    private int keyboardMask,pluckedMask;
    private long surfaceEpoch=-1;
    private PadView pads;
    private LinearLayout ddsConsole,ksConsole,fmConsole,directFmSound;
    private FrameLayout surface;
    private Button demoQuick,surfaceChoice,focusButton;
    private TextView velocityText;
    private boolean padMode=BuildConfig.PAD_EDITION,focusSurface=BuildConfig.PAD_EDITION;
    private boolean motionMode,motionFault,motionTickScheduled;
    private final MotionInstrument motionModel=new MotionInstrument();
    private MotionSource motionSensors;
    private MotionPanel motionPanel;
    private volatile MotionInstrument.Frame motionFrame;
    private int motionRotation=-1;
    private AlertDialog surfaceDialog;
    private AlertDialog midiMappingDialog;
    private final SensorMidiMapper midiMapper=new SensorMidiMapper();
    private MidiOutput midiOutput;
    private int rollMapping=SensorMidiMapper.TILT_NOTES,pitchMapping=SensorMidiMapper.TILT_OFF;
    private String midiPreview="MIDI —";
    private volatile MidiMessage lastMidiMessage;
    private final Runnable motionTick=new Runnable(){public void run(){motionTickScheduled=false;if(destroyed||!foreground)return;syncMotion();}};
    private LinearLayout headerLine,navigation,engineButtons;

    @Override public void onCreate(Bundle b){
        super.onCreate(b);
        cm=getSystemService(ConnectivityManager.class);
        session=new AppController(v->ui.post(()->{if(!destroyed)render(session.view());}));
        ports=new ControlPorts(session);
        rollMapping=getPreferences(MODE_PRIVATE).getInt("sensor_midi_roll",SensorMidiMapper.TILT_NOTES);pitchMapping=getPreferences(MODE_PRIVATE).getInt("sensor_midi_pitch",SensorMidiMapper.TILT_OFF);
        try{midiMapper.configure(rollMapping,pitchMapping);}catch(IllegalArgumentException e){rollMapping=SensorMidiMapper.TILT_NOTES;pitchMapping=SensorMidiMapper.TILT_OFF;midiMapper.configure(rollMapping,pitchMapping);}
        midiOutput=new MidiControlAdapter(new MidiControlAdapter.Target(){
            public boolean notes(int mask,int strength,long now){return session.keys(mask,strength,now);}
            public boolean cc(int cc,int value){return (cc!=74||session.view().applied==0)&&session.parameter(cc,value);}
        });
        motionSensors=new MotionSensors(this,ui,this::acceptMotion);
        build();render(session.view());
        io.scheduleWithFixedDelay(()->ports.tick(SystemClock.elapsedRealtime()),0,25,TimeUnit.MILLISECONDS);
        wifiCallback=new ConnectivityManager.NetworkCallback(){
            @Override public void onLost(Network n){if(!demo && modeChoice==ControlPort.Mode.WIFI && deviceTransport!=null && deviceTransport.uses(n))session.networkLost();}
        };
        NetworkRequest request=new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).build();
        cm.registerNetworkCallback(request,wifiCallback);
    }
    private int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    private LinearLayout line(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    private GradientDrawable shape(int fill,int stroke,float radius){GradientDrawable g=new GradientDrawable();g.setColor(fill);g.setCornerRadius(dp(radius));if(stroke!=0)g.setStroke(dp(1),stroke);return g;}
    private TextView text(String t,int size,int color){TextView v=new TextView(this);v.setText(t);v.setTextSize(size);v.setTextColor(color);v.setLineSpacing(dp(3),1);return v;}
    private void space(LinearLayout l,int h){View v=new View(this);l.addView(v,new LinearLayout.LayoutParams(1,dp(h)));}
    private TextView title(LinearLayout l,String t){TextView v=text(t,16,INK);v.setTypeface(null,Typeface.BOLD);l.addView(v);space(l,8);return v;}
    private LinearLayout card(LinearLayout parent){LinearLayout l=column();l.setPadding(dp(16),dp(16),dp(16),dp(16));l.setBackground(shape(CARD,Color.rgb(45,64,77),16));
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.bottomMargin=dp(14);parent.addView(l,p);return l;}
    private Button button(String label,int tint,Runnable action){Button b=new Button(this){@Override public void setEnabled(boolean enabled){super.setEnabled(enabled);setAlpha(enabled?1f:.4f);}};b.setText(label);b.setTextColor(tint);b.setTextSize(14);b.setAllCaps(false);b.setMinHeight(dp(48));
        b.setBackground(ConsoleTheme.button(BG,dp(7)));b.setPadding(dp(9),dp(5),dp(9),dp(5));b.setOnClickListener(v->action.run());return b;}
    private void weight(LinearLayout l,View v){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);p.setMargins(dp(3),dp(3),dp(3),dp(3));l.addView(v,p);}
    private ScrollView scroll(LinearLayout l){ScrollView s=new ScrollView(this);s.setFillViewport(false);s.setClipToPadding(false);s.setPadding(dp(12),dp(10),dp(12),dp(12));s.addView(l,new ScrollView.LayoutParams(-1,-2));return s;}
    private void build(){
        root=column();root.setBackgroundColor(BG);
        root.setOnApplyWindowInsetsListener((v,in)->{root.setPadding(in.getSystemWindowInsetLeft(),in.getSystemWindowInsetTop(),in.getSystemWindowInsetRight(),in.getSystemWindowInsetBottom());return in;});
        header=column();header.setPadding(dp(14),dp(6),dp(14),dp(4));
        LinearLayout hl=line();headerLine=hl;TextView name=text(BuildConfig.PAD_EDITION?"掌心 · Pad":"掌心 · 合成器",20,INK);name.setTypeface(null,Typeface.BOLD);name.setSingleLine(true);name.setEllipsize(TextUtils.TruncateAt.END);hl.addView(name,new LinearLayout.LayoutParams(0,-2,1));
        phase=text("未连接",12,AMBER);phase.setPadding(dp(10),dp(7),dp(10),dp(7));phase.setBackground(shape(CARD,0,20));hl.addView(phase);header.addView(hl);
        phase.setMinHeight(dp(48));phase.setGravity(Gravity.CENTER);phase.setOnClickListener(v->select(2));phase.setContentDescription("连接状态，点击打开连接设置");
        stop=button("全部停止",AMBER,()->{clearSurfaces();session.panic();});stop.setPadding(dp(4),dp(4),dp(4),dp(4));stop.setContentDescription("停止App、网页和MIDI全部来源的声音");hl.addView(stop,new LinearLayout.LayoutParams(dp(96),dp(48)));
        feedback=text("",11,MUTED);feedback.setSingleLine(true);feedback.setEllipsize(TextUtils.TruncateAt.END);header.addView(feedback);root.addView(header);
        banner=text("",12,AMBER);banner.setGravity(Gravity.CENTER);banner.setPadding(dp(12),dp(5),dp(12),dp(5));root.addView(banner);
        content=new FrameLayout(this);root.addView(content,new LinearLayout.LayoutParams(-1,0,1));
        play=column();sound=column();connection=column();buildPlay();buildSound();buildConnection();
        scoreLibrary=new ScoreLibraryPanel(this);
        pages=new ScrollView[]{scroll(play),scroll(sound),scroll(connection),scroll(scoreLibrary)};
        root.addView(pianoCard,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout nav=line();navigation=nav;nav.setPadding(dp(4),dp(4),dp(4),dp(4));
        String[] names={"演奏","音色","连接","曲谱"};for(int i=0;i<names.length;i++){final int tab=i;tabs[i]=button(names[i],MUTED,()->select(tab));weight(nav,tabs[i]);}
        root.addView(nav);root.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->{if(b-t!=ob-ot)fitSurface();});setContentView(root);select(0);
    }
    private void select(int tab){
        if(selected!=tab){clearSurfaces();session.releaseNotes();for(ParameterRow r:rows)r.cancel();}
        selected=tab;content.removeAllViews();content.addView(pages[tab],new FrameLayout.LayoutParams(-1,-1));
        for(int i=0;i<tabs.length;i++){tabs[i].setTextColor(i==tab?BG:MUTED);tabs[i].setBackground(shape(i==tab?MINT:CARD,0,10));}
        pianoCard.setVisibility(tab>=2?View.GONE:View.VISIBLE);
        compactLayout();
    }
    private void buildPlay(){
        LinearLayout buttons=line();engineButtons=buttons;
        dds=button("减法合成器",MINT,()->session.mode(0));ks=button("KS 拨弦",MINT,()->session.mode(1));fm=button("FM",MINT,()->session.mode(2));weight(buttons,dds);weight(buttons,ks);weight(buttons,fm);play.addView(buttons);
        engineInfo=text("",11,MUTED);engineInfo.setVisibility(View.GONE);play.addView(engineInfo);
        practicePanel=new PracticePanel(this,new PracticePanel.Listener(){
            public void guides(int current,int next){if(keyboard!=null)keyboard.guide(current,next);if(pads!=null)pads.guide(current,next);if(plucked!=null)plucked.guide(current,next);}
            public void expanded(boolean open){if(open)focusSurface=false;compactLayout();if(!open&&pages!=null)pages[0].post(()->pages[0].scrollTo(0,0));}
            public int heldMask(){return keyboardMask|pluckedMask;}
        });play.addView(practicePanel,new LinearLayout.LayoutParams(-1,-2));space(play,8);
        LinearLayout offline=line();demoQuick=button("进入模拟体验",AMBER,()->queuePortAction(null,true));weight(offline,demoQuick);weight(offline,button("连接设备",MINT,()->select(2)));play.addView(offline);space(play,8);
        ddsConsole=column();play.addView(ddsConsole);
        LinearLayout top=rack(ddsConsole);LinearLayout osc1=module(top,"振荡器 1"),osc2=module(top,"振荡器 2"),filter=module(top,"滤波器");
        sketch(osc1,"osc1",64);controls(osc1,120,20);controls(osc1,82,14,15,25);
        sketch(osc2,"osc2",64);controls(osc2,120,21);controls(osc2,82,22,23,85);
        sketch(filter,"filter",64);controls(filter,110,29,30);controls(filter,100,74,71);
        LinearLayout env=rack(ddsConsole);LinearLayout amp=module(env,"音量包络"),mod=module(env,"调制包络");
        sketch(amp,"amp",72);controls(amp,96,73,75,79,72);sketch(mod,"mod",72);controls(mod,96,102,103,104,105,107);
        LinearLayout modulation=rack(ddsConsole);LinearLayout lfo1=module(modulation,"LFO 1"),lfo2=module(modulation,"LFO 2"),mix=module(modulation,"混合与空间");
        sketch(lfo1,"lfo1",64);controls(lfo1,90,113,76,77);sketch(lfo2,"lfo2",64);controls(lfo2,90,111,109,110,112);controls(mix,96,24,26,27,28);
        ksConsole=column();play.addView(ksConsole);ksRack=rack(ksConsole);ksWood=module(ksRack,"拨弦演奏");
        plucked=new PluckedView(this,this::sendPluckedKeys);ksWood.addView(plucked,new LinearLayout.LayoutParams(-1,dp(368)));
        LinearLayout touchStrength=line();ksVelocityText=text("力度 100",12,MUTED);touchStrength.addView(ksVelocityText,new LinearLayout.LayoutParams(dp(75),-2));
        ksVelocity=new SeekBar(this);ksVelocity.setMax(126);ksVelocity.setProgress(99);ksVelocity.setProgressTintList(ColorStateList.valueOf(AMBER));ksVelocity.setContentDescription("拨弦力度 1 到 127");touchStrength.addView(ksVelocity,new LinearLayout.LayoutParams(0,dp(48),1));ksWood.addView(touchStrength);
        ksVelocity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}public void onProgressChanged(SeekBar s,int n,boolean user){if(user){velocity=n+1;session.velocity(velocity);ksVelocityText.setText("力度 "+velocity);velocityText.setText("力度 "+velocity);velocityControl.setProgress(n);updatePerformance(session.view());}}});
        ksControls=module(ksRack,"弦与输出");
        decayText=text("衰减 T60 · 等待设备",13,MINT);ksControls.addView(decayText);
        decay=new SeekBar(this);decay.setMax(7800);decay.setProgressTintList(ColorStateList.valueOf(MINT));ksControls.addView(decay,new LinearLayout.LayoutParams(-1,dp(48)));
        decay.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onStartTrackingTouch(SeekBar s){decayEditing=true;}
            public void onStopTrackingTouch(SeekBar s){decayEditing=false;session.decay(s.getProgress()+200);}
            public void onProgressChanged(SeekBar s,int value,boolean fromUser){if(fromUser&&!decaySync)decayText.setText("衰减 T60 · "+String.format(java.util.Locale.CHINA,"%.2f 秒 · 请求",(value+200)/1000.0));}
        });
        controls(ksControls,112,7,10);
        strumText=text("扫弦间隔 45 ms",12,MUTED);ksControls.addView(strumText);
        strumSpacing=new SeekBar(this);strumSpacing.setMax(140);strumSpacing.setProgress(25);strumSpacing.setContentDescription("扫弦间隔 20 到 160 毫秒");strumSpacing.setProgressTintList(ColorStateList.valueOf(AMBER));ksControls.addView(strumSpacing,new LinearLayout.LayoutParams(-1,dp(48)));
        strumSpacing.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}public void onProgressChanged(SeekBar s,int n,boolean user){plucked.interval(20+n);strumText.setText("扫弦间隔 "+(20+n)+" ms");}});
        LinearLayout sweep=line();strumDown=button("下扫",AMBER,()->plucked.strum(false));strumUp=button("上扫",AMBER,()->plucked.strum(true));weight(sweep,strumDown);weight(sweep,strumUp);ksControls.addView(sweep);
        ksKeyboardButton=button("显示键盘",MINT,()->{clearSurfaces();session.releaseNotes();ksDockVisible=!ksDockVisible;if(!ksDockVisible){motionMode=false;motionFault=false;}focusSurface=false;compactLayout();});ksControls.addView(ksKeyboardButton,new LinearLayout.LayoutParams(-1,-2));
        ksPerformance=new PerformanceView(this,feedbackModel);ksConsole.addView(ksPerformance,0,new LinearLayout.LayoutParams(-1,dp(40)));
        fmConsole=card(play);title(fmConsole,"FM 合成器");fmConsole.addView(button("预设 / 算子",MINT,()->select(1)));
        masterConsole=card(play);title(masterConsole,"主输出");controls(masterConsole,136,7,10);
        pianoCard=column();pianoCard.setBackground(shape(CARD,ConsoleTheme.BORDER,8));pianoCard.setPadding(dp(12),dp(4),dp(12),dp(6));
        LinearLayout tools=line();velocityText=text("力度 100",12,MUTED);tools.addView(velocityText,new LinearLayout.LayoutParams(dp(75),-2));
        SeekBar strength=new SeekBar(this);velocityControl=strength;strength.setMax(126);strength.setProgress(99);strength.setProgressTintList(ColorStateList.valueOf(MINT));
        strength.setContentDescription("演奏力度 1 到 127");tools.addView(strength,new LinearLayout.LayoutParams(0,dp(48),1));
        strength.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}
            public void onProgressChanged(SeekBar s,int n,boolean user){if(user){velocity=n+1;session.velocity(velocity);velocityText.setText("力度 "+velocity);ksVelocityText.setText("力度 "+velocity);ksVelocity.setProgress(n);updatePerformance(session.view());}}
        });
        surfaceChoice=button("键盘",MINT,this::chooseInstrument);tools.addView(surfaceChoice,new LinearLayout.LayoutParams(dp(88),dp(48)));
        focusButton=button(focusSurface?"控制台":"聚焦演奏",MUTED,()->{clearSurfaces();session.releaseNotes();focusSurface=!focusSurface;compactLayout();});tools.addView(focusButton,new LinearLayout.LayoutParams(dp(104),dp(48)));pianoCard.addView(tools);
        performance=new PerformanceView(this,feedbackModel);pianoCard.addView(performance,new LinearLayout.LayoutParams(-1,dp(40)));
        surface=new FrameLayout(this);keyboard=new KeyboardView(this,(mask,now)->!padMode&&!motionMode&&sendKeys(mask,now));pads=new PadView(this,(mask,now)->padMode&&!motionMode&&sendKeys(mask,now));
        motionPanel=new MotionPanel(this,new MotionPanel.Listener(){public void calibrate(){calibrateMotion();}public void toggle(){toggleMotion();}public void mapping(){showMidiMapping();}});
        surface.addView(keyboard,new FrameLayout.LayoutParams(-1,-1));surface.addView(pads,new FrameLayout.LayoutParams(-1,-1));surface.addView(motionPanel,new FrameLayout.LayoutParams(-1,-1));pianoCard.addView(surface,new LinearLayout.LayoutParams(-1,dp(144)));
        pianoCard.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->fitSurface());
    }
    private boolean sendKeys(int mask,long now){return sendSurfaceKeys(false,mask,now);}
    private boolean sendPluckedKeys(int mask,long now){return sendSurfaceKeys(true,mask,now);}
    private boolean sendSurfaceKeys(boolean wood,int mask,long now){
        AppController.View v=session.view();if(surfaceEpoch!=v.inputEpoch){surfaceEpoch=v.inputEpoch;keyboardMask=pluckedMask=0;plucked.clear();keyboard.clear();pads.clear();return false;}
        if(selected>=2||!foreground||(wood&&(selected!=0||v.performanceApplied!=1||motionMode))||(!wood&&mask!=0&&!pianoCard.isShown()))return false;
        if(wood)pluckedMask=mask;else keyboardMask=mask;
        boolean accepted=session.keys(keyboardMask|pluckedMask,velocity,now);if(!accepted)keyboardMask=pluckedMask=0;else if(practicePanel!=null)practicePanel.input(keyboardMask|pluckedMask,now);updatePerformance(session.view());return accepted;
    }
    private void clearSurfaces(){if(practicePanel!=null)practicePanel.stop();keyboardMask=pluckedMask=0;if(plucked!=null)plucked.clear();if(keyboard!=null)keyboard.clear();if(pads!=null)pads.clear();motionModel.stop("已停止");stopMidi();}
    private void chooseSurface(boolean next){chooseInstrumentKind(next?1:0);}
    private void chooseInstrument(){
        surfaceDialog=new AlertDialog.Builder(this).setTitle("演奏方式").setItems(new String[]{"键盘","打击垫","体感"},(dialog,index)->chooseInstrumentKind(index)).create();surfaceDialog.show();
    }
    private void chooseInstrumentKind(int kind){
        clearSurfaces();session.releaseNotes();padMode=kind==1;motionMode=kind==2;ksDockVisible=true;motionFault=false;compactLayout();syncMotion();
    }
    private boolean motionActive(){return motionMode&&foreground&&hasWindowFocus()&&selected<2&&!destroyed;}
    private void scheduleMotion(){if(!motionTickScheduled&&motionMode&&foreground&&!destroyed){motionTickScheduled=true;ui.postDelayed(motionTick,20);}}
    private void syncMotion(){
        if(motionSensors==null||motionPanel==null)return;
        boolean active=motionActive();int rotation=getWindowManager().getDefaultDisplay().getRotation();
        if(motionSensors.running()&&(!active||motionRotation!=rotation)){motionSensors.stop();motionModel.reset("请校准");}
        if(active&&!motionSensors.running()&&motionSensors.available()&&!motionFault){
            motionModel.reset("请校准");motionRotation=rotation;motionFault=!motionSensors.start(rotation);
        }
        pushMotion();scheduleMotion();
    }
    private void acceptMotion(double roll,double pitch,double force,long sampledAt,long now){
        if(!motionActive()||!motionSensors.running())return;
        AppController.View v=session.view();motionModel.frame(now,v.inputEpoch,v.playable,velocity);
        motionModel.sample(roll,pitch,force,sampledAt,now);pushMotion();
    }
    private void pushMotion(){
        if(motionPanel==null)return;long now=SystemClock.elapsedRealtime();AppController.View v=session.view();
        boolean compatible=midiCompatible(v);motionFrame=motionModel.frame(now,v.inputEpoch,motionActive()&&v.playable&&compatible,velocity);
        if(!sendMidi(midiMapper.update(motionFrame,v.inputEpoch,motionActive()&&v.playable&&compatible,now),now)){
            motionModel.stop("等待连接");midiMapper.stop();session.releaseNotes();midiOutput.reset();v=session.view();motionFrame=motionModel.frame(now,v.inputEpoch,false,velocity);
        }
        motionPanel.update(motionFrame,motionSensors.available()&&!motionFault,v.playable,motionSensors.source(),compatible,midiPreview,rollMapping==SensorMidiMapper.TILT_NOTES?motionFrame.note:60);
        if(motionMode)updatePerformance(session.view());
    }
    private boolean midiCompatible(AppController.View v){return (rollMapping!=74&&pitchMapping!=74)||v.applied==0;}
    private boolean sendMidi(List<MidiMessage> events,long now){
        if(midiOutput==null)return events.isEmpty();
        for(MidiMessage event:events){MidiMessage wire=event;AppController.View v=session.view();if(v.directMidi)wire=channelMessage(event,v.midiChannel+1);lastMidiMessage=wire;midiPreview=String.format(java.util.Locale.ROOT,"MIDI %02X %02X %02X",wire.status,wire.data1,wire.data2);if(!midiOutput.send(event,now))return false;}
        return true;
    }
    private static MidiMessage channelMessage(MidiMessage message,int channel){switch(message.status&0xf0){case 0x90:return MidiMessage.noteOn(channel,message.data1,message.data2);case 0x80:return MidiMessage.noteOff(channel,message.data1);case 0xb0:return MidiMessage.controlChange(channel,message.data1,message.data2);default:return MidiMessage.pitchBend(channel,message.data1|(message.data2<<7));}}
    private void stopMidi(){if(midiOutput==null)return;if(!sendMidi(midiMapper.stop(),SystemClock.elapsedRealtime())&&session!=null)session.releaseNotes();midiOutput.reset();}
    private String midiTarget(int value){return value==0?"音符 C3–C5":value<0?"关闭":value==7?"CC7 · 音量":value==10?"CC10 · 声像":"CC74 · 滤波";}
    private void showMidiMapping(){
        midiMappingDialog=new AlertDialog.Builder(this).setTitle("MIDI 映射 · 通道 "+(session.view().directMidi?session.view().midiChannel+1:1)).setItems(new String[]{"左右倾斜 → "+midiTarget(rollMapping),"前后倾斜 → "+midiTarget(pitchMapping)},(d,index)->{
            final int[] values=index==0?new int[]{0,74,10,7,-1}:new int[]{-1,74,10,7};String[] labels=new String[values.length];for(int i=0;i<values.length;i++)labels[i]=midiTarget(values[i]);
            midiMappingDialog=new AlertDialog.Builder(this).setTitle(index==0?"左右倾斜":"前后倾斜").setItems(labels,(dialog,which)->configureSensorMidi(index==0?values[which]:rollMapping,index==1?values[which]:pitchMapping)).create();midiMappingDialog.show();
        }).create();midiMappingDialog.show();
    }
    private void configureSensorMidi(int roll,int pitch){
        clearSurfaces();session.releaseNotes();
        try{midiMapper.configure(roll,pitch);}catch(IllegalArgumentException e){android.widget.Toast.makeText(this,"两个轴请选择不同 CC",android.widget.Toast.LENGTH_SHORT).show();return;}
        rollMapping=roll;pitchMapping=pitch;getPreferences(MODE_PRIVATE).edit().putInt("sensor_midi_roll",roll).putInt("sensor_midi_pitch",pitch).apply();motionModel.reset("请校准");syncMotion();
    }
    private void calibrateMotion(){
        if(!motionActive())return;motionModel.stop("请校准");stopMidi();session.releaseNotes();long now=SystemClock.elapsedRealtime();
        if(motionModel.calibrate(now))midiMapper.calibrate(motionModel.frame(now,session.view().inputEpoch,false,velocity).pitch);pushMotion();
    }
    private void toggleMotion(){
        if(!motionActive())return;long now=SystemClock.elapsedRealtime();AppController.View v=session.view();
        if(motionFrame!=null&&motionFrame.armed){motionModel.stop("已停止");stopMidi();session.releaseNotes();}
        else{stopMidi();now=SystemClock.elapsedRealtime();v=session.view();if(motionModel.arm(v.inputEpoch,v.playable&&midiCompatible(v),velocity,now))midiMapper.arm(v.inputEpoch,now);}pushMotion();
    }
    private LinearLayout rack(LinearLayout parent){LinearLayout r=line();r.setGravity(Gravity.TOP);racks.add(r);parent.addView(r,new LinearLayout.LayoutParams(-1,-2));return r;}
    private LinearLayout module(LinearLayout parent,String name){LinearLayout m=column();m.setPadding(dp(10),dp(10),dp(10),dp(10));m.setBackground(shape(CARD,ConsoleTheme.BORDER,7));title(m,name);parent.addView(m);return m;}
    private void sketch(LinearLayout parent,String kind,int height){SynthSketch s=new SynthSketch(this,kind);sketches.add(s);parent.addView(s,new LinearLayout.LayoutParams(-1,dp(height)));space(parent,6);}
    private void controls(LinearLayout parent,int min,int...ccs){ControlGrid grid=new ControlGrid(this,min,4);for(int cc:ccs)for(PatchControls.Control c:PatchControls.all())if(c.cc==cc){ParameterRow row=new ParameterRow(c,true);rows.add(row);grid.addView(row.box);break;}parent.addView(grid,new LinearLayout.LayoutParams(-1,-2));space(parent,6);}
    private void buildSound(){
        soundHint=text("",13,MUTED);soundHint.setVisibility(View.GONE);sound.addView(soundHint);
        fmPanel=new FmPanel(this,session);sound.addView(fmPanel);ddsSound=column();sound.addView(ddsSound);
        directFmSound=column();sound.addView(directFmSound);LinearLayout directMaster=card(directFmSound);title(directMaster,"FM MIDI");controls(directMaster,136,7,10);
        String group="";ControlGrid grid=null;
        for(PatchControls.Control c:PatchControls.all()){
            if(!group.equals(c.group)){group=c.group;LinearLayout box=card(ddsSound);title(box,group);grid=new ControlGrid(this,150,4);box.addView(grid,new LinearLayout.LayoutParams(-1,-2));}
            ParameterRow row=new ParameterRow(c,false);rows.add(row);grid.addView(row.box);
        }
    }
    private void buildConnection(){
        LinearLayout choose=card(connection);title(choose,"控制连接方式");LinearLayout modes=line();
        wifiMode=button("Wi-Fi 控制",MINT,()->chooseMode(ControlPort.Mode.WIFI));
        bluetoothMode=button("蓝牙控制",MINT,()->chooseMode(ControlPort.Mode.BLUETOOTH));
        weight(modes,wifiMode);weight(modes,bluetoothMode);choose.addView(modes);
        portNotice=text("未连接",12,AMBER);space(choose,8);choose.addView(portNotice);
        wifiSection=column();connection.addView(wifiSection);
        LinearLayout guide=card(wifiSection);title(guide,"Wi-Fi 连接");
        guide.addView(button("打开 Wi-Fi 设置",MINT,()->startActivity(new Intent(Settings.ACTION_WIFI_SETTINGS))),new LinearLayout.LayoutParams(-1,-2));
        LinearLayout target=card(wifiSection);title(target,"设备地址");
        address=new EditText(this);address.setTextColor(INK);address.setTextSize(15);address.setSingleLine(true);address.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_VARIATION_URI);
        address.setText(getPreferences(MODE_PRIVATE).getString("address","http://192.168.4.1"));address.setContentDescription("设备 HTTP 地址");target.addView(address,new LinearLayout.LayoutParams(-1,dp(54)));
        bluetoothSection=column();connection.addView(bluetoothSection);
        LinearLayout ble=card(bluetoothSection);title(ble,"蓝牙 MIDI");
        bluetoothAddress=new EditText(this);bluetoothAddress.setTextColor(INK);bluetoothAddress.setTextSize(15);bluetoothAddress.setSingleLine(true);
        bluetoothAddress.setHint("设备地址");bluetoothAddress.setContentDescription("蓝牙设备地址");
        bluetoothAddress.setText(getPreferences(MODE_PRIVATE).getString("ble_address",""));ble.addView(bluetoothAddress,new LinearLayout.LayoutParams(-1,dp(54)));
        space(ble,8);ble.addView(button("扫描设备",MINT,()->bleAction("scan")),new LinearLayout.LayoutParams(-1,-2));
        LinearLayout action=card(connection);connect=button("连接设备",MINT,this::connectSelected);action.addView(connect,new LinearLayout.LayoutParams(-1,-2));space(action,8);
        disconnect=button("断开",MUTED,()->queuePortAction(null,false));action.addView(disconnect,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout local=card(connection);
        local.addView(button("进入模拟体验",AMBER,()->queuePortAction(null,true)),new LinearLayout.LayoutParams(-1,-2));
        LinearLayout details=card(connection);
        raw=text("等待设备状态",11,MUTED);raw.setTypeface(Typeface.MONOSPACE);raw.setTextIsSelectable(true);raw.setVisibility(View.GONE);
        TextView help=text("旋钮上下拖动，点读值可精确输入。切换连接会松键并断开旧通道。\n蓝牙直接发送 MIDI；参数显示本地发送值，不读回设备状态。发送不表示已发声。\n模拟模式不连接硬件、不输出音频。图形为参数示意。",12,MUTED);help.setVisibility(View.GONE);
        details.addView(button("帮助 / 诊断",MUTED,()->{boolean show=raw.getVisibility()!=View.VISIBLE;raw.setVisibility(show?View.VISIBLE:View.GONE);help.setVisibility(show?View.VISIBLE:View.GONE);}),new LinearLayout.LayoutParams(-1,-2));details.addView(help);details.addView(raw);
        connection.addView(text("v"+BuildConfig.VERSION,11,MUTED));
        updatePortUi();
    }
    private void updatePortUi(){
        boolean wifi=modeChoice==ControlPort.Mode.WIFI;
        wifiSection.setVisibility(wifi?View.VISIBLE:View.GONE);bluetoothSection.setVisibility(wifi?View.GONE:View.VISIBLE);
        wifiMode.setSelected(wifi);bluetoothMode.setSelected(!wifi);
        wifiMode.setTextColor(wifi?BG:MUTED);wifiMode.setBackground(shape(wifi?MINT:CARD,0,10));
        bluetoothMode.setTextColor(wifi?MUTED:BG);bluetoothMode.setBackground(shape(wifi?CARD:MINT,0,10));
    }
    private void stopScan(){scanner.stop();if(scanDialog!=null){scanDialog.dismiss();scanDialog=null;}}
    private void chooseMode(ControlPort.Mode next){
        if(modeChoice==next&&!demo)return;stopScan();modeChoice=next;updatePortUi();queuePortAction(null,false);
    }
    private interface PortFactory {ControlPort create()throws IOException;}
    private void queuePortAction(PortFactory factory,boolean local){
        cancelBleAction();
        stopScan();final long intent=connectionIntent.incrementAndGet();final ControlPort.Mode selectedMode=modeChoice;
        clearSurfaces();session.pause();connectionBusy=true;portNotice.setText("切换中…");render(session.view());
        io.execute(()->{
            String notice;boolean open=false;
            try{
                ports.select(selectedMode,SystemClock.elapsedRealtime());deviceTransport=null;bleLink=null;
                if(intent!=connectionIntent.get()||destroyed||!foreground)return;
                if(local){ports.connectDemo(new DemoDevice(SystemClock::elapsedRealtime),SystemClock.elapsedRealtime());demo=true;open=true;notice="模拟";}
                else if(factory!=null){ControlPort port=factory.create();
                    if(intent!=connectionIntent.get()||destroyed||!foreground){port.close();return;}
                    demo=false;ports.connect(port,SystemClock.elapsedRealtime());open=true;
                    notice="连接中…";
                }else{demo=false;notice="未连接";}
                if(intent!=connectionIntent.get()||destroyed||!foreground){ports.disconnect(SystemClock.elapsedRealtime());return;}
            }catch(Exception e){ports.disconnect(SystemClock.elapsedRealtime());notice="连接未启动："+e.getMessage();}
            final String message=notice;final boolean showPlay=open;
            ui.post(()->{if(!destroyed&&intent==connectionIntent.get()){connectionBusy=false;portNotice.setText(message);if(showPlay)select(0);render(session.view());}});
        });
    }
    private void connectSelected(){
        if(modeChoice==ControlPort.Mode.BLUETOOTH){bleAction("connect");return;}
        try{String url=Protocol.endpoint(address.getText().toString());address.setText(url);getPreferences(MODE_PRIVATE).edit().putString("address",url).apply();
            queuePortAction(()->{WifiTransport transport=new WifiTransport(cm,url);deviceTransport=transport;return transport;},false);
        }catch(IllegalArgumentException e){address.setError(e.getMessage());}
    }
    private String[] blePermissions(){return Build.VERSION.SDK_INT>=31?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}:new String[]{Manifest.permission.ACCESS_FINE_LOCATION};}
    private boolean hasBlePermissions(){for(String p:blePermissions())if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED)return false;return true;}
    private void bleAction(String action){
        if(modeChoice!=ControlPort.Mode.BLUETOOTH||destroyed)return;
        if(!hasBlePermissions()){permissionAction=action;permissionIntent=connectionIntent.get();permissionOutcome=0;requestPermissions(blePermissions(),BLE_PERMISSIONS);return;}
        try{
            BluetoothManager manager=getSystemService(BluetoothManager.class);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
            if(adapter==null){portNotice.setText("此设备不支持蓝牙，Wi-Fi 模式仍可使用。");return;}
            if(!adapter.isEnabled()){permissionAction=action;permissionIntent=connectionIntent.get();permissionOutcome=0;
                startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE).addFlags(Intent.FLAG_ACTIVITY_NO_USER_ACTION),BLE_ENABLE);return;}
            if(action.equals("scan")){scanBle(manager,adapter);return;}
            String value=bluetoothAddress.getText().toString().trim().toUpperCase(Locale.ROOT);
            if(!BluetoothAdapter.checkBluetoothAddress(value)){bluetoothAddress.setError("请先扫描选择设备，或输入完整蓝牙地址。");return;}
            bluetoothAddress.setText(value);getPreferences(MODE_PRIVATE).edit().putString("ble_address",value).apply();
            queuePortAction(()->{BleGattLink link=new BleGattLink(this,value);bleLink=link;return new BleMidiPort(link);},false);
        }catch(SecurityException e){portNotice.setText("蓝牙权限未授予或已撤回。");}
    }
    private void scanBle(BluetoothManager manager,BluetoothAdapter adapter){
        stopScan();LinearLayout body=column();body.setPadding(dp(20),dp(10),dp(20),dp(8));TextView status=text("已知设备与附近控制服务 · 扫描10秒",13,MUTED);body.addView(status);
        final LinkedHashMap<String,Button> devices=new LinkedHashMap<>();
        scanDialog=new AlertDialog.Builder(this).setTitle("选择蓝牙控制设备").setView(scroll(body)).setNegativeButton("取消",(d,w)->scanner.stop()).create();
        scanDialog.setOnDismissListener(d->scanner.stop());scanDialog.show();
        try{scanner.start(manager,adapter,new BleDeviceScanner.Listener(){
            public void found(String name,String address,String source){if(destroyed||modeChoice!=ControlPort.Mode.BLUETOOTH)return;
                addBleCandidate(devices,body,name,address,source);}
            public void ended(String message){status.setText(message);}
        });}catch(IOException e){status.setText(e.getMessage());}
    }
    private void addBleCandidate(LinkedHashMap<String,Button> devices,LinearLayout body,String name,String address,String source){
        Button b=devices.get(address);if(b==null){b=button("",MINT,()->{bluetoothAddress.setText(address);stopScan();portNotice.setText("已选择设备，请连接蓝牙 MIDI。");});devices.put(address,b);body.addView(b,new LinearLayout.LayoutParams(-1,-2));}
        b.setText(name+"\n"+address+" · "+source);
    }
    @Override public void onRequestPermissionsResult(int code,String[] permissions,int[] results){
        super.onRequestPermissionsResult(code,permissions,results);if(code!=BLE_PERMISSIONS)return;
        permissionOutcome=hasBlePermissions()?1:2;completeBleAction();
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(scoreLibrary!=null&&scoreLibrary.onActivityResult(request,result,data))return;if(request!=BLE_ENABLE)return;
        permissionOutcome=result==RESULT_OK?1:3;completeBleAction();
    }
    private void cancelBleAction(){permissionAction=null;permissionOutcome=0;}
    private void completeBleAction(){
        if(permissionAction==null)return;
        if(destroyed||modeChoice!=ControlPort.Mode.BLUETOOTH||permissionIntent!=connectionIntent.get()){cancelBleAction();return;}
        if(!foreground||permissionOutcome==0)return;
        String action=permissionAction;int outcome=permissionOutcome;cancelBleAction();
        if(outcome==1)bleAction(action);
        else portNotice.setText(outcome==2?"未获得蓝牙权限；请在系统设置授权后重试。":"已取消启用蓝牙，可重新扫描或连接。");
    }
    private void render(AppController.View v){
        if(practicePanel!=null)practicePanel.state(v.playable&&foreground&&selected==0&&!motionMode&&!connectionBusy,v.inputEpoch,v.performanceApplied,padMode?0xffff:(1<<25)-1);
        boolean ksRoute=v.performanceApplied==1;
        if(ksLayoutActive!=ksRoute){ksLayoutActive=ksRoute;if(ksRoute){if(padMode||motionMode)ksDockVisible=true;focusSurface=false;}compactLayout();return;}
        if(surfaceEpoch!=v.inputEpoch){surfaceEpoch=v.inputEpoch;keyboardMask=pluckedMask=0;}
        phase.setText(v.demo?"模拟":(modeChoice==ControlPort.Mode.WIFI?"Wi-Fi":"蓝牙")+" · "+(connectionBusy?"切换中":v.phase));phase.setTextColor(v.connected?MINT:AMBER);feedback.setText(v.message);
        banner.setVisibility(View.GONE);
        boolean error=!v.connected&&(v.phase.contains("中断")||v.phase.contains("失败"));
        feedback.setVisibility(error?View.VISIBLE:View.GONE);
        boolean playing=v.playable&&selected<2&&foreground;
        boolean dockShown=pianoCard.getVisibility()==View.VISIBLE;
        keyboard.state(playing&&dockShown&&!padMode&&!motionMode,v.ownerMask,v.inputEpoch,v.directMidi);pads.state(playing&&dockShown&&padMode&&!motionMode,v.ownerMask,v.inputEpoch,v.directMidi);
        boolean woodPlaying=playing&&selected==0&&ksRoute&&!motionMode;plucked.state(woodPlaying,v.ownerMask,v.inputEpoch,v.directMidi);strumUp.setEnabled(woodPlaying);strumDown.setEnabled(woodPlaying);syncMotion();updatePerformance(v);stop.setEnabled(v.connected);stop.setText(v.directMidi?"停止演奏":"全部停止");stop.setContentDescription(v.directMidi?"发送蓝牙MIDI停音控制":"停止App、网页和MIDI全部来源的声音");
        disconnect.setEnabled(ports.isOpen()||v.demo||connectionBusy);connect.setEnabled(!connectionBusy);
        connect.setText(modeChoice==ControlPort.Mode.BLUETOOTH?(v.connected&&!v.demo?"重新连接蓝牙":"连接蓝牙 MIDI"):(v.connected&&!v.demo?"重新连接设备":"连接设备"));
        DeviceState s=v.state;boolean isFm=v.applied==2;
        engineInfo.setVisibility(v.channelRoutes&&s!=null?View.VISIBLE:View.GONE);
        dds.setEnabled(v.canSwitch&&(v.caps==null||v.caps.performanceAvailable(0)));
        ks.setEnabled(v.canSwitch&&s!=null&&(v.directMidi||(v.caps==null?s.available:v.caps.performanceAvailable(1))));fm.setEnabled(v.fmSelectable);
        boolean inlineEngine=engineButtons.getParent()==headerLine;
        modeButton(dds,v.performanceApplied==0,inlineEngine||getResources().getConfiguration().screenWidthDp<600?"减法":"减法合成器");modeButton(ks,v.performanceApplied==1,inlineEngine?"KS":"KS 拨弦");modeButton(fm,v.performanceApplied==2,"FM");
        ((View)demoQuick.getParent()).setVisibility(v.connected?View.GONE:View.VISIBLE);
        boolean practiceFocus=practicePanel.expanded();ddsConsole.setVisibility(!practiceFocus&&!ksRoute&&v.requested<=0?View.VISIBLE:View.GONE);ksConsole.setVisibility(ksRoute?View.VISIBLE:View.GONE);fmConsole.setVisibility(!practiceFocus&&!ksRoute&&v.requested==2&&!v.directMidi?View.VISIBLE:View.GONE);masterConsole.setVisibility(ksRoute||practiceFocus?View.GONE:View.VISIBLE);
        for(SynthSketch sketch:sketches)sketch.state(s,v.requested==0&&v.editable);
        if(s!=null){
            engineInfo.setText(v.directMidi?(engineLabel(v.performanceApplied)+" · MIDI CH"+(v.midiChannel+1)+" · 单向发送"):v.channelRoutes?("演奏 "+engineLabel(v.performanceApplied)+" · MIDI "+(v.midiChannel+1)+"  |  编辑 "+engineLabel(v.applied)):
                    v.v2!=null?("控制 v2 · context "+v.v2.context+" · requested "+v.requested+" / applied "+v.applied+" · 实际发声未验证"):s.recovering?"设备恢复中，请等待后重新按键":s.switching?"切换中，等待模式读回":s.fault!=0?"当前模式已读回 · 上次故障记录见诊断":"当前模式已读回 · 实际发声未验证");
            if(!decayEditing){decaySync=true;decay.setProgress(Math.max(0,Math.min(7800,s.decay-200)));decayText.setText(String.format(java.util.Locale.CHINA,"衰减 T60 · %.2f 秒",s.decay/1000.0));decaySync=false;}
            soundHint.setText(isFm?"先选择目标算子，再调节参数。":v.requested==1?"KS 可调主音量、声像和演奏页 T60。":"设备当前减法音色 · 旋钮上下拖动，点读值精确输入。");
            for(ParameterRow r:rows){if(isFm&&!v.directMidi){if(r.control.cc==7||r.control.cc==10)r.updateFm(v);else r.disable();}else r.update(s,v.editable&&v.ccAvailable(r.control.cc)&&(v.requested==0||r.control.cc==7||r.control.cc==10),v.directMidi,v.ccRequests.get(r.control.cc));}
            if(raw.getVisibility()==View.VISIBLE){String modern="";if(v.v2!=null){FmApi.Fm f=v.v2.fm;modern="\ncontrol context="+v.v2.context+"\nFM profile="+f.profileId+" backend="+f.backendId+" native session="+f.session+" epoch="+f.epoch+" topology="+f.topology+" flags="+f.flags+" phase="+f.phase+"\n";}
                raw.setText(v.directMidi?("BLE MIDI CH"+(v.midiChannel+1)+" · 单向发送\nlocal="+v.localMask+" sent="+v.ownerMask+" queued="+v.queueSize+"\n以下为本地参数值，未读取设备状态\n"+s.json):("client="+v.client+" token="+v.token+" control seq="+v.sequence+"\nlocal="+v.localMask+" owner="+v.ownerMask+" queued="+v.queueSize+modern+"\n"+(v.v2!=null?v.v2.json:s.json)));}
        }else for(ParameterRow r:rows)r.disable();
        fmPanel.update(v);fmPanel.setVisibility(!v.directMidi&&(isFm||v.requested==2)?View.VISIBLE:View.GONE);ddsSound.setVisibility(isFm?View.GONE:View.VISIBLE);directFmSound.setVisibility(v.directMidi&&isFm?View.VISIBLE:View.GONE);
        decay.setEnabled(v.editable&&s!=null&&v.requested==1&&!v.directMidi);if(v.directMidi&&ksRoute)decayText.setText("衰减由ESP32设置");else if(ksRoute&&v.requested!=1)decayText.setText("衰减 · 当前编辑器非KS");
        decay.setVisibility(ksRoute?View.VISIBLE:View.GONE);decayText.setVisibility(ksRoute?View.VISIBLE:View.GONE);
        if(v.playable)getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    private static String engineLabel(int engine){return engine==0?"减法":engine==1?"KS":engine==2?"FM":"未映射";}
    private void modeButton(Button button,boolean active,String label){if(!button.getText().toString().equals(label))button.setText(label);if(button.isSelected()!=active){button.setSelected(active);button.setTextColor(active?BG:MINT);button.setBackground(ConsoleTheme.button(active?MINT:BG,dp(7)));}}
    private void updatePerformance(AppController.View v){if(performance==null)return;int strength=motionMode&&motionFrame!=null?motionFrame.velocity:velocity;feedbackModel.update(v.inputEpoch,v.playable&&foreground&&selected<2,v.demo,v.localMask,v.ownerMask,strength,SystemClock.elapsedRealtime(),v.directMidi);performance.changed();if(ksPerformance!=null)ksPerformance.changed();}
    private void compactLayout(){
        if(pianoCard==null||header==null)return;
        android.content.res.Configuration config=getResources().getConfiguration();boolean wide=config.screenWidthDp>=780;
        LinearLayout practiceHome=wide&&ksLayoutActive?ksControls:play;
        if(practicePanel.getParent()!=practiceHome){((ViewGroup)practicePanel.getParent()).removeView(practicePanel);practiceHome.addView(practicePanel,practiceHome==ksControls?2:play.indexOfChild(engineInfo)+1,new LinearLayout.LayoutParams(-1,-2));}
        boolean shortScreen=shortScreen(),strip=config.screenWidthDp>=1024&&config.fontScale<1.3f,inlineNav=strip||shortScreen;
        if(inlineNav&&navigation.getParent()!=headerLine){root.removeView(navigation);headerLine.addView(navigation,1,new LinearLayout.LayoutParams(dp(288),dp(56)));}
        else if(!inlineNav&&navigation.getParent()==headerLine){headerLine.removeView(navigation);root.addView(navigation,new LinearLayout.LayoutParams(-1,-2));}
        if(strip&&engineButtons.getParent()!=headerLine){play.removeView(engineButtons);headerLine.addView(engineButtons,2,new LinearLayout.LayoutParams(dp(282),dp(48)));}
        else if(!strip&&engineButtons.getParent()==headerLine){headerLine.removeView(engineButtons);play.addView(engineButtons,0,new LinearLayout.LayoutParams(-1,-2));}
        feedback.setVisibility(View.GONE);
        for(LinearLayout rack:racks){rack.setOrientation(wide?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);
            for(int i=0;i<rack.getChildCount();i++){View child=rack.getChildAt(i);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(wide?0:-1,-2,wide?(rack==ksRack&&child==ksWood?2:1):0);int side=rack==ksRack&&!wide?0:dp(4);lp.setMargins(side,dp(4),side,dp(4));child.setLayoutParams(lp);}}
        ksWood.getChildAt(0).setVisibility(wide?View.VISIBLE:View.GONE);ksWood.getChildAt(1).setVisibility(wide?View.VISIBLE:View.GONE);
        boolean hideDock=selected==0&&ksLayoutActive&&!ksDockVisible;pianoCard.setVisibility(selected>=2||hideDock?View.GONE:View.VISIBLE);ksKeyboardButton.setText(ksDockVisible?"收起键盘":"显示键盘");
        boolean hide=selected==0&&focusSurface&&!hideDock;content.setVisibility(hide?View.GONE:View.VISIBLE);LinearLayout.LayoutParams contentParams=(LinearLayout.LayoutParams)content.getLayoutParams();contentParams.weight=hide?0:1;content.setLayoutParams(contentParams);
        pads.columns(shortScreen?8:4);
        int normal=(padMode||motionMode)?(shortScreen?112:264):shortScreen?100:config.orientation==android.content.res.Configuration.ORIENTATION_LANDSCAPE?112:156;
        int focused=Math.max(padMode||motionMode?256:156,config.screenHeightDp-230);
        surface.setLayoutParams(new LinearLayout.LayoutParams(-1,dp(focusSurface&&selected==0?focused:normal)));
        keyboard.setVisibility(!padMode&&!motionMode?View.VISIBLE:View.GONE);pads.setVisibility(padMode?View.VISIBLE:View.GONE);motionPanel.setVisibility(motionMode?View.VISIBLE:View.GONE);
        surfaceChoice.setText(motionMode?"体感":padMode?"打击垫":"键盘");surfaceChoice.setContentDescription("演奏方式，当前"+surfaceChoice.getText()+"，点按选择键盘、打击垫或体感");focusButton.setText(focusSurface?"控制台":"聚焦演奏");
        fitSurface();
        if(session!=null)render(session.view());
    }
    private boolean shortScreen(){android.content.res.Configuration c=getResources().getConfiguration();return c.screenWidthDp>=640&&c.screenHeightDp<=520;}
    private void fitSurface(){
        if(selected>=2||root.getHeight()==0||pianoCard.getVisibility()!=View.VISIBLE)return;
        int available=root.getHeight()-root.getPaddingTop()-root.getPaddingBottom()-header.getHeight()-(banner.getVisibility()==View.VISIBLE?banner.getHeight():0)-(navigation.getParent()==root?navigation.getHeight():0)-dp(100);
        boolean focused=focusSurface&&selected==0,shortScreen=shortScreen();int normal=padMode||motionMode?(shortScreen?112:264):getResources().getConfiguration().orientation==android.content.res.Configuration.ORIENTATION_LANDSCAPE?112:156;
        int minimum=dp(padMode||motionMode?(shortScreen?112:224):100),limit=available-(focused?0:dp(72));
        if(available>0){LinearLayout.LayoutParams lp=(LinearLayout.LayoutParams)surface.getLayoutParams();int size=Math.max(minimum,Math.min(focused?available:dp(normal),limit));if(lp.height!=size){lp.height=size;surface.setLayoutParams(lp);}}
    }
    @Override public void onConfigurationChanged(android.content.res.Configuration c){super.onConfigurationChanged(c);clearSurfaces();session.releaseNotes();for(ParameterRow row:rows)row.cancel();compactLayout();}
    private final class ParameterRow {
        final PatchControls.Control control;final LinearLayout box=column();final TextView value;
        final ControlGesture gesture=new ControlGesture();
        ConsoleKnob knob;Button choice,down,up;int requested=-1;long protectedUntil;
        AlertDialog editorDialog;
        ParameterRow(PatchControls.Control c,boolean compact){
            control=c;TextView label=text(c.label,compact?12:14,INK);label.setMaxLines(2);label.setMinHeight(dp(20));if(c.choices==null||!compact)box.addView(label);value=text("—",11,MUTED);value.setMinHeight(dp(48));value.setMaxLines(2);value.setGravity(Gravity.CENTER);value.setEllipsize(TextUtils.TruncateAt.END);
            if(c.choices==null){
                knob=new ConsoleKnob(MainActivity.this,new ConsoleKnob.Listener(){
                    public void begin(){gesture.begin(knob.value(),session.view().inputEpoch,SystemClock.elapsedRealtime());}
                    public void change(int n){AppController.View v=session.view();Integer send=gesture.move(n,v.inputEpoch,SystemClock.elapsedRealtime(),canEdit(v));if(!gesture.editing())return;requested=n;protectedUntil=SystemClock.elapsedRealtime()+1800;line("请求 "+n+" / 127",AMBER);if(send!=null&&!session.parameter(c.cc,send))cancel();}
                    public void end(boolean cancelled){if(cancelled){gesture.cancel();requested=-1;return;}AppController.View v=session.view();Integer send=gesture.end(v.inputEpoch,SystemClock.elapsedRealtime(),canEdit(v));if(send!=null&&!session.parameter(c.cc,send))cancel();}
                });
                knob.setEnabled(false);knob.setContentDescription(c.group+"，"+c.label+"，上下拖动或方向键调节，刻度0到127");box.addView(knob,new LinearLayout.LayoutParams(-1,dp(compact?68:92)));box.addView(value);
                value.setOnClickListener(v->precise());value.setContentDescription(c.label+"精确输入");
                if(!compact){LinearLayout adjust=MainActivity.this.line();down=button("−1",MUTED,()->knob.step(-1));up=button("+1",MUTED,()->knob.step(1));down.setEnabled(false);up.setEnabled(false);weight(adjust,down);weight(adjust,up);box.addView(adjust);}
            }else {
                choice=button("—",MINT,()->new AlertDialog.Builder(MainActivity.this).setTitle(c.label).setItems(c.choices,(dialog,i)->session.parameter(c.cc,c.values[i])).show());
                choice.setContentDescription(c.group+"，"+c.label);choice.setEnabled(false);box.addView(choice,new LinearLayout.LayoutParams(-1,-2));
            }
        }
        boolean canEdit(AppController.View v){return v.ccAvailable(control.cc)&&(v.applied==2&&!v.directMidi?v.fmEditable&&(control.cc==7||control.cc==10):v.editable&&(v.requested==0||control.cc==7||control.cc==10));}
        void line(String message,int color){if(!value.getText().toString().equals(message))value.setText(message);value.setTextColor(color);}
        void cancel(){gesture.cancel();requested=-1;protectedUntil=0;if(knob!=null)knob.cancel();}
        void precise(){if(knob==null||!knob.isEnabled())return;AppController.View initial=session.view();final long connection=connectionIntent.get(),token=initial.token,context=initial.v2==null?0:initial.v2.context;final int engine=initial.requested;
            EditText input=new EditText(MainActivity.this);input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);input.setText(Integer.toString(knob.value()));input.setSelectAllOnFocus(true);
            AlertDialog dialog=new AlertDialog.Builder(MainActivity.this).setTitle(control.label+" · 刻度0–127").setView(input).setPositiveButton("应用",null).setNegativeButton("取消",null).create();editorDialog=dialog;dialog.setOnDismissListener(d->editorDialog=null);dialog.show();
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{int n=Integer.parseInt(input.getText().toString().trim());if(n<0||n>127)throw new NumberFormatException();AppController.View current=session.view();
                if(connection!=connectionIntent.get()||token!=current.token||engine!=current.requested||context!=(current.v2==null?0:current.v2.context)){input.setError("连接或引擎已变化，请重新打开调节。");return;}
                if(!canEdit(current)||!session.parameter(control.cc,n)){input.setError("设备暂不可调，输入已保留，请等待就绪后重试。");return;}
                requested=n;protectedUntil=SystemClock.elapsedRealtime()+1800;knob.value(n,true);line("请求 "+n+" / 127",AMBER);dialog.dismiss();
            }catch(NumberFormatException e){input.setError("请输入0到127的整数。");}});
        }
        void closeEditor(){if(editorDialog!=null)editorDialog.dismiss();}
        void updateFm(AppController.View v){
            if(knob!=null){boolean enabled=v.fmEditable&&v.ccAvailable(control.cc);knob.setEnabled(enabled);if(down!=null){down.setEnabled(enabled);up.setEnabled(enabled);}Integer n=v.ccRequests.get(control.cc);if(!knob.editing()){if(n!=null){knob.value(n,false);line("请求 "+n,MUTED);}else{knob.unknown();line("—",MUTED);}}}
        }
        void disable(){cancel();if(knob!=null){knob.setEnabled(false);knob.unknown();}if(choice!=null)choice.setEnabled(false);if(down!=null){down.setEnabled(false);up.setEnabled(false);}line("—",MUTED);}
        void update(DeviceState s,boolean enabled){update(s,enabled,false,null);}
        void update(DeviceState s,boolean enabled,boolean localOnly,Integer sentValue){
            if(knob!=null)knob.setEnabled(enabled);if(choice!=null)choice.setEnabled(enabled);if(down!=null){down.setEnabled(enabled);up.setEnabled(enabled);}if(!enabled)cancel();
            if(knob!=null&&knob.editing())return;
            try{int actual=localOnly&&sentValue!=null?sentValue:control.value(s);boolean waiting=requested>=0&&actual!=requested&&SystemClock.elapsedRealtime()<protectedUntil;
                if(!waiting)requested=-1;
                line(localOnly?(waiting?"排队 "+requested+" · "+(sentValue!=null?"已发送 ":"本地 ")+actual:(sentValue!=null?"已发送 ":"本地默认 ")+control.display(s)):(waiting?"请求 "+requested+" · 设备 "+actual:control.display(s)),waiting?AMBER:MINT);
                if(knob!=null)knob.value(waiting?requested:actual,waiting);if(choice!=null){String text=(localOnly?(sentValue!=null?"已发送 · ":"本地默认 · "):"")+control.display(s);if(!choice.getText().toString().equals(text))choice.setText(text);}
            }catch(RuntimeException e){disable();line(localOnly?"本地未定义此参数":"设备未提供此参数",MUTED);}
        }
    }
    @Override protected void onPause(){
        foreground=false;stopScan();clearSurfaces();motionSensors.stop();motionModel.reset("请校准");ui.removeCallbacks(motionTick);motionTickScheduled=false;if(surfaceDialog!=null)surfaceDialog.dismiss();if(midiMappingDialog!=null)midiMappingDialog.dismiss();for(ParameterRow row:rows){row.cancel();row.closeEditor();}session.pause();pushMotion();updatePerformance(session.view());
        if(permissionAction==null)connectionIntent.incrementAndGet();connectionBusy=false;
        if(modeChoice==ControlPort.Mode.BLUETOOTH)retirePort(false);
        super.onPause();
    }
    private void retirePort(boolean finish){
        Runnable abort=ports.abortHandle();
        io.execute(()->{try{ports.disconnect(SystemClock.elapsedRealtime());}finally{if(finish)io.shutdown();}});
        ui.postDelayed(abort,1500);
    }
    @Override protected void onUserLeaveHint(){cancelBleAction();connectionIntent.incrementAndGet();super.onUserLeaveHint();}
    @Override protected void onResume(){super.onResume();foreground=true;motionFault=false;if(session!=null)session.resume();completeBleAction();syncMotion();}
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(!focus&&keyboard!=null){clearSurfaces();session.releaseNotes();for(ParameterRow row:rows)row.cancel();updatePerformance(session.view());}syncMotion();}
    @Override protected void onDestroy(){
        if(practicePanel!=null)practicePanel.close();if(scoreLibrary!=null)scoreLibrary.close();
        destroyed=true;foreground=false;motionSensors.stop();ui.removeCallbacks(motionTick);motionModel.reset("已停止");stopMidi();connectionIntent.incrementAndGet();cancelBleAction();stopScan();session.pause();
        if(wifiCallback!=null)cm.unregisterNetworkCallback(wifiCallback);
        retirePort(true);super.onDestroy();
    }
}
