using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace AzForgeSetup
{
    internal enum SetupMode
    {
        Player,
        Developer
    }

    internal sealed class StartupArgs
    {
        public bool Uninstall { get; set; }
        public bool AutoInstall { get; set; }
        public bool AutoUninstall { get; set; }
        public UiLang? Lang { get; set; }
        public SetupMode? Mode { get; set; }
        public string GameDir { get; set; }
        public string SdkDir { get; set; }
        public bool? InstallSampleMod { get; set; }
        public bool? AddToPath { get; set; }
        public bool? InstallVsCodeExt { get; set; }
        public bool? IncludeVsVsix { get; set; }

        public static StartupArgs Parse(string[] args)
        {
            var r = new StartupArgs();
            if (args == null) return r;

            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i];
                string Next() => (i + 1 < args.Length) ? args[++i] : string.Empty;

                if (a.Equals("--uninstall", StringComparison.OrdinalIgnoreCase))
                    r.Uninstall = true;
                else if (a.Equals("--auto-uninstall", StringComparison.OrdinalIgnoreCase))
                {
                    r.Uninstall = true;
                    r.AutoUninstall = true;
                }
                else if (a.Equals("--install", StringComparison.OrdinalIgnoreCase))
                    r.AutoInstall = true;
                else if (a.Equals("--lang", StringComparison.OrdinalIgnoreCase))
                    r.Lang = Next().Equals("ru", StringComparison.OrdinalIgnoreCase) ? UiLang.Ru : UiLang.En;
                else if (a.Equals("--mode", StringComparison.OrdinalIgnoreCase))
                    r.Mode = Next().Equals("dev", StringComparison.OrdinalIgnoreCase) ? SetupMode.Developer : SetupMode.Player;
                else if (a.Equals("--game", StringComparison.OrdinalIgnoreCase))
                    r.GameDir = Next();
                else if (a.Equals("--sdk", StringComparison.OrdinalIgnoreCase))
                    r.SdkDir = Next();
                else if (a.Equals("--sample", StringComparison.OrdinalIgnoreCase))
                    r.InstallSampleMod = Next() == "1";
                else if (a.Equals("--path", StringComparison.OrdinalIgnoreCase))
                    r.AddToPath = Next() == "1";
                else if (a.Equals("--vscode", StringComparison.OrdinalIgnoreCase))
                    r.InstallVsCodeExt = Next() == "1";
                else if (a.Equals("--vs", StringComparison.OrdinalIgnoreCase))
                    r.IncludeVsVsix = Next() == "1";
            }
            return r;
        }
    }

    internal sealed class InstallPlan
    {
        public SetupMode Mode { get; set; } = SetupMode.Player;
        public string GameDir { get; set; } = string.Empty;
        public string SdkDir { get; set; } = string.Empty;
        public bool InstallSampleMod { get; set; }
        public bool AddToPath { get; set; }
        public bool InstallVsCodeExt { get; set; }
        public bool IncludeVsVsix { get; set; }
    }

    internal sealed class InstallProgress
    {
        public int Percent { get; set; }
        public string Status { get; set; }
        public string LogLine { get; set; }
    }

    internal sealed class InstalledManifest
    {
        public string GameDir { get; set; } = string.Empty;
        public string SdkDir { get; set; } = string.Empty;
        public string AddedPathDir { get; set; } = string.Empty;
        public bool InstalledSampleMod { get; set; }
        public List<string> InstalledFiles { get; } = new List<string>();
    }

    internal static class InstallerEngine
    {
        public const string AppName = "AzForge";
        public const string DocsUrl = "https://bazkamaz.github.io/AzForge/docs/index.html#/overview";
        public const string TemplatesRegKey = @"Software\AzForge\Templates";
        public const string UninstallRegKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\AzForge";
        private const string PayloadResourceName = "AzForgeSetup.payload.zip";

        public static string AppVersion
        {
            get
            {
                var v = Assembly.GetExecutingAssembly().GetName().Version;
                return v != null ? $"{v.Major}.{v.Minor}.{v.Build}" : "0.1.0";
            }
        }

        public static string UninstallDir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AzForge", "Uninstall");

        public static string UninstallExePath => Path.Combine(UninstallDir, "AzForge-Uninstall.exe");
        public static string ManifestPath => Path.Combine(UninstallDir, "install-manifest.cfg");

        public static bool IsRunningFromUninstallLocation()
        {
            string self = Assembly.GetExecutingAssembly().Location;
            return !string.IsNullOrEmpty(self) &&
                   self.Equals(UninstallExePath, StringComparison.OrdinalIgnoreCase);
        }

        public static bool IsAdministrator()
        {
            try
            {
                using (var id = WindowsIdentity.GetCurrent())
                {
                    var principal = new WindowsPrincipal(id);
                    return principal.IsInRole(WindowsBuiltInRole.Administrator);
                }
            }
            catch
            {
                return false;
            }
        }

        public static bool CanWriteDirectory(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) return false;

            try
            {
                // Walk up to the nearest existing directory to test write permission if dir doesn't exist yet
                string probeDir = dir;
                while (!string.IsNullOrEmpty(probeDir) && !Directory.Exists(probeDir))
                {
                    string parent = Path.GetDirectoryName(probeDir);
                    if (parent == probeDir) break;
                    probeDir = parent;
                }

                if (string.IsNullOrEmpty(probeDir) || !Directory.Exists(probeDir))
                    return false;

                string probeFile = Path.Combine(probeDir, ".azforge_perm_" + Guid.NewGuid().ToString("N") + ".tmp");
                using (File.Create(probeFile)) { }
                File.Delete(probeFile);
                return true;
            }
            catch
            {
                return false;
            }
        }

        public static bool IsGameRunning(string gameDir)
        {
            if (string.IsNullOrWhiteSpace(gameDir)) return false;

            try
            {
                string expected = Path.GetFullPath(Path.Combine(gameDir, "game.exe"));
                foreach (var proc in Process.GetProcessesByName("game"))
                {
                    try
                    {
                        string path = proc.MainModule?.FileName;
                        if (!string.IsNullOrEmpty(path) &&
                            Path.GetFullPath(path).Equals(expected, StringComparison.OrdinalIgnoreCase))
                        {
                            return true;
                        }
                    }
                    catch
                    {
                        // Access denied inspecting another session's process: check file lock fallback below
                    }
                }
            }
            catch { }

            // Also check whether AzForge.dll or d3d8.dll is currently locked in that folder
            try
            {
                foreach (string dll in new[] { "AzForge.dll", "d3d8.dll" })
                {
                    string full = Path.Combine(gameDir, dll);
                    if (File.Exists(full))
                    {
                        try
                        {
                            using (new FileStream(full, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { }
                        }
                        catch (IOException)
                        {
                            return true;
                        }
                        catch (UnauthorizedAccessException)
                        {
                            // Permission error, not necessarily running
                        }
                    }
                }
            }
            catch { }

            return false;
        }

        public static string DetectGameDirectory()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(TemplatesRegKey))
                {
                    string saved = key?.GetValue("GameDirectory") as string;
                    if (!string.IsNullOrWhiteSpace(saved) && Directory.Exists(saved))
                        return saved.TrimEnd('\\');
                }
            }
            catch { }

            string pf86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            string[] candidates =
            {
                @"C:\Games\Azangara",
                @"D:\Games\Azangara",
                Path.Combine(pf86, "Azangara"),
                Path.Combine(pf86, "AxySoft", "Azangara"),
                Path.Combine(pf86, "Alawar", "Azangara"),
                Path.Combine(pf86, "MyPlayCity.com", "Azangara"),
                Path.Combine(pf86, "Nevosoft", "Azangara"),
                @"C:\Azangara",
                @"C:\Games\az_orginal"
            };

            foreach (string c in candidates)
            {
                if (File.Exists(Path.Combine(c, "game.exe")) && File.Exists(Path.Combine(c, "data.pak")))
                    return c;
            }

            return @"C:\Games\Azangara";
        }

        public static string DetectSdkDirectory()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(TemplatesRegKey))
                {
                    string saved = key?.GetValue("SdkDirectory") as string;
                    if (!string.IsNullOrWhiteSpace(saved))
                        return saved.TrimEnd('\\');
                }
            }
            catch { }

            return @"C:\AzForge";
        }

        public static string FindVsCodeCmd()
        {
            string local = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                @"Programs\Microsoft VS Code\bin\code.cmd");
            if (File.Exists(local)) return local;

            string pf = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                @"Microsoft VS Code\bin\code.cmd");
            if (File.Exists(pf)) return pf;

            string envPath = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
            foreach (string dir in envPath.Split(';'))
            {
                if (string.IsNullOrWhiteSpace(dir)) continue;
                try
                {
                    string candidate = Path.Combine(dir.Trim(), "code.cmd");
                    if (File.Exists(candidate)) return candidate;
                }
                catch { }
            }

            return null;
        }

        public static InstalledManifest LoadExistingManifest()
        {
            if (File.Exists(ManifestPath))
            {
                try
                {
                    var m = new InstalledManifest();
                    foreach (string raw in File.ReadAllLines(ManifestPath, Encoding.UTF8))
                    {
                        int eq = raw.IndexOf('=');
                        if (eq <= 0) continue;
                        string k = raw.Substring(0, eq).Trim();
                        string v = raw.Substring(eq + 1).Trim();
                        switch (k)
                        {
                            case "game": m.GameDir = v; break;
                            case "sdk": m.SdkDir = v; break;
                            case "path": m.AddedPathDir = v; break;
                            case "sample": m.InstalledSampleMod = v == "1"; break;
                            case "file": if (!string.IsNullOrEmpty(v)) m.InstalledFiles.Add(v); break;
                        }
                    }

                    if (!string.IsNullOrEmpty(m.GameDir) || !string.IsNullOrEmpty(m.SdkDir))
                        return m;
                }
                catch { }
            }

            // Fallback: check registry if installed previously without manifest
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(TemplatesRegKey))
                {
                    string g = key?.GetValue("GameDirectory") as string;
                    if (!string.IsNullOrWhiteSpace(g) &&
                        (File.Exists(Path.Combine(g, "d3d8.dll")) || File.Exists(Path.Combine(g, "AzForge.dll"))))
                    {
                        var m = new InstalledManifest { GameDir = g };
                        string s = key?.GetValue("SdkDirectory") as string;
                        if (!string.IsNullOrWhiteSpace(s) && Directory.Exists(s)) m.SdkDir = s;
                        return m;
                    }
                }
            }
            catch { }

            return null;
        }

        public static bool RestartElevated(string arguments)
        {
            try
            {
                var psi = new ProcessStartInfo
                {
                    FileName = Assembly.GetExecutingAssembly().Location,
                    Arguments = arguments,
                    UseShellExecute = true,
                    Verb = "runas"
                };
                Process.Start(psi);
                return true;
            }
            catch
            {
                return false;
            }
        }

        public static string BuildResumeInstallArgs(InstallPlan plan, UiLang lang)
        {
            string Q(string v) => "\"" + (v ?? string.Empty).Replace("\"", "\\\"") + "\"";
            return string.Join(" ", new[]
            {
                "--install",
                "--lang", lang == UiLang.Ru ? "ru" : "en",
                "--mode", plan.Mode == SetupMode.Developer ? "dev" : "player",
                "--game", Q(plan.GameDir),
                "--sdk", Q(plan.SdkDir),
                "--sample", plan.InstallSampleMod ? "1" : "0",
                "--path", plan.AddToPath ? "1" : "0",
                "--vscode", plan.InstallVsCodeExt ? "1" : "0",
                "--vs", plan.IncludeVsVsix ? "1" : "0"
            });
        }

        public static Task InstallAsync(InstallPlan plan, Strings s, IProgress<InstallProgress> progress)
        {
            return Task.Run(() =>
            {
                void Report(int pct, string status, string log = null)
                {
                    progress?.Report(new InstallProgress { Percent = pct, Status = status, LogLine = log });
                }

                var manifest = new InstalledManifest
                {
                    GameDir = plan.GameDir.TrimEnd('\\'),
                    SdkDir = plan.Mode == SetupMode.Developer ? plan.SdkDir.TrimEnd('\\') : string.Empty,
                    InstalledSampleMod = plan.InstallSampleMod
                };

                Stream resStream = Assembly.GetExecutingAssembly().GetManifestResourceStream(PayloadResourceName);
                if (resStream == null)
                {
                    throw new InvalidOperationException("Embedded payload.zip not found in executable.");
                }

                using (resStream)
                using (var zip = new ZipArchive(resStream, ZipArchiveMode.Read))
                {
                    Directory.CreateDirectory(manifest.GameDir);

                    // 1. Core DLLs
                    Report(10, s.LogExtractCore, s.LogExtractCore);
                    ExtractPrefix(zip, "core/", manifest.GameDir, manifest.InstalledFiles,
                        rel => Report(18, s.LogExtractCore, "  + " + rel));

                    // 2. mods/ directory
                    Report(24, s.LogCreateMods, s.LogCreateMods);
                    string modsDir = Path.Combine(manifest.GameDir, "mods");
                    Directory.CreateDirectory(modsDir);

                    // 3. Optional SampleMod
                    if (plan.InstallSampleMod)
                    {
                        Report(32, s.LogSampleMod, s.LogSampleMod);
                        ExtractPrefix(zip, "samplemod/", modsDir, manifest.InstalledFiles,
                            rel => Report(36, s.LogSampleMod, "  + mods\\" + rel));
                    }

                    // 4. Developer SDK + tools + plugins
                    if (plan.Mode == SetupMode.Developer && !string.IsNullOrWhiteSpace(manifest.SdkDir))
                    {
                        Directory.CreateDirectory(manifest.SdkDir);

                        Report(42, s.LogExtractSdk, s.LogExtractSdk);
                        ExtractPrefix(zip, "sdk/", manifest.SdkDir, manifest.InstalledFiles, null);
                        Report(64, s.LogExtractSdk, "  + SDK, AmodPack.exe, AzmPack.exe, SampleMod, AzForge.Mod.props");

                        if (plan.IncludeVsVsix)
                        {
                            Report(70, s.LogExtractVs, s.LogExtractVs);
                            ExtractPrefix(zip, "vs/", manifest.SdkDir, manifest.InstalledFiles,
                                rel => Report(74, s.LogExtractVs, "  + " + rel));
                        }

                        Report(78, s.LogExtractVsCode, s.LogExtractVsCode);
                        ExtractPrefix(zip, "vscode/", manifest.SdkDir, manifest.InstalledFiles,
                            rel => Report(80, s.LogExtractVsCode, "  + " + rel));
                    }

                    // 5. Registry for Visual Studio wizard & future installs
                    Report(84, s.LogRegistry, s.LogRegistry);
                    using (var key = Registry.CurrentUser.CreateSubKey(TemplatesRegKey))
                    {
                        key?.SetValue("GameDirectory", manifest.GameDir, RegistryValueKind.String);
                        if (!string.IsNullOrEmpty(manifest.SdkDir))
                            key?.SetValue("SdkDirectory", manifest.SdkDir, RegistryValueKind.String);
                    }

                    // 6. Optional PATH update
                    if (plan.Mode == SetupMode.Developer && plan.AddToPath && !string.IsNullOrEmpty(manifest.SdkDir))
                    {
                        Report(88, s.LogPath, s.LogPath);
                        string toolsDir = Path.Combine(manifest.SdkDir, "Release");
                        AddUserPathEntry(toolsDir);
                        manifest.AddedPathDir = toolsDir;
                    }

                    // 7. Optional VS Code extension installation
                    if (plan.Mode == SetupMode.Developer && plan.InstallVsCodeExt && !string.IsNullOrEmpty(manifest.SdkDir))
                    {
                        string codeCmd = FindVsCodeCmd();
                        string vsix = Directory.GetFiles(
                            Path.Combine(manifest.SdkDir, "plugins", "vscode"), "*.vsix")
                            .FirstOrDefault();

                        if (!string.IsNullOrEmpty(codeCmd) && !string.IsNullOrEmpty(vsix))
                        {
                            Report(92, s.LogInstallVsCode, s.LogInstallVsCode);
                            try
                            {
                                var psi = new ProcessStartInfo
                                {
                                    FileName = "cmd.exe",
                                    Arguments = $"/c \"\"{codeCmd}\" --install-extension \"{vsix}\" --force\"",
                                    UseShellExecute = false,
                                    CreateNoWindow = true,
                                    RedirectStandardOutput = true,
                                    RedirectStandardError = true
                                };
                                using (var p = Process.Start(psi))
                                {
                                    p?.WaitForExit(30000);
                                }
                            }
                            catch (Exception ex)
                            {
                                Report(93, s.LogInstallVsCode, "  ! VS Code: " + ex.Message);
                            }
                        }
                    }

                    // 8. Save manifest & register Windows uninstaller
                    Report(96, s.LogUninstaller, s.LogUninstaller);
                    SaveManifestAndRegisterUninstaller(manifest);

                    Report(100, s.ProgressDone, s.LogComplete);
                }
            });
        }

        public static Task UninstallAsync(InstalledManifest manifest, Strings s, IProgress<InstallProgress> progress)
        {
            return Task.Run(() =>
            {
                void Report(int pct, string status, string log = null)
                {
                    progress?.Report(new InstallProgress { Percent = pct, Status = status, LogLine = log });
                }

                Report(10, s.ProgressTitleUninstall, s.ProgressTitleUninstall);

                var filesToDelete = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (string f in manifest.InstalledFiles)
                {
                    filesToDelete.Add(f);
                }

                // Always make sure loader DLLs in game folder are removed
                if (!string.IsNullOrEmpty(manifest.GameDir))
                {
                    filesToDelete.Add(Path.Combine(manifest.GameDir, "d3d8.dll"));
                    filesToDelete.Add(Path.Combine(manifest.GameDir, "AzForge.dll"));
                }

                int total = Math.Max(1, filesToDelete.Count);
                int done = 0;

                foreach (string file in filesToDelete)
                {
                    done++;
                    if (File.Exists(file))
                    {
                        try
                        {
                            File.SetAttributes(file, FileAttributes.Normal);
                            File.Delete(file);
                            int pct = 10 + (int)(done * 65.0 / total);
                            if (file.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ||
                                file.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ||
                                file.EndsWith(".amod", StringComparison.OrdinalIgnoreCase) ||
                                file.EndsWith(".vsix", StringComparison.OrdinalIgnoreCase))
                            {
                                Report(pct, s.ProgressTitleUninstall, "  - " + file);
                            }
                        }
                        catch (Exception ex)
                        {
                            Report(10 + (int)(done * 65.0 / total), s.ProgressTitleUninstall, "  ! " + file + ": " + ex.Message);
                        }
                    }
                }

                // Remove empty subdirectories inside SdkDir (never touch GameDir or GameDir\mods)
                if (!string.IsNullOrEmpty(manifest.SdkDir) && Directory.Exists(manifest.SdkDir))
                {
                    Report(80, s.ProgressTitleUninstall, "  - " + manifest.SdkDir);
                    DeleteEmptyDirsRecursive(manifest.SdkDir);
                }

                // Clean PATH
                if (!string.IsNullOrEmpty(manifest.AddedPathDir))
                {
                    Report(88, s.ProgressTitleUninstall, "  - PATH: " + manifest.AddedPathDir);
                    RemoveUserPathEntry(manifest.AddedPathDir);
                }

                // Clean registry entries
                Report(94, s.ProgressTitleUninstall, "  - Registry cleanup");
                try { Registry.CurrentUser.DeleteSubKeyTree(UninstallRegKey, false); } catch { }
                try { Registry.CurrentUser.DeleteSubKeyTree(TemplatesRegKey, false); } catch { }

                // Delete manifest file
                try
                {
                    if (File.Exists(ManifestPath)) File.Delete(ManifestPath);
                }
                catch { }

                // If NOT running from UninstallExePath, remove the cached copy right away
                if (!IsRunningFromUninstallLocation())
                {
                    try
                    {
                        if (File.Exists(UninstallExePath)) File.Delete(UninstallExePath);
                        DeleteEmptyDirsRecursive(UninstallDir);
                    }
                    catch { }
                }

                Report(100, s.ProgressDone, s.UninstallDoneTitle);
            });
        }

        public static void ScheduleSelfDeleteIfUninstaller()
        {
            if (!IsRunningFromUninstallLocation()) return;

            try
            {
                string exe = UninstallExePath;
                string dir = UninstallDir;
                string parent = Path.GetDirectoryName(dir);
                var psi = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    Arguments = $"/c ping 127.0.0.1 -n 3 > nul & del /f /q \"{exe}\" & rmdir \"{dir}\" & rmdir \"{parent}\"",
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                Process.Start(psi);
            }
            catch { }
        }

        private static void ExtractPrefix(
            ZipArchive zip,
            string prefix,
            string targetRoot,
            List<string> trackedFiles,
            Action<string> onFile)
        {
            foreach (ZipArchiveEntry entry in zip.Entries)
            {
                if (!entry.FullName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    continue;
                if (string.IsNullOrEmpty(entry.Name))
                    continue;

                string relative = entry.FullName.Substring(prefix.Length).Replace('/', Path.DirectorySeparatorChar);
                string destPath = Path.Combine(targetRoot, relative);
                string destDir = Path.GetDirectoryName(destPath);
                if (!string.IsNullOrEmpty(destDir))
                    Directory.CreateDirectory(destDir);

                using (Stream src = entry.Open())
                using (FileStream dst = new FileStream(destPath, FileMode.Create, FileAccess.Write, FileShare.None))
                {
                    src.CopyTo(dst);
                }

                trackedFiles.Add(destPath);
                onFile?.Invoke(relative);
            }
        }

        private static void SaveManifestAndRegisterUninstaller(InstalledManifest manifest)
        {
            Directory.CreateDirectory(UninstallDir);

            string self = Assembly.GetExecutingAssembly().Location;
            if (!string.IsNullOrEmpty(self) &&
                !self.Equals(UninstallExePath, StringComparison.OrdinalIgnoreCase))
            {
                File.Copy(self, UninstallExePath, true);
            }

            var sb = new StringBuilder();
            sb.AppendLine("game=" + manifest.GameDir);
            if (!string.IsNullOrEmpty(manifest.SdkDir)) sb.AppendLine("sdk=" + manifest.SdkDir);
            if (!string.IsNullOrEmpty(manifest.AddedPathDir)) sb.AppendLine("path=" + manifest.AddedPathDir);
            sb.AppendLine("sample=" + (manifest.InstalledSampleMod ? "1" : "0"));
            foreach (string f in manifest.InstalledFiles.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                sb.AppendLine("file=" + f);
            }
            File.WriteAllText(ManifestPath, sb.ToString(), new UTF8Encoding(false));

            long totalBytes = 0;
            foreach (string f in manifest.InstalledFiles)
            {
                try { if (File.Exists(f)) totalBytes += new FileInfo(f).Length; } catch { }
            }

            using (var key = Registry.CurrentUser.CreateSubKey(UninstallRegKey))
            {
                if (key == null) return;
                key.SetValue("DisplayName", $"{AppName} {AppVersion}", RegistryValueKind.String);
                key.SetValue("DisplayVersion", AppVersion, RegistryValueKind.String);
                key.SetValue("Publisher", "BAZKAMAZ", RegistryValueKind.String);
                key.SetValue("URLInfoAbout", "https://bazkamaz.github.io/AzForge/", RegistryValueKind.String);
                key.SetValue("DisplayIcon", UninstallExePath, RegistryValueKind.String);
                key.SetValue("InstallLocation", manifest.GameDir, RegistryValueKind.String);
                key.SetValue("UninstallString", $"\"{UninstallExePath}\" --uninstall", RegistryValueKind.String);
                key.SetValue("EstimatedSize", (int)Math.Max(1, totalBytes / 1024), RegistryValueKind.DWord);
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
            }
        }

        private static void DeleteEmptyDirsRecursive(string dir)
        {
            if (!Directory.Exists(dir)) return;
            foreach (string sub in Directory.GetDirectories(dir))
            {
                DeleteEmptyDirsRecursive(sub);
            }
            try
            {
                if (!Directory.EnumerateFileSystemEntries(dir).Any())
                    Directory.Delete(dir);
            }
            catch { }
        }

        private static void AddUserPathEntry(string dirToAdd)
        {
            string normalized = dirToAdd.TrimEnd('\\');
            using (var env = Registry.CurrentUser.OpenSubKey("Environment", true))
            {
                if (env == null) return;
                string current = env.GetValue("Path", string.Empty, RegistryValueOptions.DoNotExpandEnvironmentNames) as string ?? string.Empty;
                var parts = current.Split(new[] { ';' }, StringSplitOptions.RemoveEmptyEntries)
                                   .Select(p => p.Trim())
                                   .ToList();
                if (parts.Any(p => p.TrimEnd('\\').Equals(normalized, StringComparison.OrdinalIgnoreCase)))
                    return;

                parts.Add(normalized);
                env.SetValue("Path", string.Join(";", parts), RegistryValueKind.ExpandString);
            }
            BroadcastEnvironmentChange();
        }

        private static void RemoveUserPathEntry(string dirToRemove)
        {
            string normalized = dirToRemove.TrimEnd('\\');
            using (var env = Registry.CurrentUser.OpenSubKey("Environment", true))
            {
                if (env == null) return;
                string current = env.GetValue("Path", string.Empty, RegistryValueOptions.DoNotExpandEnvironmentNames) as string ?? string.Empty;
                var parts = current.Split(new[] { ';' }, StringSplitOptions.RemoveEmptyEntries)
                                   .Select(p => p.Trim())
                                   .Where(p => !p.TrimEnd('\\').Equals(normalized, StringComparison.OrdinalIgnoreCase))
                                   .ToList();
                env.SetValue("Path", string.Join(";", parts), RegistryValueKind.ExpandString);
            }
            BroadcastEnvironmentChange();
        }

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        private static extern IntPtr SendMessageTimeout(
            IntPtr hWnd, uint msg, UIntPtr wParam, string lParam,
            uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);

        private static void BroadcastEnvironmentChange()
        {
            try
            {
                SendMessageTimeout(new IntPtr(0xFFFF), 0x001A, UIntPtr.Zero, "Environment", 0x0002, 2000, out _);
            }
            catch { }
        }
    }
}
