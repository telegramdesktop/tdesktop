/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#include "platform/mac/widget_helper_mac.h"
#include "platform/posix/widget_channel_posix.h"
#include "platform/posix/widget_reaping_posix.h"
#include "platform/posix/widget_stderr_posix.h"
#include "widget_helper_identity.h"

#include <QtCore/QCryptographicHash>

#include <Foundation/Foundation.h>
#include <Security/Security.h>
#include <mach/mach.h>
#include <mach/machine.h>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

namespace Platform::WidgetHelper {
namespace {

#if defined __arm64__
constexpr auto kArchitecture = "arm64";
#elif defined __x86_64__
constexpr auto kArchitecture = "x86_64";
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

struct Slice {
	cpu_type_t type = 0;
	const char *name = nullptr;
};

constexpr auto kSlices = std::array{
	Slice{ CPU_TYPE_ARM64, "arm64" },
	Slice{ CPU_TYPE_X86_64, "x86_64" },
};

void CFReleaser::operator()(CFTypeRef value) const {
	CFRelease(value);
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

[[nodiscard]] CFHolder<CFDictionaryRef> NumberAttributes(
		CFStringRef key,
		int value) {
	const auto number = CFHolder<CFNumberRef>(
		CFNumberCreate(nullptr, kCFNumberIntType, &value));
	auto keys = (const void*)key;
	auto values = (const void*)number.get();
	return CFHolder<CFDictionaryRef>(values
		? CFDictionaryCreate(
			nullptr,
			&keys,
			&values,
			1,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks)
		: nullptr);
}

// CFEqual treats a CFBoolean and a CFNumber as equal, the XML keeps the type.
[[nodiscard]] CFHolder<CFDataRef> Serialized(CFPropertyListRef list) {
	return CFHolder<CFDataRef>(CFPropertyListCreateData(
		nullptr,
		list,
		kCFPropertyListXMLFormat_v1_0,
		0,
		nullptr));
}

[[nodiscard]] CFHolder<CFDataRef> ExpectedEntitlements() {
	const auto expected = std::string_view(details::kEntitlements);
	const auto data = CFHolder<CFDataRef>(CFDataCreateWithBytesNoCopy(
		nullptr,
		reinterpret_cast<const UInt8*>(expected.data()),
		CFIndex(expected.size()),
		kCFAllocatorNull));
	const auto list = CFHolder<CFPropertyListRef>(data
		? CFPropertyListCreateWithData(
			nullptr,
			data.get(),
			kCFPropertyListImmutable,
			nullptr,
			nullptr)
		: nullptr);
	return list ? Serialized(list.get()) : nullptr;
}

[[nodiscard]] QString CheckSlice(
		CFURLRef url,
		const Slice &slice,
		SecRequirementRef signer,
		CFDataRef expected,
		not_null<QStringList*> digests) {
	const auto name = QString::fromLatin1(slice.name);
	const auto failed = [&](const QString &error) {
		return u"the %1 slice: %2"_q.arg(name, error);
	};
	const auto attributes = NumberAttributes(
		kSecCodeAttributeArchitecture,
		slice.type);
	if (!attributes) {
		return failed(u"the code attributes are unavailable"_q);
	}
	auto created = SecStaticCodeRef();
	const auto opened = SecStaticCodeCreateWithPathAndAttributes(
		url,
		kSecCSDefaultFlags,
		attributes.get(),
		&created);
	const auto code = CFHolder<SecStaticCodeRef>(created);
	if (opened == errSecCSBadObjectFormat) {
		return QString();
	} else if (opened != errSecSuccess) {
		return failed(SecurityError(
			"SecStaticCodeCreateWithPathAndAttributes",
			opened));
	}
	const auto checked = SecStaticCodeCheckValidity(
		code.get(),
		kSecCSDefaultFlags,
		signer);
	if (checked != errSecSuccess) {
		return failed(SecurityError("SecStaticCodeCheckValidity", checked));
	}
	auto signing = CFDictionaryRef();
	const auto read = SecCodeCopySigningInformation(
		code.get(),
		kSecCSDefaultFlags,
		&signing);
	const auto information = CFHolder<CFDictionaryRef>(signing);
	if (read != errSecSuccess) {
		return failed(SecurityError("SecCodeCopySigningInformation", read));
	}
	const auto unique = CFDictionaryGetValue(
		information.get(),
		kSecCodeInfoUnique);
	if (!unique
		|| CFGetTypeID(unique) != CFDataGetTypeID()
		|| !CFDataGetLength((CFDataRef)unique)) {
		return failed(u"the CDHash is unreadable"_q);
	}
	const auto entitlements = CFDictionaryGetValue(
		information.get(),
		kSecCodeInfoEntitlementsDict);
	const auto empty = CFHolder<CFDictionaryRef>(entitlements
		? nullptr
		: CFDictionaryCreate(
			nullptr,
			nullptr,
			nullptr,
			0,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks));
	const auto list = entitlements ? entitlements : empty.get();
	const auto actual = list ? Serialized(list) : nullptr;
	if (!actual) {
		return failed(u"the entitlements are unreadable"_q);
	} else if (!CFEqual(actual.get(), expected)) {
		return u"the %1 slice's entitlements differ from this build's"_q.arg(
			name);
	}
	const auto digest = QByteArray::fromRawCFData((CFDataRef)unique).toHex();
	digests->push_back(u"cdhash H\"%1\""_q.arg(QString::fromLatin1(digest)));
	return QString();
}

[[nodiscard]] details::RequirementText SignerRequirement(
		const QString &identifier,
		CFTypeRef certificates,
		const QByteArray &path) {
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
	const auto signer = u"identifier \"%1\" and certificate leaf = H\"%2\""_q
		.arg(identifier, QString::fromLatin1(hash));
	const auto requirement = MakeRequirement(signer, u"signer"_q);
	if (!requirement.error.isEmpty()) {
		return { .error = requirement.error };
	}
	const auto expected = ExpectedEntitlements();
	if (!expected) {
		return {
			.error = u"the widget helper entitlements of this build "
				"are unreadable"_q,
		};
	}
	const auto url = CFHolder<CFURLRef>(
		CFURLCreateFromFileSystemRepresentation(
			nullptr,
			reinterpret_cast<const UInt8*>(path.constData()),
			CFIndex(path.size()),
			false));
	if (!url) {
		return { .error = u"the widget helper path is unknown"_q };
	}
	const auto refused = [&](const QString &error) {
		return details::RequirementText{
			.error = u"the widget helper failed the %1 check: %2"_q.arg(
				requirement.check,
				error),
		};
	};
	auto digests = QStringList();
	// launchd starts a service's native slice even for a translated client.
	for (const auto &slice : kSlices) {
		const auto error = CheckSlice(
			url.get(),
			slice,
			requirement.value.get(),
			expected.get(),
			&digests);
		if (!error.isEmpty()) {
			return refused(error);
		}
	}
	if (digests.isEmpty()) {
		return refused(u"there is no arm64 or x86_64 slice"_q);
	}
	return {
		.text = u"%1 and (%2)"_q.arg(signer, digests.join(u" or "_q)),
		.check = requirement.check,
	};
}

[[nodiscard]] details::RequirementText BuiltRequirement() {
	for (const auto &built : details::kBuiltDigests) {
		if (std::string_view(built.architecture) == kArchitecture) {
			return {
				.text = u"cdhash H\"%1\""_q.arg(
					QString::fromLatin1(built.digest)),
				.check = u"build identity"_q,
			};
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

} // namespace

namespace details {

QString SystemError(const char *call, int error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(error));
}

QByteArray BundledPath(const char *relative) {
	@autoreleasepool {
		const auto bundle = [[NSBundle mainBundle] bundlePath];
		const auto path = [bundle stringByAppendingPathComponent:@(relative)];
		return path
			? QByteArray([path fileSystemRepresentation])
			: QByteArray();
	}
}

RequirementText HelperRequirementText(
		const QString &identifier,
		const QByteArray &path) {
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
	const auto ownIdentifier = CFDictionaryGetValue(
		information.get(),
		kSecCodeInfoIdentifier);
	const auto flags = CFDictionaryGetValue(
		information.get(),
		kSecCodeInfoFlags);
	auto value = int64_t();
	if (!ownIdentifier
		|| !flags
		|| CFGetTypeID(flags) != CFNumberGetTypeID()
		|| !CFNumberGetValue((CFNumberRef)flags, kCFNumberSInt64Type, &value)) {
		return { .error = u"the signature of this build is unreadable"_q };
	} else if (value & kSecCodeSignatureAdhoc) {
		return BuiltRequirement();
	}
	return SignerRequirement(
		identifier,
		CFDictionaryGetValue(information.get(), kSecCodeInfoCertificates),
		path);
}

} // namespace details

QString UnsupportedReason() {
#ifdef OS_MAC_STORE
	if (@available(macOS 12.0, *)) {
		return QString();
	}
	return u"the widget helper needs macOS 12 or later in the Store build"_q;
#else // OS_MAC_STORE
	return QString();
#endif // OS_MAC_STORE
}

bool Enabled() {
	return UnsupportedReason().isEmpty();
}

} // namespace Platform::WidgetHelper

#ifndef OS_MAC_STORE

extern "C" int responsibility_spawnattrs_setdisclaim(
	posix_spawnattr_t *attributes,
	int disclaim) API_AVAILABLE(macos(10.14));

namespace Platform::WidgetHelper {
namespace {

using details::SystemError;
using details::kRelayEndWait;

constexpr auto kSpareDescriptorMin = 10;
constexpr auto kAbandonWait = crl::time(1000);

#if defined __arm64__
constexpr auto kCpuType = cpu_type_t(CPU_TYPE_ARM64);
#elif defined __x86_64__
constexpr auto kCpuType = cpu_type_t(CPU_TYPE_X86_64);
#endif // __arm64__ || __x86_64__

[[nodiscard]] QString PrepareActions(
		not_null<posix_spawn_file_actions_t*> actions,
		int errorOutput,
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
	if (const auto error = posix_spawn_file_actions_adddup2(
			actions,
			errorOutput,
			STDERR_FILENO)) {
		return SystemError("posix_spawn_file_actions_adddup2", error);
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
	if (@available(macOS 10.14, *)) {
		// TCC must not attribute the helper's requests to Telegram's grants.
		if (const auto error = responsibility_spawnattrs_setdisclaim(
				attributes,
				1)) {
			return SystemError("responsibility_spawnattrs_setdisclaim", error);
		}
	}
	return QString();
}

[[nodiscard]] QString CheckStarted(
		pid_t pid,
		const Requirement &requirement) {
	Expects(requirement.value != nullptr);

	const auto refused = [&](const QString &error) {
		return u"the widget helper failed the %1 check: %2"_q.arg(
			requirement.check,
			error);
	};
	const auto attributes = NumberAttributes(kSecGuestAttributePid, pid);
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

[[nodiscard]] QString Abandon(pid_t pid, const QString &error) {
	const auto cleanup = CompleteChild(pid, 0, kAbandonWait);
	return u"%1 (%2)"_q.arg(error, cleanup.detail);
}

} // namespace

Launched Launch(const QStringList &arguments) {
	auto path = details::BundledPath("Contents/Helpers/TelegramWidgetHelper");
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
	auto output = PrepareHelperStderr(kSpareDescriptorMin);
	if (!output.error.empty()) {
		return { .error = QString::fromStdString(output.error) };
	}
	const auto closeOutput = gsl::finally([descriptor = output.descriptor] {
		close(descriptor);
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
	auto prepared = PrepareActions(&actions, output.descriptor, spare);
	if (prepared.isEmpty()) {
		prepared = PrepareAttributes(&attributes);
	}
	if (!prepared.isEmpty()) {
		return { .error = std::move(prepared) };
	}
	auto text = details::HelperRequirementText(
		QString::fromLatin1(details::kIdentifier),
		path);
	if (!text.error.isEmpty()) {
		return { .error = std::move(text.error) };
	}
	auto requirement = MakeRequirement(text.text, std::move(text.check));
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
		.complete = [owned = std::move(owned), relay = std::move(output.relay)](
				crl::time exitWait,
				crl::time killWait) mutable {
			auto completion = CompleteChild(
				base::take(*owned),
				exitWait,
				killWait);
			if (relay && completion.reaped) {
				// macOS discards the queued bytes once the end is shut.
				relay->drain(kRelayEndWait);
			}
			relay = nullptr;
			return completion;
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

#endif // OS_MAC_STORE
