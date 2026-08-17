// frontend/app.js - 模拟运行 + 代码生成 + 导入 前端逻辑

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

  // ---------- 代码生成 ----------
  function renderGenSelect() {
    els.genFramework.innerHTML = "";
    frameworks.forEach(function (f) {
      var opt = document.createElement("option");
      opt.value = f.id;
      opt.textContent = f.name;
      els.genFramework.appendChild(opt);
    });
    renderGenParams();
  }

  function currentGenParams() {
    for (var i = 0; i < frameworks.length; i++) {
      if (frameworks[i].id === els.genFramework.value) {
        return frameworks[i].params || [];
      }
    }
    return [];
  }

  function renderGenParams() {
    els.genParams.innerHTML = "";
    currentGenParams().forEach(function (p) {
      var wrap = document.createElement("label");
      wrap.className = "param";
      wrap.innerHTML = '<span>' + p.label + '</span>';
      var inp = document.createElement("input");
      inp.type = "number";
      inp.value = p.default;
      inp.dataset.key = p.key;
      wrap.appendChild(inp);
      els.genParams.appendChild(wrap);
    });
  }

  function genDownload() {
    var fid = els.genFramework.value;
    var params = {};
    els.genParams.querySelectorAll("input[data-key]").forEach(function (inp) {
      params[inp.dataset.key] = Number(inp.value);
    });
    els.genOutput.innerHTML = '<span class="hint">正在生成库文件…</span>';
    fetch("/api/generate", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ framework: fid, params: params }),
    }).then(function (r) {
      if (!r.ok) { return r.json().then(function (j) { throw new Error(j.error || "生成失败"); }); }
      var cd = r.headers.get("Content-Disposition") || "";
      var m = cd.match(/filename="?([^"]+)"?/);
      var fname = m ? m[1] : "library.zip";
      return r.blob().then(function (b) { return { b: b, fname: fname }; });
    }).then(function (o) {
      var url = URL.createObjectURL(o.b);
      var a = document.createElement("a");
      a.href = url;
      a.download = o.fname;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      appendOut(els.genOutput, "ok", "已生成并下载：" + o.fname);
      appendOut(els.genOutput, "info", "包内含 .c/.h + test_main.c + PORTING.md + manifest.json");
      appendOut(els.genOutput, "info", "下一步：在③中上传此 zip 进行导入校验。");
    }).catch(function (e) {
      appendOut(els.genOutput, "stderr", "生成失败：" + e.message);
    });
  }

  // ---------- 导入 ----------
  function doImport() {
    var file = els.importFile.files[0];
    if (!file) {
      appendOut(els.importOutput, "stderr", "请先选择要导入的 zip 文件");
      return;
    }
    els.importOutput.innerHTML = '<span class="hint">正在上传并校验…</span>';
    var reader = new FileReader();
    reader.onload = function () {
      var b64 = arrayBufferToBase64(reader.result);
      api("POST", "/api/import", { file_b64: b64 }).then(function (res) {
        els.importOutput.innerHTML = "";
        if (res.success) {
          appendOut(els.importOutput, "ok", "✓ " + (res.message || "导入成功"));
          if (res.warning) { appendOut(els.importOutput, "stderr", res.warning); }
          appendOut(els.importOutput, "info", "框架「" + res.name + "」已注册");
          appendOut(els.importOutput, "info", "点击①中的「刷新框架」即可看到并运行。");
          loadFrameworks();
        } else {
          appendOut(els.importOutput, "fail", "✗ " + (res.error || "导入失败"));
          (res.lines || []).forEach(function (l) {
            appendOut(els.importOutput, l.level, l.text);
          });
        }
      }).catch(function (e) {
        appendOut(els.importOutput, "stderr", "请求失败：" + e);
      });
    };
    reader.readAsArrayBuffer(file);
  }

  function arrayBufferToBase64(buf) {
    var bytes = new Uint8Array(buf);
    var bin = "";
    for (var i = 0; i < bytes.length; i++) { bin += String.fromCharCode(bytes[i]); }
    return btoa(bin);
  }

  // ---------- 运行测试（同前） ----------
  function renderOutput(result) {
    els.output.innerHTML = "";
    var lines = result.lines || [];
    if (lines.length === 0 && result.error) {
      appendOut(els.output, "stderr", "错误：" + result.error);
    }
    lines.forEach(function (l) { appendOut(els.output, l.level, l.text); });
  }

  function appendOut(el, level, text) {
    var span = document.createElement("span");
    span.className = "line-" + level;
    span.textContent = text + "\n";
    el.appendChild(span);
    el.scrollTop = el.scrollHeight;
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
      appendOut(els.output, "stderr", "请求失败：" + e);
      setPill("fail", "错误");
      els.runBtn.disabled = false;
    });
  }

  els.genFramework.onchange = renderGenParams;
  els.genBtn.onclick = genDownload;
  els.importBtn.onclick = doImport;
  els.runBtn.onclick = runTest;
  els.refreshBtn.onclick = loadFrameworks;
  loadFrameworks();
})();
