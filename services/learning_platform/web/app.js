import { confirmAction, downloadJson, escapeHtml, setBusy, showNotice, skeletonCards } from "./ui.js";
import { applyAnkiPlan, collectAnkiReviews, preflightAnki } from "./anki_sync.js";

const auth = document.querySelector("#auth");
const dashboard = document.querySelector("#dashboard");
const logout = document.querySelector("#logout");
const WORD_PAGE_SIZE = 50;
const state = { user: null, stats: null, goals: null, words: [], wordsOffset: 0, wordsHaveMore: false, wordRequestVersion: 0, activity: { days: [], games: [] }, devices: [], sessions: [], queue: [], reviewIndex: 0, reviewCollection: null, ankiCollection: null, ankiPlan: null };
const connectionStatus = document.querySelector("#connection-status");

function setConnectionStatus(message = "", retry = false) {
  connectionStatus.querySelector("span").textContent = message;
  const button = connectionStatus.querySelector("button");
  button.classList.toggle("hidden", !retry);
  button.onclick = retry ? () => loadDashboard() : null;
  connectionStatus.classList.toggle("hidden", !message);
}

window.addEventListener("offline", () => setConnectionStatus("You are offline. Changes that need the account service will wait until the connection returns."));
window.addEventListener("online", () => {
  setConnectionStatus("Connection restored. Refreshing account data…");
  loadDashboard().catch(() => {});
});

async function api(path, options = {}) {
  const response = await fetch(path, { ...options, headers: { "Content-Type": "application/json", ...(options.headers || {}) } });
  const body = response.status === 204 ? null : await response.json();
  if (!response.ok) throw new Error(body?.error?.message || `Request failed (${response.status})`);
  return body;
}
function formJson(form) { return Object.fromEntries(new FormData(form).entries()); }

async function loadDashboard() {
  let user;
  try {
    user = await api("/v1/me");
  } catch (_) {
    auth.classList.remove("hidden"); dashboard.classList.add("hidden"); logout.classList.add("hidden");
    return;
  }
  auth.classList.add("hidden"); dashboard.classList.remove("hidden"); logout.classList.remove("hidden");
  dashboard.setAttribute("aria-busy", "true");
  document.querySelector("#stats").innerHTML = skeletonCards(6);
  document.querySelector("#games").innerHTML = skeletonCards(2);
  document.querySelector("#learning-states").innerHTML = skeletonCards(4);
  try {
    const [stats, activity, goals, devices, sessions, reviewCollection] = await Promise.all([
      api("/v1/me/stats"), api("/v1/me/activity"), api("/v1/me/goals"), api("/v1/me/devices"), api("/v1/me/sessions"), api("/v1/me/review-collection"),
    ]);
    Object.assign(state, { user, stats, activity, goals, devices, sessions, reviewCollection });
    document.querySelectorAll(".account-username").forEach(input => { input.value = user.email; });
    document.querySelector("#greeting").textContent = `${user.displayName}'s learning overview`;
    renderOverview(goals); renderConnections(); await populateGames(); renderReviewOwner(); await loadWords();
    if (navigator.onLine) setConnectionStatus();
  } catch (error) {
    showNotice(`Your account is signed in, but some data could not be loaded: ${error.message}`, true);
    setConnectionStatus("Some account data could not be loaded.", true);
  } finally {
    dashboard.setAttribute("aria-busy", "false");
  }
}

function renderOverview(goals) {
  const metrics = [[state.stats.uniqueWords,"unique words"],[state.stats.savedWords,"saved"],[state.stats.dueReviews,"due reviews"],[state.stats.encounters,"encounters"],[state.stats.activityStreakDays,"day streak"],[state.stats.games,"games"]];
  document.querySelector("#stats").innerHTML = metrics.map(([value,label]) => `<div class="stat"><strong>${value}</strong><span>${label}</span></div>`).join("");
  const max = Math.max(1, ...state.activity.days.map(day => day.events + day.reviews));
  document.querySelector("#activity-chart").innerHTML = state.activity.days.length ? state.activity.days.map(day => { const label=`${day.date}: ${day.events} game events, ${day.reviews} reviews`; return `<div class="activity-day" role="img" aria-label="${escapeHtml(label)}" style="height:${Math.max(3,(day.events+day.reviews)/max*100)}%" title="${escapeHtml(label)}"></div>`; }).join("") : '<p class="muted">Activity will appear after your mod syncs.</p>';
  document.querySelector("#games").innerHTML = state.activity.games.length ? state.activity.games.map(game => `<div class="card-row"><div><strong>${escapeHtml(game.gameId)}</strong><small>${game.uniqueWords} words · ${game.encounters} encounters</small></div><span>${escapeHtml(game.lastSeenAt.slice(0,10))}</span></div>`).join("") : '<p class="muted">No game activity yet.</p>';
  const stateTotal = Math.max(1, Object.values(state.stats.learningStates).reduce((sum,value) => sum + value, 0));
  document.querySelector("#learning-states").innerHTML = ["new","learning","known","ignored"].map(label => { const value=state.stats.learningStates[label]||0; return `<div><span><strong>${label}</strong><small>${value}</small></span><progress value="${value}" max="${stateTotal}">${value}</progress></div>`; }).join("");
  const form = document.querySelector("#goals-form"); form.dailyNewWords.value = goals.dailyNewWords; form.dailyReviews.value = goals.dailyReviews; form.remindersEnabled.checked = goals.remindersEnabled; form.timezone.value = goals.updatedAt ? goals.timezone : (Intl.DateTimeFormat().resolvedOptions().timeZone || goals.timezone);
  document.querySelector("#goal-progress").innerHTML = [
    ["New words", state.stats.newWordsToday, goals.dailyNewWords], ["Reviews", state.stats.reviewedToday, goals.dailyReviews],
  ].map(([label,value,target]) => `<div><span>${label}<strong>${value} / ${target}</strong></span><progress value="${Math.min(value,target)}" max="${Math.max(target,1)}">${value} of ${target}</progress></div>`).join("");
}

async function populateGames() {
  const options = state.activity.games.map(game => `<option value="${escapeHtml(game.gameId)}">${escapeHtml(game.gameId)}</option>`).join("");
  document.querySelector("#export-game").innerHTML = `<option value="">All games</option>${options}`;
  document.querySelector("#clear-game").innerHTML = options || '<option value="">No games</option>';
  await loadAnkiCollection();
}

async function loadWords(reset = true) {
  const container = document.querySelector("#words");
  const moreButton = document.querySelector("#load-more-words");
  const requestVersion = ++state.wordRequestVersion;
  if (reset) { state.wordsOffset = 0; state.words = []; container.innerHTML = skeletonCards(6); }
  container.setAttribute("aria-busy", "true"); moreButton.disabled = true;
  const query = new URLSearchParams({ search: document.querySelector("#word-search").value, sort: document.querySelector("#word-sort").value, limit: String(WORD_PAGE_SIZE + 1), offset: String(state.wordsOffset) });
  const learningState = document.querySelector("#word-state").value; if (learningState) query.set("learningState", learningState);
  if (document.querySelector("#saved-only").checked) query.set("savedOnly", "true");
  try {
    const results = await api(`/v1/me/words?${query}`);
    if (requestVersion !== state.wordRequestVersion) return;
    state.wordsHaveMore = results.length > WORD_PAGE_SIZE;
    const page = results.slice(0, WORD_PAGE_SIZE);
    state.words = reset ? page : state.words.concat(page);
    state.wordsOffset = state.words.length;
    renderWords();
  } finally {
    if (requestVersion === state.wordRequestVersion) { container.setAttribute("aria-busy", "false"); moreButton.disabled = false; }
  }
}
function renderWords() {
  document.querySelector("#words").innerHTML = state.words.length ? state.words.map((word,index) => {
    const dictionary = word.dictionary || {}; const title = dictionary.written || word.wordId; const detail = [dictionary.reading, dictionary.meaning].filter(Boolean).join(" · ");
    return `<button class="word-card" data-word-index="${index}" type="button"><span><strong>${escapeHtml(title)}</strong><small>${escapeHtml(detail || word.wordId)}</small><small>${word.tags.map(tag => `#${escapeHtml(tag)}`).join(" ")}</small></span><span><span class="pill">${escapeHtml(word.learningState)}</span>${word.saved?'<span class="pill saved">saved</span>':""}<small>${word.encounterCount}×</small></span></button>`;
  }).join("") : '<p class="muted">No words match these filters.</p>';
  document.querySelector("#load-more-words").classList.toggle("hidden", !state.wordsHaveMore);
}
function openWordEditor(index) {
  const word = state.words[index], form = document.querySelector("#word-editor");
  form.wordId.value = word.wordId; form.senseId.value = word.senseId || ""; form.learningState.value = word.learningState; form.tags.value = word.tags.join(", "); form.note.value = word.note;
  const dictionary = word.dictionary;
  document.querySelector("#editor-word").textContent = dictionary?.written || word.wordId;
  document.querySelector("#editor-details").innerHTML = `<div><strong>${escapeHtml(dictionary?.reading || "No reading")}</strong><span>${escapeHtml(dictionary?.partOfSpeech || "Unclassified")}</span></div><p>${escapeHtml(dictionary?.meaning || "No dictionary definition has been imported.")}</p><p class="small muted">Games: ${escapeHtml(word.gameIds.join(", ") || "unknown")} · ${word.encounterCount} encounters · ${word.selectionCount} selections<br>First seen ${escapeHtml(word.firstSeenAt.slice(0,10))} · Last seen ${escapeHtml(word.lastSeenAt.slice(0,10))}${dictionary?.source ? `<br>Source: ${escapeHtml(dictionary.source)}${dictionary.attribution ? ` — ${escapeHtml(dictionary.attribution)}` : ""}` : ""}</p>`;
  form.classList.remove("hidden"); form.scrollIntoView({ behavior: "smooth", block: "nearest" });
}

function renderConnections() {
  document.querySelector("#devices").innerHTML = state.devices.length ? state.devices.map(device => `<div class="card-row"><div><strong>${escapeHtml(device.deviceName)}</strong><small>${escapeHtml(device.gameId)} · last seen ${escapeHtml(device.lastSeenAt.slice(0,10))}</small></div><button class="quiet revoke-device" data-id="${escapeHtml(device.id)}">Revoke</button></div>`).join("") : '<p class="muted">No connected mods yet.</p>';
  document.querySelector("#sessions").innerHTML = state.sessions.map(session => `<div class="card-row"><div><strong>${session.current?"This session":"Website session"}</strong><small>Created ${escapeHtml(session.createdAt.slice(0,10))} · expires ${escapeHtml(session.expiresAt.slice(0,10))}</small></div><button class="quiet revoke-session" data-id="${escapeHtml(session.id)}">${session.current?"Sign out":"Revoke"}</button></div>`).join("");
}

async function loadReviewQueue() { state.queue = await api("/v1/me/reviews/queue?limit=100"); state.reviewIndex = 0; renderReview(false); }
function renderReviewOwner() {
  const owner = state.reviewCollection?.reviewOwner || "jp_assist";
  document.querySelector("#review-owner-note").textContent = owner === "anki" ? "Anki owns scheduling for this collection. Review due cards in Anki." : "JP Assist owns scheduling for this collection.";
}
function formatInterval(days) {
  const minutes = Math.round(days * 1440);
  if (minutes < 60) return `${Math.max(1, minutes)}m`;
  const hours = Math.round(days * 24);
  if (hours < 24) return `${hours}h`;
  if (days < 30) return `${Math.round(days)}d`;
  if (days < 365) return `${Math.round(days / 30)}mo`;
  return `${(days / 365).toFixed(1)}y`;
}
function renderReview(revealed) {
  const card = state.queue[state.reviewIndex], box = document.querySelector("#review-card"), actions = document.querySelector("#review-actions");
  if (!card) { box.disabled = true; box.innerHTML = `<span class="muted">${state.reviewCollection?.reviewOwner === "anki" ? "Anki owns this review queue." : "You are caught up. Save more words in-game or mark words as learning."}</span>`; actions.classList.add("hidden"); return; }
  box.disabled = false;
  box.innerHTML = `<div><div class="reading">${escapeHtml(card.reading)}</div><div class="written">${escapeHtml(card.written)}</div>${revealed?`<hr><div class="meaning">${escapeHtml(card.meaning || "No dictionary meaning imported")}</div><p>${escapeHtml(card.partOfSpeech)}</p>`:'<p class="muted">Click the card to reveal</p>'}</div>`;
  const labels = ["Again", "Hard", "Good", "Easy"];
  actions.querySelectorAll("[data-rating]").forEach((button, index) => {
    const preview = card.ratingPreviews?.find(item => item.rating === index + 1);
    button.innerHTML = `${labels[index]}${preview ? `<small>${formatInterval(preview.intervalDays)}</small>` : ""}`;
  });
  actions.classList.toggle("hidden", !revealed);
}

async function anki(action, params = {}) {
  const response = await fetch("http://127.0.0.1:8765", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action, version: 6, params }) });
  const result = await response.json(); if (result.error) throw new Error(result.error); return result.result;
}
function collectionQuery(game = document.querySelector("#export-game").value) { return game ? `?gameId=${encodeURIComponent(game)}` : ""; }
function updateAnkiHistoryAvailability() {
  const allGames = !document.querySelector("#export-game").value;
  const button = document.querySelector("#anki-history");
  button.disabled = !allGames || state.ankiCollection?.reviewOwner !== "anki";
  button.title = allGames ? "" : "Select All games to import the global Anki review history.";
}
async function loadAnkiCollection() {
  state.ankiCollection = await api(`/v1/me/review-collection${collectionQuery()}`);
  document.querySelector("#anki-owner").value = state.ankiCollection.reviewOwner;
  document.querySelector("#anki-deck").value = state.ankiCollection.ankiDeck;
  document.querySelector("#anki-last-sync").textContent = state.ankiCollection.lastAnkiSyncAt ? `Last successful sync ${state.ankiCollection.lastAnkiSyncAt.slice(0, 16).replace("T", " ")} UTC` : "Not synced yet";
  updateAnkiHistoryAvailability();
  state.ankiPlan = null; renderAnkiPlan();
}
async function saveAnkiCollection(reviewOwner, confirmed = false) {
  const gameId = document.querySelector("#export-game").value || null;
  state.ankiCollection = await api("/v1/me/review-collection", { method: "PUT", body: JSON.stringify({ gameId, reviewOwner, ankiDeck: document.querySelector("#anki-deck").value.trim(), confirmed }) });
  if (!gameId) { state.reviewCollection = state.ankiCollection; renderReviewOwner(); }
  updateAnkiHistoryAvailability();
  return state.ankiCollection;
}
function renderAnkiPlan() {
  const box = document.querySelector("#anki-preflight"), apply = document.querySelector("#anki-apply"), plan = state.ankiPlan;
  if (!plan) { box.classList.add("hidden"); apply.classList.add("hidden"); return; }
  const entries = [["Add",plan.counts.additions],["Update",plan.counts.updates],["Unchanged",plan.counts.unchanged],["Duplicates",plan.counts.duplicates],["Skipped",plan.counts.skipped],["Field migrations",plan.counts.fieldMigrations],["Conflicts",plan.counts.conflicts]];
  box.innerHTML = `<h3>Anki preflight</h3><div class="preflight-counts">${entries.map(([label,value])=>`<span><strong>${value}</strong>${label}</span>`).join("")}</div>${plan.blockers.length?`<ul class="error-list">${plan.blockers.map(item=>`<li>${escapeHtml(item)}</li>`).join("")}</ul>`:'<p class="muted small">Stable identities are unique and the sync can be applied safely.</p>'}`;
  box.classList.remove("hidden");
  apply.classList.toggle("hidden", plan.blockers.length > 0 || (plan.counts.additions + plan.counts.updates + plan.counts.fieldMigrations === 0));
}
async function checkAnki() {
  if (!document.querySelector("#anki-deck").value.trim()) throw new Error("Enter a deck name.");
  await saveAnkiCollection(document.querySelector("#anki-owner").value);
  const manifest = await api(`/v1/me/exports/saved-words${collectionQuery()}`);
  state.ankiPlan = await preflightAnki(anki, manifest, state.ankiCollection.ankiDeck);
  renderAnkiPlan();
  return state.ankiPlan;
}
async function applyAnkiSync() {
  const result = await applyAnkiPlan(anki, state.ankiPlan);
  const gameId = document.querySelector("#export-game").value || null;
  state.ankiCollection = await api("/v1/me/review-collection/anki-synced", { method: "POST", body: JSON.stringify({ gameId }) });
  showNotice(`Anki sync complete: ${result.added} added, ${result.updated} updated, ${result.unchanged} unchanged${result.racedDuplicates ? `, ${result.racedDuplicates} concurrent duplicates skipped` : ""}.`);
  await checkAnki();
}
async function importAnkiHistory() {
  if (document.querySelector("#export-game").value) throw new Error("Select All games before importing review history.");
  if (state.ankiCollection.reviewOwner !== "anki") throw new Error("Set Anki as the review owner before importing review history.");
  const gameId = document.querySelector("#export-game").value || null;
  const manifest = await api(`/v1/me/exports/saved-words${collectionQuery()}`);
  const collected = await collectAnkiReviews(anki, manifest);
  if (!collected.reviews.length) return { accepted: 0, duplicates: 0, skipped: collected.skipped, cards: collected.cards };
  const result = { accepted: 0, duplicates: 0 };
  for (let offset = 0; offset < collected.reviews.length; offset += 5000) {
    const batch = await api("/v1/me/reviews/import/anki", { method: "POST", body: JSON.stringify({ gameId, reviews: collected.reviews.slice(offset, offset + 5000) }) });
    result.accepted += batch.accepted;
    result.duplicates += batch.duplicates;
  }
  return { ...result, skipped: collected.skipped, cards: collected.cards };
}

document.querySelector("#tabs").addEventListener("click", async event => { const button = event.target.closest("[data-view]"); if (!button) return; document.querySelectorAll("#tabs button").forEach(item => item.classList.toggle("active", item===button)); document.querySelectorAll(".view").forEach(view => view.classList.add("hidden")); document.querySelector(`#view-${button.dataset.view}`).classList.remove("hidden"); if (button.dataset.view==="review") await loadReviewQueue(); });
for (const [id,path] of [["login-form","/v1/auth/login"],["register-form","/v1/auth/register"]]) document.querySelector(`#${id}`).addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget, button=form.querySelector("button[type=submit]"); setBusy(button,true,id==="login-form"?"Signing in…":"Creating account…"); try { await api(path,{method:"POST",body:JSON.stringify(formJson(form))}); showNotice(""); await loadDashboard(); } catch(error){showNotice(error.message,true);} finally { setBusy(button,false); } });
document.querySelector("#pair-form").addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget, button=form.querySelector("button[type=submit]"); setBusy(button,true,"Pairing…"); try { const result=await api("/v1/device-pairings/approve",{method:"POST",body:JSON.stringify(formJson(form))}); showNotice(`${result.deviceName} is approved. Return to the game to finish connecting.`); form.reset(); } catch(error){showNotice(error.message,true);} finally { setBusy(button,false); } });
document.querySelector("#goals-form").addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget, button=form.querySelector("button[type=submit]"); setBusy(button,true,"Saving…"); try { state.goals=await api("/v1/me/goals",{method:"PUT",body:JSON.stringify({dailyNewWords:Number(form.dailyNewWords.value),dailyReviews:Number(form.dailyReviews.value),remindersEnabled:form.remindersEnabled.checked,timezone:form.timezone.value.trim()})});renderOverview(state.goals);showNotice("Goals saved."); } catch(error){showNotice(error.message,true);} finally { setBusy(button,false); } });
let searchTimer; for (const id of ["word-search","word-state","word-sort","saved-only"]) document.querySelector(`#${id}`).addEventListener("input",()=>{clearTimeout(searchTimer);searchTimer=setTimeout(()=>loadWords(true).catch(error=>showNotice(error.message,true)),180);});
document.querySelector("#load-more-words").addEventListener("click",()=>loadWords(false).catch(error=>showNotice(error.message,true)));
document.querySelector("#words").addEventListener("click",event=>{const card=event.target.closest("[data-word-index]");if(card)openWordEditor(Number(card.dataset.wordIndex));}); document.querySelector("#close-editor").addEventListener("click",()=>document.querySelector("#word-editor").classList.add("hidden"));
document.querySelector("#word-editor").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget,button=form.querySelector("button[type=submit]");setBusy(button,true,"Saving…");try{await api("/v1/me/words/annotation",{method:"PUT",body:JSON.stringify({wordId:form.wordId.value,senseId:form.senseId.value||null,learningState:form.learningState.value,note:form.note.value,tags:form.tags.value.split(",").map(tag=>tag.trim()).filter(Boolean)})});showNotice("Word details saved.");form.classList.add("hidden");await loadWords();}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}});
document.querySelector("#review-card").addEventListener("click",()=>renderReview(true)); document.querySelector("#review-actions").addEventListener("click",async event=>{const button=event.target.closest("[data-rating]"),card=state.queue[state.reviewIndex];if(!button||!card)return;setBusy(button,true,"Saving…");try{await api("/v1/me/reviews",{method:"POST",body:JSON.stringify({wordId:card.wordId,senseId:card.senseId,rating:Number(button.dataset.rating)})});state.reviewIndex++;renderReview(false);}catch(error){showNotice(error.message,true);setBusy(button,false);}});
document.querySelector("#bury-review").addEventListener("click",async event=>{const card=state.queue[state.reviewIndex],button=event.currentTarget;if(!card)return;setBusy(button,true,"Burying…");try{await api("/v1/me/reviews/bury",{method:"POST",body:JSON.stringify({wordId:card.wordId,senseId:card.senseId})});state.queue.splice(state.reviewIndex,1);renderReview(false);showNotice("Card buried until your next review day.");}catch(error){showNotice(error.message,true);setBusy(button,false);}});
document.querySelector("#download-manifest").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Preparing…");try{const game=document.querySelector("#export-game").value;downloadJson(await api(`/v1/me/exports/saved-words${game?`?gameId=${encodeURIComponent(game)}`:""}`),"jp_assist_cloud_progress.json");}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}}); document.querySelector("#download-account").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Preparing…");try{downloadJson(await api("/v1/me/exports/account"),"jp_assist_account_export.json");}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}});
document.querySelector("#anki-import").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Checking…");try{const plan=await checkAnki();showNotice(`Preflight ready: ${plan.counts.additions} add, ${plan.counts.updates} update, ${plan.counts.unchanged} unchanged.`);}catch(error){showNotice(`AnkiConnect: ${error.message}`,true);}finally{setBusy(button,false);}});
document.querySelector("#anki-apply").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Syncing…");try{await applyAnkiSync();}catch(error){showNotice(`AnkiConnect: ${error.message}`,true);}finally{setBusy(button,false);}});
document.querySelector("#anki-history").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Importing…");try{const result=await importAnkiHistory();showNotice(`Anki history: ${result.accepted} imported, ${result.duplicates} already present${result.skipped?`, ${result.skipped} unsupported entries skipped`:""}.`);}catch(error){showNotice(`AnkiConnect: ${error.message}`,true);}finally{setBusy(button,false);}});
document.querySelector("#export-game").addEventListener("change",()=>loadAnkiCollection().catch(error=>showNotice(error.message,true)));
document.querySelector("#anki-owner").addEventListener("change",async event=>{const select=event.currentTarget,next=select.value,previous=state.ankiCollection.reviewOwner;if(next===previous)return;const confirmed=await confirmAction("Change review owner?",`${next==="anki"?"Anki":"JP Assist"} will become the only scheduler for this collection. Existing review history is kept, but due cards appear only in the owning scheduler.`,"Change owner",true);if(!confirmed){select.value=previous;return;}try{await saveAnkiCollection(next,true);showNotice(`${next==="anki"?"Anki":"JP Assist"} now owns scheduling for this collection.`);}catch(error){select.value=previous;showNotice(error.message,true);}});
document.querySelector("#devices").addEventListener("click",async event=>{const button=event.target.closest(".revoke-device");if(!button)return;if(!await confirmAction("Revoke device?","This mod will no longer be able to synchronize until it is paired again.","Revoke device",true))return;try{await api(`/v1/me/devices/${encodeURIComponent(button.dataset.id)}`,{method:"DELETE"});showNotice("Device access revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}}); document.querySelector("#sessions").addEventListener("click",async event=>{const button=event.target.closest(".revoke-session");if(!button)return;if(!await confirmAction(button.textContent==="Sign out"?"Sign out this session?":"Revoke website session?","That browser will need to sign in again.",button.textContent,true))return;try{await api(`/v1/me/sessions/${encodeURIComponent(button.dataset.id)}`,{method:"DELETE"});showNotice("Session revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}});
document.querySelector("#password-form").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget,button=form.querySelector("button[type=submit]");setBusy(button,true,"Changing…");try{await api("/v1/me/password",{method:"PUT",body:JSON.stringify(formJson(form))});form.reset();showNotice("Password changed. Other website sessions were revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}});
document.querySelector("#clear-game-form").addEventListener("submit",async event=>{event.preventDefault();const gameId=event.currentTarget.gameId.value;if(!gameId||!await confirmAction("Clear game progress?",`Permanently remove synchronized events and progress for ${gameId}. Notes and review history will remain.`,"Clear progress",true))return;try{await api("/v1/me/clear-game",{method:"POST",body:JSON.stringify({gameId})});showNotice(`${gameId} progress cleared.`);await loadDashboard();}catch(error){showNotice(error.message,true);}});
document.querySelector("#delete-account-form").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget;if(!await confirmAction("Delete this account?","This permanently removes the account and all synchronized learning data. This cannot be undone.","Delete account",true))return;try{await api("/v1/me",{method:"DELETE",body:JSON.stringify(formJson(form))});showNotice("Account deleted.");await loadDashboard();}catch(error){showNotice(error.message,true);}});
logout.addEventListener("click",async()=>{try{await api("/v1/auth/logout",{method:"POST"});}finally{await loadDashboard();}}); loadDashboard();
