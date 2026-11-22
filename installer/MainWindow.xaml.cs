using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace AzForgeSetup
{
    public partial class MainWindow : Window
    {
        private enum PageId
        {
            Mode,
            Config,
            Progress,
            Done,
            Uninstall
        }

        private readonly StartupArgs _startup;
        private Strings _s;
        private PageId _currentPage = PageId.Mode;
        private SetupMode _selectedMode = SetupMode.Player;
        private InstalledManifest _existingManifest;
        private InstallPlan _lastPlan;
        private bool _isBusy;
        private bool _wasUninstallDone;
        private bool _vsCodeAvailable;
        private TaskCompletionSource<bool> _modalTcs;

        internal MainWindow(StartupArgs startup)
        {
            InitializeComponent();
            _startup = startup ?? new StartupArgs();

            UiLang lang = _startup.Lang ?? UiLang.En;
            _s = new Strings(lang);
            TxtVersion.Text = "v" + InstallerEngine.AppVersion;

            _existingManifest = InstallerEngine.LoadExistingManifest();
            _vsCodeAvailable = InstallerEngine.FindVsCodeCmd() != null;

            // Pre-fill paths & options
            InputGameDir.Text = !string.IsNullOrWhiteSpace(_startup.GameDir)
                ? _startup.GameDir
                : InstallerEngine.DetectGameDirectory();

            InputSdkDir.Text = !string.IsNullOrWhiteSpace(_startup.SdkDir)
                ? _startup.SdkDir
                : InstallerEngine.DetectSdkDirectory();

            if (_startup.Mode.HasValue)
                _selectedMode = _startup.Mode.Value;

            ChkPlayerSample.IsChecked = _startup.InstallSampleMod ?? false;
            ChkDevSample.IsChecked = _startup.InstallSampleMod ?? true;
            ChkAddPath.IsChecked = _startup.AddToPath ?? true;
            ChkVsCode.IsChecked = _startup.InstallVsCodeExt ?? _vsCodeAvailable;
            ChkVsCode.IsEnabled = _vsCodeAvailable;
            ChkVs.IsChecked = _startup.IncludeVsVsix ?? true;

            AttachSafePaste(InputGameDir);
            AttachSafePaste(InputSdkDir);

            ApplyLocalization();
            UpdateModeCardVisuals();
            UpdateGameDirStatus();

            if (_startup.Uninstall)
            {
                PopulateUninstallSummary();
                ShowPage(PageId.Uninstall, animate: false);
            }
            else
            {
                ShowPage(PageId.Mode, animate: false);
            }

            Loaded += MainWindow_Loaded;
        }

        private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            if (_startup.AutoInstall)
            {
                await RunInstallAsync(BuildCurrentPlan());
            }
            else if (_startup.AutoUninstall && _existingManifest != null)
            {
                await RunUninstallAsync(_existingManifest);
            }
        }

        private void ApplyLocalization()
        {
            Title = _s.WindowTitle;

            // Lang switch styling
            var accentTint = (Brush)FindResource("AccentTintBrush");
            var accentBrush = (Brush)FindResource("AccentBrush");
            var dimBrush = (Brush)FindResource("TextDimBrush");

            BtnLangRu.Background = _s.Lang == UiLang.Ru ? accentTint : Brushes.Transparent;
            BtnLangRu.Foreground = _s.Lang == UiLang.Ru ? accentBrush : dimBrush;
            BtnLangEn.Background = _s.Lang == UiLang.En ? accentTint : Brushes.Transparent;
            BtnLangEn.Foreground = _s.Lang == UiLang.En ? accentBrush : dimBrush;

            // Stepper
            Step1Label.Text = _s.Step1;
            Step2Label.Text = _s.Step2;
            Step3Label.Text = _s.Step3;
            Step4Label.Text = _s.Step4;

            // Page 1: Mode
            TxtModeKicker.Text = _s.ModeKicker;
            TxtModeTitle.Text = _s.ModeTitle;
            TxtModeSubtitle.Text = _s.ModeSubtitle;
            TxtCardPlayerTitle.Text = _s.CardPlayerTitle;
            TxtPlayerBadge.Text = _s.CardPlayerBadge;
            TxtCardPlayerDesc.Text = _s.CardPlayerDesc;
            TxtCardDevTitle.Text = _s.CardDevTitle;
            TxtDevBadge.Text = _s.CardDevBadge;
            TxtCardDevDesc.Text = _s.CardDevDesc;
            TxtFooterNote.Text = _s.FooterNote;
            BtnUninstallLink.Content = _s.BtnUninstallLink;

            // Page 2: Config
            TxtConfigTitle.Text = _selectedMode == SetupMode.Player ? _s.ConfigTitlePlayer : _s.ConfigTitleDev;
            TxtConfigSub.Text = _selectedMode == SetupMode.Player ? _s.ConfigSubPlayer : _s.ConfigSubDev;
            TxtLabelGameDir.Text = _s.LabelGameDir;
            TxtLabelSdkDir.Text = _s.LabelSdkDir;
            BtnBrowseGame.Content = _s.BtnBrowse;
            BtnBrowseSdk.Content = _s.BtnBrowse;
            TxtPlayerInfoTitle.Text = _s.PlayerInfoTitle;
            TxtPlayerInfoBody.Text = _s.PlayerInfoBody;

            TxtPlayerSampleTitle.Text = _s.OptSampleMod;
            TxtPlayerSampleHint.Text = _s.OptSampleModHint;
            TxtDevSampleTitle.Text = _s.OptSampleMod;
            TxtDevSampleHint.Text = _s.OptSampleModHint;
            TxtAddPathTitle.Text = _s.OptAddPath;
            TxtAddPathHint.Text = _s.OptAddPathHint;
            TxtVsCodeTitle.Text = _s.OptVsCode;
            TxtVsCodeHint.Text = _vsCodeAvailable ? _s.OptVsCodeHintFound : _s.OptVsCodeHintMissing;
            TxtVsTitle.Text = _s.OptVs;
            TxtVsHint.Text = _s.OptVsHint;

            // Page 4: Done
            TxtDoneTitle.Text = _wasUninstallDone ? _s.UninstallDoneTitle : _s.DoneTitle;
            TxtDoneSubtitle.Text = _wasUninstallDone ? _s.UninstallDoneSubtitle : _s.DoneSubtitle;
            TxtDoneGameLabel.Text = _s.DoneGameLabel;
            TxtDoneSdkLabel.Text = _s.DoneSdkLabel;
            BtnOpenGameDir.Content = _s.DoneOpenHint;
            BtnOpenSdkDir.Content = _s.DoneOpenHint;
            BtnLaunchGame.Content = _s.BtnLaunchGame;
            BtnOpenDocs.Content = _s.BtnOpenDocs;
            BtnInstallVsix.Content = _s.BtnInstallVsix;

            // Page 5: Uninstall
            TxtUninstTitle.Text = _s.UninstallTitle;
            TxtUninstSub.Text = _s.UninstallSubtitle;
            TxtUninstWillRemove.Text = _s.UninstallWillRemove;
            TxtUninstNotice.Text = _s.UninstallNotice;
            BtnDoUninstall.Content = _s.BtnUninstallNow;

            // Footer buttons
            BtnBack.Content = "←  " + _s.BtnBack;
            BtnFinish.Content = _s.BtnClose;
            UpdateNextButtonCaption();
            UpdateGameDirStatus();
        }

        private void UpdateNextButtonCaption()
        {
            if (_currentPage == PageId.Mode)
            {
                BtnNext.Content = _s.BtnContinue + "  →";
            }
            else if (_currentPage == PageId.Config)
            {
                string gameDir = CleanPath(InputGameDir.Text);
                string sdkDir = CleanPath(InputSdkDir.Text);
                bool needAdmin = !InstallerEngine.IsAdministrator() &&
                    ((IsValidFolderPath(gameDir) && !InstallerEngine.CanWriteDirectory(gameDir)) ||
                     (_selectedMode == SetupMode.Developer && IsValidFolderPath(sdkDir) && !InstallerEngine.CanWriteDirectory(sdkDir)));
                BtnNext.Content = needAdmin ? _s.BtnInstallAdmin : _s.BtnInstall;
            }
        }

        private void UpdateModeCardVisuals()
        {
            var accent = (Brush)FindResource("AccentBrush");
            var borderDark = (Brush)FindResource("BorderBrushDark");
            var borderStrong = (Brush)FindResource("BorderStrongBrush");
            var panel = (Brush)FindResource("BgPanelBrush");
            var selectedBg = new SolidColorBrush(Color.FromRgb(0x1B, 0x20, 0x2E));

            bool isPlayer = _selectedMode == SetupMode.Player;

            CardPlayer.BorderBrush = isPlayer ? accent : borderDark;
            CardPlayer.Background = isPlayer ? selectedBg : panel;
            CheckPlayer.Background = isPlayer ? accent : Brushes.Transparent;
            CheckPlayer.BorderBrush = isPlayer ? accent : borderStrong;
            CheckPlayerMark.Visibility = isPlayer ? Visibility.Visible : Visibility.Collapsed;

            CardDev.BorderBrush = !isPlayer ? accent : borderDark;
            CardDev.Background = !isPlayer ? selectedBg : panel;
            CheckDev.Background = !isPlayer ? accent : Brushes.Transparent;
            CheckDev.BorderBrush = !isPlayer ? accent : borderStrong;
            CheckDevMark.Visibility = !isPlayer ? Visibility.Visible : Visibility.Collapsed;

            PanelPlayerConfig.Visibility = isPlayer ? Visibility.Visible : Visibility.Collapsed;
            PanelDevConfig.Visibility = !isPlayer ? Visibility.Visible : Visibility.Collapsed;
            TxtConfigTitle.Text = isPlayer ? _s.ConfigTitlePlayer : _s.ConfigTitleDev;
            TxtConfigSub.Text = isPlayer ? _s.ConfigSubPlayer : _s.ConfigSubDev;
        }

        private const uint CF_UNICODETEXT = 13;

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool OpenClipboard(IntPtr hWndNewOwner);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool CloseClipboard();

        [DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr GetClipboardData(uint uFormat);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GlobalLock(IntPtr hMem);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GlobalUnlock(IntPtr hMem);

        private static string SanitizePastedText(string raw)
        {
            if (string.IsNullOrEmpty(raw)) return string.Empty;
            string s = raw.Replace("\r", string.Empty).Replace("\n", string.Empty).Trim();
            if (s.Length >= 2 && s[0] == '"' && s[s.Length - 1] == '"')
                s = s.Substring(1, s.Length - 2).Trim();
            return s;
        }

        private static string CleanPath(string raw)
        {
            string s = SanitizePastedText(raw);
            while (s.Length > 3 && s[s.Length - 1] == '\\')
                s = s.Substring(0, s.Length - 1);
            return s;
        }

        private static bool IsValidFolderPath(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) return false;
            if (dir.IndexOfAny(Path.GetInvalidPathChars()) >= 0) return false;
            if (dir.IndexOfAny(new[] { '*', '?' }) >= 0) return false;
            try
            {
                string full = Path.GetFullPath(dir);
                return !string.IsNullOrWhiteSpace(full);
            }
            catch
            {
                return false;
            }
        }

        private static bool SafeFileExistsIn(string dir, string fileName)
        {
            if (!IsValidFolderPath(dir)) return false;
            try
            {
                return File.Exists(Path.Combine(dir, fileName));
            }
            catch
            {
                return false;
            }
        }

        private static string TryReadClipboardTextWin32()
        {
            for (int attempt = 0; attempt < 6; attempt++)
            {
                if (OpenClipboard(IntPtr.Zero))
                {
                    try
                    {
                        IntPtr handle = GetClipboardData(CF_UNICODETEXT);
                        if (handle == IntPtr.Zero) return null;
                        IntPtr ptr = GlobalLock(handle);
                        if (ptr == IntPtr.Zero) return null;
                        try
                        {
                            return Marshal.PtrToStringUni(ptr);
                        }
                        finally
                        {
                            GlobalUnlock(handle);
                        }
                    }
                    catch
                    {
                        return null;
                    }
                    finally
                    {
                        CloseClipboard();
                    }
                }
                System.Threading.Thread.Sleep(20);
            }
            return null;
        }

        private static string TryReadClipboardText()
        {
            string text = TryReadClipboardTextWin32();
            if (text != null) return text;

            for (int attempt = 0; attempt < 4; attempt++)
            {
                try
                {
                    if (Clipboard.ContainsText())
                        return Clipboard.GetText();
                    return null;
                }
                catch
                {
                    System.Threading.Thread.Sleep(20);
                }
            }
            return null;
        }

        private static void InsertTextAtSelection(TextBox tb, string text)
        {
            string current = tb.Text ?? string.Empty;
            int start = Math.Max(0, Math.Min(tb.SelectionStart, current.Length));
            int len = Math.Max(0, Math.Min(tb.SelectionLength, current.Length - start));
            tb.Text = current.Substring(0, start) + text + current.Substring(start + len);
            tb.CaretIndex = start + text.Length;
        }

        private void AttachSafePaste(TextBox tb)
        {
            CommandManager.AddPreviewExecutedHandler(tb, (sender, e) =>
            {
                if (e.Command != ApplicationCommands.Paste) return;
                e.Handled = true;

                string clip = TryReadClipboardText();
                if (clip == null) return;

                InsertTextAtSelection(tb, SanitizePastedText(clip));
            });

            DataObject.AddPastingHandler(tb, (sender, e) =>
            {
                try
                {
                    if (e.DataObject != null && e.DataObject.GetDataPresent(DataFormats.UnicodeText))
                    {
                        string clip = e.DataObject.GetData(DataFormats.UnicodeText) as string;
                        if (clip != null)
                        {
                            e.CancelCommand();
                            InsertTextAtSelection(tb, SanitizePastedText(clip));
                        }
                    }
                }
                catch
                {
                    e.CancelCommand();
                }
            });
        }

        private void UpdateGameDirStatus()
        {
            string dir = CleanPath(InputGameDir.Text);
            bool exists = SafeFileExistsIn(dir, "game.exe");

            if (exists)
            {
                BadgeGameStatus.Background = new SolidColorBrush(Color.FromArgb(0x1A, 0x4A, 0xDE, 0x80));
                BadgeGameStatus.BorderBrush = new SolidColorBrush(Color.FromArgb(0x44, 0x4A, 0xDE, 0x80));
                DotGameStatus.Fill = (Brush)FindResource("OkBrush");
                TxtGameStatus.Foreground = (Brush)FindResource("OkBrush");
                TxtGameStatus.Text = _s.GameFound;
            }
            else
            {
                BadgeGameStatus.Background = new SolidColorBrush(Color.FromArgb(0x1A, 0xFB, 0xBF, 0x24));
                BadgeGameStatus.BorderBrush = new SolidColorBrush(Color.FromArgb(0x44, 0xFB, 0xBF, 0x24));
                DotGameStatus.Fill = (Brush)FindResource("WarnBrush");
                TxtGameStatus.Foreground = (Brush)FindResource("WarnBrush");
                TxtGameStatus.Text = _s.GameMissing;
            }

            if (_currentPage == PageId.Config)
                UpdateNextButtonCaption();
        }

        private void ShowPage(PageId page, bool animate = true)
        {
            _currentPage = page;

            PageMode.Visibility = page == PageId.Mode ? Visibility.Visible : Visibility.Collapsed;
            PageConfig.Visibility = page == PageId.Config ? Visibility.Visible : Visibility.Collapsed;
            PageProgress.Visibility = page == PageId.Progress ? Visibility.Visible : Visibility.Collapsed;
            PageDone.Visibility = page == PageId.Done ? Visibility.Visible : Visibility.Collapsed;
            PageUninstall.Visibility = page == PageId.Uninstall ? Visibility.Visible : Visibility.Collapsed;

            StepperPanel.Visibility = page == PageId.Uninstall ? Visibility.Collapsed : Visibility.Visible;
            UpdateStepper(page);

            // Footer buttons visibility
            BtnBack.Visibility = (page == PageId.Config || (page == PageId.Uninstall && !_startup.Uninstall))
                ? Visibility.Visible : Visibility.Collapsed;
            BtnUninstallLink.Visibility = (page == PageId.Mode && _existingManifest != null)
                ? Visibility.Visible : Visibility.Collapsed;
            TxtFooterNote.Visibility = (page == PageId.Mode && _existingManifest == null)
                ? Visibility.Visible : Visibility.Collapsed;

            BtnNext.Visibility = (page == PageId.Mode || page == PageId.Config)
                ? Visibility.Visible : Visibility.Collapsed;
            BtnDoUninstall.Visibility = (page == PageId.Uninstall && _existingManifest != null)
                ? Visibility.Visible : Visibility.Collapsed;
            BtnFinish.Visibility = (page == PageId.Done || (page == PageId.Uninstall && _existingManifest == null))
                ? Visibility.Visible : Visibility.Collapsed;

            UpdateNextButtonCaption();

            if (animate)
            {
                var fade = new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(180))
                {
                    EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
                };
                var slide = new DoubleAnimation(10, 0, TimeSpan.FromMilliseconds(180))
                {
                    EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
                };
                PageHost.BeginAnimation(OpacityProperty, fade);
                PageSlide.BeginAnimation(TranslateTransform.YProperty, slide);
            }
        }

        private void UpdateStepper(PageId page)
        {
            int idx = page switch
            {
                PageId.Mode => 1,
                PageId.Config => 2,
                PageId.Progress => 3,
                PageId.Done => 4,
                _ => 1
            };

            SetStepStyle(Step1Pill, Step1Num, Step1Label, 1, idx);
            SetStepStyle(Step2Pill, Step2Num, Step2Label, 2, idx);
            SetStepStyle(Step3Pill, Step3Num, Step3Label, 3, idx);
            SetStepStyle(Step4Pill, Step4Num, Step4Label, 4, idx);
        }

        private void SetStepStyle(Border pill, TextBlock num, TextBlock label, int step, int active)
        {
            var accentTint = (Brush)FindResource("AccentTintBrush");
            var accent = (Brush)FindResource("AccentBrush");
            var ok = (Brush)FindResource("OkBrush");
            var faint = (Brush)FindResource("TextFaintBrush");
            var text = (Brush)FindResource("TextBrush");

            if (step == active)
            {
                pill.Background = accentTint;
                num.Foreground = accent;
                label.Foreground = text;
            }
            else if (step < active)
            {
                pill.Background = Brushes.Transparent;
                num.Foreground = ok;
                label.Foreground = (Brush)FindResource("TextDimBrush");
            }
            else
            {
                pill.Background = Brushes.Transparent;
                num.Foreground = faint;
                label.Foreground = faint;
            }
        }

        private InstallPlan BuildCurrentPlan()
        {
            return new InstallPlan
            {
                Mode = _selectedMode,
                GameDir = CleanPath(InputGameDir.Text),
                SdkDir = CleanPath(InputSdkDir.Text),
                InstallSampleMod = _selectedMode == SetupMode.Player
                    ? (ChkPlayerSample.IsChecked == true)
                    : (ChkDevSample.IsChecked == true),
                AddToPath = _selectedMode == SetupMode.Developer && (ChkAddPath.IsChecked == true),
                InstallVsCodeExt = _selectedMode == SetupMode.Developer && _vsCodeAvailable && (ChkVsCode.IsChecked == true),
                IncludeVsVsix = _selectedMode == SetupMode.Developer && (ChkVs.IsChecked == true)
            };
        }

        private void PopulateUninstallSummary()
        {
            if (_existingManifest == null)
            {
                TxtUninstSummary.Text = _s.UninstallNothingFound;
                return;
            }

            var sb = new StringBuilder();
            if (!string.IsNullOrEmpty(_existingManifest.GameDir))
            {
                sb.AppendLine("• " + Path.Combine(_existingManifest.GameDir, "d3d8.dll"));
                sb.AppendLine("• " + Path.Combine(_existingManifest.GameDir, "AzForge.dll"));
                if (_existingManifest.InstalledSampleMod)
                    sb.AppendLine("• " + Path.Combine(_existingManifest.GameDir, "mods", "SampleMod.amod"));
            }
            if (!string.IsNullOrEmpty(_existingManifest.SdkDir))
            {
                sb.AppendLine("• " + _existingManifest.SdkDir + "  (SDK & tools)");
            }
            if (!string.IsNullOrEmpty(_existingManifest.AddedPathDir))
            {
                sb.AppendLine("• PATH: " + _existingManifest.AddedPathDir);
            }
            TxtUninstSummary.Text = sb.ToString().TrimEnd();
        }

        // ==================== CUSTOM MODAL ====================
        private Task<bool> ShowModalAsync(string title, string message, bool showCancel = true, string okText = null, string cancelText = null)
        {
            ModalTitle.Text = title;
            ModalMessage.Text = message;
            ModalBtnOk.Content = okText ?? (_s.Lang == UiLang.Ru ? "Да" : "Yes");
            ModalBtnCancel.Content = cancelText ?? (_s.Lang == UiLang.Ru ? "Отмена" : "Cancel");
            ModalBtnCancel.Visibility = showCancel ? Visibility.Visible : Visibility.Collapsed;
            ModalOverlay.Visibility = Visibility.Visible;

            _modalTcs = new TaskCompletionSource<bool>();
            return _modalTcs.Task;
        }

        private void ModalBtnOk_Click(object sender, RoutedEventArgs e)
        {
            ModalOverlay.Visibility = Visibility.Collapsed;
            _modalTcs?.TrySetResult(true);
        }

        private void ModalBtnCancel_Click(object sender, RoutedEventArgs e)
        {
            ModalOverlay.Visibility = Visibility.Collapsed;
            _modalTcs?.TrySetResult(false);
        }

        // ==================== WINDOW & NAV HANDLERS ====================
        private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ButtonState == MouseButtonState.Pressed)
                DragMove();
        }

        private void BtnMinimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

        private void BtnClose_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy) return;
            if (_wasUninstallDone)
                InstallerEngine.ScheduleSelfDeleteIfUninstaller();
            Close();
        }

        private void BtnLangRu_Click(object sender, RoutedEventArgs e)
        {
            _s = new Strings(UiLang.Ru);
            ApplyLocalization();
        }

        private void BtnLangEn_Click(object sender, RoutedEventArgs e)
        {
            _s = new Strings(UiLang.En);
            ApplyLocalization();
        }

        private void CardPlayer_Click(object sender, MouseButtonEventArgs e)
        {
            _selectedMode = SetupMode.Player;
            UpdateModeCardVisuals();
            if (e.ClickCount == 2) ShowPage(PageId.Config);
        }

        private void CardDev_Click(object sender, MouseButtonEventArgs e)
        {
            _selectedMode = SetupMode.Developer;
            UpdateModeCardVisuals();
            if (e.ClickCount == 2) ShowPage(PageId.Config);
        }

        private void ModeCard_MouseEnter(object sender, MouseEventArgs e)
        {
            if (sender is Border b)
            {
                bool selected = (b == CardPlayer && _selectedMode == SetupMode.Player) ||
                                (b == CardDev && _selectedMode == SetupMode.Developer);
                if (!selected)
                    b.BorderBrush = (Brush)FindResource("AccentBorderBrush");
            }
        }

        private void ModeCard_MouseLeave(object sender, MouseEventArgs e)
        {
            UpdateModeCardVisuals();
        }

        private void InputGameDir_TextChanged(object sender, TextChangedEventArgs e)
        {
            UpdateGameDirStatus();
        }

        private void InputSdkDir_TextChanged(object sender, TextChangedEventArgs e)
        {
            if (_currentPage == PageId.Config)
                UpdateNextButtonCaption();
        }

        private void BtnBrowseGame_Click(object sender, RoutedEventArgs e)
        {
            string picked = FolderPicker.PickFolder(this, _s.PickerGameTitle, CleanPath(InputGameDir.Text));
            if (!string.IsNullOrWhiteSpace(picked))
                InputGameDir.Text = CleanPath(picked);
        }

        private void BtnBrowseSdk_Click(object sender, RoutedEventArgs e)
        {
            string picked = FolderPicker.PickFolder(this, _s.PickerSdkTitle, CleanPath(InputSdkDir.Text));
            if (!string.IsNullOrWhiteSpace(picked))
                InputSdkDir.Text = CleanPath(picked);
        }

        private void BtnBack_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy) return;
            ShowPage(PageId.Mode);
        }

        private void BtnUninstallLink_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy) return;
            _existingManifest = InstallerEngine.LoadExistingManifest();
            PopulateUninstallSummary();
            ShowPage(PageId.Uninstall);
        }

        private async void BtnNext_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy) return;

            if (_currentPage == PageId.Mode)
            {
                ShowPage(PageId.Config);
                return;
            }

            if (_currentPage == PageId.Config)
            {
                var plan = BuildCurrentPlan();

                if (string.IsNullOrWhiteSpace(plan.GameDir) ||
                    (plan.Mode == SetupMode.Developer && string.IsNullOrWhiteSpace(plan.SdkDir)))
                {
                    await ShowModalAsync(_s.WindowTitle, _s.ErrEmptyPath, showCancel: false, okText: "OK");
                    return;
                }

                if (!IsValidFolderPath(plan.GameDir) ||
                    (plan.Mode == SetupMode.Developer && !IsValidFolderPath(plan.SdkDir)))
                {
                    await ShowModalAsync(_s.WindowTitle, _s.ErrInvalidPath, showCancel: false, okText: "OK");
                    return;
                }

                if (!SafeFileExistsIn(plan.GameDir, "game.exe"))
                {
                    bool go = await ShowModalAsync(
                        _s.ConfirmMissingGameTitle,
                        _s.ConfirmMissingGameText(plan.GameDir));
                    if (!go) return;
                }

                if (InstallerEngine.IsGameRunning(plan.GameDir))
                {
                    await ShowModalAsync(_s.ErrGameRunningTitle, _s.ErrGameRunningText, showCancel: false, okText: "OK");
                    return;
                }

                bool needAdmin = !InstallerEngine.IsAdministrator() &&
                    (!InstallerEngine.CanWriteDirectory(plan.GameDir) ||
                     (plan.Mode == SetupMode.Developer && !InstallerEngine.CanWriteDirectory(plan.SdkDir)));

                if (needAdmin)
                {
                    bool elevate = await ShowModalAsync(_s.AdminNeededTitle, _s.AdminNeededText);
                    if (!elevate) return;

                    if (InstallerEngine.RestartElevated(InstallerEngine.BuildResumeInstallArgs(plan, _s.Lang)))
                    {
                        Close();
                    }
                    return;
                }

                await RunInstallAsync(plan);
            }
        }

        private async void BtnDoUninstall_Click(object sender, RoutedEventArgs e)
        {
            if (_isBusy || _existingManifest == null) return;

            if (!string.IsNullOrEmpty(_existingManifest.GameDir) &&
                InstallerEngine.IsGameRunning(_existingManifest.GameDir))
            {
                await ShowModalAsync(_s.ErrGameRunningTitle, _s.ErrGameRunningText, showCancel: false, okText: "OK");
                return;
            }

            bool needAdmin = !InstallerEngine.IsAdministrator() &&
                (!string.IsNullOrEmpty(_existingManifest.GameDir) && !InstallerEngine.CanWriteDirectory(_existingManifest.GameDir));

            if (needAdmin)
            {
                bool elevate = await ShowModalAsync(_s.AdminNeededTitle, _s.AdminNeededText);
                if (!elevate) return;

                string langArg = _s.Lang == UiLang.Ru ? "ru" : "en";
                if (InstallerEngine.RestartElevated($"--auto-uninstall --lang {langArg}"))
                {
                    Close();
                }
                return;
            }

            await RunUninstallAsync(_existingManifest);
        }

        private async Task RunInstallAsync(InstallPlan plan)
        {
            _isBusy = true;
            _wasUninstallDone = false;
            _lastPlan = plan;
            BtnClose.IsEnabled = false;

            TxtProgressTitle.Text = _s.ProgressTitleInstall;
            TxtProgressStatus.Text = _s.ProgressPreparing;
            TxtProgressPercent.Text = "0%";
            ProgressFill.Width = 0;
            TxtLog.Text = string.Empty;

            ShowPage(PageId.Progress);

            var logBuilder = new StringBuilder();
            var progress = new Progress<InstallProgress>(p =>
            {
                TxtProgressPercent.Text = p.Percent + "%";
                if (!string.IsNullOrEmpty(p.Status)) TxtProgressStatus.Text = p.Status;
                UpdateProgressBar(p.Percent);
                if (!string.IsNullOrEmpty(p.LogLine))
                {
                    logBuilder.AppendLine(p.LogLine);
                    TxtLog.Text = logBuilder.ToString().TrimEnd();
                    LogScroll.ScrollToEnd();
                }
            });

            try
            {
                await InstallerEngine.InstallAsync(plan, _s, progress);
                await Task.Delay(250);

                // Configure Done page
                TxtDoneTitle.Text = _s.DoneTitle;
                TxtDoneSubtitle.Text = _s.DoneSubtitle;
                RowDoneGame.Visibility = Visibility.Visible;
                TxtDoneGamePath.Text = plan.GameDir;
                RowDoneSdk.Visibility = plan.Mode == SetupMode.Developer ? Visibility.Visible : Visibility.Collapsed;
                TxtDoneSdkPath.Text = plan.SdkDir;

                bool hasGame = SafeFileExistsIn(plan.GameDir, "game.exe");
                BtnLaunchGame.Visibility = hasGame ? Visibility.Visible : Visibility.Collapsed;

                string vsixPath = plan.Mode == SetupMode.Developer && plan.IncludeVsVsix && IsValidFolderPath(plan.SdkDir)
                    ? Path.Combine(plan.SdkDir, "plugins", "vs2026", "AzForge.Templates.vsix")
                    : string.Empty;
                BtnInstallVsix.Visibility = !string.IsNullOrEmpty(vsixPath) && File.Exists(vsixPath)
                    ? Visibility.Visible : Visibility.Collapsed;

                DoneActionsPanel.Visibility = Visibility.Visible;
                _isBusy = false;
                BtnClose.IsEnabled = true;
                ShowPage(PageId.Done);
            }
            catch (Exception ex)
            {
                _isBusy = false;
                BtnClose.IsEnabled = true;
                TxtProgressStatus.Text = _s.ProgressFailed;
                logBuilder.AppendLine("[ERROR] " + ex.Message);
                TxtLog.Text = logBuilder.ToString().TrimEnd();
                LogScroll.ScrollToEnd();
                await ShowModalAsync(_s.ProgressFailed, ex.Message, showCancel: false, okText: "OK");
                ShowPage(PageId.Config);
            }
        }

        private async Task RunUninstallAsync(InstalledManifest manifest)
        {
            _isBusy = true;
            _wasUninstallDone = true;
            BtnClose.IsEnabled = false;

            TxtProgressTitle.Text = _s.ProgressTitleUninstall;
            TxtProgressStatus.Text = _s.ProgressPreparing;
            TxtProgressPercent.Text = "0%";
            ProgressFill.Width = 0;
            TxtLog.Text = string.Empty;

            ShowPage(PageId.Progress);

            var logBuilder = new StringBuilder();
            var progress = new Progress<InstallProgress>(p =>
            {
                TxtProgressPercent.Text = p.Percent + "%";
                if (!string.IsNullOrEmpty(p.Status)) TxtProgressStatus.Text = p.Status;
                UpdateProgressBar(p.Percent);
                if (!string.IsNullOrEmpty(p.LogLine))
                {
                    logBuilder.AppendLine(p.LogLine);
                    TxtLog.Text = logBuilder.ToString().TrimEnd();
                    LogScroll.ScrollToEnd();
                }
            });

            try
            {
                await InstallerEngine.UninstallAsync(manifest, _s, progress);
                await Task.Delay(250);

                TxtDoneTitle.Text = _s.UninstallDoneTitle;
                TxtDoneSubtitle.Text = _s.UninstallDoneSubtitle;
                RowDoneGame.Visibility = !string.IsNullOrEmpty(manifest.GameDir) ? Visibility.Visible : Visibility.Collapsed;
                TxtDoneGamePath.Text = manifest.GameDir;
                RowDoneSdk.Visibility = Visibility.Collapsed;
                DoneActionsPanel.Visibility = Visibility.Collapsed;

                _existingManifest = null;
                _isBusy = false;
                BtnClose.IsEnabled = true;
                ShowPage(PageId.Done);
            }
            catch (Exception ex)
            {
                _isBusy = false;
                BtnClose.IsEnabled = true;
                await ShowModalAsync(_s.ProgressFailed, ex.Message, showCancel: false, okText: "OK");
                ShowPage(PageId.Uninstall);
            }
        }

        private void UpdateProgressBar(int percent)
        {
            double maxW = ProgressTrack.ActualWidth > 2 ? ProgressTrack.ActualWidth - 2 : 720;
            double target = Math.Max(0, Math.Min(100, percent)) / 100.0 * maxW;
            var anim = new DoubleAnimation(target, TimeSpan.FromMilliseconds(160))
            {
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
            };
            ProgressFill.BeginAnimation(WidthProperty, anim);
        }

        // ==================== DONE PAGE ACTIONS ====================
        private void BtnOpenGameDir_Click(object sender, RoutedEventArgs e)
        {
            OpenPath(TxtDoneGamePath.Text);
        }

        private void BtnOpenSdkDir_Click(object sender, RoutedEventArgs e)
        {
            OpenPath(TxtDoneSdkPath.Text);
        }

        private void BtnLaunchGame_Click(object sender, RoutedEventArgs e)
        {
            if (_lastPlan == null || !SafeFileExistsIn(_lastPlan.GameDir, "game.exe")) return;
            string exe = Path.Combine(_lastPlan.GameDir, "game.exe");
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = exe,
                    WorkingDirectory = _lastPlan.GameDir,
                    UseShellExecute = true
                });
            }
            catch { }
        }

        private void BtnInstallVsix_Click(object sender, RoutedEventArgs e)
        {
            if (_lastPlan == null || !IsValidFolderPath(_lastPlan.SdkDir)) return;
            string vsix = Path.Combine(_lastPlan.SdkDir, "plugins", "vs2026", "AzForge.Templates.vsix");
            if (File.Exists(vsix))
                OpenPath(vsix);
        }

        private void BtnOpenDocs_Click(object sender, RoutedEventArgs e)
        {
            OpenPath(InstallerEngine.DocsUrl);
        }

        private static void OpenPath(string target)
        {
            if (string.IsNullOrWhiteSpace(target)) return;
            try
            {
                Process.Start(new ProcessStartInfo { FileName = target, UseShellExecute = true });
            }
            catch { }
        }
    }
}
