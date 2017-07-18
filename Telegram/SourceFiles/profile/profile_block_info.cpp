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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "profile/profile_block_info.h"

#include "styles/style_profile.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/widget_slide_wrap.h"
#include "core/click_handler_types.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "messenger.h"

namespace Profile {

constexpr int kCommonGroupsLimit = 20;

using UpdateFlag = Notify::PeerUpdate::Flag;

InfoWidget::InfoWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_info_section)) {
	auto observeEvents = UpdateFlag::AboutChanged
		| UpdateFlag::UsernameChanged
		| UpdateFlag::UserPhoneChanged
		| UpdateFlag::UserCanShareContact;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

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
		newHeight += qMax(label->height(), text->height() - st::profileBlockTextPart.margin.top() - st::profileBlockTextPart.margin.bottom()) + st::profileBlockOneLineSkip;
	};
	moveLabeledText(_channelLinkLabel, _channelLink, _channelLinkShort);
	moveLabeledText(_mobileNumberLabel, _mobileNumber, nullptr);
	moveLabeledText(_bioLabel, _bio, nullptr);
	moveLabeledText(_usernameLabel, _username, nullptr);

	newHeight += st::profileBlockMarginBottom;
	return newHeight;
}

void InfoWidget::leaveEventHook(QEvent *e) {
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
	setVisible(_about || _mobileNumber || _username || _bio || _channelLink);
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
	_bioLabel.destroy();
	_bio.destroy();
	auto aboutText = TextWithEntities { TextUtilities::Clean(getAboutText()) };
	auto displayAsBio = false;
	if (auto user = peer()->asUser()) {
		if (!user->botInfo) {
			displayAsBio = true;
		}
	}
	if (displayAsBio) {
		aboutText.text = TextUtilities::SingleLine(aboutText.text);
	}
	if (!aboutText.text.isEmpty()) {
		if (displayAsBio) {
			setLabeledText(&_bioLabel, lang(lng_profile_bio), &_bio, aboutText, st::profileBioLabel, QString());
		} else {
			_about.create(this, st::profileBlockTextPart);
			_about->show();

			TextUtilities::ParseEntities(aboutText, TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands);
			_about->setMarkedText(aboutText);
			_about->setSelectable(true);
			_about->setClickHandlerHook([this](const ClickHandlerPtr &handler, Qt::MouseButton button) {
				BotCommandClickHandler::setPeerForCommand(peer());
				return true;
			});
		}
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
	setSingleLineLabeledText(&_mobileNumberLabel, lang(lng_profile_mobile_number), &_mobileNumber, phoneText, lang(lng_profile_copy_phone));
}

void InfoWidget::refreshUsername() {
	TextWithEntities usernameText;
	if (auto user = peer()->asUser()) {
		if (!user->username.isEmpty()) {
			usernameText.text = '@' + user->username;
		}
	}
	setSingleLineLabeledText(&_usernameLabel, lang(lng_profile_username), &_username, usernameText, lang(lng_context_copy_mention));
}

void InfoWidget::refreshChannelLink() {
	TextWithEntities channelLinkText;
	TextWithEntities channelLinkTextShort;
	if (auto channel = peer()->asChannel()) {
		if (!channel->username.isEmpty()) {
			channelLinkText.text = Messenger::Instance().createInternalLinkFull(channel->username);
			channelLinkText.entities.push_back(EntityInText(EntityInTextUrl, 0, channelLinkText.text.size()));
			channelLinkTextShort.text = Messenger::Instance().createInternalLink(channel->username);
			channelLinkTextShort.entities.push_back(EntityInText(EntityInTextCustomUrl, 0, channelLinkTextShort.text.size(), Messenger::Instance().createInternalLinkFull(channel->username)));
		}
	}
	setSingleLineLabeledText(nullptr, lang(lng_profile_link), &_channelLink, channelLinkText, QString());
	setSingleLineLabeledText(&_channelLinkLabel, lang(lng_profile_link), &_channelLinkShort, channelLinkTextShort, QString());
	if (_channelLinkShort) {
		_channelLinkShort->setExpandLinksMode(ExpandLinksUrlOnly);
	}
}

void InfoWidget::setLabeledText(object_ptr<Ui::FlatLabel> *labelWidget, const QString &label,
	object_ptr<Ui::FlatLabel> *textWidget, const TextWithEntities &textWithEntities,
	const style::FlatLabel &st, const QString &copyText) {
	if (labelWidget) labelWidget->destroy();
	textWidget->destroy();
	if (textWithEntities.text.isEmpty()) {
		return;
	}

	if (labelWidget) {
		labelWidget->create(this, label, Ui::FlatLabel::InitType::Simple, st::profileBlockLabel);
		(*labelWidget)->show();
	}
	textWidget->create(this, QString(), Ui::FlatLabel::InitType::Simple, st);
	(*textWidget)->show();
	(*textWidget)->setMarkedText(textWithEntities);
	(*textWidget)->setContextCopyText(copyText);
	(*textWidget)->setSelectable(true);
}

void InfoWidget::setSingleLineLabeledText(object_ptr<Ui::FlatLabel> *labelWidget, const QString &label,
	object_ptr<Ui::FlatLabel> *textWidget, const TextWithEntities &textWithEntities, const QString &copyText) {
	setLabeledText(labelWidget, label, textWidget, textWithEntities, st::profileBlockOneLineTextPart, copyText);
	if (*textWidget) {
		(*textWidget)->setDoubleClickSelectsParagraph(true);
	}
}

} // namespace Profile
