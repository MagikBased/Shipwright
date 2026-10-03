let noticeTimer;

export function escapeHtml(value) {
  const node = document.createElement("span");
  node.textContent = value ?? "";
  return node.innerHTML;
}

export function showNotice(message, error = false) {
  const notice = document.querySelector("#notice");
  clearTimeout(noticeTimer);
  notice.textContent = message;
  notice.classList.toggle("error", error);
  if (message && !error) {
    noticeTimer = setTimeout(() => { notice.textContent = ""; }, 5000);
  }
}

export function downloadJson(value, filename) {
  const link = document.createElement("a");
  link.href = URL.createObjectURL(new Blob([JSON.stringify(value, null, 2)], { type: "application/json" }));
  link.download = filename;
  link.click();
  URL.revokeObjectURL(link.href);
}

export function confirmAction(title, message, confirmLabel = "Confirm", danger = false) {
  const dialog = document.querySelector("#confirm-dialog");
  const accept = document.querySelector("#confirm-accept");
  document.querySelector("#confirm-title").textContent = title;
  document.querySelector("#confirm-message").textContent = message;
  accept.textContent = confirmLabel;
  accept.classList.toggle("danger-button", danger);
  dialog.showModal();
  return new Promise(resolve => {
    dialog.addEventListener("close", () => resolve(dialog.returnValue === "confirm"), { once: true });
  });
}

export function setBusy(button, busy, busyLabel = "Working…") {
  if (busy) {
    button.dataset.idleLabel = button.textContent;
    button.textContent = busyLabel;
    button.disabled = true;
  } else {
    button.textContent = button.dataset.idleLabel || button.textContent;
    button.disabled = false;
    delete button.dataset.idleLabel;
  }
}

export function skeletonCards(count = 4) {
  return Array.from({ length: count }, () => '<span class="skeleton-card" aria-hidden="true"></span>').join("");
}
