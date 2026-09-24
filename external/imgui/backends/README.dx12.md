# Win32 / DirectX 12 backends for Tracy demos

`imgui_impl_win32.{h,cpp}` and `imgui_impl_dx12.{h,cpp}` are unmodified files
from Dear ImGui **v1.92.8-docking**, commit
`b61e56346a92cfcaf1f43a545ca37b0b32239654`.

Source: https://github.com/ocornut/imgui/tree/b61e56346a92cfcaf1f43a545ca37b0b32239654/backends

License: `../LICENSE.txt` (MIT). The existing ImGui core and OpenGL backends
are unchanged. The D3D12 backends are linked only into the Tracy demos.
