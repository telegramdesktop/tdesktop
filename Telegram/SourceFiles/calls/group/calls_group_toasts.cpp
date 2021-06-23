/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_toasts.h"

#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_panel.h"
#include "data/data_peer.h"
#include "data/data_group_call.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "lang/lang_keys.h"

namespace Calls::Group {
namespace {

constexpr auto kErrorDuration = 2 * crl::time(1000);

using State = GroupCall::State;

} // namespace

Toasts::Toasts(not_null<Panel*> panel)
: _panel(panel)
, _call(panel->call()) {
	setup();
}

void Toasts::setup() {
	setupJoinAsChanged();
	setupTitleChanged();
	setupRequestedToSpeak();
	setupAllowedToSpeak();
	setupPinnedVideo();
	setupError();
}

void Toasts::setupJoinAsChanged() {
	_call->rejoinEvents(
	) | rpl::filter([](RejoinEvent event) {
		return (event.wasJoinAs != event.nowJoinAs);
	}) | rpl::map([=] {
		return _call->stateValue() | rpl::filter([](State state) {
			return (state == State::Joined);
		}) | rpl::take(1);
	}) | rpl::flatten_latest() | rpl::start_with_next([=] {
		_panel->showToast(tr::lng_group_call_join_as_changed(
			tr::now,
			lt_name,
			Ui::Text::Bold(_call->joinAs()->name),
			Ui::Text::WithEntities));
	}, _lifetime);
}

void Toasts::setupTitleChanged() {
	_call->titleChanged(
	) | rpl::filter([=] {
		return (_call->lookupReal() != nullptr);
	}) | rpl::map([=] {
		const auto peer = _call->peer();
		return peer->groupCall()->title().isEmpty()
			? peer->name
			: peer->groupCall()->title();
	}) | rpl::start_with_next([=](const QString &title) {
		_panel->showToast(tr::lng_group_call_title_changed(
			tr::now,
			lt_title,
			Ui::Text::Bold(title),
			Ui::Text::WithEntities));
	}, _lifetime);
}

void Toasts::setupAllowedToSpeak() {
	_call->allowedToSpeakNotifications(
	) | rpl::start_with_next([=] {
		if (_panel->isActive()) {
			_panel->showToast({
				tr::lng_group_call_can_speak_here(tr::now),
			});
		} else {
			const auto real = _call->lookupReal();
			const auto name = (real && !real->title().isEmpty())
				? real->title()
				: _call->peer()->name;
			Ui::ShowMultilineToast({
				.text = tr::lng_group_call_can_speak(
					tr::now,
					lt_chat,
					Ui::Text::Bold(name),
					Ui::Text::WithEntities),
			});
		}
	}, _lifetime);
}

void Toasts::setupPinnedVideo() {
	_call->videoEndpointPinnedValue(
	) | rpl::map([=](bool pinned) {
		return pinned
			? _call->videoEndpointLargeValue()
			: rpl::single(_call->videoEndpointLarge());
	}) | rpl::flatten_latest(
	) | rpl::filter([=] {
		return (_call->shownVideoTracks().size() > 1);
	}) | rpl::start_with_next([=](const VideoEndpoint &endpoint) {
		const auto pinned = _call->videoEndpointPinned();
		const auto peer = endpoint.peer;
		if (!peer) {
			return;
		}
		const auto text = [&] {
			const auto me = (peer == _call->joinAs());
			const auto camera = (endpoint.type == VideoEndpointType::Camera);
			if (me) {
				const auto key = camera
					? (pinned
						? tr::lng_group_call_pinned_camera_me
						: tr::lng_group_call_unpinned_camera_me)
					: (pinned
						? tr::lng_group_call_pinned_screen_me
						: tr::lng_group_call_unpinned_screen_me);
				return key(tr::now);
			}
			const auto key = camera
				? (pinned
					? tr::lng_group_call_pinned_camera
					: tr::lng_group_call_unpinned_camera)
				: (pinned
					? tr::lng_group_call_pinned_screen
					: tr::lng_group_call_unpinned_screen);
			return key(tr::now, lt_user, peer->shortName());
		}();
		_panel->showToast({ text });
	}, _lifetime);
}

void Toasts::setupRequestedToSpeak() {
	_call->mutedValue(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](MuteState was, MuteState now) {
		if (was == MuteState::ForceMuted && now == MuteState::RaisedHand) {
			_panel->showToast({
				tr::lng_group_call_tooltip_raised_hand(tr::now),
			});
		}
	}, _lifetime);
}

void Toasts::setupError() {
	_call->errors(
	) | rpl::start_with_next([=](Error error) {
		const auto key = [&] {
			switch (error) {
			case Error::NoCamera: return tr::lng_call_error_no_camera;
			case Error::CameraFailed:
				return tr::lng_group_call_failed_camera;
			case Error::ScreenFailed:
				return tr::lng_group_call_failed_screen;
			case Error::MutedNoCamera:
				return tr::lng_group_call_muted_no_camera;
			case Error::MutedNoScreen:
				return tr::lng_group_call_muted_no_screen;
			case Error::DisabledNoCamera:
				return tr::lng_group_call_chat_no_camera;
			case Error::DisabledNoScreen:
				return tr::lng_group_call_chat_no_screen;
			}
			Unexpected("Error in Calls::Group::Toasts::setupErrorToasts.");
		}();
		_panel->showToast({ key(tr::now) }, kErrorDuration);
	}, _lifetime);
}

} // namespace Calls::Group
