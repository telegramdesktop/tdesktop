/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "calls/calls_instance.h"

#include "mtproto/connection.h"
#include "messenger.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "calls/calls_call.h"
#include "calls/calls_panel.h"
#include "media/media_audio_track.h"

#include "boxes/rate_call_box.h"
namespace Calls {
namespace {

constexpr auto kServerConfigUpdateTimeoutMs = 24 * 3600 * TimeMs(1000);

} // namespace

Instance::Instance() = default;

void Instance::startOutgoingCall(not_null<UserData*> user) {
	if (alreadyInCall()) { // Already in a call.
		_currentCallPanel->showAndActivate();
		return;
	}
	if (user->callsStatus() == UserData::CallsStatus::Private) {
		// Request full user once more to refresh the setting in case it was changed.
		Auth().api().requestFullPeer(user);
		Ui::show(Box<InformBox>(lng_call_error_not_available(lt_user, App::peerName(user))));
		return;
	}
	createCall(user, Call::Type::Outgoing);
}

void Instance::callFinished(not_null<Call*> call) {
	destroyCall(call);
}

void Instance::callFailed(not_null<Call*> call) {
	destroyCall(call);
}

void Instance::callRedial(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		refreshDhConfig();
	}
}

void Instance::playSound(Sound sound) {
	switch (sound) {
	case Sound::Busy: {
		if (!_callBusyTrack) {
			_callBusyTrack = Media::Audio::Current().createTrack();
			_callBusyTrack->fillFromFile(Auth().data().getSoundPath(qsl("call_busy")));
		}
		_callBusyTrack->playOnce();
	} break;

	case Sound::Ended: {
		if (!_callEndedTrack) {
			_callEndedTrack = Media::Audio::Current().createTrack();
			_callEndedTrack->fillFromFile(Auth().data().getSoundPath(qsl("call_end")));
		}
		_callEndedTrack->playOnce();
	} break;

	case Sound::Connecting: {
		if (!_callConnectingTrack) {
			_callConnectingTrack = Media::Audio::Current().createTrack();
			_callConnectingTrack->fillFromFile(Auth().data().getSoundPath(qsl("call_connect")));
		}
		_callConnectingTrack->playOnce();
	} break;
	}
}

void Instance::destroyCall(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		destroyCurrentPanel();
		_currentCall.reset();
		_currentCallChanged.notify(nullptr, true);

		if (App::quitting()) {
			LOG(("Calls::Instance doesn't prevent quit any more."));
		}
		Messenger::Instance().quitPreventFinished();
	}
}

void Instance::destroyCurrentPanel() {
	_pendingPanels.erase(std::remove_if(_pendingPanels.begin(), _pendingPanels.end(), [](auto &&panel) {
		return !panel;
	}), _pendingPanels.end());
	_pendingPanels.push_back(_currentCallPanel.release());
	_pendingPanels.back()->hideAndDestroy(); // Always queues the destruction.
}

void Instance::createCall(not_null<UserData*> user, Call::Type type) {
	auto call = std::make_unique<Call>(getCallDelegate(), user, type);;
	if (_currentCall) {
		_currentCallPanel->replaceCall(call.get());
		std::swap(_currentCall, call);
		call->hangup();
	} else {
		_currentCallPanel = std::make_unique<Panel>(call.get());
		_currentCall = std::move(call);
	}
	_currentCallChanged.notify(_currentCall.get(), true);
	refreshServerConfig();
	refreshDhConfig();
}

void Instance::refreshDhConfig() {
	Expects(_currentCall != nullptr);
	request(MTPmessages_GetDhConfig(MTP_int(_dhConfig.version), MTP_int(Call::kRandomPowerSize))).done([this, call = base::make_weak_unique(_currentCall)](const MTPmessages_DhConfig &result) {
		auto random = base::const_byte_span();
		switch (result.type()) {
		case mtpc_messages_dhConfig: {
			auto &config = result.c_messages_dhConfig();
			if (!MTP::IsPrimeAndGood(bytesFromMTP(config.vp), config.vg.v)) {
				LOG(("API Error: bad p/g received in dhConfig."));
				callFailed(call.get());
				return;
			}
			_dhConfig.g = config.vg.v;
			_dhConfig.p = byteVectorFromMTP(config.vp);
			random = bytesFromMTP(config.vrandom);
		} break;

		case mtpc_messages_dhConfigNotModified: {
			auto &config = result.c_messages_dhConfigNotModified();
			random = bytesFromMTP(config.vrandom);
			if (!_dhConfig.g || _dhConfig.p.empty()) {
				LOG(("API Error: dhConfigNotModified on zero version."));
				callFailed(call.get());
				return;
			}
		} break;

		default: Unexpected("Type in messages.getDhConfig");
		}

		if (random.size() != Call::kRandomPowerSize) {
			LOG(("API Error: dhConfig random bytes wrong size: %1").arg(random.size()));
			callFailed(call.get());
			return;
		}
		if (call) {
			call->start(random);
		}
	}).fail([this, call = base::make_weak_unique(_currentCall)](const RPCError &error) {
		if (!call) {
			DEBUG_LOG(("API Warning: call was destroyed before got dhConfig."));
			return;
		}
		callFailed(call.get());
	}).send();
}

void Instance::refreshServerConfig() {
	if (_serverConfigRequestId) {
		return;
	}
	if (_lastServerConfigUpdateTime && (getms(true) - _lastServerConfigUpdateTime) < kServerConfigUpdateTimeoutMs) {
		return;
	}
	_serverConfigRequestId = request(MTPphone_GetCallConfig()).done([this](const MTPDataJSON &result) {
		_serverConfigRequestId = 0;
		_lastServerConfigUpdateTime = getms(true);

		auto configUpdate = std::map<std::string, std::string>();
		auto bytes = bytesFromMTP(result.c_dataJSON().vdata);
		auto error = QJsonParseError { 0, QJsonParseError::NoError };
		auto document = QJsonDocument::fromJson(QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &error);
		if (error.error != QJsonParseError::NoError) {
			LOG(("API Error: Failed to parse call config JSON, error: %1").arg(error.errorString()));
			return;
		} else if (!document.isObject()) {
			LOG(("API Error: Not an object received in call config JSON."));
			return;
		}

		auto parseValue = [](QJsonValueRef data) -> std::string {
			switch (data.type()) {
			case QJsonValue::String: return data.toString().toStdString();
			case QJsonValue::Double: return QString::number(data.toDouble(), 'f').toStdString();
			case QJsonValue::Bool: return data.toBool() ? "true" : "false";
			case QJsonValue::Null: {
				LOG(("API Warning: null field in call config JSON."));
			} return "null";
			case QJsonValue::Undefined: {
				LOG(("API Warning: undefined field in call config JSON."));
			} return "undefined";
			case QJsonValue::Object:
			case QJsonValue::Array: {
				LOG(("API Warning: complex field in call config JSON."));
				QJsonDocument serializer;
				if (data.isArray()) {
					serializer.setArray(data.toArray());
				} else {
					serializer.setObject(data.toObject());
				}
				auto byteArray = serializer.toJson(QJsonDocument::Compact);
				return std::string(byteArray.constData(), byteArray.size());
			} break;
			}
			Unexpected("Type in Json parse.");
		};

		auto object = document.object();
		for (auto i = object.begin(), e = object.end(); i != e; ++i) {
			auto key = i.key().toStdString();
			auto value = parseValue(i.value());
			configUpdate[key] = value;
		}

		UpdateConfig(configUpdate);
	}).fail([this](const RPCError &error) {
		_serverConfigRequestId = 0;
	}).send();
}

void Instance::handleUpdate(const MTPDupdatePhoneCall& update) {
	handleCallUpdate(update.vphone_call);
}

void Instance::showInfoPanel(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		_currentCallPanel->showAndActivate();
	}
}

bool Instance::isQuitPrevent() {
	if (!_currentCall || _currentCall->isIncomingWaiting()) {
		return false;
	}
	_currentCall->hangup();
	if (!_currentCall) {
		return false;
	}
	LOG(("Calls::Instance prevents quit, saving drafts..."));
	return true;
}

void Instance::handleCallUpdate(const MTPPhoneCall &call) {
	if (call.type() == mtpc_phoneCallRequested) {
		auto &phoneCall = call.c_phoneCallRequested();
		auto user = App::userLoaded(phoneCall.vadmin_id.v);
		if (!user) {
			LOG(("API Error: User not loaded for phoneCallRequested."));
		} else if (user->isSelf()) {
			LOG(("API Error: Self found in phoneCallRequested."));
		}
		if (alreadyInCall() || !user || user->isSelf()) {
			request(MTPphone_DiscardCall(MTP_inputPhoneCall(phoneCall.vid, phoneCall.vaccess_hash), MTP_int(0), MTP_phoneCallDiscardReasonBusy(), MTP_long(0))).send();
		} else if (phoneCall.vdate.v + (Global::CallRingTimeoutMs() / 1000) < unixtime()) {
			LOG(("Ignoring too old call."));
		} else {
			createCall(user, Call::Type::Incoming);
			_currentCall->handleUpdate(call);
		}
	} else if (!_currentCall || !_currentCall->handleUpdate(call)) {
		DEBUG_LOG(("API Warning: unexpected phone call update %1").arg(call.type()));
	}
}

bool Instance::alreadyInCall() {
	return (_currentCall && _currentCall->state() != Call::State::Busy);
}

Instance::~Instance() {
	for (auto panel : _pendingPanels) {
		if (panel) {
			delete panel;
		}
	}
}

Instance &Current() {
	return Auth().calls();
}

} // namespace Calls
