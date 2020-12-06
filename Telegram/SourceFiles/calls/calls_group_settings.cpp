/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_settings.h"

#include "calls/calls_group_call.h"
#include "calls/calls_group_panel.h" // LeaveGroupCallBox.
#include "calls/calls_instance.h"
#include "ui/widgets/level_meter.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/toast/toast.h"
#include "lang/lang_keys.h"
#include "base/event_filter.h"
#include "base/global_shortcuts.h"
#include "data/data_channel.h"
#include "data/data_group_call.h"
#include "core/application.h"
#include "boxes/single_choice_box.h"
#include "webrtc/webrtc_audio_input_tester.h"
#include "webrtc/webrtc_media_devices.h"
#include "settings/settings_common.h"
#include "settings/settings_calls.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "styles/style_layers.h"
#include "styles/style_calls.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>

namespace Calls {
namespace {

void SaveCallJoinMuted(
		not_null<ChannelData*> channel,
		uint64 callId,
		bool joinMuted) {
	const auto call = channel->call();
	if (!call
		|| call->id() != callId
		|| !channel->canManageCall()
		|| !call->canChangeJoinMuted()
		|| call->joinMuted() == joinMuted) {
		return;
	}
	call->setJoinMutedLocally(joinMuted);
	channel->session().api().request(MTPphone_ToggleGroupCallSettings(
		MTP_flags(MTPphone_ToggleGroupCallSettings::Flag::f_join_muted),
		call->input(),
		MTP_bool(joinMuted)
	)).send();
}

} // namespace

void GroupCallSettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<GroupCall*> call) {
	using namespace Settings;

	const auto weakCall = base::make_weak(call.get());
	const auto weakBox = Ui::MakeWeak(box);

	struct State {
		rpl::event_stream<QString> outputNameStream;
		rpl::event_stream<QString> inputNameStream;
		std::unique_ptr<Webrtc::AudioInputTester> micTester;
		Ui::LevelMeter *micTestLevel = nullptr;
		float micLevel = 0.;
		Ui::Animations::Simple micLevelAnimation;
		base::Timer levelUpdateTimer;
		bool generatingLink = false;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto channel = call->channel();
	const auto real = channel->call();
	const auto id = call->id();
	const auto goodReal = (real && real->id() == id);

	const auto layout = box->verticalLayout();
	const auto &settings = Core::App().settings();

	const auto joinMuted = goodReal ? real->joinMuted() : false;
	const auto canChangeJoinMuted = (goodReal && real->canChangeJoinMuted());
	const auto addCheck = (channel->canManageCall() && canChangeJoinMuted);
	if (addCheck) {
		AddSkip(layout);
	}
	const auto muteJoined = addCheck
		? AddButton(
			layout,
			tr::lng_group_call_new_muted(),
			st::groupCallSettingsButton)->toggleOn(rpl::single(joinMuted))
		: nullptr;
	if (addCheck) {
		AddSkip(layout);
	}

	AddButtonWithLabel(
		layout,
		tr::lng_group_call_speakers(),
		rpl::single(
			CurrentAudioOutputName()
		) | rpl::then(
			state->outputNameStream.events()
		),
		st::groupCallSettingsButton
	)->addClickHandler([=] {
		box->getDelegate()->show(ChooseAudioOutputBox(crl::guard(box, [=](
				const QString &id,
				const QString &name) {
			state->outputNameStream.fire_copy(name);
		}), &st::groupCallCheckbox, &st::groupCallRadio));
	});

	AddButtonWithLabel(
		layout,
		tr::lng_group_call_microphone(),
		rpl::single(
			CurrentAudioInputName()
		) | rpl::then(
			state->inputNameStream.events()
		),
		st::groupCallSettingsButton
	)->addClickHandler([=] {
		box->getDelegate()->show(ChooseAudioInputBox(crl::guard(box, [=](
				const QString &id,
				const QString &name) {
			state->inputNameStream.fire_copy(name);
			if (state->micTester) {
				state->micTester->setDeviceId(id);
			}
		}), &st::groupCallCheckbox, &st::groupCallRadio));
	});

	state->micTestLevel = box->addRow(
		object_ptr<Ui::LevelMeter>(
			box.get(),
			st::groupCallLevelMeter),
		st::settingsLevelMeterPadding);
	state->micTestLevel->resize(QSize(0, st::defaultLevelMeter.height));

	state->levelUpdateTimer.setCallback([=] {
		const auto was = state->micLevel;
		state->micLevel = state->micTester->getAndResetLevel();
		state->micLevelAnimation.start([=] {
			state->micTestLevel->setValue(
				state->micLevelAnimation.value(state->micLevel));
		}, was, state->micLevel, kMicTestAnimationDuration);
	});

	AddSkip(layout);
	//AddDivider(layout);
	//AddSkip(layout);

	using GlobalShortcut = base::GlobalShortcut;
	struct PushToTalkState {
		rpl::variable<QString> recordText = tr::lng_group_call_ptt_shortcut();
		rpl::variable<QString> shortcutText;
		GlobalShortcut shortcut;
		bool recording = false;
	};
	const auto manager = call->ensureGlobalShortcutManager();
	if (manager) {
		const auto state = box->lifetime().make_state<PushToTalkState>();
		state->shortcut = manager->shortcutFromSerialized(
			settings.groupCallPushToTalkShortcut());
		state->shortcutText = state->shortcut
			? state->shortcut->toDisplayString()
			: QString();
		const auto pushToTalk = AddButton(
			layout,
			tr::lng_group_call_push_to_talk(),
			st::groupCallSettingsButton
		)->toggleOn(rpl::single(settings.groupCallPushToTalk()));
		const auto recordingWrap = layout->add(
			object_ptr<Ui::SlideWrap<Button>>(
				layout,
				object_ptr<Button>(
					layout,
					state->recordText.value(),
					st::groupCallSettingsButton)));
		const auto recording = recordingWrap->entity();
		CreateRightLabel(
			recording,
			state->shortcutText.value(),
			st::groupCallSettingsButton,
			state->recordText.value());
		const auto startRecording = [=] {
			state->recording = true;
			state->recordText = tr::lng_group_call_ptt_recording();
			auto progress = crl::guard(box, [=](GlobalShortcut shortcut) {
				state->shortcutText = shortcut->toDisplayString();
			});
			auto done = crl::guard(box, [=](GlobalShortcut shortcut) {
				state->recording = false;
				state->shortcut = shortcut;
				state->shortcutText = shortcut
					? shortcut->toDisplayString()
					: QString();
				state->recordText = tr::lng_group_call_ptt_shortcut();
				Core::App().settings().setGroupCallPushToTalkShortcut(shortcut
					? shortcut->serialize()
					: QByteArray());
				Core::App().saveSettingsDelayed();
			});
			manager->startRecording(std::move(progress), std::move(done));
		};
		const auto stopRecording = [=] {
			state->recording = false;
			state->recordText = tr::lng_group_call_ptt_shortcut();
			state->shortcutText = state->shortcut
				? state->shortcut->toDisplayString()
				: QString();
			manager->stopRecording();
		};
		recording->addClickHandler([=] {
			if (state->recording) {
				stopRecording();
			} else {
				startRecording();
			}
		});
		recordingWrap->toggle(
			settings.groupCallPushToTalk(),
			anim::type::instant);
		pushToTalk->toggledChanges(
		) | rpl::start_with_next([=](bool toggled) {
			if (!toggled) {
				stopRecording();
			}
			Core::App().settings().setGroupCallPushToTalk(toggled);
			Core::App().saveSettingsDelayed();
			recordingWrap->toggle(toggled, anim::type::normal);
		}, pushToTalk->lifetime());

		box->boxClosing(
		) | rpl::start_with_next([=] {
			call->applyGlobalShortcutChanges();
		}, box->lifetime());

		auto boxKeyFilter = [=](not_null<QEvent*> e) {
			if (e->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			return (state->recording)
				? base::EventFilterResult::Cancel
				: base::EventFilterResult::Continue;
		};
		box->lifetime().make_state<base::unique_qptr<QObject>>(
			base::install_event_filter(box, std::move(boxKeyFilter)));
	}

	AddSkip(layout);
	//AddDivider(layout);
	//AddSkip(layout);

	const auto lookupLink = [=] {
		return channel->hasUsername()
			? channel->session().createInternalLinkFull(channel->username)
			: channel->inviteLink();
	};
	if (!lookupLink().isEmpty() || channel->canHaveInviteLink()) {
		const auto copyLink = [=] {
			const auto link = lookupLink();
			if (link.isEmpty()) {
				return false;
			}
			QGuiApplication::clipboard()->setText(link);
			if (weakBox) {
				Ui::Toast::Show(
					box->getDelegate()->outerContainer(),
					tr::lng_create_channel_link_copied(tr::now));
			}
			return true;
		};
		AddButton(
			layout,
			tr::lng_group_call_share(),
			st::groupCallSettingsButton
		)->addClickHandler([=] {
			if (!copyLink() && !state->generatingLink) {
				state->generatingLink = true;
				channel->session().api().request(MTPmessages_ExportChatInvite(
					channel->input
				)).done([=](const MTPExportedChatInvite &result) {
					if (result.type() == mtpc_chatInviteExported) {
						channel->setInviteLink(
							qs(result.c_chatInviteExported().vlink()));
						copyLink();
					}
				}).send();
			}
		});
	}

	if (channel->canManageCall()) {
		AddButton(
			layout,
			tr::lng_group_call_end(),
			st::groupCallSettingsAttentionButton
		)->addClickHandler([=] {
			if (const auto call = weakCall.get()) {
				box->getDelegate()->show(Box(
					LeaveGroupCallBox,
					call,
					true,
					BoxContext::GroupCallPanel));
				box->closeBox();
			}
		});
	}

	box->setShowFinishedCallback([=] {
		// Means we finished showing the box.
		crl::on_main(box, [=] {
			state->micTester = std::make_unique<Webrtc::AudioInputTester>(
				Core::App().settings().callInputDeviceId());
			state->levelUpdateTimer.callEach(kMicTestUpdateInterval);
		});
	});

	box->setTitle(tr::lng_group_call_settings_title());
	box->boxClosing(
	) | rpl::start_with_next([=] {
		if (canChangeJoinMuted
			&& muteJoined
			&& muteJoined->toggled() != joinMuted) {
			SaveCallJoinMuted(channel, id, muteJoined->toggled());
		}
	}, box->lifetime());
	box->addButton(tr::lng_box_done(), [=] {
		box->closeBox();
	});
}

} // namespace Calls
