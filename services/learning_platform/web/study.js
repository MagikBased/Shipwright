const parts = location.pathname.split("/").filter(Boolean);
const gameId = decodeURIComponent(parts[1] || "");
const chapterId = decodeURIComponent(parts[2] || "");
const browseMode = parts[3] === "cards";
const state = { chapter: null, progress: null, queue: [], revealed: false, reviewOwner: "jp_assist" };

function text(node, value) { node.textContent = value == null ? "" : String(value); }
function csrfToken() { return document.cookie.split("; ").find(item => item.startsWith("jp_assist_csrf="))?.split("=").slice(1).join("=") || ""; }
async function request(path, options = {}) {
  const headers = { Accept: "application/json", ...(options.headers || {}) };
  if (options.body) headers["Content-Type"] = "application/json";
  if (options.method && !["GET", "HEAD"].includes(options.method)) headers["X-CSRF-Token"] = decodeURIComponent(csrfToken());
  const response = await fetch(path, { ...options, headers });
  let body = null; try { body = await response.json(); } catch (_) {}
  if (!response.ok) { const error = new Error(body?.error?.message || body?.detail || `Request failed (${response.status})`); error.status = response.status; throw error; }
  return body;
}
function formatInterval(days) {
  const minutes = Math.round(days * 1440); if (minutes < 60) return `${Math.max(1, minutes)}m`;
  const hours = Math.round(days * 24); if (hours < 24) return `${hours}h`;
  if (days < 30) return `${Math.round(days)}d`; if (days < 365) return `${Math.round(days / 30)}mo`; return `${(days / 365).toFixed(1)}y`;
}
function renderCards() {
  const list = document.querySelector("#study-card-list"); list.replaceChildren();
  for (const card of state.chapter.cards) {
    const item = document.createElement("li"), heading = document.createElement("h3"), reading = document.createElement("p"), meaning = document.createElement("p"), sentence = document.createElement("p"), translation = document.createElement("p");
    text(heading, card.written); text(reading, `${card.reading} · ${card.partOfSpeech}`); text(meaning, card.meaning); text(sentence, card.sentenceJapanese); text(translation, card.sentenceEnglish);
    reading.className = "reading"; sentence.className = "sentence"; translation.className = "translation"; item.append(heading, reading, meaning, sentence, translation); list.append(item);
  }
  text(document.querySelector("#cards-count"), `${state.chapter.total.toLocaleString()} reviewed cards`);
}
function renderProgress() {
  const progress = state.progress, signedIn = Boolean(progress);
  document.body.classList.toggle("course-active", Boolean(progress?.active));
  document.querySelector("#sign-in").classList.toggle("hidden", signedIn);
  document.querySelector("#enroll").classList.toggle("hidden", !signedIn);
  document.querySelector("#study-review").classList.toggle("hidden", !progress?.active);
  if (!progress) { text(document.querySelector("#progress-copy"), "Sign in to start this chapter and share progress with every supported game."); return; }
  text(document.querySelector("#enroll"), progress.active ? "Pause chapter" : progress.enrolledAt ? "Resume chapter" : "Start chapter");
  text(document.querySelector("#progress-copy"), `${progress.percentMastered}% mastered. Known vocabulary is skipped automatically.`);
  document.querySelector("#progress-bar").style.width = `${progress.percentMastered}%`;
  text(document.querySelector("#metric-mastered"), `${progress.masteredCards}/${progress.totalCards}`); text(document.querySelector("#metric-reviewed"), progress.reviewedCards); text(document.querySelector("#metric-new"), progress.newCards); text(document.querySelector("#metric-due"), progress.dueCards);
}
function renderReview() {
  const card = state.queue[0], box = document.querySelector("#course-review-card"), actions = document.querySelector("#course-review-actions");
  box.replaceChildren();
  if (!card) { const message = document.createElement("span"); message.className = "muted"; text(message, state.reviewOwner === "anki" ? "Anki owns scheduling. Review this chapter in Anki or change the review owner in your account." : "You are caught up for now."); box.append(message); box.disabled = true; actions.classList.add("hidden"); return; }
  box.disabled = false; const wrap = document.createElement("div"), written = document.createElement("div"); written.className = "written"; text(written, card.written); wrap.append(written);
  if (state.revealed) {
    const reading = document.createElement("div"), meaning = document.createElement("div"), example = document.createElement("p"), translation = document.createElement("p"); reading.className = "reading"; meaning.className = "meaning"; example.className = "example"; translation.className = "translation";
    text(reading, `${card.reading} · ${card.partOfSpeech}`); text(meaning, card.meaning || "No meaning available"); text(example, card.courseCard?.sentenceJapanese || ""); text(translation, card.courseCard?.sentenceEnglish || ""); wrap.append(reading, meaning, example, translation);
  } else { const hint = document.createElement("p"); hint.className = "muted"; text(hint, "Select the card to reveal its meaning and example."); wrap.append(hint); }
  box.append(wrap); actions.classList.toggle("hidden", !state.revealed);
  actions.querySelectorAll("[data-rating]").forEach((button, index) => { const preview = card.ratingPreviews?.find(item => item.rating === index + 1); const labels = ["Again", "Hard", "Good", "Easy"]; button.disabled = false; button.replaceChildren(document.createTextNode(labels[index])); if (preview) { const small = document.createElement("small"); text(small, formatInterval(preview.intervalDays)); button.append(small); } });
  document.querySelector("#course-bury").disabled = false;
}
async function loadProgress() {
  try { state.progress = await request(`/v1/me/courses/${encodeURIComponent(gameId)}/chapters/${encodeURIComponent(chapterId)}`); state.reviewOwner = (await request("/v1/me/review-collection")).reviewOwner; }
  catch (error) { if (error.status !== 401) throw error; state.progress = null; }
  renderProgress();
}
async function loadQueue() {
  if (!state.progress?.active) return;
  state.queue = await request(`/v1/me/reviews/queue?gameId=${encodeURIComponent(gameId)}&chapterId=${encodeURIComponent(chapterId)}&limit=100`); state.revealed = false; renderReview();
}
async function load() {
  try {
    state.chapter = await request(`/v1/catalog/games/${encodeURIComponent(gameId)}/chapters/${encodeURIComponent(chapterId)}/cards?limit=250`);
    document.body.classList.toggle("review-mode", !browseMode);
    const studyPath = `/study/${encodeURIComponent(gameId)}/${encodeURIComponent(chapterId)}`;
    document.querySelectorAll(browseMode ? ".browse-only" : ".review-only").forEach(element => element.classList.remove("hidden"));
    const modeLink = document.querySelector("#mode-link"); modeLink.href = browseMode ? studyPath : `${studyPath}/cards`; text(modeLink, browseMode ? "Study this chapter" : "Browse chapter cards");
    document.title = `${browseMode ? "Cards — " : ""}${state.chapter.chapterTitle} — JP Assist Learning`; text(document.querySelector("#study-title"), `${String(state.chapter.chapterOrder).padStart(2, "0")} ${state.chapter.chapterTitle}`); text(document.querySelector("#study-subtitle"), `${state.chapter.gameTitle} · ${state.chapter.total} reviewed cards`);
    if (browseMode) renderCards(); else { await loadProgress(); await loadQueue(); }
    document.querySelector("#study-status").classList.add("hidden"); document.querySelector("#study-content").classList.remove("hidden");
  } catch (error) { text(document.querySelector("#study-status"), `The chapter could not be loaded. ${error.message}`); }
}
document.querySelector("#enroll").addEventListener("click", async event => { const button = event.currentTarget; button.disabled = true; try { state.progress = await request(`/v1/me/courses/${encodeURIComponent(gameId)}/chapters/${encodeURIComponent(chapterId)}`, { method: "PUT", body: JSON.stringify({ active: !state.progress.active }) }); renderProgress(); state.queue = []; if (state.progress.active) await loadQueue(); } catch (error) { alert(error.message); } finally { button.disabled = false; } });
document.querySelector("#course-review-card").addEventListener("click", () => { if (!state.queue[0]) return; state.revealed = true; renderReview(); });
document.querySelector("#course-review-actions").addEventListener("click", async event => { const button = event.target.closest("[data-rating]"), card = state.queue[0]; if (!button || !card) return; button.disabled = true; try { await request("/v1/me/reviews", { method: "POST", body: JSON.stringify({ wordId: card.wordId, senseId: card.senseId, rating: Number(button.dataset.rating) }) }); state.queue.shift(); state.progress = await request(`/v1/me/courses/${encodeURIComponent(gameId)}/chapters/${encodeURIComponent(chapterId)}`); state.revealed = false; renderProgress(); renderReview(); } catch (error) { alert(error.message); button.disabled = false; } });
document.querySelector("#course-bury").addEventListener("click", async event => { const card = state.queue[0]; if (!card) return; event.currentTarget.disabled = true; try { await request("/v1/me/reviews/bury", { method: "POST", body: JSON.stringify({ wordId: card.wordId, senseId: card.senseId }) }); state.queue.shift(); state.revealed = false; renderReview(); } catch (error) { alert(error.message); event.currentTarget.disabled = false; } });
load();
