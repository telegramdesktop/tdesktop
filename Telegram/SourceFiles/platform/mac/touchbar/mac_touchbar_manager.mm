/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_manager.h"

#include "apiwrap.h" // ApiWrap::updateStickers()
#include "core/application.h"
#include "data/data_chat_participant_status.h" // Data::CanSendAnyOf.
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h" // Stickers::setsRef()
#include "main/main_domain.h"
#include "main/main_session.h"
#include "media/audio/media_audio_capture.h"
#include "media/player/media_player_instance.h"
#include "platform/mac/touchbar/mac_touchbar_audio.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"
#include "platform/mac/touchbar/mac_touchbar_main.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#import <AppKit/NSGroupTouchBarItem.h>

using namespace TouchBar::Main;

namespace {

const auto kMainItemIdentifier = @"touchbarMain";
const auto kAudioItemIdentifier = @"touchbarAudio";

} // namespace

@interface GroupTouchBarItem : NSGroupTouchBarItem
- (rpl::lifetime &)lifetime;
@end // @interface GroupTouchBarItem

@implementation GroupTouchBarItem {
	rpl::lifetime _lifetime;
}

- (rpl::lifetime &)lifetime {
	return _lifetime;
}

@end // GroupTouchBarItem

#pragma mark - RootTouchBar

@interface RootTouchBar()
@end // @interface RootTouchBar

@implementation RootTouchBar {
	Main::Session *_session;
	Window::Controller *_controller;

	rpl::variable<Ui::MarkdownEnabledState> _markdownState;
	rpl::event_stream<> _touchBarSwitches;
	rpl::lifetime _lifetime;
}

- (id)init:(rpl::producer<Ui::MarkdownEnabledState>)markdownState
		controller:(not_null<Window::Controller*>)controller
		domain:(not_null<Main::Domain*>)domain {
	self = [super init];
	if (!self) {
		return self;
	}
	self.delegate = self;
	TouchBar::CustomEnterToCocoaEventLoop([=] {
		self.defaultItemIdentifiers = @[];
	});
	_controller = controller;
	_markdownState = std::move(markdownState);

	auto sessionChanges = domain->activeSessionChanges(
	) | rpl::map([=](Main::Session *session) {
		if (session && session->data().stickers().setsRef().empty()) {
			session->api().updateStickers();
		}
		return session;
	});

	const auto type = AudioMsgId::Type::Song;
	auto audioPlayer = rpl::merge(
		Media::Player::instance()->stops(type) | rpl::map_to(false),
		Media::Player::instance()->startsPlay(type) | rpl::map_to(true)
	);

	auto voiceRecording = ::Media::Capture::instance()->startedChanges();

	rpl::combine(
		std::move(sessionChanges),
		rpl::single(false) | rpl::then(Core::App().passcodeLockChanges()),
		rpl::single(false) | rpl::then(std::move(audioPlayer)),
		rpl::single(false) | rpl::then(std::move(voiceRecording))
	) | rpl::start_with_next([=](
			Main::Session *session,
			bool lock,
			bool audio,
			bool recording) {
		TouchBar::CustomEnterToCocoaEventLoop([=] {
			_touchBarSwitches.fire({});
			if (!audio) {
				self.defaultItemIdentifiers = @[];
			}
			self.defaultItemIdentifiers = (lock || recording)
				? @[]
				: audio
				? @[kAudioItemIdentifier]
				: session
				? @[kMainItemIdentifier]
				: @[];
		});
	}, _lifetime);

	return self;
}

- (NSTouchBarItem*)touchBar:(NSTouchBar*)touchBar
		makeItemForIdentifier:(NSTouchBarItemIdentifier)itemId {
	if (!touchBar || !_controller->sessionController()) {
		return nil;
	}
	const auto isEqual = [&](NSString *string) {
		return [itemId isEqualToString:string];
	};

	if (isEqual(kMainItemIdentifier)) {
		auto *item = [[GroupTouchBarItem alloc] initWithIdentifier:itemId];
		item.groupTouchBar
			= [[[TouchBarMain alloc]
				init:_controller
				touchBarSwitches:_touchBarSwitches.events()] autorelease];
		rpl::combine(
			_markdownState.value(),
			_controller->sessionController()->activeChatValue(
			) | rpl::map([](Dialogs::Key k) {
				const auto topic = k.topic();
				const auto peer = k.peer();
				const auto rights = ChatRestriction::SendStickers
					| ChatRestriction::SendOther;
				return topic
					? Data::CanSendAnyOf(topic, rights)
					: (peer && Data::CanSendAnyOf(peer, rights));
			}) | rpl::distinct_until_changed()
		) | rpl::start_with_next([=](
				Ui::MarkdownEnabledState state,
				bool hasActiveChat) {
			item.groupTouchBar.defaultItemIdentifiers = @[
				kPinnedPanelItemIdentifier,
				(!state.disabled()
					? kPopoverInputItemIdentifier
					: hasActiveChat
					? kPopoverPickerItemIdentifier
					: @"")];
		}, [item lifetime]);

		return [item autorelease];
	} else if (isEqual(kAudioItemIdentifier)) {
		auto *item = [[GroupTouchBarItem alloc] initWithIdentifier:itemId];
		auto *touchBar = [[[TouchBarAudioPlayer alloc] init]
			autorelease];
		item.groupTouchBar = touchBar;
		[touchBar closeRequests] | rpl::start_with_next([=] {
			Media::Player::instance()->stopAndClose();
		}, [item lifetime]);
		return [item autorelease];
	}

	return nil;
}

@end // @implementation RootTouchBar
