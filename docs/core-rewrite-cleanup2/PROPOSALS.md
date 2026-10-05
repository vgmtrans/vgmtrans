# core-rewrite-cleanup2: proposal and investigation status

Scope: ideas raised after branch starting commit `cb9e0b766`, through code commit
`528db5857`. The [audit archive](AUDIT.md) preserves every cleanup2 batch note,
including proposed next steps and rejected or superseded drafts. The
[branch guide](README.md) describes the final implementation.

## Accepted ideas

The accepted proposals became the changes described in the guide: direct Session
registration, one Collection model, direct dependency requests and diagnostics,
stable typed drafts, ordinary builder results, fewer command/runtime authoring
choices, canonical VM identity/timing, simpler neutral pitch/sample values,
direct exporter buffers, and removal of redundant native state.

Many historical “Next” paragraphs subsequently landed in later batches. Examples
include typed IDs, void song state, optional sample-reference removal, the concrete
Q8 helper, ScanInput metadata borrowing, and direct MIDI pitch routing. Those
paragraphs are a record of the investigation sequence, not an outstanding backlog.

## Rejected or retained approaches

| Branch investigation | Outcome and reason |
| --- | --- |
| Unconditional serial format scanning | Rejected in batch 29. The measured sum of per-input median times rose by 56.6%, with substantial NDS/AKAO regressions. Bounded concurrent scanning remains. |
| Collapse immutable shared chunks or global source indexes into simpler flat/per-chunk searches | Retained the owners/indexes needed for snapshot lifetime and large-session lookup behavior. |
| Borrow or collapse prepared-bank ownership and membership unconditionally | Retained actual copy, placement, failure, and lifetime distinctions; some measured shortcuts regressed repeated-placement work. |
| Reassociate DLS/SF2 floating-point conversion to save expressions | Rejected formulations that changed rounding/output. Accepted changes retain explicit destination bounds and the checked numerical contract. |
| Remove `SourceMapBuilder::field` | Rejected and reverted in batch 63 after real native callers were identified. The failed build and incorrect absence judgment are retained. |
| Replace multiple CompilerCursor Event values with one generic choice/capture representation | Rejected where the replacement increased variants or hid ownership and flow choices rather than removing them. |
| Replace the tempo map's owning points result with a borrowed view | Retained the value result because actual temporary-map callers retain it. |
| Merge independent note, voice, envelope, LFO, and automation lifecycles | Retained the distinct driver behaviors; a small wrapper deletion cannot safely replace them with one flag. |
| Borrow the VM's mutable options during rendering | Rejected because callbacks can replace the VM; the render-local immutable options copy remains. |

These are decisions within this branch's investigation, including rechecks of
existing boundaries. They do not imply that every possible future redesign has
been exhausted. The detailed scope and counterexamples remain in the archive.

## Remaining findings

- **Zero/subnormal physical LFO frequency policy.** Batch 29 records a path from
  MP2k's permitted zero rate into `log2`/rounding in shared synth conversion.
  Choosing a floor or a native disable policy changes meaning and affects shared
  consumers. This branch records the issue without implementing that policy.
- **Legacy Org recognition gap.** The original value registration already lacked
  the legacy Org scanner/signature. Cleanup2 identified and reproduced recognition
  and header-parsing differences. It did not add Org or prove end-to-end legacy
  Org conversion.
- **Scope of real-input coverage.** The sampled archives cover 37 of 38 registered
  value families. More files and legacy/value comparisons could extend confidence;
  successful sampled checks do not prove all-format or all-input equivalence.
- **Concurrent numeric asset IDs.** The final Sony inventory finding is admitted
  narrowly, with all raw evidence retained. This branch does not implement a new
  deterministic cross-session ID-assignment policy.

The earlier SF2 root/residual numeric follow-up was addressed by later pitch
boundary work, including batch 42. It should not be presented as an entirely
unimplemented proposal merely because it appears in the batch 41 stop notes.

## Proposals that predate this branch

The substantial redesign proposals in
[VALUE_CORE_REVIEW.md](../VALUE_CORE_REVIEW.md), including prepared instrument
handles, explicit voice continuity, and the combined rendering/resolution design,
already exist in `cb9e0b766`. So does the
[resolved-instrument prototype](../value-core-review/resolved-instrument-prototype.html).
All existing documents remain in place. Cleanup2's small later edits to their
alternatives wording remain preserved there and are explained by the current
branch guide and asset-resolution documentation. The substantive redesigns
should not be attributed to this branch merely because those documents changed.

Use Git ancestry, rather than the document title or filesystem timestamp, to
distinguish those earlier proposals from cleanup2. Untracked local artifacts have
no branch provenance and are outside this tracked-document separation.
