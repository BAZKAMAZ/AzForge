// Справочник: страница списка подсистем и страница одной подсистемы.
// Язык — window.AF (меняется перезагрузкой страницы).
(function () {
  "use strict";
  const U = window.UTIL;
  const esc = U.esc;
  const T = window.AF.t;

  // ---------- страница-указатель ----------
  function refIndex() {
    const cards = U.REGISTRY.map((s) => {
      const nFn = s.functions.length;
      const nTypes = s.enums.length + s.structs.length + s.callbacks.length + s.defines.length;
      const ds = s.summaryRu || s.summary || "";
      return '<a class="sys-card" href="#/ref/' + s.id + '">' +
        '<div class="nm"><code>' + esc(s.id === "hdr:ForgeTypes.h" ? "ForgeTypes" : s.id === "hdr:AzmFile.h" ? "AzmFile" : s.id) + "</code></div>" +
        '<div class="ds">' + esc(ds.split(". ")[0] || "") + "</div>" +
        '<div class="mt">' + (nFn ? nFn + " " + T("функций", "functions") + " · " : "") + nTypes + " " + T("типов", "types") + "</div></a>";
    }).join("");

    return '<div class="page-head"><div class="crumb"><a href="#/overview">' + T("Документация", "Documentation") + '</a><span class="sep">/</span>' + T("Справочник", "Reference") + "</div>" +
      "<h1 class=\"page-title\">" + T("Справочник API", "API Reference") + "</h1>" +
      '<div class="rule"></div></div>' +
      '<div class="stat-row">' +
      '<div class="stat"><div class="n">' + U.fnCount + '</div><div class="l">' + T("функций", "functions") + "</div></div>" +
      '<div class="stat"><div class="n">' + U.REGISTRY.length + '</div><div class="l">' + T("подсистем и форматов", "subsystems & formats") + "</div></div>" +
      "</div>" +
      '<div class="sys-grid">' + cards + "</div>";
  }

  // ---------- карточка функции ----------
  function fnCard(sys, fn) {
    const loc = esc(U.fnRu(sys.id, fn.name) || fn.comment || "");
    const legacy = /Remove\w+|legacy/i.test(fn.comment || "") ? ' <span class="badge legacy">' + T("legacy-алиас", "legacy alias") + "</span>" : "";
    const wrap = U.wrapperCall(sys, fn);
    const args = fn.args.filter(a => a.type !== "AmodMod");
    return '<div class="fn-card" id="fn-' + esc(fn.name) + '">' +
      '<div class="fn-sig"><code class="sig-code">' + U.sigHtml(fn) + legacy + "</code></div>" +
      '<div class="fn-body">' +
      (loc ? '<p class="fn-desc">' + loc + "</p>" : "") +
      (wrap ? '<p class="fn-desc" style="font-size:12.5px;color:var(--text-faint)">' + T("обёртка:", "wrapper:") + " <code>" + esc(wrap) + "</code></p>" : "") +
      (args.length ? '<div class="fn-args">' + args.map(a =>
        '<div class="arg"><span class="arg-name">' + esc(a.name) + '</span><span class="arg-type">' + esc(a.variadic ? "..." : a.type) + "</span></div>"
      ).join("") + "</div>" : "") +
      "</div></div>";
  }

  // ---------- enum ----------
  function enumBlock(en) {
    const d = esc(U.enumRu(en.name));
    return '<div class="kind-block"><div class="kind-head"><span class="kind-tag">enum</span>' + esc(en.name) + "</div>" +
      (d || en.comment ? '<div class="kind-desc">' + (d || esc(en.comment)) + "</div>" : "") +
      '<div class="kind-members">' + en.members.map(m =>
        '<div class="km"><span class="m-name">' + esc(m.name) + '</span><span class="m-val">' + esc(m.value) + "</span>" +
        '<span class="m-desc">' + esc(U.memberRu(m.name) || m.comment) + "</span></div>"
      ).join("") + "</div></div>";
  }

  // ---------- структура ----------
  function structBlock(st) {
    const d = esc(U.structRu(st.name));
    const fields = st.fields.map(f => {
      if (f.kind === "fn") return "";
      return '<div class="km"><span class="m-name">' + esc(f.name) + '</span><span class="m-type">' + esc(f.type) + "</span>" +
        '<span class="m-desc">' + esc(f.comment) + "</span></div>";
    }).join("");
    return '<div class="kind-block"><div class="kind-head"><span class="kind-tag">struct</span>' + esc(st.name) + "</div>" +
      (d || st.comment ? '<div class="kind-desc">' + (d || esc(st.comment)) + "</div>" : "") +
      (fields ? '<div class="kind-members field-grid">' + fields + "</div>" : "") + "</div>";
  }

  // ---------- колбэк ----------
  function cbBlock(cb) {
    const d = esc(U.cbRu(cb.name) || cb.comment || "");
    const args = cb.args.map(a => '<span class="tok-type">' + esc(a.variadic ? "..." : a.type) + '</span> <span class="tok-fn">' + esc(a.name) + "</span>").join(", ");
    const sig = '<span class="tok-type">' + esc(cb.ret) + '</span> <span class="fn-name">' + esc(cb.name) + '</span><span class="tok-punc">(</span>' + args + '<span class="tok-punc">)</span>';
    return '<div class="fn-card"><div class="fn-sig"><code class="sig-code">' + sig + "</code></div>" +
      '<div class="fn-body">' + (d ? '<p class="fn-desc">' + d + "</p>" : "") + "</div></div>";
  }

  // ---------- define ----------
  function defineRows(defs) {
    if (!defs.length) return "";
    return '<div class="kind-block"><div class="kind-head"><span class="kind-tag">defines</span>' + T("константы", "constants") + "</div>" +
      '<div class="kind-members">' + defs.map(d =>
        '<div class="km"><span class="m-name">' + esc(d.name) + '</span><span class="m-val">' + esc(d.value || "") + "</span>" +
        '<span class="m-desc">' + esc(U.defRu(d.name) || d.comment) + "</span></div>"
      ).join("") + "</div></div>";
  }

  // ---------- страница подсистемы ----------
  function refPage(sys) {
    const isHdr = String(sys.id).startsWith("hdr:");
    const parts = [];

    parts.push('<div class="page-head"><div class="crumb"><a href="#/reference">' + T("Справочник", "Reference") + '</a><span class="sep">/</span>' + esc(sys.title) + "</div>");
    parts.push('<h1 class="page-title">' + (isHdr ? esc(sys.title) : '<span style="font-family:var(--font-mono);font-size:.85em">' + esc(sys.id) + "</span>") + "</h1>");
    parts.push('<p class="page-sub">' + esc(sys.summaryRu || sys.summary) + "</p>");
    parts.push('<div class="pill-list">');
    parts.push('<span class="pill">' + esc(sys.header) + "</span>");
    if (sys.ns) parts.push('<span class="pill">Amod::' + esc(sys.ns) + "</span>");
    if (sys.functions.length) parts.push('<span class="pill">' + sys.functions.length + " " + T("функций", "functions") + "</span>");
    if (sys.last) parts.push('<span class="pill">' + T("последнее поле @", "last field @") + " " + esc(sys.last.offset) + "</span>");
    parts.push("</div></div>");

    if (sys.functions.length) {
      parts.push('<section class="ref-section" id="sec-functions"><h2>' + T("Функции таблицы", "Table functions") + '<span class="cnt">' + sys.functions.length + "</span></h2>");
      if (sys.structSize) parts.push('<div class="note"><span class="ic">◈</span><div>' +
        T("Все выходные структуры начинаются с поля <code>structSize</code> — перед вызовом с <code>out</code> выставьте его в <code>sizeof</code> своей копии.",
          "All output structures start with a <code>structSize</code> field — before a call with <code>out</code>, set it to the <code>sizeof</code> of your copy.") +
        "</div></div>");
      parts.push(sys.functions.map(f => fnCard(sys, f)).join(""));
      parts.push("</section>");
    }

    // экспорты мода (AmodInit / AmodShutdown / AmodUpdate)
    const hdExports = window.UTIL.DATA.headers[sys.header].exports || [];
    if (hdExports.length) {
      parts.push('<section class="ref-section" id="sec-exports"><h2>' + T("Экспорты мода", "Mod exports") + '<span class="cnt">' + hdExports.length + "</span></h2>");
      parts.push(hdExports.map(ex => {
        const d = esc(U.cbRu(ex.name));
        const args = ex.args.map(a => '<span class="tok-type">' + esc(a.variadic ? "..." : a.type) + '</span> <span class="tok-fn">' + esc(a.name) + "</span>").join(", ");
        return '<div class="fn-card" id="fn-' + esc(ex.name) + '"><div class="fn-sig"><code class="sig-code">' +
          '<span class="tok-kw">AMOD_EXPORT</span> <span class="tok-type">' + esc(ex.ret) + '</span> <span class="fn-name">' + esc(ex.name) + '</span><span class="tok-punc">(</span>' + args + '<span class="tok-punc">)</span></code></div>' +
          '<div class="fn-body">' + (d ? '<p class="fn-desc">' + d + "</p>" : "") + "</div></div>";
      }).join("") + "</section>");
    }

    if (sys.callbacks.length) {
      parts.push('<section class="ref-section" id="sec-callbacks"><h2>' + T("Колбэки", "Callbacks") + '<span class="cnt">' + sys.callbacks.length + "</span></h2>");
      parts.push(sys.callbacks.map(cbBlock).join(""));
      parts.push("</section>");
    }

    if (sys.enums.length) {
      parts.push('<section class="ref-section" id="sec-enums"><h2>' + T("Перечисления", "Enums") + '<span class="cnt">' + sys.enums.length + "</span></h2>");
      parts.push(sys.enums.map(enumBlock).join(""));
      parts.push("</section>");
    }

    if (sys.structs.length) {
      parts.push('<section class="ref-section" id="sec-structs"><h2>' + T("Структуры", "Structures") + '<span class="cnt">' + sys.structs.length + "</span></h2>");
      parts.push(sys.structs.map(structBlock).join(""));
      parts.push("</section>");
    }

    if (sys.typedefs.length) {
      parts.push('<section class="ref-section"><h2>' + T("Типы", "Typedefs") + '</h2><div class="kind-members">' +
        sys.typedefs.map(t => '<div class="km"><span class="m-name">' + esc(t.name) + '</span><span class="m-type">' + esc(t.type) + '</span><span class="m-desc">' + esc(t.comment) + "</span></div>").join("") +
        "</div></section>");
    }

    if (sys.defines.length) {
      parts.push('<section class="ref-section" id="sec-defines"><h2>' + T("Константы", "Constants") + '<span class="cnt">' + sys.defines.length + "</span></h2>");
      parts.push(defineRows(sys.defines));
      parts.push("</section>");
    }

    return parts.join("");
  }

  window.REF = { refIndex, refPage };
})();
