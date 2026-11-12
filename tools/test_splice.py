#!/usr/bin/env python3
"""splice.function_name regressions on synthetic C items, no game data."""
import functools
import itertools
import json
from pathlib import Path
import subprocess
import time
import sys
import unittest

import splice

TOOLS = Path(__file__).resolve().parent
TIMEOUT = 10.0     # seconds the child process gets before it is killed
FAST = 0.5         # seconds one pathological call may take

# function_name's expression before the backtracking fix is the reference its
# answers must keep:
#     \s*(?:static\s+)?[\w\s\*]+?\b(\w+)\s*\([^;{]*\)\s*(?:[\w\s\*\[\],]+;\s*)*\{
# original_match evaluates it by hand, trying the alternatives in the order
# re.match would, with one step per piece of the expression. Each step's
# result depends only on its position (there are no backreferences), so every
# step is cached per position and no position is retried, where re.match
# re-splits whitespace runs exponentially often.


def is_word(c: str) -> bool:
    return c.isalnum() or c == "_"


def is_decl(c: str) -> bool:      # [\w\s\*\[\],]
    return is_word(c) or c.isspace() or c in "*[],"


def original_match(code: str) -> str | None:
    """Group 1 of re.match(<the expression above>, code), or None."""
    n = len(code)

    def run(p: int, accept) -> int:
        while p < n and accept(code[p]):
            p += 1
        return p

    def at(p: int, c: str) -> bool:
        return p < n and code[p] == c

    @functools.cache
    def decls(p: int) -> bool:        # (?:[\w\s\*\[\],]+;\s*)*\{, greedy
        q = run(p, is_decl)           # ';' is not in the class: only q can end it
        if q > p and at(q, ";"):
            w = run(q + 1, str.isspace)
            for f in range(w, q, -1):
                if decls(f):
                    return True
        return at(p, "{")

    @functools.cache
    def after_params(p: int) -> bool:  # [^;{]*\)\s* then decls, greedy
        q = run(p, lambda c: c not in ";{")
        for e in range(q, p - 1, -1):
            if at(e, ")"):
                w = run(e + 1, str.isspace)
                if any(decls(f) for f in range(w, e, -1)):
                    return True
        return False

    @functools.cache
    def name(p: int) -> str | None:   # \b(\w+)\s*\( then after_params
        if (p > 0 and is_word(code[p - 1])) == (p < n and is_word(code[p])):
            return None
        q = run(p, is_word)
        for e in range(q, p, -1):
            w = run(e, str.isspace)
            if any(at(f, "(") and after_params(f + 1) for f in range(w, e - 1, -1)):
                return code[p:e]
        return None

    @functools.cache
    def type_then_name(p: int) -> str | None:  # [\w\s\*]+?, lazy
        e = p
        while e < n and (is_word(code[e]) or code[e].isspace() or code[e] == "*"):
            e += 1
            if (found := name(e)) is not None:
                return found
        return None

    def after_space(p: int) -> str | None:   # (?:static\s+)?, greedy
        if code.startswith("static", p):
            w = run(p + 6, str.isspace)
            for f in range(w, p + 6, -1):
                if (found := type_then_name(f)) is not None:
                    return found
        return type_then_name(p)

    for f in range(run(0, str.isspace), -1, -1):   # \s*, greedy
        if (found := after_space(f)) is not None:
            return found
    return None

CHILD = """
import json, sys, time
sys.path.insert(0, sys.argv[1])
import splice
item = sys.stdin.read()
t = time.perf_counter()
name = splice.function_name(item)
print(json.dumps({"name": name, "seconds": time.perf_counter() - t}))
"""


def bounded_function_name(item: str, splice_dir: Path = TOOLS, timeout: float = TIMEOUT) -> tuple[str | None, float]:
    """function_name(item) from splice_dir's splice.py, in a child process that
    is killed after timeout seconds (subprocess.TimeoutExpired)."""
    out = subprocess.run([sys.executable, "-I", "-c", CHILD, str(splice_dir)], input=item,
                         capture_output=True, text=True, timeout=timeout, check=True)
    result = json.loads(out.stdout)
    return result["name"], result["seconds"]


def original_name(item: str) -> str | None:
    code = splice.strip_comments(item)
    found = original_match(code)
    if found is not None and not code.lstrip().startswith(("typedef", "struct", "union", "extern")):
        return found
    return None


def pathological(count: int, brace: bool = False) -> str:
    return "int func_80012345(a)\n" + "\t\t;" * count + ("\n{\n}" if brace else "")


class FunctionNameTests(unittest.TestCase):
    def check(self, cases):
        for item, name in cases:
            with self.subTest(item=item):
                self.assertEqual(splice.function_name(item), name)
                self.assertEqual(original_name(item), name)

    def test_plain_definitions(self):
        self.check([
            ("void func_80012345(void) {\n}", "func_80012345"),
            ("s32 func_80012345(s32 arg0, s32 arg1)\n{\n    return arg0 + arg1;\n}", "func_80012345"),
            ("u8 *func_80012345(void) {\n    return 0;\n}", "func_80012345"),
            ("void* func_80012345(void){}", "func_80012345"),
            ("unsigned long long func_80012345(int (*cb)(int), char *p[4]) {\n}", "func_80012345"),
        ])

    def test_static_definitions(self):
        self.check([
            ("static void func_80012345(void) {\n}", "func_80012345"),
            ("static inline s32 helper(s32 *p)\n{\n    return *p;\n}", "helper"),
            ("static\nchar *\nhelper(void)\n{\n}", "helper"),
        ])

    def test_kr_parameter_declarations(self):
        self.check([
            ("void func_80012345(a, b)\n    int a;\n    char *b;\n{\n}", "func_80012345"),
            ("int func_80012345(a, b, c)\n\tint a[4], *b;\n\tunsigned c;\n{\n\treturn a[0];\n}", "func_80012345"),
            ("static int func_80012345(a)\nint a;{}", "func_80012345"),
            ("int func_80012345(a)\nint a;\n\n\n{\n}", "func_80012345"),
            ("void func_80012345(void) ; {\n}", "func_80012345"),  # ' ;' counts as a declaration
        ])

    def test_comments_and_whitespace(self):
        self.check([
            ("/* header { */\n// note (x) {\nvoid func_80012345(void) /* here */ {\n}", "func_80012345"),
            ("\n\n\t  void   func_80012345 ( void )\n\n{\n}", "func_80012345"),
            ("void func_80012345(a)\n    int a; /* the count */\n    // done\n{\n}", "func_80012345"),
            ("/* void func_80012345(void) { } */\nint D_80012345;", None),
            ("// void func_80012345(void) {", None),
        ])

    def test_not_definitions(self):
        self.check([
            ("typedef struct {\n    s32 m0_x;\n} Foo;", None),
            ("typedef void (*Callback)(s32 arg0);", None),
            ("typedef int func_80012345(int a) {}", None),
            ("struct Foo {\n    s32 m0_x;\n};", None),
            ("struct Foo *func_80012345(void) {\n}", None),
            ("union Bar {\n    s32 m0_word;\n    u8 m0_bytes[4];\n};", None),
            ("extern void func_80012345(void);", None),
            ("extern int func_80012345(int a) {}", None),
            ("void func_80012345(void);", None),
            ("s32 D_80012345[4] = { 1, 2, 3, 4 };", None),
            ('INCLUDE_ASM("asm/us/main/nonmatchings/foo", func_80012345);', None),
            ("#define MAX(a, b) { (a) > (b) ? (a) : (b) }", None),
            ("int func_80012345(a)\nint a;;\n{\n}", None),   # ';;' is no declaration
            ("int func_80012345(a)\nint a;", None),
            ("", None),
        ])

    def test_same_answers_as_the_original_expression(self):
        # Every short text between ')' and the end, over the characters the
        # declaration part treats differently: the old and new expressions
        # must accept the same items and pick the same name.
        alphabet = " \t\na*[];{("
        for length in range(6):
            for tail in itertools.product(alphabet, repeat=length):
                item = "int f(a)" + "".join(tail)
                self.assertEqual(splice.function_name(item), original_name(item), repr(item))

    def test_pathological_declarations_without_brace(self):
        name, seconds = bounded_function_name(pathological(20000))
        self.assertIsNone(name)
        self.assertLess(seconds, FAST)

    def test_pathological_declarations_with_brace(self):
        name, seconds = bounded_function_name(pathological(20000, brace=True))
        self.assertEqual(name, "func_80012345")
        self.assertLess(seconds, FAST)

    def test_reference_on_pathological_declarations(self):
        # The reference itself must stay fast on the input the old regex
        # backtracked on; 200 repetitions were already far beyond re.match.
        for brace, expected in ((False, None), (True, "func_80012345")):
            with self.subTest(brace=brace):
                t = time.perf_counter()
                self.assertEqual(original_name(pathological(200, brace)), expected)
                self.assertLess(time.perf_counter() - t, FAST)

    def test_reference_alternative_order(self):
        # Where several splits match, the reference picks re.match's: the
        # lazy type part stops at the first name that leads to '{', 'static'
        # is taken as the keyword, and ')' is the last one before ';' or '{'.
        for item, name in [
            ("int f(a) int a; {", "f"),
            ("a b c(x) {", "c"),          # 'b' is not followed by '('
            ("static f(x) {", "f"),
            ("staticf g(x) {", "g"),
            ("int f(a)(b) {", "f"),
            ("int f(a) ;; {", None),
            ("int f (a)\n\t\t;\t\t;\n{", "f"),
            ("int f(a)\n\t\t;\t\t;", None),
        ]:
            with self.subTest(item=item):
                self.assertEqual(original_match(item), name)


if __name__ == "__main__":
    unittest.main()
