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
    rtLog: document.getElementById("rt-log"),
    rtLogBody: document.getElementById("rt-log-body"),
    rtLogStatus: document.getElementById("rt-log-status"),
    rtLogClear: document.getElementById("rt-log-clear"),
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
    fwTitle: document.getElementById("fw-title"),
    fwDesc: document.getElementById("fw-desc"),
  };

  /* 标签页切换 */
  function showTab(name) {
    document.querySelectorAll(".tab").forEach(function (t) {
      t.classList.toggle("active", t.getAttribute("data-tab") === name);
    });
    document.querySelectorAll(".tab-panel").forEach(function (p) {
      p.classList.toggle("active", p.id === "tab-" + name);
    });
  }
  document.querySelectorAll(".tab").forEach(function (t) {
    t.onclick = function () { showTab(t.getAttribute("data-tab")); };
  });

  function setPill(state, text) {
    els.pill.className = "pill pill-" + state;
    els.pill.textContent = text;
  }

  function api(method, path, body) {
    var opts = { method: method, headers: { "Content-Type": "application/json" } };
    if (body) { opts.body = JSON.stringify(body); }
    return fetch(path, opts).then(function (r) { return r.json(); });
  }

  // 按介质类型（NOR/NAND/EEPROM）刷新的模拟基座字段默认值映射，
  // 由后端 /api/frameworks 返回，避免前端硬编码。
  var simTypeDefaults = {};
  // 应用层测试任务 schema 与分类标签，由后端返回。
  var appTaskSchema = [];
  var categoryLabels = {
    simulator: "驱动层 · 模拟基座",
    baremetal: "组件层 · 裸机简单管理",
    kv: "组件层 · KV 管理",
    fs: "组件层 · 文件系统",
  };
  // 分类排序（决定左侧分组顺序）
  var categoryOrder = ["simulator", "baremetal", "kv", "fs"];

  function loadFrameworks() {
    els.list.innerHTML = '<p class="hint">正在加载框架列表…</p>';
    api("GET", "/api/frameworks").then(function (data) {
      frameworks = data.frameworks || [];
      simTypeDefaults = data.sim_type_defaults || {};
      appTaskSchema = data.app_task_schema || [];
      renderList();
      renderGenSelect();
      renderAppConfig();
      if (frameworks.length > 0 && !selectedId) select(frameworks[0].id);
    }).catch(function (e) {
      els.list.innerHTML = '<p class="hint">加载失败：' + e + "</p>";
    });
  }

  /*
   * 根据当前选中的介质类型（type 字段）刷新同表单内其他模拟基座
   * 字段的 value，使其匹配该类型的硬件特性表默认指标。
   * 仅刷新 data-group=="sim" 且 key 在映射中、且 user_typed 标记未置位的字段，
   * 避免覆盖用户已显式修改的值。
   */
  function applySimTypeDefaults(formRoot) {
    if (!formRoot || !simTypeDefaults) return;
    var typeEl = formRoot.querySelector('select[data-key="type"]');
    if (!typeEl) return;
    var t = String(typeEl.value);
    var inputs = formRoot.querySelectorAll('input[data-group="sim"], select[data-group="sim"]');
    inputs.forEach(function (inp) {
      if (inp === typeEl) return;
      var key = inp.dataset.key;
      if (!key || inp.dataset.userTyped === "1") return;
      var m = simTypeDefaults[key];
      if (!m || m[t] == null) return;
      inp.value = m[t];
    });
  }

  /* 按分类分组渲染框架列表。
   * 分组顺序：simulator(驱动层) -> baremetal(裸机) -> kv -> fs，
   * 每个分组带标题；未标注 category 的框架归入"其他"。 */
  function renderList() {
    els.list.innerHTML = "";
    var groups = {};
    frameworks.forEach(function (f) {
      var cat = f.category || "other";
      if (!groups[cat]) groups[cat] = [];
      groups[cat].push(f);
    });
    categoryOrder.forEach(function (cat) {
      if (!groups[cat]) return;
      renderCategoryTitle(cat);
      groups[cat].forEach(function (f) { appendFwItem(f); });
    });
    if (groups.other) {
      renderCategoryTitle("other");
      groups.other.forEach(function (f) { appendFwItem(f); });
    }
  }

  function renderCategoryTitle(cat) {
    var t = document.createElement("div");
    t.className = "fw-cat";
    t.textContent = categoryLabels[cat] || "其他";
    els.list.appendChild(t);
  }

  function appendFwItem(f) {
    var item = document.createElement("button");
    item.className = "fw-item" + (f.id === selectedId ? " selected" : "");
    item.innerHTML = '<span class="fw-dot"></span><span class="fw-name">' + f.name + "</span>";
    item.title = (f.desc || "") + (f.app_supported ? "\n[支持应用层测试]" : "");
    item.onclick = function () { select(f.id); };
    els.list.appendChild(item);
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
    var f = selectedFramework();
    els.fwTitle.textContent = f ? f.name : id;
    els.fwDesc.textContent = f ? (f.desc || "") : "";
    renderConfigForm();
    showTab("run");
  }

  function nameOf(id) {
    var f = selectedFramework();
    return f ? f.name : id;
  }

  // ---- 配置区（右侧栏）：模拟基座配置 + 测试配置/测试项/条目表 ----
  function renderConfigForm() {
    var f = selectedFramework();
    els.configArea.innerHTML = "";
    if (!f) { els.configArea.innerHTML = '<p class="hint">请先在左侧选择一个框架。</p>'; return; }

    // 模拟基座配置
    var sim = document.createElement("div");
    sim.className = "cfg-group";
    sim.innerHTML = '<div class="cfg-title">模拟基座配置</div>';
    (f.config_schema || []).forEach(function (p) { sim.appendChild(makeField(p, "sim")); });
    els.configArea.appendChild(sim);
    // 首次渲染：若当前 type 不是 0，按类型应用默认硬件指标
    applySimTypeDefaults(sim);

    // 测试配置（KV 含测试项与条目表）
    var tst = document.createElement("div");
    tst.className = "cfg-group";
    (f.test_schema || []).forEach(function (p) { tst.appendChild(makeField(p, "test")); });
    if (f.test_items && f.test_items.length) {
      tst.appendChild(makeTestItems(f.test_items));
    }
    if (f.id === "kv") {
      tst.appendChild(makeItemTable());
    }
    if (tst.children.length > 1 || (f.test_schema && f.test_schema.length)) {
      tst.insertAdjacentHTML("afterbegin", '<div class="cfg-title">测试配置</div>');
      els.configArea.appendChild(tst);
    }
  }

  function makeField(p, group) {
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
      // 介质类型 select：切换时刷新其他基座字段默认值
      if (p.key === "type") {
        input.addEventListener("change", function () {
          applySimTypeDefaults(input.closest("#config-area") || input.parentNode.parentNode);
        });
      }
    } else {
      input = document.createElement("input");
      input.type = "number";
      input.value = p.default;
      if (p.min != null) input.min = p.min;
      if (p.max != null) input.max = p.max;
      if (p.step != null) input.step = p.step;
      // 用户一旦手动改过，标记为"已显式配置"，避免后续 type 切换被覆盖
      if (group === "sim") {
        input.addEventListener("input", function () {
          input.dataset.userTyped = "1";
        });
      }
    }
    input.dataset.key = p.key;
    input.dataset.group = group;
    row.appendChild(label);
    row.appendChild(input);
    return row;
  }

  function makeTestItems(items) {
    var box = document.createElement("div");
    box.className = "checklist";
    box.innerHTML = '<div class="cfg-subtitle">测试项（勾选执行）</div>';
    items.forEach(function (it) {
      var lbl = document.createElement("label");
      lbl.className = "chk";
      var cb = document.createElement("input");
      cb.type = "checkbox";
      cb.value = it.id;
      cb.checked = true;
      cb.dataset.testitem = "1";
      lbl.appendChild(cb);
      lbl.appendChild(document.createTextNode(" " + it.label));
      box.appendChild(lbl);
    });
    return box;
  }

  function makeItemTable() {
    var box = document.createElement("div");
    box.className = "item-table";
    box.innerHTML = '<div class="cfg-subtitle">KV 模拟条目表（可累加多条）</div>';
    var tbl = document.createElement("div");
    tbl.className = "itable";
    tbl.id = "item-table-body";
    box.appendChild(tbl);
    var add = document.createElement("button");
    add.className = "btn btn-sm";
    add.textContent = "+ 添加条目";
    add.onclick = function () { addItemRow(tbl, null); };
    box.appendChild(add);
    // 默认一条
    addItemRow(tbl, { vlen: 32, n: 50, freq: 50 });
    return box;
  }

  function addItemRow(tbl, val) {
    val = val || { vlen: 32, n: 50, freq: 50 };
    var row = document.createElement("div");
    row.className = "irow";
    row.innerHTML =
      '<span>长度</span><input type="number" class="i-vlen" value="' + val.vlen + '" min="1">' +
      '<span>条数</span><input type="number" class="i-n" value="' + val.n + '" min="1">' +
      '<span>修改%</span><input type="number" class="i-freq" value="' + val.freq + '" min="0" max="100">';
    var del = document.createElement("button");
    del.className = "btn btn-sm btn-del";
    del.textContent = "✕";
    del.onclick = function () { tbl.removeChild(row); };
    row.appendChild(del);
    tbl.appendChild(row);
  }

  function collectConfig() {
    var config = {}, test = {};
    els.configArea.querySelectorAll("input[data-key],select[data-key]").forEach(function (inp) {
      var v = inp.value;
      if (inp.type === "number") v = Number(v);
      if (inp.dataset.group === "sim") config[inp.dataset.key] = v;
      else test[inp.dataset.key] = v;
    });
    // 测试项
    var tests = [];
    els.configArea.querySelectorAll("input[data-testitem]").forEach(function (cb) {
      if (cb.checked) tests.push(cb.value);
    });
    if (tests.length) test.tests = tests;
    // 条目表
    var items = [];
    var tbl = document.getElementById("item-table-body");
    if (tbl) {
      tbl.querySelectorAll(".irow").forEach(function (row) {
        items.push({
          vlen: Number(row.querySelector(".i-vlen").value),
          n: Number(row.querySelector(".i-n").value),
          freq: Number(row.querySelector(".i-freq").value),
        });
      });
    }
    if (items.length) test.items = items;
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
      ["模式", stats.mode],
      ["读次数", stats.reads != null ? stats.reads : stats.ops],
      ["写次数", stats.writes],
      ["擦除次数", stats.erases],
      ["写入字节", stats.write_bytes],
      ["总操作数", stats.ops],
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

  function renderWearMap(wearmap, eraseCycles) {
    if (!wearmap || !wearmap.length) { els.wearWrap.classList.add("hidden"); return; }
    /* 用标称擦写寿命作为分母（而非当前最大值），1次/100000次 = 0.001% */
    var maxCycle = Math.max.apply(null, wearmap.concat([1]));
    var denom = (eraseCycles && eraseCycles > 0) ? eraseCycles : maxCycle;
    var c = els.wearCanvas, ctx = c.getContext("2d");
    var W = c.width, H = c.height;
    ctx.clearRect(0, 0, W, H);

    var n = wearmap.length;
    var cellSize = 13;
    var gap = 3;
    var cols = Math.floor((W - 20) / (cellSize + gap));
    if (cols < 4) cols = 4;
    var rows = Math.ceil(n / cols);
    var gridH = rows * (cellSize + gap) + 40;
    if (gridH > H) c.height = gridH;

    for (var i = 0; i < n; i++) {
      var v = wearmap[i];
      var ratio = denom > 0 ? v / denom : 0;
      /* GitHub 贡献图配色：5 级 */
      var hue, sat, lgt;
      if (ratio === 0)       { hue = 210; sat = "10%"; lgt = "22%"; }
      else if (ratio < 0.25) { hue = 130; sat = "55%"; lgt = "42%"; }
      else if (ratio < 0.50) { hue = 100; sat = "65%"; lgt = "38%"; }
      else if (ratio < 0.75) { hue = 45;  sat = "75%"; lgt = "44%"; }
      else                   { hue = 5;   sat = "70%"; lgt = "42%"; }

      ctx.fillStyle = "hsl(" + hue + "," + sat + "," + lgt + ")";
      var col = i % cols;
      var row = Math.floor(i / cols);
      var x = 10 + col * (cellSize + gap);
      var y = 10 + row * (cellSize + gap);
      roundRect(ctx, x, y, cellSize, cellSize, 2);

      ctx.fillStyle = ratio > 0.6 ? "#fff" : "#8b97ad";
      ctx.font = "9px sans-serif";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      if (v > 0 && cellSize >= 11)
        ctx.fillText(String(v), x + cellSize / 2, y + cellSize / 2);
    }

    /* 底部图例 */
    var ly = rows * (cellSize + gap) + 18;
    var levels = [
      [0,   "#1e2a3a", "0"],
      [0.15,"#7ec86e", "低"],
      [0.4, "#56a33b", "中低"],
      [0.65,"#d4a72c", "中高"],
      [1,   "#e05555", "高"],
    ];
    var lw = 36, lx = 10;
    for (var li = 0; li < levels.length; li++) {
      ctx.fillStyle = levels[li][1];
      roundRect(ctx, lx, ly, lw, 12, 3);
      ctx.fillStyle = "#8b97ad";
      ctx.font = "10px sans-serif";
      ctx.textAlign = "center";
      ctx.fillText(levels[li][2], lx + lw / 2, ly + 24);
      lx += lw + 8;
    }
    ctx.textAlign = "left";
    ctx.fillStyle = "#8b97ad";
    ctx.font = "11px sans-serif";
    ctx.fillText("共 " + n + " 块 · 寿命 " + denom + " · 最高 " + maxCycle, lx + 10, ly + 16);

    els.wearLegend.textContent =
      "GitHub 风格热力图：颜色 = 当前擦写次数 / 标称寿命，越接近寿命上限颜色越深。";
    els.wearWrap.classList.remove("hidden");
  }

  /* 辅助：圆角矩形 */
  function roundRect(ctx, x, y, w, h, r) {
    r = Math.min(r, w / 2, h / 2);
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.arcTo(x + w, y, x + w, y + r, r);
    ctx.lineTo(x + w, y + h - r);
    ctx.arcTo(x + w, y + h, x + h - r, y + h, r);
    ctx.lineTo(x + r, y + h);
    ctx.arcTo(x, y + h, x, y + h - r, r);
    ctx.lineTo(x, y + r);
    ctx.arcTo(x, y, x + r, y, r);
    ctx.closePath();
    ctx.fill();
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

    // 运行时日志窗口：清空并切到"运行中"
    els.rtLogBody.innerHTML = "";
    els.rtLogStatus.textContent = "运行中…";
    els.rtLogStatus.className = "rt-log-status running";
    var collected = [];      // 收集到的日志行 {level, text}
    var statsLine = null;    // STATS_JSON 行（若有）
    var wearRaw = null;      // WEARMAP 行（若有）
    var doneResult = {};       // done 事件中的汇总结果

    var body = JSON.stringify({
      framework: selectedId, config: cfg.config, test_config: cfg.test_config
    });

    // 用 fetch + ReadableStream 消费 SSE（支持 POST，且便于超时/中断控制）
    fetch("/api/run/stream", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: body
    }).then(function (resp) {
      if (!resp.ok) throw new Error("HTTP " + resp.status);
      var reader = resp.body.getReader();
      var dec = new TextDecoder("utf-8");
      var buf = "";
      var pendingLines = [];
      var evName = "log";

      function appendLog(level, text) {
        collected.push({ level: level, text: text });
        appendOut(els.rtLogBody, level, text);
        // 轻量节流滚动：每 50ms 滚一次底，避免高频重绘卡 UI
        if (!appendLog._t) {
          appendLog._t = setTimeout(function () {
            appendLog._t = null;
            els.rtLogBody.scrollTop = els.rtLogBody.scrollHeight;
          }, 50);
        }
        // 捕获 STATS_JSON / WEARMAP 汇总行（用前缀长度切分，避免 off-by-one）
        if (text.indexOf("STATS_JSON:") === 0) {
          statsLine = text.slice("STATS_JSON:".length).trim();
        }
        if (text.indexOf("WEARMAP:") === 0) {
          wearRaw = text.slice("WEARMAP:".length).trim();
        }
      }

      function pump() {
        return reader.read().then(function (r) {
          if (r.done) return;
          buf += dec.decode(r.value, { stream: true });
          // 按 SSE 帧切分（event:/data:/空行）
          var frames = buf.split("\n\n");
          buf = frames.pop();
          frames.forEach(function (fr) {
            var ev = "log", data = "";
            fr.split("\n").forEach(function (ln) {
              if (ln.indexOf("event:") === 0) ev = ln.slice(6).trim();
              else if (ln.indexOf("data:") === 0) data += ln.slice(5).trim();
            });
            if (!data) return;
            var obj;
            try { obj = JSON.parse(data); } catch (e) { return; }
            if (ev === "end") return;
            if (ev === "log") {
              appendLog(obj.level || "info", obj.text || "");
            } else if (ev === "build") {
              appendLog("info", obj.text || "");
            } else if (ev === "done") {
              doneResult = obj.result || {};
            }
          });
          return pump();
        });
      }
      return pump();
    }).then(function () {
      // 流结束：用收集到的日志渲染最终结果区，并统计通过/失败
      finalizeRun(collected, statsLine, wearRaw, doneResult);
    }).catch(function (e) {
      appendOut(els.rtLogBody, "stderr", "请求失败：" + e);
      els.rtLogStatus.textContent = "错误";
      els.rtLogStatus.className = "rt-log-status fail";
      setPill("fail", "错误");
      els.runBtn.disabled = false;
    });

    // 中断：若用户在运行中再次点击（这里简单提供 cancel），当前实现在
    // fetch 完成前 runBtn 禁用，无法直接重入，避免并发。
  }

  /*
   * 流式日志收集完成后：把日志渲染成最终结果区，统计 OK/FAIL 并更新
   * 顶部状态、磨损图与性能图（若日志中含 STATS_JSON/WEARMAP）。
   */
  function finalizeRun(lines, statsLine, wearRaw, doneResult) {
    var okCount = 0, failCount = 0;
    els.output.innerHTML = "";
    lines.forEach(function (ln) {
      if (ln.level === "ok") okCount++;
      else if (ln.level === "fail") failCount++;
      appendOut(els.output, ln.level, ln.text);
    });

    // 优先使用后端 done 事件返回的汇总（更可靠），否则用日志行统计兜底
    okCount = doneResult && typeof doneResult.ok_count === "number"
              ? doneResult.ok_count : okCount;
    failCount = doneResult && typeof doneResult.fail_count === "number"
              ? doneResult.fail_count : failCount;

    var ok = failCount === 0;
    els.summary.classList.remove("hidden");
    els.statOk.textContent = okCount;
    els.statFail.textContent = failCount;
    els.statTime.textContent = (doneResult && typeof doneResult.elapsed_ms === "number")
      ? doneResult.elapsed_ms + " ms" : "?";
    els.verdict.className = "verdict " + (ok ? "ok" : "fail");
    els.verdict.textContent = ok ? "✓ 全部通过" : "✗ 存在失败";
    setPill(ok ? "ok" : "fail", ok ? "通过" : "失败");
    els.meta.textContent = "已选：" + nameOf(selectedId);

    // 解析 STATS_JSON 渲染性能/磨损
    var stats = null;
    if (statsLine) {
      try { stats = JSON.parse(statsLine); } catch (e) { stats = null; }
    }
    if (stats) {
      renderPerfStats(stats);
      if (wearRaw) renderWearMap(parseWearList(wearRaw), stats.erase_cycles);
    }
    els.rtLogStatus.textContent = ok ? "完成（通过）" : "完成（失败）";
    els.rtLogStatus.className = "rt-log-status " + (ok ? "ok" : "fail");
    els.runBtn.disabled = false;
  }

  // 把 "1,2,3" 形式的 WEARMAP 解析成数组
  function parseWearList(s) {
    return s.split(",").map(function (x) {
      var n = parseInt(x, 10);
      return isNaN(n) ? 0 : n;
    });
  }

  // ---------- 应用层测试（统一任务引擎 + 适配层） ----------
  var appEls = {
    config: document.getElementById("app-config"),
    runBtn: document.getElementById("app-run-btn"),
    runAllBtn: document.getElementById("app-run-all-btn"),
    result: document.getElementById("app-result"),
    rtStatus: document.getElementById("app-rt-status"),
    rtBody: document.getElementById("app-rt-body"),
    rtClear: document.getElementById("app-rt-clear"),
    output: document.getElementById("app-output"),
    wearWrap: document.getElementById("app-wear"),
    wearCanvas: document.getElementById("app-wear-canvas"),
  };

  function renderAppConfig() {
    appEls.config.innerHTML = "";
    if (!appTaskSchema.length) {
      appEls.config.innerHTML = '<p class="hint">后端未返回应用层测试配置。</p>';
      return;
    }
    // 组件选择（仅支持应用层测试的组件）
    var compRow = makeLabel("组件");
    var compSel = document.createElement("select");
    compSel.className = "select";
    compSel.id = "app-component";
    frameworks.forEach(function (f) {
      if (!f.app_supported) return;
      var opt = document.createElement("option");
      opt.value = f.id;
      opt.textContent = f.name;
      compSel.appendChild(opt);
    });
    compRow.appendChild(compSel);
    appEls.config.appendChild(compRow);

    // 任务与参数（来自后端 app_task_schema）
    appTaskSchema.forEach(function (p) {
      var row = makeLabel(p.label);
      row.style.gridColumn = "auto";
      if (p.type === "select") {
        var sel = document.createElement("select");
        sel.dataset.key = p.key;
        (p.options || []).forEach(function (o) {
          var opt = document.createElement("option");
          opt.value = o[0]; opt.textContent = o[1];
          if (String(o[0]) === String(p.default)) opt.selected = true;
          sel.appendChild(opt);
        });
        row.appendChild(sel);
      } else {
        var inp = document.createElement("input");
        inp.type = "number";
        inp.dataset.key = p.key;
        inp.value = p.default;
        if (p.min != null) inp.min = p.min;
        if (p.max != null) inp.max = p.max;
        if (p.step != null) inp.step = p.step;
        row.appendChild(inp);
      }
      appEls.config.appendChild(row);
    });
  }

  function makeLabel(text) {
    var row = document.createElement("label");
    row.className = "cfg-row";
    var span = document.createElement("span");
    span.textContent = text;
    row.appendChild(span);
    return row;
  }

  function collectAppConfig() {
    var app_config = {};
    appEls.config.querySelectorAll("input[data-key],select[data-key]").forEach(function (inp) {
      app_config[inp.dataset.key] = inp.type === "number" ? Number(inp.value) : inp.value;
    });
    return { framework: appEls.config.querySelector("#app-component").value,
             config: {}, app_config: app_config };
  }

  /* 通用 SSE 消费（与 runTest 相同的帧解析逻辑） */
  function streamConsume(reader, onEvent) {
    var dec = new TextDecoder("utf-8");
    var buf = "";
    function pump() {
      return reader.read().then(function (r) {
        if (r.done) return;
        buf += dec.decode(r.value, { stream: true });
        var frames = buf.split("\n\n");
        buf = frames.pop();
        frames.forEach(function (fr) {
          var ev = "log", data = "";
          fr.split("\n").forEach(function (ln) {
            if (ln.indexOf("event:") === 0) ev = ln.slice(6).trim();
            else if (ln.indexOf("data:") === 0) data += ln.slice(5).trim();
          });
          if (!data) return;
          try { onEvent(ev, JSON.parse(data)); } catch (e) {}
        });
        return pump();
      });
    }
    return pump();
  }

  function appAppendLog(level, text) {
    var span = document.createElement("span");
    span.className = "line-" + level;
    span.textContent = text + "\n";
    appEls.rtBody.appendChild(span);
    appEls.rtBody.scrollTop = appEls.rtBody.scrollHeight;
    if (text.indexOf("STATS_JSON:") === 0) {
      try { appRenderStats(JSON.parse(text.slice("STATS_JSON:".length).trim())); } catch (e) {}
    }
    if (text.indexOf("WEARMAP:") === 0) {
      appRenderWear(text.slice("WEARMAP:".length).trim());
    }
  }

  function appRun(componentOverride) {
    var body = collectAppConfig();
    if (componentOverride) body.framework = componentOverride;
    appEls.result.classList.remove("hidden");
    appEls.output.innerHTML = "";
    appEls.rtBody.innerHTML = "";
    appEls.rtStatus.textContent = "运行中…";
    appEls.rtStatus.className = "rt-log-status running";
    appEls.wearWrap.classList.add("hidden");
    appEls.runBtn.disabled = true;
    appEls.runAllBtn.disabled = true;

    fetch("/api/app/run/stream", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    }).then(function (resp) {
      if (!resp.ok) throw new Error("HTTP " + resp.status);
      var reader = resp.body.getReader();
      var allLines = [];
      return streamConsume(reader, function (ev, obj) {
        if (ev === "log") {
          allLines.push({ level: obj.level || "info", text: obj.text || "" });
          appAppendLog(obj.level || "info", obj.text || "");
        } else if (ev === "done") {
          var res = obj.result || {};
          allLines.forEach(function (l) {
            var span = document.createElement("span");
            span.className = "line-" + l.level;
            span.textContent = l.text + "\n";
            appEls.output.appendChild(span);
          });
          if (res.error) {
            var e = document.createElement("span");
            e.className = "line-fail";
            e.textContent = "错误：" + res.error + "\n";
            appEls.output.appendChild(e);
          }
          var ok = !res.error && (!res.lost || res.lost === 0);
          appEls.rtStatus.textContent = ok ? "完成（通过）" : "完成（失败）";
          appEls.rtStatus.className = "rt-log-status " + (ok ? "ok" : "fail");
        }
      });
    }).catch(function (e) {
      appAppendLog("stderr", "请求失败：" + e);
      appEls.rtStatus.textContent = "错误";
      appEls.rtStatus.className = "rt-log-status fail";
    }).then(function () {
      appEls.runBtn.disabled = false;
      appEls.runAllBtn.disabled = false;
    });
  }

  /* 批量跑全部支持应用层测试的组件（串行，日志打标签） */
  function appRunAll() {
    var comps = frameworks.filter(function (f) { return f.app_supported; });
    if (!comps.length) return;
    var seq = 0;
    appEls.result.classList.remove("hidden");
    appEls.rtBody.innerHTML = "";
    appEls.rtStatus.textContent = "批量运行中…";
    appEls.rtStatus.className = "rt-log-status running";
    appEls.runBtn.disabled = true;
    appEls.runAllBtn.disabled = true;

    function next() {
      if (seq >= comps.length) {
        appEls.rtStatus.textContent = "批量完成";
        appEls.rtStatus.className = "rt-log-status ok";
        appEls.runBtn.disabled = false;
        appEls.runAllBtn.disabled = false;
        return;
      }
      var f = comps[seq++];
      appAppendLog("info", "===== [" + seq + "/" + comps.length + "] 组件: " + f.name + " =====");
      fetch("/api/app/run/stream", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ framework: f.id, config: {}, app_config: collectAppConfig().app_config })
      }).then(function (resp) {
        var reader = resp.body.getReader();
        return streamConsume(reader, function (ev, obj) {
          if (ev === "log") appAppendLog(obj.level || "info", "[" + f.id + "] " + obj.text);
        });
      }).catch(function (e) {
        appAppendLog("stderr", "[" + f.id + "] 请求失败：" + e);
      }).then(next);
    }
    next();
  }

  function appRenderStats(stats) {
    var items = [
      ["模式", stats.mode], ["操作数", stats.ops], ["数据丢失", stats.lost],
      ["墙钟(us)", stats.wall_us], ["介质阻塞(us)", stats.block_us],
      ["读/写/擦", (stats.reads || 0) + " / " + (stats.writes || 0) + " / " + (stats.erases || 0)],
      ["有效写入(B)", stats.app_bytes], ["介质写入(B)", stats.media_bytes],
      ["写放大", stats.write_amp], ["吞吐(ops/s)", stats.ops_per_sec],
      ["吞吐(KB/s)", stats.kbps], ["最大擦写", stats.max_cycles],
      ["平均擦写", stats.avg_cycles], ["坏块", stats.bad_blocks],
    ];
    var html = '<div class="perf-grid">';
    items.forEach(function (it) {
      if (it[1] === undefined || it[1] === null) return;
      html += '<div class="perf-item"><span class="perf-num">' + it[1] +
              '</span><span class="perf-label">' + it[0] + "</span></div>";
    });
    html += "</div>";
    var w = document.createElement("div");
    w.innerHTML = '<h3>应用层性能统计</h3>' + html;
    var old = appEls.output.querySelector(".perf-grid");
    var h = appEls.output.querySelector("h3");
    if (h) { h.textContent = "应用层性能统计"; h.parentNode.insertBefore(w.firstChild, h); }
    if (old) old.replaceWith(w.lastChild);
    else appEls.output.insertAdjacentHTML("afterbegin", w.outerHTML);
  }

  function appRenderWear(raw) {
    var map = raw.split(",").map(function (x) { var n = parseInt(x, 10); return isNaN(n) ? 0 : n; });
    if (!map.length) return;
    var c = appEls.wearCanvas, ctx = c.getContext("2d");
    var W = c.width, H = c.height;
    ctx.clearRect(0, 0, W, H);
    var maxCycle = Math.max.apply(null, map.concat([1]));
    var n = map.length, cellSize = 13, gap = 3;
    var cols = Math.floor((W - 20) / (cellSize + gap));
    if (cols < 4) cols = 4;
    var rows = Math.ceil(n / cols);
    for (var i = 0; i < n; i++) {
      var ratio = map[i] / maxCycle;
      var hue = ratio === 0 ? 210 : (ratio < .25 ? 130 : ratio < .5 ? 100 : ratio < .75 ? 45 : 5);
      ctx.fillStyle = "hsl(" + hue + ", 60%, 40%)";
      var x = 10 + (i % cols) * (cellSize + gap);
      var y = 10 + Math.floor(i / cols) * (cellSize + gap);
      ctx.fillRect(x, y, cellSize, cellSize);
      if (map[i] > 0) {
        ctx.fillStyle = "#fff";
        ctx.font = "9px sans-serif";
        ctx.textAlign = "center";
        ctx.fillText(String(map[i]), x + cellSize / 2, y + cellSize / 2);
      }
    }
    appEls.wearWrap.classList.remove("hidden");
  }

  appEls.runBtn.onclick = function () { appRun(null); };
  appEls.runAllBtn.onclick = appRunAll;
  appEls.rtClear.onclick = function () {
    appEls.rtBody.innerHTML = '<span class="hint">日志已清空。</span>';
    appEls.output.innerHTML = "";
  };

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
        appendOut(els.genOutput, "info", "包内含 README.md / PORTING.md / HAL_CONTRACT.md / AI_PORTING_PROMPT.md + 零依赖内存 HAL 自检 demo + manifest.json");
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
  els.rtLogClear.onclick = function () {
    els.rtLogBody.innerHTML = '<span class="hint">日志已清空。</span>';
  };
  loadFrameworks();
})();
