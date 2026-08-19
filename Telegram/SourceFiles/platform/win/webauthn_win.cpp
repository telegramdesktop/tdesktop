/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include "platform/platform_webauthn.h"

#include "base/platform/win/base_windows_safe_library.h"
#include "core/application.h"
#include "data/data_passkey_deserialize.h"
#include "lang/lang_keys.h"
#include "webauthn/webauthn_common.h"
#include "window/window_controller.h"

#include <windows.h>
#include <combaseapi.h>
#include <webauthn.h>

namespace Platform::WebAuthn {
namespace {

HRESULT(__stdcall *WebAuthNIsUserVerifyingPlatformAuthenticatorAvailable)(
	BOOL *pbIsUserVerifyingPlatformAuthenticatorAvailable);
HRESULT(__stdcall *WebAuthNAuthenticatorMakeCredential)(
	HWND hWnd,
	PCWEBAUTHN_RP_ENTITY_INFORMATION pRpInformation,
	PCWEBAUTHN_USER_ENTITY_INFORMATION pUserInformation,
	PCWEBAUTHN_COSE_CREDENTIAL_PARAMETERS pPubKeyCredParams,
	PCWEBAUTHN_CLIENT_DATA pWebAuthNClientData,
	PCWEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS
		pWebAuthNCredentialOptions,
	PWEBAUTHN_CREDENTIAL_ATTESTATION *ppWebAuthNCredentialAttestation);
void(__stdcall *WebAuthNFreeCredentialAttestation)(
	PWEBAUTHN_CREDENTIAL_ATTESTATION pWebAuthNCredentialAttestation);
HRESULT(__stdcall *WebAuthNAuthenticatorGetAssertion)(
	HWND hWnd,
	LPCWSTR pwszRpId,
	PCWEBAUTHN_CLIENT_DATA pWebAuthNClientData,
	PCWEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS
		pWebAuthNGetAssertionOptions,
	PWEBAUTHN_ASSERTION *ppWebAuthNAssertion);
void(__stdcall *WebAuthNFreeAssertion)(
	PWEBAUTHN_ASSERTION pWebAuthNAssertion);

[[nodiscard]] bool Resolve() {
	const auto webauthn = base::Platform::SafeLoadLibrary(L"webauthn.dll");
	if (!webauthn) {
		return false;
	}
	auto total = 0, resolved = 0;
#define LOAD_SYMBOL(name) \
	++total; \
	if (base::Platform::LoadMethod(webauthn, #name, name)) ++resolved;

	LOAD_SYMBOL(WebAuthNIsUserVerifyingPlatformAuthenticatorAvailable);
	LOAD_SYMBOL(WebAuthNAuthenticatorMakeCredential);
	LOAD_SYMBOL(WebAuthNFreeCredentialAttestation);
	LOAD_SYMBOL(WebAuthNAuthenticatorGetAssertion);
	LOAD_SYMBOL(WebAuthNFreeAssertion);
#undef LOAD_SYMBOL

	return (total == resolved);
}

[[nodiscard]] bool Supported() {
	static const auto Result = Resolve();
	return Result;
}

[[nodiscard]] bool NativeAuthenticatorAvailable() {
	if (!Supported()) {
		return false;
	}
	auto available = (BOOL)(FALSE);
	return SUCCEEDED(
		WebAuthNIsUserVerifyingPlatformAuthenticatorAvailable(&available))
			&& available;
}

[[nodiscard]] bool NativeCancelled(HRESULT hr) {
	return (hr == NTE_USER_CANCELLED)
		|| (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED));
}

[[nodiscard]] Window::Controller *ActiveController() {
	if (const auto active = Core::App().activeWindow()) {
		return active;
	}
	return Core::App().activePrimaryWindow();
}

void ShowNativeError(const QString &text) {
	if (const auto controller = ActiveController()) {
		controller->showToast(text);
	}
}

[[nodiscard]] HWND NativeParentWindow() {
	const auto controller = ActiveController();
	if (!controller) {
		return nullptr;
	}
	const auto result = HWND(controller->widget()->winId());
	if (result) {
		SetForegroundWindow(result);
		SetFocus(result);
	}
	return result;
}

void RegisterViaNative(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult result)> callback) {
	if (!Supported()) {
		callback({});
		return;
	}

	auto rpId = data.rp.id.toStdWString();
	auto rpName = data.rp.name.toStdWString();
	auto userName = data.user.name.toStdWString();
	auto userDisplayName = data.user.displayName.toStdWString();

	auto rpInfo = WEBAUTHN_RP_ENTITY_INFORMATION();
	memset(&rpInfo, 0, sizeof(rpInfo));
	rpInfo.dwVersion =
		WEBAUTHN_RP_ENTITY_INFORMATION_CURRENT_VERSION;
	rpInfo.pwszId = rpId.c_str();
	rpInfo.pwszName = rpName.c_str();

	auto userInfo = WEBAUTHN_USER_ENTITY_INFORMATION();
	memset(&userInfo, 0, sizeof(userInfo));
	userInfo.dwVersion =
		WEBAUTHN_USER_ENTITY_INFORMATION_CURRENT_VERSION;
	userInfo.cbId = data.user.id.size();
	userInfo.pbId = (PBYTE)data.user.id.data();
	userInfo.pwszName = userName.c_str();
	userInfo.pwszDisplayName = userDisplayName.c_str();

	auto credParams = std::vector<WEBAUTHN_COSE_CREDENTIAL_PARAMETER>();
	for (const auto &param : data.pubKeyCredParams) {
		auto cp = WEBAUTHN_COSE_CREDENTIAL_PARAMETER{};
		cp.dwVersion =
			WEBAUTHN_COSE_CREDENTIAL_PARAMETER_CURRENT_VERSION;
		auto type = param.type.toStdWString();
		cp.pwszCredentialType = type == L"public-key"
			? WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY
			: L"";
		cp.lAlg = param.alg;
		credParams.push_back(cp);
	}

	auto credParamsList = WEBAUTHN_COSE_CREDENTIAL_PARAMETERS();
	memset(&credParamsList, 0, sizeof(credParamsList));
	credParamsList.cCredentialParameters = credParams.size();
	credParamsList.pCredentialParameters = credParams.data();

	auto clientDataJson = Data::Passkey::SerializeClientDataCreate(
		data.challenge);
	auto clientData = WEBAUTHN_CLIENT_DATA();
	memset(&clientData, 0, sizeof(clientData));
	clientData.dwVersion = WEBAUTHN_CLIENT_DATA_CURRENT_VERSION;
	clientData.cbClientDataJSON = clientDataJson.size();
	clientData.pbClientDataJSON = (PBYTE)clientDataJson.data();
	clientData.pwszHashAlgId = WEBAUTHN_HASH_ALGORITHM_SHA_256;

	auto cancellationId = GUID();
	CoCreateGuid(&cancellationId);

	auto options = WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS();
	memset(&options, 0, sizeof(options));
	options.dwVersion =
		WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_CURRENT_VERSION;
	options.dwTimeoutMilliseconds = data.timeout;
	options.dwAuthenticatorAttachment =
		WEBAUTHN_AUTHENTICATOR_ATTACHMENT_ANY;
	options.bRequireResidentKey = FALSE;
	options.dwUserVerificationRequirement =
		WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED;
	options.dwAttestationConveyancePreference =
		WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_NONE;
	options.pCancellationId = &cancellationId;

#if defined(WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_4) \
	|| defined(WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_5) \
	|| defined(WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_6) \
	|| defined(WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_7) \
	|| defined(WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_8) \
	|| defined(WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_9)
	options.bPreferResidentKey = TRUE;
#endif

	const auto hwnd = NativeParentWindow();

	auto attestation = (PWEBAUTHN_CREDENTIAL_ATTESTATION)(nullptr);
	auto hr = (HRESULT)(WebAuthNAuthenticatorMakeCredential)(
		hwnd,
		&rpInfo,
		&userInfo,
		&credParamsList,
		&clientData,
		&options,
		&attestation);

	if (SUCCEEDED(hr) && attestation) {
		auto result = RegisterResult();
		result.success = true;
		result.credentialId = QByteArray(
			(char*)attestation->pbCredentialId,
			attestation->cbCredentialId);
		result.attestationObject = QByteArray(
			(char*)attestation->pbAttestationObject,
			attestation->cbAttestationObject);
		result.clientDataJSON = QByteArray::fromStdString(clientDataJson);
		WebAuthNFreeCredentialAttestation(attestation);
		callback(result);
	} else {
		auto result = RegisterResult();
		if (NativeCancelled(hr)) {
			result.error = Error::Cancelled;
		} else {
			LOG(("Passkey Error: MakeCredential failed, hr 0x%1."
				).arg(ulong(hr), 0, 16));
			result.error = Error::Other;
			ShowNativeError(tr::lng_passkey_error_register(tr::now));
		}
		callback(result);
	}
}

void LoginViaNative(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult result)> callback) {
	if (!Supported()) {
		callback({});
		return;
	}

	auto rpId = data.rpId.toStdWString();
	auto clientDataJson = Data::Passkey::SerializeClientDataGet(
		data.challenge);

	auto clientData = WEBAUTHN_CLIENT_DATA();
	memset(&clientData, 0, sizeof(clientData));
	clientData.dwVersion = WEBAUTHN_CLIENT_DATA_CURRENT_VERSION;
	clientData.cbClientDataJSON = clientDataJson.size();
	clientData.pbClientDataJSON = (PBYTE)clientDataJson.data();
	clientData.pwszHashAlgId = WEBAUTHN_HASH_ALGORITHM_SHA_256;

	auto allowCredentials = std::vector<WEBAUTHN_CREDENTIAL>();
	auto credentialIds = std::vector<QByteArray>();
	for (const auto &cred : data.allowCredentials) {
		credentialIds.push_back(cred.id);
		auto credential = WEBAUTHN_CREDENTIAL();
		memset(&credential, 0, sizeof(credential));
		credential.dwVersion = WEBAUTHN_CREDENTIAL_CURRENT_VERSION;
		credential.cbId = cred.id.size();
		credential.pbId = (PBYTE)credentialIds.back().data();
		credential.pwszCredentialType = WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY;
		allowCredentials.push_back(credential);
	}

	auto cancellationId = GUID();
	CoCreateGuid(&cancellationId);

	auto options = WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS();
	memset(&options, 0, sizeof(options));
	options.dwVersion =
		WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS_CURRENT_VERSION;
	options.dwTimeoutMilliseconds = data.timeout;
	if (!allowCredentials.empty()) {
		options.CredentialList.cCredentials = allowCredentials.size();
		options.CredentialList.pCredentials = allowCredentials.data();
	}
	options.pCancellationId = &cancellationId;
	if (data.userVerification == "required") {
		options.dwUserVerificationRequirement =
			WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED;
	} else if (data.userVerification == "preferred") {
		options.dwUserVerificationRequirement =
			WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED;
	} else {
		options.dwUserVerificationRequirement =
			WEBAUTHN_USER_VERIFICATION_REQUIREMENT_DISCOURAGED;
	}

	const auto hwnd = NativeParentWindow();

	auto assertion = (PWEBAUTHN_ASSERTION)(nullptr);
	auto hr = (HRESULT)(WebAuthNAuthenticatorGetAssertion)(
		hwnd,
		rpId.c_str(),
		&clientData,
		&options,
		&assertion);

	if (SUCCEEDED(hr) && assertion) {
		auto result = LoginResult();
		result.clientDataJSON = QByteArray::fromStdString(clientDataJson);
		result.credentialId = QByteArray(
			(char*)assertion->Credential.pbId,
			assertion->Credential.cbId);
		result.authenticatorData = QByteArray(
			(char*)assertion->pbAuthenticatorData,
			assertion->cbAuthenticatorData);
		result.signature = QByteArray(
			(char*)assertion->pbSignature,
			assertion->cbSignature);
		result.userHandle = assertion->cbUserId > 0
			? QByteArray((char*)assertion->pbUserId, assertion->cbUserId)
			: QByteArray();
		WebAuthNFreeAssertion(assertion);
		callback(result);
	} else {
		auto result = LoginResult();
		if (NativeCancelled(hr)) {
			result.error = Error::Cancelled;
		} else {
			LOG(("Passkey Error: GetAssertion failed, hr 0x%1."
				).arg(ulong(hr), 0, 16));
			result.error = Error::Other;
			ShowNativeError(tr::lng_passkey_error_login(tr::now));
		}
		callback(result);
	}
}

} // namespace

bool IsSupported() {
	return true;
}

bool SecurityKeyPresent() {
	return Supported() || Libfido2DevicePresent();
}

void RegisterViaSecurityKey(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult)> callback) {
	if (Supported()) {
		RegisterViaNative(data, std::move(callback));
	} else {
		RegisterViaLibfido2(data, std::move(callback));
	}
}

void LoginViaSecurityKey(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult)> callback) {
	if (Supported()) {
		LoginViaNative(data, std::move(callback));
	} else {
		LoginViaLibfido2(data, std::move(callback));
	}
}

void RegisterKey(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult result)> callback) {
	if (NativeAuthenticatorAvailable()) {
		RegisterViaNative(data, std::move(callback));
	} else {
		RegisterViaCable(data, std::move(callback));
	}
}

void Login(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult result)> callback) {
	if (NativeAuthenticatorAvailable()) {
		LoginViaNative(data, std::move(callback));
	} else {
		LoginViaCable(data, std::move(callback));
	}
}

} // namespace Platform::WebAuthn
