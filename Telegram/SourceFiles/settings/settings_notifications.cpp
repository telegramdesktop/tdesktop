/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_notifications.h"

#include "settings/settings_common.h"
#include "ui/effects/animations.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "platform/platform_specific.h"
#include "platform/platform_notifications_manager.h"
#include "base/platform/base_platform_info.h"
#include "mainwindow.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "apiwrap.h"
#include "facades.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"

#include <QTimer>

namespace Settings {
namespace {

constexpr auto kMaxNotificationsCount = 5;

[[nodiscard]] int CurrentCount() {
	return std::clamp(
		Core::App().settings().notificationsCount(),
		1,
		kMaxNotificationsCount);
}

using ChangeType = Window::Notifications::ChangeType;

class NotificationsCount : public Ui::RpWidget {
public:
	NotificationsCount(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setCount(int count);

	~NotificationsCount();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	using ScreenCorner = Core::Settings::ScreenCorner;
	void setOverCorner(ScreenCorner corner);
	void clearOverCorner();

	class SampleWidget;
	void removeSample(SampleWidget *widget);

	QRect getScreenRect() const;
	QRect getScreenRect(int width) const;
	int getContentLeft() const;
	void prepareNotificationSampleSmall();
	void prepareNotificationSampleLarge();
	void prepareNotificationSampleUserpic();

	const not_null<Window::SessionController*> _controller;

	QPixmap _notificationSampleUserpic;
	QPixmap _notificationSampleSmall;
	QPixmap _notificationSampleLarge;
	ScreenCorner _chosenCorner;
	std::vector<Ui::Animations::Simple> _sampleOpacities;

	bool _isOverCorner = false;
	ScreenCorner _overCorner = ScreenCorner::TopLeft;
	bool _isDownCorner = false;
	ScreenCorner _downCorner = ScreenCorner::TopLeft;

	int _oldCount;

	QVector<SampleWidget*> _cornerSamples[4];

};

class NotificationsCount::SampleWidget : public QWidget {
public:
	SampleWidget(NotificationsCount *owner, const QPixmap &cache);

	void detach();
	void showFast();
	void hideFast();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void startAnimation();
	void animationCallback();

	void destroyDelayed();

	NotificationsCount *_owner;
	QPixmap _cache;
	Ui::Animations::Simple _opacity;
	bool _hiding = false;
	bool _deleted = false;

};

NotificationsCount::NotificationsCount(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _chosenCorner(Core::App().settings().notificationsCorner())
, _oldCount(CurrentCount()) {
	setMouseTracking(true);

	_sampleOpacities.resize(kMaxNotificationsCount);

	prepareNotificationSampleSmall();
	prepareNotificationSampleLarge();
}

void NotificationsCount::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto contentLeft = getContentLeft();

	auto screenRect = getScreenRect();
	p.fillRect(
		screenRect.x(),
		screenRect.y(),
		st::notificationsBoxScreenSize.width(),
		st::notificationsBoxScreenSize.height(),
		st::notificationsBoxScreenBg);

	auto monitorTop = 0;
	st::notificationsBoxMonitor.paint(p, contentLeft, monitorTop, width());

	for (int corner = 0; corner != 4; ++corner) {
		auto screenCorner = static_cast<ScreenCorner>(corner);
		auto isLeft = Core::Settings::IsLeftCorner(screenCorner);
		auto isTop = Core::Settings::IsTopCorner(screenCorner);
		auto sampleLeft = isLeft ? (screenRect.x() + st::notificationsSampleSkip) : (screenRect.x() + screenRect.width() - st::notificationsSampleSkip - st::notificationSampleSize.width());
		auto sampleTop = isTop ? (screenRect.y() + st::notificationsSampleTopSkip) : (screenRect.y() + screenRect.height() - st::notificationsSampleBottomSkip - st::notificationSampleSize.height());
		if (corner == static_cast<int>(_chosenCorner)) {
			auto count = _oldCount;
			for (int i = 0; i != kMaxNotificationsCount; ++i) {
				auto opacity = _sampleOpacities[i].value((i < count) ? 1. : 0.);
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
}

void NotificationsCount::setCount(int count) {
	auto moreSamples = (count > _oldCount);
	auto from = moreSamples ? 0. : 1.;
	auto to = moreSamples ? 1. : 0.;
	auto indexDelta = moreSamples ? 1 : -1;
	auto animatedDelta = moreSamples ? 0 : -1;
	for (; _oldCount != count; _oldCount += indexDelta) {
		_sampleOpacities[_oldCount + animatedDelta].start([this] { update(); }, from, to, st::notifyFastAnim);
	}

	if (count != Core::App().settings().notificationsCount()) {
		Core::App().settings().setNotificationsCount(count);
		Core::App().saveSettingsDelayed();
		Core::App().notifications().notifySettingsChanged(
			ChangeType::MaxCount);
	}
}

int NotificationsCount::getContentLeft() const {
	return (width() - st::notificationsBoxMonitor.width()) / 2;
}

QRect NotificationsCount::getScreenRect() const {
	return getScreenRect(width());
}

QRect NotificationsCount::getScreenRect(int width) const {
	auto screenLeft = (width - st::notificationsBoxScreenSize.width()) / 2;
	auto screenTop = st::notificationsBoxScreenTop;
	return QRect(screenLeft, screenTop, st::notificationsBoxScreenSize.width(), st::notificationsBoxScreenSize.height());
}

int NotificationsCount::resizeGetHeight(int newWidth) {
	update();
	return st::notificationsBoxMonitor.height();
}

void NotificationsCount::prepareNotificationSampleSmall() {
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
		p.drawEllipse(style::rtlrect(padding, padding, userpicSize, userpicSize, width));

		auto rowLeft = height;
		auto rowHeight = padding;
		auto nameTop = (height - 5 * padding) / 2;
		auto nameWidth = height;
		p.setBrush(st::notificationSampleNameFg);
		p.drawRoundedRect(style::rtlrect(rowLeft, nameTop, nameWidth, rowHeight, width), rowHeight / 2, rowHeight / 2);

		auto rowWidth = (width - rowLeft - 3 * padding);
		auto rowTop = nameTop + rowHeight + padding;
		p.setBrush(st::notificationSampleTextFg);
		p.drawRoundedRect(style::rtlrect(rowLeft, rowTop, rowWidth, rowHeight, width), rowHeight / 2, rowHeight / 2);
		rowTop += rowHeight + padding;
		p.drawRoundedRect(style::rtlrect(rowLeft, rowTop, rowWidth, rowHeight, width), rowHeight / 2, rowHeight / 2);

		auto closeLeft = width - 2 * padding;
		p.fillRect(style::rtlrect(closeLeft, padding, padding, padding, width), st::notificationSampleCloseFg);
	}
	_notificationSampleSmall = Ui::PixmapFromImage(std::move(sampleImage));
	_notificationSampleSmall.setDevicePixelRatio(cRetinaFactor());
}

void NotificationsCount::prepareNotificationSampleUserpic() {
	if (_notificationSampleUserpic.isNull()) {
		_notificationSampleUserpic = Ui::PixmapFromImage(
			Core::App().logoNoMargin().scaled(
				st::notifyPhotoSize * cIntRetinaFactor(),
				st::notifyPhotoSize * cIntRetinaFactor(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation));
		_notificationSampleUserpic.setDevicePixelRatio(cRetinaFactor());
	}
}

void NotificationsCount::prepareNotificationSampleLarge() {
	int w = st::notifyWidth, h = st::notifyMinHeight;
	auto sampleImage = QImage(
		w * cIntRetinaFactor(),
		h * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
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

		auto rectForName = style::rtlrect(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height, w);

		auto notifyText = st::dialogsTextFont->elided(tr::lng_notification_sample(tr::now), itemWidth);
		p.setFont(st::dialogsTextFont);
		p.setPen(st::dialogsTextFgService);
		p.drawText(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height + st::dialogsTextFont->ascent, notifyText);

		p.setPen(st::dialogsNameFg);
		p.setFont(st::msgNameFont);

		auto notifyTitle = st::msgNameFont->elided(qsl("Telegram Desktop"), rectForName.width());
		p.drawText(rectForName.left(), rectForName.top() + st::msgNameFont->ascent, notifyTitle);

		st::notifyClose.icon.paint(p, w - st::notifyClosePos.x() - st::notifyClose.width + st::notifyClose.iconPosition.x(), st::notifyClosePos.y() + st::notifyClose.iconPosition.y(), w);
	}

	_notificationSampleLarge = Ui::PixmapFromImage(std::move(sampleImage));
}

void NotificationsCount::removeSample(SampleWidget *widget) {
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

void NotificationsCount::mouseMoveEvent(QMouseEvent *e) {
	auto screenRect = getScreenRect();
	auto cornerWidth = screenRect.width() / 3;
	auto cornerHeight = screenRect.height() / 3;
	auto topLeft = style::rtlrect(screenRect.x(), screenRect.y(), cornerWidth, cornerHeight, width());
	auto topRight = style::rtlrect(screenRect.x() + screenRect.width() - cornerWidth, screenRect.y(), cornerWidth, cornerHeight, width());
	auto bottomRight = style::rtlrect(screenRect.x() + screenRect.width() - cornerWidth, screenRect.y() + screenRect.height() - cornerHeight, cornerWidth, cornerHeight, width());
	auto bottomLeft = style::rtlrect(screenRect.x(), screenRect.y() + screenRect.height() - cornerHeight, cornerWidth, cornerHeight, width());
	if (topLeft.contains(e->pos())) {
		setOverCorner(ScreenCorner::TopLeft);
	} else if (topRight.contains(e->pos())) {
		setOverCorner(ScreenCorner::TopRight);
	} else if (bottomRight.contains(e->pos())) {
		setOverCorner(ScreenCorner::BottomRight);
	} else if (bottomLeft.contains(e->pos())) {
		setOverCorner(ScreenCorner::BottomLeft);
	} else {
		clearOverCorner();
	}
}

void NotificationsCount::leaveEventHook(QEvent *e) {
	clearOverCorner();
}

void NotificationsCount::setOverCorner(ScreenCorner corner) {
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
		Core::App().notifications().notifySettingsChanged(
			ChangeType::DemoIsShown);
	}
	_overCorner = corner;

	auto &samples = _cornerSamples[static_cast<int>(_overCorner)];
	auto samplesAlready = samples.size();
	auto samplesNeeded = _oldCount;
	auto samplesLeave = qMin(samplesAlready, samplesNeeded);
	for (int i = 0; i != samplesLeave; ++i) {
		samples[i]->showFast();
	}
	if (samplesNeeded > samplesLeave) {
		auto r = _controller->widget()->desktopRect();
		auto isLeft = Core::Settings::IsLeftCorner(_overCorner);
		auto isTop = Core::Settings::IsTopCorner(_overCorner);
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

void NotificationsCount::clearOverCorner() {
	if (_isOverCorner) {
		_isOverCorner = false;
		setCursor(style::cur_default);
		Core::App().notifications().notifySettingsChanged(
			ChangeType::DemoIsHidden);

		for_const (const auto &samples, _cornerSamples) {
			for_const (const auto widget, samples) {
				widget->hideFast();
			}
		}
	}
}

void NotificationsCount::mousePressEvent(QMouseEvent *e) {
	_isDownCorner = _isOverCorner;
	_downCorner = _overCorner;
}

void NotificationsCount::mouseReleaseEvent(QMouseEvent *e) {
	auto isDownCorner = base::take(_isDownCorner);
	if (isDownCorner
		&& _isOverCorner
		&& _downCorner == _overCorner
		&& _downCorner != _chosenCorner) {
		_chosenCorner = _downCorner;
		update();

		if (_chosenCorner != Core::App().settings().notificationsCorner()) {
			Core::App().settings().setNotificationsCorner(_chosenCorner);
			Core::App().saveSettingsDelayed();
			Core::App().notifications().notifySettingsChanged(
				ChangeType::Corner);
		}
	}
}

NotificationsCount::~NotificationsCount() {
	for_const (auto &samples, _cornerSamples) {
		for_const (auto widget, samples) {
			widget->detach();
		}
	}
	clearOverCorner();
}

NotificationsCount::SampleWidget::SampleWidget(
	NotificationsCount *owner,
	const QPixmap &cache)
: _owner(owner)
, _cache(cache) {
	const QSize size(
		cache.width() / cache.devicePixelRatio(),
		cache.height() / cache.devicePixelRatio());

	resize(size);
	setMinimumSize(size);
	setMaximumSize(size);

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::WindowStaysOnTopHint
		| Qt::BypassWindowManagerHint
		| Qt::NoDropShadowWindowHint
		| Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_OpaquePaintEvent);

	setWindowOpacity(0.);
	show();
}

void NotificationsCount::SampleWidget::detach() {
	_owner = nullptr;
	hideFast();
}

void NotificationsCount::SampleWidget::showFast() {
	_hiding = false;
	startAnimation();
}

void NotificationsCount::SampleWidget::hideFast() {
	_hiding = true;
	startAnimation();
}

void NotificationsCount::SampleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.drawPixmap(0, 0, _cache);
}

void NotificationsCount::SampleWidget::startAnimation() {
	_opacity.start(
		[=] { animationCallback(); },
		_hiding ? 1. : 0.,
		_hiding ? 0. : 1.,
		st::notifyFastAnim);
}

void NotificationsCount::SampleWidget::animationCallback() {
	setWindowOpacity(_opacity.value(_hiding ? 0. : 1.));
	if (!_opacity.animating() && _hiding) {
		if (_owner) {
			_owner->removeSample(this);
		}
		hide();
		destroyDelayed();
	}
}

void NotificationsCount::SampleWidget::destroyDelayed() {
	if (_deleted) return;
	_deleted = true;

	// Ubuntu has a lag if deleteLater() called immediately.
#if defined Q_OS_UNIX && !defined Q_OS_MAC
	QTimer::singleShot(1000, [this] { delete this; });
#else // Q_OS_UNIX && !Q_OS_MAC
	deleteLater();
#endif // Q_OS_UNIX && !Q_OS_MAC
}

void SetupAdvancedNotifications(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsCheckboxesSkip);
	AddDivider(container);
	AddSkip(container, st::settingsCheckboxesSkip);
	AddSubsectionTitle(container, tr::lng_settings_notifications_position());
	AddSkip(container, st::settingsCheckboxesSkip);

	const auto position = container->add(
		object_ptr<NotificationsCount>(container, controller));

	AddSkip(container, st::settingsCheckboxesSkip);
	AddSubsectionTitle(container, tr::lng_settings_notifications_count());

	const auto count = container->add(
		object_ptr<Ui::SettingsSlider>(container, st::settingsSlider),
		st::settingsBigScalePadding);
	for (int i = 0; i != kMaxNotificationsCount; ++i) {
		count->addSection(QString::number(i + 1));
	}
	count->setActiveSectionFast(CurrentCount() - 1);
	count->sectionActivated(
	) | rpl::start_with_next([=](int section) {
		position->setCount(section + 1);
	}, count->lifetime());
	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupMultiAccountNotifications(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	if (Core::App().domain().accounts().size() < 2) {
		return;
	}
	AddSubsectionTitle(container, tr::lng_settings_show_from());

	const auto fromAll = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			tr::lng_settings_notify_all(tr::now),
			Core::App().settings().notifyFromAll(),
			st::settingsCheckbox),
		st::settingsCheckboxPadding);
	fromAll->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().notifyFromAll());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setNotifyFromAll(checked);
		Core::App().saveSettingsDelayed();
		if (!checked) {
			auto &notifications = Core::App().notifications();
			const auto &list = Core::App().domain().accounts();
			for (const auto &[index, account] : list) {
				if (account.get() == &Core::App().domain().active()) {
					continue;
				} else if (const auto session = account->maybeSession()) {
					notifications.clearFromSession(session);
				}
			}
		}
	}, fromAll->lifetime());

	AddSkip(container);
	AddDividerText(container, tr::lng_settings_notify_all_about());
	AddSkip(container);
}

void SetupNotificationsContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using NotifyView = Core::Settings::NotifyView;
	SetupMultiAccountNotifications(controller, container);

	AddSubsectionTitle(container, tr::lng_settings_notify_title());

	const auto session = &controller->session();
	const auto checkbox = [&](const QString &label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			label,
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](const QString &label, bool checked) {
		return container->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding);
	};
	const auto addSlidingCheckbox = [&](const QString &label, bool checked) {
		return container->add(
			object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
				container,
				checkbox(label, checked),
				st::settingsCheckboxPadding));
	};
	const auto &settings = Core::App().settings();
	const auto desktop = addCheckbox(
		tr::lng_settings_desktop_notify(tr::now),
		settings.desktopNotify());
	const auto name = addSlidingCheckbox(
		tr::lng_settings_show_name(tr::now),
		(settings.notifyView() <= NotifyView::ShowName));
	const auto preview = addSlidingCheckbox(
		tr::lng_settings_show_preview(tr::now),
		(settings.notifyView() <= NotifyView::ShowPreview));
	const auto sound = addCheckbox(
		tr::lng_settings_sound_notify(tr::now),
		settings.soundNotify());
	const auto flashbounce = addCheckbox(
		(Platform::IsWindows()
			? tr::lng_settings_alert_windows
			: Platform::IsMac()
			? tr::lng_settings_alert_mac
			: tr::lng_settings_alert_linux)(tr::now),
		settings.flashBounceNotify());

	AddSkip(container, st::settingsCheckboxesSkip);
	AddDivider(container);
	AddSkip(container, st::settingsCheckboxesSkip);
	AddSubsectionTitle(container, tr::lng_settings_events_title());

	const auto joined = addCheckbox(
		tr::lng_settings_events_joined(tr::now),
		!session->api().contactSignupSilentCurrent().value_or(false));
	session->api().contactSignupSilent(
	) | rpl::start_with_next([=](bool silent) {
		joined->setChecked(!silent);
	}, joined->lifetime());
	joined->checkedChanges(
	) | rpl::filter([=](bool enabled) {
		const auto silent = session->api().contactSignupSilentCurrent();
		return (enabled == silent.value_or(false));
	}) | rpl::start_with_next([=](bool enabled) {
		session->api().saveContactSignupSilent(!enabled);
	}, joined->lifetime());

	const auto pinned = addCheckbox(
		tr::lng_settings_events_pinned(tr::now),
		settings.notifyAboutPinned());
	settings.notifyAboutPinnedChanges(
	) | rpl::start_with_next([=](bool notify) {
		pinned->setChecked(notify);
	}, pinned->lifetime());
	pinned->checkedChanges(
	) | rpl::filter([=](bool notify) {
		return (notify != Core::App().settings().notifyAboutPinned());
	}) | rpl::start_with_next([=](bool notify) {
		Core::App().settings().setNotifyAboutPinned(notify);
		Core::App().saveSettingsDelayed();
	}, joined->lifetime());

	AddSkip(container, st::settingsCheckboxesSkip);
	AddDivider(container);
	AddSkip(container, st::settingsCheckboxesSkip);
	AddSubsectionTitle(
		container,
		tr::lng_settings_notifications_calls_title());
	addCheckbox(
		tr::lng_settings_call_accept_calls(tr::now),
		!settings.disableCalls()
	)->checkedChanges(
	) | rpl::filter([&settings](bool value) {
		return (settings.disableCalls() == value);
	}) | rpl::start_with_next([=](bool value) {
		Core::App().settings().setDisableCalls(!value);
		Core::App().saveSettingsDelayed();
	}, container->lifetime());

	AddSkip(container, st::settingsCheckboxesSkip);
	AddDivider(container);
	AddSkip(container, st::settingsCheckboxesSkip);
	AddSubsectionTitle(container, tr::lng_settings_badge_title());

	const auto muted = addCheckbox(
		tr::lng_settings_include_muted(tr::now),
		settings.includeMutedCounter());
	const auto count = addCheckbox(
		tr::lng_settings_count_unread(tr::now),
		settings.countUnreadMessages());

	const auto nativeText = [&] {
		if (!Platform::Notifications::Supported()
			|| Platform::Notifications::Enforced()) {
			return QString();
		} else if (Platform::IsWindows()) {
			return tr::lng_settings_use_windows(tr::now);
		}
		return tr::lng_settings_use_native_notifications(tr::now);
	}();
	const auto native = [&]() -> Ui::Checkbox* {
		if (nativeText.isEmpty()) {
			return nullptr;
		}

		AddSkip(container, st::settingsCheckboxesSkip);
		AddDivider(container);
		AddSkip(container, st::settingsCheckboxesSkip);
		AddSubsectionTitle(container, tr::lng_settings_native_title());
		return addCheckbox(nativeText, settings.nativeNotifications());
	}();

	const auto advancedSlide = !Platform::Notifications::Enforced()
		? container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)))
		: nullptr;
	const auto advancedWrap = advancedSlide
		? advancedSlide->entity()
		: nullptr;
	if (advancedWrap) {
		SetupAdvancedNotifications(controller, advancedWrap);
	}

	if (!name->entity()->checked()) {
		preview->hide(anim::type::instant);
	}
	if (!desktop->checked()) {
		name->hide(anim::type::instant);
		preview->hide(anim::type::instant);
	}
	if (native && advancedSlide && settings.nativeNotifications()) {
		advancedSlide->hide(anim::type::instant);
	}

	using Change = Window::Notifications::ChangeType;
	const auto changed = [=](Change change) {
		Core::App().saveSettingsDelayed();
		Core::App().notifications().notifySettingsChanged(change);
	};
	desktop->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().desktopNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setDesktopNotify(checked);
		changed(Change::DesktopEnabled);
	}, desktop->lifetime());

	name->entity()->checkedChanges(
	) | rpl::map([=](bool checked) {
		if (!checked) {
			return NotifyView::ShowNothing;
		} else if (!preview->entity()->checked()) {
			return NotifyView::ShowName;
		}
		return NotifyView::ShowPreview;
	}) | rpl::filter([=](NotifyView value) {
		return (value != Core::App().settings().notifyView());
	}) | rpl::start_with_next([=](NotifyView value) {
		Core::App().settings().setNotifyView(value);
		changed(Change::ViewParams);
	}, name->lifetime());

	preview->entity()->checkedChanges(
	) | rpl::map([=](bool checked) {
		if (checked) {
			return NotifyView::ShowPreview;
		} else if (name->entity()->checked()) {
			return NotifyView::ShowName;
		}
		return NotifyView::ShowNothing;
	}) | rpl::filter([=](NotifyView value) {
		return (value != Core::App().settings().notifyView());
	}) | rpl::start_with_next([=](NotifyView value) {
		Core::App().settings().setNotifyView(value);
		changed(Change::ViewParams);
	}, preview->lifetime());

	sound->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().soundNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setSoundNotify(checked);
		changed(Change::SoundEnabled);
	}, sound->lifetime());

	flashbounce->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().flashBounceNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setFlashBounceNotify(checked);
		changed(Change::FlashBounceEnabled);
	}, flashbounce->lifetime());

	muted->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != Core::App().settings().includeMutedCounter());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setIncludeMutedCounter(checked);
		changed(Change::IncludeMuted);
	}, muted->lifetime());

	count->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != Core::App().settings().countUnreadMessages());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setCountUnreadMessages(checked);
		changed(Change::CountMessages);
	}, count->lifetime());

	Core::App().notifications().settingsChanged(
	) | rpl::start_with_next([=](Change change) {
		if (change == Change::DesktopEnabled) {
			desktop->setChecked(Core::App().settings().desktopNotify());
			name->toggle(
				Core::App().settings().desktopNotify(),
				anim::type::normal);
			preview->toggle(
				(Core::App().settings().desktopNotify()
					&& name->entity()->checked()),
				anim::type::normal);
		} else if (change == Change::ViewParams) {
			preview->toggle(name->entity()->checked(), anim::type::normal);
		} else if (change == Change::SoundEnabled) {
			sound->setChecked(Core::App().settings().soundNotify());
		} else if (change == Change::FlashBounceEnabled) {
			flashbounce->setChecked(Core::App().settings().flashBounceNotify());
		}
	}, desktop->lifetime());

	if (native) {
		native->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != Core::App().settings().nativeNotifications());
		}) | rpl::start_with_next([=](bool checked) {
			Core::App().settings().setNativeNotifications(checked);
			Core::App().saveSettingsDelayed();
			Core::App().notifications().createManager();

			if (advancedSlide) {
				advancedSlide->toggle(
					!Core::App().settings().nativeNotifications(),
					anim::type::normal);
			}
		}, native->lifetime());
	}
}

void SetupNotifications(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsCheckboxesSkip);

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	SetupNotificationsContent(controller, wrap.data());

	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap)));

	AddSkip(container, st::settingsCheckboxesSkip);
}

} // namespace

Notifications::Notifications(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void Notifications::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupNotifications(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
