"""Bind every number on the artwork card to the source that declares it.

The art gate settles whether the card fits its columns and matches its spec.
Whether the card is true of this plugin is a different question, and this file
is where it gets answered: each row is measured again from the header, the
table, the harness or the CMake script that declares it, and a row that no
longer agrees is a failure rather than a stale drawing nobody noticed.

Standard library only, so it runs anywhere the repository is checked out and
does not need MSVC, the Windows SDK, Skyrim or a graphics device.
"""
import io
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / "docs" / "art" / "skyrimbridge.art.json"

WORDS = {
    1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 6: "six",
    7: "seven", 8: "eight", 9: "nine", 10: "ten", 11: "eleven",
    12: "twelve", 13: "thirteen", 14: "fourteen", 15: "fifteen",
    16: "sixteen", 17: "seventeen", 18: "eighteen", 19: "nineteen",
}

PARAMS = "src/core/BridgeData.cpp"
SHADERS = "shaders/SkyrimBridge_CB.fxh"
TARGETS = "src/core/BridgeData.h"
REFLECT = "src/core/EngineReflect.cpp"
COMMAND = "src/core/BridgeCommand.cpp"
LAYOUT = "src/SB_CommandLayout.h"
PAPYRUS = "src/core/PapyrusBridge.cpp"
API = "include/SkyrimBridgeAPI.h"
RUNNER = "scripts/run_validation.py"
BUILD = "CMakeLists.txt"
PROTOCOL = "docs/VALIDATION-PROTOCOL.md"

ENTRY = re.compile(r'ENTRY\([\w.]+, *"(SB_\w+)"\)')
DECL = re.compile(r"^float4\s+(SB_\w+)\s*<", re.MULTILINE)
DOMAIN = re.compile(r"^// ---- (\d+)\. (.+?) \((\d+) float4s?", re.MULTILINE)
EMIT = re.compile(r"RF_(?:F|U32|U8|C3F|C3B|INT|B|FLAGS|S|LINK)\(|Field\{"
                  r"|^\s*(?:colorB|fF|i8|link)\(")
LOOP = re.compile(r"for \(int \w+ = 0; \w+ < (\d+); \+\+\w+\)")
FACTORY = re.compile(r"auto \w+ = \[&\]\(")
SCALAR = re.compile(r"^\s*std::u?int32_t\s+\w+;", re.MULTILINE)
CHARS = re.compile(r"^\s*char\s+\w+\[(\d+)\];", re.MULTILINE)
RESULT = re.compile(r"^\s*(?:Result|Outcome|Verdict)\s*:\s*(?:PASS|FAIL)",
                    re.MULTILINE)


def _read(relative: str) -> str:
    return io.open(ROOT / relative, encoding="utf-8").read()


def _word(count: int) -> str:
    """Spelled out, because the card draws words where it can."""
    if count not in WORDS:
        raise AssertionError(f"no word for {count}; widen WORDS or use digits")
    return WORDS[count]


def _block(text: str, anchor: str, closer: str) -> str:
    """The source between an anchor line and the line that closes it."""
    start = re.search(anchor, text)
    if start is None:
        raise AssertionError(f"{anchor} is gone from the source")
    end = text.index(closer, start.start())
    return text[start.start():end]


def _fields(block: str) -> int:
    """Count what a schema pushes, multiplying through the loops that push it.

    Three shapes appear: the RF_ macros, bare Field literals, and the four
    weather factories. Each is worth one field, and a field inside a counted
    for loop is worth as many times as the loop runs. The factory definitions
    themselves push nothing at registration time, so they are skipped.
    """
    total, pending, depth, loops, skipping = 0, 1, 0, [], False
    for line in block.split("\n"):
        if FACTORY.search(line):
            skipping = True
        if skipping:
            skipping = line.rstrip() != "        };"
            continue
        loop = LOOP.search(line)
        if loop and line.rstrip().endswith("{"):
            loops.append((depth, int(loop.group(1)) * pending))
            pending, depth = 1, depth + 1
            continue
        if loop:
            pending *= int(loop.group(1))
            continue
        factor = pending
        for _, multiplier in loops:
            factor *= multiplier
        total += factor * len(EMIT.findall(line))
        pending = 1
        depth += line.count("{") - line.count("}")
        while loops and depth <= loops[-1][0]:
            loops.pop()
    return total


def _schemas() -> dict[str, int]:
    """Every registered record type, and how many fields it carries."""
    text = _read(REFLECT)
    found = {
        "Weather": _fields(_block(text, r"static void BuildWeatherFields",
                                  "\n    void RegisterBuiltins()")),
        "LandTexture": _fields(_block(text, r'Schema s\{ "LandTexture"',
                                      "R.push_back(std::move(s));")),
    }
    for opener in re.finditer(r'R\.push_back\(Schema\{ "(\w+)"', text):
        end = text.index("}});", opener.start())
        found[opener.group(1)] = _fields(text[opener.start():end])
    return found


def _domains() -> list[tuple[int, str, int, int]]:
    """Each shader-header domain: its number, name, claimed and actual count."""
    text = _read(SHADERS)
    heads = list(DOMAIN.finditer(text))
    rows = []
    for index, head in enumerate(heads):
        stop = heads[index + 1].start() if index + 1 < len(heads) else len(text)
        rows.append((int(head.group(1)), head.group(2).strip(),
                     int(head.group(3)),
                     len(DECL.findall(text[head.start():stop]))))
    return rows


def _mailbox() -> int:
    """The command block size, summed from the struct rather than the assert."""
    body = _block(_read(LAYOUT), r"struct SB_CommandBlock", "\n    };")
    return len(SCALAR.findall(body)) * 4 + sum(int(n)
                                               for n in CHARS.findall(body))


def _add_tests() -> list[str]:
    """Every declared CTest name, however the call is wrapped."""
    text = _read(BUILD)
    names = []
    for call in re.finditer(r"add_test\(", text):
        window = text[call.end():call.end() + 240]
        name = re.search(r"NAME\s+([A-Za-z_0-9.]+)", window)
        if name is None:
            raise AssertionError(f"an add_test in {BUILD} names nothing")
        names.append(name.group(1))
    return names


def _recorded_results() -> int:
    """Recorded in-game PASS or FAIL lines, and any receipt kept beside them."""
    recorded = sum(len(RESULT.findall(io.open(path, encoding="utf-8").read()))
                   for path in sorted((ROOT / "docs").rglob("*.md")))
    kept = [path for path in sorted((ROOT / "docs").rglob("*"))
            if path.is_file()
            and re.search(r"receipt|acceptance", path.name, re.I)]
    return recorded + len(kept)


def measure() -> dict[str, str]:
    """Every card value, rebuilt from the source rather than from the card."""
    schemas = _schemas()
    corpus = _block(_read(RUNNER), r"CORPUS_TESTS = frozenset", "\n)")
    harnesses = sorted((ROOT / "tests").glob("validate_*.py"))
    verbs = re.findall(r'verb == "([\w.]+)"', _read(COMMAND))
    version = re.search(r"kBridgeInterfaceVersion = (\d+)U", _read(API))
    targets = _block(_read(TARGETS), r"kTargetShaders\[\] = \{", "\n    };")

    return {
        "published parameters": f"{len(ENTRY.findall(_read(PARAMS)))} float4s",
        "parameter domains": f"{len(_domains())} of them",
        "target shaders": f"{_word(targets.count('.FX'))} of them",
        "record schemas": _word(len(schemas)),
        "named fields": f"{sum(schemas.values())} in all",
        "weather record": f"{schemas['Weather']} fields",
        "validation harnesses": f"{len(harnesses)} in tests",
        "corpus harnesses": f"{_word(corpus.count('validate_'))} of them",
        "command verbs": _word(len(verbs)),
        "command mailbox": f"{_mailbox():,} bytes",
        "papyrus functions":
            f"{_read(PAPYRUS).count('RegisterFunction(')} declared",
        "bridge abi version": _word(int(version.group(1))),
        "ctest targets": f"{_word(len(_add_tests()))} declared",
        "in-game acceptance": "none in the tree" if _recorded_results() == 0
                              else f"{_recorded_results()} in the tree",
    }


def check_card_rows_match_the_source() -> list[str]:
    card = json.load(io.open(SPEC, encoding="utf-8"))["cards"][0]
    drawn = {field["key"]: field["value"] for field in card["fields"]}
    measured = measure()
    bad = []
    for key, value in sorted(measured.items()):
        if key not in drawn:
            bad.append(f"the card no longer has a {key!r} row")
        elif drawn[key] != value:
            bad.append(f"{key}: the card says {drawn[key]!r}, "
                       f"the source says {value!r}")
    for key in sorted(set(drawn) - set(measured)):
        bad.append(f"{key!r} is drawn but nothing measures it")
    return bad


def check_both_languages_declare_the_same_parameters() -> list[str]:
    """The C++ table and the shader header publish one set of names, twice.

    Neither file can see the other. A parameter added on one side reads as a
    zero on the other, silently, in the game. So the two lists are compared in
    both directions, which is the strongest binding this repository has.
    """
    table = ENTRY.findall(_read(PARAMS))
    shader = DECL.findall(_read(SHADERS))
    bad = []
    for name in sorted(set(table) - set(shader)):
        bad.append(f"{name} is published by C++ and no shader declares it")
    for name in sorted(set(shader) - set(table)):
        bad.append(f"{name} is declared in the shader and nothing publishes it")
    if len(table) != len(set(table)):
        bad.append("the C++ table declares a name twice, so the count is high")
    if len(shader) != len(set(shader)):
        bad.append("the shader header declares a name twice")
    return bad


def check_each_domain_header_counts_what_follows_it() -> list[str]:
    """The header comments carry their own counts, and those counts drift."""
    bad = []
    rows = _domains()
    for number, name, claimed, actual in rows:
        if claimed != actual:
            bad.append(f"domain {number}, {name}, says {claimed} float4s and "
                       f"{actual} are declared under it")
    if [row[0] for row in rows] != list(range(1, len(rows) + 1)):
        bad.append("the domains are no longer numbered one through "
                   f"{len(rows)} in order")
    if sum(row[3] for row in rows) != len(DECL.findall(_read(SHADERS))):
        bad.append("some declarations sit outside every numbered domain")
    return bad


def check_the_counters_are_read_not_guessed() -> list[str]:
    """A regex that matches nothing reports zero and passes every row.

    So each parser is aimed at a shape it must refuse, and at a number this
    file did not use to draw the card. A reformat that breaks a parser fails
    here rather than quietly deflating a row.
    """
    bad = []
    try:
        _block(_read(REFLECT), r"static void NoSuchBuilderExists", "\n")
    except AssertionError:
        pass
    else:
        bad.append("a missing block was counted instead of refused")
    if f"== {_mailbox()}" not in _read(LAYOUT):
        bad.append(f"the struct sums to {_mailbox()} bytes and the static "
                   "assertion in the same header pins a different number")
    names = _add_tests()
    if len(names) != len(set(names)):
        bad.append("two CTest targets share a name, so the count is inflated")
    if _schemas()["ImageSpace"] != 17:
        bad.append("the image space schema no longer reads as seventeen "
                   "fields, so the field counter has stopped working")
    return bad


def check_the_marked_row_is_still_an_honest_null() -> list[str]:
    """The one toned row says no in-game acceptance is recorded here.

    If somebody records a live result, this fails, and the right repair is to
    redraw the card rather than to loosen the check.
    """
    protocol = _read(PROTOCOL)
    bad = []
    if "PASS/FAIL" not in protocol:
        bad.append("the protocol no longer asks for a recorded PASS or FAIL, "
                   "so the honest null has nothing to be a null about")
    if measure()["in-game acceptance"] != "none in the tree":
        bad.append("an in-game result is recorded now, and the card still "
                   "draws this row as an honest null")
    return bad


CHECKS = (
    check_card_rows_match_the_source,
    check_both_languages_declare_the_same_parameters,
    check_each_domain_header_counts_what_follows_it,
    check_the_counters_are_read_not_guessed,
    check_the_marked_row_is_still_an_honest_null,
)


def main() -> int:
    worst = 0
    for check in CHECKS:
        failures = check()
        name = check.__name__.removeprefix("check_")
        print(("ok   " if not failures else "FAIL ") + f"facts.{name}")
        for failure in failures:
            print(f"       {failure}")
        worst = max(worst, 1 if failures else 0)
    return worst


if __name__ == "__main__":
    raise SystemExit(main())
