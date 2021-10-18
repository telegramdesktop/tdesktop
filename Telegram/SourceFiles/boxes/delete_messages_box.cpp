/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/delete_messages_box.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_ttl_button.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "facades.h" // Ui::showChatsList
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

DeleteMessagesBox::DeleteMessagesBox(
	QWidget*,
	not_null<HistoryItem*> item,
	bool suggestModerateActions)
: _session(&item->history()->session())
, _ids(1, item->fullId()) {
	if (suggestModerateActions) {
		_moderateBan = item->suggestBanReport();
		_moderateDeleteAll = item->suggestDeleteAllReport();
		if (_moderateBan || _moderateDeleteAll) {
			_moderateFrom = item->from()->asUser();
			_moderateInChannel = item->history()->peer->asChannel();
		}
	}
}

DeleteMessagesBox::DeleteMessagesBox(
	QWidget*,
	not_null<Main::Session*> session,
	MessageIdsList &&selected)
: _session(session)
, _ids(std::move(selected)) {
	Expects(!_ids.empty());
}

DeleteMessagesBox::DeleteMessagesBox(
	QWidget*,
	not_null<PeerData*> peer,
	bool justClear)
: _session(&peer->session())
, _wipeHistoryPeer(peer)
, _wipeHistoryJustClear(justClear) {
}

void DeleteMessagesBox::prepare() {
	auto details = TextWithEntities();
	const auto appendDetails = [&](TextWithEntities &&text) {
		details.append(qstr("\n\n")).append(std::move(text));
	};
	auto deleteText = lifetime().make_state<rpl::variable<QString>>();
	*deleteText = tr::lng_box_delete();
	auto deleteStyle = &st::defaultBoxButton;
	auto canDelete = true;
	if (const auto peer = _wipeHistoryPeer) {
		if (_wipeHistoryJustClear) {
			const auto isChannel = peer->isBroadcast();
			const auto isPublicGroup = peer->isMegagroup()
				&& peer->asChannel()->isPublic();
			if (isChannel || isPublicGroup) {
				canDelete = false;
			}
			details.text = isChannel
				? tr::lng_no_clear_history_channel(tr::now)
				: isPublicGroup
				? tr::lng_no_clear_history_group(tr::now)
				: peer->isSelf()
				? tr::lng_sure_delete_saved_messages(tr::now)
				: peer->isUser()
				? tr::lng_sure_delete_history(tr::now, lt_contact, peer->name)
				: tr::lng_sure_delete_group_history(
					tr::now,
					lt_group,
					peer->name);
			deleteStyle = &st::attentionBoxButton;
		} else {
			details.text = peer->isSelf()
				? tr::lng_sure_delete_saved_messages(tr::now)
				: peer->isUser()
				? tr::lng_sure_delete_history(tr::now, lt_contact, peer->name)
				: peer->isChat()
				? tr::lng_sure_delete_and_exit(tr::now, lt_group, peer->name)
				: peer->isMegagroup()
				? tr::lng_sure_leave_group(tr::now)
				: tr::lng_sure_leave_channel(tr::now);
			if (!peer->isUser()) {
				*deleteText = tr::lng_box_leave();
			}
			deleteStyle = &st::attentionBoxButton;
		}
		if (auto revoke = revokeText(peer)) {
			_revoke.create(
				this,
				revoke->checkbox,
				false,
				st::defaultBoxCheckbox);
			appendDetails(std::move(revoke->description));
			if (!peer->isUser() && !_wipeHistoryJustClear) {
				_revoke->checkedValue(
				) | rpl::start_with_next([=](bool revokeForAll) {
					*deleteText = revokeForAll
						? tr::lng_box_delete()
						: tr::lng_box_leave();
				}, _revoke->lifetime());
			}
		}
	} else if (_moderateFrom) {
		Assert(_moderateInChannel != nullptr);

		details.text = tr::lng_selected_delete_sure_this(tr::now);
		if (_moderateBan) {
			_banUser.create(
				this,
				tr::lng_ban_user(tr::now),
				false,
				st::defaultBoxCheckbox);
		}
		_reportSpam.create(
			this,
			tr::lng_report_spam(tr::now),
			false,
			st::defaultBoxCheckbox);
		if (_moderateDeleteAll) {
			_deleteAll.create(
				this,
				tr::lng_delete_all_from(tr::now),
				false,
				st::defaultBoxCheckbox);
		}
	} else {
		details.text = (_ids.size() == 1)
			? tr::lng_selected_delete_sure_this(tr::now)
			: tr::lng_selected_delete_sure(tr::now, lt_count, _ids.size());
		if (const auto peer = checkFromSinglePeer()) {
			auto count = int(_ids.size());
			if (hasScheduledMessages()) {
			} else if (auto revoke = revokeText(peer)) {
				_revoke.create(
					this,
					revoke->checkbox,
					false,
					st::defaultBoxCheckbox);
				appendDetails(std::move(revoke->description));
			} else if (peer->isChannel()) {
				if (peer->isMegagroup()) {
					appendDetails({
						tr::lng_delete_for_everyone_hint(
							tr::now,
							lt_count,
							count)
					});
				}
			} else if (peer->isChat()) {
				appendDetails({
					tr::lng_delete_for_me_chat_hint(tr::now, lt_count, count)
				});
			} else if (!peer->isSelf()) {
				appendDetails({
					tr::lng_delete_for_me_hint(tr::now, lt_count, count)
				});
			}
		}
	}
	_text.create(this, rpl::single(std::move(details)), st::boxLabel);

	if (_wipeHistoryJustClear
		&& _wipeHistoryPeer
		&& ((_wipeHistoryPeer->isUser()
			&& !_wipeHistoryPeer->isSelf()
			&& !_wipeHistoryPeer->isNotificationsUser())
			|| (_wipeHistoryPeer->isChat()
				&& _wipeHistoryPeer->asChat()->canDeleteMessages())
			|| (_wipeHistoryPeer->isChannel()
				&& _wipeHistoryPeer->asChannel()->canDeleteMessages()))) {
		_wipeHistoryPeer->updateFull();
		_autoDeleteSettings.create(
			this,
			(_wipeHistoryPeer->messagesTTL()
				? tr::lng_edit_auto_delete_settings(tr::now)
				: tr::lng_enable_auto_delete(tr::now)),
			st::boxLinkButton);
		_autoDeleteSettings->setClickedCallback([=] {
			getDelegate()->show(
				Box(
					HistoryView::Controls::AutoDeleteSettingsBox,
					_wipeHistoryPeer),
				Ui::LayerOption(0));
		});
	}

	if (canDelete) {
		addButton(
			deleteText->value(),
			[=] { deleteAndClear(); },
			*deleteStyle);
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	} else {
		addButton(tr::lng_about_done(), [=] { closeBox(); });
	}

	auto fullHeight = st::boxPadding.top()
		+ _text->height()
		+ st::boxPadding.bottom();
	if (_moderateFrom) {
		fullHeight += st::boxMediumSkip;
		if (_banUser) {
			fullHeight += _banUser->heightNoMargins() + st::boxLittleSkip;
		}
		fullHeight += _reportSpam->heightNoMargins();
		if (_deleteAll) {
			fullHeight += st::boxLittleSkip + _deleteAll->heightNoMargins();
		}
	} else if (_revoke) {
		fullHeight += st::boxMediumSkip + _revoke->heightNoMargins();
	}
	if (_autoDeleteSettings) {
		fullHeight += st::boxMediumSkip
			+ _autoDeleteSettings->height()
			+ st::boxLittleSkip;
	}
	setDimensions(st::boxWidth, fullHeight);
}

bool DeleteMessagesBox::hasScheduledMessages() const {
	for (const auto &fullId : _ids) {
		if (const auto item = _session->data().message(fullId)) {
			if (item->isScheduled()) {
				return true;
			}
		}
	}
	return false;
}

PeerData *DeleteMessagesBox::checkFromSinglePeer() const {
	auto result = (PeerData*)nullptr;
	for (const auto &fullId : _ids) {
		if (const auto item = _session->data().message(fullId)) {
			const auto peer = item->history()->peer;
			if (!result) {
				result = peer;
			} else if (result != peer) {
				return nullptr;
			}
		}
	}
	return result;
}

auto DeleteMessagesBox::revokeText(not_null<PeerData*> peer) const
-> std::optional<RevokeConfig> {
	auto result = RevokeConfig();
	if (peer == _wipeHistoryPeer) {
		if (!peer->canRevokeFullHistory()) {
			return std::nullopt;
		} else if (const auto user = peer->asUser()) {
			result.checkbox = tr::lng_delete_for_other_check(
				tr::now,
				lt_user,
				user->firstName);
		} else if (_wipeHistoryJustClear) {
			return std::nullopt;
		} else {
			result.checkbox = tr::lng_delete_for_everyone_check(tr::now);
		}
		return result;
	}

	const auto items = ranges::views::all(
		_ids
	) | ranges::views::transform([&](FullMsgId id) {
		return peer->owner().message(id);
	}) | ranges::views::filter([](HistoryItem *item) {
		return (item != nullptr);
	}) | ranges::to_vector;

	if (items.size() != _ids.size()) {
		// We don't have information about all messages.
		return std::nullopt;
	}

	const auto now = base::unixtime::now();
	const auto canRevoke = [&](HistoryItem * item) {
		return item->canDeleteForEveryone(now);
	};
	const auto cannotRevoke = [&](HistoryItem *item) {
		return !item->canDeleteForEveryone(now);
	};
	const auto canRevokeAll = ranges::none_of(items, cannotRevoke);
	auto outgoing = items | ranges::views::filter(&HistoryItem::out);
	const auto canRevokeOutgoingCount = canRevokeAll
		? -1
		: ranges::count_if(outgoing, canRevoke);

	if (canRevokeAll) {
		if (const auto user = peer->asUser()) {
			result.checkbox = tr::lng_delete_for_other_check(
				tr::now,
				lt_user,
				user->firstName);
		} else {
			result.checkbox = tr::lng_delete_for_everyone_check(tr::now);
		}
		return result;
	} else if (canRevokeOutgoingCount > 0) {
		result.checkbox = tr::lng_delete_for_other_my(tr::now);
		if (const auto user = peer->asUser()) {
			if (canRevokeOutgoingCount == 1) {
				result.description = tr::lng_selected_unsend_about_user_one(
					tr::now,
					lt_user,
					Ui::Text::Bold(user->shortName()),
					Ui::Text::WithEntities);
			} else {
				result.description = tr::lng_selected_unsend_about_user(
					tr::now,
					lt_count,
					canRevokeOutgoingCount,
					lt_user,
					Ui::Text::Bold(user->shortName()),
					Ui::Text::WithEntities);
			}
		} else if (canRevokeOutgoingCount == 1) {
			result.description = tr::lng_selected_unsend_about_group_one(
				tr::now,
				Ui::Text::WithEntities);
		} else {
			result.description = tr::lng_selected_unsend_about_group(
				tr::now,
				lt_count,
				canRevokeOutgoingCount,
				Ui::Text::WithEntities);
		}
		return result;
	}
	return std::nullopt;
}

void DeleteMessagesBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	auto top = _text->bottomNoMargins() + st::boxMediumSkip;
	if (_moderateFrom) {
		if (_banUser) {
			_banUser->moveToLeft(st::boxPadding.left(), top);
			top += _banUser->heightNoMargins() + st::boxLittleSkip;
		}
		_reportSpam->moveToLeft(st::boxPadding.left(), top);
		top += _reportSpam->heightNoMargins() + st::boxLittleSkip;
		if (_deleteAll) {
			_deleteAll->moveToLeft(st::boxPadding.left(), top);
			top += _deleteAll->heightNoMargins() + st::boxLittleSkip;
		}
	} else if (_revoke) {
		const auto availableWidth = width() - 2 * st::boxPadding.left();
		_revoke->resizeToNaturalWidth(availableWidth);
		_revoke->moveToLeft(st::boxPadding.left(), top);
		top += _revoke->heightNoMargins() + st::boxLittleSkip;
	}
	if (_autoDeleteSettings) {
		top += st::boxMediumSkip - st::boxLittleSkip;
		_autoDeleteSettings->moveToLeft(st::boxPadding.left(), top);
	}
}

void DeleteMessagesBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		// Don't make the clearing history so easy.
		if (!_wipeHistoryPeer) {
			deleteAndClear();
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void DeleteMessagesBox::deleteAndClear() {
	const auto revoke = _revoke ? _revoke->checked() : false;
	if (const auto peer = _wipeHistoryPeer) {
		const auto justClear = _wipeHistoryJustClear;
		closeBox();

		if (justClear) {
			peer->session().api().clearHistory(peer, revoke);
		} else {
			for (const auto &controller : peer->session().windows()) {
				if (controller->activeChatCurrent().peer() == peer) {
					Ui::showChatsList(&peer->session());
				}
			}
			// Don't delete old history by default,
			// because Android app doesn't.
			//
			//if (const auto from = peer->migrateFrom()) {
			//	peer->session().api().deleteConversation(from, false);
			//}
			peer->session().api().deleteConversation(peer, revoke);
		}
		return;
	}
	if (_moderateFrom) {
		if (_banUser && _banUser->checked()) {
			_moderateInChannel->session().api().kickParticipant(
				_moderateInChannel,
				_moderateFrom,
				ChatRestrictionsInfo());
		}
		if (_reportSpam->checked()) {
			_moderateInChannel->session().api().request(
				MTPchannels_ReportSpam(
					_moderateInChannel->inputChannel,
					_moderateFrom->inputUser,
					MTP_vector<MTPint>(1, MTP_int(_ids[0].msg)))
			).send();
		}
		if (_deleteAll && _deleteAll->checked()) {
			_moderateInChannel->session().api().deleteAllFromUser(
				_moderateInChannel,
				_moderateFrom);
		}
	}

	if (_deleteConfirmedCallback) {
		_deleteConfirmedCallback();
	}

	// deleteMessages can initiate closing of the current section,
	// which will cause this box to be destroyed.
	const auto session = _session;
	const auto weak = Ui::MakeWeak(this);

	session->data().histories().deleteMessages(_ids, revoke);

	if (const auto strong = weak.data()) {
		strong->closeBox();
	}
	session->data().sendHistoryChangeNotifications();
}
