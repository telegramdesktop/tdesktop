/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable.h"

#include "webauthn/cable_box.h"
#include "webauthn/cable_scanner.h"
#include "webauthn/cable_tunnel.h"
#include "webauthn/webauthn_common.h"
#include "lang/lang_keys.h"
#include "base/algorithm.h"
#include "base/timer.h"
#include "base/unixtime.h"

#include <crl/crl.h>

#include <memory>
#include <vector>

namespace Platform::WebAuthn::Cable {
namespace {

constexpr auto kCeremonyTimeoutMs = crl::time(180000);
constexpr auto kBluetoothProbeMs = crl::time(1500);

[[nodiscard]] QByteArray ToByteArray(const Bytes &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.data()),
		int(bytes.size()));
}

[[nodiscard]] Bytes FromByteArray(const QByteArray &bytes) {
	return Bytes(bytes.begin(), bytes.end());
}

struct Ceremony final : std::enable_shared_from_this<Ceremony> {
	bool isRegister = false;
	RegisterRequest registerRequest;
	LoginRequest loginRequest;
	Fn<void(RegisterResult)> registerDone;
	Fn<void(LoginResult)> loginDone;

	QRKey qrKey;
	std::array<uint8_t, kEidKeySize> eidKey = {};
	std::unique_ptr<TunnelSocket> socket;
	std::unique_ptr<BleScanner> scanner;
	std::unique_ptr<HandshakeInitiator> handshake;
	std::unique_ptr<Crypter> crypter;
	int protocolRevision = 1;

	std::shared_ptr<BoxState> ui;
	bool boxShown = false;
	base::Timer timeout;
	base::Timer bluetoothProbe;

	// Keeps the ceremony alive for the duration of the async flow; cleared in
	// Finish() so nothing lingers once we are done.
	std::shared_ptr<Ceremony> keepAlive;

	enum class Step {
		Scanning,
		Connecting,
		Handshake,
		PostHandshake,
		AwaitingReply,
		Finished,
	};
	Step step = Step::Scanning;
	bool finished = false;
	bool bluetoothAvailable = false;
};

void SetSheet(not_null<Ceremony*> ceremony, Sheet sheet) {
	const auto weak = ceremony->weak_from_this();
	crl::on_main([weak, sheet] {
		if (const auto strong = weak.lock()) {
			if (!strong->finished && strong->ui) {
				strong->ui->sheet = sheet;
			}
		}
	});
}

void Finish(std::shared_ptr<Ceremony> ceremony, Outcome outcome);

void FinishSuccessRegister(
		std::shared_ptr<Ceremony> ceremony,
		const MakeCredentialResponse &response) {
	if (ceremony->registerDone) {
		auto result = RegisterResult();
		result.credentialId = response.credentialId;
		result.authData = response.authData;
		result.outcome = Outcome::Success;
		base::take(ceremony->registerDone)(std::move(result));
	}
	Finish(std::move(ceremony), Outcome::Success);
}

void FinishSuccessLogin(
		std::shared_ptr<Ceremony> ceremony,
		const GetAssertionResponse &response) {
	if (ceremony->loginDone) {
		auto result = LoginResult();
		result.credentialId = response.credentialId;
		result.authData = response.authData;
		result.signature = response.signature;
		result.userHandle = response.userHandle;
		result.outcome = Outcome::Success;
		base::take(ceremony->loginDone)(std::move(result));
	}
	Finish(std::move(ceremony), Outcome::Success);
}

void Finish(std::shared_ptr<Ceremony> ceremony, Outcome outcome) {
	if (ceremony->finished) {
		return;
	}
	ceremony->finished = true;
	ceremony->step = Ceremony::Step::Finished;
	ceremony->timeout.cancel();
	if (ceremony->scanner) {
		ceremony->scanner->stop();
	}
	if (ceremony->socket) {
		ceremony->socket->onConnected = nullptr;
		ceremony->socket->onBinary = nullptr;
		ceremony->socket->onClosed = nullptr;
		ceremony->socket->close();
	}
	const auto failed = (outcome == Outcome::Failed)
		|| (outcome == Outcome::NoBluetooth);
	const auto failure = [&] {
		return (outcome == Outcome::NoBluetooth)
			? tr::lng_passkey_cable_no_bluetooth(tr::now)
			: tr::lng_passkey_cable_error(tr::now);
	};
	if (ceremony->boxShown && ceremony->ui) {
		if (failed) {
			const auto ui = ceremony->ui;
			ui->error = failure();
			crl::on_main([ui] { ui->sheet = Sheet::Error; });
		} else {
			ceremony->ui->closeRequests.fire({});
		}
	} else if (failed) {
		ShowCableToast(failure());
	}
	if (ceremony->registerDone) {
		auto result = RegisterResult();
		result.outcome = outcome;
		base::take(ceremony->registerDone)(std::move(result));
	}
	if (ceremony->loginDone) {
		auto result = LoginResult();
		result.outcome = outcome;
		base::take(ceremony->loginDone)(std::move(result));
	}
	crl::on_main([ceremony] {
		ceremony->keepAlive = nullptr;
	});
}

[[nodiscard]] QByteArray BuildCtapRequest(not_null<Ceremony*> ceremony) {
	if (ceremony->isRegister) {
		const auto &request = ceremony->registerRequest;
		return ToByteArray(BuildMakeCredentialRequest(
			request.clientDataHash,
			request.rpId,
			request.rpName,
			request.userId,
			request.userName,
			request.userDisplayName,
			request.algorithms));
	}
	const auto &request = ceremony->loginRequest;
	return ToByteArray(BuildGetAssertionRequest(
		request.rpId,
		request.clientDataHash,
		request.allowCredentialIds));
}

void SendCtapRequest(std::shared_ptr<Ceremony> ceremony) {
	auto plaintext = Bytes();
	if (ceremony->protocolRevision >= 1) {
		plaintext.push_back(uint8_t(MessageType::Ctap));
	}
	const auto request = FromByteArray(BuildCtapRequest(ceremony.get()));
	plaintext.insert(plaintext.end(), request.begin(), request.end());

	auto encrypted = ceremony->crypter->encrypt(plaintext);
	if (!encrypted) {
		Finish(std::move(ceremony), Outcome::Failed);
		return;
	}
	ceremony->step = Ceremony::Step::AwaitingReply;
	SetSheet(ceremony.get(), Sheet::Continue);
	ceremony->socket->sendBinary(ToByteArray(*encrypted));
}

void HandleReply(std::shared_ptr<Ceremony> ceremony, Bytes plaintext) {
	if (ceremony->protocolRevision >= 1) {
		if (plaintext.empty()) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
		const auto type = MessageType(plaintext.front());
		plaintext.erase(plaintext.begin());
		if (type == MessageType::Update) {
			return;
		} else if (type != MessageType::Ctap) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
	}
	if (ceremony->isRegister) {
		const auto parsed = ParseMakeCredentialResponse(plaintext);
		if (!parsed) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
		FinishSuccessRegister(std::move(ceremony), *parsed);
	} else {
		const auto parsed = ParseGetAssertionResponse(plaintext);
		if (!parsed) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
		FinishSuccessLogin(std::move(ceremony), *parsed);
	}
}

void HandleTunnelData(std::shared_ptr<Ceremony> ceremony, QByteArray message) {
	const auto bytes = FromByteArray(message);
	switch (ceremony->step) {
	case Ceremony::Step::Handshake: {
		auto result = ceremony->handshake->processResponse(bytes);
		if (!result) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
		ceremony->crypter = std::move(result->crypter);
		ceremony->handshake.reset();
		ceremony->step = Ceremony::Step::PostHandshake;
	} break;

	case Ceremony::Step::PostHandshake: {
		auto decrypted = ceremony->crypter->decrypt(bytes);
		if (!decrypted) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
		const auto parsed = ParsePostHandshakeMessage(*decrypted);
		if (!parsed || !parsed->supportsCtap) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
		ceremony->protocolRevision = parsed->protocolRevision;
		SendCtapRequest(std::move(ceremony));
	} break;

	case Ceremony::Step::AwaitingReply: {
		auto decrypted = ceremony->crypter->decrypt(bytes);
		if (!decrypted) {
			Finish(std::move(ceremony), Outcome::Failed);
			return;
		}
		HandleReply(std::move(ceremony), std::move(*decrypted));
	} break;

	default:
		break;
	}
}

void OnAdvert(std::shared_ptr<Ceremony> ceremony, QByteArray serviceData) {
	if (ceremony->step != Ceremony::Step::Scanning) {
		return;
	}
	const auto decrypted = DecryptAdvert(
		FromByteArray(serviceData),
		ceremony->eidKey);
	if (!decrypted) {
		return;
	}
	ceremony->step = Ceremony::Step::Connecting;
	ceremony->scanner->stop();

	const auto components = ToEidComponents(*decrypted);
	const auto domain = DecodeTunnelServerDomain(
		components.tunnelServerDomain);
	if (domain.empty()) {
		Finish(std::move(ceremony), Outcome::Failed);
		return;
	}
	const auto secret = ByteSpan(
		ceremony->qrKey.secret.data(),
		ceremony->qrKey.secret.size());
	const auto psk = Derive<kPskSize>(secret, *decrypted, DerivedValueType::Psk);
	const auto tunnelId = Derive<kTunnelIdSize>(
		secret,
		{},
		DerivedValueType::TunnelId);
	const auto path = QString::fromStdString("/cable/connect/"
		+ HexLower(components.routingId)
		+ "/"
		+ HexLower(tunnelId));

	ceremony->handshake = std::make_unique<HandshakeInitiator>(
		psk,
		ceremony->qrKey.identity.get());

	SetSheet(ceremony.get(), Sheet::Connecting);

	ceremony->socket = std::make_unique<TunnelSocket>();
	const auto raw = ceremony->socket.get();
	const auto weak = std::weak_ptr(ceremony);
	raw->onConnected = [weak] {
		const auto strong = weak.lock();
		if (!strong || strong->finished) {
			return;
		}
		const auto initial = strong->handshake->buildInitialMessage();
		if (initial.empty()) {
			Finish(strong, Outcome::Failed);
			return;
		}
		strong->step = Ceremony::Step::Handshake;
		strong->socket->sendBinary(ToByteArray(initial));
	};
	raw->onBinary = [weak](QByteArray message) {
		if (const auto strong = weak.lock()) {
			if (!strong->finished) {
				HandleTunnelData(strong, std::move(message));
			}
		}
	};
	raw->onClosed = [weak] {
		if (const auto strong = weak.lock()) {
			if (!strong->finished) {
				Finish(strong, Outcome::Failed);
			}
		}
	};
	raw->connectToTunnel(QString::fromStdString(domain), path);
}

void ShowBox(std::shared_ptr<Ceremony> ceremony, bool bluetooth) {
	if (ceremony->finished || ceremony->boxShown) {
		return;
	}
	ceremony->bluetoothProbe.cancel();
	ceremony->bluetoothAvailable = bluetooth;

	const auto weak = std::weak_ptr(ceremony);
	ceremony->boxShown = ShowCableBox({
		.state = ceremony->ui,
		.qrText = (bluetooth
			? QString::fromStdString(EncodeQRContents(
				ceremony->qrKey,
				ceremony->isRegister,
				int64_t(base::unixtime::now())))
			: QString()),
		.isRegister = ceremony->isRegister,
		.bluetoothAvailable = bluetooth,
		.securityKeyChosen = [weak] {
			const auto strong = weak.lock();
			if (!strong || strong->finished) {
				return;
			} else if (!SecurityKeyPresent()) {
				ShowCableToast(tr::lng_passkey_error_no_device(tr::now));
				return;
			}
			Finish(strong, Outcome::SecurityKey);
		},
		.cancelled = [weak] {
			const auto strong = weak.lock();
			if (strong && !strong->finished) {
				Finish(strong, Outcome::Cancelled);
			}
		},
	});
	if (!ceremony->boxShown) {
		Finish(std::move(ceremony), Outcome::Failed);
	}
}

void Start(std::shared_ptr<Ceremony> ceremony) {
	ceremony->ui = std::make_shared<BoxState>();
	ceremony->qrKey = MakeQRKey();
	if (!ceremony->qrKey.identity) {
		Finish(std::move(ceremony), Outcome::Failed);
		return;
	}
	const auto secret = ByteSpan(
		ceremony->qrKey.secret.data(),
		ceremony->qrKey.secret.size());
	ceremony->eidKey = Derive<kEidKeySize>(
		secret,
		{},
		DerivedValueType::EidKey);

	const auto weak = std::weak_ptr(ceremony);
	ceremony->scanner = MakeBleScanner();
	const auto started = ceremony->scanner
		&& ceremony->scanner->start([weak](QByteArray advert) {
			crl::on_main([weak, advert = std::move(advert)]() mutable {
				if (const auto strong = weak.lock()) {
					OnAdvert(strong, std::move(advert));
				}
			});
		}, [weak](bool available) {
			crl::on_main([weak, available] {
				if (const auto strong = weak.lock()) {
					ShowBox(strong, available);
				}
			});
		});

	ceremony->timeout.setCallback([weak] {
		if (const auto strong = weak.lock()) {
			if (!strong->finished) {
				Finish(strong, Outcome::Failed);
			}
		}
	});
	const auto requested = crl::time(ceremony->isRegister
		? ceremony->registerRequest.timeoutMs
		: ceremony->loginRequest.timeoutMs);
	ceremony->timeout.callOnce((requested > 0)
		? requested
		: kCeremonyTimeoutMs);

	if (!started) {
		ShowBox(std::move(ceremony), false);
		return;
	}
	ceremony->bluetoothProbe.setCallback([weak] {
		if (const auto strong = weak.lock()) {
			ShowBox(strong, false);
		}
	});
	ceremony->bluetoothProbe.callOnce(kBluetoothProbeMs);
}

} // namespace

void Register(RegisterRequest request, Fn<void(RegisterResult)> done) {
	auto ceremony = std::make_shared<Ceremony>();
	ceremony->isRegister = true;
	ceremony->registerRequest = std::move(request);
	ceremony->registerDone = std::move(done);
	ceremony->keepAlive = ceremony;
	Start(std::move(ceremony));
}

void Login(LoginRequest request, Fn<void(LoginResult)> done) {
	auto ceremony = std::make_shared<Ceremony>();
	ceremony->isRegister = false;
	ceremony->loginRequest = std::move(request);
	ceremony->loginDone = std::move(done);
	ceremony->keepAlive = ceremony;
	Start(std::move(ceremony));
}

} // namespace Platform::WebAuthn::Cable
