using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace AzForge.Templates
{
    /// <summary>
    /// What a new mod is asked for. The two folders are remembered between projects, so they are
    /// typed once and filled in on every mod after that.
    /// </summary>
    internal sealed class ModSettingsDialog : Form
    {
        private readonly TextBox _id = new TextBox();
        private readonly TextBox _name = new TextBox();
        private readonly TextBox _description = new TextBox();
        private readonly TextBox _author = new TextBox();
        private readonly TextBox _game = new TextBox();
        private readonly TextBox _sdk = new TextBox();

        public string ModId => _id.Text.Trim();
        public string ModName => _name.Text.Trim();
        public string ModDescription => _description.Text.Trim();
        public string ModAuthor => _author.Text.Trim();
        public string GameDirectory => _game.Text.Trim().TrimEnd('\\');
        public string SdkDirectory => _sdk.Text.Trim().TrimEnd('\\');

        public ModSettingsDialog(string projectName, string game, string sdk)
        {
            Text = "New AzForge mod";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MinimizeBox = false;
            MaximizeBox = false;
            ClientSize = new Size(560, 300);
            Font = SystemFonts.MessageBoxFont;

            _name.Text = projectName;
            _id.Text = "azforge." + projectName.ToLowerInvariant();
            _author.Text = Environment.UserName;
            _game.Text = game;
            _sdk.Text = sdk;

            var top = 16;
            Row("Name", _name, ref top, "Shown on the MODS screen.");
            Row("Id", _id, ref top, "Names the mod everywhere. Two mods sharing one are the same mod.");
            Row("Description", _description, ref top, "One line, shown beside the name.");
            Row("Author", _author, ref top, null);
            Row("Game folder", _game, ref top, "Holds game.exe. The mod is installed here and F5 runs it.", browseFolder: true);
            Row("AzForge folder", _sdk, ref top, "Holds sdk\\include and plugins\\vs2026.", browseFolder: true);

            var create = new Button { Text = "Create", DialogResult = DialogResult.OK, Size = new Size(90, 26) };
            var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Size = new Size(90, 26) };
            create.Location = new Point(ClientSize.Width - 200, ClientSize.Height - 40);
            cancel.Location = new Point(ClientSize.Width - 100, ClientSize.Height - 40);

            // Everything here ends up in the .vcxproj, and a folder left empty leaves an import
            // pointing at the root of the drive. Better to say so now than to hand back a project
            // that cannot build.
            create.Click += (s, e) =>
            {
                var complaint = Complaint();

                if (complaint == null)
                {
                    return;
                }

                // Empty means it has already been said, and answered
                if (complaint.Length > 0)
                {
                    MessageBox.Show(this, complaint, "AzForge", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                DialogResult = DialogResult.None;
            };

            Controls.Add(create);
            Controls.Add(cancel);
            AcceptButton = create;
            CancelButton = cancel;
        }

        /// <summary>What is wrong with what has been typed, or null when nothing is.</summary>
        private string Complaint()
        {
            if (ModName.Length == 0 || ModId.Length == 0)
            {
                return "A mod needs a name and an id.";
            }

            if (SdkDirectory.Length == 0)
            {
                return "Which folder is AzForge in? The project imports its build settings from "
                     + "there, and without it nothing will compile.";
            }

            if (ModWizard.FindProps(SdkDirectory).Length == 0)
            {
                return "That is not an AzForge folder: it has no plugins\\*\\AzForge.Mod.props.\r\n\r\n"
                     + "Pick the folder holding sdk\\ and plugins\\.";
            }

            if (!File.Exists(Path.Combine(SdkDirectory, @"sdk\include\ForgeApi.h")))
            {
                return "That AzForge folder has no sdk\\include\\ForgeApi.h, so a mod could not "
                     + "include the interface.";
            }

            // The game is only wanted to install into and to run under the debugger, so this is
            // worth asking about rather than refusing over.
            if (GameDirectory.Length == 0 || !File.Exists(Path.Combine(GameDirectory, "game.exe")))
            {
                var answer = MessageBox.Show(this,
                    "There is no game.exe in the game folder. The mod will build and pack, but it "
                    + "will not be installed anywhere and F5 will have nothing to run.\r\n\r\n"
                    + "Create it anyway?",
                    "AzForge", MessageBoxButtons.YesNo, MessageBoxIcon.Question);

                return answer == DialogResult.Yes ? null : string.Empty;
            }

            return null;
        }

        private void Row(string label, TextBox box, ref int top, string hint, bool browseFolder = false)
        {
            Controls.Add(new Label
            {
                Text = label,
                Location = new Point(16, top + 3),
                Size = new Size(104, 20),
                TextAlign = ContentAlignment.MiddleLeft
            });

            box.Location = new Point(126, top);
            box.Size = new Size(browseFolder ? 348 : 384, 22);
            Controls.Add(box);

            if (browseFolder)
            {
                var pick = new Button { Text = "...", Location = new Point(482, top - 1), Size = new Size(28, 24) };
                pick.Click += (s, e) =>
                {
                    using (var dialog = new FolderBrowserDialog())
                    {
                        if (box.Text.Length > 0)
                        {
                            dialog.SelectedPath = box.Text;
                        }
                        if (dialog.ShowDialog(this) == DialogResult.OK)
                        {
                            box.Text = dialog.SelectedPath;
                        }
                    }
                };
                Controls.Add(pick);
            }

            top += 26;

            if (hint != null)
            {
                Controls.Add(new Label
                {
                    Text = hint,
                    Location = new Point(128, top),
                    Size = new Size(400, 15),
                    ForeColor = SystemColors.GrayText
                });
                top += 17;
            }
        }
    }
}
