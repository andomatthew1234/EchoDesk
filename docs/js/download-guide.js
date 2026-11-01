(() => {
  const guide = document.querySelector("[data-download-guide]");
  if (!guide) return;

  const downloadUrl = guide.dataset.autoDownload;
  const status = guide.querySelector("[data-download-status]");
  const manualOnly = new URLSearchParams(window.location.search).get("manual") === "1";

  if (!downloadUrl || manualOnly) {
    if (manualOnly && status) status.textContent = "Automatic download paused. Use the direct download link below.";
    return;
  }

  window.setTimeout(() => {
    const trigger = document.createElement("a");
    trigger.href = downloadUrl;
    trigger.download = "";
    trigger.hidden = true;
    trigger.setAttribute("aria-hidden", "true");
    document.body.append(trigger);
    trigger.click();
    guide.dataset.downloadStarted = "true";
    if (status) status.textContent = "Your download has started.";
    window.setTimeout(() => trigger.remove(), 5000);
  }, 450);
})();
