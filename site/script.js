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
  const themes = {
    type: { title: "TYPE", description: "让数字，直接成为主角。" },
    orbit: { title: "ORBIT", description: "环形刻度，把时间放回轨道。" },
    shift: { title: "SHIFT", description: "错位之间，找到时间的节奏。" },
  };
  const kinds = ["点阵", "大字", "环形", "天气", "图片"];
  // 466px fixtures are arranged at these coordinates on each 1680 × 1220 sheet.
  const coordinates = [[60, 145], [607, 145], [1154, 145], [334, 709], [881, 709]];
  const sheet = document.querySelector("#face-sheet");
  const preview = document.querySelector("#face-preview");
  const title = document.querySelector("#face-title");
  const description = document.querySelector("#face-description");
  const error = document.querySelector("#preview-error");
  const themeButtons = [...document.querySelectorAll("[data-theme]")];
  const kindButtons = [...document.querySelectorAll("[data-kind]")];
  let theme = "orbit";
  let kind = 2;

  function render() {
    const [x, y] = coordinates[kind];
    sheet.src = `${assetRoot}${theme}.png`;
    sheet.style.setProperty("--crop-x", `${-x / 466 * 100}%`);
    sheet.style.setProperty("--crop-y", `${-y / 466 * 100}%`);
    preview.setAttribute("aria-label", translate("{theme} {kind}表盘的原生界面预览", {
      theme: themes[theme].title, kind: translate(kinds[kind]),
    }));
    title.textContent = themes[theme].title;
    description.textContent = translate(themes[theme].description);
    themeButtons.forEach(button => button.setAttribute("aria-pressed", String(button.dataset.theme === theme)));
    kindButtons.forEach(button => button.setAttribute("aria-pressed", String(Number(button.dataset.kind) === kind)));
    error.hidden = true;
  }

  themeButtons.forEach(button => button.addEventListener("click", () => {
    theme = button.dataset.theme;
    render();
  }));
  kindButtons.forEach(button => button.addEventListener("click", () => {
    kind = Number(button.dataset.kind);
    render();
  }));
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
