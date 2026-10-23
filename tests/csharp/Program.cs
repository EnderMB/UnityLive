// Valida el wrapper P/Invoke REAL de Unity (UvlNative.cs) sin necesitar Unity.
// Uso: dotnet run -- <url> [segundos]   (uvl_video.dll / libuvl_video.so debe poder cargarse)
using System;
using System.Diagnostics;
using System.Text;
using System.Threading;
using UnityLive;

class Program
{
    static int Main(string[] a)
    {
        string url = a.Length > 0 ? a[0] : "udp://127.0.0.1:5000";
        int secs = a.Length > 1 ? int.Parse(a[1]) : 8;

        Console.WriteLine("version: " + System.Runtime.InteropServices.Marshal.PtrToStringAnsi(UvlNative.uvl_version()));
        IntPtr h = UvlNative.uvl_create();
        var o = new UvlOptions();
        UvlNative.uvl_options_default(ref o);   // mismos campos que usa UvlPlayer
        o.rtspTransport = (int)UvlRtspTransport.Tcp;
        UvlNative.uvl_open(h, url, ref o);

        byte[] buf = null; ulong last = 0; int shown = 0; int w = 0, hh = 0;
        var sw = Stopwatch.StartNew();
        while (sw.Elapsed.TotalSeconds < secs)
        {
            UvlNative.uvl_get_info(h, out UvlStreamInfo i);
            if (i.width > 0 && (buf == null || i.width != w || i.height != hh))
            { w = i.width; hh = i.height; buf = new byte[w * hh * 4]; }       // igual que UvlPlayer.Update
            if (buf != null && UvlNative.uvl_try_get_frame(h, buf, buf.Length, out long pts, out ulong n) == 1 && n != last)
            { last = n; shown++; }
            Thread.Sleep(5);
        }
        UvlNative.uvl_get_info(h, out UvlStreamInfo info);
        UvlNative.uvl_get_stats(h, out UvlStats s);
        var eb = new byte[256]; int en = UvlNative.uvl_get_last_error(h, eb, eb.Length);
        Console.WriteLine($"state={(UvlState)UvlNative.uvl_get_state(h)} {info.width}x{info.height} codec={info.codecId} fps={info.fps:F2}");
        Console.WriteLine($"packets={s.packets} bytes={s.bytes} decoded={s.framesDecoded} dropped={s.framesDropped} reconnects={s.reconnects} shown={shown}");
        Console.WriteLine($"error='{Encoding.UTF8.GetString(eb, 0, en)}'");
        UvlNative.uvl_destroy(h);
        return s.framesDecoded > 0 && shown > 0 ? 0 : 1;
    }
}
