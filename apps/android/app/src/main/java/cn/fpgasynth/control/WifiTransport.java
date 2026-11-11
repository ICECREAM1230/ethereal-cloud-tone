package cn.fpgasynth.control;

import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import cn.fpgasynth.control.core.ControlPort;
import cn.fpgasynth.control.core.WifiControlPort;
import java.io.IOException;
import java.net.HttpURLConnection;

/** Routes each request on an existing Wi-Fi network, including unvalidated SoftAPs. */
final class WifiTransport implements ControlPort {
    private final ConnectivityManager connectivity;
    private final WifiControlPort delegate;
    private volatile Network usedNetwork;
    WifiTransport(ConnectivityManager cm,String address){
        connectivity=cm;
        delegate=new WifiControlPort(address,url->{Network route=wifi();usedNetwork=route;return (HttpURLConnection)route.openConnection(url);});
    }
    @Override public Mode mode(){return Mode.WIFI;}
    @Override public void close(){delegate.close();}
    boolean uses(Network n){return n.equals(usedNetwork);}
    private Network wifi() throws IOException {
        Network active=connectivity.getActiveNetwork();
        NetworkCapabilities caps=active==null?null:connectivity.getNetworkCapabilities(active);
        if(caps!=null&&caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI))return active;
        for(Network n:connectivity.getAllNetworks()){
            caps=connectivity.getNetworkCapabilities(n);
            if(caps!=null&&caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI))return n;
        }
        throw new IOException("尚未连接 Wi-Fi。请在系统设置连接 FPGA_Project，并保留无互联网连接。");
    }
    @Override public Response request(String path,String body) throws IOException {return delegate.request(path,body);}
}
