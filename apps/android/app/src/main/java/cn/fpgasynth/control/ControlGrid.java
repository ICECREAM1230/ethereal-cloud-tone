package cn.fpgasynth.control;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/** A measured wrapping grid: large text grows rows instead of clipping fixed-height tiles. */
final class ControlGrid extends ViewGroup {
    private final int cell,gap,maxColumns;
    ControlGrid(Context context,int minDp,int maxColumns){super(context);float d=getResources().getDisplayMetrics().density;cell=Math.round(minDp*d);gap=Math.round(8*d);this.maxColumns=maxColumns;}
    @Override protected void onMeasure(int widthSpec,int heightSpec){
        int width=MeasureSpec.getSize(widthSpec),cols=Math.max(1,Math.min(maxColumns,(width+gap)/(cell+gap))),item=(width-gap*(cols-1))/cols,total=0,row=0;
        for(int i=0;i<getChildCount();i++){View child=getChildAt(i);child.measure(MeasureSpec.makeMeasureSpec(item,MeasureSpec.EXACTLY),MeasureSpec.makeMeasureSpec(0,MeasureSpec.UNSPECIFIED));row=Math.max(row,child.getMeasuredHeight());if(i%cols==cols-1||i==getChildCount()-1){total+=row+gap;row=0;}}
        setMeasuredDimension(width,resolveSize(Math.max(0,total-gap),heightSpec));
    }
    @Override protected void onLayout(boolean changed,int l,int t,int r,int b){
        int width=r-l,cols=Math.max(1,Math.min(maxColumns,(width+gap)/(cell+gap))),item=(width-gap*(cols-1))/cols,y=0,row=0;
        for(int i=0;i<getChildCount();i++){View child=getChildAt(i);int x=(i%cols)*(item+gap);child.layout(x,y,x+item,y+child.getMeasuredHeight());row=Math.max(row,child.getMeasuredHeight());if(i%cols==cols-1||i==getChildCount()-1){y+=row+gap;row=0;}}
    }
}
