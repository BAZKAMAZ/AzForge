/* AzForge — логика главной страницы: тема, мобильное меню,
   появление блоков, копирование кода, год в футере. */
(function () {
  "use strict";
  var $ = function (s, c) { return (c || document).querySelector(s); };
  var $$ = function (s, c) { return Array.prototype.slice.call((c || document).querySelectorAll(s)); };

  /* тема: тёмная по умолчанию, выбор запоминается */
  try {
    var t = localStorage.getItem("af-theme");
    if (t) document.body.setAttribute("data-theme", t);
  } catch (e) {}
  $("#themeBtn").addEventListener("click", function () {
    var next = document.body.getAttribute("data-theme") === "light" ? "dark" : "light";
    document.body.setAttribute("data-theme", next);
    try { localStorage.setItem("af-theme", next); } catch (e) {}
  });

  /* мобильное меню */
  var mnav = $("#mnav"), burger = $("#burger");
  burger.addEventListener("click", function () {
    var open = mnav.classList.toggle("open");
    $("svg use", burger).setAttribute("href", open ? "#i-x" : "#i-menu");
  });
  $$("a", mnav).forEach(function (a) { a.addEventListener("click", function () {
    mnav.classList.remove("open");
    $("svg use", burger).setAttribute("href", "#i-menu");
  }); });

  /* появление блоков при прокрутке */
  if ("IntersectionObserver" in window) {
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) { if (e.isIntersecting) { e.target.classList.add("on"); io.unobserve(e.target); } });
    }, { rootMargin: "0px 0px -8% 0px", threshold: .08 });
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
        copyBtn.innerHTML = '<svg><use href="#i-check"/></svg>';
        setTimeout(function () { copyBtn.innerHTML = '<svg><use href="#i-copy"/></svg>'; }, 1400);
      }
      function fallback() {
        var ta = document.createElement("textarea");
        ta.value = txt; ta.style.cssText = "position:fixed;opacity:0";
        document.body.appendChild(ta); ta.select();
        try { document.execCommand("copy"); ok(); } catch (e) {}
        document.body.removeChild(ta);
      }
      if (navigator.clipboard && window.isSecureContext) { navigator.clipboard.writeText(txt).then(ok, fallback); }
      else { fallback(); }
    });
  }

  /* год в футере */
  var yy = $("#yy"); if (yy) yy.textContent = new Date().getFullYear();
})();
