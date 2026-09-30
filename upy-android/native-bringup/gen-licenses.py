#!/usr/bin/env python3
# Generates licenses.html (Settings > About > Licenses) from LICENSES.txt
# and LiteRT's THIRD_PARTY_NOTICE.txt (from the LiteRT AAR).
#
# LiteRT's notice file repeats the full Apache 2.0 text once per
# component (116 copies, ~1.3 MB of its 1.9 MB). A copy whose text,
# whitespace ignored, equals the Apache 2.0 text in LICENSES.txt is
# replaced by a link to that one copy; each component's own copyright
# and NOTICE lines stay. Everything else is kept verbatim. If the notice
# file cannot be split into components, it is included whole.
#
# usage: gen-licenses.py LICENSES.txt THIRD_PARTY_NOTICE.txt out.html

import html
import re
import sys

APACHE_START = re.compile(r"Apache License\s+Version 2\.0, January 2004")
APACHE_END = "END OF TERMS AND CONDITIONS"
APPENDIX_END = "limitations under the License."
SECTION = re.compile(r"^={20,}\n(\d+)\. ([^\n]+)\n={20,}\n", re.M)
COMPONENT = re.compile(r"\n\n\n(?=\S[^\n]{0,80}:\n)")


def norm(text):
    return re.sub(r"\s+", " ", text.replace("https://", "http://")).strip()


def apache_span(text, start=0):
    """(begin, end) of the Apache 2.0 terms in text, up to END OF TERMS."""
    m = APACHE_START.search(text, start)
    if not m:
        return None
    end = text.find(APACHE_END, m.end())
    if end < 0:
        return None
    return m.start(), end + len(APACHE_END)


def appendix_span(text, start):
    """(begin, end) of the "APPENDIX: How to apply..." right after start."""
    begin = text.find("APPENDIX", start, start + 200)
    if begin < 0 or text[start:begin].strip():
        return None
    end = text.find(APPENDIX_END, begin, begin + 2500)
    if end < 0:
        return None
    return begin, end + len(APPENDIX_END)


def pre(text):
    return "<pre>" + html.escape(text.strip("\n")) + "</pre>\n"


def main(licenses_path, notice_path, out_path):
    licenses = open(licenses_path, encoding="utf-8").read()
    notice = open(notice_path, encoding="utf-8").read()

    # LICENSES.txt: intro, then numbered sections.
    heads = list(SECTION.finditer(licenses))
    sections = []
    for i, m in enumerate(heads):
        body_end = heads[i + 1].start() if i + 1 < len(heads) else len(licenses)
        sections.append((m.group(1), m.group(2), licenses[m.end():body_end]))
    intro = licenses[:heads[0].start()] if heads else licenses

    apache_ref = None
    apache_text = None
    appendix_text = None
    for num, title, body in sections:
        span = apache_span(body)
        if span:
            apache_ref = "license-" + num
            apache_text = norm(body[span[0]:span[1]])
            app = appendix_span(body, span[1])
            if app:
                appendix_text = norm(body[app[0]:app[1]])
            break

    # LiteRT notices: "Name:" line, then that component's texts.
    components = []
    for part in COMPONENT.split("\n\n" + notice):
        part = part.strip("\n")
        if not part:
            continue
        name, _, body = part.partition(":\n")
        components.append((name.strip(), body))

    out = []
    out.append("""<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root { color-scheme: light dark; }
body { font-family: sans-serif; margin: 12px; }
pre { font-size: 11px; white-space: pre-wrap; overflow-wrap: anywhere; }
summary { padding: 6px 0; cursor: pointer; }
h2 { font-size: 1.1em; margin-top: 1.5em; }
</style></head><body>
<h1>Licenses</h1>
""")
    out.append(pre(intro.split("\n", 2)[-1]))
    out.append("<h2>upy-android and its components</h2>\n")
    for num, title, body in sections:
        out.append('<details id="license-%s"><summary>%s. %s</summary>\n%s</details>\n'
                   % (num, num, html.escape(title), pre(body)))

    out.append("<h2>LiteRT third-party notices</h2>\n")
    replaced = 0
    if len(components) < 2:
        out.append(pre(notice))
    else:
        out.append("<p>From the LiteRT AAR. Copies of the Apache License 2.0 are replaced by "
                   'a link to <a href="#%s">one copy</a>.</p>\n' % apache_ref)
        for name, body in components:
            chunks = []
            pos = 0
            while True:
                span = apache_span(body, pos)
                if not span:
                    break
                if apache_text and norm(body[span[0]:span[1]]) == apache_text:
                    chunks.append(pre(body[pos:span[0]]) if body[pos:span[0]].strip() else "")
                    chunks.append('<p><a href="#%s">Apache License 2.0</a></p>\n' % apache_ref)
                    replaced += 1
                    pos = span[1]
                    # The unfilled "how to apply" template adds nothing;
                    # a filled-in one carries a copyright line and stays.
                    app = appendix_span(body, pos)
                    if app and norm(body[app[0]:app[1]]) == appendix_text:
                        pos = app[1]
                else:
                    chunks.append(pre(body[pos:span[1]]))
                    pos = span[1]
            if body[pos:].strip():
                chunks.append(pre(body[pos:]))
            out.append("<details><summary>%s</summary>\n%s</details>\n" % (html.escape(name), "".join(chunks)))

    # A link to a collapsed section opens it.
    out.append("""<script>
document.addEventListener('click', function (ev) {
  var a = ev.target.closest('a[href^="#"]');
  var d = a && document.getElementById(a.getAttribute('href').slice(1));
  if (d && d.tagName === 'DETAILS') d.open = true;
});
</script>
""")
    out.append("</body></html>\n")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("".join(out))
    print("%s: %d sections, %d LiteRT components, %d Apache copies replaced"
          % (out_path, len(sections), len(components), replaced))


if __name__ == "__main__":
    main(*sys.argv[1:4])
