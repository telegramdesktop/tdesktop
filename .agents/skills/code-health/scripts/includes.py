"""Include graph over project files: build cost proxies and layering.

Build cost is counted in source lines the compiler reads, not seconds:
`weight` of a translation unit is the lines of project headers it pulls in
beyond its target's precompiled header, and the `rebuild` cost of a header
is the lines recompiled across every unit that depends on it."""
import os
import posixpath

import cpp_text


class IncludeGraph:
    def __init__(self, tree, model):
        self.tree = tree
        self.model = model
        self.styles = {
            posixpath.basename(path)[:-len('.style')]: path
            for path in tree.files if path.endswith('.style')
        }
        self.edges = {}
        self.unresolved = {}
        for path, text in tree.files.items():
            if path.endswith('.style'):
                self.edges[path] = [
                    (number, target)
                    for number, target in style_imports(text, path, tree)
                ]
            elif path.endswith(cpp_text.EXTENSIONS_TU + cpp_text.EXTENSIONS_HEADER):
                resolved = []
                for number, include in cpp_text.includes(text):
                    target = self.resolve(path, include)
                    if target:
                        resolved.append((number, target))
                    else:
                        self.unresolved[include] = self.unresolved.get(include, 0) + 1
                self.edges[path] = resolved
        self.units = sorted(
            path for path in tree.files
            if cpp_text.is_tu(path)
            and (model.target_of(path) or not model.file_target))
        self._closure = {}

    def resolve(self, source, include):
        local = posixpath.normpath(posixpath.join(posixpath.dirname(source), include))
        if local in self.tree.files:
            return local
        own = self.tree.root_of(source)
        roots = ([own] if own else []) + [r for r in self.tree.roots if r != own]
        for root in roots:
            candidate = f'{root}/{include}'
            if candidate in self.tree.files:
                return candidate
        if include.startswith('styles/style_') and include.endswith('.h'):
            return self.styles.get(include[len('styles/style_'):-len('.h')])
        return None

    def closure(self, start):
        cached = self._closure.get(start)
        if cached is not None:
            return cached
        seen = {start}
        stack = [start]
        while stack:
            for _, target in self.edges.get(stack.pop(), ()):
                if target not in seen:
                    seen.add(target)
                    stack.append(target)
        self._closure[start] = frozenset(seen)
        return self._closure[start]

    def pch_closure(self, unit):
        pch = self.model.pch.get(self.model.target_of(unit) or '')
        return self.closure(pch) if pch in self.tree.files else frozenset()

    def costs(self):
        """Per-unit weight, per-header reach and rebuild, and the total."""
        weight = {}
        reach = {}
        rebuild = {}
        total = 0
        for unit in self.units:
            own = self.closure(unit)
            pch = self.pch_closure(unit)
            unit_weight = sum(
                self.tree.lines(path) for path in own - pch if path != unit)
            weight[unit] = unit_weight
            work = self.tree.lines(unit) + unit_weight
            total += work
            for header in (own | pch) - {unit}:
                reach[header] = reach.get(header, 0) + 1
                rebuild[header] = rebuild.get(header, 0) + work
        pch_lines = 0
        for header in set(self.model.pch.values()):
            if header in self.tree.files:
                pch_lines += sum(self.tree.lines(p) for p in self.closure(header))
        return {
            'weight': weight,
            'reach': reach,
            'rebuild': rebuild,
            'total': total + pch_lines,
        }

    def layering(self):
        """Includes that reach a target the including target doesn't link."""
        result = []
        for path, edges in self.edges.items():
            source = self.model.target_of(path)
            if not source:
                continue
            allowed = self.model.dependencies(source)
            for number, target in edges:
                owner = self.model.target_of(target)
                if owner and owner not in allowed:
                    result.append({
                        'path': path,
                        'line': number,
                        'include': target,
                        'from': source,
                        'to': owner,
                    })
        return result


def style_imports(text, path, tree):
    for number, line in enumerate(text.split('\n'), 1):
        stripped = line.strip()
        if stripped.startswith('using "') and stripped.endswith('";'):
            name = stripped[len('using "'):-2]
            for root in tree.roots:
                candidate = f'{root}/{name}'
                if candidate in tree.files:
                    yield number, candidate
                    break
