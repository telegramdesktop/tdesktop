/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_controls.h"

#include "editor/controllers/controllers.h"
#include "lang/lang_keys.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/buttons.h"

#include "styles/style_editor.h"

namespace Editor {

class EdgeButton final : public Ui::RippleButton {
public:
	EdgeButton(
		not_null<Ui::RpWidget*> parent,
		const QString &text,
		int height,
		bool left,
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
	const bool _left;

	QImage rounded(std::optional<QColor> color) const;

};

EdgeButton::EdgeButton(
	not_null<Ui::RpWidget*> parent,
	const QString &text,
	int height,
	bool left,
	const style::color &bg,
	const style::color &fg,
	const style::RippleAnimation &st)
: Ui::RippleButton(parent, st)
, _fg(fg)
, _text(st::semiboldTextStyle, text.toUpper())
, _width(_text.maxWidth()
	+ st::photoEditorTextButtonPadding.left()
	+ st::photoEditorTextButtonPadding.right())
, _rippleRect(QRect(0, 0, _width, height))
, _bg(bg->c)
, _left(left) {
	resize(_width, height);
	init();
}

void EdgeButton::init() {
	// const auto bg = rounded(_bg);

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		// p.drawImage(QPoint(), bg);

		paintRipple(p, _rippleRect.x(), _rippleRect.y());

		p.setPen(_fg);
		const auto textTop = (height() - _text.minHeight()) / 2;
		_text.draw(p, 0, textTop, width(), style::al_center);
	}, lifetime());
}

QImage EdgeButton::rounded(std::optional<QColor> color) const {
	auto result = QImage(
		_rippleRect.size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cIntRetinaFactor());
	result.fill(color.value_or(Qt::white));

	using Option = Images::Option;
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| (_left ? Option::RoundedTopLeft : Option::RoundedTopRight)
		| (_left ? Option::RoundedBottomLeft : Option::RoundedBottomRight);
	return Images::prepare(std::move(result), 0, 0, options, 0, 0);
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
	) | rpl::start_with_next([=](const QSize &size) {
		const auto children = RpWidget::children();
		if (children.empty()) {
			return;
		}
		const auto widgets = ranges::views::all(
			children
		) | ranges::views::filter([](not_null<const QObject*> object) {
			return object->isWidgetType();
		}) | ranges::views::transform([](not_null<QObject*> object) {
			return static_cast<Ui::RpWidget*>(object.get());
		}) | ranges::to_vector;

		const auto residualWidth = size.width()
			- ranges::accumulate(widgets, 0, ranges::plus(), &QWidget::width);
		const auto step = residualWidth / float(widgets.size() - 1);

		auto left = 0.;
		for (const auto &widget : widgets) {
			widget->moveToLeft(int(left), 0);
			left += widget->width() + step;
		}

		auto result = QImage(
			size * cIntRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(cIntRetinaFactor());
		result.fill(bg->c);

		const auto options = Images::Option::Smooth
			| Images::Option::RoundedLarge
			| Images::Option::RoundedAll;
		_roundedBg = Images::prepare(std::move(result), 0, 0, options, 0, 0);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		p.drawImage(QPoint(), _roundedBg);
	}, lifetime());
}

PhotoEditorControls::PhotoEditorControls(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Controllers> controllers,
	const PhotoModifications modifications,
	bool doneControls)
: RpWidget(parent)
, _bg(st::roundedBg)
, _buttonHeight(st::photoEditorButtonBarHeight)
, _transformButtons(base::make_unique_q<ButtonBar>(this, _bg))
, _paintTopButtons(base::make_unique_q<ButtonBar>(this, _bg))
, _paintBottomButtons(base::make_unique_q<ButtonBar>(this, _bg))
, _transformCancel(base::make_unique_q<EdgeButton>(
	_transformButtons,
	tr::lng_cancel(tr::now),
	_buttonHeight,
	true,
	_bg,
	st::activeButtonFg,
	st::photoEditorRotateButton.ripple))
, _rotateButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorRotateButton))
, _flipButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorFlipButton))
, _paintModeButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorPaintModeButton))
, _transformDone(base::make_unique_q<EdgeButton>(
	_transformButtons,
	tr::lng_box_done(tr::now),
	_buttonHeight,
	false,
	_bg,
	st::lightButtonFg,
	st::photoEditorRotateButton.ripple))
, _paintCancel(base::make_unique_q<EdgeButton>(
	_paintBottomButtons,
	tr::lng_cancel(tr::now),
	_buttonHeight,
	true,
	_bg,
	st::activeButtonFg,
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
, _paintDone(base::make_unique_q<EdgeButton>(
	_paintBottomButtons,
	tr::lng_box_done(tr::now),
	_buttonHeight,
	false,
	_bg,
	st::lightButtonFg,
	st::photoEditorRotateButton.ripple)) {

	{
		const auto &padding = st::photoEditorButtonBarPadding;
		const auto w = st::photoEditorButtonBarWidth
			- padding.left()
			- padding.right();
		_transformButtons->resize(w, _buttonHeight);
		_paintBottomButtons->resize(w, _buttonHeight);
		_paintTopButtons->resize(w, _buttonHeight);
	}

	{
		const auto icon = &st::photoEditorPaintIconActive;
		_paintModeButtonActive->setIconOverride(icon, icon);
	}
	_paintModeButtonActive->setAttribute(Qt::WA_TransparentForMouseEvents);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		if (size.isEmpty()) {
			return;
		}

		const auto buttonsTop = bottomButtonsTop();

		const auto &current = _transformButtons->isHidden()
			? _paintBottomButtons
			: _transformButtons;

		current->moveToLeft(
			(size.width() - current->width()) / 2,
			buttonsTop);
	}, lifetime());

	_mode.changes(
	) | rpl::start_with_next([=](const PhotoEditorMode &mode) {
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
	) | rpl::start_with_next([=](const QPoint &containerPos) {
		_paintTopButtons->moveToLeft(
			containerPos.x(),
			containerPos.y()
				- st::photoEditorControlsCenterSkip
				- _paintTopButtons->height());
	}, _paintBottomButtons->lifetime());

	_paintBottomButtons->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		_paintTopButtons->setVisible(shown);
	}, _paintBottomButtons->lifetime());

	controllers->undoController->setPerformRequestChanges(rpl::merge(
		_undoButton->clicks() | rpl::map_to(Undo::Undo),
		_redoButton->clicks() | rpl::map_to(Undo::Redo)));

	controllers->undoController->canPerformChanges(
	) | rpl::start_with_next([=](const UndoController::EnableRequest &r) {
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
		) | rpl::start_with_next([=](bool shown) {
			const auto icon = shown
				? &st::photoEditorStickersIconActive
				: nullptr;
			_stickersButton->setIconOverride(icon, icon);
		}, _stickersButton->lifetime());
	}

	rpl::single(
		rpl::empty_value()
	) | rpl::skip(modifications.flipped ? 0 : 1) | rpl::then(
		_flipButton->clicks() | rpl::to_empty
	) | rpl::start_with_next([=] {
		_flipped = !_flipped;
		const auto icon = _flipped ? &st::photoEditorFlipIconActive : nullptr;
		_flipButton->setIconOverride(icon, icon);
	}, _flipButton->lifetime());

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

rpl::producer<> PhotoEditorControls::doneRequests() const {
	return rpl::merge(
		_transformDone->clicks() | rpl::to_empty,
		_paintDone->clicks() | rpl::to_empty,
		_keyPresses.events(
		) | rpl::filter([=](int key) {
			return ((key == Qt::Key_Enter) || (key == Qt::Key_Return))
				&& !_toggledBarAnimation.animating();
		}) | rpl::to_empty);
}

rpl::producer<> PhotoEditorControls::cancelRequests() const {
	return rpl::merge(
		_transformCancel->clicks() | rpl::to_empty,
		_paintCancel->clicks() | rpl::to_empty,
		_keyPresses.events(
		) | rpl::filter([=](int key) {
			return (key == Qt::Key_Escape)
				&& !_toggledBarAnimation.animating();
		}) | rpl::to_empty);
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
				duration);
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
			duration);
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
	_keyPresses.fire(e->key());
	return true;
}

bool PhotoEditorControls::animating() const {
	return _toggledBarAnimation.animating();
}

} // namespace Editor
