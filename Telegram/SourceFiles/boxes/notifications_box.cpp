/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/notifications_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "messenger.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "window/notifications_manager.h"
#include "platform/platform_specific.h"

namespace {

constexpr int kMaxNotificationsCount = 5;

using ChangeType = Window::Notifications::ChangeType;

} // namespace

class NotificationsBox::SampleWidget : public QWidget {
public:
	SampleWidget(NotificationsBox *owner, const QPixmap &cache) : QWidget(nullptr)
	, _owner(owner)
	, _cache(cache) {
		resize(cache.width() / cache.devicePixelRatio(), cache.height() / cache.devicePixelRatio());

		setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint | Qt::NoDropShadowWindowHint | Qt::Tool);
		setAttribute(Qt::WA_MacAlwaysShowToolWindow);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_OpaquePaintEvent);

		setWindowOpacity(0.);
		show();
	}

	void detach() {
		_owner = nullptr;
		hideFast();
	}

	void showFast() {
		_hiding = false;
		startAnimation();
	}

	void hideFast() {
		_hiding = true;
		startAnimation();
	}

protected:
	virtual void paintEvent(QPaintEvent *e) {
		Painter p(this);
		p.drawPixmap(0, 0, _cache);
	}

private:
	void startAnimation() {
		_opacity.start([this] { animationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., st::notifyFastAnim);
	}
	void animationCallback() {
		setWindowOpacity(_opacity.current(_hiding ? 0. : 1.));
		if (!_opacity.animating() && _hiding) {
			if (_owner) {
				_owner->removeSample(this);
			}
			hide();
			destroyDelayed();
		}
	}

	void destroyDelayed() {
		if (_deleted) return;
		_deleted = true;

		// Ubuntu has a lag if deleteLater() called immediately.
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
		QTimer::singleShot(1000, [this] { delete this; });
#else // Q_OS_LINUX32 || Q_OS_LINUX64
		deleteLater();
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
	}

	NotificationsBox *_owner;
	QPixmap _cache;
	Animation _opacity;
	bool _hiding = false;
	bool _deleted = false;

};

NotificationsBox::NotificationsBox(QWidget *parent)
: _chosenCorner(Global::NotificationsCorner())
, _oldCount(snap(Global::NotificationsCount(), 1, kMaxNotificationsCount))
, _countSlider(this) {
}

void NotificationsBox::prepare() {
	addButton(langFactory(lng_close), [this] { closeBox(); });

	_sampleOpacities.reserve(kMaxNotificationsCount);
	for (int i = 0; i != kMaxNotificationsCount; ++i) {
		_countSlider->addSection(QString::number(i + 1));
		_sampleOpacities.push_back(Animation());
	}
	_countSlider->setActiveSectionFast(_oldCount - 1);
	_countSlider->setSectionActivatedCallback([this] { countChanged(); });

	setMouseTracking(true);

	prepareNotificationSampleSmall();
	prepareNotificationSampleLarge();

	setDimensions(st::boxWideWidth, st::notificationsBoxHeight);
}

void NotificationsBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	auto contentLeft = getContentLeft();

	p.setFont(st::boxTitleFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft(contentLeft, st::boxTitlePosition.y(), width(), lang(lng_settings_notifications_position));

	auto screenRect = getScreenRect();
	p.fillRect(screenRect.x(), screenRect.y(), st::notificationsBoxScreenSize.width(), st::notificationsBoxScreenSize.height(), st::notificationsBoxScreenBg);

	auto monitorTop = st::notificationsBoxMonitorTop;
	st::notificationsBoxMonitor.paint(p, contentLeft, monitorTop, width());

	for (int corner = 0; corner != 4; ++corner) {
		auto screenCorner = static_cast<Notify::ScreenCorner>(corner);
		auto isLeft = Notify::IsLeftCorner(screenCorner);
		auto isTop = Notify::IsTopCorner(screenCorner);
		auto sampleLeft = isLeft ? (screenRect.x() + st::notificationsSampleSkip) : (screenRect.x() + screenRect.width() - st::notificationsSampleSkip - st::notificationSampleSize.width());
		auto sampleTop = isTop ? (screenRect.y() + st::notificationsSampleTopSkip) : (screenRect.y() + screenRect.height() - st::notificationsSampleBottomSkip - st::notificationSampleSize.height());
		if (corner == static_cast<int>(_chosenCorner)) {
			auto count = currentCount();
			for (int i = 0; i != kMaxNotificationsCount; ++i) {
				auto opacity = _sampleOpacities[i].current(getms(), (i < count) ? 1. : 0.);
				p.setOpacity(opacity);
				p.drawPixmapLeft(sampleLeft, sampleTop, width(), _notificationSampleSmall);
				sampleTop += (isTop ? 1 : -1) * (st::notificationSampleSize.height() + st::notificationsSampleMargin);
			}
			p.setOpacity(1.);
		} else {
			p.setOpacity(st::notificationSampleOpacity);
			p.drawPixmapLeft(sampleLeft, sampleTop, width(), _notificationSampleSmall);
			p.setOpacity(1.);
		}
	}

	auto labelTop = screenRect.y() + screenRect.height() + st::notificationsBoxCountLabelTop;
	p.setFont(st::boxTitleFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft(contentLeft, labelTop, width(), lang(lng_settings_notifications_count));
}

void NotificationsBox::countChanged() {
	auto count = currentCount();
	auto moreSamples = (count > _oldCount);
	auto from = moreSamples ? 0. : 1.;
	auto to = moreSamples ? 1. : 0.;
	auto indexDelta = moreSamples ? 1 : -1;
	auto animatedDelta = moreSamples ? 0 : -1;
	for (; _oldCount != count; _oldCount += indexDelta) {
		_sampleOpacities[_oldCount + animatedDelta].start([this] { update(); }, from, to, st::notifyFastAnim);
	}

	if (currentCount() != Global::NotificationsCount()) {
		Global::SetNotificationsCount(currentCount());
		Auth().notifications().settingsChanged().notify(ChangeType::MaxCount);
		Local::writeUserSettings();
	}
}

int NotificationsBox::getContentLeft() const {
	return (width() - st::notificationsBoxMonitor.width()) / 2;
}

QRect NotificationsBox::getScreenRect() const {
	auto screenLeft = (width() - st::notificationsBoxScreenSize.width()) / 2;
	auto screenTop = st::notificationsBoxMonitorTop + st::notificationsBoxScreenTop;
	return QRect(screenLeft, screenTop, st::notificationsBoxScreenSize.width(), st::notificationsBoxScreenSize.height());
}

void NotificationsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	auto screenRect = getScreenRect();
	auto sliderTop = screenRect.y() + screenRect.height() + st::notificationsBoxCountLabelTop + st::notificationsBoxCountTop;
	auto contentLeft = getContentLeft();
	_countSlider->resizeToWidth(width() - 2 * contentLeft);
	_countSlider->move(contentLeft, sliderTop);
}

void NotificationsBox::prepareNotificationSampleSmall() {
	auto width = st::notificationSampleSize.width();
	auto height = st::notificationSampleSize.height();
	auto sampleImage = QImage(width * cIntRetinaFactor(), height * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	sampleImage.setDevicePixelRatio(cRetinaFactor());
	sampleImage.fill(st::notificationBg->c);
	{
		Painter p(&sampleImage);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);

		auto padding = height / 8;
		auto userpicSize = height - 2 * padding;
		p.setBrush(st::notificationSampleUserpicFg);
		p.drawEllipse(rtlrect(padding, padding, userpicSize, userpicSize, width));

		auto rowLeft = height;
		auto rowHeight = padding;
		auto nameTop = (height - 5 * padding) / 2;
		auto nameWidth = height;
		p.setBrush(st::notificationSampleNameFg);
		p.drawRoundedRect(rtlrect(rowLeft, nameTop, nameWidth, rowHeight, width), rowHeight / 2, rowHeight / 2);

		auto rowWidth = (width - rowLeft - 3 * padding);
		auto rowTop = nameTop + rowHeight + padding;
		p.setBrush(st::notificationSampleTextFg);
		p.drawRoundedRect(rtlrect(rowLeft, rowTop, rowWidth, rowHeight, width), rowHeight / 2, rowHeight / 2);
		rowTop += rowHeight + padding;
		p.drawRoundedRect(rtlrect(rowLeft, rowTop, rowWidth, rowHeight, width), rowHeight / 2, rowHeight / 2);

		auto closeLeft = width - 2 * padding;
		p.fillRect(rtlrect(closeLeft, padding, padding, padding, width), st::notificationSampleCloseFg);
	}
	_notificationSampleSmall = App::pixmapFromImageInPlace(std::move(sampleImage));
	_notificationSampleSmall.setDevicePixelRatio(cRetinaFactor());
}

void NotificationsBox::prepareNotificationSampleUserpic() {
	if (_notificationSampleUserpic.isNull()) {
		_notificationSampleUserpic = App::pixmapFromImageInPlace(Messenger::Instance().logoNoMargin().scaled(st::notifyPhotoSize * cIntRetinaFactor(), st::notifyPhotoSize * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		_notificationSampleUserpic.setDevicePixelRatio(cRetinaFactor());
	}
}

void NotificationsBox::prepareNotificationSampleLarge() {
	int w = st::notifyWidth, h = st::notifyMinHeight;
	auto sampleImage = QImage(w * cIntRetinaFactor(), h * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	sampleImage.setDevicePixelRatio(cRetinaFactor());
	sampleImage.fill(st::notificationBg->c);
	{
		Painter p(&sampleImage);
		p.fillRect(0, 0, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(w - st::notifyBorderWidth, 0, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(st::notifyBorderWidth, h - st::notifyBorderWidth, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(0, st::notifyBorderWidth, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder->b);

		prepareNotificationSampleUserpic();
		p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), _notificationSampleUserpic);

		int itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;

		auto rectForName = rtlrect(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height, w);

		auto notifyText = st::dialogsTextFont->elided(lang(lng_notification_sample), itemWidth);
		p.setFont(st::dialogsTextFont);
		p.setPen(st::dialogsTextFgService);
		p.drawText(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height + st::dialogsTextFont->ascent, notifyText);

		p.setPen(st::dialogsNameFg);
		p.setFont(st::msgNameFont);

		auto notifyTitle = st::msgNameFont->elided(qsl("Telegram Desktop"), rectForName.width());
		p.drawText(rectForName.left(), rectForName.top() + st::msgNameFont->ascent, notifyTitle);

		st::notifyClose.icon.paint(p, w - st::notifyClosePos.x() - st::notifyClose.width + st::notifyClose.iconPosition.x(), st::notifyClosePos.y() + st::notifyClose.iconPosition.y(), w);
	}

	_notificationSampleLarge = App::pixmapFromImageInPlace(std::move(sampleImage));
}

void NotificationsBox::removeSample(SampleWidget *widget) {
	for (auto &samples : _cornerSamples) {
		for (int i = 0, size = samples.size(); i != size; ++i) {
			if (samples[i] == widget) {
				for (int j = i + 1; j != size; ++j) {
					samples[j]->detach();
				}
				samples.resize(i);
				break;
			}
		}
	}
}

void NotificationsBox::mouseMoveEvent(QMouseEvent *e) {
	auto screenRect = getScreenRect();
	auto cornerWidth = screenRect.width() / 3;
	auto cornerHeight = screenRect.height() / 3;
	auto topLeft = rtlrect(screenRect.x(), screenRect.y(), cornerWidth, cornerHeight, width());
	auto topRight = rtlrect(screenRect.x() + screenRect.width() - cornerWidth, screenRect.y(), cornerWidth, cornerHeight, width());
	auto bottomRight = rtlrect(screenRect.x() + screenRect.width() - cornerWidth, screenRect.y() + screenRect.height() - cornerHeight, cornerWidth, cornerHeight, width());
	auto bottomLeft = rtlrect(screenRect.x(), screenRect.y() + screenRect.height() - cornerHeight, cornerWidth, cornerHeight, width());
	if (topLeft.contains(e->pos())) {
		setOverCorner(Notify::ScreenCorner::TopLeft);
	} else if (topRight.contains(e->pos())) {
		setOverCorner(Notify::ScreenCorner::TopRight);
	} else if (bottomRight.contains(e->pos())) {
		setOverCorner(Notify::ScreenCorner::BottomRight);
	} else if (bottomLeft.contains(e->pos())) {
		setOverCorner(Notify::ScreenCorner::BottomLeft);
	} else {
		clearOverCorner();
	}
}

void NotificationsBox::leaveEventHook(QEvent *e) {
	clearOverCorner();
}

void NotificationsBox::setOverCorner(Notify::ScreenCorner corner) {
	if (_isOverCorner) {
		if (corner == _overCorner) {
			return;
		}
		for_const (auto widget, _cornerSamples[static_cast<int>(_overCorner)]) {
			widget->hideFast();
		}
	} else {
		_isOverCorner = true;
		setCursor(style::cur_pointer);
		Global::SetNotificationsDemoIsShown(true);
		Auth().notifications().settingsChanged().notify(ChangeType::DemoIsShown);
	}
	_overCorner = corner;

	auto &samples = _cornerSamples[static_cast<int>(_overCorner)];
	auto samplesAlready = samples.size();
	auto samplesNeeded = currentCount();
	auto samplesLeave = qMin(samplesAlready, samplesNeeded);
	for (int i = 0; i != samplesLeave; ++i) {
		samples[i]->showFast();
	}
	if (samplesNeeded > samplesLeave) {
		auto r = psDesktopRect();
		auto isLeft = Notify::IsLeftCorner(_overCorner);
		auto isTop = Notify::IsTopCorner(_overCorner);
		auto sampleLeft = (isLeft == rtl()) ? (r.x() + r.width() - st::notifyWidth - st::notifyDeltaX) : (r.x() + st::notifyDeltaX);
		auto sampleTop = isTop ? (r.y() + st::notifyDeltaY) : (r.y() + r.height() - st::notifyDeltaY - st::notifyMinHeight);
		for (int i = samplesLeave; i != samplesNeeded; ++i) {
			auto widget = std::make_unique<SampleWidget>(this, _notificationSampleLarge);
			widget->move(sampleLeft, sampleTop + (isTop ? 1 : -1) * i * (st::notifyMinHeight + st::notifyDeltaY));
			widget->showFast();
			samples.push_back(widget.release());
		}
	} else {
		for (int i = samplesLeave; i != samplesAlready; ++i) {
			samples[i]->hideFast();
		}
	}
}

void NotificationsBox::clearOverCorner() {
	if (_isOverCorner) {
		_isOverCorner = false;
		setCursor(style::cur_default);
		Global::SetNotificationsDemoIsShown(false);
		Auth().notifications().settingsChanged().notify(ChangeType::DemoIsShown);

		for_const (auto &samples, _cornerSamples) {
			for_const (auto widget, samples) {
				widget->hideFast();
			}
		}
	}
}

int NotificationsBox::currentCount() const {
	return _countSlider->activeSection() + 1;
}

void NotificationsBox::mousePressEvent(QMouseEvent *e) {
	_isDownCorner = _isOverCorner;
	_downCorner = _overCorner;
}

void NotificationsBox::mouseReleaseEvent(QMouseEvent *e) {
	auto isDownCorner = base::take(_isDownCorner);
	if (isDownCorner && _isOverCorner && _downCorner == _overCorner && _downCorner != _chosenCorner) {
		_chosenCorner = _downCorner;
		update();

		if (_chosenCorner != Global::NotificationsCorner()) {
			Global::SetNotificationsCorner(_chosenCorner);
			Auth().notifications().settingsChanged().notify(ChangeType::Corner);
			Local::writeUserSettings();
		}
	}
}

NotificationsBox::~NotificationsBox() {
	for_const (auto &samples, _cornerSamples) {
		for_const (auto widget, samples) {
			widget->detach();
		}
	}
	clearOverCorner();
}
