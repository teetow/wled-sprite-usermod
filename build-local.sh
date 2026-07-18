#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 /path/to/WLED [pio-env] [--channel stable|nightly] [--deploy-host HOST] [--skip-build]" >&2
}

if [[ $# -eq 1 && ( $1 == -h || $1 == --help ) ]]; then
  usage
  exit 0
fi

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

USERMOD_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
WLED_DIR=$(cd -- "$1" && pwd)
shift

PIO_ENV=esp32dev
CHANNEL=stable
DEPLOY_HOST=
SKIP_BUILD=false

if [[ $# -gt 0 && $1 != --* ]]; then
  PIO_ENV=$1
  shift
fi

while [[ $# -gt 0 ]]; do
  case $1 in
    --channel)
      if [[ $# -lt 2 ]]; then
        usage
        exit 1
      fi
      CHANNEL=$2
      shift 2
      ;;
    --channel=*)
      CHANNEL=${1#*=}
      shift
      ;;
    --deploy-host)
      if [[ $# -lt 2 ]]; then
        usage
        exit 1
      fi
      DEPLOY_HOST=$2
      shift 2
      ;;
    --deploy-host=*)
      DEPLOY_HOST=${1#*=}
      shift
      ;;
    --skip-build)
      SKIP_BUILD=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ $CHANNEL != stable && $CHANNEL != nightly ]]; then
  echo "Unsupported channel '$CHANNEL'; expected stable or nightly." >&2
  exit 1
fi

if [[ $SKIP_BUILD == true && -z $DEPLOY_HOST ]]; then
  echo "--skip-build requires --deploy-host." >&2
  exit 1
fi

if [[ ! $PIO_ENV =~ ^[A-Za-z0-9_.-]+$ ]]; then
  echo "Invalid PlatformIO environment: $PIO_ENV" >&2
  exit 1
fi

USERMOD_NAME=wled-sprite-usermod
ALT_CONF=platformio.sprite.ini
ALT_ENV=${PIO_ENV}_sprite_usermod
PATCH_FILE="$USERMOD_DIR/patches/wled-alpha-over.patch"
OUTPUT_DIR="$USERMOD_DIR/.build/$CHANNEL/$PIO_ENV"
FIRMWARE_OUTPUT="$OUTPUT_DIR/firmware.bin"
USERMOD_PATH=$(python3 -c 'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve())' "$USERMOD_DIR")
USERMOD_SPEC="$USERMOD_NAME = symlink://$USERMOD_PATH"

if [[ ! -f "$WLED_DIR/platformio.ini" ]] || ! git -C "$WLED_DIR" rev-parse --git-dir >/dev/null 2>&1; then
  echo "Expected a WLED git checkout at: $WLED_DIR" >&2
  exit 1
fi

if [[ ! -f "$PATCH_FILE" ]]; then
  echo "WLED compatibility patch not found at: $PATCH_FILE" >&2
  exit 1
fi

if [[ -x "$WLED_DIR/.venv-pio/bin/python" ]] && "$WLED_DIR/.venv-pio/bin/python" -c 'import platformio' >/dev/null 2>&1; then
  PLATFORMIO_COMMAND=("$WLED_DIR/.venv-pio/bin/python" -m platformio)
elif command -v platformio >/dev/null 2>&1; then
  PLATFORMIO_COMMAND=(platformio)
elif python3 -c 'import platformio' >/dev/null 2>&1; then
  PLATFORMIO_COMMAND=(python3 -m platformio)
else
  echo "PlatformIO was not found. Install it or create $WLED_DIR/.venv-pio." >&2
  exit 1
fi

find_official_remote() {
  local remote url normalized
  while IFS= read -r remote; do
    url=$(git -C "$WLED_DIR" remote get-url "$remote" 2>/dev/null || true)
    normalized=${url,,}
    if [[ $normalized =~ github\.com[:/]wled/wled(\.git)?$ ]]; then
      printf '%s\n' "$remote"
      return 0
    fi
  done < <(git -C "$WLED_DIR" remote)

  return 1
}

resolve_stable_ref() {
  local tag
  tag=$(git -C "$WLED_DIR" ls-remote --tags "$WLED_REMOTE" 'refs/tags/v*' | python3 -c '
import re
import sys

tags = []
for line in sys.stdin:
    ref = line.rstrip().split("\t")[-1]
    match = re.fullmatch(r"refs/tags/(v(\d+)\.(\d+)\.(\d+))", ref)
    if match:
        tags.append((tuple(map(int, match.groups()[1:])), match.group(1)))
if tags:
    print(max(tags)[1])
')
  if [[ -z $tag ]]; then
    return 1
  fi

  git -C "$WLED_DIR" fetch --force "$WLED_REMOTE" "+refs/tags/$tag:refs/tags/$tag"
  printf '%s\n' "$tag"
}

resolve_nightly_ref() {
  git -C "$WLED_DIR" fetch --force "$WLED_REMOTE" '+refs/tags/nightly:refs/tags/nightly'
  printf '%s\n' nightly
}

json_field() {
  python3 -c '
import json, sys

path = sys.argv[1].split(".")
try:
    data = json.load(sys.stdin)
    for key in path:
        data = data[key]
    print(data)
except Exception:
    print("")
' "$1"
}

deploy_firmware() {
  local host=$1 firmware=$2

  if [[ ! -f $firmware ]]; then
    echo "Built firmware not found at: $firmware" >&2
    exit 1
  fi

  local pre_uptime
  pre_uptime=$(curl -fsS --max-time 10 "http://$host/json/info" 2>/dev/null | json_field uptime || true)

  echo "Uploading firmware to $host..."
  local response
  if ! response=$(curl -sS --max-time 600 -F "update=@$firmware" "http://$host/update" 2>&1); then
    echo "OTA upload to $host failed: $response" >&2
    exit 1
  fi
  if [[ $response != *"Update successful!"* ]]; then
    echo "OTA update failed: $response" >&2
    exit 1
  fi

  local deadline=$((SECONDS + 120))
  while (( SECONDS < deadline )); do
    local post_info
    if post_info=$(curl -fsS --max-time 5 "http://$host/json/info" 2>/dev/null); then
      local post_uptime ready
      post_uptime=$(printf '%s' "$post_info" | json_field uptime)
      if [[ -n $post_uptime ]] && { [[ -z $pre_uptime ]] || (( post_uptime < pre_uptime )); }; then
        ready=$(printf '%s' "$post_info" | json_field sprite_usermod.ready)
        ready=${ready,,}
        if [[ $ready != true ]]; then
          echo "Panel rebooted but sprite_usermod is not ready after deployment." >&2
          exit 1
        fi
        echo "Deployment verified on $host (uptime: ${post_uptime}s)."
        return 0
      fi
    fi
    sleep 1
  done

  echo "Uploaded firmware to $host but it did not come back online within 2 minutes." >&2
  exit 1
}

if [[ $SKIP_BUILD == false ]]; then
  WLED_REMOTE=$(find_official_remote || true)
  if [[ -z $WLED_REMOTE ]]; then
    echo "No remote points to official WLED (github.com/wled/WLED)." >&2
    echo "Add an official upstream remote before building." >&2
    exit 1
  fi

  if [[ $CHANNEL == stable ]]; then
    BASE_REF=$(resolve_stable_ref) || {
      echo "Could not resolve the latest stable WLED tag." >&2
      exit 1
    }
  else
    BASE_REF=$(resolve_nightly_ref)
  fi

  BASE_SHA=$(git -C "$WLED_DIR" rev-parse "$BASE_REF^{commit}")
  BUILD_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/wled-sprite-build.XXXXXX")
  BUILD_WLED_DIR="$BUILD_ROOT/WLED"
  WORKTREE_ADDED=false

  cleanup() {
    cd "$USERMOD_DIR" || true
    if [[ $WORKTREE_ADDED == true ]]; then
      git -C "$WLED_DIR" worktree remove --force "$BUILD_WLED_DIR" >/dev/null 2>&1 || true
    fi
    rmdir "$BUILD_ROOT" >/dev/null 2>&1 || true
  }
  trap cleanup EXIT

  echo "Building WLED $CHANNEL from $BASE_REF ($BASE_SHA)"
  git -C "$WLED_DIR" worktree add --detach "$BUILD_WLED_DIR" "$BASE_SHA"
  WORKTREE_ADDED=true

  if ! git -C "$BUILD_WLED_DIR" apply --check "$PATCH_FILE"; then
    echo "The alpha-over patch does not apply cleanly to WLED $BASE_REF ($BASE_SHA)." >&2
    echo "Review patches/wled-alpha-over.patch against the selected WLED version." >&2
    exit 1
  fi
  git -C "$BUILD_WLED_DIR" apply "$PATCH_FILE"

  PLATFORMIO_INI="$BUILD_WLED_DIR/platformio.ini"
  ALT_CONF_PATH="$BUILD_WLED_DIR/$ALT_CONF"

  awk '
  BEGIN{skip=0}
  /^extra_configs =/ {print; skip=1; next}
  skip==1 && /platformio_override\.ini/ {next}
  skip==1 && /platformio_release\.ini/ {print; skip=0; next}
  {print}
  ' "$PLATFORMIO_INI" > "$ALT_CONF_PATH"

  {
    printf '\n[env:%s]\n' "$ALT_ENV"
    printf 'extends = env:%s\n' "$PIO_ENV"
    printf 'custom_usermods =\n'
    printf '  ${env:%s.custom_usermods}\n' "$PIO_ENV"
    printf '  %s\n' "$USERMOD_SPEC"
  } >> "$ALT_CONF_PATH"

  cd "$BUILD_WLED_DIR"
  "${PLATFORMIO_COMMAND[@]}" run -c "$ALT_CONF" -e "$ALT_ENV"

  FIRMWARE_PATH="$BUILD_WLED_DIR/.pio/build/$ALT_ENV/firmware.bin"
  if [[ ! -f $FIRMWARE_PATH ]]; then
    echo "Built firmware not found at: $FIRMWARE_PATH" >&2
    exit 1
  fi

  mkdir -p "$OUTPUT_DIR"
  cp "$FIRMWARE_PATH" "$FIRMWARE_OUTPUT"

  USERMOD_SHA=$(git -C "$USERMOD_DIR" rev-parse HEAD 2>/dev/null || printf 'unknown')
  if [[ -n $(git -C "$USERMOD_DIR" status --porcelain --untracked-files=normal) ]]; then
    USERMOD_DIRTY=true
  else
    USERMOD_DIRTY=false
  fi
  PATCH_SHA=$(git hash-object "$PATCH_FILE")
  BUILT_AT=$(date -u +'%Y-%m-%dT%H:%M:%SZ')

  {
    printf '{\n'
    printf '  "channel": "%s",\n' "$CHANNEL"
    printf '  "wled_ref": "%s",\n' "$BASE_REF"
    printf '  "wled_sha": "%s",\n' "$BASE_SHA"
    printf '  "usermod_sha": "%s",\n' "$USERMOD_SHA"
    printf '  "usermod_dirty": %s,\n' "$USERMOD_DIRTY"
    printf '  "patch_sha": "%s",\n' "$PATCH_SHA"
    printf '  "pio_environment": "%s",\n' "$PIO_ENV"
    printf '  "built_at": "%s"\n' "$BUILT_AT"
    printf '}\n'
  } > "$OUTPUT_DIR/build-info.json"

  echo "Firmware: $FIRMWARE_OUTPUT"
  echo "Build metadata: $OUTPUT_DIR/build-info.json"
fi

if [[ -n $DEPLOY_HOST ]]; then
  deploy_firmware "$DEPLOY_HOST" "$FIRMWARE_OUTPUT"
fi
