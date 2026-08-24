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

# ✅ 预计算冲突文件集合，用于后续 O(1) 查找
declare -A CONFLICT_FILES
while IFS= read -r f; do
  [ -z "$f" ] && continue
  if ! git merge-file -p \
      <(git show "$BASE:$f" 2>/dev/null) \
      <(git show "HEAD:$f" 2>/dev/null) \
      <(git show "$REMOTE:$f" 2>/dev/null) > /dev/null 2>&1; then
    CONFLICT_FILES["$f"]=1
  fi
done < <(comm -12 \
  <(git diff --name-only "$BASE" HEAD | sort) \
  <(git diff --name-only "$BASE" "$REMOTE" | sort))

# ✅ 遍历 HEAD 相对于 BASE 的【所有】变更文件
while IFS= read -r f; do
  [ -z "$f" ] && continue

  mkdir -p "$PATCH_DIR"
  patch_file="$(echo "$f" | tr '/' '_').patch"

  # ✅ 根据是否在冲突集合中决定标签
  if [[ -v "CONFLICT_FILES[$f]" ]]; then
    echo "❌ $f"
    # echo "   📝 Conflict patch: $patch_file"
    has_conflict=1
    patch_file="$PATCH_DIR/c_${patch_file}"
  else
    # echo "✅ $f (auto-mergeable)"
    # echo "   📝 Clean patch: $patch_file"
    patch_file="$PATCH_DIR/a_${patch_file}"
  fi

  # 生成补丁（加 --binary 支持二进制文件）
  if git diff --binary "$BASE" HEAD -- "$f" > "$patch_file" 2>/dev/null; then
    # 清理空补丁（例如仅权限变更等极端情况）
    [ -s "$patch_file" ] || rm -f "$patch_file"
  else
    echo "   ⚠️  Failed to generate patch for $f"
    continue
  fi
 
done < <(git diff --name-only "$BASE" HEAD | sort)

echo "---"
if [ "$has_conflict" == 1 ]; then
  echo "💡 Patches saved to: $PATCH_DIR/"
  echo "   Apply with: git apply $PATCH_DIR/<file>.patch"
else
  echo "🎉 No conflicts detected!"
  if [ -d "$PATCH_DIR" ]; then
    echo "💡 Clean patches saved to: $PATCH_DIR/"
  fi
fi
