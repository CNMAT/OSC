#!/bin/sh
# Install the repo's git hooks. Hooks live in .git/hooks, which git does not
# version, so a fresh clone has none -- run this once after cloning.
#
#   sh tools/install-hooks.sh
#
# Installs: pre-commit, which refuses to commit WiFi credentials (see the
# comments in the generated file, and the note in .gitignore).
set -e
root=$(git rev-parse --show-toplevel)
cp "$root/tools/pre-commit" "$root/.git/hooks/pre-commit"
chmod +x "$root/.git/hooks/pre-commit"
echo "installed .git/hooks/pre-commit"
