// Роутер, сайдбар, поиск, тема. Язык — window.AF.
(function () {
  "use strict";
  const U = window.UTIL;
  const esc = U.esc;
  const T = window.AF.t;

  const $ = (s) => document.querySelector(s);
  const content = () => document.getElementById("content");

  // ---------- навигация и сайдбар ----------
  const GUIDE_LINKS = [
    ["#/overview", "Обзор", "Overview", "overview"],
    ["#/quickstart", "Быстрый старт", "Quick start", "quickstart"],
    ["#/package", "Мод-пакет и манифест", "Mod package & manifest", "package"],
    ["#/apimodel", "Модель API", "API model", "apimodel"],
    ["#/reference", "Справочник API", "API reference", "reference"],
  ];

  function sidebarGuide(active) {
    let h = '<div class="side-group"><div class="side-label">' + T("Документация", "Documentation") + "</div>";
    h += GUIDE_LINKS.map(([href, ru, en, id]) =>
      '<a class="side-link' + (id === active ? " active" : "") + '" href="' + href + '">' + T(ru, en) + "</a>").join("");
    h += "</div>";
    return h;
  }

  function sidebarRef(activeId) {
    let h = '<a class="side-back" href="#/reference">' + T("← Все подсистемы", "← All subsystems") + "</a>";
    for (const [group, items] of U.groups()) {
      h += '<div class="side-group"><div class="side-label">' + group + "</div>";
      h += items.map(s =>
        '<a class="side-link sub' + (s.id === activeId ? " active" : "") + '" href="#/ref/' + s.id + '" title="' + esc(s.title) + '">' +
        (String(s.id).startsWith("hdr:") ? esc(s.id.slice(4)) : esc(s.id)) +
        (s.functions.length ? '<span class="cnt">' + s.functions.length + "</span>" : "") + "</a>").join("");
      h += "</div>";
    }
    return h;
  }

  function renderSidebar(active, mode) {
    document.getElementById("sidebarContent").innerHTML =
      mode === "ref" ? sidebarRef(active) : sidebarGuide(active);
  }

  // ---------- роутер ----------
  function route() {
    // "#/ref/AmodStageApi#fn-TakeUpdate" -> маршрут + якорь карточки
    const raw = location.hash.replace(/^#\/?/, "");
    const anchorIdx = raw.indexOf("#");
    const hash = anchorIdx === -1 ? raw : raw.slice(0, anchorIdx);
    const [head, arg] = hash.split("/");
    let html = "", sidebarActive = head, mode = "guide";

    if (head === "ref" && arg) {
      const sys = U.byId(arg);
      if (sys) {
        html = window.REF.refPage(sys);
        sidebarActive = arg; mode = "ref";
      } else {
        html = '<div class="page-head"><h1 class="page-title">' + T("Подсистема не найдена", "Subsystem not found") +
          '</h1><p class="page-sub"><a href="#/reference">' + T("К списку подсистем", "Back to the subsystem list") + "</a></p></div>";
      }
    } else if (head === "reference") {
      html = window.REF.refIndex();
    } else if (window.GUIDES[head]) {
      html = window.GUIDES[head]();
    } else {
      html = window.GUIDES.overview();
      sidebarActive = "overview";
    }

    content().innerHTML = html;
    renderSidebar(sidebarActive, mode);
    document.querySelectorAll(".topnav a").forEach(a =>
      a.classList.toggle("active", a.dataset.nav === sidebarActive));
    document.body.classList.remove("side-open");
    window.scrollTo(0, 0);
    highlightAnchor();
  }

  function highlightAnchor() {
    if (!location.hash.includes("#fn-")) return;
    const id = location.hash.split("#fn-")[1];
    const el = document.getElementById("fn-" + id);
    if (el) {
      el.classList.add("flash");
      el.scrollIntoView({ block: "start" });
      setTimeout(() => el.classList.remove("flash"), 2000);
    }
  }

  // ---------- тема ----------
  const savedTheme = localStorage.getItem("af-theme");
  if (savedTheme) document.body.dataset.theme = savedTheme;
  document.getElementById("themeBtn").addEventListener("click", () => {
    const next = document.body.dataset.theme === "dark" ? "light" : "dark";
    document.body.dataset.theme = next;
    localStorage.setItem("af-theme", next);
  });

  // ---------- мобильный сайдбар ----------
  document.getElementById("burger").addEventListener("click", () =>
    document.body.classList.toggle("side-open"));
  document.getElementById("sidebarBackdrop").addEventListener("click", () =>
    document.body.classList.remove("side-open"));

  // ---------- поиск ----------
  const modal = document.getElementById("searchModal");
  const input = document.getElementById("searchInput");
  const results = document.getElementById("searchResults");
  const countEl = document.getElementById("searchCount");
  let INDEX = null, sel = 0, flat = [];

  function buildIndex() {
    if (INDEX) return INDEX;
    INDEX = [];
    GUIDE_LINKS.forEach(([href, ru, en]) => INDEX.push({
      kind: T("Раздел", "Section"), name: T(ru, en), desc: T("раздел документации", "documentation section"),
      href, low: (T(ru, en) + " " + href).toLowerCase(),
    }));

    for (const s of U.REGISTRY) {
      INDEX.push({
        kind: T("Подсистема", "Subsystem"), name: s.id, desc: s.summaryRu || s.title,
        href: "#/ref/" + s.id, low: (s.id + " " + s.title).toLowerCase(),
      });
      for (const f of s.functions) {
        const d = U.fnRu(s.id, f.name);
        INDEX.push({
          kind: T("Функция", "Function"), name: s.id + "." + f.name, desc: d || f.comment || "",
          href: "#/ref/" + s.id + "#fn-" + f.name, low: (f.name + " " + s.id + " " + d + " " + f.comment).toLowerCase(),
        });
      }
      for (const cb of s.callbacks)
        INDEX.push({ kind: T("Колбэк", "Callback"), name: cb.name, desc: U.cbRu(cb.name) || cb.comment || "", href: "#/ref/" + s.id, low: (cb.name + " callback").toLowerCase() });
      for (const en of s.enums) {
        INDEX.push({ kind: "Enum", name: en.name, desc: U.enumRu(en.name) || en.comment || "", href: "#/ref/" + s.id, low: en.name.toLowerCase() });
        for (const m of en.members)
          INDEX.push({ kind: T("Константа", "Constant"), name: m.name, desc: U.memberRu(m.name) || m.comment || "", href: "#/ref/" + s.id, low: m.name.toLowerCase() });
      }
      for (const st of s.structs)
        INDEX.push({ kind: T("Структура", "Structure"), name: st.name, desc: U.structRu(st.name) || st.comment || "", href: "#/ref/" + s.id, low: st.name.toLowerCase() });
      for (const d of s.defines)
        INDEX.push({ kind: "define", name: d.name, desc: U.defRu(d.name) || d.comment || "", href: "#/ref/" + s.id, low: d.name.toLowerCase() });
    }
    return INDEX;
  }

  function mark(name, q) {
    if (!q) return esc(name);
    const i = name.toLowerCase().indexOf(q.toLowerCase());
    if (i < 0) return esc(name);
    return esc(name.slice(0, i)) + "<mark>" + esc(name.slice(i, i + q.length)) + "</mark>" + esc(name.slice(i + q.length));
  }

  function runSearch() {
    const q = input.value.trim();
    const idx = buildIndex();
    let hits;
    if (!q) {
      hits = idx.filter(e => e.kind === T("Раздел", "Section") || e.kind === T("Подсистема", "Subsystem"));
    } else {
      const ql = q.toLowerCase();
      const starts = [], has = [];
      for (const e of idx) {
        if (e.low.startsWith(ql)) starts.push(e);
        else if (e.name.toLowerCase().includes(ql) || (e.desc && e.desc.toLowerCase().includes(ql))) has.push(e);
        if (starts.length > 60) break;
      }
      hits = starts.concat(has).slice(0, 60);
    }
    flat = hits;
    sel = Math.min(sel, Math.max(0, flat.length - 1));
    if (!hits.length) {
      results.innerHTML = '<div class="sr-empty">' + T("Ничего не найдено", "Nothing found") + "</div>";
      countEl.textContent = "";
      return;
    }
    let html = "", lastKind = null;
    hits.forEach((e, i) => {
      if (e.kind !== lastKind) { html += '<div class="sr-group">' + e.kind + "</div>"; lastKind = e.kind; }
      html += '<div class="sr-item' + (i === sel ? " sel" : "") + '" data-i="' + i + '">' +
        '<span class="nm">' + mark(e.name, q) + '</span><span class="ds">' + esc(e.desc.slice(0, 90)) + "</span></div>";
    });
    results.innerHTML = html;
    countEl.textContent = hits.length + " " + T("совпадений", "matches");
    const selEl = results.querySelector(".sr-item.sel");
    if (selEl) selEl.scrollIntoView({ block: "nearest" });
  }

  function go(i) {
    const e = flat[i];
    if (!e) return;
    closeSearch();
    location.hash = e.href;
    setTimeout(highlightAnchor, 60);
  }

  function openSearch() {
    modal.hidden = false;
    input.value = ""; sel = 0;
    runSearch();
    input.focus();
  }
  function closeSearch() { modal.hidden = true; }

  document.getElementById("searchBtn").addEventListener("click", openSearch);
  modal.addEventListener("click", (e) => { if (e.target === modal) closeSearch(); });
  input.addEventListener("input", () => { sel = 0; runSearch(); });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown") { e.preventDefault(); sel = Math.min(sel + 1, flat.length - 1); runSearch(); }
    else if (e.key === "ArrowUp") { e.preventDefault(); sel = Math.max(sel - 1, 0); runSearch(); }
    else if (e.key === "Enter") { e.preventDefault(); go(sel); }
  });
  results.addEventListener("click", (e) => {
    const item = e.target.closest(".sr-item");
    if (item) go(parseInt(item.dataset.i, 10));
  });
  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "k") { e.preventDefault(); openSearch(); }
    if (e.key === "Escape" && !modal.hidden) closeSearch();
  });

  // ---------- локализованные статические строки ----------
  document.querySelector(".searchbtn span").textContent = T("Поиск…", "Search…");
  input.setAttribute("placeholder", T("Функция, тип, константа…", "Function, type, constant…"));
  // в топбаре — короткие подписи (длинные живут в сайдбаре)
  const TOP_LABELS = {
    overview: ["Обзор", "Overview"], quickstart: ["Быстрый старт", "Quick start"],
    package: ["Мод-пакет", "Mod package"], apimodel: ["Модель API", "API model"],
    reference: ["Справочник", "Reference"],
  };
  document.querySelectorAll("#topnav a").forEach(a => {
    const m = TOP_LABELS[a.dataset.nav];
    if (m) a.textContent = T(m[0], m[1]);
  });

  // ---------- запуск ----------
  document.getElementById("footVer").textContent = U.DATA.meta.loaderVersion;
  document.getElementById("footApi").textContent = U.DATA.meta.apiVersion;
  window.addEventListener("hashchange", route);
  route();
})();
