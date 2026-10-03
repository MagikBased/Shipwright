const auth = document.querySelector("#auth");
const dashboard = document.querySelector("#dashboard");
const notice = document.querySelector("#notice");
const logout = document.querySelector("#logout");
const state = { user: null, stats: null, words: [], activity: { days: [], games: [] }, devices: [], sessions: [], queue: [], reviewIndex: 0 };

async function api(path, options = {}) {
  const response = await fetch(path, { ...options, headers: { "Content-Type": "application/json", ...(options.headers || {}) } });
  const body = response.status === 204 ? null : await response.json();
  if (!response.ok) throw new Error(body?.error?.message || `Request failed (${response.status})`);
  return body;
}
function formJson(form) { return Object.fromEntries(new FormData(form).entries()); }
function escapeHtml(value) { const node = document.createElement("span"); node.textContent = value ?? ""; return node.innerHTML; }
function showNotice(message, error = false) { notice.textContent = message; notice.style.color = error ? "#ff8e8e" : ""; }
function downloadJson(value, filename) { const link = document.createElement("a"); link.href = URL.createObjectURL(new Blob([JSON.stringify(value, null, 2)], { type: "application/json" })); link.download = filename; link.click(); URL.revokeObjectURL(link.href); }

async function loadDashboard() {
  try {
    const [user, stats, activity, goals, devices, sessions] = await Promise.all([
      api("/v1/me"), api("/v1/me/stats"), api("/v1/me/activity"), api("/v1/me/goals"), api("/v1/me/devices"), api("/v1/me/sessions"),
    ]);
    Object.assign(state, { user, stats, activity, devices, sessions });
    auth.classList.add("hidden"); dashboard.classList.remove("hidden"); logout.classList.remove("hidden");
    document.querySelector("#greeting").textContent = `${user.displayName}'s learning overview`;
    renderOverview(goals); renderConnections(); populateGames(); await loadWords();
  } catch (_) { auth.classList.remove("hidden"); dashboard.classList.add("hidden"); logout.classList.add("hidden"); }
}

function renderOverview(goals) {
  const metrics = [[state.stats.uniqueWords,"unique words"],[state.stats.encounters,"encounters"],[state.stats.savedWords,"saved"],[state.stats.games,"games"],[state.stats.connectedDevices,"devices"]];
  document.querySelector("#stats").innerHTML = metrics.map(([value,label]) => `<div class="stat"><strong>${value}</strong><span>${label}</span></div>`).join("");
  const max = Math.max(1, ...state.activity.days.map(day => day.events + day.reviews));
  document.querySelector("#activity-chart").innerHTML = state.activity.days.length ? state.activity.days.map(day => `<div class="activity-day" style="height:${Math.max(3,(day.events+day.reviews)/max*100)}%" title="${escapeHtml(day.date)}: ${day.events} game events, ${day.reviews} reviews"></div>`).join("") : '<p class="muted">Activity will appear after your mod syncs.</p>';
  document.querySelector("#games").innerHTML = state.activity.games.length ? state.activity.games.map(game => `<div class="card-row"><div><strong>${escapeHtml(game.gameId)}</strong><small>${game.uniqueWords} words · ${game.encounters} encounters</small></div><span>${escapeHtml(game.lastSeenAt.slice(0,10))}</span></div>`).join("") : '<p class="muted">No game activity yet.</p>';
  const form = document.querySelector("#goals-form"); form.dailyNewWords.value = goals.dailyNewWords; form.dailyReviews.value = goals.dailyReviews; form.remindersEnabled.checked = goals.remindersEnabled;
}

function populateGames() {
  const options = state.activity.games.map(game => `<option value="${escapeHtml(game.gameId)}">${escapeHtml(game.gameId)}</option>`).join("");
  document.querySelector("#export-game").innerHTML = `<option value="">All games</option>${options}`;
  document.querySelector("#clear-game").innerHTML = options || '<option value="">No games</option>';
}

async function loadWords() {
  const query = new URLSearchParams({ search: document.querySelector("#word-search").value, sort: document.querySelector("#word-sort").value, limit: "500" });
  const learningState = document.querySelector("#word-state").value; if (learningState) query.set("learningState", learningState);
  if (document.querySelector("#saved-only").checked) query.set("savedOnly", "true");
  state.words = await api(`/v1/me/words?${query}`); renderWords();
}
function renderWords() {
  document.querySelector("#words").innerHTML = state.words.length ? state.words.map((word,index) => {
    const dictionary = word.dictionary || {}; const title = dictionary.written || word.wordId; const detail = [dictionary.reading, dictionary.meaning].filter(Boolean).join(" · ");
    return `<button class="word-card" data-word-index="${index}" type="button"><span><strong>${escapeHtml(title)}</strong><small>${escapeHtml(detail || word.wordId)}</small><small>${word.tags.map(tag => `#${escapeHtml(tag)}`).join(" ")}</small></span><span><span class="pill">${escapeHtml(word.learningState)}</span>${word.saved?'<span class="pill saved">saved</span>':""}<small>${word.encounterCount}×</small></span></button>`;
  }).join("") : '<p class="muted">No words match these filters.</p>';
}
function openWordEditor(index) {
  const word = state.words[index], form = document.querySelector("#word-editor");
  form.wordId.value = word.wordId; form.senseId.value = word.senseId || ""; form.learningState.value = word.learningState; form.tags.value = word.tags.join(", "); form.note.value = word.note;
  document.querySelector("#editor-word").textContent = word.dictionary?.written || word.wordId; form.classList.remove("hidden"); form.scrollIntoView({ behavior: "smooth", block: "nearest" });
}

function renderConnections() {
  document.querySelector("#devices").innerHTML = state.devices.length ? state.devices.map(device => `<div class="card-row"><div><strong>${escapeHtml(device.deviceName)}</strong><small>${escapeHtml(device.gameId)} · last seen ${escapeHtml(device.lastSeenAt.slice(0,10))}</small></div><button class="quiet revoke-device" data-id="${escapeHtml(device.id)}">Revoke</button></div>`).join("") : '<p class="muted">No connected mods yet.</p>';
  document.querySelector("#sessions").innerHTML = state.sessions.map(session => `<div class="card-row"><div><strong>${session.current?"This session":"Website session"}</strong><small>Created ${escapeHtml(session.createdAt.slice(0,10))} · expires ${escapeHtml(session.expiresAt.slice(0,10))}</small></div><button class="quiet revoke-session" data-id="${escapeHtml(session.id)}">${session.current?"Sign out":"Revoke"}</button></div>`).join("");
}

async function loadReviewQueue() { state.queue = await api("/v1/me/reviews/queue?limit=100"); state.reviewIndex = 0; renderReview(false); }
function renderReview(revealed) {
  const card = state.queue[state.reviewIndex], box = document.querySelector("#review-card"), actions = document.querySelector("#review-actions");
  if (!card) { box.disabled = true; box.innerHTML = '<span class="muted">You are caught up. Save more words in-game or mark words as learning.</span>'; actions.classList.add("hidden"); return; }
  box.disabled = false;
  box.innerHTML = `<div><div class="reading">${escapeHtml(card.reading)}</div><div class="written">${escapeHtml(card.written)}</div>${revealed?`<hr><div class="meaning">${escapeHtml(card.meaning || "No dictionary meaning imported")}</div><p>${escapeHtml(card.partOfSpeech)}</p>`:'<p class="muted">Click the card to reveal</p>'}</div>`;
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
for (const [id,path] of [["login-form","/v1/auth/login"],["register-form","/v1/auth/register"]]) document.querySelector(`#${id}`).addEventListener("submit", async event => { event.preventDefault(); try { await api(path,{method:"POST",body:JSON.stringify(formJson(event.currentTarget))}); showNotice(""); await loadDashboard(); } catch(error){showNotice(error.message,true);} });
document.querySelector("#pair-form").addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget; try { const result=await api("/v1/device-pairings/approve",{method:"POST",body:JSON.stringify(formJson(form))}); showNotice(`${result.deviceName} is approved. Return to the game to finish connecting.`); form.reset(); } catch(error){showNotice(error.message,true);} });
document.querySelector("#goals-form").addEventListener("submit", async event => { event.preventDefault(); const form=event.currentTarget; try { await api("/v1/me/goals",{method:"PUT",body:JSON.stringify({dailyNewWords:Number(form.dailyNewWords.value),dailyReviews:Number(form.dailyReviews.value),remindersEnabled:form.remindersEnabled.checked})}); showNotice("Goals saved."); } catch(error){showNotice(error.message,true);} });
let searchTimer; for (const id of ["word-search","word-state","word-sort","saved-only"]) document.querySelector(`#${id}`).addEventListener("input",()=>{clearTimeout(searchTimer);searchTimer=setTimeout(()=>loadWords().catch(error=>showNotice(error.message,true)),180);});
document.querySelector("#words").addEventListener("click",event=>{const card=event.target.closest("[data-word-index]");if(card)openWordEditor(Number(card.dataset.wordIndex));}); document.querySelector("#close-editor").addEventListener("click",()=>document.querySelector("#word-editor").classList.add("hidden"));
document.querySelector("#word-editor").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget;try{await api("/v1/me/words/annotation",{method:"PUT",body:JSON.stringify({wordId:form.wordId.value,senseId:form.senseId.value||null,learningState:form.learningState.value,note:form.note.value,tags:form.tags.value.split(",").map(tag=>tag.trim()).filter(Boolean)})});showNotice("Word details saved.");form.classList.add("hidden");await loadWords();}catch(error){showNotice(error.message,true);}});
document.querySelector("#review-card").addEventListener("click",()=>renderReview(true)); document.querySelector("#review-actions").addEventListener("click",async event=>{const button=event.target.closest("[data-rating]"),card=state.queue[state.reviewIndex];if(!button||!card)return;try{await api("/v1/me/reviews",{method:"POST",body:JSON.stringify({wordId:card.wordId,senseId:card.senseId,rating:Number(button.dataset.rating)})});state.reviewIndex++;renderReview(false);}catch(error){showNotice(error.message,true);}});
document.querySelector("#download-manifest").addEventListener("click",async()=>{try{const game=document.querySelector("#export-game").value;downloadJson(await api(`/v1/me/exports/saved-words${game?`?gameId=${encodeURIComponent(game)}`:""}`),"jp_assist_cloud_progress.json");}catch(error){showNotice(error.message,true);}}); document.querySelector("#download-account").addEventListener("click",async()=>{try{downloadJson(await api("/v1/me/exports/account"),"jp_assist_account_export.json");}catch(error){showNotice(error.message,true);}}); document.querySelector("#anki-import").addEventListener("click",()=>sendToAnki().catch(error=>showNotice(`AnkiConnect: ${error.message}`,true)));
document.querySelector("#devices").addEventListener("click",async event=>{const button=event.target.closest(".revoke-device");if(!button)return;try{await api(`/v1/me/devices/${encodeURIComponent(button.dataset.id)}`,{method:"DELETE"});showNotice("Device access revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}}); document.querySelector("#sessions").addEventListener("click",async event=>{const button=event.target.closest(".revoke-session");if(!button)return;try{await api(`/v1/me/sessions/${encodeURIComponent(button.dataset.id)}`,{method:"DELETE"});showNotice("Session revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}});
document.querySelector("#password-form").addEventListener("submit",async event=>{event.preventDefault();const form=event.currentTarget;try{await api("/v1/me/password",{method:"PUT",body:JSON.stringify(formJson(form))});form.reset();showNotice("Password changed. Other website sessions were revoked.");await loadDashboard();}catch(error){showNotice(error.message,true);}});
document.querySelector("#clear-game-form").addEventListener("submit",async event=>{event.preventDefault();const gameId=event.currentTarget.gameId.value;if(!gameId||!confirm(`Permanently clear synchronized progress for ${gameId}?`))return;try{await api("/v1/me/clear-game",{method:"POST",body:JSON.stringify({gameId})});showNotice(`${gameId} progress cleared.`);await loadDashboard();}catch(error){showNotice(error.message,true);}});
document.querySelector("#delete-account-form").addEventListener("submit",async event=>{event.preventDefault();if(!confirm("Permanently delete this account and all synchronized data?"))return;try{await api("/v1/me",{method:"DELETE",body:JSON.stringify(formJson(event.currentTarget))});showNotice("Account deleted.");await loadDashboard();}catch(error){showNotice(error.message,true);}});
logout.addEventListener("click",async()=>{try{await api("/v1/auth/logout",{method:"POST"});}finally{await loadDashboard();}}); loadDashboard();
