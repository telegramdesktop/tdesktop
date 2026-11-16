/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#include "platform/posix/widget_channel_posix.h"
#ifndef OS_MAC_STORE
#include "widget_helper_identity.h"
#endif // OS_MAC_STORE

#include <QtCore/QCryptographicHash>

#include <Foundation/Foundation.h>
#include <Security/Security.h>
#include <mach/mach.h>
#include <mach/machine.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <thread>
#include <unistd.h>

#ifndef OS_MAC_STORE
extern "C" int responsibility_spawnattrs_setdisclaim(
	posix_spawnattr_t *attributes,
	int disclaim) API_AVAILABLE(macos(10.14));
#endif // OS_MAC_STORE

namespace Platform::WidgetHelper {
namespace {

constexpr auto kSpareDescriptorMin = 10;
constexpr auto kPollDelay = crl::time(10);
constexpr auto kAbandonWait = crl::time(1000);

#if defined __arm64__
constexpr auto kArchitecture = "arm64";
constexpr auto kCpuType = cpu_type_t(CPU_TYPE_ARM64);
#elif defined __x86_64__
constexpr auto kArchitecture = "x86_64";
constexpr auto kCpuType = cpu_type_t(CPU_TYPE_X86_64);
#else // __arm64__ || __x86_64__
#error "The widget helper starts only arm64 or x86_64 slices."
#endif // __arm64__ || __x86_64__

struct CFReleaser {
	void operator()(CFTypeRef value) const;
};

template <typename Ref>
using CFHolder = std::unique_ptr<std::remove_pointer_t<Ref>, CFReleaser>;

struct Requirement {
	CFHolder<SecRequirementRef> value;
	QString check;
	QString error;
};

void CFReleaser::operator()(CFTypeRef value) const {
	CFRelease(value);
}

[[nodiscard]] QString SystemError(const char *call, int error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(error));
}

[[nodiscard]] QString SecurityError(const char *call, OSStatus status) {
	const auto message = CFHolder<CFStringRef>(
		SecCopyErrorMessageString(status, nullptr));
	const auto name = QString::fromLatin1(call);
	return message
		? u"%1 failed: %2 (%3)"_q.arg(
			name,
			QString::fromCFString(message.get()),
			QString::number(status))
		: u"%1 failed: %2"_q.arg(name, QString::number(status));
}

[[nodiscard]] QByteArray HelperPath() {
	@autoreleasepool {
		const auto bundle = [[NSBundle mainBundle] bundlePath];
		const auto path = [bundle stringByAppendingPathComponent:
			@"Contents/Helpers/TelegramWidgetHelper"];
		return path
			? QByteArray([path fileSystemRepresentation])
			: QByteArray();
	}
}

[[nodiscard]] QString PrepareActions(
		not_null<posix_spawn_file_actions_t*> actions,
		int spare) {
	if (const auto error = posix_spawn_file_actions_addopen(
			actions,
			STDIN_FILENO,
			"/dev/null",
			O_RDONLY,
			0)) {
		return SystemError("posix_spawn_file_actions_addopen", error);
	}
	if (const auto error = posix_spawn_file_actions_addopen(
			actions,
			STDOUT_FILENO,
			"/dev/null",
			O_WRONLY,
			0)) {
		return SystemError("posix_spawn_file_actions_addopen", error);
	}
	if (const auto error = posix_spawn_file_actions_addinherit_np(
			actions,
			STDERR_FILENO)) {
		return SystemError("posix_spawn_file_actions_addinherit_np", error);
	}
	if (const auto error = posix_spawn_file_actions_adddup2(
			actions,
			spare,
			kChannelDescriptor)) {
		return SystemError("posix_spawn_file_actions_adddup2", error);
	}
	return QString();
}

[[nodiscard]] QString PrepareAttributes(
		not_null<posix_spawnattr_t*> attributes) {
	if (const auto error = posix_spawnattr_setflags(
			attributes,
			POSIX_SPAWN_CLOEXEC_DEFAULT
				| POSIX_SPAWN_SETSIGDEF
				| POSIX_SPAWN_SETSIGMASK
				| POSIX_SPAWN_START_SUSPENDED)) {
		return SystemError("posix_spawnattr_setflags", error);
	}
	auto defaults = sigset_t();
	sigfillset(&defaults);
	if (const auto error = posix_spawnattr_setsigdefault(
			attributes,
			&defaults)) {
		return SystemError("posix_spawnattr_setsigdefault", error);
	}
	auto mask = sigset_t();
	sigemptyset(&mask);
	if (const auto error = posix_spawnattr_setsigmask(attributes, &mask)) {
		return SystemError("posix_spawnattr_setsigmask", error);
	}
	// The client's Crashpad ports (EXC_CRASH too) must not get helper crashes.
	if (const auto error = posix_spawnattr_setexceptionports_np(
			attributes,
			EXC_MASK_ALL | EXC_MASK_CRASH,
			MACH_PORT_NULL,
			EXCEPTION_DEFAULT,
			THREAD_STATE_NONE)) {
		return SystemError("posix_spawnattr_setexceptionports_np", error);
	}
	auto type = kCpuType;
	auto count = size_t();
	const auto preferenceError = posix_spawnattr_setbinpref_np(
		attributes,
		1,
		&type,
		&count);
	if (preferenceError || count != 1) {
		return SystemError(
			"posix_spawnattr_setbinpref_np",
			preferenceError ? preferenceError : EINVAL);
	}
#ifndef OS_MAC_STORE
	if (@available(macOS 10.14, *)) {
		// TCC must not attribute the helper's requests to Telegram's grants.
		if (const auto error = responsibility_spawnattrs_setdisclaim(
				attributes,
				1)) {
			return SystemError("responsibility_spawnattrs_setdisclaim", error);
		}
	}
#endif // OS_MAC_STORE
	return QString();
}

#ifndef OS_MAC_STORE

[[nodiscard]] Requirement MakeRequirement(
		const QString &text,
		QString check) {
	const auto string = CFHolder<CFStringRef>(text.toCFString());
	auto created = SecRequirementRef();
	const auto status = SecRequirementCreateWithString(
		string.get(),
		kSecCSDefaultFlags,
		&created);
	auto value = CFHolder<SecRequirementRef>(created);
	if (status != errSecSuccess) {
		return {
			.error = SecurityError("SecRequirementCreateWithString", status),
		};
	}
	return { .value = std::move(value), .check = std::move(check) };
}

[[nodiscard]] Requirement SignerRequirement(CFTypeRef certificates) {
	const auto list = (certificates
		&& CFGetTypeID(certificates) == CFArrayGetTypeID())
		? (CFArrayRef)certificates
		: nullptr;
	const auto leaf = (list && CFArrayGetCount(list) > 0)
		? CFArrayGetValueAtIndex(list, 0)
		: nullptr;
	const auto der = (leaf && CFGetTypeID(leaf) == SecCertificateGetTypeID())
		? CFHolder<CFDataRef>(SecCertificateCopyData((SecCertificateRef)leaf))
		: nullptr;
	if (!der) {
		return {
			.error = u"the signing certificate of this build is unreadable"_q,
		};
	}
	const auto hash = QCryptographicHash::hash(
		QByteArray::fromRawCFData(der.get()),
		QCryptographicHash::Sha1).toHex();
	return MakeRequirement(
		u"identifier \"%1\" and certificate leaf = H\"%2\""_q.arg(
			QString::fromLatin1(details::kIdentifier),
			QString::fromLatin1(hash)),
		u"signer"_q);
}

[[nodiscard]] Requirement BuiltRequirement() {
	for (const auto &built : details::kBuiltDigests) {
		if (std::string_view(built.architecture) == kArchitecture) {
			return MakeRequirement(
				u"cdhash H\"%1\""_q.arg(QString::fromLatin1(built.digest)),
				u"build identity"_q);
		}
	}
	return {
		.error = u"the widget helper identity of this build is unknown"_q,
	};
}

[[nodiscard]] bool RunningUnsigned(SecCodeRef code) {
	auto dynamic = CFDictionaryRef();
	const auto read = SecCodeCopySigningInformation(
		code,
		kSecCSDynamicInformation,
		&dynamic);
	const auto information = CFHolder<CFDictionaryRef>(dynamic);
	const auto status = (read == errSecSuccess)
		? CFDictionaryGetValue(information.get(), kSecCodeInfoStatus)
		: nullptr;
	auto value = int64_t();
	return status
		&& CFGetTypeID(status) == CFNumberGetTypeID()
		&& CFNumberGetValue((CFNumberRef)status, kCFNumberSInt64Type, &value)
		&& !(value & kSecCodeStatusValid);
}

[[nodiscard]] Requirement HelperRequirement() {
	auto self = SecCodeRef();
	const auto copied = SecCodeCopySelf(kSecCSDefaultFlags, &self);
	const auto code = CFHolder<SecCodeRef>(self);
	if (copied != errSecSuccess) {
		return { .error = SecurityError("SecCodeCopySelf", copied) };
	}
	auto own = SecRequirementRef();
	const auto described = SecCodeCopyDesignatedRequirement(
		code.get(),
		kSecCSDefaultFlags,
		&own);
	const auto designated = CFHolder<SecRequirementRef>(own);
	if (described == errSecCSUnsigned && RunningUnsigned(code.get())) {
		return BuiltRequirement();
	} else if (described != errSecSuccess) {
		return {
			.error = SecurityError(
				"SecCodeCopyDesignatedRequirement",
				described),
		};
	}
	const auto checked = SecCodeCheckValidity(
		code.get(),
		kSecCSDefaultFlags,
		designated.get());
	if (checked != errSecSuccess) {
		return { .error = SecurityError("SecCodeCheckValidity", checked) };
	}
	auto signing = CFDictionaryRef();
	const auto read = SecCodeCopySigningInformation(
		code.get(),
		kSecCSSigningInformation,
		&signing);
	const auto information = CFHolder<CFDictionaryRef>(signing);
	if (read != errSecSuccess) {
		return {
			.error = SecurityError("SecCodeCopySigningInformation", read),
		};
	}
	const auto identifier = CFDictionaryGetValue(
		information.get(),
		kSecCodeInfoIdentifier);
	const auto flags = CFDictionaryGetValue(
		information.get(),
		kSecCodeInfoFlags);
	auto value = int64_t();
	if (!identifier
		|| !flags
		|| CFGetTypeID(flags) != CFNumberGetTypeID()
		|| !CFNumberGetValue((CFNumberRef)flags, kCFNumberSInt64Type, &value)) {
		return { .error = u"the signature of this build is unreadable"_q };
	} else if (value & kSecCodeSignatureAdhoc) {
		return BuiltRequirement();
	}
	return SignerRequirement(CFDictionaryGetValue(
		information.get(),
		kSecCodeInfoCertificates));
}

#else // OS_MAC_STORE

[[nodiscard]] Requirement HelperRequirement() {
	return { .error = UnsupportedReason() };
}

#endif // OS_MAC_STORE

[[nodiscard]] QString CheckStarted(
		pid_t pid,
		const Requirement &requirement) {
	Expects(requirement.value != nullptr);

	const auto refused = [&](const QString &error) {
		return u"the widget helper failed the %1 check: %2"_q.arg(
			requirement.check,
			error);
	};
	const auto number = CFHolder<CFNumberRef>(
		CFNumberCreate(nullptr, kCFNumberIntType, &pid));
	auto key = (const void*)kSecGuestAttributePid;
	auto value = (const void*)number.get();
	const auto attributes = CFHolder<CFDictionaryRef>(value
		? CFDictionaryCreate(
			nullptr,
			&key,
			&value,
			1,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks)
		: nullptr);
	if (!attributes) {
		return refused(u"the guest attributes are unavailable"_q);
	}
	auto found = SecCodeRef();
	const auto copied = SecCodeCopyGuestWithAttributes(
		nullptr,
		attributes.get(),
		kSecCSDefaultFlags,
		&found);
	const auto guest = CFHolder<SecCodeRef>(found);
	if (copied != errSecSuccess) {
		return refused(
			SecurityError("SecCodeCopyGuestWithAttributes", copied));
	}
	const auto checked = SecCodeCheckValidity(
		guest.get(),
		kSecCSDefaultFlags,
		requirement.value.get());
	return (checked != errSecSuccess)
		? refused(SecurityError("SecCodeCheckValidity", checked))
		: QString();
}

[[nodiscard]] QString DescribeStatus(int status) {
	if (WIFEXITED(status)) {
		return u"exit %1"_q.arg(WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		return u"signal %1"_q.arg(WTERMSIG(status));
	}
	return u"status %1"_q.arg(status);
}

[[nodiscard]] std::optional<Completion> WaitFor(
		pid_t pid,
		crl::time timeout) {
	const auto deadline = crl::now() + timeout;
	while (true) {
		auto status = 0;
		const auto result = waitpid(pid, &status, WNOHANG);
		const auto error = (result < 0) ? errno : 0;
		if (result == pid) {
			return Completion{
				.detail = DescribeStatus(status),
				.reaped = true,
			};
		} else if (error == ECHILD) {
			return Completion{
				.detail = u"reaped elsewhere"_q,
				.reaped = true,
			};
		} else if (error && error != EINTR) {
			return Completion{ .detail = SystemError("waitpid", error) };
		} else if (crl::now() >= deadline) {
			return std::nullopt;
		} else if (!error) {
			std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelay));
		}
	}
}

[[nodiscard]] Completion Complete(
		pid_t pid,
		crl::time exitWait,
		crl::time killWait) {
	Expects(pid > 0);

	const auto started = crl::now();
	auto exited = WaitFor(pid, exitWait);
	if (exited && exited->reaped) {
		return std::move(*exited);
	}
	const auto signaled = crl::now() - started;
	const auto killError = (kill(pid, SIGKILL) == 0) ? 0 : errno;
	auto result = WaitFor(pid, killWait);
	if (!result) {
		result = Completion{
			.detail = u"not reaped after %1 ms"_q.arg(crl::now() - started),
		};
	}
	if (exited) {
		result->detail = exited->detail + u", "_q + result->detail;
	}
	result->detail += u", "_q + (killError
		? SystemError("kill", killError)
		: u"sent SIGKILL after %1 ms"_q.arg(signaled));
	return std::move(*result);
}

[[nodiscard]] QString Abandon(pid_t pid, const QString &error) {
	const auto cleanup = Complete(pid, 0, kAbandonWait);
	return u"%1 (%2)"_q.arg(error, cleanup.detail);
}

} // namespace

QString UnsupportedReason() {
#ifdef OS_MAC_STORE
	// The App Sandbox blocks the profile.
	return u"the widget helper is not available in the Store build"_q;
#else // OS_MAC_STORE
	return QString();
#endif // OS_MAC_STORE
}

bool Enabled() {
	return UnsupportedReason().isEmpty();
}

Launched Launch(const QStringList &arguments) {
	auto path = HelperPath();
	if (path.isEmpty()) {
		return { .error = u"the widget helper path is unknown"_q };
	} else if (fcntl(STDERR_FILENO, F_GETFD) < 0) {
		return { .error = u"stderr is not open"_q };
	}
	auto channel = CreatePosixChannel();
	if (!channel.error.empty()) {
		return { .error = QString::fromStdString(channel.error) };
	}
	const auto spare = fcntl(
		channel.child->descriptor(),
		F_DUPFD_CLOEXEC,
		kSpareDescriptorMin);
	if (spare < 0) {
		return { .error = SystemError("fcntl", errno) };
	}
	const auto closeSpare = gsl::finally([=] {
		close(spare);
	});

	auto actions = posix_spawn_file_actions_t();
	if (const auto error = posix_spawn_file_actions_init(&actions)) {
		return { .error = SystemError("posix_spawn_file_actions_init", error) };
	}
	const auto destroyActions = gsl::finally([&] {
		posix_spawn_file_actions_destroy(&actions);
	});
	auto attributes = posix_spawnattr_t();
	if (const auto error = posix_spawnattr_init(&attributes)) {
		return { .error = SystemError("posix_spawnattr_init", error) };
	}
	const auto destroyAttributes = gsl::finally([&] {
		posix_spawnattr_destroy(&attributes);
	});
	auto prepared = PrepareActions(&actions, spare);
	if (prepared.isEmpty()) {
		prepared = PrepareAttributes(&attributes);
	}
	if (!prepared.isEmpty()) {
		return { .error = std::move(prepared) };
	}
	auto requirement = HelperRequirement();
	if (!requirement.error.isEmpty()) {
		return { .error = std::move(requirement.error) };
	}

	auto token = QByteArray::number(kChannelDescriptor);
	auto encoded = std::vector<QByteArray>();
	encoded.reserve(arguments.size());
	for (const auto &argument : arguments) {
		encoded.push_back(argument.toUtf8());
	}
	auto argv = std::vector<char*>{ path.data(), token.data() };
	for (auto &argument : encoded) {
		argv.push_back(argument.data());
	}
	argv.push_back(nullptr);
	char *environment[] = { nullptr };
	auto owned = std::make_unique<pid_t>();
	const auto pid = owned.get();
	auto result = Launched{
		.stream = std::move(channel.parent),
		.complete = [owned = std::move(owned)](
				crl::time exitWait,
				crl::time killWait) {
			return Complete(base::take(*owned), exitWait, killWait);
		},
	};
	if (const auto error = posix_spawn(
			pid,
			path.constData(),
			&actions,
			&attributes,
			argv.data(),
			environment)) {
		return { .error = SystemError("posix_spawn", error) };
	}
	const auto child = *pid;
	const auto refused = CheckStarted(child, requirement);
	if (!refused.isEmpty()) {
		return { .error = Abandon(child, refused) };
	}
	const auto resumeError = (kill(child, SIGCONT) == 0) ? 0 : errno;
	if (resumeError) {
		return { .error = Abandon(child, SystemError("kill", resumeError)) };
	}
	result.pid = child;
	return result;
}

} // namespace Platform::WidgetHelper
