(function () {
  "use strict";

  const viewer = document.querySelector("[data-viewer]");
  const orbit = document.querySelector("[data-product-orbit]");
  const loading = document.querySelector("[data-viewer-loading]");
  if (!viewer || !orbit) return;

  const image = orbit.querySelector("img");
  const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
  let targetX = 0;
  let targetY = -3;
  let currentX = 0;
  let currentY = -3;
  let dragging = false;
  let startClientX = 0;
  let startClientY = 0;
  let startTargetX = 0;
  let startTargetY = 0;
  let lastInteraction = performance.now();

  function markReady() {
    loading?.classList.add("is-hidden");
  }

  if (!image || image.complete) markReady();
  else {
    image.addEventListener("load", markReady, { once: true });
    image.addEventListener("error", function () {
      loading.textContent = "Static product visual unavailable";
    }, { once: true });
  }

  function supportsWebGL() {
    try {
      const canvas = document.createElement("canvas");
      return Boolean(canvas.getContext("webgl") || canvas.getContext("experimental-webgl"));
    } catch (_error) {
      return false;
    }
  }

  if (!supportsWebGL()) {
    viewer.classList.add("viewer-static");
    orbit.setAttribute("aria-label", "Static concept visual of EchoDesk Mini. WebGL is unavailable; no model is required for this image-based preview.");
  }

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function pointerDown(event) {
    if (reducedMotion.matches) return;
    dragging = true;
    startClientX = event.clientX;
    startClientY = event.clientY;
    startTargetX = targetX;
    startTargetY = targetY;
    lastInteraction = performance.now();
    orbit.setPointerCapture?.(event.pointerId);
  }

  function pointerMove(event) {
    if (!dragging) return;
    const deltaX = event.clientX - startClientX;
    const deltaY = event.clientY - startClientY;
    targetY = clamp(startTargetY + deltaX * 0.075, -13, 13);
    targetX = clamp(startTargetX - deltaY * 0.055, -7, 7);
    lastInteraction = performance.now();
    if (Math.abs(deltaX) > Math.abs(deltaY)) event.preventDefault();
  }

  function pointerUp(event) {
    dragging = false;
    orbit.releasePointerCapture?.(event.pointerId);
  }

  orbit.addEventListener("pointerdown", pointerDown);
  orbit.addEventListener("pointermove", pointerMove);
  orbit.addEventListener("pointerup", pointerUp);
  orbit.addEventListener("pointercancel", pointerUp);

  orbit.addEventListener("keydown", function (event) {
    const keys = ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home"];
    if (!keys.includes(event.key)) return;
    event.preventDefault();
    if (event.key === "ArrowLeft") targetY -= 2;
    if (event.key === "ArrowRight") targetY += 2;
    if (event.key === "ArrowUp") targetX -= 1.5;
    if (event.key === "ArrowDown") targetX += 1.5;
    if (event.key === "Home") { targetX = 0; targetY = -3; }
    targetY = clamp(targetY, -13, 13);
    targetX = clamp(targetX, -7, 7);
    lastInteraction = performance.now();
  });

  function animate(now) {
    if (!reducedMotion.matches) {
      if (!dragging && now - lastInteraction > 2600) {
        targetY = Math.sin(now / 2600) * 4.5;
        targetX = Math.cos(now / 3400) * 1.2;
      }
      currentX += (targetX - currentX) * 0.08;
      currentY += (targetY - currentY) * 0.08;
      orbit.style.transform = `rotateX(${currentX.toFixed(2)}deg) rotateY(${currentY.toFixed(2)}deg) translateZ(8px)`;
    }
    window.requestAnimationFrame(animate);
  }

  window.requestAnimationFrame(animate);
})();
