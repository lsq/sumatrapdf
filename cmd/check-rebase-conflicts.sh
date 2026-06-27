#!/bin/bash
set -euo pipefail
set +m
shopt -s lastpipe

REMOTE="${1:-upstream/master}"
BASE=$(git merge-base HEAD "$REMOTE")
PATCH_DIR="conflict_patches_$(date +%Y%m%d_%H%M%S)"

echo "📍 Base: $BASE"
echo "🔍 Checking conflicts between HEAD and $REMOTE ..."
echo "---"

has_conflict=0

comm -12 \
  <(git diff --name-only "$BASE" HEAD | sort) \
  <(git diff --name-only "$BASE" "$REMOTE" | sort) | \
while IFS= read -r f; do
  [ -z "$f" ] && continue

  # 使用三路 diff 检查：如果两边对同一区域的改动不同，则冲突
  if ! git merge-file -p \
      <(git show "$BASE:$f" 2>/dev/null) \
      <(git show "HEAD:$f" 2>/dev/null) \
      <(git show "$REMOTE:$f" 2>/dev/null) > /dev/null 2>&1; then
 
    echo "❌ $f"
 
    # ✅ 新增：为冲突文件生成 HEAD vs BASE 的补丁
    mkdir -p "$PATCH_DIR"
    patch_file="$PATCH_DIR/$(echo "$f" | tr '/' '_').patch"
    if git diff "$BASE" HEAD -- "$f" > "$patch_file" 2>/dev/null; then
      # echo "   📝 Patch generated: $patch_file"
      # printf "❌ "
      printf ""
    else
      echo "   ⚠️  Failed to generate patch for $f"
    fi
 
    has_conflict=1
  else
    echo "✅ $f (auto-mergeable)"
  fi
done

echo "---"
if [ "$has_conflict" == 1 ]; then
  echo "💡 Conflict patches saved to: $PATCH_DIR/"
  echo "   Apply with: git apply $PATCH_DIR/<file>.patch"
else
  echo "🎉 No conflicts detected!"
fi
