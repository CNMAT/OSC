#!/bin/bash

# This script is triggered from the script section of .travis.yml
# It runs the appropriate commands depending on the task requested.

set -e

CPP_LINT_URL="https://raw.githubusercontent.com/google/styleguide/gh-pages/cpplint/cpplint.py";

SPELLINGBLACKLIST=$(cat <<-BLACKLIST
      -wholename "./.codespellignore" -or \
      -wholename "./.git/*"
BLACKLIST
)

if [[ $TASK = 'lint' ]]; then
  # run the lint tool only if it is the requested task
  # first check we've not got any generic NOLINTs
  # count the number of generic NOLINTs
  nolints=$(grep -IR NOLINT * | grep -v "NOLINT(" | wc -l)
  if [[ $nolints -ne 0 ]]; then
    # print the output for info
    echo $(grep -IR NOLINT * | grep -v "NOLINT(")
    echo "Found $nolints generic NOLINTs"
    exit 1;
  else
    echo "Found $nolints generic NOLINTs"
  fi;
  # then fetch and run the main cpplint tool
  wget -O cpplint.py $CPP_LINT_URL;
  chmod u+x cpplint.py;
  ./cpplint.py \
    --filter=-legal/copyright,-readability/streams,-runtime/arrays \
    $(find ./ \( -name "*.h" -or -name "*.cpp" \) | xargs)
  if [[ $? -ne 0 ]]; then
    exit 1;
  fi;
elif [[ $TASK = 'spellintian' ]]; then
  # run spellintian only if it is the requested task, ignoring duplicate words
  spellingfiles=$(eval "find ./ -type f -and ! \( \
      $SPELLINGBLACKLIST \
      \) | xargs")
  # count the number of spellintian errors, ignoring duplicate words
  spellingerrors=$(zrun spellintian $spellingfiles 2>&1 | grep -v "\(duplicate word\)" | wc -l)
  if [[ $spellingerrors -ne 0 ]]; then
    # print the output for info
    zrun spellintian $spellingfiles | grep -v "\(duplicate word\)"
    echo "Found $spellingerrors spelling errors via spellintian, ignoring duplicates"
    exit 1;
  else
    echo "Found $spellingerrors spelling errors via spellintian, ignoring duplicates"
  fi;
elif [[ $TASK = 'spellintian-duplicates' ]]; then
  # run spellintian only if it is the requested task
  spellingfiles=$(eval "find ./ -type f -and ! \( \
      $SPELLINGBLACKLIST \
      \) | xargs")
  # count the number of spellintian errors
  spellingerrors=$(zrun spellintian $spellingfiles 2>&1 | wc -l)
  if [[ $spellingerrors -ne 0 ]]; then
    # print the output for info
    zrun spellintian $spellingfiles
    echo "Found $spellingerrors spelling errors via spellintian"
    exit 1;
  else
    echo "Found $spellingerrors spelling errors via spellintian"
  fi;
elif [[ $TASK = 'codespell' ]]; then
  # run codespell only if it is the requested task
  spellingfiles=$(eval "find ./ -type f -and ! \( \
      $SPELLINGBLACKLIST \
      \) | xargs")
  # count the number of codespell errors
  spellingerrors=$(zrun codespell --check-filenames --quiet 2 --regex "[a-zA-Z0-9][\\-'a-zA-Z0-9]+[a-zA-Z0-9]" --exclude-file .codespellignore $spellingfiles 2>&1 | wc -l)
  if [[ $spellingerrors -ne 0 ]]; then
    # print the output for info
    zrun codespell --check-filenames --quiet 2 --regex "[a-zA-Z0-9][\\-'a-zA-Z0-9]+[a-zA-Z0-9]" --exclude-file .codespellignore $spellingfiles
    echo "Found $spellingerrors spelling errors via codespell"
    exit 1;
  else
    echo "Found $spellingerrors spelling errors via codespell"
  fi;
else
  platformio ci --lib="." --board=$BOARD
fi
