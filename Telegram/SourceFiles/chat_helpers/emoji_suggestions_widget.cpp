/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_suggestions_widget.h"

#include "chat_helpers/emoji_suggestions_helper.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/input_fields.h"
#include "ui/emoji_config.h"
#include "platform/platform_specific.h"
#include "core/event_filter.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace Emoji {
namespace {

constexpr auto kRowLimit = 5;

} // namespace

class SuggestionsWidget::Row {
public:
	Row(not_null<EmojiPtr> emoji, const QString &label, const QString &replacement);
	Row(const Row &other) = delete;
	Row &operator=(const Row &other) = delete;
	Row(Row &&other) = default;
	Row &operator=(Row &&other) = default;
	~Row();

	not_null<EmojiPtr> emoji() const {
		return _emoji;
	}
	const QString &label() const {
		return _label;
	}
	const QString &replacement() const {
		return _replacement;
	}
	RippleAnimation *ripple() const {
		return _ripple.get();
	}
	void setRipple(std::unique_ptr<RippleAnimation> ripple) {
		_ripple = std::move(ripple);
	}
	void resetRipple() {
		_ripple.reset();
	}

private:
	not_null<EmojiPtr> _emoji;
	QString _label;
	QString _replacement;
	std::unique_ptr<RippleAnimation> _ripple;

};

SuggestionsWidget::Row::Row(not_null<EmojiPtr> emoji, const QString &label, const QString &replacement)
: _emoji(emoji)
, _label(label)
, _replacement(replacement) {
}

SuggestionsWidget::Row::~Row() = default;

SuggestionsWidget::SuggestionsWidget(QWidget *parent, const style::Menu &st) : TWidget(parent)
, _st(&st)
, _rowHeight(_st->itemPadding.top() + _st->itemFont->height + _st->itemPadding.bottom()) {
	setMouseTracking(true);
}

rpl::producer<bool> SuggestionsWidget::toggleAnimated() const {
	return _toggleAnimated.events();
}

rpl::producer<QString> SuggestionsWidget::triggered() const {
	return _triggered.events();
}

void SuggestionsWidget::showWithQuery(const QString &query) {
	if (_query == query) {
		return;
	}
	_query = query;
	auto rows = getRowsByQuery();
	if (rows.empty()) {
		_toggleAnimated.fire(false);
	}
	clearSelection();
	_rows = std::move(rows);
	resizeToRows();
	update();
	if (!_rows.empty()) {
		setSelected(0);
	}
	if (!_rows.empty()) {
		_toggleAnimated.fire(true);
	}
}

std::vector<SuggestionsWidget::Row> SuggestionsWidget::getRowsByQuery() const {
	auto result = std::vector<Row>();
	if (_query.isEmpty()) {
		return result;
	}
	auto suggestions = GetSuggestions(QStringToUTF16(_query));
	if (suggestions.empty()) {
		return result;
	}
	auto count = suggestions.size();
	auto suggestionsEmoji = std::vector<EmojiPtr>(count, nullptr);
	for (auto i = 0; i != count; ++i) {
		suggestionsEmoji[i] = Find(QStringFromUTF16(suggestions[i].emoji()));
	}
	auto recents = 0;
	auto &recent = GetRecent();
	for (auto &item : recent) {
		auto emoji = item.first->original();
		if (!emoji) emoji = item.first;
		auto it = std::find(suggestionsEmoji.begin(), suggestionsEmoji.end(), emoji);
		if (it != suggestionsEmoji.end()) {
			auto index = (it - suggestionsEmoji.begin());
			if (index >= recents) {
				if (index > recents) {
					auto recentEmoji = suggestionsEmoji[index];
					auto recentSuggestion = suggestions[index];
					for (auto i = index; i != recents; --i) {
						suggestionsEmoji[i] = suggestionsEmoji[i - 1];
						suggestions[i] = suggestions[i - 1];
					}
					suggestionsEmoji[recents] = recentEmoji;
					suggestions[recents] = recentSuggestion;
				}
				++recents;
			}
		}
	}

	result.reserve(kRowLimit);
	auto index = 0;
	for (auto &item : suggestions) {
		if (auto emoji = suggestionsEmoji[index++]) {
			if (emoji->hasVariants()) {
				auto it = cEmojiVariants().constFind(emoji->nonColoredId());
				if (it != cEmojiVariants().cend()) {
					emoji = emoji->variant(it.value());
				}
			}
			result.emplace_back(emoji, QStringFromUTF16(item.label()), QStringFromUTF16(item.replacement()));
			if (result.size() == kRowLimit) {
				break;
			}
		}
	}
	return result;
}

void SuggestionsWidget::resizeToRows() {
	auto newWidth = 0;
	for (auto &row : _rows) {
		accumulate_max(newWidth, countWidth(row));
	}
	newWidth = snap(newWidth, _st->widthMin, _st->widthMax);
	auto newHeight = _st->skip + (_rows.size() * _rowHeight) + _st->skip;
	resize(newWidth, newHeight);
}

int SuggestionsWidget::countWidth(const Row &row) {
	auto textw = _st->itemFont->width(row.label());
	return _st->itemPadding.left() + textw + _st->itemPadding.right();
}

void SuggestionsWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto clip = e->rect();

	auto topskip = QRect(0, 0, width(), _st->skip);
	auto bottomskip = QRect(0, height() - _st->skip, width(), _st->skip);
	if (clip.intersects(topskip)) p.fillRect(clip.intersected(topskip), _st->itemBg);
	if (clip.intersects(bottomskip)) p.fillRect(clip.intersected(bottomskip), _st->itemBg);

	const auto top = _st->skip;
	p.setFont(_st->itemFont);
	const auto from = floorclamp(clip.top() - top, _rowHeight, 0, _rows.size());
	const auto to = ceilclamp(clip.top() + clip.height() - top, _rowHeight, 0, _rows.size());
	p.translate(0, top + from * _rowHeight);
	for (auto i = from; i != to; ++i) {
		auto &row = _rows[i];
		const auto selected = (i == _selected || i == _pressed);
		p.fillRect(0, 0, width(), _rowHeight, selected ? _st->itemBgOver : _st->itemBg);
		if (const auto ripple = row.ripple()) {
			ripple->paint(p, 0, 0, width(), ms);
			if (ripple->empty()) {
				row.resetRipple();
			}
		}
		const auto emoji = row.emoji();
		const auto esize = Ui::Emoji::GetSizeLarge();
		Ui::Emoji::Draw(
			p,
			emoji,
			esize,
			(_st->itemPadding.left() - (esize / cIntRetinaFactor())) / 2,
			(_rowHeight - (esize / cIntRetinaFactor())) / 2);
		p.setPen(selected ? _st->itemFgOver : _st->itemFg);
		p.drawTextLeft(_st->itemPadding.left(), _st->itemPadding.top(), width(), row.label());
		p.translate(0, _rowHeight);
	}
}

void SuggestionsWidget::keyPressEvent(QKeyEvent *e) {
	handleKeyEvent(e->key());
}

void SuggestionsWidget::handleKeyEvent(int key) {
	if (key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Tab) {
		triggerSelectedRow();
		return;
	}
	if ((key != Qt::Key_Up && key != Qt::Key_Down) || _rows.size() < 1) {
		return;
	}

	auto delta = (key == Qt::Key_Down ? 1 : -1), start = _selected;
	if (start < 0 || start >= _rows.size()) {
		start = (delta > 0) ? (_rows.size() - 1) : 0;
	}
	auto newSelected = start + delta;
	if (newSelected < 0) {
		newSelected += _rows.size();
	} else if (newSelected >= _rows.size()) {
		newSelected -= _rows.size();
	}

	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	setSelected(newSelected);
}

void SuggestionsWidget::setSelected(int selected) {
	if (selected >= _rows.size()) {
		selected = -1;
	}
	if (_selected != selected) {
		updateSelectedItem();
		_selected = selected;
		updateSelectedItem();
	}
}

void SuggestionsWidget::setPressed(int pressed) {
	if (pressed >= _rows.size()) {
		pressed = -1;
	}
	if (_pressed != pressed) {
		_pressed = pressed;
	}
}

void SuggestionsWidget::clearMouseSelection() {
	if (_mouseSelection) {
		clearSelection();
	}
}

void SuggestionsWidget::clearSelection() {
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	setSelected(-1);
}

int SuggestionsWidget::itemTop(int index) {
	if (index > _rows.size()) {
		index = _rows.size();
	}
	return _st->skip + (_rowHeight * index);
}

void SuggestionsWidget::updateItem(int index) {
	if (index >= 0 && index < _rows.size()) {
		update(0, itemTop(index), width(), _rowHeight);
	}
}

void SuggestionsWidget::updateSelectedItem() {
	updateItem(_selected);
}

void SuggestionsWidget::mouseMoveEvent(QMouseEvent *e) {
	auto inner = rect().marginsRemoved(QMargins(0, _st->skip, 0, _st->skip));
	auto localPosition = e->pos();
	if (inner.contains(localPosition)) {
		const auto globalPosition = e->globalPos();
		if (!_lastMousePosition) {
			_lastMousePosition = globalPosition;
			return;
		} else if (!_mouseSelection
			&& *_lastMousePosition == globalPosition) {
			return;
		}
		selectByMouse(globalPosition);
	} else {
		clearMouseSelection();
	}
}

void SuggestionsWidget::selectByMouse(QPoint globalPosition) {
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	auto p = mapFromGlobal(globalPosition) - QPoint(0, _st->skip);
	auto selected = (p.y() >= 0) ? (p.y() / _rowHeight) : -1;
	setSelected((selected >= 0 && selected < _rows.size()) ? selected : -1);
}

void SuggestionsWidget::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());
	if (_selected >= 0 && _selected < _rows.size()) {
		setPressed(_selected);
		if (!_rows[_pressed].ripple()) {
			auto mask = RippleAnimation::rectMask(QSize(width(), _rowHeight));
			_rows[_pressed].setRipple(std::make_unique<RippleAnimation>(_st->ripple, std::move(mask), [this, selected = _pressed] {
				updateItem(selected);
			}));
		}
		_rows[_pressed].ripple()->add(mapFromGlobal(QCursor::pos()) - QPoint(0, itemTop(_pressed)));
	}
}

void SuggestionsWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_pressed >= 0 && _pressed < _rows.size()) {
		auto pressed = _pressed;
		setPressed(-1);
		if (_rows[pressed].ripple()) {
			_rows[pressed].ripple()->lastStop();
		}
		if (pressed == _selected) {
			triggerRow(_rows[_selected]);
		}
	}
}

void SuggestionsWidget::triggerSelectedRow() {
	if (_selected >= 0 && _selected < _rows.size()) {
		triggerRow(_rows[_selected]);
	}
}

void SuggestionsWidget::triggerRow(const Row &row) {
	_triggered.fire(row.emoji()->text());
}

void SuggestionsWidget::enterEventHook(QEvent *e) {
	auto mouse = QCursor::pos();
	if (!rect().marginsRemoved(QMargins(0, _st->skip, 0, _st->skip)).contains(mapFromGlobal(mouse))) {
		clearMouseSelection();
	}
	return TWidget::enterEventHook(e);
}

void SuggestionsWidget::leaveEventHook(QEvent *e) {
	clearMouseSelection();
	return TWidget::leaveEventHook(e);
}

SuggestionsController::SuggestionsController(
	not_null<QWidget*> outer,
	not_null<QTextEdit*> field)
: _field(field) {
	_container = base::make_unique_q<InnerDropdown>(
		outer,
		st::emojiSuggestionsDropdown);
	_container->setAutoHiding(false);
	_suggestions = _container->setOwnedWidget(
		object_ptr<Ui::Emoji::SuggestionsWidget>(
			_container,
			st::emojiSuggestionsMenu));

	setReplaceCallback(nullptr);

	_fieldFilter.reset(Core::InstallEventFilter(
		_field,
		[=](not_null<QEvent*> event) { return fieldFilter(event); }));
	_outerFilter.reset(Core::InstallEventFilter(
		outer,
		[=](not_null<QEvent*> event) { return outerFilter(event); }));
	QObject::connect(
		_field,
		&QTextEdit::textChanged,
		_container,
		[=] { handleTextChange(); });
	QObject::connect(
		_field,
		&QTextEdit::cursorPositionChanged,
		_container,
		[=] { handleCursorPositionChange(); });

	_suggestions->toggleAnimated(
	) | rpl::start_with_next([=](bool visible) {
		suggestionsUpdated(visible);
	}, _lifetime);
	_suggestions->triggered(
	) | rpl::start_with_next([=](QString replacement) {
		replaceCurrent(replacement);
	}, _lifetime);

	updateForceHidden();

	handleTextChange();
}

SuggestionsController *SuggestionsController::Init(
		not_null<QWidget*> outer,
		not_null<Ui::InputField*> field) {
	const auto result = Ui::CreateChild<SuggestionsController>(
		field.get(),
		outer,
		field->rawTextEdit());
	result->setReplaceCallback([=](
			int from,
			int till,
			const QString &replacement) {
		field->commitInstantReplacement(from, till, replacement);
	});
	return result;
}

void SuggestionsController::setReplaceCallback(
	Fn<void(
		int from,
		int till,
		const QString &replacement)> callback) {
	if (callback) {
		_replaceCallback = std::move(callback);
	} else {
		_replaceCallback = [=](int from, int till, const QString &replacement) {
			auto cursor = _field->textCursor();
			cursor.setPosition(from);
			cursor.setPosition(till, QTextCursor::KeepAnchor);
			cursor.insertText(replacement);
		};
	}
}

void SuggestionsController::handleTextChange() {
	_ignoreCursorPositionChange = true;
	InvokeQueued(_container, [=] { _ignoreCursorPositionChange = false; });

	const auto query = getEmojiQuery();
	if (query.isEmpty() || _textChangeAfterKeyPress) {
		_suggestions->showWithQuery(query);
	}
}

QString SuggestionsController::getEmojiQuery() {
	if (!Global::SuggestEmoji()) {
		return QString();
	}

	auto cursor = _field->textCursor();
	if (cursor.hasSelection()) {
		return QString();
	}

	auto position = cursor.position();
	auto findTextPart = [this, &position] {
		auto document = _field->document();
		auto block = document->findBlock(position);
		for (auto i = block.begin(); !i.atEnd(); ++i) {
			auto fragment = i.fragment();
			if (!fragment.isValid()) continue;

			auto from = fragment.position();
			auto till = from + fragment.length();
			if (from >= position || till < position) {
				continue;
			}
			if (fragment.charFormat().isImageFormat()) {
				continue;
			}
			position -= from;
			_queryStartPosition = from;
			return fragment.text();
		}
		return QString();
	};

	auto text = findTextPart();
	if (text.isEmpty()) {
		return QString();
	}

	const auto isUpperCaseLetter = [](QChar ch) {
		return (ch >= 'A' && ch <= 'Z');
	};
	const auto isLetter = [](QChar ch) {
		return (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9');
	};
	const auto isSuggestionChar = [](QChar ch) {
		return (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9')
			|| (ch == '_')
			|| (ch == '-')
			|| (ch == '+');
	};
	const auto isGoodCharBeforeSuggestion = [&](QChar ch) {
		return !isSuggestionChar(ch) || (ch == 0);
	};
	Assert(position > 0 && position <= text.size());
	for (auto i = position; i != 0;) {
		auto ch = text[--i];
		if (ch == ':') {
			auto beforeColon = (i < 1) ? QChar(0) : text[i - 1];
			if (isGoodCharBeforeSuggestion(beforeColon)) {
				// At least one letter after colon.
				if (position > i + 1) {
					// Skip colon and the first letter.
					_queryStartPosition += i + 2;
					const auto length = position - i;
					auto result = text.mid(i, length);
					const auto upperCaseLetters = std::count_if(
						result.begin(),
						result.end(),
						isUpperCaseLetter);
					const auto letters = std::count_if(
						result.begin(),
						result.end(),
						isLetter);
					if (letters == upperCaseLetters && letters == 1) {
						// No upper case single letter suggestions.
						// We don't want to suggest emoji on :D and :-P
						return QString();
					}
					return result.toLower();
				}
			}
			return QString();
		}
		if (position - i > kSuggestionMaxLength) {
			return QString();
		}
		if (!isSuggestionChar(ch)) {
			return QString();
		}
	}
	return QString();
}

void SuggestionsController::replaceCurrent(const QString &replacement) {
	auto suggestion = getEmojiQuery();
	if (suggestion.isEmpty()) {
		_suggestions->showWithQuery(QString());
	} else {
		const auto cursor = _field->textCursor();
		const auto position = cursor.position();
		const auto from = position - suggestion.size();
		_replaceCallback(from, position, replacement);
	}
}

void SuggestionsController::handleCursorPositionChange() {
	InvokeQueued(_container, [=] {
		if (_ignoreCursorPositionChange) {
			return;
		}
		_suggestions->showWithQuery(QString());
	});
}

void SuggestionsController::suggestionsUpdated(bool visible) {
	_shown = visible;
	if (_shown) {
		_container->resizeToContent();
		updateGeometry();
		if (!_forceHidden) {
			if (_container->isHidden() || _container->isHiding()) {
				raise();
			}
			_container->showAnimated(
				Ui::PanelAnimation::Origin::BottomLeft);
		}
	} else if (!_forceHidden) {
		_container->hideAnimated();
	}
}

void SuggestionsController::updateGeometry() {
	auto cursor = _field->textCursor();
	cursor.setPosition(_queryStartPosition);
	auto aroundRect = _field->cursorRect(cursor);
	aroundRect.setTopLeft(_field->viewport()->mapToGlobal(aroundRect.topLeft()));
	aroundRect.setTopLeft(_container->parentWidget()->mapFromGlobal(aroundRect.topLeft()));
	auto boundingRect = _container->parentWidget()->rect();
	auto origin = rtl() ? PanelAnimation::Origin::BottomRight : PanelAnimation::Origin::BottomLeft;
	auto point = rtl() ? (aroundRect.topLeft() + QPoint(aroundRect.width(), 0)) : aroundRect.topLeft();
	point -= rtl() ? QPoint(_container->width() - st::emojiSuggestionsDropdown.padding.right(), _container->height()) : QPoint(st::emojiSuggestionsDropdown.padding.left(), _container->height());
	if (rtl()) {
		if (point.x() < boundingRect.x()) {
			point.setX(boundingRect.x());
		}
		if (point.x() + _container->width() > boundingRect.x() + boundingRect.width()) {
			point.setX(boundingRect.x() + boundingRect.width() - _container->width());
		}
	} else {
		if (point.x() + _container->width() > boundingRect.x() + boundingRect.width()) {
			point.setX(boundingRect.x() + boundingRect.width() - _container->width());
		}
		if (point.x() < boundingRect.x()) {
			point.setX(boundingRect.x());
		}
	}
	if (point.y() < boundingRect.y()) {
		point.setY(aroundRect.y() + aroundRect.height());
		origin = (origin == PanelAnimation::Origin::BottomRight) ? PanelAnimation::Origin::TopRight : PanelAnimation::Origin::TopLeft;
	}
	_container->move(point);
}

void SuggestionsController::updateForceHidden() {
	_forceHidden = !_field->isVisible() || !_field->hasFocus();
	if (_forceHidden) {
		_container->hideFast();
	} else if (_shown) {
		_container->showFast();
	}
}

bool SuggestionsController::fieldFilter(not_null<QEvent*> event) {
	auto type = event->type();
	switch (type) {
	case QEvent::Move:
	case QEvent::Resize: {
		if (_shown) {
			updateGeometry();
		}
	} break;

	case QEvent::Show:
	case QEvent::ShowToParent:
	case QEvent::Hide:
	case QEvent::HideToParent:
	case QEvent::FocusIn:
	case QEvent::FocusOut: {
		updateForceHidden();
	} break;

	case QEvent::KeyPress: {
		const auto key = static_cast<QKeyEvent*>(event.get())->key();
		switch (key) {
		case Qt::Key_Enter:
		case Qt::Key_Return:
		case Qt::Key_Tab:
		case Qt::Key_Up:
		case Qt::Key_Down:
			if (_shown && !_forceHidden) {
				_suggestions->handleKeyEvent(key);
				return true;
			}
			break;

		case Qt::Key_Escape:
			if (_shown && !_forceHidden) {
				_suggestions->showWithQuery(QString());
				return true;
			}
			break;
		}
		_textChangeAfterKeyPress = true;
		InvokeQueued(_container, [=] { _textChangeAfterKeyPress = false; });
	} break;
	}
	return false;
}

bool SuggestionsController::outerFilter(not_null<QEvent*> event) {
	auto type = event->type();
	switch (type) {
	case QEvent::Move:
	case QEvent::Resize: {
		// updateGeometry uses not only container geometry, but also
		// container children geometries that will be updated later.
		InvokeQueued(_container, [=] {
			if (_shown) {
				updateGeometry();
			}
		});
	} break;
	}
	return false;
}

void SuggestionsController::raise() {
	_container->raise();
}

} // namespace Emoji
} // namespace Ui
