#!/usr/bin/env python3
"""Regenerate src/ssl/IntOpcodes.inc from a fallout2-ce checkout."""
#
# The compiled Fallout 2 script (.int) opcode names are not shipped game data; the engine is the only
# source of truth. This walks the same registrations the interpreter performs at startup:
#
#   * interpreter.h            - core VM opcodes (OPCODE_* enum, 0x8000..0x804B)
#   * interpreter_extra.cc     - game opcodes, named by the original op_* comment on each registration
#   * interpreter_lib.cc       - window/sound/movie opcodes
#   * sfall_opcodes.cc         - sfall extensions
#
# A registration without an op_* comment keeps the engine's handler identifier. The dispatcher indexes
# its table with (opcode & 0x3FF), so the table is keyed the same way; two registrations landing on one
# index with different names are a hard error rather than a silent pick.
#
# Each entry also carries the handler's stack effect, read from its body: how many values it pops off
# the data stack and whether it pushes a result. A pop inside `for (int arg = 0; arg < N; arg++)`
# counts N times; any other loop around a pop, or a call that hands `program` to a function not known
# to leave the data stack alone, makes the effect unknown (-1). A handler pushes at most one value on
# any path (alternative branches push alternatives), so pushes is 0 or 1. Of the core VM opcodes only
# the expression operators get an effect: control flow (IF, JUMP, CALL, the returns) and the stack
# shuffles (DUP pushes twice, SWAP) stay unknown.
#
# Run it from the root of the fallout2-ce checkout; it takes no arguments:
#   cd /path/to/fallout2-ce && python3 /path/to/gecko/tools/gen_int_opcodes.py > /path/to/gecko/src/ssl/IntOpcodes.inc

import re
import sys
from pathlib import Path

# The only files read, relative to the checkout's src/ directory.
REGISTRATION_SOURCES = ('interpreter_extra.cc', 'interpreter_lib.cc', 'sfall_opcodes.cc')
CORE_OPCODES_SOURCE = 'interpreter.h'
CORE_HANDLERS_SOURCE = 'interpreter.cc'

# Functions handlers pass `program` to that do not touch the data stack.
NON_STACK_CALLS = {
    'scriptPredefinedError', 'programGetString', 'programGetIdentifier', 'scriptWindowSelectId',
    'scriptGetSelf', 'scriptGetSid', 'scriptContextResolve', 'scriptContextConsumeOverrideSelf',
    'scriptContextSetOverrideSelf', 'scriptContextSetReturnValue', 'scriptContextGetLocalVar',
    'scriptContextSetLocalVar', 'internal_free_safe', 'interpreterStringRefCountIncrease',
    'interpreterStringRefCountDecrease', 'gameDialogSetTextReply', 'gameDialogSetMessageReply',
    'criticalTableArgsAreValid', 'sfall_gl_scr_is_global_script', 'sfall_gl_scr_is_loaded',
    'sfall_gl_scr_set_repeat', 'sfall_gl_scr_set_type', 'programReturnStackPopInteger',
    'programReturnStackPopPointer', 'programReturnStackPopValue', 'programReturnStackPushValue',
    'programReturnStackPushInteger',
}

# The core VM opcodes that compute a value from the stack (sslc's expression code).
CORE_EXPRESSION_OPCODES = {
    'OPCODE_ADD', 'OPCODE_SUB', 'OPCODE_MUL', 'OPCODE_DIV', 'OPCODE_MOD', 'OPCODE_EQUAL',
    'OPCODE_NOT_EQUAL', 'OPCODE_LESS_THAN_EQUAL', 'OPCODE_GREATER_THAN_EQUAL', 'OPCODE_LESS_THAN',
    'OPCODE_GREATER_THAN', 'OPCODE_AND', 'OPCODE_OR', 'OPCODE_NOT', 'OPCODE_NEGATE', 'OPCODE_BITWISE_NOT',
    'OPCODE_FLOOR', 'OPCODE_BITWISE_AND', 'OPCODE_BITWISE_OR', 'OPCODE_BITWISE_XOR', 'OPCODE_FETCH',
    'OPCODE_FETCH_GLOBAL', 'OPCODE_FETCH_EXTERNAL', 'OPCODE_FETCH_PROCEDURE_ADDRESS',
}

POP = re.compile(r'programStackPop\w*(?:<[^>]*>)?\(program\b')
PUSH = re.compile(r'programStackPush\w*(?:<[^>]*>)?\(program\b')
ARG_LOOP = re.compile(r'for \(int (\w+) = 0; \1 < (\d+); \1\+\+\) \{')
CALL_WITH_PROGRAM = re.compile(r'(\w+)(?:<[^>]*>)?\(program\s*[,)]')


def handler_bodies(text):
    """Map each `void name(Program* program)` defined in `text` to its body."""
    bodies = {}
    for match in re.finditer(r'\n(?:static )?void (\w+)\(Program\* program\)\s*\{', text):
        end, depth = match.end(), 1
        while depth:
            depth += {'{': 1, '}': -1}.get(text[end], 0)
            end += 1
        # Comments out, so their words ("for float values") don't read as code.
        body = re.sub(r'//[^\n]*|/\*.*?\*/', '', text[match.end():end - 1], flags=re.S)
        bodies[match.group(1)] = body
    return bodies


def block_end(text, start):
    """The index just past the block whose opening brace ends at `start`."""
    end, depth = start, 1
    while depth:
        depth += {'{': 1, '}': -1}.get(text[end], 0)
        end += 1
    return end


def stack_effect(body):
    """(pops, pushes) of a handler body, or (-1, -1) when it can't be read off the source."""
    if body is None:
        return -1, -1
    for call in CALL_WITH_PROGRAM.findall(body):
        if not call.startswith(('programStackPop', 'programStackPush')) and call not in NON_STACK_CALLS:
            return -1, -1
    pops, rest = 0, body
    for loop in ARG_LOOP.finditer(body):
        end = block_end(body, loop.end())
        pops += int(loop.group(2)) * len(POP.findall(body, loop.end(), end))
        rest = rest.replace(body[loop.start():end], '')
    if POP.search(rest) and re.search(r'\b(for|while|do)\b', rest):
        return -1, -1
    pops += len(POP.findall(rest))
    return pops, 1 if PUSH.search(body) else 0


def checkout_root():
    """Return the current directory, which must be the root of a fallout2-ce checkout."""
    root = Path.cwd()
    if not (root / 'src' / CORE_OPCODES_SOURCE).is_file():
        sys.exit(f'run this from the root of a fallout2-ce checkout ({root} has no src/{CORE_OPCODES_SOURCE})')
    return root


def read_source(root, name):
    """Read one of the known engine sources, which must resolve inside the checkout."""
    path = (root / 'src' / name).resolve(strict=True)
    if not path.is_relative_to(root.resolve()):
        sys.exit(f'src/{name} resolves outside {root}')
    return path.read_text(encoding='utf-8')


def head_commit(root):
    """Return the checkout's abbreviated HEAD commit, read from .git rather than by running git."""
    git_dir = root / '.git'
    if not git_dir.is_dir():
        return 'unknown'
    head = (git_dir / 'HEAD').read_text(encoding='utf-8').strip()
    if not head.startswith('ref: '):
        return head[:8]
    ref = head[len('ref: '):]
    ref_file = git_dir / ref
    if ref_file.is_file():
        return ref_file.read_text(encoding='utf-8').strip()[:8]
    packed = git_dir / 'packed-refs'
    if packed.is_file():
        for line in packed.read_text(encoding='utf-8').splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[1] == ref:
                return parts[0][:8]
    return 'unknown'


def main():
    """Print the opcode table for the fallout2-ce checkout in the current directory."""
    if len(sys.argv) > 1:
        sys.exit('usage: run from the root of a fallout2-ce checkout, with no arguments')
    root = checkout_root()
    entries = {}

    def add(index, name, effect):
        if index in entries and entries[index][0] != name:
            sys.exit(f'opcode index 0x{index:03X} registered as both {entries[index][0]} and {name}')
        entries.setdefault(index, (name, effect))

    core_text = read_source(root, CORE_HANDLERS_SOURCE)
    core_bodies = handler_bodies(core_text)
    core_handlers = dict(re.findall(r'interpreterRegisterOpcode\((OPCODE_\w+),\s*(\w+)\);', core_text))
    for name, value in re.findall(r'(OPCODE_\w+)\s*=\s*(0x[0-9A-Fa-f]+)', read_source(root, CORE_OPCODES_SOURCE)):
        if int(value, 16) & 0x8000:
            effect = (-1, -1)
            if name in CORE_EXPRESSION_OPCODES:
                effect = stack_effect(core_bodies.get(core_handlers.get(name)))
            add(int(value, 16) & 0x3FF, name, effect)

    registration = re.compile(r'interpreterRegisterOpcode\((0x[0-9A-Fa-f]+),\s*(\w+)\);[ \t]*(?://[ \t]*([a-z_0-9]+))?')
    for source in REGISTRATION_SOURCES:
        bodies = handler_bodies(read_source(root, source))
        for value, handler, comment in registration.findall(read_source(root, source)):
            add(int(value, 16) & 0x3FF, comment or handler, stack_effect(bodies.get(handler)))

    print('// Generated by tools/gen_int_opcodes.py from fallout2-ce ' + head_commit(root) + ' - do not edit by hand.')
    print('// { index = opcode & 0x3FF, engine name, values popped, values pushed (-1: unknown) }.')
    print('// See the script for the sources it walks.')
    for index, (name, (pops, pushes)) in sorted(entries.items()):
        print(f'{{ 0x{index:03X}, "{name}", {pops}, {pushes} }},')


if __name__ == '__main__':
    main()
