package cn.fpgasynth.control;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import cn.fpgasynth.control.core.ScoreFilePolicy;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** Search uses the named original site; all saved-file reading remains native and offline. */
final class ScoreLibraryPanel extends LinearLayout {
    static final int IMPORT_DOCUMENT=6201,EXPORT_DOCUMENT=6202;
    private final Activity activity;
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final EditText search,url;
    private final TextView status,libraryTitle;
    private final Button importButton,downloadButton,cancelButton;
    private final LinearLayout library;
    private ScoreFiles files;
    private ScoreFiles.Entry exporting;
    private ScoreReader reader;
    private AlertDialog deletion;
    private Future<?> pending;
    private volatile boolean closed;
    private boolean busy;
    private long generation;
    private interface Work {String run()throws Exception;}
    ScoreLibraryPanel(Activity activity){
        super(activity);this.activity=activity;setOrientation(VERTICAL);
        LinearLayout online=card();title(online,"在线查谱");
        online.addView(text("歌名",13,ConsoleTheme.INK));search=field("歌名","输入歌名",InputType.TYPE_CLASS_TEXT);online.addView(search,new LayoutParams(-1,-2));
        Button original=button("在曲谱图网搜索",ConsoleTheme.TEAL,this::search);online.addView(original,new LayoutParams(-1,dp(48)));
        TextView source=text("原站搜索 · 查谱需互联网，保存后可离线查看",12,ConsoleTheme.MUTED);source.setPadding(0,dp(8),0,0);online.addView(source);
        LinearLayout save=card();title(save,"保存曲谱");
        importButton=button("导入图片 / PDF",ConsoleTheme.TEAL,this::chooseImport);save.addView(importButton,new LayoutParams(-1,dp(48)));
        TextView linkLabel=text("图片 / PDF 文件直链",13,ConsoleTheme.INK);linkLabel.setPadding(0,dp(12),0,0);save.addView(linkLabel);
        url=field("图片或 PDF 的 HTTPS 直链","https://…",InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI);save.addView(url,new LayoutParams(-1,-2));
        downloadButton=button("下载到离线谱夹",ConsoleTheme.TEAL,this::download);save.addView(downloadButton,new LayoutParams(-1,dp(48)));
        cancelButton=button("取消当前操作",ConsoleTheme.AMBER,this::cancel);save.addView(cancelButton,new LayoutParams(-1,dp(48)));cancelButton.setVisibility(GONE);
        status=text("正在读取谱夹…",13,ConsoleTheme.MUTED);status.setAccessibilityLiveRegion(ACCESSIBILITY_LIVE_REGION_POLITE);status.setPadding(dp(4),dp(4),dp(4),dp(12));addView(status);
        library=card();libraryTitle=title(library,"离线谱夹");refresh();
    }
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private TextView text(String value,int size,int color){TextView v=new TextView(activity);v.setText(value);v.setTextColor(color);v.setTextSize(size);v.setLineSpacing(dp(3),1);return v;}
    private TextView title(LinearLayout parent,String value){TextView v=text(value,17,ConsoleTheme.INK);v.setPadding(0,0,0,dp(10));parent.addView(v);return v;}
    private LinearLayout card(){LinearLayout panel=new LinearLayout(activity);panel.setOrientation(VERTICAL);panel.setPadding(dp(14),dp(14),dp(14),dp(14));panel.setBackground(ConsoleTheme.panel(ConsoleTheme.PANEL,ConsoleTheme.BORDER,dp(8)));LayoutParams lp=new LayoutParams(-1,-2);lp.bottomMargin=dp(12);addView(panel,lp);return panel;}
    private EditText field(String label,String hint,int input){EditText edit=new EditText(activity);edit.setTextColor(ConsoleTheme.INK);edit.setHintTextColor(ConsoleTheme.MUTED);edit.setTextSize(15);edit.setHint(label+" · "+hint);edit.setContentDescription(label);edit.setInputType(input);edit.setSingleLine(true);edit.setMinHeight(dp(56));edit.setSelectAllOnFocus(true);return edit;}
    private Button button(String label,int color,Runnable action){Button button=new Button(activity);button.setText(label);button.setTextColor(color);button.setTextSize(14);button.setAllCaps(false);button.setMinHeight(dp(48));button.setBackground(ConsoleTheme.button(ConsoleTheme.WELL,dp(6)));button.setOnClickListener(v->action.run());return button;}
    private void search(){
        String query=search.getText().toString().trim();
        if(query.isEmpty()){search.setError("请输入歌名。");return;}
        if(query.length()>120){search.setError("歌名最多 120 字。");return;}
        Uri destination=Uri.parse("https://www.quputu.com/Search").buildUpon().appendQueryParameter("keys",query).build();
        try{activity.startActivity(new Intent(Intent.ACTION_VIEW,destination));}
        catch(ActivityNotFoundException e){notice("未找到浏览器，请先安装可打开 HTTPS 网页的浏览器。",true);}
    }
    private void chooseImport(){
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("*/*");intent.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"image/png","image/jpeg","image/webp","application/pdf"});intent.putExtra(Intent.EXTRA_LOCAL_ONLY,true);
        try{activity.startActivityForResult(intent,IMPORT_DOCUMENT);}catch(ActivityNotFoundException e){notice("系统文件选择器不可用。",true);}
    }
    private void download(){String address=url.getText().toString();runOperation(()->{ScoreFiles.Entry saved=files.download(address);return "已下载："+saved.name;});}
    boolean onActivityResult(int requestCode,int resultCode,Intent data){
        if(requestCode!=IMPORT_DOCUMENT&&requestCode!=EXPORT_DOCUMENT)return false;
        if(closed)return true;
        if(resultCode!=Activity.RESULT_OK||data==null||data.getData()==null){exporting=null;notice("已取消文件选择。",false);return true;}
        Uri uri=data.getData();if(!"content".equals(uri.getScheme())){exporting=null;notice("请选择系统文件提供器中的文件。",true);return true;}
        if(requestCode==IMPORT_DOCUMENT){
            runOperation(()->{
                String name="曲谱";
                try(Cursor cursor=activity.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
                    if(cursor!=null&&cursor.moveToFirst()){int index=cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);if(index>=0)name=cursor.getString(index);}
                }
                try(InputStream source=activity.getContentResolver().openInputStream(uri)){ScoreFiles.Entry saved=files.importStream(source,name);return "已导入："+saved.name;}
            });
        }else{
            final ScoreFiles.Entry target=exporting;exporting=null;if(target==null){notice("请重新选择要另存的曲谱。",true);return true;}
            runOperation(()->{try(OutputStream out=activity.getContentResolver().openOutputStream(uri,"wt")){files.export(target,out);}return "已另存："+target.name;});
        }
        return true;
    }
    void refresh(){if(!closed&&!busy)runOperation(()->null);}
    private void runOperation(Work work){
        if(closed||busy)return;
        busy=true;final long ticket=++generation;controls();notice("正在处理…",false);
        pending=worker.submit(()->{
            String message=null,failure=null;List<ScoreFiles.Entry> entries=Collections.emptyList();
            try{if(files==null)files=new ScoreFiles(activity.getApplicationContext());message=work.run();entries=files.list();}
            catch(Exception e){failure=Thread.currentThread().isInterrupted()?"已取消。":(e.getMessage()==null?"文件操作失败。":e.getMessage());try{if(files!=null)entries=files.list();}catch(IOException ignored){}}
            final String result=message,error=failure;final List<ScoreFiles.Entry> saved=entries;
            ui.post(()->{
                if(closed||ticket!=generation)return;
                busy=false;controls();showLibrary(saved);
                notice(error!=null?error:result!=null?result:"已保存 "+saved.size()+" 份 · 可离线查看",error!=null);
            });
        });
    }
    private void cancel(){
        if(!busy)return;
        generation++;if(pending!=null)pending.cancel(true);busy=false;controls();notice("已取消。",false);runOperation(()->"已取消。");
    }
    private void controls(){importButton.setEnabled(!busy);downloadButton.setEnabled(!busy);url.setEnabled(!busy);cancelButton.setVisibility(busy?VISIBLE:GONE);}
    private void notice(String value,boolean error){status.setText(value);status.setTextColor(error?ConsoleTheme.ERROR:ConsoleTheme.MUTED);}
    private void showLibrary(List<ScoreFiles.Entry> entries){
        library.removeViews(1,library.getChildCount()-1);libraryTitle.setText("离线谱夹 · "+entries.size()+" / "+ScoreFilePolicy.MAX_FILES);
        if(entries.isEmpty()){library.addView(text("尚未保存曲谱。导入图片/PDF，或粘贴文件直链下载。",14,ConsoleTheme.MUTED));return;}
        for(ScoreFiles.Entry entry:entries){
            TextView name=text(entry.name,16,ConsoleTheme.INK);name.setPadding(0,dp(12),0,dp(4));library.addView(name);
            library.addView(text(entry.kind.toUpperCase(Locale.ROOT)+" · "+String.format(Locale.CHINA,"%.1f MB",entry.bytes/1048576.0),12,ConsoleTheme.MUTED));
            LinearLayout actions=new LinearLayout(activity);actions.setOrientation(HORIZONTAL);
            actions.addView(button("查看",ConsoleTheme.TEAL,()->{if(reader!=null)reader.dismiss();reader=new ScoreReader(activity,entry,worker);reader.show();}),new LayoutParams(0,dp(48),1));
            actions.addView(button("另存",ConsoleTheme.TEAL,()->export(entry)),new LayoutParams(0,dp(48),1));
            actions.addView(button("删除",ConsoleTheme.AMBER,()->confirmRemove(entry)),new LayoutParams(0,dp(48),1));library.addView(actions);
        }
    }
    private void export(ScoreFiles.Entry entry){
        if(busy||closed)return;
        exporting=entry;String filename=entry.name;if(!filename.toLowerCase(Locale.ROOT).endsWith("."+entry.kind))filename+="."+entry.kind;
        Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType(entry.mime());intent.putExtra(Intent.EXTRA_TITLE,filename);
        try{activity.startActivityForResult(intent,EXPORT_DOCUMENT);}catch(ActivityNotFoundException e){exporting=null;notice("系统文件选择器不可用。",true);}
    }
    private void confirmRemove(ScoreFiles.Entry entry){
        if(busy||closed)return;
        deletion=new AlertDialog.Builder(activity).setTitle("删除离线曲谱？").setMessage(entry.name).setNegativeButton("保留",null).setPositiveButton("删除",(d,w)->runOperation(()->{files.remove(entry);return "已删除："+entry.name;})).create();deletion.show();
    }
    void close(){
        if(closed)return;closed=true;generation++;if(pending!=null)pending.cancel(true);if(reader!=null)reader.dismiss();if(deletion!=null)deletion.dismiss();worker.shutdownNow();
    }
}
