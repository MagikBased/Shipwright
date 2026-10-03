export const ANKI_MODEL = "JP Assist Vocabulary";
export const ANKI_FIELDS = ["Word ID", "Word", "Reading", "Meaning", "Part of Speech", "Game"];

export function noteIdentity(word) {
  return `${word.wordId}|${word.senseId || ""}`;
}

function desiredNote(word, deckName) {
  return {
    deckName,
    modelName: ANKI_MODEL,
    fields: {
      "Word ID": noteIdentity(word),
      Word: word.dictionary.written,
      Reading: word.dictionary.reading,
      Meaning: word.dictionary.meaning,
      "Part of Speech": word.dictionary.partOfSpeech,
      Game: word.gameIds.join(", "),
    },
    options: { allowDuplicate: false },
    tags: ["jp-assist", ...word.gameIds.map(id => `game::${id.replace(/[^A-Za-z0-9_-]/g, "_")}`)],
  };
}

function fieldValue(note, name) {
  return note.fields?.[name]?.value ?? "";
}

function needsUpdate(existing, desired) {
  return ANKI_FIELDS.some(name => fieldValue(existing, name) !== desired.fields[name]) ||
    desired.tags.some(tag => !existing.tags?.includes(tag));
}

export async function preflightAnki(anki, manifest, deckName) {
  const eligible = manifest.words.filter(word => word.dictionary?.meaning);
  const skipped = manifest.words.length - eligible.length;
  const desired = eligible.map(word => desiredNote(word, deckName));
  const modelNames = await anki("modelNames");
  const modelExists = modelNames.includes(ANKI_MODEL);
  let modelFields = [];
  let existingNotes = [];
  if (modelExists) {
    modelFields = await anki("modelFieldNames", { modelName: ANKI_MODEL });
    if (modelFields.includes("Word ID")) {
      const noteIds = await anki("findNotes", { query: `note:\"${ANKI_MODEL}\"` });
      existingNotes = noteIds.length ? await anki("notesInfo", { notes: noteIds }) : [];
    }
  }

  const byIdentity = new Map();
  for (const note of existingNotes) {
    const identity = fieldValue(note, "Word ID");
    if (!identity) continue;
    const matches = byIdentity.get(identity) || [];
    matches.push(note);
    byIdentity.set(identity, matches);
  }

  const additions = [], updates = [], unchanged = [], duplicateIdentities = [];
  for (const note of desired) {
    const matches = byIdentity.get(note.fields["Word ID"]) || [];
    if (!matches.length) additions.push(note);
    else if (matches.length > 1) duplicateIdentities.push({ identity: note.fields["Word ID"], noteIds: matches.map(item => item.noteId) });
    else if (needsUpdate(matches[0], note)) updates.push({ existing: matches[0], desired: note });
    else unchanged.push({ existing: matches[0], desired: note });
  }

  const missingFields = modelExists ? ANKI_FIELDS.filter(field => !modelFields.includes(field)) : [];
  const blockers = [];
  if (modelExists && missingFields.includes("Word ID")) blockers.push("The existing JP Assist note type has no Word ID field, so its notes cannot be matched safely.");
  if (duplicateIdentities.length) blockers.push(`${duplicateIdentities.length} stable identit${duplicateIdentities.length === 1 ? "y has" : "ies have"} multiple Anki notes.`);
  return {
    deckName, modelExists, missingFields, blockers, additions, updates, unchanged,
    duplicateIdentities, skipped,
    counts: {
      additions: additions.length,
      updates: updates.length,
      unchanged: unchanged.length,
      duplicates: duplicateIdentities.length,
      skipped,
      fieldMigrations: modelExists ? missingFields.filter(field => field !== "Word ID").length : 0,
      conflicts: blockers.length,
    },
  };
}

export async function applyAnkiPlan(anki, plan) {
  if (plan.blockers.length) throw new Error(`Resolve preflight conflicts first: ${plan.blockers.join(" ")}`);
  await anki("createDeck", { deck: plan.deckName });
  if (!plan.modelExists) {
    await anki("createModel", {
      modelName: ANKI_MODEL,
      inOrderFields: ANKI_FIELDS,
      css: ".card{font-family:sans-serif;font-size:24px;text-align:center;color:#eee;background:#222}.word{font-size:44px}",
      cardTemplates: [{ Name: "Recognition", Front: '<div class="word">{{Word}}</div>', Back: '{{FrontSide}}<hr>{{Reading}}<br>{{Meaning}}<br><small>{{Part of Speech}} · {{Game}}</small>' }],
    });
  } else {
    for (const fieldName of plan.missingFields) {
      await anki("modelFieldAdd", { modelName: ANKI_MODEL, fieldName });
    }
  }
  for (const update of plan.updates) {
    await anki("updateNote", {
      note: {
        id: update.existing.noteId,
        fields: update.desired.fields,
        tags: [...new Set([...(update.existing.tags || []), ...update.desired.tags])],
      },
    });
  }
  const results = plan.additions.length ? await anki("addNotes", { notes: plan.additions }) : [];
  return {
    added: results.filter(Boolean).length,
    updated: plan.updates.length,
    unchanged: plan.unchanged.length,
    racedDuplicates: results.filter(value => !value).length,
  };
}

function ankiIntervalDays(value) {
  const interval = Number(value) || 0;
  return interval < 0 ? Math.abs(interval) / 86400 : interval;
}

export async function collectAnkiReviews(anki, manifest) {
  const desired = new Map(manifest.words.map(word => [noteIdentity(word), word]));
  const noteIds = await anki("findNotes", { query: `note:\"${ANKI_MODEL}\"` });
  const notes = noteIds.length ? await anki("notesInfo", { notes: noteIds }) : [];
  const cards = new Map();
  for (const note of notes) {
    const word = desired.get(fieldValue(note, "Word ID"));
    if (!word) continue;
    for (const cardId of note.cards || []) cards.set(String(cardId), word);
  }
  if (!cards.size) return { reviews: [], skipped: 0, cards: 0 };

  let history;
  try {
    history = await anki("getReviewsOfCards", { cards: [...cards.keys()] });
  } catch (error) {
    throw new Error(`This AnkiConnect version does not support review-history import. Update AnkiConnect and retry. (${error.message})`);
  }
  if (!history || typeof history !== "object" || Array.isArray(history)) {
    throw new Error("AnkiConnect returned malformed review history.");
  }

  const reviews = [];
  let skipped = 0;
  for (const [cardId, entries] of Object.entries(history)) {
    const word = cards.get(String(cardId));
    if (!word || !Array.isArray(entries)) continue;
    for (const entry of entries) {
      const rating = Number(entry.ease);
      const timestamp = Number(entry.id);
      if (![1, 2, 3, 4].includes(rating) || !Number.isFinite(timestamp)) {
        skipped++;
        continue;
      }
      reviews.push({
        sourceReviewId: String(entry.id), sourceCardId: String(cardId),
        wordId: word.wordId, senseId: word.senseId || null, rating,
        reviewedAt: new Date(timestamp).toISOString(),
        intervalDays: ankiIntervalDays(entry.ivl),
        previousIntervalDays: ankiIntervalDays(entry.lastIvl),
        factor: Number.isInteger(Number(entry.factor)) ? Number(entry.factor) : null,
        durationMs: Number.isInteger(Number(entry.time)) ? Number(entry.time) : null,
        reviewType: Number.isInteger(Number(entry.type)) ? Number(entry.type) : null,
      });
    }
  }
  reviews.sort((left, right) => left.reviewedAt.localeCompare(right.reviewedAt) || left.sourceReviewId.localeCompare(right.sourceReviewId));
  return { reviews, skipped, cards: cards.size };
}
