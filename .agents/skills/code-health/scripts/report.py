"""Snapshot assembly, findings with stable identities, and the report."""
import datetime
import posixpath
import re
import statistics

import atlas
import clones
import cmake_model
import cpp_text
import history
import harness
import includes
import metrics


LIMITS = {
    'file_lines_tu': 4000,
    'file_lines_header': 1500,
    'class_methods': 120,
    'class_fields': 60,
    'function_lines': 300,
    'clone_lines': 20,
    'unit_weight': 60000,
    'header_rebuild_share': 0.05,
    'header_churn': 5,
    'heavy_headers': 30,
}
CLASS_BY_KIND = {
    'unused': 'Q0',
    'large-file': 'Q1',
    'heavy-header': 'Q1',
    'heavy-unit': 'Q1',
    'clone': 'Q2',
    'untested-hot-area': 'Q3',
    'large-class': 'Q4',
    'long-function': 'Q4',
    'layering': 'Q5',
    'stale-map': 'map',
    'unmapped': 'map',
    'stale-atlas': 'map',
    'harness-retention': 'decision',
    'harness-docs': 'decision',
}
MAP_PATH = re.compile(r'`([\w./-]+\.(?:cpp|h|mm|style))`')


def short(path):
    return path.replace('Telegram/SourceFiles/', '')


def file_limit(path):
    return LIMITS['file_lines_header' if cpp_text.is_header(path) else 'file_lines_tu']


def snapshot(tree, queue=None, churn_days=180, cost_days=120):
    sources_atlas = atlas.load(tree)

    def area_of(path):
        return (sources_atlas.area_of(path) if sources_atlas else None) or metrics.area_of(path)

    model = cmake_model.build(tree.files)
    graph = includes.IncludeGraph(tree, model)
    cost = graph.costs()
    pch = set()
    for header in model.pch.values():
        if header in tree.files:
            pch |= graph.closure(header)
    files = metrics.file_metrics(tree)
    changes = history.churn(tree, churn_days)
    tasks = history.agent_cost(tree, queue, cost_days) if queue else {}
    found_clones = clones.find(tree.files)
    layering = graph.layering()
    unused = metrics.unused_candidates(tree)
    tested = model.tested_files()
    harness_data = harness.inspect(tree, queue)

    for path, entry in files.items():
        entry['area'] = area_of(path)
        entry['churn'] = changes.get(path, 0)
        entry['hotspot'] = round(entry['churn'] * entry['lines'] / 1000, 1)
        entry['target'] = model.target_of(path)
        entry['tested'] = path in tested
        if path in cost['weight']:
            entry['weight'] = cost['weight'][path]
        if path in cost['reach']:
            entry['reach'] = cost['reach'][path]
            entry['rebuild'] = cost['rebuild'][path]
            entry['in_pch'] = path in pch

    areas = {}
    for path, entry in files.items():
        area = areas.setdefault(entry['area'], {
            'files': 0, 'lines': 0, 'churn': 0, 'hotspot': 0.0,
            'clone_lines': 0, 'layering': 0, 'unused': 0, 'tested_lines': 0,
        })
        area['files'] += 1
        area['lines'] += entry['lines']
        area['churn'] += entry['churn']
        area['hotspot'] = round(area['hotspot'] + entry['hotspot'], 1)
        if entry['tested']:
            area['tested_lines'] += entry['lines']
    for clone in found_clones:
        for side in ('left', 'right'):
            area = areas.get(area_of(clone[side]['path']))
            if area:
                area['clone_lines'] += clone['lines']
    for violation in layering:
        area = areas.get(area_of(violation['path']))
        if area:
            area['layering'] += 1
    for item in unused:
        area = areas.get(area_of(item['path']))
        if area:
            area['unused'] += 1
    per_area = {}
    for task, entry in tasks.items():
        for area in {area_of(p) for p in entry['paths'] if p in files}:
            per_area.setdefault(area, []).append(entry)
    for area, entries in per_area.items():
        if area in areas:
            areas[area]['tasks'] = len(entries)
            areas[area]['task_hours_median'] = round(
                statistics.median(e['hours'] for e in entries), 1)
            areas[area]['review_rounds_mean'] = round(
                statistics.mean(e['review_rounds'] for e in entries), 2)

    totals = {
        'files': len(files),
        'lines': sum(e['lines'] for e in files.values()),
        'build_work_lines': cost['total'],
        'clone_pairs': len(found_clones),
        'clone_lines': sum(c['lines'] for c in found_clones),
        'layering': len(layering),
        'unused_candidates': len(unused),
        'unused_lines': sum(u['lines'] for u in unused),
        'tested_lines': sum(e['lines'] for e in files.values() if e['tested']),
        'oversized_files': sum(
            1 for p, e in files.items() if e['lines'] > file_limit(p)),
    }
    if tasks:
        totals['tasks'] = len(tasks)
        totals['task_hours_median'] = round(
            statistics.median(t['hours'] for t in tasks.values()), 2)
    totals.update({
        'harness_modules': len(harness_data['modules']),
        'harness_lines': sum(row['source_lines'] for row in harness_data['modules']),
        'harness_reference_bytes': harness_data['reference_bytes'],
    })
    if harness_data['readme_bytes'] is not None:
        totals['harness_readme_bytes'] = harness_data['readme_bytes']
    if (harness_data['coverage']['available']
            and not harness_data['coverage']['shallow_source_history']):
        totals['harness_review_candidates'] = sum(
            bool(row['review_reasons']) for row in harness_data['modules'])
    return {
        'schema_version': 1,
        'ref': tree.ref,
        'commit': tree.commit,
        'checked_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'notes': tree.notes,
        'limits': LIMITS,
        'totals': totals,
        'areas': areas,
        'files': files,
        'clones': found_clones,
        'layering': layering,
        'unused': unused,
        'tasks': tasks,
        'maps': map_references(tree),
        'atlas': atlas.check(sources_atlas, tree) if sources_atlas else None,
        'harness': harness_data,
    }


def map_references(tree):
    """Area maps and the source paths they name that no longer exist. The
    atlas is left out: its patterns are checked as stale-atlas findings."""
    result = {}
    for path, text in tree.files.items():
        if not path.endswith('/AGENTS.md') or path == atlas.ATLAS_PATH:
            continue
        folder = posixpath.dirname(path)
        missing = []
        for name in sorted(set(MAP_PATH.findall(text))):
            candidates = [f'{folder}/{name}', f'Telegram/SourceFiles/{name}', name]
            if not any(candidate in tree.files for candidate in candidates):
                missing.append(name)
        result[path] = {'missing': missing}
    return result


def findings(snap):
    """Stable-id findings with a crude priority score; churn multiplies it,
    because code nobody changes costs nothing to leave alone."""
    files = snap['files']
    total = snap['totals']['build_work_lines'] or 1
    result = {}

    def area_of(path):
        return files[path]['area'] if path in files else metrics.area_of(path)

    def heat(*paths):
        return 1 + sum(files.get(p, {}).get('churn', 0) for p in paths) / 10

    def add(kind, key, title, paths, impact, data):
        identity = f'{kind}:{key}'
        result[identity] = {
            'id': identity,
            'kind': kind,
            'class': CLASS_BY_KIND[kind],
            'area': area_of(paths[0]) if paths else key,
            'title': title,
            'paths': paths,
            'score': round(impact * heat(*paths), 1),
            'data': data,
        }

    for path, entry in files.items():
        if entry['lines'] > file_limit(path):
            add('large-file', path, f'{path} has {entry["lines"]} lines',
                [path], entry['lines'] / 1000, {'lines': entry['lines']})
        for item in entry['classes']:
            if (item['methods'] >= LIMITS['class_methods']
                    or item['fields'] >= LIMITS['class_fields']):
                add('large-class', f'{path}:{item["name"]}',
                    f'class {item["name"]}: {item["methods"]} methods, {item["fields"]} fields',
                    [path], item['methods'] / 10, item)
        for item in entry['long_functions']:
            if item['lines'] >= LIMITS['function_lines']:
                add('long-function', f'{path}:{item["name"]}',
                    f'{item["name"]} is {item["lines"]} lines',
                    [path], item['lines'] / 100, item)
        if entry.get('weight', 0) >= LIMITS['unit_weight']:
            add('heavy-unit', path,
                f'{path} compiles {entry["weight"]} header lines beyond its PCH',
                [path], entry['weight'] / 20000, {'weight': entry['weight']})

    headers = [
        (path, entry) for path, entry in files.items()
        if cpp_text.is_header(path) and not entry.get('in_pch')
        and entry.get('rebuild', 0) / total >= LIMITS['header_rebuild_share']
        and entry['churn'] >= LIMITS['header_churn']
    ]
    headers.sort(key=lambda kv: -kv[1]['rebuild'] * kv[1]['churn'])
    for path, entry in headers[:LIMITS['heavy_headers']]:
        share = entry['rebuild'] / total
        add('heavy-header', path,
            f'{path}: {entry["churn"]} edits, each recompiling '
            f'{entry["reach"]} units ({share:.0%} of build work)',
            [path], share * 10, {
                'reach': entry['reach'],
                'rebuild': entry['rebuild'],
                'share': round(share, 4),
            })

    grouped = {}
    for clone in snap['clones']:
        if clone['lines'] >= LIMITS['clone_lines']:
            grouped.setdefault(clone['id'], []).append(clone)
    for identity, pairs in grouped.items():
        places = []
        for pair in pairs:
            for side in ('left', 'right'):
                place = pair[side]
                if place not in places:
                    places.append(place)
        paths = sorted({place['path'] for place in places})
        where = ', '.join(
            f'{short(place["path"])}:{place["start"]}' for place in places[:3])
        add('clone', identity,
            f'{pairs[0]["lines"]} lines repeated in {len(places)} places: {where}',
            paths, pairs[0]['lines'] * (len(places) - 1) / 10,
            {'lines': pairs[0]['lines'], 'places': places})

    by_file = {}
    for violation in snap['layering']:
        key = (violation['path'], violation['from'], violation['to'])
        by_file.setdefault(key, []).append(violation)
    for (path, source, target), items in by_file.items():
        add('layering', f'{source}->{target}:{path}',
            f'{path} ({source}) includes {len(items)} header(s) of {target}',
            [path], len(items), {
                'includes': [
                    {'line': v['line'], 'include': v['include']} for v in items
                ],
            })

    for item in snap['unused']:
        add('unused', f'{item["path"]}:{item["name"]}',
            f'{item["name"]} ({item["lines"]} lines) looks unused',
            [item['path']], 1 + item['lines'] / 20, item)

    for path, data in snap.get('maps', {}).items():
        if data['missing']:
            add('stale-map', path,
                f'{path} names {len(data["missing"])} missing file(s): '
                + ', '.join(data['missing'][:5]),
                [path], 5, data)

    checked = snap.get('atlas') or {}
    folders = {}
    for path in checked.get('unmapped', ()):
        folders.setdefault(posixpath.dirname(path), []).append(path)
    for folder, paths in folders.items():
        add('unmapped', folder,
            f'{len(paths)} file(s) in {folder} match no atlas area',
            paths, 5, {'files': paths[:20]})
    for name, pattern in checked.get('unused', ()):
        add('stale-atlas', f'{name}:{pattern}',
            f'atlas area {name} names `{pattern}`, which matches no file',
            [atlas.ATLAS_PATH], 5, {'area': name, 'pattern': pattern})

    hot = sorted(snap['areas'].items(), key=lambda kv: -kv[1]['hotspot'])[:20]
    for area, data in hot:
        if data['tested_lines'] == 0 and data['hotspot'] > 0:
            add('untested-hot-area', area,
                f'area {area} ({data["lines"]} lines, hotspot {data["hotspot"]}) '
                f'has no headless test', [], data['hotspot'] / 100, {
                    'lines': data['lines'],
                    'hotspot': data['hotspot'],
                })
            result[f'untested-hot-area:{area}']['area'] = area
    harness_data = snap.get('harness', {})
    for row in harness_data.get('modules', ()):
        if row['review_reasons']:
            add('harness-retention', row['module'],
                row['module'] + ': ' + '; '.join(row['review_reasons']),
                row['paths'], row['source_lines'] / 1000, row)
    if harness_data.get('modules') and (
            harness_data['readme_bytes'] is None
            or harness_data['readme_bytes'] >= harness.README_LIMIT
            or harness_data['missing_references']):
        add('harness-docs', harness.README,
            'Harness documentation exceeds its guide budget or lacks indexed references',
            [harness.README], 1, {
                'readme_bytes': harness_data['readme_bytes'],
                'limit': harness.README_LIMIT,
                'missing_references': harness_data['missing_references'],
            })
    return result


def merge(previous, current, checked_at, unavailable=()):
    """Carry first-seen dates and mark vanished findings resolved."""
    ledger = {}
    old = previous.get('findings', {}) if previous else {}
    for identity, finding in current.items():
        prior = old.get(identity, {})
        finding['first_seen'] = prior.get('first_seen', checked_at)
        finding['status'] = 'open'
        ledger[identity] = finding
    cutoff = (datetime.datetime.fromisoformat(checked_at)
              - datetime.timedelta(days=180)).isoformat()
    for identity, finding in old.items():
        if identity in ledger:
            continue
        if finding.get('kind') in unavailable and finding.get('status') != 'resolved':
            ledger[identity] = dict(finding, status=(
                'ignored' if finding.get('status') == 'ignored' else 'unverified'))
            continue
        if finding.get('status') in ('open', 'ignored', 'unverified'):
            finding = dict(finding, status='resolved', resolved_at=checked_at)
        if finding.get('resolved_at', checked_at) >= cutoff:
            ledger[identity] = finding
    return ledger


def markdown(snap, ledger, previous):
    totals = snap['totals']
    before = (previous or {}).get('totals', {})
    out = [
        f'# Code health — {snap["checked_at_utc"][:10]}',
        '',
        f'`{snap["ref"]}` at `{snap["commit"][:12]}`'
        + ''.join(f'; {note}' for note in snap['notes']) + '.',
        '',
        '## Totals',
        '',
        '| Metric | Now | Change |',
        '|---|---|---|',
    ]
    labels = [
        ('lines', 'Source lines'),
        ('build_work_lines', 'Build work (lines compiled)'),
        ('oversized_files', 'Files over the size limit'),
        ('clone_lines', 'Duplicated lines (clone pairs)'),
        ('layering', 'Layering violations'),
        ('unused_candidates', 'Unused-function candidates'),
        ('tested_lines', 'Lines in headless test targets'),
        ('task_hours_median', 'Median task hours (Start→Approve)'),
        ('harness_modules', 'Harness modules'),
        ('harness_lines', 'Harness source lines'),
        ('harness_readme_bytes', 'Mandatory harness README bytes'),
        ('harness_reference_bytes', 'On-demand harness documentation bytes'),
        ('harness_review_candidates', 'Harness retention review candidates'),
    ]
    for key, label in labels:
        if key not in totals:
            continue
        now = totals[key]
        change = ''
        if key in before:
            delta = now - before[key]
            change = f'{delta:+,}' if isinstance(delta, int) else f'{delta:+.2f}'
        out.append(f'| {label} | {now:,} | {change} |')

    open_findings = [f for f in ledger.values() if f['status'] == 'open']
    new = [f for f in open_findings if f['first_seen'] == snap['checked_at_utc']]
    resolved = [
        f for f in ledger.values()
        if f['status'] == 'resolved' and f.get('resolved_at') == snap['checked_at_utc']
    ]
    if previous:
        out += ['', f'## Since the previous run', '',
                f'{len(new)} new, {len(resolved)} resolved.', '']
        for finding in sorted(new, key=lambda f: -f['score'])[:15]:
            out.append(f'- new [{finding["class"]}] {finding["title"]}')
        for finding in resolved[:15]:
            out.append(f'- resolved [{finding["class"]}] {finding["title"]}')

    out += ['', '## Areas by hotspot', '',
            '| Area | Lines | Churn | Hotspot | Clone lines | Layering | Unused | Tested | Tasks | Median h | Review rounds |',
            '|---|---|---|---|---|---|---|---|---|---|---|']
    hot = sorted(snap['areas'].items(), key=lambda kv: -kv[1]['hotspot'])[:25]
    for area, data in hot:
        tested = f'{data["tested_lines"] / data["lines"]:.0%}' if data['lines'] else ''
        out.append(
            f'| {area} | {data["lines"]:,} | {data["churn"]} | {data["hotspot"]:,} '
            f'| {data["clone_lines"]} | {data["layering"]} | {data["unused"]} '
            f'| {tested} | {data.get("tasks", "")} | {data.get("task_hours_median", "")} '
            f'| {data.get("review_rounds_mean", "")} |')

    out += ['', '## Top findings', '',
            'Score is impact × churn; class is the suggested task class.', '',
            '| Score | Class | Finding |', '|---|---|---|']
    for finding in sorted(open_findings, key=lambda f: -f['score'])[:40]:
        out.append(f'| {finding["score"]} | {finding["class"]} | {finding["title"]} |')

    counts = {}
    for finding in open_findings:
        counts[finding['kind']] = counts.get(finding['kind'], 0) + 1
    out += ['', '## Open findings by kind', '']
    for kind, count in sorted(counts.items()):
        out.append(f'- {kind}: {count}')

    headers = sorted(
        ((p, e) for p, e in snap['files'].items()
         if 'rebuild' in e and not e.get('in_pch') and cpp_text.is_header(p)),
        key=lambda kv: -kv[1]['rebuild'] * kv[1]['churn'])[:12]
    total = totals['build_work_lines'] or 1
    out += ['', '## Build cost', '',
            'Headers outside every precompiled header, by rebuild work their '
            'edits caused (share of build work × edits in 180 days):', '',
            '| Header | Edits | Units | Share per edit |', '|---|---|---|---|']
    for path, entry in headers:
        out.append(f'| {short(path)} | {entry["churn"]} | {entry["reach"]} '
                   f'| {entry["rebuild"] / total:.0%} |')
    units = sorted(
        ((p, e) for p, e in snap['files'].items() if 'weight' in e),
        key=lambda kv: -kv[1]['weight'])[:8]
    out += ['', 'Heaviest units (header lines compiled beyond the PCH):', '']
    for path, entry in units:
        out.append(f'- {short(path)}: {entry["weight"]:,}')
    if 'harness' in snap:
        coverage = snap['harness']['coverage']
        out += ['', '## Harness lifecycle', '',
                'See [the helper inventory](harness.md) and [consumer evidence](harness.json). '
                'Retention findings are review signals, not Q0 deletion proof.',
                'Usage/age coverage: ' + ('available' if coverage['available']
                                         and not coverage['shallow_source_history']
                                      else 'unavailable; prior retention findings are unverified') + '.']
    return '\n'.join(out) + '\n'
