// frontend/app.js - 模拟运行 + 代码生成 + 导入 + 配置化测试 前端逻辑

(function () {
  "use strict";

  var frameworks = [];
  var selectedId = null;

  var els = {
    list: document.getElementById("framework-list"),
    configArea: document.getElementById("config-area"),
    runBtn: document.getElementById("run-btn"),
    refreshBtn: document.getElementById("refresh-btn"),
    output: document.getElementById("output"),
    summary: document.getElementById("result-summary"),
    statOk: document.getElementById("stat-ok"),
    statFail: document.getElementById("stat-fail"),
    statTime: document.getElementById("stat-time"),
    verdict: document.getElementById("verdict"),
    pill: document.getElementById("status-pill"),
    meta: document.getElementById("meta"),
    perfStats: document.getElementById("perf-stats"),
    wearWrap: document.getElementById("wear-wrap"),
    wearCanvas: document.getElementById("wear-canvas"),
    wearLegend: document.getElementById("wear-legend"),
    genFramework: document.getElementById("gen-framework"),
    genBtn: document.getElementById("gen-btn"),
    genParams: document.getElementById("gen-params"),
    genOutput: document.getElementById("gen-output"),
    importFile: document.getElementById("import-file"),
    importBtn: document.getElementById("import-btn"),
    importOutput: document.getElementById("import-output"),
  };

  function setPill(state, text) {
    els.pill.className = "pill pill-" + state;
    els.pill.textContent = text;
  }

  function api(method, path, body) {
    var opts = { method: method, headers: { "Content-Type": "application/json" } };
    if (body) { opts.body = JSON.stringify(body); }
    return fetch(path, opts).then(function (r) { return r.json(); });
  }

  function loadFrameworks() {
    els.list.innerHTML = '<p class="hint">正在加载框架列表…</p>';
    api("GET", "/api/frameworks").then(function (data) {
      frameworks = data.frameworks || [];
      renderList();
      renderGenSelect();
      if (frameworks.length > 0 && !selectedId) select(frameworks[0].id);
    }).catch(function (e) {
      els.list.innerHTML = '<p class="hint">加载失败：' + e + "</p>";
    });
  }

  function renderList() {
    els.list.innerHTML = "";
    frameworks.forEach(function (f) {
      var card = document.createElement("button");
      card.className = "framework-card" + (f.id === selectedId ? " selected" : "");
      card.innerHTML = '<div class="fc-name">' + f.name + "</div>" +
                       '<div class="fc-desc">' + (f.desc || "") + "</div>";
      card.onclick = function () { select(f.id); };
      els.list.appendChild(card);
    });
  }

  function selectedFramework() {
    for (var i = 0; i < frameworks.length; i++) {
      if (frameworks[i].id === selectedId) return frameworks[i];
    }
    return null;
  }

  function select(id) {
    selectedId = id;
    renderList();
    els.runBtn.disabled = false;
    els.meta.textContent = "已选：" + (nameOf(id) || id);
    renderConfigForm();
  }

  function nameOf(id) {
    var f = selectedFramework();
    return f ? f.name : id;
  }

  // 动态渲染：模拟基座配置 + 测试配置
  function renderConfigForm() {
    var f = selectedFramework();
    els.configArea.innerHTML = "";
    if (!f) return;
    var groups = [
      { title: "模拟基座配置", schema: f.config_schema || [] },
      { title: "测试配置", schema: f.test_schema || [] },
    ];
    groups.forEach(function (g) {
      if (!g.schema.length) return;
      var box = document.createElement("div");
      box.className = "cfg-group";
      box.innerHTML = '<div class="cfg-title">' + g.title + "</div>";
      g.schema.forEach(function (p) {
        var row = document.createElement("label");
        row.className = "cfg-row";
        var label = document.createElement("span");
        label.textContent = p.label;
        var input;
        if (p.type === "select") {
          input = document.createElement("select");
          (p.options || []).forEach(function (o) {
            var opt = document.createElement("option");
            opt.value = o[0]; opt.textContent = o[1];
            if (String(o[0]) === String(p.default)) opt.selected = true;
            input.appendChild(opt);
          });
        } else {
          input = document.createElement("input");
          input.type = "number";
          input.value = p.default;
          if (p.min != null) input.min = p.min;
          if (p.max != null) input.max = p.max;
          if (p.step != null) input.step = p.step;
        }
        input.dataset.key = p.key;
        input.dataset.group = g.title;
        row.appendChild(label);
        row.appendChild(input);
        box.appendChild(row);
      });
      els.configArea.appendChild(box);
    });
    if (els.configArea.children.length) els.configArea.classList.remove("hidden");
    else els.configArea.classList.add("hidden");
  }

  function collectConfig() {
    var config = {}, test = {};
    els.configArea.querySelectorAll("input[data-key],select[data-key]").forEach(function (inp) {
      var v = inp.value;
      if (inp.type === "number") v = Number(v);
      if (inp.dataset.group === "模拟基座配置") config[inp.dataset.key] = v;
      else test[inp.dataset.key] = v;
    });
    return { config: config, test_config: test };
  }

  function renderOutput(result) {
    els.output.innerHTML = "";
    (result.lines || []).forEach(function (l) { appendOut(els.output, l.level, l.text); });
    if (!result.lines && result.error) appendOut(els.output, "stderr", "错误：" + result.error);
  }

  function appendOut(el, level, text) {
    var span = document.createElement("span");
    span.className = "line-" + level;
    span.textContent = text + "\n";
    el.appendChild(span);
    el.scrollTop = el.scrollHeight;
  }

  function renderPerfStats(stats) {
    if (!stats) { els.perfStats.classList.add("hidden"); return; }
    var items = [
      ["读次数", stats.reads != null ? stats.reads : stats.ops],
      ["写次数", stats.writes],
      ["擦除次数", stats.erases],
      ["写入字节", stats.write_bytes],
      ["数据丢失", stats.lost],
      ["阻塞耗时(us)", stats.block_us],
      ["读耗时(us)", stats.read_us],
      ["写耗时(us)", stats.write_us],
      ["擦除耗时(us)", stats.erase_us],
      ["最大擦写", stats.max_cycles],
      ["平均擦写", stats.avg_cycles],
      ["坏块数", stats.bad_blocks],
    ].filter(function (it) { return it[1] !== undefined && it[1] !== null; });
    var html = '<h3>性能与使用统计</h3><div class="perf-grid">';
    items.forEach(function (it) {
      html += '<div class="perf-item"><span class="perf-num">' + it[1] +
              '</span><span class="perf-label">' + it[0] + "</span></div>";
    });
    html += "</div>";
    els.perfStats.innerHTML = html;
    els.perfStats.classList.remove("hidden");
  }

  function renderWearMap(wearmap, maxCycle) {
    if (!wearmap || !wearmap.length) { els.wearWrap.classList.add("hidden"); return; }
    var max = maxCycle || Math.max.apply(null, wearmap.concat([1]));
    var c = els.wearCanvas, ctx = c.getContext("2d");
    var W = c.width, H = c.height;
    ctx.clearRect(0, 0, W, H);
    var n = wearmap.length;
    var bw = W / n;
    if (bw > 14) bw = 14;
    for (var i = 0; i < n; i++) {
      var v = wearmap[i];
      var h = max > 0 ? (v / max) * (H - 20) : 0;
      var ratio = max > 0 ? v / max : 0;
      // 热度配色：绿->黄->红
      var r = Math.round(255 * Math.min(1, ratio * 1.6));
      var g = Math.round(200 * (1 - ratio));
      ctx.fillStyle = "rgb(" + r + "," + g + ",80)";
      ctx.fillRect(i * bw, H - h - 16, bw - 1, h);
    }
    ctx.fillStyle = "#8b97ad";
    ctx.font = "11px sans-serif";
    ctx.fillText("块数=" + n + "  最高擦写=" + max, 6, H - 4);
    els.wearLegend.textContent = "柱越高表示该块擦写越频繁；颜色越红表示越接近寿命上限。";
    els.wearWrap.classList.remove("hidden");
  }

  function runTest() {
    if (!selectedId) return;
    var cfg = collectConfig();
    setPill("running", "运行中…");
    els.runBtn.disabled = true;
    els.meta.textContent = "正在运行：" + nameOf(selectedId);
    els.summary.classList.add("hidden");
    els.perfStats.classList.add("hidden");
    els.wearWrap.classList.add("hidden");
    els.output.innerHTML = '<span class="hint">编译并运行测试程序，请稍候…</span>';

    api("POST", "/api/run", {
      framework: selectedId, config: cfg.config, test_config: cfg.test_config
    }).then(function (result) {
      renderOutput(result);
      els.summary.classList.remove("hidden");
      els.statOk.textContent = result.ok_count || 0;
      els.statFail.textContent = result.fail_count || 0;
      els.statTime.textContent = result.elapsed_ms || 0;
      var ok = result.success === true;
      els.verdict.className = "verdict " + (ok ? "ok" : "fail");
      els.verdict.textContent = ok ? "✓ 全部通过" : "✗ 存在失败";
      setPill(ok ? "ok" : "fail", ok ? "通过" : "失败");
      els.meta.textContent = "已选：" + nameOf(selectedId) +
        "（返回码 " + (result.return_code != null ? result.return_code : "-") + "）";
      renderPerfStats(result.stats);
      renderWearMap(result.wearmap, result.stats ? result.stats.max_cycles : null);
      els.runBtn.disabled = false;
    }).catch(function (e) {
      appendOut(els.output, "stderr", "请求失败：" + e);
      setPill("fail", "错误");
      els.runBtn.disabled = false;
    });
  }

  // ---------- 代码生成 ----------
  function renderGenSelect() {
    els.genFramework.innerHTML = "";
    frameworks.forEach(function (f) {
      var opt = document.createElement("option");
      opt.value = f.id; opt.textContent = f.name;
      els.genFramework.appendChild(opt);
    });
    renderGenParams();
  }
  function currentGenParams() {
    var f = null;
    frameworks.forEach(function (x) { if (x.id === els.genFramework.value) f = x; });
    return (f && f.config_schema) ? f.config_schema : [];
  }
  function renderGenParams() {
    els.genParams.innerHTML = "";
    currentGenParams().forEach(function (p) {
      var wrap = document.createElement("label");
      wrap.className = "param";
      wrap.innerHTML = "<span>" + p.label + "</span>";
      var inp = document.createElement(p.type === "select" ? "select" : "input");
      if (p.type === "select") {
        (p.options || []).forEach(function (o) {
          var op = document.createElement("option");
          op.value = o[0]; op.textContent = o[1];
          if (String(o[0]) === String(p.default)) op.selected = true;
          inp.appendChild(op);
        });
      } else {
        inp.type = "number"; inp.value = p.default;
        if (p.min != null) inp.min = p.min;
        if (p.step != null) inp.step = p.step;
      }
      inp.dataset.key = p.key;
      wrap.appendChild(inp);
      els.genParams.appendChild(wrap);
    });
  }
  function genDownload() {
    var fid = els.genFramework.value;
    var params = {};
    els.genParams.querySelectorAll("input[data-key],select[data-key]").forEach(function (inp) {
      params[inp.dataset.key] = inp.type === "number" ? Number(inp.value) : inp.value;
    });
    els.genOutput.innerHTML = '<span class="hint">正在生成库文件…</span>';
    fetch("/api/generate", { method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ framework: fid, params: params }) })
      .then(function (r) {
        if (!r.ok) return r.json().then(function (j) { throw new Error(j.error || "生成失败"); });
        var cd = r.headers.get("Content-Disposition") || "";
        var m = cd.match(/filename="?([^"]+)"?/);
        var fname = m ? m[1] : "library.zip";
        return r.blob().then(function (b) { return { b: b, fname: fname }; });
      })
      .then(function (o) {
        var url = URL.createObjectURL(o.b);
        var a = document.createElement("a");
        a.href = url; a.download = o.fname; document.body.appendChild(a);
        a.click(); document.body.removeChild(a); URL.revokeObjectURL(url);
        appendOut(els.genOutput, "ok", "已生成并下载：" + o.fname);
        appendOut(els.genOutput, "info", "包内含 .c/.h + test_main.c + PORTING.md + manifest.json");
      })
      .catch(function (e) { appendOut(els.genOutput, "stderr", "生成失败：" + e.message); });
  }

  // ---------- 导入 ----------
  function doImport() {
    var file = els.importFile.files[0];
    if (!file) { appendOut(els.importOutput, "stderr", "请先选择要导入的 zip 文件"); return; }
    els.importOutput.innerHTML = '<span class="hint">正在上传并校验…</span>';
    var reader = new FileReader();
    reader.onload = function () {
      var bytes = new Uint8Array(reader.result);
      var bin = "";
      for (var i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
      api("POST", "/api/import", { file_b64: btoa(bin) }).then(function (res) {
        els.importOutput.innerHTML = "";
        if (res.success) {
          appendOut(els.importOutput, "ok", "✓ " + (res.message || "导入成功"));
          loadFrameworks();
        } else {
          appendOut(els.importOutput, "fail", "✗ " + (res.error || "导入失败"));
          (res.lines || []).forEach(function (l) { appendOut(els.importOutput, l.level, l.text); });
        }
      }).catch(function (e) { appendOut(els.importOutput, "stderr", "请求失败：" + e); });
    };
    reader.readAsArrayBuffer(file);
  }

  els.genFramework.onchange = renderGenParams;
  els.genBtn.onclick = genDownload;
  els.importBtn.onclick = doImport;
  els.runBtn.onclick = runTest;
  els.refreshBtn.onclick = loadFrameworks;
  loadFrameworks();
})();
