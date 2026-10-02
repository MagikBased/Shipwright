const auth = document.querySelector("#auth");
const dashboard = document.querySelector("#dashboard");
const notice = document.querySelector("#notice");
const logout = document.querySelector("#logout");

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
  });
  const body = response.status === 204 ? null : await response.json();
  if (!response.ok) throw new Error(body?.error?.message || `Request failed (${response.status})`);
  return body;
}

function formJson(form) { return Object.fromEntries(new FormData(form).entries()); }
function showNotice(message, error = false) { notice.textContent = message; notice.style.color = error ? "#ff8e8e" : ""; }

async function loadDashboard() {
  try {
    const [user, stats, words, devices] = await Promise.all([
      api("/v1/me"), api("/v1/me/stats"), api("/v1/me/words"), api("/v1/me/devices"),
    ]);
    auth.classList.add("hidden");
    dashboard.classList.remove("hidden");
    logout.classList.remove("hidden");
    document.querySelector("#greeting").textContent = `${user.displayName}'s learning overview`;
    const metrics = [
      [stats.uniqueWords, "unique words"], [stats.encounters, "encounters"],
      [stats.savedWords, "saved"], [stats.games, "games"], [stats.connectedDevices, "devices"],
    ];
    document.querySelector("#stats").innerHTML = metrics.map(([value, label]) =>
      `<div class="stat"><strong>${value}</strong><span>${label}</span></div>`).join("");
    document.querySelector("#words").innerHTML = words.length ? words.slice(0, 30).map(word =>
      `<div class="word"><span>${escapeHtml(word.wordId)}</span><span>${word.encounterCount}×</span>` +
      `<span class="${word.saved ? "saved" : ""}">${word.saved ? "saved" : ""}</span></div>`).join("") :
      '<p class="muted">No synchronized words yet.</p>';
    document.querySelector("#devices").innerHTML = devices.length ? devices.map(device =>
      `<div class="device"><div>${escapeHtml(device.deviceName)}<small>${escapeHtml(device.gameId)}</small></div>` +
      `<button class="quiet revoke-device" data-device-id="${escapeHtml(device.id)}" type="button">Revoke</button></div>`
    ).join("") : '<p class="muted">No connected mods yet.</p>';
  } catch (_) {
    auth.classList.remove("hidden"); dashboard.classList.add("hidden"); logout.classList.add("hidden");
  }
}

function escapeHtml(value) {
  const node = document.createElement("span"); node.textContent = value; return node.innerHTML;
}

for (const [id, path] of [["login-form", "/v1/auth/login"], ["register-form", "/v1/auth/register"]]) {
  document.querySelector(`#${id}`).addEventListener("submit", async event => {
    event.preventDefault();
    try { await api(path, { method: "POST", body: JSON.stringify(formJson(event.currentTarget)) }); showNotice(""); await loadDashboard(); }
    catch (error) { showNotice(error.message, true); }
  });
}

document.querySelector("#pair-form").addEventListener("submit", async event => {
  event.preventDefault();
  try {
    const result = await api("/v1/device-pairings/approve", { method: "POST", body: JSON.stringify(formJson(event.currentTarget)) });
    showNotice(`${result.deviceName} is approved. Return to the game to finish connecting.`);
    event.currentTarget.reset();
  } catch (error) { showNotice(error.message, true); }
});

document.querySelector("#download-manifest").addEventListener("click", async () => {
  try {
    const manifest = await api("/v1/me/exports/saved-words");
    const blob = new Blob([JSON.stringify(manifest, null, 2)], { type: "application/json" });
    const link = document.createElement("a"); link.href = URL.createObjectURL(blob);
    link.download = "jp_assist_cloud_progress.json"; link.click(); URL.revokeObjectURL(link.href);
  } catch (error) { showNotice(error.message, true); }
});

document.querySelector("#devices").addEventListener("click", async event => {
  const button = event.target.closest(".revoke-device");
  if (!button) return;
  try {
    await api(`/v1/me/devices/${encodeURIComponent(button.dataset.deviceId)}`, { method: "DELETE" });
    showNotice("Device access revoked."); await loadDashboard();
  } catch (error) { showNotice(error.message, true); }
});

logout.addEventListener("click", async () => {
  try { await api("/v1/auth/logout", { method: "POST" }); } finally { await loadDashboard(); }
});

loadDashboard();
