#!/data/data/com.termux/files/usr/bin/bash
set -Eeuo pipefail

ZIP_NAME="AKISoundStudio-v0.7.1-source-full-bank-relocation.zip"
REPO_NAME="AKISoundStudio"
WORK="$HOME/aki-sound-studio-v070-update"
ARTIFACT="AKISoundStudio-v0.7.1-win64"
WORKFLOW="windows-build.yml"
DESCRIPTION="AKI N64 wrestling game sound-bank editor"
LOG_FILE=""
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REQUESTED_ZIP="${1:-}"
DOWNLOADS=""

fail() {
  echo
  echo "ERROR: $*" >&2
  [[ -n "$LOG_FILE" ]] && echo "Log: $LOG_FILE" >&2
  exit 1
}

info() {
  echo
  echo "== $* =="
}

on_error() {
  local code=$?
  echo
  echo "ERROR: Command failed on line $1:" >&2
  echo "  $2" >&2
  echo "Exit code: $code" >&2
  [[ -n "$LOG_FILE" ]] && echo "Log: $LOG_FILE" >&2
  exit "$code"
}
trap 'on_error "$LINENO" "$BASH_COMMAND"' ERR

info "Installing required Termux packages"
pkg install -y git gh unzip zip findutils coreutils

if [[ ! -d "$HOME/storage/shared" ]]; then
  info "Requesting Android storage access"
  termux-setup-storage
  sleep 3
fi

# Prefer Termux's Downloads link, but fall back to Android's real Download
# folder or the folder containing this script. Different Android apps save
# attachments in different shared-storage locations.
for output_dir in \
  "$HOME/storage/downloads" \
  "$HOME/storage/shared/Download" \
  "/storage/emulated/0/Download" \
  "/sdcard/Download" \
  "$SCRIPT_DIR"; do
  if [[ -d "$output_dir" && -w "$output_dir" ]]; then
    DOWNLOADS="$output_dir"
    break
  fi
done
[[ -n "$DOWNLOADS" ]] || fail "No writable Android download folder was found. Run termux-setup-storage, grant storage permission, and rerun this script."

LOG_FILE="$DOWNLOADS/AKISoundStudio-v0.7.1-termux-update.log"
: > "$LOG_FILE"
exec > >(tee -a "$LOG_FILE") 2>&1

resolve_source_zip() {
  local supplied="$REQUESTED_ZIP"
  local candidate mtime newest_mtime=0
  local -a roots=()
  local -a candidates=()
  declare -A seen=()

  if [[ -n "$supplied" ]]; then
    if [[ "$supplied" == ~/* ]]; then
      supplied="$HOME/${supplied#~/}"
    fi
    [[ -f "$supplied" ]] || fail "The source ZIP path you supplied does not exist: $supplied"
    printf '%s\n' "$supplied"
    return 0
  fi

  roots=(
    "$SCRIPT_DIR"
    "$HOME/storage/downloads"
    "$HOME/storage/shared/Download"
    "$HOME/storage/shared/Downloads"
    "$HOME/storage/shared/Documents"
    "$HOME/storage/shared"
    "/storage/emulated/0/Download"
    "/storage/emulated/0/Downloads"
    "/storage/emulated/0/Documents"
    "/storage/emulated/0"
    "/sdcard/Download"
    "/sdcard/Downloads"
    "/sdcard/Documents"
  )

  for root in "${roots[@]}"; do
    [[ -d "$root" ]] || continue
    while IFS= read -r -d '' candidate; do
      [[ -n "${seen[$candidate]:-}" ]] && continue
      seen[$candidate]=1
      candidates+=("$candidate")
    done < <(
      find "$root" -maxdepth 5 -type f \
        \( -iname 'AKISoundStudio-v0.7.1-source*.zip' \
           -o -iname '*AKISoundStudio*v0.7.1*source*.zip' \) \
        -print0 2>/dev/null
    )
  done

  ZIP_PATH=""
  for candidate in "${candidates[@]}"; do
    mtime="$(stat -c '%Y' "$candidate" 2>/dev/null || printf '0')"
    if [[ "$mtime" =~ ^[0-9]+$ ]] && (( mtime >= newest_mtime )); then
      newest_mtime="$mtime"
      ZIP_PATH="$candidate"
    fi
  done

  if [[ -n "$ZIP_PATH" && -f "$ZIP_PATH" ]]; then
    printf '%s\n' "$ZIP_PATH"
    return 0
  fi

  echo "Searched these phone-storage locations:" >&2
  for root in "${roots[@]}"; do
    [[ -d "$root" ]] && echo "  $root" >&2
  done
  echo >&2
  echo "You can bypass automatic detection by passing the ZIP path:" >&2
  echo "  bash '$SCRIPT_DIR/$(basename "${BASH_SOURCE[0]}")' '/storage/emulated/0/Download/$ZIP_NAME'" >&2
  return 1
}

ZIP_PATH="$(resolve_source_zip)" || fail "Could not find the AKISoundStudio v0.7.1 source ZIP anywhere in shared phone storage."
[[ -f "$ZIP_PATH" ]] || fail "Source ZIP resolved to an invalid path: $ZIP_PATH"

info "Checking GitHub authentication"
if ! gh auth status --hostname github.com >/dev/null 2>&1; then
  export BROWSER="${BROWSER:-termux-open-url}"
  gh auth login \
    --hostname github.com \
    --git-protocol https \
    --web \
    --scopes repo,workflow
fi

gh auth setup-git --hostname github.com

OWNER="$(gh api user --jq .login)"
[[ -n "$OWNER" ]] || fail "Could not determine the signed-in GitHub username."
REPO="$OWNER/$REPO_NAME"

info "Preparing source from $(basename "$ZIP_PATH")"
rm -rf "$WORK"
mkdir -p "$WORK/unpacked"
unzip -q "$ZIP_PATH" -d "$WORK/unpacked"

CMAKE_FILE="$(find "$WORK/unpacked" -type f -name CMakeLists.txt -print -quit)"
[[ -n "$CMAKE_FILE" ]] || fail "The source ZIP does not contain CMakeLists.txt."
SOURCE_ROOT="${CMAKE_FILE%/CMakeLists.txt}"
[[ -f "$SOURCE_ROOT/.github/workflows/$WORKFLOW" ]] || fail "The Windows build workflow is missing from the source ZIP."

REPO_EXISTS=false
if gh repo view "$REPO" >/dev/null 2>&1; then
  REPO_EXISTS=true
  info "Updating existing repository $REPO on main only"

  VISIBILITY="$(gh repo view "$REPO" --json visibility --jq .visibility)"
  if [[ "$VISIBILITY" != "PUBLIC" ]]; then
    info "Changing $REPO from $VISIBILITY to PUBLIC"
    gh repo edit "$REPO" \
      --visibility public \
      --accept-visibility-change-consequences
  fi

  if git ls-remote --exit-code --heads "https://github.com/$REPO.git" main >/dev/null 2>&1; then
    git clone --branch main --single-branch "https://github.com/$REPO.git" "$WORK/repository"
  else
    # An existing but empty repository has no branch yet. Its first and only
    # branch will be main; no version/update branch is created.
    mkdir -p "$WORK/repository"
    cd "$WORK/repository"
    git init -b main
    git remote add origin "https://github.com/$REPO.git"
  fi
else
  info "Creating PUBLIC GitHub repository $REPO with main as its only branch"
  mkdir -p "$WORK/repository"
  cd "$WORK/repository"
  git init -b main
fi

cd "$WORK/repository"
git config user.name "$OWNER"
git config user.email "$OWNER@users.noreply.github.com"
git config core.autocrlf false

# Replace the checked-out source tree while preserving only .git. This updates
# main in place and avoids creating a v0.7.1 or any other separate branch.
find . -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf -- {} +
cp -a "$SOURCE_ROOT"/. .
rm -rf build build-linux build-clang

git add -A
if git diff --cached --quiet; then
  info "The main branch already contains this v0.7.1 source"
else
  git commit -m "AKI Sound Studio v0.7.1"
fi
HEAD_SHA="$(git rev-parse HEAD)"

if [[ "$REPO_EXISTS" == true ]]; then
  info "Pushing directly to the existing main branch"
  git push --set-upstream origin main
else
  gh repo create "$REPO" \
    --public \
    --description "$DESCRIPTION" \
    --source=. \
    --remote=origin \
    --push
fi

VISIBILITY="$(gh repo view "$REPO" --json visibility --jq .visibility)"
[[ "$VISIBILITY" == "PUBLIC" ]] || fail "$REPO exists but is not public. Current visibility: $VISIBILITY"

CURRENT_BRANCH="$(git branch --show-current)"
[[ "$CURRENT_BRANCH" == "main" ]] || fail "Updater unexpectedly left main. Current branch: $CURRENT_BRANCH"

info "Public repository confirmed on main only: https://github.com/$REPO"

info "Locating the GitHub Actions Windows build"
RUN_ID=""
for _ in $(seq 1 45); do
  RUN_ID="$(gh run list \
    --repo "$REPO" \
    --workflow "$WORKFLOW" \
    --branch main \
    --limit 20 \
    --json databaseId,headSha \
    --jq ".[] | select(.headSha == \"$HEAD_SHA\") | .databaseId" 2>/dev/null | head -n 1)"
  [[ -n "$RUN_ID" ]] && break
  sleep 2
done

if [[ -z "$RUN_ID" ]]; then
  info "No push-triggered run appeared; dispatching the workflow manually"
  gh workflow run "$WORKFLOW" --repo "$REPO" --ref main

  for _ in $(seq 1 45); do
    RUN_ID="$(gh run list \
      --repo "$REPO" \
      --workflow "$WORKFLOW" \
      --branch main \
      --limit 20 \
      --json databaseId,headSha \
      --jq ".[] | select(.headSha == \"$HEAD_SHA\") | .databaseId" 2>/dev/null | head -n 1)"
    [[ -n "$RUN_ID" ]] && break
    sleep 2
  done
fi

[[ -n "$RUN_ID" ]] || fail "Could not locate or start the Windows build. Open https://github.com/$REPO/actions to inspect Actions permissions."

info "Watching GitHub Actions run $RUN_ID"
if ! gh run watch "$RUN_ID" --repo "$REPO" --exit-status; then
  BUILD_LOG="$DOWNLOADS/AKISoundStudio-v0.7.1-failed-build-$RUN_ID.log"
  gh run view "$RUN_ID" --repo "$REPO" --log > "$BUILD_LOG" 2>&1 || true
  fail "Windows build failed. Build log saved to: $BUILD_LOG"
fi

DEST="$DOWNLOADS/$ARTIFACT"
FLAT_ZIP="$DOWNLOADS/$ARTIFACT.zip"
rm -rf "$DEST" "$FLAT_ZIP"
mkdir -p "$DEST"

info "Downloading the Windows build"
AVAILABLE_ARTIFACTS="$(gh api \
  -H "Accept: application/vnd.github+json" \
  "/repos/$REPO/actions/runs/$RUN_ID/artifacts?per_page=100" \
  --jq '.artifacts[] | select(.expired == false) | .name' 2>/dev/null || true)"

if [[ -z "$AVAILABLE_ARTIFACTS" ]]; then
  fail "The successful Actions run uploaded no downloadable artifacts. Open https://github.com/$REPO/actions/runs/$RUN_ID"
fi

SELECTED_ARTIFACT=""
if grep -Fxq "$ARTIFACT" <<< "$AVAILABLE_ARTIFACTS"; then
  SELECTED_ARTIFACT="$ARTIFACT"
else
  SELECTED_ARTIFACT="$(grep -Ei 'AKISoundStudio.*(win|windows)|win64|windows|release' <<< "$AVAILABLE_ARTIFACTS" | head -n 1 || true)"
fi
if [[ -z "$SELECTED_ARTIFACT" ]]; then
  ARTIFACT_COUNT="$(sed '/^$/d' <<< "$AVAILABLE_ARTIFACTS" | wc -l | tr -d ' ')"
  if [[ "$ARTIFACT_COUNT" == "1" ]]; then
    SELECTED_ARTIFACT="$(sed '/^$/d' <<< "$AVAILABLE_ARTIFACTS" | head -n 1)"
  else
    echo "Available artifacts:" >&2
    sed 's/^/  - /' <<< "$AVAILABLE_ARTIFACTS" >&2
    fail "Could not identify the Windows artifact automatically."
  fi
fi

echo "Using artifact: $SELECTED_ARTIFACT"
gh run download "$RUN_ID" --repo "$REPO" --name "$SELECTED_ARTIFACT" --dir "$DEST"
(
  cd "$DEST"
  zip -qr "$FLAT_ZIP" .
)

echo
echo "BUILD COMPLETE"
echo "Public repository: https://github.com/$REPO"
echo "Windows folder: $DEST"
echo "Windows ZIP: $FLAT_ZIP"
echo "Termux log: $LOG_FILE"
