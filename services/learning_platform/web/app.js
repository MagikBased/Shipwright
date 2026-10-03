import { confirmAction, downloadJson, escapeHtml, setBusy, showNotice, skeletonCards } from "./ui.js";

const auth = document.querySelector("#auth");
const dashboard = document.querySelector("#dashboard");
const logout = document.querySelector("#logout");
const WORD_PAGE_SIZE = 50;
const state = { user: null, stats: null, goals: null, words: [], wordsOffset: 0, wordsHaveMore: false, wordRequestVersion: 0, activity: { days: [], games: [] }, devices: [], sessions: [], queue: [], reviewIndex: 0 };
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
    const [stats, activity, goals, devices, sessions] = await Promise.all([
      api("/v1/me/stats"), api("/v1/me/activity"), api("/v1/me/goals"), api("/v1/me/devices"), api("/v1/me/sessions"),
    ]);
    Object.assign(state, { user, stats, activity, goals, devices, sessions });
    document.querySelectorAll(".account-username").forEach(input => { input.value = user.email; });
    document.querySelector("#greeting").textContent = `${user.displayName}'s learning overview`;
    renderOverview(goals); renderConnections(); populateGames(); await loadWords();
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
  const form = document.querySelector("#goals-form"); form.dailyNewWords.value = goals.dailyNewWords; form.dailyReviews.value = goals.dailyReviews; form.remindersEnabled.checked = goals.remindersEnabled;
  document.querySelector("#goal-progress").innerHTML = [
    ["New words", state.stats.newWordsToday, goals.dailyNewWords], ["Reviews", state.stats.reviewedToday, goals.dailyReviews],
  ].map(([label,value,target]) => `<div><span>${label}<strong>${value} / ${target}</strong></span><progress value="${Math.min(value,target)}" max="${Math.max(target,1)}">${value} of ${target}</progress></div>`).join("");
}

function populateGames() {
  const options = state.activity.games.map(game => `<option value="${escapeHtml(game.gameId)}">${escapeHtml(game.gameId)}</option>`).join("");
  document.querySelector("#export-game").innerHTML = `<option value="">All games</option>${options}`;
  document.querySelector("#clear-game").innerHTML = options || '<option value="">No games</option>';
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
  if (!card) { box.disabled = true; box.innerHTML = '<span class="muted">You are caught up. Save more words in-game or mark words as learning.</span>'; actions.classList.add("hidden"); return; }
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
async function sendToAnki() {
  const game = document.querySelector("#export-game").value, query = game ? `?gameId=${encodeURIComponent(game)}` : "";
  const manifest = await api(`/v1/me/exports/saved-words${query}`), words = manifest.words.filter(word => word.dictionary?.meaning);
  if (!words.length) throw new Error("No saved words with dictionary meanings are available.");
  const deckName = document.querySelector("#anki-deck").value.trim(); if (!deckName) throw new Error("Enter a deck name.");
  await anki("createDeck", { deck: deckName }); const modelName = "JP Assist Vocabulary"; const models = await anki("modelNames");
  if (!models.includes(modelName)) await anki("createModel", { modelName, inOrderFields: ["Word ID","Word","Reading","Meaning","Part of Speech","Game"], css: ".card{font-family:sans-serif;font-size:24px;text-align:center;color:#eee;background:#222}.word{font-size:44px}", cardTemplates: [{ Name:"Recognition", Front:'<div class="word">{{Word}}</div>', Back:'{{FrontSide}}<hr>{{Reading}}<br>{{Meaning}}<br><small>{{Part of Speech}} · {{Game}}</small>' }] });
  const notes = words.map(word => ({ deckName, modelName, fields: { "Word ID": `${word.wordId}|${word.senseId || ""}`, Word: word.dictionary.written, Reading: word.dictionary.reading, Meaning: word.dictionary.meaning, "Part of Speech": word.dictionary.partOfSpeech, Game: word.gameIds.join(", ") }, options: { allowDuplicate: false }, tags: ["jp-assist", ...word.gameIds.map(id => `game::${id.replace(/[^A-Za-z0-9_-]/g,"_")}`)] }));
  const results = await anki("addNotes", { notes }); const added = results.filter(Boolean).length; showNotice(`Sent ${added} new card${added===1?"":"s"} to Anki; ${results.length-added} duplicate${results.length-added===1?"":"s"} skipped.`);
}

document.querySelector("#tabs").addEventListener("click", async event => { const button = event.target.closest("[data-view]"); if (!button) return; document.querySelectorAll("#tabs button").forEach(item => item.classList.toggle("active", item===button)); document.querySelectorAll(".view").forEach(view => view.classList.add("hidden")); document.querySelector(`#view-${button.dataset.view}`).classList.remove("hidden"); if (button.dataset.view==="review") await loadReviewQueue(); });
for (const [id,path] of [["login-form","/v1/auth/login"],["register-form","/v1/auth/register"]]) document.querySelector(`#${id}`).addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget, button=form.querySelector("button[type=submit]"); setBusy(button,true,id==="login-form"?"Signing in…":"Creating account…"); try { await api(path,{method:"POST",body:JSON.stringify(formJson(form))}); showNotice(""); await loadDashboard(); } catch(error){showNotice(error.message,true);} finally { setBusy(button,false); } });
document.querySelector("#pair-form").addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget, button=form.querySelector("button[type=submit]"); setBusy(button,true,"Pairing…"); try { const result=await api("/v1/device-pairings/approve",{method:"POST",body:JSON.stringify(formJson(form))}); showNotice(`${result.deviceName} is approved. Return to the game to finish connecting.`); form.reset(); } catch(error){showNotice(error.message,true);} finally { setBusy(button,false); } });
document.querySelector("#goals-form").addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget, button=form.querySelector("button[type=submit]"); setBusy(button,true,"Saving…"); try { state.goals=await api("/v1/me/goals",{method:"PUT",body:JSON.stringify({dailyNewWords:Number(form.dailyNewWords.value),dailyReviews:Number(form.dailyReviews.value),remindersEnabled:form.remindersEnabled.checked})});renderOverview(state.goals);showNotice("Goals saved."); } catch(error){showNotice(error.message,true);} finally { setBusy(button,false); } });
let searchTimer; for (const id of ["word-search","word-state","word-sort","saved-only"]) document.querySelector(`#${id}`).addEventListener("input",()=>{clearTimeout(searchTimer);searchTimer=setTimeout(()=>loadWords(true).catch(error=>showNotice(error.message,true)),180);});
document.querySelector("#load-more-words").addEventListener("click",()=>loadWords(false).catch(error=>showNotice(error.message,true)));
document.querySelector("#words").addEventListener("click",event=>{const card=event.target.closest("[data-word-index]");if(card)openWordEditor(Number(card.dataset.wordIndex));}); document.querySelector("#close-editor").addEventListener("click",()=>document.querySelector("#word-editor").classList.add("hidden"));
document.querySelector("#word-editor").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget,button=form.querySelector("button[type=submit]");setBusy(button,true,"Saving…");try{await api("/v1/me/words/annotation",{method:"PUT",body:JSON.stringify({wordId:form.wordId.value,senseId:form.senseId.value||null,learningState:form.learningState.value,note:form.note.value,tags:form.tags.value.split(",").map(tag=>tag.trim()).filter(Boolean)})});showNotice("Word details saved.");form.classList.add("hidden");await loadWords();}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}});
document.querySelector("#review-card").addEventListener("click",()=>renderReview(true)); document.querySelector("#review-actions").addEventListener("click",async event=>{const button=event.target.closest("[data-rating]"),card=state.queue[state.reviewIndex];if(!button||!card)return;setBusy(button,true,"Saving…");try{await api("/v1/me/reviews",{method:"POST",body:JSON.stringify({wordId:card.wordId,senseId:card.senseId,rating:Number(button.dataset.rating)})});state.reviewIndex++;renderReview(false);}catch(error){showNotice(error.message,true);setBusy(button,false);}});
document.querySelector("#download-manifest").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Preparing…");try{const game=document.querySelector("#export-game").value;downloadJson(await api(`/v1/me/exports/saved-words${game?`?gameId=${encodeURIComponent(game)}`:""}`),"jp_assist_cloud_progress.json");}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}}); document.querySelector("#download-account").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Preparing…");try{downloadJson(await api("/v1/me/exports/account"),"jp_assist_account_export.json");}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}}); document.querySelector("#anki-import").addEventListener("click",async event=>{const button=event.currentTarget;setBusy(button,true,"Sending…");try{await sendToAnki();}catch(error){showNotice(`AnkiConnect: ${error.message}`,true);}finally{setBusy(button,false);}});
document.querySelector("#devices").addEventListener("click",async event=>{const button=event.target.closest(".revoke-device");if(!button)return;if(!await confirmAction("Revoke device?","This mod will no longer be able to synchronize until it is paired again.","Revoke device",true))return;try{await api(`/v1/me/devices/${encodeURIComponent(button.dataset.id)}`,{method:"DELETE"});showNotice("Device access revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}}); document.querySelector("#sessions").addEventListener("click",async event=>{const button=event.target.closest(".revoke-session");if(!button)return;if(!await confirmAction(button.textContent==="Sign out"?"Sign out this session?":"Revoke website session?","That browser will need to sign in again.",button.textContent,true))return;try{await api(`/v1/me/sessions/${encodeURIComponent(button.dataset.id)}`,{method:"DELETE"});showNotice("Session revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}});
document.querySelector("#password-form").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget,button=form.querySelector("button[type=submit]");setBusy(button,true,"Changing…");try{await api("/v1/me/password",{method:"PUT",body:JSON.stringify(formJson(form))});form.reset();showNotice("Password changed. Other website sessions were revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}finally{setBusy(button,false);}});
document.querySelector("#clear-game-form").addEventListener("submit",async event=>{event.preventDefault();const gameId=event.currentTarget.gameId.value;if(!gameId||!await confirmAction("Clear game progress?",`Permanently remove synchronized events and progress for ${gameId}. Notes and review history will remain.`,"Clear progress",true))return;try{await api("/v1/me/clear-game",{method:"POST",body:JSON.stringify({gameId})});showNotice(`${gameId} progress cleared.`);await loadDashboard();}catch(error){showNotice(error.message,true);}});
document.querySelector("#delete-account-form").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget;if(!await confirmAction("Delete this account?","This permanently removes the account and all synchronized learning data. This cannot be undone.","Delete account",true))return;try{await api("/v1/me",{method:"DELETE",body:JSON.stringify(formJson(form))});showNotice("Account deleted.");await loadDashboard();}catch(error){showNotice(error.message,true);}});
logout.addEventListener("click",async()=>{try{await api("/v1/auth/logout",{method:"POST"});}finally{await loadDashboard();}}); loadDashboard();
