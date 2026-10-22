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
  const themes = JSON.parse(document.querySelector("#watchface-catalog").textContent);
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
      sheet.src = `${assetRoot}${selected.kinds[kind].file}`;
      sheet.style.removeProperty("--crop-x");
      sheet.style.removeProperty("--crop-y");
    }
    preview.setAttribute("aria-label", translate("{theme} {kind}表盘的原生界面预览", {
      theme: selected.title, kind: translate(selected.kinds[kind].label),
    }));
    title.textContent = `${selected.title} / ${translate(selected.kinds[kind].label)}`;
    document.querySelector("#face-state").textContent = translate(selected.kinds[kind].preview ? "本地新设计 · 尚未发布" : "已有表盘");
    document.querySelectorAll("[data-face-id]").forEach(button => {
      button.setAttribute("aria-pressed", String(button.dataset.faceTheme === theme && Number(button.dataset.faceKind) === kind));
    });
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
  // 图鉴与大图共用一份目录和选择状态，不另外维护表盘绘制。
  // The gallery and large preview share one catalog and selection state, with no parallel renderer.
  document.querySelector("#face-gallery").addEventListener("click", event => {
    const card = event.target.closest("[data-face-id]");
    if (!card) return;
    theme = card.dataset.faceTheme;
    kind = Number(card.dataset.faceKind);
    render();
    document.querySelector(".face-explorer").scrollIntoView({block: "start", behavior: "instant"});
    themeButtons.find(button => button.dataset.theme === theme).focus({preventScroll: true});
  });
  const filters = [...document.querySelectorAll("[data-face-filter]")];
  filters.forEach(button => button.addEventListener("click", () => {
    filters.forEach(item => item.setAttribute("aria-pressed", String(item === button)));
    document.querySelectorAll("[data-face-id]").forEach(card => {
      card.hidden = button.dataset.faceFilter !== "all" && card.dataset.faceState !== button.dataset.faceFilter;
    });
    document.querySelectorAll("[data-face-group]").forEach(group => {
      const visible = group.querySelectorAll("[data-face-id]:not([hidden])").length;
      group.hidden = visible === 0;
      group.querySelector(".face-group-heading span").textContent = `${String(visible).padStart(2, "0")} DESIGNS`;
    });
  }));
  render();
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
