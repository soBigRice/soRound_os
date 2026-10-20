"use strict";

const siteMessages = JSON.parse(document.querySelector("#site-messages").textContent);
const siteEnglish = document.documentElement.lang === "en";
const assetRoot = siteEnglish ? "../assets/" : "assets/";
const appRoot = assetRoot + (siteEnglish ? "apps/en/" : "apps/");
function translate(message, values = {}) {
  return (siteMessages[message] ?? message).replace(/\{(\w+)\}/g, (_, key) => values[key]);
}
document.querySelector(".language-switch").addEventListener("click", event => {
  event.currentTarget.hash = window.location.hash;
});

(() => {
  const legacyKinds = ["点阵", "大字", "环形", "天气", "图片"].map(label => ({label}));
  const themes = {
    type: { title: "TYPE", description: "让数字，直接成为主角。", family: "legacy", kinds: legacyKinds },
    orbit: { title: "ORBIT", description: "环形刻度，把时间放回轨道。", family: "legacy", kinds: legacyKinds },
    shift: { title: "SHIFT", description: "错位之间，找到时间的节奏。", family: "legacy", kinds: legacyKinds },
    hand: { title: "HAND", description: "六种指针表达，让时间自成风景。", family: "hand", kinds: [
      {label: "刻度", file: "hand-mark"}, {label: "弧线", file: "hand-arc"},
      {label: "数字", file: "hand-numeral"}, {label: "轨道", file: "hand-orbit"},
      {label: "框架", file: "hand-frame"}, {label: "圆点", file: "hand-dots"},
    ] },
    studio: { title: "STUDIO", description: "器械、玩物与自然，九种新的时间想象。", family: "studio", kinds: [
      {label: "雷达", file: "active-radar"}, {label: "磁带", file: "active-tape"},
      {label: "光圈", file: "active-iris"}, {label: "迷宫秒", file: "active-maze"},
      {label: "翻页", file: "active-fold"}, {label: "像素夜营", file: "active-pixel-camp"},
      {label: "地平线", file: "active-horizon"}, {label: "潮汐", file: "active-tide"},
      {label: "年轮", file: "active-growth"},
    ] },
  };
  // 旧三组只使用既有合辑坐标；HAND/STUDIO 各自加载完整原生 PNG。
  // Only the legacy groups use contact-sheet coordinates; HAND/STUDIO load complete native PNGs.
  const coordinates = [[60, 145], [607, 145], [1154, 145], [334, 709], [881, 709]];
  const sheet = document.querySelector("#face-sheet");
  const preview = document.querySelector("#face-preview");
  const title = document.querySelector("#face-title");
  const description = document.querySelector("#face-description");
  const error = document.querySelector("#preview-error");
  const themeButtons = [...document.querySelectorAll("[data-theme]")];
  const kindPicker = document.querySelector(".kind-picker");
  let theme = "orbit";
  let kind = 2;
  let renderedFamily = "legacy";

  function render() {
    const selected = themes[theme];
    // 保留暂停开放时的保护，已启用组照常渲染。 / Keep the availability guard for a temporarily disabled group.
    if (selected.available === false) return;
    if (selected.family !== renderedFamily) {
      const buttons = selected.kinds.map((item, index) => {
        const button = document.createElement("button");
        button.type = "button";
        button.dataset.kind = String(index);
        button.textContent = translate(item.label);
        return button;
      });
      kindPicker.replaceChildren(...buttons);
      renderedFamily = selected.family;
    }
    if (selected.family === "legacy") {
      const [x, y] = coordinates[kind];
      sheet.className = "native-sheet";
      sheet.width = 1680;
      sheet.height = 1220;
      sheet.src = `${assetRoot}${theme}.png`;
      sheet.style.setProperty("--crop-x", `${-x / 466 * 100}%`);
      sheet.style.setProperty("--crop-y", `${-y / 466 * 100}%`);
    } else {
      sheet.className = "native-face";
      sheet.width = 466;
      sheet.height = 466;
      const directory = selected.family === "hand" ? "hands" : "studio";
      sheet.src = `${assetRoot}${directory}/${selected.kinds[kind].file}.png`;
      sheet.style.removeProperty("--crop-x");
      sheet.style.removeProperty("--crop-y");
    }
    preview.setAttribute("aria-label", translate("{theme} {kind}表盘的原生界面预览", {
      theme: selected.title, kind: translate(selected.kinds[kind].label),
    }));
    title.textContent = selected.title;
    description.textContent = translate(selected.description);
    themeButtons.forEach(button => button.setAttribute("aria-pressed", String(button.dataset.theme === theme)));
    [...kindPicker.children].forEach(button => button.setAttribute("aria-pressed", String(Number(button.dataset.kind) === kind)));
    error.hidden = true;
  }

  themeButtons.forEach(button => button.addEventListener("click", () => {
    const next = button.dataset.theme;
    if (button.disabled || themes[next].available === false) return;
    if (themes[next].family !== themes[theme].family) kind = 0;
    theme = next;
    render();
  }));
  kindPicker.addEventListener("click", event => {
    const button = event.target.closest("[data-kind]");
    if (!button || !kindPicker.contains(button)) return;
    kind = Number(button.dataset.kind);
    render();
  });
  sheet.addEventListener("error", () => { error.hidden = false; });
  sheet.addEventListener("load", () => { error.hidden = true; });
})();

(() => {
  const buttons = [...document.querySelectorAll("[data-app]")];
  const screen = document.querySelector("#app-screen");
  const title = document.querySelector("#app-title");
  const description = document.querySelector("#app-description");
  const position = document.querySelector("#app-position");
  const error = document.querySelector("#app-error");

  function select(button, index) {
    screen.src = `${appRoot}${button.dataset.app}.png`;
    screen.alt = translate("{name}的 LVGL 原生界面预览", {name: button.textContent});
    title.textContent = button.textContent;
    description.textContent = button.dataset.description;
    position.textContent = `${String(index + 1).padStart(2, "0")} / ${buttons.length}`;
    buttons.forEach(item => item.setAttribute("aria-pressed", String(item === button)));
    error.hidden = true;
  }
  buttons.forEach((button, index) => button.addEventListener("click", () => {
    select(button, index);
    if (window.matchMedia("(max-width: 760px)").matches) {
      screen.scrollIntoView({block: "center", behavior: window.matchMedia("(prefers-reduced-motion: reduce)").matches ? "instant" : "smooth"});
    }
  }));
  document.querySelectorAll("[data-preview-app]").forEach(link => link.addEventListener("click", () => {
    const index = buttons.findIndex(button => button.dataset.app === link.dataset.previewApp);
    select(buttons[index], index);
  }));
  screen.addEventListener("error", () => { error.hidden = false; });
  screen.addEventListener("load", () => { error.hidden = true; });
})();
