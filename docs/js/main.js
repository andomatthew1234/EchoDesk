(function () {
  "use strict";

  const root = document.documentElement;
  const header = document.querySelector("[data-header]");
  const menu = document.querySelector("[data-mobile-menu]");
  const menuToggle = document.querySelector("[data-menu-toggle]");
  const themeToggle = document.querySelector("[data-theme-toggle]");
  const themeMeta = document.querySelector('meta[name="theme-color"]');
  const year = document.querySelector("[data-year]");
  let menuWasOpened = false;

  function savedTheme() {
    try {
      return window.localStorage.getItem("echodesk-theme");
    } catch (_error) {
      return null;
    }
  }

  function setTheme(theme, persist) {
    const next = theme === "dark" ? "dark" : "light";
    root.dataset.theme = next;
    if (themeToggle) {
      themeToggle.setAttribute("aria-label", next === "dark" ? "Switch to light theme" : "Switch to dark theme");
    }
    if (themeMeta) themeMeta.content = next === "dark" ? "#0a0a0b" : "#f5f4ef";
    if (persist) {
      try {
        window.localStorage.setItem("echodesk-theme", next);
      } catch (_error) {
        // The visual toggle still works when storage is unavailable.
      }
    }
  }

  const preferredTheme = savedTheme() || (window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
  setTheme(preferredTheme, false);

  themeToggle?.addEventListener("click", function () {
    setTheme(root.dataset.theme === "dark" ? "light" : "dark", true);
  });

  function closeMenu(returnFocus) {
    if (!menu || !menuToggle) return;
    header?.classList.remove("is-menu-open");
    document.body.classList.remove("menu-open");
    menuToggle.setAttribute("aria-expanded", "false");
    menuToggle.setAttribute("aria-label", "Open navigation");
    menu.hidden = true;
    if (returnFocus && menuWasOpened) menuToggle.focus();
  }

  function openMenu() {
    if (!menu || !menuToggle) return;
    menuWasOpened = true;
    menu.hidden = false;
    header?.classList.add("is-menu-open");
    document.body.classList.add("menu-open");
    menuToggle.setAttribute("aria-expanded", "true");
    menuToggle.setAttribute("aria-label", "Close navigation");
    menu.querySelector("a")?.focus();
  }

  menuToggle?.addEventListener("click", function () {
    if (menu?.hidden) openMenu(); else closeMenu(false);
  });

  menu?.addEventListener("click", function (event) {
    if (event.target.closest("a")) closeMenu(false);
  });

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && menu && !menu.hidden) closeMenu(true);
    if (event.key !== "Tab" || !menu || menu.hidden) return;
    const focusable = Array.from(menu.querySelectorAll("a[href], button:not([disabled])"));
    focusable.unshift(menuToggle);
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  });

  window.addEventListener("resize", function () {
    if (window.innerWidth > 1100) closeMenu(false);
  });

  let ticking = false;
  function updateHeader() {
    header?.classList.toggle("is-scrolled", window.scrollY > 18);
    ticking = false;
  }
  window.addEventListener("scroll", function () {
    if (!ticking) {
      window.requestAnimationFrame(updateHeader);
      ticking = true;
    }
  }, { passive: true });
  updateHeader();

  if (year) year.textContent = String(new Date().getFullYear());
})();
