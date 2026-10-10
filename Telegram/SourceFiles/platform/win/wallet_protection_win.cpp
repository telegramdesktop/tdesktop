/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/wallet_protection_win.h"

#include "base/call_delayed.h"
#include "base/platform/win/base_windows_safe_library.h"
#include "base/platform/win/base_windows_winrt.h"
#include "base/random.h"
#include "core/update_channel.h"
#include "lang/lang_keys.h"
#include "storage/serialize_common.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_vault.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <tbs.h>

// The wrap key is stored nowhere: it is HKDF-SHA256 over the signature a
// Windows Hello credential produces for a device-wrap random challenge kept
// in the wrap's payload, salted with the wrap's own random salt. That rests
// on the Hello key storage provider answering RequestSignAsync with
// RSASSA-PKCS1-v1_5 over SHA-256 - a deterministic function of the private
// key and the challenge, measured on a discrete-TPM host rather than
// promised by the platform's documentation. The keyring thus holds every
// public input and no private one, and DeriveVaultWrapKey refuses this
// kind, so nothing derivable from the keyring opens D.
// Should Windows ever move the provider to a probabilistic scheme, every
// existing wrap would read Corrupt - read-only for every account, nothing
// deleted, restore from the backup or the phrase - and never leak a key.
//
// Where the credential's private key lives is Windows' choice: the TPM the
// availability check found when Hello can use it, a software key store
// otherwise. Its attestation is not asked for. That proof depended on the
// TPM's endorsement certificate and on an attestation certificate Windows
// obtains from Microsoft's online service, which many machines never get,
// so demanding it turned the offered row into a failure right after the
// user's PIN. The row is offered on a qualifying build with a TPM and
// Windows Hello set up, and whatever Hello then creates is accepted.

namespace Platform {
namespace {

using Wallet::ProtectionError;
using namespace winrt::Windows::Security::Credentials;

using winrt::Windows::Foundation::IAsyncAction;
using winrt::Windows::Foundation::IAsyncInfo;
using winrt::Windows::Foundation::IAsyncOperation;
using AsyncStatus = winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Storage::Streams::IBuffer;
using winrt::Windows::Security::Cryptography::CryptographicBuffer;
using winrt::Windows::UI::WindowId;

constexpr auto kPayloadVersion = quint32(1);
constexpr auto kCredentialIdSize = 16;
constexpr auto kChallengeSize = 32;
constexpr char kWrapLabel[] = "tdesktop-wallet-vault/windows-hello-wrap/v1";
constexpr auto kCredentialPrefix
	= std::wstring_view(L"TelegramDesktop.WalletVault.");
constexpr auto kPromptFocusInterval = crl::time(100);
constexpr auto kPromptFocusAttempts = 50;

TBS_RESULT(WINAPI *GetTpmDeviceInfo)(UINT32, PVOID) = nullptr;

// ABI::Windows::UI::WindowId, as windows.ui.interop.h declares the export.
struct WindowIdAbi {
	uint64_t value = 0;
};
HRESULT(WINAPI *GetWindowIdFromWindow)(HWND, WindowIdAbi*) = nullptr;

struct Payload {
	QByteArray credentialId;
	QByteArray challenge;
};

struct Retrieval {
	ProtectionError error = ProtectionError::Unavailable;
	KeyCredential credential = nullptr;
};

struct Signing {
	ProtectionError error = ProtectionError::Unavailable;
	Wallet::SecureBytes signature;
};

struct EnrollOperation {
	winrt::hstring name;
	QByteArray challenge;
	std::optional<WindowId> owner;
	KeyCredential credential = nullptr;
	Wallet::VaultWrap wrap;
	Fn<void(Wallet::ProtectionEnrollResult)> done;
};

struct UnwrapOperation {
	winrt::hstring name;
	QByteArray challenge;
	std::optional<WindowId> owner;
	KeyCredential credential = nullptr;
	Wallet::VaultWrap wrap;
	Fn<void(Wallet::ProtectionUnwrapResult)> done;
};

[[nodiscard]] QByteArray SerializePayload(const Payload &payload) {
	auto stream = Serialize::ByteArrayWriter(int(sizeof(quint32))
		+ Serialize::bytearraySize(payload.credentialId)
		+ Serialize::bytearraySize(payload.challenge));
	stream << kPayloadVersion << payload.credentialId << payload.challenge;
	return std::move(stream).result();
}

[[nodiscard]] std::optional<Payload> ParsePayload(const QByteArray &data) {
	auto stream = Serialize::ByteArrayReader(data);
	auto version = quint32();
	auto result = Payload();
	stream >> version >> result.credentialId >> result.challenge;
	if (!stream.ok()
		|| !stream.atEnd()
		|| version != kPayloadVersion
		|| result.credentialId.size() != kCredentialIdSize
		|| result.challenge.size() != kChallengeSize) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] winrt::hstring CredentialName(const QByteArray &credentialId) {
	const auto hex = QString::fromLatin1(credentialId.toHex());
	return winrt::hstring(
		std::wstring(kCredentialPrefix) + hex.toStdWString());
}

// A Windows Hello prompt asked for without an owner window opens behind the
// app and only flashes in the taskbar. Windows 11 24H2 accepts the owner as a
// WindowId, so every ask resolves one from the window the user is acting in
// when the operation starts - later prompts of the same operation keep it,
// because by then the active window can be the previous prompt. Without the
// export, or with no active window, the prompts go unowned as before.
[[nodiscard]] std::optional<WindowId> PromptOwner() {
	static const auto loaded = [] {
		const auto library = base::Platform::SafeLoadLibrary(
			L"Windows.UI.dll");
		return base::Platform::LoadMethod(
			library,
			"GetWindowIdFromWindow",
			GetWindowIdFromWindow);
	}();
	const auto window = QApplication::activeWindow();
	if (!loaded || !window) {
		return std::nullopt;
	}
	auto id = WindowIdAbi();
	const auto handle = reinterpret_cast<HWND>(window->winId());
	if (FAILED(GetWindowIdFromWindow(handle, &id)) || !id.value) {
		return std::nullopt;
	}
	return WindowId{ id.value };
}

// An unowned prompt is shown by the credential broker, which may not take the
// foreground from the app the user is in, so it opens behind and flashes in
// the taskbar. Right after the user acted the app still is the foreground
// process, which lets it bring the prompt forward itself once the broker has
// shown it. The window is found by its class name, which is undocumented: a
// Windows that renames it leaves the prompt where the broker put it.
void FocusUnownedPrompt(int attemptsLeft = kPromptFocusAttempts) {
	const auto window = FindWindow(L"Credential Dialog Xaml Host", nullptr);
	if (window && IsWindowVisible(window)) {
		if (GetForegroundWindow() == window || SetForegroundWindow(window)) {
			LOG(("Wallet Info: the Windows Hello prompt is in the "
				"foreground, attempts left: %1.").arg(attemptsLeft));
			return;
		}
	}
	if (attemptsLeft > 0) {
		base::call_delayed(kPromptFocusInterval, [=] {
			FocusUnownedPrompt(attemptsLeft - 1);
		});
	} else {
		LOG(("Wallet Warning: the Windows Hello prompt was not brought to "
			"the foreground, found: %1.").arg(window ? 1 : 0));
	}
}

// The owned overloads exist from Windows 11 24H2: on an older system the
// factory or the credential lacks the interface, and the unowned ask runs
// with the prompt brought forward by its class name instead.
[[nodiscard]] IAsyncOperation<KeyCredentialRetrievalResult> RequestCreate(
		const winrt::hstring &name,
		const std::optional<WindowId> &owner) {
	const auto option = KeyCredentialCreationOption::ReplaceExisting;
	if (owner) {
		auto owned = base::WinRT::Try([&] {
			return KeyCredentialManager::RequestCreateForWindowAsync(
				*owner,
				name,
				option);
		});
		if (owned) {
			return std::move(*owned);
		}
	}
	auto result = KeyCredentialManager::RequestCreateAsync(name, option);
	FocusUnownedPrompt();
	return result;
}

[[nodiscard]] IAsyncOperation<KeyCredentialOperationResult> RequestSign(
		const KeyCredential &credential,
		const IBuffer &data,
		const std::optional<WindowId> &owner) {
	if (owner) {
		if (const auto owned = credential.try_as<IKeyCredentialWithWindow>()) {
			auto request = base::WinRT::Try([&] {
				return owned.RequestSignForWindowAsync(*owner, data);
			});
			if (request) {
				return std::move(*request);
			}
		}
	}
	auto result = credential.RequestSignAsync(data);
	FocusUnownedPrompt();
	return result;
}

[[nodiscard]] QByteArray RandomBytes(int size) {
	auto result = QByteArray(size, Qt::Uninitialized);
	base::RandomFill(result.data(), result.size());
	return result;
}

[[nodiscard]] IBuffer ToBuffer(const QByteArray &bytes) {
	return CryptographicBuffer::CreateFromByteArray(
		winrt::array_view<const uint8_t>(
			reinterpret_cast<const uint8_t*>(bytes.constData()),
			uint32_t(bytes.size())));
}

// The signature is copied into a SecureBytes and the runtime buffer we hold
// is zeroed in place; the runtime may keep other copies of the bytes it
// handed out, which is the same caveat the Touch ID provider records for
// its CFData - this zeroes the one copy under our control.
[[nodiscard]] Wallet::SecureBytes TakeSecure(const IBuffer &buffer) {
	if (!buffer) {
		return Wallet::SecureBytes();
	}
	const auto data = buffer.data();
	const auto length = buffer.Length();
	if (!data || !length) {
		return Wallet::SecureBytes();
	}
	auto result = Wallet::SecureBytes(bytes::make_span(data, length));
	SecureZeroMemory(data, length);
	return result;
}

// The one environment not offered Windows Hello is an unpacked copy of an
// official release: no uninstaller beside the executable and not a canary
// build. An installed copy and either canary are offered it whenever the
// TPM and Hello itself allow, and so is a Debug build working from its
// TelegramForcePortable folder, to debug the flow from the build output.
[[nodiscard]] bool UninstallerPresent() {
	static const auto Result = QFile::exists(cExeDir() + u"unins000.exe"_q);
	return Result;
}

[[nodiscard]] bool DebugPortable() {
#ifdef _DEBUG
	static const auto Result = (QDir(cWorkingDir())
		== QDir(cExeDir() + u"TelegramForcePortable"_q));
	return Result;
#else // _DEBUG
	return false;
#endif // _DEBUG
}

[[nodiscard]] bool ReadTpmPresence() {
	const auto library = base::Platform::SafeLoadLibrary(L"tbs.dll");
	if (!base::Platform::LoadMethod(
			library,
			"Tbsi_GetDeviceInfo",
			GetTpmDeviceInfo)) {
		return false;
	}
	auto info = TPM_DEVICE_INFO();
	const auto result = GetTpmDeviceInfo(sizeof(info), &info);
	LOG(("Wallet Info: TBS device info result %1, TPM version %2."
		).arg(result
		).arg(info.tpmVersion));
	return (result == TBS_SUCCESS) && (info.tpmVersion != 0);
}

[[nodiscard]] bool TpmPresent() {
	static const auto Result = ReadTpmPresence();
	return Result;
}

// NotFound alone is Absent: it is what OpenAsync answers once the credential
// is gone - after a DeleteAsync or a Windows Hello reset - the confirmed
// typed absence that lands the keyless modes, with nothing deleted here.
// UserCanceled is the dismissed prompt, and a Hello lockout
// (SecurityDeviceLocked) is answered the same way: Windows refuses the
// gesture for a while after too many wrong attempts, so the prompt is over
// for now and the user simply asks again once Hello accepts input, with no
// mode change and nothing written. Everything else is a service or access
// failure and stays Unavailable, which preserves the keyring; a non-Completed
// async status or a thrown HRESULT lands there too, the fail-safe side.
// AuthenticationFailed is never produced: Hello retries a wrong gesture
// inside its own UI and reports only the lockout.
[[nodiscard]] ProtectionError Classify(KeyCredentialStatus status) {
	switch (status) {
	case KeyCredentialStatus::Success:
		return ProtectionError::None;
	case KeyCredentialStatus::NotFound:
		LOG(("Wallet Error: Windows Hello credential not found."));
		return ProtectionError::Absent;
	case KeyCredentialStatus::UserCanceled:
		return ProtectionError::Cancelled;
	case KeyCredentialStatus::SecurityDeviceLocked:
		LOG(("Wallet Info: Windows Hello is locked out, prompt dismissed."));
		return ProtectionError::Cancelled;
	}
	LOG(("Wallet Error: Windows Hello operation failed, status %1."
		).arg(int(status)));
	return ProtectionError::Unavailable;
}

[[nodiscard]] ProtectionError ClassifyFailure(
		const IAsyncInfo &info,
		AsyncStatus status) {
	const auto code = base::WinRT::Try([&] {
		return uint32(info.ErrorCode().value);
	}).value_or(0);
	LOG(("Wallet Error: Windows Hello operation ended with async status %1, "
		"code 0x%2."
		).arg(int(status)
		).arg(code, 8, 16, QChar('0')));
	return ProtectionError::Unavailable;
}

[[nodiscard]] Wallet::SecureBytes DeriveWrapKey(
		const Wallet::SecureBytes &signature,
		const QByteArray &salt) {
	return Wallet::HkdfSha256(
		signature.span(),
		bytes::make_span(salt),
		bytes::make_span(kWrapLabel, sizeof(kWrapLabel) - 1),
		Wallet::kVaultKeySize);
}

[[nodiscard]] Retrieval ReadRetrieval(
		const IAsyncOperation<KeyCredentialRetrievalResult> &that,
		AsyncStatus status) {
	if (status != AsyncStatus::Completed) {
		return { .error = ClassifyFailure(that, status) };
	}
	auto read = base::WinRT::Try([&] {
		const auto result = that.GetResults();
		auto retrieval = Retrieval{ .error = Classify(result.Status()) };
		if (retrieval.error == ProtectionError::None) {
			retrieval.credential = result.Credential();
			if (!retrieval.credential) {
				retrieval.error = ProtectionError::Unavailable;
			}
		}
		return retrieval;
	});
	return read ? std::move(*read) : Retrieval();
}

[[nodiscard]] Signing ReadSigning(
		const IAsyncOperation<KeyCredentialOperationResult> &that,
		AsyncStatus status) {
	if (status != AsyncStatus::Completed) {
		return { .error = ClassifyFailure(that, status) };
	}
	auto read = base::WinRT::Try([&] {
		const auto result = that.GetResults();
		auto signing = Signing{ .error = Classify(result.Status()) };
		if (signing.error == ProtectionError::None) {
			signing.signature = TakeSecure(result.Result());
			if (signing.signature.empty()) {
				signing.error = ProtectionError::Unavailable;
			}
		}
		return signing;
	});
	return read ? std::move(*read) : Signing();
}

void AnswerEnroll(
		Fn<void(Wallet::ProtectionEnrollResult)> done,
		ProtectionError error) {
	crl::on_main([done = std::move(done), error] {
		done({ .error = error });
	});
}

void AnswerUnwrap(
		Fn<void(Wallet::ProtectionUnwrapResult)> done,
		ProtectionError error) {
	crl::on_main([done = std::move(done), error] {
		done({ .error = error });
	});
}

class WindowsHelloProtection final : public Wallet::ProtectionProvider {
public:
	[[nodiscard]] Wallet::VaultKind kind() const override;
	[[nodiscard]] rpl::producer<bool> available() const override;
	[[nodiscard]] rpl::producer<QString> title() const override;
	[[nodiscard]] rpl::producer<QString> description() const override;
	[[nodiscard]] rpl::producer<QString> label() const override;

	void enroll(
		not_null<Storage::Account*> local,
		Fn<void(Wallet::ProtectionEnrollResult)> done) override;
	void unwrap(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(Wallet::ProtectionUnwrapResult)> done) override;
	void remove(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(ProtectionError)> done) override;

	void prime();

private:
	void signForEnroll(std::shared_ptr<EnrollOperation> op);
	void signForUnwrap(std::shared_ptr<UnwrapOperation> op);
	void discardCredential(const winrt::hstring &name);

	rpl::variable<bool> _available = false;
	bool _uninstaller = false;
	bool _debugPortable = false;
	bool _tpm = false;

};

Wallet::VaultKind WindowsHelloProtection::kind() const {
	return Wallet::VaultKind::WindowsHello;
}

rpl::producer<bool> WindowsHelloProtection::available() const {
	return _available.value();
}

rpl::producer<QString> WindowsHelloProtection::title() const {
	return tr::lng_settings_use_winhello();
}

rpl::producer<QString> WindowsHelloProtection::description() const {
	return tr::lng_wallet_protection_hello_about();
}

rpl::producer<QString> WindowsHelloProtection::label() const {
	return tr::lng_wallet_protection_hello_label();
}

void WindowsHelloProtection::prime() {
	_uninstaller = UninstallerPresent();
	_debugPortable = DebugPortable();
	_tpm = TpmPresent();
	const auto started = base::WinRT::Try([&] {
		KeyCredentialManager::IsSupportedAsync().Completed([this](
				IAsyncOperation<bool> that,
				AsyncStatus status) {
			const auto supported = (status == AsyncStatus::Completed)
				&& base::WinRT::Try([&] {
					return that.GetResults();
				}).value_or(false);
			crl::on_main([this, supported] {
				_available = (_uninstaller
						|| Core::BuildIsCanary
						|| _debugPortable)
					&& _tpm
					&& supported;
				LOG(("Wallet Info: Windows Hello availability: "
					"uninstaller %1, canary %2, debug portable %3, "
					"TPM %4, supported %5."
					).arg(_uninstaller ? 1 : 0
					).arg(Core::BuildIsCanary ? 1 : 0
					).arg(_debugPortable ? 1 : 0
					).arg(_tpm ? 1 : 0
					).arg(supported ? 1 : 0));
			});
		});
	});
	if (!started) {
		LOG(("Wallet Error: Windows Hello support check could not start."));
	}
}

void WindowsHelloProtection::enroll(
		not_null<Storage::Account*> local,
		Fn<void(Wallet::ProtectionEnrollResult)> done) {
	if (!_available.current()) {
		AnswerEnroll(std::move(done), ProtectionError::Unavailable);
		return;
	}
	const auto credentialId = RandomBytes(kCredentialIdSize);
	auto op = std::make_shared<EnrollOperation>();
	op->name = CredentialName(credentialId);
	op->challenge = RandomBytes(kChallengeSize);
	op->owner = PromptOwner();
	op->wrap = Wallet::VaultWrap{
		.kind = Wallet::VaultKind::WindowsHello,
		.salt = RandomBytes(Wallet::kVaultSaltSize),
		.openSecret = SerializePayload({
			.credentialId = credentialId,
			.challenge = op->challenge,
		}),
	};
	op->done = std::move(done);
	const auto started = base::WinRT::Try([&] {
		RequestCreate(op->name, op->owner).Completed([op, this](
				IAsyncOperation<KeyCredentialRetrievalResult> that,
				AsyncStatus status) {
			auto retrieval = ReadRetrieval(that, status);
			crl::on_main([op, result = std::move(retrieval), this]() mutable {
				if (result.error != ProtectionError::None) {
					if (result.error == ProtectionError::Unavailable) {
						discardCredential(op->name);
					}
					op->done({ .error = result.error });
					return;
				}
				op->credential = std::move(result.credential);
				signForEnroll(op);
			});
		});
	});
	if (!started) {
		AnswerEnroll(std::move(op->done), ProtectionError::Unavailable);
	}
}

void WindowsHelloProtection::signForEnroll(
		std::shared_ptr<EnrollOperation> op) {
	const auto started = base::WinRT::Try([&] {
		RequestSign(
			op->credential,
			ToBuffer(op->challenge),
			op->owner
		).Completed([op, this](
				IAsyncOperation<KeyCredentialOperationResult> that,
				AsyncStatus status) {
			auto signing = ReadSigning(that, status);
			crl::on_main([op, signing = std::move(signing), this]() mutable {
				if (signing.error != ProtectionError::None) {
					discardCredential(op->name);
					op->done({ .error = signing.error });
					return;
				}
				auto wrapKey = DeriveWrapKey(signing.signature, op->wrap.salt);
				signing.signature.clear();
				if (wrapKey.empty()) {
					discardCredential(op->name);
					op->done({ .error = ProtectionError::Unavailable });
					return;
				}
				op->done({
					.wrap = Wallet::VaultPreparedWrap{
						.wrap = std::move(op->wrap),
						.wrapKey = std::move(wrapKey),
					},
					.error = ProtectionError::None,
				});
			});
		});
	});
	if (!started) {
		discardCredential(op->name);
		AnswerEnroll(std::move(op->done), ProtectionError::Unavailable);
	}
}

void WindowsHelloProtection::unwrap(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(Wallet::ProtectionUnwrapResult)> done) {
	auto payload = ParsePayload(wrap.openSecret);
	if (!payload) {
		AnswerUnwrap(std::move(done), ProtectionError::Corrupt);
		return;
	}
	auto op = std::make_shared<UnwrapOperation>();
	op->name = CredentialName(payload->credentialId);
	op->challenge = std::move(payload->challenge);
	op->owner = PromptOwner();
	op->wrap = std::move(wrap);
	op->done = std::move(done);
	const auto started = base::WinRT::Try([&] {
		KeyCredentialManager::OpenAsync(op->name).Completed([op, this](
				IAsyncOperation<KeyCredentialRetrievalResult> that,
				AsyncStatus status) {
			auto retrieval = ReadRetrieval(that, status);
			crl::on_main([op, result = std::move(retrieval), this]() mutable {
				if (result.error != ProtectionError::None) {
					op->done({ .error = result.error });
					return;
				}
				op->credential = std::move(result.credential);
				signForUnwrap(op);
			});
		});
	});
	if (!started) {
		AnswerUnwrap(std::move(op->done), ProtectionError::Unavailable);
	}
}

void WindowsHelloProtection::signForUnwrap(
		std::shared_ptr<UnwrapOperation> op) {
	const auto started = base::WinRT::Try([&] {
		RequestSign(
			op->credential,
			ToBuffer(op->challenge),
			op->owner
		).Completed([op](
				IAsyncOperation<KeyCredentialOperationResult> that,
				AsyncStatus status) {
			auto signing = ReadSigning(that, status);
			crl::on_main([op, signing = std::move(signing)]() mutable {
				if (signing.error != ProtectionError::None) {
					op->done({ .error = signing.error });
					return;
				}
				const auto wrapKey = DeriveWrapKey(
					signing.signature,
					op->wrap.salt);
				signing.signature.clear();
				if (wrapKey.empty()) {
					op->done({ .error = ProtectionError::Unavailable });
					return;
				}
				auto key = Wallet::UnwrapVaultKey(op->wrap, wrapKey);
				if (!key) {
					op->done({ .error = ProtectionError::Corrupt });
					return;
				}
				op->done({
					.key = std::move(key),
					.error = ProtectionError::None,
				});
			});
		});
	});
	if (!started) {
		AnswerUnwrap(std::move(op->done), ProtectionError::Unavailable);
	}
}

void WindowsHelloProtection::remove(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(ProtectionError)> done) {
	if (const auto payload = ParsePayload(wrap.openSecret)) {
		discardCredential(CredentialName(payload->credentialId));
	}
	crl::on_main([done = std::move(done)] {
		done(ProtectionError::None);
	});
}

// Retirement follows the successful replacement or removal of the one
// device wrap. An abandoned enrollment is also safe to retire because no
// committed ring names it. Native deletion stays asynchronous: an already
// missing credential is harmless, and other failures are logged by the
// existing provider path without changing the stored protection.
void WindowsHelloProtection::discardCredential(const winrt::hstring &name) {
	base::WinRT::Try([&] {
		KeyCredentialManager::DeleteAsync(name).Completed([](
				IAsyncAction,
				AsyncStatus status) {
			if (status != AsyncStatus::Completed) {
				LOG(("Wallet Warning: Windows Hello credential deletion "
					"ended with async status %1.").arg(int(status)));
			}
		});
	});
}

} // namespace

void RegisterWalletProtectionProvider() {
	auto provider = std::make_unique<WindowsHelloProtection>();
	const auto raw = provider.get();
	Wallet::RegisterProtectionProvider(std::move(provider));
	if (Wallet::ProtectionProviderFor(Wallet::VaultKind::WindowsHello) == raw) {
		raw->prime();
	}
}

} // namespace Platform
