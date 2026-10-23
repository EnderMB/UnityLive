using System;
using System.Text;
using UnityEngine;
using UnityEngine.UI;

namespace UnityLive
{
    /// <summary>Reproduce un stream en vivo (UDP, multicast, RTP, RTSP, MPEG-TS) sobre un Renderer o RawImage.</summary>
    [AddComponentMenu("UnityLive/Uvl Player")]
    public class UvlPlayer : MonoBehaviour
    {
        [Header("Fuente")]
        [Tooltip("udp://@239.255.0.1:5000 (multicast) | udp://0.0.0.0:5000 | rtp://... | rtsp://user:pass@host/stream")]
        public string url = "udp://@239.255.0.1:5000";
        public bool playOnStart = true;

        [Header("Opciones")]
        public UvlRtspTransport rtspTransport = UvlRtspTransport.Tcp;
        public bool lowLatency = true;
        public bool hardwareDecode = true;
        public bool autoReconnect = true;
        public int timeoutMs = 5000;

        [Header("Salida")]
        public Renderer targetRenderer;
        public RawImage targetRawImage;

        public event Action<int, int> OnResolutionChanged;
        public UvlState State => _h == IntPtr.Zero ? UvlState.Idle : (UvlState)UvlNative.uvl_get_state(_h);
        public Texture2D Texture => _tex;
        public UvlStats Stats { get { UvlStats s = default; if (_h != IntPtr.Zero) UvlNative.uvl_get_stats(_h, out s); return s; } }
        public UvlStreamInfo Info { get { UvlStreamInfo i = default; if (_h != IntPtr.Zero) UvlNative.uvl_get_info(_h, out i); return i; } }
        public string LastError
        {
            get { if (_h == IntPtr.Zero) return ""; var b = new byte[256]; int n = UvlNative.uvl_get_last_error(_h, b, b.Length); return Encoding.UTF8.GetString(b, 0, n); }
        }

        IntPtr _h = IntPtr.Zero;
        Texture2D _tex;
        byte[] _buf;
        ulong _lastCounter;

        void Awake()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            UvlNative.uvl_set_jvm(AndroidJNI.GetJavaVM());   // necesario para MediaCodec (Unity 2021.2+)
            AcquireMulticastLock();
#endif
        }

        void Start() { if (playOnStart) Play(); }

        public void Play()
        {
            Stop();
            _h = UvlNative.uvl_create();
            var o = new UvlOptions();
            UvlNative.uvl_options_default(ref o);
            o.rtspTransport = (int)rtspTransport;
            o.lowLatency = lowLatency ? 1 : 0;
            o.hwDecode = hardwareDecode ? 1 : 0;
            o.autoReconnect = autoReconnect ? 1 : 0;
            o.timeoutMs = timeoutMs;
            UvlNative.uvl_open(_h, url, ref o);
        }

        public void Stop()
        {
            if (_h != IntPtr.Zero) { UvlNative.uvl_destroy(_h); _h = IntPtr.Zero; }
            _lastCounter = 0;
        }

        void Update()
        {
            if (_h == IntPtr.Zero) return;
            var info = Info;
            if (info.width <= 0 || info.height <= 0) return;

            if (_tex == null || _tex.width != info.width || _tex.height != info.height)
            {
                if (_tex != null) Destroy(_tex);
                _tex = new Texture2D(info.width, info.height, TextureFormat.RGBA32, false) { wrapMode = TextureWrapMode.Clamp };
                _buf = new byte[info.width * info.height * 4];
                if (targetRenderer != null) targetRenderer.material.mainTexture = _tex;
                if (targetRawImage != null) targetRawImage.texture = _tex;
                OnResolutionChanged?.Invoke(info.width, info.height);
            }

            if (UvlNative.uvl_try_get_frame(_h, _buf, _buf.Length, out _, out ulong counter) == 1 && counter != _lastCounter)
            {
                _lastCounter = counter;
                _tex.LoadRawTextureData(_buf);
                _tex.Apply(false);
            }
        }

        void OnDestroy() { Stop(); if (_tex != null) Destroy(_tex); }

#if UNITY_ANDROID && !UNITY_EDITOR
        // En Wi-Fi Android filtra el multicast salvo que se adquiera un MulticastLock
        // (requiere permisos INTERNET y CHANGE_WIFI_MULTICAST_STATE en el manifiesto).
        static AndroidJavaObject s_lock;
        static void AcquireMulticastLock()
        {
            if (s_lock != null) return;
            using (var up = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using (var act = up.GetStatic<AndroidJavaObject>("currentActivity"))
            using (var wifi = act.Call<AndroidJavaObject>("getSystemService", "wifi"))
            {
                s_lock = wifi.Call<AndroidJavaObject>("createMulticastLock", "uvl");
                s_lock.Call("setReferenceCounted", false);
                s_lock.Call("acquire");
            }
        }
#endif
    }
}
