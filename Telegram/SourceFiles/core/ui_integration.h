/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/integration.h"

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class ElementDelegate;
} // namespace HistoryView

namespace Core {

struct TextContextDetails {
	enum class HashtagMentionType : uchar {
		Telegram,
		Twitter,
		Instagram,
	};

	Main::Session *session = nullptr;
	HashtagMentionType type = HashtagMentionType::Telegram;
};

struct TextContextArgs {
	not_null<Main::Session*> session;
	TextContextDetails details;
	Fn<void()> repaint;
	int customEmojiLoopLimit = 0;
};
[[nodiscard]] Ui::Text::MarkedContext TextContext(TextContextArgs &&args);

class UiIntegration final : public Ui::Integration {
public:
	void postponeCall(FnMut<void()> &&callable) override;
	void registerLeaveSubscription(not_null<QWidget*> widget) override;
	void unregisterLeaveSubscription(not_null<QWidget*> widget) override;

	QString emojiCacheFolder() override;
	QString openglCheckFilePath() override;
	QString angleBackendFilePath() override;

	void textActionsUpdated() override;
	void activationFromTopPanel() override;

	bool screenIsLocked() override;

	std::shared_ptr<ClickHandler> createLinkHandler(
		const EntityLinkData &data,
		const Ui::Text::MarkedContext &context) override;
	bool handleUrlClick(
		const QString &url,
		const QVariant &context) override;
	bool copyPreOnClick(const QVariant &context) override;
	rpl::producer<> forcePopupMenuHideRequests() override;
	const Ui::Emoji::One *defaultEmojiVariant(
		const Ui::Emoji::One *emoji) override;

	QString phraseContextCopyText() override;
	QString phraseContextCopyEmail() override;
	QString phraseContextCopyLink() override;
	QString phraseContextCopySelected() override;
	QString phraseFormattingTitle() override;
	QString phraseFormattingLinkCreate() override;
	QString phraseFormattingLinkEdit() override;
	QString phraseFormattingClear() override;
	QString phraseFormattingBold() override;
	QString phraseFormattingItalic() override;
	QString phraseFormattingUnderline() override;
	QString phraseFormattingStrikeOut() override;
	QString phraseFormattingBlockquote() override;
	QString phraseFormattingMonospace() override;
	QString phraseFormattingSpoiler() override;
	QString phraseButtonOk() override;
	QString phraseButtonClose() override;
	QString phraseButtonCancel() override;
	QString phrasePanelCloseWarning() override;
	QString phrasePanelCloseUnsaved() override;
	QString phrasePanelCloseAnyway() override;
	QString phraseBotSharePhone() override;
	QString phraseBotSharePhoneTitle() override;
	QString phraseBotSharePhoneConfirm() override;
	QString phraseBotAllowWrite() override;
	QString phraseBotAllowWriteTitle() override;
	QString phraseBotAllowWriteConfirm() override;
	QString phraseQuoteHeaderCopy() override;

};

[[nodiscard]] bool OpenGLLastCheckFailed();

} // namespace Core
