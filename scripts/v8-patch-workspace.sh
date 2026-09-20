#!/usr/bin/env bash

# Keep reusable V8 workspaces free of patches owned by WebScene. These helpers
# deliberately reverse only a patch whose complete reverse applies cleanly;
# unrelated and partially overlapping edits remain for gclient to reject.
webscene_restore_patch_if_applied() {
  local checkout="$1"
  local patch_file="$2"

  if [[ ! -f "$patch_file" ]] \
      || ! git -C "$checkout" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    return 0
  fi
  if git -C "$checkout" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
    git -C "$checkout" apply --reverse "$patch_file"
  fi
}

webscene_apply_patch_once() {
  local checkout="$1"
  local patch_file="$2"

  if git -C "$checkout" apply --check "$patch_file" >/dev/null 2>&1; then
    git -C "$checkout" apply "$patch_file"
  elif ! git -C "$checkout" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
    echo "Cannot apply or recognize V8 patch '$patch_file' in '$checkout'." >&2
    return 1
  fi
}

webscene_restore_v8_patches() {
  local v8_root="$1"
  local repo_root="$2"

  webscene_restore_patch_if_applied \
    "$v8_root" "$repo_root/third-party/v8-patches/V8InspectorConsolePatch.txt"
  webscene_restore_patch_if_applied \
    "$v8_root" "$repo_root/third-party/v8-patches/V8Patch.txt"
  webscene_restore_patch_if_applied \
    "$v8_root" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8ToolchainPatch.txt"
  webscene_restore_patch_if_applied \
    "$v8_root" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8ThinLtoPatch.txt"
  webscene_restore_patch_if_applied \
    "$v8_root/build" "$repo_root/third-party/v8-patches/BuildPatch.txt"
  webscene_restore_patch_if_applied \
    "$v8_root/build" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8BuildNoCrelPatch.txt"
  webscene_restore_patch_if_applied \
    "$v8_root/third_party/icu" "$repo_root/third-party/v8-patches/ICUPatch.txt"
  webscene_restore_patch_if_applied \
    "$v8_root/third_party/partition_alloc" \
    "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8PartitionAllocMacVisibilityPatch.txt"
}
