# Publish the prepared repository

This repository is prepared locally. No GitHub repository or remote was
created, and no files were pushed.

If the local repository has no initial commit, complete it from a terminal
where the configured signing agent can prompt:

```sh
git commit -F docs/initial-commit-message.txt
```

The prepared commit message records validation and AI assistance without a
human sign-off. Keep the configured commit signing and verification hooks.

After reviewing the patches, notices and validation boundary, create an empty
GitHub repository using the desired account and visibility. Do not initialize
the remote with another README or license. Then, from this repository:

```sh
git remote add origin git@github.com:YOUR_ACCOUNT/nvidia-gpu-ext-patches.git
git push -u origin main
```

Replace `YOUR_ACCOUNT` with the actual owner. Keep the exact-base metadata,
ordered series, licensing files and validation limits together in the release.
No release tag, binary package or performance certification is implied by this
source-only preparation.
