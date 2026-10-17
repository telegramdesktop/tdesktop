# ELF execution-copy evidence

`elf_identity.py` compares an original Debug ELF with one execution copy in that
direction. A successful JSON report proves the artifact relationship below for
the two acquired files and reports their separate, unmasked whole-file SHA-256
identities. It does not prove authenticity, safe execution, successful startup,
dynamic-library identity, or a general linker-equivalence theorem. It neither
strips nor executes either input, and needs no Telegram build, launch, account,
elevation or external ELF parser.

## Supported profile

The host must provide Linux/WSL `pread`, `fcntl`, `O_NOFOLLOW`, `O_NONBLOCK` and
`O_CLOEXEC` semantics. Inputs must be ordinary regular files. Missing paths,
final-component symlinks, directories, devices and FIFOs refuse; the helper does
not block waiting for a FIFO writer. Parent-directory symlinks are resolved by
the operating system; this is not a sandbox or a path-component trust check.

The profile is `ELF64-LE-x86_64-ordinary-exec-dyn-v1`:

- ELF64, little-endian, x86-64 machine 62, ET_EXEC (2) or ET_DYN (3), ELF
  versions 1, System V (0) or GNU (3) OS ABI, ABI version 0, zero reserved
  identity bytes and zero `e_flags`.
- ELF/program/section header sizes exactly 64/56/64 bytes. Both tables must
  exist, have ordinary nonzero counts, start at an 8-byte-aligned offset at or
  after 64, fit the file and not overlap each other or other declared file
  regions. Section 0 is entirely zero. Sectionless files, PN_XNUM, SHN_XINDEX,
  extended counts and additional null sections are unsupported.
- Program types: NULL, LOAD, DYNAMIC, INTERP, NOTE, PHDR, TLS, GNU_EH_FRAME,
  GNU_STACK, GNU_RELRO and GNU_PROPERTY. Only R/W/X flags are supported.
  NULL records are entirely zero; LOAD records have nonempty file payloads,
  ordered disjoint rounded virtual ranges and disjoint declared file ranges,
  are congruent at the profile's 4096-byte page size, and contain a file-backed
  executable entry point. Every rounded file page must be fully backed by EOF
  in each input; partial EOF pages and page-rounding overflow refuse. Rounded
  file pages may alias at distinct virtual addresses. Sizes/addresses must not
  overflow or exceed their extent, and `filesz <= memsz` except for NOTE.
  Alignments are zero or powers of two with file/address congruence.
- Non-NULL/non-LOAD/non-NOTE program types are unique. PHDR and INTERP precede
  the first LOAD. PHDR describes the actual program table. INTERP is one
  nonempty absolute NUL-terminated path with no interior NUL; its two sizes
  match. DYNAMIC has nonzero equal file/memory sizes divisible by 16 and a
  matching SHT_DYNAMIC section. GNU_STACK has no file or address payload.
  PHDR, INTERP, DYNAMIC, GNU_EH_FRAME, GNU_RELRO and TLS initial data require
  compatible LOAD mappings. NOTE and GNU_PROPERTY may have payloads outside
  LOAD; those bytes remain protected.
- Section types: PROGBITS, SYMTAB, STRTAB, RELA, HASH, DYNAMIC, NOTE, NOBITS,
  REL, DYNSYM, INIT_ARRAY, FINI_ARRAY, PREINIT_ARRAY, RELR, GNU_ATTRIBUTES,
  GNU_HASH, GNU_VERDEF, GNU_VERNEED and GNU_VERSYM. GROUP, SYMTAB_SHNDX and
  unknown type/reference encodings refuse. Allowed flags are WRITE, ALLOC,
  EXECINSTR, MERGE, STRINGS, INFO_LINK, LINK_ORDER, TLS, COMPRESSED and
  GNU_RETAIN. Unsupported flag bits refuse.
- Fixed entry sizes are 24 for SYMTAB/DYNSYM/RELA, 16 for REL/DYNAMIC, 4 for
  HASH, 8 for INIT_ARRAY/FINI_ARRAY/PREINIT_ARRAY/RELR, and 2 for GNU_VERSYM.
  STRTAB/NOTE/NOBITS/GNU_ATTRIBUTES/GNU_HASH/GNU_VERDEF/GNU_VERNEED require
  zero entry size. PROGBITS may declare an entry size; noncompressed sizes
  must be divisible by any nonzero entry size.
- Nonempty file-backed sections cannot overlap each other or either header
  table/ELF header. Allocated sections require compatible LOAD permissions
  and file/address mappings. Nonallocated sections have address zero. Section
  address and file alignment must agree with their declared power-of-two
  alignment (NOBITS has no file-payload alignment check).
- Ordinary NOBITS lies in a unique LOAD zero-fill range; its conceptual file
  offset lies between the end of that LOAD's file payload and the address-
  derived conceptual offset, and cannot exceed EOF. TLS sections are allocated
  PROGBITS/NOBITS inside the single TLS template, with compatible alignment.
  TLS NOBITS uses TLS zero-fill/conceptual offsets. Ordinary allocated address
  ranges cannot overlap each other; TLS ranges cannot overlap each other, but
  TLS BSS may overlap ordinary allocated addresses. Empty sections are allowed.
- Each nonnull section has a unique, nonempty raw byte name. The section-name
  table is an ordinary nonallocated STRTAB with address/link/info/entry size
  zero and alignment zero or one. Its first/final bytes are NUL. Every name
  index and terminator must be valid. Names are matched as bytes, not decoded
  text. All nonempty STRTAB sections require first/final NUL bytes.
- `sh_link` is a validated section index only for the supported linked types
  or LINK_ORDER; its target type is checked where defined. Otherwise it must
  be zero. `sh_info` is a section index for REL/RELA and INFO_LINK, and a
  numeric value for SYMTAB/DYNSYM/GNU_VERDEF/GNU_VERNEED. Numeric fields must
  not set INFO_LINK. Other nonzero auxiliary information refuses. Supported
  references compare by resolved raw identity, preserving numeric meanings.
  Symbol/dynamic tables are unique per type. GNU_VERSYM count matches DYNSYM.

LOAD coverage follows an explicit Linux x86-64 4096-byte mapping profile. For
`filesz == memsz`, exposed file bytes run from the page floor of `p_offset`
through the page ceiling of `p_offset + p_filesz`, including prefix and tail.
For `memsz > filesz`, PF_W is required. A nonfinal BSS LOAD must end its memory
range on a page boundary; a final writable BSS LOAD may end inside a page.
These BSS mappings expose file bytes from the page floor through the declared
file end, and zero-fill virtual bytes from `vaddr + filesz` through the rounded
memory end. Read-only BSS, nonfinal partial-page BSS, empty LOAD file payloads,
partial EOF pages and rounded virtual overlaps explicitly refuse. This avoids
kernel-version-dependent nonfinal BSS padding behavior without emulating a
loader. TLS template and NOBITS rules above remain separate semantic checks.

Each LOAD's exposure is calculated before merging file intervals. A BSS
mapping never subtracts another LOAD's exposed file bytes, including a shared
rounded file page. The same merged exposure plus independent record payloads
governs bookkeeping placement, debug/static classification, exact comparison,
zero-gap accounting and the report; virtual zero-fill is never read from disk
or materialized in a buffer.

This validates a deliberately narrow structural profile. It does not interpret
relocations, dynamic tags, note contents, DWARF, or loader policy. Unknown
nonallocated PROGBITS names are supported as protected content; an unknown ELF
section type is a refusal, not an invitation to guess its reference semantics.

## Exact protected content and directional exceptions

All ordered program-header fields are exact: type, flags, file offset, virtual
and physical address, file size, memory size and alignment. Their complete
union of qualified exposed file pages and independent record payloads is
compared byte for byte, including exposed prefixes, tails, loaded padding and
non-LOAD payload outside LOAD exposure. Overlapping program coverage is traversed once
as a union. The program header table receives no masks.

Both complete inputs must validate before the only ELF-header exception can
produce success. The section table and section-name table must lie outside
every loader-visible payload. Exactly these absolute ELF-header bytes may differ:

| Field | Half-open byte range | Width |
| --- | --- | --- |
| `e_shoff` | [40, 48) | 8 |
| `e_shnum` | [60, 62) | 2 |
| `e_shstrndx` | [62, 64) | 2 |

Every nonempty non-NULL/non-LOAD payload must avoid all three mutable ranges;
any intersection refuses in either input, even if the two payloads match. This
includes INTERP, NOTE, PHDR, TLS, DYNAMIC and supported GNU payload records.
Their independently interpreted bytes cannot inherit a header-field exception.
The three-field allowance still applies when an ordinary LOAD contains the ELF header. It never masks another
occurrence of those byte patterns or offsets relative to a later segment. Every
other header byte remains exact, including entry, `e_shentsize` and neighboring
program fields. The report records all three old/new values and each changed
byte, including an empty changed-byte list for an unchanged field.

Sections match by unique raw name. Retained type, flags, address, size,
alignment, entry size and resolved link/info remain equal, including NOBITS.
All allocated or loader-visible file offsets remain exact. Other retained
nonloaded sections may move but retain their metadata and payload bytes.
`.comment`, `.gnu.build.attributes`, other notes, unknown sections and retained
debug contents are protected. Nonloaded padding outside every declared region
must be all zero in each file; arbitrary zero gap/trailer lengths are allowed,
while even one unaccounted nonzero byte refuses.

A copy cannot invent sections or remove ordinary sections, the static tables,
or the section-name table. Only source debug sections from this exact name set
may disappear: `.debug_` plus `abbrev`, `addr`, `aranges`, `cu_index`, `frame`,
`info`, `line`, `line_str`, `loc`, `loclists`, `macinfo`, `macro`, `names`,
`pubnames`, `pubtypes`, `ranges`, `rnglists`, `str`, `str_offsets`, `sup`,
`tu_index`, `types`, `gnu_pubnames`, or `gnu_pubtypes`. They must be nonloaded,
nonallocated PROGBITS with address/link/info zero, and flags/entry size zero.
COMPRESSED is additionally allowed; `.debug_str`/`.debug_line_str` may instead
use MERGE|STRINGS with entry size one, optionally COMPRESSED. A debug-looking
name alone grants no exemption. Retained metadata cannot depend on a removed
section. Debug relocation relationships are unsupported.

Every compressed PROGBITS section, including a source debug section that
disappears, needs a 24-byte ELF64 compression header plus nonempty payload:
kind 1 (zlib) or 2 (zstd), reserved field zero, positive bounded uncompressed
size and positive power-of-two uncompressed alignment. Nonzero entry size must
divide the uncompressed length. Only the envelope is validated; compressed
data is not decompressed or authenticated. Retained compressed payload is exact.

Section/name-table relocation, section renumbering and section-name offset/size
repacking are permitted after identity/reference validation. Unused bytes in
that name table are bookkeeping, not protected content. Its type, flags,
alignment and other nonexception metadata still match, and no section may
reference it as an external dependency.

An ordinary, nonloaded SYMTAB and its private STRTAB have a separate exemption.
Both have zero flags, lie outside every qualified loader-visible interval, are distinct from the
section-name table and cannot be dependencies of retained sections (apart from
SYMTAB's own link to its private STRTAB). Neither can be added or removed.
No relocation or other section may reference the static symbol table. The
private STRTAB additionally has address/link/info/entry size zero and alignment
zero or one. Their size/offset and SYMTAB's local-count bookkeeping may change.

Static symbols are streamed in order. The first record is all zero, the local
boundary agrees with every binding, visibility is 0..3, and supported bindings
are LOCAL/GLOBAL/WEAK/GNU_UNIQUE (0/1/2/10); types are NOTYPE/OBJECT/FUNC/SECTION/
FILE/COMMON/TLS/GNU_IFUNC (0/1/2/3/4/5/6/10). Ordinary section indexes, undefined
(0) and ABS (0xfff1) are supported; SHN_COMMON, XINDEX and other special indexes
refuse. Symbols cannot target mutable section/string/symbol bookkeeping.
FILE must be local and ABS; SECTION must be local with an ordinary target.

Retained symbol name bytes, info, other, resolved section target, value and size
remain equal in order. Name offsets and ordinary section indexes may repack.
Only these source records may be deleted:

- Local FILE with ABS target and zero other/value/size.
- Local SECTION with empty name, zero other/size and value equal to the
  target's address (relative to the TLS base for TLS sections).

Changed noncanonical FILE/SECTION records, missing ordinary symbols, new or
reordered retained symbols and disappeared ordinary symbol targets refuse.
Unused bytes in the private static STRTAB are explicitly name bookkeeping;
resolved symbol names must remain equal. This is not a blanket exemption for
`.symtab`/`.strtab` bytes. A loaded DYNSYM/dynstr never receives it: both must be
allocated, remain byte-exact, and undergo a bounded embedded `st_shndx` pass so
an unchanged numeric index cannot silently name another allocated section after
renumbering. Dynamic names are checked for in-table offsets; dynamic-name string
contents are protected as loaded bytes, without dynamic linking interpretation.

## Finite limits and cost

Limits are per file, fixed by the supported profile. The CLI has no override,
validation bypass or digest-skipping mode. The following are binary units:

| Quantity | Maximum |
| --- | ---: |
| Logical input size, including sparse holes | 16 GiB = 17,179,869,184 bytes |
| One OS read | 1 MiB = 1,048,576 bytes |
| Program headers | 1,024 |
| Sections, including null | 4,096 |
| Section-name table | 16 MiB = 16,777,216 bytes |
| One section name, excluding NUL | 4,096 bytes |
| Entries per static or dynamic symbol table | 4,194,304 |
| Private static-name table | 1 GiB = 1,073,741,824 bytes |
| One resolved static symbol name, excluding NUL | 65,536 bytes |
| Aggregate resolved static names, including each NUL | 4 GiB = 4,294,967,296 bytes |
| Aggregate static-name read requests | 5,242,880 |
| Aggregate static-name bytes requested, including read-ahead | 20 GiB = 21,474,836,480 bytes |
| One static-name chunk | 4,096 bytes |
| Name cache | 0 bytes; no cache |
| Interpreter payload | 4,096 bytes |
| Declared uncompressed section length | 16 GiB = 17,179,869,184 bytes |

Every request is checked before I/O. Negative/unbounded/out-of-range/oversized
reads refuse; a zero-length range causes no OS read. A short read refuses.
Metadata counts and ranges are checked before allocation/iteration according
to them. Static-name budgets are reserved before reads, with each resolved
name's NUL accounted even for an empty name.

Payloads, uncovered zero gaps and exact file digests are streamed. Two comparison
buffers are bounded by 1 MiB each, symbol-record batches by 1 MiB per iterator,
and each symbol-name buffer by 65,537 bytes. Header/name/reference/report
collections are capped by the table/name limits; section-name bytes can total
up to roughly 16 MiB before Python container/report overhead. Memory does not
scale with whole ELF size or symbol count. These are allocation bounds, not a
promise of a particular process RSS.

At most one mapping record per LOAD and one range per program are retained.
Loader file aliases are merged before payload I/O, with no per-record replay; retained nonloaded payloads and zero gaps are scanned
separately and each entire logical file is hashed once. There is no separate
global I/O-byte switch: aggregate payload/gap/hash work is bounded by input and
table caps plus the explicit static-name work budgets. Large gaps still cost
linear read/hash time. Sparse files do not bypass exact hashing. Reported read
bytes include repeated metadata and name read-ahead, so they may exceed file
size. Preserve measured elapsed time, maximum RSS, read counts/bytes and maximum
request with the evidence; RSS alone is not a proof of boundedness.

## Stable acquisition

The comparator opens both files once, read-only/no-follow/nonblocking/close-on-
exec, and retains both descriptors through parsing, comparison and hashing.
Initial and final descriptor/path checks cover device, inode, logical size,
mtime_ns and ctime_ns. Atime is excluded because reading may change it.
Observable same-size writes, truncation/growth, pathname replacement, symlink
substitution and short reads refuse. Descriptors close on success and failure.

Stat stability is observational, not a lock or an atomic pair snapshot. A
same-clock-tick write can leave the checked timestamps unchanged; timestamp-
preserving or privileged changes can also evade it. Separate final checks leave
an interval between them. Ensure inputs are quiescent for the complete evidence
window; independent before/after digests and snapshots extend that window across
strip and verification. Deterministic mutation tests must force and verify a
chosen stat change, rather than assuming a write changed the clock reading.

## CLI, library and report

From the WSL/Linux source root:

```bash
python3 -B .agents/shared/evidence/elf_identity.py --help
python3 -B .agents/shared/evidence/elf_identity.py ORIGINAL COPY > comparison.json
```

Use paths to acquired, stable artifacts. Exit 0 produces JSON only after all
checks and final snapshots pass. Expected input/OS errors produce exit 1, a
useful `elf_identity:` stderr diagnostic and no success JSON. Argument usage
errors are argparse exit 2. Ordinary Python and `python3 -O -B` perform the
same input validation. There is no external Python dependency.

Library example, also from the source root:

```bash
python3 -B - ORIGINAL COPY <<'PY'
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path('.agents/shared/evidence').resolve()))
from elf_identity import compare_elf
print(json.dumps(compare_elf(sys.argv[1], sys.argv[2]), indent=2))
PY
```

Call `compare_elf` for each pair/acquisition. Do not cache a parsed object and
later label a different file equivalent. Library refusals are `ElfIdentityError`
or `OSError`; callers should treat either as no established relationship.

Schema version 1 reports:

- `load_equivalent`, `exact_sha256_equal`, and each `source`/`copy` absolute
  path, size, unmasked `sha256`, parsed header, acquisition times/stat snapshots
  and read measurements.
- `profile` and `limits_per_file`: supported types/flags/policy, stability
  caveat and numerical caps.
- `allowed_header_fields`: three old/new values and precise changed bytes.
- `program_comparison`: ordered full headers, merged loader ranges and their
  `normalized_sha256`, unique byte count, program-table coverage, and non-LOAD
  bytes outside LOAD exposure. `load_mappings` records each program index,
  rounded virtual interval, fully backed file pages, exposed file interval,
  protected prefix/tail and proven virtual zero-fill interval. All intervals
  are half-open. `independent_record_header_aliases` states the refusal rule.
- `section_comparison`: full source/copy semantic metadata, roles, retained
  nonloaded payload digests and removed source debug section inventory.
- `static_symbol_comparison`: presence, source/copy/retained counts, qualified
  removal counts/digest, ordered retained semantic digest, repacked index/name
  counts, local boundaries and private string-table policy.
- `dynamic_symbol_section_indexes` and `zero_nonloaded_padding`: resolved
  embedded allocated identities and each subject's checked zero-gap coverage.

`normalized_sha256` masks only the declared header fields and is never exact
whole-file identity. Static semantic digests are not raw table digests either.
Use only `source.sha256`/`copy.sha256` as the exact artifact identities. A
genuinely stripped pair normally has unequal exact digests despite passing.

## Fresh task-local copy and independent verification

Keep full logs, fixtures and copies in the active task's ignored `.local/`
storage. Record exact commands, cwd, environment, UTC start/end, exits and tool
versions. Publish only selected concise results/digests in durable evidence.
Never strip in place or reuse a historical execution copy as fresh evidence.

Set `TASK_DIR` to the absolute active `ai-tdesktop` task directory. Run this
example from the source root, with GNU strip, sha256sum and Python available:

```bash
set -euo pipefail
task_dir=${TASK_DIR:?Set TASK_DIR to the absolute active task directory}
mkdir -p "$task_dir/.local"
git -C "$task_dir" check-ignore -q .local/elf-copy-probe
run_dir=$(mktemp -d "$task_dir/.local/elf-copy.XXXXXX")
source_elf=$(realpath out/Debug/Telegram)
copy_elf="$run_dir/Telegram.stripped"
exec > >(tee "$run_dir/commands.log") 2>&1
trap 'result=$?; printf "exit=%s\n" "$result" > "$run_dir/exit.txt"' EXIT
set -x
date -u +%FT%TZ
python3 --version
strip --version
sha256sum --version
test ! -e "$copy_elf"
stat -c '%d %i %s %y %z' "$source_elf" > "$run_dir/source.before.stat"
sha256sum "$source_elf" > "$run_dir/source.before.sha256"
strip --strip-debug --no-merge-notes -o "$copy_elf" "$source_elf"
stat -c '%d %i %s %y %z' "$copy_elf" > "$run_dir/copy.before.stat"
python3 -B .agents/shared/evidence/elf_identity.py "$source_elf" "$copy_elf" \
  > "$run_dir/comparison.json" 2> "$run_dir/comparison.stderr"
sha256sum "$source_elf" "$copy_elf" > "$run_dir/independent.sha256"
sha256sum "$source_elf" > "$run_dir/source.after.sha256"
stat -c '%d %i %s %y %z' "$source_elf" > "$run_dir/source.after.stat"
stat -c '%d %i %s %y %z' "$copy_elf" > "$run_dir/copy.after.stat"
cmp "$run_dir/source.before.sha256" "$run_dir/source.after.sha256"
cmp "$run_dir/source.before.stat" "$run_dir/source.after.stat"
cmp "$run_dir/copy.before.stat" "$run_dir/copy.after.stat"
python3 -B - "$run_dir" <<'PY'
import json
from pathlib import Path
import sys
root = Path(sys.argv[1])
report = json.loads((root / 'comparison.json').read_text())
independent = dict(line.split(maxsplit=1)[::-1]
    for line in (root / 'independent.sha256').read_text().splitlines())
for role in ('source', 'copy'):
    item = report[role]
    if independent.get(item['path']) != item['sha256']:
        raise SystemExit(role + ': independent digest mismatch')
    if Path(item['path']).stat().st_size != item['size']:
        raise SystemExit(role + ': independent size mismatch')
left = report['source']['acquisition']['initial_stat']
right = report['copy']['acquisition']['initial_stat']
if (left['device'], left['inode']) == (right['device'], right['inode']):
    raise SystemExit('execution copy must be a distinct artifact')
print('Independent whole-file identities and distinct artifacts verified.')
PY
date -u +%FT%TZ
```

The `%y`/`%z` fields include fractional mtime/ctime; atime is deliberately absent.
For qualification also wrap the helper command with `/usr/bin/time -v -o
"$run_dir/resources.txt"` to retain independent process memory/time metrics.
That optional measurement tool is not needed to use the comparator itself.
The acquisition example stops on any nonzero exit; preceding commands in a
completed trace succeeded. Keep its terminal trace and `exit.txt` together.

Expected-error example (exit 1, no stdout, missing-path diagnostic):

```bash
python3 -B .agents/shared/evidence/elf_identity.py ORIGINAL /nonexistent/elf-copy
```

## Required self-tests and controls

The complete suite uses standard-library unittest and requires g++, GNU strip,
readelf and sha256sum. Missing required tools fail explicitly. It compiles only
a tiny independent `g++ -g -O0` Debug fixture with data/BSS/TLS, strips a unique
copy, verifies actual sections, compares under ordinary and optimized Python
and mutates a readelf-located executable byte. Neither that compiled binary
nor Telegram is executed. Separate literal x86-64 mapping fixtures contain only
a byte read and an exit syscall: the suite inspects the instruction bytes and
address before running the prefix/tail 7-versus-9 and writable-BSS 0/0 oracles.
INTERP alias fixtures are compared only; no interpreter is installed or run.

```bash
test_root=$(mktemp -d "$TASK_DIR/.local/elf-tests.XXXXXX")
ELF_IDENTITY_TEST_ROOT="$test_root" \
  python3 -B .agents/shared/evidence/elf_identity_test.py -v \
  > "$test_root/suite.stdout" 2> "$test_root/suite.stderr"
```

The suite always includes sparse files whose section table is at 4,294,971,392
(2**32 + 4096), real CLI comparison, full independent sha256sum readings,
high-offset metadata mutation and an out-of-file high pointer. It hashes
multiple GiB and needs sparse-file support/time/quota despite low allocated
storage. A roughly 4.2 decimal GB Telegram executable is below 4 GiB and is
not this control. Fixture and command files remain at `ELF_IDENTITY_TEST_ROOT`;
without it a retained temporary root is printed. Remove disposable runs only
under the evidence owner's normal retention policy.

During authoring a focused run can omit the heavy control, but cannot replace
the full qualification suite:

```bash
ELF_IDENTITY_TEST_ROOT="$test_root/focused" \
  python3 -B .agents/shared/evidence/elf_identity_test.py \
  ElfComparisonTests ElfMalformedTests ElfAcquisitionTests ElfMappingTests CommandOwnerTests -v
ELF_IDENTITY_TEST_ROOT="$test_root/optimized" \
  python3 -O -B .agents/shared/evidence/elf_identity_test.py \
  ElfComparisonTests.test_cli_modes_valid_strip_invalid_and_loaded_mutation ElfMappingTests CommandOwnerTests -v
```

Fixtures use their own literal ABI offsets and packing, including valid TLS BSS
overlap, zero-sized sections, nested program coverage, non-LOAD notes and
independent symbol/name repacking. They never obtain mutation offsets from the
production parser. Keep both positive and paired forbidden controls: refusing
everything is not qualification. Bounds tests spy on the real OS read boundary,
exercise exact and one-over limits, and force explicit aggregate name-budget
refusals. Acquisition controls use deterministic interception and real verified
stat/path changes, with descriptor accounting on every tested outcome.

The page fixtures retain literal prefix/tail counterexamples, final writable-BSS
bookkeeping, ordinary nonaliased mappings, aligned nonfinal BSS, shared file-page
exposure, and explicit EOF/rounding/overlap refusals in both Python modes. An
INTERP alias changing `/a` to `/b` inside e_shoff and table-driven intersections
for other independent records must refuse. Valid relocation changing all three
header fields still passes. The original composite fixture uses a single final
writable BSS LOAD, so its nonloaded bookkeeping remains outside actual exposure;
its independent offsets and TLS/static-symbol controls stay intact. The tiny
compiler fixture also retains 64 ordinary globals: its original smaller stripped
output ended partway through the final mapped file page and now correctly
refuses. These globals make the genuine strip output fully page-backed without
padding or rewriting it after stripping, while preserving the original
initialized-data/BSS/TLS and protected-byte controls.

`FixtureCase.command` writes an attempted JSON record before launch and a final
record for success, nonzero exit, launch failure, timeout, cancellation or error.
Records include argv, cwd, environment additions, start/end/deadline, exception,
owned PID/PGID, cleanup signals/errors and available stdout/stderr file paths.
Output goes directly to log files, so an inherited output descriptor cannot hold
pipe draining open. The owner starts a dedicated session/group, observes exit
without reaping its leader, and retains that PID anchor through signaling. It
uses TERM with a 0.25-second grace, KILL with a 2-second stop window, then a
1-second direct-child reap; cleanup failure remains an explicit failed record
with exact remaining ownership. SIGINT is deferred during launch/cleanup;
cancellation propagates after cleanup and evidence finalization. Capture reading
happens only after cleanup; log sizes depend on these trusted qualification tools.

This owner covers trusted compiler/strip/CLI descendants that stay in its group;
it makes no claim to contain daemonized or adversarial escaped sessions. It
never kills by name or signals a group after reaping its identity anchor. Tests
synchronize on a live compiler-like worker before timeout/cancellation/error,
include TERM resistance and inherited output files, verify an unrelated sentinel
survives, and check direct-child and worker reaping. Grandchild subreaping exists
only inside an isolated test subprocess; the suite caller and comparator retain
their process-wide settings. Successful commands, launch failures and cleanup
failure recording also have controls, and the genuine tiny toolchain runs through
the same command owner.
