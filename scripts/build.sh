#!/usr/bin/env bash
# Configure + build KLOPGEEST (AU/VST3/Standalone + headless tool).
#   ./scripts/build.sh              # everything
#   ./scripts/build.sh KLOPGEEST    # just the plug-in bundles
#   ./scripts/build.sh klop_tool    # just the tests/render/snapshot tool
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[ -d .deps/JUCE ] || ./scripts/fetch-juce.sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DKLOPGEEST_JUCE_DIR="$ROOT/.deps/JUCE" >/dev/null
TARGETS=("${@:-all}")
for i in "${!TARGETS[@]}"; do
  if [ "${TARGETS[$i]}" = "KLOPGEEST" ]; then
    TARGETS=("${TARGETS[@]}" KLOPGEEST_AU KLOPGEEST_VST3 KLOPGEEST_Standalone)
    break
  fi
done
cmake --build build -j"$(sysctl -n hw.ncpu)" --target "${TARGETS[@]}"
echo
echo "Artifacts:"
find build/KLOPGEEST_artefacts -maxdepth 3 \( -name '*.component' -o -name '*.vst3' -o -name '*.app' \) 2>/dev/null | sed 's/^/  /'
