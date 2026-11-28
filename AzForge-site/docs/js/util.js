// Утилиты, реестр подсистем, подсветка синтаксиса. Язык — window.AF.
(function () {
  "use strict";

  const W = window;
  W.UTIL = {};
  const AF = window.AF;
  const T = AF.t;

  const esc = (s) => String(s == null ? "" : s)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
  W.UTIL.esc = esc;

  // ---- описания API на двух языках ----
  const RU = W.AZFORGE_RU || {};
  const EN = W.AZFORGE_EN || {};
  W.UTIL.desc = (key) => {
    const own = AF.lang === "en" ? EN : RU;
    const other = AF.lang === "en" ? RU : EN;
    return own[key] || other[key] || "";
  };

  // ---- карта таблиц -> namespace C++ -> заголовок ----
  const D = W.AZFORGE_DATA;
  const HEADERS = D.headers;
  W.UTIL.DATA = D;

  // namespace из ForgeApi.h Attach()
  const TABLE_NS = {
    AmodLauncherApi: "Launcher", AmodObjectsMenuApi: "Menu", AmodBoxMenuApi: "Box",
    AmodBlockExpressionApi: "Data", AmodConsoleApi: "Console", AmodRenderApi: "Render",
    AmodWorldApi: "World", AmodInteropApi: "Interop",
    AmodInputApi: "Input", AmodBonusesApi: "Bonuses", AmodInventoryApi: "Inventory",
    AmodHudApi: "Hud", AmodMonstersApi: "Monsters", AmodMmdApi: "Mmd",
    AmodTextureApi: "Textures", AmodSoundApi: "Sound", AmodPlayerApi: "Player",
    AmodCtrlsApi: "Ctrls", AmodCameraApi: "Camera", AmodParticlesApi: "Particles",
    AmodFilesApi: "Files", AmodConfigApi: "Config", AmodDatApi: "Scores",
    AmodSavApi: "Sav", AmodLevelEditorApi: "LevelEditor", AmodDemoApi: "Demo",
    AmodSpriteFontApi: "SpriteFont", AmodStageApi: "Stage", AmodAzmApi: "Azm",
    AmodShaderApi: "Shader", AmodApi: "Amod",
  };

  // заголовок каждой таблицы
  const tableHeader = {};
  for (const [h, hd] of Object.entries(HEADERS)) {
    for (const st of hd.structs) if (st.isTable) tableHeader[st.name] = h;
  }

  // группы сайдбара: id -> [ru, en]
  const GROUPS = {
    core:     ["Основное", "Core"],
    world:    ["Мир", "World"],
    graphics: ["Графика", "Graphics"],
    ui:       ["Интерфейс", "UI"],
    data:     ["Данные", "Data"],
    platform: ["Платформа", "Platform"],
    types:    ["Типы и форматы", "Types & formats"],
  };
  const GROUP_ORDER = ["core", "world", "graphics", "ui", "data", "platform", "types"];

  const TABLE_META = {
    AmodApi:            { t: ["AmodApi — корень", "AmodApi — root"],                  group: "core" },
    AmodWorldApi:       { t: ["World — уровень и комнаты", "World — level and rooms"], group: "world" },
    AmodCtrlsApi:       { t: ["Ctrls — лифты и двери", "Ctrls — lifts and doors"],     group: "world" },
    AmodPlayerApi:      { t: ["Player — игрок", "Player — the player"],                group: "world" },
    AmodCameraApi:      { t: ["Camera — камера", "Camera — camera"],                   group: "world" },
    AmodMonstersApi:    { t: ["Monsters — монстры", "Monsters — monsters"],            group: "world" },
    AmodBonusesApi:     { t: ["Bonuses — бонусы", "Bonuses — bonuses"],                group: "world" },
    AmodParticlesApi:   { t: ["Particles — частицы", "Particles — particles"],         group: "world" },
    AmodRenderApi:      { t: ["Render — кадр и 2D", "Render — frame and 2D"],          group: "graphics" },
    AmodMmdApi:         { t: ["Mmd — 3D-модели", "Mmd — 3D models"],                   group: "graphics" },
    AmodTextureApi:     { t: ["Textures — текстуры", "Textures — textures"],           group: "graphics" },
    AmodShaderApi:      { t: ["Shader — vs/ps 1.1", "Shader — vs/ps 1.1"],             group: "graphics" },
    AmodSpriteFontApi:  { t: ["SpriteFont — шрифты", "SpriteFont — fonts"],            group: "graphics" },
    AmodAzmApi:         { t: ["Azm — модели .azm", "Azm — .azm models"],               group: "graphics" },
    AmodLauncherApi:    { t: ["Launcher — стартовый диалог", "Launcher — start dialog"], group: "ui" },
    AmodObjectsMenuApi: { t: ["ObjectsMenu — меню $id", "ObjectsMenu — $id menus"],    group: "ui" },
    AmodBoxMenuApi:     { t: ["BoxMenu — боксовые меню", "BoxMenu — box menus"],       group: "ui" },
    AmodHudApi:         { t: ["Hud — экран игры", "Hud — game screen"],                group: "ui" },
    AmodConsoleApi:     { t: ["Console — консоль", "Console — console"],               group: "ui" },
    AmodFilesApi:       { t: ["Files — ресурсы", "Files — resources"],                 group: "data" },
    AmodConfigApi:      { t: ["Config — Azangara.cfg", "Config — Azangara.cfg"],       group: "data" },
    AmodDatApi:         { t: ["Dat — рекорды scores.dat", "Dat — scores.dat records"], group: "data" },
    AmodSavApi:         { t: ["Sav — сохранения", "Sav — saves"],                      group: "data" },
    AmodBlockExpressionApi: { t: ["BlockExpression — блоки", "BlockExpression — blocks"], group: "data" },
    AmodStageApi:       { t: ["Stage — захват кадра", "Stage — frame capture"],        group: "platform" },
    AmodInputApi:       { t: ["Input — клавиатура и мышь", "Input — keyboard and mouse"], group: "platform" },
    AmodDemoApi:        { t: ["Demo — демо-скрипты", "Demo — demo scripts"],           group: "platform" },
    AmodLevelEditorApi: { t: ["LevelEditor — редактор", "LevelEditor — editor"],       group: "platform" },
    AmodInteropApi:     { t: ["Interop — память и хуки", "Interop — memory and hooks"], group: "platform" },
  };

  const HDR_META = {
    "ForgeTypes.h": { t: ["ForgeTypes — базовые типы", "ForgeTypes — base types"], group: "types" },
    "AzmFile.h":    { t: ["AzmFile — формат .azm", "AzmFile — the .azm format"],   group: "types" },
  };

  // ---- сборка реестра подсистем ----
  function buildRegistry() {
    const list = [];
    for (const [table, meta] of Object.entries(TABLE_META)) {
      const h = tableHeader[table];
      const hd = HEADERS[h];
      const fns = [];
      let structSize = null;
      for (const st of hd.structs) if (st.isTable && st.name === table) {
        for (const f of st.fields) {
          if (f.kind === "fn") fns.push(f);
          else if (f.name === "structSize") structSize = f;
        }
      }
      list.push({
        id: table, header: h, ns: TABLE_NS[table] || "", title: T(meta.t[0], meta.t[1]),
        group: meta.group, functions: fns, structSize, last: (hd.last_fields || {})[table] || null,
        enums: hd.enums, structs: hd.structs.filter(s => !s.isTable),
        callbacks: hd.callbacks, defines: hd.defines, typedefs: hd.typedefs,
        summary: hd.summary || "", summaryRu: W.UTIL.desc("hdr:" + h),
      });
    }
    for (const [h, meta] of Object.entries(HDR_META)) {
      const hd = HEADERS[h];
      list.push({
        id: "hdr:" + h, header: h, ns: "", title: T(meta.t[0], meta.t[1]), group: meta.group,
        functions: [], structSize: null, last: null,
        enums: hd.enums, structs: hd.structs, callbacks: hd.callbacks,
        defines: hd.defines, typedefs: hd.typedefs,
        summary: hd.summary || "", summaryRu: W.UTIL.desc("hdr:" + h),
      });
    }
    return list;
  }

  W.UTIL.REGISTRY = buildRegistry();
  W.UTIL.byId = (id) => W.UTIL.REGISTRY.find(s => s.id === id);

  // группы в фиксированном порядке
  W.UTIL.groups = () => {
    const g = {};
    for (const s of W.UTIL.REGISTRY) (g[s.group] = g[s.group] || []).push(s);
    return GROUP_ORDER.filter(k => g[k]).map(k => [T(GROUPS[k][0], GROUPS[k][1]), g[k]]);
  };

  W.UTIL.fnCount = W.UTIL.REGISTRY.reduce((a, s) => a + s.functions.length, 0);

  // ---- описания вызовов ----
  W.UTIL.fnRu = (table, name) => W.UTIL.desc(table + "." + name);
  W.UTIL.cbRu = (name) => W.UTIL.desc("cb:" + name);
  W.UTIL.structRu = (name) => W.UTIL.desc("struct:" + name);
  W.UTIL.enumRu = (name) => W.UTIL.desc("enum:" + name);
  W.UTIL.memberRu = (name) => W.UTIL.desc("member:" + name);
  W.UTIL.defRu = (name) => W.UTIL.desc("def:" + name);

  // ---- подсветка синтаксиса (C-подобный) ----
  const C_KEYWORDS = /^(typedef|struct|enum|union|const|static|inline|extern|void|int|float|double|char|long|short|unsigned|signed|bool|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|uintptr_t|intptr_t|size_t|__cdecl|__stdcall|__declspec|dllexport|return|if|else|for|while|switch|case|break|continue|sizeof|static_assert|nullptr|NULL|true|false)$/;
  const C_TYPES = /^(Amod\w*|D3D\w*|IDirect3D\w*|HRESULT|DWORD|WORD|BYTE|HANDLE|HWND|LPCWSTR|LPVOID|HMODULE|FILE|AmodMod|AmodToken)$/;

  function hlC(code) {
    let out = "";
    let i = 0;
    const n = code.length;
    let buf = "";
    const flushPlain = () => {
      if (!buf) return;
      let b = esc(buf);
      const ents = [];
      b = b.replace(/&(amp|quot|lt|gt|#39|#x27);/g, (m) => { ents.push(m); return "\x03"; });
      b = b.replace(/[A-Za-z_][A-Za-z0-9_]*/g, (w) => {
        if (C_KEYWORDS.test(w)) return '<span class="tok-kw">' + w + "</span>";
        if (C_TYPES.test(w)) return '<span class="tok-type">' + w + "</span>";
        if (/^AMOD_|^AZM_|^IDC_|^BN_/.test(w)) return '<span class="tok-tag">' + w + "</span>";
        return w;
      });
      b = b.replace(/\b(0x[0-9a-fA-F]+|\d+\.?\d*f?)\b/g, '<span class="tok-num">$1</span>');
      b = b.replace(/\x03/g, () => ents.shift());
      out += b;
      buf = "";
    };
    while (i < n) {
      const two = code.substr(i, 2);
      if (two === "/*") {
        flushPlain();
        let j = code.indexOf("*/", i + 2);
        j = j === -1 ? n : j + 2;
        out += '<span class="tok-com">' + esc(code.slice(i, j)) + "</span>";
        i = j;
        continue;
      }
      if (two === "//") {
        flushPlain();
        let j = code.indexOf("\n", i);
        j = j === -1 ? n : j;
        out += '<span class="tok-com">' + esc(code.slice(i, j)) + "</span>";
        i = j;
        continue;
      }
      const c = code[i];
      if (c === '"' || c === "'") {
        flushPlain();
        let j = i + 1;
        while (j < n && code[j] !== c) { if (code[j] === "\\") j++; j++; }
        out += '<span class="tok-str">' + esc(code.slice(i, Math.min(j + 1, n))) + "</span>";
        i = j + 1;
        continue;
      }
      buf += c;
      i++;
    }
    flushPlain();
    return out;
  }
  W.UTIL.hlC = hlC;

  function hlJson(code) {
    let s = esc(code);
    const strings = [];
    const enc = (v) => {
      let out = "";
      do { out += String.fromCharCode(97 + v % 26); v = Math.floor(v / 26); } while (v > 0);
      return out;
    };
    s = s.replace(/&quot;[^&]*?&quot;/g, (m) => "\x01" + enc(strings.push(m) - 1) + "\x02");
    s = s.replace(/\b(-?\d+\.?\d*)\b/g, '<span class="tok-num">$1</span>');
    s = s.replace(/\b(true|false|null)\b/g, '<span class="tok-kw">$1</span>');
    s = s.replace(/\x01([a-z]+)\x02/g, (_, e) => {
      let v = 0;
      for (let i = e.length - 1; i >= 0; i--) v = v * 26 + (e.charCodeAt(i) - 97);
      return '<span class="tok-str">' + strings[v] + "</span>";
    });
    s = s.replace(/(<span class="tok-str">(&quot;[^&]*&quot;)<\/span>)(\s*:)/g, '<span class="tok-tag">$2</span>$3');
    return s;
  }
  W.UTIL.hlJson = hlJson;

  function hlShell(code) {
    return esc(code)
      .replace(/(^|\n)(\s*)(amodpack|msbuild|copy|cd|AzmPack)(\s)/g, '$1$2<span class="tok-tag">$3</span>$4')
      .replace(/(--?[a-zA-Z-]+)(\s)/g, '<span class="tok-type">$1</span>$2');
  }
  W.UTIL.hlShell = hlShell;

  const COPY_ICON = '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><use href="#i-copy"/></svg>';
  const CHECK_ICON = '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><use href="#i-check"/></svg>';

  // ---- блок кода с подписью и копированием ----
  W.UTIL.codeBlock = (code, label, lang) => {
    const body = lang === "json" ? hlJson(code) : lang === "sh" ? hlShell(code) : hlC(code);
    return '<div class="codeblk">' +
      (label ? '<div class="cb-head"><span>' + esc(label) + '</span><button class="copybtn" type="button" data-copy>' +
        COPY_ICON + "<span>" + T("Копировать", "Copy") + "</span></button></div>" : "") +
      "<pre>" + body + "</pre></div>";
  };

  // ---- сигнатура функции из таблицы ----
  W.UTIL.sigHtml = (fn) => {
    const args = fn.args.map(a =>
      a.variadic ? "..." :
      '<span class="tok-type">' + esc(a.type) + '</span> <span class="tok-fn">' + esc(a.name) + "</span>"
    ).join(", ");
    return '<span class="tok-type">' + esc(fn.ret) + "</span> " +
      '<span class="tok-punc">(__cdecl*</span><span class="fn-name">' + esc(fn.name) +
      '</span><span class="tok-punc">)</span><span class="tok-punc">(</span>' + args + '<span class="tok-punc">);</span>';
  };

  // ---- C++ обёртка ----
  W.UTIL.wrapperCall = (sys, fn) => {
    if (!sys.ns) return "";
    const args = fn.args.filter(a => a.type !== "AmodMod").map(a => a.variadic ? "..." : a.name).join(", ");
    return "Amod::" + sys.ns + "::" + fn.name + "(" + args + ")";
  };

  W.UTIL.toast = (() => {
    let t = null;
    return (msg) => {
      const el = document.getElementById("toast");
      const txt = document.getElementById("toastText");
      if (!el) return;
      if (txt) txt.textContent = msg;
      else el.textContent = msg;
      el.classList.add("show");
      clearTimeout(t);
      t = setTimeout(() => el.classList.remove("show"), 1900);
    };
  })();

  function copyRawText(text, done) {
    function fallback() {
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.cssText = "position:fixed;opacity:0";
      document.body.appendChild(ta);
      ta.select();
      try { document.execCommand("copy"); done(); } catch (e) {}
      document.body.removeChild(ta);
    }
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(done, fallback);
    } else {
      fallback();
    }
  }

  // копирование
  document.addEventListener("click", (e) => {
    const btn = e.target.closest("[data-copy]");
    if (!btn) return;
    const pre = btn.closest(".codeblk").querySelector("pre");
    copyRawText(pre.textContent, () => {
      btn.classList.add("copied");
      btn.innerHTML = CHECK_ICON + "<span>" + T("Скопировано", "Copied") + "</span>";
      W.UTIL.toast(T("Скопировано в буфер обмена", "Copied to clipboard"));
      setTimeout(() => {
        btn.classList.remove("copied");
        btn.innerHTML = COPY_ICON + "<span>" + T("Копировать", "Copy") + "</span>";
      }, 1500);
    });
  });

})();
