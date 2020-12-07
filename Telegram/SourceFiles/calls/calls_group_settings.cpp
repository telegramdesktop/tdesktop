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
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "lang/lang_keys.h"
#include "base/timer_rpl.h"
#include "base/event_filter.h"
#include "base/global_shortcuts.h"
#include "base/platform/base_platform_info.h"
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

constexpr auto kDelaysCount = 201;
constexpr auto kCheckAccessibilityInterval = crl::time(500);

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

[[nodiscard]] crl::time DelayByIndex(int index) {
	return index * crl::time(10);
}

[[nodiscard]] QString FormatDelay(crl::time delay) {
	return (delay < crl::time(1000))
		? tr::lng_group_call_ptt_delay_ms(
			tr::now,
			lt_amount,
			QString::number(delay))
		: tr::lng_group_call_ptt_delay_s(
			tr::now,
			lt_amount,
			QString::number(delay / 1000., 'f', 2));
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
		rpl::event_stream<bool> pushToTalkToggles;
		std::shared_ptr<base::GlobalShortcutManager> manager;
		GlobalShortcut shortcut;
		crl::time delay = 0;
		bool recording = false;
	};
	if (base::GlobalShortcutsAvailable()) {
		const auto state = box->lifetime().make_state<PushToTalkState>();
		if (!base::GlobalShortcutsAllowed()) {
			Core::App().settings().setGroupCallPushToTalk(false);
		}
		const auto tryFillFromManager = [=] {
			state->shortcut = state->manager
				? state->manager->shortcutFromSerialized(
					Core::App().settings().groupCallPushToTalkShortcut())
				: nullptr;
			state->shortcutText = state->shortcut
				? state->shortcut->toDisplayString()
				: QString();
		};
		state->manager = settings.groupCallPushToTalk()
			? call->ensureGlobalShortcutManager()
			: nullptr;
		tryFillFromManager();

		state->delay = settings.groupCallPushToTalkDelay();
		const auto pushToTalk = AddButton(
			layout,
			tr::lng_group_call_push_to_talk(),
			st::groupCallSettingsButton
		)->toggleOn(rpl::single(
			settings.groupCallPushToTalk()
		) | rpl::then(state->pushToTalkToggles.events()));
		const auto pushToTalkWrap = layout->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				layout,
				object_ptr<Ui::VerticalLayout>(layout)));
		const auto pushToTalkInner = pushToTalkWrap->entity();
		const auto recording = pushToTalkInner->add(
			object_ptr<Button>(
				layout,
				state->recordText.value(),
				st::groupCallSettingsButton));
		CreateRightLabel(
			recording,
			state->shortcutText.value(),
			st::groupCallSettingsButton,
			state->recordText.value());

		const auto applyAndSave = [=] {
			call->applyGlobalShortcutChanges();
			Core::App().saveSettingsDelayed();
		};
		const auto showPrivacyRequest = [=] {
#ifdef Q_OS_MAC
			if (!Platform::IsMac10_14OrGreater()) {
				return;
			}
			const auto requestInputMonitoring = Platform::IsMac10_15OrGreater();
			box->getDelegate()->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->addRow(
					object_ptr<Ui::FlatLabel>(
						box.get(),
						rpl::combine(
							tr::lng_group_call_mac_access(),
							(requestInputMonitoring
								? tr::lng_group_call_mac_input()
								: tr::lng_group_call_mac_accessibility())
						) | rpl::map([](QString a, QString b) {
							auto result = Ui::Text::RichLangValue(a);
							result.append("\n\n").append(Ui::Text::RichLangValue(b));
							return result;
						}),
						st::groupCallBoxLabel),
					style::margins(
						st::boxRowPadding.left(),
						st::boxPadding.top(),
						st::boxRowPadding.right(),
						st::boxPadding.bottom()));
				box->addButton(tr::lng_group_call_mac_settings(), [=] {
					if (requestInputMonitoring) {
						Platform::OpenInputMonitoringPrivacySettings();
					} else {
						Platform::OpenAccessibilityPrivacySettings();
					}
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

				if (!requestInputMonitoring) {
					// Accessibility is enabled without app restart, so short-poll it.
					base::timer_each(
						kCheckAccessibilityInterval
					) | rpl::filter([] {
						return base::GlobalShortcutsAllowed();
					}) | rpl::start_with_next([=] {
						box->closeBox();
					}, box->lifetime());
				}
			}));
#endif // Q_OS_MAC
		};
		const auto ensureManager = [=] {
			if (state->manager) {
				return true;
			} else if (base::GlobalShortcutsAllowed()) {
				state->manager = call->ensureGlobalShortcutManager();
				tryFillFromManager();
				return true;
			}
			showPrivacyRequest();
			return false;
		};
		const auto stopRecording = [=] {
			state->recording = false;
			state->recordText = tr::lng_group_call_ptt_shortcut();
			state->shortcutText = state->shortcut
				? state->shortcut->toDisplayString()
				: QString();
			recording->setColorOverride(std::nullopt);
			if (state->manager) {
				state->manager->stopRecording();
			}
		};
		const auto startRecording = [=] {
			if (!ensureManager()) {
				state->pushToTalkToggles.fire(false);
				pushToTalkWrap->hide(anim::type::instant);
				return;
			}
			state->recording = true;
			state->recordText = tr::lng_group_call_ptt_recording();
			recording->setColorOverride(
				st::groupCallSettingsAttentionButton.textFg->c);
			auto progress = crl::guard(box, [=](GlobalShortcut shortcut) {
				state->shortcutText = shortcut->toDisplayString();
			});
			auto done = crl::guard(box, [=](GlobalShortcut shortcut) {
				state->shortcut = shortcut;
				Core::App().settings().setGroupCallPushToTalkShortcut(shortcut
					? shortcut->serialize()
					: QByteArray());
				applyAndSave();
				stopRecording();
			});
			state->manager->startRecording(std::move(progress), std::move(done));
		};
		recording->addClickHandler([=] {
			if (state->recording) {
				stopRecording();
			} else {
				startRecording();
			}
		});

		const auto label = pushToTalkInner->add(
			object_ptr<Ui::LabelSimple>(
				pushToTalkInner,
				st::groupCallDelayLabel),
			st::groupCallDelayLabelMargin);
		const auto value = std::clamp(
			state->delay,
			crl::time(0),
			DelayByIndex(kDelaysCount - 1));
		const auto callback = [=](crl::time delay) {
			state->delay = delay;
			label->setText(tr::lng_group_call_ptt_delay(
				tr::now,
				lt_delay,
				FormatDelay(delay)));
			Core::App().settings().setGroupCallPushToTalkDelay(delay);
			applyAndSave();
		};
		callback(value);
		const auto slider = pushToTalkInner->add(
			object_ptr<Ui::MediaSlider>(
				pushToTalkInner,
				st::groupCallDelaySlider),
			st::groupCallDelayMargin);
		slider->resize(st::groupCallDelaySlider.seekSize);
		slider->setPseudoDiscrete(
			kDelaysCount,
			DelayByIndex,
			value,
			callback);

		pushToTalkWrap->toggle(
			settings.groupCallPushToTalk(),
			anim::type::instant);
		pushToTalk->toggledChanges(
		) | rpl::start_with_next([=](bool toggled) {
			if (!toggled) {
				stopRecording();
			} else if (!ensureManager()) {
				state->pushToTalkToggles.fire(false);
				pushToTalkWrap->hide(anim::type::instant);
				return;
			}
			Core::App().settings().setGroupCallPushToTalk(toggled);
			applyAndSave();
			pushToTalkWrap->toggle(toggled, anim::type::normal);
		}, pushToTalk->lifetime());

		auto boxKeyFilter = [=](not_null<QEvent*> e) {
			return (e->type() == QEvent::KeyPress && state->recording)
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
