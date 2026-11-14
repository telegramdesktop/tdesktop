/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/widget_lockdown_linux.h"

#ifdef Q_OS_LINUX

#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/futex.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sched.h>
#include <span>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef __NR_close_range
#define __NR_close_range 436
#endif // __NR_close_range

#ifndef __NR_clone3
#define __NR_clone3 435
#endif // __NR_clone3

#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#endif // __NR_landlock_create_ruleset

#ifndef __NR_landlock_restrict_self
#define __NR_landlock_restrict_self 446
#endif // __NR_landlock_restrict_self

#ifndef LANDLOCK_CREATE_RULESET_VERSION
#define LANDLOCK_CREATE_RULESET_VERSION (1U << 0)
#endif // LANDLOCK_CREATE_RULESET_VERSION

#ifndef LANDLOCK_ACCESS_FS_EXECUTE
#define LANDLOCK_ACCESS_FS_EXECUTE (1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE (1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE (1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR (1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR (1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE (1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR (1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR (1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG (1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK (1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO (1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK (1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM (1ULL << 12)
#endif // LANDLOCK_ACCESS_FS_EXECUTE

#ifndef LANDLOCK_ACCESS_FS_REFER
#define LANDLOCK_ACCESS_FS_REFER (1ULL << 13)
#endif // LANDLOCK_ACCESS_FS_REFER

#ifndef LANDLOCK_ACCESS_FS_TRUNCATE
#define LANDLOCK_ACCESS_FS_TRUNCATE (1ULL << 14)
#endif // LANDLOCK_ACCESS_FS_TRUNCATE

#ifndef LANDLOCK_ACCESS_FS_IOCTL_DEV
#define LANDLOCK_ACCESS_FS_IOCTL_DEV (1ULL << 15)
#endif // LANDLOCK_ACCESS_FS_IOCTL_DEV

#ifndef LANDLOCK_ACCESS_NET_BIND_TCP
#define LANDLOCK_ACCESS_NET_BIND_TCP (1ULL << 0)
#define LANDLOCK_ACCESS_NET_CONNECT_TCP (1ULL << 1)
#endif // LANDLOCK_ACCESS_NET_BIND_TCP

#ifndef LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET
#define LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET (1ULL << 0)
#define LANDLOCK_SCOPE_SIGNAL (1ULL << 1)
#endif // LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET

#ifndef MADV_GUARD_INSTALL
#define MADV_GUARD_INSTALL 102
#endif // MADV_GUARD_INSTALL

#ifndef MADV_GUARD_REMOVE
#define MADV_GUARD_REMOVE 103
#endif // MADV_GUARD_REMOVE

namespace Platform::WidgetHelper {
namespace {

constexpr auto kDirectoryBufferSize = 4096;
constexpr auto kMaxClosedDescriptor = rlim_t(1) << 20;

struct DirectoryEntry {
	uint64_t inode = 0;
	int64_t offset = 0;
	uint16_t length = 0;
	uint8_t type = 0;
	char name[1] = {};
};

struct Threads {
	int count = 0;
	int error = 0;
};

struct RulesetAttr {
	uint64_t fs = 0;
	uint64_t net = 0;
	uint64_t scoped = 0;
};

[[nodiscard]] int ParseDescriptor(const char *name) {
	if (!*name) {
		return -1;
	}
	auto result = 0;
	for (; *name; ++name) {
		const auto digit = *name - '0';
		if (digit < 0 || digit > 9 || result > (INT_MAX - digit) / 10) {
			return -1;
		}
		result = result * 10 + digit;
	}
	return result;
}

template <typename Visit>
[[nodiscard]] int ForEachEntry(int directory, Visit visit) {
	char buffer[kDirectoryBufferSize] = {};
	while (true) {
		const auto size = syscall(
			SYS_getdents64,
			directory,
			buffer,
			sizeof(buffer));
		if (size <= 0) {
			return (size < 0) ? errno : 0;
		}
		auto changed = false;
		for (auto offset = long(0); offset < size;) {
			auto length = uint16_t(0);
			std::memcpy(
				&length,
				buffer + offset + offsetof(DirectoryEntry, length),
				sizeof(length));
			if (!length) {
				return EINVAL;
			}
			if (visit(buffer + offset + offsetof(DirectoryEntry, name))) {
				changed = true;
			}
			offset += length;
		}
		if (changed && lseek(directory, 0, SEEK_SET) < 0) {
			return errno;
		}
	}
}

[[nodiscard]] bool CloseListed(int directory, int first) {
	const auto error = ForEachEntry(directory, [&](const char *name) {
		const auto descriptor = ParseDescriptor(name);
		if (descriptor < first || descriptor == directory) {
			return false;
		}
		close(descriptor);
		return true;
	});
	return !error;
}

[[nodiscard]] bool CloseListedDescriptors(int first) {
	const auto directory = open(
		"/proc/self/fd",
		O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (directory < 0) {
		return false;
	}
	const auto result = CloseListed(directory, first);
	close(directory);
	return result;
}

void CloseUpToLimit(int first) {
	auto limit = rlimit();
	const auto last = getrlimit(RLIMIT_NOFILE, &limit)
		? kMaxClosedDescriptor
		: std::min(limit.rlim_cur, kMaxClosedDescriptor);
	for (auto descriptor = rlim_t(first); descriptor < last; ++descriptor) {
		close(int(descriptor));
	}
}

[[nodiscard]] std::string ErrorName(int error) {
	switch (error) {
	case EACCES: return "EACCES";
	case ENOENT: return "ENOENT";
	case ENOSYS: return "ENOSYS";
	case EPERM: return "EPERM";
	case EBADF: return "EBADF";
	case EINVAL: return "EINVAL";
	case EOPNOTSUPP: return "EOPNOTSUPP";
	case ENOSPC: return "ENOSPC";
	case ENOTSOCK: return "ENOTSOCK";
	case EAGAIN: return "EAGAIN";
	}
	return "errno" + std::to_string(error);
}

[[nodiscard]] Threads ThreadCount() {
	const auto directory = open(
		"/proc/self/task",
		O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (directory < 0) {
		return { .error = errno };
	}
	auto result = Threads();
	result.error = ForEachEntry(directory, [&](const char *name) {
		if (std::strcmp(name, ".") && std::strcmp(name, "..")) {
			++result.count;
		}
		return false;
	});
	close(directory);
	return result;
}

[[nodiscard]] int LandlockAbi() {
	const auto abi = syscall(
		__NR_landlock_create_ruleset,
		nullptr,
		0,
		LANDLOCK_CREATE_RULESET_VERSION);
	return (abi < 0) ? 0 : int(abi);
}

#if defined __x86_64__ || defined __aarch64__

#if defined __x86_64__
constexpr auto kArchitecture = uint32_t(AUDIT_ARCH_X86_64);
#elif defined __aarch64__
constexpr auto kArchitecture = uint32_t(AUDIT_ARCH_AARCH64);
#endif // __x86_64__ || __aarch64__

constexpr auto kNotImplemented = uint32_t(SECCOMP_RET_ERRNO | ENOSYS);
constexpr auto kNotPermitted = uint32_t(SECCOMP_RET_ERRNO | EPERM);
constexpr auto kFutexOptions = uint32_t(
	FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME);
constexpr auto kThreadFlags = uint32_t(CLONE_VM
	| CLONE_FS
	| CLONE_FILES
	| CLONE_SIGHAND
	| CLONE_THREAD
	| CLONE_SYSVSEM
	| CLONE_SETTLS
	| CLONE_PARENT_SETTID
	| CLONE_CHILD_CLEARTID);

constexpr int kChannelCalls[] = {
	__NR_read,
	__NR_write,
	__NR_writev,
	__NR_close,
	__NR_recvfrom,
	__NR_sendto,
	__NR_shutdown,
};

constexpr int kMemoryCalls[] = {
	__NR_munmap,
	__NR_mremap,
	__NR_brk,
};

constexpr uint32_t kAdvice[] = {
	MADV_NORMAL,
	MADV_RANDOM,
	MADV_SEQUENTIAL,
	MADV_WILLNEED,
	MADV_DONTNEED,
	MADV_FREE,
	MADV_GUARD_INSTALL,
	MADV_GUARD_REMOVE,
};

constexpr uint32_t kFutexOperations[] = {
	FUTEX_WAIT,
	FUTEX_WAKE,
	FUTEX_REQUEUE,
	FUTEX_CMP_REQUEUE,
	FUTEX_WAKE_OP,
	FUTEX_WAIT_BITSET,
	FUTEX_WAKE_BITSET,
};

constexpr int kClockCalls[] = {
	__NR_clock_gettime,
	__NR_clock_getres,
	__NR_gettimeofday,
#if defined __x86_64__
	__NR_time,
#endif // __x86_64__
	__NR_nanosleep,
	__NR_clock_nanosleep,
	__NR_sched_yield,
};

constexpr int kThreadCalls[] = {
	__NR_rt_sigprocmask,
	__NR_set_robust_list,
#ifdef __NR_rseq
	__NR_rseq,
#endif // __NR_rseq
	__NR_exit,
};

constexpr uint32_t kThreadNameOptions[] = {
	PR_SET_NAME,
	PR_GET_NAME,
};

constexpr int kSignalCalls[] = {
	__NR_rt_sigaction,
	__NR_rt_sigreturn,
	__NR_restart_syscall,
	__NR_getpid,
	__NR_gettid,
	__NR_exit_group,
};

constexpr int kRandomCalls[] = {
	__NR_getrandom,
};

constexpr int kKilledCalls[] = {
	__NR_execve,
	__NR_execveat,
	__NR_socket,
	__NR_connect,
	__NR_ptrace,
	__NR_process_vm_readv,
	__NR_process_vm_writev,
#if defined __x86_64__
	__NR_fork,
	__NR_vfork,
#endif // __x86_64__
};

struct FilterBuilder {
	std::vector<sock_filter> code;

	void stmt(uint16_t operation, uint32_t value);
	void jump(uint16_t operation, uint32_t value, uint8_t yes, uint8_t no);
	void answer(int number, uint32_t action);
	void allow(std::span<const int> numbers);
	void refuseFlag(int number, int argument, uint32_t flag, uint32_t action);
	void allowValues(
		int number,
		int argument,
		uint32_t ignored,
		std::span<const uint32_t> values,
		uint32_t otherwise);
};

[[nodiscard]] uint32_t ArgumentOffset(int argument) {
	return uint32_t(offsetof(seccomp_data, args)
		+ argument * sizeof(uint64_t));
}

void FilterBuilder::stmt(uint16_t operation, uint32_t value) {
	code.push_back({ .code = operation, .jt = 0, .jf = 0, .k = value });
}

void FilterBuilder::jump(
		uint16_t operation,
		uint32_t value,
		uint8_t yes,
		uint8_t no) {
	code.push_back({ .code = operation, .jt = yes, .jf = no, .k = value });
}

void FilterBuilder::answer(int number, uint32_t action) {
	jump(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1);
	stmt(BPF_RET | BPF_K, action);
}

void FilterBuilder::allow(std::span<const int> numbers) {
	for (const auto number : numbers) {
		answer(number, SECCOMP_RET_ALLOW);
	}
}

void FilterBuilder::refuseFlag(
		int number,
		int argument,
		uint32_t flag,
		uint32_t action) {
	jump(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 4);
	stmt(BPF_LD | BPF_W | BPF_ABS, ArgumentOffset(argument));
	jump(BPF_JMP | BPF_JSET | BPF_K, flag, 0, 1);
	stmt(BPF_RET | BPF_K, action);
	stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
}

void FilterBuilder::allowValues(
		int number,
		int argument,
		uint32_t ignored,
		std::span<const uint32_t> values,
		uint32_t otherwise) {
	const auto count = int(values.size());
	const auto block = 1 + (ignored ? 1 : 0) + count + 2;
	jump(BPF_JMP | BPF_JEQ | BPF_K, number, 0, uint8_t(block));
	stmt(BPF_LD | BPF_W | BPF_ABS, ArgumentOffset(argument));
	if (ignored) {
		stmt(BPF_ALU | BPF_AND | BPF_K, ~ignored);
	}
	for (auto i = 0; i != count; ++i) {
		jump(BPF_JMP | BPF_JEQ | BPF_K, values[i], uint8_t(count - i), 0);
	}
	stmt(BPF_RET | BPF_K, otherwise);
	stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
}

[[nodiscard]] std::vector<sock_filter> BuildFilter(pid_t self) {
	auto filter = FilterBuilder();
	filter.stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch));
	filter.jump(BPF_JMP | BPF_JEQ | BPF_K, kArchitecture, 1, 0);
	filter.stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
	filter.stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr));
#if defined __x86_64__
	filter.jump(BPF_JMP | BPF_JGE | BPF_K, __X32_SYSCALL_BIT, 0, 1);
	filter.stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif // __x86_64__

	filter.allow(kChannelCalls);

	filter.allow(kMemoryCalls);
	for (const auto number : { __NR_mmap, __NR_mprotect }) {
		filter.refuseFlag(number, 2, PROT_EXEC, kNotPermitted);
	}
	filter.allowValues(__NR_madvise, 2, 0, kAdvice, kNotImplemented);

	filter.allowValues(
		__NR_futex,
		1,
		kFutexOptions,
		kFutexOperations,
		kNotImplemented);

	filter.allow(kClockCalls);

	filter.allow(kThreadCalls);
	filter.allowValues(
		__NR_prctl,
		0,
		0,
		kThreadNameOptions,
		kNotImplemented);
	filter.allowValues(
		__NR_clone,
		0,
		0,
		std::array{ kThreadFlags },
		SECCOMP_RET_KILL_PROCESS);
	filter.answer(__NR_clone3, kNotImplemented);

	filter.allow(kSignalCalls);
	filter.allowValues(
		__NR_tgkill,
		0,
		0,
		std::array{ uint32_t(self) },
		kNotImplemented);

	filter.allow(kRandomCalls);

	for (const auto number : kKilledCalls) {
		filter.answer(number, SECCOMP_RET_KILL_PROCESS);
	}
	filter.stmt(BPF_RET | BPF_K, kNotImplemented);
	return std::move(filter.code);
}

#else // __x86_64__ || __aarch64__

[[nodiscard]] std::vector<sock_filter> BuildFilter(pid_t) {
	return {};
}

#endif // __x86_64__ || __aarch64__

[[nodiscard]] uint64_t HandledFilesystem(int abi) {
	auto result = uint64_t(LANDLOCK_ACCESS_FS_EXECUTE
		| LANDLOCK_ACCESS_FS_WRITE_FILE
		| LANDLOCK_ACCESS_FS_READ_FILE
		| LANDLOCK_ACCESS_FS_READ_DIR
		| LANDLOCK_ACCESS_FS_REMOVE_DIR
		| LANDLOCK_ACCESS_FS_REMOVE_FILE
		| LANDLOCK_ACCESS_FS_MAKE_CHAR
		| LANDLOCK_ACCESS_FS_MAKE_DIR
		| LANDLOCK_ACCESS_FS_MAKE_REG
		| LANDLOCK_ACCESS_FS_MAKE_SOCK
		| LANDLOCK_ACCESS_FS_MAKE_FIFO
		| LANDLOCK_ACCESS_FS_MAKE_BLOCK
		| LANDLOCK_ACCESS_FS_MAKE_SYM);
	if (abi >= 2) {
		result |= LANDLOCK_ACCESS_FS_REFER;
	}
	if (abi >= 3) {
		result |= LANDLOCK_ACCESS_FS_TRUNCATE;
	}
	if (abi >= 5) {
		result |= LANDLOCK_ACCESS_FS_IOCTL_DEV;
	}
	return result;
}

[[nodiscard]] std::string ApplyLandlock(int abi) {
	auto attr = RulesetAttr{ .fs = HandledFilesystem(abi) };
	auto size = offsetof(RulesetAttr, net);
	if (abi >= 4) {
		attr.net = LANDLOCK_ACCESS_NET_BIND_TCP
			| LANDLOCK_ACCESS_NET_CONNECT_TCP;
		size = offsetof(RulesetAttr, scoped);
	}
	if (abi >= 6) {
		attr.scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET
			| LANDLOCK_SCOPE_SIGNAL;
		size = sizeof(RulesetAttr);
	}
	const auto ruleset = syscall(
		__NR_landlock_create_ruleset,
		&attr,
		size,
		0);
	if (ruleset < 0) {
		return "landlock_create_ruleset failed: " + ErrorName(errno);
	}
	const auto restricted = syscall(
		__NR_landlock_restrict_self,
		ruleset,
		0);
	const auto error = errno;
	close(int(ruleset));
	return (restricted != 0)
		? ("landlock_restrict_self failed: " + ErrorName(error))
		: std::string();
}

[[nodiscard]] std::string EmptyRoot() {
	// Own mount table first: a helper run as root must not change the host's.
	if (unshare(CLONE_NEWNS) != 0
		|| mount(
			nullptr,
			"/",
			nullptr,
			MS_REC | MS_PRIVATE,
			nullptr) != 0
		|| mount(
			"tmpfs",
			"/tmp",
			"tmpfs",
			MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC,
			nullptr) != 0
		|| chdir("/tmp") != 0
		|| syscall(SYS_pivot_root, ".", ".") != 0
		|| umount2(".", MNT_DETACH) != 0) {
		return std::string();
	}
	return (chdir("/") != 0)
		? ("chdir failed: " + ErrorName(errno))
		: std::string();
}

[[nodiscard]] bool DropCapabilities() {
	auto header = __user_cap_header_struct{
		.version = _LINUX_CAPABILITY_VERSION_3,
		.pid = 0,
	};
	auto data = std::array<__user_cap_data_struct, _LINUX_CAPABILITY_U32S_3>();
	return syscall(SYS_capset, &header, data.data()) == 0;
}

[[nodiscard]] bool InstallFilter(const sock_fprog &program) {
	const auto installed = syscall(
		__NR_seccomp,
		SECCOMP_SET_MODE_FILTER,
		SECCOMP_FILTER_FLAG_SPEC_ALLOW,
		&program);
	return (installed == 0)
		|| (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0);
}

} // namespace

void CloseDescriptorsFrom(int first) {
	if (syscall(__NR_close_range, first, ~0U, 0) == 0
		|| CloseListedDescriptors(first)) {
		return;
	}
	CloseUpToLimit(first);
}

void PrepareHelperProcess(int firstToClose) {
	prctl(PR_SET_PDEATHSIG, SIGKILL);
	CloseDescriptorsFrom(firstToClose);
}

std::string LockDownHelper() {
	if (!kLockdownAvailable) {
		return "the widget helper has no seccomp filter for this architecture";
	}
	const auto threads = ThreadCount();
	if (threads.error) {
		return "the widget helper cannot count its threads: "
			+ ErrorName(threads.error);
	} else if (threads.count != 1) {
		return "the widget helper has "
			+ std::to_string(threads.count)
			+ " threads before its lockdown";
	}
	tzset();
	const auto abi = LandlockAbi();
	auto filter = BuildFilter(getpid());
	const auto rooted = EmptyRoot();
	if (!rooted.empty()) {
		return rooted;
	} else if (!DropCapabilities()) {
		return "capset failed: " + ErrorName(errno);
	} else if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
		return "prctl(PR_SET_NO_NEW_PRIVS) failed: " + ErrorName(errno);
	}
	if (abi > 0) {
		auto refusal = ApplyLandlock(abi);
		if (!refusal.empty()) {
			return refusal;
		}
	}
	const auto program = sock_fprog{
		.len = static_cast<unsigned short>(filter.size()),
		.filter = filter.data(),
	};
	if (!InstallFilter(program)) {
		return "prctl(PR_SET_SECCOMP) failed: " + ErrorName(errno);
	}
	return {};
}

} // namespace Platform::WidgetHelper

#endif // Q_OS_LINUX
