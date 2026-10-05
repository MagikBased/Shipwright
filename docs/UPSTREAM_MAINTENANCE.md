# Upstream maintenance

This repository is a fork of Shipwright with the JP Assist plugin and a small,
generic study-mod host API layered on top. Keep the official projects as
read-only `upstream` remotes and push project branches only to the forks.

## Remotes

The main repository uses:

- `origin`: `https://github.com/MagikBased/Shipwright.git`
- `upstream`: `https://github.com/HarbourMasters/Shipwright.git`

The `libultraship` submodule uses:

- `origin`: `https://github.com/MagikBased/libultraship.git`
- `upstream`: `https://github.com/kenix3/libultraship.git`

The parent repository intentionally points `.gitmodules` at the libultraship
fork because its pinned revision contains the code-mod loading and shutdown
reliability patches. The fork still tracks the official project through its
own `upstream` remote.

## Updating Shipwright

Fetch and merge the official development branch from the repository root:

```sh
git fetch upstream
git switch mod/oot-jp-assist
git merge upstream/develop
git submodule sync --recursive
git submodule update --init --recursive
```

Resolve conflicts in favor of Shipwright's current implementation, then add
back the narrow host hooks. The intended high-conflict integration points are:

- `soh/src/code/z_message_PAL.c`: native dialogue observation/highlighting;
- `soh/soh/OTRGlobals.cpp`: one audio mixer callback;
- `soh/soh/Enhancements/mod_menu.cpp`: collect mounted archives and hand them
  to `ModApi/CodeModLoader`;
- `soh/CMakeLists.txt`: host/plugin dependencies and packaging.

Game-specific behavior belongs in
`games/ocarina-of-time/adapters/shipwright/plugin`; reusable host ABI
code belongs in `soh/soh/ModApi` and `soh/include/mods`. Avoid adding JP Assist
policy to the generic bridge.

## Updating libultraship

Shipwright pins an exact submodule revision. Rebase the fork-only patches onto
the new revision rather than merging an old libultraship history:

```sh
cd libultraship
git fetch upstream
git switch -C mod/code-mod-binary-loading <new-shipwright-pinned-revision>
git cherry-pick <unattended-crash-reporting> <shutdown-order> <code-mod-loader>
git push --force-with-lease origin mod/code-mod-binary-loading
cd ..
git add libultraship
```

Before pushing the parent branch, configure a fresh-enough build, build
`soh`, `jpassist_plugin_package`, `jpassist_tests`, and
`jpassist_plugin_tests`, then run `ctest --test-dir build-cmake
--output-on-failure` and the automated plugin smoke suite.
