/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "base/unique_qptr.h"
#include "base/timer.h"

#include <QtWidgets/QTextEdit>

namespace style {
struct EmojiSuggestions;
} // namespace style

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class InnerDropdown;
class InputField;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui::Emoji {

class SuggestionsWidget;

using SuggestionsQuery = std::variant<QString, EmojiPtr>;

class SuggestionsController final : public QObject {
public:
	struct Options {
		bool suggestExactFirstWord = true;
		bool suggestCustomEmoji = false;
		Fn<bool(not_null<DocumentData*>)> allowCustomWithoutPremium;
		const style::EmojiSuggestions *st = nullptr;
	};

	SuggestionsController(
		not_null<QWidget*> parent,
		not_null<QWidget*> outer,
		not_null<QTextEdit*> field,
		not_null<Main::Session*> session,
		const Options &options);

	void raise();
	void setReplaceCallback(Fn<void(
		int from,
		int till,
		const QString &replacement,
		const QString &customEmojiData)> callback);

	static not_null<SuggestionsController*> Init(
			not_null<QWidget*> outer,
			not_null<Ui::InputField*> field,
			not_null<Main::Session*> session) {
		return Init(outer, field, session, {});
	}
	static not_null<SuggestionsController*> Init(
		not_null<QWidget*> outer,
		not_null<Ui::InputField*> field,
		not_null<Main::Session*> session,
		const Options &options);

private:
	void handleCursorPositionChange();
	void handleTextChange();
	void showWithQuery(SuggestionsQuery query);
	[[nodiscard]] SuggestionsQuery getEmojiQuery();
	void suggestionsUpdated(bool visible);
	void updateGeometry();
	void updateForceHidden();
	void replaceCurrent(
		const QString &replacement,
		const QString &customEmojiData);
	bool fieldFilter(not_null<QEvent*> event);
	bool outerFilter(not_null<QEvent*> event);

	const style::EmojiSuggestions &_st;
	bool _shown = false;
	bool _forceHidden = false;
	int _queryStartPosition = 0;
	int _emojiQueryLength = 0;
	bool _ignoreCursorPositionChange = false;
	bool _textChangeAfterKeyPress = false;
	QPointer<QTextEdit> _field;
	const not_null<Main::Session*> _session;
	Fn<void(
		int from,
		int till,
		const QString &replacement,
		const QString &customEmojiData)> _replaceCallback;
	base::unique_qptr<InnerDropdown> _container;
	QPointer<SuggestionsWidget> _suggestions;
	base::unique_qptr<QObject> _fieldFilter;
	base::unique_qptr<QObject> _outerFilter;
	base::Timer _showExactTimer;
	bool _keywordsRefreshed = false;
	SuggestionsQuery _lastShownQuery;
	Options _options;

	rpl::lifetime _lifetime;

};

} // namespace Ui::Emoji
