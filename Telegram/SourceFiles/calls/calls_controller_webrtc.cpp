/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_controller_webrtc.h"

#include "webrtc/webrtc_call_context.h"

namespace Calls {
namespace {

using namespace Webrtc;

[[nodiscard]] CallConnectionDescription ConvertEndpoint(const TgVoipEndpoint &data) {
	return CallConnectionDescription{
		.ip = QString::fromStdString(data.host.ipv4),
		.ipv6 = QString::fromStdString(data.host.ipv6),
		.peerTag = QByteArray(
			reinterpret_cast<const char*>(data.peerTag),
			base::array_size(data.peerTag)),
		.connectionId = data.endpointId,
		.port = data.port,
	};
}

[[nodiscard]] CallContext::Config MakeContextConfig(
		const TgVoipConfig &config,
		const TgVoipPersistentState &persistentState,
		const std::vector<TgVoipEndpoint> &endpoints,
		const TgVoipProxy *proxy,
		TgVoipNetworkType initialNetworkType,
		const TgVoipEncryptionKey &encryptionKey,
		Fn<void(QByteArray)> sendSignalingData,
		Fn<void(QImage)> displayNextFrame) {
	Expects(!endpoints.empty());

	auto result = CallContext::Config{
		.proxy = (proxy
			? ProxyServer{
				.host = QString::fromStdString(proxy->host),
				.username = QString::fromStdString(proxy->login),
				.password = QString::fromStdString(proxy->password),
				.port = proxy->port }
			: ProxyServer()),
		.dataSaving = (config.dataSaving != TgVoipDataSaving::Never),
		.key = QByteArray(
			reinterpret_cast<const char*>(encryptionKey.value.data()),
			encryptionKey.value.size()),
		.outgoing = encryptionKey.isOutgoing,
		.primary = ConvertEndpoint(endpoints.front()),
		.alternatives = endpoints | ranges::views::drop(
			1
		) | ranges::views::transform(ConvertEndpoint) | ranges::to_vector,
		.maxLayer = config.maxApiLayer,
		.allowP2P = config.enableP2P,
		.sendSignalingData = std::move(sendSignalingData),
		.displayNextFrame = std::move(displayNextFrame),
	};
	return result;
}

} // namespace

WebrtcController::WebrtcController(
	const TgVoipConfig &config,
	const TgVoipPersistentState &persistentState,
	const std::vector<TgVoipEndpoint> &endpoints,
	const TgVoipProxy *proxy,
	TgVoipNetworkType initialNetworkType,
	const TgVoipEncryptionKey &encryptionKey,
	Fn<void(QByteArray)> sendSignalingData,
	Fn<void(QImage)> displayNextFrame)
: _impl(std::make_unique<CallContext>(MakeContextConfig(
		config,
		persistentState,
		endpoints,
		proxy,
		initialNetworkType,
		encryptionKey,
		std::move(sendSignalingData),
		std::move(displayNextFrame)))) {
}

WebrtcController::~WebrtcController() = default;

std::string WebrtcController::Version() {
	return CallContext::Version().toStdString();
}

std::string WebrtcController::version() {
	return Version();
}

void WebrtcController::setNetworkType(TgVoipNetworkType networkType) {
}

void WebrtcController::setMuteMicrophone(bool muteMicrophone) {
	_impl->setIsMuted(muteMicrophone);
}

void WebrtcController::setAudioOutputGainControlEnabled(bool enabled) {
}

void WebrtcController::setEchoCancellationStrength(int strength) {
}

void WebrtcController::setAudioInputDevice(std::string id) {
}

void WebrtcController::setAudioOutputDevice(std::string id) {
}

void WebrtcController::setInputVolume(float level) {
}

void WebrtcController::setOutputVolume(float level) {
}

void WebrtcController::setAudioOutputDuckingEnabled(bool enabled) {
}

bool WebrtcController::receiveSignalingData(const QByteArray &data) {
	return _impl->receiveSignalingData(data);
}

std::string WebrtcController::getLastError() {
	return {};
}

std::string WebrtcController::getDebugInfo() {
	return _impl->getDebugInfo().toStdString();
}

int64_t WebrtcController::getPreferredRelayId() {
	return 0;
}

TgVoipTrafficStats WebrtcController::getTrafficStats() {
	return {};
}

TgVoipPersistentState WebrtcController::getPersistentState() {
	return TgVoipPersistentState{};
}

void WebrtcController::setOnStateUpdated(
		Fn<void(TgVoipState)> onStateUpdated) {
	_stateUpdatedLifetime.destroy();
	_impl->state().changes(
	) | rpl::start_with_next([=](CallState state) {
		onStateUpdated([&] {
			switch (state) {
			case CallState::Initializing: return TgVoipState::WaitInit;
			case CallState::Reconnecting: return TgVoipState::Reconnecting;
			case CallState::Connected: return TgVoipState::Established;
			case CallState::Failed: return TgVoipState::Failed;
			}
			Unexpected("State value in Webrtc::CallContext::state.");
		}());
	}, _stateUpdatedLifetime);
}

void WebrtcController::setOnSignalBarsUpdated(
	Fn<void(int)> onSignalBarsUpdated) {
}

TgVoipFinalState WebrtcController::stop() {
	_impl->stop();
	return TgVoipFinalState();
}

} // namespace Calls
