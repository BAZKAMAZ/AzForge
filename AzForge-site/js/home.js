/* логика главной страницы: тема, курсорный прожектор, мобильное меню, появление блоков, копирование кода, год в футере. */
(function () {
  "use strict";
  var $ = function (s, c) { return (c || document).querySelector(s); };
  var $$ = function (s, c) { return Array.prototype.slice.call((c || document).querySelectorAll(s)); };

  function syncThemeMeta(theme) {
    var meta = $('meta[name="theme-color"]');
    if (meta) meta.setAttribute("content", theme === "light" ? "#f6f8fc" : "#080c15");
  }

  /* тема: тёмная по умолчанию, выбор запоминается */
  try {
    var t = localStorage.getItem("af-theme");
    if (t === "light" || t === "dark") {
      document.body.setAttribute("data-theme", t);
      syncThemeMeta(t);
    }
  } catch (e) {}

  $("#themeBtn").addEventListener("click", function () {
    var next = document.body.getAttribute("data-theme") === "light" ? "dark" : "light";
    document.body.setAttribute("data-theme", next);
    syncThemeMeta(next);
    try { localStorage.setItem("af-theme", next); } catch (e) {}
  });

  /* курсорный прожектор: сетка + границы карточек */
  var spotBtn = $("#spotBtn");
  var bg = $(".az-bg");
  var cards = $$(".az-glass-card");

  function syncSpotBtn() {
    if (!spotBtn) return;
    var on = document.body.getAttribute("data-spotlight") !== "off";
    var label = window.AF && window.AF.t
      ? (on
          ? window.AF.t("Выключить подсветку за курсором", "Disable cursor spotlight")
          : window.AF.t("Включить подсветку за курсором", "Enable cursor spotlight"))
      : "Spotlight";
    spotBtn.setAttribute("title", label);
    spotBtn.setAttribute("aria-label", label);
    spotBtn.setAttribute("aria-pressed", on ? "true" : "false");
  }

  try {
    var savedSpot = localStorage.getItem("af-spotlight");
    if (savedSpot === "0") document.body.setAttribute("data-spotlight", "off");
  } catch (e) {}
  syncSpotBtn();

  if (spotBtn) {
    spotBtn.addEventListener("click", function () {
      var next = document.body.getAttribute("data-spotlight") === "off" ? "on" : "off";
      document.body.setAttribute("data-spotlight", next);
      try { localStorage.setItem("af-spotlight", next === "on" ? "1" : "0"); } catch (e) {}
      syncSpotBtn();
    });
  }

  window.addEventListener("pointermove", function (e) {
    if (document.body.getAttribute("data-spotlight") === "off") return;
    if (bg) {
      bg.style.setProperty("--gx", e.clientX + "px");
      bg.style.setProperty("--gy", e.clientY + "px");
    }
  }, { passive: true });

  cards.forEach(function (card) {
    card.addEventListener("pointermove", function (e) {
      if (document.body.getAttribute("data-spotlight") === "off") return;
      var r = card.getBoundingClientRect();
      card.style.setProperty("--mx", (e.clientX - r.left) + "px");
      card.style.setProperty("--my", (e.clientY - r.top) + "px");
    }, { passive: true });
  });

  /* мобильное меню */
  var mnav = $("#mnav"), burger = $("#burger");
  burger.addEventListener("click", function () {
    var open = mnav.classList.toggle("open");
    $("svg use", burger).setAttribute("href", open ? "#i-x" : "#i-menu");
  });
  $$("a", mnav).forEach(function (a) {
    a.addEventListener("click", function () {
      mnav.classList.remove("open");
      $("svg use", burger).setAttribute("href", "#i-menu");
    });
  });

  /* появление блоков при прокрутке */
  if ("IntersectionObserver" in window) {
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) {
        if (e.isIntersecting) {
          e.target.classList.add("on");
          io.unobserve(e.target);
        }
      });
    }, { rootMargin: "0px 0px -8% 0px", threshold: 0.08 });
    $$(".rv").forEach(function (el) { io.observe(el); });
  } else {
    $$(".rv").forEach(function (el) { el.classList.add("on"); });
  }

  /* копирование кода из hero-карточки */
  var copyBtn = $(".copy[data-copy]");
  if (copyBtn) {
    copyBtn.addEventListener("click", function () {
      var txt = $("#heroCode").textContent;
      function ok() {
        copyBtn.classList.add("copied");
        copyBtn.innerHTML = '<svg><use href="#i-check"/></svg>';
        setTimeout(function () {
          copyBtn.classList.remove("copied");
          copyBtn.innerHTML = '<svg><use href="#i-copy"/></svg>';
        }, 1500);
      }
      function fallback() {
        var ta = document.createElement("textarea");
        ta.value = txt;
        ta.style.cssText = "position:fixed;opacity:0";
        document.body.appendChild(ta);
        ta.select();
        try { document.execCommand("copy"); ok(); } catch (e) {}
        document.body.removeChild(ta);
      }
      if (navigator.clipboard && window.isSecureContext) {
        navigator.clipboard.writeText(txt).then(ok, fallback);
      } else {
        fallback();
      }
    });
  }

  /* год в футере */
  var yy = $("#yy");
  if (yy) yy.textContent = new Date().getFullYear();
})();
