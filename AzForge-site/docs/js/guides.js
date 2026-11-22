// Гайдовые страницы: обзор, быстрый старт, мод-пакет, модель API, консоль, совместимость.
(function () {
  "use strict";
  const U = window.UTIL;
  const esc = U.esc;
  const code = U.codeBlock;
  const T = window.AF.t;

  const HEAD = (crumb, crumbEn, title, titleEn, sub, subEn) =>
    '<div class="page-head"><div class="crumb"><a href="#/overview">' + T("Документация", "Documentation") + '</a><span class="sep">/</span>' + T(crumb, crumbEn) + "</div>" +
    "<h1 class=\"page-title\">" + T(title, titleEn) + "</h1>" +
    (sub ? '<p class="page-sub">' + T(sub, subEn) + "</p>" : "") + '<div class="rule"></div></div>';

  // ==================================================================
  const overview = () =>
    "<h2 class=\"sec\">" + T("Принцип работы", "How it works") + "</h2>" +
    "<p>" + T(
      "AzForge попадает в процесс через прокси <code>d3d8.dll</code>, лежащий рядом с <code>game.exe</code>. Никаких байт в файлах игры не меняется: <b>ничего не патчится на диске</b>, а удаление одного файла возвращает игру в исходное состояние. Лоадер находит нужные процедуры игры по байтовым сигнатурам; сигнатура, которая не нашлась или нашлась больше чем в одном месте, — отказ, и лоадер просто стоит в стороне.",
      "AzForge enters the process through a <code>d3d8.dll</code> proxy placed next to <code>game.exe</code>. No bytes of the game files are changed: <b>nothing is patched on disk</b>, and deleting one file returns the game to its stock state. The loader locates the game procedures by byte signatures; a signature that matches nothing — or matches more than one place — is a hard refusal, and the loader simply steps aside.") +
    "</p>" +
    "<p>" + T(
      "Загрузчик перехватывает запросы игры к ресурсам. Порядок разрешения пути: <b>моды → диск → data.pak</b>. Мод отвечает своими файлами через манифест и <code>files/</code>-префикс, либо в рантайме через <code>Files::Serve</code>.",
      "The loader intercepts the game's resource requests. Path resolution order: <b>mods → disk → data.pak</b>. A mod serves its files through the manifest and the <code>files/</code> prefix, or at runtime via <code>Files::Serve</code>.") +
    "</p>" +

    '<div class="arch">' +
    '<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-family="Segoe UI, sans-serif">' +
      '<defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10z" fill="#ff7800"/></marker></defs>' +
      '<rect x="20" y="40" width="150" height="52" rx="10" fill="none" stroke="#ff7800" stroke-width="1.5"/>' +
      '<text x="95" y="62" text-anchor="middle" fill="#ff9440" font-size="13" font-weight="700">d3d8.dll</text>' +
      '<text x="95" y="80" text-anchor="middle" fill="#8b96a8" font-size="10.5">' + T("точка входа", "entry point") + "</text>" +
      '<rect x="230" y="40" width="160" height="52" rx="10" fill="none" stroke="#ff7800" stroke-width="1.5"/>' +
      '<text x="310" y="62" text-anchor="middle" fill="#ff9440" font-size="13" font-weight="700">AzForge.dll</text>' +
      '<text x="310" y="80" text-anchor="middle" fill="#8b96a8" font-size="10.5">' + T("лоадер + SDK", "loader + SDK") + "</text>" +
      '<rect x="470" y="40" width="130" height="52" rx="10" fill="none" stroke="#5b6575" stroke-width="1.5"/>' +
      '<text x="535" y="62" text-anchor="middle" fill="#dce3ee" font-size="13" font-weight="700">game.exe</text>' +
      '<text x="535" y="80" text-anchor="middle" fill="#8b96a8" font-size="10.5">Azangara, x86</text>' +
      '<line x1="170" y1="66" x2="226" y2="66" stroke="#ff7800" stroke-width="1.5" marker-end="url(#ar)"/>' +
      '<line x1="390" y1="66" x2="466" y2="66" stroke="#ff7800" stroke-width="1.5" marker-end="url(#ar)"/>' +
      '<rect x="230" y="170" width="160" height="52" rx="10" fill="none" stroke="#5b6575" stroke-width="1.5"/>' +
      '<text x="310" y="192" text-anchor="middle" fill="#dce3ee" font-size="13" font-weight="700">mods/*.amod</text>' +
      '<text x="310" y="210" text-anchor="middle" fill="#8b96a8" font-size="10.5">' + T("манифест + файлы + DLL", "manifest + files + DLL") + "</text>" +
      '<line x1="310" y1="170" x2="310" y2="96" stroke="#ff7800" stroke-width="1.5" marker-end="url(#ar)"/>' +
      '<rect x="470" y="170" width="130" height="52" rx="10" fill="none" stroke="#5b6575" stroke-width="1.5"/>' +
      '<text x="535" y="192" text-anchor="middle" fill="#dce3ee" font-size="13" font-weight="700">data.pak</text>' +
      '<text x="535" y="210" text-anchor="middle" fill="#8b96a8" font-size="10.5">' + T("родные ресурсы", "native resources") + "</text>" +
      '<line x1="470" y1="196" x2="394" y2="196" stroke="#5b6575" stroke-width="1.2" stroke-dasharray="4 4"/>' +
      '<text x="700" y="66" fill="#8b96a8" font-size="10.5" text-anchor="middle">forge.log</text>' +
      '<line x1="390" y1="52" x2="688" y2="58" stroke="#5b6575" stroke-width="1" stroke-dasharray="3 4"/>' +
      '<text x="95" y="196" text-anchor="middle" fill="#616c7e" font-size="11">' + T("порядок ресурсов:", "resource order:") + "</text>" +
      '<text x="95" y="214" text-anchor="middle" fill="#ff9440" font-size="11" font-weight="600">' + T("моды → диск → pak", "mods → disk → pak") + "</text>" +
    "</svg></div>" +

    "<h2 class=\"sec\">" + T("Что умеет мод", "What a mod can do") + "</h2>" +
    '<div class="cols">' +
      '<div class="card"><h4><span class="ic">◈</span>' + T("Заменять любой ресурс", "Replace any resource") + "</h4><p>" +
        T("Скрипты, уровни, комнаты, текстуры, модели, звуки — файлом в <code>files/</code> или байтами через <code>Files::Serve</code>.",
          "Scripts, levels, rooms, textures, models, sounds — as a file under <code>files/</code> or as bytes via <code>Files::Serve</code>.") + "</p></div>" +
      '<div class="card"><h4><span class="ic">◈</span>' + T("Исполнять код", "Run code") + "</h4><p>" +
        T("Мод — DLL внутри контейнера. При загрузке получает таблицу <code>AmodApi</code> и 32 подсистемы: мир, монстры, рендер, камера, инвентарь…",
          "A mod is a DLL inside a container. At load time it receives the <code>AmodApi</code> table and 32 subsystems: world, monsters, render, camera, inventory…") + "</p></div>" +
    "</div>" +

    "<h2 class=\"sec\">" + T("Модификации совместимы между собой", "Mods are compatible with each other") + "</h2>" +
    "<p>" + T(
      "Два мода, заменяющих один файл скрипта, конфликтуют — побеждает установленный последним. Два мода, двигающих разные элементы меню, работают одновременно. Манифест умеет объявлять <code>load_after</code>, <code>conflicts</code>, <code>requires</code> — подсистемы, без которых мод не смыслит загружаться.",
      "Two mods replacing the same script file conflict — the one installed last wins. Two mods moving different menu elements run side by side. The manifest can declare <code>load_after</code>, <code>conflicts</code>, <code>requires</code> — subsystems without which the mod refuses to load.") +
    "</p>" +

    '<div class="note"><span class="ic">◈</span><div><b>' + T("С чего начать:", "Where to start:") + "</b> " +
    T("раздел <a href=\"#/quickstart\">Быстрый старт</a> доводит до работающего .amod за пять шагов. Полный протокол взаимодействия мода и лоадера описан в разделе <a href=\"#/apimodel\">Модель API</a>.",
      "the <a href=\"#/quickstart\">Quick start</a> section gets you to a working .amod in five steps. The full protocol between mod and loader is described in <a href=\"#/apimodel\">API model</a>.") +
    "</div></div>";

  // ==================================================================
  const quickstart = () =>
    HEAD("Быстрый старт", "Quick start", "Быстрый старт", "Quick start", "", "") +

    "<h2 class=\"sec\">" + T("Шаги", "Steps") + '</h2><ol class="steps">' +
    "<li><h4>" + T("Папка мода", "Mod folder") + "</h4>" +
      "<p>" + T("Минимальный состав — манифест и, при желании, замещаемые ресурсы и DLL:", "The minimum is a manifest plus, optionally, replacement resources and a DLL:") + "</p>" +
      code("mod.json\nfiles/scripts/local/dialog_eng.txt\nbin/MyMod.dll", T("структура", "layout"), null) +
    "</li>" +
    "<li><h4>mod.json</h4>" +
      "<p>" + T("<code>id</code> — строчные буквы, цифры, <code>. _ -</code>; <code>api_version</code> должен быть не больше версии лоадера и не меньше старейшей поддерживаемой (сейчас 1). Полный разбор полей — в разделе <a href=\"#/package\">Мод-пакет</a>.",
        "<code>id</code> — lowercase letters, digits, <code>. _ -</code>; <code>api_version</code> must be no greater than the loader version and no less than the oldest supported one (currently 1). The full field breakdown is in the <a href=\"#/package\">Mod package</a> section.") + "</p>" +
      code('{\n  "id": "author.mymod",\n  "name": "My Mod",\n  "version": "1.0.0",\n  "api_version": 3,\n  "entry": "bin/MyMod.dll"\n}', "mod.json", "json") +
    "</li>" +
    "<li><h4>" + T("Код мода", "Mod code") + "</h4>" +
      "<p>" + T("Один заголовок, линковаться ни с чем не нужно. Три экспорта: <code>AmodInit</code>, <code>AmodShutdown</code>, <code>AmodUpdate</code>. В C++-обёртках подсистемы доступны как <code>Amod::Files::Serve</code>, <code>Amod::Console::Print</code> и т.д.",
        "One header, nothing to link against. Three exports: <code>AmodInit</code>, <code>AmodShutdown</code>, <code>AmodUpdate</code>. In the C++ wrappers the subsystems are reachable as <code>Amod::Files::Serve</code>, <code>Amod::Console::Print</code>, and so on.") + "</p>" +
      code("#include <ForgeApi.h>\n\nAMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)\n{\n    if (!Amod::Attach(api))\n        return AMOD_ERROR_API_VERSION;\n\n    Amod::Log(AMOD_LOG_INFO, \"%s is up\", Amod::ModId());\n    return AMOD_OK;\n}\n\nAMOD_EXPORT void __cdecl AmodShutdown(void) { }\n\nAMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame) { }", "Mod.cpp", null) +
      "<p>" + T("<code>Attach</code> проверяет версию API и размер таблицы, затем раскладывает указатели подсистем по namespace'ам. Подсистема нулевая, если лоадер её не несёт, — проверяйте <code>Amod::X::IsAvailable()</code>.",
        "<code>Attach</code> checks the API version and the table size, then spreads the subsystem pointers across namespaces. A subsystem is null if the loader does not carry it — check <code>Amod::X::IsAvailable()</code>.") + "</p>" +
    "</li>" +
    "<li><h4>" + T("Сборка", "Build") + "</h4>" +
      "<p>" + T("Visual Studio или MSBuild напрямую. Готовый пример с пост-сборкой в .amod — <code>sdk/SampleMod</code>:",
        "Visual Studio, or MSBuild directly. A ready sample with a .amod post-build step is <code>sdk/SampleMod</code>:") + "</p>" +
      code("msbuild SampleMod.vcxproj /p:Configuration=Release /p:Platform=x86", T("терминал", "terminal"), "sh") +
    "</li>" +
    "<li><h4>" + T("Упаковка и установка", "Pack and install") + "</h4>" +
      "<p>" + T("<code>amodpack</code> проверяет манифест перед упаковкой — битый мод до игрока не доедет:",
        "<code>amodpack</code> validates the manifest before packing — a broken mod never reaches the player:") + "</p>" +
      code("amodpack pack MyMod -o MyMod.amod\namodpack install MyMod.amod --game \"C:\\path\\to\\the\\game\"\namodpack list --game \"C:\\path\\to\\the\\game\"", T("терминал", "terminal"), "sh") +
      "<p>" + T("Моды живут в <code>mods/</code> рядом с <code>game.exe</code>; туда же кладутся <code>d3d8.dll</code> и <code>AzForge.dll</code>.",
        "Mods live in <code>mods/</code> next to <code>game.exe</code>; <code>d3d8.dll</code> and <code>AzForge.dll</code> go there too.") + "</p>" +
    "</li>" +
    "<li><h4>" + T("Запуск и журнал", "Run and log") + "</h4>" +
      "<p>" + T("После запуска игры рядом с ней появляется <code>forge.log</code>: что найдено, что загружено и почему что-то отказано.",
        "After the game starts, <code>forge.log</code> appears next to it: what was found, what was loaded, and why anything was refused.") + "</p>" +
    "</li>" +
    "</ol>";

  // ==================================================================
  const package_ = () =>
    HEAD("Мод-пакет", "Mod package", "Мод-пакет: .amod и манифест", "Mod package: .amod and the manifest",
      "Контейнер .amod — обычный zip. Ставится, только когда манифест валиден: проверка происходит при упаковке, так что сломанный мод не доходит до игрока.",
      "An .amod container is a plain zip. It installs only when the manifest is valid: validation happens at pack time, so a broken mod never reaches the player.") +

    "<h2 class=\"sec\">" + T("Состав контейнера", "Container layout") + "</h2>" +
    code("mod.json                " + T("манифест, обязателен", "manifest, required") + "\nfiles/                  " + T("ресурсы: пути как их спрашивает игра", "resources: paths as the game asks for them") + "\n  scripts/…\n  levels/…  rooms/…\n  textures/…  models/…\n  sounds/…  music/…\nbin/Mod.dll             " + T("код мода (поле entry)", "mod code (the entry field)") + "\nicon.png                " + T("иконка (поле icon)", "icon (the icon field)"), ".amod", null) +
    "<p>" + T("Пути под <code>files/</code> зеркалят те, что игра спрашивает, — те же ключи, что и в <code>data.pak</code>. Префикс <code>files/</code> снимается при загрузке: <code>files/scripts/rs/gui.txt</code> отвечает за <code>scripts/rs/gui.txt</code>.",
      "Paths under <code>files/</code> mirror what the game asks for — the same keys as in <code>data.pak</code>. The <code>files/</code> prefix is stripped on load: <code>files/scripts/rs/gui.txt</code> answers for <code>scripts/rs/gui.txt</code>.") + "</p>" +

    "<h2 class=\"sec\">mod.json</h2>" +
    '<div class="tbl-wrap"><table class="tbl"><thead><tr><th>' + T("Поле", "Field") + "</th><th>" + T("Тип", "Type") + "</th><th>" + T("Правила", "Rules") + "</th></tr></thead><tbody>" +
    "<tr><td><code>id</code></td><td>" + T("строка, обязателен", "string, required") + "</td><td class='t-dim'>" + T("только <code>[a-z0-9._-]</code>; иначе отказ", "<code>[a-z0-9._-]</code> only; otherwise refused") + "</td></tr>" +
    "<tr><td><code>name</code></td><td>" + T("строка, обязателен", "string, required") + "</td><td class='t-dim'>" + T("отображаемое имя", "display name") + "</td></tr>" +
    "<tr><td><code>version</code></td><td>" + T("строка, обязателен", "string, required") + "</td><td class='t-dim'>" + T("версия мода, например <code>1.2.0</code>", "mod version, e.g. <code>1.2.0</code>") + "</td></tr>" +
    "<tr><td><code>api_version</code></td><td>" + T("число, обязателен", "number, required") + "</td><td class='t-dim'>" + T("целое; &gt; версии лоадера — отказ, &lt; старейшей поддерживаемой — отказ", "integer; &gt; loader version — refused, &lt; oldest supported — refused") + "</td></tr>" +
    "<tr><td><code>entry</code></td><td>" + T("строка", "string") + "</td><td class='t-dim'>" + T("путь DLL внутри контейнера; файл обязан существовать", "DLL path inside the container; the file must exist") + "</td></tr>" +
    "<tr><td><code>author</code></td><td>" + T("строка", "string") + "</td><td class='t-dim'>" + T("имя автора; показывается в карточке мода в списке модов", "the author's name; shown on the mod's card in the mod list") + "</td></tr>" +
    "<tr><td><code>description</code></td><td>" + T("строка", "string") + "</td><td class='t-dim'>" + T("короткое описание; показывается в списке модов и читается через <code>ModManager</code>", "a short description; shown in the mod list and readable via <code>ModManager</code>") + "</td></tr>" +
    "<tr><td><code>icon</code></td><td>" + T("строка", "string") + "</td><td class='t-dim'>" + T("путь к иконке внутри контейнера; без него берётся <code>icon.bmp</code> из корня; иконка показывается плиткой в списке модов", "path to an icon inside the container; defaults to <code>icon.bmp</code> in the root; shown as the tile picture in the mod list") + "</td></tr>" +
    "<tr><td><code>game_version</code></td><td>" + T("строка", "string") + "</td><td class='t-dim'>" + T("версия игры, под которую собран мод; пока ни на что не влияет — задел на будущее", "the game version the mod targets; not enforced yet — reserved for the future") + "</td></tr>" +
    "<tr><td><code>load_after</code></td><td>" + T("массив id", "array of ids") + "</td><td class='t-dim'>" + T("этот мод грузится после перечисленных", "this mod loads after the listed ones") + "</td></tr>" +
    "<tr><td><code>conflicts</code></td><td>" + T("массив id", "array of ids") + "</td><td class='t-dim'>" + T("совместная установка с перечисленными запрещена", "installing together with the listed ones is forbidden") + "</td></tr>" +
    "<tr><td><code>requires</code></td><td>" + T("массив подсистем", "array of subsystems") + "</td><td class='t-dim'>" + T("мод не грузится, если лоадер не несёт перечисленное; имена — из <code>sdk/subsystems.txt</code>", "the mod won't load if the loader doesn't carry the listed ones; names are from <code>sdk/subsystems.txt</code>") + "</td></tr>" +
    "<tr><td><code>capabilities</code></td><td>" + T("массив строк", "array of strings") + "</td><td class='t-dim'>" + T("заявление о намерениях для списка модов", "a statement of intent for the mod list") + "</td></tr>" +
    "</tbody></table></div>" +

    "<h2 class=\"sec\">amodpack</h2>" +
    '<div class="tbl-wrap"><table class="tbl"><thead><tr><th>' + T("Команда", "Command") + "</th><th>" + T("Действие", "Action") + "</th></tr></thead><tbody>" +
    "<tr><td><code>amodpack pack &lt;folder&gt; [-o out.amod]</code></td><td class='t-dim'>" + T("собрать контейнер из папки с mod.json (проверяет манифест и наличие entry)", "build a container from a folder with mod.json (validates the manifest and entry)") + "</td></tr>" +
    "<tr><td><code>amodpack unpack &lt;file.amod&gt; [-o folder]</code></td><td class='t-dim'>" + T("распаковать", "unpack") + "</td></tr>" +
    "<tr><td><code>amodpack info &lt;file.amod&gt;</code></td><td class='t-dim'>" + T("манифест и содержимое", "manifest and contents") + "</td></tr>" +
    "<tr><td><code>amodpack validate &lt;file.amod|folder&gt;</code></td><td class='t-dim'>" + T("сквозная проверка контейнера", "end-to-end container validation") + "</td></tr>" +
    "<tr><td><code>amodpack list [--game dir]</code></td><td class='t-dim'>" + T("установленные моды", "installed mods") + "</td></tr>" +
    "<tr><td><code>amodpack install &lt;file.amod&gt; [--game dir]</code></td><td class='t-dim'>" + T("скопировать в mods/ (замена при той же версии)", "copy into mods/ (replaced at the same version)") + "</td></tr>" +
    "<tr><td><code>amodpack remove &lt;id&gt;</code></td><td class='t-dim'>" + T("удалить установленный мод", "remove an installed mod") + "</td></tr>" +
    "<tr><td><code>amodpack enable/disable &lt;id&gt;</code></td><td class='t-dim'>" + T("вернуть в порядок загрузки / оставить установленным, но не загружать (суффикс .disabled)", "back into load order / keep installed but not loaded (the .disabled suffix)") + "</td></tr>" +
    "</tbody></table></div>" +
    "<p><code>--game</code> " + T("по умолчанию — текущая папка; моды живут в <code>&lt;game&gt;\\mods</code>.", "defaults to the current folder; mods live in <code>&lt;game&gt;\\mods</code>.") + "</p>" +

    "<h2 class=\"sec\">" + T("Порядок загрузки", "Load order") + "</h2>" +
    "<p>" + T("Контейнеры в <code>mods/</code> читаются в порядке установки, порядок внутри корректируют <code>load_after</code>. Каждый мод получает свой срез файловой системы, так что одинаковые пути из разных модов не пересекаются: конфликт разрешается порядком, а не случайностью. Отказ одного мода не мешает остальным — причина пишется в <code>forge.log</code> и видна через <code>ModManager::FailureReason</code>.",
      "Containers in <code>mods/</code> are read in installation order; <code>load_after</code> corrects it. Each mod gets its own slice of the file system, so identical paths from different mods never collide: conflicts are resolved by order, not by chance. One mod failing does not hurt the rest — the reason goes to <code>forge.log</code> and is visible via <code>ModManager::FailureReason</code>.") + "</p>" +
    '<div class="note warn"><span class="ic">!</span><div>' +
    T("Отладочная копия DLL мода вынимается из .amod в <code>mods\\.cache</code>. Запуская игру под отладчиком, собирайте мод заново — устаревшая сборка загрузится молча.",
      "The debug copy of the mod DLL is unpacked from the .amod into <code>mods\\.cache</code>. When running the game under a debugger, rebuild the mod — a stale build loads silently.") +
    "</div></div>";

  // ==================================================================
  const apimodel = () =>
    HEAD("Модель API", "API model", "Модель API", "API model",
      "Протокол, на котором мод и лоадер договорились: чистый C, версии, ошибки, время жизни.",
      "The protocol mod and loader agreed on: pure C, versions, errors, lifetimes.") +

    "<h2 class=\"sec\">" + T("Контракт ABI", "ABI contract") + "</h2>" +
    '<div class="cols">' +
      '<div class="card"><h4><span class="ic">◈</span>' + T("Только C, __cdecl", "C only, __cdecl") + "</h4><p>" +
        T("Через границу не проходит ни один объект C++ и ни одно исключение. Всё — таблицы указателей на функции и плоские структуры.",
          "No C++ object and no exception crosses the boundary. Everything is tables of function pointers and flat structures.") + "</p></div>" +
      '<div class="card"><h4><span class="ic">◈</span>' + T("structSize первым полем", "structSize first") + "</h4><p>" +
        T("Каждая выходная структура начинается с <code>structSize</code>. Вызывающий выставляет <code>sizeof</code> своей копии — так старый мод читает новую структуру безопасно.",
          "Every output structure begins with <code>structSize</code>. The caller sets the <code>sizeof</code> of its own copy — that is how an old mod reads a new structure safely.") + "</p></div>" +
      '<div class="card"><h4><span class="ic">◈</span>' + T("Поля только в конец", "Fields appended only") + "</h4><p>" +
        T("Таблицы растут только дописыванием. Каждый заголовок закрепляет смещение последнего поля: <code>static_assert(offsetof(AmodStageApi, RunMusic) == 0x38)</code>. Поля не переезжают.",
          "Tables grow only by appending. Every header pins the offset of its last field: <code>static_assert(offsetof(AmodStageApi, RunMusic) == 0x38)</code>. Fields never move.") + "</p></div>" +
    "</div>" +

    "<h2 class=\"sec\">" + T("Загрузка и AmodApi", "Loading and AmodApi") + "</h2>" +
    "<p>" + T("Лоадер находит DLL мода по <code>entry</code> манифеста, загружает её и вызывает три экспорта. <code>AmodInit</code> получает единственный аргумент — <code>const AmodApi*</code>:",
      "The loader finds the mod DLL by the manifest <code>entry</code>, loads it and calls the three exports. <code>AmodInit</code> receives a single argument — <code>const AmodApi*</code>:") + "</p>" +
    code(T("typedef struct AmodApi\n{\n    uint32_t structSize;   /* sizeof, каким собрал лоадер */\n    uint32_t apiVersion;   /* AMOD_API_VERSION лоадера */\n    AmodMod  self;         /* хендл этого мода */\n\n    void (__cdecl* Log)(AmodMod mod, AmodLogLevel level, const char* text);\n    /* ...версии, каталог игры, вёрстка game.exe... */\n\n    const AmodWorldApi*  world;    /* ноль — подсистемы нет,   */\n    const AmodRenderApi* render;   /* это легальный ответ      */\n    /* ...ещё ~30 подсистем... */\n} AmodApi;",
      "typedef struct AmodApi\n{\n    uint32_t structSize;   /* sizeof, as the loader built it */\n    uint32_t apiVersion;   /* the loader's AMOD_API_VERSION */\n    AmodMod  self;         /* handle of this mod */\n\n    void (__cdecl* Log)(AmodMod mod, AmodLogLevel level, const char* text);\n    /* ...versions, game directory, the game.exe layout... */\n\n    const AmodWorldApi*  world;    /* null — the subsystem    */\n    const AmodRenderApi* render;   /* is absent: a legal reply */\n    /* ...~30 more subsystems... */\n} AmodApi;"), "ForgeApi.h — " + T("вырезка", "excerpt"), null) +
    "<p>" + T("<code>Amod::Attach(api)</code> в C++-обёртке проверяет <code>apiVersion</code> и <code>structSize</code> и раскладывает подсистемы по namespace'ам. Возвращать из <code>AmodInit</code> нужно <code>AMOD_OK</code> либо <code>AMOD_ERROR_API_VERSION</code>, если лоадер старше, чем мод требует.",
      "<code>Amod::Attach(api)</code> in the C++ wrapper checks <code>apiVersion</code> and <code>structSize</code>, then spreads the subsystems across namespaces. Return <code>AMOD_OK</code> from <code>AmodInit</code>, or <code>AMOD_ERROR_API_VERSION</code> if the loader is older than the mod requires.") + "</p>" +

    "<h2 class=\"sec\">" + T("Ошибки", "Errors") + "</h2>" +
    "<p>" + T("Подсистемы сообщают отказ возвращаемым значением: <code>int</code>/<code>AmodId</code> — <code>0</code>, <code>bool</code>-подобное — <code>false</code>. Причина отказа читается сразу после отказа:",
      "Subsystems report failure through the return value: <code>int</code>/<code>AmodId</code> — <code>0</code>, <code>bool</code>-like — <code>false</code>. Read the failure reason right after the call:") + "</p>" +
    '<div class="tbl-wrap"><table class="tbl"><thead><tr><th>' + T("Код", "Code") + "</th><th>" + T("Смысл", "Meaning") + "</th></tr></thead><tbody>" +
    "<tr><td><code>AMOD_FAIL_NO_SUCH</code></td><td class='t-dim'>" + T("нет такого имени, индекса или id", "no such name, index or id") + "</td></tr>" +
    "<tr><td><code>AMOD_FAIL_BAD_ARGUMENT</code></td><td class='t-dim'>" + T("вызов сформирован неверно", "the call is malformed") + "</td></tr>" +
    "<tr><td><code>AMOD_FAIL_NOT_HERE</code></td><td class='t-dim'>" + T("сейчас нет уровня/комнаты/игрока", "no level/room/player right now") + "</td></tr>" +
    "<tr><td><code>AMOD_FAIL_NOT_DECLARED</code></td><td class='t-dim'>" + T("скрипт не писал эту часть", "the script never wrote this part") + "</td></tr>" +
    "<tr><td><code>AMOD_FAIL_REFUSED</code></td><td class='t-dim'>" + T("игра спрошена и ответила отказом", "the game was asked and refused") + "</td></tr>" +
    "<tr><td><code>AMOD_FAIL_NOT_OFFERED</code></td><td class='t-dim'>" + T("этот лоадер не несёт вызов", "this loader does not carry the call") + "</td></tr>" +
    "</tbody></table></div>" +
    code("if (!Amod::Player::Revive())\n{\n    Amod::Log(AMOD_LOG_WARNING, \"revive refused: %s (code %d)\",\n             Amod::LastError(), Amod::LastErrorCode());\n}", T("чтение причины отказа", "reading the failure reason"), null) +

    "<h2 class=\"sec\">" + T("Токены и время жизни", "Tokens and lifetimes") + "</h2>" +
    '<div class="tbl-wrap"><table class="tbl"><thead><tr><th>' + T("Правило", "Rule") + "</th><th>" + T("Что это значит на практике", "What it means in practice") + "</th></tr></thead><tbody>" +
    "<tr><td><b>" + T("Токены", "Tokens") + "</b></td><td class='t-dim'>" + T("обработчики (колбэки) возвращают <code>AmodToken</code>. Токен снимается <code>Cancel</code>/<code>Unsubscribe</code>/<code>Remove*</code> — это старое и новое имя одного вызова.",
      "handlers (callbacks) return an <code>AmodToken</code>. The token is released with <code>Cancel</code>/<code>Unsubscribe</code>/<code>Remove*</code> — the old and new names of the same call.") + "</td></tr>" +
    "<tr><td><b>" + T("Обратный демонтаж", "Reverse teardown") + "</b></td><td class='t-dim'>" + T("при выгрузке мода всё зарегистрированное им снимается само, в обратном порядке. Тирдаун-код, зеркалящий сетап, писать не нужно.",
      "when a mod unloads, everything it registered is released automatically, in reverse order. You don't write teardown code mirroring your setup.") + "</td></tr>" +
    "<tr><td><b>" + T("Снимки против хендлов", "Snapshots vs handles") + "</b></td><td class='t-dim'>" + T("структуры-«снимки» (<code>AmodPlayer</code>, <code>AmodCamera</code>) действительны один кадр; устойчивые вещи выдают id (<code>AmodMonsterId</code>, <code>AmodCtrlId</code>) — индексы живут кадр, id — весь срок жизни объекта.",
      "snapshot structures (<code>AmodPlayer</code>, <code>AmodCamera</code>) are valid for one frame; stable things come with ids (<code>AmodMonsterId</code>, <code>AmodCtrlId</code>) — indices live a frame, ids live as long as the object.") + "</td></tr>" +
    "<tr><td><b>" + T("Строки игры", "Game strings") + "</b></td><td class='t-dim'>" + T("строка, которую вернула функция, принадлежит игре: она действительна до момента, указанного в комментарии к функции, — до следующего вызова, выгрузки уровня или выгрузки мода. Нужна дольше — сразу копируйте к себе.",
      "a string a function returned belongs to the game: it stays valid until the moment stated in that function's comment — until the next call, level unload or mod unload. If you need it longer, copy it right away.") + "</td></tr>" +
    "<tr><td><b>" + T("Колбэки", "Callbacks") + "</b></td><td class='t-dim'>" + T("info-структуры колбэка действительны только внутри вызова.",
      "callback info structures are valid only inside the call.") + "</td></tr>" +
    "</tbody></table></div>" +

    "<h2 class=\"sec\">" + T("Версионирование", "Versioning") + "</h2>" +
    '<div class="tbl-wrap"><table class="tbl"><thead><tr><th>' + T("Величина", "Quantity") + "</th><th>" + T("Значение", "Value") + "</th><th>" + T("Где проверяется", "Where it is checked") + "</th></tr></thead><tbody>" +
    "<tr><td>" + T("Текущая версия API", "Current API version") + "</td><td><code>" + U.DATA.meta.apiVersion + "</code></td><td class='t-dim'><code>AMOD_API_VERSION</code> " + T("в ForgeTypes.h; мод требует её в <code>api_version</code>", "in ForgeTypes.h; the mod requests it via <code>api_version</code>") + "</td></tr>" +
    "<tr><td>" + T("Старейшая поддерживаемая", "Oldest supported") + "</td><td><code>1</code></td><td class='t-dim'>" + T("лоадер отказывает модам с меньшим <code>api_version</code>", "the loader refuses mods with a lower <code>api_version</code>") + "</td></tr>" +
    "<tr><td>" + T("Верстка game.exe", "game.exe build") + "</td><td><code>AMOD_GAME_BUILD_AZANGARA_10</code></td><td class='t-dim'><code>GetGameBuild()</code>; " + T("сырые указатели выдают только при совпадении ожидаемой верстки", "raw pointers are only granted when the expected build matches") + "</td></tr>" +
    "</tbody></table></div>" +
    "<p>" + T("Проверка двойная: <code>api-&gt;apiVersion &gt;= AMOD_API_VERSION</code> (лоадер знает всё, что знает мод) и <code>api-&gt;structSize &gt;= sizeof(AmodApi)</code> (таблица действительно полная). В <code>AmodInit</code> уровня игры ещё нет: вызовы, требующие уровня, вернут <code>AMOD_FAIL_NOT_HERE</code>.",
      "The check is doubled: <code>api-&gt;apiVersion &gt;= AMOD_API_VERSION</code> (the loader knows everything the mod knows) and <code>api-&gt;structSize &gt;= sizeof(AmodApi)</code> (the table is really complete). During <code>AmodInit</code> there is no level yet: calls that need one return <code>AMOD_FAIL_NOT_HERE</code>.") + "</p>";

  window.GUIDES = { overview, quickstart, package: package_, apimodel };
})();
