# Minifies and gzips web/ into src/Web/WebAssets.generated.h (gitignored), served
# from flash by src/Web/WebAssets.cpp. The sources stay readable; only flash size
# matters, so the browser gets the minified text. Runs as a PlatformIO pre-script
# and standalone:
#   python helpers/web_assets.py
#   python helpers/web_assets.py --selftest   (minifier cases only, writes nothing)
# The header is built whole in memory and only written when its bytes change, so
# an unchanged web/ never forces a rebuild and a failure never leaves a partial one.

import gzip
import os
import re
import sys

# URL path, file under web/, Content-Type. The route table in PlayerSetupWebUi.cpp
# registers the same paths.
ASSETS = [
    ("/", "profile.html", "text/html; charset=utf-8"),
    ("/settings", "settings.html", "text/html; charset=utf-8"),
    ("/update", "update.html", "text/html; charset=utf-8"),
    ("/app.css", "app.css", "text/css; charset=utf-8"),
    ("/profile.js", "profile.js", "application/javascript; charset=utf-8"),
    ("/settings.js", "settings.js", "application/javascript; charset=utf-8"),
    ("/update.js", "update.js", "application/javascript; charset=utf-8"),
]

OUTPUT = os.path.join("src", "Web", "WebAssets.generated.h")


class WebAssetsError(Exception):
    pass


def _is_ident(c):
    return c.isalnum() or c in "_$\\" or ord(c) > 126


# A '/' after one of these, or after a keyword below, starts a regex literal.
_REGEX_AFTER = set("(,=:[!&|?{};+-*%<>~^")
_REGEX_KEYWORDS = ("return", "typeof", "case", "do", "else", "in", "of", "void",
                   "delete", "new", "throw", "instanceof")
_TRAILING_WORD = re.compile(r"[\w$]+$")


def _min_js(name, src):
    """jsmin's rules: a newline survives wherever ASI could depend on it, a space
    only between two identifier characters or doubled + / -."""
    out = []
    i, n = 0, len(src)
    gap = None  # None, " " or "\n": whitespace skipped since the last token
    after_regex = False  # a regex ends like an identifier: letters after it would be flags

    def scan_quoted(start, quote):
        j = start + 1
        while j < n and src[j] != quote:
            if src[j] == "\\":
                # A line continuation may be CRLF: skip both characters.
                j += 2 if src.startswith("\r\n", j + 1) else 1
            elif src[j] == "\n":
                raise WebAssetsError("web/%s: unterminated string" % name)
            j += 1
        if j >= n:
            raise WebAssetsError("web/%s: unterminated string" % name)
        return j + 1

    def scan_regex(start):
        j, in_class = start + 1, False
        while j < n:
            c = src[j]
            if c == "\\":
                j += 1
            elif c == "[":
                in_class = True
            elif c == "]":
                in_class = False
            elif c == "/" and not in_class:
                j += 1
                while j < n and _is_ident(src[j]):
                    j += 1
                return j
            elif c == "\n":
                break
            j += 1
        raise WebAssetsError("web/%s: unterminated regex" % name)

    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if c in " \t\r\n\f\v":
            gap = "\n" if c == "\n" or gap == "\n" else " "
            i += 1
            continue
        if c == "/" and nxt == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c == "/" and nxt == "*":
            end = src.find("*/", i + 2)
            if end < 0:
                raise WebAssetsError("web/%s: unterminated comment" % name)
            gap = "\n" if "\n" in src[i:end] or gap == "\n" else " "
            i = end + 2
            continue
        if c == "`":
            raise WebAssetsError("web/%s: template literals are not supported by the minifier" % name)

        prev = out[-1][-1] if out else ""
        # Chunks are mostly single characters; 16 covers the longest keyword.
        tail = "".join(out[-16:])
        prev_ident = _is_ident(prev) or after_regex
        if gap and prev:
            if (gap == "\n" and (prev_ident or prev in "}])'\"+-")
                    and (_is_ident(c) or c in "{[('\"+-!~")):
                out.append("\n")
            elif ((prev_ident and _is_ident(c)) or (prev == c and c in "+-")
                  or (c == "." and prev.isdigit())):  # 1 .toFixed() - "1." is a number
                out.append(" ")
        gap = None
        after_regex = False

        if c in "'\"":
            j = scan_quoted(i, c)
        elif c == "/":
            word = _TRAILING_WORD.search(tail)
            postfix = tail.endswith("++") or tail.endswith("--")  # i++ / 2 divides
            if not postfix and (not prev or prev in _REGEX_AFTER
                                or (word and word.group(0) in _REGEX_KEYWORDS)):
                j = scan_regex(i)
                after_regex = True
            else:
                j = i + 1
        else:
            j = i + 1
        out.append(src[i:j])
        i = j
    return "".join(out)


def _min_css(name, src):
    out = []
    i, n = 0, len(src)
    gap = False
    while i < n:
        c = src[i]
        if c.isspace():
            gap = True
            i += 1
            continue
        if src.startswith("/*", i):
            end = src.find("*/", i + 2)
            if end < 0:
                raise WebAssetsError("web/%s: unterminated comment" % name)
            gap = True
            i = end + 2
            continue
        prev = out[-1][-1] if out else ""
        # A space before ':' is kept: in a selector it is a descendant combinator.
        if gap and prev and prev not in "{};,:>(" and c not in "{};,>)!":
            out.append(" ")
        gap = False
        if c == "}" and prev == ";":
            out.pop()
        if c in "'\"":
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == "\\":
                    j += 1
                elif src[j] == "\n":
                    break
                j += 1
            if j >= n or src[j] != c:
                raise WebAssetsError("web/%s: unterminated string" % name)
            j += 1
        else:
            j = i + 1
        out.append(src[i:j])
        i = j
    return "".join(out)


_HTML_RAW = re.compile(r"<(pre|textarea)\b|<(script|style)\b[^>]*>\s*[^<\s]", re.I)
_HTML_ATTR_WS = re.compile(r"(\"[^\"]*\"|'[^']*')|\s+")


def _min_html(name, src):
    if _HTML_RAW.search(src):
        raise WebAssetsError("web/%s: <pre>, <textarea> or inline script/style would be "
                             "mangled by whitespace collapsing" % name)
    src = re.sub(r"<!--.*?-->", "", src, flags=re.S)
    # Whitespace between tags, where a line break in the source holds it, is layout
    # only; within a line it may separate inline content, so one space survives.
    src = re.sub(r">\s*\n\s*<", "><", src)
    parts = []
    for part in re.split(r"(<[^>]*>)", src):
        if part.startswith("<"):
            # Quoted attribute values are content (an input's value): kept verbatim.
            part = _HTML_ATTR_WS.sub(lambda m: m.group(1) or " ", part)
        else:
            part = re.sub(r"\s+", " ", part)
        parts.append(part)
    return "".join(parts).strip()


_MINIFIERS = {".js": _min_js, ".css": _min_css, ".html": _min_html}


def _compress(name, raw):
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        raise WebAssetsError("web/%s is not valid UTF-8: %s" % (name, e))

    raw = _MINIFIERS[os.path.splitext(name)[1]](name, text).encode("utf-8")

    try:
        # mtime=0: no timestamp in the gzip header, so the output is reproducible.
        blob = gzip.compress(raw, compresslevel=9, mtime=0)
    except Exception as e:
        raise WebAssetsError("gzip failed for web/%s: %s" % (name, e))

    if gzip.decompress(blob) != raw:
        raise WebAssetsError("gzip round trip mismatch for web/%s" % name)

    return blob, len(raw)


def _c_bytes(blob):
    lines = []
    for i in range(0, len(blob), 16):
        lines.append("    " + ",".join("0x%02x" % b for b in blob[i:i + 16]) + ",")
    return "\n".join(lines)


def generate(project_dir):
    """Returns (header_path, written). Raises WebAssetsError on any problem."""
    web_dir = os.path.join(project_dir, "web")
    out = ["// GENERATED by helpers/web_assets.py from web/ - do not edit, not in git.",
           "#pragma once",
           "",
           "#include <stddef.h>",
           "#include <stdint.h>",
           "#include <pgmspace.h>",
           "",
           "namespace WebAssetData {",
           "struct Asset {",
           "    const char *path;",
           "    const char *contentType;",
           "    const uint8_t *data;",
           "    size_t length;",
           "};",
           ""]
    rows = []
    total_raw = total_min = total_gz = 0

    for index, (url, name, content_type) in enumerate(ASSETS):
        path = os.path.join(web_dir, name)
        if not os.path.isfile(path):
            raise WebAssetsError("missing web/%s (served at %s)" % (name, url))
        with open(path, "rb") as f:
            raw = f.read()

        blob, minified = _compress(name, raw)
        total_raw += len(raw)
        total_min += minified
        total_gz += len(blob)
        print("web_assets: %-13s %6d B -> %6d B min -> %5d B gz" % (name, len(raw), minified, len(blob)))

        out.append("// web/%s" % name)
        out.append("static const uint8_t ASSET_%d[] PROGMEM = {" % index)
        out.append(_c_bytes(blob))
        out.append("};")
        out.append("")
        rows.append('    {"%s", "%s", ASSET_%d, %d},' % (url, content_type, index, len(blob)))

    out.append("static const Asset ASSETS[] = {")
    out.extend(rows)
    out.append("};")
    out.append("static const size_t COUNT = %d;" % len(ASSETS))
    out.append("}")
    out.append("")
    print("web_assets: total         %6d B -> %6d B min -> %5d B gz" % (total_raw, total_min, total_gz))

    content = "\n".join(out).encode("ascii")
    header = os.path.join(project_dir, OUTPUT)

    if os.path.isfile(header):
        with open(header, "rb") as f:
            if f.read() == content:
                return header, False

    tmp = header + ".tmp"
    with open(tmp, "wb") as f:
        f.write(content)
    os.replace(tmp, header)
    return header, True


# (minifier, input, expected output); expected None = must raise WebAssetsError.
_SELFTEST = [
    (_min_js, "x = a + +b; y = a - -b; z = a++ + b", "x=a+ +b;y=a- -b;z=a++ +b"),
    (_min_js, "var a = b\n(c)\n[d]\na\n++c", "var a=b\n(c)\n[d]\na\n++c"),
    (_min_js, "function f () {\n    return\n    1\n}", "function f(){return\n1}"),
    (_min_js, "function g (s) {\n    return /x\\//.test(s);\n}", "function g(s){return/x\\//.test(s);}"),
    (_min_js, "var r = /[/\"]x/g, d = a / b / c", "var r=/[/\"]x/g,d=a/b/c"),
    (_min_js, "if (!/\\.bin$/i.test(n)) {\n    x();\n}", "if(!/\\.bin$/i.test(n)){x();}"),
    (_min_js, "s = 'a // b /* c */' + \"it's\"; // tail", "s='a // b /* c */'+\"it's\";"),
    (_min_js, "a = 1 /* x\n */ b = 2", "a=1\nb=2"),
    (_min_js, "x = i++ / 2; y = 3 / 4; n = a[0]-- / 2 + 'q'", "x=i++/2;y=3/4;n=a[0]--/2+'q'"),
    (_min_js, "var r = /a/\nfoo()\nvar s = /b/ in o", "var r=/a/\nfoo()\nvar s=/b/ in o"),
    (_min_js, "x = 1 .toFixed(2) + a1 .b", "x=1 .toFixed(2)+a1 .b"),
    (_min_js, "s = 'a\\\r\nb'", "s='a\\\r\nb'"),
    (_min_js, "var t = `x`;", None),
    (_min_js, "var s = 'open", None),
    (_min_js, "var r = /open", None),
    (_min_js, "/* open", None),
    (_min_css, "/* c */\na:hover , b > c {\n    margin: 0 auto;\n    color: red;\n}\n",
     "a:hover,b>c{margin:0 auto;color:red}"),
    (_min_css, "nav :first-child { font: 16px 'Segoe UI' , sans-serif !important; }",
     "nav :first-child{font:16px 'Segoe UI',sans-serif!important}"),
    (_min_css, "a { content: '\\'x  /* y */' ; color: red }", "a{content:'\\'x  /* y */';color:red}"),
    (_min_css, "a { content: 'open }", None),
    (_min_html, "<!-- c -->\n<p>\n    Ala <b>ma</b> kota\n    i psa</p>\n<p>x</p>\n",
     "<p> Ala <b>ma</b> kota i psa</p><p>x</p>"),
    (_min_html, "<input  type=\"text\"\n    value=\"a  b\" title='x\n y'>",
     "<input type=\"text\" value=\"a  b\" title='x\n y'>"),
    (_min_html, "<pre>x</pre>", None),
    (_min_html, "<script>alert(1)</script>", None),
    (_min_html, '<script src="/a.js"></script>', '<script src="/a.js"></script>'),
]


def selftest():
    """Returns the number of failing cases; prints each one."""
    failures = 0
    for index, (minify, src, expected) in enumerate(_SELFTEST):
        try:
            got = minify("selftest", src)
        except WebAssetsError as e:
            if expected is None:
                continue
            reason = "raised: %s" % e
        else:
            if got == expected:
                continue
            reason = "got %r" % got
        failures += 1
        print("web_assets: selftest #%d %s(%r) %s, expected %r"
              % (index, minify.__name__, src, reason, expected), file=sys.stderr)
    print("web_assets: selftest %d/%d passed" % (len(_SELFTEST) - failures, len(_SELFTEST)))
    return failures


def _run(project_dir):
    try:
        header, written = generate(project_dir)
    except (WebAssetsError, OSError) as e:
        print("web_assets: ERROR: %s" % e, file=sys.stderr)
        return 1
    print("web_assets: %s %s" % ("wrote" if written else "unchanged", os.path.relpath(header, project_dir)))
    return 0


# SCons injects Import() into a pre-script's globals; plain python does not.
if "Import" in globals():
    Import("env")  # noqa: F821
    if _run(env["PROJECT_DIR"]) != 0:  # noqa: F821
        env.Exit(1)  # noqa: F821
elif sys.argv[1:] == ["--selftest"]:
    sys.exit(1 if selftest() else 0)
else:
    sys.exit(_run(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
