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
	const auto bg = rounded(_bg);

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		p.drawImage(QPoint(), bg);

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

class HorizontalContainer final : public Ui::RpWidget {
public:
	HorizontalContainer(not_null<Ui::RpWidget*> parent);

	void updateChildrenPosition();

};

HorizontalContainer::HorizontalContainer(not_null<Ui::RpWidget*> parent)
: RpWidget(parent) {
}

void HorizontalContainer::updateChildrenPosition() {
	auto left = 0;
	auto height = 0;
	for (auto child : RpWidget::children()) {
		if (child->isWidgetType()) {
			const auto widget = static_cast<QWidget*>(child);
			widget->move(left, 0);
			left += widget->width();
			height = std::max(height, widget->height());
		}
	}
	resize(left, height);
}

PhotoEditorControls::PhotoEditorControls(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Controllers> controllers,
	const PhotoModifications modifications,
	bool doneControls)
: RpWidget(parent)
, _bg(st::roundedBg)
, _transformButtons(base::make_unique_q<HorizontalContainer>(this))
, _paintButtons(base::make_unique_q<HorizontalContainer>(this))
, _rotateButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorRotateButton))
, _flipButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorFlipButton))
, _paintModeButton(base::make_unique_q<Ui::IconButton>(
	_transformButtons,
	st::photoEditorPaintModeButton))
, _undoButton(base::make_unique_q<Ui::IconButton>(
	_paintButtons,
	st::photoEditorUndoButton))
, _redoButton(base::make_unique_q<Ui::IconButton>(
	_paintButtons,
	st::photoEditorRedoButton))
, _paintModeButtonActive(base::make_unique_q<Ui::IconButton>(
	_paintButtons,
	st::photoEditorPaintModeButton))
, _stickersButton(controllers->stickersPanelController
		? base::make_unique_q<Ui::IconButton>(
			_paintButtons,
			st::photoEditorStickersButton)
		: nullptr)
, _cancel(base::make_unique_q<EdgeButton>(
	this,
	tr::lng_cancel(tr::now),
	_flipButton->height(),
	true,
	_bg,
	st::activeButtonFg,
	st::photoEditorRotateButton.ripple))
, _done(base::make_unique_q<EdgeButton>(
	this,
	tr::lng_box_done(tr::now),
	_flipButton->height(),
	false,
	_bg,
	st::lightButtonFg,
	st::photoEditorRotateButton.ripple)) {

	_transformButtons->updateChildrenPosition();
	_paintButtons->updateChildrenPosition();

	{
		const auto icon = &st::photoEditorPaintIconActive;
		_paintModeButtonActive->setIconOverride(icon, icon);
	}
	_paintModeButtonActive->setAttribute(Qt::WA_TransparentForMouseEvents);

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		const auto &current = _transformButtons->isHidden()
			? _paintButtons
			: _transformButtons;

		p.setPen(Qt::NoPen);
		p.setBrush(_bg);
		p.drawRect(current->geometry());

	}, lifetime());

	const auto &buttonsTop = st::photoEditorControlsTopSkip;

	rpl::combine(
		sizeValue(),
		_mode.value()
	) | rpl::start_with_next([=](
			const QSize &size,
			const PhotoEditorMode &mode) {
		if (size.isEmpty()) {
			return;
		}

		const auto &current = _transformButtons->isHidden()
			? _paintButtons
			: _transformButtons;

		current->moveToLeft(
			(size.width() - current->width()) / 2,
			buttonsTop);

		_cancel->moveToLeft(current->x() - _cancel->width(), buttonsTop);
		_done->moveToLeft(current->x() + current->width(), buttonsTop);

	}, lifetime());

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
		controllers->stickersPanelController->setShowRequestChanges(
			_stickersButton->clicks(
			) | rpl::map_to(std::optional<bool>(std::nullopt)));

		controllers->stickersPanelController->setMoveRequestChanges(
			_paintButtons->positionValue(
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
	return _done->clicks() | rpl::to_empty;
}

rpl::producer<> PhotoEditorControls::cancelRequests() const {
	return _cancel->clicks() | rpl::to_empty;
}

void PhotoEditorControls::applyMode(const PhotoEditorMode &mode) {
	using Mode = PhotoEditorMode::Mode;
	_transformButtons->setVisible(mode.mode == Mode::Transform);
	_paintButtons->setVisible(mode.mode == Mode::Paint);
	_mode = mode;
}

} // namespace Editor
