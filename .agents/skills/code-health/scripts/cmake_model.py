"""Targets, their sources, link dependencies and precompiled headers, read
from the CMake lists without running CMake."""
import re


CALL = re.compile(
    r'\b(nice_target_sources|target_link_libraries|target_precompile_headers'
    r'|add_library|add_executable)\s*\(', re.I)
SCOPES = {'PRIVATE', 'PUBLIC', 'INTERFACE'}
NAMESPACES = ('desktop-app::', 'tdesktop::')


class CMakeModel:
    def __init__(self):
        self.file_target = {}
        self.sources = {}
        self.links = {}
        self.pch = {}
        self.executables = set()
        self.libraries = set()
        self._closure = {}

    def target_of(self, path):
        return self.file_target.get(path)

    def dependencies(self, target):
        """The target itself and everything it links, transitively."""
        cached = self._closure.get(target)
        if cached is not None:
            return cached
        result = {target}
        stack = [target]
        while stack:
            for linked in self.links.get(stack.pop(), ()):
                if linked not in result:
                    result.add(linked)
                    stack.append(linked)
        self._closure[target] = result
        return result

    def tests(self):
        return {name for name in self.executables if 'test' in name}

    def tested_files(self):
        """Sources of test executables and of project libraries they link
        directly; a toolkit library linked for support does not count."""
        result = set()
        for test in self.tests():
            result.update(self.sources.get(test, ()))
            for target in self.links.get(test, ()):
                if target.startswith('td_'):
                    result.update(self.sources.get(target, ()))
        return result


def base_for(cmake_path):
    parts = cmake_path.split('/')
    if len(parts) >= 2 and parts[1].startswith('lib_'):
        return '/'.join(parts[:2])
    return 'Telegram/SourceFiles'


def calls(text):
    text = re.sub(r'#[^\n]*', '', text)
    for match in CALL.finditer(text):
        depth = 1
        position = match.end()
        while position < len(text) and depth:
            if text[position] == '(':
                depth += 1
            elif text[position] == ')':
                depth -= 1
            position += 1
        yield match.group(1).lower(), text[match.end():position - 1].split()


def local_name(token):
    for prefix in NAMESPACES:
        if token.startswith(prefix):
            return token[len(prefix):]
    return token


def build(files):
    model = CMakeModel()
    cmake_files = sorted(
        path for path in files
        if path.endswith('.cmake') or path.endswith('/CMakeLists.txt'))
    for path in cmake_files:
        base = base_for(path)
        for name, tokens in calls(files[path]):
            if not tokens:
                continue
            target = local_name(tokens[0])
            if name == 'add_library':
                if 'ALIAS' not in tokens:
                    model.libraries.add(target)
            elif name == 'add_executable':
                model.executables.add(target)
            elif name == 'target_link_libraries':
                model.links.setdefault(target, set()).update(
                    local_name(token) for token in tokens[1:]
                    if token not in SCOPES
                    and not token.startswith(('-', '$'))
                    and '"' not in token)
            elif name == 'target_precompile_headers':
                for token in tokens[1:]:
                    found = re.search(r'\$\{src_loc\}/([^>\s]+\.h)', token)
                    if found:
                        model.pch[target] = f'{base}/{found.group(1)}'
            elif name == 'nice_target_sources' and len(tokens) > 1:
                if tokens[1] != '${src_loc}':
                    continue
                for token in tokens[2:]:
                    if token in SCOPES or '$' in token:
                        continue
                    source = f'{base}/{token}'
                    model.sources.setdefault(target, set()).add(source)
                    model.file_target.setdefault(source, target)
    return model
