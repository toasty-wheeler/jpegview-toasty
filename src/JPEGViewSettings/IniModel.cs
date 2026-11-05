using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;

namespace JPEGViewSettings
{
    public enum SettingKind { Text, Bool, Choice, Color, Font }

    public class Setting
    {
        public string Key;
        public string Category;
        public string Description = "";
        public string GlobalValue;      // value in the global INI next to JPEGView.exe, null if not present there
        public string UserValue;        // value in the user INI, null if not overridden there
        public string BaselineValue;    // what "default" means for this setting, null if unknown
        public SettingKind Kind = SettingKind.Text;
        public string[] Choices;
        public bool ChoiceEditable;
        public string PerformanceHint;

        public string EffectiveValue => UserValue ?? GlobalValue ?? "";
        public bool IsNumeric => GlobalValue != null && IniModel.IsNumber(GlobalValue);
    }

    // Reads the settings and their documentation from the global JPEGView.ini and the overrides from the user INI.
    // Writing is done with WritePrivateProfileString, the same API JPEGView uses, so the file format stays compatible.
    public class IniModel
    {
        public const string Section = "JPEGView";
        public const string OtherCategory = "Other (only in user INI)";

        public string GlobalIniPath { get; }
        public string UserIniPath { get; }
        public bool SingleFile => string.Equals(Path.GetFullPath(GlobalIniPath), Path.GetFullPath(UserIniPath), StringComparison.OrdinalIgnoreCase);
        public List<Setting> Settings { get; } = new List<Setting>();
        public List<string> Categories { get; } = new List<string>();

        static readonly Regex KeyLine = new Regex(@"^\s*([A-Za-z][A-Za-z0-9]*)\s*=\s*(.*?)\s*$");
        static readonly Regex HeaderLine = new Regex(@"^;\s*\*\s+([A-Z][A-Z0-9 /&\-]+?)\s*$");
        static readonly Regex SectionLine = new Regex(@"^\s*\[(.+)\]\s*$");
        // "; ---------" and "; - AUTO CONTRAST CORRECTION", but not "; -1: Use monitor..."
        static readonly Regex SubHeaderLine = new Regex(@"^;\s*(-{3,}|-\s+[A-Z][A-Z0-9 /&\-]*)\s*$");

        public IniModel(string globalIniPath, string userIniPath)
        {
            GlobalIniPath = globalIniPath;
            UserIniPath = userIniPath;
            LoadGlobal(ReadLines(GlobalIniPath));
            // settings added in newer versions are missing from an older single INI, list them with their stock value
            if (SingleFile) LoadGlobal(ReadDefaultLines());
            LoadUser();
            LoadBaseline();
            ApplyMetadata();
        }

        void LoadBaseline()
        {
            if (!SingleFile)
            {
                // the user INI overrides the global INI, so removing a key from the user INI restores the global value
                foreach (Setting s in Settings) s.BaselineValue = s.GlobalValue;
                return;
            }
            // Only one INI file: compare against the stock settings shipped with JPEGView
            var defaults = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (string line in ReadDefaultLines())
            {
                Match m = KeyLine.Match(line);
                if (m.Success && !defaults.ContainsKey(m.Groups[1].Value)) defaults[m.Groups[1].Value] = m.Groups[2].Value;
            }
            foreach (Setting s in Settings) defaults.TryGetValue(s.Key, out s.BaselineValue);
        }

        // The stock JPEGView.ini, embedded at build time
        static IEnumerable<string> ReadDefaultLines()
        {
            using (var stream = typeof(IniModel).Assembly.GetManifestResourceStream("DefaultJPEGView.ini"))
            using (var reader = new StreamReader(stream, Encoding.Default))
            {
                string line;
                var lines = new List<string>();
                while ((line = reader.ReadLine()) != null) lines.Add(line);
                return lines;
            }
        }

        // Joins the hard wrapped lines of an INI comment into paragraphs, but keeps line breaks of lists and examples
        static string JoinComment(List<string> lines)
        {
            var sb = new StringBuilder();
            for (int i = 0; i < lines.Count; i++)
            {
                string line = lines[i].TrimEnd();
                if (i > 0)
                {
                    bool continuation = line.Length > 0 && char.IsLower(line[0]) && lines[i - 1].Trim().Length > 0;
                    sb.Append(continuation ? " " : "\n");
                }
                sb.Append(line);
            }
            return sb.ToString().Trim();
        }

        public static bool IsNumber(string s) =>
            double.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out _);

        static IEnumerable<string> ReadLines(string path)
        {
            // INI files written by Windows are ANSI or UTF-16 with BOM; File.ReadAllLines detects the BOM
            return File.Exists(path) ? File.ReadAllLines(path, Encoding.Default) : new string[0];
        }

        void LoadGlobal(IEnumerable<string> lines)
        {
            string category = "General";
            string section = null;
            var comments = new List<string>();
            string lastDescription = "";
            foreach (string line in lines)
            {
                string trimmed = line.Trim();
                Match m;
                if ((m = SectionLine.Match(trimmed)).Success) { section = m.Groups[1].Value; continue; }
                if (!string.Equals(section, Section, StringComparison.OrdinalIgnoreCase)) continue;

                if (trimmed.Length == 0)
                {
                    comments.Clear();
                    lastDescription = "";
                    continue;
                }
                if (trimmed.StartsWith(";"))
                {
                    if ((m = HeaderLine.Match(trimmed)).Success)
                    {
                        category = CategoryName(m.Groups[1].Value);
                        comments.Clear();
                    }
                    else if (!trimmed.StartsWith("; *") && !trimmed.StartsWith(";*") && !SubHeaderLine.IsMatch(trimmed))
                    {
                        string text = trimmed.Substring(1);
                        comments.Add(text.StartsWith(" ") ? text.Substring(1) : text);
                    }
                    continue;
                }
                if ((m = KeyLine.Match(line)).Success)
                {
                    string key = m.Groups[1].Value;
                    if (Settings.Any(s => s.Key.Equals(key, StringComparison.OrdinalIgnoreCase))) continue;
                    // several keys can share one comment block, e.g. ACCExclude / ACCInclude
                    string description = comments.Count > 0 ? JoinComment(comments) : lastDescription;
                    Settings.Add(new Setting { Key = key, Category = category, Description = description, GlobalValue = m.Groups[2].Value });
                    if (!Categories.Contains(category)) Categories.Add(category);
                    lastDescription = description;
                    comments.Clear();
                }
            }
        }

        void LoadUser()
        {
            if (SingleFile) return;
            string section = null;
            foreach (string line in ReadLines(UserIniPath))
            {
                Match m;
                if ((m = SectionLine.Match(line)).Success) { section = m.Groups[1].Value; continue; }
                if (!string.Equals(section, Section, StringComparison.OrdinalIgnoreCase)) continue;
                if (line.TrimStart().StartsWith(";")) continue;
                if (!(m = KeyLine.Match(line)).Success) continue;

                string key = m.Groups[1].Value;
                Setting s = Settings.FirstOrDefault(x => x.Key.Equals(key, StringComparison.OrdinalIgnoreCase));
                if (s == null)
                {
                    s = new Setting
                    {
                        Key = key,
                        Category = OtherCategory,
                        Description = key.StartsWith("UserCmd") || key.StartsWith("OpenWith")
                            ? "Custom command defined in your user INI."
                            : "This key is not documented in the global INI. It may be obsolete (renamed or removed in this JPEGView version) and is probably ignored."
                    };
                    Settings.Add(s);
                    if (!Categories.Contains(OtherCategory)) Categories.Add(OtherCategory);
                }
                s.UserValue = m.Groups[2].Value; // last occurrence wins, like in JPEGView
            }
        }

        static string CategoryName(string header)
        {
            string name = Regex.Replace(header, @"\s+(OPTIONS|CONFIG)$", "");
            name = name.ToLowerInvariant();
            return char.ToUpperInvariant(name[0]) + name.Substring(1);
        }

        void ApplyMetadata()
        {
            foreach (Setting s in Settings)
            {
                if (Metadata.Choices.TryGetValue(s.Key, out var choice))
                {
                    s.Kind = SettingKind.Choice;
                    s.Choices = choice.Item1;
                    s.ChoiceEditable = choice.Item2;
                }
                else if (s.Key == "Language")
                {
                    s.Kind = SettingKind.Choice;
                    s.ChoiceEditable = true;
                    s.Choices = new[] { "auto", "en" }.Concat(LanguagesNextToExe()).Distinct().ToArray();
                }
                else if (s.Key.EndsWith("Font"))
                {
                    s.Kind = SettingKind.Font;
                }
                else if (s.Key.EndsWith("Color") && Regex.IsMatch(s.EffectiveValue, @"^\s*\d+\s+\d+\s+\d+\s*$"))
                {
                    s.Kind = SettingKind.Color;
                }
                else if (IsBool(s.GlobalValue ?? s.UserValue))
                {
                    s.Kind = SettingKind.Bool;
                }
                Metadata.PerformanceHints.TryGetValue(s.Key, out s.PerformanceHint);
            }
        }

        IEnumerable<string> LanguagesNextToExe()
        {
            string dir = Path.GetDirectoryName(GlobalIniPath);
            if (dir == null || !Directory.Exists(dir)) return Enumerable.Empty<string>();
            return Directory.GetFiles(dir, "strings_*.txt")
                .Select(f => Path.GetFileNameWithoutExtension(f).Substring("strings_".Length))
                .OrderBy(x => x);
        }

        public static bool IsBool(string v) =>
            v != null && (v.Trim().Equals("true", StringComparison.OrdinalIgnoreCase) || v.Trim().Equals("false", StringComparison.OrdinalIgnoreCase));

        // Writes the value to the user INI, or removes the key from the user INI when value is null
        // (JPEGView then falls back to the global INI value).
        public void Write(Setting s, string value)
        {
            string dir = Path.GetDirectoryName(UserIniPath);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            if (!WritePrivateProfileString(Section, s.Key, value, UserIniPath))
                throw new IOException($"Could not write '{s.Key}' to {UserIniPath} (error {Marshal.GetLastWin32Error()}).");
            if (SingleFile) s.GlobalValue = value;
            else s.UserValue = value;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool WritePrivateProfileString(string section, string key, string value, string fileName);
    }

    static class Metadata
    {
        // key -> (choices, editable)
        public static readonly Dictionary<string, Tuple<string[], bool>> Choices = new Dictionary<string, Tuple<string[], bool>>(StringComparer.OrdinalIgnoreCase)
        {
            ["ShowFullScreen"] = C(false, "auto", "true", "false"),
            ["DefaultWindowRect"] = C(true, "auto", "max", "image", "sticky"),
            ["CPUType"] = C(false, "AutoDetect", "Generic", "MMX", "SSE", "AVX2"),
            ["DownSamplingFilter"] = C(false, "BestQuality", "NoAliasing", "Narrow"),
            ["FileDisplayOrder"] = C(false, "LastModDate", "CreationDate", "FileName", "FileSize", "Random"),
            ["FolderNavigation"] = C(false, "LoopFolder", "LoopSameFolderLevel", "LoopSubFolders"),
            ["AutoZoomMode"] = C(false, "FitNoZoom", "FillNoZoom", "Fit", "Fill"),
            ["AutoZoomModeFullscreen"] = C(false, "", "FitNoZoom", "FillNoZoom", "Fit", "Fill"),
            ["SlideShowTransitionEffect"] = C(false, "None", "Blend", "SlideRL", "SlideLR", "SlideTB", "SlideBT", "RollRL", "RollLR", "RollTB", "RollBT", "ScrollRL", "ScrollLR", "ScrollTB", "ScrollBT"),
            ["Units"] = C(false, "auto", "metric", "english"),
            ["DeleteConfirmation"] = C(false, "Never", "OnlyWhenNoRecycleBin", "Always"),
            ["DefaultSaveFormat"] = C(false, "jpg", "bmp", "png", "tif", "webp"),
            ["IniEditor"] = C(true, "%exepath%JPEGViewSettings.exe", "notepad", "system"),
            ["DisplayFullSizeRAW"] = C(false, "0", "1", "2", "3"),
            ["DisplayMonitor"] = C(true, "-1", "0", "1", "2"),
            ["CPUCoresUsed"] = C(false, "0", "1", "2", "3", "4"),
        };

        static Tuple<string[], bool> C(bool editable, params string[] values) => Tuple.Create(values, editable);

        // Settings that influence how fast images open
        public static readonly Dictionary<string, string> PerformanceHints = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["UseEmbeddedColorProfiles"] = "true forces GDI+ for JPEG decoding, which is several times slower than libjpeg-turbo. Keep false unless you need color management.",
            ["ForceGDIPlus"] = "true makes JPEG decoding much slower. Keep false.",
            ["CPUType"] = "AutoDetect picks the fastest SIMD code path (AVX2 on modern CPUs).",
            ["CPUCoresUsed"] = "0 (auto) uses all available cores for resizing and filtering.",
            ["HighQualityResampling"] = "Small extra cost per image; turning it off only helps on very slow PCs.",
            ["AutoContrastCorrection"] = "Adds a histogram pass to every image.",
            ["LocalDensityCorrection"] = "Adds a noticeable processing pass to every image.",
            ["LandscapeMode"] = "Enables both contrast and local density correction on every image.",
            ["DisplayFullSizeRAW"] = "0 (embedded thumbnail) opens RAW files fastest; full size decoding is slow.",
            ["FolderNavigation"] = "LoopSubFolders and LoopSameFolderLevel scan additional folders when you reach the end of a folder.",
            ["ReloadWhenDisplayedImageChanged"] = "Watches the folder for changes; folders that change constantly (downloads, screenshots) cause reloads.",
            ["FilesProcessedByWIC"] = "WIC decoding is slower than the built-in decoders; only list formats nothing else can read.",
            ["ShowHistogram"] = "Computes a histogram for every image while the info panel is visible.",
        };
    }
}
