/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

// Methods that must be implemented outside lib_ui.

class QString;
class QWidget;
class QVariant;

struct TextParseOptions;
class ClickHandler;
enum class EntityType;

namespace Ui {
namespace Emoji {
class One;
} // namespace Emoji

class Integration {
public:
	static void Set(not_null<Integration*> instance);
	static Integration &Instance();

	virtual void postponeCall(FnMut<void()> &&callable) = 0;
	virtual void registerLeaveSubscription(not_null<QWidget*> widget) = 0;
	virtual void unregisterLeaveSubscription(not_null<QWidget*> widget) = 0;

	virtual void writeLogEntry(const QString &entry) = 0;
	[[nodiscard]] virtual QString emojiCacheFolder() = 0;

	virtual void textActionsUpdated();
	virtual void activationFromTopPanel();

	[[nodiscard]] virtual std::shared_ptr<ClickHandler> createLinkHandler(
		EntityType type,
		const QString &text,
		const QString &data,
		const TextParseOptions &options);
	[[nodiscard]] virtual bool handleUrlClick(
		const QString &url,
		const QVariant &context);
	[[nodiscard]] virtual QString convertTagToMimeTag(const QString &tagId);
	[[nodiscard]] virtual const Emoji::One *defaultEmojiVariant(
		const Emoji::One *emoji);

	[[nodiscard]] virtual rpl::producer<> forcePopupMenuHideRequests();

	[[nodiscard]] virtual QString phraseContextCopyText();
	[[nodiscard]] virtual QString phraseContextCopyEmail();
	[[nodiscard]] virtual QString phraseContextCopyLink();
	[[nodiscard]] virtual QString phraseContextCopySelected();
	[[nodiscard]] virtual QString phraseFormattingTitle();
	[[nodiscard]] virtual QString phraseFormattingLinkCreate();
	[[nodiscard]] virtual QString phraseFormattingLinkEdit();
	[[nodiscard]] virtual QString phraseFormattingClear();
	[[nodiscard]] virtual QString phraseFormattingBold();
	[[nodiscard]] virtual QString phraseFormattingItalic();
	[[nodiscard]] virtual QString phraseFormattingUnderline();
	[[nodiscard]] virtual QString phraseFormattingStrikeOut();
	[[nodiscard]] virtual QString phraseFormattingMonospace();

};

} // namespace Ui
