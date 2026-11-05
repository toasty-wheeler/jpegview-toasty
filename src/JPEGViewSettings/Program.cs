using System;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace JPEGViewSettings
{
    static class Program
    {
        // Usage: JPEGViewSettings.exe [path to INI to edit]
        // JPEGView passes the user INI path when IniEditor=%exepath%JPEGViewSettings.exe
        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            string globalIni = FindGlobalIni();
            if (globalIni == null)
            {
                MessageBox.Show("JPEGView.ini was not found. Put JPEGViewSettings.exe in the same folder as JPEGView.exe.",
                    "JPEGView Settings", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            // JPEGView does not quote the argument, so join everything in case the path contains spaces
            string argPath = string.Join(" ", args).Trim().Trim('"');
            string userIni = argPath.Length > 0 ? argPath : DefaultUserIni(globalIni);

            Application.Run(new MainForm(new IniModel(globalIni, userIni)));
        }

        static string FindGlobalIni()
        {
            var candidates = new[]
            {
                AppDomain.CurrentDomain.BaseDirectory,
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), @"scoop\apps\jpegview-fork\current"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "JPEGView"),
            };
            return candidates.Select(dir => Path.Combine(dir, "JPEGView.ini")).FirstOrDefault(File.Exists);
        }

        static string DefaultUserIni(string globalIni)
        {
            // StoreToEXEPath=true makes JPEGView keep its settings in the global INI only
            bool storeToExePath = File.ReadLines(globalIni).Any(l =>
                l.Replace(" ", "").Equals("StoreToEXEPath=true", StringComparison.OrdinalIgnoreCase));
            if (storeToExePath) return globalIni;
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), @"JPEGView\JPEGView.ini");
        }
    }
}
