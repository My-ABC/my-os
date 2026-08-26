---
layout: default
---

<div style="text-align: right; padding: 10px;">
  <select id="langSelect" onchange="switchLanguage()" style="
    background: #1a1a1a;
    color: #00ff41;
    border: 1px solid #00ff41;
    padding: 5px 10px;
    border-radius: 4px;
    cursor: pointer;
    font-size: 14px;
  ">
    <option value="/my-os/">English</option>
    <option value="/my-os/zh/" selected>中文</option>
  </select>
</div>

<script>
function switchLanguage() {
  var select = document.getElementById('langSelect');
  window.location.href = select.value;
}
</script>

<script src="https://cdn.jsdelivr.net/npm/marked/marked.min.js"></script>

<div id="content"></div>

<script>
fetch('https://raw.githubusercontent.com/My-ABC/my-os/main/README-zh.md')
  .then(response => response.text())
  .then(text => {
    document.getElementById('content').innerHTML = marked.parse(text);
  })
  .catch(error => {
    document.getElementById('content').innerHTML = '<p>加载 README 失败: ' + error + '</p>';
  });
</script>