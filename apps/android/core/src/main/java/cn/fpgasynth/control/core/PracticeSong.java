package cn.fpgasynth.control.core;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** Immutable, explicit score events. Built-in phrases are original practice patterns. */
public final class PracticeSong {
    public static final int PPQ=480;
    public final String id,title;
    public final int defaultBpm,beatsPerBar,totalTicks;
    public final List<Note> notes;

    public static final class Note {
        public final int midi,startTick,durationTicks;
        public Note(int midi,int startTick,int durationTicks){
            if((midi!=-1&&(midi<48||midi>72))||startTick<0||durationTicks<=0||
                    (long)startTick+durationTicks>Integer.MAX_VALUE)
                throw new IllegalArgumentException("Invalid practice note");
            this.midi=midi;this.startTick=startTick;this.durationTicks=durationTicks;
        }
    }

    public PracticeSong(String id,String title,int defaultBpm,int beatsPerBar,List<Note> notes){
        if(id==null||id.trim().isEmpty()||title==null||title.trim().isEmpty()||
                defaultBpm<40||defaultBpm>180||beatsPerBar<1||beatsPerBar>12||
                notes==null||notes.isEmpty()||notes.size()>1024)
            throw new IllegalArgumentException("Invalid practice song");
        List<Note> copy=new ArrayList<>(notes);int cursor=0;
        for(Note note:copy){
            if(note==null||note.startTick!=cursor)throw new IllegalArgumentException("Practice events must be contiguous; include explicit rests");
            cursor=note.startTick+note.durationTicks;
        }
        this.id=id;this.title=title;this.defaultBpm=defaultBpm;this.beatsPerBar=beatsPerBar;
        this.totalTicks=cursor;this.notes=Collections.unmodifiableList(copy);
    }

    private static PracticeSong phrase(String id,String title,int bpm,int[] pitches,int[] durations){
        List<Note> events=new ArrayList<>();int position=0;
        for(int i=0;i<pitches.length;i++){events.add(new Note(pitches[i],position,durations[i]));position+=durations[i];}
        return new PracticeSong(id,title,bpm,4,events);
    }

    private static final List<PracticeSong> BUILT_INS=Collections.unmodifiableList(Arrays.asList(
        phrase("steady-four","四拍起步",80,
            new int[]{48,50,52,55,53,52,50,48,48,52,55,60,57,55,50,48},
            new int[]{480,480,480,480,480,480,480,480,480,480,480,480,480,480,480,480}),
        phrase("eighth-rest","八分与休止",72,
            new int[]{48,50,52,-1,55,53,52,50,-1,48,52,53,55,-1,57,55,52,50,-1,48},
            new int[]{240,240,480,480,480,240,240,480,480,480,240,240,480,480,480,240,240,480,480,480}),
        phrase("dotted-sustain","附点与长音",68,
            new int[]{48,50,52,53,55,57,55,-1,52,50,48},
            new int[]{720,240,960,720,240,960,960,480,480,480,1440})
    ));

    public static List<PracticeSong> builtIns(){return BUILT_INS;}

    /** Numbered notation in C major: C4 is 1; dots distinguish C3 and C5. */
    public static String degree(int midi){
        if(midi==-1)return "0";
        if(midi<48||midi>72)throw new IllegalArgumentException("Invalid notation note");
        String[] degrees={"1","♯1","2","♯2","3","4","♯4","5","♯5","6","♯6","7"};
        String value=degrees[midi%12];
        return value+(midi<60?"\u0323":midi>=72?"\u0307":"");
    }
}
