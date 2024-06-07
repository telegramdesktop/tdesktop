/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_top_bar_widget.h"

#include "history/history.h"
#include "history/view/history_view_send_action.h"
#include "boxes/add_contact_box.h"
#include "ui/boxes/confirm_box.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_shared_media.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "lang/lang_keys.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/controls/userpic_button.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/effects/radial_animation.h"
#include "ui/boxes/report_box.h" // Ui::ReportReason
#include "ui/text/text.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "ui/unread_badge.h"
#include "ui/ui_utility.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "calls/calls_instance.h"
#include "data/data_peer_values.h"
#include "data/data_group_call.h" // GroupCall::input.
#include "data/data_folder.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_forum_topic.h"
#include "data/data_send_action.h"
#include "chat_helpers/emoji_interactions.h"
#include "base/unixtime.h"
#include "base/event_filter.h"
#include "support/support_helper.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QWindow>

namespace HistoryView {
namespace {

constexpr auto kEmojiInteractionSeenDuration = 3 * crl::time(1000);

inline bool HasGroupCallMenu(const not_null<PeerData*> &peer) {
	return !peer->groupCall()
		&& ((peer->isChannel() && peer->asChannel()->amCreator())
			|| (peer->isChat() && peer->asChat()->amCreator()));
}

QString TopBarNameText(
		not_null<PeerData*> peer,
		Dialogs::EntryState::Section section) {
	if (section == Dialogs::EntryState::Section::SavedSublist) {
		if (peer->isSelf()) {
			return tr::lng_my_notes(tr::now);
		} else if (peer->isSavedHiddenAuthor()) {
			return tr::lng_hidden_author_messages(tr::now);
		}
	}
	return peer->topBarNameText();
}

} // namespace

struct TopBarWidget::EmojiInteractionSeenAnimation {
	Ui::SendActionAnimation animation;
	Ui::Animations::Basic scheduler;
	Ui::Text::String text = { st::dialogsTextWidthMin };
	crl::time till = 0;
};

QString SwitchToChooseFromQuery() {
	return u"from:"_q;
}

TopBarWidget::TopBarWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _primaryWindow(controller->isPrimary())
, _clear(this, tr::lng_selected_clear(), st::topBarClearButton)
, _forward(this, tr::lng_selected_forward(), st::defaultActiveButton)
, _sendNow(this, tr::lng_selected_send_now(), st::defaultActiveButton)
, _delete(this, tr::lng_selected_delete(), st::defaultActiveButton)
, _back(this, st::historyTopBarBack)
, _cancelChoose(this, st::topBarCloseChoose)
, _call(this, st::topBarCall)
, _groupCall(this, st::topBarGroupCall)
, _search(this, st::topBarSearch)
, _infoToggle(this, st::topBarInfo)
, _menuToggle(this, st::topBarMenuToggle)
, _titlePeerText(st::windowMinWidth / 3)
, _onlineUpdater([=] { updateOnlineDisplay(); }) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_forward->setClickedCallback([=] { _forwardSelection.fire({}); });
	_forward->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_sendNow->setClickedCallback([=] { _sendNowSelection.fire({}); });
	_sendNow->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_delete->setClickedCallback([=] { _deleteSelection.fire({}); });
	_delete->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_clear->setClickedCallback([=] { _clearSelection.fire({}); });
	_call->setClickedCallback([=] { call(); });
	_groupCall->setClickedCallback([=] { groupCall(); });
	_menuToggle->setClickedCallback([=] { showPeerMenu(); });
	_infoToggle->setClickedCallback([=] { toggleInfoSection(); });
	_back->setAcceptBoth();
	_back->addClickHandler([=](Qt::MouseButton) {
		InvokeQueued(_back.data(), [=] { backClicked(); });
	});
	_cancelChoose->setClickedCallback(
		[=] { _cancelChooseForReport.fire({}); });

	rpl::combine(
		_controller->activeChatValue(),
		_controller->searchInChatValue()
	) | rpl::combine_previous(
		std::make_tuple(Dialogs::Key(), Dialogs::Key())
	) | rpl::map([](
			const std::tuple<Dialogs::Key, Dialogs::Key> &previous,
			const std::tuple<Dialogs::Key, Dialogs::Key> &current) {
		const auto &active = std::get<0>(current);
		const auto &search = std::get<1>(current);
		const auto activeChanged = (active != std::get<0>(previous));
		const auto searchInChat = search && (active == search);
		return std::make_tuple(searchInChat, activeChanged);
	}) | rpl::start_with_next([=](
			bool searchInActiveChat,
			bool activeChanged) {
		auto animated = activeChanged
			? anim::type::instant
			: anim::type::normal;
		_search->setForceRippled(searchInActiveChat, animated);
	}, lifetime());

	controller->adaptive().changes(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	refreshUnreadBadge();
	{
		using AnimationUpdate = Data::SendActionManager::AnimationUpdate;
		session().data().sendActionManager().animationUpdated(
		) | rpl::filter([=](const AnimationUpdate &update) {
			return (update.thread == _activeChat.key.thread());
		}) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	}

	using UpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		UpdateFlag::HasCalls
		| UpdateFlag::OnlineStatus
		| UpdateFlag::Members
		| UpdateFlag::SupportInfo
		| UpdateFlag::Rights
		| UpdateFlag::EmojiStatus
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (update.flags & UpdateFlag::HasCalls) {
			if (update.peer->isUser()
				&& (update.peer->isSelf()
					|| _activeChat.key.peer() == update.peer)) {
				updateControlsVisibility();
			}
		} else if ((update.flags & UpdateFlag::Rights)
			&& (_activeChat.key.peer() == update.peer)) {
			updateControlsVisibility();
		}
		if ((update.flags & UpdateFlag::OnlineStatus)
			&& trackOnlineOf(update.peer)) {
			updateOnlineDisplay();
		} else if (update.flags
			& (UpdateFlag::Members | UpdateFlag::SupportInfo)) {
			if (update.peer == _activeChat.key.peer()
				&& !_activeChat.key.topic()) {
				updateOnlineDisplay();
			}
		}
		if ((update.flags & UpdateFlag::EmojiStatus)
			&& (_activeChat.key.peer() == update.peer)) {
			this->update();
		}
	}, lifetime());

	rpl::combine(
		Core::App().settings().thirdSectionInfoEnabledValue(),
		Core::App().settings().tabbedReplacedWithInfoValue()
	) | rpl::start_with_next([=] {
		updateInfoToggleActive();
	}, lifetime());

	Core::App().settings().proxy().connectionTypeValue(
	) | rpl::start_with_next([=] {
		updateConnectingState();
	}, lifetime());

	base::install_event_filter(
		this,
		window()->windowHandle(),
		[=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Expose) {
				updateConnectingState();
			}
			return base::EventFilterResult::Continue;
		});

	setCursor(style::cur_pointer);
}

TopBarWidget::~TopBarWidget() = default;

Main::Session &TopBarWidget::session() const {
	return _controller->session();
}

void TopBarWidget::updateConnectingState() {
	const auto state = _controller->session().mtp().dcstate();
	const auto exposed = window()->windowHandle()->isExposed();
	if (state == MTP::ConnectedState || !exposed) {
		if (_connecting) {
			_connecting = nullptr;
			update();
		}
	} else if (!_connecting) {
		_connecting = std::make_unique<Ui::InfiniteRadialAnimation>(
			[=] { connectingAnimationCallback(); },
			st::topBarConnectingAnimation);
		_connecting->start();
		update();
	}
}

void TopBarWidget::connectingAnimationCallback() {
	if (!anim::Disabled()) {
		update();
	}
}

void TopBarWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void TopBarWidget::call() {
	if (const auto peer = _activeChat.key.peer()) {
		if (const auto user = peer->asUser()) {
			Core::App().calls().startOutgoingCall(user, false);
		}
	}
}

void TopBarWidget::groupCall() {
	if (const auto peer = _activeChat.key.peer()) {
		if (HasGroupCallMenu(peer)) {
			showGroupCallMenu(peer);
		} else {
			_controller->startOrJoinGroupCall(peer, {});
		}
	}
}

void TopBarWidget::showChooseMessagesForReport(Ui::ReportReason reason) {
	setChooseForReportReason(reason);
}

void TopBarWidget::clearChooseMessagesForReport() {
	setChooseForReportReason(std::nullopt);
}

rpl::producer<> TopBarWidget::searchRequest() const {
	return _search->clicks() | rpl::to_empty;
}

void TopBarWidget::setChooseForReportReason(
		std::optional<Ui::ReportReason> reason) {
	if (_chooseForReportReason == reason) {
		return;
	}
	const auto wasNoReason = !_chooseForReportReason;
	_chooseForReportReason = reason;
	const auto nowNoReason = !_chooseForReportReason;
	updateControlsVisibility();
	updateControlsGeometry();
	update();
	if (wasNoReason != nowNoReason && showSelectedState()) {
		toggleSelectedControls(false);
		finishAnimating();
	}
	setCursor((nowNoReason && !showSelectedState())
		? style::cur_pointer
		: style::cur_default);
}

bool TopBarWidget::createMenu(not_null<Ui::IconButton*> button) {
	if (!_activeChat.key || _menu) {
		return false;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuExpandedSeparator);
	_menu->setDestroyedCallback([
			weak = Ui::MakeWeak(this),
			weakButton = Ui::MakeWeak(button),
			menu = _menu.get()] {
		if (weak && weak->_menu == menu) {
			if (weakButton) {
				weakButton->setForceRippled(false);
			}
		}
	});
	button->setForceRippled(true);
	return true;
}

void TopBarWidget::showPeerMenu() {
	const auto created = createMenu(_menuToggle);
	if (!created) {
		return;
	}
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	Window::FillDialogsEntryMenu(_controller, _activeChat, addAction);
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		_menu->popup(mapToGlobal(QPoint(
			width() + st::topBarMenuPosition.x(),
			st::topBarMenuPosition.y())));
	}
}

void TopBarWidget::showGroupCallMenu(not_null<PeerData*> peer) {
	const auto created = createMenu(_groupCall);
	if (!created) {
		return;
	}
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	Window::FillVideoChatMenu(_controller, _activeChat, addAction);
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(mapToGlobal(QPoint(
		_groupCall->x() + _groupCall->width() + st::topBarMenuGroupCallSkip,
		st::topBarMenuPosition.y())));
}

void TopBarWidget::toggleInfoSection() {
	const auto isThreeColumn = _controller->adaptive().isThreeColumn();
	if (isThreeColumn
		&& (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo())) {
		_controller->closeThirdSection();
	} else if (_activeChat.key.peer()) {
		if (_controller->canShowThirdSection()) {
			Core::App().settings().setThirdSectionInfoEnabled(true);
			Core::App().saveSettingsDelayed();
			if (isThreeColumn) {
				_controller->showSection(
					(_activeChat.key.topic()
						? std::make_shared<Info::Memento>(
							_activeChat.key.topic())
						: Info::Memento::Default(_activeChat.key.peer())),
					Window::SectionShow().withThirdColumn());
			} else {
				_controller->resizeForThirdSection();
				_controller->updateColumnLayout();
			}
		} else {
			infoClicked();
		}
	} else {
		updateControlsVisibility();
	}
}

bool TopBarWidget::eventFilter(QObject *obj, QEvent *e) {
	if (obj == _membersShowArea) {
		switch (e->type()) {
		case QEvent::MouseButtonPress:
			mousePressEvent(static_cast<QMouseEvent*>(e));
			return true;

		case QEvent::Enter:
			_membersShowAreaActive.fire(true);
			break;

		case QEvent::Leave:
			_membersShowAreaActive.fire(false);
			break;
		}
	}
	return RpWidget::eventFilter(obj, e);
}

int TopBarWidget::resizeGetHeight(int newWidth) {
	return st::topBarHeight;
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	if (_animatingMode) {
		return;
	}
	Painter p(this);

	const auto selectedButtonsTop = countSelectedButtonsTop(
		_selectedShown.value(showSelectedActions() ? 1. : 0.));
	const auto searchFieldTop = _searchField
		? countSelectedButtonsTop(_searchShown.value(_searchMode ? 1. : 0.))
		: -st::topBarHeight;
	const auto slidingTop = std::max(selectedButtonsTop, searchFieldTop);

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBg);
	if (slidingTop < 0) {
		p.translate(0, slidingTop + st::topBarHeight);
		paintTopBar(p);
	}
}

void TopBarWidget::paintTopBar(Painter &p) {
	if (!_activeChat.key || _narrowRatio == 1.) {
		return;
	}
	auto nameleft = _leftTaken;
	auto nametop = st::topBarArrowPadding.top();
	auto statustop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto availableWidth = width()
		- _rightTaken
		- nameleft
		- st::topBarNameRightPadding;

	if (_chooseForReportReason) {
		const auto text = [&] {
			using Reason = Ui::ReportReason;
			switch (*_chooseForReportReason) {
			case Reason::Spam: return tr::lng_report_reason_spam(tr::now);
			case Reason::Violence:
				return tr::lng_report_reason_violence(tr::now);
			case Reason::ChildAbuse:
				return tr::lng_report_reason_child_abuse(tr::now);
			case Reason::Pornography:
				return tr::lng_report_reason_pornography(tr::now);
			case Reason::Copyright:
				return tr::lng_report_reason_copyright(tr::now);
			}
			Unexpected("reason in TopBarWidget::paintTopBar.");
		}();
		p.setPen(st::dialogsNameFg);
		p.setFont(st::semiboldFont);
		p.drawTextLeft(nameleft, nametop, width(), text);

		p.setFont(st::dialogsTextFont);
		p.setPen(st::historyStatusFg);
		p.drawTextLeft(
			nameleft,
			statustop,
			width(),
			tr::lng_report_select_messages(tr::now));
		return;
	}

	const auto now = crl::now();
	const auto peer = _activeChat.key.owningHistory()
		? _activeChat.key.owningHistory()->peer.get()
		: nullptr;
	const auto folder = _activeChat.key.folder();
	const auto sublist = _activeChat.key.sublist();
	const auto topic = _activeChat.key.topic();
	const auto history = _activeChat.key.history();
	const auto namePeer = history
		? history->peer.get()
		: sublist ? sublist->peer().get()
		: nullptr;
	if (topic && _activeChat.section == Section::Replies) {
		p.setPen(st::dialogsNameFg);
		topic->chatListNameText().drawElided(
			p,
			nameleft,
			nametop,
			availableWidth);

		p.setFont(st::dialogsTextFont);
		if (!paintConnectingState(p, nameleft, statustop, width())
			&& !paintSendAction(
				p,
				nameleft,
				statustop,
				availableWidth,
				width(),
				st::historyStatusFgTyping,
				now)) {
			p.setPen(st::historyStatusFg);
			p.drawTextLeft(nameleft, statustop, width(), _customTitleText);
		}
	} else if (folder
		|| (peer && peer->sharedMediaInfo())
		|| (_activeChat.section == Section::Scheduled)
		|| (_activeChat.section == Section::Pinned)) {
		auto text = (_activeChat.section == Section::Scheduled)
			? ((peer && peer->isSelf())
				? tr::lng_reminder_messages(tr::now)
				: tr::lng_scheduled_messages(tr::now))
			: (_activeChat.section == Section::Pinned)
			? _customTitleText
			: folder
			? folder->chatListName()
			: peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: peer->isRepliesChat()
			? tr::lng_replies_messages(tr::now)
			: peer->name();
		const auto textWidth = st::historySavedFont->width(text);
		if (availableWidth < textWidth) {
			text = st::historySavedFont->elided(text, availableWidth);
		}
		p.setPen(st::dialogsNameFg);
		p.setFont(st::historySavedFont);
		p.drawTextLeft(
			nameleft,
			(height() - st::historySavedFont->height) / 2,
			width(),
			text);
	} else if (_activeChat.section == Section::Replies) {
		p.setPen(st::dialogsNameFg);
		p.setFont(st::semiboldFont);
		p.drawTextLeft(
			nameleft,
			nametop,
			width(),
			tr::lng_manage_discussion_group(tr::now));

		p.setFont(st::dialogsTextFont);
		if (!paintConnectingState(p, nameleft, statustop, width())
			&& !paintSendAction(
				p,
				nameleft,
				statustop,
				availableWidth,
				width(),
				st::historyStatusFgTyping,
				now)) {
			paintStatus(p, nameleft, statustop, availableWidth, width());
		}
	} else if (namePeer) {
		if (_titleNameVersion < namePeer->nameVersion()) {
			_titleNameVersion = namePeer->nameVersion();
			_title.setText(
				st::msgNameStyle,
				TopBarNameText(namePeer, _activeChat.section),
				Ui::NameTextOptions());
		}
		const auto badgeWidth = _titleBadge.drawGetWidth(
			p,
			QRect(
				nameleft,
				nametop,
				availableWidth,
				st::msgNameStyle.font->height),
			_title.maxWidth(),
			width(),
			{
				.peer = namePeer,
				.verified = &st::dialogsVerifiedIcon,
				.premium = &st::dialogsPremiumIcon.icon,
				.scam = &st::attentionButtonFg,
				.premiumFg = &st::dialogsVerifiedIconBg,
				.customEmojiRepaint = [=] { update(); },
				.now = now,
				.paused = _controller->isGifPausedAtLeastFor(
					Window::GifPauseReason::Any),
			});
		const auto namewidth = availableWidth - badgeWidth;

		p.setPen(st::dialogsNameFg);
		_title.drawElided(
			p,
			nameleft,
			nametop,
			namewidth);

		p.setFont(st::dialogsTextFont);
		if (!paintConnectingState(p, nameleft, statustop, width())
			&& !paintSendAction(
				p,
				nameleft,
				statustop,
				availableWidth,
				width(),
				st::historyStatusFgTyping,
				now)) {
			paintStatus(p, nameleft, statustop, availableWidth, width());
		}
	}
}

bool TopBarWidget::paintSendAction(
		Painter &p,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		style::color fg,
		crl::time now) {
	if (!_sendAction) {
		return false;
	}
	const auto seen = _emojiInteractionSeen.get();
	if (!seen || seen->till <= now) {
		return _sendAction->paint(p, x, y, availableWidth, outerWidth, fg, now);
	}
	const auto animationWidth = seen->animation.width();
	const auto extraAnimationWidth = animationWidth * 2;
	seen->animation.paint(
		p,
		fg,
		x,
		y + st::normalFont->ascent,
		outerWidth,
		now);

	x += animationWidth;
	availableWidth -= extraAnimationWidth;
	p.setPen(fg);
	seen->text.drawElided(p, x, y, availableWidth);
	return true;
}

bool TopBarWidget::paintConnectingState(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	if (!_connecting) {
		return false;
	}
	_connecting->draw(
		p,
		{
			st::topBarConnectingPosition.x() + left,
			st::topBarConnectingPosition.y() + top
		},
		outerWidth);
	left += st::topBarConnectingPosition.x()
		+ st::topBarConnectingAnimation.size.width()
		+ st::topBarConnectingSkip;
	p.setPen(st::historyStatusFg);
	p.drawTextLeft(left, top, outerWidth, tr::lng_status_connecting(tr::now));
	return true;
}

void TopBarWidget::paintStatus(
		Painter &p,
		int left,
		int top,
		int availableWidth,
		int outerWidth) {
	using Section = Dialogs::EntryState::Section;
	const auto section = _activeChat.section;
	if (section == Section::Replies || section == Section::SavedSublist) {
		p.setPen(st::historyStatusFg);
		p.drawTextLeft(left, top, outerWidth, _customTitleText);
	} else {
		p.setPen(_titlePeerTextOnline
			? st::historyStatusFgActive
			: st::historyStatusFg);
		_titlePeerText.drawLeftElided(
			p,
			left,
			top,
			availableWidth,
			outerWidth);
	}
}

QRect TopBarWidget::getMembersShowAreaGeometry() const {
	int membersTextLeft = _leftTaken;
	int membersTextTop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	int membersTextWidth = _titlePeerText.maxWidth();
	int membersTextHeight = st::topBarHeight - membersTextTop;

	return myrtlrect(membersTextLeft, membersTextTop, membersTextWidth, membersTextHeight);
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	const auto handleClick = (e->button() == Qt::LeftButton)
		&& (e->pos().y() < st::topBarHeight)
		&& !showSelectedState()
		&& !_chooseForReportReason;
	if (handleClick) {
		if ((_animatingMode && _back->rect().contains(e->pos()))
			|| (_activeChat.section == Section::ChatsList
				&& _activeChat.key.folder())) {
			backClicked();
		} else {
			infoClicked();
		}
	}
}

void TopBarWidget::infoClicked() {
	const auto key = _activeChat.key;
	if (!key) {
		return;
	} else if (const auto topic = key.topic()) {
		_controller->showSection(std::make_shared<Info::Memento>(topic));
	} else if (const auto sublist = key.sublist()) {
		_controller->showSection(std::make_shared<Info::Memento>(
			_controller->session().user(),
			Info::Section(Storage::SharedMediaType::Photo)));
	} else if (key.peer()->savedSublistsInfo()) {
		_controller->showSection(std::make_shared<Info::Memento>(
			key.peer(),
			Info::Section::Type::SavedSublists));
	} else if (key.peer()->sharedMediaInfo()) {
		_controller->showSection(std::make_shared<Info::Memento>(
			key.peer(),
			Info::Section(Storage::SharedMediaType::Photo)));
	} else {
		_controller->showPeerInfo(key.peer());
	}
}

void TopBarWidget::backClicked() {
	if (_activeChat.key.folder()) {
		_controller->closeFolder();
	} else if (_activeChat.section == Section::ChatsList
		&& _activeChat.key.history()
		&& _activeChat.key.history()->isForum()) {
		_controller->closeForum();
	} else {
		_controller->showBackFromStack();
	}
}

void TopBarWidget::setActiveChat(
		ActiveChat activeChat,
		SendActionPainter *sendAction) {
	_sendAction = sendAction;
	if (_activeChat.key == activeChat.key
		&& _activeChat.section == activeChat.section) {
		_activeChat = activeChat;
		return;
	}
	const auto topicChanged = (_activeChat.key.topic()
		!= activeChat.key.topic());
	const auto peerChanged = (_activeChat.key.history()
		!= activeChat.key.history());

	_activeChat = activeChat;
	_titlePeerText.clear();
	_back->clearState();
	update();

	if (peerChanged || topicChanged) {
		_titleBadge.unload();
		_titleNameVersion = 0;
		_emojiInteractionSeen = nullptr;
		_activeChatLifetime.destroy();
		if (const auto peer = _activeChat.key.peer()) {
			session().changes().peerFlagsValue(
				peer,
				Data::PeerUpdate::Flag::GroupCall
			) | rpl::map([=] {
				return peer->groupCall();
			}) | rpl::distinct_until_changed(
			) | rpl::map([](Data::GroupCall *call) {
				return call ? call->fullCountValue() : rpl::single(-1);
			}) | rpl::flatten_latest(
			) | rpl::map([](int count) {
				return (count == 0);
			}) | rpl::distinct_until_changed(
			) | rpl::start_with_next([=] {
				updateControlsVisibility();
				updateControlsGeometry();
			}, _activeChatLifetime);

			if (const auto channel = peer->asChannel()) {
				if (channel->canEditStories()
					&& !channel->owner().stories().archiveCountKnown(
						channel->id)) {
					channel->owner().stories().archiveLoadMore(channel->id);
				}
			}
		}

		if (const auto history = _activeChat.key.history()) {
			using InteractionSeen = ChatHelpers::EmojiInteractionSeen;
			_controller->emojiInteractions().seen(
			) | rpl::filter([=](const InteractionSeen &seen) {
				return (seen.peer == history->peer);
			}) | rpl::start_with_next([=](const InteractionSeen &seen) {
				handleEmojiInteractionSeen(seen.emoticon);
			}, _activeChatLifetime);
		}

		if (const auto topic = _activeChat.key.topic()) {
			Info::Profile::NameValue(
				topic->channel()
			) | rpl::start_with_next([=](const QString &name) {
				_titlePeerText.setText(st::dialogsTextStyle, name);
				_titlePeerTextOnline = false;
				update();
			}, _activeChatLifetime);

			// _menuToggle visibility depends on "View topic info",
			// "View topic info" visibility depends on activeChatCurrent.
			_controller->activeChatChanges(
			) | rpl::start_with_next([=] {
				updateControlsVisibility();
			}, _activeChatLifetime);
		}
	}
	updateUnreadBadge();
	refreshInfoButton();
	if (_menu) {
		_menu = nullptr;
	}
	updateOnlineDisplay();
	updateControlsVisibility();
	refreshUnreadBadge();
	setupDragOnBackButton();
}

void TopBarWidget::handleEmojiInteractionSeen(const QString &emoticon) {
	auto seen = _emojiInteractionSeen.get();
	if (!seen) {
		_emojiInteractionSeen
			= std::make_unique<EmojiInteractionSeenAnimation>();
		seen = _emojiInteractionSeen.get();
		seen->animation.start(Ui::SendActionAnimation::Type::ChooseSticker);
		seen->scheduler.init([=] {
			if (seen->till <= crl::now()) {
				crl::on_main(this, [=] {
					if (_emojiInteractionSeen
						&& _emojiInteractionSeen->till <= crl::now()) {
						_emojiInteractionSeen = nullptr;
						update();
					}
				});
			} else {
				const auto skip = st::topBarArrowPadding.bottom();
				update(
					_leftTaken,
					st::topBarHeight - skip - st::dialogsTextFont->height,
					seen->animation.width(),
					st::dialogsTextFont->height);
			}
		});
		seen->scheduler.start();
	}
	seen->till = crl::now() + kEmojiInteractionSeenDuration;
	seen->text.setText(
		st::dialogsTextStyle,
		tr::lng_user_action_watching_animations(tr::now, lt_emoji, emoticon),
		Ui::NameTextOptions());
	update();
}

void TopBarWidget::setCustomTitle(const QString &title) {
	if (_customTitleText != title) {
		_customTitleText = title;
		update();
	}
}

void TopBarWidget::refreshInfoButton() {
	if (_activeChat.key.topic()
		|| _activeChat.section == Section::ChatsList) {
		_info.destroy();
	} else if (const auto peer = _activeChat.key.peer()) {
		auto info = object_ptr<Ui::UserpicButton>(
			this,
			peer,
			st::topBarInfoButton);
		info->showSavedMessagesOnSelf(true);
		_info.destroy();
		_info = std::move(info);
	}
	if (_info) {
		_info->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	updateSearchVisibility();
	updateControlsGeometry();
}

int TopBarWidget::countSelectedButtonsTop(float64 selectedShown) {
	return (1. - selectedShown) * (-st::topBarHeight);
}

void TopBarWidget::updateSearchVisibility() {
	const auto searchAllowedMode = (_activeChat.section == Section::History)
		|| (_activeChat.section == Section::Replies
			&& _activeChat.key.topic())
		|| (_activeChat.section == Section::SavedSublist
			&& _activeChat.key.sublist());
	_search->setVisible(searchAllowedMode && !_chooseForReportReason);
}

void TopBarWidget::updateControlsGeometry() {
	if (!_activeChat.key) {
		return;
	}
	const auto hasSelected = showSelectedActions();
	auto selectedButtonsTop = countSelectedButtonsTop(
		_selectedShown.value(hasSelected ? 1. : 0.));
	if (!_searchMode && !_searchShown.animating() && _searchField) {
		_searchField.destroy();
		_searchCancel.destroy();
		_jumpToDate.destroy();
		_chooseFromUser.destroy();
	}
	auto searchFieldTop = _searchField
		? countSelectedButtonsTop(_searchShown.value(_searchMode ? 1. : 0.))
		: -st::topBarHeight;
	const auto otherButtonsTop = std::max(selectedButtonsTop, searchFieldTop)
		+ st::topBarHeight;
	const auto backButtonTop = selectedButtonsTop + st::topBarHeight;
	auto buttonsLeft = st::topBarActionSkip
		+ (_controller->adaptive().isOneColumn() ? 0 : st::lineWidth);
	auto buttonsWidth = (_forward->isHidden() ? 0 : _forward->contentWidth())
		+ (_sendNow->isHidden() ? 0 : _sendNow->contentWidth())
		+ (_delete->isHidden() ? 0 : _delete->contentWidth())
		+ _clear->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	auto widthLeft = qMin(width() - buttonsWidth, -2 * st::defaultActiveButton.width);
	auto buttonFullWidth = qMin(-(widthLeft / 2), 0);
	_forward->setFullWidth(buttonFullWidth);
	_sendNow->setFullWidth(buttonFullWidth);
	_delete->setFullWidth(buttonFullWidth);

	selectedButtonsTop += (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_forward->isHidden()) {
		buttonsLeft += _forward->width() + st::topBarActionSkip;
	}

	_sendNow->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_sendNow->isHidden()) {
		buttonsLeft += _sendNow->width() + st::topBarActionSkip;
	}

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	_clear->moveToRight(st::topBarActionSkip, selectedButtonsTop);

	if (!_cancelChoose->isHidden()) {
		_leftTaken = 0;
		_cancelChoose->moveToLeft(_leftTaken, otherButtonsTop);
		_leftTaken += _cancelChoose->width();
	} else if (_back->isHidden()) {
		_leftTaken = st::topBarArrowPadding.right();
	} else {
		_leftTaken = anim::interpolate(
			0,
			(_narrowWidth - _back->width()) / 2,
			_narrowRatio);
		_back->moveToLeft(_leftTaken, backButtonTop);
		_leftTaken += _back->width();
	}
	if (_info && !_info->isHidden()) {
		_info->moveToLeft(_leftTaken, otherButtonsTop);
		_leftTaken += _info->width();
	} else if (_activeChat.key.topic()
		|| _activeChat.section == Section::ChatsList) {
		_leftTaken += st::normalFont->spacew;
	}

	if (_searchField) {
		const auto fieldLeft = _leftTaken;
		const auto fieldTop = searchFieldTop
			+ (height() - _searchField->height()) / 2;
		const auto fieldRight = st::dialogsFilterSkip
			+ st::dialogsFilterPadding.x();
		const auto fieldWidth = width() - fieldLeft - fieldRight;
		_searchField->setGeometryToLeft(
			fieldLeft,
			fieldTop,
			fieldWidth,
			_searchField->height());

		auto right = fieldLeft + fieldWidth;
		_searchCancel->moveToLeft(
			right - _searchCancel->width(),
			_searchField->y());
		right -= st::dialogsCalendar.width;
		if (_jumpToDate) {
			_jumpToDate->moveToLeft(right, _searchField->y());
		}
		right -= st::dialogsSearchFrom.width;
		if (_chooseFromUser) {
			_chooseFromUser->moveToLeft(right, _searchField->y());
		}
	}

	_rightTaken = 0;
	_menuToggle->moveToRight(_rightTaken, otherButtonsTop);
	if (_menuToggle->isHidden()) {
		_rightTaken += (_menuToggle->width() - _search->width());
	} else {
		_rightTaken += _menuToggle->width() + st::topBarSkip;
	}
	_infoToggle->moveToRight(_rightTaken, otherButtonsTop);
	if (!_infoToggle->isHidden()) {
		_infoToggle->moveToRight(_rightTaken, otherButtonsTop);
		_rightTaken += _infoToggle->width();
	}
	if (!_call->isHidden() || !_groupCall->isHidden()) {
		_call->moveToRight(_rightTaken, otherButtonsTop);
		_groupCall->moveToRight(_rightTaken, otherButtonsTop);
		_rightTaken += _call->width();
	}
	_search->moveToRight(_rightTaken, otherButtonsTop);
	if (!_search->isHidden()) {
		_rightTaken += _search->width() + st::topBarCallSkip;
	}

	updateMembersShowArea();
}

void TopBarWidget::finishAnimating() {
	_selectedShown.stop();
	updateControlsVisibility();
	update();
}

void TopBarWidget::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setAttribute(Qt::WA_OpaquePaintEvent, !_animatingMode);
		finishAnimating();
	}
}

void TopBarWidget::updateControlsVisibility() {
	if (!_activeChat.key) {
		return;
	} else if (_animatingMode) {
		hideChildren();
		return;
	}
	_clear->show();
	_delete->setVisible(_canDelete);
	_forward->setVisible(_canForward);
	_sendNow->setVisible(_canSendNow);

	const auto isOneColumn = _controller->adaptive().isOneColumn();
	auto backVisible = isOneColumn
		|| !_controller->content()->stackIsEmpty()
		|| (_activeChat.section == Section::ChatsList);
	_back->setVisible(backVisible && !_chooseForReportReason);
	_cancelChoose->setVisible(_chooseForReportReason.has_value());
	if (_info) {
		_info->setVisible(!_chooseForReportReason
			&& (isOneColumn || !_primaryWindow));
	}
	if (_unreadBadge) {
		_unreadBadge->setVisible(!_chooseForReportReason);
	}
	const auto topic = _activeChat.key.topic();
	const auto section = _activeChat.section;
	const auto historyMode = (section == Section::History);
	const auto hasPollsMenu = (_activeChat.key.peer()
		&& _activeChat.key.peer()->canCreatePolls())
		|| (topic && Data::CanSend(topic, ChatRestriction::SendPolls));
	const auto hasTopicMenu = [&] {
		if (!topic || section != Section::Replies) {
			return false;
		}
		auto empty = true;
		const auto callback = [&](const Ui::Menu::MenuCallback::Args&) {
			empty = false;
			return (QAction*)nullptr;
		};
		Window::FillDialogsEntryMenu(
			_controller,
			_activeChat,
			Ui::Menu::MenuCallback(callback));
		return !empty;
	}();
	const auto hasMenu = !_activeChat.key.folder()
		&& (section == Section::History
			? true
			: (section == Section::Scheduled)
			? hasPollsMenu
			: (section == Section::Replies)
			? (hasPollsMenu || hasTopicMenu)
			: (section == Section::ChatsList)
			? (_activeChat.key.peer() && _activeChat.key.peer()->isForum())
			: false);
	const auto hasInfo = !_activeChat.key.folder()
		&& (section == Section::History
			? true
			: (section == Section::Replies)
			? (_activeChat.key.topic() != nullptr)
			: false);
	updateSearchVisibility();
	if (_searchMode) {
		const auto hasSearchQuery = _searchField
			&& !_searchField->getLastText().isEmpty();
		if (!_jumpToDate || hasSearchQuery) {
			_searchCancel->show(anim::type::normal);
			if (_jumpToDate) {
				_jumpToDate->hide(anim::type::normal);
			}
		} else {
			_searchCancel->hide(anim::type::normal);
			_jumpToDate->show(anim::type::normal);
		}
	}
	_menuToggle->setVisible(hasMenu
		&& !_chooseForReportReason
		&& (_narrowRatio < 1.));
	_infoToggle->setVisible(hasInfo
		&& !isOneColumn
		&& _controller->canShowThirdSection()
		&& !_chooseForReportReason);
	const auto callsEnabled = [&] {
		if (const auto peer = _activeChat.key.peer()) {
			if (const auto user = peer->asUser()) {
				return !user->isSelf()
					&& !user->isBot()
					&& !peer->isServiceUser();
			}
		}
		return false;
	}();
	_call->setVisible(historyMode
		&& callsEnabled
		&& !_chooseForReportReason);
	const auto groupCallsEnabled = [&] {
		if (const auto peer = _activeChat.key.peer()) {
			if (peer->canManageGroupCall()) {
				return true;
			} else if (const auto call = peer->groupCall()) {
				return (call->fullCount() == 0);
			}
			return false;
		}
		return false;
	}();
	_groupCall->setVisible(historyMode
		&& groupCallsEnabled
		&& !_chooseForReportReason);

	if (_membersShowArea) {
		_membersShowArea->setVisible(!_chooseForReportReason);
	}
	updateControlsGeometry();
}

void TopBarWidget::updateMembersShowArea() {
	const auto membersShowAreaNeeded = [&] {
		const auto peer = _activeChat.key.peer();
		if (showSelectedState()
			|| !peer
			|| _activeChat.section == Section::ChatsList
			|| _activeChat.key.topic()) {
			return false;
		} else if (const auto chat = peer->asChat()) {
			return chat->amIn();
		} else if (const auto megagroup = peer->asMegagroup()) {
			return megagroup->canViewMembers()
				&& (megagroup->membersCount()
					< megagroup->session().serverConfig().chatSizeMax);
		}
		return false;
	}();
	if (!membersShowAreaNeeded) {
		if (_membersShowArea) {
			_membersShowAreaActive.fire(false);
			_membersShowArea.destroy();
		}
		return;
	} else if (!_membersShowArea) {
		_membersShowArea.create(this);
		_membersShowArea->show();
		_membersShowArea->installEventFilter(this);
	}
	_membersShowArea->setGeometry(getMembersShowAreaGeometry());
}

bool TopBarWidget::showSelectedState() const {
	return (_selectedCount > 0)
		&& (_canDelete || _canForward || _canSendNow);
}

void TopBarWidget::showSelected(SelectedState state) {
	auto canDelete = (state.count > 0 && state.count == state.canDeleteCount);
	auto canForward = (state.count > 0 && state.count == state.canForwardCount);
	auto canSendNow = (state.count > 0 && state.count == state.canSendNowCount);
	auto count = (!canDelete && !canForward && !canSendNow) ? 0 : state.count;
	if (_selectedCount == count
		&& _canDelete == canDelete
		&& _canForward == canForward
		&& _canSendNow == canSendNow) {
		return;
	}
	if (count == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
		canForward = _canForward;
		canSendNow = _canSendNow;
	}

	const auto wasSelectedState = showSelectedState();
	const auto visibilityChanged = (_canDelete != canDelete)
		|| (_canForward != canForward)
		|| (_canSendNow != canSendNow);
	_selectedCount = count;
	_canDelete = canDelete;
	_canForward = canForward;
	_canSendNow = canSendNow;
	const auto nowSelectedState = showSelectedState();
	if (nowSelectedState) {
		_forward->setNumbersText(_selectedCount);
		_sendNow->setNumbersText(_selectedCount);
		_delete->setNumbersText(_selectedCount);
		if (!wasSelectedState) {
			_forward->finishNumbersAnimation();
			_sendNow->finishNumbersAnimation();
			_delete->finishNumbersAnimation();
		}
	}
	if (visibilityChanged) {
		updateControlsVisibility();
	}
	if (wasSelectedState != nowSelectedState && !_chooseForReportReason) {
		setCursor(nowSelectedState
			? style::cur_default
			: style::cur_pointer);

		updateMembersShowArea();
		toggleSelectedControls(nowSelectedState);
	} else {
		updateControlsGeometry();
	}
}

bool TopBarWidget::toggleSearch(bool shown, anim::type animated) {
	if (_searchMode == shown) {
		if (animated == anim::type::instant) {
			_searchShown.stop();
		}
		return false;
	}
	_searchMode = shown;
	if (shown && !_searchField) {
		_searchField.create(this, st::dialogsFilter, tr::lng_dlg_filter());
		_searchField->setFocusPolicy(Qt::StrongFocus);
		_searchField->customUpDown(true);
		_searchField->show();
		_searchCancel.create(this, st::dialogsCancelSearch);
		_searchCancel->show(anim::type::instant);
		_searchCancel->setClickedCallback([=] { _searchCancelled.fire({}); });
		_searchField->submits(
		) | rpl::start_with_next([=] {
			_searchSubmitted.fire({});
		}, _searchField->lifetime());
		_searchField->changes(
		) | rpl::start_with_next([=] {
			const auto was = _searchQuery.current();
			const auto now = _searchField->getLastText();
			if (_jumpToDate && was.isEmpty() != now.isEmpty()) {
				updateControlsVisibility();
			}
			if (_chooseFromUser) {
				auto switchToChooseFrom = SwitchToChooseFromQuery();
				if (was != switchToChooseFrom
					&& switchToChooseFrom.startsWith(was)
					&& now == switchToChooseFrom) {
					_chooseFromUserRequests.fire({});
				}
			}
			_searchQuery = now;
		}, _searchField->lifetime());
	} else {
		Assert(_searchField != nullptr);
	}
	_searchQuery = shown ? _searchField->getLastText() : QString();
	if (animated == anim::type::normal) {
		_searchShown.start(
			[=] { slideAnimationCallback(); },
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			st::slideWrapDuration,
			anim::easeOutCirc);
	} else {
		_searchShown.stop();
		slideAnimationCallback();
	}
	if (shown) {
		_searchField->setFocusFast();
	}
	return true;
}

void TopBarWidget::searchEnableJumpToDate(bool enable) {
	if (!_searchMode) {
		return;
	} else if (!enable) {
		_jumpToDate.destroy();
	} else if (!_jumpToDate) {
		_jumpToDate.create(
			this,
			object_ptr<Ui::IconButton>(this, st::dialogsCalendar));
		_jumpToDate->toggle(
			_searchField->getLastText().isEmpty(),
			anim::type::instant);
		_jumpToDate->entity()->clicks(
		) | rpl::to_empty | rpl::start_to_stream(
			_jumpToDateRequests,
			_jumpToDate->lifetime());
	}
	updateControlsVisibility();
	updateControlsGeometry();
}

void TopBarWidget::searchEnableChooseFromUser(bool enable, bool visible) {
	if (!_searchMode) {
		return;
	} else if (!enable) {
		_chooseFromUser.destroy();
	} else if (!_chooseFromUser) {
		_chooseFromUser.create(
			this,
			object_ptr<Ui::IconButton>(this, st::dialogsSearchFrom));
		_chooseFromUser->toggle(visible, anim::type::instant);
		_chooseFromUser->entity()->clicks(
		) | rpl::to_empty | rpl::start_to_stream(
			_chooseFromUserRequests,
			_chooseFromUser->lifetime());
	} else {
		_chooseFromUser->toggle(visible, anim::type::normal);
	}
	auto additional = QMargins();
	if (_chooseFromUser && _chooseFromUser->toggled()) {
		additional.setRight(_chooseFromUser->width());
	}
	_searchField->setAdditionalMargins(additional);
	updateControlsVisibility();
	updateControlsGeometry();
}

bool TopBarWidget::searchSetFocus() {
	if (!_searchMode) {
		return false;
	}
	_searchField->setFocus();
	return true;
}

bool TopBarWidget::searchMode() const {
	return _searchMode;
}

bool TopBarWidget::searchHasFocus() const {
	return _searchMode && _searchField->hasFocus();
}

rpl::producer<> TopBarWidget::searchCancelled() const {
	return _searchCancelled.events();
}

rpl::producer<> TopBarWidget::searchSubmitted() const {
	return _searchSubmitted.events();
}

rpl::producer<QString> TopBarWidget::searchQuery() const {
	return _searchQuery.value();
}

QString TopBarWidget::searchQueryCurrent() const {
	return _searchQuery.current();
}

int TopBarWidget::searchQueryCursorPosition() const {
	return _searchMode
		? _searchField->textCursor().position()
		: _searchQuery.current().size();
}

void TopBarWidget::searchClear() {
	if (_searchMode) {
		_searchField->clear();
	}
}

void TopBarWidget::searchSetText(const QString &query, int cursorPosition) {
	if (_searchMode) {
		if (cursorPosition < 0) {
			cursorPosition = query.size();
		}
		_searchField->setText(query);
		_searchField->setCursorPosition(cursorPosition);
	}
}

void TopBarWidget::toggleSelectedControls(bool shown) {
	_selectedShown.start(
		[this] { slideAnimationCallback(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration,
		anim::easeOutCirc);
}

void TopBarWidget::setGeometryWithNarrowRatio(
		QRect geometry,
		int narrowWidth,
		float64 narrowRatio) {
	if (_activeChat.section != Section::ChatsList) {
		narrowRatio = 0.;
		narrowWidth = 0;
	}
	const auto changed = (_narrowRatio != narrowRatio);
	const auto started = (_narrowRatio == 0.) != (narrowRatio == 0.);
	const auto finished = (_narrowRatio == 1.) != (narrowRatio == 1.);
	const auto resized = (size() != geometry.size());
	_narrowRatio = narrowRatio;
	_narrowWidth = narrowWidth;
	if (started || finished) {
		updateControlsVisibility();
	}
	setGeometry(geometry);
	if (changed && !resized) {
		updateSearchVisibility();
		updateControlsGeometry();
	}
}

bool TopBarWidget::showSelectedActions() const {
	return showSelectedState() && !_chooseForReportReason;
}

void TopBarWidget::slideAnimationCallback() {
	updateControlsGeometry();
	update();
}

void TopBarWidget::updateAdaptiveLayout() {
	updateControlsVisibility();
	updateInfoToggleActive();
	refreshUnreadBadge();
}

void TopBarWidget::refreshUnreadBadge() {
	if (!_controller->adaptive().isOneColumn() && !_activeChat.key.folder()) {
		_unreadBadge.destroy();
		return;
	} else if (_unreadBadge) {
		return;
	}
	_unreadBadge.create(this);

	rpl::combine(
		_back->geometryValue(),
		_unreadBadge->widthValue()
	) | rpl::start_with_next([=](QRect geometry, int width) {
		_unreadBadge->move(
			geometry.x() + geometry.width() - width,
			geometry.y() + st::titleUnreadCounterTop);
	}, _unreadBadge->lifetime());

	_unreadBadge->show();
	_unreadBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
	_controller->session().data().unreadBadgeChanges(
	) | rpl::start_with_next([=] {
		updateUnreadBadge();
	}, _unreadBadge->lifetime());
	updateUnreadBadge();
}

void TopBarWidget::updateUnreadBadge() {
	if (!_unreadBadge) return;

	const auto key = _activeChat.key;
	const auto muted = session().data().unreadBadgeMutedIgnoreOne(key);
	const auto counter = session().data().unreadBadgeIgnoreOne(key);
	const auto text = [&] {
		if (!counter) {
			return QString();
		}
		return (counter > 999)
			? u"..%1"_q.arg(counter % 100, 2, 10, QChar('0'))
			: QString::number(counter);
	}();
	_unreadBadge->setText(text, !muted);
}

void TopBarWidget::updateInfoToggleActive() {
	auto infoThirdActive = _controller->adaptive().isThreeColumn()
		&& (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo());
	auto iconOverride = infoThirdActive
		? &st::topBarInfoActive
		: nullptr;
	auto rippleOverride = infoThirdActive
		? &st::lightButtonBgOver
		: nullptr;
	_infoToggle->setIconOverride(iconOverride, iconOverride);
	_infoToggle->setRippleColorOverride(rippleOverride);
}

void TopBarWidget::setupDragOnBackButton() {
	_backLifetime.destroy();
	if (_activeChat.section != Section::ChatsList) {
		_back->setAcceptDrops(false);
		return;
	}
	const auto lifetime = _backLifetime.make_state<rpl::lifetime>();
	_back->setAcceptDrops(true);
	_back->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return e->type() == QEvent::DragEnter;
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		using namespace Storage;
		const auto d = static_cast<QDragEnterEvent*>(e.get());
		const auto data = d->mimeData();
		if (ComputeMimeDataState(data) == MimeDataState::None) {
			return;
		}
		const auto timer = _backLifetime.make_state<base::Timer>([=] {
			backClicked();
		});
		timer->callOnce(ChoosePeerByDragTimeout);
		d->setDropAction(Qt::CopyAction);
		d->accept();
		_back->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return e->type() == QEvent::DragMove
				|| e->type() == QEvent::DragLeave;
		}) | rpl::start_with_next([=](not_null<QEvent*> e) {
			if (e->type() == QEvent::DragMove) {
				timer->callOnce(ChoosePeerByDragTimeout);
			} else if (e->type() == QEvent::DragLeave) {
				timer->cancel();
				lifetime->destroy();
			}
		}, *lifetime);
	}, _backLifetime);
}

bool TopBarWidget::trackOnlineOf(not_null<PeerData*> user) const {
	const auto peer = _activeChat.key.peer();
	if (!peer || _activeChat.key.topic() || !user->isUser()) {
		return false;
	} else if (peer->isUser()) {
		return (peer == user);
	} else if (const auto chat = peer->asChat()) {
		return chat->participants.contains(user->asUser());
	} else if (const auto channel = peer->asMegagroup()) {
		return channel->canViewMembers()
			&& ranges::contains(
				channel->mgInfo->lastParticipants,
				not_null{ user->asUser() });
	}
	return false;
}

void TopBarWidget::updateOnlineDisplay() {
	const auto peer = _activeChat.key.peer();
	if (!peer || _activeChat.key.topic()) {
		return;
	}

	QString text;
	const auto now = base::unixtime::now();
	bool titlePeerTextOnline = false;
	if (const auto user = peer->asUser()) {
		if (session().supportMode()
			&& !session().supportHelper().infoCurrent(user).text.empty()) {
			text = QString::fromUtf8("\xe2\x9a\xa0\xef\xb8\x8f check info");
			titlePeerTextOnline = false;
		} else {
			text = Data::OnlineText(user, now);
			titlePeerTextOnline = Data::OnlineTextActive(user, now);
		}
	} else if (const auto chat = peer->asChat()) {
		if (!chat->amIn()) {
			text = tr::lng_chat_status_unaccessible(tr::now);
		} else if (chat->participants.empty()) {
			if (!_titlePeerText.isEmpty()) {
				text = _titlePeerText.toString();
			} else if (chat->count <= 0) {
				text = tr::lng_group_status(tr::now);
			} else {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->count);
			}
		} else {
			const auto self = session().user();
			auto online = 0;
			auto onlyMe = true;
			for (const auto &user : chat->participants) {
				if (user->lastseen().isOnline(now)) {
					++online;
					if (onlyMe && user != self) onlyMe = false;
				}
			}
			if (online > 0 && !onlyMe) {
				auto membersCount = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->participants.size());
				auto onlineCount = tr::lng_chat_status_online(tr::now, lt_count, online);
				text = tr::lng_chat_status_members_online(tr::now, lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (chat->participants.size() > 0) {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->participants.size());
			} else {
				text = tr::lng_group_status(tr::now);
			}
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->isMegagroup()
			&& channel->canViewMembers()
			&& (channel->membersCount() > 0)
			&& (channel->membersCount()
				<= channel->session().serverConfig().chatSizeMax)) {
			if (channel->lastParticipantsRequestNeeded()) {
				session().api().chatParticipants().requestLast(channel);
			}
			const auto self = session().user();
			auto online = 0;
			auto onlyMe = true;
			for (auto &participant : std::as_const(channel->mgInfo->lastParticipants)) {
				if (participant->lastseen().isOnline(now)) {
					++online;
					if (onlyMe && participant != self) {
						onlyMe = false;
					}
				}
			}
			if (online && !onlyMe) {
				auto membersCount = tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount());
				auto onlineCount = tr::lng_chat_status_online(tr::now, lt_count, online);
				text = tr::lng_chat_status_members_online(tr::now, lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (channel->membersCount() > 0) {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount());
			} else {
				text = tr::lng_group_status(tr::now);
			}
		} else if (channel->membersCount() > 0) {
			text = channel->isMegagroup()
				? tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount())
				: tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, channel->membersCount());

		} else {
			text = channel->isMegagroup() ? tr::lng_group_status(tr::now) : tr::lng_channel_status(tr::now);
		}
	}
	if (_titlePeerText.toString() != text) {
		_titlePeerText.setText(st::dialogsTextStyle, text);
		_titlePeerTextOnline = titlePeerTextOnline;
		updateMembersShowArea();
		update();
	}
	updateOnlineDisplayTimer();
}

void TopBarWidget::updateOnlineDisplayTimer() {
	const auto peer = _activeChat.key.peer();
	if (!peer) {
		return;
	}

	const auto now = base::unixtime::now();
	auto minTimeout = 86400 * crl::time(1000);
	const auto handleUser = [&](not_null<UserData*> user) {
		const auto hisTimeout = Data::OnlineChangeTimeout(user, now);
		accumulate_min(minTimeout, hisTimeout);
	};
	if (const auto user = peer->asUser()) {
		handleUser(user);
	} else if (const auto chat = peer->asChat()) {
		for (const auto &user : chat->participants) {
			handleUser(user);
		}
	} else if (peer->isChannel()) {
	}
	updateOnlineDisplayIn(minTimeout);
}

void TopBarWidget::updateOnlineDisplayIn(crl::time timeout) {
	_onlineUpdater.callOnce(timeout);
}

} // namespace HistoryView
