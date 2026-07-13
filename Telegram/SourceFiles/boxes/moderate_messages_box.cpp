/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/moderate_messages_box.h"

#include "api/api_blocked_peers.h"
#include "api/api_chat_participants.h"
#include "api/api_messages_search.h"
#include "api/api_report.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/options.h"
#include "base/timer.h"
#include "boxes/delete_messages_box.h"
#include "boxes/peers/community_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_chat_participant_status.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rect_part.h"
#include "ui/text/text_lottie_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/expandable_peer_list.h"
#include "ui/widgets/participants_check_view.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "boxes/choose_filter_box.h"
#include "boxes/peer_list_box.h"
#include "main/main_session_settings.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "base/qt/qt_key_modifiers.h"
#include "ui/effects/round_checkbox.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"
#include "styles/style_info.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck

base::options::toggle ModerateCommonGroups({
	.id = kModerateCommonGroups,
	.name = "Ban users from several groups at once.",
});

const char kModerateCommonGroups[] = "moderate-common-groups";

namespace {

constexpr auto kModerateMessagesBoxAnimationDuration = crl::time(80);

struct ModerateOptions final {
	bool reportSpam = false;
	bool deleteAllMessages = false;
	bool deleteAllReactions = false;
	bool banOrRestrict = false;
	Participants participants;
};

[[nodiscard]] bool PeerCanDeleteMessages(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return chat->canDeleteMessages();
	}
	const auto channel = peer->asChannel();
	return channel && channel->canDeleteMessages();
}

[[nodiscard]] bool IsExcludedModerateParticipant(
		not_null<PeerData*> peer,
		not_null<PeerData*> participant) {
	if ((participant == peer) || participant->isSelf()) {
		return true;
	} else if (const auto channel = participant->asChannel()) {
		return (channel->discussionLink() == peer);
	}
	return false;
}

ModerateOptions CalculateModerateOptions(const HistoryItemsList &items) {
	Expects(!items.empty());

	auto result = ModerateOptions{
		.deleteAllMessages = true,
		.banOrRestrict = true,
	};

	const auto peer = items.front()->history()->peer;
	for (const auto &item : items) {
		if (!result.deleteAllMessages && !result.banOrRestrict) {
			return {};
		}
		if (peer != item->history()->peer) {
			return {};
		}
		{
			const auto author = item->author();
			if (author == peer) {
				return {};
			} else if (const auto channel = author->asChannel()) {
				if (channel->discussionLink() == peer) {
					return {};
				}
			}
		}
		if (!item->suggestBanReport()) {
			result.banOrRestrict = false;
		}
		if (!item->suggestDeleteAllReport()) {
			result.deleteAllMessages = false;
		}
		if (const auto p = item->from()) {
			if (!ranges::contains(result.participants, not_null{ p })) {
				result.participants.push_back(p);
			}
		}
	}
	result.deleteAllReactions = result.deleteAllMessages;
	result.reportSpam = result.deleteAllMessages || result.banOrRestrict;
	return result;
}

ModerateOptions CalculateModerateOptions(const ModerateReactionEntry &reaction) {
	auto result = ModerateOptions{
		.participants = { reaction.participant },
	};
	if (IsExcludedModerateParticipant(reaction.peer, reaction.participant)) {
		return result;
	}
	result.reportSpam = Api::GetReactionReportCapabilities(
		reaction.peer,
		reaction.participant
	).canReport || (reaction.peer->asChannel() != nullptr);
	result.deleteAllReactions = PeerCanDeleteMessages(reaction.peer);
	if (const auto channel = reaction.peer->asChannel()) {
		result.deleteAllMessages = channel->canDeleteMessages();
		result.banOrRestrict = channel->canRestrictParticipant(
			reaction.participant);
	}
	return result;
}

[[nodiscard]] bool HasModerateActions(const ModerateOptions &options) {
	return options.reportSpam
		|| options.deleteAllMessages
		|| options.deleteAllReactions
		|| options.banOrRestrict;
}

[[nodiscard]] TextWithEntities ParticipantsExpanderText(int count) {
	return tr::marked()
		.append(st::moderateBoxExpand)
		.append(QString::number(count));
}

[[nodiscard]] TextWithEntities DeleteOptionsExpanderText(
		int checkedCount,
		int totalCount) {
	return tr::marked(u"%1 / %2"_q.arg(checkedCount).arg(totalCount));
}

[[nodiscard]] rpl::producer<base::flat_map<PeerId, int>> MessagesCountValue(
		not_null<History*> history,
		std::vector<not_null<PeerData*>> from) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		struct State final {
			base::flat_map<PeerId, int> messagesCounts;
			int index = 0;
			rpl::lifetime apiLifetime;
		};
		const auto search = lifetime.make_state<Api::MessagesSearch>(history);
		const auto state = lifetime.make_state<State>();
		const auto send = [=](auto repeat) -> void {
			if (state->index >= from.size()) {
				consumer.put_next_copy(state->messagesCounts);
				return;
			}
			const auto peer = from[state->index];
			const auto peerId = peer->id;
			state->apiLifetime = search->messagesFounds(
			) | rpl::on_next([=](const Api::FoundMessages &found) {
				state->messagesCounts[peerId] = found.total;
				state->index++;
				repeat(repeat);
			});
			search->searchMessages({ .from = peer });
		};
		consumer.put_next({});
		send(send);

		return lifetime;
	};
}

using CommonGroups = std::vector<not_null<PeerData*>>;
using CollectCommon = std::shared_ptr<std::vector<PeerId>>;

void FillMenuModerateCommonGroups(
		not_null<Ui::PopupMenu*> menu,
		CommonGroups common,
		CollectCommon collectCommon,
		not_null<UserData*> user,
		Fn<void()> onDestroyedCallback) {
	const auto resultList
		= menu->lifetime().make_state<base::flat_set<PeerId>>();
	const auto rememberCheckbox = Ui::CreateChild<Ui::Checkbox>(
		menu,
		QString());
	auto multiline = base::make_unique_q<Ui::Menu::MultilineAction>(
		menu->menu(),
		menu->st().menu,
		st::historyHasCustomEmoji,
		st::historyHasCustomEmojiPosition,
		tr::lng_restrict_users_kick_from_common_group(tr::now, tr::rich));
	multiline->setAttribute(Qt::WA_TransparentForMouseEvents);
	menu->addAction(std::move(multiline));
	const auto session = &common.front()->session();
	const auto settingsOnStart = session->settings().moderateCommonGroups();
	const auto checkboxesUpdate = std::make_shared<rpl::event_stream<>>();
	const auto save = [=] {
		auto result = std::vector<PeerId>(
			resultList->begin(),
			resultList->end());
		*collectCommon = std::move(result);
	};
	for (const auto &group : common) {
		struct State {
			std::optional<Ui::RoundImageCheckbox> checkbox;
			Ui::RpWidget *checkboxWidget = nullptr;
		};
		auto item = base::make_unique_q<Ui::Menu::Action>(
			menu->menu(),
			menu->st().menu,
			Ui::Menu::CreateAction(
				menu->menu(),
				group->name(),
				[] {}),
			nullptr,
			nullptr);
		const auto state = item->lifetime().make_state<State>();
		const auto setChecked = [=, peerId = group->id](bool checked) {
			state->checkbox->setChecked(checked);
			if (state->checkbox->checked()) {
				resultList->insert(peerId);
			} else {
				resultList->erase(peerId);
			}
			save();
		};
		item->setActionTriggered([=] {
			setChecked(!state->checkbox->checked());
		});
		const auto raw = item.get();
		checkboxesUpdate->events() | rpl::on_next([=, peerId = group->id] {
			setChecked(ranges::contains(*collectCommon, peerId));
		}, raw->lifetime());
		state->checkboxWidget = Ui::CreateChild<Ui::RpWidget>(raw);
		state->checkboxWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
		state->checkboxWidget->resize(item->width() * 2, item->height());
		state->checkboxWidget->show();
		state->checkbox.emplace(
			st::moderateCommonGroupsCheckbox,
			[=] { state->checkboxWidget->update(); },
			PaintUserpicCallback(group, true),
			[=](int size) { return (group->isForum() || group->isMonoforum())
				? int(size * Ui::ForumUserpicRadiusMultiplier())
				: std::optional<int>(); });
		state->checkbox->setChecked(
			/*ranges::contains(
				session->settings().moderateCommonGroups(),
				group->id)
			|| */(collectCommon
				&& ranges::contains(*collectCommon, group->id)),
			anim::type::instant);
		state->checkboxWidget->paintOn([=](QPainter &p) {
			auto pp = Painter(state->checkboxWidget);
			state->checkbox->paint(
				pp,
				st::menuWithIcons.itemIconPosition.x(),
				st::menuWithIcons.itemIconPosition.y(),
				raw->width());
		});
		menu->addAction(std::move(item));
	}
	menu->addSeparator();
	if (const auto window = Core::App().findWindow(menu->parentWidget())) {
		auto hasActions = false;
		Ui::Menu::CreateAddActionCallback(menu)(Ui::Menu::MenuCallback::Args{
			.text = tr::lng_restrict_users_kick_from_common_group(tr::now),
			.handler = nullptr,
			.icon = &st::menuIconAddToFolder,
			.fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
				hasActions = FillChooseFilterWithAdminedGroupsMenu(
					window->sessionController(),
					menu,
					user,
					checkboxesUpdate,
					common,
					collectCommon);
			},
			.submenuSt = &st::foldersMenu,
		});
		if (!hasActions) {
			menu->removeAction(menu->actions().size() - 1);
			menu->removeAction(menu->actions().size() - 1); // Separator.
		}
	}
	menu->addSeparator();
	{
		auto item = base::make_unique_q<Ui::Menu::Action>(
			menu->menu(),
			menu->st().menu,
			Ui::Menu::CreateAction(
				menu->menu(),
				tr::lng_remember(tr::now),
				[] {}),
			nullptr,
			nullptr);
		item->setPreventClose(true);
		item->setActionTriggered([=] {
			rememberCheckbox->setChecked(!rememberCheckbox->checked());
		});
		rememberCheckbox->setParent(item.get());
		rememberCheckbox->setAttribute(Qt::WA_TransparentForMouseEvents);
		rememberCheckbox->move(st::lineWidth * 8, -st::lineWidth * 2);
		rememberCheckbox->show();
		menu->addAction(std::move(item));
	}
	menu->setDestroyedCallback([=] {
		onDestroyedCallback();
		if (!rememberCheckbox->checked()) {
			session->settings().setModerateCommonGroups(settingsOnStart);
			session->saveSettingsDelayed();
		}
	});
}

void ProccessCommonGroups(
		const HistoryItemsList &items,
		Fn<void(CommonGroups, not_null<UserData*>)> processHas) {
	const auto moderateOptions = CalculateModerateOptions(items);
	if (moderateOptions.participants.size() != 1
		|| !moderateOptions.banOrRestrict) {
		return;
	}
	const auto participant = moderateOptions.participants.front();
	const auto user = participant->asUser();
	if (!user) {
		return;
	}
	const auto currentGroupId = items.front()->history()->peer->id;
	user->session().api().requestBotCommonGroups(user, [=] {
		const auto commonGroups = user->session().api().botCommonGroups(user);
		if (!commonGroups || commonGroups->empty()) {
			return;
		}

		auto filtered = CommonGroups();
		for (const auto &group : *commonGroups) {
			if (group->id == currentGroupId) {
				continue;
			}
			const auto channel = group->asChannel();
			if (channel && channel->canRestrictParticipant(user)) {
				if (channel->isGroupAdmin(user) && !channel->amCreator()) {
					continue;
				}
				filtered.push_back(group);
			}
		}

		if (!filtered.empty()) {
			processHas(filtered, user);
		}
	});
}

} // namespace

void CreateModerateMessagesBox(
		not_null<Ui::GenericBox*> box,
		ModerateMessagesBoxEntry entry,
		Fn<void()> confirmed,
		ModerateMessagesBoxOptions options) {
	const auto &items = entry.items;
	const auto reaction = entry.reaction;
	Expects(!items.empty() || reaction.has_value());

	box->setLayerAnimationDuration(kModerateMessagesBoxAnimationDuration);

	const auto hasItems = !items.empty();
	const auto hasReaction = reaction.has_value();
	const auto itemsCount = hasItems ? int(items.size()) : 0;

	using Controller = Ui::ExpandablePeerListController;

	const auto moderateOptions = hasItems
		? CalculateModerateOptions(items)
		: CalculateModerateOptions(*reaction);
	const auto reportSpam = moderateOptions.reportSpam;
	const auto deleteAllMessages = moderateOptions.deleteAllMessages;
	const auto deleteAllReactions = moderateOptions.deleteAllReactions;
	const auto banOrRestrict = moderateOptions.banOrRestrict;
	const auto &participants = moderateOptions.participants;
	const auto inner = box->verticalLayout();

	Assert(!participants.empty());

	const auto confirms = inner->lifetime().make_state<rpl::event_stream<>>();
	const auto collectCommon = std::make_shared<std::vector<PeerId>>();

	const auto isSingle = participants.size() == 1;
	const auto buttonPadding = isSingle
		? QMargins()
		: QMargins(
			0,
			0,
			Ui::ExpanderButton::ComputeSize(
				ParticipantsExpanderText(int(participants.size()))).width(),
			0);

	const auto firstItem = hasItems ? items.front().get() : nullptr;
	const auto session = hasItems
		? &firstItem->history()->session()
		: &reaction->peer->session();
	const auto peer = hasItems
		? firstItem->history()->peer
		: reaction->peer;
	const auto history = hasItems
		? firstItem->history().get()
		: session->data().historyLoaded(peer);
	const auto historyPeerId = peer->id;
	const auto ids = hasItems
		? session->data().itemsToIds(items)
		: MessageIdsList{ FullMsgId(reaction->peer->id, reaction->msgId) };
	const auto selectedMessagesByParticipant = [&] {
		auto result = base::flat_map<PeerId, int>();
		if (!hasItems && !hasReaction) {
			return result;
		}
		if (hasItems) {
			for (const auto &item : items) {
				const auto from = item->from();
				if (!from) {
					continue;
				}
				const auto i = result.find(from->id);
				if (i == result.end()) {
					result.emplace(from->id, 1);
				} else {
					++i->second;
				}
			}
		} else {
			result.emplace(reaction->participant->id, 1);
		}
		return result;
	}();
	const auto participantIds = ranges::views::all(
		participants
	) | ranges::views::transform([](not_null<PeerData*> peer) {
		return peer->id;
	}) | ranges::to_vector;

	if (hasItems) {
		const auto remainingIds
			= box->lifetime().make_state<base::flat_set<FullMsgId>>(
				ids.begin(),
				ids.end());
		session->data().itemRemoved(
		) | rpl::on_next([=](not_null<const HistoryItem*> item) {
			remainingIds->erase(item->fullId());
			if (remainingIds->empty()) {
				box->closeBox();
			}
		}, box->lifetime());
	}

	if (hasItems
		&& (ModerateCommonGroups.value() || session->supportMode())) {
	ProccessCommonGroups(
		items,
		crl::guard(box, [=](CommonGroups groups, not_null<UserData*> user) {
			using namespace Ui;
			const auto top = box->addTopButton(st::infoTopBarMenu);
			auto &lifetime = top->lifetime();
			const auto menu
				= lifetime.make_state<base::unique_qptr<Ui::PopupMenu>>();

			{
				const auto was = collectCommon->size();
				*menu = base::make_unique_q<Ui::PopupMenu>(
					top,
					st::popupMenuExpandedSeparator);
				FillMenuModerateCommonGroups(
					*menu,
					groups,
					collectCommon,
					user,
					[]{});
				*menu = nullptr;
				if (was != collectCommon->size()) {
					top->setIconOverride(
						&st::infoTopBarMenuActive,
						&st::infoTopBarMenuActive);
					const auto minicheck = Ui::CreateChild<Ui::RpWidget>(top);
					minicheck->paintRequest() | rpl::on_next([=] {
						auto p = Painter(minicheck);
						const auto rect = minicheck->rect();
						const auto iconSize = QSize(
							st::mediaPlayerMenuCheck.width(),
							st::mediaPlayerMenuCheck.height());
						const auto scale = std::min(
							rect.width() / float64(iconSize.width()),
							rect.height() / float64(iconSize.height()));
						if (scale < 1.0) {
							p.save();
							p.translate(rect.center());
							p.scale(scale, scale);
							p.translate(-rect.center());
						}
						st::mediaPlayerMenuCheck.paintInCenter(
							p,
							rect,
							st::windowActiveTextFg->c);
						if (scale < 1.0) {
							p.restore();
						}
					}, minicheck->lifetime());
					minicheck->resize(
						st::mediaPlayerMenuCheck.width() / 1.5,
						st::mediaPlayerMenuCheck.width() / 1.5);
					minicheck->show();
					minicheck->moveToLeft(
						top->width() - st::lineWidth * 26,
						top->height() - st::lineWidth * 29);
				}
			}

			top->setClickedCallback([=] {
				top->setForceRippled(true);
				*menu = base::make_unique_q<Ui::PopupMenu>(
					top,
					st::popupMenuExpandedSeparator);
				const auto onDestroyedCallback = [=, weak = top] {
					if (const auto strong = weak.data()) {
						strong->setForceRippled(false);
					}
				};
				FillMenuModerateCommonGroups(
					*menu,
					groups,
					collectCommon,
					user,
					onDestroyedCallback);
				(*menu)->setForcedOrigin(PanelAnimation::Origin::TopRight);
				const auto point = QPoint(top->width(), top->height());
				(*menu)->popup(top->mapToGlobal(point));
			});
		}));
	}

	using Request = Fn<void(not_null<PeerData*>, not_null<ChannelData*>)>;
	const auto sequentiallyRequest = [=](
			Request request,
			Participants participants,
			std::optional<std::vector<PeerId>> channelIds = {}) {
		constexpr auto kSmallDelayMs = 5;
		const auto participantIds = ranges::views::all(
			participants
		) | ranges::views::transform([](not_null<PeerData*> peer) {
			return peer->id;
		}) | ranges::to_vector;
		const auto channelIdList = channelIds.value_or(
			std::vector<PeerId>{ historyPeerId });
		const auto lifetime = std::make_shared<rpl::lifetime>();
		const auto participantIndex = lifetime->make_state<int>(0);
		const auto channelIndex = lifetime->make_state<int>(0);
		const auto timer = lifetime->make_state<base::Timer>();
		timer->setCallback(crl::guard(session, [=] {
			if ((*participantIndex) < participantIds.size()) {
				if ((*channelIndex) < channelIdList.size()) {
					const auto from = session->data().peer(
						participantIds[*participantIndex]);
					const auto channel = session->data().peer(
						channelIdList[*channelIndex])->asChannel();
					if (from && channel) {
						request(from, channel);
					}
					(*channelIndex)++;
				} else {
					(*participantIndex)++;
					*channelIndex = 0;
				}
			} else {
				lifetime->destroy();
			}
		}));
		timer->callEach(kSmallDelayMs);
	};

	const auto handleConfirmation = [=](
			not_null<Ui::Checkbox*> checkbox,
			not_null<Controller*> controller,
			Request request) {
		confirms->events() | rpl::on_next([=] {
			if (checkbox->checked() && controller->collectRequests) {
				sequentiallyRequest(request, controller->collectRequests());
			}
		}, checkbox->lifetime());
	};

	const auto isEnter = [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::KeyPress) {
			if (const auto k = static_cast<QKeyEvent*>(event.get())) {
				return (k->key() == Qt::Key_Enter)
					|| (k->key() == Qt::Key_Return);
			}
		}
		return false;
	};

	base::install_event_filter(box, [=](not_null<QEvent*> event) {
		if (isEnter(event)) {
			box->triggerButton(0);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	const auto handleSubmitionIf = [=](Fn<bool()> enabled) {
		base::install_event_filter(box, [=, enabled = std::move(enabled)](
				not_null<QEvent*> event) {
			if (!isEnter(event) || !enabled()) {
				return base::EventFilterResult::Continue;
			}
			box->uiShow()->show(Ui::MakeConfirmBox({
				.text = tr::lng_gigagroup_warning_title(),
				.confirmed = [=](Fn<void()> close) {
					box->triggerButton(0);
					close();
				},
				.confirmText = tr::lng_box_yes(),
				.cancelText = tr::lng_box_no(),
			}));
			return base::EventFilterResult::Cancel;
		});
	};
	const auto handleSubmition = [=](not_null<Ui::Checkbox*> checkbox) {
		handleSubmitionIf([=] {
			return checkbox->checked();
		});
	};
	Ui::Checkbox *deleteOptions = nullptr;
	Ui::Checkbox *deleteMessages = nullptr;
	Controller *deleteMessagesController = nullptr;
	rpl::variable<base::flat_map<PeerId, int>> *deleteMessagesCounts = nullptr;
	Ui::Checkbox *deleteReactions = nullptr;
	Controller *deleteReactionsController = nullptr;
	const auto effectiveCheckedParticipants = [](
			Ui::Checkbox *checkbox,
			Controller *controller) {
		if (!checkbox || !controller || !controller->collectRequests) {
			return Participants();
		} else if (!checkbox->checked()
			&& (controller->data.participants.size() == 1)) {
			return Participants();
		}
		return controller->collectRequests();
	};
	const auto checkedParticipantsValue = [=](
			not_null<Ui::Checkbox*> checkbox,
			not_null<Controller*> controller)
				-> rpl::producer<Participants> {
		if (controller->data.participants.size() == 1) {
			return checkbox->checkedValue() | rpl::map([=](bool) {
				return effectiveCheckedParticipants(checkbox, controller);
			});
		}
		return rpl::merge(
			rpl::single(false),
			controller->checkAllRequests.events(),
			controller->toggleRequestsFromInner.events()
		) | rpl::map([=](bool) {
			return effectiveCheckedParticipants(checkbox, controller);
		});
	};

	const auto subtitle = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				QString(),
				st::boxLabel)));
	subtitle->entity()->setTextColorOverride(st::windowSubTextFg->c);
	subtitle->hide(anim::type::instant);
	Ui::AddSkip(inner);
	if (reportSpam) {
		const auto report = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_report_spam(tr::now),
				options.reportSpam,
				st::defaultBoxCheckbox),
			st::boxRowPadding + buttonPadding);
		const auto controller = box->lifetime().make_state<Controller>(
			Controller::Data{ .participants = participants });
		Ui::AddExpandablePeerList(report, controller, inner);
		handleSubmition(report);

		const auto show = box->uiShow();
		handleConfirmation(report, controller, [=](
				not_null<PeerData*> p,
				not_null<ChannelData*> c) {
			if (reaction.has_value()
				&& Api::GetReactionReportCapabilities(
					reaction->peer,
					p
				).canReport) {
				Api::ReportReaction(
					show,
					reaction->peer,
					reaction->msgId,
					p);
			} else {
				Api::ReportSpam(p, ids);
			}
		});
	}

	const auto showMessagesCheckbox = deleteAllMessages;
	const auto showReactionsCheckbox = deleteAllReactions;
	const auto useSingleDeleteOptions = isSingle
		&& showMessagesCheckbox
		&& showReactionsCheckbox;
	if (showMessagesCheckbox || showReactionsCheckbox) {
		Ui::AddSkip(inner);
		Ui::AddSkip(inner);
		const auto checkedParticipants = options.deleteAll
			? participantIds
			: std::vector<PeerId>();

		if (useSingleDeleteOptions) {
			const auto participant = participants.front();
			Assert(history != nullptr);
			deleteMessagesCounts = box->lifetime().make_state<
				rpl::variable<base::flat_map<PeerId, int>>>(
					base::flat_map<PeerId, int>());
			MessagesCountValue(
				history,
				participants
			) | rpl::on_next([=](base::flat_map<PeerId, int> counts) {
				deleteMessagesCounts->force_assign(std::move(counts));
			}, box->lifetime());
			deleteMessagesController = box->lifetime().make_state<Controller>(
				Controller::Data{
					.messagesCounts = deleteMessagesCounts->value(),
					.participants = Participants{ participant },
					.checked = checkedParticipants,
				});
			deleteReactionsController = box->lifetime().make_state<Controller>(
				Controller::Data{
					.participants = Participants{ participant },
					.checked = checkedParticipants,
				});

			const auto deleteOptionsSize = Ui::ExpanderButton::ComputeSize(
				DeleteOptionsExpanderText(2, 2));
			const auto deleteOptionsPadding = QMargins(
				0,
				0,
				deleteOptionsSize.width(),
				0);
			deleteOptions = inner->add(
				object_ptr<Ui::Checkbox>(
					inner,
					tr::lng_delete_all_from_user(
						lt_user,
						rpl::single(participant->shortName())),
					options.deleteAll,
					st::defaultBoxCheckbox),
				st::boxRowPadding + deleteOptionsPadding);
			const auto button = Ui::CreateChild<Ui::ExpanderButton>(
				inner,
				DeleteOptionsExpanderText(2, 2));
			button->resize(deleteOptionsSize);
			deleteOptions->geometryValue(
			) | rpl::on_next([=](const QRect &rect) {
				button->moveToRight(
					st::moderateBoxExpandRight,
					rect.top() + (rect.height() - button->height()) / 2,
					inner->width());
				button->raise();
			}, button->lifetime());

			const auto wrap = inner->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					inner,
					object_ptr<Ui::VerticalLayout>(inner)));
			wrap->toggle(false, anim::type::instant);
			button->setClickedCallback([=] {
				button->checkView()->setChecked(
					!button->checkView()->checked(),
					anim::type::normal);
				wrap->toggle(
					button->checkView()->checked(),
					anim::type::normal);
			});

			const auto container = wrap->entity();
			const auto optionCheckRect = deleteOptions->checkRect();
			const auto childOptionPadding = st::boxRowPadding
				+ QMargins(
					optionCheckRect.width()
						+ st::defaultBoxCheckbox.textPosition.x()
						- optionCheckRect.x(),
					0,
					0,
					0);
			Ui::AddSkip(container);
			Ui::AddSkip(container);
			deleteMessages = container->add(
				object_ptr<Ui::Checkbox>(
					container,
					tr::lng_delete_sub_messages(tr::now),
					options.deleteAll,
					st::defaultBoxCheckbox),
				childOptionPadding);
			Ui::AddSkip(container);
			Ui::AddSkip(container);
			deleteReactions = container->add(
				object_ptr<Ui::Checkbox>(
					container,
					tr::lng_delete_sub_reactions(tr::now),
					options.deleteAll,
					st::defaultBoxCheckbox),
				childOptionPadding);
			deleteMessagesController->collectRequests = [=] {
				return deleteMessages->checked()
					? Participants{ participant }
					: Participants();
			};
			deleteReactionsController->collectRequests = [=] {
				return deleteReactions->checked()
					? Participants{ participant }
					: Participants();
			};
			const auto updateDeleteOptions = [=] {
				const auto count = (deleteMessages->checked() ? 1 : 0)
					+ (deleteReactions->checked() ? 1 : 0);
				deleteOptions->setChecked(
					count == 2,
					Ui::Checkbox::NotifyAboutChange::DontNotify);
				button->setText(DeleteOptionsExpanderText(count, 2));
			};
			deleteOptions->checkedChanges(
			) | rpl::on_next([=](bool checked) {
				deleteMessages->setChecked(checked);
				deleteReactions->setChecked(checked);
				updateDeleteOptions();
			}, deleteOptions->lifetime());
			deleteMessages->checkedChanges(
			) | rpl::on_next(updateDeleteOptions, deleteMessages->lifetime());
			deleteReactions->checkedChanges(
			) | rpl::on_next(updateDeleteOptions, deleteReactions->lifetime());
			updateDeleteOptions();
			handleSubmitionIf([=] {
				return deleteMessages->checked()
					|| deleteReactions->checked();
			});
			handleConfirmation(
				not_null{ deleteMessages },
				not_null{ deleteMessagesController },
				[=](
					not_null<PeerData*> p,
					not_null<ChannelData*> c) {
					p->session().api().deleteAllFromParticipant(c, p);
			});
			confirms->events() | rpl::on_next([=] {
				if (deleteReactions->checked()
					&& deleteReactionsController->collectRequests
					&& !effectiveCheckedParticipants(
						deleteReactions,
						deleteReactionsController).empty()) {
					for (const auto &participant
							: deleteReactionsController->collectRequests()) {
						const auto useOriginReaction = reaction
							&& (participant == reaction->participant);
						const auto originMsgId = useOriginReaction
							? reaction->msgId
							: MsgId();
						const auto originReaction = useOriginReaction
							? reaction->reaction
							: Data::ReactionId();
						peer->session().api().deleteAllReactionsFromParticipant(
							peer,
							participant,
							originMsgId,
							originReaction);
					}
				}
			}, deleteReactions->lifetime());
		} else {
			if (showMessagesCheckbox) {
				Assert(history != nullptr);
				deleteMessagesCounts = box->lifetime().make_state<
					rpl::variable<base::flat_map<PeerId, int>>>(
						base::flat_map<PeerId, int>());
				MessagesCountValue(
					history,
					participants
				) | rpl::on_next([=](base::flat_map<PeerId, int> counts) {
					deleteMessagesCounts->force_assign(std::move(counts));
				}, box->lifetime());
				deleteMessagesController = box->lifetime().make_state<Controller>(
					Controller::Data{
						.messagesCounts = deleteMessagesCounts->value(),
						.participants = participants,
						.checked = checkedParticipants,
					});
				deleteMessages = inner->add(
					object_ptr<Ui::Checkbox>(
						inner,
						tr::lng_delete_sub_messages(tr::now),
						options.deleteAll,
						st::defaultBoxCheckbox),
					st::boxRowPadding + buttonPadding);
				Ui::AddExpandablePeerList(
					not_null{ deleteMessages },
					not_null{ deleteMessagesController },
					inner);
				handleSubmition(not_null{ deleteMessages });
				handleConfirmation(
					not_null{ deleteMessages },
					not_null{ deleteMessagesController },
					[=](
						not_null<PeerData*> p,
						not_null<ChannelData*> c) {
						p->session().api().deleteAllFromParticipant(c, p);
				});
			}

			if (deleteMessages && showReactionsCheckbox) {
				Ui::AddSkip(inner);
				Ui::AddSkip(inner);
			}

			if (showReactionsCheckbox) {
				deleteReactionsController = box->lifetime().make_state<Controller>(
					Controller::Data{
						.participants = participants,
						.checked = checkedParticipants,
					});
				deleteReactions = inner->add(
					object_ptr<Ui::Checkbox>(
						inner,
						tr::lng_delete_sub_reactions(tr::now),
						options.deleteAll,
						st::defaultBoxCheckbox),
					st::boxRowPadding + buttonPadding);
				Ui::AddExpandablePeerList(
					not_null{ deleteReactions },
					not_null{ deleteReactionsController },
					inner);
				handleSubmition(not_null{ deleteReactions });
				confirms->events() | rpl::on_next([=] {
					if (deleteReactions->checked()
						&& deleteReactionsController->collectRequests
						&& !effectiveCheckedParticipants(
							deleteReactions,
							deleteReactionsController).empty()) {
						for (const auto &participant
								: deleteReactionsController->collectRequests()) {
							const auto useOriginReaction = reaction
								&& (participant == reaction->participant);
							peer->session().api()
								.deleteAllReactionsFromParticipant(
									peer,
									participant,
									useOriginReaction ? reaction->msgId : MsgId(),
									useOriginReaction
										? reaction->reaction
										: Data::ReactionId());
						}
					}
				}, deleteReactions->lifetime());
			}
		}
	}
	const auto makeTitleLoadingDescriptor = [] {
		return Lottie::IconDescriptor{
			.name = u"transcribe_loading"_q,
			.color = &st::attentionButtonFg,
			.sizeOverride = Size(st::historyTranscribeLoadingSize),
			.colorizeUsingAlpha = true,
		};
	};
	const auto titleLoadingEmojiData = Ui::Text::LottieEmojiData(
		makeTitleLoadingDescriptor());
	struct MessageTitleData final {
		int count = 0;
		bool resolved = false;
	};
	const auto baseMessagesCount = int(ids.size());
	const auto langUpdated = rpl::single(
		0
	) | rpl::then(Lang::Updated() | rpl::map([] {
		return 0;
	}));
	const auto makeMessageTitleData = [=](
			const base::flat_map<PeerId, int> &messagesCounts,
			const Participants &checked) {
		auto result = MessageTitleData{
			.count = baseMessagesCount,
			.resolved = true,
		};
		for (const auto &peer : checked) {
			const auto i = messagesCounts.find(peer->id);
			if (i == end(messagesCounts)) {
				result.resolved = false;
			} else {
				result.count += i->second;
			}
			if (const auto j = selectedMessagesByParticipant.find(peer->id);
				j != end(selectedMessagesByParticipant)) {
				result.count -= j->second;
			}
		}
		return result;
	};
	auto title = [&]() -> rpl::producer<TextWithEntities> {
		if (showMessagesCheckbox && !(hasReaction && !hasItems)) {
			auto messageTitleData = rpl::combine(
				deleteMessagesCounts->value(),
				checkedParticipantsValue(
					not_null{ deleteMessages },
					not_null{ deleteMessagesController })
			) | rpl::map(makeMessageTitleData);
			return rpl::combine(
				std::move(messageTitleData),
				rpl::duplicate(langUpdated)
			) | rpl::map([=](const MessageTitleData &data, int) {
				const auto count = data.count;
				const auto resolved = data.resolved;
				const auto text = (count == 1)
					? tr::lng_delete_title_message_one(tr::now)
					: tr::lng_delete_title_message_many(
						tr::now,
						lt_count,
						count);
				if (resolved || count != 0) {
					return TextWithEntities{ text };
				}
				const auto zeroIndex = text.indexOf('0');
				return (zeroIndex == -1)
					? TextWithEntities{ text }
					: TextWithEntities()
						.append(text.mid(0, zeroIndex))
						.append(Ui::Text::LottieEmoji(
							makeTitleLoadingDescriptor()))
						.append(text.mid(zeroIndex + 1));
			});
		} else if (hasReaction && showMessagesCheckbox) {
			auto messageTitleData = rpl::combine(
				deleteMessagesCounts->value(),
				checkedParticipantsValue(
					not_null{ deleteMessages },
					not_null{ deleteMessagesController })
			) | rpl::map(makeMessageTitleData);
			auto deleteReactionsChecked = deleteReactions
				? deleteReactions->checkedValue()
				: rpl::single(false);
			return rpl::combine(
				deleteMessages->checkedValue(),
				std::move(messageTitleData),
				std::move(deleteReactionsChecked),
				rpl::duplicate(langUpdated)
			) | rpl::map([=](
					bool deleteMessagesChecked,
					const MessageTitleData &data,
					bool deleteReactionsChecked,
					int) {
				if (!deleteMessagesChecked) {
					return TextWithEntities{ deleteReactionsChecked
						? tr::lng_delete_title_reaction_all(tr::now)
						: tr::lng_delete_title_reaction_this(tr::now) };
				}
				const auto count = data.count;
				const auto resolved = data.resolved;
				const auto text = (count == 1)
					? tr::lng_delete_title_message_one(tr::now)
					: tr::lng_delete_title_message_many(
						tr::now,
						lt_count,
						count);
				if (resolved || count != 0) {
					return TextWithEntities{ text };
				}
				const auto zeroIndex = text.indexOf('0');
				return (zeroIndex == -1)
					? TextWithEntities{ text }
					: TextWithEntities()
						.append(text.mid(0, zeroIndex))
						.append(Ui::Text::LottieEmoji(
							makeTitleLoadingDescriptor()))
						.append(text.mid(zeroIndex + 1));
			});
		} else if (hasItems) {
			return rpl::duplicate(langUpdated) | rpl::map([=](int) {
				return (itemsCount == 1)
					? TextWithEntities{
						tr::lng_delete_title_message_one(tr::now)
					}
					: TextWithEntities{
						tr::lng_delete_title_message_many(
							tr::now,
							lt_count,
							itemsCount)
					};
			});
		} else if (deleteReactions) {
			return rpl::combine(
				deleteReactions->checkedValue(),
				rpl::duplicate(langUpdated)
			) | rpl::map([=](bool checked, int) {
				return TextWithEntities{ checked
					? tr::lng_delete_title_reaction_all(tr::now)
					: tr::lng_delete_title_reaction_this(tr::now) };
			});
		}
		return rpl::duplicate(langUpdated) | rpl::map([](int) {
			return TextWithEntities{
				tr::lng_delete_title_reaction_this(tr::now)
			};
		});
	}();
	auto titleContext = Core::TextContext({ .session = session });
	auto titleFactory = std::move(titleContext.customEmojiFactory);
	titleContext.customEmojiFactory = [
		titleFactory = std::move(titleFactory),
		titleLoadingEmojiData,
		makeTitleLoadingDescriptor
	](QStringView data, const Ui::Text::MarkedContext &context)
			-> std::unique_ptr<Ui::Text::CustomEmoji> {
		if (data == titleLoadingEmojiData) {
			return std::make_unique<Ui::Text::LottieCustomEmoji>(
				makeTitleLoadingDescriptor(),
				context.repaint);
		}
		return titleFactory(data, context);
	};
	box->getDelegate()->setTitle(std::move(title), std::move(titleContext));
	enum class SubtitleKind {
		None,
		ThisReaction,
		SomeReactions,
		AllReactions,
	};
	if (hasItems || (hasReaction && showMessagesCheckbox)) {
		const auto subtitleKind = box->lifetime().make_state<
			rpl::variable<SubtitleKind>>(SubtitleKind::None);
		auto reactionsCheckedValue = showReactionsCheckbox
			? checkedParticipantsValue(
				not_null{ deleteReactions },
				not_null{ deleteReactionsController })
			: rpl::single(Participants());
		auto messageTitleShownValue = [&] {
			return hasItems
				? rpl::single(true)
				: (hasReaction && showMessagesCheckbox)
				? deleteMessages->checkedValue()
				: rpl::single(false);
		}();
		rpl::combine(
			subtitleKind->value(),
			rpl::duplicate(langUpdated)
		) | rpl::map([](SubtitleKind kind, int) {
			switch (kind) {
			case SubtitleKind::ThisReaction:
				return tr::lng_delete_label_also_this_reaction(tr::now);
			case SubtitleKind::SomeReactions:
				return tr::lng_delete_label_also_some_reactions(tr::now);
			case SubtitleKind::AllReactions:
				return tr::lng_delete_label_also_all_reactions(tr::now);
			case SubtitleKind::None:
				return QString();
			}
			Unexpected("Bad subtitle kind.");
		}) | rpl::on_next([=](const QString &text) {
			subtitle->entity()->setText(text);
		}, subtitle->lifetime());
		rpl::combine(
			std::move(reactionsCheckedValue),
			std::move(messageTitleShownValue)
		) | rpl::on_next([=](
				const Participants &checked,
				bool messageTitleShown) {
			auto kind = SubtitleKind::None;
			if (messageTitleShown) {
				if (!checked.empty()) {
					kind = (checked.size() == participants.size())
						? SubtitleKind::AllReactions
						: SubtitleKind::SomeReactions;
				} else if (hasReaction) {
					kind = SubtitleKind::ThisReaction;
				}
			}
			subtitleKind->force_assign(kind);
			subtitle->toggle(kind != SubtitleKind::None, anim::type::normal);
		}, subtitle->lifetime());
	}
	if (banOrRestrict) {
		auto ownedWrap = peer->isMonoforum()
			? nullptr
			: object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner));
		auto computeRestrictions = Fn<ChatRestrictions()>();
		const auto wrap = ownedWrap.data();

		Ui::AddSkip(inner);
		Ui::AddSkip(inner);
		const auto ban = inner->add(
			object_ptr<Ui::Checkbox>(
				box,
				rpl::conditional(
					(ownedWrap
						? ownedWrap->toggledValue()
						: rpl::single(false) | rpl::type_erased),
					tr::lng_restrict_user(
						lt_count,
						rpl::single(participants.size()) | tr::to_count()),
					rpl::conditional(
						rpl::single(isSingle),
						tr::lng_ban_specific_user(
							lt_user,
							rpl::single(participants.front()->shortName())),
						tr::lng_ban_users())),
				options.banUser,
				st::defaultBoxCheckbox),
			st::boxRowPadding + buttonPadding);
		const auto controller = box->lifetime().make_state<Controller>(
			Controller::Data{ .participants = participants });
		Ui::AddExpandablePeerList(ban, controller, inner);
		handleSubmition(ban);

		Ui::AddSkip(inner);
		Ui::AddSkip(inner);

		if (ownedWrap) {
			inner->add(std::move(ownedWrap));

			const auto container = wrap->entity();
			wrap->toggle(false, anim::type::instant);

			const auto emojiUp = Ui::Text::IconEmoji(
				&st::moderateBoxExpandIcon);
			const auto emojiDown = Ui::Text::IconEmoji(
				&st::moderateBoxExpandIconDown);

			auto label = object_ptr<Ui::FlatLabel>(
				inner,
				QString(),
				st::moderateBoxDividerLabel);
			const auto raw = label.data();

			auto &lifetime = wrap->lifetime();
			const auto scrollLifetime = lifetime.make_state<rpl::lifetime>();
			label->setClickHandlerFilter([=](
					const ClickHandlerPtr &handler,
					Qt::MouseButton button) {
				if (button != Qt::LeftButton) {
					return false;
				}
				wrap->toggle(!wrap->toggled(), anim::type::normal);
				{
					inner->heightValue() | rpl::on_next([=] {
						if (!wrap->animating()) {
							scrollLifetime->destroy();
							Ui::PostponeCall(crl::guard(box, [=] {
								box->scrollToY(std::numeric_limits<int>::max());
							}));
						} else {
							box->scrollToY(std::numeric_limits<int>::max());
						}
					}, *scrollLifetime);
				}
				return true;
			});
			wrap->toggledValue(
			) | rpl::map([isSingle, emojiUp, emojiDown](bool toggled) {
				return ((toggled && isSingle)
					? tr::lng_restrict_user_full
					: (toggled && !isSingle)
					? tr::lng_restrict_users_full
					: isSingle
					? tr::lng_restrict_user_part
					: tr::lng_restrict_users_part)(
						lt_emoji,
						rpl::single(toggled ? emojiUp : emojiDown),
						tr::marked);
			}) | rpl::flatten_latest(
			) | rpl::on_next([=](const TextWithEntities &text) {
				raw->setMarkedText(tr::link(text, u"internal:"_q));
			}, label->lifetime());

			Ui::AddSkip(inner);
			inner->add(object_ptr<Ui::DividerLabel>(
				inner,
				std::move(label),
				st::defaultBoxDividerLabelPadding));

			using Flag = ChatRestriction;
			using Flags = ChatRestrictions;
			const auto chat = peer->asChat();
			const auto channel = peer->asChannel();
			const auto defaultRestrictions = chat
				? chat->defaultRestrictions()
				: channel->defaultRestrictions();
			const auto prepareFlags = FixDependentRestrictions(
				defaultRestrictions
				| ((channel && channel->isPublic())
					? (Flag::ChangeInfo | Flag::PinMessages)
					: Flags(0)));
			const auto disabledMessages = [&] {
				auto result = base::flat_map<Flags, QString>();
				{
					const auto disabled = FixDependentRestrictions(
						defaultRestrictions
						| ((channel && channel->isPublic())
							? (Flag::ChangeInfo | Flag::PinMessages)
							: Flags(0)));
					result.emplace(
						disabled,
						tr::lng_rights_restriction_for_all(tr::now));
				}
				return result;
			}();

			auto [checkboxes, getRestrictions, changes, highlightWidget] = CreateEditRestrictions(
				box,
				prepareFlags,
				disabledMessages,
				{ .isForum = peer->isForum(), .isUserSpecific = true });
			computeRestrictions = getRestrictions;
			std::move(changes) | rpl::on_next([=] {
				ban->setChecked(true);
			}, ban->lifetime());
			Ui::AddSkip(container);
			Ui::AddDivider(container);
			Ui::AddSkip(container);
			Ui::AddSubsectionTitle(
				container,
				rpl::conditional(
					rpl::single(isSingle),
					tr::lng_restrict_users_part_single_header(),
					tr::lng_restrict_users_part_header(
						lt_count,
						rpl::single(participants.size()) | tr::to_count())));
			container->add(std::move(checkboxes));
		}

		const auto communityBan = [&]() -> Ui::Checkbox* {
			const auto channel = isSingle ? peer->asChannel() : nullptr;
			const auto communityId = channel
				? channel->linkedCommunityId()
				: ChannelId();
			const auto community = communityId
				? channel->owner().channelLoaded(communityId)
				: nullptr;
			if (!community
				|| !(community->amCreator()
					|| (community->adminRights()
						& ChatAdminRight::BanUsers))) {
				return nullptr;
			}
			Ui::AddSkip(inner);
			Ui::AddSkip(inner);
			const auto result = inner->add(
				object_ptr<Ui::Checkbox>(
					box,
					tr::lng_community_ban_toggle(tr::now),
					false,
					st::defaultBoxCheckbox),
				st::boxRowPadding + buttonPadding);
			Ui::AddSkip(inner);
			Ui::AddSkip(inner);
			const auto info = community->communityInfo();
			const auto chatsCount = info
				? int(info->linkedPeers().size())
				: 0;
			if (chatsCount > 0) {
				const auto aboutWrap = inner->add(
					object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
						inner,
						object_ptr<Ui::VerticalLayout>(inner)));
				aboutWrap->entity()->add(object_ptr<Ui::DividerLabel>(
					aboutWrap->entity(),
					object_ptr<Ui::FlatLabel>(
						aboutWrap->entity(),
						tr::lng_community_ban_about(
							tr::now,
							lt_count,
							chatsCount),
						st::moderateBoxDividerLabel),
					st::defaultBoxDividerLabelPadding));
				aboutWrap->toggleOn(result->checkedValue());
				aboutWrap->finishAnimating();
			}
			return result;
		}();
		if (communityBan) {
			confirms->events() | rpl::on_next([=] {
				if (!communityBan->checked()) {
					return;
				}
				const auto channel = peer->asChannel();
				const auto community = channel
					? channel->owner().channelLoaded(
						channel->linkedCommunityId())
					: nullptr;
				if (community) {
					BanFromCommunityWithWarning(
						box->uiShow(),
						community,
						participants.front());
				}
			}, communityBan->lifetime());
		}

		// Handle confirmation manually.
		confirms->events() | rpl::on_next([=] {
			if (ban->checked() && controller->collectRequests) {
				const auto kick = !wrap || !wrap->toggled();
				const auto restrictions = computeRestrictions
					? computeRestrictions()
					: ChatRestrictions();
				const auto request = [=](
						not_null<PeerData*> peer,
						not_null<ChannelData*> channel) {
					if (base::IsAltPressed() || base::IsCtrlPressed()) {
						return;
					}
					if (!kick) {
						Api::ChatParticipants::Restrict(
							channel,
							peer,
							ChatRestrictionsInfo(), // Unused.
							ChatRestrictionsInfo(restrictions, 0),
							nullptr,
							nullptr);
					} else {
						const auto block = channel->isMonoforum()
							? channel->monoforumBroadcast()
							: channel.get();
						if (block) {
							block->session().api().chatParticipants().kick(
								block,
								peer,
								{ block->restrictions(), 0 });
						}
					}
				};
				if (collectCommon && !collectCommon->empty()) {
					sequentiallyRequest(
						request,
						controller->collectRequests(),
						*collectCommon);
				} else {
					sequentiallyRequest(
						request,
						controller->collectRequests());
				}
			}
		}, ban->lifetime());
	}

	const auto close = crl::guard(box, [=] { box->closeBox(); });
	box->addButton(tr::lng_box_delete(), [=] {
		confirms->fire({});
		if (confirmed) {
			confirmed();
		}
		if (hasItems) {
			session->data().histories().deleteMessages(ids, true);
			session->data().sendHistoryChangeNotifications();
		}
		const auto deleteThisReaction = reaction
			&& !ranges::contains(
				effectiveCheckedParticipants(
					deleteReactions,
					deleteReactionsController),
				reaction->participant);
		if (deleteThisReaction) {
			session->api().deleteParticipantReaction(
				reaction->peer,
				reaction->msgId,
				reaction->participant,
				reaction->reaction);
		}
		close();
	});
	box->addButton(tr::lng_cancel(), close);
}

bool CanCreateModerateMessagesBox(const HistoryItemsList &items) {
	const auto options = CalculateModerateOptions(items);
	return HasModerateActions(options)
		&& !options.participants.empty();
}

void SafeSubmitOnEnter(not_null<Ui::GenericBox*> box) {
	base::install_event_filter(box, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::KeyPress) {
			if (const auto k = static_cast<QKeyEvent*>(event.get())) {
				if ((k->key() == Qt::Key_Enter)
					|| (k->key() == Qt::Key_Return)) {
					box->uiShow()->show(Ui::MakeConfirmBox({
						.text = tr::lng_gigagroup_warning_title(),
						.confirmed = [=](Fn<void()> close) {
							box->triggerButton(0);
							close();
						},
						.confirmText = tr::lng_box_yes(),
						.cancelText = tr::lng_box_no(),
						}));
				}
			}
		}
		return base::EventFilterResult::Continue;
	});
}

void DeleteChatBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	const auto container = box->verticalLayout();

	const auto userpicPeer = peer->userpicPaintingPeer();
	const auto maybeUser = peer->asUser();
	const auto isBot = maybeUser && maybeUser->isBot();

	Ui::AddSkip(container);
	Ui::AddSkip(container);

	SafeSubmitOnEnter(box);

	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		container,
		userpicPeer,
		st::mainMenuUserpic,
		peer->userpicShape());
	userpic->showSavedMessagesOnSelf(true);
	Ui::IconWithTitle(
		container,
		userpic,
		Ui::CreateChild<Ui::FlatLabel>(
			container,
			peer->isSelf()
				? tr::lng_saved_messages(tr::bold)
				: maybeUser
				? tr::lng_profile_delete_conversation(tr::bold)
				: rpl::single(
					tr::bold(userpicPeer->name())
				) | rpl::type_erased,
			box->getDelegate()->style().title));

	Ui::AddSkip(container);
	Ui::AddSkip(container);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			container,
			peer->isSelf()
				? tr::lng_sure_delete_saved_messages()
				: maybeUser
				? tr::lng_sure_delete_history(
					lt_contact,
					rpl::single(peer->name()))
				: (peer->isChannel() && !peer->isMegagroup())
				? tr::lng_sure_leave_channel()
				: tr::lng_sure_leave_group(),
			st::boxLabel));

	const auto maybeCheckbox = [&]() -> Ui::Checkbox* {
		if (!peer->canRevokeFullHistory()) {
			return nullptr;
		}
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		return box->addRow(
			object_ptr<Ui::Checkbox>(
				container,
				maybeUser
					? tr::lng_delete_for_other_check(
						tr::now,
						lt_user,
						TextWithEntities{ maybeUser->firstName },
						tr::rich)
					: tr::lng_delete_for_everyone_check(
						tr::now,
						tr::marked),
				false,
				st::defaultBoxCheckbox));
	}();

	const auto maybeBotCheckbox = [&]() -> Ui::Checkbox* {
		if (!isBot) {
			return nullptr;
		}
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		return box->addRow(
			object_ptr<Ui::Checkbox>(
				container,
				tr::lng_profile_block_bot(tr::now, tr::marked),
				false,
				st::defaultBoxCheckbox));
	}();

	const auto removeFromChatsFilters = [=](
			not_null<History*> history) -> std::vector<FilterId> {
		auto result = std::vector<FilterId>();
		for (const auto &filter : peer->owner().chatsFilters().list()) {
			if (filter.withoutAlways(history) != filter) {
				result.push_back(filter.id());
			}
		}
		return result;
	};

	const auto maybeChatsFiltersCheckbox = [&]() -> Ui::Checkbox* {
		const auto history = (isBot || !maybeUser)
			? peer->owner().history(peer).get()
			: nullptr;
		if (!history || removeFromChatsFilters(history).empty()) {
			return nullptr;
		}
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		return box->addRow(
			object_ptr<Ui::Checkbox>(
				container,
				(maybeBotCheckbox
					? tr::lng_filters_checkbox_remove_bot
					: (peer->isChannel() && !peer->isMegagroup())
					? tr::lng_filters_checkbox_remove_channel
					: tr::lng_filters_checkbox_remove_group)(
						tr::now,
						tr::marked),
				false,
				st::defaultBoxCheckbox));
	}();

	Ui::AddSkip(container);

	auto buttonText = maybeUser
		? tr::lng_box_delete()
		: !maybeCheckbox
		? tr::lng_box_leave()
		: maybeCheckbox->checkedValue() | rpl::map([](bool checked) {
			return checked ? tr::lng_box_delete() : tr::lng_box_leave();
		}) | rpl::flatten_latest();

	const auto close = crl::guard(box, [=] { box->closeBox(); });
	box->addButton(std::move(buttonText), [=] {
		const auto revoke = maybeCheckbox && maybeCheckbox->checked();
		const auto stopBot = maybeBotCheckbox && maybeBotCheckbox->checked();
		const auto removeFromChats = maybeChatsFiltersCheckbox
			&& maybeChatsFiltersCheckbox->checked();
		Core::App().closeChatFromWindows(peer);
		if (stopBot) {
			peer->session().api().blockedPeers().block(peer);
		}
		if (removeFromChats) {
			const auto history = peer->owner().history(peer).get();
			const auto removeFrom = removeFromChatsFilters(history);
			for (const auto &filter : peer->owner().chatsFilters().list()) {
				if (!ranges::contains(removeFrom, filter.id())) {
					continue;
				}
				const auto result = filter.withoutAlways(history);
				if (result == filter) {
					continue;
				}
				const auto tl = result.tl();
				peer->owner().chatsFilters().apply(MTP_updateDialogFilter(
					MTP_flags(MTPDupdateDialogFilter::Flag::f_filter),
					MTP_int(filter.id()),
					tl));
				peer->session().api().request(MTPmessages_UpdateDialogFilter(
					MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
					MTP_int(filter.id()),
					tl
				)).send();
			}
		}
		// Don't delete old history by default,
		// because Android app doesn't.
		//
		//if (const auto from = peer->migrateFrom()) {
		//	peer->session().api().deleteConversation(from, false);
		//}
		peer->session().api().deleteConversation(peer, revoke);
		close();
	}, st::attentionBoxButton);
	box->addButton(tr::lng_cancel(), close);
}

void DeleteSublistBox(
		not_null<Ui::GenericBox*> box,
		not_null<Data::SavedSublist*> sublist) {
	const auto container = box->verticalLayout();

	const auto weak = base::make_weak(sublist.get());
	const auto peer = sublist->sublistPeer();

	Ui::AddSkip(container);
	Ui::AddSkip(container);

	SafeSubmitOnEnter(box);

	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		container,
		peer,
		st::mainMenuUserpic);
	Ui::IconWithTitle(
		container,
		userpic,
		Ui::CreateChild<Ui::FlatLabel>(
			container,
			tr::lng_profile_delete_conversation(tr::bold),
			box->getDelegate()->style().title));

	Ui::AddSkip(container);
	Ui::AddSkip(container);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_sure_delete_history(
				lt_contact,
				rpl::single(peer->name())),
			st::boxLabel));

	Ui::AddSkip(container);

	const auto close = crl::guard(box, [=] { box->closeBox(); });
	box->addButton(tr::lng_box_delete(), [=] {
		const auto strong = weak.get();
		const auto parentChat = strong ? strong->parentChat() : nullptr;
		if (!parentChat) {
			return;
		}
		peer->session().api().deleteSublistHistory(parentChat, peer);
		close();
	}, st::attentionBoxButton);
	box->addButton(tr::lng_cancel(), close);
}

ModerateMessagesBoxOptions DefaultModerateMessagesBoxOptions() {
	return base::IsCtrlPressed()
		? ModerateMessagesBoxOptions{
			.reportSpam = true,
			.deleteAll = true,
			.banUser = true,
		}
		: ModerateMessagesBoxOptions{};
}
