---
issue: 112
---

# Issue #112 — Bound coverage-tile message size: chunk large dirty windows into fixed-size patches at the publisher

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-26 00:15 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))
**Verdict**: changes-requested

**Branch**: feature/issue-112 at `8cadd48`
**Mode**: pre-push
**Depth**: Deep (reason: user-forced; 13 commits, boat-facing behaviour change incl. a default flip, lifecycle state, new wire behaviour)
**Must-fix**: 7 | **Suggestions**: 10
**Round**: 1 | **Ship**: continue — one finding invalidates the design premise, not the polish.

Specialists: Governance, Claude Adversarial Lens A + Lens B. Static analysis via colcon (cpplint + uncrustify in-suite): clean. Tests independently confirmed EXECUTED, not merely compiled: 597 tests, 0 failures, 0 skipped in the three affected suites.

### Findings
- [ ] (must-fix) THE DESIGN PREMISE IS WRONG FOR THE SECOND CONSUMER. marine_web_view/coverage_renderer.py advances possession ONLY on a whole tile (deliberately), and reconciler.py re-requests any tile whose catalog version exceeds what it holds. The producer bumps the catalog on a patch, so EVERY patched tile is re-requested whole every 5 s, served immediately and unthrottled by tileRequestCallback — ~110 kB/s of request-driven whole tiles ON TOP of the patch stream, worse than the 56-86 kB/s whole-tile stream this branch replaces. The renderer's own comment says its rule "costs nothing today" precisely BECAUSE the producer serves whole tiles. Fix: bump the catalog version only on a whole send. That also makes CAMP correct (its held version runs ahead, so it stops re-requesting too) — `src/cube_bathymetry_node.cpp:1129`, `marine_web_view/coverage_renderer.py:707`, `reconciler.py:142`
- [ ] (must-fix) The heal never runs when pings stop — the exact case it exists for. drainRefreshQueue is reachable only from pingCallback -> publishBounded -> publishDirtyTiles, gated on ping-stamp time, and sits INSIDE publishBounded's TF-miss early return. End of a survey line, sonar off, transit, deactivate, or a TF gap all suspend the heal indefinitely, leaving a permanent invisible hole in FINISHED coverage. The file's own comment shows eviction was deliberately moved OUT of that early return as a stall hazard; the drain was put back inside it. Fix: wall timer, like save_timer_/catalog_timer_/disk_serve_timer_ — `src/cube_bathymetry_node.cpp:1853`
- [ ] (must-fix) A brand-new tile's FIRST message is a patch (refreshDue is false when the tile is not yet in patched_), so a tile first touched on the last ping before pings stop reaches the consumer as a patch over a tile it never received whole — `include/cube_bathymetry/coverage_refresh.h:74`
- [ ] (must-fix) subwindow_refresh_interval and subwindow_refresh_tiles_per_cycle are accepted-reads-back-inert: read once into refresh_tracker_.configure() at configure, no read_only, no set_parameters callback anywhere in the node. This is the IDENTICAL defect commit 1665651 closed on the sibling parameter in this same branch, citing that it cost a wrong diagnosis and an operator-link outage — and the descriptor actively invites the operator to tune it, tabulating 300s/60s/30s costs. Flagged independently by all three specialists. Fix: a callback that re-configures the tracker (read_only would contradict the descriptor's own invitation) — `src/cube_bathymetry_node.cpp:573`, `:590`
- [ ] (must-fix) The "within 60 s" bound is stated unconditionally in three places and is budget-limited in fact: 2 tiles/cycle x one cycle per 5 s = 24 tiles per interval. A line-end turn owing more than that degrades heal latency without bound, signalled only by an RCLCPP_DEBUG that is off in production. Separately, the drain's own saturation ceiling (2 x ~183 kB / 5 s = ~73 kB/s) EXCEEDS the 56.0 kB/s transit stream it replaces — `include/cube_bathymetry/coverage_refresh.h:51`, `src/cube_bathymetry_node.cpp:546`
- [ ] (must-fix) ADR-0001's addendum now contradicts the shipped code: it states "off by default" and "false is the correct value" (8cadd48 ships true), never mentions the refresh queue or the two new parameters, omits test_coverage_refresh from its Enforced-by list, and still leads with ~958 kB/s / "64% of the budget" — an UNCOMPRESSED cell-count figure that this branch's own commit f82e8dd establishes is not what the link carries (measured: 56.0/85.7 kB/s, 4-6%). The correct argument, which the commits make well, is peak-message admission; the ADR does not contain it — `docs/decisions/0001-tile-eviction-and-incremental-publish.md:162,352,407,423,457,460`
- [ ] (must-fix) Zero package documentation for all three parameters: README.md has no parameter section at all, so the shipped defaults, the heal semantics and the consumer prerequisite exist only in source. (Related pre-existing gap, NOT introduced here: this repo has no .agents/README.md, so there is no verified-parameter table to update — worth its own issue) — `README.md`
- [ ] (suggestion) The .msg contracts are falsified by the flip. SonarVisualizationTile.msg promises the catalog/TileRequest path heals a lost patch — false for a consumer that takes possession from a patch, and now true only by a DIFFERENT mechanism (a producer-side timer) the contract never mentions. TileCatalogEntry.msg's deferral was explicitly scoped "while publish_dirty_subwindow defaults to false"; the flip removed that precondition without revisiting it. Fixing the catalog bump above resolves most of this — cross-repo, unh_marine_autonomy
- [ ] (suggestion) refreshDue uses now() while the publish cycle is triggered off the ping stamp. Under use_sim_time bag replay — the intended validation route, and how the Appledore numbers were produced — now() may not advance, so the heal never fires; a slow replay makes every publish whole and silently erases the optimisation
- [ ] (suggestion) The commit messages' 11.2/9.1 kB/s and the 300/60/30 s cost table cannot be reproduced from the tool they cite: --tile-size-report never substitutes a whole tile for an aged tile and carries no per-tile last-full state. The numbers came from a post-hoc simulation over the CSV that is not in the diff. Either model the refresh in the tool (CoverageRefreshTracker is header-only and already linkable there) or state the derivation
- [ ] (suggestion) A whole tile served via TileRequest or the disk-serve drain does not discharge the refresh debt, so a tile whole-served one second ago is re-sent whole within 60 s
- [ ] (suggestion) Nothing in the eviction path calls forget(); evicted tiles are reaped lazily inside dueForRefresh, whose early return is skipped entirely when the refresh is disabled — so with interval 0 or budget 0, both documented values, patched_ grows for the life of the sheet
- [ ] (suggestion) subwindow_refresh_tiles_per_cycle has no upper bound and no integer_range: a YAML typo of 20 sends up to 20 whole tiles in one cycle, the exact burst the parameter exists to prevent
- [ ] (suggestion) The throttled INFO that exists so the enable/disable call can be made from measured data excludes the drain's whole-tile re-sends (the dominant cost at a 60 s refresh) and reports uncompressed cells x 4 — the proxy f82e8dd dismisses. It will systematically overstate the saving to an operator making a live link decision
- [ ] (suggestion) A written window with no finite depth returns nullopt, publishes nothing, records no debt, and then has its box cleared — whole-tile mode would have re-sent those cells on the next dirty cycle; sub-window mode will not unless they are written again
- [ ] (suggestion) on_configure re-entry throws ParameterAlreadyDeclaredException on its first line (pre-existing, whole-node), so the cleanup->configure path refresh_tracker_.clear() is written to protect cannot actually be reached; rclcpp_lifecycle swallows it, so the operator sees a transition that did not obviously fail
- [ ] (suggestion) import_bag's size report checks the stream only at construction: no check after any row write and no close/flush check, so a full disk truncates silently the CSV that is the evidence base for a fleet-wide default flip

### Notes
- CROSS-REPO OBLIGATION (report only): bizzyboat.yaml has the three coverage entries commented out of the vpn topics_list with the restore condition written in — "RESTORE once tiles are patched or the link budget grows". This branch satisfies that condition, so merging without the config change ships a fix the operator cannot see. That repo is field-mode/gitcloud; file as a linked issue, and GATE it on the catalog-bump fix above. Its sizing comments are also stale (1,843,200 B and "~183 KB compressed" against a measured 3,686,400 B payload, 145-345 KB median, 1.12 MB peak).
- VERIFIED CLEAN, worth recording because the prior work in this package failed here: all 597 tests were EXECUTED, not merely compiled. The four fixture fixes remove zero assertions; one STRENGTHENS a pre-existing test that had been silently vacuous (its two "well-separated" touchdowns straddled a tile boundary, making it a single-value test that still passed). Also clean: dirty-set lifecycle (no path loses a cell), eviction contract and the no-dirty-mark reload, tracker state machine (no starvation, no every-cycle re-send, both disable paths), quantizeTileWindow clamping and row-major packing, and the byte-identity claim for the whole-tile path verified by diff against origin/jazzy.

## Implementation

**Status**: complete
**When**: 2026-08-26 08:56 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: feature/issue-112 at `d316d7b` (7 commits on top of `d80d33f`)
**Scope**: the SUGGESTIONS from the round-1 Local Review, plus one stale-doc
defect found while checking them. All 7 must-fixes had already landed in
`673c23e`..`d80d33f`; this pass closes what was left open.

**Verification**: `colcon test` EXECUTED (not merely compiled): **602 tests,
0 errors, 0 failures**, 70 skipped (all 70 are cppcheck's own file skips,
pre-existing). cpplint and uncrustify clean on every changed file. The
import_bag change is verified against a real bag rather than asserted --
6000 pings (602 s) of `bag_2026-06-09T14.51.50_m3_detections`, 2 tiles,
zlib-compressed as udp_bridge sends it:

| mode | bytes | rate | vs whole-tile |
|---|---|---|---|
| whole-tile (today) | 2,957,492 | 4,913 B/s | 100% |
| pure sub-window | 358,714 | 596 B/s | 12.1% |
| **as shipped (patches + heal)** | **600,126** | **997 B/s** | **20.3%** |

The heal is 40% of the shipped mode's traffic. That is the measurement the
old report could not produce, and it is why the saving was being overstated
by 1.7x to whoever decides whether to leave the mode on over a survey link.
(Two tiles over ten minutes is a sample, not a fleet number: what it
establishes is that the omitted half is the same order as the reported half.)

### Findings addressed

- [x] (suggestion) `.msg` contracts falsified by the flip — RESOLVED IN THIS
  REPO, CROSS-REPO REMAINDER OPEN. The catalog-bump fix (`673c23e`) restored
  the documented heal for both consumers. The comment-only corrections to
  `SonarVisualizationTile.msg` / `TileCatalogEntry.msg` live in
  `unh_marine_autonomy` and still need their own issue — surfaced to the user,
  not filed unilaterally (cross-repo)
- [x] (suggestion) `refreshDue` uses `now()` while the drain ran on a WALL
  timer — the drain is now on the NODE clock (`34c60d1`), so cadence and
  due-test share a clock under `use_sim_time`. This was the one that would
  have corrupted the bag-replay validation route itself: played slower than
  real time the heal never fires, and the run reports a saving the boat will
  not reproduce
- [x] (suggestion) The `--tile-size-report` numbers could not be reproduced
  from the tool that produced them — the tool now MODELS the shipped policy
  with the same `CoverageRefreshTracker` the node runs, driven by bag time
  (`fb61f58`). Four new columns (`source,sent,sent_serialized,sent_compressed`)
  plus `--tile-refresh-interval` / `--tile-refresh-budget`. Chosen over
  "state the derivation" deliberately: a derivation ages out of step with the
  code, a model does not
- [x] (suggestion) A whole tile served via TileRequest did not discharge the
  refresh debt — it does now (`981249b`). Worse than a wasted message in the
  case that produces it: requests arrive in BURSTS after a restart or a link
  outage, so every tile in the burst owed a duplicate whole send inside one
  interval, against a link that had just been down
- [x] (suggestion) Nothing called `forget()` on eviction; `patched_` grew for
  the life of the sheet whenever the heal was disabled — eviction now
  discharges the debt AND warns (`9daa9e2`). Eviction is also the last moment
  anything knows the tile existed, so it is the only moment the operator can
  be told the gap is unpayable
- [x] (suggestion) `subwindow_refresh_tiles_per_cycle` unbounded — ALREADY
  CLOSED by `b589811`'s `kMaxRefreshTilesPerCycle` validation, verified at
  source this pass
- [x] (suggestion) The throttled INFO overstated the saving to an operator
  making a live link decision — rewritten as an accumulating ledger over a
  fixed 30 s window that counts the heal's whole-tile re-sends, reported from
  the drain as well as the publish path so it does not go quiet when the
  pings stop, and labelled as uncompressed cell bytes with a pointer to the
  tool that measures the wire (`32cc45b`)
- [x] (found this pass, not in the review) The tracker's class comment and its
  test file's header still argued from the pre-`673c23e` premise ("the producer
  bumps the catalog version on a patch exactly as on a whole tile") — corrected
  (`6e327e0`). Stale rationale is how a correct guard gets removed later by
  someone who checks the claim and finds it false
- [x] (found this pass, not in the review) `main()` in `import_bag_main.cpp`
  was one line under cpplint's 500-line ceiling, so the new options tipped it
  over — a REAL `colcon test` failure that `fb61f58` shipped, since the
  pre-commit hooks here do not run cpplint. Fixed by extraction, not by
  suppression (`d316d7b`)

### Findings assessed, no change made

- (suggestion) A written window with no finite depth publishes nothing, records
  no debt, and has its box cleared. Traced: `quantizeTileWindow` returns
  nullopt only when the ENTIRE window has no finite depth, so no displayable
  cell is lost — a cell that is NaN now and never written again has nothing to
  show, and one that later becomes finite is re-marked dirty by that write.
  Whole-tile mode would have re-sent the tile's OTHER, already-delivered cells;
  that is redundancy, not data
- (suggestion) `on_configure` re-entry throws `ParameterAlreadyDeclaredException`
  on its first line, so the cleanup->configure path is unreachable. Pre-existing
  and whole-node, not introduced by #112; fixing it here would be an unrelated
  lifecycle change on a boat-facing node. Worth its own issue

### Still open (not code in this repo)

- **CROSS-REPO, GATED CONDITION NOW MET**: `bizzyboat.yaml` has the three
  coverage topics commented out of the VPN `topics_list` with the restore
  condition written in — "RESTORE once tiles are patched or the link budget
  grows". This branch satisfies it, so merging without that config change
  ships a fix the operator cannot see. Field-mode/gitcloud repo. Its sizing
  comments are also stale (1,843,200 B and "~183 KB compressed" against a
  measured 3,686,400 B payload)
- **#112 stays OPEN on its own terms**: fixed-size chunking — what the issue
  actually asks for — is still not done. This branch bounds the message to the
  dirty window, which is a large practical reduction but not a BOUND: a
  diagonal track's bounding box degrades toward the full tile

### Note

Nondeterminism worth knowing before anyone diffs two reports: re-running the
same binary over the same bag gives byte-identical serialized sizes but 20 of
233 COMPRESSED sizes differing by 1-2 bytes (aggregate ratio unchanged at
20.29%). That is CDR alignment padding the serializer does not zero, not a
finding.

## Integrated Review
**Status**: complete
**When**: 2026-08-26 09:09 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #136 at `54ade25`
**Sources**: 2 effective (Copilot R1 @ `e5a32d7`, CI rollup @ `54ade25`) — plus the
local timeline (`## Local Review (Pre-Push)` @ `8cadd48`, `## Implementation` @
`d316d7b`), whose 17 findings were all dispositioned before this head
**Cross-source confirmations**: 0
**CI**: failures-noted (both checks red; neither caused by this diff)

**THE CURRENT HEAD HAS NOT BEEN REVIEWED BY COPILOT.** Both review runs after
`e5a32d7` — at `d80d33f` and at `54ade251` — returned "Copilot was unable to
review this pull request because the user who requested the review has reached
their quota limit", and the `copilot-pull-request-reviewer` check is red for
that reason, not for a finding. So the multi-model review this workflow depends
on has run against NONE of the 13 commits that followed `e5a32d7`, including
every fix for the round-1 must-fixes and all 7 commits from this session. The
red check is not a defect signal and must not be read as one; equally, a green
board here would not mean the code was reviewed.

### Findings
- [ ] (valid, Copilot R1 @ `e5a32d7`) `dtypeSize()` treats every dtype other
  than INT16 as one byte, so an unexpected dtype produces a misleading "band
  does not cover the window" failure that accuses the message rather than the
  helper. Stronger than Copilot argued: `VisualizationBand.msg` ALREADY declares
  `UINT16 = 4` ("reserved for sidescan source rasters"), so this is wrong today
  for a dtype the wire contract names, not merely for a hypothetical future one.
  Fix: handle UINT8/INT16/UINT16 explicitly and `ADD_FAILURE()` on anything else
  — `cube_bathymetry/test/test_quantize_tile.cpp:82`
- [ ] (valid, CI @ `54ade25`) `ROS 2 Jazzy (industrial_ci)` fails in
  `setup_upstream_workspace`, before cube_bathymetry is built at all:
  `marine_web_view: Cannot locate rosdep definition for [marine_ais_msgs]`.
  Upstream drift, not this diff — `marine_web_view` is a new package in the
  `unh_marine_autonomy` monorepo that cube does not ship, and it will fail every
  PR in this repo until fixed. The repo already has the pattern for exactly this
  case, documented in `ci.yml` for `mission_manager*`: prune the package with
  COLCON_IGNORE via `AFTER_SETUP_UPSTREAM_WORKSPACE` and add its unresolvable
  key to `ROSDEP_SKIP_KEYS`, because rosdep does not honour COLCON_IGNORE. Fix:
  add `marine_web_view` to the prune list and `marine_ais_msgs` to the skip
  keys. (Cloning `rolker/marine_ais` into `upstream.repos` would also work but
  contradicts the documented intent — "cube CI should not compile packages it
  never ships") — `.github/workflows/ci.yml:84,85`

### False positives
- None. Copilot raised exactly one finding across three review attempts, and it
  is valid.

### Notes
- No cross-source confirmations, and that is a fact about coverage rather than
  about the code: the only Copilot review that completed predates every commit
  the local review's findings were fixed in, so the two sources never examined
  the same head.
- Merge verification (ADR-0018): this is a project repo, so a full-scope
  `ci_local.sh` attestation can satisfy the gate without hosted Actions — but
  `ci_local.sh` builds `upstream.repos` as an underlay too, so it will hit the
  SAME rosdep failure until the finding above is fixed. The CI fix is therefore
  a prerequisite for either verification route, not optional tidying.
- Local verification standing at `54ade25`: 602 tests, 0 failures, cpplint and
  uncrustify clean (recorded in the `## Implementation` entry).

## Implementation

**Status**: complete
**When**: 2026-08-26 09:24 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: feature/issue-112 at `5589abc`
**Scope**: both findings from the `## Integrated Review` at `54ade25`.

**Verification**: `colcon test` EXECUTED: **602 tests, 0 errors, 0 failures**,
70 skipped (cppcheck's own file skips, pre-existing). cpplint and uncrustify
clean on both changed files. `ci.yml` re-parsed as YAML after editing.

### Findings addressed
- [x] (valid, Copilot R1 @ `e5a32d7`) `dtypeSize()` sized every non-INT16 dtype
  at one byte — every dtype `VisualizationBand.msg` declares is now handled by
  name, and anything else is an explicit `ADD_FAILURE` naming the value
  (`5589abc`). Copilot argued it as future-proofing; the already-declared
  `UINT16 = 4` makes it current, and the old fallback produced a failure that
  accused the message rather than the helper
- [x] (valid, CI @ `54ade25`) `marine_web_view: Cannot locate rosdep definition
  for [marine_ais_msgs]` — pruned via COLCON_IGNORE and its key added to
  `ROSDEP_SKIP_KEYS`, the same two-part treatment the block already documents
  for `mission_manager*` (`172cd3b`). Cloning `marine_ais` into
  `upstream.repos` was rejected: it would have cube CI compile a package it
  never ships, which is what the surrounding comment says not to do. The
  comment now names the rule rather than just the list

### Notes
- The CI fix is a prerequisite for BOTH merge-verification routes under
  ADR-0018, not only hosted Actions: `ci_local.sh` builds `upstream.repos` as
  an underlay and hits the identical rosdep failure.
- STILL OPEN, and not a code finding: Copilot has reviewed none of the commits
  after `e5a32d7` — three attempts, two refused for quota. Hosted CI going green
  will not change that. A human read or a fresh Copilot run once quota resets is
  the only way this head gets a second pair of eyes.
