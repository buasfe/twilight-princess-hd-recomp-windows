# Twilight Princess HD button prompts

Windows Edition includes the mapping-aware prompt implementation used in the Windows Vulkan candidate.

The Windows Vulkan candidate verifies live Xbox, PlayStation and Original Wii U styles in the
Quest Log and the 21:9 Ordon gameplay HUD. This validation used local frame captures and Vulkan
sample traces. The tests also cover
rebound inputs, hotplug policy, BFN reading, glyph generation and returning to the original
image addresses. The maintainer has also confirmed controller input and PlayStation prompts
through extensive use of a physical PS5 DualSense. The focused style checks used manual
selection in Pro Controller gameplay. A dedicated check of automatic style selection on
a physical DualSense and broader dialogue and HUD coverage are still open.

F1 > Controls > Button prompts selects the appearance independently of the bindings and the
emulated Wii U controller. Auto uses the detected family. Manual Xbox or PlayStation can
override a virtual controller's reported family.

TP HD-specific findings: named resources are returned through both JKRArchive and
JUTResReference. HD handles are registered directly by `02BE7984`; their image-key table is
`1028595C`, handle table `1028995C`, and GX2 descriptor pool pointer `1012CABC` (168-byte entries).
The prior candidate registered older letter panes and the inline `font_*.bti` icons. The active HD
HUD and many menus sample complete buttons named `tr_*_btn_*.bti`, with the letter baked into
each active and dimmed image. Registering these resources makes the existing sampled-image
lookup work with cached display lists. The renderer uploads immutable generated variants
through its usual texture path. The Wii U originals, native descriptors, and pane geometry
remain intact, so Original simply uses the original image addresses.

## Upstream comparison

Wind Waker HD main was pinned at `1af1d61b3b30ad6efd62c350fb3c85f9fa917f23` for this comparison.
Reviewed `docs/button-glyphs.md`, `docs/button-glyphs-development.md`, `hooks_glyphs.txt`,
`button_glyphs_test.cpp`, the runtime glyph/font/art helpers and SDL/input mapping code.

| Component | Reuse in Twilight Princess |
| --- | --- |
| Separate appearance from input mappings | Direct design reuse; prompt options must never rewrite existing bindings |
| SDL physical-position face labels | Direct API reuse for Xbox/PlayStation/Nintendo identification |
| Active controller / virtual-device policy | Adapt to this fork's shared bindings; meaningful input chooses the pad, virtual duplicates do not override physical identity |
| Xbox/PlayStation generated geometry | Reusable MPL-2.0 artwork; no third-party textures are needed |
| Native-font caption composition | Reuse the method, adapt the font reader: TP uses JUT/BFN rather than NW4F glyph records |
| NW4F cached text and CharWriter hooks | Game-specific replacement required: TP uses J2D pictures and COutFont message icons |
| HUD placement hooks | Keep TP's native pane geometry, transforms, fade and existing aspect correction; no WWHD addresses/layouts |
| Original mode and scoped draw changes | Direct design reuse: original must take the unmodified game draw path |
| Tests | Reuse coverage categories; fixtures need TP's J2D layouts and structures |

Hookshot and Longshot were examined for their Auto/manual prompt choices and the separation
between controller layout and prompt appearance. WWHD remains the implementation reference.

## Game-specific investigation

TP HD's USA Rev 2 J2DPicture/J2DPictureEx drawing was located through the RPX's own RTTI/vtables
and checked against generated PowerPC code and the public Twilight Princess decompilation.
HUD letters are picture panes such as `a_btn_l`, `b_btn_l`, `x_btn_l`, `y_btn_l`; message icons
are separately instantiated pictures using `COutFont_c`'s named `font_*.bti` resources.
The player-owned archives contain layout metadata/BFN data and separately packed HD GTX sheets.
These assets are inspected locally under ignored build directories and must never enter Git.

Reference sources:

- https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/main/docs/button-glyphs.md
- https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/main/docs/button-glyphs-development.md
- https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/main/tools/recomp/hooks_glyphs.txt
- https://github.com/zeldaret/tp/blob/main/src/d/d_msg_out_font.cpp
- https://git.magicbean.dev/MagicBeanDev/Longshot
- https://git.magicbean.dev/MagicBeanDev/Hookshot
