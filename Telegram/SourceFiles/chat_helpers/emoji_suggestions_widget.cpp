/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_suggestions_widget.h"

#include "chat_helpers/emoji_keywords.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "emoji_suggestions_helper.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/input_fields.h"
#include "ui/emoji_config.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "platform/platform_specific.h"
#include "core/application.h"
#include "base/event_filter.h"
#include "main/main_session.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>
#include <QtGui/QTextBlock>

namespace Ui {
namespace Emoji {
namespace {

constexpr auto kShowExactDelay = crl::time(300);
constexpr auto kMaxNonScrolledEmoji = 7;
constexpr auto kAnimationDuration = crl::time(120);

} // namespace

SuggestionsWidget::SuggestionsWidget(QWidget *parent)
: RpWidget(parent)
, _oneWidth(st::emojiSuggestionSize)
, _padding(st::emojiSuggestionsPadding) {
	resize(
		_oneWidth + _padding.left() + _padding.right(),
		_oneWidth + _padding.top() + _padding.bottom());
	setMouseTracking(true);
}

rpl::producer<bool> SuggestionsWidget::toggleAnimated() const {
	return _toggleAnimated.events();
}

rpl::producer<QString> SuggestionsWidget::triggered() const {
	return _triggered.events();
}

void SuggestionsWidget::showWithQuery(const QString &query, bool force) {
	if (!force && (_query == query)) {
		return;
	}
	_query = query;
	auto rows = getRowsByQuery();
	if (rows.empty()) {
		_toggleAnimated.fire(false);
	}
	clearSelection();
	setPressed(-1);
	_rows = std::move(rows);
	resizeToRows();
	update();

	Ui::PostponeCall(this, [=] {
		if (!_rows.empty()) {
			_toggleAnimated.fire(true);
		}
	});
}

void SuggestionsWidget::selectFirstResult() {
	if (!_rows.empty() && _selected < 0) {
		setSelected(0);
	}
}

SuggestionsWidget::Row::Row(
	not_null<EmojiPtr> emoji,
	const QString &replacement)
: emoji(emoji)
, replacement(replacement) {
}

auto SuggestionsWidget::getRowsByQuery() const -> std::vector<Row> {
	if (_query.isEmpty()) {
		return {};
	}
	const auto middle = (_query[0] == ':');
	const auto real = middle ? _query.mid(1) : _query;
	const auto simple = [&] {
		if (!middle || _query.size() > 2) {
			return false;
		}
		// Suggest :D and :-P only as exact matches.
		return ranges::none_of(_query, [](QChar ch) { return ch.isLower(); });
	}();
	const auto exact = !middle || simple;
	const auto list = Core::App().emojiKeywords().query(real, exact);
	if (list.empty()) {
		return {};
	}
	using Entry = ChatHelpers::EmojiKeywords::Result;
	auto result = ranges::views::all(
		list
	) | ranges::views::transform([](const Entry &result) {
		return Row(result.emoji, result.replacement);
	}) | ranges::to_vector;

	auto lastRecent = begin(result);
	const auto &recent = GetRecentEmoji();
	for (const auto &item : recent) {
		const auto emoji = item.first->original()
			? item.first->original()
			: item.first;
		const auto it = ranges::find(result, emoji, [](const Row &row) {
			return row.emoji.get();
		});
		if (it > lastRecent && it != end(result)) {
			std::rotate(lastRecent, it, it + 1);
			++lastRecent;
		}
	}

	for (auto &item : result) {
		item.emoji = [&] {
			const auto result = item.emoji;
			const auto &variants = cEmojiVariants();
			const auto i = result->hasVariants()
				? variants.constFind(result->nonColoredId())
				: variants.cend();
			return (i != variants.cend())
				? result->variant(i.value())
				: result.get();
		}();
	}
	return result;
}

void SuggestionsWidget::resizeToRows() {
	const auto count = int(_rows.size());
	const auto scrolled = (count > kMaxNonScrolledEmoji);
	const auto fullWidth = count * _oneWidth;
	const auto newWidth = scrolled
		? st::emojiSuggestionsScrolledWidth
		: fullWidth;
	_scrollMax = std::max(0, fullWidth - newWidth);
	if (_scrollValue > _scrollMax || scrollCurrent() > _scrollMax) {
		scrollTo(std::min(_scrollValue, _scrollMax));
	}
	resize(_padding.left() + newWidth + _padding.right(), height());
	update();
}

bool SuggestionsWidget::eventHook(QEvent *e) {
	if (e->type() == QEvent::Wheel) {
		selectByMouse(QCursor::pos());
		if (_selected >= 0 && _pressed < 0) {
			scrollByWheelEvent(static_cast<QWheelEvent*>(e));
		}
	}
	return RpWidget::eventHook(e);
}

void SuggestionsWidget::scrollByWheelEvent(not_null<QWheelEvent*> e) {
	const auto horizontal = (e->angleDelta().x() != 0);
	const auto vertical = (e->angleDelta().y() != 0);
	const auto current = scrollCurrent();
	const auto scroll = [&] {
		if (horizontal) {
			const auto delta = e->pixelDelta().x()
				? e->pixelDelta().x()
				: e->angleDelta().x();
			return std::clamp(
				current - ((rtl() ? -1 : 1) * delta),
				0,
				_scrollMax);
		} else if (vertical) {
			const auto delta = e->pixelDelta().y()
				? e->pixelDelta().y()
				: e->angleDelta().y();
			return std::clamp(current - delta, 0, _scrollMax);
		}
		return current;
	}();
	if (current != scroll) {
		scrollTo(scroll);
		if (!_lastMousePosition) {
			_lastMousePosition = QCursor::pos();
		}
		selectByMouse(*_lastMousePosition);
		update();
	}
}

void SuggestionsWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto clip = e->rect();
	p.fillRect(clip, st::boxBg);

	const auto shift = innerShift();
	p.translate(-shift);
	const auto paint = clip.translated(shift);
	const auto from = std::max(paint.x(), 0) / _oneWidth;
	const auto till = std::min(
		(paint.x() + paint.width() + _oneWidth - 1) / _oneWidth,
		int(_rows.size()));

	const auto selected = (_pressed >= 0)
		? _pressed
		: _selectedAnimation.value(_selected);
	if (selected > -1.) {
		Ui::FillRoundRect(
			p,
			QRect(selected * _oneWidth, 0, _oneWidth, _oneWidth),
			st::emojiPanHover,
			Ui::StickerHoverCorners);
	}

	for (auto i = from; i != till; ++i) {
		const auto &row = _rows[i];
		const auto emoji = row.emoji;
		const auto esize = Ui::Emoji::GetSizeLarge();
		const auto x = i * _oneWidth;
		const auto y = 0;
		Ui::Emoji::Draw(
			p,
			emoji,
			esize,
			x + (_oneWidth - (esize / cIntRetinaFactor())) / 2,
			y + (_oneWidth - (esize / cIntRetinaFactor())) / 2);
	}
	paintFadings(p);
}

void SuggestionsWidget::paintFadings(Painter &p) const {
	const auto scroll = scrollCurrent();
	const auto o_left = std::clamp(
		scroll / float64(st::emojiSuggestionsFadeAfter),
		0.,
		1.);
	const auto shift = innerShift();
	if (o_left > 0.) {
		p.setOpacity(o_left);
		const auto rect = myrtlrect(
			shift.x(),
			0,
			st::emojiSuggestionsFadeLeft.width(),
			height());
		st::emojiSuggestionsFadeLeft.fill(p, rect);
		p.setOpacity(1.);
	}
	const auto o_right = std::clamp(
		(_scrollMax - scroll) / float64(st::emojiSuggestionsFadeAfter),
		0.,
		1.);
	if (o_right > 0.) {
		p.setOpacity(o_right);
		const auto rect = myrtlrect(
			shift.x() + width() - st::emojiSuggestionsFadeRight.width(),
			0,
			st::emojiSuggestionsFadeRight.width(),
			height());
		st::emojiSuggestionsFadeRight.fill(p, rect);
		p.setOpacity(1.);
	}
}

void SuggestionsWidget::keyPressEvent(QKeyEvent *e) {
	handleKeyEvent(e->key());
}

bool SuggestionsWidget::handleKeyEvent(int key) {
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		return triggerSelectedRow();
	} else if (key == Qt::Key_Tab) {
		if (_selected < 0 || _selected >= _rows.size()) {
			setSelected(0);
		}
		return triggerSelectedRow();
	} else if (_rows.empty()
		|| (key != Qt::Key_Up
			&& key != Qt::Key_Down
			&& key != Qt::Key_Left
			&& key != Qt::Key_Right)) {
		return false;
	}

	const auto delta = (key == Qt::Key_Down || key == Qt::Key_Right)
		? 1
		: -1;
	if (delta < 0 && _selected < 0) {
		return false;
	}
	auto start = _selected;
	if (start < 0 || start >= _rows.size()) {
		start = (delta > 0) ? (_rows.size() - 1) : 0;
	}
	auto newSelected = start + delta;
	if (newSelected < 0) {
		newSelected = -1;
	} else if (newSelected >= _rows.size()) {
		newSelected -= _rows.size();
	}

	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	setSelected(newSelected, anim::type::normal);
	return true;
}

void SuggestionsWidget::setSelected(int selected, anim::type animated) {
	if (selected >= _rows.size()) {
		selected = -1;
	}
	if (animated == anim::type::normal) {
		_selectedAnimation.start(
			[=] { update(); },
			_selected,
			selected,
			kAnimationDuration,
			anim::sineInOut);
		if (_scrollMax > 0) {
			const auto selectedMax = int(_rows.size()) - 3;
			const auto selectedForScroll = std::min(
				std::max(selected, 1) - 1,
				selectedMax);
			scrollTo((_scrollMax * selectedForScroll) / selectedMax, animated);
		}
	} else if (_selectedAnimation.animating()) {
		_selectedAnimation.stop();
		update();
	}
	if (_selected != selected) {
		updateSelectedItem();
		_selected = selected;
		updateSelectedItem();
	}
}

int SuggestionsWidget::scrollCurrent() const {
	return _scrollAnimation.value(_scrollValue);
}

void SuggestionsWidget::scrollTo(int value, anim::type animated) {
	if (animated == anim::type::instant) {
		_scrollAnimation.stop();
	} else {
		_scrollAnimation.start(
			[=] { update(); },
			_scrollValue,
			value,
			kAnimationDuration,
			anim::sineInOut);
	}
	_scrollValue = value;
	update();
}

void SuggestionsWidget::stopAnimations() {
	_scrollValue = _scrollAnimation.value(_scrollValue);
	_scrollAnimation.stop();
}

void SuggestionsWidget::setPressed(int pressed) {
	if (pressed >= _rows.size()) {
		pressed = -1;
	}
	if (_pressed != pressed) {
		_pressed = pressed;
		if (_pressed >= 0) {
			_mousePressPosition = QCursor::pos();
		}
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

void SuggestionsWidget::updateItem(int index) {
	if (index >= 0 && index < _rows.size()) {
		update(
			_padding.left() + index * _oneWidth - scrollCurrent(),
			_padding.top(),
			_oneWidth,
			_oneWidth);
	}
}

void SuggestionsWidget::updateSelectedItem() {
	updateItem(_selected);
}

QRect SuggestionsWidget::inner() const {
	return QRect(0, 0, _rows.size() * _oneWidth, _oneWidth);
}

QPoint SuggestionsWidget::innerShift() const {
	return QPoint(scrollCurrent() - _padding.left(), -_padding.top());
}

QPoint SuggestionsWidget::mapToInner(QPoint globalPosition) const {
	return mapFromGlobal(globalPosition) + innerShift();
}

void SuggestionsWidget::mouseMoveEvent(QMouseEvent *e) {
	const auto globalPosition = e->globalPos();
	if (_dragScrollStart >= 0) {
		const auto delta = (_mousePressPosition.x() - globalPosition.x());
		const auto scroll = std::clamp(
			_dragScrollStart + (rtl() ? -1 : 1) * delta,
			0,
			_scrollMax);
		if (scrollCurrent() != scroll) {
			scrollTo(scroll);
			update();
		}
		return;
	} else if ((_pressed >= 0)
		&& (_scrollMax > 0)
		&& ((_mousePressPosition - globalPosition).manhattanLength()
			>= QApplication::startDragDistance())) {
		_dragScrollStart = scrollCurrent();
		_mousePressPosition = globalPosition;
		scrollTo(_dragScrollStart);
	}
	if (inner().contains(mapToInner(globalPosition))) {
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
	const auto p = mapToInner(globalPosition);
	const auto selected = (p.x() >= 0) ? (p.x() / _oneWidth) : -1;
	setSelected((selected >= 0 && selected < _rows.size()) ? selected : -1);
}

void SuggestionsWidget::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());
	if (_selected >= 0) {
		setPressed(_selected);
	}
}

void SuggestionsWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_pressed >= 0) {
		const auto pressed = _pressed;
		setPressed(-1);
		if (_dragScrollStart >= 0) {
			_dragScrollStart = -1;
		} else if (pressed == _selected) {
			triggerRow(_rows[_selected]);
		}
	}
}

bool SuggestionsWidget::triggerSelectedRow() const {
	if (_selected >= 0) {
		triggerRow(_rows[_selected]);
		return true;
	}
	return false;
}

void SuggestionsWidget::triggerRow(const Row &row) const {
	_triggered.fire(row.emoji->text());
}

void SuggestionsWidget::enterEventHook(QEvent *e) {
	if (!inner().contains(mapToInner(QCursor::pos()))) {
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
	not_null<QTextEdit*> field,
	not_null<Main::Session*> session,
	const Options &options)
: _field(field)
, _session(session)
, _showExactTimer([=] { showWithQuery(getEmojiQuery()); })
, _options(options) {
	_container = base::make_unique_q<InnerDropdown>(
		outer,
		st::emojiSuggestionsDropdown);
	_container->setAutoHiding(false);
	_suggestions = _container->setOwnedWidget(
		object_ptr<Ui::Emoji::SuggestionsWidget>(_container));

	setReplaceCallback(nullptr);

	const auto fieldCallback = [=](not_null<QEvent*> event) {
		return fieldFilter(event)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	};
	_fieldFilter.reset(base::install_event_filter(_field, fieldCallback));

	const auto outerCallback = [=](not_null<QEvent*> event) {
		return outerFilter(event)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	};
	_outerFilter.reset(base::install_event_filter(outer, outerCallback));

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
	Core::App().emojiKeywords().refreshed(
	) | rpl::start_with_next([=] {
		_keywordsRefreshed = true;
		if (!_showExactTimer.isActive()) {
			showWithQuery(_lastShownQuery);
		}
	}, _lifetime);

	updateForceHidden();

	handleTextChange();
}

SuggestionsController *SuggestionsController::Init(
		not_null<QWidget*> outer,
		not_null<Ui::InputField*> field,
		not_null<Main::Session*> session,
		const Options &options) {
	const auto result = Ui::CreateChild<SuggestionsController>(
		field.get(),
		outer,
		field->rawTextEdit(),
		session,
		options);
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
	if (Core::App().settings().suggestEmoji()
		&& _field->textCursor().position() > 0) {
		Core::App().emojiKeywords().refresh();
	}

	_ignoreCursorPositionChange = true;
	InvokeQueued(_container, [=] { _ignoreCursorPositionChange = false; });

	const auto query = getEmojiQuery();
	if (query.isEmpty() || _textChangeAfterKeyPress) {
		const auto exact = (!query.isEmpty() && query[0] != ':');
		if (exact) {
			const auto hidden = _container->isHidden()
				|| _container->isHiding();
			_showExactTimer.callOnce(hidden ? kShowExactDelay : 0);
		} else {
			showWithQuery(query);
			_suggestions->selectFirstResult();
		}
	}
}

void SuggestionsController::showWithQuery(const QString &query) {
	_showExactTimer.cancel();
	const auto force = base::take(_keywordsRefreshed);
	_lastShownQuery = query;
	_suggestions->showWithQuery(_lastShownQuery, force);
}

QString SuggestionsController::getEmojiQuery() {
	if (!Core::App().settings().suggestEmoji()) {
		return QString();
	}

	const auto cursor = _field->textCursor();
	if (cursor.hasSelection()) {
		return QString();
	}

	const auto modernLimit = Core::App().emojiKeywords().maxQueryLength();
	const auto legacyLimit = GetSuggestionMaxLength();
	const auto position = cursor.position();
	const auto findTextPart = [&] {
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
			_queryStartPosition = from;
			return fragment.text();
		}
		return QString();
	};

	const auto text = findTextPart();
	if (text.isEmpty()) {
		return QString();
	}
	const auto length = position - _queryStartPosition;
	for (auto i = length; i != 0;) {
		if (text[--i] == ':') {
			const auto previous = (i > 0) ? text[i - 1] : QChar(0);
			if (i > 0 && (previous.isLetter() || previous.isDigit())) {
				return QString();
			} else if (i + 1 == length || text[i + 1].isSpace()) {
				return QString();
			}
			_queryStartPosition += i + 2;
			return text.mid(i, length - i);
		}
		if (length - i > legacyLimit && length - i > modernLimit) {
			return QString();
		}
	}

	// Exact query should be full input field value.
	const auto end = [&] {
		auto cursor = _field->textCursor();
		cursor.movePosition(QTextCursor::End);
		return cursor.position();
	}();
	if (!_options.suggestExactFirstWord
		|| !length
		|| text[0].isSpace()
		|| (length > modernLimit)
		|| (_queryStartPosition != 0)
		|| (position != end)) {
		return QString();
	}
	return text;
}

void SuggestionsController::replaceCurrent(const QString &replacement) {
	const auto suggestion = getEmojiQuery();
	if (suggestion.isEmpty()) {
		showWithQuery(QString());
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
		showWithQuery(QString());
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
	const auto padding = st::emojiSuggestionsDropdown.padding;
	const auto shift = std::min(_container->width() - padding.left() - padding.right(), st::emojiSuggestionSize) / 2;
	point -= rtl() ? QPoint(_container->width() - padding.right() - shift, _container->height()) : QPoint(padding.left() + shift, _container->height());
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
		case Qt::Key_Left:
		case Qt::Key_Right:
			if (_shown && !_forceHidden) {
				return _suggestions->handleKeyEvent(key);
			}
			break;

		case Qt::Key_Escape:
			if (_shown && !_forceHidden) {
				showWithQuery(QString());
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
