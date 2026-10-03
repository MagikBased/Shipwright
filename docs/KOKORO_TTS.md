# Optional local Japanese TTS

Catalog decks are text-only by default. Audio generation is a separate staging
step and never runs while serving the website or building a normal deck.

## Reviewed provider

The optional adapter targets `kokoro==0.9.4`, Kokoro-82M, Japanese language code
`j`, and the `jf_alpha` voice. At the time of review (2026-10-03):

- the upstream Kokoro inference package declares the Apache License 2.0 and
  describes the weights as deployable from production to personal projects;
- the Kokoro-82M repository declares `apache-2.0`;
- the upstream Japanese voice table lists `jf_alpha` without a separate CC BY
  source, while the other four Japanese voices name CC BY sources; and
- upstream warns that non-English support can be thin and very short utterances
  may perform poorly.

Upstream references:

- <https://github.com/hexgrad/kokoro/blob/main/README.md>
- <https://github.com/hexgrad/kokoro/blob/main/pyproject.toml>
- <https://huggingface.co/hexgrad/Kokoro-82M/blob/c3327e9bac3dbe55779397bfa82de0f8806fb3bc/VOICES.md>

This is a documented dependency review, not a blanket legal guarantee about
generated output. Recheck the exact package, model, and voice before distributing
audio, preserve required notices, and have a Japanese speaker review every clip.

## Generate a review batch

Install the optional dependencies in an isolated environment; they are large and
are not required by the website, mod, corpus tools, or text-only deck builder:

```bash
python3 -m venv .venv-jp-assist-tts
. .venv-jp-assist-tts/bin/activate
pip install -r scripts/jp_assist/requirements-tts-kokoro.txt
```

Then stage both word and sentence clips:

```bash
python scripts/jp_assist/generate_catalog_audio.py \
  --chapter 1 \
  --provider kokoro-local \
  --accept-kokoro-license-review
```

The first run may download model and voice weights. Output goes to the ignored
`scripts/jp_assist/out/catalog_audio/` directory with a manifest marked
`needs-human-review`. Generation does not mutate catalog JSON or publish media.
After pronunciation, clipping, silence, and license review, approved WAV files
can be copied under the catalog content root and their relative paths added to
`wordAudio` and `sentenceAudio`.

To explicitly exercise the dependency-free path:

```bash
python scripts/jp_assist/generate_catalog_audio.py --chapter 1 --provider none
```
