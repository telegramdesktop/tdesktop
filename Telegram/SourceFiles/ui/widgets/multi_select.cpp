/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/multi_select.h"

#include "styles/style_widgets.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/effects/cross_animation.h"
#include "ui/text/text_options.h"
#include "lang/lang_keys.h"
#include "app.h"

namespace Ui {
namespace {

constexpr int kWideScale = 3;

} // namespace

MultiSelect::Item::Item(const style::MultiSelectItem &st, uint64 id, const QString &text, style::color color, PaintRoundImage &&paintRoundImage)
: _st(st)
, _id(id)
, _color(color)
, _paintRoundImage(std::move(paintRoundImage)) {
	setText(text);
}

void MultiSelect::Item::setText(const QString &text) {
	_text.setText(_st.style, text, NameTextOptions());
	_width = _st.height + _st.padding.left() + _text.maxWidth() + _st.padding.right();
	accumulate_min(_width, _st.maxWidth);
}

void MultiSelect::Item::paint(Painter &p, int outerWidth) {
	if (!_cache.isNull() && !_visibility.animating()) {
		if (_hiding) {
			return;
		} else {
			_cache = QPixmap();
		}
	}
	if (_copies.empty()) {
		paintOnce(p, _x, _y, outerWidth);
	} else {
		for (auto i = _copies.begin(), e = _copies.end(); i != e;) {
			auto x = qRound(i->x.value(_x));
			auto y = i->y;
			auto animating = i->x.animating();
			if (animating || (y == _y)) {
				paintOnce(p, x, y, outerWidth);
			}
			if (animating) {
				++i;
			} else {
				i = _copies.erase(i);
				e = _copies.end();
			}
		}
	}
}

void MultiSelect::Item::paintOnce(Painter &p, int x, int y, int outerWidth) {
	if (!_cache.isNull()) {
		paintCached(p, x, y, outerWidth);
		return;
	}

	auto radius = _st.height / 2;
	auto inner = style::rtlrect(x + radius, y, _width - radius, _st.height, outerWidth);

	auto clipEnabled = p.hasClipping();
	auto clip = clipEnabled ? p.clipRegion() : QRegion();
	p.setClipRect(inner);

	p.setPen(Qt::NoPen);
	p.setBrush(_active ? _st.textActiveBg : _st.textBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(style::rtlrect(x, y, _width, _st.height, outerWidth), radius, radius);
	}

	if (clipEnabled) {
		p.setClipRegion(clip);
	} else {
		p.setClipping(false);
	}

	auto overOpacity = _overOpacity.value(_over ? 1. : 0.);
	if (overOpacity < 1.) {
		_paintRoundImage(p, x, y, outerWidth, _st.height);
	}
	if (overOpacity > 0.) {
		paintDeleteButton(p, x, y, outerWidth, overOpacity);
	}

	auto textLeft = _st.height + _st.padding.left();
	auto textWidth = _width - textLeft - _st.padding.right();
	p.setPen(_active ? _st.textActiveFg : _st.textFg);
	_text.drawLeftElided(p, x + textLeft, y + _st.padding.top(), textWidth, outerWidth);
}

void MultiSelect::Item::paintDeleteButton(Painter &p, int x, int y, int outerWidth, float64 overOpacity) {
	p.setOpacity(overOpacity);

	p.setPen(Qt::NoPen);
	p.setBrush(_color);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(style::rtlrect(x, y, _st.height, _st.height, outerWidth));
	}

	CrossAnimation::paint(p, _st.deleteCross, _st.deleteFg, x, y, outerWidth, overOpacity);

	p.setOpacity(1.);
}

bool MultiSelect::Item::paintCached(Painter &p, int x, int y, int outerWidth) {
	PainterHighQualityEnabler hq(p);

	auto opacity = _visibility.value(_hiding ? 0. : 1.);
	auto scale = opacity + _st.minScale * (1. - opacity);
	auto height = opacity * _cache.height() / _cache.devicePixelRatio();
	auto width = opacity * _cache.width() / _cache.devicePixelRatio();

	p.setOpacity(opacity);
	p.drawPixmap(style::rtlrect(x + (_width - width) / 2., y + (_st.height - height) / 2., width, height, outerWidth), _cache);
	p.setOpacity(1.);
	return true;
}

void MultiSelect::Item::mouseMoveEvent(QPoint point) {
	if (!_cache.isNull()) return;
	_overDelete = QRect(0, 0, _st.height, _st.height).contains(point);
	setOver(true);
}

void MultiSelect::Item::leaveEvent() {
	_overDelete = false;
	setOver(false);
}

void MultiSelect::Item::setPosition(int x, int y, int outerWidth, int maxVisiblePadding) {
	if (_x >= 0 && _y >= 0 && (_x != x || _y != y)) {
		// Make an animation if it is not the first setPosition().
		auto found = false;
		auto leftHidden = -_width - maxVisiblePadding;
		auto rightHidden = outerWidth + maxVisiblePadding;
		for (auto i = _copies.begin(), e = _copies.end(); i != e;) {
			if (i->x.animating()) {
				if (i->y == y) {
					i->x.start(_updateCallback, i->toX, x, _st.duration);
					found = true;
				} else {
					i->x.start(_updateCallback, i->fromX, (i->toX > i->fromX) ? rightHidden : leftHidden, _st.duration);
				}
				++i;
			} else {
				i = _copies.erase(i);
				e = _copies.end();
			}
		}
		if (_copies.empty()) {
			if (_y == y) {
				auto copy = SlideAnimation(_updateCallback, _x, x, _y, _st.duration);
				_copies.push_back(std::move(copy));
			} else {
				auto copyHiding = SlideAnimation(_updateCallback, _x, (y > _y) ? rightHidden : leftHidden, _y, _st.duration);
				_copies.push_back(std::move(copyHiding));
				auto copyShowing = SlideAnimation(_updateCallback, (y > _y) ? leftHidden : rightHidden, x, y, _st.duration);
				_copies.push_back(std::move(copyShowing));
			}
		} else if (!found) {
			auto copy = SlideAnimation(_updateCallback, (y > _y) ? leftHidden : rightHidden, x, y, _st.duration);
			_copies.push_back(std::move(copy));
		}
	}
	_x = x;
	_y = y;
}

QRect MultiSelect::Item::paintArea(int outerWidth) const {
	if (_copies.empty()) {
		return rect();
	}
	auto yMin = 0, yMax = 0;
	for_const (auto &copy, _copies) {
		accumulate_max(yMax, copy.y);
		if (yMin) {
			accumulate_min(yMin, copy.y);
		} else {
			yMin = copy.y;
		}
	}
	return QRect(0, yMin, outerWidth, yMax - yMin + _st.height);
}

void MultiSelect::Item::prepareCache() {
	if (!_cache.isNull()) return;

	Assert(!_visibility.animating());
	auto cacheWidth = _width * kWideScale * cIntRetinaFactor();
	auto cacheHeight = _st.height * kWideScale * cIntRetinaFactor();
	auto data = QImage(cacheWidth, cacheHeight, QImage::Format_ARGB32_Premultiplied);
	data.fill(Qt::transparent);
	data.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&data);
		paintOnce(p, _width * (kWideScale - 1) / 2, _st.height  * (kWideScale - 1) / 2, cacheWidth);
	}
	_cache = App::pixmapFromImageInPlace(std::move(data));
}

void MultiSelect::Item::setVisibleAnimated(bool visible) {
	_hiding = !visible;
	prepareCache();
	auto from = visible ? 0. : 1.;
	auto to = visible ? 1. : 0.;
	auto transition = visible ? anim::bumpy(1.0625) : anim::linear;
	_visibility.start(_updateCallback, from, to, _st.duration, transition);
}

void MultiSelect::Item::setOver(bool over) {
	if (over != _over) {
		_over = over;
		_overOpacity.start(_updateCallback, _over ? 0. : 1., _over ? 1. : 0., _st.duration);
	}
}

MultiSelect::MultiSelect(
	QWidget *parent,
	const style::MultiSelect &st,
	rpl::producer<QString> placeholder)
: RpWidget(parent)
, _st(st)
, _scroll(this, _st.scroll) {
	const auto scrollCallback = [=](int activeTop, int activeBottom) {
		scrollTo(activeTop, activeBottom);
	};
	_inner = _scroll->setOwnedWidget(object_ptr<Inner>(
		this,
		st,
		std::move(placeholder),
		scrollCallback));
	_scroll->installEventFilter(this);
	_inner->setResizedCallback([this](int innerHeightDelta) {
		auto newHeight = resizeGetHeight(width());
		if (innerHeightDelta > 0) {
			_scroll->scrollToY(_scroll->scrollTop() + innerHeightDelta);
		}
		if (newHeight != height()) {
			resize(width(), newHeight);
			if (_resizedCallback) {
				_resizedCallback();
			}
		}
	});
	_inner->setQueryChangedCallback([this](const QString &query) {
		_scroll->scrollToY(_scroll->scrollTopMax());
		if (_queryChangedCallback) {
			_queryChangedCallback(query);
		}
	});

	setAttribute(Qt::WA_OpaquePaintEvent);
	auto defaultWidth = _st.item.maxWidth + _st.fieldMinWidth + _st.fieldCancelSkip;
	resizeToWidth(_st.padding.left() + defaultWidth + _st.padding.right());
}

bool MultiSelect::eventFilter(QObject *o, QEvent *e) {
	if (o == _scroll && e->type() == QEvent::KeyPress) {
		e->ignore();
		return true;
	}
	return false;
}

void MultiSelect::scrollTo(int activeTop, int activeBottom) {
	auto scrollTop = _scroll->scrollTop();
	auto scrollHeight = _scroll->height();
	auto scrollBottom = scrollTop + scrollHeight;
	if (scrollTop > activeTop) {
		_scroll->scrollToY(activeTop);
	} else if (scrollBottom < activeBottom) {
		_scroll->scrollToY(activeBottom - scrollHeight);
	}
}

void MultiSelect::setQueryChangedCallback(Fn<void(const QString &query)> callback) {
	_queryChangedCallback = std::move(callback);
}

void MultiSelect::setSubmittedCallback(Fn<void(Qt::KeyboardModifiers)> callback) {
	_inner->setSubmittedCallback(std::move(callback));
}

void MultiSelect::setCancelledCallback(Fn<void()> callback) {
	_inner->setCancelledCallback(std::move(callback));
}

void MultiSelect::setResizedCallback(Fn<void()> callback) {
	_resizedCallback = std::move(callback);
}

void MultiSelect::setInnerFocus() {
	if (_inner->setInnerFocus()) {
		_scroll->scrollToY(_scroll->scrollTopMax());
	}
}

void MultiSelect::clearQuery() {
	_inner->clearQuery();
}

QString MultiSelect::getQuery() const {
	return _inner->getQuery();
}

void MultiSelect::addItem(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage, AddItemWay way) {
	addItemInBunch(itemId, text, color, std::move(paintRoundImage));
	_inner->finishItemsBunch(way);
}

void MultiSelect::addItemInBunch(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage) {
	_inner->addItemInBunch(std::make_unique<Item>(_st.item, itemId, text, color, std::move(paintRoundImage)));
}

void MultiSelect::finishItemsBunch() {
	_inner->finishItemsBunch(AddItemWay::SkipAnimation);
}

void MultiSelect::setItemRemovedCallback(Fn<void(uint64 itemId)> callback) {
	_inner->setItemRemovedCallback(std::move(callback));
}

void MultiSelect::removeItem(uint64 itemId) {
	_inner->removeItem(itemId);
}

int MultiSelect::getItemsCount() const {
	return _inner->getItemsCount();
}

QVector<uint64> MultiSelect::getItems() const {
	return _inner->getItems();
}

bool MultiSelect::hasItem(uint64 itemId) const {
	return _inner->hasItem(itemId);
}

int MultiSelect::resizeGetHeight(int newWidth) {
	if (newWidth != _inner->width()) {
		_inner->resizeToWidth(newWidth);
	}
	auto newHeight = qMin(_inner->height(), _st.maxHeight);
	_scroll->setGeometryToLeft(0, 0, newWidth, newHeight);
	return newHeight;
}

MultiSelect::Inner::Inner(
	QWidget *parent,
	const style::MultiSelect &st,
	rpl::producer<QString> placeholder,
	ScrollCallback callback)
: TWidget(parent)
, _st(st)
, _scrollCallback(std::move(callback))
, _field(this, _st.field, std::move(placeholder))
, _cancel(this, _st.fieldCancel) {
	_field->customUpDown(true);
	connect(_field, &Ui::InputField::focused, [=] { fieldFocused(); });
	connect(_field, &Ui::InputField::changed, [=] { queryChanged(); });
	connect(_field, &Ui::InputField::submitted, this, &Inner::submitted);
	connect(_field, &Ui::InputField::cancelled, this, &Inner::cancelled);
	_cancel->setClickedCallback([=] {
		clearQuery();
		_field->setFocus();
	});
	setMouseTracking(true);
}

void MultiSelect::Inner::queryChanged() {
	auto query = getQuery();
	_cancel->toggle(!query.isEmpty(), anim::type::normal);
	updateFieldGeometry();
	if (_queryChangedCallback) {
		_queryChangedCallback(query);
	}
}

QString MultiSelect::Inner::getQuery() const {
	return _field->getLastText().trimmed();
}

bool MultiSelect::Inner::setInnerFocus() {
	if (_active >= 0) {
		setFocus();
	} else if (!_field->hasFocus()) {
		_field->setFocusFast();
		return true;
	}
	return false;
}

void MultiSelect::Inner::clearQuery() {
	_field->setText(QString());
}

void MultiSelect::Inner::setQueryChangedCallback(Fn<void(const QString &query)> callback) {
	_queryChangedCallback = std::move(callback);
}

void MultiSelect::Inner::setSubmittedCallback(
		Fn<void(Qt::KeyboardModifiers)> callback) {
	_submittedCallback = std::move(callback);
}

void MultiSelect::Inner::setCancelledCallback(Fn<void()> callback) {
	_cancelledCallback = std::move(callback);
}

void MultiSelect::Inner::updateFieldGeometry() {
	auto fieldFinalWidth = _fieldWidth;
	if (_cancel->toggled()) {
		fieldFinalWidth -= _st.fieldCancelSkip;
	}
	_field->resizeToWidth(fieldFinalWidth);
	_field->moveToLeft(_st.padding.left() + _fieldLeft, _st.padding.top() + _fieldTop);
}

void MultiSelect::Inner::updateHasAnyItems(bool hasAnyItems) {
	_field->setPlaceholderHidden(hasAnyItems);
	updateCursor();
	_iconOpacity.start([this] {
		rtlupdate(_st.padding.left(), _st.padding.top(), _st.fieldIcon.width(), _st.fieldIcon.height());
	}, hasAnyItems ? 1. : 0., hasAnyItems ? 0. : 1., _st.item.duration);
}

void MultiSelect::Inner::updateCursor() {
	setCursor(_items.empty() ? style::cur_text : (_overDelete ? style::cur_pointer : style::cur_default));
}

void MultiSelect::Inner::setActiveItem(int active, ChangeActiveWay skipSetFocus) {
	if (_active == active) return;

	if (_active >= 0) {
		Assert(_active < _items.size());
		_items[_active]->setActive(false);
	}
	_active = active;
	if (_active >= 0) {
		Assert(_active < _items.size());
		_items[_active]->setActive(true);
	}
	if (skipSetFocus != ChangeActiveWay::SkipSetFocus) {
		setInnerFocus();
	}
	if (_scrollCallback) {
		auto rect = (_active >= 0) ? _items[_active]->rect() : _field->geometry().translated(-_st.padding.left(), -_st.padding.top());
		_scrollCallback(rect.y(), rect.y() + rect.height() + _st.padding.top() + _st.padding.bottom());
	}
	update();
}

void MultiSelect::Inner::setActiveItemPrevious() {
	if (_active > 0) {
		setActiveItem(_active - 1);
	} else if (_active < 0 && !_items.empty()) {
		setActiveItem(_items.size() - 1);
	}
}

void MultiSelect::Inner::setActiveItemNext() {
	if (_active >= 0 && _active + 1 < _items.size()) {
		setActiveItem(_active + 1);
	} else {
		setActiveItem(-1);
	}
}

int MultiSelect::Inner::resizeGetHeight(int newWidth) {
	computeItemsGeometry(newWidth);
	updateFieldGeometry();

	auto cancelLeft = _fieldLeft + _fieldWidth + _st.padding.right() - _cancel->width();
	auto cancelTop = _fieldTop - _st.padding.top();
	_cancel->moveToLeft(_st.padding.left() + cancelLeft, _st.padding.top() + cancelTop);

	return _field->y() + _field->height() + _st.padding.bottom();
}

void MultiSelect::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto paintRect = e->rect();
	p.fillRect(paintRect, _st.bg);

	auto offset = QPoint(rtl() ? _st.padding.right() : _st.padding.left(), _st.padding.top());
	p.translate(offset);
	paintRect.translate(-offset);

	auto outerWidth = width() - _st.padding.left() - _st.padding.right();
	auto iconOpacity = _iconOpacity.value(_items.empty() ? 1. : 0.);
	if (iconOpacity > 0.) {
		p.setOpacity(iconOpacity);
		_st.fieldIcon.paint(p, 0, 0, outerWidth);
		p.setOpacity(1.);
	}

	auto checkRect = myrtlrect(paintRect);
	auto paintMargins = itemPaintMargins();
	for (auto i = _removingItems.begin(), e = _removingItems.end(); i != e;) {
		auto &item = *i;
		auto itemRect = item->paintArea(outerWidth);
		itemRect = itemRect.marginsAdded(paintMargins);
		if (checkRect.intersects(itemRect)) {
			item->paint(p, outerWidth);
		}
		if (item->hideFinished()) {
			i = _removingItems.erase(i);
			e = _removingItems.end();
		} else {
			++i;
		}
	}
	for (const auto &item : _items) {
		auto itemRect = item->paintArea(outerWidth);
		itemRect = itemRect.marginsAdded(paintMargins);
		if (checkRect.y() + checkRect.height() <= itemRect.y()) {
			break;
		} else if (checkRect.intersects(itemRect)) {
			item->paint(p, outerWidth);
		}
	}
}

QMargins MultiSelect::Inner::itemPaintMargins() const {
	return {
		qMax(_st.itemSkip, _st.padding.left()),
		_st.itemSkip,
		qMax(_st.itemSkip, _st.padding.right()),
		_st.itemSkip,
	};
}

void MultiSelect::Inner::leaveEventHook(QEvent *e) {
	clearSelection();
}

void MultiSelect::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelection(e->pos());
}

void MultiSelect::Inner::keyPressEvent(QKeyEvent *e) {
	if (_active >= 0) {
		Expects(_active < _items.size());
		if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
			auto itemId = _items[_active]->id();
			setActiveItemNext();
			removeItem(itemId);
		} else if (e->key() == Qt::Key_Left) {
			setActiveItemPrevious();
		} else if (e->key() == Qt::Key_Right) {
			setActiveItemNext();
		} else if (e->key() == Qt::Key_Escape) {
			setActiveItem(-1);
		} else {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Backspace) {
		setActiveItemPrevious();
	} else {
		e->ignore();
	}
}

void MultiSelect::Inner::submitted(Qt::KeyboardModifiers modifiers) {
	if (_submittedCallback) {
		_submittedCallback(modifiers);
	}
}

void MultiSelect::Inner::cancelled() {
	if (_cancelledCallback) {
		_cancelledCallback();
	}
}

void MultiSelect::Inner::fieldFocused() {
	setActiveItem(-1, ChangeActiveWay::SkipSetFocus);
}

void MultiSelect::Inner::updateSelection(QPoint mousePosition) {
	auto point = myrtlpoint(mousePosition) - QPoint(_st.padding.left(), _st.padding.right());
	auto selected = -1;
	for (auto i = 0, count = int(_items.size()); i != count; ++i) {
		auto itemRect = _items[i]->rect();
		if (itemRect.y() > point.y()) {
			break;
		} else if (itemRect.contains(point)) {
			point -= itemRect.topLeft();
			selected = i;
			break;
		}
	}
	if (_selected != selected) {
		if (_selected >= 0) {
			Assert(_selected < _items.size());
			_items[_selected]->leaveEvent();
		}
		_selected = selected;
		update();
	}
	auto overDelete = false;
	if (_selected >= 0) {
		_items[_selected]->mouseMoveEvent(point);
		overDelete = _items[_selected]->isOverDelete();
	}
	if (_overDelete != overDelete) {
		_overDelete = overDelete;
		updateCursor();
	}
}

void MultiSelect::Inner::mousePressEvent(QMouseEvent *e) {
	if (_overDelete) {
		Expects(_selected >= 0);
		Expects(_selected < _items.size());
		removeItem(_items[_selected]->id());
	} else if (_selected >= 0) {
		setActiveItem(_selected);
	} else {
		setInnerFocus();
	}
}

void MultiSelect::Inner::addItemInBunch(std::unique_ptr<Item> item) {
	auto wasEmpty = _items.empty();
	item->setUpdateCallback([this, item = item.get()] {
		auto itemRect = item->paintArea(width() - _st.padding.left() - _st.padding.top());
		itemRect = itemRect.translated(_st.padding.left(), _st.padding.top());
		itemRect = itemRect.marginsAdded(itemPaintMargins());
		rtlupdate(itemRect);
	});
	_idsMap.insert(item->id());
	_items.push_back(std::move(item));
	if (wasEmpty) {
		updateHasAnyItems(true);
	}
}

void MultiSelect::Inner::finishItemsBunch(AddItemWay way) {
	updateItemsGeometry();
	if (way != AddItemWay::SkipAnimation) {
		_items.back()->showAnimated();
	} else {
		_field->finishAnimating();
		finishHeightAnimation();
	}
}

void MultiSelect::Inner::computeItemsGeometry(int newWidth) {
	newWidth -= _st.padding.left() + _st.padding.right();

	auto itemLeft = 0;
	auto itemTop = 0;
	auto widthLeft = newWidth;
	auto maxVisiblePadding = qMax(_st.padding.left(), _st.padding.right());
	for_const (auto &item, _items) {
		auto itemWidth = item->getWidth();
		Assert(itemWidth <= newWidth);
		if (itemWidth > widthLeft) {
			itemLeft = 0;
			itemTop += _st.item.height + _st.itemSkip;
			widthLeft = newWidth;
		}
		item->setPosition(itemLeft, itemTop, newWidth, maxVisiblePadding);
		itemLeft += itemWidth + _st.itemSkip;
		widthLeft -= itemWidth + _st.itemSkip;
	}

	auto fieldMinWidth = _st.fieldMinWidth + _st.fieldCancelSkip;
	Assert(fieldMinWidth <= newWidth);
	if (fieldMinWidth > widthLeft) {
		_fieldLeft = 0;
		_fieldTop = itemTop + _st.item.height + _st.itemSkip;
	} else {
		_fieldLeft = itemLeft + (_items.empty() ? _st.fieldIconSkip : 0);
		_fieldTop = itemTop;
	}
	_fieldWidth = newWidth - _fieldLeft;
}

void MultiSelect::Inner::updateItemsGeometry() {
	auto newHeight = resizeGetHeight(width());
	if (newHeight == _newHeight) return;

	_newHeight = newHeight;
	_height.start([this] { updateHeightStep(); }, height(), _newHeight, _st.item.duration);
}

void MultiSelect::Inner::updateHeightStep() {
	auto newHeight = qRound(_height.value(_newHeight));
	if (auto heightDelta = newHeight - height()) {
		resize(width(), newHeight);
		if (_resizedCallback) {
			_resizedCallback(heightDelta);
		}
		update();
	}
}

void MultiSelect::Inner::finishHeightAnimation() {
	_height.stop();
	updateHeightStep();
}

void MultiSelect::Inner::setItemText(uint64 itemId, const QString &text) {
	for_const (auto &item, _items) {
		if (item->id() == itemId) {
			item->setText(text);
			updateItemsGeometry();
			return;
		}
	}
}

void MultiSelect::Inner::setItemRemovedCallback(Fn<void(uint64 itemId)> callback) {
	_itemRemovedCallback = std::move(callback);
}

void MultiSelect::Inner::setResizedCallback(Fn<void(int heightDelta)> callback) {
	_resizedCallback = std::move(callback);
}

void MultiSelect::Inner::removeItem(uint64 itemId) {
	auto found = false;
	for (auto i = 0, count = int(_items.size()); i != count; ++i) {
		auto &item = _items[i];
		if (item->id() == itemId) {
			found = true;
			clearSelection();

			item->hideAnimated();
			_idsMap.erase(item->id());
			auto inserted = _removingItems.insert(std::move(item));
			_items.erase(_items.begin() + i);

			if (_active == i) {
				_active = -1;
			} else if (_active > i) {
				--_active;
			}

			updateItemsGeometry();
			if (_items.empty()) {
				updateHasAnyItems(false);
			}
			auto point = QCursor::pos();
			if (auto parent = parentWidget()) {
				if (parent->rect().contains(parent->mapFromGlobal(point))) {
					updateSelection(mapFromGlobal(point));
				}
			}
			break;
		}
	}
	if (found && _itemRemovedCallback) {
		_itemRemovedCallback(itemId);
	}
	setInnerFocus();
}

int MultiSelect::Inner::getItemsCount() const {
	return _items.size();
}

QVector<uint64> MultiSelect::Inner::getItems() const {
	auto result = QVector<uint64>();
	result.reserve(_items.size());
	for_const (auto &item, _items) {
		result.push_back(item->id());
	}
	return result;
}

bool MultiSelect::Inner::hasItem(uint64 itemId) const {
	return _idsMap.find(itemId) != _idsMap.cend();
}

MultiSelect::Inner::~Inner() = default;

} // namespace Ui
