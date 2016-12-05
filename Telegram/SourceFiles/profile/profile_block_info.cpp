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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "profile/profile_block_info.h"

#include "profile/profile_block_common_groups.h"
#include "profile/profile_section_memento.h"
#include "styles/style_profile.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/widget_slide_wrap.h"
#include "core/click_handler_types.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "lang.h"

namespace Profile {

constexpr int kCommonGroupsLimit = 20;

using UpdateFlag = Notify::PeerUpdate::Flag;

InfoWidget::InfoWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_info_section)) {
	auto observeEvents = UpdateFlag::AboutChanged
		| UpdateFlag::UsernameChanged
		| UpdateFlag::UserPhoneChanged
		| UpdateFlag::UserCanShareContact
		| UpdateFlag::UserCommonChatsChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	refreshLabels();
	if (_commonGroups && _commonGroups->isHidden()) {
		_commonGroups->show();
		refreshVisibility();
	}
}

void InfoWidget::showFinished() {
	_showFinished = true;
	if (_commonGroups && _commonGroups->isHidden() && getCommonGroupsCount() > 0) {
		slideCommonGroupsDown();
	}
}

void InfoWidget::slideCommonGroupsDown() {
	_commonGroups->show();
	refreshVisibility();
	_height.start([this] { contentSizeUpdated(); }, isHidden() ? 0 : height(), resizeGetHeight(width()), st::widgetSlideDuration);
	contentSizeUpdated();
}

void InfoWidget::restoreState(const SectionMemento *memento) {
	if (!memento->getCommonGroups().isEmpty()) {
		onForceHideCommonGroups();
	}
}

void InfoWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		return;
	}

	if (update.flags & UpdateFlag::AboutChanged) {
		refreshAbout();
	}
	if (update.flags & UpdateFlag::UsernameChanged) {
		refreshUsername();
		refreshChannelLink();
	}
	if (update.flags & (UpdateFlag::UserPhoneChanged | UpdateFlag::UserCanShareContact)) {
		refreshMobileNumber();
	}
	if (update.flags & UpdateFlag::UserCommonChatsChanged) {
		refreshCommonGroups();
	}
	refreshVisibility();

	contentSizeUpdated();
}

int InfoWidget::resizeGetHeight(int newWidth) {
	int initialHeight = contentTop();
	int newHeight = initialHeight;

	int marginLeft = st::profileBlockTextPart.margin.left();
	int marginRight = st::profileBlockTextPart.margin.right();
	int left = st::profileBlockTitlePosition.x();
	if (_about) {
		int textWidth = _about->naturalWidth();
		int availableWidth = newWidth - left - st::profileBlockMarginRight;
		int maxWidth = st::msgMaxWidth;
		accumulate_min(textWidth, availableWidth);
		accumulate_min(textWidth, st::msgMaxWidth);
		_about->resizeToWidth(textWidth + marginLeft + marginRight);
		_about->moveToLeft(left - marginLeft, newHeight - st::profileBlockTextPart.margin.top());
		newHeight += _about->height();
	}

	auto moveLabeledText = [&newHeight, left, newWidth, marginLeft, marginRight](Ui::FlatLabel *label, Ui::FlatLabel *text, Ui::FlatLabel *shortText) {
		if (!label) return;

		label->moveToLeft(left, newHeight);
		int textLeft = left + label->width() + st::normalFont->spacew;
		int textWidth = text->naturalWidth();
		int availableWidth = newWidth - textLeft - st::profileBlockMarginRight;
		bool doesNotFit = (textWidth > availableWidth);
		accumulate_min(textWidth, availableWidth);
		accumulate_min(textWidth, st::msgMaxWidth);
		text->resizeToWidth(textWidth + marginLeft + marginRight);
		text->moveToLeft(textLeft - marginLeft, newHeight - st::profileBlockOneLineTextPart.margin.top());
		if (shortText) {
			shortText->resizeToWidth(textWidth + marginLeft + marginRight);
			shortText->moveToLeft(textLeft - marginLeft, newHeight - st::profileBlockOneLineTextPart.margin.top());
			if (doesNotFit) {
				shortText->show();
				text->hide();
			} else {
				shortText->hide();
				text->show();
			}
		}
		newHeight += label->height() + st::profileBlockOneLineSkip;
	};
	moveLabeledText(_channelLinkLabel, _channelLink, _channelLinkShort);
	moveLabeledText(_mobileNumberLabel, _mobileNumber, nullptr);
	moveLabeledText(_usernameLabel, _username, nullptr);

	if (_commonGroups && !_commonGroups->isHidden()) {
		int left = defaultOutlineButtonLeft();
		int availableWidth = newWidth - left - st::profileBlockMarginRight;
		accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);

		_commonGroups->resizeToWidth(availableWidth);
		_commonGroups->moveToLeft(left, newHeight);
		newHeight += _commonGroups->height();
	}

	newHeight += st::profileBlockMarginBottom;
	return qRound(_height.current(newHeight));
}

void InfoWidget::leaveEvent(QEvent *e) {
	BotCommandClickHandler::setPeerForCommand(nullptr);
	BotCommandClickHandler::setBotForCommand(nullptr);
}

void InfoWidget::refreshLabels() {
	refreshAbout();
	refreshMobileNumber();
	refreshUsername();
	refreshChannelLink();
	refreshCommonGroups();

	refreshVisibility();
}

void InfoWidget::refreshVisibility() {
	setVisible(_about || _mobileNumber || _username || _channelLink || (_commonGroups && !_commonGroups->isHidden()));
}

void InfoWidget::refreshAbout() {
	auto getAboutText = [this]() -> QString {
		if (auto user = peer()->asUser()) {
			return user->about();
		} else if (auto channel = peer()->asChannel()) {
			return channel->about();
		}
		return QString();
	};

	_about.destroy();
	auto aboutText = textClean(getAboutText());
	if (!aboutText.isEmpty()) {
		_about.create(this, st::profileBlockTextPart);
		_about->show();

		EntitiesInText aboutEntities;
		textParseEntities(aboutText, TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands, &aboutEntities);
		_about->setMarkedText({ aboutText, aboutEntities });
		_about->setSelectable(true);
		_about->setClickHandlerHook([this](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			BotCommandClickHandler::setPeerForCommand(peer());
			return true;
		});
	}
}

void InfoWidget::refreshMobileNumber() {
	TextWithEntities phoneText;
	if (auto user = peer()->asUser()) {
		if (!user->phone().isEmpty()) {
			phoneText.text = App::formatPhone(user->phone());
		} else {
			phoneText.text = App::phoneFromSharedContact(peerToUser(user->id));
		}
	}
	setLabeledText(&_mobileNumberLabel, lang(lng_profile_mobile_number), &_mobileNumber, phoneText, lang(lng_profile_copy_phone));
}

void InfoWidget::refreshUsername() {
	TextWithEntities usernameText;
	if (auto user = peer()->asUser()) {
		if (!user->username.isEmpty()) {
			usernameText.text = '@' + user->username;
		}
	}
	setLabeledText(&_usernameLabel, lang(lng_profile_username), &_username, usernameText, lang(lng_context_copy_mention));
}

void InfoWidget::refreshChannelLink() {
	TextWithEntities channelLinkText;
	TextWithEntities channelLinkTextShort;
	if (auto channel = peer()->asChannel()) {
		if (!channel->username.isEmpty()) {
			channelLinkText.text = qsl("https://telegram.me/") + channel->username;
			channelLinkText.entities.push_back(EntityInText(EntityInTextUrl, 0, channelLinkText.text.size()));
			channelLinkTextShort.text = qsl("telegram.me/") + channel->username;
			channelLinkTextShort.entities.push_back(EntityInText(EntityInTextCustomUrl, 0, channelLinkTextShort.text.size(), qsl("https://telegram.me/") + channel->username));
		}
	}
	setLabeledText(nullptr, lang(lng_profile_link), &_channelLink, channelLinkText, QString());
	setLabeledText(&_channelLinkLabel, lang(lng_profile_link), &_channelLinkShort, channelLinkTextShort, QString());
	if (_channelLinkShort) {
		_channelLinkShort->setExpandLinksMode(ExpandLinksUrlOnly);
	}
}

int InfoWidget::getCommonGroupsCount() const {
	if (auto user = peer()->asUser()) {
		return user->commonChatsCount();
	}
	return 0;
}

void InfoWidget::refreshCommonGroups() {
	if (auto count = (_forceHiddenCommonGroups ? 0 : getCommonGroupsCount())) {
		auto text = lng_profile_common_groups(lt_count, count);
		if (_commonGroups) {
			_commonGroups->setText(text);
		} else {
			_commonGroups.create(this, text, st::defaultLeftOutlineButton);
			_commonGroups->setClickedCallback([this] { onShowCommonGroups(); });
			_commonGroups->hide();
			if (_showFinished) {
				slideCommonGroupsDown();
			}
		}
	} else if (_commonGroups) {
		_commonGroups.destroyDelayed();
	}
}

void InfoWidget::setShowCommonGroupsObservable(base::Observable<CommonGroupsEvent> *observable) {
	_showCommonGroupsObservable = observable;
	subscribe(_showCommonGroupsObservable, [this](const CommonGroupsEvent &event) {
		onForceHideCommonGroups();
	});
}

void InfoWidget::onForceHideCommonGroups() {
	if (_forceHiddenCommonGroups) {
		return;
	}
	_forceHiddenCommonGroups = true;
	_commonGroups.destroyDelayed();
	refreshVisibility();
	contentSizeUpdated();
}

void InfoWidget::onShowCommonGroups() {
	auto count = getCommonGroupsCount();
	if (count <= 0) {
		refreshCommonGroups();
		return;
	}
	if (_getCommonGroupsRequestId) {
		return;
	}
	auto user = peer()->asUser();
	t_assert(user != nullptr);
	auto request = MTPmessages_GetCommonChats(user->inputUser, MTP_int(0), MTP_int(kCommonGroupsLimit));
	_getCommonGroupsRequestId = MTP::send(request, ::rpcDone(base::lambda_guarded(this, [this](const MTPmessages_Chats &result) {
		_getCommonGroupsRequestId = 0;

		CommonGroupsEvent event;
		if (auto chats = Api::getChatsFromMessagesChats(result)) {
			auto &list = chats->c_vector().v;
			event.groups.reserve(list.size());
			for_const (auto &chatData, list) {
				if (auto chat = App::feedChat(chatData)) {
					event.groups.push_back(chat);
				}
			}
		}

		auto oldHeight = height();
		onForceHideCommonGroups();
		if (!event.groups.empty() && _showCommonGroupsObservable) {
			event.initialHeight = oldHeight - (isHidden() ? 0 : height());
			_showCommonGroupsObservable->notify(event, true);
		}
	})));
}

void InfoWidget::setLabeledText(ChildWidget<Ui::FlatLabel> *labelWidget, const QString &label,
	ChildWidget<Ui::FlatLabel> *textWidget, const TextWithEntities &textWithEntities, const QString &copyText) {
	if (labelWidget) labelWidget->destroy();
	textWidget->destroy();
	if (textWithEntities.text.isEmpty()) return;

	if (labelWidget) {
		labelWidget->create(this, label, Ui::FlatLabel::InitType::Simple, st::profileBlockLabel);
		(*labelWidget)->show();
	}
	textWidget->create(this, QString(), Ui::FlatLabel::InitType::Simple, st::profileBlockOneLineTextPart);
	(*textWidget)->show();
	(*textWidget)->setMarkedText(textWithEntities);
	(*textWidget)->setContextCopyText(copyText);
	(*textWidget)->setSelectable(true);
	(*textWidget)->setDoubleClickSelectsParagraph(true);
}

} // namespace Profile
