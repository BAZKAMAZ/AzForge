(function () {
  "use strict";

  var LANGS = [
    { id: "ru", label: "Русский" },
    { id: "en", label: "English" }
  ];

  function savedLang() {
    try { var v = localStorage.getItem("af-lang"); } catch (e) { v = null; }
    for (var i = 0; i < LANGS.length; i++) if (LANGS[i].id === v) return v;
    return "ru";
  }

  var AF = window.AF = {
    lang: savedLang(),
    langs: LANGS,
    /* парный перевод прямо в месте использования */
    t: function (ru, en) { return AF.lang === "en" ? en : ru; }
  };

  function dict() {
    return AF.lang === "en" ? (window.AF_LANG_EN || {}) : (window.AF_LANG_RU || {});
  }

  /* применить словарь к статическим узлам страницы */
  AF.apply = function () {
    var d = dict(), k, i, el, nodes;

    nodes = document.querySelectorAll("[data-i18n]");
    for (i = 0; i < nodes.length; i++) {
      el = nodes[i]; k = el.getAttribute("data-i18n");
      if (typeof d[k] === "string") el.innerHTML = d[k];
    }
    nodes = document.querySelectorAll("[data-i18n-ph]");
    for (i = 0; i < nodes.length; i++) {
      el = nodes[i]; k = el.getAttribute("data-i18n-ph");
      if (typeof d[k] === "string") el.setAttribute("placeholder", d[k]);
    }
    nodes = document.querySelectorAll("[data-i18n-title]");
    for (i = 0; i < nodes.length; i++) {
      el = nodes[i]; k = el.getAttribute("data-i18n-title");
      if (typeof d[k] === "string") { el.setAttribute("title", d[k]); el.setAttribute("aria-label", d[k]); }
    }
    document.documentElement.setAttribute("lang", AF.lang);
    var titleKey = document.body.getAttribute("data-title-key") || "meta.title";
    if (typeof d[titleKey] === "string") document.title = d[titleKey];
  };

  AF.switchTo = function (lang) {
    try { localStorage.setItem("af-lang", lang); } catch (e) {}
    location.reload();
  };

  /* droplist выбора языка */
  AF.renderSwitch = function (host) {
    var w = document.createElement("div");
    w.className = "langw";
    var btn = document.createElement("button");
    btn.className = "iconbtn langbtn";
    btn.type = "button";
    btn.setAttribute("aria-haspopup", "listbox");
    btn.setAttribute("aria-expanded", "false");
    btn.setAttribute("data-i18n-title", "aria.lang");
    btn.innerHTML =
      '<svg viewBox="0 0 24 24" width="16" height="16"><circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3a15 15 0 0 1 0 18"/><path d="M12 3a15 15 0 0 0 0 18"/></svg>' +
      "<span>" + AF.lang.toUpperCase() + "</span>" +
      '<svg class="lb-chev" viewBox="0 0 24 24"><polyline points="6 9 12 15 18 9"/></svg>';
    var menu = document.createElement("div");
    menu.className = "lang-menu";
    menu.setAttribute("role", "listbox");
    LANGS.forEach(function (l) {
      var o = document.createElement("button");
      o.type = "button";
      o.className = "lang-opt" + (l.id === AF.lang ? " on" : "");
      o.setAttribute("data-lang", l.id);
      o.innerHTML = "<span>" + l.label + "</span>" +
        '<svg class="tick" viewBox="0 0 24 24"><polyline points="20 6 9 17 4 12"/></svg>';
      o.addEventListener("click", function () { AF.switchTo(l.id); });
      menu.appendChild(o);
    });
    btn.addEventListener("click", function (e) {
      e.stopPropagation();
      var open = w.classList.toggle("open");
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });
    document.addEventListener("click", function (e) {
      if (!w.contains(e.target)) { w.classList.remove("open"); btn.setAttribute("aria-expanded", "false"); }
    });
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") { w.classList.remove("open"); btn.setAttribute("aria-expanded", "false"); }
    });
    w.appendChild(btn); w.appendChild(menu);
    host.appendChild(w);
  };

  function boot() {
    AF.apply();
    var hosts = document.querySelectorAll("[data-lang-switch]");
    for (var i = 0; i < hosts.length; i++) AF.renderSwitch(hosts[i]);
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();
