using System;
using System.Collections.Generic;
using System.Drawing;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace ThirdDigital.ProjectWise.Themes.Harness.WinForms
{
    internal sealed class ThemeForm : Form
    {
        public readonly TextBox Input = new TextBox();
        public readonly ListView List = new ListView();
        public readonly Button Action = new Button();

        public ThemeForm(string title)
        {
            Text = title;
            StartPosition = FormStartPosition.Manual;
            Location = new Point(80, 80);
            ClientSize = new Size(600, 360);
            TopMost = true;
            Input.SetBounds(10, 10, 300, 40);
            Input.Font = new Font("Arial", 14f);
            List.SetBounds(10, 70, 300, 200);
            List.View = View.Details;
            List.Columns.Add("Name", 200);
            for (int i = 0; i < 3; i++)
            {
                List.Items.Add("Item " + i);
            }
            Action.SetBounds(330, 10, 140, 40);
            Action.Text = "Button";
            Controls.Add(Input);
            Controls.Add(List);
            Controls.Add(Action);
        }
    }

    internal sealed class Samples
    {
        public Color? TextBox;
        public Color? ListView;
        public Color? Form;
    }

    internal static partial class Program
    {
        private struct Expectation
        {
            public int Background;
            public int Foreground;
        }

        // Independent expectations: Windows Terminal background and foreground for each scheme.
        private static readonly Dictionary<string, Expectation> Expected = new Dictionary<string, Expectation>
        {
            { "Campbell", new Expectation { Background = 0x0C0C0C, Foreground = 0xCCCCCC } },
            { "Campbell Powershell", new Expectation { Background = 0x012456, Foreground = 0xCCCCCC } },
            { "Vintage", new Expectation { Background = 0x000000, Foreground = 0xC0C0C0 } },
            { "One Half Dark", new Expectation { Background = 0x282C34, Foreground = 0xDCDFE4 } },
            { "One Half Light", new Expectation { Background = 0xFAFAFA, Foreground = 0x383A42 } },
            { "Solarized Dark", new Expectation { Background = 0x002B36, Foreground = 0x839496 } },
            { "Solarized Light", new Expectation { Background = 0xFDF6E3, Foreground = 0x657B83 } },
            { "Tango Dark", new Expectation { Background = 0x000000, Foreground = 0xD3D7CF } },
            { "Tango Light", new Expectation { Background = 0xFFFFFF, Foreground = 0x555753 } },
        };

        private static readonly Color Sentinel = Color.FromArgb(255, 0xFE, 0x01, 0xFE);

        static partial void RegisterThemeChecks()
        {
            Application.EnableVisualStyles();
            Register("WinForms", "ClrPInvokeSeesPalette", "-", false, false, ClrPInvokeSeesPalette);
            Register("WinForms", "ExistingSystemColorsCacheRefreshes", "-", false, false, ExistingSystemColorsCacheRefreshes);
            Register("WinForms", "NewControlsUsePalette", "-", false, false, NewControlsUsePalette);
            Register("WinForms", "DarkToLightRefreshes", "-", false, false, DarkToLightRefreshes);
            Register("WinForms", "NoneRestoresSystemColorsAndPixels", "none", false, true, NoneRestoresSystemColorsAndPixels);
            Register("WinForms", "PreInit", "pre-init", true, true, PreInit);
            Register("WinForms", "PostInit", "post-init", true, true, PostInit);
            Register("WinForms", "StockMatchesBaseline", "stock", true, false, StockMatchesBaseline);
        }

        private static Color Rgb(int rgb)
        {
            return Color.FromArgb(255, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }

        private static Color FromColorRef(int colorRef)
        {
            return Color.FromArgb(255, colorRef & 0xFF, (colorRef >> 8) & 0xFF, (colorRef >> 16) & 0xFF);
        }

        private static Color Mix(Color from, Color to, double weight)
        {
            Func<int, int, int> channel = (a, b) => (int)Math.Round(a + (b - a) * weight, MidpointRounding.AwayFromZero);
            return Color.FromArgb(255, channel(from.R, to.R), channel(from.G, to.G), channel(from.B, to.B));
        }

        private static string Hex(Color? c)
        {
            return c.HasValue ? string.Format("{0:X2}{1:X2}{2:X2}", c.Value.R, c.Value.G, c.Value.B) : "(none)";
        }

        private static void Pump(int milliseconds)
        {
            DateTime end = DateTime.UtcNow.AddMilliseconds(milliseconds);
            while (DateTime.UtcNow < end)
            {
                Application.DoEvents();
                Thread.Sleep(10);
            }
        }

        private static bool WaitFor(Func<bool> condition, int milliseconds)
        {
            DateTime end = DateTime.UtcNow.AddMilliseconds(milliseconds);
            while (DateTime.UtcNow < end)
            {
                Application.DoEvents();
                if (condition())
                {
                    return true;
                }
                Thread.Sleep(10);
            }
            return condition();
        }

        private static void SaveScheme(string scheme)
        {
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(SettingsKey()))
            {
                if (scheme == null)
                {
                    key.DeleteValue("Scheme", false);
                }
                else
                {
                    key.SetValue("Scheme", scheme, RegistryValueKind.String);
                }
            }
        }

        private sealed class Bridge
        {
            public NativeMethods.InitializeHarness Initialize;
            public NativeMethods.SwitchHarnessScheme Switch;
        }

        private static Bridge LoadBridge(Recorder r, CheckContext context)
        {
            IntPtr module = NativeMethods.LoadLibraryW(context.BridgePath);
            if (module == IntPtr.Zero)
            {
                r.Expect(false, "harness bridge loads from " + context.BridgePath);
                return null;
            }
            var bridge = new Bridge
            {
                Initialize = NativeMethods.Export<NativeMethods.InitializeHarness>(module, "InitializeHarness"),
                Switch = NativeMethods.Export<NativeMethods.SwitchHarnessScheme>(module, "SwitchHarnessScheme"),
            };
            if (bridge.Initialize == null || bridge.Switch == null)
            {
                r.Invalidate("bridge exports missing");
                return null;
            }
            bridge.Initialize();
            return bridge;
        }

        private static NativeMethods.CustomInitialize LoadProduction(Recorder r, CheckContext context)
        {
            IntPtr module = NativeMethods.LoadLibraryW(context.DllPath);
            if (module == IntPtr.Zero)
            {
                r.Invalidate("cannot load the module " + context.DllPath);
                return null;
            }
            var init = NativeMethods.Export<NativeMethods.CustomInitialize>(module, "CustomInitialize");
            if (init == null)
            {
                r.Invalidate("CustomInitialize not exported");
            }
            return init;
        }

        private static ThemeForm ShowForm(string title)
        {
            var form = new ThemeForm(title);
            form.Show();
            Pump(500);
            return form;
        }

        private static Bitmap Capture(Form form)
        {
            NativeMethods.DwmFlush();
            var bitmap = new Bitmap(form.ClientSize.Width, form.ClientSize.Height);
            using (Graphics g = Graphics.FromImage(bitmap))
            {
                g.Clear(Sentinel);
                IntPtr dc = g.GetHdc();
                bool printed = NativeMethods.PrintWindow(form.Handle, dc, NativeMethods.PW_CLIENTONLY | NativeMethods.PW_RENDERFULLCONTENT);
                g.ReleaseHdc(dc);
                if (!printed)
                {
                    bitmap.Dispose();
                    return null;
                }
            }
            return bitmap;
        }

        private static Samples Sample(Recorder r, ThemeForm form)
        {
            var points = new[]
            {
                form.PointToClient(form.Input.PointToScreen(new Point(form.Input.ClientSize.Width - 8, form.Input.ClientSize.Height / 2))),
                form.PointToClient(form.List.PointToScreen(new Point(form.List.ClientSize.Width / 2, form.List.ClientSize.Height - 10))),
                new Point(form.ClientSize.Width - 10, form.ClientSize.Height - 10),
            };
            using (Bitmap first = Capture(form))
            using (Bitmap second = Capture(form))
            {
                if (first == null || second == null)
                {
                    r.Invalidate("PrintWindow failed");
                    return new Samples();
                }
                var values = new Color?[points.Length];
                for (int i = 0; i < points.Length; i++)
                {
                    Color a = first.GetPixel(points[i].X, points[i].Y);
                    Color b = second.GetPixel(points[i].X, points[i].Y);
                    if (a.ToArgb() == Sentinel.ToArgb())
                    {
                        r.Invalidate("sample " + i + " was not drawn by the capture");
                    }
                    else if (a.ToArgb() != b.ToArgb())
                    {
                        r.Invalidate("sample " + i + " unstable between captures");
                    }
                    else
                    {
                        values[i] = Color.FromArgb(255, a.R, a.G, a.B);
                    }
                }
                return new Samples { TextBox = values[0], ListView = values[1], Form = values[2] };
            }
        }

        private static void ExpectColour(Recorder r, Color? actual, Color want, string what)
        {
            if (actual.HasValue)
            {
                r.Expect(actual.Value.ToArgb() == want.ToArgb(), what + " " + Hex(actual) + " want " + Hex(want));
            }
        }

        private static bool SystemWindowIs(Color want)
        {
            return SystemColors.Window.ToArgb() == want.ToArgb();
        }

        private static void ExpectThemed(Recorder r, ThemeForm form, string scheme)
        {
            Expectation e = Expected[scheme];
            Color window = Rgb(e.Background);
            Color face = Mix(Rgb(e.Background), Rgb(e.Foreground), 0.08);
            r.Expect(WaitFor(() => SystemWindowIs(window) && SystemColors.WindowText.ToArgb() == Rgb(e.Foreground).ToArgb() && SystemColors.Control.ToArgb() == face.ToArgb(), 3000), scheme + " SystemColors.Window " + Hex(SystemColors.Window) + " want " + Hex(window));
            r.Expect(SystemColors.WindowText.ToArgb() == Rgb(e.Foreground).ToArgb(), scheme + " SystemColors.WindowText " + Hex(SystemColors.WindowText));
            r.Expect(SystemColors.Control.ToArgb() == face.ToArgb(), scheme + " SystemColors.Control " + Hex(SystemColors.Control) + " want " + Hex(face));
            r.Expect(FromColorRef(NativeMethods.GetSysColor(NativeMethods.COLOR_WINDOW)).ToArgb() == window.ToArgb(), scheme + " P/Invoke GetSysColor(COLOR_WINDOW)");
            Pump(400);
            Samples s = Sample(r, form);
            ExpectColour(r, s.TextBox, window, scheme + " TextBox background");
            ExpectColour(r, s.ListView, window, scheme + " ListView background");
            ExpectColour(r, s.Form, face, scheme + " Form background");
        }

        private sealed class Baseline
        {
            public int Window;
            public int Control;
            public int WindowText;
            public int NativeWindow;
            public Samples Pixels;
        }

        private static Baseline TakeBaseline(Recorder r, ThemeForm form)
        {
            return new Baseline
            {
                Window = SystemColors.Window.ToArgb(),
                Control = SystemColors.Control.ToArgb(),
                WindowText = SystemColors.WindowText.ToArgb(),
                NativeWindow = NativeMethods.GetSysColor(NativeMethods.COLOR_WINDOW),
                Pixels = Sample(r, form),
            };
        }

        private static void ExpectBaseline(Recorder r, Baseline b, ThemeForm form, string label)
        {
            r.Expect(WaitFor(() => SystemColors.Window.ToArgb() == b.Window && SystemColors.Control.ToArgb() == b.Control && SystemColors.WindowText.ToArgb() == b.WindowText, 3000), label + " SystemColors.Window " + Hex(SystemColors.Window));
            r.Expect(SystemColors.Control.ToArgb() == b.Control, label + " SystemColors.Control " + Hex(SystemColors.Control));
            r.Expect(SystemColors.WindowText.ToArgb() == b.WindowText, label + " SystemColors.WindowText " + Hex(SystemColors.WindowText));
            r.Expect(NativeMethods.GetSysColor(NativeMethods.COLOR_WINDOW) == b.NativeWindow, label + " P/Invoke GetSysColor(COLOR_WINDOW)");
            Pump(400);
            Samples now = Sample(r, form);
            if (b.Pixels.TextBox.HasValue) ExpectColour(r, now.TextBox, b.Pixels.TextBox.Value, label + " TextBox background");
            if (b.Pixels.ListView.HasValue) ExpectColour(r, now.ListView, b.Pixels.ListView.Value, label + " ListView background");
            if (b.Pixels.Form.HasValue) ExpectColour(r, now.Form, b.Pixels.Form.Value, label + " Form background");
        }

        private static int ClrPInvokeSeesPalette(CheckContext context)
        {
            var r = new Recorder("ClrPInvokeSeesPalette");
            Bridge bridge = LoadBridge(r, context);
            if (bridge == null)
            {
                return r.Result;
            }
            r.Expect(bridge.Switch("Campbell") == 1, "switch to Campbell");
            r.Expect(FromColorRef(NativeMethods.GetSysColor(NativeMethods.COLOR_WINDOW)).ToArgb() == Rgb(0x0C0C0C).ToArgb(), "P/Invoke GetSysColor sees the palette");
            r.Expect(WaitFor(() => SystemWindowIs(Rgb(0x0C0C0C)), 3000), "SystemColors.Window " + Hex(SystemColors.Window));
            return r.Result;
        }

        private static int ExistingSystemColorsCacheRefreshes(CheckContext context)
        {
            var r = new Recorder("ExistingSystemColorsCacheRefreshes");
            ThemeForm form = ShowForm("ExistingSystemColorsCacheRefreshes");
            r.Expect(SystemColors.Window.ToArgb() != Rgb(0x0C0C0C).ToArgb(), "stock SystemColors cached first: " + Hex(SystemColors.Window));
            Bridge bridge = LoadBridge(r, context);
            if (bridge == null)
            {
                return r.Result;
            }
            r.Expect(bridge.Switch("Campbell") == 1, "switch to Campbell");
            ExpectThemed(r, form, "Campbell");
            form.Close();
            return r.Result;
        }

        private static int NewControlsUsePalette(CheckContext context)
        {
            var r = new Recorder("NewControlsUsePalette");
            Bridge bridge = LoadBridge(r, context);
            if (bridge == null)
            {
                return r.Result;
            }
            r.Expect(bridge.Switch("Solarized Dark") == 1, "switch to Solarized Dark");
            Pump(300);
            ThemeForm form = ShowForm("NewControlsUsePalette");
            ExpectThemed(r, form, "Solarized Dark");
            form.Close();
            return r.Result;
        }

        private static int DarkToLightRefreshes(CheckContext context)
        {
            var r = new Recorder("DarkToLightRefreshes");
            ThemeForm form = ShowForm("DarkToLightRefreshes");
            Bridge bridge = LoadBridge(r, context);
            if (bridge == null)
            {
                return r.Result;
            }
            r.Expect(bridge.Switch("Tango Dark") == 1, "switch to Tango Dark");
            ExpectThemed(r, form, "Tango Dark");
            r.Expect(bridge.Switch("One Half Light") == 1, "switch to One Half Light");
            ExpectThemed(r, form, "One Half Light");
            form.Close();
            return r.Result;
        }

        private static int NoneRestoresSystemColorsAndPixels(CheckContext context)
        {
            var r = new Recorder("NoneRestoresSystemColorsAndPixels");
            ThemeForm form = ShowForm("NoneRestoresSystemColorsAndPixels");
            Baseline baseline = TakeBaseline(r, form);
            Bridge bridge = LoadBridge(r, context);
            if (bridge == null)
            {
                return r.Result;
            }
            r.Expect(bridge.Switch(context.Scheme) == 1, "switch to " + context.Scheme);
            ExpectThemed(r, form, context.Scheme);
            r.Expect(bridge.Switch("None") == 1, "switch to None");
            ExpectBaseline(r, baseline, form, "after None");
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(SettingsKey()))
            {
                r.Expect(key == null || key.GetValue("Scheme") == null, "Scheme value deleted");
            }
            form.Close();
            return r.Result;
        }

        private static int PreInit(CheckContext context)
        {
            var r = new Recorder("PreInit");
            SaveScheme(context.Scheme);
            NativeMethods.CustomInitialize init = LoadProduction(r, context);
            if (init == null)
            {
                return r.Result;
            }
            r.Expect(init(NativeMethods.AAMODULE_EXPLORER, IntPtr.Zero) == 1, "CustomInitialize returns IDOK");
            Pump(300);
            ThemeForm form = ShowForm("PreInit");
            ExpectThemed(r, form, context.Scheme);
            form.Close();
            return r.Result;
        }

        private static int PostInit(CheckContext context)
        {
            var r = new Recorder("PostInit");
            ThemeForm form = ShowForm("PostInit");
            r.Expect(SystemColors.Window.ToArgb() == FromColorRef(NativeMethods.GetSysColor(NativeMethods.COLOR_WINDOW)).ToArgb(), "stock SystemColors cached first");
            SaveScheme(context.Scheme);
            NativeMethods.CustomInitialize init = LoadProduction(r, context);
            if (init == null)
            {
                return r.Result;
            }
            r.Expect(init(NativeMethods.AAMODULE_EXPLORER, IntPtr.Zero) == 1, "CustomInitialize returns IDOK");
            ExpectThemed(r, form, context.Scheme);
            form.Close();
            return r.Result;
        }

        private static int StockMatchesBaseline(CheckContext context)
        {
            var r = new Recorder("StockMatchesBaseline");
            ThemeForm form = ShowForm("StockMatchesBaseline");
            Baseline baseline = TakeBaseline(r, form);
            SaveScheme(null);
            NativeMethods.CustomInitialize init = LoadProduction(r, context);
            if (init == null)
            {
                return r.Result;
            }
            r.Expect(init(NativeMethods.AAMODULE_EXPLORER, IntPtr.Zero) == 1, "CustomInitialize returns IDOK");
            Pump(500);
            ExpectBaseline(r, baseline, form, "no selection");
            form.Close();
            return r.Result;
        }
    }
}
