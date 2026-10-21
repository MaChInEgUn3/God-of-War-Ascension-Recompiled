# VRAM evict/defrag: static RE (read from lifted code and EBOOT.elf words; nothing run)

Glossary:
- `side(d)` is a 16-byte entry at `[0x96E174] + ((d-[0x96E178])>>7)*16`. Fields: +2 flags, +8 frame stamp, +0xC priority.
- `desc+2` is a **refcount**. `desc+4&0xFF` is the variant. `desc+6` is the slot in table 0x96B51C. `desc+0x24` links to the object's next descriptor (the chain starts at obj+0x40).
- `obj+0x1C` is the evictable top-mip size (summed at 0x36E2E0-0x36E2F0). `obj+0x20` is the full size. A fresh bind allocates only `obj+0x20-obj+0x1C`.

## 1. Path to 0x3734BC (func_00372B70)
- **Loop.** It walks a group list, then an object list; it is not the descriptor table.
  - Outer: `r22=[heap+0x1C]`, next is `[r22]`.
  - Inner: head `r20=[(r22-0xC)+0x44]` (0x373018), object `r21=r20-8`, next `[r20]` (0x373488).
- **Object filter.** `obj+0x12&1` must be set (0x3730A8), then `desc=[obj+0x44]`.
- **Frame stamp.** If `desc+0x28` is non-zero, `side+8` is set to `[0x975888]+2`, which is the frame counter (line 519624).
- **LRU pick.** Only when `[sp+0xB8]` (pressure) is set. A candidate needs:
  - side flags that are not "0x8000 without 0x2000";
  - `side+8 != 0` and `side+8 < [0x975888]-[0x81C6F4]`;
  - side bit 0x800 clear.
  The object with the smallest stamp is kept at loc_00373258: `vm_write32(r1+0xAC, r21)`.
- **The call.** 0x3734AC: `r4=[sp+0xAC]`, r3=heap, r5=0x96E174.
- **Other release sites.**
  - 0x3731F8 passes `r4=r21` directly. It fires when the flags are 0xA000 with 0x4000 clear, or when `[0x96DDCC]==1`.
  - func_00371004 (0x3711D0; called from 0x3734F8, 0x3712F0 and 0x376500) walks the table top-down. It requires `desc+0x20!=0` and side bits 0x800 and 0x1000 clear. It then reverse-looks-up the **first** object with `+0x44==desc`.
- **Refcount.** None of the three sites reads `desc+2`. Only func_00372080 does (0x372144 `lhz r4,2(r4); cmpwi 1`, release at 0x372168).

## 2. Writers of obj+0x44 and descriptor lifecycle
- **Binds**
  - 0x36DF38, func_0036DD2C: binds the new descriptor. It also sets +0x40 (0x36DF30), and `desc+0x24` = old +0x40.
  - 0x36E6B4, func_0036E488: binds.
- **Zeroing:** 0x36DFA0, 0x36E4F4 and 0x36EDC0 (init and clone). Destroy, func_0036E6D4, zeroes at 0x36E7E4 after decref (func_0036F70C) of the **+0x40 chain only**.
- **Rebind, func_0036E804 (0x36E820).** It sets `obj+0x44=new` and moves the user list. It **copies side(old) over side(new)** (lvx/stvx at 0x36E898). It takes no ref and does not push onto +0x40.
  - Variant select (func_0036E8A4/func_0036E9C4, from 0x381124) calls it at 0x36E940, 0x36EA4C and 0x36EB70.
  - Job completion (func_003721C8) calls it at 0x37227C and 0x3723C0.
- **Ignored:** 0x36F510 and 0x2A2D18/0x2A300C/0x2A33E4/0x2A344C store into other struct types.

**Descriptor changes**
- **func_003707E8, allocation.**
  - The hash lookup at 0x370880-0x370914 matches `desc+0x10/+0x14` plus side bit 0x8000. It then does `refcount++` and returns the existing record. **There is no size compare.**
  - Otherwise it takes a new record (0x3984D0), bump-allocates `[0x96DDC0]`, writes `+0xC=size` (0x370AC0) and the hash (0x370AB4/0x370AB8).
- **func_00370D8C(dst,src).** dst takes src's slot, offset, **size** (0x370DD8), +4 and +0x1C. It does not copy the hash.
- **func_0036F70C.** Decrements the refcount. At 0 it NULLs the slot (0x36F778), sets +6=-1 and frees the record (0x39864C).
- **func_00370E48.** Also NULL-inserts a slot.
- **Staging descriptor `[0x96DDEC]`.** func_00371BB8 allocates it with hash 0 and size `obj+0x20`. Job completion goes one of two ways:
  - func_003723F0: sets side **0x2000** (0x372474, the only setter) and calls func_00370D8C(obj+0x44, staging).
  - func_003721C8: calls func_00370D8C(J+0xC, staging) and then rebinds, or allocates a new descriptor (hash zeroed at 0x3722C8/0x3722D0) and rebinds.
- **Jobs.** Queued jobs live in a 0x80-slot table at 0x96DF48 and are purged per object by func_00373AAC. The current job `[0x96E588]` is aborted by func_0036F7C0.

## 3. GPU, SPU and frame dependence
- **RSX label gate.** At entry the compactor reads `seq=[0x96E158]`. If it is non-zero it compares it with label 0x4E at `[0x9BDF94]+0x4E*[0x9BDF98]` (0x372BE8-0x372C1C), and returns if they differ. Otherwise it does `seq++` and emits NV4097 0x1D6C/0x1D74 (offset 0x4E<<4, value seq) through func_00434844/func_004347C4. That happens at 0x372C58/0x372E0C, or in func_00373690, which flushes the copy list at 0x96E714.
- **What the gate covers.** It covers:
  - the release sites;
  - LRU and stream-in selection;
  - job dispatch;
  - the sequencer, which is called only at 0x372CB4.
  It does **not** cover binds, rebinds (func_0036E9C4), destroy/decref or queueing.
- **Frame counter.** The stamp comes from `[0x975888]`. There is also a 6-frame countdown `[0x96E160]` (loc_00372ED4).
- **Sequencer helpers.** func_002A85E0 and func_002A88A4 are async file-open checks: a mutex on `[0x81ACC8]`, `req+0x98` status read with isync, the `h+0x108` error bit, and a vtable "is open" call. I found no SPU or RSX dependence.

## 4. Hypotheses and a trace that separates them
Your observation that `D+0x10/14 == O+0x34/38` matters here. Variant and staging descriptors carry hash 0, and func_00370D8C does not copy the hash. So D is a descriptor that func_003707E8 created for a hashed object.
- **(d) Rebind inherits flags (best fit).**
  1. D1 is O's first descriptor, resident-only, with `size = O+0x20-O+0x1C`.
  2. A variant job puts D2 in place, and D2 later gets side bit 0x2000.
  3. A rebind back to D1 copies side(D2), including 0x2000, onto the small D1.
  4. The LRU at 0x3734BC uses flags only and evicts it, so `+0xC` goes negative. func_00371004 would not, because D1's `+0x20` is 0.
- **(a) Hash dedup.** D belongs to another object with the same hash and a different size.
- **(b) Shared descriptor.** N sharers lead to N subtractions, because there is no refcount check.
- **(c) Stale record.** A record freed and reissued, which fits only if it was reissued for an identical hash.

```
PPU_TRACE_ADDRS=370E48,36E804,370D8C,3707E8,3984D0,39864C,371BB8,36DD2C
PPU_TRACE_DUMP=370E48,4,50    # O: +1C,+20,+34/38,+40,+44
PPU_TRACE_DUMP2=36E804,4,20   # new desc: +2 ref,+4 variant,+6 slot,+8 off,+C size
```
Optional second run: `DUMP=371BB8,4,1C` (job: obj, variant, flags, J+0xC) and `DUMP2=370D8C,4,20` (the src size that dst takes). 0x3984D0/0x39864C are generic pools: keep only `r3==[0x96E168]` (descriptor pool; `[0x96E16C]` is jobs), with r4 = the record at 0x39864C.

How to read the faulting release (object O, descriptor D):
- **(d):** a func_0036E804 entry with r3=O and r4=D comes first; the dumped `D+0xC == O+0x20-O+0x1C`; and there is no 0x39864C with r4=D in between.
- **(a):** D was returned by a func_003707E8 entry that was *not* followed by 0x3984D0, and that entry's r4 is not O's size (r5 = hash).
- **(b):** func_00370E48 runs for two different objects with the same +0x44, with no func_00370D8C(r3=D) in between.
- **(c):** 0x39864C with r4=D happens after O's last bind/rebind and before the release.

Caveats: bit meanings are inferred. Lifted names differ from the bl targets: 0x3721C8/0x3723F0 appear as func_003721C0/func_003723EC in `_001.cpp`, which also has a second compactor copy, func_00372B64. func_00372B70 is called from 0x366EAC and 0x3E8894.
