using System.Globalization;

namespace AzForgeSetup
{
    internal enum UiLang
    {
        Ru,
        En
    }

    internal sealed class Strings
    {
        public UiLang Lang { get; private set; }

        public Strings(UiLang lang)
        {
            Lang = lang;
        }

        public static UiLang DetectSystemLang()
        {
            string name = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName.ToLowerInvariant();
            return (name == "ru" || name == "uk" || name == "be" || name == "kk") ? UiLang.Ru : UiLang.En;
        }

        private string T(string ru, string en) => Lang == UiLang.Ru ? ru : en;

        public string WindowTitle => T("Установка AzForge", "AzForge Setup");

        // Stepper
        public string Step1 => T("Режим", "Mode");
        public string Step2 => T("Пути", "Setup");
        public string Step3 => T("Установка", "Install");
        public string Step4 => T("Готово", "Done");

        // Page 1: Mode
        public string ModeKicker => T("ПЛАТФОРМА МОДОВ ДЛЯ AZANGARA", "MOD PLATFORM FOR AZANGARA");
        public string ModeTitle => T("Выберите режим установки", "Choose your installation");
        public string ModeSubtitle => T(
            "AzForge подключается через d3d8.dll и не изменяет оригинальные файлы игры.",
            "AzForge attaches via d3d8.dll without patching any original game files.");

        public string CardPlayerTitle => T("Для игрока", "Player");
        public string CardPlayerBadge => T("РЕКОМЕНДУЕТСЯ", "RECOMMENDED");
        public string CardPlayerDesc => T(
            "Только загрузчик модов. Ставит d3d8.dll и AzForge.dll рядом с game.exe и создаёт папку mods/. Ничего лишнего.",
            "Loader only. Places d3d8.dll and AzForge.dll next to game.exe and creates mods/. Nothing extra.");

        public string CardDevTitle => T("Для разработчика", "Developer");
        public string CardDevBadge => T("ПОЛНЫЙ НАБОР", "FULL TOOLKIT");
        public string CardDevDesc => T(
            "Загрузчик + полный C/C++ SDK, AmodPack, AzmPack, исходники SampleMod и расширения для Visual Studio и VS Code.",
            "Loader + full C/C++ SDK, AmodPack, AzmPack, SampleMod source, and Visual Studio / VS Code extensions.");

        public string FooterNote => T(
            "Windows x86  ·  Open Source  ·  Удаляется в один клик",
            "Windows x86  ·  Open Source  ·  Clean one-click uninstall");
        public string BtnUninstallLink => T("Удалить AzForge", "Uninstall AzForge");
        public string BtnContinue => T("Продолжить", "Continue");

        // Page 2: Config
        public string ConfigTitlePlayer => T("Папка игры Azangara", "Azangara game folder");
        public string ConfigTitleDev => T("Настройка установки", "Configure installation");
        public string ConfigSubPlayer => T(
            "Укажи папку, в которой лежит game.exe.",
            "Point to the folder containing game.exe.");
        public string ConfigSubDev => T(
            "Укажи папку игры и место для SDK, инструментов и плагинов.",
            "Choose the game folder and where to place the SDK, tools, and IDE plugins.");

        public string LabelGameDir => T("Папка игры (game.exe)", "Game folder (game.exe)");
        public string LabelSdkDir => T("Папка SDK и инструментов", "SDK & tools folder");
        public string BtnBrowse => T("Обзор…", "Browse…");
        public string PickerGameTitle => T("Выберите папку с игрой Azangara (game.exe)", "Select the Azangara folder (game.exe)");
        public string PickerSdkTitle => T("Выберите папку для AzForge SDK", "Select folder for AzForge SDK");

        public string GameFound => T("game.exe найден", "game.exe found");
        public string GameMissing => T("game.exe не найден", "game.exe not found");

        public string PlayerInfoTitle => T("Что будет установлено", "What will be installed");
        public string PlayerInfoBody => T(
            "В папку игры добавятся d3d8.dll, AzForge.dll и пустая папка mods/. В игре нажми F10, чтобы открыть встроенный менеджер модов. Оригинальные файлы Azangara не затрагиваются.",
            "d3d8.dll, AzForge.dll, and an empty mods/ folder will be placed next to game.exe. Press F10 in game to open the built-in MODS manager. Original game files stay untouched.");

        public string OptSampleMod => T("Установить SampleMod в mods/", "Install SampleMod into mods/");
        public string OptSampleModHint => T("Демо-мод с командами консоли", "Showcase mod with console commands");
        public string OptAddPath => T("Добавить утилиты в PATH", "Add CLI tools to user PATH");
        public string OptAddPathHint => T("AmodPack и AzmPack из любого терминала", "Run AmodPack & AzmPack anywhere");
        public string OptVsCode => T("Установить расширение VS Code", "Install VS Code extension");
        public string OptVsCodeHintFound => T("Поставит azforge-mod-tools.vsix автоматически", "Installs azforge-mod-tools.vsix automatically");
        public string OptVsCodeHintMissing => T("VS Code не найден (файл .vsix всё равно скопируется)", "VS Code not detected (.vsix will still be copied)");
        public string OptVs => T("Расширение Visual Studio (.vsix)", "Visual Studio extension (.vsix)");
        public string OptVsHint => T("VS 2022 / 2026 — мастер создания модов", "VS 2022 / 2026 — New Mod wizard");

        public string BtnBack => T("Назад", "Back");
        public string BtnInstall => T("Установить AzForge", "Install AzForge");
        public string BtnInstallAdmin => T("Установить (нужны права админа)", "Install (Admin required)");

        // Confirm dialogs
        public string ConfirmMissingGameTitle => T("game.exe не найден", "game.exe not found");
        public string ConfirmMissingGameText(string dir) => T(
            $"В папке «{dir}» не найден файл game.exe.\n\nВы уверены, что это папка игры Azangara?",
            $"game.exe was not found in:\n{dir}\n\nAre you sure this is your Azangara game folder?");
        public string ErrEmptyPath => T("Укажи путь к папке.", "Please choose a valid folder path.");
        public string ErrInvalidPath => T(
            "Путь содержит недопустимые символы (например, кавычки, <, >, |, *, ?). Проверь путь и попробуй снова.",
            "The path contains invalid characters (such as quotes, <, >, |, *, ?). Check the path and try again.");
        public string ErrGameRunningTitle => T("Azangara запущена", "Azangara is running");
        public string ErrGameRunningText => T(
            "Закрой игру Azangara (game.exe) перед установкой или удалением AzForge — файлы заблокированы.",
            "Please close Azangara (game.exe) before installing or removing AzForge — files are currently in use.");
        public string AdminNeededTitle => T("Нужны права администратора", "Administrator privileges required");
        public string AdminNeededText => T(
            "Выбранная папка защищена системой (например, Program Files). Нажми «Да», чтобы продолжить установку от имени администратора — выбранные пути сохранятся.",
            "The selected folder requires administrator privileges (e.g. Program Files). Click Yes to restart setup elevated — your chosen paths will be preserved.");

        // Page 3: Progress
        public string ProgressTitleInstall => T("Установка AzForge", "Installing AzForge");
        public string ProgressTitleUninstall => T("Удаление AzForge", "Removing AzForge");
        public string ProgressPreparing => T("Подготовка…", "Preparing…");
        public string ProgressDone => T("Готово", "Done");
        public string ProgressFailed => T("Ошибка", "Error");

        // Page 4: Done
        public string DoneTitle => T("AzForge установлен!", "AzForge is ready!");
        public string DoneSubtitle => T(
            "Нажми F10 в игре, чтобы открыть список модов. Кидай .amod-архивы в папку mods/.",
            "Press F10 in game to open the MODS screen. Drop .amod archives into the mods/ folder.");
        public string DoneGameLabel => T("Папка игры", "Game folder");
        public string DoneSdkLabel => T("Папка SDK", "SDK folder");
        public string DoneOpenHint => T("открыть", "open");
        public string BtnLaunchGame => T("Запустить Azangara", "Launch Azangara");
        public string BtnOpenDocs => T("Документация", "Documentation");
        public string BtnInstallVsix => T("Установить VS VSIX", "Install VS VSIX");
        public string BtnClose => T("Закрыть", "Close");

        // Uninstall page
        public string UninstallTitle => T("Удаление AzForge", "Uninstall AzForge");
        public string UninstallSubtitle => T(
            "Уберёт файлы загрузчика и вернёт Azangara в исходное состояние.",
            "Removes the loader files and restores Azangara to stock.");
        public string UninstallNotice => T(
            "Папка mods/ с твоими модами и оригинальные файлы Azangara НЕ будут удалены.",
            "Your mods/ folder and original Azangara files will NOT be touched.");
        public string UninstallWillRemove => T("Будет удалено:", "Will be removed:");
        public string UninstallNothingFound => T("Установленный AzForge не найден.", "No existing AzForge installation was found.");
        public string BtnUninstallNow => T("Удалить AzForge", "Uninstall AzForge");
        public string UninstallDoneTitle => T("AzForge удалён", "AzForge removed");
        public string UninstallDoneSubtitle => T(
            "Оригинальная игра Azangara осталась в целости. Папка mods/ не тронута.",
            "Your original Azangara installation is intact. The mods/ folder was left untouched.");

        // Engine log lines
        public string LogExtractCore => T("Копирование загрузчика (d3d8.dll, AzForge.dll)…", "Copying loader (d3d8.dll, AzForge.dll)…");
        public string LogCreateMods => T("Проверка папки mods/…", "Ensuring mods/ folder…");
        public string LogSampleMod => T("Установка SampleMod.amod…", "Installing SampleMod.amod…");
        public string LogExtractSdk => T("Распаковка SDK и утилит (AmodPack, AzmPack)…", "Extracting SDK & CLI tools (AmodPack, AzmPack)…");
        public string LogExtractVs => T("Копирование расширения Visual Studio (.vsix)…", "Copying Visual Studio extension (.vsix)…");
        public string LogExtractVsCode => T("Копирование расширения VS Code (.vsix)…", "Copying VS Code extension (.vsix)…");
        public string LogRegistry => T("Запись путей в реестр (Visual Studio wizard)…", "Writing paths to registry (Visual Studio wizard)…");
        public string LogPath => T("Обновление пользовательского PATH…", "Updating user PATH…");
        public string LogInstallVsCode => T("Установка расширения в VS Code…", "Installing extension into VS Code…");
        public string LogUninstaller => T("Регистрация деинсталлятора в Windows…", "Registering Windows uninstaller…");
        public string LogComplete => T("Установка успешно завершена.", "Installation completed successfully.");
    }
}
