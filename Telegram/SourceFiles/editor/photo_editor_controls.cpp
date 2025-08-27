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
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
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
, _text(st::photoEditorButtonStyle, text)
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
		const auto textTop = st::photoEditorButtonTextTop;
		_text.draw(p, 0, textTop, width(), style::al_center);
	}, lifetime());
}

QImage EdgeButton::rounded(std::optional<QColor> color) const {
	auto result = QImage(
		_rippleRect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(color.value_or(Qt::white));

	const auto parts = RectPart::None
		| (_left ? RectPart::TopLeft : RectPart::TopRight)
		| (_left ? RectPart::BottomLeft : RectPart::BottomRight);
	return Images::Round(std::move(result), ImageRoundRadius::Large, parts);
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

		_roundedBg = Images::Round(
			std::move(result),
			ImageRoundRadius::Large);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(this);
		p.drawImage(QPoint(), _roundedBg);
	}, lifetime());
}

PhotoEditorControls::PhotoEditorControls(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Controllers> controllers,
	const PhotoModifications modifications,
	const EditorData &data)
: RpWidget(parent)
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
	true,
	_bg,
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
, _transformDone(base::make_unique_q<EdgeButton>(
	_transformButtons,
	(data.confirm.isEmpty() ? tr::lng_box_done(tr::now) : data.confirm),
	_buttonHeight,
	false,
	_bg,
	st::mediaviewTextLinkFg,
	st::photoEditorRotateButton.ripple))
, _paintCancel(base::make_unique_q<EdgeButton>(
	_paintBottomButtons,
	tr::lng_cancel(tr::now),
	_buttonHeight,
	true,
	_bg,
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
, _paintDone(base::make_unique_q<EdgeButton>(
	_paintBottomButtons,
	tr::lng_box_done(tr::now),
	_buttonHeight,
	false,
	_bg,
	st::mediaviewTextLinkFg,
	st::photoEditorRotateButton.ripple)) {

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

	rpl::single(rpl::empty) | rpl::skip(
		modifications.flipped ? 0 : 1
	) | rpl::then(
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
	_keyPresses.fire(std::move(e));
	return true;
}

bool PhotoEditorControls::animating() const {
	return _toggledBarAnimation.animating();
}

} // namespace Editor
