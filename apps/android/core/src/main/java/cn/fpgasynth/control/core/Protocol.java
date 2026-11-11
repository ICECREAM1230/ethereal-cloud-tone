package cn.fpgasynth.control.core;

import java.net.URI;
import java.util.Arrays;

/** Current frozen ESP32 ABI. Does not infer hardware capabilities. */
public final class Protocol {
    public static final long UINT_MAX = 0xffff_ffffL;
    public static final int MASK_MAX = (1 << 25) - 1;
    public static final int[] CCS = {7,10,14,15,20,21,22,23,24,25,26,27,28,29,30,31,
            71,72,73,74,75,76,77,79,85,86,87,102,103,104,105,106,107,109,110,111,112,113};
    private Protocol() {}

    public static boolean allowed(int cc) { return Arrays.binarySearch(CCS, cc) >= 0; }
    public static String encode(long token, long sequence, int op, long a, long b) {
        if (token <= 0 || token > UINT_MAX || sequence <= 0 || sequence > UINT_MAX)
            throw new IllegalArgumentException("token/sequence must be nonzero uint32");
        boolean valid;
        switch (op) {
            case 2: valid = a >= 0 && a <= 127 && allowed((int)a) && b >= 0 && b <= 127; break;
            case 3: valid = a >= 0 && a <= MASK_MAX && b >= 1 && b <= 127; break;
            case 4: valid = a == 0 && b == 0; break;
            case 5: valid = (a == 0 || a == 1) && b == 0; break;
            case 6: valid = a >= 200 && a <= 8000 && b == 0; break;
            default: valid = false;
        }
        if (!valid) throw new IllegalArgumentException("unsupported command");
        return token + " " + sequence + " " + op + " " + a + " " + b;
    }

    /** Explicit device origin: no credentials, paths, query, fragment or redirects. */
    public static String endpoint(String input) {
        try {
            String value = input.trim();
            if (!value.contains("://")) value = "http://" + value;
            URI u = new URI(value);
            if (!("http".equals(u.getScheme()) || "https".equals(u.getScheme()))
                    || u.getHost() == null || u.getUserInfo() != null || u.getQuery() != null
                    || u.getFragment() != null || !(u.getPath().isEmpty() || "/".equals(u.getPath()))
                    || u.getPort() == 0 || u.getPort() > 65535)
                throw new IllegalArgumentException();
            return u.getScheme() + "://" + u.getRawAuthority();
        } catch (Exception e) { throw new IllegalArgumentException("请输入设备地址，例如 http://192.168.4.1"); }
    }
}
