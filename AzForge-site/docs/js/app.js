// Роутер, сайдбар, фильтр подсистем, поиск (Ctrl+K), тема, курсорный прожектор.
(function () {
  "use strict";
  const U = window.UTIL;
  const esc = U.esc;
  const T = window.AF.t;

  const $ = (s) => document.querySelector(s);
  const content = () => document.getElementById("content");

  let subFilterValue = "";

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
      '<a class="side-link' + (id === active ? " active" : "") + '" href="' + href + '"><span>' + T(ru, en) + "</span></a>").join("");
    h += "</div>";

    const secs = Array.from(content().querySelectorAll("h2.sec[id]"));
    if (secs.length) {
      h += '<div class="side-group"><div class="side-label">' + T("На этой странице", "On this page") + "</div>";
      h += secs.map(sec =>
        '<a class="side-link" data-jump="' + esc(sec.id) + '" href="javascript:void(0)"><span>' + esc(sec.textContent) + "</span></a>"
      ).join("");
      h += "</div>";
    }
    return h;
  }

  function renderRefGroups(activeId, filterQuery) {
    const q = (filterQuery || "").trim().toLowerCase();
    let html = "";
    for (const [group, items] of U.groups()) {
      const matched = q
        ? items.filter(s => s.id.toLowerCase().includes(q) || s.title.toLowerCase().includes(q) || (s.ns && s.ns.toLowerCase().includes(q)))
        : items;
      if (!matched.length) continue;
      html += '<div class="side-group"><div class="side-label">' + esc(group) + "</div>";
      html += matched.map(s =>
        '<a class="side-link sub' + (s.id === activeId ? " active" : "") + '" href="#/ref/' + s.id + '" title="' + esc(s.title) + '">' +
        "<span>" + (String(s.id).startsWith("hdr:") ? esc(s.id.slice(4)) : esc(s.id)) + "</span>" +
        (s.functions.length ? '<span class="cnt">' + s.functions.length + "</span>" : "") + "</a>"
      ).join("");
      html += "</div>";
    }
    if (!html) {
      html = '<div class="sr-empty" style="padding:18px 8px">' + T("Ничего не найдено", "Nothing found") + "</div>";
    }
    return html;
  }

  function sidebarRef(activeId) {
    let h = "";
    if (activeId && activeId !== "reference") {
      h += '<a class="side-back" href="#/reference">' + T("← Все подсистемы", "← All subsystems") + "</a>";
    }
    h += '<div class="side-filter-wrap">' +
      '<input class="side-filter" id="sideSubFilter" type="text" value="' + esc(subFilterValue) + '" placeholder="' +
      T("Фильтр подсистем…", "Filter subsystems…") + '" autocomplete="off" spellcheck="false">' +
      "</div>";
    h += '<div id="sideRefGroups">' + renderRefGroups(activeId, subFilterValue) + "</div>";
    return h;
  }

  function renderSidebar(active, mode) {
    const host = document.getElementById("sidebarContent");
    host.innerHTML = mode === "ref" ? sidebarRef(active) : sidebarGuide(active);

    const filterEl = document.getElementById("sideSubFilter");
    if (filterEl) {
      filterEl.addEventListener("input", () => {
        subFilterValue = filterEl.value;
        const groupsHost = document.getElementById("sideRefGroups");
        if (groupsHost) groupsHost.innerHTML = renderRefGroups(active, subFilterValue);
      });
    }

    host.querySelectorAll("[data-jump]").forEach(link => {
      link.addEventListener("click", (e) => {
        e.preventDefault();
        const target = document.getElementById(link.getAttribute("data-jump"));
        document.body.classList.remove("side-open");
        if (target) target.scrollIntoView({ behavior: "smooth", block: "start" });
      });
    });
  }

  // ---------- роутер ----------
  function route() {
    const raw = location.hash.replace(/^#\/?/, "");
    const anchorIdx = raw.indexOf("#");
    const hash = anchorIdx === -1 ? raw : raw.slice(0, anchorIdx);
    const [head, arg] = hash.split("/");
    let html = "", sidebarActive = head, topActive = head, mode = "guide";

    if (head === "ref" && arg) {
      const sys = U.byId(arg);
      if (sys) {
        html = window.REF.refPage(sys);
        sidebarActive = arg;
        topActive = "reference";
        mode = "ref";
      } else {
        html = '<div class="page-head az-glass-card"><div class="doc-card-inner"><h1 class="page-title">' +
          T("Подсистема не найдена", "Subsystem not found") +
          '</h1><p class="page-sub"><a href="#/reference">' +
          T("К списку подсистем", "Back to the subsystem list") + "</a></p></div></div>";
        topActive = "reference";
        mode = "ref";
      }
    } else if (head === "reference") {
      html = window.REF.refIndex();
      sidebarActive = "reference";
      topActive = "reference";
      mode = "ref";
    } else if (window.GUIDES[head]) {
      html = window.GUIDES[head]();
    } else {
      html = window.GUIDES.overview();
      sidebarActive = "overview";
      topActive = "overview";
    }

    content().innerHTML = html;
    renderSidebar(sidebarActive, mode);
    document.querySelectorAll("#topnav a, #mobileTabs a").forEach(a =>
      a.classList.toggle("active", a.dataset.nav === topActive));
    document.body.classList.remove("side-open");
    if (!location.hash.includes("#fn-")) window.scrollTo(0, 0);
    highlightAnchor();
  }

  function highlightAnchor() {
    if (!location.hash.includes("#fn-")) return;
    const id = location.hash.split("#fn-")[1];
    const el = document.getElementById("fn-" + id);
    if (el) {
      el.classList.add("flash");
      el.scrollIntoView({ behavior: "smooth", block: "start" });
      setTimeout(() => el.classList.remove("flash"), 2200);
    }
  }

  // ---------- тема ----------
  function syncThemeMeta(theme) {
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute("content", theme === "light" ? "#f6f8fc" : "#080c15");
  }
  try {
    const savedTheme = localStorage.getItem("af-theme");
    if (savedTheme === "light" || savedTheme === "dark") {
      document.body.dataset.theme = savedTheme;
      syncThemeMeta(savedTheme);
    }
  } catch (e) {}
  document.getElementById("themeBtn").addEventListener("click", () => {
    const next = document.body.dataset.theme === "dark" ? "light" : "dark";
    document.body.dataset.theme = next;
    syncThemeMeta(next);
    try { localStorage.setItem("af-theme", next); } catch (e) {}
  });

  // ---------- курсорный прожектор ----------
  const spotBtn = document.getElementById("spotBtn");
  const bg = document.querySelector(".az-bg");

  function syncSpotBtn() {
    if (!spotBtn) return;
    const on = document.body.getAttribute("data-spotlight") !== "off";
    const label = on
      ? T("Выключить подсветку за курсором", "Disable cursor spotlight")
      : T("Включить подсветку за курсором", "Enable cursor spotlight");
    spotBtn.setAttribute("title", label);
    spotBtn.setAttribute("aria-label", label);
    spotBtn.setAttribute("aria-pressed", on ? "true" : "false");
  }
  try {
    if (localStorage.getItem("af-spotlight") === "0") {
      document.body.setAttribute("data-spotlight", "off");
    }
  } catch (e) {}
  syncSpotBtn();

  if (spotBtn) {
    spotBtn.addEventListener("click", () => {
      const next = document.body.getAttribute("data-spotlight") === "off" ? "on" : "off";
      document.body.setAttribute("data-spotlight", next);
      try { localStorage.setItem("af-spotlight", next === "on" ? "1" : "0"); } catch (e) {}
      syncSpotBtn();
    });
  }

  window.addEventListener("pointermove", (e) => {
    if (document.body.getAttribute("data-spotlight") === "off") return;
    if (bg) {
      bg.style.setProperty("--gx", e.clientX + "px");
      bg.style.setProperty("--gy", e.clientY + "px");
    }
    const card = e.target && e.target.closest ? e.target.closest(".az-glass-card") : null;
    if (card) {
      const r = card.getBoundingClientRect();
      card.style.setProperty("--mx", (e.clientX - r.left) + "px");
      card.style.setProperty("--my", (e.clientY - r.top) + "px");
    }
  }, { passive: true });

  // ---------- мобильный сайдбар ----------
  const toggleSide = () => document.body.classList.toggle("side-open");
  document.getElementById("burger").addEventListener("click", toggleSide);
  const mSideBtn = document.getElementById("mobileSideBtn");
  if (mSideBtn) mSideBtn.addEventListener("click", toggleSide);
  document.getElementById("sidebarBackdrop").addEventListener("click", () =>
    document.body.classList.remove("side-open"));

  // ---------- поиск (Ctrl+K) ----------
  const modal = document.getElementById("searchModal");
  const input = document.getElementById("searchInput");
  const results = document.getElementById("searchResults");
  const countEl = document.getElementById("searchCount");
  let INDEX = null, sel = 0, flat = [];

  function buildIndex() {
    if (INDEX) return INDEX;
    INDEX = [];
    GUIDE_LINKS.forEach(([href, ru, en]) => INDEX.push({
      kind: T("Раздел", "Section"), badge: "GUIDE", name: T(ru, en), desc: T("раздел документации", "documentation section"),
      href, low: (T(ru, en) + " " + href).toLowerCase(),
    }));

    for (const s of U.REGISTRY) {
      INDEX.push({
        kind: T("Подсистема", "Subsystem"), badge: "API", name: s.id, desc: s.summaryRu || s.title,
        href: "#/ref/" + s.id, low: (s.id + " " + s.title + " " + (s.ns || "")).toLowerCase(),
      });
      for (const f of s.functions) {
        const d = U.fnRu(s.id, f.name);
        INDEX.push({
          kind: T("Функция", "Function"), badge: "FN", name: s.id + "." + f.name, desc: d || f.comment || "",
          href: "#/ref/" + s.id + "#fn-" + f.name, low: (f.name + " " + s.id + " " + d + " " + f.comment).toLowerCase(),
        });
      }
      for (const cb of s.callbacks)
        INDEX.push({ kind: T("Колбэк", "Callback"), badge: "CB", name: cb.name, desc: U.cbRu(cb.name) || cb.comment || "", href: "#/ref/" + s.id, low: (cb.name + " callback").toLowerCase() });
      for (const en of s.enums) {
        INDEX.push({ kind: "Enum", badge: "ENUM", name: en.name, desc: U.enumRu(en.name) || en.comment || "", href: "#/ref/" + s.id, low: en.name.toLowerCase() });
        for (const m of en.members)
          INDEX.push({ kind: T("Константа", "Constant"), badge: "CONST", name: m.name, desc: U.memberRu(m.name) || m.comment || "", href: "#/ref/" + s.id, low: m.name.toLowerCase() });
      }
      for (const st of s.structs)
        INDEX.push({ kind: T("Структура", "Structure"), badge: "STRUCT", name: st.name, desc: U.structRu(st.name) || st.comment || "", href: "#/ref/" + s.id, low: st.name.toLowerCase() });
      for (const d of s.defines)
        INDEX.push({ kind: "define", badge: "DEF", name: d.name, desc: U.defRu(d.name) || d.comment || "", href: "#/ref/" + s.id, low: d.name.toLowerCase() });
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
      hits = idx.filter(e => e.badge === "GUIDE" || e.badge === "API");
    } else {
      const ql = q.toLowerCase();
      const starts = [], has = [];
      for (const e of idx) {
        if (e.low.startsWith(ql) || e.name.toLowerCase().startsWith(ql)) starts.push(e);
        else if (e.name.toLowerCase().includes(ql) || (e.desc && e.desc.toLowerCase().includes(ql))) has.push(e);
        if (starts.length > 60) break;
      }
      hits = starts.concat(has).slice(0, 60);
    }
    flat = hits;
    sel = Math.min(sel, Math.max(0, flat.length - 1));
    if (!hits.length) {
      results.innerHTML = '<div class="sr-empty">' + T("Ничего не найдено", "Nothing found") + "</div>";
      countEl.textContent = "0";
      return;
    }
    let html = "", lastKind = null;
    hits.forEach((e, i) => {
      if (e.kind !== lastKind) { html += '<div class="sr-group">' + esc(e.kind) + "</div>"; lastKind = e.kind; }
      html += '<div class="sr-item' + (i === sel ? " sel" : "") + '" data-i="' + i + '">' +
        '<div class="sr-main">' +
          '<span class="nm">' + mark(e.name, q) + "</span>" +
          '<span class="ds">' + esc((e.desc || "").slice(0, 110)) + "</span>" +
        "</div>" +
        '<div class="sr-right">' +
          '<span class="sr-badge' + (e.badge === "FN" ? " fn" : "") + '">' + esc(e.badge) + "</span>" +
          '<svg class="sr-enter"><use href="#i-enter"/></svg>' +
        "</div></div>";
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
    input.value = "";
    sel = 0;
    runSearch();
    setTimeout(() => input.focus(), 20);
  }
  function closeSearch() { modal.hidden = true; }

  document.getElementById("searchBtn").addEventListener("click", openSearch);
  const closeBtn = document.getElementById("searchCloseBtn");
  if (closeBtn) closeBtn.addEventListener("click", closeSearch);
  modal.addEventListener("click", (e) => { if (e.target === modal) closeSearch(); });
  input.addEventListener("input", () => { sel = 0; runSearch(); });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown") { e.preventDefault(); sel = Math.min(sel + 1, Math.max(0, flat.length - 1)); runSearch(); }
    else if (e.key === "ArrowUp") { e.preventDefault(); sel = Math.max(sel - 1, 0); runSearch(); }
    else if (e.key === "Enter") { e.preventDefault(); go(sel); }
  });
  results.addEventListener("click", (e) => {
    const item = e.target.closest(".sr-item");
    if (item) go(parseInt(item.dataset.i, 10));
  });
  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "k") {
      e.preventDefault();
      if (modal.hidden) openSearch(); else closeSearch();
    }
    if (e.key === "Escape" && !modal.hidden) closeSearch();
  });

  // ---------- локализованные статические строки ----------
  $(".searchbtn span").textContent = T("Поиск…", "Search…");
  $("#searchBtn").setAttribute("title", T("Поиск (Ctrl+K)", "Search (Ctrl+K)"));
  input.setAttribute("placeholder", T("Функция, тип, константа…", "Function, type, constant…"));
  const modalBox = modal.querySelector(".modal");
  if (modalBox) modalBox.setAttribute("aria-label", T("Поиск по документации", "Search documentation"));
  const backLbl = document.getElementById("backHomeLabel");
  if (backLbl) backLbl.textContent = T("На главную", "Back to Home");
  const mSideLbl = document.getElementById("mobileSideLabel");
  if (mSideLbl) mSideLbl.textContent = T("Навигация", "Sidebar");
  const keysHint = document.getElementById("searchKeysHint");
  if (keysHint) keysHint.textContent = T("↑↓ — выбор · Enter — перейти · Esc — закрыть", "↑↓ navigate · Enter open · Esc close");

  const TOP_LABELS = {
    overview: ["Обзор", "Overview"],
    quickstart: ["Быстрый старт", "Quick start"],
    package: ["Мод-пакет", "Mod package"],
    apimodel: ["Модель API", "API model"],
    reference: ["Справочник", "Reference"],
  };
  document.querySelectorAll("#topnav a, #mobileTabs a").forEach(a => {
    const m = TOP_LABELS[a.dataset.nav];
    if (m) a.textContent = T(m[0], m[1]);
  });

  // ---------- запуск ----------
  document.getElementById("footVer").textContent = U.DATA.meta.loaderVersion;
  document.getElementById("footApi").textContent = U.DATA.meta.apiVersion;
  const fc = document.getElementById("footCounts");
  if (fc) fc.textContent = "31 " + T("подсистема", "subsystems") + " · " + U.fnCount + " " + T("функции", "functions");

  window.addEventListener("hashchange", route);
  route();
})();
