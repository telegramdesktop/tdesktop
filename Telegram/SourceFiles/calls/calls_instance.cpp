/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_instance.h"

#include "calls/calls_call.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_choose_join_as.h"
#include "calls/group/calls_group_call.h"
#include "mtproto/mtproto_dh_utils.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_panel.h"
#include "calls/calls_call.h"
#include "calls/calls_panel.h"
#include "data/data_user.h"
#include "data/data_group_call.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "media/audio/media_audio_track.h"
#include "platform/platform_specific.h"
#include "ui/toast/toast.h"
#include "base/unixtime.h"
#include "mainwidget.h"
#include "mtproto/mtproto_config.h"
#include "boxes/rate_call_box.h"
#include "app.h"

#include <tgcalls/VideoCaptureInterface.h>
#include <tgcalls/StaticThreads.h>

namespace Calls {
namespace {

constexpr auto kServerConfigUpdateTimeoutMs = 24 * 3600 * crl::time(1000);

using CallSound = Call::Delegate::CallSound;
using GroupCallSound = GroupCall::Delegate::GroupCallSound;

} // namespace

class Instance::Delegate final
	: public Call::Delegate
	, public GroupCall::Delegate {
public:
	explicit Delegate(not_null<Instance*> instance);

	DhConfig getDhConfig() const override;

	void callFinished(not_null<Call*> call) override;
	void callFailed(not_null<Call*> call) override;
	void callRedial(not_null<Call*> call) override;
	void callRequestPermissionsOrFail(
		Fn<void()> onSuccess,
		bool video) override;
	void callPlaySound(CallSound sound) override;
	auto callGetVideoCapture()
		-> std::shared_ptr<tgcalls::VideoCaptureInterface> override;

	void groupCallFinished(not_null<GroupCall*> call) override;
	void groupCallFailed(not_null<GroupCall*> call) override;
	void groupCallRequestPermissionsOrFail(Fn<void()> onSuccess) override;
	void groupCallPlaySound(GroupCallSound sound) override;
	auto groupCallGetVideoCapture(const QString &deviceId)
		-> std::shared_ptr<tgcalls::VideoCaptureInterface> override;
	FnMut<void()> groupCallAddAsyncWaiter() override;

private:
	const not_null<Instance*> _instance;

};

Instance::Delegate::Delegate(not_null<Instance*> instance)
: _instance(instance) {
}

DhConfig Instance::Delegate::getDhConfig() const {
	return *_instance->_cachedDhConfig;
}

void Instance::Delegate::callFinished(not_null<Call*> call) {
	crl::on_main(call, [=] {
		_instance->destroyCall(call);
	});
}

void Instance::Delegate::callFailed(not_null<Call*> call) {
	crl::on_main(call, [=] {
		_instance->destroyCall(call);
	});
}

void Instance::Delegate::callRedial(not_null<Call*> call) {
	if (_instance->_currentCall.get() == call) {
		_instance->refreshDhConfig();
	}
}

void Instance::Delegate::callRequestPermissionsOrFail(
		Fn<void()> onSuccess,
		bool video) {
	_instance->requestPermissionsOrFail(std::move(onSuccess), video);
}

void Instance::Delegate::callPlaySound(CallSound sound) {
	_instance->playSoundOnce([&] {
		switch (sound) {
		case CallSound::Busy: return "call_busy";
		case CallSound::Ended: return "call_end";
		case CallSound::Connecting: return "call_connect";
		}
		Unexpected("CallSound in Instance::callPlaySound.");
	}());
}

auto Instance::Delegate::callGetVideoCapture()
-> std::shared_ptr<tgcalls::VideoCaptureInterface> {
	return _instance->getVideoCapture();
}

void Instance::Delegate::groupCallFinished(not_null<GroupCall*> call) {
	crl::on_main(call, [=] {
		_instance->destroyGroupCall(call);
	});
}

void Instance::Delegate::groupCallFailed(not_null<GroupCall*> call) {
	crl::on_main(call, [=] {
		_instance->destroyGroupCall(call);
	});
}

void Instance::Delegate::groupCallRequestPermissionsOrFail(
		Fn<void()> onSuccess) {
	_instance->requestPermissionsOrFail(std::move(onSuccess), false);
}

void Instance::Delegate::groupCallPlaySound(GroupCallSound sound) {
	_instance->playSoundOnce([&] {
		switch (sound) {
		case GroupCallSound::Started: return "group_call_start";
		case GroupCallSound::Ended: return "group_call_end";
		case GroupCallSound::AllowedToSpeak: return "group_call_allowed";
		case GroupCallSound::Connecting: return "group_call_connect";
		}
		Unexpected("GroupCallSound in Instance::groupCallPlaySound.");
	}());
}

auto Instance::Delegate::groupCallGetVideoCapture(const QString &deviceId)
-> std::shared_ptr<tgcalls::VideoCaptureInterface> {
	return _instance->getVideoCapture(deviceId);
}

FnMut<void()> Instance::Delegate::groupCallAddAsyncWaiter() {
	return _instance->addAsyncWaiter();
}

Instance::Instance()
: _delegate(std::make_unique<Delegate>(this))
, _cachedDhConfig(std::make_unique<DhConfig>())
, _chooseJoinAs(std::make_unique<Group::ChooseJoinAsProcess>()) {
}

Instance::~Instance() {
	destroyCurrentCall();

	while (!_asyncWaiters.empty()) {
		_asyncWaiters.front()->acquire();
		_asyncWaiters.erase(_asyncWaiters.begin());
	}
}

void Instance::startOutgoingCall(not_null<UserData*> user, bool video) {
	if (activateCurrentCall()) {
		return;
	}
	if (user->callsStatus() == UserData::CallsStatus::Private) {
		// Request full user once more to refresh the setting in case it was changed.
		user->session().api().requestFullPeer(user);
		Ui::show(Box<InformBox>(
			tr::lng_call_error_not_available(tr::now, lt_user, user->name)));
		return;
	}
	requestPermissionsOrFail(crl::guard(this, [=] {
		createCall(user, Call::Type::Outgoing, video);
	}), video);
}

void Instance::startOrJoinGroupCall(
		not_null<PeerData*> peer,
		const QString &joinHash,
		bool confirmNeeded) {
	const auto context = confirmNeeded
		? Group::ChooseJoinAsProcess::Context::JoinWithConfirm
		: peer->groupCall()
		? Group::ChooseJoinAsProcess::Context::Join
		: Group::ChooseJoinAsProcess::Context::Create;
	_chooseJoinAs->start(peer, context, [=](object_ptr<Ui::BoxContent> box) {
		Ui::show(std::move(box), Ui::LayerOption::KeepOther);
	}, [=](QString text) {
		Ui::Toast::Show(text);
	}, [=](Group::JoinInfo info) {
		const auto call = info.peer->groupCall();
		info.joinHash = joinHash;
		createGroupCall(
			std::move(info),
			call ? call->input() : MTP_inputGroupCall(MTPlong(), MTPlong()));
	});
}

not_null<Media::Audio::Track*> Instance::ensureSoundLoaded(
		const QString &key) {
	const auto i = _tracks.find(key);
	if (i != end(_tracks)) {
		return i->second.get();
	}
	const auto result = _tracks.emplace(
		key,
		Media::Audio::Current().createTrack()).first->second.get();
	result->fillFromFile(Core::App().settings().getSoundPath(key));
	return result;
}

void Instance::playSoundOnce(const QString &key) {
	ensureSoundLoaded(key)->playOnce();
}

void Instance::destroyCall(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		_currentCallPanel->closeBeforeDestroy();
		_currentCallPanel = nullptr;

		auto taken = base::take(_currentCall);
		_currentCallChanges.fire(nullptr);
		taken.reset();

		if (App::quitting()) {
			LOG(("Calls::Instance doesn't prevent quit any more."));
		}
		Core::App().quitPreventFinished();
	}
}

void Instance::createCall(not_null<UserData*> user, Call::Type type, bool video) {
	auto call = std::make_unique<Call>(_delegate.get(), user, type, video);
	const auto raw = call.get();

	user->session().account().sessionChanges(
	) | rpl::start_with_next([=] {
		destroyCall(raw);
	}, raw->lifetime());

	if (_currentCall) {
		_currentCallPanel->replaceCall(raw);
		std::swap(_currentCall, call);
		call->hangup();
	} else {
		_currentCallPanel = std::make_unique<Panel>(raw);
		_currentCall = std::move(call);
	}
	_currentCallChanges.fire_copy(raw);
	refreshServerConfig(&user->session());
	refreshDhConfig();
}

void Instance::destroyGroupCall(not_null<GroupCall*> call) {
	if (_currentGroupCall.get() == call) {
		_currentGroupCallPanel->closeBeforeDestroy();
		_currentGroupCallPanel = nullptr;

		auto taken = base::take(_currentGroupCall);
		_currentGroupCallChanges.fire(nullptr);
		taken.reset();

		if (App::quitting()) {
			LOG(("Calls::Instance doesn't prevent quit any more."));
		}
		Core::App().quitPreventFinished();
	}
}

void Instance::createGroupCall(
		Group::JoinInfo info,
		const MTPInputGroupCall &inputCall) {
	destroyCurrentCall();

	auto call = std::make_unique<GroupCall>(
		_delegate.get(),
		std::move(info),
		inputCall);
	const auto raw = call.get();

	info.peer->session().account().sessionChanges(
	) | rpl::start_with_next([=] {
		destroyGroupCall(raw);
	}, raw->lifetime());

	_currentGroupCallPanel = std::make_unique<Group::Panel>(raw);
	_currentGroupCall = std::move(call);
	_currentGroupCallChanges.fire_copy(raw);
}

void Instance::refreshDhConfig() {
	Expects(_currentCall != nullptr);

	const auto weak = base::make_weak(_currentCall);
	_currentCall->user()->session().api().request(MTPmessages_GetDhConfig(
		MTP_int(_cachedDhConfig->version),
		MTP_int(MTP::ModExpFirst::kRandomPowerSize)
	)).done([=](const MTPmessages_DhConfig &result) {
		const auto call = weak.get();
		const auto random = updateDhConfig(result);
		if (!call) {
			return;
		}
		if (!random.empty()) {
			Assert(random.size() == MTP::ModExpFirst::kRandomPowerSize);
			call->start(random);
		} else {
			_delegate->callFailed(call);
		}
	}).fail([=](const MTP::Error &error) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		_delegate->callFailed(call);
	}).send();
}

bytes::const_span Instance::updateDhConfig(
		const MTPmessages_DhConfig &data) {
	const auto validRandom = [](const QByteArray & random) {
		if (random.size() != MTP::ModExpFirst::kRandomPowerSize) {
			return false;
		}
		return true;
	};
	return data.match([&](const MTPDmessages_dhConfig &data)
	-> bytes::const_span {
		auto primeBytes = bytes::make_vector(data.vp().v);
		if (!MTP::IsPrimeAndGood(primeBytes, data.vg().v)) {
			LOG(("API Error: bad p/g received in dhConfig."));
			return {};
		} else if (!validRandom(data.vrandom().v)) {
			return {};
		}
		_cachedDhConfig->g = data.vg().v;
		_cachedDhConfig->p = std::move(primeBytes);
		_cachedDhConfig->version = data.vversion().v;
		return bytes::make_span(data.vrandom().v);
	}, [&](const MTPDmessages_dhConfigNotModified &data)
	-> bytes::const_span {
		if (!_cachedDhConfig->g || _cachedDhConfig->p.empty()) {
			LOG(("API Error: dhConfigNotModified on zero version."));
			return {};
		} else if (!validRandom(data.vrandom().v)) {
			return {};
		}
		return bytes::make_span(data.vrandom().v);
	});
}

void Instance::refreshServerConfig(not_null<Main::Session*> session) {
	if (_serverConfigRequestSession) {
		return;
	}
	if (_lastServerConfigUpdateTime
		&& ((crl::now() - _lastServerConfigUpdateTime)
			< kServerConfigUpdateTimeoutMs)) {
		return;
	}
	_serverConfigRequestSession = session;
	session->api().request(MTPphone_GetCallConfig(
	)).done([=](const MTPDataJSON &result) {
		_serverConfigRequestSession = nullptr;
		_lastServerConfigUpdateTime = crl::now();

		const auto &json = result.c_dataJSON().vdata().v;
		UpdateConfig(std::string(json.data(), json.size()));
	}).fail([=](const MTP::Error &error) {
		_serverConfigRequestSession = nullptr;
	}).send();
}

void Instance::handleUpdate(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	update.match([&](const MTPDupdatePhoneCall &data) {
		handleCallUpdate(session, data.vphone_call());
	}, [&](const MTPDupdatePhoneCallSignalingData &data) {
		handleSignalingData(session, data);
	}, [&](const MTPDupdateGroupCall &data) {
		handleGroupCallUpdate(session, update);
	}, [&](const MTPDupdateGroupCallConnection &data) {
		handleGroupCallUpdate(session, update);
	}, [&](const MTPDupdateGroupCallParticipants &data) {
		handleGroupCallUpdate(session, update);
	}, [](const auto &) {
		Unexpected("Update type in Calls::Instance::handleUpdate.");
	});
}

void Instance::showInfoPanel(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		_currentCallPanel->showAndActivate();
	}
}

void Instance::showInfoPanel(not_null<GroupCall*> call) {
	if (_currentGroupCall.get() == call) {
		_currentGroupCallPanel->showAndActivate();
	}
}

void Instance::setCurrentAudioDevice(bool input, const QString &deviceId) {
	if (input) {
		Core::App().settings().setCallInputDeviceId(deviceId);
	} else {
		Core::App().settings().setCallOutputDeviceId(deviceId);
	}
	Core::App().saveSettingsDelayed();
	if (const auto call = currentCall()) {
		call->setCurrentAudioDevice(input, deviceId);
	} else if (const auto group = currentGroupCall()) {
		group->setCurrentAudioDevice(input, deviceId);
	}
}

FnMut<void()> Instance::addAsyncWaiter() {
	auto semaphore = std::make_unique<crl::semaphore>();
	const auto raw = semaphore.get();
	const auto weak = base::make_weak(this);
	_asyncWaiters.emplace(std::move(semaphore));
	return [raw, weak] {
		raw->release();
		crl::on_main(weak, [raw, weak] {
			auto &waiters = weak->_asyncWaiters;
			auto wrapped = std::unique_ptr<crl::semaphore>(raw);
			const auto i = waiters.find(wrapped);
			wrapped.release();

			if (i != end(waiters)) {
				waiters.erase(i);
			}
		});
	};
}

bool Instance::isQuitPrevent() {
	if (!_currentCall || _currentCall->isIncomingWaiting()) {
		return false;
	}
	_currentCall->hangup();
	if (!_currentCall) {
		return false;
	}
	LOG(("Calls::Instance prevents quit, hanging up a call..."));
	return true;
}

void Instance::handleCallUpdate(
		not_null<Main::Session*> session,
		const MTPPhoneCall &call) {
	if (call.type() == mtpc_phoneCallRequested) {
		auto &phoneCall = call.c_phoneCallRequested();
		auto user = session->data().userLoaded(phoneCall.vadmin_id());
		if (!user) {
			LOG(("API Error: User not loaded for phoneCallRequested."));
		} else if (user->isSelf()) {
			LOG(("API Error: Self found in phoneCallRequested."));
		}
		const auto &config = session->serverConfig();
		if (inCall() || inGroupCall() || !user || user->isSelf()) {
			const auto flags = phoneCall.is_video()
				? MTPphone_DiscardCall::Flag::f_video
				: MTPphone_DiscardCall::Flag(0);
			session->api().request(MTPphone_DiscardCall(
				MTP_flags(flags),
				MTP_inputPhoneCall(phoneCall.vid(), phoneCall.vaccess_hash()),
				MTP_int(0),
				MTP_phoneCallDiscardReasonBusy(),
				MTP_long(0)
			)).send();
		} else if (phoneCall.vdate().v + (config.callRingTimeoutMs / 1000)
			< base::unixtime::now()) {
			LOG(("Ignoring too old call."));
		} else if (Core::App().settings().disableCalls()) {
			LOG(("Ignoring call because of 'accept calls' settings."));
		} else {
			createCall(user, Call::Type::Incoming, phoneCall.is_video());
			_currentCall->handleUpdate(call);
		}
	} else if (!_currentCall
		|| (&_currentCall->user()->session() != session)
		|| !_currentCall->handleUpdate(call)) {
		DEBUG_LOG(("API Warning: unexpected phone call update %1").arg(call.type()));
	}
}

void Instance::handleGroupCallUpdate(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	if (_currentGroupCall
		&& (&_currentGroupCall->peer()->session() == session)) {
		update.match([&](const MTPDupdateGroupCall &data) {
			_currentGroupCall->handlePossibleCreateOrJoinResponse(data);
		}, [&](const MTPDupdateGroupCallConnection &data) {
			_currentGroupCall->handlePossibleCreateOrJoinResponse(data);
		}, [](const auto &) {
		});
	}

	if (update.type() == mtpc_updateGroupCallConnection) {
		return;
	}
	const auto callId = update.match([](const MTPDupdateGroupCall &data) {
		return data.vcall().match([](const auto &data) {
			return data.vid().v;
		});
	}, [](const MTPDupdateGroupCallParticipants &data) {
		return data.vcall().match([&](const MTPDinputGroupCall &data) {
			return data.vid().v;
		});
	}, [](const auto &) -> uint64 {
		Unexpected("Type in Instance::handleGroupCallUpdate.");
	});
	if (const auto existing = session->data().groupCall(callId)) {
		existing->enqueueUpdate(update);
	} else {
		applyGroupCallUpdateChecked(session, update);
	}
}

void Instance::applyGroupCallUpdateChecked(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	if (_currentGroupCall
		&& (&_currentGroupCall->peer()->session() == session)) {
		_currentGroupCall->handleUpdate(update);
	}
}

void Instance::handleSignalingData(
		not_null<Main::Session*> session,
		const MTPDupdatePhoneCallSignalingData &data) {
	if (!_currentCall
		|| (&_currentCall->user()->session() != session)
		|| !_currentCall->handleSignalingData(data)) {
		DEBUG_LOG(("API Warning: unexpected call signaling data %1"
			).arg(data.vphone_call_id().v));
	}
}

bool Instance::inCall() const {
	if (!_currentCall) {
		return false;
	}
	const auto state = _currentCall->state();
	return (state != Call::State::Busy);
}

bool Instance::inGroupCall() const {
	if (!_currentGroupCall) {
		return false;
	}
	const auto state = _currentGroupCall->state();
	return (state != GroupCall::State::HangingUp)
		&& (state != GroupCall::State::Ended)
		&& (state != GroupCall::State::FailedHangingUp)
		&& (state != GroupCall::State::Failed);
}

void Instance::destroyCurrentCall() {
	if (const auto current = currentCall()) {
		current->hangup();
		if (const auto still = currentCall()) {
			destroyCall(still);
		}
	}
	if (const auto current = currentGroupCall()) {
		current->hangup();
		if (const auto still = currentGroupCall()) {
			destroyGroupCall(still);
		}
	}
}

bool Instance::hasActivePanel(not_null<Main::Session*> session) const {
	if (inCall()) {
		return (&_currentCall->user()->session() == session)
			&& _currentCallPanel->isActive();
	} else if (inGroupCall()) {
		return (&_currentGroupCall->peer()->session() == session)
			&& _currentGroupCallPanel->isActive();
	}
	return false;
}

bool Instance::activateCurrentCall(const QString &joinHash) {
	if (inCall()) {
		_currentCallPanel->showAndActivate();
		return true;
	} else if (inGroupCall()) {
		if (!joinHash.isEmpty()) {
			_currentGroupCall->rejoinWithHash(joinHash);
		}
		_currentGroupCallPanel->showAndActivate();
		return true;
	}
	return false;
}

bool Instance::minimizeCurrentActiveCall() {
	if (inCall() && _currentCallPanel->isActive()) {
		_currentCallPanel->minimize();
		return true;
	} else if (inGroupCall() && _currentGroupCallPanel->isActive()) {
		_currentGroupCallPanel->minimize();
		return true;
	}
	return false;
}

bool Instance::closeCurrentActiveCall() {
	if (inGroupCall() && _currentGroupCallPanel->isActive()) {
		_currentGroupCallPanel->close();
		return true;
	}
	return false;
}

Call *Instance::currentCall() const {
	return _currentCall.get();
}

rpl::producer<Call*> Instance::currentCallValue() const {
	return _currentCallChanges.events_starting_with(currentCall());
}

GroupCall *Instance::currentGroupCall() const {
	return _currentGroupCall.get();
}

rpl::producer<GroupCall*> Instance::currentGroupCallValue() const {
	return _currentGroupCallChanges.events_starting_with(currentGroupCall());
}

void Instance::requestPermissionsOrFail(Fn<void()> onSuccess, bool video) {
	using Type = Platform::PermissionType;
	requestPermissionOrFail(Type::Microphone, [=] {
		auto callback = [=] { crl::on_main(onSuccess); };
		if (video) {
			requestPermissionOrFail(Type::Camera, std::move(callback));
		} else {
			callback();
		}
	});
}

void Instance::requestPermissionOrFail(Platform::PermissionType type, Fn<void()> onSuccess) {
	using Status = Platform::PermissionStatus;
	const auto status = Platform::GetPermissionStatus(type);
	if (status == Status::Granted) {
		onSuccess();
	} else if (status == Status::CanRequest) {
		Platform::RequestPermission(type, crl::guard(this, [=](Status status) {
			if (status == Status::Granted) {
				crl::on_main(onSuccess);
			} else {
				if (_currentCall) {
					_currentCall->hangup();
				}
			}
		}));
	} else {
		if (inCall()) {
			_currentCall->hangup();
		}
		if (inGroupCall()) {
			_currentGroupCall->hangup();
		}
		Ui::show(Box<ConfirmBox>(tr::lng_no_mic_permission(tr::now), tr::lng_menu_settings(tr::now), crl::guard(this, [=] {
			Platform::OpenSystemSettingsForPermission(type);
			Ui::hideLayer();
		})));
	}
}

std::shared_ptr<tgcalls::VideoCaptureInterface> Instance::getVideoCapture(
		QString deviceId) {
	if (deviceId.isEmpty()) {
		deviceId = Core::App().settings().callVideoInputDeviceId();
	}
	if (auto result = _videoCapture.lock()) {
		result->switchToDevice(deviceId.toStdString());
		return result;
	}
	auto result = std::shared_ptr<tgcalls::VideoCaptureInterface>(
		tgcalls::VideoCaptureInterface::Create(
			tgcalls::StaticThreads::getThreads(),
			deviceId.toStdString()));
	_videoCapture = result;
	return result;
}

} // namespace Calls
