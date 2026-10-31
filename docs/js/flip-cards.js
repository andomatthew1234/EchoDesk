(function () {
  "use strict";

  document.querySelectorAll("[data-flip-card]").forEach(function (card) {
    card.addEventListener("click", function () {
      const isFlipped = card.classList.toggle("is-flipped");
      card.setAttribute("aria-pressed", String(isFlipped));
    });
  });
})();
