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
#include "platform/platform_specific.h"
#include "styles/style_chat_helpers.h"
#include "ui/widgets/inner_dropdown.h"

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

void SuggestionsWidget::showWithQuery(const QString &query) {
	if (_query == query) {
		return;
	}
	_query = query;
	auto rows = getRowsByQuery();
	if (rows.empty()) {
		toggleAnimated.notify(false, true);
	}
	clearSelection();
	_rows = std::move(rows);
	resizeToRows();
	update();
	if (!_rows.empty()) {
		setSelected(0);
	}
	if (!_rows.empty()) {
		toggleAnimated.notify(true, true);
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

	auto top = _st->skip;
	p.setFont(_st->itemFont);
	auto from = floorclamp(clip.top() - top, _rowHeight, 0, _rows.size());
	auto to = ceilclamp(clip.top() + clip.height() - top, _rowHeight, 0, _rows.size());
	p.translate(0, top + from * _rowHeight);
	for (auto i = from; i != to; ++i) {
		auto &row = _rows[i];
		auto selected = (i == _selected || i == _pressed);
		p.fillRect(0, 0, width(), _rowHeight, selected ? _st->itemBgOver : _st->itemBg);
		if (auto ripple = row.ripple()) {
			ripple->paint(p, 0, 0, width(), ms);
			if (ripple->empty()) {
				row.resetRipple();
			}
		}
		auto emoji = row.emoji();
		auto esize = Ui::Emoji::Size(Ui::Emoji::Index() + 1);
		p.drawPixmapLeft((_st->itemPadding.left() - (esize / cIntRetinaFactor())) / 2, (_rowHeight - (esize / cIntRetinaFactor())) / 2, width(), App::emojiLarge(), QRect(emoji->x() * esize, emoji->y() * esize, esize, esize));
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
		_mouseSelection = true;
		updateSelection(e->globalPos());
	} else {
		clearMouseSelection();
	}
}

void SuggestionsWidget::updateSelection(QPoint globalPosition) {
	if (!_mouseSelection) return;

	auto p = mapFromGlobal(globalPosition) - QPoint(0, _st->skip);
	auto selected = (p.y() >= 0) ? (p.y() / _rowHeight) : -1;
	setSelected((selected >= 0 && selected < _rows.size()) ? selected : -1);
}
void SuggestionsWidget::mousePressEvent(QMouseEvent *e) {
	if (!_mouseSelection) {
		return;
	}
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
	triggered.notify(row.emoji()->text(), true);
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

SuggestionsController::SuggestionsController(QWidget *parent, not_null<QTextEdit*> field) : QObject(nullptr)
, _field(field)
, _container(parent, st::emojiSuggestionsDropdown)
, _suggestions(_container->setOwnedWidget(object_ptr<Ui::Emoji::SuggestionsWidget>(parent, st::emojiSuggestionsMenu))) {
	_container->setAutoHiding(false);

	_field->installEventFilter(this);
	connect(_field, &QTextEdit::textChanged, this, [this] { handleTextChange(); });
	connect(_field, &QTextEdit::cursorPositionChanged, this, [this] { handleCursorPositionChange(); });

	subscribe(_suggestions->toggleAnimated, [this](bool visible) { suggestionsUpdated(visible); });
	subscribe(_suggestions->triggered, [this](QString replacement) { replaceCurrent(replacement); });
	updateForceHidden();

	handleTextChange();
}

void SuggestionsController::handleTextChange() {
	_ignoreCursorPositionChange = true;
	InvokeQueued(this, [this] { _ignoreCursorPositionChange = false; });

	auto query = getEmojiQuery();
	if (query.isEmpty() || _textChangeAfterKeyPress) {
		_suggestions->showWithQuery(query);
	}
}

QString SuggestionsController::getEmojiQuery() {
	if (!cReplaceEmojis()) {
		return QString();
	}

	auto cursor = _field->textCursor();
	auto position = _field->textCursor().position();
	if (cursor.anchor() != position) {
		return QString();
	}

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

	auto isSuggestionChar = [](QChar ch) {
		return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || (ch == '_') || (ch == '-') || (ch == '+');
	};
	auto isGoodCharBeforeSuggestion = [isSuggestionChar](QChar ch) {
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
					return text.mid(i, position - i);
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
	auto cursor = _field->textCursor();
	auto suggestion = getEmojiQuery();
	if (suggestion.isEmpty()) {
		_suggestions->showWithQuery(QString());
	} else {
		cursor.setPosition(cursor.position() - suggestion.size(), QTextCursor::KeepAnchor);
		cursor.insertText(replacement);
	}

	auto emojiText = GetSuggestionEmoji(QStringToUTF16(replacement));
	if (auto emoji = Find(QStringFromUTF16(emojiText))) {
		if (emoji->hasVariants()) {
			auto it = cEmojiVariants().constFind(emoji->nonColoredId());
			if (it != cEmojiVariants().cend()) {
				emoji = emoji->variant(it.value());
			}
		}
		AddRecent(emoji);
	}
}

void SuggestionsController::handleCursorPositionChange() {
	InvokeQueued(this, [this] {
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
			_container->showAnimated(Ui::PanelAnimation::Origin::BottomLeft);
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
	_forceHidden = !_field->isVisible();
	if (_forceHidden) {
		_container->hideFast();
	} else if (_shown) {
		_container->showFast();
	}
}

bool SuggestionsController::eventFilter(QObject *object, QEvent *event) {
	if (object == _field) {
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
		case QEvent::HideToParent: {
			updateForceHidden();
		} break;

		case QEvent::KeyPress: {
			auto key = static_cast<QKeyEvent*>(event)->key();
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
			InvokeQueued(this, [this] { _textChangeAfterKeyPress = false; });
		} break;
		}
	}
	return QObject::eventFilter(object, event);
}

void SuggestionsController::raise() {
	_container->raise();
}

} // namespace Emoji
} // namespace Ui
