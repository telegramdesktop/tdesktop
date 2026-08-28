/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_controls.h"

#include "editor/controllers/controllers.h"
#include "lang/lang_keys.h"
#include "ui/effects/round_checkbox.h"
#include "ui/image/image_prepare.h"
#include "ui/qt_object_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_editor.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck

#include <QRegion>
#include <QtGui/QGuiApplication>

namespace Editor {
namespace {

[[nodiscard]] bool AnyModifierPressed() {
	return QGuiApplication::keyboardModifiers()
		& (Qt::ShiftModifier
			| Qt::ControlModifier
			| Qt::AltModifier
			| Qt::MetaModifier);
}

class CheckAction final : public Ui::Menu::ItemBase {
public:
	CheckAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		const QString &text,
		bool checked);

	void setChecked(bool checked);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;
	void paintEvent(QPaintEvent *e) override;

	const style::Menu &_st;
	Ui::CheckView _check;
	const base::unique_qptr<Ui::FlatLabel> _text;
	const not_null<QAction*> _dummyAction;

};

CheckAction::CheckAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	const QString &text,
	bool checked)
: ItemBase(parent, st)
, _st(st)
, _check(st::photoEditorMenuCheck, checked, [=] { update(); })
, _text(base::make_unique_q<Ui::FlatLabel>(
	this,
	rpl::single(text),
	st::photoEditorMenuCheckLabel))
, _dummyAction(Ui::CreateChild<QAction>(parent.get())) {
	ItemBase::enableMouseSelecting();
	setPreventClose(true);
	_text->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMinWidth(_st.widthMin);
	parent->widthValue() | rpl::on_next([=](int width) {
		const auto &padding = _st.itemPadding;
		_text->resizeToWidth(width - rect::m::sum::h(padding));
		_text->moveToLeft(padding.left(), padding.top());
		resize(width, contentHeight());
	}, lifetime());
}

void CheckAction::setChecked(bool checked) {
	_check.setChecked(checked, anim::type::normal);
}

not_null<QAction*> CheckAction::action() const {
	return _dummyAction;
}

bool CheckAction::isEnabled() const {
	return true;
}

int CheckAction::contentHeight() const {
	return rect::m::sum::v(_st.itemPadding)
		+ std::max(_text->heightNoMargins(), _check.getSize().height());
}

void CheckAction::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto selected = isSelected();
	p.fillRect(rect(), selected ? _st.itemBgOver : _st.itemBg);
	RippleButton::paintRipple(p, 0, 0);
	const auto size = _check.getSize();
	_check.paint(
		p,
		(_st.itemPadding.left() - size.width()) / 2,
		(height() - size.height()) / 2,
		width());
}

class ShapeAction final : public Ui::Menu::Action {
public:
	ShapeAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		not_null<QAction*> action,
		const style::icon *outline,
		const style::icon *fill,
		bool filled);

	void setFilled(bool filled);

private:
	void paintEvent(QPaintEvent *e) override;

	const not_null<const style::icon*> _outline;
	const style::icon *_fill = nullptr;
	QImage _fillFrame;
	Ui::Animations::Simple _filledAnimation;
	bool _filled = false;

};

ShapeAction::ShapeAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	not_null<QAction*> action,
	const style::icon *outline,
	const style::icon *fill,
	bool filled)
: Ui::Menu::Action(parent, st, action, nullptr, nullptr)
, _outline(outline)
, _fill(fill)
, _filled(filled) {
}

void ShapeAction::setFilled(bool filled) {
	if (_filled == filled) {
		return;
	}
	_filled = filled;
	if (_fill) {
		_filledAnimation.start(
			[=] { update(); },
			filled ? 0. : 1.,
			filled ? 1. : 0.,
			st::photoEditorShapeFillDuration,
			anim::linear);
	}
}

void ShapeAction::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto selected = isSelected();
	paintBackground(p, selected);
	paintRipple(p, 0, 0);
	p.setPen(selected ? st().itemFgOver : st().itemFg);
	paintText(p);

	const auto position = st().itemIconPosition;
	_outline->paint(p, position, width());
	if (!_fill) {
		return;
	}
	const auto progress = _filledAnimation.value(_filled ? 1. : 0.);
	if (progress <= 0.) {
		return;
	} else if (progress >= 1.) {
		_fill->paint(p, position, width());
		return;
	}
	const auto size = _fill->size();
	const auto ratio = style::DevicePixelRatio();
	if (_fillFrame.size() != size * ratio) {
		_fillFrame = QImage(
			size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_fillFrame.setDevicePixelRatio(ratio);
	}
	_fillFrame.fill(Qt::transparent);
	{
		auto q = QPainter(&_fillFrame);
		_fill->paint(q, QPoint(), size.width());

		auto hq = PainterHighQualityEnabler(q);
		const auto radius = st::photoEditorShapeFillRadius * progress;
		const auto inner = QRectF(QPointF(), QSizeF(size));
		auto path = QPainterPath();
		path.addRect(inner);
		path.addEllipse(inner.center(), radius, radius);
		q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		q.setPen(Qt::NoPen);
		q.setBrush(Qt::black);
		q.drawPath(path);
	}
	p.drawImage(
		style::rtlrect(QRect(position, size), width()).topLeft(),
		_fillFrame);
}

class RoundCheckAction final : public Ui::Menu::Action {
public:
	RoundCheckAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		not_null<QAction*> action,
		bool checked);

	void setChecked(bool checked);

private:
	void paintEvent(QPaintEvent *e) override;

	Ui::RoundCheckbox _check;
	Ui::Animations::Simple _checkedAnimation;
	bool _checked = false;

};

RoundCheckAction::RoundCheckAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	not_null<QAction*> action,
	bool checked)
: Ui::Menu::Action(parent, st, action, nullptr, nullptr)
, _check(st::photoEditorMenuRoundCheck, [=] { update(); })
, _checked(checked) {
	_check.setChecked(checked, anim::type::instant);
}

void RoundCheckAction::setChecked(bool checked) {
	if (_checked == checked) {
		return;
	}
	_checked = checked;
	_check.setChecked(checked, anim::type::normal);
	_checkedAnimation.start(
		[=] { update(); },
		checked ? 0. : 1.,
		checked ? 1. : 0.,
		st::photoEditorMenuRoundCheck.duration,
		anim::linear);
}

void RoundCheckAction::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto selected = isSelected();
	paintBackground(p, selected);
	paintRipple(p, 0, 0);
	p.setPen(selected ? st().itemFgOver : st().itemFg);
	paintText(p);

	const auto &check = st::photoEditorMenuRoundCheck;
	const auto left = (st().itemPadding.left() - check.size) / 2;
	const auto top = (height() - check.size) / 2;
	const auto progress = _checkedAnimation.value(_checked ? 1. : 0.);
	const auto untoggled = 1. - std::min(progress / check.bgDuration, 1.);
	if (untoggled > 0.) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::photoEditorMenuRoundCheckUntoggledFg->p;
		pen.setWidth(check.width);
		p.setOpacity(untoggled);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRect(left, top, check.size, check.size));
		p.setOpacity(1.);
	}
	_check.paint(p, left, top, width());
}

} // namespace

class EdgeButton final : public Ui::RippleButton {
public:
	EdgeButton(
		not_null<Ui::RpWidget*> parent,
		const QString &text,
		int height,
		const style::color &bg,
		const style::color &fg,
		const style::RippleAnimation &st);

protected:
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void init();

	const style::color &_fg;
	Ui::Text::String _text;
	const int _width;
	const QRect _rippleRect;
	const QColor _bg;

	QImage rounded(std::optional<QColor> color) const;

};

EdgeButton::EdgeButton(
	not_null<Ui::RpWidget*> parent,
	const QString &text,
	int height,
	const style::color &bg,
	const style::color &fg,
	const style::RippleAnimation &st)
: Ui::RippleButton(parent, st)
, _fg(fg)
, _text(st::photoEditorButtonStyle, text)
, _width(_text.maxWidth()
	+ st::photoEditorTextButtonPadding.left()
	+ st::photoEditorTextButtonPadding.right())
, _rippleRect(QRect(
	rect::m::pos::tl(st::photoEditorEdgeButtonMargins),
	QSize(
		_width,
		height - rect::m::sum::v(st::photoEditorEdgeButtonMargins))))
, _bg(bg->c) {
	resize(
		_width + rect::m::sum::h(st::photoEditorEdgeButtonMargins),
		height);
	init();
}

void EdgeButton::init() {
	const auto bg = rounded(_bg);

	paintRequest(
	) | rpl::on_next([=] {
		Painter p(this);

		if (isOver()) {
			p.drawImage(_rippleRect.topLeft(), bg);
		}

		paintRipple(p, _rippleRect.x(), _rippleRect.y());

		p.setPen(_fg);
		const auto textTop = _rippleRect.y()
			+ (_rippleRect.height() - _text.minHeight()) / 2;
		_text.draw(
			p,
			_rippleRect.x(),
			textTop,
			_rippleRect.width(),
			style::al_center);
	}, lifetime());
}

QImage EdgeButton::rounded(std::optional<QColor> color) const {
	auto result = QImage(
		_rippleRect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(color.value_or(Qt::white));

	const auto radius = std::min(_rippleRect.width(), _rippleRect.height())
		/ 2;
	const auto mask = Images::CornersMask(radius);
	return Images::Round(std::move(result), mask);
}

QImage EdgeButton::prepareRippleMask() const {
	return rounded(std::nullopt);
}

QPoint EdgeButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _rippleRect.topLeft();
}

class ButtonBar final : public Ui::RpWidget {
public:
	ButtonBar(
		not_null<Ui::RpWidget*> parent,
		const style::color &bg);

private:
	QImage _roundedBg;

};

ButtonBar::ButtonBar(
	not_null<Ui::RpWidget*> parent,
	const style::color &bg)
: RpWidget(parent) {
	sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		const auto children = RpWidget::children();
		const auto widgets = ranges::views::all(
			children
		) | ranges::views::filter([](not_null<const QObject*> object) {
			return object->isWidgetType();
		}) | ranges::views::transform([](not_null<QObject*> object) {
			return static_cast<QWidget*>(object.get());
		}) | ranges::to_vector;
		if (widgets.size() < 2) {
			return;
		}

		const auto layout = [&](bool symmetrical) {
			auto widths = widgets | ranges::views::transform(
				&QWidget::width
			) | ranges::to_vector;
			const auto count = int(widths.size());
			const auto middle = count / 2;
			if (symmetrical) {
				for (auto i = 0; i != middle; ++i) {
					const auto j = count - i - 1;
					widths[i] = widths[j] = std::max(widths[i], widths[j]);
				}
			}
			const auto residualWidth = size.width()
				- ranges::accumulate(widths, 0);
			if (symmetrical && residualWidth < 0) {
				return false;
			}
			const auto step = residualWidth / float(count - 1);

			auto left = 0.;
			auto &&ints = ranges::views::ints(0, ranges::unreachable);
			auto &&list = ranges::views::zip(widgets, widths, ints);
			for (const auto &[widget, width, index] : list) {
				widget->move(int((index >= middle)
					? (left + width - widget->width())
					: left), 0);
				left += width + step;
			}
			return true;
		};
		if (!layout(true)) {
			layout(false);
		}

		auto result = QImage(
			size * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(bg->c);

		const auto radius = std::min(size.width(), size.height()) / 2;
		const auto mask = Images::CornersMask(radius);
		_roundedBg = Images::Round(std::move(result), mask);
	}, lifetime());

	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);
		p.drawImage(QPoint(), _roundedBg);
	}, lifetime());
}

class TextToolButton final : public Ui::AbstractButton {
public:
	TextToolButton(not_null<QWidget*> parent)
	: AbstractButton(parent) {
		resize(
			st::photoEditorTextButtonSize.width(),
			st::photoEditorTextButtonSize.height());
		events(
		) | rpl::on_next([=](not_null<QEvent*> event) {
			if (event->type() == QEvent::Enter
				|| event->type() == QEvent::Leave) {
				update();
			}
		}, lifetime());
	}

private:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		auto font = st::semiboldFont->f;
		font.setPixelSize(QWidget::rect().height() / 2);
		p.setFont(font);
		p.setPen(isOver()
			? st::photoEditorButtonIconFgOver
			: st::photoEditorButtonIconFg);
		p.translate(0, st::photoEditorTextButtonGlyphSkip);
		p.drawText(QWidget::rect(), style::al_center, u"A"_q);
	}
};

PhotoEditorControls::PhotoEditorControls(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Controllers> controllers,
	const PhotoModifications modifications,
	const EditorData &data,
	const QSize &imageSize,
	bool shapesFilled)
: RpWidget(parent)
, _imageSize(imageSize)
, _originalRatio(data.originalRatio)
, _bg(st::roundedBg)
, _buttonHeight(st::photoEditorButtonBarHeight)
, _transformButtons(base::make_unique_q<ButtonBar>(this, _bg))
, _paintTopButtons(base::make_unique_q<ButtonBar>(this, _bg))
, _paintBottomButtons(base::make_unique_q<ButtonBar>(this, _bg))
, _about(data.about.empty()
	? nullptr
	: base::make_unique_q<Ui::FadeWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			rpl::single(data.about),
			st::photoEditorAbout)))
, _transformCancel(base::make_unique_q<EdgeButton>(
	_transformButtons,
	tr::lng_cancel(tr::now),
	_buttonHeight,
	st::photoEditorEdgeButtonBg,
	st::mediaviewCaptionFg,
	st::photoEditorRotateButton.ripple))
, _flipButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorFlipButton))
, _rotateButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorRotateButton))
, _paintModeButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorPaintModeButton))
, _cropRatioButton(data.keepAspectRatio
	? nullptr
	: base::make_unique_q<Ui::IconButton>(
		_transformButtons,
		st::photoEditorCropRatioButton))
, _cornersButton(((data.cropType == EditorData::CropType::RoundedRect)
		&& (data.cropMode == EditorData::CropMode::Mask))
	? base::make_unique_q<Ui::IconButton>(
		_transformButtons,
		st::photoEditorCornersButton)
	: nullptr)
, _transformDone(base::make_unique_q<EdgeButton>(
	_transformButtons,
	(data.confirm.isEmpty() ? tr::lng_box_done(tr::now) : data.confirm),
	_buttonHeight,
	st::photoEditorEdgeButtonBg,
	st::mediaviewTextLinkFg,
	st::photoEditorRotateButton.ripple))
, _paintCancel(base::make_unique_q<EdgeButton>(
	_paintBottomButtons,
	tr::lng_cancel(tr::now),
	_buttonHeight,
	st::photoEditorEdgeButtonBg,
	st::mediaviewCaptionFg,
	st::photoEditorRotateButton.ripple))
, _undoButton(base::make_unique_q<Ui::IconButton>(
	_paintTopButtons,
	st::photoEditorUndoButton))
, _redoButton(base::make_unique_q<Ui::IconButton>(
	_paintTopButtons,
	st::photoEditorRedoButton))
, _paintModeButtonActive(base::make_unique_q<Ui::IconButton>(
	_paintBottomButtons,
	st::photoEditorPaintModeButton))
, _stickersButton(controllers->stickersPanelController
		? base::make_unique_q<Ui::IconButton>(
			_paintBottomButtons,
			st::photoEditorStickersButton)
		: nullptr)
, _textButton(base::make_unique_q<TextToolButton>(_paintBottomButtons))
, _shapesButton(base::make_unique_q<Ui::IconButton>(
	_paintBottomButtons,
	st::photoEditorShapesButton))
, _paintDone(base::make_unique_q<EdgeButton>(
	_paintBottomButtons,
	tr::lng_box_done(tr::now),
	_buttonHeight,
	st::photoEditorEdgeButtonBg,
	st::mediaviewTextLinkFg,
	st::photoEditorRotateButton.ripple)) {

	_shapesFilled = shapesFilled;
	_shapesButton->setClickedCallback([=] {
		if (_shapeToolActive) {
			_shapeRequests.fire({ .action = ShapeRequest::Action::Cancel });
		} else {
			showShapesMenu();
		}
	});

	{
		const auto icon = &st::photoEditorPaintIconActive;
		_paintModeButtonActive->setIconOverride(icon, icon);
	}
	_paintModeButtonActive->setAttribute(Qt::WA_TransparentForMouseEvents);

	sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		if (size.isEmpty()) {
			return;
		}

		const auto &padding = st::photoEditorButtonBarPadding;
		const auto w = std::min(st::photoEditorButtonBarWidth, size.width())
			- padding.left()
			- padding.right();
		_transformButtons->resize(w, _buttonHeight);
		_paintBottomButtons->resize(w, _buttonHeight);
		_paintTopButtons->resize(w, _buttonHeight);

		const auto buttonsTop = bottomButtonsTop();

		const auto &current = _transformButtons->isHidden()
			? _paintBottomButtons
			: _transformButtons;

		current->moveToLeft(
			(size.width() - current->width()) / 2,
			buttonsTop);

		if (_about) {
			const auto &margin = st::photoEditorAboutMargin;
			const auto skip = st::photoEditorCropPointSize;
			_about->resizeToWidth(
				size.width() - margin.left() - margin.right());
			_about->moveToLeft(
				(size.width() - _about->width()) / 2,
				margin.top() - skip);
		}
	}, lifetime());

	_mode.changes(
	) | rpl::on_next([=](const PhotoEditorMode &mode) {
		if (mode.mode == PhotoEditorMode::Mode::Out) {
			return;
		}
		const auto animated = (_paintBottomButtons->isVisible()
				== _transformButtons->isVisible())
			? anim::type::instant
			: anim::type::normal;
		showAnimated(mode.mode, animated);
	}, lifetime());

	_paintBottomButtons->positionValue(
	) | rpl::on_next([=](const QPoint &containerPos) {
		_paintTopButtons->moveToLeft(
			containerPos.x(),
			containerPos.y()
				- st::photoEditorControlsCenterSkip
				- _paintTopButtons->height());
	}, _paintBottomButtons->lifetime());

	_paintBottomButtons->shownValue(
	) | rpl::on_next([=](bool shown) {
		_paintTopButtons->setVisible(shown);
	}, _paintBottomButtons->lifetime());

	auto aboutChanges = _about
		? rpl::merge(
			_about->geometryValue() | rpl::to_empty,
			_about->shownValue() | rpl::to_empty)
		: rpl::never<>() | rpl::type_erased;
	rpl::merge(
		geometryValue() | rpl::to_empty,
		_transformButtons->geometryValue() | rpl::to_empty,
		_transformButtons->shownValue() | rpl::to_empty,
		_paintBottomButtons->geometryValue() | rpl::to_empty,
		_paintBottomButtons->shownValue() | rpl::to_empty,
		_paintTopButtons->geometryValue() | rpl::to_empty,
		_paintTopButtons->shownValue() | rpl::to_empty,
		std::move(aboutChanges)
	) | rpl::on_next([=] {
		updateInputMask();
	}, lifetime());

	controllers->undoController->setPerformRequestChanges(rpl::merge(
		_undoButton->clicks() | rpl::map_to(Undo::Undo),
		_redoButton->clicks() | rpl::map_to(Undo::Redo),
		_keyPresses.events(
		) | rpl::filter([=](not_null<QKeyEvent*> e) {
			using Mode = PhotoEditorMode::Mode;
			return (e->matches(QKeySequence::Undo)
					&& !_undoButton->isHidden()
					&& !_undoButton->testAttribute(
						Qt::WA_TransparentForMouseEvents)
					&& (_mode.current().mode == Mode::Paint))
				|| (e->matches(QKeySequence::Redo)
					&& !_redoButton->isHidden()
					&& !_redoButton->testAttribute(
						Qt::WA_TransparentForMouseEvents)
					&& (_mode.current().mode == Mode::Paint));
		}) | rpl::map([=](not_null<QKeyEvent*> e) {
			return e->matches(QKeySequence::Undo) ? Undo::Undo : Undo::Redo;
		})));

	controllers->undoController->canPerformChanges(
	) | rpl::on_next([=](const UndoController::EnableRequest &r) {
		const auto isUndo = (r.command == Undo::Undo);
		const auto &button = isUndo ? _undoButton : _redoButton;
		button->setAttribute(Qt::WA_TransparentForMouseEvents, !r.enable);
		if (!r.enable) {
			button->clearState();
		}

		button->setIconOverride(r.enable
			? nullptr
			: isUndo
			? &st::photoEditorUndoButtonInactive
			: &st::photoEditorRedoButtonInactive);
	}, lifetime());

	if (_stickersButton) {
		using ShowRequest = StickersPanelController::ShowRequest;

		controllers->stickersPanelController->setShowRequestChanges(
			rpl::merge(
				_mode.value(
				) | rpl::map_to(ShowRequest::HideFast),
				_stickersButton->clicks(
				) | rpl::map_to(ShowRequest::ToggleAnimated)
			));

		controllers->stickersPanelController->setMoveRequestChanges(
			_paintBottomButtons->positionValue(
			) | rpl::map([=](const QPoint &containerPos) {
				return QPoint(
					(x() + width()) / 2,
					y() + containerPos.y() + _stickersButton->y());
			}));

		controllers->stickersPanelController->panelShown(
		) | rpl::on_next([=](bool shown) {
			const auto icon = shown
				? &st::photoEditorStickersIconActive
				: nullptr;
			_stickersButton->setIconOverride(icon, icon);
		}, _stickersButton->lifetime());
	}

	rpl::single(rpl::empty) | rpl::skip(
		modifications.flipped ? 0 : 1
	) | rpl::then(
		_flipButton->clicks() | rpl::to_empty
	) | rpl::on_next([=] {
		_flipped = !_flipped;
		const auto icon = _flipped ? &st::photoEditorFlipIconActive : nullptr;
		_flipButton->setIconOverride(icon, icon);
	}, _flipButton->lifetime());

	if (_cropRatioButton) {
		const auto imageRatio = float64(
			_imageSize.width()) / _imageSize.height();
		const auto ratiosMatch = [](float64 a, float64 b) {
			return std::abs(a - b) < 0.01;
		};
		_cropRatioButton->setClickedCallback([=] {
			_ratioMenu = base::make_unique_q<Ui::PopupMenu>(
				_cropRatioButton.get(),
				st::photoEditorCropRatioMenu);
			_ratioMenu->setForcedOrigin(
				Ui::PanelAnimation::Origin::BottomRight);
			const auto check = &st::mediaPlayerMenuCheck;
			const auto add = [&](const QString &text, float64 ratio) {
				const auto selected = ratiosMatch(_currentRatio, ratio);
				_ratioMenu->addAction(
					text,
					[=] {
						if (ratiosMatch(_currentRatio, ratio)) {
							return;
						}
						_currentRatio = ratio;
						_aspectRatioChanges.fire_copy(ratio);
						const auto locked = (ratio > 0.);
						const auto icon = locked
							? &st::photoEditorCropRatioIconActive
							: nullptr;
						_cropRatioButton->setIconOverride(icon, icon);
					},
					selected ? check : nullptr);
			};
			add(tr::lng_photo_editor_crop_original(tr::now), imageRatio);
			add(tr::lng_photo_editor_crop_square(tr::now), 1.);
			add(u"3:2"_q, 3. / 2.);
			add(u"16:9"_q, 16. / 9.);
			add(u"3:4"_q, 3. / 4.);
			add(u"9:16"_q, 9. / 16.);
			add(tr::lng_photo_editor_crop_free(tr::now), 0.);
			const auto button = _cropRatioButton.get();
			const auto bottomRight = button->mapToGlobal(
				QPoint(button->width(), 0));
			_ratioMenu->popup(bottomRight);
		});
	}

	_currentCornersLevel = modifications.cornersLevel;
	if (_cornersButton) {
		const auto updateIcon = [=] {
			const auto active = (_currentCornersLevel
				!= RoundedCornersLevel::Large);
			const auto icon = active
				? &st::photoEditorCornersIconActive
				: nullptr;
			_cornersButton->setIconOverride(icon, icon);
		};
		updateIcon();
		_cornersButton->setClickedCallback([=] {
			_cornersMenu = base::make_unique_q<Ui::PopupMenu>(
				_cornersButton.get(),
				st::photoEditorCropRatioMenu);
			_cornersMenu->setForcedOrigin(
				Ui::PanelAnimation::Origin::BottomRight);
			auto about = base::make_unique_q<Ui::Menu::MultilineAction>(
				_cornersMenu->menu(),
				_cornersMenu->menu()->st(),
				st::photoEditorCornersMenuAboutLabel,
				st::photoEditorCornersMenuAboutPosition,
				TextWithEntities{
					tr::lng_photo_editor_corners_about(tr::now),
				});
			_cornersMenu->addAction(std::move(about));
			_cornersMenu->addSeparator();
			const auto check = &st::mediaPlayerMenuCheck;
			const auto add = [&](
					const QString &text,
					RoundedCornersLevel level) {
				const auto selected = (_currentCornersLevel == level);
				_cornersMenu->addAction(
					text,
					[=] {
						if (_currentCornersLevel == level) {
							return;
						}
						_currentCornersLevel = level;
						updateIcon();
						_cornersLevelChanges.fire_copy(level);
					},
					selected ? check : nullptr);
			};
			add(
				tr::lng_photo_editor_corners_large(tr::now),
				RoundedCornersLevel::Large);
			add(
				tr::lng_photo_editor_corners_medium(tr::now),
				RoundedCornersLevel::Medium);
			add(
				tr::lng_photo_editor_corners_small(tr::now),
				RoundedCornersLevel::Small);
			add(
				tr::lng_photo_editor_corners_none(tr::now),
				RoundedCornersLevel::None);
			if (_originalRatio > 0.) {
				_cornersMenu->addSeparator();
				auto keepRatio = base::make_unique_q<CheckAction>(
					_cornersMenu->menu(),
					_cornersMenu->menu()->st(),
					tr::lng_photo_editor_keep_ratio(tr::now),
					_keepOriginalRatio);
				const auto raw = keepRatio.get();
				keepRatio->setActionTriggered([=] {
					_keepOriginalRatio = !_keepOriginalRatio;
					raw->setChecked(_keepOriginalRatio);
					_aspectRatioChanges.fire_copy(_keepOriginalRatio
						? _originalRatio
						: 1.);
				});
				_cornersMenu->addAction(std::move(keepRatio));
			}
			const auto button = _cornersButton.get();
			const auto bottomRight = button->mapToGlobal(
				QPoint(button->width(), 0));
			_cornersMenu->popup(bottomRight);
		});
	}

	updateInputMask();

}

rpl::producer<int> PhotoEditorControls::rotateRequests() const {
	return _rotateButton->clicks() | rpl::map_to(90);
}

rpl::producer<> PhotoEditorControls::flipRequests() const {
	return _flipButton->clicks() | rpl::to_empty;
}

rpl::producer<> PhotoEditorControls::paintModeRequests() const {
	return _paintModeButton->clicks() | rpl::to_empty;
}

rpl::producer<> PhotoEditorControls::textRequests() const {
	return _textButton->clicks() | rpl::to_empty;
}

rpl::producer<ShapeRequest> PhotoEditorControls::shapeRequests() const {
	return _shapeRequests.events();
}

void PhotoEditorControls::setShapeToolActive(bool active) {
	if (_shapeToolActive == active) {
		return;
	}
	_shapeToolActive = active;
	const auto icon = active ? &st::photoEditorShapesIconActive : nullptr;
	_shapesButton->setIconOverride(icon, icon);
}

rpl::producer<bool> PhotoEditorControls::shapesFillChanges() const {
	return _shapesFillChanges.events();
}

void PhotoEditorControls::showShapesMenu() {
	_shapesMenu = base::make_unique_q<Ui::PopupMenu>(
		_shapesButton.get(),
		st::photoEditorCropRatioMenu);
	_shapesMenu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomRight);
	const auto menu = _shapesMenu.get();

	const auto entries = menu->lifetime().make_state<
		std::vector<ShapeAction*>>();
	const auto add = [&](
			const QString &text,
			ShapeType shape,
			const style::icon *outline,
			const style::icon *fill) {
		auto item = base::make_unique_q<ShapeAction>(
			menu->menu(),
			menu->st().menu,
			new QAction(text, menu),
			outline,
			fill,
			_shapesFilled);
		item->setActionTriggered([=] {
			_shapeRequests.fire({
				.shape = shape,
				.action = AnyModifierPressed()
					? ShapeRequest::Action::Immediate
					: ShapeRequest::Action::Arm,
			});
		});
		entries->push_back(item.get());
		menu->addAction(std::move(item));
	};
	add(
		tr::lng_photo_editor_shape_circle(tr::now),
		ShapeType::Circle,
		&st::photoEditorShapeCircle,
		&st::photoEditorShapeCircleFill);
	add(
		tr::lng_photo_editor_shape_rectangle(tr::now),
		ShapeType::Rectangle,
		&st::photoEditorShapeRectangle,
		&st::photoEditorShapeRectangleFill);
	add(
		tr::lng_photo_editor_shape_star(tr::now),
		ShapeType::Star,
		&st::photoEditorShapeStar,
		&st::photoEditorShapeStarFill);
	add(
		tr::lng_photo_editor_shape_bubble(tr::now),
		ShapeType::Bubble,
		&st::photoEditorShapeBubble,
		&st::photoEditorShapeBubbleFill);
	add(
		tr::lng_photo_editor_shape_arrow(tr::now),
		ShapeType::Arrow,
		&st::photoEditorShapeArrow,
		nullptr);
	menu->addSeparator();

	auto filled = base::make_unique_q<RoundCheckAction>(
		menu->menu(),
		menu->st().menu,
		new QAction(tr::lng_photo_editor_shape_filled(tr::now), menu),
		_shapesFilled);
	const auto filledRaw = filled.get();
	filled->setActionTriggered([=] {
		_shapesFilled = !_shapesFilled;
		_shapesFillChanges.fire_copy(_shapesFilled);
		filledRaw->setChecked(_shapesFilled);
		for (const auto entry : *entries) {
			entry->setFilled(_shapesFilled);
		}
	});
	filled->setPreventClose(true);
	menu->addAction(std::move(filled));

	const auto button = _shapesButton.get();
	const auto bottomRight = button->mapToGlobal(
		QPoint(button->width(), 0));
	menu->popup(bottomRight);
}

rpl::producer<> PhotoEditorControls::doneRequests() const {
	return rpl::merge(
		_transformDone->clicks() | rpl::to_empty,
		_paintDone->clicks() | rpl::to_empty,
		_keyPresses.events(
		) | rpl::filter([=](not_null<QKeyEvent*> e) {
			const auto key = e->key();
			return ((key == Qt::Key_Enter) || (key == Qt::Key_Return))
				&& !_toggledBarAnimation.animating();
		}) | rpl::to_empty);
}

rpl::producer<> PhotoEditorControls::cancelRequests() const {
	return rpl::merge(
		_transformCancel->clicks() | rpl::to_empty,
		_paintCancel->clicks() | rpl::to_empty,
		_keyPresses.events(
	) | rpl::filter([=](not_null<QKeyEvent*> e) {
		const auto key = e->key();
		return (key == Qt::Key_Escape)
			&& !_toggledBarAnimation.animating();
	}) | rpl::to_empty);
}

rpl::producer<float64> PhotoEditorControls::aspectRatioChanges() const {
	return _aspectRatioChanges.events();
}

auto PhotoEditorControls::cornersLevelChanges() const
-> rpl::producer<RoundedCornersLevel> {
	return _cornersLevelChanges.events();
}

int PhotoEditorControls::bottomButtonsTop() const {
	return height()
		- st::photoEditorControlsBottomSkip
		- _transformButtons->height();
}

void PhotoEditorControls::showAnimated(
		PhotoEditorMode::Mode mode,
		anim::type animated) {
	using Mode = PhotoEditorMode::Mode;

	const auto duration = st::photoEditorBarAnimationDuration;

	const auto isTransform = (mode == Mode::Transform);
	if (_about) {
		_about->toggle(isTransform, animated);
	}

	const auto buttonsLeft = (width() - _transformButtons->width()) / 2;
	const auto buttonsTop = bottomButtonsTop();

	const auto visibleBar = _transformButtons->isVisible()
		? _transformButtons.get()
		: _paintBottomButtons.get();

	const auto shouldVisibleBar = isTransform
		? _transformButtons.get()
		: _paintBottomButtons.get(); // Mode::Paint

	const auto computeTop = [=](float64 progress) {
		return anim::interpolate(buttonsTop, height() * 2, progress);
	};

	const auto showShouldVisibleBar = [=] {
		_toggledBarAnimation.stop();
		auto callback = [=](float64 value) {
			shouldVisibleBar->moveToLeft(buttonsLeft, computeTop(value));
		};
		if (animated == anim::type::instant) {
			callback(1.);
		} else {
			_toggledBarAnimation.start(
				std::move(callback),
				1.,
				0.,
				duration,
				anim::easeOutCirc);
		}
	};

	auto animationCallback = [=](float64 value) {
		if (shouldVisibleBar == visibleBar) {
			showShouldVisibleBar();
			return;
		}
		visibleBar->moveToLeft(buttonsLeft, computeTop(value));

		if (value == 1.) {
			shouldVisibleBar->show();
			shouldVisibleBar->moveToLeft(buttonsLeft, computeTop(1.));
			visibleBar->hide();

			showShouldVisibleBar();
		}
	};

	if (animated == anim::type::instant) {
		animationCallback(1.);
	} else {
		_toggledBarAnimation.start(
			std::move(animationCallback),
			0.,
			1.,
			duration,
			anim::easeInCirc);
	}
}

void PhotoEditorControls::updateInputMask() {
	auto region = QRegion();
	const auto visibleRect = rect();
	const auto add = [&](not_null<const QWidget*> widget) {
		if (!widget->isHidden()) {
			const auto geometry = widget->geometry() & visibleRect;
			if (!geometry.isEmpty()) {
				region += geometry;
			}
		}
	};
	add(_transformButtons);
	add(_paintBottomButtons);
	add(_paintTopButtons);
	if (_about && !_about->isHidden()) {
		const auto geometry = _about->geometry() & visibleRect;
		if (!geometry.isEmpty()) {
			region += geometry;
		}
	}
	if (region.isEmpty()) {
		clearMask();
	} else {
		setMask(region);
	}
}

void PhotoEditorControls::applyMode(const PhotoEditorMode &mode) {
	_mode = mode;
}

rpl::producer<QPoint> PhotoEditorControls::colorLinePositionValue() const {
	return rpl::merge(
		geometryValue() | rpl::to_empty,
		_paintTopButtons->geometryValue() | rpl::to_empty
	) | rpl::map([=] {
		const auto r = _paintTopButtons->geometry();
		return mapToParent(r.topLeft())
			+ QPoint(r.width() / 2, r.height() / 2);
	});
}

rpl::producer<bool> PhotoEditorControls::colorLineShownValue() const {
	return _paintTopButtons->shownValue();
}

bool PhotoEditorControls::handleKeyPress(not_null<QKeyEvent*> e) const {
	_keyPresses.fire(std::move(e));
	return true;
}

bool PhotoEditorControls::animating() const {
	return _toggledBarAnimation.animating();
}

} // namespace Editor
