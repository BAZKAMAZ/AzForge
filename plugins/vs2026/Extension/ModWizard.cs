using System;
using System.Collections.Generic;
using System.IO;
using System.Windows.Forms;
using EnvDTE;
using Microsoft.VisualStudio.TemplateWizard;
using Microsoft.Win32;

namespace AzForge.Templates
{
    /// <summary>
    /// Asks what the mod is called and where the game is, then hands the answers to the template
    /// as replacements. Everything it collects also lives on the project's Mod Settings page, so
    /// nothing asked here is asked only once.
    /// </summary>
    public sealed class ModWizard : IWizard
    {
        private const string SettingsKey = @"Software\AzForge\Templates";

        public void RunStarted(object automationObject,
                               Dictionary<string, string> replacements,
                               WizardRunKind runKind,
                               object[] customParams)
        {
            var projectName = replacements.TryGetValue("$safeprojectname$", out var safe) ? safe : "MyMod";

            using (var dialog = new ModSettingsDialog(projectName, Read("GameDirectory"), Read("SdkDirectory")))
            {
                if (dialog.ShowDialog() != DialogResult.OK)
                {
                    // Stops the project being created rather than leaving a half-filled one behind
                    throw new WizardBackoutException();
                }

                replacements["$modid$"] = dialog.ModId;
                replacements["$modname$"] = Escape(dialog.ModName);
                replacements["$moddescription$"] = Escape(dialog.ModDescription);
                replacements["$modauthor$"] = Escape(dialog.ModAuthor);
                replacements["$gamedir$"] = dialog.GameDirectory;
                replacements["$azforgedir$"] = dialog.SdkDirectory;

                // AzForge.Mod.props is plain MSBuild and is not tied to Visual Studio, so the
                // template is given the path rather than spelling plugins\vs2026 out itself
                replacements["$propspath$"] = FindProps(dialog.SdkDirectory);

                // The two folders are the same for every mod on this machine, so they are offered
                // back the next time instead of being typed again
                Write("GameDirectory", dialog.GameDirectory);
                Write("SdkDirectory", dialog.SdkDirectory);
            }
        }

        /// <summary>
        /// AzForge.Mod.props under the given AzForge folder, or empty when there is none. It is
        /// looked for rather than named so that moving it out of plugins\vs2026 breaks nothing.
        /// </summary>
        internal static string FindProps(string root)
        {
            var plugins = Path.Combine(root ?? string.Empty, "plugins");

            if (!Directory.Exists(plugins))
            {
                return string.Empty;
            }

            foreach (var folder in Directory.GetDirectories(plugins))
            {
                var candidate = Path.Combine(folder, "AzForge.Mod.props");

                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }

            return string.Empty;
        }

        /// <summary>An ampersand or a bracket in a name would otherwise break the .vcxproj.</summary>
        private static string Escape(string text)
        {
            return (text ?? string.Empty)
                .Replace("&", "&amp;")
                .Replace("<", "&lt;")
                .Replace(">", "&gt;");
        }

        private static string Read(string name)
        {
            using (var key = Registry.CurrentUser.OpenSubKey(SettingsKey))
            {
                return key?.GetValue(name) as string ?? string.Empty;
            }
        }

        private static void Write(string name, string value)
        {
            if (string.IsNullOrEmpty(value))
            {
                return;
            }

            using (var key = Registry.CurrentUser.CreateSubKey(SettingsKey))
            {
                key?.SetValue(name, value);
            }
        }

        public void ProjectFinishedGenerating(Project project) { }
        public void ProjectItemFinishedGenerating(ProjectItem projectItem) { }
        public void BeforeOpeningFile(ProjectItem projectItem) { }
        public void RunFinished() { }

        public bool ShouldAddProjectItem(string filePath) => true;
    }
}
