const statusBox = document.querySelector("#catalog-status");
const catalog = document.querySelector("#game-catalog");

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
    const response = await fetch("/v1/catalog/games/ocarina-of-time", { headers: { Accept: "application/json" } });
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
      fact("Audio", game.audio.status === "not-generated" ? "Optional · not generated" : game.audio.status),
      fact("Deck status", "In development")
    );
    const names = new Map(game.chapters.map(chapter => [chapter.id, `${String(chapter.order).padStart(2, "0")} ${chapter.title}`]));
    const list = document.querySelector("#chapter-list");
    for (const chapter of game.chapters) list.append(chapterCard(chapter, names));
    statusBox.classList.add("hidden");
    catalog.classList.remove("hidden");
  } catch (error) {
    statusBox.classList.add("error");
    text(statusBox, `The catalog could not be loaded. ${error.message}`);
  }
}

loadCatalog();
