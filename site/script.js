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

const siteMotion = (() => {
  const preference = window.matchMedia("(prefers-reduced-motion: reduce)");
  const active = new Map();
  let observer;

  function cancel(element) {
    active.get(element)?.cancel();
  }
  function play(element, frames, options = {}) {
    cancel(element);
    if (preference.matches || !element?.animate) return;
    // 内容默认可见；动画只临时覆盖样式，取消或API不可用时仍可操作。
    // Content stays visible by default; temporary effects never gate access to the page.
    let animation;
    try {
      animation = element.animate(frames, {duration: 440, easing: "cubic-bezier(.22, 1, .36, 1)", ...options});
    } catch (error) {
      console.warn("Optional motion is unavailable; content remains visible.", error);
      return;
    }
    active.set(element, animation);
    const release = () => {
      if (active.get(element) === animation) active.delete(element);
      animation.onfinish = animation.oncancel = null;
      animation.cancel();
    };
    animation.onfinish = animation.oncancel = release;
  }
  function screen(element) {
    play(element, [{opacity: .45, clipPath: "circle(47% at 50% 50%)"},
      {opacity: 1, clipPath: "circle(50% at 50% 50%)"}], {duration: 280});
  }
  function stop() {
    observer?.disconnect();
    [...active.values()].forEach(animation => animation.cancel());
  }
  preference.addEventListener("change", event => { if (event.matches) stop(); });
  window.addEventListener("pagehide", stop, {once: true});

  if (!preference.matches && typeof Element.prototype.animate === "function") {
    play(document.querySelector(".hero-copy"), [{opacity: 0, transform: "translateY(18px)"},
      {opacity: 1, transform: "translateY(0)"}], {duration: 680});
    // 以圆形裁切展开外圈和红点，保持原生截图的比例与像素内容。
    // Reveal the ring and red dot with a circular mask without warping the native screen image.
    play(document.querySelector(".hero-orbit"), [{opacity: .25, clipPath: "circle(42% at 50% 50%)"},
      {opacity: 1, clipPath: "circle(74% at 50% 50%)"}], {duration: 900});
    if (typeof window.IntersectionObserver === "function") {
      const targets = document.querySelectorAll(".spec-strip, .section-lead, .face-explorer, .face-collection, .feature-columns, .app-explorer, .start-grid, .author-grid");
      let remaining = targets.length;
      try {
        observer = new IntersectionObserver(entries => {
          entries.forEach(entry => {
            if (!entry.isIntersecting) return;
            observer.unobserve(entry.target);
            play(entry.target, [{opacity: 0, transform: "translateY(16px)"},
              {opacity: 1, transform: "translateY(0)"}], {duration: 560});
            if (--remaining === 0) observer.disconnect();
          });
        }, {threshold: .08});
        targets.forEach(target => observer.observe(target));
      } catch (error) {
        observer?.disconnect();
        console.warn("Optional scroll entrances are unavailable; content remains visible.", error);
      }
    }
  }
  return {play, cancel, screen};
})();

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
  const gallery = document.querySelector("#face-gallery");
  const collection = document.querySelector(".face-collection");
  const collectionTrigger = document.querySelector(".collection-trigger");
  const collectionPanel = document.querySelector(".collection-panel");
  const collectionClose = document.querySelector(".collection-close");
  const steps = [...document.querySelectorAll("[data-face-step]")];
  const explorer = document.querySelector(".face-explorer");
  const playback = document.querySelector("#face-playback");
  const progress = document.querySelector(".face-playback-progress > span");
  let dialog;
  // 原生对话框负责焦点与Escape；不支持时保留默认收起的details，无JS也可浏览。
  // Native dialog handles focus and Escape; collapsed details remains the no-JS/older-browser fallback.
  if (typeof HTMLDialogElement !== "undefined" && typeof HTMLDialogElement.prototype.showModal === "function") {
    dialog = document.createElement("dialog");
    dialog.className = "face-dialog";
    dialog.id = "face-library-dialog";
    dialog.setAttribute("aria-labelledby", "collection-title");
    collectionClose.hidden = false;
    collectionClose.autofocus = true;
    dialog.append(collectionPanel);
    document.body.append(dialog);
    collectionTrigger.setAttribute("aria-controls", dialog.id);
    collectionTrigger.setAttribute("aria-haspopup", "dialog");
    collectionTrigger.setAttribute("aria-expanded", "false");
    collectionTrigger.addEventListener("click", event => {
      event.preventDefault();
      pausePlayback();
      dialog.showModal();
      document.documentElement.classList.add("face-library-open");
      collectionTrigger.setAttribute("aria-expanded", "true");
    });
    collectionClose.addEventListener("click", () => dialog.close());
    dialog.addEventListener("click", event => {
      const bounds = dialog.getBoundingClientRect();
      if (event.target === dialog && (event.clientX < bounds.left || event.clientX > bounds.right ||
          event.clientY < bounds.top || event.clientY > bounds.bottom)) dialog.close();
    });
    dialog.addEventListener("close", () => {
      document.documentElement.classList.toggle("face-library-open", dialog.open);
      collectionTrigger.setAttribute("aria-expanded", String(dialog.open));
    });
  }
  document.querySelector(".face-pagination").hidden = false;
  let theme = "orbit";
  let kind = 2;
  let renderedPage = "";
  let animateImage = false;
  let automaticTransition = false;
  const motionPreference = window.matchMedia("(prefers-reduced-motion: reduce)");
  const sequence = Object.entries(themes).filter(([, group]) => group.available !== false)
    .flatMap(([key, group]) => group.kinds.map((_, index) => ({theme: key, kind: index})));
  let wantsPlayback = !motionPreference.matches;
  let inView = false;
  let hovering = false;
  let pageActive = true;
  let playbackTimer;
  let progressAnimation;
  let playbackObserver;
  playback.hidden = false;
  progress.parentElement.hidden = false;

  function clearPlaybackCycle() {
    window.clearTimeout(playbackTimer);
    playbackTimer = undefined;
    progressAnimation?.cancel();
    progressAnimation = undefined;
  }
  function syncPlayback() {
    clearPlaybackCycle();
    playback.dataset.playing = String(wantsPlayback);
    playback.setAttribute("aria-label", translate(wantsPlayback ? "暂停自动播放" : "开始自动播放"));
    playback.querySelector("span").textContent = translate(wantsPlayback ? "暂停" : "播放");
    playback.querySelector("[data-playback-pause]").toggleAttribute("hidden", !wantsPlayback);
    playback.querySelector("[data-playback-play]").toggleAttribute("hidden", wantsPlayback);
    const active = wantsPlayback && inView && pageActive && !hovering && !document.hidden &&
      !dialog?.open && !collection.open && sheet.complete && sheet.naturalWidth > 0;
    // 自动轮播不反复播报标题；手动浏览仍保留原有的礼貌播报。
    // Suppress repetitive announcements during rotation; retain polite updates for manual browsing.
    document.querySelector(".face-description").setAttribute("aria-live", wantsPlayback ? "off" : "polite");
    document.querySelector("#face-position").setAttribute("aria-live", wantsPlayback ? "off" : "polite");
    if (!active || sequence.length < 2) return;
    if (!motionPreference.matches && typeof progress.animate === "function") {
      progressAnimation = progress.animate([{transform: "scaleX(0)"}, {transform: "scaleX(1)"}],
        {duration: 5000, easing: "linear", fill: "forwards"});
    }
    // 单个有界计时器在图片就绪后计时；隐藏、离屏或用户操作时立即释放。
    // One bounded timer starts only after image readiness and is released on hiding or interaction.
    playbackTimer = window.setTimeout(() => {
      const index = sequence.findIndex(face => face.theme === theme && face.kind === kind);
      const next = sequence[(index + 1) % sequence.length];
      theme = next.theme;
      kind = next.kind;
      render(true, true);
    }, 5000);
  }
  function pausePlayback() {
    wantsPlayback = false;
    syncPlayback();
  }
  function revealPreview() {
    if (automaticTransition) {
      siteMotion.play(preview, [{opacity: .25, transform: "translateX(14px) scale(.985)"},
        {opacity: 1, transform: "translateX(0) scale(1)"}], {duration: 600});
    } else siteMotion.screen(preview);
  }

  function render(animate = true, automatic = false) {
    const selected = themes[theme];
    // 保留暂停开放时的保护，已启用组照常渲染。 / Keep the availability guard for a temporarily disabled group.
    if (selected.available === false) return;
    // 首页只显示当前系列的五张缩略图；图鉴复用同一原生素材与选择状态。
    // Show at most five thumbnails on the homepage, sharing native artwork and state with the library.
    const start = Math.min(Math.floor(kind / 5) * 5, Math.max(0, selected.kinds.length - 5));
    const pageKey = `${theme}:${start}`;
    if (pageKey !== renderedPage) {
      const buttons = selected.kinds.slice(start, start + 5).map((item, offset) => {
        const index = start + offset;
        const button = document.createElement("button");
        button.type = "button";
        button.dataset.kind = String(index);
        button.setAttribute("aria-controls", "face-preview");
        const screen = gallery.querySelector(`[data-face-theme="${theme}"][data-face-kind="${index}"] .native-screen`).cloneNode(true);
        const name = document.createElement("span");
        name.textContent = translate(item.label);
        button.append(screen, name);
        return button;
      });
      kindPicker.replaceChildren(...buttons);
      renderedPage = pageKey;
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
    document.querySelector("#face-state").textContent = translate(selected.kinds[kind].new ? "本次新增 · beta.38" : "已发布表盘");
    document.querySelectorAll("[data-face-id]").forEach(button => {
      button.setAttribute("aria-pressed", String(button.dataset.faceTheme === theme && Number(button.dataset.faceKind) === kind));
    });
    description.textContent = translate(selected.description);
    themeButtons.forEach(button => button.setAttribute("aria-pressed", String(button.dataset.theme === theme)));
    [...kindPicker.children].forEach(button => button.setAttribute("aria-pressed", String(Number(button.dataset.kind) === kind)));
    document.querySelector("#face-position").textContent = `${String(kind + 1).padStart(2, "0")} / ${String(selected.kinds.length).padStart(2, "0")}`;
    steps.forEach(button => { button.disabled = Number(button.dataset.faceStep) < 0 ? kind === 0 : kind === selected.kinds.length - 1; });
    error.hidden = true;
    siteMotion.cancel(preview);
    animateImage = animate;
    automaticTransition = automatic;
    if (animateImage && sheet.complete && sheet.naturalWidth) {
      revealPreview();
      animateImage = false;
    }
    if (automatic) siteMotion.play(document.querySelector(".face-description"),
      [{opacity: .4, transform: "translateY(8px)"}, {opacity: 1, transform: "translateY(0)"}], {duration: 500});
    syncPlayback();
  }

  themeButtons.forEach(button => button.addEventListener("click", () => {
    const next = button.dataset.theme;
    if (button.disabled || themes[next].available === false) return;
    pausePlayback();
    if (themes[next].family !== themes[theme].family) kind = 0;
    theme = next;
    render();
  }));
  kindPicker.addEventListener("click", event => {
    const button = event.target.closest("[data-kind]");
    if (!button || !kindPicker.contains(button)) return;
    pausePlayback();
    kind = Number(button.dataset.kind);
    render();
  });
  steps.forEach(button => button.addEventListener("click", () => {
    pausePlayback();
    kind += Number(button.dataset.faceStep);
    render();
  }));
  // 图鉴与大图共用一份目录和选择状态，不另外维护表盘绘制。
  // The gallery and large preview share one catalog and selection state, with no parallel renderer.
  gallery.addEventListener("click", event => {
    const card = event.target.closest("[data-face-id]");
    if (!card) return;
    pausePlayback();
    theme = card.dataset.faceTheme;
    kind = Number(card.dataset.faceKind);
    render();
    if (dialog?.open) dialog.close();
    collection.open = false;
    document.querySelector(".face-explorer").scrollIntoView({block: "start", behavior: "instant"});
    kindPicker.querySelector('[aria-pressed="true"]').focus({preventScroll: true});
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
      const grid = group.querySelector(".face-grid");
      siteMotion.cancel(grid);
      if (visible) siteMotion.play(grid, [{opacity: .35, transform: "translateY(6px)"},
        {opacity: 1, transform: "translateY(0)"}], {duration: 240});
    });
  }));
  playback.addEventListener("click", () => {
    wantsPlayback = !wantsPlayback;
    // 明确点击播放立即生效，随后再次移入展示区才触发悬停暂停。
    // An explicit play request takes effect immediately; a later pointer entry pauses again.
    hovering = false;
    syncPlayback();
  });
  explorer.addEventListener("pointerenter", event => {
    if (event.pointerType !== "mouse") return;
    hovering = true;
    syncPlayback();
  });
  explorer.addEventListener("pointerleave", event => {
    if (event.pointerType !== "mouse") return;
    hovering = false;
    syncPlayback();
  });
  explorer.addEventListener("focusin", event => {
    if (!playback.contains(event.target)) pausePlayback();
  });
  collection.addEventListener("toggle", () => { if (collection.open) pausePlayback(); });
  document.addEventListener("visibilitychange", syncPlayback);
  motionPreference.addEventListener("change", event => {
    if (event.matches) pausePlayback();
  });
  if (typeof IntersectionObserver === "function") {
    playbackObserver = new IntersectionObserver(entries => {
      inView = entries[0].isIntersecting && entries[0].intersectionRatio >= .25;
      syncPlayback();
    }, {threshold: .25});
    playbackObserver.observe(explorer);
  } else inView = true;
  window.addEventListener("pagehide", () => {
    pageActive = false;
    clearPlaybackCycle();
    playbackObserver?.disconnect();
  });
  window.addEventListener("pageshow", () => {
    pageActive = true;
    playbackObserver?.observe(explorer);
    syncPlayback();
  });
  render(false);
  sheet.addEventListener("error", () => { pausePlayback(); error.hidden = false; });
  sheet.addEventListener("load", () => {
    error.hidden = true;
    if (animateImage) revealPreview();
    animateImage = false;
    syncPlayback();
  });
})();

(() => {
  const buttons = [...document.querySelectorAll("[data-app]")];
  const screen = document.querySelector("#app-screen");
  const title = document.querySelector("#app-title");
  const description = document.querySelector("#app-description");
  const position = document.querySelector("#app-position");
  const error = document.querySelector("#app-error");
  let animateImage = false;

  function select(button, index) {
    screen.src = `${appRoot}${button.dataset.app}.png`;
    screen.alt = translate("{name}的 LVGL 原生界面预览", {name: button.textContent});
    title.textContent = button.textContent;
    description.textContent = button.dataset.description;
    position.textContent = `${String(index + 1).padStart(2, "0")} / ${buttons.length}`;
    buttons.forEach(item => item.setAttribute("aria-pressed", String(item === button)));
    error.hidden = true;
    siteMotion.cancel(screen);
    animateImage = true;
    if (screen.complete && screen.naturalWidth) {
      siteMotion.screen(screen);
      animateImage = false;
    }
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
  screen.addEventListener("load", () => {
    error.hidden = true;
    if (animateImage) siteMotion.screen(screen);
    animateImage = false;
  });
})();
