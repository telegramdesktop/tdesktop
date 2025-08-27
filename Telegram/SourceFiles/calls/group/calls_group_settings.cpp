/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_settings.h"

#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_menu.h" // LeaveBox.
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_choose_join_as.h"
#include "calls/group/calls_volume_item.h"
#include "calls/calls_instance.h"
#include "ui/widgets/level_meter.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "lang/lang_keys.h"
#include "boxes/share_box.h"
#include "history/view/history_view_schedule_box.h"
#include "history/history_item_helpers.h" // GetErrorForSending.
#include "history/history.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "base/timer_rpl.h"
#include "base/event_filter.h"
#include "base/global_shortcuts.h"
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_group_call.h"
#include "data/data_user.h"
#include "calls/group/calls_group_rtmp.h"
#include "ui/toast/toast.h"
#include "data/data_changes.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "webrtc/webrtc_audio_input_tester.h"
#include "webrtc/webrtc_device_resolver.h"
#include "settings/settings_calls.h"
#include "settings/settings_credits_graphics.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_invite_links.h"
#include "styles/style_layers.h"
#include "styles/style_calls.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>

namespace Calls::Group {
namespace {

constexpr auto kDelaysCount = 201;
constexpr auto kMicrophoneTooltipAfterLoudCount = 3;
constexpr auto kDropLoudAfterQuietCount = 5;
constexpr auto kMicrophoneTooltipLevelThreshold = 0.2;
constexpr auto kMicrophoneTooltipCheckInterval = crl::time(500);

#ifdef Q_OS_MAC
constexpr auto kCheckAccessibilityInterval = crl::time(500);
#endif // Q_OS_MAC

void SaveCallJoinMuted(
		not_null<PeerData*> peer,
		CallId callId,
		bool joinMuted) {
	const auto call = peer->groupCall();
	if (!call
		|| call->id() != callId
		|| !peer->canManageGroupCall()
		|| !call->canChangeJoinMuted()
		|| call->joinMuted() == joinMuted) {
		return;
	}
	call->setJoinMutedLocally(joinMuted);
	peer->session().api().request(MTPphone_ToggleGroupCallSettings(
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

object_ptr<ShareBox> ShareInviteLinkBox(
		not_null<PeerData*> peer,
		const QString &linkSpeaker,
		const QString &linkListener,
		std::shared_ptr<Ui::Show> show) {
	const auto sending = std::make_shared<bool>();
	const auto box = std::make_shared<base::weak_qptr<ShareBox>>();

	auto bottom = linkSpeaker.isEmpty()
		? nullptr
		: object_ptr<Ui::PaddingWrap<Ui::Checkbox>>(
			nullptr,
			object_ptr<Ui::Checkbox>(
				nullptr,
				tr::lng_group_call_share_speaker(tr::now),
				true,
				st::groupCallCheckbox),
			st::groupCallShareMutedMargin);
	const auto speakerCheckbox = bottom ? bottom->entity() : nullptr;
	const auto currentLink = [=] {
		return (!speakerCheckbox || !speakerCheckbox->checked())
			? linkListener
			: linkSpeaker;
	};
	auto copyCallback = [=] {
		QGuiApplication::clipboard()->setText(currentLink());
		show->showToast(tr::lng_group_invite_copied(tr::now));
	};
	auto countMessagesCallback = [=](const TextWithTags &comment) {
		return 1;
	};
	auto submitCallback = [=](
			std::vector<not_null<Data::Thread*>> &&result,
			Fn<bool()> checkPaid,
			TextWithTags &&comment,
			Api::SendOptions options,
			Data::ForwardOptions) {
		if (*sending || result.empty()) {
			return;
		}

		const auto error = GetErrorForSending(
			result,
			{ .text = &comment });
		if (error.error) {
			if (const auto weak = *box) {
				weak->getDelegate()->show(
					MakeSendErrorBox(error, result.size() > 1));
			}
			return;
		} else if (!checkPaid()) {
			return;
		}

		*sending = true;
		const auto link = currentLink();
		if (!comment.text.isEmpty()) {
			comment.text = link + "\n" + comment.text;
			const auto add = link.size() + 1;
			for (auto &tag : comment.tags) {
				tag.offset += add;
			}
		} else {
			comment.text = link;
		}
		auto &api = peer->session().api();
		for (const auto thread : result) {
			auto message = Api::MessageToSend(
				Api::SendAction(thread, options));
			message.textWithTags = comment;
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		if (*box) {
			(*box)->closeBox();
		}
		show->showToast(tr::lng_share_done(tr::now));
	};
	auto filterCallback = [](not_null<Data::Thread*> thread) {
		if (const auto user = thread->peer()->asUser()) {
			if (user->canSendIgnoreMoneyRestrictions()) {
				return true;
			}
		}
		return Data::CanSend(thread, ChatRestriction::SendOther);
	};

	const auto st = ::Settings::DarkCreditsEntryBoxStyle();
	auto result = Box<ShareBox>(ShareBox::Descriptor{
		.session = &peer->session(),
		.copyCallback = std::move(copyCallback),
		.countMessagesCallback = std::move(countMessagesCallback),
		.submitCallback = std::move(submitCallback),
		.filterCallback = std::move(filterCallback),
		.bottomWidget = std::move(bottom),
		.copyLinkText = rpl::conditional(
			(speakerCheckbox
				? speakerCheckbox->checkedValue()
				: rpl::single(false)),
			tr::lng_group_call_copy_speaker_link(),
			tr::lng_group_call_copy_listener_link()),
		.st = st.shareBox ? *st.shareBox : ShareBoxStyleOverrides(),
		.moneyRestrictionError = ShareMessageMoneyRestrictionError(),
	});
	*box = result.data();
	return result;
}

} // namespace

void SettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<GroupCall*> call) {
	using namespace Settings;

	const auto weakCall = base::make_weak(call);
	const auto weakBox = base::make_weak(box);

	struct State {
		std::unique_ptr<Webrtc::DeviceResolver> deviceId;
		std::unique_ptr<Webrtc::AudioInputTester> micTester;
		Ui::LevelMeter *micTestLevel = nullptr;
		float micLevel = 0.;
		Ui::Animations::Simple micLevelAnimation;
		base::Timer levelUpdateTimer;
		bool generatingLink = false;
	};
	const auto peer = call->peer();
	const auto state = box->lifetime().make_state<State>();
	const auto real = peer->groupCall();
	const auto rtmp = call->rtmp();
	const auto id = call->id();
	const auto goodReal = (real && real->id() == id);

	const auto layout = box->verticalLayout();
	const auto &settings = Core::App().settings();

	const auto joinMuted = goodReal ? real->joinMuted() : false;
	const auto canChangeJoinMuted = !rtmp
		&& goodReal
		&& real->canChangeJoinMuted();
	const auto addCheck = (peer->canManageGroupCall() && canChangeJoinMuted);

	const auto addDivider = [&] {
		layout->add(object_ptr<Ui::BoxContentDivider>(
			layout,
			st::boxDividerHeight,
			st::groupCallDividerBg));
	};

	if (addCheck) {
		Ui::AddSkip(layout);
	}
	const auto muteJoined = addCheck
		? layout->add(object_ptr<Ui::SettingsButton>(
			layout,
			tr::lng_group_call_new_muted(),
			st::groupCallSettingsButton))->toggleOn(rpl::single(joinMuted))
		: nullptr;
	if (addCheck) {
		Ui::AddSkip(layout);
	}

	auto playbackIdWithFallback = Webrtc::DeviceIdValueWithFallback(
		Core::App().settings().callPlaybackDeviceIdValue(),
		Core::App().settings().playbackDeviceIdValue());
	AddButtonWithLabel(
		layout,
		tr::lng_group_call_speakers(),
		PlaybackDeviceNameValue(rpl::duplicate(playbackIdWithFallback)),
		st::groupCallSettingsButton
	)->addClickHandler([=] {
		box->getDelegate()->show(ChoosePlaybackDeviceBox(
			rpl::duplicate(playbackIdWithFallback),
			crl::guard(box, [=](const QString &id) {
				Core::App().settings().setCallPlaybackDeviceId(id);
				Core::App().saveSettingsDelayed();
			}),
			&st::groupCallCheckbox,
			&st::groupCallRadio));
	});

	if (!rtmp) {
		auto captureIdWithFallback = Webrtc::DeviceIdValueWithFallback(
			Core::App().settings().callCaptureDeviceIdValue(),
			Core::App().settings().captureDeviceIdValue());
		AddButtonWithLabel(
			layout,
			tr::lng_group_call_microphone(),
			CaptureDeviceNameValue(rpl::duplicate(captureIdWithFallback)),
			st::groupCallSettingsButton
		)->addClickHandler([=] {
			box->getDelegate()->show(ChooseCaptureDeviceBox(
				rpl::duplicate(captureIdWithFallback),
				crl::guard(box, [=](const QString &id) {
					Core::App().settings().setCallCaptureDeviceId(id);
					Core::App().saveSettingsDelayed();
				}),
				&st::groupCallCheckbox,
				&st::groupCallRadio));
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

		Ui::AddSkip(layout);
		//Ui::AddDivider(layout);
		//Ui::AddSkip(layout);

		layout->add(object_ptr<Ui::SettingsButton>(
			layout,
			tr::lng_group_call_noise_suppression(),
			st::groupCallSettingsButton
		))->toggleOn(rpl::single(
			settings.groupCallNoiseSuppression()
		))->toggledChanges(
		) | rpl::start_with_next([=](bool enabled) {
			Core::App().settings().setGroupCallNoiseSuppression(enabled);
			call->setNoiseSuppression(enabled);
			Core::App().saveSettingsDelayed();
		}, layout->lifetime());

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
			const auto pushToTalk = layout->add(
				object_ptr<Ui::SettingsButton>(
					layout,
					tr::lng_group_call_push_to_talk(),
					st::groupCallSettingsButton
			))->toggleOn(rpl::single(
				settings.groupCallPushToTalk()
			) | rpl::then(state->pushToTalkToggles.events()));
			const auto pushToTalkWrap = layout->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					layout,
					object_ptr<Ui::VerticalLayout>(layout)));
			const auto pushToTalkInner = pushToTalkWrap->entity();
			const auto recording = pushToTalkInner->add(
				object_ptr<Ui::SettingsButton>(
					pushToTalkInner,
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
				if (Core::App().settings().groupCallPushToTalkDelay() != delay) {
					Core::App().settings().setGroupCallPushToTalkDelay(delay);
					applyAndSave();
				}
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

		Ui::AddSkip(layout);
		//Ui::AddDivider(layout);
		//Ui::AddSkip(layout);
	}
	auto shareLink = Fn<void()>();
	if (peer->isChannel()
		&& peer->asChannel()->hasUsername()
		&& goodReal) {
		const auto showBox = crl::guard(box, [=](
				object_ptr<Ui::BoxContent> next) {
			box->getDelegate()->show(std::move(next));
		});
		const auto showToast = crl::guard(box, [=](QString text) {
			box->showToast(text);
		});
		auto [shareLinkCallback, shareLinkLifetime] = ShareInviteLinkAction(
			peer,
			box->uiShow());
		shareLink = std::move(shareLinkCallback);
		box->lifetime().add(std::move(shareLinkLifetime));
	} else {
		const auto lookupLink = [=] {
			if (const auto group = peer->asMegagroup()) {
				return group->hasUsername()
					? group->session().createInternalLinkFull(
						group->username())
					: group->inviteLink();
			} else if (const auto chat = peer->asChat()) {
				return chat->inviteLink();
			}
			return QString();
		};
		const auto canCreateLink = [&] {
			if (const auto chat = peer->asChat()) {
				return chat->canHaveInviteLink();
			} else if (const auto group = peer->asMegagroup()) {
				return group->canHaveInviteLink();
			}
			return false;
		};
		const auto alreadyHasLink = !lookupLink().isEmpty();
		if (alreadyHasLink || canCreateLink()) {
			if (!alreadyHasLink) {
				// Request invite link.
				peer->session().api().requestFullPeer(peer);
			}
			const auto copyLink = [=] {
				const auto link = lookupLink();
				if (link.isEmpty()) {
					return false;
				}
				QGuiApplication::clipboard()->setText(link);
				if (weakBox) {
					box->showToast(
						tr::lng_create_channel_link_copied(tr::now));
				}
				return true;
			};
			shareLink = [=] {
				if (!copyLink() && !state->generatingLink) {
					state->generatingLink = true;
					peer->session().api().inviteLinks().create({
						peer,
						crl::guard(layout, [=](auto&&) { copyLink(); })
					});
				}
			};
		}
	}
	if (shareLink) {
		layout->add(object_ptr<Ui::SettingsButton>(
			layout,
			tr::lng_group_call_share(),
			st::groupCallSettingsButton
		))->addClickHandler(std::move(shareLink));
	}
	if (rtmp && !call->rtmpInfo().url.isEmpty()) {
		Ui::AddSkip(layout);
		addDivider();
		Ui::AddSkip(layout);

		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
			mtpRequestId requestId;
			rpl::event_stream<RtmpInfo> data;
		};
		const auto top = box->addTopButton(st::groupCallMenuToggle);
		const auto state = top->lifetime().make_state<State>();
		const auto revokeSure = [=] {
			const auto session = &peer->session();
			state->requestId = session->api().request(
				MTPphone_GetGroupCallStreamRtmpUrl(
					peer->input,
					MTP_bool(true)
			)).done([=](const MTPphone_GroupCallStreamRtmpUrl &result) {
				auto data = result.match([&](
						const MTPDphone_groupCallStreamRtmpUrl &data) {
					return RtmpInfo{
						.url = qs(data.vurl()),
						.key = qs(data.vkey()),
					};
				});
				if (const auto call = weakCall.get()) {
					call->setRtmpInfo(data);
				}
				if (!top) {
					return;
				}
				state->requestId = 0;
				state->data.fire(std::move(data));
			}).fail([=] {
				state->requestId = 0;
			}).send();
		};
		const auto revoke = [=] {
			if (state->requestId || !top) {
				return;
			}
			box->getDelegate()->show(Ui::MakeConfirmBox({
				.text = tr::lng_group_call_rtmp_revoke_sure(),
				.confirmed = [=](Fn<void()> &&close) {
					revokeSure();
					close();
				},
				.confirmText = tr::lng_group_invite_context_revoke(),
				.labelStyle = &st::groupCallBoxLabel,
			}));
		};
		top->setClickedCallback([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				box,
				st::groupCallPopupMenu);
			state->menu->addAction(
				tr::lng_group_call_rtmp_revoke(tr::now),
				revoke);
			state->menu->setForcedOrigin(
				Ui::PanelAnimation::Origin::TopRight);
			top->setForceRippled(true);
			const auto raw = state->menu.get();
			raw->setDestroyedCallback([=] {
				if ((state->menu == raw) && top) {
					top->setForceRippled(false);
				}
			});
			state->menu->popup(
				top->mapToGlobal(QPoint(top->width() / 2, top->height())));
			return true;
		});

		StartRtmpProcess::FillRtmpRows(
			layout,
			false,
			box->uiShow(),
			state->data.events(),
			&st::groupCallBoxLabel,
			&st::groupCallSettingsRtmpShowButton,
			&st::groupCallSubsectionTitle,
			&st::groupCallAttentionBoxButton,
			&st::groupCallPopupMenu);
		state->data.fire(call->rtmpInfo());

		addDivider();
		Ui::AddSkip(layout);
	}
	if (rtmp) {
		const auto volumeItem = layout->add(
			object_ptr<MenuVolumeItem>(
				layout,
				st::groupCallVolumeSettings,
				st::groupCallVolumeSettingsSlider,
				call->otherParticipantStateValue(
				) | rpl::filter([=](const Group::ParticipantState &data) {
					return data.peer == peer;
				}),
				call->rtmpVolume(),
				Group::kMaxVolume,
				false,
				st::groupCallVolumeSettingsPadding));

		const auto toggleMute = crl::guard(layout, [=](bool m, bool local) {
			if (call) {
				call->toggleMute({
					.peer = peer,
					.mute = m,
					.locallyOnly = local,
				});
			}
		});
		const auto changeVolume = crl::guard(layout, [=](int v, bool local) {
			if (call) {
				call->changeVolume({
					.peer = peer,
					.volume = std::clamp(v, 1, Group::kMaxVolume),
					.locallyOnly = local,
				});
			}
		});

		volumeItem->toggleMuteLocallyRequests(
		) | rpl::start_with_next([=](bool muted) {
			toggleMute(muted, true);
		}, volumeItem->lifetime());

		volumeItem->changeVolumeLocallyRequests(
		) | rpl::start_with_next([=](int volume) {
			changeVolume(volume, true);
		}, volumeItem->lifetime());
	}

	if (call->canManage()) {
		layout->add(object_ptr<Ui::SettingsButton>(
			layout,
			(peer->isBroadcast()
				? tr::lng_group_call_end_channel()
				: tr::lng_group_call_end()),
			st::groupCallSettingsAttentionButton
		))->addClickHandler([=] {
			if (const auto call = weakCall.get()) {
				box->getDelegate()->show(Box(
					LeaveBox,
					call,
					true,
					BoxContext::GroupCallPanel));
				box->closeBox();
			}
		});
	}

	if (!rtmp) {
		box->setShowFinishedCallback([=] {
			// Means we finished showing the box.
			crl::on_main(box, [=] {
				state->deviceId = std::make_unique<Webrtc::DeviceResolver>(
					&Core::App().mediaDevices(),
					Webrtc::DeviceType::Capture,
					Webrtc::DeviceIdValueWithFallback(
						Core::App().settings().callCaptureDeviceIdValue(),
						Core::App().settings().captureDeviceIdValue()));
				state->micTester = std::make_unique<Webrtc::AudioInputTester>(
					state->deviceId->value());
				state->levelUpdateTimer.callEach(kMicTestUpdateInterval);
			});
		});
	}

	box->setTitle(tr::lng_group_call_settings_title());
	box->boxClosing(
	) | rpl::start_with_next([=] {
		if (canChangeJoinMuted
			&& muteJoined
			&& muteJoined->toggled() != joinMuted) {
			SaveCallJoinMuted(peer, id, muteJoined->toggled());
		}
	}, box->lifetime());
	box->addButton(tr::lng_box_done(), [=] {
		box->closeBox();
	});
}

std::pair<Fn<void()>, rpl::lifetime> ShareInviteLinkAction(
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::Show> show) {
	auto lifetime = rpl::lifetime();
	struct State {
		State(not_null<Main::Session*> session) : session(session) {
		}
		~State() {
			session->api().request(linkListenerRequestId).cancel();
			session->api().request(linkSpeakerRequestId).cancel();
		}

		not_null<Main::Session*> session;
		std::optional<QString> linkSpeaker;
		QString linkListener;
		mtpRequestId linkListenerRequestId = 0;
		mtpRequestId linkSpeakerRequestId = 0;
		bool generatingLink = false;
	};
	const auto state = lifetime.make_state<State>(&peer->session());
	if (!peer->canManageGroupCall()) {
		state->linkSpeaker = QString();
	}

	const auto shareReady = [=] {
		if (!state->linkSpeaker.has_value()
			|| state->linkListener.isEmpty()) {
			return false;
		}
		show->showBox(ShareInviteLinkBox(
			peer,
			*state->linkSpeaker,
			state->linkListener,
			show));
		return true;
	};
	auto callback = [=] {
		const auto real = peer->migrateToOrMe()->groupCall();
		if (shareReady() || state->generatingLink || !real) {
			return;
		}
		state->generatingLink = true;

		state->linkListenerRequestId = peer->session().api().request(
			MTPphone_ExportGroupCallInvite(
				MTP_flags(0),
				real->input()
			)
		).done([=](const MTPphone_ExportedGroupCallInvite &result) {
			state->linkListenerRequestId = 0;
			result.match([&](
				const MTPDphone_exportedGroupCallInvite &data) {
				state->linkListener = qs(data.vlink());
				shareReady();
			});
		}).send();

		if (real->rtmp()) {
			state->linkSpeaker = QString();
			state->linkSpeakerRequestId = 0;
			shareReady();
		} else if (!state->linkSpeaker.has_value()) {
			using Flag = MTPphone_ExportGroupCallInvite::Flag;
			state->linkSpeakerRequestId = peer->session().api().request(
				MTPphone_ExportGroupCallInvite(
					MTP_flags(Flag::f_can_self_unmute),
					real->input())
			).done([=](const MTPphone_ExportedGroupCallInvite &result) {
				state->linkSpeakerRequestId = 0;
				result.match([&](
						const MTPDphone_exportedGroupCallInvite &data) {
					state->linkSpeaker = qs(data.vlink());
					shareReady();
				});
			}).fail([=] {
				state->linkSpeakerRequestId = 0;
				state->linkSpeaker = QString();
				shareReady();
			}).send();
		}
	};
	return { std::move(callback), std::move(lifetime) };
}

MicLevelTester::MicLevelTester(Fn<void()> show)
: _show(std::move(show))
, _timer([=] { check(); })
, _deviceId(std::make_unique<Webrtc::DeviceResolver>(
	&Core::App().mediaDevices(),
	Webrtc::DeviceType::Capture,
	Webrtc::DeviceIdValueWithFallback(
		Core::App().settings().callCaptureDeviceIdValue(),
		Core::App().settings().captureDeviceIdValue())))
, _tester(std::make_unique<Webrtc::AudioInputTester>(_deviceId->value())) {
	_timer.callEach(kMicrophoneTooltipCheckInterval);
}

bool MicLevelTester::showTooltip() const {
	return (_loudCount >= kMicrophoneTooltipAfterLoudCount);
}

void MicLevelTester::check() {
	const auto level = _tester->getAndResetLevel();
	if (level >= kMicrophoneTooltipLevelThreshold) {
		_quietCount = 0;
		if (++_loudCount >= kMicrophoneTooltipAfterLoudCount) {
			_show();
		}
	} else if (_loudCount > 0 && ++_quietCount >= kDropLoudAfterQuietCount) {
		_quietCount = 0;
		_loudCount = 0;
	}
}

} // namespace Calls::Group
