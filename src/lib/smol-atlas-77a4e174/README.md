# smol-atlas 77a4e174

Aras Pranckevičius's dynamic texture atlas allocator (MIT or Unlicense, `license.md`): rectangles packed into shelves
by height (Shelf Best Height Fit), each one freed again on its own. The GPU renderer packs HD art frames and sharp
glyphs into the pages they share with it (`platform/MCVulkanRenderer`'s `SharedPage`), so draws from any of a page's
pictures share their bindings. From `https://github.com/aras-p/smol-atlas`, commit
77a4e17414e52f5363a822a5563c1c8d5533fbf5 (4 Sep 2026; its code last changed 5 Aug 2024): `src/smol-atlas.h`
(2,599 bytes), `src/smol-atlas.cpp` (10,599 bytes) and `license.md`, each checked against the commit's tree.

Unchanged. Built outside MCCore's project, without its precompiled header, as on Windows.
