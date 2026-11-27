# Security policy

## Reporting a vulnerability

Please report security problems privately: open the repository's **Security** tab and choose
**Report a vulnerability**. Don't open a public issue for them.

Include what you opened or did, what happened, and the file that triggers it if there is one
(attach it to the private report, not anywhere public).

## Supported versions

Only the latest release gets fixes. Older releases are not patched; update to the newest one.

## Scope

In scope:

- Crashes, memory corruption or code execution from opening a crafted file: module formats
  (XM, MOD, S3M, IT and the other formats Kollapse loads through its bundled libxmp), `.zip`
  archives, `.m3u` playlists, and WAV files loaded as samples
- Saving and WAV export writing outside the folder they should write to
- The Modland search: handling of the downloaded index and of downloaded modules, and anything
  that makes Kollapse fetch from somewhere other than `ftp.modland.com` over HTTPS
- Kollapse's own files: `kollapse.ini`, `favorites.m3u` and the autosave folder
- The release build: the GitHub Actions workflow, the published zip and its attestation

Out of scope:

- The Windows SmartScreen warning: releases are not code-signed, so Windows shows it for any
  download; verify the zip with `gh attestation verify` instead
- Bugs in libxmp, miniz or miniaudio that Kollapse can't reach; report those upstream (do tell
  us if a release ships an affected version)
- Anything that needs an attacker who can already write to your user folder or run programs as you
- Builds you made yourself from modified source, and the Linux version, which has no releases yet
