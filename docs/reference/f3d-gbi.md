# Fast3D GBI: opcode reference for the PowerVR interpreter

Scope: plain F3D ("Fast3D", the stock N64 microcode), i.e. gbi.h with `F3DEX_GBI`, `F3DEX_GBI_2` and `F3DLP_GBI` **undefined**. The F3DEX2 layouts are not used.

Sources:
- Encodings: `external/pw64-decomp/include/libultra/PR/gbi.h` (line numbers below are gbi.h lines).
- Game usage: `docs/notes/gfx.md` of the Birdman64 reference (cited as **gfx.md:N**).
- Interpreter behaviour: `crates/pw64-gfx/src/interp.rs` of the same reference (cited as **interp.rs:N**).
- Header with constants and decoders: `src/dc/gfx/f3d_gbi.h`.

Bit notation: `w0[a:b]` = bits a..b of the high word, inclusive, bit 0 = LSB. Opcode = w0[24:31]. "10.2" = raw value, 2 fraction bits.

"Emitted" is what gfx.md says about the game's display lists. "not in gfx.md" means gfx.md says nothing either way.

---

## 1. Opcode table

### 1a. RSP DMA commands (gbi.h:125-134)

| Name | Byte | w0 layout | w1 layout | Emitted | PowerVR interpreter action |
|---|---|---|---|---|---|
| G_SPNOOP | 0x00 | 00000000 | - | not in gfx.md | no-op |
| G_MTX | 0x01 | params[16:23] (0x01 PROJECTION, 0x02 LOAD, 0x04 PUSH), len[0:15] (64) | addr (Mtx, 4x4 s15.16) | yes, with POPMTX 2% (gfx.md:63) | MODELVIEW or PROJECTION: push if bit 2, then load (bit 1) or multiply |
| G_RESERVED0 | 0x02 | - | - | not in gfx.md | unimplemented (warn) |
| G_MOVEMEM | 0x03 | index[16:23], len[0:15] | addr | VIEWPORT only in the frame audit (gfx.md:63); LOOKATX/Y present (gfx.md:91) | see MOVEMEM table 2a |
| G_VTX | 0x04 | n-1 [20:23], v0 [16:19], len[0:15] = n*16 | addr of Vtx[n] (16 bytes each) | yes, 8% of cmds (gfx.md:63) | transform n (1-16) verts into cache slots v0..v0+n-1 (16-slot cache, gfx.md:18) |
| G_RESERVED1 | 0x05 | - | - | not in gfx.md | unimplemented (warn) |
| G_DL | 0x06 | push flag [16:23] (0 = call, 1 = branch), len unused | addr | yes, DL/ENDDL 9% (gfx.md:63) | call (push return, depth 10) or branch |
| G_RESERVED2 | 0x07 | - | - | not in gfx.md | unimplemented |
| G_RESERVED3 | 0x08 | - | - | not in gfx.md | unimplemented |
| G_SPRITE2D_BASE | 0x09 | - | - | not in gfx.md | unimplemented (sprite path, unused) |

### 1b. RSP immediate commands (gbi.h:138-164, G_IMMFIRST = -65, byte = 0xBF - n)

| Name | Byte | w0 layout | w1 layout | Emitted | PowerVR interpreter action |
|---|---|---|---|---|---|
| G_TRI1 | 0xBF | - | flag[24:31], v0*10 [16:23], v1*10 [8:15], v2*10 [0:7] | yes, 36% of cmds (gfx.md:63) | one triangle; index = byte / 10 (cache slots); flag = provoking vertex for flat shade (interp.rs:1096-1140); skipped if both cull bits set |
| G_CULLDL | 0xBE | vstart*40 in [0:15] (vstart & 0xF) | ((vend+1)&0xF)*40 | never (gfx.md:68) | no-op (CPU culls, gfx.md:91) |
| G_POPMTX | 0xBD | - | count (bytes implied by MTX, gImmp1 gbi.h:2803) | yes, with MTX 2% (gfx.md:63) | pop count modelview levels (empty = no-op) |
| G_MOVEWORD | 0xBC | offset [8:23], index [0:7] | data | SEGMENT, CLIP x4, FOG (gfx.md:63); NUMLIGHT/LIGHTCOL in encodings (gfx.md:27-29) | per index, table 2b |
| G_TEXTURE | 0xBB | level [11:13], tile [8:10], on [0:7] (bowtie [16:23] = 0) | s [16:31], t [0:15] (scale, 0.16) | UVTX lists (gfx.md:7-8, 11) | set S/T scale (0xFFFF = 1.0 in interp.rs:477), tile, LOD clamp = level |
| G_SETOTHERMODE_H | 0xBA | sft [8:15], len [0:7] | data already in place | yes, OTHERMODE H+L 12% (gfx.md:65) | `shadow = (shadow & ~mask) | (w1 & mask)`, mask = ((1<<len)-1)<<sft, interp.rs:488-499 |
| G_SETOTHERMODE_L | 0xB9 | sft [8:15], len [0:7] | data already in place | same | same, on OTHERMODE_L |
| G_ENDDL | 0xB8 | - | 0 | yes (gfx.md:63) | return from DL, or stop when stack empty |
| G_SETGEOMETRYMODE | 0xB7 | - | bits to OR in | not in gfx.md (set by uvGfxStateDraw, gfx.md:19-20) | `geometry |= w1` (bits in table 3) |
| G_CLEARGEOMETRYMODE | 0xB6 | - | bits to clear | same as above | `geometry &= ~w1` |
| G_LINE3D | 0xB5 | - | flag[24:31], v0*10 [16:23], v1*10 [8:15], wd [0:7] | never (gfx.md:68) | unimplemented |
| G_RDPHALF_1 | 0xB4 | - | s [16:31] (s10.5), t [0:15] (s10.5) | second word of each TEXRECT (gfx.md:38, 66); also a stray one per frame (gfx.md:66, see conflicts) | consumed by TEXRECT; standalone no-op |
| G_RDPHALF_2 | 0xB3 | - | dsdx [16:31] (s5.10), dtdy [0:15] (s5.10) | third word of each TEXRECT (gfx.md:38, 66) | consumed by TEXRECT; standalone no-op |
| G_RDPHALF_CONT | 0xB2 | - | - | not in gfx.md (gbi.h:157) | no-op (interp.rs:442) |

Aliases: G_SPRITE2D_SCALEFLIP = 0xBE and G_SPRITE2D_DRAW = 0xBD (gbi.h:163-164) collide with CULLDL and POPMTX. Do not dispatch them as sprites.

### 1c. RDP commands passed through the RSP (gbi.h:167-198)

| Name | Byte | w0 layout | w1 layout | Emitted | PowerVR interpreter action |
|---|---|---|---|---|---|
| G_NOOP | 0xC0 | - | native build uses w1 markers (WIDE, ANCHOR, VANCHOR, HUD tags, interp.rs:419-431) | not in gfx.md | no-op; markers set world/HUD state (native only) |
| G_TEXRECT | 0xE4 | xh (lrx, 10.2) [12:23], yh (lry) [0:11] | tile [24:26], xl (ulx) [12:23], yl (uly) [0:11] | yes, 3 words with RDPHALF_1/2 (gfx.md:38, 66) | 2D textured rect; S/T and deltas from the next two words; point-sampled when 1:1 aligned (interp.rs:1194) |
| G_TEXRECTFLIP | 0xE5 | as TEXRECT | as TEXRECT | never (gfx.md:68) | as TEXRECT with S/T axes swapped |
| G_RDPLOADSYNC | 0xE6 | - | - | yes (gfx.md:5, 66) | no-op (ordering) |
| G_RDPPIPESYNC | 0xE7 | - | - | yes (gfx.md:66) | no-op |
| G_RDPTILESYNC | 0xE8 | - | - | yes (gfx.md:66) | no-op |
| G_RDPFULLSYNC | 0xE9 | - | - | yes (gfx.md:66) | no-op; flush before any readback |
| G_SETKEYGB | 0xEA | - | - | chroma key, never (gfx.md:68) | ignore (interp.rs:444) |
| G_SETKEYR | 0xEB | - | - | never (gfx.md:68) | ignore |
| G_SETCONVERT | 0xEC | - | - | never (gfx.md:68) | ignore |
| G_SETSCISSOR | 0xED | ulx (10.2) [12:23], uly [0:11] | mode [24:25], lrx [12:23], lry [0:11] (10.2) | yes (gfx.md:64) | set scissor rect (raw / 4 px, interp.rs:536) |
| G_SETPRIMDEPTH | 0xEE | - | z [16:31], dz [0:15] | never (gfx.md:68) | store prim z (unused by interp) |
| G_RDPSETOTHERMODE | 0xEF | OTHERMODE_H value [0:23] | OTHERMODE_L value | never (gfx.md:68) | replace both shadow words raw |
| G_LOADTLUT | 0xF0 | - | tile [24:26], count [14:23] (raw, caller passes entries-1) | never (gfx.md:68) | TLUT load into TMEM (not emitted) |
| G_SETTILESIZE | 0xF2 | uls [12:23], ult [0:11] (10.2) | tile [24:26], lrs [12:23], lrt [0:11] (10.2) | yes (gfx.md:64) | set tile extents |
| G_LOADBLOCK | 0xF3 | uls [12:23], ult [0:11] (texels) | tile [24:26], lrs [12:23] (texels-1, <= 2047), dxt [0:11] (1.11, 0 = pre-swizzled) | yes (gfx.md:64, 9-10) | DMA texels uls..lrs from SETTIMG address into TMEM at tile |
| G_LOADTILE | 0xF4 | uls, ult (10.2) | tile, lrs, lrt (10.2) | never (gfx.md:68) | 2D TMEM load (not emitted) |
| G_SETTILE | 0xF5 | fmt [21:23], siz [19:20], line [9:17] (64-bit words), tmem [0:8] | tile [24:26], palette [20:23], cmt [18:19], maskt [14:17], shiftt [10:13], cms [8:9], masks [4:7], shifts [0:3] | yes (gfx.md:64) | set tile descriptor (struct f3d_settile) |
| G_FILLRECT | 0xF6 | lrx [14:23], lry [2:11] (raw 10-bit) | ulx [14:23], uly [2:11] (raw 10-bit) | yes (gfx.md:39, 66) | fill rect; fill and copy include the lower-right edge; if colour image == z image, depth clear (interp.rs:1158) |
| G_SETFILLCOLOR | 0xF7 | - | fill word: 16-bit colour = two RGBA5551 halves (interp uses high half); on z image = z value | yes (gfx.md:40, 64) | store fill colour |
| G_SETFOGCOLOR | 0xF8 | - | r [24:31], g [16:23], b [8:15], a [0:7] | yes (gfx.md:64) | store fog colour |
| G_SETBLENDCOLOR | 0xF9 | - | r, g, b, a as above | yes, title only (gfx.md:64) | store blend colour (alpha threshold) |
| G_SETPRIMCOLOR | 0xFA | minlevel [8:15], lodfrac [0:7] | r [24:31], g [16:23], b [8:15], a [0:7] | yes (gfx.md:5, 64) | store prim colour and LOD fraction |
| G_SETENVCOLOR | 0xFB | - | r, g, b, a as above | conflict: listed in UVTX lists (gfx.md:7) but "never emitted by the game's lists, viewer-only" (gfx.md:68) | store env colour |
| G_SETCOMBINE | 0xFC | 24 bits: cycle 0 a [20:23], c [15:19], Aa [12:14], Ac [9:11]; cycle 1 a [5:8], c [0:4] | cycle 0 b [28:31], d [15:17], Ab [12:14], Ad [9:11]; cycle 1 b [24:27], Aa [21:23], Ac [18:20], d [6:8], Ab [3:5], Ad [0:2] | yes, 2% (gfx.md:64); 13 muxes in 4 frames (gfx.md:75) | store mux; select shader from both cycles (table 4) |
| G_SETTIMG | 0xFD | fmt [21:23], siz [19:20], width-1 [0:11] | texture address (segmented) | yes (gfx.md:64) | set texture source (fmt, siz, width, addr) |
| G_SETZIMG | 0xFE | - | depth buffer address | yes (gfx.md:64) | set depth buffer |
| G_SETCIMG | 0xFF | fmt [21:23], siz [19:20], width-1 [0:11] | colour buffer address | yes (gfx.md:64) | set colour buffer |

### 1d. RSP-generated RDP triangles (gbi.h:209-216)

0xC8 TRI_FILL, 0xC9 TRI_FILL_ZBUFF, 0xCA TRI_TXTR, 0xCB TRI_TXTR_ZBUFF, 0xCC TRI_SHADE, 0xCD TRI_SHADE_ZBUFF, 0xCE TRI_SHADE_TXTR, 0xCF TRI_SHADE_TXTR_ZBUFF. Generated by the RSP, never in a game DL. Not dispatched by the interpreter.

---

## 2. MOVEMEM and MOVEWORD indices

### 2a. G_MOVEMEM index (w0[16:23])

| Index | Name | Meaning (gbi.h) | Interpreter action |
|---|---|---|---|
| 0x80 | G_MV_VIEWPORT (1214) | Vp: s16 vscale[4], vtrans[4], xy in 1/4 px (gfx.md:30) | set viewport transform (interp.rs:690+) |
| 0x82 | G_MV_LOOKATY (1215) | lookat Y | ignored (gfx.md:91) |
| 0x84 | G_MV_LOOKATX (1216) | lookat X | ignored (gfx.md:91) |
| 0x86 + 2n | G_MV_L0..L7 (1217-1224) | Light: col[0..2] bytes, dir s8 [8..10]; L(n) = (n-1)*2 + 0x86 (gbi.h:2488) | light n colour and direction |
| 0x96 | G_MV_TXTATT (1225) | texture attributes | not handled |
| 0x98 | G_MV_MATRIX_2 (1227) | | not handled |
| 0x9a | G_MV_MATRIX_3 (1228) | | not handled |
| 0x9c | G_MV_MATRIX_4 (1229) | | not handled |
| 0x9e | G_MV_MATRIX_1 (1226) | | not handled |

### 2b. G_MOVEWORD index (w0[0:7]) and offset (w0[8:23])

| Index | Name (gbi.h) | Offset | Data (w1) | Emitted | Interpreter action |
|---|---|---|---|---|---|
| 0x00 | G_MW_MATRIX (1240) | G_MWO_MATRIX_* (1311-1326) | value | not in gfx.md | not handled |
| 0x02 | G_MW_NUMLIGHT (1241) | 0 | (n+1)*32 + 0x80000000 (NUML, gbi.h:2445) | encoding in gfx.md:27 | light count n = ((w1 & 0x7FFFFFFF)/32) - 1 |
| 0x04 | G_MW_CLIP (1242) | RNX 4, RNY 0xC, RPX 0x14, RPY 0x1C (1257-1260) | FR_NEG_n = n (x and y), FR_POS_n = 0xFFFF - n | gfx.md:49 | clip ratio from RNX; triangles clipped to ratio x viewport (interp.rs:1073) |
| 0x06 | G_MW_SEGMENT (1243) | seg*4 (0..0x3C) | physical base | gfx.md:28 | segment base table, `seg = (addr>>24)&0xF` |
| 0x08 | G_MW_FOG (1244) | 0 | fm [16:31], fo [0:15] (s16) | gfx.md:28-29 | fog factor per vertex: alpha = z/w * fm + fo |
| 0x0A | G_MW_LIGHTCOL (1245) | aLIGHT_n = (n-1)*0x20 (1278, 1296-1309); bLIGHT_n = a+4 | colour | gfx.md:29 | light n colour (a entry only; b is a copy) |
| 0x0C | G_MW_POINTS (1249) | G_MWO_POINT_* (1327-1330) | | not in gfx.md | not handled |
| 0x0E | G_MW_PERSPNORM (1251) | 0 | s | conflict (see section 6) | no-op (interp.rs) |

Light offsets: aLIGHT_1 = 0x00 and bLIGHT_1 = 0x04 (gbi.h:1278-1279); aLIGHT_2..8 = 0x20..0xE0 in steps of 0x20, b = a+4 (gbi.h:1296-1309).

---

## 3. Geometry mode, OTHERMODE, render-mode and blender fields

### 3a. Geometry mode (gbi.h:340-364, Fast3D values)

| Bit | Name | Value |
|---|---|---|
| 0 | G_ZBUFFER | 0x00000001 |
| 1 | G_TEXTURE_ENABLE (microcode only) | 0x00000002 |
| 2 | G_SHADE | 0x00000004 |
| 9 | G_SHADING_SMOOTH | 0x00000200 |
| 12 | G_CULL_FRONT | 0x00001000 |
| 13 | G_CULL_BACK | 0x00002000 |
| 12-13 | G_CULL_BOTH | 0x00003000 |
| 16 | G_FOG | 0x00010000 |
| 17 | G_LIGHTING | 0x00020000 |
| 18 | G_TEXTURE_GEN | 0x00040000 |
| 19 | G_TEXTURE_GEN_LINEAR | 0x00080000 |
| 20 | G_LOD (not implemented) | 0x00100000 |
| - | G_CLIPPING | 0x00000000 (F3DEX only) |

Values match interp.rs `geom` (`crates/pw64-formats/src/gbi.rs`). The F3DEX2 values (0x200000 smooth, 0x400/0x200 cull) do not apply (gfx.md:20).

### 3b. G_SETOTHERMODE_H fields (gbi.h:570-581; shift = bit position, len = width)

| Field | sft | len | Values |
|---|---|---|---|
| ALPHADITHER | 4 | 2 | PATTERN 0, NOTPATTERN 1, NOISE 2, DISABLE 3 |
| RGBDITHER | 6 | 2 | MAGICSQ 0, BAYER 1, NOISE 2, DISABLE 3 (gbi.h:626-631) |
| COMBKEY | 8 | 1 | NONE 0, KEY 1 |
| TEXTCONV | 9 | 3 | CONV 0, FILTCONV 5, FILT 6 |
| TEXTFILT | 12 | 2 | POINT 0, AVERAGE 3, BILERP 2 (gfx.md:44, 71: BILERP in 32 of 463 UVTX) |
| TEXTLUT | 14 | 2 | NONE 0, RGBA16 2, IA16 3 (gfx.md:13: type at bits 14-15) |
| TEXTLOD | 16 | 1 | TILE 0, LOD 1 (gfx.md:72: all 0 in game) |
| TEXTDETAIL | 17 | 2 | CLAMP 0, SHARPEN 1, DETAIL 2 (gfx.md:72 names "TEXTSHARPEN", not in gbi.h) |
| TEXTPERSP | 19 | 1 | NONE 0, PERSP 1 |
| CYCLETYPE | 20 | 2 | 1CYCLE 0, 2CYCLE 1, COPY 2, FILL 3 (gfx.md:71: cycle types {1, 2, fill/copy}) |
| PIPELINE | 23 | 1 | 1PRIMITIVE 1, NPRIMITIVE 0 |

### 3c. G_SETOTHERMODE_L fields

| Field | sft | len | Values |
|---|---|---|---|
| ALPHACOMPARE | 0 | 2 | NONE 0, THRESHOLD 1, DITHER 3 |
| ZSRCSEL | 2 | 1 | PIXEL 0, PRIM 1 |
| RENDERMODE | 3 | 29 | flags below, bits 3..31 |
| BLENDER | 16 (G_MDSFT_BLENDER) | - | cycle selectors are at fixed bits, see 3e |

Render-mode flags (gbi.h:654-670), absolute bit values in the RENDERMODE field:

| Flag | Value | Flag | Value |
|---|---|---|---|
| AA_EN | 0x008 | ZMODE_OPA | 0x000 |
| Z_CMP | 0x010 | ZMODE_INTER | 0x400 |
| Z_UPD | 0x020 | ZMODE_XLU | 0x800 |
| IM_RD | 0x040 | ZMODE_DEC | 0xC00 |
| CLR_ON_CVG | 0x080 | CVG_X_ALPHA | 0x1000 |
| CVG_DST_CLAMP | 0x000 | ALPHA_CVG_SEL | 0x2000 |
| CVG_DST_WRAP | 0x100 | FORCE_BL | 0x4000 |
| CVG_DST_FULL | 0x200 | TEX_EDGE | 0x0000 |
| CVG_DST_SAVE | 0x300 | | |

Render-mode composites the game uses (gfx.md:42, 77-78). Values derived from gbi.h macros and checked against them in the host test:

| Name | Value | gbi.h |
|---|---|---|
| G_RM_FOG_SHADE_A (cycle 1, always used per gfx.md:42, 77) | 0xC8000000 | gbi.h:1004 |
| G_RM_PASS (cycle 1 alternative, gfx.md:42) | 0x0C080000 | gbi.h:1006 |
| G_RM_OPA_SURF(1) | 0x0C084000 | gbi.h:867 |
| G_RM_XLU_SURF(1) | 0x00404240 | gbi.h:871 |
| G_RM_AA_ZB_OPA_SURF(1) | 0x00442078 | gbi.h:689 |
| G_RM_AA_ZB_XLU_SURF(1) | 0x004049D8 | gbi.h:699 |
| G_RM_AA_ZB_TEX_TERR(1) | 0x00403078 | gbi.h:769 |

gfx.md:77-78 names only the families "OPA", "XLU" and "TEX_TERR" for the last cycle. Mapping them to the AA_ZB_* macros above is my reading, not stated in gfx.md. Cycle 2 is the `(clk)=2` form of each macro.

### 3d. Blender selectors (gbi.h:672-682 and 684-687)

Cycle 1 sits in bits 30..18: m1a [30:31], m1b [26:27], m2a [22:23], m2b [18:19]. Cycle 2 sits in bits 28..16: m1a [28:29], m1b [24:25], m2a [20:21], m2b [16:17]. Values:

| Selector | Values |
|---|---|
| G_BL_CLR_IN / G_BL_CLR_MEM / G_BL_CLR_BL / G_BL_CLR_FOG | 0 / 1 / 2 / 3 |
| G_BL_A_IN / G_BL_A_MEM / G_BL_A_FOG / G_BL_A_SHADE | 0 / 1 / 1 / 2 |
| G_BL_1MA (m2b only) / G_BL_1 / G_BL_0 | 0 / 2 / 3 |

G_RM_FOG_SHADE_A = GBL_c1(CLR_FOG, A_SHADE, CLR_IN, 1MA). G_RM_PASS = GBL_c1(CLR_IN, 0, CLR_IN, 1).

### 3e. Texture tile fields (gfx.md:9-11, 13)

- SETTILE `line` and `tmem` count 64-bit words (gfx.md:9). SETTILESIZE and LOADTILE coordinates are 10.2 (gfx.md:9).
- LOADBLOCK: lrs = texel count - 1, limited to G_TX_LDBLK_MAX_TXL = 2047 (gbi.h:3263); dxt = 1.11, G_TX_DXT_FRAC = 11 (gbi.h:3248).
- LOADTLUT count sits at w1 bit 14 (gfx.md:11; gbi.h:3361).

---

## 4. SETCOMBINE

Field layout (gbi.h:3045-3095; `GCCc0w0`, `GCCc1w0`, `GCCc0w1`, `GCCc1w1`). Each input is a field value from the CCMUX/ACMUX tables.

| Input | Word | Bits | Width | Table |
|---|---|---|---|---|
| a0 | w0 | [20:23] | 4 | CCMUX |
| c0 | w0 | [15:19] | 5 | CCMUX |
| Aa0 | w0 | [12:14] | 3 | ACMUX |
| Ac0 | w0 | [9:11] | 3 | ACMUX |
| a1 | w0 | [5:8] | 4 | CCMUX |
| c1 | w0 | [0:4] | 5 | CCMUX |
| b0 | w1 | [28:31] | 4 | CCMUX |
| d0 | w1 | [15:17] | 3 | CCMUX |
| Ab0 | w1 | [12:14] | 3 | ACMUX |
| Ad0 | w1 | [9:11] | 3 | ACMUX |
| b1 | w1 | [24:27] | 4 | CCMUX |
| Aa1 | w1 | [21:23] | 3 | ACMUX |
| Ac1 | w1 | [18:20] | 3 | ACMUX |
| d1 | w1 | [6:8] | 3 | CCMUX |
| Ab1 | w1 | [3:5] | 3 | ACMUX |
| Ad1 | w1 | [0:2] | 3 | ACMUX |

The helpers `f3d_cc_a0()` ... `f3d_cc_Ad1()` and `f3d_cc_decode()` in `f3d_gbi.h` return these. A 3-bit d or 4-bit b field truncates the constant: `G_CCMUX_0` (31) reads as 7 in d and 15 in b. The hardware "zero" input is therefore 7 (d) or 15 (b), and 7 for the alpha slots.

CCMUX values (gbi.h:444-464): COMBINED 0, TEXEL0 1, TEXEL1 2, PRIMITIVE 3, SHADE 4, ENVIRONMENT 5, CENTER/SCALE/1 6, NOISE/K4 7, COMBINED_ALPHA 7, TEXEL0_ALPHA 8, TEXEL1_ALPHA 9, PRIMITIVE_ALPHA 10, SHADE_ALPHA 11, ENV_ALPHA 12, LOD_FRACTION 13, PRIM_LOD_FRAC 14, K5 15, 0 = 31.

ACMUX values (gbi.h:467-476): COMBINED / LOD_FRACTION 0, TEXEL0 1, TEXEL1 2, PRIMITIVE 3, SHADE 4, ENVIRONMENT 5, PRIM_LOD_FRAC / 1 6, 0 = 7.

### 4a. The top mux (gfx.md:43-44, 77)

Raw words `0xFC26A004 1F1093FF` (gfx.md:44): w0 = 0x0026A004, w1 = 0x1F1093FF.

| Cycle | Output | Decoded |
|---|---|---|
| 0 RGB | (a, b, c, d) = (TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0) | (TEXEL1 - TEXEL0) * LOD_FRACTION + TEXEL0 |
| 0 alpha | (Aa, Ab, Ac, Ad) = (TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0) | same formula on alpha |
| 1 RGB | (a, b, c, d) = (COMBINED, 0, SHADE, 0) | COMBINED * SHADE |
| 1 alpha | (Aa, Ab, Ac, Ad) = (COMBINED, 0, SHADE, 0) | COMBINED_A * SHADE_A |

This matches gfx.md:44 "(T1-T0)*LOD_FRAC+T0, then *SHADE".

The other 12 muxes are not listed in gfx.md (it says only "13 distinct muxes", gfx.md:75). They must come from the game's data or `combiner.rs`, not from this document.

---

## 5. Field decoders in f3d_gbi.h

| Command | Helper |
|---|---|
| G_VTX | `f3d_vtx_decode(w0, w1, &n, &v0, &len, &addr)` |
| G_TRI1 | `f3d_tri1_decode(w1, &a, &b, &c, &flag)`, `f3d_tri1_encode(a, b, c, flag)` |
| G_LINE3D | `f3d_line3d_decode(w1, &v0, &v1, &wd, &flag)` |
| G_CULLDL | `f3d_culldl_decode(w0, w1, &vstart, &vend)` |
| G_MTX | `f3d_mtx_decode(w0, w1, &params, &len, &addr)` |
| G_POPMTX | `f3d_popmtx_count(w1)` |
| G_DL | `f3d_dl_decode(w0, w1, &push, &addr)` |
| G_MOVEMEM | `f3d_movemem_decode(w0, w1, &index, &len, &addr)` |
| G_MOVEWORD | `f3d_moveword_decode(w0, w1, &index, &offset, &data)`, `f3d_numlights_from_data`, `f3d_clip_ratio_from_data`, `f3d_fog_from_data`, `f3d_lightcol_index` |
| Segments | `f3d_seg_index`, `f3d_seg_offset`, `f3d_seg_resolve(segs[16], addr)` |
| G_TEXTURE | `f3d_texture_decode(w0, w1, &level, &tile, &on, &s, &t)` |
| G_SETOTHERMODE_H/L | `f3d_othermode_decode`, `f3d_othermode_mask`, `f3d_othermode_apply`, `f3d_omh_*()`, `f3d_oml_*()`, `f3d_rm_blend_decode(rm, cycle, ...)` |
| G_SETCOMBINE | `f3d_cc_a0()` ... `f3d_cc_Ad1()`, `f3d_cc_decode(w0, w1, out[2][8])` |
| G_SETTILE | `f3d_settile_decode(w0, w1, &struct f3d_settile)` |
| G_SETTILESIZE / G_LOADTILE | `f3d_settilesize_decode`, `f3d_loadtile_decode` (same layout) |
| G_LOADBLOCK | `f3d_loadblock_decode(w0, w1, &tile, &uls, &ult, &lrs, &dxt)` |
| G_LOADTLUT | `f3d_loadtlut_decode(w1, &tile, &count)` |
| G_SETTIMG / SETCIMG / SETZIMG | `f3d_setimg_decode(w0, w1, &fmt, &siz, &width, &addr)` |
| G_TEXRECT | `f3d_texrect_decode(w0, w1, &xl, &yl, &xh, &yh, &tile)` |
| RDPHALF_1 / RDPHALF_2 | `f3d_texrect_half1_decode(w1, &s, &t)`, `f3d_texrect_half2_decode(w1, &dsdx, &dtdy)` |
| G_FILLRECT | `f3d_fillrect_decode(w0, w1, &lrx, &lry, &ulx, &uly)` |
| G_SETSCISSOR | `f3d_scissor_decode(w0, w1, &mode, &ulx, &uly, &lrx, &lry)` |
| G_SETPRIMCOLOR | `f3d_primcolor_decode(w0, w1, &minlevel, &lodfrac, &r, &g, &b, &a)` |
| ENV / FOG / BLEND colour | `f3d_rgba_decode(w1, &r, &g, &b, &a)` |
| G_SETFILLCOLOR | `f3d_fillcolor_rgba5551(w1)` |
| G_SETPRIMDEPTH | `f3d_primdepth_decode(w1, &z, &dz)` |

Verified by `.agent-tmp/f3d_test.c` (deleted after the run): 908 checks, 0 failures. The test builds the commands with the real gbi.h macros, decodes them with these helpers, and checks every F3D_ constant against its gbi.h macro.

---

## 6. Conflicts found

These are where gbi.h, gfx.md and interp.rs disagree, or where a gbi.h macro is itself wrong. Nothing in the header depends on resolving them; the header follows gbi.h.

1. **gfx.md:66 says the stray 0xB4 is gSPPerspNormalize. gbi.h disagrees.** gSPPerspNormalize = gMoveWd(G_MW_PERSPNORM=0x0E) (gbi.h:2794), a MOVEWORD (0xBC), not 0xB4. 0xB4 is G_RDPHALF_1 (gbi.h:149). Check chan.c before trusting either. interp.rs treats 0xB4 as a no-op either way.
2. **gfx.md:65-68 vs gfx.md:7 on G_SETENVCOLOR (0xFB).** It is in the UVTX list (gfx.md:7) but "never emitted by the game's lists" (gfx.md:68). The two are reconcilable only if the UVTX lists are not "the game's lists".
3. **FILLRECT coordinate scale (interp.rs:1158-1170 vs gbi.h:4320-4326).** gbi.h stores lrx/lry/ulx/uly as raw 10-bit values at bits 14 and 2, which RDP reads as 10.2 (raw/4 px). interp.rs reads the 12-bit field at bit 12 and divides by 4, which gives raw as pixels, a factor of 4. For a full-screen fill (0..1023) this makes no visible difference, but for a partial rect it would. Check against a real non-full-screen FILLRECT from the game before relying on it.
4. **gSPTextureRectangle pkt form (gbi.h:4485-4494) is broken.** The RDPHALF words are written with `gImmp1(pkt, ...)` into the same slot, so only one of three words survives. The gs form (gbi.h:4479-4484) is correct and matches interp.rs (E4, B4, B3 in order). gDPTextureRectangle (gbi.h:4442) writes a 2-slot 128-bit layout with no opcode bytes, which interp.rs does not read.
5. **gSPLightColor (gbi.h:2498-2505) does not compile.** It pastes `G_MWO_a##n` / `G_MWO_b##n`, but gbi.h defines `G_MWO_aLIGHT_n` / `G_MWO_bLIGHT_n`. The pkt form also writes both words to the same pkt. Use gMoveWd with the aLIGHT_n names directly (as the host test does).
6. **gSPClipRatio pkt form (gbi.h:2215-2221) writes all four words to the same pkt.** The gs form is correct.
7. **gSPSegment, gSPFogPosition, gSPTexture, gSPNumLights, gSPPerspNormalize:** the pkt forms are correct (one slot each).
8. **gfx.md:72 names G_MDSFT_TEXTSHARPEN.** gbi.h has only G_MDSFT_TEXTDETAIL with G_TD_SHARPEN = 1 << 17 (gbi.h:597-600).
9. **gfx.md:38 "copy mode dsdx x4".** Read with gfx.md:106 and interp.rs (`dsdx /= 4` in copy mode), the game writes dsdx = sx*4 and the RDP divides by 4 in copy mode. The wording is ambiguous, not contradictory.
10. **TRI1 flag semantics.** gbi.h stores the flag in w1[24:31] and says nothing about its meaning. interp.rs uses it as the provoking-vertex index for flat shading (interp.rs:1113). Consistent with the header comment, but this is an interpreter assumption.
11. **gfx.md:9 "SETTILESIZE/LOADTILE coords are 10.2"** and **gfx.md:9 "LOADBLOCK ... lrs = texel count - 1"**: both agree with gbi.h field widths. No conflict.
12. **gbi.h G_TX_LDBLK_MAX_TXL** is 2047 without `_HW_VERSION_1` (gbi.h:3263). gfx.md:10 does not give a limit. No conflict.

No conflicts were found between gfx.md and interp.rs on opcode values, the G_VTX/G_TRI1/G_MOVEWORD/G_MOVEMEM/G_TEXTURE/G_SETOTHERMODE field layouts, or the combiner field positions (interp.rs `combiner.rs:cycles()` agrees with gbi.h's GCCc* macros bit for bit).
