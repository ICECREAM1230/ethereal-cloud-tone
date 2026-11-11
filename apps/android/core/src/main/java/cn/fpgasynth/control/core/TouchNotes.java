package cn.fpgasynth.control.core;

import java.util.HashMap;
import java.util.Map;

/** Pointer IDs are stable across pointer-index changes; same-key contacts share a bit. */
public final class TouchNotes {
    private final Map<Integer,Integer> notes=new HashMap<>();
    public int move(int pointer,int key) { if(key<0)notes.remove(pointer);else {if(key>24)throw new IllegalArgumentException();notes.put(pointer,key);} return mask(); }
    public int up(int pointer) {notes.remove(pointer);return mask();}
    public int mask(){int m=0;for(int k:notes.values())m|=1<<k;return m;}
    public void clear(){notes.clear();}
}
