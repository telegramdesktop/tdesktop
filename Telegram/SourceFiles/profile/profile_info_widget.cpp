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
#include "profile/profile_info_widget.h"

#include "styles/style_profile.h"
#include "ui/flatlabel.h"
#include "core/click_handler_types.h"
#include "observer_peer.h"
#include "lang.h"

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

InfoWidget::InfoWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_info_section)) {
	auto observeEvents = UpdateFlag::AboutChanged
		| UpdateFlag::UsernameChanged
		| UpdateFlag::UserPhoneChanged
		| UpdateFlag::UserCanShareContact;
	Notify::registerPeerObserver(observeEvents, this, &InfoWidget::notifyPeerUpdated);

	refreshLabels();
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
	refreshVisibility();

	contentSizeUpdated();
}

int InfoWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop();

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

	auto moveLabeledText = [&newHeight, left, newWidth, marginLeft, marginRight](FlatLabel *label, FlatLabel *text, FlatLabel *shortText) {
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

	newHeight += st::profileBlockMarginBottom;
	return newHeight;
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

	refreshVisibility();
}

void InfoWidget::refreshVisibility() {
	setVisible(_about || _mobileNumber || _username || _channelLink);
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
	auto aboutText = getAboutText();
	if (!aboutText.isEmpty()) {
		_about = new FlatLabel(this, st::profileBlockTextPart);
		_about->show();

		EntitiesInText aboutEntities;
		textParseEntities(aboutText, TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands, &aboutEntities);
		_about->setMarkedText({ aboutText, aboutEntities });
		_about->setSelectable(true);
		_about->setClickHandlerHook(func(this, &InfoWidget::aboutClickHandlerHook));
	}
}

bool InfoWidget::aboutClickHandlerHook(const ClickHandlerPtr &handler, Qt::MouseButton button) {
	BotCommandClickHandler::setPeerForCommand(peer());
	return true;
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

void InfoWidget::setLabeledText(ChildWidget<FlatLabel> *labelWidget, const QString &label,
	ChildWidget<FlatLabel> *textWidget, const TextWithEntities &textWithEntities, const QString &copyText) {
	if (labelWidget) labelWidget->destroy();
	textWidget->destroy();
	if (textWithEntities.text.isEmpty()) return;

	if (labelWidget) {
		*labelWidget = new FlatLabel(this, label, FlatLabel::InitType::Simple, st::profileBlockLabel);
		(*labelWidget)->show();
	}
	*textWidget = new FlatLabel(this, QString(), FlatLabel::InitType::Simple, st::profileBlockOneLineTextPart);
	(*textWidget)->show();
	(*textWidget)->setMarkedText(textWithEntities);
	(*textWidget)->setContextCopyText(copyText);
	(*textWidget)->setSelectable(true);
	(*textWidget)->setDoubleClickSelectsParagraph(true);
}

} // namespace Profile
