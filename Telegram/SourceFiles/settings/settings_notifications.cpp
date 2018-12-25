/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_notifications.h"

#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_button.h"
#include "storage/localstorage.h"
#include "window/notifications_manager.h"
#include "platform/platform_notifications_manager.h"
#include "mainwindow.h"
#include "messenger.h"
#include "auth_session.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"

namespace Settings {
namespace {

constexpr auto kMaxNotificationsCount = 5;

int CurrentCount() {
	return snap(Global::NotificationsCount(), 1, kMaxNotificationsCount);
}

using ChangeType = Window::Notifications::ChangeType;

class NotificationsCount : public Ui::RpWidget {
public:
	NotificationsCount(QWidget *parent);

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
	using ScreenCorner = Notify::ScreenCorner;
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

	QPixmap _notificationSampleUserpic;
	QPixmap _notificationSampleSmall;
	QPixmap _notificationSampleLarge;
	ScreenCorner _chosenCorner;
	std::vector<Animation> _sampleOpacities;

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
	Animation _opacity;
	bool _hiding = false;
	bool _deleted = false;

};

NotificationsCount::NotificationsCount(QWidget *parent)
: _chosenCorner(Global::NotificationsCorner())
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
		auto screenCorner = static_cast<Notify::ScreenCorner>(corner);
		auto isLeft = Notify::IsLeftCorner(screenCorner);
		auto isTop = Notify::IsTopCorner(screenCorner);
		auto sampleLeft = isLeft ? (screenRect.x() + st::notificationsSampleSkip) : (screenRect.x() + screenRect.width() - st::notificationsSampleSkip - st::notificationSampleSize.width());
		auto sampleTop = isTop ? (screenRect.y() + st::notificationsSampleTopSkip) : (screenRect.y() + screenRect.height() - st::notificationsSampleBottomSkip - st::notificationSampleSize.height());
		if (corner == static_cast<int>(_chosenCorner)) {
			auto count = _oldCount;
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

	if (count != Global::NotificationsCount()) {
		Global::SetNotificationsCount(count);
		Auth().notifications().settingsChanged().notify(ChangeType::MaxCount);
		Local::writeUserSettings();
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

void NotificationsCount::prepareNotificationSampleUserpic() {
	if (_notificationSampleUserpic.isNull()) {
		_notificationSampleUserpic = App::pixmapFromImageInPlace(
			Messenger::Instance().logoNoMargin().scaled(
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

void NotificationsCount::leaveEventHook(QEvent *e) {
	clearOverCorner();
}

void NotificationsCount::setOverCorner(Notify::ScreenCorner corner) {
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
	auto samplesNeeded = _oldCount;
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

void NotificationsCount::clearOverCorner() {
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

void NotificationsCount::mousePressEvent(QMouseEvent *e) {
	_isDownCorner = _isOverCorner;
	_downCorner = _overCorner;
}

void NotificationsCount::mouseReleaseEvent(QMouseEvent *e) {
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
: QWidget(nullptr)
, _owner(owner)
, _cache(cache) {
	resize(
		cache.width() / cache.devicePixelRatio(),
		cache.height() / cache.devicePixelRatio());

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
	setWindowOpacity(_opacity.current(_hiding ? 0. : 1.));
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
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	QTimer::singleShot(1000, [this] { delete this; });
#else // Q_OS_LINUX32 || Q_OS_LINUX64
	deleteLater();
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void SetupAdvancedNotifications(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsCheckboxesSkip);
	AddDivider(container);
	AddSkip(container, st::settingsCheckboxesSkip);
	AddSubsectionTitle(container, lng_settings_notifications_position);
	AddSkip(container, st::settingsCheckboxesSkip);

	const auto position = container->add(
		object_ptr<NotificationsCount>(container));

	AddSkip(container, st::settingsCheckboxesSkip);
	AddSubsectionTitle(container, lng_settings_notifications_count);

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

void SetupNotificationsContent(not_null<Ui::VerticalLayout*> container) {
	const auto checkbox = [&](LangKey label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			lang(label),
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](LangKey label, bool checked) {
		return container->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding);
	};
	const auto addSlidingCheckbox = [&](LangKey label, bool checked) {
		return container->add(
			object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
				container,
				checkbox(label, checked),
				st::settingsCheckboxPadding));
	};
	const auto desktop = addCheckbox(
		lng_settings_desktop_notify,
		Global::DesktopNotify());
	const auto name = addSlidingCheckbox(
		lng_settings_show_name,
		(Global::NotifyView() <= dbinvShowName));
	const auto preview = addSlidingCheckbox(
		lng_settings_show_preview,
		(Global::NotifyView() <= dbinvShowPreview));
	const auto sound = addCheckbox(
		lng_settings_sound_notify,
		Global::SoundNotify());
	const auto muted = addCheckbox(
		lng_settings_include_muted,
		Auth().settings().includeMutedCounter());
	const auto count = addCheckbox(
		lng_settings_count_unread,
		Auth().settings().countUnreadMessages());

	const auto nativeNotificationsKey = [&] {
		if (!Platform::Notifications::Supported()) {
			return LangKey();
		} else if (cPlatform() == dbipWindows) {
			return lng_settings_use_windows;
		} else if (cPlatform() == dbipLinux32
			|| cPlatform() == dbipLinux64) {
			return lng_settings_use_native_notifications;
		}
		return LangKey();
	}();
	const auto native = nativeNotificationsKey
		? addCheckbox(nativeNotificationsKey, Global::NativeNotifications())
		: nullptr;

	const auto advancedSlide = (cPlatform() != dbipMac)
		? container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)))
		: nullptr;
	const auto advancedWrap = advancedSlide
		? advancedSlide->entity()
		: nullptr;
	if (advancedWrap) {
		SetupAdvancedNotifications(advancedWrap);
	}

	if (!name->entity()->checked()) {
		preview->hide(anim::type::instant);
	}
	if (!desktop->checked()) {
		name->hide(anim::type::instant);
		preview->hide(anim::type::instant);
	}
	if (native && advancedSlide && Global::NativeNotifications()) {
		advancedSlide->hide(anim::type::instant);
	}

	using Change = Window::Notifications::ChangeType;
	const auto changed = [](Change change) {
		Local::writeUserSettings();
		Auth().notifications().settingsChanged().notify(change);
	};
	desktop->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Global::DesktopNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Global::SetDesktopNotify(checked);
		changed(Change::DesktopEnabled);
	}, desktop->lifetime());

	name->entity()->checkedChanges(
	) | rpl::map([=](bool checked) {
		if (!checked) {
			return dbinvShowNothing;
		} else if (!preview->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowPreview;
	}) | rpl::filter([=](DBINotifyView value) {
		return (value != Global::NotifyView());
	}) | rpl::start_with_next([=](DBINotifyView value) {
		Global::SetNotifyView(value);
		changed(Change::ViewParams);
	}, name->lifetime());

	preview->entity()->checkedChanges(
	) | rpl::map([=](bool checked) {
		if (checked) {
			return dbinvShowPreview;
		} else if (name->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowNothing;
	}) | rpl::filter([=](DBINotifyView value) {
		return (value != Global::NotifyView());
	}) | rpl::start_with_next([=](DBINotifyView value) {
		Global::SetNotifyView(value);
		changed(Change::ViewParams);
	}, preview->lifetime());

	sound->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Global::SoundNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Global::SetSoundNotify(checked);
		changed(Change::SoundEnabled);
	}, sound->lifetime());

	muted->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Auth().settings().includeMutedCounter());
	}) | rpl::start_with_next([=](bool checked) {
		Auth().settings().setIncludeMutedCounter(checked);
		changed(Change::IncludeMuted);
	}, muted->lifetime());

	count->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Auth().settings().countUnreadMessages());
	}) | rpl::start_with_next([=](bool checked) {
		Auth().settings().setCountUnreadMessages(checked);
		changed(Change::CountMessages);
	}, count->lifetime());

	base::ObservableViewer(
		Auth().notifications().settingsChanged()
	) | rpl::start_with_next([=](Change change) {
		if (change == Change::DesktopEnabled) {
			desktop->setChecked(Global::DesktopNotify());
			name->toggle(Global::DesktopNotify(), anim::type::normal);
			preview->toggle(
				Global::DesktopNotify() && name->entity()->checked(),
				anim::type::normal);
		} else if (change == Change::ViewParams) {
			preview->toggle(name->entity()->checked(), anim::type::normal);
		} else if (change == Change::SoundEnabled) {
			sound->setChecked(Global::SoundNotify());
		}
	}, desktop->lifetime());

	if (native) {
		native->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != Global::NativeNotifications());
		}) | rpl::start_with_next([=](bool checked) {
			Global::SetNativeNotifications(checked);
			Local::writeUserSettings();

			Auth().notifications().createManager();

			if (advancedSlide) {
				advancedSlide->toggle(
					!Global::NativeNotifications(),
					anim::type::normal);
			}
		}, native->lifetime());
	}
}

void SetupNotifications(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsCheckboxesSkip);

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	SetupNotificationsContent(wrap.data());

	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap)));

	AddSkip(container, st::settingsCheckboxesSkip);
}

} // namespace

Notifications::Notifications(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

void Notifications::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupNotifications(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
