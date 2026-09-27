#!/usr/bin/env bash
# Scan the working tree and the whole git history for anything that would tie
# this repository to a specific home: credentials, LAN addresses, MAC addresses,
# personal e-mail, recordings. Run it before every push and once more before the
# repository is made public.
#
#   tools/check-private.sh            # tree + history
#   tools/check-private.sh --staged   # only what is about to be committed
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

if ! command -v gitleaks >/dev/null 2>&1; then
  echo "gitleaks is not installed. Fedora: sudo dnf install gitleaks" >&2
  exit 2
fi

if [[ "${1:-}" == "--staged" ]]; then
  gitleaks protect --staged --config .gitleaks.toml --redact --verbose
else
  gitleaks detect --config .gitleaks.toml --redact --verbose
fi

# Audio never belongs in the tree, regardless of size.
if git ls-files | grep -Eiq '\.(wav|pcm|raw|flac|mp3|ogg|opus)$'; then
  echo "audio files are tracked in git:" >&2
  git ls-files | grep -Ei '\.(wav|pcm|raw|flac|mp3|ogg|opus)$' >&2
  exit 1
fi

echo "check-private: clean"
