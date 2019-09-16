/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/ui_integration.h"

#include "ui/text/text_entity.h"
#include "ui/basic_click_handlers.h"

namespace Ui {
namespace {

Integration *IntegrationInstance = nullptr;

} // namespace

void Integration::Set(not_null<Integration*> instance) {
	IntegrationInstance = instance;
}

Integration &Integration::Instance() {
	Expects(IntegrationInstance != nullptr);

	return *IntegrationInstance;
}

void Integration::textActionsUpdated() {
}

void Integration::activationFromTopPanel() {		
}
	
std::shared_ptr<ClickHandler> Integration::createLinkHandler(
		EntityType type,
		const QString &text,
		const QString &data,
		const TextParseOptions &options) {
	switch (type) {
	case EntityType::CustomUrl:
		return !data.isEmpty()
			? std::make_shared<UrlClickHandler>(data, false)
			: nullptr;
	}
	return nullptr;
}

bool Integration::handleUrlClick(
		const QString &url,
		const QVariant &context) {
	return false;
}

QString Integration::convertTagToMimeTag(const QString &tagId) {
	return tagId;
}

const Emoji::One *Integration::defaultEmojiVariant(const Emoji::One *emoji) {
	return emoji;
}

rpl::producer<> Integration::forcePopupMenuHideRequests() {
	return rpl::never<rpl::empty_value>();
}

QString Integration::phraseContextCopyText() {
	return "Copy text";
}

QString Integration::phraseContextCopyEmail() {
	return "Copy email";
}

QString Integration::phraseContextCopyLink() {
	return "Copy link";
}

QString Integration::phraseContextCopySelected() {
	return "Copy to clipboard";
}

QString Integration::phraseFormattingTitle() {
	return "Formatting";
}

QString Integration::phraseFormattingLinkCreate() {
	return "Create link";
}

QString Integration::phraseFormattingLinkEdit() {
	return "Edit link";
}

QString Integration::phraseFormattingClear() {
	return "Plain text";
}

QString Integration::phraseFormattingBold() {
	return "Bold";
}

QString Integration::phraseFormattingItalic() {
	return "Italic";
}

QString Integration::phraseFormattingUnderline() {
	return "Underline";
}

QString Integration::phraseFormattingStrikeOut() {
	return "Strike-through";
}

QString Integration::phraseFormattingMonospace() {
	return "Monospace";
}

} // namespace Ui
