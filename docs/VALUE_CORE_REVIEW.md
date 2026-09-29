# A simpler architecture for the value core

Reviewed against b531b5abe86e02479b066899ae1f7c845f9d7e67 on core-rewrite.

**Implementation status, 29 September 2026:** The analysis below describes that baseline. Instrument resolution is implemented on `codex/resolved-instruments-prototype` (initial checkpoint: `0c24c8f42`). The collection lifecycle now returns completed preparation results (`aecf170b5`), and synth inputs group selected instruments with their banks, removing membership reconstruction. The resolved performance now also owns its output addresses and used-only policy, and consumers obtain an instrument definition/address pair through its selection interface. Explicit sounding voices now own each continuation group's instrument and hardware deadline. Source emission now assigns voice identity directly; preparation remaps it into the completed result. Pitch bindings no longer declare ownership, and output consumers no longer reconstruct voice ownership or deadlines. MIDI lowering now produces temporary events for one track at a time while reading the prepared result; it no longer clones or mutates that result. The latest review unifies tie and changed-key emission, while distinguishing the stronger export improvements from the more modest gains in format authoring. Collection inputs now separate original choices from direct bank/sample uses, and SegSat configures bank mapping during preparation. The current assessment is in section 6; [the HTML implementation notes](value-core-review/resolved-instrument-prototype.html) describe the current code and verification.

**Baseline finding: the largest opportunity is to make instrument choice and voice continuity explicit before output conversion.** At that baseline, the core executes the source program, but several later passes still have to work out which instrument a note means and whether it continues an earlier voice. Collections have a related problem: preparation reconstructs relationships that resolution has already established.

I recommend redesigning these representations, rather than making callback cleanup the main project:

| Priority | Change | What becomes simpler |
| --- | --- | --- |
| 1 | Resolve instrument selections once, then assign output addresses once | MIDI, synth export, instrument variants, and stitching stop independently recovering instrument identity. |
| 2 | Represent a sounding voice explicitly across ties and pitch changes | Consumers stop reconstructing voice continuity from note flags, note IDs, and automation links. |
| 3 | Store collection choices separately from a directly usable arrangement of banks and samples | Preparation stops joining and cross-checking flat membership and dependency lists. |

The first two changes fit together: a performance should tell us **which voice is sounding, with which instrument, over which interval**. Output code should decide how to express that in MIDI/SF2/DLS. The third gives that performance a clearer set of inputs.

The original proposals and evidence follow. The first two and the collection-input structure are now implemented in stages; [section 6](#6-a-practical-implementation-sequence) assesses the current result and its remaining weaknesses. Separating prepared bank contents from discovered asset identity remains unfinished.

My earlier review was too conservative about the larger representations and too generous about the importance of small cleanups. Removing SegSat's assignment phase remains useful, but it is not the main architectural opportunity.

## 1. Resolve instruments before output conversion

### The problem: a note does not yet tell us what plays it

Consider this song:

1. Select source instrument 5.
2. Change its attack envelope.
3. Play a note.
4. Select instrument 7 while the first voice continues.
5. Continue that voice at a different pitch.
6. Start a fresh note.

The first voice must keep the instrument and envelope it started with. The fresh note uses the later selection. MIDI may need an extra preset for the envelope change and extra note messages for the pitch transition. None of those output details should change the identity of the original voice.

Today, that answer is assembled repeatedly:

| Consumer | Work it performs |
| --- | --- |
| Instrument variants | Tracks selections and continuation relationships; resolves the base instrument; allocates a bank/program for a variant; writes note-level address overrides. |
| Used-instrument filtering | Tracks selections and continuations again to decide which instruments to retain. |
| Pitch-transition conversion | Resolves the attack's preset and copies it across linked notes and generated fragments. |
| MIDI rendering | Tracks the source selection, resolves instruments, and manages the preset currently selected on the MIDI channel. |
| Stitching | Finds the already-assigned bank numbers and rewrites both bank contents and MIDI bank-select events. |

The common lookup helper does not remove these separate decisions. See [variant materialization](/Users/mike/vgmtrans/src/value/export/InstrumentVariants.cpp:256), [synth filtering](/Users/mike/vgmtrans/src/value/export/synth/SynthExportData.cpp:263), [pitch conversion](/Users/mike/vgmtrans/src/value/export/midi/PitchTransitionMidiLowering.cpp:118), [MIDI selection](/Users/mike/vgmtrans/src/value/export/midi/PerformanceMidiRenderer.cpp:193), and [stitching](/Users/mike/vgmtrans/src/value/export/CollectionStitch.cpp:161).

The underlying model contributes to this. A selection can name a source identity or a logical bank/program address. An instrument can carry both a source identity and an explicit output address. A note can override the track's selection with another address. These distinctions are individually understandable, but together they make output addresses serve as both identity and destination.

### The proposed model

Give each prepared instrument a stable handle within the prepared song. Resolve a source selection to that handle at one boundary. Subsequent operations use the handle, including variant generation.

A handle is simply an index or small ID into owned instrument data. It is not a new global registry, and it is not a MIDI bank/program number. Two songs may prepare different versions of the same scanned instrument without sharing mutable state.

The example would become:

    source selection 5 → prepared instrument A
    envelope change   → prepared variant B
    first attack      → B
    continuation      → B
    next fresh attack → prepared instrument C, selected by source instrument 7

Only when the requested output set is known do we assign addresses:

    instrument B → bank 0, program 12
    instrument C → bank 0, program 13

MIDI and the companion synth writer consume the same address table. Variants create new instrument handles; they do not allocate MIDI addresses. Source addresses and native identities remain available as provenance and preferred-address hints.

This does not require moving all interpretation into the format handlers. A finalization operation can consume the executed events and prepared banks, resolve selections, and move the existing data into the completed result. It must run after format finalization and respect tick/execution order: emission order alone is insufficient because handlers can emit events at other times.

The result should replace the current export-facing performance/bank arrangement. Do not preserve every existing intermediate and add a fourth performance representation beside them. During migration an adapter is reasonable; the final design must retire the old consumer-side resolution paths.

### A deliberate change to the lookup contract

There is an important complication: the existing lookup policies differ **intentionally**, and a test preserves those differences.

- Ordinary performance lookup requires an exact source identity and returns the first match.
- Variants can fall back from a missing identity to its numeric address, then use the first match.
- Used-instrument filtering performs that fallback but retains every match.

This is explicit in [the selection-policy test](/Users/mike/vgmtrans/tests/core/ValueInstrumentVariantTests.cpp:75). It is not evidence of an accidental missing helper.

My recommendation is to replace these consumer-dependent policies with a single selection decision for song rendering:

- Prefer an exact native identity.
- If none exists, allow the established numeric-address fallback at the resolution boundary, with a diagnostic.
- If several definitions compete for the same selection, choose a documented deterministic winner and report the conflict. Selected-bank order is a reasonable default to prototype.
- Represent intentional layered instruments through their regions/layers, rather than relying on several conflicting preset definitions being retained.

That would change ambiguous/fallback output behavior. It needs corpus investigation, especially before treating duplicate definitions as accidental; merging layered definitions is not generally equivalent when their modulation differs. Do not silently change all-match filtering to first-match and call it a mechanical refactor.

If corpus evidence shows that multiple definitions are a required part of one playable selection, resolve that selection to an explicit group with defined semantics. The writers should still consume one resolved answer. Avoid preserving today's differing policies inside a new three-way lookup record: that would package the complexity rather than remove it.

Standalone MIDI remains supported without bank data. Its result needs an explicit external-preset reference with the source's numeric address. It must not pretend to have a resolved instrument. Synth-dependent adaptation then has a clear condition: there must be an owned instrument to adapt and a companion output that carries the adaptation.

### Assign addresses after adaptations, across the whole output

A single address planner would handle preferred source addresses, generated variants, collisions, and destination limits. This relocates necessary allocation logic; it does not abolish address allocation.

For an ordinary paired export, plan once for that song. For stitching, prepare the parts first, plan addresses over their combined instrument sets, and then render each part's MIDI using that plan. The existing MIDI timeline composition can remain. This removes the need to scan completed MIDI to discover banks and then patch both the MIDI and the banks.

Unresolved external-preset references also have to participate in planning/reservation. A reference in a stitched output must identify both its prepared part and its instrument within that part: the same scanned bank can be prepared differently for two songs. Repeated instances of the same collection can reuse one prepared instrument set, preserving the existing opportunity to share its bank allocation. The stitch result can still expose the source-to-output bank mapping for the UI.

Observed modulation scaling also remains a decision over the complete stitched output. The current implementation correctly combines parts' observed ranges before applying a common scale; a new preparation helper must not scale each part independently.

### What this can remove

The substantial deletion target is **independent recovery of instrument selection**, not the synth algorithms:

- Selection replay in used-instrument filtering; used instruments become a set of referenced handles.
- Base-instrument searches inside variant generation.
- Numeric address allocation inside variant generation.
- Note-level export-address overrides used to carry generated presets through later passes.
- Selection replay and address inheritance inside pitch-transition conversion.
- Stitching's scan of MIDI bank-select messages and its paired mutation of MIDI and instrument addresses.

MIDI still needs to remember which bank/program it last emitted. That is output encoding state, and it remains useful. Instrument pitch sensitivity can also change with controller/selection events; resolve those events to concrete references too, rather than assuming every pitch property is fixed at voice onset.

Region-response sampling, envelope approximation, sample decoding, modulation conversion, and format-specific interpretation remain. Their mathematical complexity does not disappear merely because instrument identity gets simpler.

**Why pursue this first:** it changes a contract used by several substantial subsystems and offers identifiable deletions in each. It also makes a basic question easy to answer: “Which instrument plays this note?” Exact net line savings remain unmeasured.

## 2. Make voice continuity explicit

### The problem: one sounding voice has several representations

A tie or a slur is not necessarily a new attack. Yet the current representation makes continuity depend partly on whether the pitch changes.

[PerformanceEmitter::continueVoice](/Users/mike/vgmtrans/src/value/sequence/PerformanceEmitter.cpp:190) uses the previous note ID plus an extension flag for a same-pitch continuation. A changed-pitch continuation gets a new note ID and an automation link to the previous note. Consumers then recover continuity by combining those mechanisms.

This is visible in format code too. [AKAO's FF7 note handling](/Users/mike/vgmtrans/src/value/formats/Akao/AkaoFF7Voice.h:67) and [Itikiti SNES](/Users/mike/vgmtrans/src/value/formats/ItikitiSnes/ItikitiSnesSequence.cpp:270) branch on same-pitch versus changed-pitch continuation and arrange note IDs, flags, and slide links accordingly.

Later, [performanceNotePredecessors](/Users/mike/vgmtrans/src/value/sequence/PerformanceModel.cpp:50) reconstructs links by joining notes to automations. Variant generation, filtering, pitch conversion, and the MIDI renderer use that information. The MIDI renderer then builds its own note-to-voice mapping, follows predecessor chains, and handles a “last voice” fallback. See [voiceForNote](/Users/mike/vgmtrans/src/value/export/midi/PerformanceMidiRenderer.cpp:850).

The musical distinction is simpler than the transport: start a voice, extend its gate, change its pitch, or stop it. The format still decides which action the driver performs. The shared model should record that decision directly.

### The proposed model

Represent a performed voice once, with its initial instrument, gate/lifetime information, and pitch segments. Source commands that continue it refer to that voice. Whether the new pitch happens to equal the old pitch should not determine the representation of continuity.

For the example in proposal 1:

    Voice 1
      instrument: B
      pitch: 60, then a transition to 64
      gate: extended by the continuation
      provenance: both source note commands

    Voice 2
      instrument: C
      pitch: 67
      gate: a new attack

A MIDI renderer can express Voice 1 as a held note plus pitch bend, or as several physical notes using portamento. Those generated notes belong to the MIDI conversion implementation. They should not force every other consumer to reinterpret the source performance.

Format-facing operations could express starting, extending, and changing the pitch of a retained voice directly. The eventual emitter should construct those records, rather than manufacture links for downstream code to rediscover. A temporary converter from the existing event model is useful for comparison, but keeping both mechanisms permanently would undermine the simplification.

Not every musical event becomes a voice record. Tempo and channel controls remain ordered timelines. Automation that intentionally spans several new attacks remains channel/lane automation; it cannot all be attached to one voice.

### Preserve the real independent behaviors

A voice is not the same thing as a channel or a source note annotation. One channel can play overlapping voices. Several source commands can describe one voice. Preserve those differences without making output passes infer them.

Also, an envelope restart, a vibrato restart, and a tremolo restart are different actions. FF7 can reset modulation while suppressing key-on. The proposed model must retain those actions explicitly, not replace all the current flags with one “legato” Boolean.

Likewise:

- A hardware duration cap can constrain a continuing voice across a tempo change.
- A program change can affect future attacks while an older voice keeps its instrument.
- An envelope update can target active voices, future attacks, or both.
- A driver lane can be reused after an earlier voice enters its release tail.
- Source highlighting must retain the spans of every executed command, even when several commands contribute to one voice.
- Invalid or leading continuations need a defined fallback/diagnostic; an existing test treats a leading tie as using the selected instrument.

The code already models these requirements in pieces. The redesign collects the identity and lifetime information; it does not remove the independent musical controls.

### What this can remove

Once all downstream consumers use explicit voices, delete the reconstruction protocol:

- Repeated computation of predecessor maps from automation links.
- Same-pitch versus changed-pitch branching whose sole purpose is to encode continuity.
- The source-note-to-voice chain walk and “previous voice” inference in MIDI rendering.
- Consumer-specific decisions about whether a continuation chooses another instrument.
- The need to recover original hardware end limits after a target-specific rewrite has split or moved notes.

Source command IDs and segment provenance still have a purpose. The proposal is to make the voice record replace the currently reconstructed grouping. The [architecture audit](/Users/mike/vgmtrans/VALUE_ARCHITECTURE_AUDIT.md:795) notes that an unused pitch-transition identity was previously removed. Adding that field back without changing the consumers would achieve nothing.

This also offers a path to eliminating some copies of the entire performance. Currently, source music and target-specific fragments share PerformanceSequence, while CollectionWorkspace retains original and adapted versions. A voice-based performance can remain intact while MIDI conversion builds its own fragments.

For playback highlighting, the current Qt consumer reads only [sourceSpans](/Users/mike/vgmtrans/src/ui/qt/SequencePlayer.cpp:232) from the retained performance. Preserve that trace independently; retaining a second full event tree should require another actual consumer. A source-performance analysis API may still return the full model when requested.

**Expected payoff:** broader than a naming or ownership cleanup, with changes in both format handlers and exporters. **Main uncertainty:** a good representation must handle the difficult continuity cases without becoming a more elaborate generic music model. Prototype FF7/Itikiti-style continuations and hardware duration limits, not just ordinary notes.

## 3. Let collection structure express the actual input relationships

### The problem: selection and resolution share an ambiguous representation

A collection currently has a flat member list and a separate dependency list. Preparation looks assets up through the member list, searches dependencies by owner and role, checks that the lists agree, and then supplies matched inputs to callbacks.

The core does not support arbitrary dependency graphs here. It follows a fixed relationship:

    sequence uses banks
    each bank uses sample pools
    miscellaneous assets support inspection

That structure can be represented directly.

My earlier objection to changing this was incomplete. Manual choices and resolved uses are different information, but that is a reason to distinguish them clearly—not a reason to keep the current pair of overlapping lists.

For example, the user can select pools A and B while the bank actually uses only A. We must retain the user's choice of B. Conversely, a bank can use pool A twice at different offsets. A deduplicated member list cannot express those two uses.

### The proposed model

Retain two clearly different things:

1. **Collection selection:** the sequence, the user's ordered choices when manual, and supplemental inspection assets.
2. **Resolved inputs:** the selected banks, with each bank's ordered sample uses stored directly alongside it, plus selection status and alternatives.

A schematic result might look like:

    selection
      sequence S
      user banks [X]
      user pools [A, B]

    resolved inputs
      sequence S
      bank X
        sample uses [A at offset p, A at offset q]
        sample-selection status and alternatives
      bank-selection status and alternatives

The selection is user intent. The resolved input structure is the answer. Repetition of A here represents two real uses, not two independent records of the same relationship.

For an automatically discovered collection, the definition can be just the root sequence and its collection options; there is no need to persist a second flattened audio-member list. The UI's flat membership view can be computed from the selection and resolved inputs. For manual collections that view must retain unused explicit choices, including their order.

The resolver already captures the original manual member list while it accumulates results. This change makes that existing distinction durable and explicit. See [Resolver](/Users/mike/vgmtrans/src/value/scan/AssetResolution.cpp:12).

Do not introduce a generic graph library or a hierarchy of resolution-stage classes. The result needs a bank entry with sample uses and the existing status/alternative information. Selectors and their format-specific matching algorithms can remain.

### Preparation becomes a direct walk

Preparation would visit each selected bank with the sample uses stored under that bank. It would prepare bank contents, then let the sequence configure its use of those prepared banks.

Several things no longer need reconstructing:

- Which dependency record belongs to this bank.
- Which role a target list represents.
- Whether a dependency owner also occurs in a separate member list.
- Whether every chosen provider was copied into a second flat list.

These checks currently appear in [inputsFor and prepareCollection](/Users/mike/vgmtrans/src/value/export/CollectionBinding.cpp:26). Validate asset existence/type and manual candidate restrictions when constructing the resolved result. Immutable snapshot storage can then preserve those invariants. Malformed source data and invalid sample references still need validation; removing a redundant representation is not permission to remove those checks.

Keep resolved IDs tied to the snapshot/revision they describe. A list of unchecked IDs passed to another snapshot would reintroduce the same problems. References within retained snapshot storage are one possible implementation; a revision-bound result containing validated IDs is another. Do not add a second ownership system just to avoid a few lookups.

Preserve bank-only preparation, incomplete and ambiguous selections with usable fallbacks, automatic extracted-source scoping, and mixed-format banks. Each bank must still use its own format's sample preparation. Also preserve current manual-collection lifecycle behavior unless changed deliberately: user collections are not automatically rematched on every load.

### Separate prepared contents from discovered asset identity

Currently callbacks mutate a copy of the entire SoundBankAsset, including metadata, recipe, private data, and another preparation callback. Afterwards the core checks that identity, format, and order were not changed.

A prepared bank should contain its musical contents and needed prepared format data, while its discovered asset identity stays in an immutable reference. Preparation can then change instruments and bank-specific runtime information without having the ability to replace the bank's identity or registration callbacks.

This is more useful than adding stricter setters around a mutable copy. It makes a class of post-hoc validation unnecessary. Preserve prepared private data: AKAO bank preparation updates articulation information that sequence preparation subsequently reads, so simply discarding all format data would be incorrect.

### SegSat's assignment phase becomes local work

This is where the earlier small proposal fits.

For SegSat, suppose the song requests logical bank 2, the selected bank stores number 9, and export wants a lone bank at output bank 0. Today three callbacks communicate through SegSatBankUse: assignment stores the numbers, bank preparation applies them, and sequence preparation reads them again.

In the redesigned preparation operation, calculate the mapping once, use it to configure velocity/runtime data and the song's instrument-selection mapping, and supply preferred output addresses to the final address planner. Exact matches must still be reserved before fallback assignments.

That removes BankAssigner, BankAssignmentContext, scanner assignment registration, resolver dispatch, the stored SegSatBankUse handoff, and sequence-to-bank placement plumbing. SegSat is the only production user of that assignment callback. [The three callbacks](/Users/mike/vgmtrans/src/value/formats/SegSat/SegSatModule.cpp:102) make the handoff visible.

SonyPS1's sample-pool placement is different and remains necessary. Assignment warnings would move from collection resolution to preparation unless explicitly retained as a preview; that diagnostic timing change must be documented.

**Expected payoff:** a more intuitive input model and fewer joins/consistency rules. The resolver is only about 200 lines today, so this is not a claim that collection matching dominates the codebase. The benefit spans its public representation, preparation contracts, and format callbacks.

## 4. How the pieces fit without adding another framework

The intended flow is:

    discovered assets
        ↓ choose companions
    resolved sequence / banks / sample uses
        ↓ prepare banks and execute the sequence
    performance with explicit voices and instrument references
        ↓ adapt for the requested output and assign addresses once
    MIDI + companion synth output

The raw source map remains beside this flow for inspection and provenance. Standalone bank export enters at bank preparation. Standalone MIDI can carry unresolved external presets. Neither needs a fictitious fully playable song.

The public export implementation should expose a completed preparation result, rather than a CollectionWorkspace whose callers must advance it by calling render and prepareModulation at most once in the correct order. A straightforward function with local variables is sufficient. The redesign above gives that result a stronger meaning; merely wrapping the current workspace would not deliver the same improvement.

Playback, collection export, and stitch-part preparation currently coordinate related operations separately. Their actual algorithms are already substantially shared. The additional gain is removing the repeated procedure and its ordering rules, not claiming three entire exporters can be deleted.

Stitching keeps its actual composition responsibilities: combining modulation ranges, assigning addresses over all parts, retiming MIDI, and resetting channel state between songs. Writers keep their destination-specific serialization and approximation choices.

The key rule for implementation is **replace contracts as consumers migrate**. Do not finish with collection members plus dependencies plus a new input tree, or old note flags plus predecessor inference plus an unused voice table. A new representation earns its place by making the old representation or an entire interpretation step unnecessary.

## 5. Other possibilities, ranked honestly

| Idea | Judgment |
| --- | --- |
| Own name/reverb in PreparedSynthData instead of borrowing Instrument | Good small cleanup. Two copied fields remove an exceptional lifetime dependency; they do not constitute a redesign. |
| Bundle typed commands and playback state behind one executable-program boundary | Worth an experiment only after the higher-payoff work. It can improve construction safety, but deletion is less certain. |
| Make every SessionSnapshot retain all source bytes | Defer. It enables independent/asynchronous use of old snapshots, but current synchronous Session export wrappers already supply both inputs. |
| Rename misleading terms | Useful after boundaries settle. TrackState meaning channel state and event sequence meaning emission order deserve clearer names. |
| Replace the current command system with a universal opcode/operand IR | Do not pursue on this evidence. It risks adding a second language and execution schema for every format. |
| Return to a combined legacy parser/UI/MIDI object | Do not pursue. It would restore mode-dependent behavior across parsing and conversion. |

The executable-program idea merits a precise explanation. Currently SourceCommand contains a callable taking void*, while SequenceRuntime supplies erased state factories and lifecycle hooks. Collection preparation can replace the runtime and checks compatibility through its execute-function pointer. See [SequenceRuntime](/Users/mike/vgmtrans/src/value/sequence/SequenceProgram.h:41), [the typed adapter](/Users/mike/vgmtrans/src/value/sequence/CompiledCommandRuntime.h:52), and [runtime replacement](/Users/mike/vgmtrans/src/value/export/CollectionBinding.cpp:192).

A format-owned typed program/player could keep commands, configuration, and state together, with erasure only at the common scheduler boundary. That could remove independent pairing and some runtime compatibility machinery. But the scheduler still needs stream/channel state, section callbacks, waiting, ticking, prepasses, and finalization. The current adapter is already about 213 lines and hides the erasure from format handlers. Replacing it with an equally large interface and factory system is not a demonstrated simplification.

I would judge that experiment on NDS, interleaved SonyPS2, and NinSnes prepass/analysis together. A design that looks elegant only for a simple track is insufficient. This is a substantial alternative I considered, but I would not rank it above the repeated musical interpretation in proposals 1 and 2.

The useful parts of the rewrite should survive: immutable discovered assets, shared scheduling, source inspection independent of playback, and separation between source programs, performed music, and destination encoding. Legacy [VGMSeq](/Users/mike/vgmtrans/src/main/components/seq/VGMSeq.cpp:76) and [SeqTrack](/Users/mike/vgmtrans/src/main/components/seq/SeqTrack.cpp:453) demonstrate how combining those jobs spreads read-mode conditions through otherwise musical operations.

## 6. A practical implementation sequence

Current staged work:

1. **Completed: collection preparation lifecycle.** Construct one completed result from bound inputs. Keep immutable banks available with or without a rendered sequence, retain the snapshot owning external sample metadata, and share the completed result across repeated stitched parts. Remove the public render-once workspace protocol.
2. **Completed: instrument handoffs.** Synth input owns selected instrument/address entries grouped by bank. Conversion reads them directly; selected-bank export removes whole groups. The pointer/address map, pointer-only projection, and bank-membership searches are removed. Bank-wide sampling, empty selections, and local/external sample behavior are covered by regressions.
3. **Completed: the resolved-data contract.** Preparation assigns output addresses and owns them with the performance. MIDI and synth take one completed value; neither accepts a separate plan or independent used-only setting. Output selection returns a definition/address pair. The event model stays shared, with its prepared invariants enforced by construction and const access. Address-allocation failures join the performance diagnostics. Copies retain the associated addresses; MIDI conversion reads them from its prepared input.
4. **Implemented: explicit sounding-voice identity and ownership.** Source note operations assign voice IDs through ties and key changes; preparation remaps those track-local IDs into the completed result. Each voice owns its adapted instrument and absolute hardware deadline; gate timing remains on segments. Output consumers no longer build predecessor maps or infer voice ownership; MIDI no longer recovers original deadlines or shortens previously generated fragments. Existing source restart controls and pitch anchors remain. MIDI now produces temporary events for one track at a time without cloning the prepared result. The temporary events retain prepared voice IDs and are discarded after channel rendering. They still use the shared event structures. One continuation operation now accepts both ties and changed keys; the emitter owns note/voice identity. A separate destination event taxonomy is not implemented.
5. **Implemented: collection choices and resolved bank/sample uses.** The collection retains original choices and a direct bank-to-sample input structure; flat membership is derived. Preparation no longer joins owner/role records or checks duplicated membership. SegSat configures private bank contents and runtime in one callback, replacing the generic assignment phase. Full prepared-bank/asset-identity separation remains a distinct refinement.
6. **Implemented: direct MIDI pitch-range planning.** Measure the intervals between physical attacks and associate each range change with its existing event boundary. Remove the separate range-change timeline, playback cursor, and copied source-range snapshots. The transition-to-bend/portamento algorithm and shared event types remain separate work.

Complete and validate each stage before implementing the next. The completed stages remove a stateful protocol, instrument-membership reconstruction, caller-managed pairing of performances and address plans, and repeated voice/deadline reconstruction; the remaining stages still need to demonstrate their own reductions in logic and conceptual overhead.

### Review of the implemented changes

**The code is easier to reason about at the preparation and output boundaries. Format authoring has improved more modestly.** These are different claims, and the latter should not be inferred just from the former.

| Area | What a reader or caller no longer has to work out | Assessment |
| --- | --- | --- |
| Completed preparation | Whether rendering has already happened, whether the address plan matches the performance, and who keeps its banks alive | Strong improvement: fewer independently managed objects and valid call sequences. |
| Synth input and stitching | Which selected instrument belongs to which bank; how to patch bank numbers after MIDI is rendered | Strong improvement: the required relationships are present in the input. |
| Sounding voices | Whether a tie or a pitch slide implies shared instrument ownership; whether editing a slide changes that ownership | Strong improvement: the source declares one voice identity; pitch edits affect motion only. |
| MIDI track conversion | Why conversion copies an entire prepared performance and which copy owns its references | Useful, narrower improvement: temporary track events borrow one prepared owner. The pitch-rendering algorithm remains complicated. |
| Format note emission | Which emission function to call for a tie versus a changed key, and how to transfer the predecessor's IDs | Improved by the revision below. Source note anchors and tie rules are still exposed. |

A concrete ownership example explains the benefit better than the line count. Instrument 5 attacks; the program changes to 7; the original voice changes pitch. Preparation resolves the first voice once, so it keeps instrument 5. Replacing or canceling its glide cannot make it acquire instrument 7. MIDI and synth output read that decision. A new attack selects instrument 7. Previously, multiple consumers had to recover this distinction from program changes and continuation links.

The last source-interface change removed `PitchSlideBinding::continueFrom`, `PitchTransitionIntent::previousNote`, and preparation's `noteConnections` pass. That was a sound architectural change, but it left avoidable ceremony in formats. Many still had three emission branches: fresh attack, same-key tie through `note()`, and changed-key continuation through `continueVoice()`. The helper also ignored an explicit tie flag and looked up the predecessor again when it called `note()` internally. Describing this as a finished simplification of format authoring would have overstated it.

The review revision gives both kinds of continuation the same path:

~~~cpp
NotePerformanceEvent event{
    .key = key,
    .durationTicks = duration,
    .extendsPrevious = continues && repeatsSourcePitch,
    .restartsEnvelope = !continues,
};
lastNote = continues ? out.continueVoice(lastNote, event) : out.note(event);
~~~

Here `continues` and `repeatsSourcePitch` are decisions made by the driver. An explicit tie preserves the predecessor's note ID and any unfinished pitch motion. Without that flag, continuing at the realized pitch also extends the note; changing pitch creates another note ID on the same voice. A slide, when needed, is emitted separately. Each format retains its own envelope and LFO reset rules.

This matters when a repeated key is the **target** of an unfinished glide. Comparing it only with the currently sounding pitch would interrupt a tie that the driver intended to preserve. That distinction stays explicit; a shared helper should not guess away real driver behavior. Capcom's repeated-target case and tri-Ace's named overlapping note now use the same continuation operation as other drivers.

The emitter resolves the predecessor once and assigns identity in one place. Ordinary attacks always receive fresh IDs, including when their event parameters were copied from an earlier note. An unknown predecessor starts a fresh voice instead of accepting stale IDs. These are small, enforceable contracts that remove bookkeeping from callers.

The remaining weaknesses are concrete. `NotePerformanceEvent` still serves source emission, prepared data, and temporary MIDI fragments, so its fields are not all relevant at every stage. `note()` retains a tie shorthand alongside the explicit continuation API. A format author still needs to distinguish note anchors, sounding voices, and lanes when implementing more involved drivers. MIDI still has substantial pitch-bend and portamento logic; moving its necessary work behind a better boundary does not make the algorithm intrinsically simpler. A future event-model change should earn its cost by removing those obligations, rather than adding another parallel set of structures.

This revision removes **73 physical production C++ lines and 77 nonblank, noncomment lines** relative to `d8017054a`. Against local `core-rewrite`, the accumulated change is **−201 physical C++ lines and −170 nonblank, noncomment lines** (−202 physical lines including production CMake). Those counts include additions and deletions across the core and formats. They corroborate the deletion of repeated branches; they do not measure elegance or prove preserved musical behavior. Verification and its limits are recorded in the [HTML implementation notes](value-core-review/resolved-instrument-prototype.html#verification).

### Collection inputs: remove the duplicated relationships

This was the next priority because it offered a specific deletion across the model, resolver, preparation, and format interface. Splitting source/MIDI event types first would have required a less certain choice about how much vocabulary and conversion machinery to add.

Previously, a collection stored both flat membership and a list of dependencies tagged with owner and role. Preparation searched those dependencies for each bank, then checked that every owner and provider also appeared in the flat lists. The code was reconciling two representations of the same answer.

Now `selection` records original choices, while `inputs.banks` records each resolved bank with its own sample uses. `members()` derives the flat view when needed. A manual choice of pools A and B survives even if the bank uses only A; two uses of A at different offsets remain distinct under that bank. Automatic collections store no second audio-member list. Provider types and manual restrictions are checked during resolution; snapshot lookups and final sample validation remain.

SegSat also no longer hands a mapping through assignment, bank preparation, and sequence preparation. Its sequence preparer reserves exact matches, assigns remaining logical numbers, and applies them to instrument identities and runtime velocity data together. This removes `BankAssigner`, `BankAssignmentContext`, scanner registration, resolver dispatch, and `SegSatBankUse`. The tradeoff is deliberate: bank-count warnings now appear during preparation rather than discovery.

This stage removes **127 physical production C++ lines and 105 nonblank, noncomment lines** relative to `af22ee68a`, including the UI consumers. Against `core-rewrite`, production C++ is **328 physical lines and 275 nonblank, noncomment lines smaller**. The important reduction is the disappearance of a relationship-joining protocol and an entire callback phase. Test changes include API migration plus regressions for manual unused choices, repeated placements, exact-match reservation, and private preparation isolation.

Preparation still edits complete bank copies and checks their discovered identity afterward. That part of proposal 3 is not implemented. Shared source/MIDI event fields and the complexity of pitch conversion also remain separate design problems. The [HTML notes](value-core-review/resolved-instrument-prototype.html#inputs) describe the current input model and verification.

### MIDI pitch ranges: remove a second timeline to synchronize

MIDI sensitivity determines how far a wheel value bends the note. A note that will rise six semitones needs sufficient range from its attack, including when the rise follows a tie. This lookahead is necessary; maintaining a second playback schedule for its answer was not.

Previously, the planner collected attacks, walked pitch state while advancing an attack cursor, converted the accumulated measurements into timestamped range changes, and handed those changes to another cursor in the renderer. Each change also copied the source's current range. A reader had to establish that the two cursors agreed with the event stream and that restoring the copied source range was redundant.

Now the planner examines one attack interval at a time and associates a changed range directly with the first event at that attack's position. The renderer encounters that event and applies the decision. Source sensitivity continues to come from source events. Ties stay within an interval; simultaneous attacks share one. The exact-order rule preserves controls that appear before a note with the same tick and sequence number.

The replacement cost is a local event-to-range map, used only while rendering the unchanged track timeline. No public type, format requirement, or additional event vocabulary was introduced. This is a focused simplification of output code; it does not make format authoring appreciably different.

This stage removes **40 physical production lines and 38 nonblank, noncomment lines** relative to `b3b4977b0`. Against `core-rewrite`, production C++ is **368 physical lines and 313 nonblank, noncomment lines smaller** (369 fewer physical lines including CMake). All 43 headless tests and three MIDI suites under ASan/UBSan pass. Twelve saved fixtures and 3,072 deterministically generated MIDI exports match the prior implementation byte for byte, including diagnostic logs. These comparisons do not establish real-file or audible fidelity.

The harder pitch problem is still present: transition lowering repeatedly reconstructs current bend state, and delayed slides can establish a starting pitch back at note onset. A forward-only rewrite would need an explicit account of that lookahead, source-wheel replacement, held bends, and release-tail resets. Simply replacing the replay with a cursor would risk changing pitch behavior. Shared source/MIDI event fields also remain unchanged. The [HTML notes](value-core-review/resolved-instrument-prototype.html#pitch-ranges) explain the implemented range behavior without requiring readers to follow the old synchronization mechanism.

The original implementation rationale follows:

Start with a vertical slice of **instrument resolution and address assignment**. Use existing performances and banks as inputs initially; a new voice model is not a prerequisite. Make variants, used-instrument filtering, pitch conversion, and MIDI consume the same resolved selections. Include one paired export and one stitched export so that late address assignment actually replaces the remapping path.

Then prototype **explicit voices** through the emitter and those consumers. Exercise same-pitch ties, changed-pitch slurs, instrument changes during a continuation, independent envelope/LFO restarts, and real-time duration limits. Preserve source attribution and compare both portamento and pitch-bend output modes.

The **collection representation** can follow independently. Preserve manual unused candidates, duplicate sample placements, ambiguous fallbacks, standalone banks, shared banks prepared differently for two songs, and mixed-format selection. Move SegSat's mapping into the new preparation contract as part of that work.

Each prototype should include a deletion ledger:

| Proposal | Existing machinery to remove | Replacement cost to count |
| --- | --- | --- |
| Instrument resolution | Repeated selection walks/lookups, variant address allocator, stitch address patching | One resolver, stable references, one destination address planner |
| Explicit voices | Predecessor reconstruction, voice inference, repeated continuation handling | Direct voice construction and target-local MIDI fragment handling |
| Collection inputs | Flat resolved membership duplication, owner/role joins, consistency checks, assignment handoff | Selection definition, nested bank/sample uses, derived UI membership |

Count production code across both core and migrated formats, not just the shortened caller. Record which compatibility adapters remain temporarily. Do not claim a line-count win until the old paths are removed.

Output comparisons should distinguish preserved musical behavior from deliberate contract changes. Instrument collision/fallback policy, diagnostic timing, and possibly bank-number layout need explicit expectations. Existing binary fixtures are useful, but a changed bank address with correctly updated MIDI is not itself a musical regression. Conversely, passing a small synthetic suite is not proof that native driver behavior is preserved across the corpus.

This is a recommendation for substantive redesign with staged evidence, not for an all-at-once rewrite or an open-ended abstraction exercise.

## 7. Verified bug to fix independently

When a sequence belongs to exactly one collection, exportSequenceMidi takes the collection path. It requests MIDI with event-simulated modulation but leaves dynamic envelopes at their default, InstrumentVariants. It can create a private preset and redirect MIDI to it without writing the companion bank. See [exportSequenceMidi](/Users/mike/vgmtrans/src/value/export/Export.cpp:172).

The [review probe](/Users/mike/vgmtrans/docs/value-core-review/probe.cpp) used one bank containing program 5 and one note after an attack-envelope change:

| Operation | Observed result |
| --- | --- |
| Direct sequence MIDI export | Selects program 5, then generated program 0; no diagnostics. |
| Separate full-bank SF2 export | Contains program 5 only; no diagnostics. |
| Paired collection MIDI + SF2 export | The generated program 0 exists in the accompanying SF2. |
| Collection MIDI export with DynamicEnvelopePolicy::Ignore | Keeps program 5. |

The fixture uses silent PCM; this verifies preset references, not a listening result.

Fix this without waiting for the redesign: do not generate private envelope presets when no companion bank is supplied; retain the original selection and report the unsupported adaptation. The broader proposal makes that dependency easier to express, but the bug does not justify delaying a small correction.

The probe's other observations are not additional demonstrated application bugs. Rendering a workspace twice violates its documented single-use contract. Exporting from an old snapshot after removing its encoded sources exercises the current lifetime contract.

## Documentation and evidence

The original HTML introduces too many types before walking through a complete operation. Optional mechanisms receive similar prominence to ordinary work; ownership details interrupt the main explanation; terms such as “binding” and “lowering” appear before the concrete actions they mean. Those are teaching problems. The repeated interpretation and relationship reconstruction described above are architectural problems. Clearer prose helps with the first; it cannot remove the second.

The [rewritten HTML guide](/Users/mike/Documents/VGMTrans_Value_Core_Reviewed.html) remains documentation of the existing code. It does not incorporate these proposed designs. Its original formatting is preserved, and the original supplied HTML is unchanged. The original source archive was at 70c307c; 18 of its 81 embedded files differed from the reviewed checkout, and the rewritten guide refreshes its embedded sources.

The review covered the original guide, shared session/resolution/preparation paths, command construction and execution, MIDI/synth export, representative formats, and legacy sequence/collection code. This reassessment additionally traced instrument selection and continuation across all their output consumers, current policy tests, FF7/Itikiti/Falcom/PandoraBox voice handling, and the Qt playback trace consumer. It is not an audit of every format opcode.

For the original review, the baseline headless build succeeded and all **43 configured CTest tests passed**. Real-file parity inputs were empty; no full-corpus comparison or listening test was performed. Current implementation verification is recorded separately in the [HTML notes](value-core-review/resolved-instrument-prototype.html#verification); neither set of synthetic results establishes full-corpus or audible fidelity.

Baseline commands:

~~~sh
cmake --build build/core-migration-headless -j 6
ctest --test-dir build/core-migration-headless --output-on-failure -j 6
~~~

To reproduce the synthetic export observations on this macOS checkout:

The probe below targets the `core-rewrite` baseline and intentionally uses its earlier preparation API. Run it from that revision. The current branch's equivalent behavior and ownership checks are in [ValueResolvedInstrumentTests.cpp](../tests/core/ValueResolvedInstrumentTests.cpp).

~~~sh
mkdir -p /tmp/vgmtrans-value-review
c++ -std=c++20 -O0 -mmacosx-version-min=14.1 -I src -I . \
  docs/value-core-review/probe.cpp \
  build/core-migration-headless/src/main/libvgmtransvaluecore.a \
  build/core-migration-headless/src/main/lib/fmt/libfmt.a \
  -o /tmp/vgmtrans-value-review/probe
/tmp/vgmtrans-value-review/probe
python3 docs/value-core-review/inspect_outputs.py
~~~

The [binary inspector](/Users/mike/vgmtrans/docs/value-core-review/inspect_outputs.py) reads MIDI program changes and SF2 preset headers independently of the production model.
