/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/color_picker.h"

#include "base/basic_types.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/abstract_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/color_editor.h"
#include "styles/style_editor.h"

#include <QConicalGradient>


namespace Editor {
namespace {

constexpr auto kMinBrushSize = 0.1;
constexpr auto kMinBrushWidth = 1.;
constexpr auto kMaxBrushWidth = 25.;

constexpr auto kCircleDuration = crl::time(200);
constexpr auto kSizeControlSwitchDuration = crl::time(140);
constexpr auto kColorButtonSwitchDuration = crl::time(140);

inline float64 InterpolateF(float64 a, float64 b, float64 b_ratio) {
	return a + float64(b - a) * b_ratio;
};

inline float64 InterpolationRatio(int from, int to, int result) {
	return (result - from) / float64(to - from);
};

[[nodiscard]] int ToolIndex(Brush::Tool tool) {
	switch (tool) {
	case Brush::Tool::Pen: return 0;
	case Brush::Tool::Arrow: return 1;
	case Brush::Tool::Marker: return 2;
	case Brush::Tool::Blur: return 3;
	case Brush::Tool::Eraser: return 4;
	}
	return 0;
}

[[nodiscard]] Brush::Tool ToolFromIndex(int index) {
	switch (index) {
	case 0: return Brush::Tool::Pen;
	case 1: return Brush::Tool::Arrow;
	case 2: return Brush::Tool::Marker;
	case 3: return Brush::Tool::Blur;
	case 4: return Brush::Tool::Eraser;
	}
	return Brush::Tool::Pen;
}

[[nodiscard]] bool FixedColorTool(Brush::Tool tool) {
	return (tool == Brush::Tool::Eraser) || (tool == Brush::Tool::Blur);
}

[[nodiscard]] QColor FixedToolColor() {
	return QColor(0, 0, 0);
}

void NormalizeBrushColor(Brush &brush) {
	if (FixedColorTool(brush.tool)) {
		brush.color = FixedToolColor();
	}
}

class PlusCircle final : public Ui::AbstractButton {
public:
	using Ui::AbstractButton::AbstractButton;

private:
	void paintEvent(QPaintEvent *event) override {
		auto p = QPainter(this);
		PainterHighQualityEnabler hq(p);

		const auto border = st::photoEditorColorButtonBorder;
		const auto half = border / 2.;
		const auto rect = QRectF(QWidget::rect())
			.adjusted(half, half, -half, -half);

		p.setPen(border > 0
			? QPen(st::photoEditorColorButtonBorderFg, border)
			: Qt::NoPen);
		p.setBrush(Qt::NoBrush);

		const auto lineWidth = st::photoEditorColorPalettePlusLine;
		auto pen = QPen(st::photoEditorColorPalettePlusFg, lineWidth);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);

		const auto c = rect::center(rect);
		const auto r = rect.width() / 2. - lineWidth * 1.75;
		p.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
		p.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
	}
};

class ColorButton final : public Ui::AbstractButton {
public:
	ColorButton(
		not_null<QWidget*> parent,
		Fn<QColor()> color)
	: AbstractButton(parent)
	, _color(std::move(color)) {
	}

private:
	void paintEvent(QPaintEvent *event) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);

		const auto size = std::min(width(), height());
		if (size <= 0) {
			return;
		}

		const auto left = (width() - size) / 2.;
		const auto top = (height() - size) / 2.;
		const auto outer = QRectF(left, top, size, size);
		const auto ringWidth = float64(std::max(
			st::photoEditorColorButtonBorder,
			st::photoEditorColorPaletteSelectionWidth));
		const auto ringHalf = ringWidth / 2.;
		const auto ringRect = outer.adjusted(
			ringHalf,
			ringHalf,
			-ringHalf,
			-ringHalf);

		auto gradient = QConicalGradient(outer.center(), 15.);
		gradient.setColorAt(0. / 7., QColor(0xEB, 0x4B, 0x4B));
		gradient.setColorAt(1. / 7., QColor(0xFF, 0xA5, 0x00));
		gradient.setColorAt(2. / 7., QColor(0xFF, 0xFF, 0x00));
		gradient.setColorAt(3. / 7., QColor(0x8F, 0xCE, 0x00));
		gradient.setColorAt(4. / 7., QColor(0x00, 0xFF, 0xFF));
		gradient.setColorAt(5. / 7., QColor(0x60, 0x80, 0xE4));
		gradient.setColorAt(6. / 7., QColor(0xEE, 0x82, 0xEE));
		gradient.setColorAt(7. / 7., QColor(0xEB, 0x4B, 0x4B));

		auto pen = QPen(QBrush(gradient), ringWidth);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(ringRect);

		const auto innerInset = ringWidth
			+ st::photoEditorColorButtonBorder * 1.5;
		if (outer.width() <= innerInset * 2.
			|| outer.height() <= innerInset * 2.) {
			return;
		}
		auto innerRect = outer.adjusted(
			innerInset,
			innerInset,
			-innerInset,
			-innerInset);
		const auto innerBorder = float64(st::photoEditorColorButtonBorder);
		if (innerBorder > 0.) {
			const auto innerHalf = innerBorder / 2.;
			innerRect = innerRect.adjusted(
				innerHalf,
				innerHalf,
				-innerHalf,
				-innerHalf);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(_color());
		p.drawEllipse(innerRect);
	}

	Fn<QColor()> _color;

};

class ToolLottieButton final : public Ui::AbstractButton {
public:
	ToolLottieButton(
		not_null<QWidget*> parent,
		const QString &path)
	: AbstractButton(parent)
	, _icon(Lottie::MakeIcon({
		.path = path,
		.sizeOverride = Size(st::photoEditorToolButtonIconSize),
		.frame = 0,
	})) {
		events(
		) | rpl::on_next([=](not_null<QEvent*> event) {
			const auto type = event->type();
			if (type == QEvent::Enter) {
				if (_hovered) {
					return;
				}
				_hovered = true;
				_resetPending = false;
				if (_icon && _icon->valid() && !_icon->animating()) {
					if (_icon->frameIndex() != 0) {
						_icon->jumpTo(0, [=] { update(); });
					}
				}
				playOnce();
			} else if (type == QEvent::Leave) {
				_hovered = false;
				if (_icon && _icon->animating()) {
					_resetPending = true;
				} else {
					reset();
				}
			}
		}, lifetime());
	}

private:
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		if (_icon) {
			_icon->paintInCenter(p, rect());
			if (_resetPending && !_icon->animating()) {
				_resetPending = false;
				reset();
			}
		}
	}

	void playOnce() {
		if (!_icon || !_icon->valid()) {
			return;
		}
		const auto count = _icon->framesCount();
		if (count <= 0) {
			return;
		}
		_icon->animate(
			[=] { update(); },
			0,
			count - 1,
			crl::time(st::photoEditorToolButtonHoverDuration));
	}

	void reset() {
		if (!_icon || !_icon->valid()) {
			return;
		}
		if (_icon->frameIndex() != 0) {
			_icon->jumpTo(0, [=] { update(); });
		}
	}

	std::unique_ptr<Lottie::Icon> _icon;
	bool _hovered = false;
	bool _resetPending = false;
};

std::vector<QColor> PaletteColors() {
	return {
		QColor(0, 0, 0),
		QColor(255, 255, 255),
		QColor(234, 39, 57),
		QColor(252, 150, 77),
		QColor(252, 222, 101),
		QColor(128, 200, 100),
		QColor(73, 197, 237),
		QColor(48, 81, 227),
		QColor(219, 58, 210),
		QColor(255, 114, 169),
	};
}

} // namespace

ColorPicker::ColorPicker(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Ui::Show> show,
	const std::array<Brush, 5> &savedBrushes,
	Brush::Tool savedTool)
: _parent(parent)
, _show(std::move(show))
, _colorButton(base::make_unique_q<ColorButton>(parent, [=] {
	return colorButtonColor();
}))
, _paletteWrap(std::in_place, parent)
, _sizeControlHoverArea(std::in_place, parent)
, _sizeControl(std::in_place, parent)
, _toolSelection(std::in_place, parent)
, _brush(savedBrushes[ToolIndex(savedTool)])
, _toolBrushes(savedBrushes) {
	_colorButton->resize(Size(st::photoEditorColorButtonSize));

	for (auto i = 0; i != int(_toolBrushes.size()); ++i) {
		_toolBrushes[i].tool = ToolFromIndex(i);
		NormalizeBrushColor(_toolBrushes[i]);
	}
	_brush = _toolBrushes[ToolIndex(savedTool)];
	_brush.tool = savedTool;
	NormalizeBrushColor(_brush);
	_colorButtonFrom = _brush.color;
	_colorButtonTo = _brush.color;

	_toolSelection->setAttribute(Qt::WA_TransparentForMouseEvents);
	_toolSelection->setAttribute(Qt::WA_TranslucentBackground, true);
	_toolSelection->setVisible(false);
	_toolSelection->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(st::photoEditorToolButtonSelectedOpacity);
		auto pen = QPen(
			st::photoEditorToolButtonSelectedFg,
			st::photoEditorToolButtonSelectedWidth);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(Qt::NoPen);
		p.setBrush(st::photoEditorToolButtonSelectedFg);
		const auto padding = st::photoEditorToolButtonSelectedPadding;
		const auto half = pen.widthF() / 2.;
		const auto rect = QRectF(_toolSelection->rect()).adjusted(
			padding + half,
			padding + half,
			-padding - half,
			-padding - half);
		p.drawEllipse(rect);
	});

	_toolButtons.push_back(base::make_unique_q<ToolLottieButton>(
		parent,
		u":/animations/photo_editor_pen.tgs"_q));
	_toolButtons.push_back(base::make_unique_q<ToolLottieButton>(
		parent,
		u":/animations/photo_editor_arrow.tgs"_q));
	_toolButtons.push_back(base::make_unique_q<ToolLottieButton>(
		parent,
		u":/animations/photo_editor_marker.tgs"_q));
	_toolButtons.push_back(base::make_unique_q<ToolLottieButton>(
		parent,
		u":/animations/photo_editor_blur.tgs"_q));
	_toolButtons.push_back(base::make_unique_q<ToolLottieButton>(
		parent,
		u":/animations/photo_editor_eraser.tgs"_q));
	for (const auto &button : _toolButtons) {
		button->resize(
			st::photoEditorToolButtonSize,
			st::photoEditorToolButtonSize);
		button->show();
	}
	const auto setToolRequest = [=](Brush::Tool tool) {
		_toolClicks.fire({});
		setTool(tool);
	};
	if (_toolButtons.size() >= 5) {
		_toolButtons[0]->setClickedCallback([=] {
			setToolRequest(Brush::Tool::Pen);
		});
		_toolButtons[1]->setClickedCallback([=] {
			setToolRequest(Brush::Tool::Arrow);
		});
		_toolButtons[2]->setClickedCallback([=] {
			setToolRequest(Brush::Tool::Marker);
		});
		_toolButtons[3]->setClickedCallback([=] {
			setToolRequest(Brush::Tool::Blur);
		});
		_toolButtons[4]->setClickedCallback([=] {
			setToolRequest(Brush::Tool::Eraser);
		});
	}
	updateToolSelection(false);
	_paletteWrap->setVisible(false);
	_sizeControl->resize(
		st::photoEditorBrushSizeControlHitPadding * 2
			+ st::photoEditorBrushSizeControlExpandShift
			+ st::photoEditorBrushSizeControlExpandedTopWidth,
		st::photoEditorBrushSizeControlHeight);
	_sizeControlHoverArea->setMouseTracking(true);
	_sizeControl->setMouseTracking(true);

	updateSizeControlPositionFromRatio(false);
	moveSizeControl(_parent->size());

	_colorButton->setClickedCallback([=] {
		setPaletteVisible(!_paletteVisible);
	});

	_sizeControl->paintOn([=](QPainter &p) {
		paintSizeControl(p);
	});

	_parent->sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		moveSizeControl(size);
		if (_paletteVisible) {
			rebuildPalette();
		}
	}, _sizeControl->lifetime());

	_sizeControlHoverArea->events(
	) | rpl::on_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Enter) {
			_sizeHoverAreaHovered = true;
			updateSizeControlExpanded();
		} else if (type == QEvent::Leave) {
			_sizeHoverAreaHovered = false;
			updateSizeControlExpanded();
		}
	}, _sizeControlHoverArea->lifetime());

	_sizeControl->events(
	) | rpl::on_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Enter) {
			_sizeControlHovered = true;
			updateSizeControlExpanded();
			return;
		} else if (type == QEvent::Leave) {
			_sizeControlHovered = false;
			updateSizeControlExpanded();
			return;
		}
		const auto isPress = (type == QEvent::MouseButtonPress)
			|| (type == QEvent::MouseButtonDblClick);
		const auto isMove = (type == QEvent::MouseMove);
		const auto isRelease = (type == QEvent::MouseButtonRelease);
		if (!isPress && !isMove && !isRelease) {
			return;
		}

		const auto e = static_cast<QMouseEvent*>(event.get());
		if (isPress) {
			if (e->button() != Qt::LeftButton) {
				return;
			}
			const auto progress = _sizeControlAnimation.value(
				_sizeControlExpanded ? 1. : 0.);
			const auto inHandle = sizeControlHandleRect(progress).contains(
				e->pos());
			const auto inControl = sizeControlHitRect(progress).contains(
				e->pos());
			if (!inHandle && !inControl) {
				return;
			}
			_sizeControlPositionAnimation.stop();
			_sizeDown.pressed = true;
			updateSizeControlMousePosition(e->pos().y());
			updateSizeControlExpanded();
			_sizeControl->update();
			return;
		}
		if (!_sizeDown.pressed) {
			return;
		}
		if (isMove) {
			updateSizeControlMousePosition(e->pos().y());
			_sizeControl->update();
			return;
		}
		if (isRelease && (e->button() == Qt::LeftButton)) {
			updateSizeControlMousePosition(e->pos().y());
			_sizeDown.pressed = false;
			updateSizeControlExpanded();
			_saveBrushRequests.fire_copy(_brush);
			_sizeControl->update();
		}
	}, _sizeControl->lifetime());

	rebuildPalette();
}

void ColorPicker::moveLine(const QPoint &position) {
	_colorButtonCenter = position;
	const auto gap = st::photoEditorToolButtonGap;
	const auto colorWidth = _colorButton->width();
	const auto colorHeight = _colorButton->height();
	const auto toolSize = st::photoEditorToolButtonSize;
	const auto tools = int(_toolButtons.size());
	const auto totalWidth = colorWidth + tools * toolSize + tools * gap;
	const auto left = position.x() - totalWidth / 2;
	const auto top = position.y() - colorHeight / 2;
	_colorButton->move(left, top);
	updateToolButtonsGeometry();
	updateToolSelection(false);
	updatePaletteGeometry();
}

void ColorPicker::setCanvasRect(const QRect &rect) {
	_canvasRect = rect;
	moveSizeControl(_parent->size());
}

void ColorPicker::updateToolButtonsGeometry() {
	const auto size = st::photoEditorToolButtonSize;
	const auto extra = st::photoEditorToolButtonSelectedExtra;
	const auto hit = size + extra * 2;
	const auto gap = st::photoEditorToolButtonGap;
	auto x = _colorButton->x() + _colorButton->width() + gap;
	const auto y = _colorButton->y()
		+ (_colorButton->height() - size) / 2;
	for (const auto &button : _toolButtons) {
		button->resize(hit, hit);
		button->move(x - extra, y - extra);
		x += size + gap;
	}
}

void ColorPicker::updateToolSelection(bool animated) {
	if (_toolButtons.size() < 5) {
		return;
	}
	const auto index = ToolIndex(_brush.tool);
	if (index < 0 || index >= int(_toolButtons.size())) {
		return;
	}
	const auto &button = _toolButtons[index];
	const auto target = button->pos();
	_toolSelection->resize(button->size());
	_toolSelection->raise();
	if (!animated || !_toolSelection->isVisible()) {
		_toolSelectionAnimation.stop();
		_toolSelection->move(target);
		_toolSelection->update();
		return;
	}
	_toolSelectionFrom = _toolSelection->pos();
	_toolSelectionTo = target;
	_toolSelectionAnimation.stop();
	_toolSelectionAnimation.start([=] {
		const auto progress = _toolSelectionAnimation.value(1.);
		_toolSelection->move(
			anim::interpolate(
				_toolSelectionFrom.x(),
				_toolSelectionTo.x(),
				progress),
			anim::interpolate(
				_toolSelectionFrom.y(),
				_toolSelectionTo.y(),
				progress));
		_toolSelection->update();
	}, 0., 1., crl::time(st::photoEditorToolButtonSelectDuration),
		anim::easeOutCirc);
}

void ColorPicker::setTool(Brush::Tool tool) {
	if (_brush.tool == tool) {
		return;
	}
	storeCurrentBrush();
	_brush = _toolBrushes[ToolIndex(tool)];
	_brush.tool = tool;
	NormalizeBrushColor(_brush);
	updateSizeControlPositionFromRatio(true);
	updateColorButtonColor(_brush.color, true);
	if (_paletteVisible) {
		rebuildPalette();
	} else {
		_colorButton->update();
	}
	updateToolSelection(true);
	_sizeControl->update();
	_saveBrushRequests.fire_copy(_brush);
}

void ColorPicker::storeCurrentBrush() {
	if (_toolSelectionSuppressed) {
		return;
	}
	NormalizeBrushColor(_brush);
	_toolBrushes[ToolIndex(_brush.tool)] = _brush;
}

void ColorPicker::setColor(const QColor &color) {
	_brush.color = color;
	updateColorButtonColor(color, true);
	if (_paletteVisible) {
		rebuildPalette();
	} else {
		_colorButton->update();
	}
}

void ColorPicker::setToolSelectionVisible(bool visible) {
	_toolSelectionSuppressed = !visible;
	_toolSelection->setVisible(visible);
}

void ColorPicker::updateColorButtonColor(const QColor &color, bool animated) {
	const auto hasValid = _colorButtonFrom.isValid() && _colorButtonTo.isValid();
	const auto from = hasValid ? colorButtonColor() : color;
	const auto to = color.isValid() ? color : from;
	if (from == to) {
		_colorButtonAnimation.stop();
		_colorButtonFrom = from;
		_colorButtonTo = to;
		return;
	}
	_colorButtonFrom = from;
	_colorButtonTo = to;
	_colorButtonAnimation.stop();
	if (animated) {
		_colorButtonAnimation.start(
			[=] { _colorButton->update(); },
			0.,
			1.,
			kColorButtonSwitchDuration,
			anim::easeOutCirc);
	} else {
		_colorButton->update();
	}
}

QColor ColorPicker::colorButtonColor() const {
	const auto progress = _colorButtonAnimation.value(1.);
	return anim::color(_colorButtonFrom, _colorButtonTo, progress);
}

void ColorPicker::paintSizeControl(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);

	const auto progress = _sizeControlAnimation.value(_sizeControlExpanded
		? 1.
		: 0.);
	const auto path = sizeControlShapePath(progress);

	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, anim::interpolate(96, 176, progress)));
	p.drawPath(path);

	const auto handleRect = sizeControlHandleRect(progress);
	p.setBrush(QColor(255, 255, 255, 244));
	p.drawEllipse(handleRect);
}

void ColorPicker::setVisible(bool visible) {
	if (!visible) {
		_paletteVisible = false;
		_sizeDown.pressed = false;
		_sizeHoverAreaHovered = false;
		_sizeControlHovered = false;
		_sizeControlExpanded = false;
		_sizeControlAnimation.stop();
		_sizeControlPositionAnimation.stop();
		_toolSelectionAnimation.stop();
	}
	_colorButton->setVisible(visible && !_paletteVisible);
	_paletteWrap->setVisible(visible && _paletteVisible);
	_sizeControlHoverArea->setVisible(visible);
	_sizeControl->setVisible(visible);
	const auto showTools = visible
		&& !_paletteVisible
		&& !_toolSelectionSuppressed;
	_toolSelection->setVisible(showTools);
	for (const auto &button : _toolButtons) {
		button->setVisible(visible && !_paletteVisible);
	}
	if (showTools) {
		updateToolSelection(false);
	}
}

rpl::producer<Brush> ColorPicker::saveBrushRequests() const {
	return _saveBrushRequests.events_starting_with_copy(_brush);
}

rpl::producer<> ColorPicker::toolClicks() const {
	return _toolClicks.events();
}

bool ColorPicker::preventHandleKeyPress() const {
	return _sizeControl->isVisible()
		&& (_sizeControlAnimation.animating() || _sizeDown.pressed);
}

void ColorPicker::rebuildPalette() {
	_paletteButtons.clear();
	_palettePlus = nullptr;

	auto colors = PaletteColors();
	auto hasCurrent = false;
	for (const auto &c : colors) {
		if (c == _brush.color) {
			hasCurrent = true;
			break;
		}
	}
	if (!hasCurrent) {
		colors.push_back(_brush.color);

		const auto &padding = st::photoEditorButtonBarPadding;
		const auto size = st::photoEditorColorPaletteItemSize;
		const auto gap = st::photoEditorColorPaletteGap;
		const auto barWidth = std::min(
				st::photoEditorButtonBarWidth,
				_parent->width())
			- rect::m::sum::h(padding)
			- st::photoEditorUndoButton.width
			- st::photoEditorRedoButton.width;
		const auto count = int(colors.size());
		const auto paletteWidth = count * size
			+ (count - 1) * gap
			+ (gap + size);
		if (paletteWidth > barWidth && colors.size() > 2) {
			colors.erase(colors.end() - 2);
		}
	}

	auto index = uint8(0);
	for (const auto &c : colors) {
		auto button = base::make_unique_q<Ui::ColorSample>(
			_paletteWrap,
			[c](uint8) {
				auto set = Data::ColorProfileSet();
				set.palette = { c };
				return set;
			},
			index++,
			c == _brush.color);
		button->setSelectionCutout(true);
		button->setClickedCallback([=] {
			_brush.color = c;
			storeCurrentBrush();
			updateColorButtonColor(_brush.color, true);
			rebuildPalette();
			_colorButton->update();
			_saveBrushRequests.fire_copy(_brush);
			setPaletteVisible(false);
		});
		button->show();
		_paletteButtons.push_back(std::move(button));
	}

	_palettePlus = base::make_unique_q<PlusCircle>(_paletteWrap);
	_palettePlus->setClickedCallback([=] {
		if (!_show) {
			return;
		}
		_show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
			struct State {
				QColor color;
			};
			const auto state = box->lifetime().make_state<State>();
			state->color = _brush.color;
			auto editor = box->addRow(
				object_ptr<ColorEditor>(
					box,
					ColorEditor::Mode::HSL,
					_brush.color),
				style::margins());
			box->setWidth(editor->width());
			editor->colorValue(
			) | rpl::on_next([=](QColor c) {
				state->color = c;
			}, editor->lifetime());
			box->addButton(tr::lng_box_done(), [=] {
				_brush.color = state->color;
				storeCurrentBrush();
				updateColorButtonColor(_brush.color, true);
				rebuildPalette();
				_colorButton->update();
				_saveBrushRequests.fire_copy(_brush);
				setPaletteVisible(false);
				box->closeBox();
			});
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
	});
	_palettePlus->show();
	updatePaletteGeometry();
}

void ColorPicker::updatePaletteGeometry() {
	if (!_paletteWrap) {
		return;
	}
	const auto size = st::photoEditorColorPaletteItemSize;
	const auto gap = st::photoEditorColorPaletteGap;
	const auto count = int(_paletteButtons.size());
	const auto plusSize = size;
	const auto width = (count * size)
		+ ((count > 0) ? (count - 1) * gap : 0)
		+ gap + plusSize;
	const auto height = std::max(size, plusSize);

	_paletteWrap->resize(width, height);
	auto x = 0;
	for (const auto &button : _paletteButtons) {
		button->resize(size, size);
		button->move(x, (height - size) / 2);
		x += size + gap;
	}
	if (_palettePlus) {
		_palettePlus->resize(plusSize, plusSize);
		_palettePlus->move(x, (height - plusSize) / 2);
	}

	if (_colorButtonCenter.isNull()) {
		return;
	}
	_paletteWrap->move(_colorButtonCenter - QPoint(width / 2, height / 2));
}

void ColorPicker::setPaletteVisible(bool visible) {
	if (_paletteVisible == visible) {
		return;
	}
	_paletteVisible = visible;
	_paletteWrap->setVisible(visible);
	_colorButton->setVisible(!visible);
	const auto showTools = !visible && !_toolSelectionSuppressed;
	_toolSelection->setVisible(showTools);
	for (const auto &button : _toolButtons) {
		button->setVisible(!visible);
	}
	if (visible) {
		rebuildPalette();
	} else if (showTools) {
		updateToolSelection(false);
	}
}

void ColorPicker::moveSizeControl(const QSize &size) {
	if (size.isEmpty()) {
		return;
	}
	const auto areaWidth = std::min(
		size.width(),
		st::photoEditorBrushSizeControlHitPadding);
	const auto areaTop = (_canvasRect.height() > 0)
		? _canvasRect.y()
		: (size.height() - _sizeControl->height()) / 2;
	const auto areaHeight = (_canvasRect.height() > 0)
		? _canvasRect.height()
		: _sizeControl->height();
	_sizeControlHoverArea->setGeometry(
		0,
		std::clamp(areaTop, 0, std::max(0, size.height() - areaHeight)),
		areaWidth,
		std::min(areaHeight, size.height()));

	const auto collapsedCenterX = sizeControlCurrentCenterX(0.)
		- st::photoEditorBrushSizeControlLeftSkip;
	const auto collapsedLeft = collapsedCenterX
		- (float64(st::photoEditorBrushSizeControlCollapsedWidth) / 2.);
	const auto y = (_canvasRect.height() > 0)
		? (rect::center(_canvasRect).y() - _sizeControl->height() / 2)
		: ((size.height() - _sizeControl->height()) / 2);
	const auto diff = size.height() - _sizeControl->height();
	_sizeControl->move(
		-int(base::SafeRound(collapsedLeft)),
		std::clamp(y, 0, std::max(0, diff)));
}

void ColorPicker::updateSizeControlExpanded() {
	const auto expanded = _sizeDown.pressed
		|| _sizeHoverAreaHovered
		|| _sizeControlHovered;
	if (_sizeControlExpanded == expanded
		&& !_sizeControlAnimation.animating()) {
		return;
	}
	_sizeControlExpanded = expanded;
	const auto from = _sizeControlAnimation.value(expanded ? 0. : 1.);
	const auto to = expanded ? 1. : 0.;
	_sizeControlAnimation.stop();
	_sizeControlAnimation.start(
		[=] { _sizeControl->update(); },
		from,
		to,
		kCircleDuration * std::abs(to - from),
		anim::easeOutCirc);
	_sizeControl->update();
}

void ColorPicker::updateSizeControlMousePosition(int y) {
	_sizeDown.y = std::clamp(y, sizeControlTop(), sizeControlBottom());
	_brush.sizeRatio = sizeControlRatioFromY(_sizeDown.y);
	storeCurrentBrush();
}

void ColorPicker::updateSizeControlPositionFromRatio(bool animated) {
	const auto target = sizeControlYFromRatio(_brush.sizeRatio);
	if (!animated || _sizeDown.pressed) {
		_sizeControlPositionAnimation.stop();
		_sizeDown.y = target;
		return;
	}
	if (_sizeDown.y == target) {
		return;
	}
	_sizeControlPositionFrom = _sizeDown.y;
	_sizeControlPositionTo = target;
	_sizeControlPositionAnimation.stop();
	_sizeControlPositionAnimation.start([=] {
		const auto progress = _sizeControlPositionAnimation.value(1.);
		_sizeDown.y = anim::interpolate(
			_sizeControlPositionFrom,
			_sizeControlPositionTo,
			progress);
		_sizeControl->update();
	}, 0., 1., kSizeControlSwitchDuration, anim::easeOutCirc);
	_sizeControl->update();
}

int ColorPicker::sizeControlShapeTop() const {
	return st::photoEditorBrushSizeControlHitPadding;
}

int ColorPicker::sizeControlShapeBottom() const {
	return _sizeControl->height() - st::photoEditorBrushSizeControlHitPadding;
}

int ColorPicker::sizeControlTop() const {
	return sizeControlShapeTop()
		+ (st::photoEditorBrushSizeControlExpandedTopWidth / 2);
}

int ColorPicker::sizeControlBottom() const {
	return sizeControlShapeBottom()
		- (st::photoEditorBrushSizeControlExpandedBottomWidth / 2);
}

float ColorPicker::sizeControlRatioFromY(int y) const {
	const auto top = sizeControlTop();
	const auto bottom = sizeControlBottom();
	if (bottom <= top) {
		return 1.f;
	}
	const auto ratio = 1. - InterpolationRatio(top, bottom, y);
	return std::clamp(ratio, kMinBrushSize, 1.0);
}

int ColorPicker::sizeControlYFromRatio(float ratio) const {
	const auto top = sizeControlTop();
	const auto bottom = sizeControlBottom();
	if (bottom <= top) {
		return top;
	}
	const auto normalized = std::clamp(
		(ratio - kMinBrushSize) / (1.0 - kMinBrushSize),
		0.0,
		1.0);
	return std::clamp(
		anim::interpolate(bottom, top, normalized),
		top,
		bottom);
}

QRectF ColorPicker::sizeControlHandleRect(float64 progress) const {
	const auto handleSize = sizeControlHandleSize();
	const auto centerX = sizeControlCurrentCenterX(progress);
	return QRectF(
		centerX - handleSize / 2.,
		_sizeDown.y - handleSize / 2.,
		handleSize,
		handleSize);
}

QRectF ColorPicker::sizeControlHitRect(float64 progress) const {
	const auto collapsed = st::photoEditorBrushSizeControlCollapsedWidth;
	const auto width = float64(anim::interpolate(
		collapsed,
		st::photoEditorBrushSizeControlExpandedTopWidth,
		progress));
	const auto centerX = sizeControlCurrentCenterX(progress);
	const auto top = float64(sizeControlShapeTop());
	const auto bottom = float64(sizeControlShapeBottom());
	return QRectF(
		centerX - width / 2.,
		top,
		width,
		bottom - top);
}

QPainterPath ColorPicker::sizeControlShapePath(float64 progress) const {
	const auto collapsed = st::photoEditorBrushSizeControlCollapsedWidth;
	const auto topWidth = float64(anim::interpolate(
		collapsed,
		st::photoEditorBrushSizeControlExpandedTopWidth,
		progress));
	const auto topInset = st::photoEditorBrushSizeControlTopInset;
	const auto adjustedTopWidth = std::max(
		0.,
		topWidth - float64(topInset * 2));
	const auto bottomWidth = float64(anim::interpolate(
		collapsed,
		st::photoEditorBrushSizeControlExpandedBottomWidth,
		progress));
	const auto centerX = sizeControlCurrentCenterX(progress);
	const auto top = float64(sizeControlShapeTop()) + topInset;
	const auto bottom = float64(sizeControlShapeBottom());
	const auto topRadius = adjustedTopWidth / 2.;
	const auto bottomRadius = bottomWidth / 2.;

	auto path = QPainterPath();
	const auto topRect = QRectF(
		centerX - topRadius,
		top,
		adjustedTopWidth,
		adjustedTopWidth);
	const auto bottomRect = QRectF(
		centerX - bottomRadius,
		bottom - bottomWidth,
		bottomWidth,
		bottomWidth);
	path.moveTo(centerX - topRadius, top + topRadius);
	path.arcTo(topRect, 180., -180.);
	path.lineTo(centerX + bottomRadius, bottom - bottomRadius);
	path.arcTo(bottomRect, 0., -180.);
	path.lineTo(centerX - topRadius, top + topRadius);
	path.closeSubpath();
	return path;
}

float64 ColorPicker::sizeControlCurrentCenterX(float64 progress) const {
	const auto from = float64(st::photoEditorBrushSizeControlCollapsedWidth)
		/ 2.;
	const auto to = float64(st::photoEditorBrushSizeControlExpandShift)
		+ float64(st::photoEditorBrushSizeControlExpandedTopWidth) / 2.;
	return float64(st::photoEditorBrushSizeControlHitPadding)
		+ InterpolateF(from, to, progress);
}

float64 ColorPicker::sizeControlHandleSize() const {
	const auto width = kMinBrushWidth
		+ (kMaxBrushWidth - kMinBrushWidth) * _brush.sizeRatio;
	return std::clamp(
		width,
		float64(st::photoEditorBrushSizeControlExpandedBottomWidth),
		float64(st::photoEditorBrushSizeControlExpandedTopWidth));
}

} // namespace Editor
