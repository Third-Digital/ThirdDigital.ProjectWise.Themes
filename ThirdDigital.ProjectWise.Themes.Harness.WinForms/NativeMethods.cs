using System;
using System.Runtime.InteropServices;

namespace ThirdDigital.ProjectWise.Themes.Harness.WinForms
{
    internal static class NativeMethods
    {
        public const int COLOR_WINDOW = 5;
        public const int COLOR_WINDOWTEXT = 8;
        public const int COLOR_3DFACE = 15;
        public const uint PW_CLIENTONLY = 0x1;
        public const uint PW_RENDERFULLCONTENT = 0x2;
        public const uint AAMODULE_EXPLORER = 0x1;

        [DllImport("user32.dll", CallingConvention = CallingConvention.StdCall)]
        public static extern int GetSysColor(int index);

        [DllImport("user32.dll", CallingConvention = CallingConvention.StdCall, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);

        [DllImport("dwmapi.dll", CallingConvention = CallingConvention.StdCall)]
        public static extern int DwmFlush();

        [DllImport("kernel32.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr LoadLibraryW(string path);

        [DllImport("kernel32.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi, SetLastError = true, BestFitMapping = false)]
        public static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll", CallingConvention = CallingConvention.StdCall)]
        public static extern uint GetCurrentThreadId();

        [DllImport("user32.dll", CallingConvention = CallingConvention.StdCall, SetLastError = true)]
        public static extern IntPtr GetThreadDesktop(uint threadId);

        [DllImport("user32.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetUserObjectInformationW(IntPtr handle, int index, System.Text.StringBuilder info, int length, out int needed);

        public static string DesktopName()
        {
            var name = new System.Text.StringBuilder(128);
            int needed;
            GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), 2, name, name.Capacity * 2, out needed);
            return name.ToString();
        }

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        public delegate int CustomInitialize(uint applicationMask, IntPtr reserved);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate int InitializeHarness();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public delegate int SwitchHarnessScheme([MarshalAs(UnmanagedType.LPWStr)] string name);

        public static T Export<T>(IntPtr module, string name) where T : class
        {
            IntPtr address = GetProcAddress(module, name);
            return address == IntPtr.Zero ? null : (T)(object)Marshal.GetDelegateForFunctionPointer(address, typeof(T));
        }
    }
}
