/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/labeled_emoji_tabs.h"

#include "base/object_ptr.h"
#include "ui/abstract_button.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"

#include <algorithm>
#include <cmath>

#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

[[nodiscard]] QColor ColorWithAlpha(
		const style::color &color,
		float64 alpha) {
	auto result = color->c;
	result.setAlphaF(result.alphaF() * alpha);
	return result;
}

[[nodiscard]] QColor ActiveBackgroundColor(const style::color &color) {
	return ColorWithAlpha(color, st::aiComposeButtonBgActiveOpacity);
}

[[nodiscard]] QColor RippleColor(
		const style::RippleAnimation &ripple,
		float64 opacity) {
	return ColorWithAlpha(ripple.color, opacity);
}

[[nodiscard]] qreal TabsRadius() {
	return st::aiComposeStyleTabsRadius;
}

} // namespace

class LabeledEmojiTabs::Button final : public RippleButton {
public:
	Button(
		QWidget *parent,
		LabeledEmojiTab descriptor,
		Text::CustomEmojiFactory factory);

	void setSelected(bool selected);
	void setExtraPadding(int extra);
	void setContextMenuCallback(Fn<void(QPoint)> callback);
	[[nodiscard]] const QString &id() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	[[nodiscard]] QImage prepareRippleMask() const override;

private:
	const LabeledEmojiTab _descriptor;
	std::unique_ptr<Text::CustomEmoji> _custom;
	Fn<void(QPoint)> _contextMenuCallback;
	bool _selected = false;
	int _extraPadding = 0;

};

class LabeledEmojiScrollTabs::DragScroll final : public QObject {
public:
	DragScroll(
		not_null<QObject*> parent,
		not_null<ScrollArea*> scroll,
		Fn<void()> beforeScroll);

	void add(not_null<LabeledEmojiTabs::Button*> button);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	const not_null<ScrollArea*> _scroll;
	const Fn<void()> _beforeScroll;
	QPointer<LabeledEmojiTabs::Button> _pressed;
	QPoint _pressGlobal;
	int _scrollStart = 0;
	bool _dragging = false;

};

LabeledEmojiTabs::Button::Button(
	QWidget *parent,
	LabeledEmojiTab descriptor,
	Text::CustomEmojiFactory factory)
: RippleButton(parent, st::aiComposeButtonRippleInactive)
, _descriptor(std::move(descriptor))
, _custom(!_descriptor.customEmojiData.isEmpty() && factory
	? factory(
		_descriptor.customEmojiData,
		{ .repaint = [this] { update(); } })
	: nullptr) {
	setCursor(style::cur_pointer);
	setAccessibleName(_descriptor.label);
	setNaturalWidth([&] {
		const auto padding = st::aiComposeStyleButtonPadding;
		const auto labelWidth = st::aiComposeStyleLabelFont->width(
			_descriptor.label);
		const auto emojiWidth = (_custom || _descriptor.emoji || _descriptor.icon)
			? (Emoji::GetSizeLarge() / style::DevicePixelRatio())
			: 0;
		return padding.left()
			+ std::max(labelWidth, emojiWidth)
			+ padding.right();
	}());
	setExtraPadding(0);
}

void LabeledEmojiTabs::Button::setSelected(bool selected) {
	if (_selected == selected) {
		return;
	}
	_selected = selected;
	update();
}

void LabeledEmojiTabs::Button::setExtraPadding(int extra) {
	_extraPadding = extra;
	resize(naturalWidth() + 2 * extra, height());
}

const QString &LabeledEmojiTabs::Button::id() const {
	return _descriptor.id;
}

void LabeledEmojiTabs::Button::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	const auto radius = TabsRadius();
	if (_selected) {
		p.setPen(Qt::NoPen);
		p.setBrush(ActiveBackgroundColor(st::aiComposeStyleButtonBgActive));
		p.drawRoundedRect(rect(), radius, radius);
	}
	const auto ripple = RippleColor(
		_selected
			? st::aiComposeButtonRippleActive
			: st::aiComposeButtonRippleInactive,
		_selected
			? st::aiComposeButtonRippleActiveOpacity
			: st::aiComposeButtonRippleInactiveOpacity);
	paintRipple(p, 0, 0, &ripple);

	if (_custom) {
		const auto size = Emoji::GetSizeLarge() / style::DevicePixelRatio();
		const auto adjusted = Text::AdjustCustomEmojiSize(size);
		const auto skip = (size - adjusted) / 2;
		const auto left = (width() - size) / 2;
		_custom->paint(p, {
			.textColor = (_selected
				? st::aiComposeStyleLabelFgActive
				: st::aiComposeStyleLabelFg)->c,
			.now = crl::now(),
			.position = {
				left + skip,
				st::aiComposeStyleEmojiTop + skip,
			},
		});
	} else if (_descriptor.emoji) {
		const auto size = Emoji::GetSizeLarge() / style::DevicePixelRatio();
		const auto left = (width() - size) / 2;
		Emoji::Draw(
			p,
			_descriptor.emoji,
			Emoji::GetSizeLarge(),
			left,
			st::aiComposeStyleEmojiTop);
	} else if (_descriptor.icon) {
		const auto &icon = _selected
			? *_descriptor.iconActive
			: *_descriptor.icon;
		icon.paintInCenter(
			p,
			QRect(0, 0, width(), st::aiComposeStyleLabelTop));
	}

	p.setPen(_selected
		? st::aiComposeStyleLabelFgActive
		: st::aiComposeStyleLabelFg);
	p.setFont(st::aiComposeStyleLabelFont);
	p.drawText(
		QRect(
			0,
			st::aiComposeStyleLabelTop,
			width(),
			height() - st::aiComposeStyleLabelTop),
		Qt::AlignHCenter | Qt::AlignTop,
		_descriptor.label);
}

QImage LabeledEmojiTabs::Button::prepareRippleMask() const {
	return RippleAnimation::MaskByDrawer(size(), false, [&](QPainter &p) {
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		const auto radius = TabsRadius();
		p.drawRoundedRect(rect(), radius, radius);
	});
}

void LabeledEmojiTabs::Button::setContextMenuCallback(
		Fn<void(QPoint)> callback) {
	_contextMenuCallback = std::move(callback);
}

void LabeledEmojiTabs::Button::contextMenuEvent(QContextMenuEvent *e) {
	if (_contextMenuCallback) {
		_contextMenuCallback(e->globalPos());
	}
}

LabeledEmojiScrollTabs::DragScroll::DragScroll(
	not_null<QObject*> parent,
	not_null<ScrollArea*> scroll,
	Fn<void()> beforeScroll)
: QObject(parent)
, _scroll(scroll)
, _beforeScroll(std::move(beforeScroll)) {
}

void LabeledEmojiScrollTabs::DragScroll::add(
		not_null<LabeledEmojiTabs::Button*> button) {
	button->installEventFilter(this);
}

bool LabeledEmojiScrollTabs::DragScroll::eventFilter(
		QObject *watched,
		QEvent *event) {
	const auto button = dynamic_cast<LabeledEmojiTabs::Button*>(watched);
	if (!button) {
		return QObject::eventFilter(watched, event);
	}
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		const auto mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() != Qt::LeftButton) {
			break;
		}
		_pressed = button;
		_pressGlobal = mouse->globalPos();
		_scrollStart = _scroll->scrollLeft();
		_dragging = false;
		break;
	}
	case QEvent::MouseMove: {
		const auto mouse = static_cast<QMouseEvent*>(event);
		if (_pressed != button || !(mouse->buttons() & Qt::LeftButton)) {
			break;
		}
		const auto delta = mouse->globalPos() - _pressGlobal;
		if (!_dragging) {
			if (!_scroll->scrollLeftMax()) {
				break;
			}
			if (std::abs(delta.x()) < QApplication::startDragDistance()) {
				break;
			}
			if (std::abs(delta.x()) <= std::abs(delta.y())) {
				break;
			}
			_dragging = true;
			button->setSynteticOver(false);
		}
		if (_beforeScroll) {
			_beforeScroll();
		}
		button->setSynteticOver(false);
		_scroll->scrollToX(_scrollStart - delta.x());
		return true;
	}
	case QEvent::MouseButtonRelease: {
		const auto mouse = static_cast<QMouseEvent*>(event);
		if (_pressed != button || mouse->button() != Qt::LeftButton) {
			break;
		}
		const auto dragging = std::exchange(_dragging, false);
		_pressed = nullptr;
		if (dragging) {
			button->setSynteticOver(false);
		}
		break;
	}
	case QEvent::Hide:
		if (_pressed == button) {
			_pressed = nullptr;
			_dragging = false;
		}
		break;
	default:
		break;
	}
	return QObject::eventFilter(watched, event);
}

LabeledEmojiTabs::LabeledEmojiTabs(
	QWidget *parent,
	std::vector<LabeledEmojiTab> descriptors,
	Text::CustomEmojiFactory factory)
: RpWidget(parent) {
	_buttons.reserve(descriptors.size());
	for (auto &descriptor : descriptors) {
		const auto button = CreateChild<Button>(
			this,
			std::move(descriptor),
			factory);
		button->setClickedCallback([=] {
			const auto i = ranges::find(_buttons, not_null(button));
			const auto index = int(i - begin(_buttons));
			if (index == _buttons.size()) {
				return;
			}
			setActive(index);
			_requestShown.fire({
				_buttons[index]->x(),
				_buttons[index]->x() + _buttons[index]->width(),
			});
			if (_changed) {
				_changed(index);
			}
		});
		_buttons.push_back(button);
	}
	setNaturalWidth([&] {
		const auto padding = st::aiComposeStyleTabsPadding;
		const auto skip = st::aiComposeStyleTabsSkip;
		auto total = padding.left();
		for (const auto &button : _buttons) {
			total += button->naturalWidth() + skip;
		}
		return total - (_buttons.empty() ? 0 : skip) + padding.right();
	}());
	setActive(-1);
}

void LabeledEmojiTabs::setChangedCallback(Fn<void(int)> callback) {
	_changed = std::move(callback);
}

void LabeledEmojiTabs::setContextMenuCallback(
		Fn<void(int, QPoint)> callback) {
	_contextMenu = std::move(callback);
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		_buttons[i]->setContextMenuCallback([=](QPoint globalPos) {
			if (_contextMenu) {
				_contextMenu(i, globalPos);
			}
		});
	}
}

void LabeledEmojiTabs::setActive(int index) {
	if (index < -1 || index >= int(_buttons.size())) {
		return;
	}
	_active = index;
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		_buttons[i]->setSelected(i == index);
	}
}

void LabeledEmojiTabs::resizeForOuterWidth(int outerWidth) {
	const auto count = int(_buttons.size());
	const auto padding = st::aiComposeStyleTabsPadding;
	const auto skip = st::aiComposeStyleTabsSkip;
	const auto buttonHeight = st::aiComposeStyleTabsHeight
		- padding.top()
		- padding.bottom();
	const auto height = st::aiComposeStyleTabsHeight;
	auto left = padding.left();
	const auto guard = gsl::finally([&] {
		resize(left - (count ? skip : 0) + padding.right(), height);
	});
	if (!count) {
		return;
	}
	const auto setExtraPaddingFor = [&](not_null<Button*> button, int value) {
		button->setExtraPadding(value);
		const auto width = button->width();
		button->setGeometry(left, padding.top(), width, buttonHeight);
		left += width + skip;
	};
	const auto diff = naturalWidth() - outerWidth;
	if (diff > 0) {
		auto total = left;
		for (auto fit = 0; fit != count;) {
			const auto width = _buttons[fit]->naturalWidth();
			const auto tooLarge = (total + (width / 2) > outerWidth);
			if (!tooLarge) {
				++fit;
				total += width + skip;
			}
			if (tooLarge || (fit == count)) {
				if (fit > 0) {
					const auto width = _buttons[fit - 1]->naturalWidth();
					const auto desired = total - skip - (width / 2);
					const auto add = outerWidth - desired;
					const auto extra = add / ((fit - 1) * 2 + 1);
					for (const auto &button : _buttons) {
						setExtraPaddingFor(button, extra);
					}
				} else {
					for (const auto &button : _buttons) {
						setExtraPaddingFor(button, 0);
					}
				}
				return;
			}
		}
		Unexpected("Tabs width inconsistency.");
	} else {
		const auto add = -diff / 2;
		const auto each = add / _buttons.size();
		const auto more = add - (each * _buttons.size());
		for (auto i = 0; i < more; ++i) {
			setExtraPaddingFor(_buttons[i], each + 1);
		}
		for (auto i = more; i < count; ++i) {
			setExtraPaddingFor(_buttons[i], each);
		}
	}
}

QString LabeledEmojiTabs::currentId() const {
	return (_active >= 0 && _active < int(_buttons.size()))
		? _buttons[_active]->id()
		: QString();
}

int LabeledEmojiTabs::buttonCount() const {
	return int(_buttons.size());
}

rpl::producer<ScrollToRequest> LabeledEmojiTabs::requestShown() const {
	return _requestShown.events();
}

void LabeledEmojiTabs::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::aiComposeStyleTabsBg);
	const auto radius = TabsRadius();
	p.drawRoundedRect(rect(), radius, radius);
}

LabeledEmojiScrollTabs::LabeledEmojiScrollTabs(
	QWidget *parent,
	std::vector<LabeledEmojiTab> descriptors,
	Text::CustomEmojiFactory factory)
: RpWidget(parent)
, _scroll(CreateChild<ScrollArea>(this, st::aiComposeStyleTabsScroll))
, _inner(_scroll->setOwnedWidget(
	object_ptr<LabeledEmojiTabs>(
		this,
		std::move(descriptors),
		std::move(factory))))
, _fadeLeft(CreateChild<RpWidget>(this))
, _fadeRight(CreateChild<RpWidget>(this))
, _cornerLeft(CreateChild<RpWidget>(this))
, _cornerRight(CreateChild<RpWidget>(this))
, _dragScroll(std::make_unique<DragScroll>(
	this,
	_scroll,
	[=] { _scrollAnimation.stop(); })) {
	for (const auto &button : _inner->_buttons) {
		_dragScroll->add(button);
	}
	_scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto pixelDelta = e->pixelDelta();
		const auto angleDelta = e->angleDelta();
		if (std::abs(pixelDelta.x()) + std::abs(angleDelta.x())) {
			return false;
		}
		const auto y = pixelDelta.y() ? pixelDelta.y() : angleDelta.y();
		_scrollAnimation.stop();
		_scroll->scrollToX(_scroll->scrollLeft() - y);
		return true;
	});

	const auto setupFade = [&](not_null<RpWidget*> fade, bool left) {
		fade->setAttribute(Qt::WA_TransparentForMouseEvents);
		fade->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(fade);
			const auto width = fade->width();
			const auto bg = st::aiComposeStyleTabsBg->c;
			auto transparent = bg;
			transparent.setAlpha(0);
			auto gradient = QLinearGradient(0, 0, width, 0);
			if (left) {
				gradient.setColorAt(0., bg);
				gradient.setColorAt(1., transparent);
			} else {
				gradient.setColorAt(0., transparent);
				gradient.setColorAt(1., bg);
			}
			p.fillRect(fade->rect(), gradient);
		}, fade->lifetime());
		fade->hide();
	};
	setupFade(_fadeLeft, true);
	setupFade(_fadeRight, false);

	const auto setupCorner = [&](not_null<RpWidget*> corner, bool left) {
		corner->setAttribute(Qt::WA_TransparentForMouseEvents);
		corner->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(corner);
			PainterHighQualityEnabler hq(p);
			const auto width = corner->width();
			const auto height = corner->height();
			auto mask = QPainterPath();
			mask.addRect(0, 0, width, height);
			auto rounded = QPainterPath();
			if (left) {
				rounded.addRoundedRect(0, 0, width * 2, height, width, width);
			} else {
				rounded.addRoundedRect(-width, 0, width * 2, height, width, width);
			}
			p.setPen(Qt::NoPen);
			p.setBrush(st::boxDividerBg);
			p.drawPath(mask.subtracted(rounded));
		}, corner->lifetime());
	};
	setupCorner(_cornerLeft, true);
	setupCorner(_cornerRight, false);

	_scroll->scrolls() | rpl::on_next([=] {
		updateFades();
	}, lifetime());

	rpl::combine(
		widthValue(),
		_scroll->widthValue(),
		_inner->widthValue()
	) | rpl::on_next([=] {
		updateFades();
	}, lifetime());

	_inner->requestShown() | rpl::on_next([=](ScrollToRequest request) {
		scrollToButton(request.ymin, request.ymax, true);
	}, lifetime());
}

LabeledEmojiScrollTabs::~LabeledEmojiScrollTabs() = default;

void LabeledEmojiScrollTabs::setChangedCallback(Fn<void(int)> callback) {
	_inner->setChangedCallback(std::move(callback));
}

void LabeledEmojiScrollTabs::setContextMenuCallback(
		Fn<void(int, QPoint)> callback) {
	_inner->setContextMenuCallback(std::move(callback));
}

void LabeledEmojiScrollTabs::setActive(int index) {
	_inner->setActive(index);
}

void LabeledEmojiScrollTabs::setPaintOuterCorners(bool paint) {
	if (_paintOuterCorners == paint) {
		return;
	}
	_paintOuterCorners = paint;
	_cornerLeft->setVisible(paint);
	_cornerRight->setVisible(paint);
}

void LabeledEmojiScrollTabs::scrollToActive() {
	const auto index = _inner->_active;
	if (index < 0 || index >= int(_inner->_buttons.size())) {
		_scrollToActivePending = false;
		_pendingScrollLeft.reset();
		return;
	}
	const auto button = _inner->_buttons[index];
	if (_scroll->width() <= 0 || button->width() <= 0) {
		_scrollToActivePending = true;
		return;
	}
	_scrollToActivePending = false;
	_pendingScrollLeft.reset();
	scrollToButton(button->x(), button->x() + button->width(), false);
}

int LabeledEmojiScrollTabs::scrollLeft() const {
	return _scroll->scrollLeft();
}

void LabeledEmojiScrollTabs::setScrollLeft(int value) {
	if (_scroll->width() <= 0) {
		_pendingScrollLeft = value;
		return;
	}
	_pendingScrollLeft.reset();
	_scroll->scrollToX(std::max(0, value));
}

QString LabeledEmojiScrollTabs::currentId() const {
	return _inner->currentId();
}

int LabeledEmojiScrollTabs::buttonCount() const {
	return _inner->buttonCount();
}

rpl::producer<ScrollToRequest> LabeledEmojiScrollTabs::requestShown() const {
	return _inner->requestShown();
}

int LabeledEmojiScrollTabs::resizeGetHeight(int newWidth) {
	_scroll->setGeometry(0, 0, newWidth, st::aiComposeStyleTabsHeight);
	_inner->resizeForOuterWidth(newWidth);

	const auto fadeWidth = st::aiComposeStyleFadeWidth;
	const auto fadeHeight = st::aiComposeStyleTabsHeight;
	_fadeLeft->setGeometry(0, 0, fadeWidth, fadeHeight);
	_fadeRight->setGeometry(newWidth - fadeWidth, 0, fadeWidth, fadeHeight);
	_fadeLeft->raise();
	_fadeRight->raise();

	const auto radius = st::aiComposeStyleTabsRadius;
	if (_paintOuterCorners) {
		_cornerLeft->setGeometry(0, 0, radius, fadeHeight);
		_cornerRight->setGeometry(newWidth - radius, 0, radius, fadeHeight);
		_cornerLeft->raise();
		_cornerRight->raise();
		_cornerLeft->show();
		_cornerRight->show();
	} else {
		_cornerLeft->hide();
		_cornerRight->hide();
	}
	if (_scrollToActivePending) {
		scrollToActive();
	} else if (_pendingScrollLeft) {
		setScrollLeft(*_pendingScrollLeft);
	}

	updateFades();
	return st::aiComposeStyleTabsHeight;
}

void LabeledEmojiScrollTabs::updateFades() {
	const auto scrollLeft = _scroll->scrollLeft();
	const auto scrollMax = _scroll->scrollLeftMax();
	_fadeLeft->setVisible(scrollLeft > 0);
	_fadeRight->setVisible(scrollLeft < scrollMax);
}

void LabeledEmojiScrollTabs::scrollToButton(
		int buttonLeft,
		int buttonRight,
		bool animated) {
	const auto full = _scroll->width();
	const auto tab = buttonRight - buttonLeft;
	if (tab < full) {
		const auto add = std::min(full - tab, tab) / 2;
		buttonRight += add;
		buttonLeft -= add;
	}
	const auto scrollLeft = _scroll->scrollLeft();
	const auto needed = (buttonLeft < scrollLeft)
		|| (buttonRight > scrollLeft + full);
	if (!needed) {
		return;
	}
	const auto target = (buttonLeft < scrollLeft)
		? buttonLeft
		: std::min(buttonLeft, buttonRight - full);
	_scrollAnimation.stop();
	if (!animated) {
		_scroll->scrollToX(target);
		return;
	}
	_scrollAnimation.start([=] {
		_scroll->scrollToX(qRound(_scrollAnimation.value(target)));
	}, scrollLeft, target, st::slideDuration, anim::sineInOut);
}

} // namespace Ui
