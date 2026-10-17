#!/usr/bin/env python3
"""Bounded, directional ELF64 execution-copy verification and exact identities."""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
import stat
import struct
import sys
import time
from typing import NamedTuple

ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIQQQQQQ")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
SYMBOL = struct.Struct("<IBBHQQ")
HEADER_BOOKKEEPING_RANGES = ((40, 48), (60, 62), (62, 64))
UINT64_MAX = (1 << 64) - 1
MAX_INPUT_SIZE = 16 << 30
MAX_READ_SIZE = 1 << 20
MAX_PROGRAM_HEADERS = 1024
MAX_SECTIONS = 4096
MAX_SECTION_NAME_TABLE = 16 << 20
MAX_SECTION_NAME = 4096
MAX_STATIC_SYMBOLS = 4194304
MAX_STATIC_NAME_TABLE = 1 << 30
MAX_SYMBOL_NAME = 64 << 10
MAX_RESOLVED_NAME_BYTES = 4 << 30
NAME_READ_SIZE = 4096
MAX_NAME_READ_BYTES = MAX_RESOLVED_NAME_BYTES + MAX_STATIC_SYMBOLS * NAME_READ_SIZE
MAX_NAME_READS = MAX_STATIC_SYMBOLS + MAX_RESOLVED_NAME_BYTES // NAME_READ_SIZE
MAX_INTERPRETER_SIZE = 4096
MAX_NAME_CACHE_BYTES = 0
LOAD_PAGE_SIZE = 4096

ET_EXEC = 2
ET_DYN = 3
EM_X86_64 = 62
SHN_LORESERVE = 0xff00
SHN_ABS = 0xfff1
SHN_COMMON = 0xfff2
SHN_XINDEX = 0xffff
PN_XNUM = 0xffff
PT_NULL = 0
PT_LOAD = 1
PT_DYNAMIC = 2
PT_INTERP = 3
PT_NOTE = 4
PT_PHDR = 6
PT_TLS = 7
PT_GNU_EH_FRAME = 0x6474e550
PT_GNU_STACK = 0x6474e551
PT_GNU_RELRO = 0x6474e552
PT_GNU_PROPERTY = 0x6474e553
PROGRAM_TYPES = {
	PT_NULL, PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_NOTE, PT_PHDR, PT_TLS,
	PT_GNU_EH_FRAME, PT_GNU_STACK, PT_GNU_RELRO, PT_GNU_PROPERTY,
}
PF_X = 1
PF_W = 2
PF_R = 4
SHT_NULL = 0
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_HASH = 5
SHT_DYNAMIC = 6
SHT_NOTE = 7
SHT_NOBITS = 8
SHT_REL = 9
SHT_DYNSYM = 11
SHT_INIT_ARRAY = 14
SHT_FINI_ARRAY = 15
SHT_PREINIT_ARRAY = 16
SHT_GROUP = 17
SHT_SYMTAB_SHNDX = 18
SHT_RELR = 19
SHT_GNU_ATTRIBUTES = 0x6ffffff5
SHT_GNU_HASH = 0x6ffffff6
SHT_GNU_VERDEF = 0x6ffffffd
SHT_GNU_VERNEED = 0x6ffffffe
SHT_GNU_VERSYM = 0x6fffffff
SECTION_TYPES = {
	SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_HASH,
	SHT_DYNAMIC, SHT_NOTE, SHT_NOBITS, SHT_REL, SHT_DYNSYM,
	SHT_INIT_ARRAY, SHT_FINI_ARRAY, SHT_PREINIT_ARRAY, SHT_RELR,
	SHT_GNU_ATTRIBUTES, SHT_GNU_HASH, SHT_GNU_VERDEF,
	SHT_GNU_VERNEED, SHT_GNU_VERSYM,
}
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHF_MERGE = 0x10
SHF_STRINGS = 0x20
SHF_INFO_LINK = 0x40
SHF_LINK_ORDER = 0x80
SHF_TLS = 0x400
SHF_COMPRESSED = 0x800
SHF_GNU_RETAIN = 0x200000
SECTION_FLAGS = (
	SHF_WRITE | SHF_ALLOC | SHF_EXECINSTR | SHF_MERGE | SHF_STRINGS
	| SHF_INFO_LINK | SHF_LINK_ORDER | SHF_TLS | SHF_COMPRESSED
	| SHF_GNU_RETAIN
)
SECTION_ENTRY_SIZES = {
	SHT_SYMTAB: 24, SHT_DYNSYM: 24, SHT_RELA: 24, SHT_REL: 16,
	SHT_DYNAMIC: 16, SHT_HASH: 4, SHT_INIT_ARRAY: 8, SHT_FINI_ARRAY: 8,
	SHT_PREINIT_ARRAY: 8, SHT_RELR: 8, SHT_GNU_VERSYM: 2,
	SHT_STRTAB: 0, SHT_NOTE: 0, SHT_NOBITS: 0, SHT_GNU_ATTRIBUTES: 0,
	SHT_GNU_HASH: 0, SHT_GNU_VERDEF: 0, SHT_GNU_VERNEED: 0,
}
LINK_TARGET_TYPES = {
	SHT_SYMTAB: (SHT_STRTAB,), SHT_DYNSYM: (SHT_STRTAB,),
	SHT_DYNAMIC: (SHT_STRTAB,), SHT_RELA: (SHT_SYMTAB, SHT_DYNSYM),
	SHT_REL: (SHT_SYMTAB, SHT_DYNSYM), SHT_HASH: (SHT_DYNSYM,),
	SHT_GNU_HASH: (SHT_DYNSYM,), SHT_GNU_VERSYM: (SHT_DYNSYM,),
	SHT_GNU_VERDEF: (SHT_STRTAB,), SHT_GNU_VERNEED: (SHT_STRTAB,),
}
DEBUG_NAMES = frozenset(b".debug_" + name for name in (
	b"abbrev", b"addr", b"aranges", b"cu_index", b"frame", b"info",
	b"line", b"line_str", b"loc", b"loclists", b"macinfo", b"macro",
	b"names", b"pubnames", b"pubtypes", b"ranges", b"rnglists", b"str",
	b"str_offsets", b"sup", b"tu_index", b"types", b"gnu_pubnames",
	b"gnu_pubtypes",
))
DEBUG_STRING_NAMES = frozenset((b".debug_str", b".debug_line_str"))
COMPRESSION_HEADER = struct.Struct("<IIQQ")
COMPRESSION_TYPES = {1: "zlib", 2: "zstd"}
SYMBOL_BINDINGS = frozenset((0, 1, 2, 10))
SYMBOL_TYPES = frozenset((0, 1, 2, 3, 4, 5, 6, 10))
STB_LOCAL = 0
STT_SECTION = 3
STT_FILE = 4

class ElfIdentityError(RuntimeError):
	pass

class ElfHeader(NamedTuple):
	e_ident: bytes
	e_type: int
	e_machine: int
	e_version: int
	e_entry: int
	e_phoff: int
	e_shoff: int
	e_flags: int
	e_ehsize: int
	e_phentsize: int
	e_phnum: int
	e_shentsize: int
	e_shnum: int
	e_shstrndx: int

class ProgramHeader(NamedTuple):
	p_type: int
	p_flags: int
	p_offset: int
	p_vaddr: int
	p_paddr: int
	p_filesz: int
	p_memsz: int
	p_align: int

class SectionHeader(NamedTuple):
	sh_name: int
	sh_type: int
	sh_flags: int
	sh_addr: int
	sh_offset: int
	sh_size: int
	sh_link: int
	sh_info: int
	sh_addralign: int
	sh_entsize: int

@dataclass(frozen=True)
class Section:
	index: int
	header: SectionHeader
	name: bytes
	link: bytes | int
	info: bytes | int

@dataclass(frozen=True)
class ParsedElf:
	header: ElfHeader
	header_bytes: bytes
	programs: tuple[ProgramHeader, ...]
	sections: tuple[Section, ...]
	loader_ranges: tuple[tuple[int, int], ...]
	load_mappings: tuple[dict, ...]
	file_regions: tuple[tuple[int, int, str], ...]

@dataclass(frozen=True)
class SectionClasses:
	by_name: dict
	roles: dict
	symtab: Section | None
	strtab: Section | None

class SymbolRecord(NamedTuple):
	index: int
	name_offset: int
	section_index: int
	name: bytes
	info: int
	other: int
	target: bytes | int
	value: int
	size: int
	removable: bool

	def semantics(self):
		return (self.name, self.info, self.other, self.target, self.value, self.size)

def require(condition, subject, field, detail):
	if not condition:
		raise ElfIdentityError(f"{subject}: {field}: {detail}")

def check_limit(subject, field, actual, limit):
	require(actual <= limit, subject, field, f"actual={actual}, limit={limit}")

def check_range(subject, field, offset, size, extent):
	require(
		type(offset) is int and type(size) is int
		and 0 <= offset <= extent and 0 <= size <= extent - offset,
		subject,
		field,
		f"offset={offset}, size={size}, extent={extent}",
	)

def check_alignment(subject, field, alignment):
	require(
		alignment == 0 or alignment & (alignment - 1) == 0,
		subject,
		field,
		f"alignment={alignment} is not zero or a power of two",
	)

def contains(start, size, inner_start, inner_size):
	return start <= inner_start and inner_size <= size - (inner_start - start)

def overlaps(start, size, other_start, other_size):
	return bool(size and other_size and start < other_start + other_size
		and other_start < start + size)

def merge_ranges(ranges):
	result = []
	for start, end in sorted(ranges):
		if start == end:
			continue
		if result and start <= result[-1][1]:
			result[-1] = (result[-1][0], max(result[-1][1], end))
		else:
			result.append((start, end))
	return tuple(result)

def check_disjoint(subject, regions):
	previous = None
	for start, end, label in sorted(regions):
		if start == end:
			continue
		if previous is not None:
			require(start >= previous[1], subject, label,
				f"range [{start},{end}) overlaps {previous[2]} "
				f"[{previous[0]},{previous[1]})")
		previous = (start, end, label)

def stat_identity(value):
	return {
		"device": value.st_dev,
		"inode": value.st_ino,
		"size": value.st_size,
		"mtime_ns": value.st_mtime_ns,
		"ctime_ns": value.st_ctime_ns,
	}

class ElfFile:
	def __init__(self, path):
		self.path = os.path.abspath(os.fsdecode(os.fspath(path)))
		self.fd = None
		self.size = 0
		self.initial_stat = None
		self.final_stat = None
		self.started_ns = None
		self.finished_ns = None
		self.read_count = 0
		self.bytes_read = 0
		self.max_request = 0
		self.name_read_count = 0
		self.name_read_bytes = 0
		self.resolved_name_bytes = 0

	def __enter__(self):
		require(self.fd is None and self.initial_stat is None,
			self.path, "acquisition", "file owner cannot be reopened")
		require(sys.platform == "linux", self.path, "platform",
			"Linux/WSL descriptor semantics are required")
		for name in ("pread", "O_NOFOLLOW", "O_CLOEXEC", "O_NONBLOCK"):
			require(hasattr(os, name), self.path, "platform", f"missing os.{name}")
		require(callable(os.pread), self.path, "platform", "os.pread is unavailable")
		try:
			import fcntl
		except ImportError as error:
			raise ElfIdentityError(f"{self.path}: platform: fcntl is required") from error
		self.started_ns = time.time_ns()
		try:
			self.fd = os.open(self.path,
				os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
			flags = fcntl.fcntl(self.fd, fcntl.F_GETFL)
			require(flags & os.O_ACCMODE == os.O_RDONLY, self.path,
				"descriptor flags", "read-only access is required")
			require(flags & os.O_NONBLOCK and flags & os.O_NOFOLLOW,
				self.path, "descriptor flags", "no-follow/nonblocking required")
			require(fcntl.fcntl(self.fd, fcntl.F_GETFD) & fcntl.FD_CLOEXEC,
				self.path, "descriptor flags", "close-on-exec required")
			initial = os.fstat(self.fd)
			require(stat.S_ISREG(initial.st_mode), self.path, "file type",
				"a regular file is required")
			self.initial_stat = stat_identity(initial)
			self.size = initial.st_size
			check_limit(self.path, "input size", self.size, MAX_INPUT_SIZE)
			self._verify_path()
			return self
		except BaseException:
			self.__exit__(None, None, None)
			raise

	def __exit__(self, exc_type, exc_value, traceback):
		if self.fd is not None:
			fd, self.fd = self.fd, None
			os.close(fd)
		return False

	def read_at(self, offset, size):
		require(self.fd is not None, self.path, "read", "descriptor is closed")
		check_range(self.path, "read range", offset, size, self.size)
		check_limit(self.path, "read size", size, MAX_READ_SIZE)
		if not size:
			return b""
		self.max_request = max(self.max_request, size)
		self.read_count += 1
		data = os.pread(self.fd, size, offset)
		self.bytes_read += len(data)
		require(len(data) == size, self.path, "short read",
			f"offset={offset}, requested={size}, actual={len(data)}")
		return data

	def iter_range(self, offset, size):
		check_range(self.path, "iteration range", offset, size, self.size)
		while size:
			amount = min(size, MAX_READ_SIZE)
			yield self.read_at(offset, amount)
			offset += amount
			size -= amount

	def sha256_range(self, offset, size, masks=()):
		check_range(self.path, "hash range", offset, size, self.size)
		masks = tuple(masks)
		require(all(mask in HEADER_BOOKKEEPING_RANGES for mask in masks),
			self.path, "hash masks", "only ELF section-header fields may be masked")
		digest = hashlib.sha256()
		for data in self.iter_range(offset, size):
			if any(start < offset + len(data) and end > offset
					for start, end in masks):
				data = bytearray(data)
				for start, end in masks:
					left, right = max(start, offset), min(end, offset + len(data))
					if left < right:
						data[left - offset:right - offset] = bytes(right - left)
			digest.update(data)
			offset += len(data)
		return digest.hexdigest()

	def read_name_chunk(self, offset, size):
		"""Reserve from the 20-GiB / 5,242,880-read static-name budget."""
		check_range(self.path, "symbol name chunk", 0, size, NAME_READ_SIZE)
		check_limit(self.path, "symbol name read bytes",
			self.name_read_bytes + size, MAX_NAME_READ_BYTES)
		check_limit(self.path, "symbol name read count",
			self.name_read_count + bool(size), MAX_NAME_READS)
		self.name_read_bytes += size
		self.name_read_count += bool(size)
		return self.read_at(offset, size)

	def account_symbol_name(self, size):
		"""Count resolved names including NUL, independently of read-ahead."""
		require(type(size) is int and 1 <= size <= MAX_SYMBOL_NAME + 1,
			self.path, "resolved symbol name",
			f"size including NUL={size}, limit={MAX_SYMBOL_NAME + 1}")
		check_limit(self.path, "resolved symbol name bytes",
			self.resolved_name_bytes + size, MAX_RESOLVED_NAME_BYTES)
		self.resolved_name_bytes += size

	def _verify_path(self):
		current = os.stat(self.path, follow_symlinks=False)
		require(stat.S_ISREG(current.st_mode), self.path,
			"path stability", "pathname no longer names a regular file")
		require(stat_identity(current) == self.initial_stat, self.path,
			"path stability", "pathname identity changed during inspection")

	def verify_unchanged(self):
		require(self.fd is not None, self.path, "stability", "descriptor is closed")
		current = os.fstat(self.fd)
		require(stat.S_ISREG(current.st_mode), self.path,
			"descriptor stability", "descriptor is no longer a regular file")
		require(stat_identity(current) == self.initial_stat, self.path,
			"descriptor stability", "file changed during inspection")
		self._verify_path()
		self.final_stat = stat_identity(current)
		self.finished_ns = time.time_ns()
		return self.final_stat

def parse_elf(file):
	header_bytes = file.read_at(0, ELF_HEADER.size)
	header = ElfHeader(*ELF_HEADER.unpack(header_bytes))
	ident = header.e_ident
	require(ident[:4] == b"\x7fELF", file.path, "e_ident", "invalid ELF magic")
	for field, actual, expected in (
		("EI_CLASS", ident[4], 2), ("EI_DATA", ident[5], 1),
		("EI_VERSION", ident[6], 1), ("EI_ABIVERSION", ident[8], 0),
		("e_machine", header.e_machine, EM_X86_64),
		("e_version", header.e_version, 1), ("e_flags", header.e_flags, 0),
		("e_ehsize", header.e_ehsize, ELF_HEADER.size),
		("e_phentsize", header.e_phentsize, PROGRAM_HEADER.size),
		("e_shentsize", header.e_shentsize, SECTION_HEADER.size),
	):
		require(actual == expected, file.path, field,
			f"unsupported value={actual}, required={expected}")
	require(ident[7] in (0, 3), file.path, "EI_OSABI",
		f"unsupported ABI={ident[7]}; only System V/GNU ABI is supported")
	require(ident[9:] == bytes(7), file.path, "EI_PAD", "nonzero reserved bytes")
	require(header.e_type in (ET_EXEC, ET_DYN), file.path, "e_type",
		f"unsupported type={header.e_type}; require ET_EXEC/ET_DYN")
	require(header.e_phnum not in (0, PN_XNUM), file.path, "e_phnum",
		"missing or extended program numbering is unsupported")
	require(0 < header.e_shnum < SHN_LORESERVE, file.path, "e_shnum",
		"sectionless or extended section numbering is unsupported")
	check_limit(file.path, "e_phnum", header.e_phnum, MAX_PROGRAM_HEADERS)
	check_limit(file.path, "e_shnum", header.e_shnum, MAX_SECTIONS)
	require(0 < header.e_shstrndx < header.e_shnum, file.path, "e_shstrndx",
		f"missing, extended or out-of-range index={header.e_shstrndx}")
	regions = [(0, ELF_HEADER.size, "ELF header")]
	for field, offset, size in (
		("program table", header.e_phoff, header.e_phnum * PROGRAM_HEADER.size),
		("section table", header.e_shoff, header.e_shnum * SECTION_HEADER.size),
	):
		require(offset >= ELF_HEADER.size and offset % 8 == 0,
			file.path, field, f"invalid table offset/alignment={offset}")
		check_range(file.path, field, offset, size, file.size)
		regions.append((offset, offset + size, field))
	check_disjoint(file.path, regions)
	programs = tuple(ProgramHeader(*PROGRAM_HEADER.unpack(file.read_at(
		header.e_phoff + index * PROGRAM_HEADER.size, PROGRAM_HEADER.size)))
		for index in range(header.e_phnum))
	sections = tuple(SectionHeader(*SECTION_HEADER.unpack(file.read_at(
		header.e_shoff + index * SECTION_HEADER.size, SECTION_HEADER.size)))
		for index in range(header.e_shnum))
	require(not any(sections[0]), file.path, "section[0]",
		"ordinary numbering requires an all-zero null section")
	for index, section in enumerate(sections[1:], 1):
		label = f"section[{index}]"
		require(section.sh_type != SHT_NULL, file.path, label,
			"additional null sections are unsupported")
		check_range(file.path, label + " address", section.sh_addr,
			section.sh_size, UINT64_MAX)
		file_size = 0 if section.sh_type == SHT_NOBITS else section.sh_size
		check_range(file.path, label + " file range", section.sh_offset,
			file_size, file.size)
		if file_size:
			regions.append((section.sh_offset, section.sh_offset + file_size, label))
	check_disjoint(file.path, regions)
	loader_ranges, load_mappings = validate_program_headers(file, header, programs)
	names = read_section_names(file, header, sections)
	validated = validate_sections(file, header, programs, sections, names,
		loader_ranges)
	return ParsedElf(header, header_bytes, programs, validated,
		loader_ranges, load_mappings, tuple(sorted(regions)))

def qualify_load_mappings(file, programs):
	loads = [(index, p) for index, p in enumerate(programs) if p.p_type == PT_LOAD]
	mappings = []
	for index, program in loads:
		label = f"program[{index}] 4096-byte LOAD profile"
		require(program.p_filesz > 0, file.path, label, "empty file payload")
		file_end = program.p_offset + program.p_filesz
		memory_end = program.p_vaddr + program.p_memsz
		rounded_file_end = (file_end + LOAD_PAGE_SIZE - 1) & -LOAD_PAGE_SIZE
		rounded_memory_end = (memory_end + LOAD_PAGE_SIZE - 1) & -LOAD_PAGE_SIZE
		require(max(rounded_file_end, rounded_memory_end) <= UINT64_MAX,
			file.path, label, "page rounding overflow")
		require(rounded_file_end <= file.size, file.path, label,
			"partial EOF page; rounded file pages must be fully backed")
		file_start = program.p_offset & -LOAD_PAGE_SIZE
		memory_start = program.p_vaddr & -LOAD_PAGE_SIZE
		if mappings:
			require(memory_start >= mappings[-1]["virtual_range"][1],
				file.path, label, "unordered or overlapping PT_LOAD rounded virtual ranges")
		bss = program.p_memsz > program.p_filesz
		if bss:
			require(program.p_flags & PF_W, file.path, label,
				"BSS requires a writable LOAD")
			require(index == loads[-1][0] or memory_end % LOAD_PAGE_SIZE == 0,
				file.path, label, "nonfinal BSS memory end must be page-aligned")
		exposed_end = file_end if bss else rounded_file_end
		mappings.append({
			"program_index": index,
			"virtual_range": (memory_start, rounded_memory_end),
			"backed_file_pages": (file_start, rounded_file_end),
			"exposed_file_range": (file_start, exposed_end),
			"protected_prefix": (file_start, program.p_offset),
			"protected_tail": (file_end, exposed_end),
			"zero_fill_virtual_range": (
				(program.p_vaddr + program.p_filesz, rounded_memory_end) if bss else None),
		})
	return tuple(mappings)

def validate_program_headers(file, header, programs):
	loads = []
	unique = {}
	for index, program in enumerate(programs):
		label = f"program[{index}]"
		require(program.p_type in PROGRAM_TYPES, file.path, label + ".p_type",
			f"unsupported type={program.p_type:#x}")
		require(program.p_flags & ~(PF_R | PF_W | PF_X) == 0, file.path,
			label + ".p_flags", f"unsupported flags={program.p_flags:#x}")
		check_range(file.path, label + " file range", program.p_offset,
			program.p_filesz, file.size)
		if program.p_type not in (PT_NULL, PT_LOAD):
			require(not any(overlaps(program.p_offset, program.p_filesz,
				start, end - start) for start, end in HEADER_BOOKKEEPING_RANGES),
				file.path, label, "independent program payload overlaps mutable header fields")
		check_range(file.path, label + " virtual range", program.p_vaddr,
			program.p_memsz, UINT64_MAX)
		check_range(file.path, label + " physical range", program.p_paddr,
			program.p_memsz, UINT64_MAX)
		check_alignment(file.path, label + ".p_align", program.p_align)
		if program.p_align > 1:
			require(program.p_vaddr % program.p_align
				== program.p_offset % program.p_align,
				file.path, label + ".p_align", "file/address incongruence")
		if program.p_type != PT_NOTE:
			require(program.p_filesz <= program.p_memsz, file.path, label,
				"p_filesz exceeds p_memsz")
		if program.p_type == PT_NULL:
			require(not any(program), file.path, label,
				"nonzero unused program fields are unsupported")
		elif program.p_type == PT_LOAD:
			require(program.p_vaddr % LOAD_PAGE_SIZE
				== program.p_offset % LOAD_PAGE_SIZE,
				file.path, label, "PT_LOAD is incongruent at the 4096-byte page size")
			if loads:
				last = loads[-1]
				require(program.p_vaddr >= last.p_vaddr + last.p_memsz,
					file.path, label, "unordered or overlapping PT_LOAD addresses")
			loads.append(program)
		elif program.p_type not in (PT_NOTE,):
			require(program.p_type not in unique, file.path, label,
				f"duplicate program type={program.p_type:#x}")
			unique[program.p_type] = (index, program)
	require(loads, file.path, "program table", "no PT_LOAD segment")
	check_disjoint(file.path, [(p.p_offset, p.p_offset + p.p_filesz,
		f"PT_LOAD[{index}]") for index, p in enumerate(loads)])
	mappings = qualify_load_mappings(file, programs)
	require(any(p.p_flags & PF_X and contains(p.p_vaddr, p.p_filesz,
		header.e_entry, 1) for p in loads), file.path, "e_entry",
		f"entry={header.e_entry:#x} is not in executable file-backed PT_LOAD")
	first_load = next(i for i, p in enumerate(programs) if p.p_type == PT_LOAD)
	for kind, (index, program) in unique.items():
		label = f"program[{index}]"
		if kind in (PT_PHDR, PT_INTERP):
			require(index < first_load, file.path, label,
				"PT_PHDR/PT_INTERP must precede PT_LOAD")
		if kind == PT_PHDR:
			require(program.p_offset == header.e_phoff
				and program.p_filesz == header.e_phnum * PROGRAM_HEADER.size
				and program.p_memsz == program.p_filesz,
				file.path, label, "PT_PHDR does not describe the program table")
		elif kind == PT_INTERP:
			check_limit(file.path, label + ".p_filesz",
				program.p_filesz, MAX_INTERPRETER_SIZE)
			require(program.p_filesz >= 2 and program.p_memsz == program.p_filesz,
				file.path, label, "invalid interpreter sizes")
			data = file.read_at(program.p_offset, program.p_filesz)
			require(data.startswith(b"/") and data.endswith(b"\0")
				and b"\0" not in data[:-1], file.path, label,
				"interpreter must be one NUL-terminated absolute path")
		elif kind == PT_DYNAMIC:
			require(program.p_filesz > 0 and program.p_filesz % 16 == 0
				and program.p_filesz == program.p_memsz,
				file.path, label, "invalid PT_DYNAMIC entry sizes")
		elif kind == PT_GNU_STACK:
			require(not any((program.p_offset, program.p_vaddr, program.p_paddr,
				program.p_filesz, program.p_memsz)), file.path, label,
				"GNU_STACK with a file/address payload is unsupported")
		if kind in (PT_PHDR, PT_INTERP, PT_DYNAMIC, PT_GNU_EH_FRAME,
				PT_GNU_RELRO, PT_TLS):
			memory_size = program.p_filesz if kind == PT_TLS else program.p_memsz
			require(any(contains(p.p_offset, p.p_filesz,
				program.p_offset, program.p_filesz)
				and contains(p.p_vaddr, p.p_memsz, program.p_vaddr, memory_size)
				and program.p_offset - p.p_offset == program.p_vaddr - p.p_vaddr
				for p in loads), file.path, label,
				"program payload has no compatible PT_LOAD mapping")
	ranges = [mapping["exposed_file_range"] for mapping in mappings]
	ranges.extend((p.p_offset, p.p_offset + p.p_filesz)
		for p in programs if p.p_type not in (PT_NULL, PT_LOAD))
	return merge_ranges(ranges), mappings

def read_section_names(file, header, sections):
	table = sections[header.e_shstrndx]
	require(table.sh_type == SHT_STRTAB and table.sh_flags == 0
		and table.sh_addr == 0 and table.sh_link == 0 and table.sh_info == 0
		and table.sh_entsize == 0 and table.sh_addralign in (0, 1),
		file.path, "section name table", "invalid STRTAB metadata")
	check_limit(file.path, "section name table size",
		table.sh_size, MAX_SECTION_NAME_TABLE)
	require(table.sh_size > 0, file.path, "section name table", "empty table")
	require(file.read_at(table.sh_offset, 1) == b"\0"
		and file.read_at(table.sh_offset + table.sh_size - 1, 1) == b"\0",
		file.path, "section name table", "first/final byte must be NUL")
	names = []
	seen = set()
	for index, section in enumerate(sections):
		label = f"section[{index}].sh_name"
		require(section.sh_name < table.sh_size, file.path, label,
			f"index={section.sh_name} exceeds string table size={table.sh_size}")
		remaining = min(table.sh_size - section.sh_name, MAX_SECTION_NAME + 1)
		offset = table.sh_offset + section.sh_name
		name = bytearray()
		while remaining:
			data = file.read_at(offset, min(remaining, NAME_READ_SIZE))
			end = data.find(b"\0")
			if end >= 0:
				name.extend(data[:end])
				break
			name.extend(data)
			offset += len(data)
			remaining -= len(data)
		else:
			check_limit(file.path, label + " length", len(name), MAX_SECTION_NAME)
			raise ElfIdentityError(f"{file.path}: {label}: unterminated name")
		check_limit(file.path, label + " length", len(name), MAX_SECTION_NAME)
		name = bytes(name)
		require(index == 0 or name, file.path, label,
			"unnamed sections are unsupported")
		require(name not in seen, file.path, label, f"duplicate name={name!r}")
		seen.add(name)
		names.append(name)
	return names

def validate_sections(file, header, programs, sections, names, loader_ranges):
	loads = [p for p in programs if p.p_type == PT_LOAD]
	tls = next((p for p in programs if p.p_type == PT_TLS), None)
	ordinary_addresses = []
	tls_addresses = []
	result = []
	unique = set()
	for index, section in enumerate(sections):
		label = f"section[{index}] {names[index]!r}"
		if index == 0:
			result.append(Section(index, section, names[index], 0, 0))
			continue
		require(section.sh_type in SECTION_TYPES, file.path, label + ".sh_type",
			f"unsupported type={section.sh_type:#x} or reference encoding")
		require(section.sh_flags & ~SECTION_FLAGS == 0, file.path,
			label + ".sh_flags", f"unsupported flags={section.sh_flags:#x}")
		check_alignment(file.path, label + ".sh_addralign", section.sh_addralign)
		if section.sh_addralign > 1:
			require(section.sh_addr % section.sh_addralign == 0, file.path,
				label + ".sh_addr", "section address violates alignment")
			if section.sh_type != SHT_NOBITS:
				require(section.sh_offset % section.sh_addralign == 0, file.path,
					label + ".sh_offset", "section file offset violates alignment")
		if section.sh_type in SECTION_ENTRY_SIZES:
			require(section.sh_entsize == SECTION_ENTRY_SIZES[section.sh_type],
				file.path, label + ".sh_entsize",
				f"actual={section.sh_entsize}, "
				f"required={SECTION_ENTRY_SIZES[section.sh_type]}")
		if section.sh_entsize and not section.sh_flags & SHF_COMPRESSED:
			require(section.sh_size % section.sh_entsize == 0, file.path,
				label + ".sh_size", "size is not a whole number of entries")
		if section.sh_flags & SHF_COMPRESSED:
			require(not section.sh_flags & SHF_ALLOC
				and section.sh_type == SHT_PROGBITS, file.path, label,
				"only nonallocated PROGBITS compression is supported")
		if section.sh_flags & SHF_TLS:
			require(section.sh_flags & SHF_ALLOC
				and section.sh_type in (SHT_PROGBITS, SHT_NOBITS),
				file.path, label, "TLS requires allocated PROGBITS/NOBITS")
		if section.sh_flags & SHF_ALLOC:
			validate_allocated_section(file, label, section, loads, tls)
			addresses = tls_addresses if section.sh_flags & SHF_TLS else ordinary_addresses
			addresses.append((section.sh_addr, section.sh_addr + section.sh_size, label))
		else:
			require(section.sh_addr == 0, file.path, label + ".sh_addr",
				"nonallocated section has an address")
		link, info = resolve_section_references(file, label, section, sections, names)
		if section.sh_type in (SHT_SYMTAB, SHT_DYNSYM, SHT_DYNAMIC):
			require(section.sh_type not in unique, file.path, label,
				"multiple symbol/dynamic tables of the same type are unsupported")
			unique.add(section.sh_type)
		if section.sh_type in (SHT_SYMTAB, SHT_DYNSYM):
			count = section.sh_size // SYMBOL.size
			check_limit(file.path, label + " symbol count", count, MAX_STATIC_SYMBOLS)
			require(1 <= section.sh_info <= count, file.path, label + ".sh_info",
				f"local-symbol boundary={section.sh_info}, symbol count={count}")
			if section.sh_type == SHT_SYMTAB:
				check_limit(file.path, label + " linked string table size",
					sections[section.sh_link].sh_size, MAX_STATIC_NAME_TABLE)
		if section.sh_type == SHT_STRTAB and section.sh_size:
			require(file.read_at(section.sh_offset, 1) == b"\0"
				and file.read_at(section.sh_offset + section.sh_size - 1, 1) == b"\0",
				file.path, label, "string table first/final byte must be NUL")
		if section.sh_type == SHT_GNU_VERSYM:
			require(section.sh_size // 2
				== sections[section.sh_link].sh_size // SYMBOL.size,
				file.path, label, "version-symbol count differs from DYNSYM")
		if section.sh_type == SHT_DYNAMIC:
			require(any(p.p_type == PT_DYNAMIC
				and p.p_offset == section.sh_offset and p.p_vaddr == section.sh_addr
				and p.p_filesz == section.sh_size for p in programs),
				file.path, label, "dynamic section does not match PT_DYNAMIC")
		result.append(Section(index, section, names[index], link, info))
	require(not any(p.p_type == PT_DYNAMIC for p in programs)
		or SHT_DYNAMIC in unique, file.path, "PT_DYNAMIC",
		"dynamic segment has no SHT_DYNAMIC section")
	check_disjoint(file.path, ordinary_addresses)
	check_disjoint(file.path, tls_addresses)
	for start, size, label in (
		(header.e_shoff, header.e_shnum * SECTION_HEADER.size, "section table"),
		(sections[header.e_shstrndx].sh_offset,
			sections[header.e_shstrndx].sh_size, "section name table"),
	):
		require(not any(overlaps(start, size, left, right - left)
			for left, right in loader_ranges), file.path, label,
			"section bookkeeping overlaps loader-visible bytes")
	return tuple(result)

def resolve_section_references(file, label, section, sections, names):
	link = section.sh_link
	info = section.sh_info
	target_types = LINK_TARGET_TYPES.get(section.sh_type)
	if target_types or section.sh_flags & SHF_LINK_ORDER:
		require(0 < link < len(sections), file.path, label + ".sh_link",
			f"invalid section index={link}")
		if target_types:
			require(sections[link].sh_type in target_types, file.path,
				label + ".sh_link", f"wrong target section type={sections[link].sh_type}")
			if sections[link].sh_type == SHT_STRTAB:
				require(sections[link].sh_size > 0, file.path, label + ".sh_link",
					"linked string table is empty")
		link = names[link]
	else:
		require(link == 0, file.path, label + ".sh_link",
			"unsupported nonzero section reference")
	numeric_info = section.sh_type in (
		SHT_SYMTAB, SHT_DYNSYM, SHT_GNU_VERDEF, SHT_GNU_VERNEED)
	if numeric_info:
		require(not section.sh_flags & SHF_INFO_LINK, file.path, label + ".sh_info",
			"numeric count conflicts with SHF_INFO_LINK")
	elif section.sh_type in (SHT_REL, SHT_RELA) or section.sh_flags & SHF_INFO_LINK:
		require(0 <= info < len(sections), file.path, label + ".sh_info",
			f"invalid section index={info}")
		info = names[info] if info else 0
	else:
		require(info == 0, file.path, label + ".sh_info",
			"unsupported nonzero auxiliary information")
	return link, info

def validate_allocated_section(file, label, section, loads, tls):
	if section.sh_flags & SHF_TLS:
		require(tls is not None, file.path, label, "TLS section has no PT_TLS")
		require(section.sh_addralign <= max(1, tls.p_align), file.path,
			label + ".sh_addralign", "section alignment exceeds PT_TLS alignment")
		require(contains(tls.p_vaddr, tls.p_memsz, section.sh_addr, section.sh_size),
			file.path, label, "TLS section lies outside PT_TLS template")
		if section.sh_type == SHT_NOBITS:
			validate_nobits_offset(file, label, section, tls)
			return
		require(contains(tls.p_offset, tls.p_filesz, section.sh_offset, section.sh_size)
			and section.sh_offset - tls.p_offset == section.sh_addr - tls.p_vaddr,
			file.path, label, "TLS initial data has no PT_TLS file mapping")
	matches = [p for p in loads if contains(p.p_vaddr, p.p_memsz,
		section.sh_addr, section.sh_size)
		and (not section.sh_flags & SHF_WRITE or p.p_flags & PF_W)
		and (not section.sh_flags & SHF_EXECINSTR or p.p_flags & PF_X)]
	if section.sh_type == SHT_NOBITS:
		matches = [p for p in matches
			if section.sh_addr >= p.p_vaddr + p.p_filesz]
		require(len(matches) == 1, file.path, label,
			"NOBITS section has no unique PT_LOAD zero-fill mapping")
		validate_nobits_offset(file, label, section, matches[0])
	else:
		require(any(contains(p.p_offset, p.p_filesz, section.sh_offset, section.sh_size)
			and section.sh_offset - p.p_offset == section.sh_addr - p.p_vaddr
			for p in matches), file.path, label,
			"allocated section has no compatible PT_LOAD file/address mapping")

def validate_nobits_offset(file, label, section, program):
	require(section.sh_addr >= program.p_vaddr + program.p_filesz,
		file.path, label, "NOBITS starts before the template zero-fill area")
	end = program.p_offset + program.p_filesz
	conceptual = program.p_offset + (section.sh_addr - program.p_vaddr)
	require(end <= section.sh_offset <= conceptual, file.path, label + ".sh_offset",
		f"NOBITS conceptual offset={section.sh_offset}, permitted=[{end},{conceptual}]")

def loader_visible(section, parsed):
	header = section.header
	return bool(header.sh_flags & SHF_ALLOC) or (
		header.sh_type != SHT_NOBITS
		and any(overlaps(header.sh_offset, header.sh_size, start, end - start)
			for start, end in parsed.loader_ranges))

def qualified_debug(section, parsed):
	header = section.header
	flags = header.sh_flags & ~SHF_COMPRESSED
	if (section.name not in DEBUG_NAMES or loader_visible(section, parsed)
			or header.sh_type != SHT_PROGBITS
			or header.sh_addr or header.sh_link or header.sh_info):
		return False
	if section.name in DEBUG_STRING_NAMES:
		return ((flags == 0 and header.sh_entsize == 0)
			or (flags == SHF_MERGE | SHF_STRINGS and header.sh_entsize == 1))
	return flags == 0 and header.sh_entsize == 0

def validate_compression(file, section):
	header = section.header
	label = f"section[{section.index}] {section.name!r} compression"
	require(header.sh_size > COMPRESSION_HEADER.size, file.path, label,
		"missing compression header or payload")
	kind, reserved, size, alignment = COMPRESSION_HEADER.unpack(
		file.read_at(header.sh_offset, COMPRESSION_HEADER.size))
	require(kind in COMPRESSION_TYPES, file.path, label,
		f"unsupported compression kind={kind}")
	require(reserved == 0, file.path, label, "nonzero reserved field")
	require(0 < size <= MAX_INPUT_SIZE, file.path, label,
		f"uncompressed length={size}, supported range=[1,{MAX_INPUT_SIZE}]")
	require(alignment > 0, file.path, label, "zero uncompressed alignment")
	check_alignment(file.path, label, alignment)
	if header.sh_entsize:
		require(size % header.sh_entsize == 0, file.path, label,
			"uncompressed length is not a whole number of entries")

def classify_sections(file, parsed):
	by_name = {section.name: section for section in parsed.sections[1:]}
	shstr = parsed.sections[parsed.header.e_shstrndx]
	symtab = next((section for section in by_name.values()
		if section.header.sh_type == SHT_SYMTAB), None)
	strtab = parsed.sections[symtab.header.sh_link] if symtab else None
	for section in (symtab, strtab):
		if section is None:
			continue
		header = section.header
		require(not loader_visible(section, parsed) and header.sh_flags == 0,
			file.path, f"private static metadata {section.name!r}",
			"must be ordinary, nonallocated and outside every loader payload")
		require(section.index != shstr.index, file.path,
			f"private static metadata {section.name!r}",
			"section-name and private symbol-name tables must be distinct")
	if strtab:
		header = strtab.header
		require(header.sh_size > 0 and header.sh_addr == 0
			and header.sh_link == 0 and header.sh_info == 0
			and header.sh_addralign in (0, 1) and header.sh_entsize == 0,
			file.path, "private static string table", "invalid STRTAB metadata")
	roles = {}
	for section in by_name.values():
		header = section.header
		if header.sh_flags & SHF_COMPRESSED:
			validate_compression(file, section)
		for reference in (section.link, section.info):
			require(reference != shstr.name, file.path,
				f"section {section.name!r} reference",
				"section-name table has an unsupported external dependency")
			if symtab:
				require(reference != symtab.name, file.path,
					f"section {section.name!r} reference",
					"references to static symbol entries are unsupported")
				require(reference != strtab.name or section.index == symtab.index,
					file.path, f"section {section.name!r} reference",
					"static string table must be private to SHT_SYMTAB")
		if section.index == shstr.index:
			role = "section_names"
		elif symtab and section.index == symtab.index:
			role = "static_symbols"
		elif strtab and section.index == strtab.index:
			role = "static_names"
		elif header.sh_flags & SHF_ALLOC:
			role = "allocated"
		elif qualified_debug(section, parsed):
			role = "debug"
		else:
			role = "retained"
		if header.sh_type in (SHT_REL, SHT_RELA):
			target = by_name.get(section.info)
			require(target is None or target.name not in DEBUG_NAMES,
				file.path, f"section {section.name!r}", "debug relocations are unsupported")
		roles[section.name] = role
	return SectionClasses(by_name, roles, symtab, strtab)

def compare_section_metadata(source, copy, left, right, role, exact_offset):
	label = f"section {left.name!r}"
	ignored = {"sh_name", "sh_link", "sh_info"}
	if not exact_offset:
		ignored.add("sh_offset")
	if role in ("section_names", "static_symbols", "static_names"):
		ignored.add("sh_size")
	for field in SectionHeader._fields:
		if field in ignored:
			continue
		before, after = getattr(left.header, field), getattr(right.header, field)
		require(before == after, copy.path, label + "." + field,
			f"source={before}, copy={after}")
	require(left.link == right.link, copy.path, label + ".sh_link",
		f"resolved source={left.link!r}, copy={right.link!r}")
	if role != "static_symbols":
		require(left.info == right.info, copy.path, label + ".sh_info",
			f"resolved source={left.info!r}, copy={right.info!r}")

def masked_header_bytes(data, offset):
	if not any(start < offset + len(data) and end > offset
			for start, end in HEADER_BOOKKEEPING_RANGES):
		return data
	result = bytearray(data)
	for start, end in HEADER_BOOKKEEPING_RANGES:
		left, right = max(start, offset), min(end, offset + len(data))
		if left < right:
			result[left - offset:right - offset] = bytes(right - left)
	return result

def compare_range(source, copy, source_offset, copy_offset, size,
		label, mask_header=False):
	check_range(source.path, label, source_offset, size, source.size)
	check_range(copy.path, label, copy_offset, size, copy.size)
	require(not mask_header or source_offset == copy_offset,
		copy.path, label, "header masking requires identical absolute offsets")
	digest = hashlib.sha256()
	done = 0
	while done < size:
		amount = min(size - done, MAX_READ_SIZE)
		left = source.read_at(source_offset + done, amount)
		right = copy.read_at(copy_offset + done, amount)
		if mask_header:
			left = masked_header_bytes(left, source_offset + done)
			right = masked_header_bytes(right, copy_offset + done)
		if left != right:
			index = next(i for i, (a, b) in enumerate(zip(left, right)) if a != b)
			raise ElfIdentityError(
				f"{copy.path}: {label}: byte differs at "
				f"source offset={source_offset + done + index}, "
				f"copy offset={copy_offset + done + index}")
		digest.update(left)
		done += amount
	return digest.hexdigest()

def report_name(name):
	return {"text": name.decode("utf-8", "backslashreplace"), "hex": name.hex()}

def report_reference(reference):
	return report_name(reference) if isinstance(reference, bytes) else reference

def report_section(section):
	return {
		"index": section.index,
		"name": report_name(section.name),
		"header": dict(section.header._asdict()),
		"resolved_link": report_reference(section.link),
		"resolved_info": report_reference(section.info),
	}

def compare_nonloaded(source, copy, before, after, left_classes, right_classes):
	left_names, right_names = set(left_classes.by_name), set(right_classes.by_name)
	added = right_names - left_names
	require(not added, copy.path, "section identities",
		f"copy invented section names={sorted(added)!r}")
	removed = left_names - right_names
	for name in removed:
		require(left_classes.roles[name] == "debug", copy.path,
			f"section {name!r}", "only qualified source debug sections may disappear")
	for section in left_classes.by_name.values():
		if section.name in removed:
			continue
		for reference in (section.link, section.info):
			require(reference not in removed, source.path,
				f"section {section.name!r} reference",
				f"retained metadata depends on removed section={reference!r}")
	compared = []
	for name in sorted(right_names):
		left = left_classes.by_name[name]
		right = right_classes.by_name[name]
		role = left_classes.roles[name]
		require(role == right_classes.roles[name], copy.path, f"section {name!r}",
			f"role changed from {role} to {right_classes.roles[name]}")
		exact_offset = loader_visible(left, before) or loader_visible(right, after)
		compare_section_metadata(source, copy, left, right, role, exact_offset)
		record = {
			"role": role,
			"source": report_section(left),
			"copy": report_section(right),
		}
		if left.header.sh_type == SHT_NOBITS:
			record["content"] = "NOBITS; semantic metadata compared"
		elif role == "allocated":
			record["content"] = "compared in loader union"
		elif role in ("section_names", "static_symbols", "static_names"):
			record["content"] = "validated bookkeeping; see allowance inventory"
		else:
			record["sha256"] = compare_range(source, copy,
				left.header.sh_offset, right.header.sh_offset, left.header.sh_size,
				f"section {name!r} payload")
		compared.append(record)
	return {
		"sections": compared,
		"removed_debug_sections": [
			report_section(left_classes.by_name[name]) for name in sorted(removed)],
	}

def validate_zero_gaps(file, parsed):
	covered = merge_ranges(tuple((start, end) for start, end, label
		in parsed.file_regions) + parsed.loader_ranges)
	gaps = []
	position = 0
	for start, end in covered + ((file.size, file.size),):
		if start > position:
			offset = position
			for data in file.iter_range(position, start - position):
				if data.count(0) != len(data):
					index = next(index for index, byte in enumerate(data) if byte)
					raise ElfIdentityError(
						f"{file.path}: unaccounted nonloaded bytes: "
						f"nonzero byte at offset={offset + index}")
				offset += len(data)
			gaps.append({"offset": position, "size": start - position})
		position = end
	return gaps

def _symbol_entries(file, section):
	header = section.header
	count = header.sh_size // SYMBOL.size
	require(header.sh_entsize == SYMBOL.size
		and header.sh_size % SYMBOL.size == 0 and count > 0,
		file.path, f"symbol table {section.name!r}", "invalid entry size/count")
	check_limit(file.path, "symbol count", count, MAX_STATIC_SYMBOLS)
	require(1 <= header.sh_info <= count, file.path,
		f"symbol table {section.name!r}.sh_info", "invalid local-symbol boundary")
	batch_count = MAX_READ_SIZE // SYMBOL.size
	for first in range(0, count, batch_count):
		data = file.read_at(header.sh_offset + first * SYMBOL.size,
			min(batch_count, count - first) * SYMBOL.size)
		for relative, fields in enumerate(SYMBOL.iter_unpack(data)):
			yield first + relative, fields

def _validate_symbol_fields(file, parsed, classes, section, index, fields):
	name_offset, info, other, section_index, value, size = fields
	label = f"{section.name!r} symbol[{index}]"
	if index == 0:
		require(not any(fields), file.path, label, "first symbol must be all zero")
	binding, kind = info >> 4, info & 15
	require(binding in SYMBOL_BINDINGS and kind in SYMBOL_TYPES,
		file.path, label, f"unsupported binding={binding} or type={kind}")
	require(other & ~3 == 0, file.path, label + ".st_other",
		f"unsupported visibility/reserved bits={other:#x}")
	require((binding == STB_LOCAL) == (index < section.header.sh_info),
		file.path, label, "binding disagrees with local/nonlocal boundary")
	require(section_index in (0, SHN_ABS)
		or 0 < section_index < len(parsed.sections), file.path,
		label + ".st_shndx", f"unsupported or invalid section index={section_index}")
	target = parsed.sections[section_index] if (
		0 < section_index < len(parsed.sections)) else None
	if target:
		require(classes.roles[target.name] not in (
			"section_names", "static_symbols", "static_names"),
			file.path, label + ".st_shndx",
			"symbol targets mutable section/string/symbol bookkeeping")
	if kind == STT_FILE:
		require(binding == STB_LOCAL and section_index == SHN_ABS,
			file.path, label, "FILE symbol requires local binding and SHN_ABS")
	elif kind == STT_SECTION:
		require(binding == STB_LOCAL and target is not None, file.path, label,
			"SECTION symbol requires local binding and an ordinary section index")
	return target.name if target else section_index

def _read_symbol_name(file, table, name_offset, label):
	header = table.header
	require(0 <= name_offset < header.sh_size, file.path, label + ".st_name",
		f"offset={name_offset}, string table size={header.sh_size}")
	check_limit(file.path, "resolved symbol name bytes",
		file.resolved_name_bytes + 1, MAX_RESOLVED_NAME_BYTES)
	if name_offset == 0:
		file.account_symbol_name(1)
		return b""
	offset = header.sh_offset + name_offset
	remaining = min(header.sh_size - name_offset, MAX_SYMBOL_NAME + 1,
		MAX_RESOLVED_NAME_BYTES - file.resolved_name_bytes)
	result = bytearray()
	while remaining:
		data = file.read_name_chunk(offset, min(remaining, NAME_READ_SIZE))
		end = data.find(b"\0")
		if end >= 0:
			result.extend(data[:end])
			check_limit(file.path, label + " name length", len(result), MAX_SYMBOL_NAME)
			file.account_symbol_name(len(result) + 1)
			return bytes(result)
		result.extend(data)
		offset += len(data)
		remaining -= len(data)
	check_limit(file.path, "resolved symbol name bytes",
		file.resolved_name_bytes + len(result) + 1, MAX_RESOLVED_NAME_BYTES)
	check_limit(file.path, label + " name length", len(result), MAX_SYMBOL_NAME)
	raise ElfIdentityError(f"{file.path}: {label}: unterminated static symbol name")

def iter_symbols(file, parsed, classes):
	if classes.symtab is None:
		return
	tls = next((program for program in parsed.programs
		if program.p_type == PT_TLS), None)
	for index, fields in _symbol_entries(file, classes.symtab):
		target = _validate_symbol_fields(
			file, parsed, classes, classes.symtab, index, fields)
		name_offset, info, other, section_index, value, size = fields
		name = _read_symbol_name(file, classes.strtab, name_offset,
			f"{classes.symtab.name!r} symbol[{index}]")
		kind = info & 15
		removable = (kind == STT_FILE and info >> 4 == STB_LOCAL
			and section_index == SHN_ABS and other == 0 and value == 0 and size == 0)
		if kind == STT_SECTION:
			header = parsed.sections[section_index].header
			base = header.sh_addr
			if header.sh_flags & SHF_TLS:
				base -= tls.p_vaddr
			removable = (info >> 4 == STB_LOCAL and not name
				and other == 0 and size == 0 and value == base)
		yield SymbolRecord(index, name_offset, section_index, name,
			info, other, target, value, size, removable)

def validate_dynamic_symbol_indexes(file, parsed, classes):
	section = next((item for item in parsed.sections
		if item.header.sh_type == SHT_DYNSYM), None)
	if section is None:
		return {}
	strings = parsed.sections[section.header.sh_link]
	require(section.header.sh_flags & SHF_ALLOC
		and strings.header.sh_flags & SHF_ALLOC, file.path, "DYNSYM",
		"dynamic symbol and name tables must be allocated")
	references = {}
	for index, fields in _symbol_entries(file, section):
		target = _validate_symbol_fields(file, parsed, classes, section, index, fields)
		name_offset, info, other, section_index, value, size = fields
		require(name_offset < strings.header.sh_size, file.path,
			f"DYNSYM symbol[{index}].st_name",
			f"offset={name_offset}, string table size={strings.header.sh_size}")
		if isinstance(target, bytes):
			require(classes.roles[target] == "allocated", file.path,
				f"DYNSYM symbol[{index}].st_shndx",
				f"dynamic symbol targets nonallocated section={target!r}")
			references[section_index] = target
	return references

def _symbol_digest_update(digest, symbol):
	digest.update(struct.pack("<QBBQQ", len(symbol.name), symbol.info,
		symbol.other, symbol.value, symbol.size))
	digest.update(symbol.name)
	if isinstance(symbol.target, bytes):
		digest.update(b"S" + struct.pack("<Q", len(symbol.target)))
		digest.update(symbol.target)
	else:
		digest.update(b"I" + struct.pack("<Q", symbol.target))

def compare_static_symbols(source, copy, before, after, left_classes, right_classes):
	require(bool(left_classes.symtab) == bool(right_classes.symtab),
		copy.path, "static symbol table", "table added or removed")
	if left_classes.symtab is None:
		return {"present": False}
	left_stream = iter_symbols(source, before, left_classes)
	right_stream = iter_symbols(copy, after, right_classes)
	right = next(right_stream, None)
	count = 0
	name_offsets = 0
	section_indexes = 0
	removed = {"FILE": 0, "SECTION": 0}
	removed_digest = hashlib.sha256()
	retained_digest = hashlib.sha256()
	for left in left_stream:
		if right is not None and left.semantics() == right.semantics():
			if isinstance(left.target, bytes):
				require(left.target in right_classes.by_name, copy.path,
					f"static symbol[{left.index}]",
					f"retained symbol target disappeared: {left.target!r}")
			count += 1
			name_offsets += left.name_offset != right.name_offset
			section_indexes += left.section_index != right.section_index
			_symbol_digest_update(retained_digest, left)
			right = next(right_stream, None)
		elif left.removable:
			removed["FILE" if left.info & 15 == STT_FILE else "SECTION"] += 1
			_symbol_digest_update(removed_digest, left)
		else:
			raise ElfIdentityError(
				f"{copy.path}: retained static symbol[{left.index}] "
				f"name={left.name!r}: missing, reordered or changed; "
				f"copy index={right.index if right else 'end'}")
	require(right is None, copy.path, "static symbol table",
		f"copy inserted or reordered symbol at index={right.index if right else 'end'}")
	return {
		"present": True,
		"source_entries": left_classes.symtab.header.sh_size // SYMBOL.size,
		"copy_entries": right_classes.symtab.header.sh_size // SYMBOL.size,
		"retained_entries": count,
		"removed_source_local_bookkeeping": removed,
		"removed_semantics_sha256": removed_digest.hexdigest(),
		"retained_semantics_sha256": retained_digest.hexdigest(),
		"repacked_name_offsets": name_offsets,
		"repacked_section_indexes": section_indexes,
		"source_local_boundary": left_classes.symtab.header.sh_info,
		"copy_local_boundary": right_classes.symtab.header.sh_info,
		"private_name_table": {
			"source": report_section(left_classes.strtab),
			"copy": report_section(right_classes.strtab),
			"policy": "resolved names compared; unused bytes are private string bookkeeping",
		},
	}

def subtract_ranges(ranges, covered):
	result = []
	index = 0
	for start, end in ranges:
		position = start
		while index < len(covered) and covered[index][1] <= position:
			index += 1
		cursor = index
		while cursor < len(covered) and covered[cursor][0] < end:
			left, right = covered[cursor]
			if position < left:
				result.append((position, left))
			position = max(position, min(right, end))
			if right >= end:
				break
			cursor += 1
		if position < end:
			result.append((position, end))
		index = cursor
	return tuple(result)

def compare_program_payloads(source, copy, before, after):
	require(len(before.programs) == len(after.programs),
		copy.path, "program table", "program count changed")
	for index, (left, right) in enumerate(zip(before.programs, after.programs)):
		for field in ProgramHeader._fields:
			expected, actual = getattr(left, field), getattr(right, field)
			require(expected == actual, copy.path, f"program[{index}].{field}",
				f"source={expected}, copy={actual}")
	require(before.loader_ranges == after.loader_ranges,
		copy.path, "loader coverage", "loader file ranges changed")
	coverage = []
	for start, end in before.loader_ranges:
		digest = compare_range(source, copy, start, start, end - start,
			"loader payload including padding", mask_header=True)
		coverage.append({
			"offset": start,
			"size": end - start,
			"normalized_sha256": digest,
		})
	loads = merge_ranges(mapping["exposed_file_range"]
		for mapping in before.load_mappings)
	return {
		"ordered_program_headers": [dict(program._asdict()) for program in before.programs],
		"load_mappings": before.load_mappings,
		"independent_record_header_aliases": "refused for every non-NULL/non-LOAD payload",
		"merged_loader_ranges": coverage,
		"unique_loader_bytes": sum(end - start for start, end in before.loader_ranges),
		"non_load_payload_outside_loads": [
			{"offset": start, "size": end - start}
			for start, end in subtract_ranges(before.loader_ranges, loads)],
		"program_table": {
			"offset": before.header.e_phoff,
			"size": before.header.e_phnum * PROGRAM_HEADER.size,
			"comparison": "all ordered fields; exact bytes with no masks",
		},
	}

def compare_elf_headers(source, copy, before, after):
	left = masked_header_bytes(before.header_bytes, 0)
	right = masked_header_bytes(after.header_bytes, 0)
	if left != right:
		index = next(i for i, (a, b) in enumerate(zip(left, right)) if a != b)
		raise ElfIdentityError(f"{copy.path}: ELF header: protected byte "
			f"differs at offset={index}")
	return [{
		"field": field,
		"offset": start,
		"size": end - start,
		"source": getattr(before.header, field),
		"copy": getattr(after.header, field),
		"changed_bytes": [{
			"offset": offset,
			"source": before.header_bytes[offset],
			"copy": after.header_bytes[offset],
		} for offset in range(start, end)
			if before.header_bytes[offset] != after.header_bytes[offset]],
	} for field, (start, end) in zip(
		("e_shoff", "e_shnum", "e_shstrndx"), HEADER_BOOKKEEPING_RANGES)]

def report_subject(file, parsed, digest):
	header = dict(parsed.header._asdict())
	header["e_ident_hex"] = header.pop("e_ident").hex()
	return {
		"path": file.path,
		"size": file.size,
		"sha256": digest,
		"header": header,
		"acquisition": {
			"started_unix_ns": file.started_ns,
			"finished_unix_ns": file.finished_ns,
			"initial_stat": file.initial_stat,
			"final_stat": file.final_stat,
			"descriptor_and_path_unchanged": True,
		},
		"reads": {
			"count": file.read_count,
			"bytes": file.bytes_read,
			"max_request": file.max_request,
			"symbol_name_read_count": file.name_read_count,
			"symbol_name_read_bytes": file.name_read_bytes,
			"resolved_symbol_name_bytes": file.resolved_name_bytes,
			"name_cache_bytes": 0,
		},
	}

def build_report(source, copy, before, after, source_digest, copy_digest,
		headers, programs, sections, symbols, dynamic_indexes, gaps):
	return {
		"schema_version": 1,
		"load_equivalent": True,
		"exact_sha256_equal": source_digest == copy_digest,
		"source": report_subject(source, before, source_digest),
		"copy": report_subject(copy, after, copy_digest),
		"profile": {
			"name": "ELF64-LE-x86_64-ordinary-exec-dyn-v1",
			"host": "Linux/WSL",
			"load_mapping": "4096-byte pages; nonempty fully backed rounded file pages; "
				"disjoint rounded virtual ranges; file-page aliases merged per exposure",
			"load_zero_fill": "writable BSS only; nonfinal memory end page-aligned; "
				"final BSS rounded to memory page end",
			"header_alias_policy": "non-NULL/non-LOAD payloads may not intersect mutable fields",
			"elf_class": 64,
			"endianness": "little",
			"machine": EM_X86_64,
			"types": [ET_EXEC, ET_DYN],
			"abis": [0, 3],
			"numbering": "ordinary only",
			"program_types": sorted(PROGRAM_TYPES),
			"section_types": sorted(SECTION_TYPES),
			"section_flags_mask": SECTION_FLAGS,
			"symbol_bindings": sorted(SYMBOL_BINDINGS),
			"symbol_types": sorted(SYMBOL_TYPES),
			"symbol_visibility_values": [0, 1, 2, 3],
			"symbol_special_indexes": [0, SHN_ABS],
			"compression_types": COMPRESSION_TYPES,
			"compression_validation": "header envelope only; no decompression",
			"debug_names": [name.decode("ascii") for name in sorted(DEBUG_NAMES)],
			"debug_types": [SHT_PROGBITS],
			"debug_flags": "0 or COMPRESSED; debug_str/line_str also MERGE|STRINGS",
			"section_bookkeeping": "validated tables and resolved raw identities",
			"static_bookkeeping": "source-only qualified local FILE/SECTION deletions",
			"uncovered_nonloaded_bytes": "zero padding only",
			"stability": "observed stat/path stability; not a lock; "
				"unchanged timestamps can conceal writes",
			"scope": "artifact relationship; not authenticity or runtime/linker equivalence",
		},
		"limits_per_file": {
			"input_bytes": MAX_INPUT_SIZE,
			"read_bytes": MAX_READ_SIZE,
			"program_headers": MAX_PROGRAM_HEADERS,
			"sections": MAX_SECTIONS,
			"section_name_table_bytes": MAX_SECTION_NAME_TABLE,
			"section_name_bytes": MAX_SECTION_NAME,
			"symbols_per_table": MAX_STATIC_SYMBOLS,
			"private_static_name_table_bytes": MAX_STATIC_NAME_TABLE,
			"symbol_name_bytes": MAX_SYMBOL_NAME,
			"resolved_symbol_name_bytes_including_nul": MAX_RESOLVED_NAME_BYTES,
			"symbol_name_read_bytes": MAX_NAME_READ_BYTES,
			"symbol_name_read_count": MAX_NAME_READS,
			"symbol_name_read_chunk_bytes": NAME_READ_SIZE,
			"name_cache_bytes": MAX_NAME_CACHE_BYTES,
			"interpreter_bytes": MAX_INTERPRETER_SIZE,
			"load_page_bytes": LOAD_PAGE_SIZE,
			"uncompressed_debug_bytes": MAX_INPUT_SIZE,
		},
		"allowed_header_fields": headers,
		"program_comparison": programs,
		"section_comparison": sections,
		"static_symbol_comparison": symbols,
		"dynamic_symbol_section_indexes": [{
			"index": index, "identity": report_name(name)
		} for index, name in sorted(dynamic_indexes.items())],
		"zero_nonloaded_padding": gaps,
	}

def compare_elf(source_path, copy_path):
	"""Verify one directional pair in a single, descriptor-owned acquisition."""
	with ElfFile(source_path) as source, ElfFile(copy_path) as copy:
		before = parse_elf(source)
		after = parse_elf(copy)
		left_classes = classify_sections(source, before)
		right_classes = classify_sections(copy, after)
		headers = compare_elf_headers(source, copy, before, after)
		left_indexes = validate_dynamic_symbol_indexes(source, before, left_classes)
		right_indexes = validate_dynamic_symbol_indexes(copy, after, right_classes)
		require(left_indexes == right_indexes, copy.path, "DYNSYM section indexes",
			"an embedded section index changed its resolved raw section identity")
		sections = compare_nonloaded(source, copy, before, after,
			left_classes, right_classes)
		symbols = compare_static_symbols(source, copy, before, after,
			left_classes, right_classes)
		programs = compare_program_payloads(source, copy, before, after)
		gaps = {
			"source": validate_zero_gaps(source, before),
			"copy": validate_zero_gaps(copy, after),
		}
		source_digest = source.sha256_range(0, source.size)
		copy_digest = copy.sha256_range(0, copy.size)
		source.verify_unchanged()
		copy.verify_unchanged()
		return build_report(source, copy, before, after, source_digest, copy_digest,
			headers, programs, sections, symbols, left_indexes, gaps)

def parse_args(argv=None):
	parser = argparse.ArgumentParser(
		description="Verify a Debug ELF and its debug-stripped execution copy "
			"with bounded reads, exact SHA-256 identities and strict stripping rules.")
	parser.add_argument("source", help="original ELF64 little-endian x86-64 executable")
	parser.add_argument("copy", help="execution copy to compare directionally")
	return parser.parse_args(argv)

def main(argv=None):
	args = parse_args(argv)
	try:
		report = compare_elf(args.source, args.copy)
		print(json.dumps(report, indent=2, sort_keys=True))
	except (ElfIdentityError, OSError) as error:
		print(f"elf_identity: {error}", file=sys.stderr)
		return 1
	return 0

if __name__ == "__main__":
	sys.exit(main())
