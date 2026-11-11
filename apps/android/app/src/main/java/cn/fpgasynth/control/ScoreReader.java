package cn.fpgasynth.control;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.pdf.PdfRenderer;
import android.os.Handler;
import android.os.Build;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;

/** Native, offline, single-page reader. File parsing runs on the score worker. */
final class ScoreReader extends Dialog {
    private final ScoreFiles.Entry entry;
    private final ExecutorService worker;
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final TextView status;
    private final Button previous,next;
    private final PageCanvas pageView;
    private int page,pageCount=1,request;
    private boolean closed,busy;
    private Future<?> pending;
    ScoreReader(Activity activity,ScoreFiles.Entry entry,ExecutorService worker){
        super(activity);this.entry=entry;this.worker=worker;requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout root=new LinearLayout(activity);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(12),dp(12),dp(12),dp(12));root.setBackgroundColor(ConsoleTheme.BG);
        root.setOnApplyWindowInsetsListener((v,insets)->{applyInsets(root,insets);return insets;});
        TextView title=new TextView(activity);title.setText(entry.name);title.setTextColor(ConsoleTheme.INK);title.setTextSize(18);title.setMaxLines(2);root.addView(title);
        status=new TextView(activity);status.setTextColor(ConsoleTheme.MUTED);status.setTextSize(13);status.setPadding(0,dp(8),0,dp(8));root.addView(status);
        pageView=new PageCanvas(activity);root.addView(pageView,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout zoom=line(activity);zoom.addView(button(activity,"缩小",()->pageView.zoom(.8f)),new LinearLayout.LayoutParams(0,dp(48),1));zoom.addView(button(activity,"适配",pageView::fit),new LinearLayout.LayoutParams(0,dp(48),1));zoom.addView(button(activity,"放大",()->pageView.zoom(1.25f)),new LinearLayout.LayoutParams(0,dp(48),1));root.addView(zoom);
        LinearLayout nav=line(activity);previous=button(activity,"上一页",()->load(page-1));next=button(activity,"下一页",()->load(page+1));
        nav.addView(previous,new LinearLayout.LayoutParams(0,dp(48),1));nav.addView(next,new LinearLayout.LayoutParams(0,dp(48),1));nav.addView(button(activity,"关闭",this::dismiss),new LinearLayout.LayoutParams(0,dp(48),1));root.addView(nav);
        setContentView(root);setOnDismissListener(d->{closed=true;request++;if(pending!=null)pending.cancel(true);pageView.bitmap(null);});
        setOnShowListener(d->{Window window=getWindow();if(window!=null)window.setLayout(WindowManager.LayoutParams.MATCH_PARENT,WindowManager.LayoutParams.MATCH_PARENT);root.requestApplyInsets();load(0);});
    }
    private int dp(float value){return Math.round(value*getContext().getResources().getDisplayMetrics().density);}
    @SuppressWarnings("deprecation")
    private void applyInsets(View root,WindowInsets insets){
        if(Build.VERSION.SDK_INT>=30){Insets safe=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());root.setPadding(dp(12)+safe.left,dp(12)+safe.top,dp(12)+safe.right,dp(12)+safe.bottom);}
        else root.setPadding(dp(12)+insets.getSystemWindowInsetLeft(),dp(12)+insets.getSystemWindowInsetTop(),dp(12)+insets.getSystemWindowInsetRight(),dp(12)+insets.getSystemWindowInsetBottom());
    }
    private static LinearLayout line(Activity activity){LinearLayout line=new LinearLayout(activity);line.setOrientation(LinearLayout.HORIZONTAL);return line;}
    private Button button(Activity activity,String label,Runnable action){Button b=new Button(activity);b.setText(label);b.setTextSize(14);b.setAllCaps(false);b.setTextColor(ConsoleTheme.TEAL);b.setBackground(ConsoleTheme.button(ConsoleTheme.PANEL,dp(6)));b.setOnClickListener(v->action.run());b.setMinHeight(dp(48));return b;}
    private void load(int selected){
        if(closed||busy||selected<0||selected>=pageCount)return;
        busy=true;page=selected;status.setText("正在读取曲谱…");previous.setEnabled(false);next.setEnabled(false);final int ticket=++request;
        pending=worker.submit(()->{
            Bitmap bitmap=null;int count=1;String error=null;
            try{
                if(entry.kind.equals("pdf")){
                    try(ParcelFileDescriptor descriptor=ParcelFileDescriptor.open(entry.file,ParcelFileDescriptor.MODE_READ_ONLY);PdfRenderer renderer=new PdfRenderer(descriptor)){
                        count=renderer.getPageCount();if(count<1||count>500||selected>=count)throw new IOException("PDF 页码无效。");
                        try(PdfRenderer.Page pdf=renderer.openPage(selected)){
                            int width=pdf.getWidth(),height=pdf.getHeight();if(width<=0||height<=0)throw new IOException("PDF 页面无法读取。");
                            double ratio=Math.min(2.0,Math.min(2048.0/Math.max(width,height),Math.sqrt(4000000.0/((double)width*height))));
                            bitmap=Bitmap.createBitmap(Math.max(1,(int)(width*ratio)),Math.max(1,(int)(height*ratio)),Bitmap.Config.ARGB_8888);bitmap.eraseColor(Color.WHITE);pdf.render(bitmap,null,null,PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY);
                        }
                    }
                }else{
                    BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;BitmapFactory.decodeFile(entry.file.getPath(),bounds);
                    if(bounds.outWidth<=0||bounds.outHeight<=0||(long)bounds.outWidth*bounds.outHeight>40000000L)throw new IOException("图片损坏或过大。");
                    BitmapFactory.Options sample=new BitmapFactory.Options();sample.inSampleSize=1;
                    while((long)(bounds.outWidth/sample.inSampleSize)*(bounds.outHeight/sample.inSampleSize)>4000000L)sample.inSampleSize*=2;
                    bitmap=BitmapFactory.decodeFile(entry.file.getPath(),sample);if(bitmap==null)throw new IOException("图片无法读取。");
                }
            }catch(Exception e){error="无法打开："+(e.getMessage()==null?"文件损坏":e.getMessage());}
            final Bitmap result=bitmap;final int total=count;final String failure=error;
            ui.post(()->{
                if(closed||ticket!=request){if(result!=null)result.recycle();return;}
                busy=false;
                if(failure!=null){if(result!=null)result.recycle();status.setText(failure);}
                else{pageCount=total;pageView.bitmap(result);status.setText((entry.kind.equals("pdf")?"第 "+(page+1)+" / "+total+" 页 · ":"")+"双指缩放 / 拖动查看");pageView.setContentDescription(entry.name+"，第 "+(page+1)+" 页，可通过放大缩小按钮查看");}
                previous.setEnabled(page>0);next.setEnabled(page+1<pageCount);
            });
        });
    }
    private final class PageCanvas extends View {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        private Bitmap bitmap;
        private float scale=1,minimum=1,offsetX,offsetY,lastX,lastY;
        private final ScaleGestureDetector detector;
        PageCanvas(Activity activity){super(activity);setBackgroundColor(ConsoleTheme.WELL);detector=new ScaleGestureDetector(activity,new ScaleGestureDetector.SimpleOnScaleGestureListener(){@Override public boolean onScale(ScaleGestureDetector d){zoomAt(d.getScaleFactor(),d.getFocusX(),d.getFocusY());return true;}});setFocusable(true);}
        void bitmap(Bitmap value){Bitmap old=bitmap;bitmap=value;fit();if(old!=null&&old!=value)old.recycle();}
        void fit(){if(bitmap!=null&&getWidth()>0&&getHeight()>0){minimum=Math.min((float)getWidth()/bitmap.getWidth(),(float)getHeight()/bitmap.getHeight());scale=minimum;offsetX=(getWidth()-bitmap.getWidth()*scale)/2;offsetY=(getHeight()-bitmap.getHeight()*scale)/2;}invalidate();}
        void zoom(float factor){zoomAt(factor,getWidth()/2f,getHeight()/2f);}
        private void zoomAt(float factor,float focusX,float focusY){if(bitmap==null)return;float old=scale;scale=Math.max(minimum,Math.min(minimum*5,scale*factor));offsetX=focusX-(focusX-offsetX)*scale/old;offsetY=focusY-(focusY-offsetY)*scale/old;clamp();invalidate();}
        private void clamp(){if(bitmap==null)return;float w=bitmap.getWidth()*scale,h=bitmap.getHeight()*scale;offsetX=w<getWidth()?(getWidth()-w)/2:Math.min(0,Math.max(getWidth()-w,offsetX));offsetY=h<getHeight()?(getHeight()-h)/2:Math.min(0,Math.max(getHeight()-h,offsetY));}
        @Override protected void onSizeChanged(int w,int h,int oldW,int oldH){fit();}
        @Override protected void onDraw(Canvas canvas){if(bitmap!=null){canvas.save();canvas.translate(offsetX,offsetY);canvas.scale(scale,scale);canvas.drawBitmap(bitmap,0,0,paint);canvas.restore();}}
        @Override public boolean onTouchEvent(MotionEvent event){
            detector.onTouchEvent(event);int action=event.getActionMasked();
            if(action==MotionEvent.ACTION_DOWN){lastX=event.getX();lastY=event.getY();getParent().requestDisallowInterceptTouchEvent(true);}
            else if(action==MotionEvent.ACTION_MOVE){if(!detector.isInProgress()&&event.getPointerCount()==1){offsetX+=event.getX()-lastX;offsetY+=event.getY()-lastY;clamp();invalidate();}lastX=event.getX();lastY=event.getY();}
            else if(action==MotionEvent.ACTION_POINTER_UP){int index=event.getActionIndex()==0?1:0;if(index<event.getPointerCount()){lastX=event.getX(index);lastY=event.getY(index);}}
            else if(action==MotionEvent.ACTION_UP){getParent().requestDisallowInterceptTouchEvent(false);performClick();}
            else if(action==MotionEvent.ACTION_CANCEL)getParent().requestDisallowInterceptTouchEvent(false);
            return true;
        }
        @Override public boolean performClick(){super.performClick();return true;}
    }
}
