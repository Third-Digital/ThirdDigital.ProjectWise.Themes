using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Win32;

namespace ThirdDigital.ProjectWise.Themes.Harness.WinForms
{
    internal sealed class CheckContext
    {
        public string Scheme = "";
        public string DllPath = "";
        public string BridgePath = "";
        public string EvidenceDir = "";
    }

    internal sealed class Check
    {
        public string Group;
        public string Name;
        public string Mode;
        public bool Pwc;
        public bool PerScheme;
        public Func<CheckContext, int> Run;
    }

    internal sealed class Recorder
    {
        public const int Passed = 0;
        public const int Failed = 1;
        public const int Invalid = 2;

        private readonly string name;
        private int failures;
        private int invalid;

        public Recorder(string name)
        {
            this.name = name;
        }

        public bool Expect(bool condition, string message)
        {
            Console.WriteLine("{0} {1}: {2}", condition ? "ok  " : "FAIL", name, message);
            if (!condition)
            {
                failures++;
            }
            return condition;
        }

        public void Invalidate(string message)
        {
            Console.WriteLine("INVALID {0}: {1}", name, message);
            invalid++;
        }

        public void Note(string message)
        {
            Console.WriteLine("note {0}: {1}", name, message);
        }

        public int Result
        {
            get { return invalid > 0 ? Invalid : failures > 0 ? Failed : Passed; }
        }
    }

    internal static partial class Program
    {
        private static readonly List<Check> Checks = new List<Check>();

        private static void Register(string group, string name, string mode, bool pwc, bool perScheme, Func<CheckContext, int> run)
        {
            Checks.Add(new Check { Group = group, Name = name, Mode = mode, Pwc = pwc, PerScheme = perScheme, Run = run });
        }

        private static void RegisterSmokeChecks()
        {
            Register("Smoke", "ProcessIsX86", "-", false, false, ProcessIsX86);
            Register("Smoke", "SettingsKeyIsIsolated", "-", false, false, SettingsKeyIsIsolated);
        }

        private static int ProcessIsX86(CheckContext context)
        {
            var r = new Recorder("ProcessIsX86");
            r.Expect(IntPtr.Size == 4, "pointer size " + IntPtr.Size);
            r.Expect(!Environment.Is64BitProcess, "64-bit process " + Environment.Is64BitProcess);
            return r.Result;
        }

        private static string SettingsKey()
        {
            return Environment.GetEnvironmentVariable("TDPWTHEMES_SETTINGS_KEY") ?? "";
        }

        private static int SettingsKeyIsIsolated(CheckContext context)
        {
            var r = new Recorder("SettingsKeyIsIsolated");
            string key = SettingsKey();
            if (!r.Expect(key.StartsWith(@"Software\TDPWThemesHarness-", StringComparison.Ordinal), "settings key '" + key + "'"))
            {
                return r.Result;
            }
            using (RegistryKey handle = Registry.CurrentUser.CreateSubKey(key))
            {
                if (handle == null)
                {
                    r.Invalidate("cannot create test key");
                    return r.Result;
                }
                handle.SetValue("Marker", "smoke");
            }
            r.Expect(true, "marker written for the runner's cleanup check");
            return r.Result;
        }

        private static string Argument(string[] args, string name)
        {
            for (int i = 0; i + 1 < args.Length; i++)
            {
                if (args[i] == name)
                {
                    return args[i + 1];
                }
            }
            return null;
        }

        [STAThread]
        private static int Main(string[] args)
        {
            RegisterSmokeChecks();
            RegisterThemeChecks();

            if (args.Contains("--list"))
            {
                string group = Argument(args, "--filter");
                foreach (Check check in Checks.Where(c => string.IsNullOrEmpty(group) || string.Equals(c.Group, group, StringComparison.OrdinalIgnoreCase)))
                {
                    Console.WriteLine("{0}\t{1}\t{2}\t{3}\t{4}", check.Group, check.Name, check.Mode, check.Pwc ? "pwc" : "self", check.PerScheme ? "*" : "-");
                }
                return Recorder.Passed;
            }

            string name = Argument(args, "--run");
            if (name == null)
            {
                Console.WriteLine("usage: --list [--filter group] | --run check [--scheme name] [--dll path] [--bridge path] [--evidence dir]");
                return Recorder.Invalid;
            }

            Check selected = Checks.FirstOrDefault(c => c.Name == name);
            if (selected == null)
            {
                Console.WriteLine("INVALID unknown check '" + name + "'");
                return Recorder.Invalid;
            }

            var context = new CheckContext
            {
                Scheme = Argument(args, "--scheme") ?? "",
                DllPath = Argument(args, "--dll") ?? "",
                BridgePath = Argument(args, "--bridge") ?? "",
                EvidenceDir = Argument(args, "--evidence") ?? "",
            };
            Console.WriteLine("note desktop " + NativeMethods.DesktopName());
            int result = selected.Run(context);
            string key = SettingsKey();
            if (key.StartsWith(@"Software\TDPWThemesHarness-", StringComparison.Ordinal))
            {
                Registry.CurrentUser.DeleteSubKeyTree(key, false);
            }
            return result;
        }

        static partial void RegisterThemeChecks();
    }
}
