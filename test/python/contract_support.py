import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text, name):
    """Return one C function body with balanced braces, excluding call sites."""
    signature = re.search(
        r"^[ \t]*(?:static[ \t]+)?[^;{}]*?\b"
        + re.escape(name)
        + r"[ \t]*\([^;{}]*\)[ \t]*\{",
        text,
        re.MULTILINE,
    )
    if not signature:
        raise AssertionError("function definition not found: " + name)
    opening = text.find("{", signature.start())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError("unterminated function: " + name)
