using System;
using System.Runtime.InteropServices;

namespace UnityLive
{
    public enum UvlState { Idle = 0, Connecting = 1, Playing = 2, Reconnecting = 3, Stopped = 4, Error = 5 }
    public enum UvlRtspTransport { Auto = 0, Tcp = 1, Udp = 2 }

    [StructLayout(LayoutKind.Sequential)]
    public struct UvlOptions
    {
        public int rtspTransport, lowLatency, hwDecode, timeoutMs, autoReconnect, recvBufferBytes, flipVertical;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct UvlStreamInfo { public int width, height, codecId; public double fps; }

    [StructLayout(LayoutKind.Sequential)]
    public struct UvlStats { public ulong packets, framesDecoded, framesDropped, bytes, reconnects; }

    internal static class UvlNative
    {
        const string Lib = "uvl_video";

        [DllImport(Lib)] public static extern void uvl_options_default(ref UvlOptions o);
        [DllImport(Lib)] public static extern IntPtr uvl_create();
        [DllImport(Lib)] public static extern void uvl_destroy(IntPtr p);
        [DllImport(Lib)] public static extern int uvl_open(IntPtr p, [MarshalAs(UnmanagedType.LPStr)] string url, ref UvlOptions o);
        [DllImport(Lib)] public static extern void uvl_close(IntPtr p);
        [DllImport(Lib)] public static extern int uvl_get_state(IntPtr p);
        [DllImport(Lib)] public static extern int uvl_get_info(IntPtr p, out UvlStreamInfo i);
        [DllImport(Lib)] public static extern void uvl_get_stats(IntPtr p, out UvlStats s);
        [DllImport(Lib)] public static extern int uvl_get_last_error(IntPtr p, byte[] buf, int cap);
        [DllImport(Lib)] public static extern int uvl_try_get_frame(IntPtr p, byte[] dst, int cap, out long ptsUs, out ulong counter);
        [DllImport(Lib)] public static extern IntPtr uvl_version();
        [DllImport(Lib)] public static extern void uvl_set_jvm(IntPtr vm);
    }
}
