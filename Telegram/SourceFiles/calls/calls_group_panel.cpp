/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_panel.h"

#include "calls/calls_group_members.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/window.h"
#include "ui/effects/ripple_animation.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "base/event_filter.h"
#include "app.h"
#include "styles/style_calls.h"

#ifdef Q_OS_WIN
#include "ui/platform/win/ui_window_title_win.h"
#endif // Q_OS_WIN

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>

namespace Calls {

class GroupPanel::Button final : public Ui::RippleButton {
public:
	Button(
		QWidget *parent,
		const style::CallButton &stFrom,
		const style::CallButton *stTo = nullptr);

	void setProgress(float64 progress);
	void setText(rpl::producer<QString> text);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	QPoint iconPosition(not_null<const style::CallButton*> st) const;
	void mixIconMasks();

	not_null<const style::CallButton*> _stFrom;
	const style::CallButton *_stTo = nullptr;
	float64 _progress = 0.;

	object_ptr<Ui::FlatLabel> _label = { nullptr };

	QImage _bgMask, _bg;
	QPixmap _bgFrom, _bgTo;
	QImage _iconMixedMask, _iconFrom, _iconTo, _iconMixed;

};

GroupPanel::Button::Button(
	QWidget *parent,
	const style::CallButton &stFrom,
	const style::CallButton *stTo)
: Ui::RippleButton(parent, stFrom.button.ripple)
, _stFrom(&stFrom)
, _stTo(stTo) {
	resize(_stFrom->button.width, _stFrom->button.height);

	_bgMask = prepareRippleMask();
	_bgFrom = App::pixmapFromImageInPlace(style::colorizeImage(_bgMask, _stFrom->bg));
	if (_stTo) {
		Assert(_stFrom->button.width == _stTo->button.width);
		Assert(_stFrom->button.height == _stTo->button.height);
		Assert(_stFrom->button.rippleAreaPosition == _stTo->button.rippleAreaPosition);
		Assert(_stFrom->button.rippleAreaSize == _stTo->button.rippleAreaSize);

		_bg = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_bg.setDevicePixelRatio(cRetinaFactor());
		_bgTo = App::pixmapFromImageInPlace(style::colorizeImage(_bgMask, _stTo->bg));
		_iconMixedMask = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixedMask.setDevicePixelRatio(cRetinaFactor());
		_iconFrom = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconFrom.setDevicePixelRatio(cRetinaFactor());
		_iconFrom.fill(Qt::black);
		{
			Painter p(&_iconFrom);
			p.drawImage((_stFrom->button.rippleAreaSize - _stFrom->button.icon.width()) / 2, (_stFrom->button.rippleAreaSize - _stFrom->button.icon.height()) / 2, _stFrom->button.icon.instance(Qt::white));
		}
		_iconTo = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconTo.setDevicePixelRatio(cRetinaFactor());
		_iconTo.fill(Qt::black);
		{
			Painter p(&_iconTo);
			p.drawImage((_stTo->button.rippleAreaSize - _stTo->button.icon.width()) / 2, (_stTo->button.rippleAreaSize - _stTo->button.icon.height()) / 2, _stTo->button.icon.instance(Qt::white));
		}
		_iconMixed = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixed.setDevicePixelRatio(cRetinaFactor());
	}
}

void GroupPanel::Button::setText(rpl::producer<QString> text) {
	_label.create(this, std::move(text), _stFrom->label);
	_label->show();
	rpl::combine(
		sizeValue(),
		_label->sizeValue()
	) | rpl::start_with_next([=](QSize my, QSize label) {
		_label->moveToLeft(
			(my.width() - label.width()) / 2,
			my.height() - label.height(),
			my.width());
	}, _label->lifetime());
}

void GroupPanel::Button::setProgress(float64 progress) {
	_progress = progress;
	update();
}

void GroupPanel::Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto bgPosition = myrtlpoint(_stFrom->button.rippleAreaPosition);
	auto paintFrom = (_progress == 0.) || !_stTo;
	auto paintTo = !paintFrom && (_progress == 1.);

	if (paintFrom) {
		p.drawPixmap(bgPosition, _bgFrom);
	} else if (paintTo) {
		p.drawPixmap(bgPosition, _bgTo);
	} else {
		style::colorizeImage(_bgMask, anim::color(_stFrom->bg, _stTo->bg, _progress), &_bg);
		p.drawImage(bgPosition, _bg);
	}

	auto rippleColorInterpolated = QColor();
	auto rippleColorOverride = &rippleColorInterpolated;
	if (paintFrom) {
		rippleColorOverride = nullptr;
	} else if (paintTo) {
		rippleColorOverride = &_stTo->button.ripple.color->c;
	} else {
		rippleColorInterpolated = anim::color(_stFrom->button.ripple.color, _stTo->button.ripple.color, _progress);
	}
	paintRipple(p, _stFrom->button.rippleAreaPosition.x(), _stFrom->button.rippleAreaPosition.y(), rippleColorOverride);

	auto positionFrom = iconPosition(_stFrom);
	if (paintFrom) {
		const auto icon = &_stFrom->button.icon;
		icon->paint(p, positionFrom, width());
	} else {
		auto positionTo = iconPosition(_stTo);
		if (paintTo) {
			_stTo->button.icon.paint(p, positionTo, width());
		} else {
			mixIconMasks();
			style::colorizeImage(_iconMixedMask, st::callIconFg->c, &_iconMixed);
			p.drawImage(myrtlpoint(_stFrom->button.rippleAreaPosition), _iconMixed);
		}
	}
}

QPoint GroupPanel::Button::iconPosition(not_null<const style::CallButton*> st) const {
	auto result = st->button.iconPosition;
	if (result.x() < 0) {
		result.setX((width() - st->button.icon.width()) / 2);
	}
	if (result.y() < 0) {
		result.setY((height() - st->button.icon.height()) / 2);
	}
	return result;
}

void GroupPanel::Button::mixIconMasks() {
	_iconMixedMask.fill(Qt::black);

	Painter p(&_iconMixedMask);
	PainterHighQualityEnabler hq(p);
	auto paintIconMask = [this, &p](const QImage &mask, float64 angle) {
		auto skipFrom = _stFrom->button.rippleAreaSize / 2;
		p.translate(skipFrom, skipFrom);
		p.rotate(angle);
		p.translate(-skipFrom, -skipFrom);
		p.drawImage(0, 0, mask);
	};
	p.save();
	paintIconMask(_iconFrom, (_stFrom->angle - _stTo->angle) * _progress);
	p.restore();
	p.setOpacity(_progress);
	paintIconMask(_iconTo, (_stTo->angle - _stFrom->angle) * (1. - _progress));
}

void GroupPanel::Button::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto over = isOver();
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (over != wasOver) {
		update();
	}
}

QPoint GroupPanel::Button::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _stFrom->button.rippleAreaPosition;
}

QImage GroupPanel::Button::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_stFrom->button.rippleAreaSize, _stFrom->button.rippleAreaSize));
}

GroupPanel::GroupPanel(not_null<GroupCall*> call)
: _call(call)
, _channel(call->channel())
, _window(std::make_unique<Ui::Window>(Core::App().getModalParent()))
#ifdef Q_OS_WIN
, _controls(std::make_unique<Ui::Platform::TitleControls>(
	_window.get(),
	st::callTitle))
#endif // Q_OS_WIN
, _members(widget(), call)
, _settings(widget(), st::callCancel)
, _hangup(widget(), st::callHangup)
, _mute(widget(), st::callMicrophoneMute, &st::callMicrophoneUnmute) {
	initWindow();
	initWidget();
	initControls();
	initLayout();
	showAndActivate();
}

GroupPanel::~GroupPanel() = default;

void GroupPanel::showAndActivate() {
	if (_window->isHidden()) {
		_window->show();
	}
	_window->raise();
	_window->setWindowState(_window->windowState() | Qt::WindowActive);
	_window->activateWindow();
	_window->setFocus();
}

void GroupPanel::initWindow() {
	_window->setAttribute(Qt::WA_OpaquePaintEvent);
	_window->setAttribute(Qt::WA_NoSystemBackground);
	_window->setWindowIcon(
		QIcon(QPixmap::fromImage(Image::Empty()->original(), Qt::ColorOnly)));
	_window->setTitleStyle(st::callTitle);
	_window->setTitle(computeTitleRect()
		? u" "_q
		: tr::lng_group_call_title(tr::now));

	base::install_event_filter(_window.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	_window->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		if (!widget()->rect().contains(widgetPoint)) {
			return Flag::None | Flag(0);
		}
#ifdef Q_OS_WIN
		if (_controls->geometry().contains(widgetPoint)) {
			return Flag::None | Flag(0);
		}
#endif // Q_OS_WIN
		const auto inControls = false;
		return inControls
			? Flag::None
			: (Flag::Move | Flag::Maximize);
	});
}

void GroupPanel::initWidget() {
	widget()->setMouseTracking(true);

	widget()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		paint(clip);
	}, widget()->lifetime());

	widget()->sizeValue(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, widget()->lifetime());
}

void GroupPanel::initControls() {
	_mute->setClickedCallback([=] {
		if (_call) {
			_call->setMuted(!_call->muted());
		}
	});
	_hangup->setClickedCallback([=] {
		if (_call) {
			_call->hangup();
		}
	});
	_settings->setClickedCallback([=] {
	});
	initWithCall(_call);
}

void GroupPanel::initWithCall(GroupCall *call) {
	_callLifetime.destroy();
	_call = call;
	if (!_call) {
		return;
	}

	_channel = _call->channel();

	_settings->setText(tr::lng_menu_settings());
	_hangup->setText(tr::lng_box_leave());

	_call->mutedValue(
	) | rpl::start_with_next([=](bool mute) {
		_mute->setProgress(mute ? 1. : 0.);
		_mute->setText(mute
			? tr::lng_call_unmute_audio()
			: tr::lng_call_mute_audio());
	}, _callLifetime);

	_call->stateValue(
	) | rpl::start_with_next([=](State state) {
		stateChanged(state);
	}, _callLifetime);

	_members->desiredHeightValue(
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, _members->lifetime());
}

void GroupPanel::initLayout() {
	initGeometry();

#ifdef Q_OS_WIN
	_controls->raise();
#endif // Q_OS_WIN
}

void GroupPanel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
}

void GroupPanel::closeBeforeDestroy() {
	_window->close();
	initWithCall(nullptr);
}

void GroupPanel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto rect = QRect(0, 0, st::groupCallWidth, st::groupCallHeight);
	_window->setGeometry(rect.translated(center - rect.center()));
	_window->setMinimumSize(rect.size());
	_window->show();
	updateControlsGeometry();
}

int GroupPanel::computeMembersListTop() const {
#ifdef Q_OS_WIN
	return st::callTitleButton.height + st::groupCallMembersMargin.top() / 2;
#elif defined Q_OS_MAC // Q_OS_WIN
	return st::groupCallMembersMargin.top() * 2;
#else // Q_OS_WIN || Q_OS_MAC
	return st::groupCallMembersMargin.top();
#endif // Q_OS_WIN || Q_OS_MAC
}

std::optional<QRect> GroupPanel::computeTitleRect() const {
#ifdef Q_OS_WIN
	const auto controls = _controls->geometry();
	return QRect(0, 0, controls.x(), controls.height());
#else // Q_OS_WIN
	return std::nullopt;
#endif // Q_OS_WIN
}

void GroupPanel::updateControlsGeometry() {
	if (widget()->size().isEmpty()) {
		return;
	}
	const auto desiredHeight = _members->desiredHeight();
	const auto membersWidthAvailable = widget()->width()
		- st::groupCallMembersMargin.left()
		- st::groupCallMembersMargin.right();
	const auto membersWidthMin = st::groupCallWidth
		- st::groupCallMembersMargin.left()
		- st::groupCallMembersMargin.right();
	const auto membersWidth = std::clamp(
		membersWidthAvailable,
		membersWidthMin,
		st::groupCallMembersWidthMax);
	const auto muteTop = widget()->height() - 2 * _mute->height();
	const auto buttonsTop = muteTop;
	const auto membersTop = computeMembersListTop();
	const auto availableHeight = buttonsTop
		- membersTop
		- st::groupCallMembersMargin.bottom();
	_members->setGeometry(
		st::groupCallMembersMargin.left(),
		membersTop,
		membersWidth,
		std::min(desiredHeight, availableHeight));
	_mute->move((widget()->width() - _mute->width()) / 2, muteTop);
	_settings->moveToLeft(_settings->width(), buttonsTop);
	_hangup->moveToRight(_settings->width(), buttonsTop);
	refreshTitle();
}

void GroupPanel::refreshTitle() {
	if (const auto titleRect = computeTitleRect()) {
		if (!_title) {
			_title.create(
				widget(),
				tr::lng_group_call_title(),
				st::groupCallHeaderLabel);
			_window->setTitle(u" "_q);
		}
		const auto best = _title->naturalWidth();
		const auto from = (widget()->width() - best) / 2;
		const auto top = (computeMembersListTop() - _title->height()) / 2;
		const auto left = titleRect->x();
		if (from >= left && from + best <= left + titleRect->width()) {
			_title->resizeToWidth(best);
			_title->moveToLeft(from, top);
		} else if (titleRect->width() < best) {
			_title->resizeToWidth(titleRect->width());
			_title->moveToLeft(left, top);
		} else if (from < left) {
			_title->resizeToWidth(best);
			_title->moveToLeft(left, top);
		} else {
			_title->resizeToWidth(best);
			_title->moveToLeft(left + titleRect->width() - best, top);
		}
	} else if (_title) {
		_title.destroy();
		_window->setTitle(tr::lng_group_call_title(tr::now));
	}
}

void GroupPanel::paint(QRect clip) {
	Painter p(widget());

	auto region = QRegion(clip);
	for (const auto rect : region) {
		p.fillRect(rect, st::groupCallBg);
	}
}

bool GroupPanel::handleClose() {
	if (_call) {
		_window->hide();
		return true;
	}
	return false;
}

not_null<Ui::RpWidget*> GroupPanel::widget() const {
	return _window->body();
}

void GroupPanel::stateChanged(State state) {
	Expects(_call != nullptr);

	if ((state != State::HangingUp)
		&& (state != State::Ended)
		&& (state != State::FailedHangingUp)
		&& (state != State::Failed)) {
	}
}

} // namespace Calls
