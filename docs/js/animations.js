(function () {
  "use strict";

  const root = document.documentElement;
  let started = false;

  function startAnimations() {
    if (started) return;
    started = true;

    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
    const word = document.querySelector("[data-flip-word]");
    const words = ["Desk", "Game", "Productivity", "Speed", "Setup"];
    let index = 0;

    if (word && !reducedMotion.matches) {
      window.setInterval(function () {
        word.classList.remove("is-entering");
        word.classList.add("is-exiting");
        window.setTimeout(function () {
          index = (index + 1) % words.length;
          word.textContent = words[index];
          word.classList.remove("is-exiting");
          void word.offsetWidth;
          word.classList.add("is-entering");
        }, 300);
      }, 2700);
    }

    const revealItems = document.querySelectorAll(".reveal");
    if (reducedMotion.matches || !("IntersectionObserver" in window)) {
      revealItems.forEach(function (item) { item.classList.add("is-visible"); });
      return;
    }

    const observer = new IntersectionObserver(function (entries, instance) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        entry.target.classList.add("is-visible");
        instance.unobserve(entry.target);
      });
    }, { rootMargin: "0px 0px -7%", threshold: 0.08 });

    revealItems.forEach(function (item) { observer.observe(item); });
  }

  if (root.classList.contains("page-ready") || !root.classList.contains("page-loading")) {
    startAnimations();
  } else {
    window.addEventListener("echodesk:page-ready", startAnimations, { once: true });
  }
})();
