/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include "platform/platform_webauthn.h"

#if 0

#include "data/data_passkey_deserialize.h"
#include "base/options.h"

#import <AuthenticationServices/AuthenticationServices.h>
#import <Foundation/Foundation.h>

@interface WebAuthnDelegate : NSObject<
	ASAuthorizationControllerDelegate,
	ASAuthorizationControllerPresentationContextProviding>
@property (nonatomic, copy) void (^completionRegister)(
	const Platform::WebAuthn::RegisterResult&);
@property (nonatomic, copy) void (^completionLogin)(
	const Platform::WebAuthn::LoginResult&);
@property (nonatomic, strong) ASAuthorizationController *controller API_AVAILABLE(macos(10.15));
@property (nonatomic, strong)
	ASAuthorizationPlatformPublicKeyCredentialProvider *provider API_AVAILABLE(macos(12.0));
@end

@implementation WebAuthnDelegate

- (void)authorizationController:(ASAuthorizationController *)controller
	didCompleteWithAuthorization:(ASAuthorization *)authorization
		API_AVAILABLE(macos(12.0)) {
	if (self.completionRegister) {
		if ([authorization.credential conformsToProtocol:
				@protocol(ASAuthorizationPublicKeyCredentialRegistration)]) {
			auto credential
				= (id<ASAuthorizationPublicKeyCredentialRegistration>)
					authorization.credential;
			auto result = Platform::WebAuthn::RegisterResult();
			result.success = true;
			result.credentialId = QByteArray::fromNSData(
				credential.credentialID);
			result.attestationObject = QByteArray::fromNSData(
				credential.rawAttestationObject);
			result.clientDataJSON = QByteArray::fromNSData(
				credential.rawClientDataJSON);
			self.completionRegister(result);
			self.completionRegister = nil;
		}
		self.controller = nil;
		if (@available(macOS 12.0, *)) {
			self.provider = nil;
		}
	} else if (self.completionLogin) {
		if ([authorization.credential conformsToProtocol:
				@protocol(ASAuthorizationPublicKeyCredentialAssertion)]) {
			auto credential
				= (id<ASAuthorizationPublicKeyCredentialAssertion>)
					authorization.credential;
			auto result = Platform::WebAuthn::LoginResult();
			result.credentialId = QByteArray::fromNSData(
				credential.credentialID);
			result.authenticatorData = QByteArray::fromNSData(
				credential.rawAuthenticatorData);
			result.signature = QByteArray::fromNSData(
				credential.signature);
			result.clientDataJSON = QByteArray::fromNSData(
				credential.rawClientDataJSON);
			result.userHandle = QByteArray::fromNSData(credential.userID);
			self.completionLogin(result);
			self.completionLogin = nil;
		}
		self.controller = nil;
		if (@available(macOS 12.0, *)) {
			self.provider = nil;
		}
	}
}

- (void)authorizationController:(ASAuthorizationController *)controller
			didCompleteWithError:(NSError *)error
		API_AVAILABLE(macos(10.15)) {
	const auto isCancelled = (error.code == ASAuthorizationErrorCanceled);
	const auto isUnsigned = (error.code == 1004 || error.code == 1009);
	if (!isCancelled) {
		NSLog(@"WebAuthn error: %@ (code: %ld)",
			error.localizedDescription, (long)error.code);
	}
	if (self.completionRegister) {
		auto result = Platform::WebAuthn::RegisterResult();
		result.success = false;
		result.error = isUnsigned
			? Platform::WebAuthn::Error::UnsignedBuild
			: (isCancelled
				? Platform::WebAuthn::Error::Cancelled
				: Platform::WebAuthn::Error::Other);
		self.completionRegister(result);
		self.completionRegister = nil;
	} else if (self.completionLogin) {
		auto result = Platform::WebAuthn::LoginResult();
		result.error = isUnsigned
			? Platform::WebAuthn::Error::UnsignedBuild
			: (isCancelled
				? Platform::WebAuthn::Error::Cancelled
				: Platform::WebAuthn::Error::Other);
		self.completionLogin(result);
		self.completionLogin = nil;
	}
	self.controller = nil;
	if (@available(macOS 12.0, *)) {
		self.provider = nil;
	}
}

- (ASPresentationAnchor)presentationAnchorForAuthorizationController:
		(ASAuthorizationController *)controller
		API_AVAILABLE(macos(10.15)) {
	return [NSApp mainWindow];
}

@end

namespace {

base::options::toggle WebAuthnMacOption({
	.id = "webauthn-mac",
	.name = "Enable Passkey on macOS",
	.description = "Enable Passkey support on macOS 12.0+. Experimental feature that may cause crash.",
	.defaultValue = false,
	.scope = base::options::macos,
});

} // namespace

namespace Platform::WebAuthn {

bool IsSupported() {
	if (!WebAuthnMacOption.value()) {
		return false;
	}
	if (@available(macOS 12.0, *)) {
		return true;
	}
	return false;
}

void RegisterKey(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult result)> callback) {
	if (@available(macOS 12.0, *)) {
		auto rpId = data.rp.id.toNSString();
		auto userName = data.user.name.toNSString();
		auto userDisplayName = data.user.displayName.toNSString();
		auto userId = [NSData dataWithBytes:data.user.id.constData()
			length:data.user.id.size()];
		auto challenge = [NSData dataWithBytes:data.challenge.constData()
			length:data.challenge.size()];

		if (!userId || !challenge) {
			auto result = RegisterResult();
			result.success = false;
			callback(result);
			return;
		}

		auto provider = [[ASAuthorizationPlatformPublicKeyCredentialProvider
			alloc] initWithRelyingPartyIdentifier:rpId];

		auto request = [provider
			createCredentialRegistrationRequestWithChallenge:challenge
			name:userName
			userID:userId];

		if (userDisplayName && userDisplayName.length > 0) {
			request.displayName = userDisplayName;
		}

		if (@available(macOS 13.5, *)) {
			request.attestationPreference =
				ASAuthorizationPublicKeyCredentialAttestationKindNone;
		}

		auto controller = [[ASAuthorizationController alloc]
			initWithAuthorizationRequests:@[request]];

		auto delegate = [[WebAuthnDelegate alloc] init];
		if (@available(macOS 12.0, *)) {
			delegate.provider = provider;
		}
		delegate.controller = controller;
		delegate.completionRegister = ^(const RegisterResult &result) {
			callback(result);
			[delegate release];
		};

		controller.delegate = delegate;
		controller.presentationContextProvider = delegate;
		[controller performRequests];
	} else {
		auto result = RegisterResult();
		result.success = false;
		callback(result);
	}
}

void Login(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult result)> callback) {
	if (@available(macOS 12.0, *)) {
		auto challenge = [NSData dataWithBytes:data.challenge.constData()
			length:data.challenge.size()];

		if (!challenge) {
			auto result = LoginResult();
			callback(result);
			return;
		}

		auto provider = [[ASAuthorizationPlatformPublicKeyCredentialProvider
			alloc] initWithRelyingPartyIdentifier:data.rpId.toNSString()];

		auto request = [provider
			createCredentialAssertionRequestWithChallenge:challenge];

		if (!data.allowCredentials.empty()) {
			NSMutableArray *credentialIds = [NSMutableArray array];
			for (const auto &cred : data.allowCredentials) {
				auto credId = [NSData dataWithBytes:cred.id.constData()
					length:cred.id.size()];
				if (credId) {
					[credentialIds addObject:credId];
				}
			}
			if (credentialIds.count > 0) {
				request.allowedCredentials = credentialIds;
			}
		}

		if (data.userVerification == "required") {
			request.userVerificationPreference =
				ASAuthorizationPublicKeyCredentialUserVerificationPreferenceRequired;
		} else if (data.userVerification == "preferred") {
			request.userVerificationPreference =
				ASAuthorizationPublicKeyCredentialUserVerificationPreferencePreferred;
		} else if (data.userVerification == "discouraged") {
			request.userVerificationPreference =
				ASAuthorizationPublicKeyCredentialUserVerificationPreferenceDiscouraged;
		}

		auto controller = [[ASAuthorizationController alloc]
			initWithAuthorizationRequests:@[request]];

		auto delegate = [[WebAuthnDelegate alloc] init];
		if (@available(macOS 12.0, *)) {
			delegate.provider = provider;
		}
		delegate.controller = controller;
		delegate.completionLogin = ^(const LoginResult &result) {
			callback(result);
			[delegate release];
		};

		controller.delegate = delegate;
		controller.presentationContextProvider = delegate;
		[controller performRequests];
	} else {
		auto result = LoginResult();
		callback(result);
	}
}

} // namespace Platform::WebAuthn

#endif

namespace Platform::WebAuthn {


bool IsSupported() {
	return false;
}


void RegisterKey(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult result)> callback) {
}

void Login(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult result)> callback) {
}

} // namespace Platform::WebAuthn
