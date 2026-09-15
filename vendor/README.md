# Vendored dependencies

Committed on purpose: a GitHub .zip download does not contain submodules, and
downloading them by hand turned into the usual support headache. Everything here
builds as soon as you open `BananaDrama.sln`.

| Folder | Version | Upstream |
| --- | --- | --- |
| `imgui/` | v1.92.9b | <https://github.com/ocornut/imgui> |
| `minhook/` | v1.3.4 | <https://github.com/TsudaKageyu/minhook> |

Only the files the project compiles are kept: ImGui core plus the `win32` and
`dx11` backends, and MinHook's `src/` + `include/`. Both licences are included.

To upgrade, drop the new release over the folder and check that the file lists in
`src/core/BananaDrama.vcxproj` still match (ImGui occasionally renames sources).
