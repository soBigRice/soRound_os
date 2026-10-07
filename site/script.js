"use strict";

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
    sheet.src = `assets/${theme}.png`;
    sheet.style.setProperty("--crop-x", `${-x / 466 * 100}%`);
    sheet.style.setProperty("--crop-y", `${-y / 466 * 100}%`);
    preview.setAttribute("aria-label", `${themes[theme].title} ${kinds[kind]}表盘的原生界面预览`);
    title.textContent = themes[theme].title;
    description.textContent = themes[theme].description;
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
