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

using SuggestionsQuery = std::variant<QString, EmojiPtr>;

class SuggestionsWidget final : public Ui::RpWidget {
public:
	SuggestionsWidget(
		QWidget *parent,
		not_null<Main::Session*> session,
		bool suggestCustomEmoji,
		Fn<bool(not_null<DocumentData*>)> allowCustomWithoutPremium);
	~SuggestionsWidget();

	void showWithQuery(SuggestionsQuery query, bool force = false);
	void selectFirstResult();
	bool handleKeyEvent(int key);

	[[nodiscard]] rpl::producer<bool> toggleAnimated() const;

	struct Chosen {
		QString emoji;
		QString customData;
	};
	[[nodiscard]] rpl::producer<Chosen> triggered() const;

private:
	struct Row {
		Row(not_null<EmojiPtr> emoji, const QString &replacement);

		Ui::Text::CustomEmoji *custom = nullptr;
		DocumentData *document = nullptr;
		not_null<EmojiPtr> emoji;
		QString replacement;
	};
	struct Custom {
		not_null<DocumentData*> document;
		not_null<EmojiPtr> emoji;
		QString replacement;
	};

	bool eventHook(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void scrollByWheelEvent(not_null<QWheelEvent*> e);
	void paintFadings(Painter &p) const;

	[[nodiscard]] std::vector<Row> getRowsByQuery(const QString &text) const;
	[[nodiscard]] base::flat_multi_map<int, Custom> lookupCustom(
		const std::vector<Row> &rows) const;
	[[nodiscard]] std::vector<Row> appendCustom(
		std::vector<Row> rows);
	[[nodiscard]] std::vector<Row> appendCustom(
		std::vector<Row> rows,
		const base::flat_multi_map<int, Custom> &custom);
	void resizeToRows();
	void setSelected(
		int selected,
		anim::type animated = anim::type::instant);
	void setPressed(int pressed);
	void clearMouseSelection();
	void clearSelection();
	void updateSelectedItem();
	void updateItem(int index);
	[[nodiscard]] QRect inner() const;
	[[nodiscard]] QPoint innerShift() const;
	[[nodiscard]] QPoint mapToInner(QPoint globalPosition) const;
	void selectByMouse(QPoint globalPosition);
	bool triggerSelectedRow() const;
	void triggerRow(const Row &row) const;

	[[nodiscard]] int scrollCurrent() const;
	void scrollTo(int value, anim::type animated = anim::type::instant);
	void stopAnimations();

	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomEmoji(
		not_null<DocumentData*> document);
	void customEmojiRepaint();

	const not_null<Main::Session*> _session;
	SuggestionsQuery _query;
	std::vector<Row> _rows;
	bool _suggestCustomEmoji = false;
	Fn<bool(not_null<DocumentData*>)> _allowCustomWithoutPremium;

	base::flat_map<
		not_null<DocumentData*>,
		std::unique_ptr<Ui::Text::CustomEmoji>> _customEmoji;
	bool _repaintScheduled = false;

	std::optional<QPoint> _lastMousePosition;
	bool _mouseSelection = false;
	int _selected = -1;
	int _pressed = -1;

	int _scrollValue = 0;
	Ui::Animations::Simple _scrollAnimation;
	Ui::Animations::Simple _selectedAnimation;
	int _scrollMax = 0;
	int _oneWidth = 0;
	QMargins _padding;

	QPoint _mousePressPosition;
	int _dragScrollStart = -1;

	rpl::event_stream<bool> _toggleAnimated;
	rpl::event_stream<Chosen> _triggered;

};

class SuggestionsController {
public:
	struct Options {
		bool suggestExactFirstWord = true;
		bool suggestCustomEmoji = false;
		Fn<bool(not_null<DocumentData*>)> allowCustomWithoutPremium;
	};

	SuggestionsController(
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

	bool _shown = false;
	bool _forceHidden = false;
	int _queryStartPosition = 0;
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
