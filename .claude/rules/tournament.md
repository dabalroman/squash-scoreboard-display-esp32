---
paths:
  - "src/Tournament/**"
  - "src/DeviceMode/PadelMode/**"
  - "src/DeviceMode/*/Views/*GamePlayingView.h"
  - "test/test_rules/**"
---

# Match / Tournament / Rules

- `GameResult` stores slim post-game results. `winnerPlayerId` is a `uint8_t` player ID (not `GameSide`) — resolved at write-time in `Match::finishGame()` respecting swap state.
- `Rules` (abstract) — `checkWinner(scoreA, scoreB)` returns `GameSide`. Implementations: `SquashRules`, `VolleyballRules`, `ShortVolleyballRules`, `PadelRules`. `Rules` also exposes `historyReserve()` so each sport pre-sizes its `GameScoreHistory` (Squash 32 default, Volleyball 64, Padel 16).
- **Game ball** - `Rules::willWinOnNextPointScored(a, b, side)` (non-pure, no overrides) = not already won and `checkWinner` with that side +1 returns that side. Per side (bool), never a returned `GameSide`. `Game::willWinOnNextPointScored(side)` applies it to the **committed** score: an uncommitted point neither starts nor stops it (40:00 then an uncommitted 40:15 keeps the left breathing).
- **On fire** - `Rules::onFireStreak()` (5; `PadelRules` 3, counting gems). `GameScoreHistory::committedStreak(side)` walks the history backwards: skips uncommitted `scored`, counts `lost` as committed (only `commit()` erases it, like `getRealScore()`), stops at the other side. `Game::isOnFire(side)` = streak >= threshold; per `Game`, never carried across games or sets.
- **Comeback** - `Game::commit()` sets a one-shot `takeComebackSide()` when a side scores (positive delta) while the opponent's committed streak **before** the commit is >= `onFireStreak()`: the point that breaks an on-fire streak, not a score gap. One per commit, none on a winning commit. Squash and volleyball views take it; padel's never does.
- Score changes are "uncommitted" until 4 seconds of inactivity (each GamePlaying view's local `COMMIT_TIMEOUT_MS = 4000`), then `game->commit()` is called.

### Padel scoring (two-level history)
- Padel maps its three-level structure onto the two-level engine without touching the shared model: the engine `Game` **is a set** (its score = gems won, its history entries = gems), and a separate `PadelGemScorer` (`src/Tournament/Game/`) tracks the within-gem 0/15/30/40/Ad ladder **below** the `Rules` abstraction.
- `PadelGemScorer` is backed by its own `GameScoreHistory` (rallies), mirroring how `Game` uses one. So there are two independent history levels: gems (on the engine `Game`, shown on the LED bar) and rallies (in the scorer).
- `PadelGamePlayingView` keeps a stack of finished gems' rally histories so undo can step back across gem boundaries. When a gem completes it hand-couples the levels: `game->scorePoint(gemWinner)` + snapshot the gem; step-back reverses both.
- All-square at `PadelRules::GEMS_PER_SET` (6) the set goes to a **tiebreak**: the same `PadelGemScorer` with its target moved from 4 to 7 (win by 2, uncapped), so the set ends 7-6. `checkWinner` therefore also wins on *any* score above `GEMS_PER_SET` - only a won tiebreak can produce one.
- The tiebreak mode is **derived**, never latched: `PadelRules::isTiebreakScore()` on the committed gem score, re-applied after every gem commit and every step-back. That is what lets undo walk back out of a tiebreak without a second state machine. Displays print plain numbers instead of the ladder (zero-padded on the LEDs, like every sport's `00:00`, and Love is `00` too) and the e-paper label reads `TIE` while the rows stay on gems - the panel is match-level, a partial refresh per rally would burn the ghosting budget.
- Padel breathes at **gem ball**: `PadelGemScorer::willWinOnNextRally(side)` on the committed rallies (40:00, 40:30, Ad; in the tiebreak the next rally wins the set). There is deliberately no set-ball breathing at gem level (5-4): it could breathe both sides at once.
- No serve or change-of-ends tracking: the players deliberately skip ends changes, and the board has never known who serves.
