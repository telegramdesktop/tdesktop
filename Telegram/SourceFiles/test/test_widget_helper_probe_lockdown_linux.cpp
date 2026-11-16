/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_helper_probe_lockdown.h"

#ifdef _DEBUG

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <gsl/gsl>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// The kernel headers of the oldest supported build environment predate
// Landlock.
#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#endif // __NR_landlock_create_ruleset

#ifndef LANDLOCK_CREATE_RULESET_VERSION
#define LANDLOCK_CREATE_RULESET_VERSION (1U << 0)
#endif // LANDLOCK_CREATE_RULESET_VERSION

namespace Test {
namespace {

constexpr auto kLinePrefix = std::string_view("Widget helper probe: ");
constexpr auto kCopyWait = std::chrono::milliseconds(2000);
constexpr auto kCopyPoll = std::chrono::milliseconds(10);
constexpr auto kMaxProbedDescriptor = 4096;
constexpr auto kMaxSmallFile = size_t(64 * 1024);

// A copy's exit code when its own |lockdown| call refused; errno values stay
// far below it.
constexpr auto kLockdownRefused = 255;

// The architecture whose calls the outer filter inspects. The lockdown has
// a seccomp filter only for these two and refuses before anything else on
// any other, so there the outer filter lets every call through.
#if defined __x86_64__
constexpr auto kArchitecture = uint32_t(AUDIT_ARCH_X86_64);
#elif defined __aarch64__
constexpr auto kArchitecture = uint32_t(AUDIT_ARCH_AARCH64);
#else // __x86_64__ || __aarch64__
constexpr auto kArchitecture = uint32_t(0);
#endif // __x86_64__ || __aarch64__

struct Paths {
	std::string cwd;
	std::string data;
};

// Read in the helper itself before any copy or lockdown.
struct Facts {
	std::string uidMap;
	std::string interfaces;
	std::string sandbox;
	pid_t pid = 0;
	int pdeathsig = 0;
	int landlockAbi = 0;
	int landlockError = 0;
};

// Each value is 0 for a directory that opened, else the errno of the open.
struct Directories {
	int cwd = 0;
	int clientCwd = 0;
	int data = 0;
};

// Written by the "landlock alone" copy into a page shared with the helper;
// |taken| stays false when the copy gave no reading.
struct LandlockReading {
	int cwd = 0;
	bool emptyRoot = false;
	bool taken = false;
};

struct StatusReading {
	std::string seccomp = "unknown";
	std::string nnp = "unknown";
	std::string caps = "unknown";
	std::string filters = "unknown";
};

struct CopyEnd {
	std::string text;
	bool exitedZero = false;
};

struct Locked {
	std::string refusal;
	std::string status;
};

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

// One write per line, so the lines of the copies and of the helper never
// interleave.
void WriteLine(const std::string &text) {
	auto line = std::string(kLinePrefix);
	line += text;
	line += '\n';
	auto data = line.data();
	auto left = line.size();
	while (left > 0) {
		const auto written = write(STDERR_FILENO, data, left);
		if (written < 0 && errno == EINTR) {
			continue;
		} else if (written <= 0) {
			return;
		}
		data += written;
		left -= size_t(written);
	}
}

[[nodiscard]] std::vector<std::string_view> Split(
		std::string_view text,
		std::string_view separators) {
	auto result = std::vector<std::string_view>();
	auto from = text.find_first_not_of(separators);
	while (from != std::string_view::npos) {
		const auto till = text.find_first_of(separators, from);
		result.push_back(text.substr(from, till - from));
		from = text.find_first_not_of(separators, till);
	}
	return result;
}

[[nodiscard]] std::string Join(
		const std::vector<std::string_view> &parts,
		char separator) {
	auto result = std::string();
	for (const auto &part : parts) {
		if (!result.empty()) {
			result += separator;
		}
		result += part;
	}
	return result;
}

[[nodiscard]] std::string ReadAll(int descriptor) {
	auto result = std::string(kMaxSmallFile, char(0));
	auto size = size_t(0);
	while (size < result.size()) {
		const auto got = read(
			descriptor,
			result.data() + size,
			result.size() - size);
		if (got < 0 && errno == EINTR) {
			continue;
		} else if (got < 0) {
			return std::string();
		} else if (!got) {
			break;
		}
		size += size_t(got);
	}
	result.resize(size);
	return result;
}

[[nodiscard]] std::string ReadSmall(const char *path) {
	const auto descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		return std::string();
	}
	auto result = ReadAll(descriptor);
	close(descriptor);
	return result;
}

[[nodiscard]] std::string UidMapToken(std::string_view map) {
	const auto lines = Split(map, "\n");
	const auto result = lines.empty()
		? std::string()
		: Join(Split(lines.front(), " \t"), ':');
	return result.empty()
		? std::string("none")
		: (result == "0:0:4294967295")
		? std::string("init")
		: result;
}

[[nodiscard]] std::string InterfacesToken(std::string_view dev) {
	const auto lines = Split(dev, "\n");
	auto names = std::vector<std::string_view>();
	for (auto i = size_t(2); i < lines.size(); ++i) {
		const auto line = lines[i];
		const auto colon = line.find(':');
		if (colon == std::string_view::npos) {
			continue;
		}
		const auto words = Split(line.substr(0, colon), " \t");
		if (words.size() == 1) {
			names.push_back(words.front());
		}
	}
	return names.empty() ? std::string("none") : Join(names, ',');
}

[[nodiscard]] std::string SandboxToken() {
	if (access("/.flatpak-info", F_OK) == 0) {
		return "flatpak";
	} else if (std::getenv("SNAP")) {
		return "snap";
	}
	return "none";
}

[[nodiscard]] Facts ReadFacts() {
	auto result = Facts();
	result.pid = getpid();
	result.uidMap = UidMapToken(ReadSmall("/proc/self/uid_map"));
	result.interfaces = InterfacesToken(ReadSmall("/proc/self/net/dev"));
	result.sandbox = SandboxToken();
	prctl(PR_GET_PDEATHSIG, &result.pdeathsig);
	const auto abi = syscall(
		__NR_landlock_create_ruleset,
		nullptr,
		0,
		LANDLOCK_CREATE_RULESET_VERSION);
	if (abi < 0) {
		result.landlockError = errno;
	} else {
		result.landlockAbi = int(abi);
	}
	return result;
}

[[nodiscard]] int OpenDirectory(const std::string &path) {
	const auto descriptor = open(
		path.c_str(),
		O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (descriptor < 0) {
		return errno;
	}
	close(descriptor);
	return 0;
}

[[nodiscard]] Directories OpenDirectories(const Paths &paths) {
	auto result = Directories();
	result.cwd = OpenDirectory(".");
	result.clientCwd = OpenDirectory(paths.cwd);
	result.data = OpenDirectory(paths.data);
	return result;
}

[[nodiscard]] std::string OpenText(int error) {
	return error ? ErrorName(error) : std::string("opened");
}

[[nodiscard]] std::string DirectoriesText(const Directories &directories) {
	return "cwd=" + OpenText(directories.cwd)
		+ " client-cwd=" + OpenText(directories.clientCwd)
		+ " data=" + OpenText(directories.data);
}

// A zero-length peek: EBADF marks a closed descriptor, any other answer an
// open one, and the call is one the lockdown's filter allows.
[[nodiscard]] std::string ListDescriptors() {
	auto result = std::string();
	for (auto fd = 0; fd != kMaxProbedDescriptor; ++fd) {
		auto byte = char(0);
		const auto peeked = recvfrom(
			fd,
			&byte,
			0,
			MSG_DONTWAIT | MSG_PEEK,
			nullptr,
			nullptr);
		if (peeked < 0 && errno == EBADF) {
			continue;
		} else if (!result.empty()) {
			result += ',';
		}
		result += std::to_string(fd);
	}
	return result.empty() ? std::string("none") : result;
}

[[nodiscard]] std::string_view StatusField(
		std::string_view status,
		std::string_view name) {
	for (const auto line : Split(status, "\n")) {
		const auto colon = line.find(':');
		if (colon == std::string_view::npos || line.substr(0, colon) != name) {
			continue;
		}
		const auto words = Split(line.substr(colon + 1), " \t");
		return words.empty() ? std::string_view() : words.front();
	}
	return std::string_view();
}

[[nodiscard]] std::string FlagToken(
		std::string_view value,
		std::string_view on) {
	return value.empty()
		? std::string("unknown")
		: (value == on)
		? std::string("on")
		: std::string("off");
}

[[nodiscard]] bool AllZero(std::string_view value) {
	return !value.empty()
		&& (value.find_first_not_of('0') == std::string_view::npos);
}

[[nodiscard]] StatusReading ParseStatus(std::string_view status) {
	const auto permitted = StatusField(status, "CapPrm");
	const auto effective = StatusField(status, "CapEff");
	const auto filters = StatusField(status, "Seccomp_filters");
	auto result = StatusReading();
	result.seccomp = FlagToken(StatusField(status, "Seccomp"), "2");
	result.nnp = FlagToken(StatusField(status, "NoNewPrivs"), "1");
	if (!permitted.empty() && !effective.empty()) {
		result.caps = (AllZero(permitted) && AllZero(effective))
			? "none"
			: "kept";
	}
	if (!filters.empty()) {
		result.filters = std::string(filters);
	}
	return result;
}

[[nodiscard]] std::string LandlockToken(
		const Facts &facts,
		const LandlockReading &reading) {
	if (!facts.landlockAbi) {
		return "off(" + ErrorName(facts.landlockError) + ')';
	} else if (!reading.taken) {
		return "unknown";
	}
	const auto abi = "abi" + std::to_string(facts.landlockAbi);
	return (reading.cwd == EACCES) ? abi : (abi + ",not-enforced");
}

[[nodiscard]] std::string RootToken(const LandlockReading &reading) {
	return !reading.taken
		? std::string("unknown")
		: reading.emptyRoot
		? std::string("empty")
		: std::string("kept");
}

[[nodiscard]] std::string LayersText(
		const Facts &facts,
		const LandlockReading &landlock,
		const StatusReading &status) {
	return "layers: seccomp=" + status.seccomp
		+ " landlock=" + LandlockToken(facts, landlock)
		+ " nnp=" + status.nnp
		+ " caps=" + status.caps
		+ " seccomp_filters=" + status.filters
		+ " pdeathsig=" + std::to_string(facts.pdeathsig)
		+ " pid=" + std::to_string(facts.pid)
		+ " uid_map=" + facts.uidMap
		+ " interfaces=" + facts.interfaces
		+ " root=" + RootToken(landlock)
		+ " sandbox=" + facts.sandbox;
}

// Answers seccomp(2) and prctl(PR_SET_SECCOMP) with EPERM and lets every
// other call through, so the lockdown that follows applies everything up to
// its own filter and then refuses at the install.
[[nodiscard]] std::string InstallOuterFilter() {
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
		return "prctl(PR_SET_NO_NEW_PRIVS) failed: " + ErrorName(errno);
	}
	sock_filter code[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kArchitecture, 0, 6),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_seccomp, 3, 0),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_prctl, 0, 3),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, PR_SET_SECCOMP, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	const auto program = sock_fprog{
		.len = static_cast<unsigned short>(std::size(code)),
		.filter = code,
	};
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0) {
		return "prctl(PR_SET_SECCOMP) failed: " + ErrorName(errno);
	}
	return std::string();
}

[[nodiscard]] CopyEnd EndOfCopy(int status) {
	if (WIFSIGNALED(status)) {
		const auto number = WTERMSIG(status);
		return { .text = "killed by signal " + std::to_string(number) };
	} else if (!WIFEXITED(status)) {
		return { .text = "status " + std::to_string(status) };
	}
	const auto code = WEXITSTATUS(status);
	if (!code) {
		return { .text = "succeeded", .exitedZero = true };
	} else if (code == kLockdownRefused) {
		return { .text = "lockdown refused" };
	}
	return { .text = "returned " + ErrorName(code) };
}

// The helper is single-threaded until the probe returns, so the copy may
// call anything the helper could; it never touches the channel it inherits.
// The copy is not dumpable, so a death the probe causes on purpose writes no
// core dump.
template <typename Body>
[[nodiscard]] CopyEnd RunCopy(Body body) {
	const auto child = fork();
	if (child < 0) {
		return { .text = "fork failed: " + ErrorName(errno) };
	} else if (!child) {
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		prctl(PR_SET_DUMPABLE, 0);
		_exit(body());
	}
	const auto deadline = std::chrono::steady_clock::now() + kCopyWait;
	auto status = 0;
	while (true) {
		const auto waited = waitpid(child, &status, WNOHANG);
		if (waited == child) {
			return EndOfCopy(status);
		} else if (waited < 0 && errno != EINTR) {
			return { .text = "waitpid failed: " + ErrorName(errno) };
		} else if (std::chrono::steady_clock::now() >= deadline) {
			break;
		}
		std::this_thread::sleep_for(kCopyPoll);
	}
	kill(child, SIGKILL);
	auto reaped = pid_t();
	do {
		reaped = waitpid(child, &status, 0);
	} while (reaped < 0 && errno == EINTR);
	const auto end = (reaped == child)
		? EndOfCopy(status).text
		: ("waitpid failed: " + ErrorName(errno));
	return {
		.text = "no end in "
			+ std::to_string(kCopyWait.count())
			+ " ms, killed: "
			+ end,
	};
}

[[nodiscard]] int LandlockAlone(
		bool sandboxed,
		const std::function<std::string()> &lockdown,
		const Paths &paths,
		LandlockReading *reading) {
	auto lockdownText = std::string("skipped");
	if (sandboxed) {
		const auto failure = InstallOuterFilter();
		if (!failure.empty()) {
			WriteLine("landlock alone: no outer filter: " + failure);
			return 0;
		}
		const auto refusal = lockdown();
		lockdownText = refusal.empty() ? std::string("ok") : refusal;
	}
	const auto directories = OpenDirectories(paths);
	const auto emptyRoot = (access("/proc", F_OK) != 0) && (errno == ENOENT);
	reading->cwd = directories.cwd;
	reading->emptyRoot = emptyRoot;
	reading->taken = true;
	WriteLine("landlock alone: "
		+ DirectoriesText(directories)
		+ " root="
		+ (emptyRoot ? "empty" : "kept")
		+ " lockdown="
		+ lockdownText);
	return 0;
}

[[nodiscard]] LandlockReading RunLandlockCopy(
		bool sandboxed,
		const std::function<std::string()> &lockdown,
		const Paths &paths) {
	const auto shared = mmap(
		nullptr,
		sizeof(LandlockReading),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS,
		-1,
		0);
	if (shared == MAP_FAILED) {
		WriteLine("landlock alone: mmap failed: " + ErrorName(errno));
		return LandlockReading();
	}
	const auto reading = new (shared) LandlockReading();
	const auto end = RunCopy([&] {
		return LandlockAlone(sandboxed, lockdown, paths, reading);
	});
	if (!end.exitedZero) {
		WriteLine("landlock alone: " + end.text);
	}
	const auto result = *reading;
	munmap(shared, sizeof(LandlockReading));
	return result;
}

[[nodiscard]] int AttemptLocked(
		const std::string &name,
		bool sandboxed,
		const std::function<std::string()> &lockdown,
		int (*attempt)()) {
	if (sandboxed) {
		const auto refusal = lockdown();
		if (!refusal.empty()) {
			WriteLine(name + ": lockdown refused: " + refusal);
			return kLockdownRefused;
		}
	}
	return attempt();
}

[[nodiscard]] int AttemptExecve() {
	return (syscall(__NR_execve, "", nullptr, nullptr) < 0) ? errno : 0;
}

[[nodiscard]] int AttemptSocket() {
	const auto descriptor = syscall(__NR_socket, AF_UNIX, SOCK_STREAM, 0);
	if (descriptor < 0) {
		return errno;
	}
	close(int(descriptor));
	return 0;
}

[[nodiscard]] std::string RunAttempt(
		const std::string &name,
		bool sandboxed,
		const std::function<std::string()> &lockdown,
		int (*attempt)()) {
	const auto end = RunCopy([&] {
		return AttemptLocked(name, sandboxed, lockdown, attempt);
	});
	return name + ": " + end.text;
}

// The status descriptor is opened before the lockdown and closed before the
// descriptors are listed, so the listing stays exact.
[[nodiscard]] Locked LockDown(
		bool sandboxed,
		const std::function<std::string()> &lockdown) {
	const auto status = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
	const auto guard = gsl::finally([&] {
		if (status >= 0) {
			close(status);
		}
	});
	if (!sandboxed) {
		WriteLine("lockdown: skipped (unsandboxed control)");
	} else {
		auto refusal = lockdown();
		if (!refusal.empty()) {
			WriteLine("lockdown: FAILED " + refusal);
			return { .refusal = std::move(refusal) };
		}
		WriteLine("lockdown: ok");
	}
	return { .status = (status >= 0) ? ReadAll(status) : std::string() };
}

// After its own lockdown the helper only reads, writes, peeks and allocates:
// its opens are expected to fail with ENOSYS.
[[nodiscard]] std::string RunLinuxProbe(
		bool sandboxed,
		const std::vector<std::string> &arguments,
		const std::function<std::string()> &lockdown) {
	if (!lockdown) {
		return "no lockdown callable";
	}
	const auto paths = Paths{
		.cwd = (arguments.size() > 2) ? arguments[2] : std::string(),
		.data = (arguments.size() > 3) ? arguments[3] : std::string(),
	};
	const auto facts = ReadFacts();
	const auto landlock = RunLandlockCopy(sandboxed, lockdown, paths);
	WriteLine(RunAttempt("execve", sandboxed, lockdown, AttemptExecve));
	WriteLine(RunAttempt("socket", sandboxed, lockdown, AttemptSocket));
	const auto locked = LockDown(sandboxed, lockdown);
	if (!locked.refusal.empty()) {
		return locked.refusal;
	}
	const auto directories = OpenDirectories(paths);
	WriteLine("after lockdown: "
		+ DirectoriesText(directories)
		+ " descriptors="
		+ ListDescriptors());
	WriteLine(LayersText(facts, landlock, ParseStatus(locked.status)));
	return std::string();
}

} // namespace

std::string RefuseWidgetHelperMode(const std::string &mode) {
	return "unknown widget helper mode \"" + mode.substr(0, 64) + "\"";
}

std::string RunWidgetHelperProbe(
		bool sandboxed,
		const std::vector<std::string> &arguments,
		const std::function<std::string()> &lockdown) {
	return RunLinuxProbe(sandboxed, arguments, lockdown);
}

} // namespace Test

#endif // _DEBUG
