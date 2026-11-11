package cn.fpgasynth.control.core;

import cn.fpgasynth.control.generated.FmContract;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Map;

/** Current production C serializer fixtures, with explicit host RAM inputs; no device/audio claim. */
public final class FmCurrentTest {
    private static Path fixtures;
    private static int checks;
    private FmCurrentTest() {}
    private interface Action { void run() throws Exception; }
    private static void check(boolean value,String message) {
        checks++;if(!value)throw new AssertionError(message);
    }
    private static String text(String file) throws IOException {
        return new String(Files.readAllBytes(fixtures.resolve(file)),StandardCharsets.UTF_8);
    }
    private static void rejects(Action action,String message) throws Exception {
        try {action.run();}catch(IllegalArgumentException|IOException expected){checks++;return;}
        throw new AssertionError("accepted "+message);
    }
    private static Map<String,Object> modified(String file) throws IOException {return Json.object(text(file));}
    private static String encode(FmApi.State state,FmApi.Catalog catalog,int op,long a,long b,long value) {
        return FmApi.encode(1,1,state.context,op,a,b,value,state,catalog);
    }
    public static void main(String[] args) throws Exception {
        fixtures=Paths.get(args[0]);
        FmApi.Caps offline=new FmApi.Caps(text("capabilities.json"));
        check(offline.contractMatches,"current C capabilities must match active App contract: "+offline.incompatibility);
        check(offline.engineAvailable(0)&&offline.engineAvailable(1)&&!offline.engineAvailable(2),"matched digest alone cannot enable unavailable FM");
        FmApi.State dds=new FmApi.State(text("v2-state.json"),offline);
        FmApi.State ks=new FmApi.State(text("v2-state-ks.json"),offline);
        check(dds.playable()&&dds.applied==0&&!dds.canSelectFm(),"current DDS remains playable without FM runtime");
        check(ks.playable()&&ks.applied==1&&!ks.canSelectFm(),"current KS remains playable without FM runtime");
        check(new DeviceState(text("v1-state.json")).playable(),"actual legacy state still parses");
        FmApi.Caps caps=new FmApi.Caps(text("capabilities-fm-selected.json"));
        FmApi.State selected=new FmApi.State(text("v2-state-fm-selected.json"),caps);
        check(caps.contractMatches&&caps.engineAvailable(2)&&selected.playable()&&selected.canSelectFm(),"matched C-serialized selected runtime enables FM");
        check(selected.fm.profile.id==1&&selected.fm.backendId==1&&selected.fm.session==42&&selected.fm.epoch==4,"native profile/session/epoch retain distinct identity");
        check(selected.fm.voiceCount==8&&selected.fm.patchCount==4&&selected.fm.parameterValues.size()==1,"current C runtime dimensions and completion cache");
        Transport catalogTransport=(path,body)->{
            if(body!=null)throw new IOException("catalog must use GET");
            String file;
            if(path.equals("/api/v2/fm/catalog?profile=1&section=profile"))file="fm-profile.json";
            else if(path.equals("/api/v2/fm/catalog?profile=1&section=parameters&offset=0&limit=8"))file="fm-parameters-0.json";
            else if(path.equals("/api/v2/fm/catalog?profile=1&section=parameters&offset=8&limit=8"))file="fm-parameters-8.json";
            else if(path.equals("/api/v2/fm/catalog?profile=1&section=presets&offset=0&limit=8"))file="fm-presets.json";
            else throw new IOException("unexpected catalog path "+path);
            return new Transport.Response(200,text(file));
        };
        FmApi.Catalog catalog=FmApi.Catalog.load(catalogTransport,caps,selected);
        check(catalog.parameters.size()==13&&catalog.presets.size()==3,"complete current C Catalog.load: 13 parameters/3 presets");
        int[] operators={0,1,3};
        for(int preset=0;preset<operators.length;preset++) {
            FmContract.Preset generated=FmContract.getPreset(1,preset);boolean found=false;
            for(long i=0;i<generated.values_count;i++) {
                FmContract.PresetValue value=generated.value(i);
                if(value.parameter_id==17&&value.operator_index==operators[preset]) {
                    check(value.value_u32==8,"current preset total_level must be 8: "+preset);found=true;
                }
            }
            check(found,"current preset includes the updated total_level");
        }
        check(encode(selected,catalog,7,17,4294901760L,8).equals("1 1 9 7 17 4294901760 8"),"current catalog parameter encodes seven uint32 fields");
        check(encode(selected,catalog,8,2,0,0).equals("1 1 9 8 2 0 0"),"current preset command encodes");
        rejects(()->encode(selected,catalog,7,17,4294901764L,8),"out of range operator");
        rejects(()->encode(selected,catalog,7,17,4294901760L,128),"out of range total_level");
        FmApi.Caps faultCaps=new FmApi.Caps(text("capabilities-fm-fault.json"));
        FmApi.State fault=new FmApi.State(text("v2-state-fm-fault.json"),faultCaps);
        check(faultCaps.contractMatches&&!faultCaps.engineAvailable(2)&&!fault.playable()&&!fault.canSelectFm(),"current actual fault fixture stays disabled despite matching digest");
        rejects(()->encode(fault,catalog,3,1,100,0),"nonzero note on fault runtime");
        check(encode(fault,catalog,3,0,1,0).equals("1 1 9 3 0 1 0"),"fault runtime allows safety release");
        check(encode(fault,catalog,4,0,0,0).equals("1 1 9 4 0 0 0"),"fault runtime allows STOP");
        for(String digest:new String[]{"schema_sha256","profiles_sha256","contract_sha256"}) {
            Map<String,Object> bad=modified("capabilities-fm-selected.json");bad.put(digest,new String(new char[64]).replace('\0','0'));
            FmApi.Caps rejected=new FmApi.Caps(Json.stringify(bad));
            check(!rejected.contractMatches&&!rejected.engineAvailable(2),"arbitrary SHA remains rejected: "+digest);
            check(new FmApi.State(text("v2-state.json"),rejected).playable(),"FM digest mismatch preserves DDS: "+digest);
            rejects(()->FmApi.Catalog.load(catalogTransport,rejected,new FmApi.State(text("v2-state-fm-selected.json"),rejected)),"catalog without current identity");
        }
        if(args.length>1) {
            Path historical=Paths.get(args[1]);
            FmApi.Caps old=new FmApi.Caps(new String(Files.readAllBytes(historical.resolve("v2-live-capabilities.json")),StandardCharsets.UTF_8));
            check(!old.contractMatches&&!old.engineAvailable(2),"historical profiles SHA does not silently match current contract");
            FmApi.State oldState=new FmApi.State(new String(Files.readAllBytes(historical.resolve("v2-state.json")),StandardCharsets.UTF_8),old);
            check(oldState.playable(),"historical digest mismatch keeps DDS available");
        }
        System.out.println("FmCurrent: "+checks+" assertions PASS; all 12 production C serializer fixtures parsed; host RAM inputs only");
    }
}
