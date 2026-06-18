#!/bin/bash
REMOTE="${1:-upstream/master}"
BASE=$(git merge-base HEAD "$REMOTE")

echo "📍 Base: $BASE"
echo "🔍 Checking conflicts between HEAD and $REMOTE ..."
echo "---"

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
  else
    echo "✅ $f (auto-mergeable)"
  fi
done
