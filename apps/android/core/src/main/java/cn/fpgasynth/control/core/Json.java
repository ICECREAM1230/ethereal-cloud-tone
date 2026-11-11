package cn.fpgasynth.control.core;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Small bounded JSON reader shared by Android and dependency-free JVM tests. */
public final class Json {
    private final String text;
    private int at;
    private Json(String text) { this.text = text; }
    public static Map<String,Object> object(String text) {
        if (text == null || text.length() > 65536) throw new IllegalArgumentException("JSON size");
        Json p = new Json(text);
        Object value = p.value(0); p.space();
        if (p.at != text.length() || !(value instanceof Map)) throw p.bad();
        return map(value);
    }
    @SuppressWarnings("unchecked") public static Map<String,Object> map(Object v) {
        if (!(v instanceof Map)) throw new IllegalArgumentException("expected object");
        return (Map<String,Object>)v;
    }
    @SuppressWarnings("unchecked") public static List<Object> list(Object v) {
        if (!(v instanceof List)) throw new IllegalArgumentException("expected array");
        return (List<Object>)v;
    }
    public static long number(Object v) {
        if (!(v instanceof Long)) throw new IllegalArgumentException("expected integer");
        return (Long)v;
    }
    public static boolean bool(Object v) {
        if (!(v instanceof Boolean)) throw new IllegalArgumentException("expected boolean");
        return (Boolean)v;
    }
    public static String stringify(Object value) {
        if(value==null)return "null";
        if(value instanceof Boolean||value instanceof Number)return value.toString();
        if(value instanceof String){StringBuilder b=new StringBuilder("\"");for(char c:((String)value).toCharArray()){
            if(c=='"'||c=='\\')b.append('\\').append(c);else if(c<32)b.append(String.format("\\u%04x",(int)c));else b.append(c);
        }return b.append('"').toString();}
        StringBuilder b=new StringBuilder();
        if(value instanceof Map){b.append('{');for(Map.Entry<?,?> e:((Map<?,?>)value).entrySet()){
            if(b.length()>1)b.append(',');b.append(stringify(e.getKey().toString())).append(':').append(stringify(e.getValue()));
        }return b.append('}').toString();}
        if(value instanceof List){b.append('[');for(Object x:(List<?>)value){if(b.length()>1)b.append(',');b.append(stringify(x));}return b.append(']').toString();}
        throw new IllegalArgumentException("unsupported JSON type");
    }
    private IllegalArgumentException bad() { return new IllegalArgumentException("invalid JSON at " + at); }
    private void space() { while (at < text.length() && " \t\r\n".indexOf(text.charAt(at)) >= 0) at++; }
    private boolean take(char c) { space(); if (at < text.length() && text.charAt(at) == c) { at++; return true; } return false; }
    private Object value(int depth) {
        if (depth > 16) throw bad(); space(); if (at >= text.length()) throw bad();
        char c = text.charAt(at);
        if (c == '{') {
            at++; Map<String,Object> m = new LinkedHashMap<>(); if (take('}')) return m;
            do { space(); String key = string(); if (!take(':') || m.containsKey(key)) throw bad(); m.put(key,value(depth+1)); }
            while (take(',')); if (!take('}')) throw bad(); return m;
        }
        if (c == '[') {
            at++; List<Object> a = new ArrayList<>(); if (take(']')) return a;
            do { if (a.size() >= 256) throw bad(); a.add(value(depth+1)); } while (take(','));
            if (!take(']')) throw bad(); return a;
        }
        if (c == '"') return string();
        if (text.startsWith("true",at)) { at+=4; return true; }
        if (text.startsWith("false",at)) { at+=5; return false; }
        if (text.startsWith("null",at)) { at+=4; return null; }
        int start=at; if (c=='-') at++;
        int digits=at;
        while (at<text.length() && text.charAt(at)>='0' && text.charAt(at)<='9') at++;
        if (digits==at || (at-digits>1 && text.charAt(digits)=='0')) throw bad();
        try { return Long.parseLong(text.substring(start,at)); } catch (Exception e) { throw bad(); }
    }
    private String string() {
        if (at>=text.length() || text.charAt(at++)!='"') throw bad(); StringBuilder b=new StringBuilder();
        while(at<text.length()) {
            char c=text.charAt(at++); if(c=='"')return b.toString(); if(c<32)throw bad();
            if(c=='\\') {
                if(at>=text.length())throw bad(); c=text.charAt(at++);
                switch(c) {
                    case '"': case '\\': case '/': break;
                    case 'b': c='\b'; break; case 'f': c='\f'; break; case 'n': c='\n'; break;
                    case 'r': c='\r'; break; case 't': c='\t'; break;
                    case 'u': if(at+4>text.length())throw bad();
                        try {c=(char)Integer.parseInt(text.substring(at,at+4),16);} catch(Exception e){throw bad();} at+=4; break;
                    default: throw bad();
                }
            }
            b.append(c);
        }
        throw bad();
    }
}
