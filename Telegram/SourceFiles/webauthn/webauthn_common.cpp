/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/webauthn_common.h"

#if !defined Q_OS_WIN && !defined Q_OS_MAC
#include "base/platform/linux/base_linux_library.h"
#endif // !Q_OS_WIN && !Q_OS_MAC
#include "base/weak_qptr.h"
#include "core/application.h"
#include "data/data_passkey_deserialize.h"
#include "lang/lang_keys.h"
#include "platform/platform_webauthn.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "webauthn/cable.h"
#include "window/window_controller.h"

#include "styles/style_layers.h"

#include <crl/crl.h>

#include <fido.h>

#include <mutex>
#include <vector>

#if !defined Q_OS_WIN && !defined Q_OS_MAC
extern "C" {
void _libudev_so_tramp_resolve_all(void) __attribute__((weak));
} // extern "C"
#endif // !Q_OS_WIN && !Q_OS_MAC

namespace Platform::WebAuthn {
namespace {

constexpr auto kDefaultTimeout = 60000;

[[nodiscard]] bool UdevLibraryAvailable() {
#if !defined Q_OS_WIN && !defined Q_OS_MAC
	static const auto available = !_libudev_so_tramp_resolve_all
		|| base::Platform::LoadLibrary("libudev.so.1");
	return available;
#else // !Q_OS_WIN && !Q_OS_MAC
	return true;
#endif // Q_OS_WIN || Q_OS_MAC
}

enum class Outcome {
	Success,
	NeedPin,
	PinInvalid,
	PinBlocked,
	NoDevice,
	NoCredentials,
	Other,
};

class Ceremony final {
public:
	void setDevice(fido_dev_t *dev) {
		const auto lock = std::lock_guard(_mutex);
		_dev = dev;
		if (_cancelled && _dev) {
			fido_dev_cancel(_dev);
		}
	}
	void clearDevice() {
		const auto lock = std::lock_guard(_mutex);
		_dev = nullptr;
	}
	void cancel() {
		const auto lock = std::lock_guard(_mutex);
		_cancelled = true;
		if (_dev) {
			fido_dev_cancel(_dev);
		}
	}
	[[nodiscard]] bool cancelled() {
		const auto lock = std::lock_guard(_mutex);
		return _cancelled;
	}

private:
	std::mutex _mutex;
	fido_dev_t *_dev = nullptr;
	bool _cancelled = false;

};

struct LoginState {
	Data::Passkey::LoginData data;
	std::string clientDataJson;
	Fn<void(LoginResult)> done;
	std::shared_ptr<Ceremony> ceremony;
	base::weak_qptr<Ui::GenericBox> box;
	bool finished = false;
	bool replacingBox = false;
};

struct RegisterState {
	Data::Passkey::RegisterData data;
	std::string clientDataJson;
	Fn<void(RegisterResult)> done;
	std::shared_ptr<Ceremony> ceremony;
	base::weak_qptr<Ui::GenericBox> box;
	bool finished = false;
	bool replacingBox = false;
};

[[nodiscard]] Window::Controller *ActiveController() {
	if (const auto active = Core::App().activeWindow()) {
		return active;
	}
	return Core::App().activePrimaryWindow();
}

[[nodiscard]] std::shared_ptr<Ui::Show> ActiveShow() {
	const auto controller = ActiveController();
	return controller ? controller->uiShow() : nullptr;
}

void ShowMessage(const QString &text) {
	if (const auto controller = ActiveController()) {
		controller->showToast(text);
	}
}

[[nodiscard]] base::weak_qptr<Ui::GenericBox> ShowTouchBox(Fn<void()> cancel) {
	const auto show = ActiveShow();
	if (!show) {
		return {};
	}
	return show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_passkey_touch());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_passkey_insert(),
			st::boxLabel));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		box->boxClosing() | rpl::on_next([=] {
			cancel();
		}, box->lifetime());
	}));
}

[[nodiscard]] base::weak_qptr<Ui::GenericBox> ShowPinBox(
		bool invalid,
		Fn<void(QString)> submit,
		Fn<void()> cancel) {
	const auto show = ActiveShow();
	if (!show) {
		cancel();
		return {};
	}
	return show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_passkey_pin_title());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			(invalid
				? tr::lng_passkey_pin_invalid()
				: tr::lng_passkey_pin_about()),
			st::boxLabel));
		const auto field = Settings::CloudPassword::AddPasswordField(
			box->verticalLayout(),
			tr::lng_passkey_pin_placeholder(),
			QString());
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(tr::lng_passkey_confirm(), [=] {
			const auto pin = field->getLastText();
			if (!pin.isEmpty()) {
				submit(pin);
			}
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		box->boxClosing() | rpl::on_next([=] {
			cancel();
		}, box->lifetime());
	}));
}

template <typename State>
void CloseBox(const std::shared_ptr<State> &state) {
	state->replacingBox = true;
	if (const auto strong = state->box.get()) {
		strong->closeBox();
	}
	state->box = {};
	state->replacingBox = false;
}

[[nodiscard]] QByteArray NoneAttestationObject(const QByteArray &authData) {
	static const char kPrefix[] = {
		'\xA3',
		'\x63', 'f', 'm', 't', '\x64', 'n', 'o', 'n', 'e',
		'\x67', 'a', 't', 't', 'S', 't', 'm', 't', '\xA0',
		'\x68', 'a', 'u', 't', 'h', 'D', 'a', 't', 'a',
	};
	auto result = QByteArray(kPrefix, sizeof(kPrefix));
	const auto length = authData.size();
	if (length < 24) {
		result.append(char(0x40 | length));
	} else if (length < 256) {
		result.append(char(0x58));
		result.append(char(length));
	} else {
		result.append(char(0x59));
		result.append(char((length >> 8) & 0xFF));
		result.append(char(length & 0xFF));
	}
	result.append(authData);
	return result;
}

[[nodiscard]] fido_opt_t UserVerification(const QString &requirement) {
	if (requirement == u"required"_q) {
		return FIDO_OPT_TRUE;
	} else if (requirement == u"discouraged"_q) {
		return FIDO_OPT_FALSE;
	}
	return FIDO_OPT_OMIT;
}

[[nodiscard]] int PreferredAlgorithm(
		const std::vector<Data::Passkey::CredentialParameter> &params) {
	for (const auto &param : params) {
		if (param.alg == COSE_ES256) {
			return COSE_ES256;
		}
	}
	for (const auto &param : params) {
		if (param.alg == COSE_EDDSA || param.alg == COSE_RS256) {
			return param.alg;
		}
	}
	return COSE_ES256;
}

[[nodiscard]] std::vector<QByteArray> DevicePaths() {
	auto result = std::vector<QByteArray>();
	if (!UdevLibraryAvailable()) {
		return result;
	}
	constexpr auto kMax = size_t(64);
	auto list = fido_dev_info_new(kMax);
	if (!list) {
		return result;
	}
	auto found = size_t(0);
	if (fido_dev_info_manifest(list, kMax, &found) == FIDO_OK) {
		for (auto i = size_t(0); i != found; ++i) {
			if (const auto info = fido_dev_info_ptr(list, i)) {
				if (const auto path = fido_dev_info_path(info)) {
					result.emplace_back(path);
				}
			}
		}
	}
	fido_dev_info_free(&list, kMax);
	return result;
}

[[nodiscard]] Outcome MapError(int code) {
	switch (code) {
	case FIDO_ERR_PIN_REQUIRED:
		return Outcome::NeedPin;
	case FIDO_ERR_PIN_INVALID:
		return Outcome::PinInvalid;
	case FIDO_ERR_PIN_AUTH_BLOCKED:
	case FIDO_ERR_PIN_BLOCKED:
		return Outcome::PinBlocked;
	case FIDO_ERR_NO_CREDENTIALS:
		return Outcome::NoCredentials;
	}
	return Outcome::Other;
}

[[nodiscard]] Outcome RunLoginCeremony(
		const LoginState &state,
		const QString &pin,
		LoginResult &out) {
	const auto paths = DevicePaths();
	if (paths.empty()) {
		return Outcome::NoDevice;
	}
	const auto &clientData = state.clientDataJson;
	const auto rpId = state.data.rpId.toUtf8();
	const auto pinUtf8 = pin.toUtf8();
	const auto pinPtr = pin.isEmpty() ? nullptr : pinUtf8.constData();
	const auto timeout = (state.data.timeout > 0)
		? state.data.timeout
		: kDefaultTimeout;

	auto last = Outcome::Other;
	for (const auto &path : paths) {
		auto dev = fido_dev_new();
		if (!dev) {
			continue;
		}
		fido_dev_set_timeout(dev, timeout);
		const auto opened = fido_dev_open(dev, path.constData());
		if (opened != FIDO_OK) {
			LOG(("Passkey Error: Could not open the key: %1."
				).arg(QString::fromUtf8(fido_strerr(opened))));
			fido_dev_free(&dev);
			continue;
		}
		state.ceremony->setDevice(dev);

		auto assert = fido_assert_new();
		fido_assert_set_clientdata(
			assert,
			reinterpret_cast<const unsigned char*>(clientData.data()),
			clientData.size());
		fido_assert_set_rp(assert, rpId.constData());
		for (const auto &cred : state.data.allowCredentials) {
			fido_assert_allow_cred(
				assert,
				reinterpret_cast<const unsigned char*>(cred.id.constData()),
				cred.id.size());
		}
		fido_assert_set_uv(assert, UserVerification(state.data.userVerification));

		const auto code = fido_dev_get_assert(dev, assert, pinPtr);

		state.ceremony->clearDevice();
		fido_dev_close(dev);
		fido_dev_free(&dev);

		if (code == FIDO_OK && fido_assert_count(assert) > 0) {
			out.clientDataJSON = QByteArray::fromStdString(clientData);
			out.credentialId = QByteArray(
				reinterpret_cast<const char*>(fido_assert_id_ptr(assert, 0)),
				fido_assert_id_len(assert, 0));
			out.authenticatorData = QByteArray(
				reinterpret_cast<const char*>(
					fido_assert_authdata_raw_ptr(assert, 0)),
				fido_assert_authdata_raw_len(assert, 0));
			out.signature = QByteArray(
				reinterpret_cast<const char*>(fido_assert_sig_ptr(assert, 0)),
				fido_assert_sig_len(assert, 0));
			out.userHandle = QByteArray(
				reinterpret_cast<const char*>(
					fido_assert_user_id_ptr(assert, 0)),
				fido_assert_user_id_len(assert, 0));
			fido_assert_free(&assert);
			return Outcome::Success;
		}
		fido_assert_free(&assert);

		LOG(("Passkey Error: Assertion failed: %1."
			).arg(QString::fromUtf8(fido_strerr(code))));
		const auto mapped = MapError(code);
		if (mapped == Outcome::NeedPin
			|| mapped == Outcome::PinInvalid
			|| mapped == Outcome::PinBlocked) {
			return mapped;
		}
		last = mapped;
	}
	return last;
}

[[nodiscard]] Outcome RunRegisterCeremony(
		const RegisterState &state,
		const QString &pin,
		RegisterResult &out) {
	const auto paths = DevicePaths();
	if (paths.empty()) {
		return Outcome::NoDevice;
	}
	const auto &clientData = state.clientDataJson;
	const auto rpId = state.data.rp.id.toUtf8();
	const auto rpName = state.data.rp.name.toUtf8();
	const auto userName = state.data.user.name.toUtf8();
	const auto userDisplayName = state.data.user.displayName.toUtf8();
	const auto pinUtf8 = pin.toUtf8();
	const auto pinPtr = pin.isEmpty() ? nullptr : pinUtf8.constData();
	const auto timeout = (state.data.timeout > 0)
		? state.data.timeout
		: kDefaultTimeout;

	auto dev = (fido_dev_t*)nullptr;
	for (const auto &path : paths) {
		dev = fido_dev_new();
		if (!dev) {
			continue;
		}
		fido_dev_set_timeout(dev, timeout);
		const auto opened = fido_dev_open(dev, path.constData());
		if (opened == FIDO_OK) {
			break;
		}
		LOG(("Passkey Error: Could not open the key: %1."
			).arg(QString::fromUtf8(fido_strerr(opened))));
		fido_dev_free(&dev);
		dev = nullptr;
	}
	if (!dev) {
		return Outcome::Other;
	}
	state.ceremony->setDevice(dev);

	auto cred = fido_cred_new();
	fido_cred_set_type(cred, PreferredAlgorithm(state.data.pubKeyCredParams));
	fido_cred_set_clientdata(
		cred,
		reinterpret_cast<const unsigned char*>(clientData.data()),
		clientData.size());
	fido_cred_set_rp(cred, rpId.constData(), rpName.constData());
	fido_cred_set_user(
		cred,
		reinterpret_cast<const unsigned char*>(state.data.user.id.constData()),
		state.data.user.id.size(),
		userName.constData(),
		userDisplayName.constData(),
		nullptr);
	fido_cred_set_rk(cred, FIDO_OPT_TRUE);
	fido_cred_set_uv(cred, FIDO_OPT_OMIT);

	const auto code = fido_dev_make_cred(dev, cred, pinPtr);

	state.ceremony->clearDevice();
	fido_dev_close(dev);
	fido_dev_free(&dev);

	auto outcome = Outcome::Other;
	if (code == FIDO_OK) {
		const auto authData = QByteArray(
			reinterpret_cast<const char*>(fido_cred_authdata_raw_ptr(cred)),
			fido_cred_authdata_raw_len(cred));
		out.credentialId = QByteArray(
			reinterpret_cast<const char*>(fido_cred_id_ptr(cred)),
			fido_cred_id_len(cred));
		out.attestationObject = NoneAttestationObject(authData);
		out.clientDataJSON = QByteArray::fromStdString(clientData);
		out.success = true;
		outcome = Outcome::Success;
	} else {
		LOG(("Passkey Error: Credential failed: %1."
			).arg(QString::fromUtf8(fido_strerr(code))));
		outcome = MapError(code);
	}
	fido_cred_free(&cred);
	return outcome;
}

void FinishLogin(std::shared_ptr<LoginState> state, LoginResult result) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	CloseBox(state);
	state->done(result);
}

void AttemptLogin(std::shared_ptr<LoginState> state, QString pin) {
	CloseBox(state);
	state->box = ShowTouchBox([state] {
		if (state->replacingBox) {
			return;
		}
		crl::on_main([state] {
			state->ceremony->cancel();
			auto result = LoginResult();
			result.error = Error::Cancelled;
			FinishLogin(state, result);
		});
	});
	crl::async([state, pin] {
		auto result = LoginResult();
		const auto outcome = RunLoginCeremony(*state, pin, result);
		crl::on_main([state, outcome, result] {
			if (state->finished) {
				return;
			} else if (state->ceremony->cancelled()) {
				auto cancelled = LoginResult();
				cancelled.error = Error::Cancelled;
				FinishLogin(state, cancelled);
				return;
			}
			switch (outcome) {
			case Outcome::Success:
				FinishLogin(state, result);
				return;
			case Outcome::NeedPin:
			case Outcome::PinInvalid:
				CloseBox(state);
				state->box = ShowPinBox(
					(outcome == Outcome::PinInvalid),
					[state](QString pin) { AttemptLogin(state, pin); },
					[state] {
						if (state->replacingBox) {
							return;
						}
						crl::on_main([state] {
							auto cancelled = LoginResult();
							cancelled.error = Error::Cancelled;
							FinishLogin(state, cancelled);
						});
					});
				return;
			case Outcome::NoDevice:
				ShowMessage(tr::lng_passkey_error_no_device(tr::now));
				break;
			case Outcome::NoCredentials:
				ShowMessage(tr::lng_passkey_error_no_credentials(tr::now));
				break;
			case Outcome::PinBlocked:
				ShowMessage(tr::lng_passkey_pin_blocked(tr::now));
				break;
			case Outcome::Other:
				ShowMessage(tr::lng_passkey_error_login(tr::now));
				break;
			}
			auto failed = LoginResult();
			failed.error = Error::Other;
			FinishLogin(state, failed);
		});
	});
}

void FinishRegister(std::shared_ptr<RegisterState> state, RegisterResult result) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	CloseBox(state);
	state->done(result);
}

void AttemptRegister(std::shared_ptr<RegisterState> state, QString pin) {
	CloseBox(state);
	state->box = ShowTouchBox([state] {
		if (state->replacingBox) {
			return;
		}
		crl::on_main([state] {
			state->ceremony->cancel();
			FinishRegister(state, RegisterResult());
		});
	});
	crl::async([state, pin] {
		auto result = RegisterResult();
		const auto outcome = RunRegisterCeremony(*state, pin, result);
		crl::on_main([state, outcome, result] {
			if (state->finished) {
				return;
			} else if (state->ceremony->cancelled()) {
				FinishRegister(state, RegisterResult());
				return;
			}
			switch (outcome) {
			case Outcome::Success:
				FinishRegister(state, result);
				return;
			case Outcome::NeedPin:
			case Outcome::PinInvalid:
				CloseBox(state);
				state->box = ShowPinBox(
					(outcome == Outcome::PinInvalid),
					[state](QString pin) { AttemptRegister(state, pin); },
					[state] {
						if (state->replacingBox) {
							return;
						}
						crl::on_main([state] {
							FinishRegister(state, RegisterResult());
						});
					});
				return;
			case Outcome::NoDevice:
				ShowMessage(tr::lng_passkey_error_no_device(tr::now));
				break;
			case Outcome::PinBlocked:
				ShowMessage(tr::lng_passkey_pin_blocked(tr::now));
				break;
			case Outcome::NoCredentials:
			case Outcome::Other:
				ShowMessage(tr::lng_passkey_error_register(tr::now));
				break;
			}
			FinishRegister(state, RegisterResult());
		});
	});
}

[[nodiscard]] Error MapCableError(Cable::Outcome outcome) {
	switch (outcome) {
	case Cable::Outcome::Cancelled:
		return Error::Cancelled;
	case Cable::Outcome::SecurityKey:
	case Cable::Outcome::NoBluetooth:
	case Cable::Outcome::Failed:
	case Cable::Outcome::Success:
		break;
	}
	return Error::Other;
}

[[nodiscard]] Cable::Bytes ToCableBytes(const QByteArray &bytes) {
	return Cable::Bytes(bytes.begin(), bytes.end());
}

[[nodiscard]] QByteArray FromCableBytes(const Cable::Bytes &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.data()),
		int(bytes.size()));
}

[[nodiscard]] Cable::Bytes ClientDataHash(const std::string &clientDataJson) {
	const auto digest = Cable::Sha256Digest(Cable::ByteSpan(
		reinterpret_cast<const uint8_t*>(clientDataJson.data()),
		clientDataJson.size()));
	return Cable::Bytes(digest.begin(), digest.end());
}

} // namespace

bool Libfido2DevicePresent() {
	return !DevicePaths().empty();
}

void RegisterViaLibfido2(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult)> callback) {
	auto state = std::make_shared<RegisterState>();
	state->data = data;
	state->clientDataJson = Data::Passkey::SerializeClientDataCreate(
		data.challenge);
	state->done = std::move(callback);
	state->ceremony = std::make_shared<Ceremony>();
	AttemptRegister(state, QString());
}

void LoginViaLibfido2(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult)> callback) {
	auto state = std::make_shared<LoginState>();
	state->data = data;
	state->clientDataJson = Data::Passkey::SerializeClientDataGet(
		data.challenge);
	state->done = std::move(callback);
	state->ceremony = std::make_shared<Ceremony>();
	AttemptLogin(state, QString());
}

void RegisterViaCable(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult)> callback) {
	const auto clientDataJson = std::make_shared<std::string>(
		Data::Passkey::SerializeClientDataCreate(data.challenge));
	auto request = Cable::RegisterRequest();
	request.rpId = data.rp.id.toStdString();
	request.rpName = data.rp.name.toStdString();
	request.userId = ToCableBytes(data.user.id);
	request.userName = data.user.name.toStdString();
	request.userDisplayName = data.user.displayName.toStdString();
	request.clientDataHash = ClientDataHash(*clientDataJson);
	request.timeoutMs = data.timeout;
	for (const auto &param : data.pubKeyCredParams) {
		request.algorithms.push_back(param.alg);
	}
	if (request.algorithms.empty()) {
		request.algorithms.push_back(COSE_ES256);
	}
	Cable::Register(std::move(request), [=](Cable::RegisterResult cable) {
		if (cable.outcome == Cable::Outcome::SecurityKey) {
			RegisterViaSecurityKey(data, callback);
			return;
		}
		auto result = RegisterResult();
		if (cable.outcome == Cable::Outcome::Success) {
			result.credentialId = FromCableBytes(cable.credentialId);
			result.attestationObject = NoneAttestationObject(
				FromCableBytes(cable.authData));
			result.clientDataJSON = QByteArray::fromStdString(*clientDataJson);
			result.success = true;
		} else {
			result.error = MapCableError(cable.outcome);
		}
		callback(result);
	});
}

void LoginViaCable(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult)> callback) {
	const auto clientDataJson = std::make_shared<std::string>(
		Data::Passkey::SerializeClientDataGet(data.challenge));
	auto request = Cable::LoginRequest();
	request.rpId = data.rpId.toStdString();
	request.clientDataHash = ClientDataHash(*clientDataJson);
	request.timeoutMs = data.timeout;
	for (const auto &cred : data.allowCredentials) {
		request.allowCredentialIds.push_back(ToCableBytes(cred.id));
	}
	Cable::Login(std::move(request), [=](Cable::LoginResult cable) {
		if (cable.outcome == Cable::Outcome::SecurityKey) {
			LoginViaSecurityKey(data, callback);
			return;
		}
		auto result = LoginResult();
		if (cable.outcome == Cable::Outcome::Success) {
			result.credentialId = FromCableBytes(cable.credentialId);
			result.clientDataJSON = QByteArray::fromStdString(*clientDataJson);
			result.authenticatorData = FromCableBytes(cable.authData);
			result.signature = FromCableBytes(cable.signature);
			result.userHandle = FromCableBytes(cable.userHandle);
		} else {
			result.error = MapCableError(cable.outcome);
		}
		callback(result);
	});
}

} // namespace Platform::WebAuthn
