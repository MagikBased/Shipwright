const statusBox = document.querySelector("#catalog-status");
const catalog = document.querySelector("#game-catalog");
const GAME_ID = "ocarina-of-time";
let signedIn = false;
let knownWordIds = new Set();
let vocabularyOffset = 0;
let vocabularyTotal = 0;
const vocabularyLimit = 40;

function text(element, value) {
  element.textContent = value ?? "";
}

function statusLabel(value) {
  return value === "pilot" ? "Pilot content" : value === "ready" ? "Ready" : "Planned";
}

function fact(term, description) {
  const wrapper = document.createElement("div");
  const dt = document.createElement("dt");
  const dd = document.createElement("dd");
  text(dt, term); text(dd, description);
  wrapper.append(dt, dd);
  return wrapper;
}

function csrfToken() {
  const match = document.cookie.match(/(?:^|; )jp_assist_csrf=([^;]+)/);
  return match ? decodeURIComponent(match[1]) : "";
}

function renderLanguageProfile(profile) {
  const breakdown = document.querySelector("#jlpt-breakdown");
  breakdown.replaceChildren();
  const levels = ["N5", "N4", "N3", "N2", "N1", "unclassified"];
  for (const level of levels) {
    const count = profile.uniqueByLevel[level] || 0;
    const percent = profile.uniqueWords ? count * 100 / profile.uniqueWords : 0;
    const row = document.createElement("div");
    row.className = `level-row ${level}`;
    const label = document.createElement("strong");
    text(label, level === "unclassified" ? "Other" : level);
    const track = document.createElement("div");
    track.className = "level-track";
    track.setAttribute("role", "img");
    track.setAttribute("aria-label", `${level}: ${count} words, ${percent.toFixed(1)} percent`);
    const fill = document.createElement("div");
    fill.className = "level-fill";
    fill.style.width = `${percent}%`;
    track.append(fill);
    const value = document.createElement("span");
    value.className = "level-value";
    text(value, `${count.toLocaleString()} · ${percent.toFixed(1)}%`);
    row.append(label, track, value);
    breakdown.append(row);
  }
  text(document.querySelector("#language-method"),
    `${profile.classifiedWords.toLocaleString()} of ${profile.uniqueWords.toLocaleString()} unique words map to OpenJLPT. Levels are community estimates, not official JLPT lists.`);
}

function renderCoverage(coverage) {
  knownWordIds = new Set(coverage.knownWordIds);
  text(document.querySelector("#coverage-percent"), `${coverage.percentKnown.toFixed(1)}%`);
  text(document.querySelector("#coverage-count"), `${coverage.knownWords.toLocaleString()} of ${coverage.totalWords.toLocaleString()} game words`);
  const levels = document.querySelector("#coverage-levels");
  levels.replaceChildren();
  for (const level of ["N5", "N4", "N3", "N2", "N1", "unclassified"]) {
    const span = document.createElement("span");
    const item = coverage.levels[level];
    text(span, `${level === "unclassified" ? "Other" : level}: ${item.known}/${item.total}`);
    levels.append(span);
  }
  document.querySelector("#coverage-signed-out").classList.add("hidden");
  document.querySelector("#coverage-signed-in").classList.remove("hidden");
  for (const button of document.querySelectorAll(".known-toggle")) updateKnownButton(button);
}

function updateKnownButton(button) {
  const known = knownWordIds.has(button.dataset.wordId);
  button.setAttribute("aria-pressed", String(known));
  text(button, known ? "Known ✓" : signedIn ? "Mark known" : "Sign in");
  button.disabled = !signedIn;
}

function vocabularyItem(word) {
  const item = document.createElement("li");
  const copy = document.createElement("div");
  const wordLine = document.createElement("div");
  const written = document.createElement("span");
  const reading = document.createElement("span");
  const meaning = document.createElement("div");
  written.className = "vocabulary-word";
  reading.className = "vocabulary-reading";
  meaning.className = "vocabulary-meaning";
  text(written, word.written);
  text(reading, ` ${word.reading} · ${word.jlptLevel || "Unclassified"} · ${word.occurrenceCount}×`);
  text(meaning, word.meaning);
  wordLine.append(written, reading);
  copy.append(wordLine, meaning);
  const button = document.createElement("button");
  button.type = "button";
  button.className = "known-toggle quiet";
  button.dataset.wordId = word.wordId;
  updateKnownButton(button);
  button.addEventListener("click", async () => {
    button.disabled = true;
    const known = !knownWordIds.has(word.wordId);
    try {
      const response = await fetch(`/v1/me/catalog/games/${GAME_ID}/known-word`, {
        method: "PUT",
        headers: { "Content-Type": "application/json", "X-CSRF-Token": csrfToken() },
        body: JSON.stringify({ wordId: word.wordId, known }),
      });
      if (!response.ok) throw new Error(`Update failed (${response.status})`);
      const result = await response.json();
      renderCoverage(result.coverage);
    } catch (error) {
      text(document.querySelector("#vocabulary-status"), error.message);
      updateKnownButton(button);
    }
  });
  item.append(copy, button);
  return item;
}

async function loadVocabulary(reset = false) {
  if (reset) vocabularyOffset = 0;
  const search = document.querySelector("#vocabulary-search").value.trim();
  const level = document.querySelector("#vocabulary-level").value;
  const query = new URLSearchParams({ limit: String(vocabularyLimit), offset: String(vocabularyOffset) });
  if (search) query.set("search", search);
  if (level) query.set("jlptLevel", level);
  const response = await fetch(`/v1/catalog/games/${GAME_ID}/vocabulary?${query}`);
  if (!response.ok) throw new Error(`Vocabulary request failed (${response.status})`);
  const result = await response.json();
  vocabularyTotal = result.total;
  const list = document.querySelector("#vocabulary-list");
  if (reset) list.replaceChildren();
  for (const word of result.words) list.append(vocabularyItem(word));
  vocabularyOffset += result.words.length;
  text(document.querySelector("#vocabulary-status"), `Showing ${vocabularyOffset.toLocaleString()} of ${vocabularyTotal.toLocaleString()} matching words.`);
  document.querySelector("#vocabulary-more").classList.toggle("hidden", vocabularyOffset >= vocabularyTotal);
}

async function loadPersonalCoverage() {
  const response = await fetch(`/v1/me/catalog/games/${GAME_ID}/coverage`, { headers: { Accept: "application/json" } });
  if (response.status === 401) return;
  if (!response.ok) throw new Error(`Coverage request failed (${response.status})`);
  signedIn = true;
  renderCoverage(await response.json());
}

function chapterCard(chapter, chapterNames) {
  const item = document.createElement("li");
  item.className = "chapter-card panel";
  const heading = document.createElement("div");
  heading.className = "chapter-heading";
  const number = document.createElement("span");
  number.className = "chapter-number";
  text(number, String(chapter.order).padStart(2, "0"));
  const titleBox = document.createElement("div");
  const title = document.createElement("h3");
  const subtitle = document.createElement("p");
  text(title, chapter.title); text(subtitle, chapter.subtitle);
  titleBox.append(title, subtitle);
  const badge = document.createElement("span");
  badge.className = `catalog-badge ${chapter.deck.status}`;
  text(badge, statusLabel(chapter.deck.status));
  heading.append(number, titleBox, badge);

  const scope = document.createElement("p");
  scope.className = "chapter-scope";
  text(scope, chapter.scope);
  const milestones = document.createElement("div");
  milestones.className = "chapter-milestones";
  const start = document.createElement("span");
  const end = document.createElement("span");
  text(start, `Starts: ${chapter.startMarker}`); text(end, `Ends: ${chapter.endMarker}`);
  milestones.append(start, end);
  item.append(heading, scope, milestones);

  const dependencies = [...(chapter.prerequisites || []), ...(chapter.recommendedAfter || [])];
  if (dependencies.length) {
    const note = document.createElement("p");
    note.className = "chapter-dependencies";
    const label = chapter.prerequisites?.length ? "Requires" : "Recommended after";
    text(note, `${label}: ${dependencies.map(id => chapterNames.get(id)).join(", ")}`);
    item.append(note);
  }

  if (chapter.sampleCards?.length) {
    const details = document.createElement("details");
    details.className = "pilot-cards";
    const summary = document.createElement("summary");
    text(summary, `Preview ${chapter.sampleCards.length} reviewed pilot cards`);
    const grid = document.createElement("div");
    grid.className = "pilot-grid";
    for (const card of chapter.sampleCards) {
      const article = document.createElement("article");
      const word = document.createElement("h4");
      const reading = document.createElement("span");
      const meaning = document.createElement("p");
      const sentence = document.createElement("p");
      const translation = document.createElement("p");
      text(word, card.written); text(reading, card.reading); text(meaning, card.meaning);
      text(sentence, card.sentenceJapanese); text(translation, card.sentenceEnglish);
      reading.className = "reading"; meaning.className = "card-meaning";
      translation.className = "muted small";
      article.append(word, reading, meaning, sentence, translation);
      grid.append(article);
    }
    details.append(summary, grid);
    item.append(details);
  }
  return item;
}

async function loadCatalog() {
  try {
    const response = await fetch(`/v1/catalog/games/${GAME_ID}`, { headers: { Accept: "application/json" } });
    if (!response.ok) throw new Error(`Catalog request failed (${response.status})`);
    const game = await response.json();
    text(document.querySelector("#game-series"), `${game.series} · ${game.platform}`);
    text(document.querySelector("#game-title"), game.title);
    text(document.querySelector("#game-summary"), game.summary);
    text(document.querySelector("#chapter-model"), `${game.chapterModel.description} ${game.chapterModel.assignmentRule}`);
    text(document.querySelector("#audio-policy"), game.audio.description);
    const reviewed = game.chapters.reduce((total, chapter) => total + chapter.deck.reviewedCardCount, 0);
    const facts = document.querySelector("#game-facts");
    facts.append(
      fact("Chapters", String(game.chapters.length)),
      fact("Reviewed pilot cards", String(reviewed)),
      fact("Audio", game.audio.status === "optional-not-generated" ? "Optional · not generated" : game.audio.status),
      fact("Deck status", "In development")
    );
    renderLanguageProfile(game.languageProfile);
    const names = new Map(game.chapters.map(chapter => [chapter.id, `${String(chapter.order).padStart(2, "0")} ${chapter.title}`]));
    const list = document.querySelector("#chapter-list");
    for (const chapter of game.chapters) list.append(chapterCard(chapter, names));
    statusBox.classList.add("hidden");
    catalog.classList.remove("hidden");
    await loadPersonalCoverage();
    await loadVocabulary(true);
  } catch (error) {
    statusBox.classList.add("error");
    text(statusBox, `The catalog could not be loaded. ${error.message}`);
  }
}

document.querySelector("#vocabulary-filters").addEventListener("submit", event => {
  event.preventDefault();
  loadVocabulary(true).catch(error => text(document.querySelector("#vocabulary-status"), error.message));
});
document.querySelector("#vocabulary-more").addEventListener("click", () => {
  loadVocabulary(false).catch(error => text(document.querySelector("#vocabulary-status"), error.message));
});

loadCatalog();
