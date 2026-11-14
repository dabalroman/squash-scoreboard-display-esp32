# Protocol v1 - machine-readable definition

The wire facts of [garmin-protocol.md](../garmin-protocol.md) (layouts, constants, commands,
worked examples) are written here once, in the style of the Bluetooth SIG's GATT Specification
Supplement YAML (`gss/org.bluetooth.characteristic.*.yaml`). The markdown's tables and section 16
walkthroughs are generated from these files; its behaviour prose stays hand-written.
`helpers/check_protocol.py` holds every implementation (firmware, `garmin_client.py`, watch app,
Android app) to them.

Nothing here is read at runtime or turned into code: constants, codecs and test vectors stay
hand-written in each language.

| File | Holds |
|---|---|
| `constants.yaml` | `constants` (numbers), `uuids`, `enums` (named values of a byte) |
| `characteristics/<name>.yaml` | one characteristic, its frames and their byte layouts |
| `scenarios.yaml` | use cases: ordered frames with exact bytes, decoded fields and why each happens |

## From GSS

A characteristic file keeps GSS's top-level shape:

```yaml
characteristic:
  identifier: scbd.characteristic.<name>   # GSS: org.bluetooth.characteristic.<name>
  name: AUTH
  description: ...
  structure: [...]                          # GSS: one layout per characteristic
```

and each `structure` entry keeps GSS's `field`, `type`, `size`, `description`. Types are GSS's:
`uint8`, `uint16`, `uint32` (little-endian), `uint8[]` (bytes), `utf8s` (string, no NUL).

## Extensions

GSS writes sizes and presence as free text and has one layout per characteristic. This dialect
needs both machine-readable and several layouts per characteristic:

| Key | Where | Meaning |
|---|---|---|
| `uuid` | characteristic | name of an entry in `constants.yaml` `uuids` |
| `properties` | characteristic | ATT properties: `read`, `write`, `notify` |
| `auth_required` | characteristic | `false` = usable before AUTH (section 6) |
| `frames` | characteristic | replaces `structure` when one characteristic carries several frames; each has its own `structure` |
| `frame` | frame | id, used by scenarios (`frame: proof`) |
| `access` | frame | `read`, `write` or `notify` |
| `sender` | frame | `board` or `watch` (any client) |
| `size` | frame, field | frame: integer or `[min, max]` bytes. field: integer, a constant name, or an expression over constants and earlier fields |
| `value` | field | fixed value: a constant (`PROTO`), an enum value (`auth_type.PROOF`) or an integer |
| `enum` | field | the field's value is one of that enum's |
| `range` | field | inclusive `[min, max]`, expressions allowed |
| `present_if` | field | condition on an earlier field: `status == OK`, `flags & PAIRING`; absent fields take no bytes |
| `computed` | field | derived (MACs, CRCs): a checker cannot re-encode it, the scenario's bytes are authoritative |

Every frame, field, constant and enum value carries `description` (what it is). Every
characteristic, frame, constant and enum carries `usage` (who sends or reads it, when, what the
receiver does) and `see` (the prose section, as a markdown anchor).

## Scenarios

```yaml
- id: auth                        # step ids are <scenario>/<step>: auth/proof
  section: "16.2"
  title: Mutual authentication
  goal: what the user / watch is trying to achieve
  steps:
    - step: proof
      direction: watch -> board
      access: write
      characteristic: AUTH
      frame: proof
      hex: 01 01 02 ...            # exact bytes on the wire
      fields: {slot: 2, ...}       # decoded; the checker re-encodes them and compares with hex
      derivation: {...}            # optional: HMAC/CRC proof of a computed field; `field:` binds it
      why: why this step happens
    - step: result_bad_proof
      instead_of: result_ok        # an alternative outcome, not the next step
```

`fixtures` at the top holds the synthetic inputs (key, nonces, board id) every scenario uses.

## Tagging test vectors

Each implementation's tests carry the scenario bytes as hand-written literals with a tag on the
same or the preceding line:

```cpp
// spec: auth/proof
const uint8_t proof[] = {0x01, 0x01, 0x02, 0xb0, ...};
```

`check_protocol.py --repo <name>` fails when a tagged literal's bytes differ, when a step the
repo declares is untagged, or when it finds fewer tags than declared.

## Changing the wire

YAML first, then `python helpers/check_protocol.py build` (rewrites the generated markdown
regions), then the code and the tagged vectors in every implementation. Generated regions are
never edited by hand.

## Design rules

Keep the protocol standard; extend what exists before adding anything.

- A new place to go is a new id in an existing list (MENU's menu-entry ids), acted on by the
  existing commands (FOCUS, SELECT_SPORT, BACK); a new screen is a new `screen` id.
- No magic flags: no STATE bit or field that restates what a client can already read (the
  screen, TOURNAMENT STATUS, the roster).
- No one-off commands for one feature (`ENTER_X` / `LEAVE_X`). When a behaviour needs two generic
  commands, use two: every command needs support in the firmware, `garmin_client.py`, the watch
  and the tablet.
