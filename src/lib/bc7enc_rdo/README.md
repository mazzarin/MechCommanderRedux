# bc7enc_rdo (vendored)

BC7 encoding, decoding and rate-distortion optimisation for the HD art pack, by Richard Geldreich, Jr.:
https://github.com/richgel999/bc7enc_rdo, commit b943862 (2026-07-30). MIT or public domain (see LICENSE).

Only the files the pack needs: `bc7enc` (the BC7 block encoder, modes 1/5/6/7), `bc7decomp` (the decoder, every
mode), `ert` (the entropy-reduction post-process that makes the blocks pack smaller), and the headers they include
(`utils.h`, `dds_defs.h`). Unchanged from upstream. sprite/MCBc7 wraps them.
