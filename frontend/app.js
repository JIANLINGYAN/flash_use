// frontend/app.js - 模拟运行界面前端逻辑
// 流程：加载框架列表 -> 选择 -> 调用 /api/run -> 渲染结果

(function () {
  "use strict";

  var frameworks = [];
  var selectedId = null;

  var els = {
    list: document.getElementById("framework-list"),
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
      if (frameworks.length > 0 && !selectedId) {
        select(frameworks[0].id);
      }
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

  function select(id) {
    selectedId = id;
    renderList();
    els.runBtn.disabled = false;
    els.meta.textContent = "已选：" + (nameOf(id) || id);
  }

  function nameOf(id) {
    for (var i = 0; i < frameworks.length; i++) {
      if (frameworks[i].id === id) { return frameworks[i].name; }
    }
    return null;
  }

  function renderOutput(result) {
    els.output.innerHTML = "";
    var lines = result.lines || [];
    if (lines.length === 0 && result.error) {
      appendLine("stderr", "错误：" + result.error);
    }
    lines.forEach(function (l) {
      appendLine(l.level, l.text);
    });
  }

  function appendLine(level, text) {
    var span = document.createElement("span");
    span.className = "line-" + level;
    span.textContent = text + "\n";
    els.output.appendChild(span);
  }

  function runTest() {
    if (!selectedId) { return; }
    setPill("running", "运行中…");
    els.runBtn.disabled = true;
    els.meta.textContent = "正在运行：" + nameOf(selectedId);
    els.summary.classList.add("hidden");
    els.output.innerHTML = '<span class="hint">编译并运行测试程序，请稍候…</span>';

    api("POST", "/api/run", { framework: selectedId }).then(function (result) {
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
      els.runBtn.disabled = false;
    }).catch(function (e) {
      appendLine("stderr", "请求失败：" + e);
      setPill("fail", "错误");
      els.runBtn.disabled = false;
    });
  }

  els.runBtn.onclick = runTest;
  els.refreshBtn.onclick = loadFrameworks;
  loadFrameworks();
})();
