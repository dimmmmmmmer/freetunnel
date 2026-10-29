#!/usr/bin/env python3
"""Checks that the Android device tests still name what the app has.

The device tests (android/app/src/androidTest) run only on an emulator, 25 to 30 minutes
into Android CI, and they reach into the app by name: a control by its key (the tag that
MainActivity's focus() gives it), a private field or method of MainActivity, AndroidUpdater or
FreeTunnelVpnService by reflection, a string by its resource, a member of the app's classes
by its name. Rename one of them in the app and the test is wrong in a way that neither the
compiler (for the keys and the reflection) nor the quick build job sees. This reads both
sides and reports every name a test uses that the app does not have, so that it fails in
seconds, in the job that builds the app.

It also reads the tests for two mistakes that only show on the device: an onMain block that runs another
(Instrumentation.runOnMainSync throws on the main thread), and a call of eventually() that leaves out the
message a test's own eventually() starts with.

It is a reading of the text, not of the types: a name that is in the app but used for
something else would pass, and each kind of check is a regular expression over the source.
What it catches is the usual way the tests go stale, a name changed on one side.

    check-android-device-tests.py <android directory>        (the one with app/ in it)

Prints each finding and exits 1 if there is any.
"""
import os
import re
import sys

KEY_SHAPE = re.compile(r'^[a-z]+(:[A-Za-z0-9_${}.]+)+$')
# Strings of that shape that are no key of the app's: the tags UiComponentsDeviceTest gives the views it builds for itself
# (b: for a button, t: for a toggle), and a rule that only looks like a key in VpnDeviceTest's data (bad:1).
NOT_KEYS = ('b', 't', 'bad')
STRING = re.compile(r'"((?:\\.|[^"\\\n])*)"')


# The pictures the UI tour must take: the screens the parity is looked at on. A screen it cannot reach is logged by the tour,
# but it may not drop one from its list. A name ending in - is the start of several (one for each state of the update row).
TOUR_SCREENS = (
    'home-disconnected', 'home-empty', 'home-selector-focused', 'home-config-dropdown', 'home-connecting', 'home-connected',
    'home-blocked', 'home-toast', 'nav-focused-tile', 'configs-list', 'configs-row-focused', 'configs-icon-focused',
    'configs-add-menu', 'configs-export-menu', 'configs-delete-confirm', 'configs-empty', 'split-profiles-rules',
    'split-after-recommended', 'split-mode-dropdown', 'split-new-profile-input', 'split-through-vpn-notice', 'app-picker',
    'settings-top', 'settings-theme-dropdown', 'settings-bottom-update-', 'logs', 'logs-off',
    'editor-new', 'editor-edit-focused', 'editor-discard-confirm', 'import-confirm', 'import-replace', 'receive-from-phone')


def read(path):
    with open(path, encoding='utf-8') as handle:
        return handle.read()


def sources(directory, extensions):
    found = {}
    for folder, _, names in os.walk(directory):
        for name in sorted(names):
            if name.endswith(extensions):
                found[os.path.join(folder, name)] = read(os.path.join(folder, name))
    return found


def declared(text, name):
    """Whether text declares a field, method or property called name (Java or Kotlin)."""
    n = re.escape(name)
    return bool(re.search(r'(?:val|var|fun|const val)\s+' + n + r'\b', text)
                or re.search(r'[\w>\]]\s+' + n + r'\s*[;=(,)]', text)
                or re.search(r'\b' + n + r'\s*\(', text))


LITERAL = re.compile(r'"((?:\\.|[^"\\\n])*)"')
WILD = r'[\w.$-]+'


def arities(text, name):
    """The numbers of parameters of the methods called name that text declares (Java or Kotlin)."""
    found = set()
    n = re.escape(name)
    for params in re.findall(r'\bfun\s+(?:<[^>]*>\s*)?(?:[\w.]+\.)?' + n + r'\s*\(([^)]*)\)', text):
        found.add(count(params))
    for params in re.findall(r'[\w>\]]\s+' + n + r'\s*\(([^)]*)\)\s*(?:throws [\w., ]+)?\s*\{', text):
        found.add(count(params))
    return found


def count(params):
    params = params.strip()
    while re.search(r'<[^<>]*>', params):
        params = re.sub(r'<[^<>]*>', '', params)
    return 0 if not params else params.count(',') + 1


def key_templates(text):
    """The strings the app builds, as regular expressions: a literal is itself, and "config:" + index + ":select" is
    config:<anything>:select. A piece of a Kotlin template ($name) is <anything> as well."""
    templates = []
    for line in text.split('\n'):
        if line.lstrip().startswith(('//', '*', '/*')):
            continue
        found = list(LITERAL.finditer(line))
        i = 0
        while i < len(found):
            pieces = [found[i].group(1)]
            if re.search(r'[\w)\]]\s*\+\s*$', line[:found[i].start()]):
                pieces.insert(0, None)
            while i + 1 < len(found):
                between = line[found[i].end():found[i + 1].start()]
                if re.fullmatch(r'\s*\+\s*', between):
                    pieces.append(found[i + 1].group(1))
                elif re.fullmatch(r'\s*\+\s*[\w.\[\]()]+\s*\+\s*', between):
                    pieces.extend([None, found[i + 1].group(1)])
                else:
                    break
                i += 1
            if re.match(r'\s*\+\s*[\w.\[\]()]+', line[found[i].end():]):
                pieces.append(None)
            i += 1
            words = ''.join(p for p in pieces if p)
            if not re.search(r'[A-Za-z]{3}', words):
                continue
            regex = ''
            for piece in pieces:
                if piece is None:
                    regex += WILD
                else:
                    for part in re.split(r'(\$\{[^}]*\}|\$\w+)', piece):
                        regex += WILD if part.startswith('$') else re.escape(part)
            templates.append((re.compile(regex), pieces[0] is None))
    return templates


def fits(literal, templates, words, app_text):
    """Whether a test's key is a shape the app builds. A number or a $name in it stands for a number (the first time) or
    for any word the app uses in a key (a name). A shape that begins with something unknown ("key + ":select"", a row
    that adds its own part to the key it is given) takes any beginning, so the key's first word is asked for as well,
    as the start of some key of the app's."""
    def one(key):
        for template, open_start in templates:
            if template.fullmatch(key):
                if not open_start or ('"%s:' % key.split(':')[0]) in app_text:
                    return True
        return False
    if '$' not in literal:
        return one(literal)
    for candidate in ['0'] + sorted(words):
        if one(re.sub(r'\$\{[^}]*\}|\$\w+', candidate, literal)):
            return True
    return False


def strip_code(source):
    """The Kotlin source with its comments, strings and character literals emptied, so that braces in them count for nothing."""
    out = []
    i = 0
    n = len(source)
    while i < n:
        c = source[i]
        if source.startswith('"""', i):
            j = source.find('"""', i + 3)
            out.append('""')
            i = n if j < 0 else j + 3
        elif c in '"\'':
            j = i + 1
            while j < n and source[j] != c:
                j += 2 if source[j] == '\\' else 1
            out.append(c + c)
            i = j + 1
        elif source.startswith('//', i):
            j = source.find('\n', i)
            i = n if j < 0 else j
        elif source.startswith('/*', i):
            j = source.find('*/', i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def block_after(code, start):
    """The text inside the braces that open at code[start]."""
    depth = 0
    j = start
    while j < len(code):
        if code[j] == '{':
            depth += 1
        elif code[j] == '}':
            depth -= 1
            if depth == 0:
                break
        j += 1
    return code[start + 1:j]


def nested_main(text):
    """Line numbers where a block run on the main thread (onMain, runOnMainSync) runs another, itself or through a helper of
    the test that does. Instrumentation.runOnMainSync throws when it is called on the main thread, and a test that does it
    fails on the device with nothing wrong in the app."""
    code = strip_code(text)
    bodies = {}
    for m in re.finditer(r'\bfun\s+(?:<[^>]*>\s*)?(\w+)\s*\(', code):
        k, depth = m.end(), 1
        while k < len(code) and depth:
            depth += {'(': 1, ')': -1}.get(code[k], 0)
            k += 1
        rest = code[k:k + 3000]
        head = re.match(r'\s*(?::\s*[^={]+)?\s*([={])', rest)
        if not head:
            continue
        if head.group(1) == '{':
            body = block_after(code, k + head.end() - 1)
        else:
            body = rest[head.end():head.end() + 400].split('\n')[0]
        bodies[m.group(1)] = bodies.get(m.group(1), '') + body
    uses = {f for f, b in bodies.items() if re.search(r'\bonMain\s*[{(]|runOnMainSync', b)}
    changed = True
    while changed:
        changed = False
        for f, b in bodies.items():
            if f not in uses and any(re.search(r'\b' + re.escape(g) + r'\s*[({]', b) for g in uses):
                uses.add(f)
                changed = True
    lines = set()
    for m in re.finditer(r'\b(?:onMain|runOnMainSync)\s*\{', code):
        inside = block_after(code, m.end() - 1)
        line = code.count('\n', 0, m.start()) + 1
        if re.search(r'\bonMain\s*[{(]|runOnMainSync', inside) or any(
                g != 'onMain' and re.search(r'(?<![\w.])' + re.escape(g) + r'\s*\(', inside) for g in uses):
            lines.add(line)
    return sorted(lines)


def main(android):
    main_dir = os.path.join(android, 'app/src/main')
    test_dir = os.path.join(android, 'app/src/androidTest')
    app = sources(os.path.join(main_dir, 'java'), ('.java', '.kt'))
    tests = sources(test_dir, ('.kt', '.java'))
    if not app or not tests:
        print('no sources under %s' % android)
        return 1
    by_class = {}
    for path, text in app.items():
        by_class[os.path.splitext(os.path.basename(path))[0]] = text
    app_text = '\n'.join(app.values())
    templates = key_templates(app_text)
    # Every word the app puts between colons in a key: "select", "export", "remove".
    words = set()
    for literal in STRING.findall('\n'.join(app.values())):
        if KEY_SHAPE.match(literal) or re.fullmatch(r'[a-z]+(:[a-z0-9]*)+', literal):
            words.update(w for w in literal.split(':') if re.fullmatch(r'[a-z]+', w))
    strings = read(os.path.join(main_dir, 'res/values/strings.xml'))
    string_names = set(re.findall(r'<string name="([^"]+)"', strings))
    string_names |= set(re.findall(r'<plurals name="([^"]+)"', strings))

    findings = []

    def find(path, line, message):
        findings.append('%s:%d: %s' % (os.path.relpath(path, android), line, message))

    # Which class a reflective helper of a test reaches into. field() is MainActivity's in the TV tests and the
    # updater's in the update tests, which name it by the file.
    helpers = {
        'MainActivity': [r'\bfield\(\s*"(\w+)"\s*\)', r'\bmethod\(\s*"(\w+)"', r'\bprivateMethod\(\s*"(\w+)"',
                         r'MainActivity::class\.java\.getDeclared(?:Field|Method)\(\s*"(\w+)"'],
        'AndroidUpdater': [r'\bupdater\(\s*"(\w+)"', r'AndroidUpdater::class\.java\.getDeclared(?:Field|Method)\(\s*"(\w+)"',
                           r'\bvalue\(\s*"(\w+)"\s*\)'],
        'FreeTunnelVpnService': [r'\bserviceField\(\s*"(\w+)"', r'FreeTunnelVpnService::class\.java\.getDeclared(?:Field|Method)\(\s*"(\w+)"'],
    }
    for path, text in tests.items():
        base = os.path.basename(path)
        lines = text.split('\n')
        if path.endswith('.kt'):
            # A test whose eventually() starts with the message it fails with takes no call without one.
            if re.search(r'fun eventually\(\s*what\s*:\s*String', text):
                for number, line in enumerate(lines, 1):
                    if re.search(r'(?<![\w.])eventually\s*(?:\{|\(\s*(?:\d|\{))', line) and 'fun eventually' not in line:
                        find(path, number, 'eventually() without its message: this test\'s starts with what it waits for')
            for number in nested_main(text):
                find(path, number, 'a block run on the main thread runs another (onMain does not nest: '
                                   'runOnMainSync throws on the main thread)')
        # UpdateDeviceTest's own field(name, value) and value(name) are the updater's.
        patterns = {k: list(v) for k, v in helpers.items()}
        if base == 'UpdateDeviceTest.kt':
            patterns['MainActivity'] = [p for p in patterns['MainActivity'] if not p.startswith(r'\bfield')]
            patterns['AndroidUpdater'].append(r'\bfield\(\s*"(\w+)"\s*,')
        for cls, regexes in patterns.items():
            target = by_class.get(cls, '')
            for regex in regexes:
                for number, line in enumerate(lines, 1):
                    for name in re.findall(regex, line):
                        if not declared(target, name):
                            find(path, number, '%s has no "%s" (a test reaches into it by name)' % (cls, name))

        # A method reached by reflection takes the parameters the test says: method("showConfirm", A::class.java, B::class.java).
        for match in re.finditer(r'\b(?:method|privateMethod)\(\s*"(\w+)"((?:\s*,\s*[^,()]+(?:\([^()]*\))?)*)\s*\)', text):
            name, rest = match.group(1), match.group(2)
            given = 0 if not rest.strip() else rest.count(',')
            have = arities(by_class.get('MainActivity', ''), name)
            if have and given not in have:
                find(path, text.count('\n', 0, match.start()) + 1,
                     'MainActivity.%s takes %s parameters, the test says %d' % (name, '/'.join(str(h) for h in sorted(have)), given))

        # The members of the app's classes a test calls or reads: Class.member( and Class.CONSTANT.
        for number, line in enumerate(lines, 1):
            code = STRING.sub('""', line.split('//')[0])
            for cls, member in re.findall(r'\b([A-Z][A-Za-z0-9]+)\.([a-z][A-Za-z0-9]*)\s*\(', code):
                if cls in by_class and not declared(by_class[cls], member):
                    find(path, number, '%s has no "%s"' % (cls, member))
            for cls, member in re.findall(r'\b([A-Z][A-Za-z0-9]+)\.([A-Z][A-Z0-9_]{2,})\b', code):
                if cls in by_class and not re.search(r'\b' + member + r'\b\s*[=;]|(?:val|const val)\s+' + member + r'\b', by_class[cls]):
                    find(path, number, '%s has no "%s"' % (cls, member))

        # The strings a test reads by resource.
        for number, line in enumerate(lines, 1):
            for name in re.findall(r'\bR\.string\.(\w+)', line):
                if name not in string_names:
                    find(path, number, 'no string resource "%s"' % name)

        # The keys of controls: every string that has a key's shape must be one the app builds, whole. A key made of a fixed
        # part, a number and another fixed part ("config:" + i + ":select") is in the app as that, so a test's "config:0:select"
        # or "config:$i:select" must fit one of the shapes the app builds. A name in it ("config:0:$kind") is tried as each word
        # the app uses in a key. A key asked for in assertNull or assertFalse is one the test says is not there, and need not be.
        for number, line in enumerate(lines, 1):
            if line.lstrip().startswith('//') or 'assertNull(' in line or 'assertFalse(' in line:
                continue
            for literal in STRING.findall(line):
                if not KEY_SHAPE.match(literal) or literal.split(':')[0] in NOT_KEYS:
                    continue
                if not fits(literal, templates, words, app_text):
                    find(path, number, 'no control has the key "%s" (the app builds no key of that shape)' % literal)
    for path, text in tests.items():
        if os.path.basename(path) == 'UiTourDeviceTest.kt':
            for name in TOUR_SCREENS:
                if ('shot("%s' % name) not in text:
                    find(path, 1, 'the tour takes no picture "%s"' % name)
            if 'phone-$name-$language' not in text:
                find(path, 1, 'the tour does not save the phone pages')
    for finding in sorted(set(findings)):
        print(finding)
    return 1 if findings else 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
