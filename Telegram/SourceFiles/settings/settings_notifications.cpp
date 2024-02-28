/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_notifications.h"

#include "settings/settings_notifications_type.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/chat_service_checkbox.h"
#include "ui/effects/animations.h"
#include "ui/text/text_utilities.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "window/section_widget.h"
#include "platform/platform_specific.h"
#include "platform/platform_notifications_manager.h"
#include "base/platform/base_platform_info.h"
#include "base/call_delayed.h"
#include "mainwindow.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "api/api_authorizations.h"
#include "api/api_ringtones.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/notify/data_notify_settings.h"
#include "boxes/ringtones_box.h"
#include "apiwrap.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"

#include <QSvgRenderer>

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

	std::vector<SampleWidget*> _cornerSamples[4];

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

void AddTypeButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Data::DefaultNotify type,
		Fn<void(Type)> showOther) {
	using Type = Data::DefaultNotify;
	auto label = [&] {
		switch (type) {
		case Type::User: return tr::lng_notification_private_chats();
		case Type::Group: return tr::lng_notification_groups();
		case Type::Broadcast: return tr::lng_notification_channels();
		}
		Unexpected("Type value in AddTypeButton.");
	}();
	const auto icon = [&] {
		switch (type) {
		case Type::User: return &st::menuIconProfile;
		case Type::Group: return &st::menuIconGroups;
		case Type::Broadcast: return &st::menuIconChannel;
		}
		Unexpected("Type value in AddTypeButton.");
	}();
	const auto button = AddButtonWithIcon(
		container,
		std::move(label),
		st::settingsNotificationType,
		{ icon });
	button->setClickedCallback([=] {
		showOther(NotificationsType::Id(type));
	});

	const auto session = &controller->session();
	const auto settings = &session->data().notifySettings();
	const auto &st = st::settingsNotificationType;
	auto status = rpl::combine(
		NotificationsEnabledForTypeValue(session, type),
		rpl::single(
			type
		) | rpl::then(settings->exceptionsUpdates(
		) | rpl::filter(rpl::mappers::_1 == type))
	) | rpl::map([=](bool enabled, const auto &) {
		const auto count = int(settings->exceptions(type).size());
		return !count
			? tr::lng_notification_click_to_change()
			: (enabled
				? tr::lng_notification_on
				: tr::lng_notification_off)(
					lt_exceptions,
					tr::lng_notification_exceptions(
						lt_count,
						rpl::single(float64(count))));
	}) | rpl::flatten_latest();
	const auto details = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		std::move(status),
		st::settingsNotificationTypeDetails);
	details->show();
	details->moveToLeft(
		st.padding.left(),
		st.padding.top() + st.height - details->height());
	details->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto toggleButton = Ui::CreateChild<Ui::SettingsButton>(
		container.get(),
		nullptr,
		st);
	const auto checkView = button->lifetime().make_state<Ui::ToggleView>(
		st.toggle,
		NotificationsEnabledForType(session, type),
		[=] { toggleButton->update(); });

	const auto separator = Ui::CreateChild<Ui::RpWidget>(container.get());
	separator->paintRequest(
	) | rpl::start_with_next([=, bg = st.textBgOver] {
		auto p = QPainter(separator);
		p.fillRect(separator->rect(), bg);
	}, separator->lifetime());
	const auto separatorHeight = st.height - 2 * st.toggle.border;
	button->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		const auto w = st::rightsButtonToggleWidth;
		toggleButton->setGeometry(
			r.x() + r.width() - w,
			r.y(),
			w,
			r.height());
		separator->setGeometry(
			toggleButton->x() - st::lineWidth,
			r.y() + (r.height() - separatorHeight) / 2,
			st::lineWidth,
			separatorHeight);
	}, toggleButton->lifetime());

	const auto checkWidget = Ui::CreateChild<Ui::RpWidget>(toggleButton);
	checkWidget->resize(checkView->getSize());
	checkWidget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(checkWidget);
		checkView->paint(p, 0, 0, checkWidget->width());
	}, checkWidget->lifetime());
	toggleButton->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		checkWidget->moveToRight(
			st.toggleSkip,
			(s.height() - checkWidget->height()) / 2);
	}, toggleButton->lifetime());

	const auto toggle = crl::guard(toggleButton, [=] {
		const auto enabled = !checkView->checked();
		checkView->setChecked(enabled, anim::type::normal);
		settings->defaultUpdate(type, Data::MuteValue{
			.unmute = enabled,
			.forever = !enabled,
		});
	});
	toggleButton->clicks(
	) | rpl::start_with_next([=] {
		const auto count = int(settings->exceptions(type).size());
		if (!count) {
			toggle();
		} else {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				const auto phrase = [&] {
					switch (type) {
					case Type::User:
						return tr::lng_notification_about_private_chats;
					case Type::Group:
						return tr::lng_notification_about_groups;
					case Type::Broadcast:
						return tr::lng_notification_about_channels;
					}
					Unexpected("Type in AddTypeButton.");
				}();
				Ui::ConfirmBox(box, {
					.text = phrase(
						lt_count,
						rpl::single(float64(count)),
						Ui::Text::RichLangValue),
					.confirmed = [=](auto close) { toggle(); close(); },
					.confirmText = tr::lng_box_ok(),
					.title = tr::lng_notification_exceptions_title(),
					.inform = true,
				});
				box->addLeftButton(
					tr::lng_notification_exceptions_view(),
					[=] {
						box->closeBox();
						showOther(NotificationsType::Id(type));
					});
			}));
		}
	}, toggleButton->lifetime());
}

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
			Window::LogoNoMargin().scaled(
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

		auto notifyTitle = st::msgNameFont->elided(u"Telegram Desktop"_q, rectForName.width());
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
		const auto index = static_cast<int>(_overCorner);
		for (const auto widget : _cornerSamples[index]) {
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
	auto samplesAlready = int(samples.size());
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

		for (const auto &samples : _cornerSamples) {
			for (const auto widget : samples) {
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
	for (const auto &samples : _cornerSamples) {
		for (const auto widget : samples) {
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
	setFixedSize(
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
	if constexpr (Platform::IsLinux()) {
		base::call_delayed(1000, this, [this] { delete this; });
	} else {
		deleteLater();
	}
}

class NotifyPreview final {
public:
	NotifyPreview(bool nameShown, bool previewShown);

	void setNameShown(bool shown);
	void setPreviewShown(bool shown);

	int resizeGetHeight(int newWidth);
	void paint(Painter &p, int x, int y);

private:
	int _width = 0;
	int _height = 0;
	bool _nameShown = false;
	bool _previewShown = false;
	Ui::RoundRect _roundRect;
	Ui::Text::String _name, _title;
	Ui::Text::String _text, _preview;
	QSvgRenderer _userpic;
	QImage _logo;

};

NotifyPreview::NotifyPreview(bool nameShown, bool previewShown)
: _nameShown(nameShown)
, _previewShown(previewShown)
, _roundRect(st::boxRadius, st::msgInBg)
, _userpic(u":/gui/icons/settings/dino.svg"_q)
, _logo(Window::LogoNoMargin()) {
	const auto ratio = style::DevicePixelRatio();
	_logo = _logo.scaledToWidth(
		st::notifyPreviewUserpicSize * ratio,
		Qt::SmoothTransformation);
	_logo.setDevicePixelRatio(ratio);

	_name.setText(
		st::defaultSubsectionTitle.style,
		tr::lng_notification_preview_title(tr::now));
	_title.setText(st::defaultSubsectionTitle.style, AppName.utf16());

	_text.setText(
		st::boxTextStyle,
		tr::lng_notification_preview_text(tr::now));
	_preview.setText(
		st::boxTextStyle,
		tr::lng_notification_preview(tr::now));
}

void NotifyPreview::setNameShown(bool shown) {
	_nameShown = shown;
}

void NotifyPreview::setPreviewShown(bool shown) {
	_previewShown = shown;
}

int NotifyPreview::resizeGetHeight(int newWidth) {
	_width = newWidth;
	_height = st::notifyPreviewUserpicPosition.y()
		+ st::notifyPreviewUserpicSize
		+ st::notifyPreviewUserpicPosition.y();
	const auto available = _width
		- st::notifyPreviewTextPosition.x()
		- st::notifyPreviewUserpicPosition.x();
	if (std::max(_text.maxWidth(), _preview.maxWidth()) >= available) {
		_height += st::defaultTextStyle.font->height;
	}
	return _height;
}

void NotifyPreview::paint(Painter &p, int x, int y) {
	if (!_width || !_height) {
		return;
	}
	p.translate(x, y);
	const auto guard = gsl::finally([&] { p.translate(-x, -y); });

	_roundRect.paint(p, { 0, 0, _width, _height });
	const auto userpic = QRect(
		st::notifyPreviewUserpicPosition,
		QSize{ st::notifyPreviewUserpicSize, st::notifyPreviewUserpicSize });

	if (_nameShown) {
		_userpic.render(&p, QRectF(userpic));
	} else {
		p.drawImage(userpic.topLeft(), _logo);
	}

	p.setPen(st::historyTextInFg);

	const auto &title = _nameShown ? _name : _title;
	title.drawElided(
		p,
		st::notifyPreviewTitlePosition.x(),
		st::notifyPreviewTitlePosition.y(),
		_width - st::notifyPreviewTitlePosition.x() - userpic.x());

	const auto &text = _previewShown ? _text : _preview;
	text.drawElided(
		p,
		st::notifyPreviewTextPosition.x(),
		st::notifyPreviewTextPosition.y(),
		_width - st::notifyPreviewTextPosition.x() - userpic.x(),
		2);
}

struct NotifyViewCheckboxes {
	not_null<Ui::SlideWrap<>*> wrap;
	not_null<Ui::Checkbox*> name;
	not_null<Ui::Checkbox*> preview;
};

NotifyViewCheckboxes SetupNotifyViewOptions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		bool nameShown,
		bool previewShown) {
	using namespace rpl::mappers;

	auto wrap = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::RpWidget>(container)));
	const auto widget = wrap->entity();

	const auto makeCheckbox = [&](const QString &text, bool checked) {
		return Ui::MakeChatServiceCheckbox(
			widget,
			text,
			st::backgroundCheckbox,
			st::backgroundCheck,
			checked).release();
	};
	const auto name = makeCheckbox(
		tr::lng_notification_show_name(tr::now),
		nameShown);
	const auto preview = makeCheckbox(
		tr::lng_notification_show_text(tr::now),
		previewShown);

	const auto view = widget->lifetime().make_state<NotifyPreview>(
		nameShown,
		previewShown);
	widget->widthValue(
	) | rpl::filter(
		_1 >= (st::historyMinimalWidth / 2)
	) | rpl::start_with_next([=](int width) {
		const auto margins = st::notifyPreviewMargins;
		const auto bubblew = width - margins.left() - margins.right();
		const auto bubbleh = view->resizeGetHeight(bubblew);
		const auto height = bubbleh + margins.top() + margins.bottom();
		widget->resize(width, height);

		const auto skip = st::notifyPreviewChecksSkip;
		const auto checksWidth = name->width() + skip + preview->width();
		const auto checksLeft = (width - checksWidth) / 2;
		const auto checksTop = height
			- (margins.bottom() + name->height()) / 2;
		name->move(checksLeft, checksTop);
		preview->move(checksLeft + name->width() + skip, checksTop);
	}, widget->lifetime());

	widget->paintRequest(
	) | rpl::start_with_next([=](QRect rect) {
		Window::SectionWidget::PaintBackground(
			controller,
			controller->defaultChatTheme().get(), // #TODO themes
			widget,
			rect);

		Painter p(widget);
		view->paint(
			p,
			st::notifyPreviewMargins.left(),
			st::notifyPreviewMargins.top());
	}, widget->lifetime());

	name->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		view->setNameShown(checked);
		widget->update();
	}, name->lifetime());

	preview->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		view->setPreviewShown(checked);
		widget->update();
	}, preview->lifetime());

	return {
		.wrap = wrap,
		.name = name,
		.preview = preview,
	};
}

void SetupAdvancedNotifications(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddDivider(container);
	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddSubsectionTitle(
		container,
		tr::lng_settings_notifications_position());
	Ui::AddSkip(container, st::settingsCheckboxesSkip);

	const auto position = container->add(
		object_ptr<NotificationsCount>(container, controller));

	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddSubsectionTitle(container, tr::lng_settings_notifications_count());

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
	Ui::AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupMultiAccountNotifications(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	if (Core::App().domain().accounts().size() < 2) {
		return;
	}
	Ui::AddSubsectionTitle(container, tr::lng_settings_show_from());

	const auto fromAll = container->add(object_ptr<Button>(
		container,
		tr::lng_settings_notify_all(),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(Core::App().settings().notifyFromAll()));
	fromAll->toggledChanges(
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

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_settings_notify_all_about());
	Ui::AddSkip(container);
}

void SetupNotificationsContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	using namespace rpl::mappers;

	Ui::AddSkip(container, st::settingsPrivacySkip);

	using NotifyView = Core::Settings::NotifyView;
	SetupMultiAccountNotifications(controller, container);

	AddSubsectionTitle(container, tr::lng_settings_notify_global());

	const auto session = &controller->session();
	const auto checkbox = [&](
			rpl::producer<QString> label,
			IconDescriptor &&descriptor,
			rpl::producer<bool> checked) {
		auto result = CreateButtonWithIcon(
			container,
			std::move(label),
			st::settingsButton,
			std::move(descriptor)
		);
		result->toggleOn(std::move(checked));
		return result;
	};
	const auto addCheckbox = [&](
			rpl::producer<QString> label,
			IconDescriptor &&descriptor,
			rpl::producer<bool> checked) {
		return container->add(
			checkbox(
				std::move(label),
				std::move(descriptor),
				std::move(checked)));
	};
	const auto &settings = Core::App().settings();
	const auto desktopToggles = container->lifetime(
	).make_state<rpl::event_stream<bool>>();
	const auto desktop = addCheckbox(
		tr::lng_settings_desktop_notify(),
		{ &st::menuIconNotifications },
		desktopToggles->events_starting_with(settings.desktopNotify()));

	const auto flashbounceToggles = container->lifetime(
	).make_state<rpl::event_stream<bool>>();
	const auto flashbounce = addCheckbox(
		(Platform::IsWindows()
			? tr::lng_settings_alert_windows
			: Platform::IsMac()
			? tr::lng_settings_alert_mac
			: tr::lng_settings_alert_linux)(),
		{ &st::menuIconDockBounce },
		flashbounceToggles->events_starting_with(
			settings.flashBounceNotify()));

	const auto soundAllowed = container->lifetime(
	).make_state<rpl::event_stream<bool>>();
	const auto allowed = [=] {
		return Core::App().settings().soundNotify();
	};
	const auto sound = addCheckbox(
		tr::lng_settings_sound_allowed(),
		{ &st::menuIconUnmute },
		soundAllowed->events_starting_with(allowed()));

	Ui::AddSkip(container);

	const auto checkboxes = SetupNotifyViewOptions(
		controller,
		container,
		(settings.notifyView() <= NotifyView::ShowName),
		(settings.notifyView() <= NotifyView::ShowPreview));
	const auto name = checkboxes.name;
	const auto preview = checkboxes.preview;
	const auto previewWrap = checkboxes.wrap;
	const auto previewDivider = container->add(
		object_ptr<Ui::SlideWrap<Ui::BoxContentDivider>>(
			container,
			object_ptr<Ui::BoxContentDivider>(container)));
	previewWrap->toggle(settings.desktopNotify(), anim::type::instant);
	previewDivider->toggle(!settings.desktopNotify(), anim::type::instant);

	controller->session().data().notifySettings().loadExceptions();

	Ui::AddSkip(container, st::notifyPreviewBottomSkip);
	Ui::AddSubsectionTitle(container, tr::lng_settings_notify_title());
	const auto addType = [&](Data::DefaultNotify type) {
		AddTypeButton(container, controller, type, showOther);
	};
	addType(Data::DefaultNotify::User);
	addType(Data::DefaultNotify::Group);
	addType(Data::DefaultNotify::Broadcast);

	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddDivider(container);
	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddSubsectionTitle(container, tr::lng_settings_events_title());

	auto joinSilent = rpl::single(
		session->api().contactSignupSilentCurrent().value_or(false)
	) | rpl::then(session->api().contactSignupSilent());
	const auto joined = addCheckbox(
		tr::lng_settings_events_joined(),
		{ &st::menuIconInvite },
		std::move(joinSilent) | rpl::map(!_1));
	joined->toggledChanges(
	) | rpl::filter([=](bool enabled) {
		const auto silent = session->api().contactSignupSilentCurrent();
		return (enabled == silent.value_or(false));
	}) | rpl::start_with_next([=](bool enabled) {
		session->api().saveContactSignupSilent(!enabled);
	}, joined->lifetime());

	const auto pinned = addCheckbox(
		tr::lng_settings_events_pinned(),
		{ &st::menuIconPin },
		rpl::single(
			settings.notifyAboutPinned()
		) | rpl::then(settings.notifyAboutPinnedChanges()));
	pinned->toggledChanges(
	) | rpl::filter([=](bool notify) {
		return (notify != Core::App().settings().notifyAboutPinned());
	}) | rpl::start_with_next([=](bool notify) {
		Core::App().settings().setNotifyAboutPinned(notify);
		Core::App().saveSettingsDelayed();
	}, joined->lifetime());

	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddDivider(container);
	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddSubsectionTitle(
		container,
		tr::lng_settings_notifications_calls_title());
	const auto authorizations = &session->api().authorizations();
	const auto acceptCalls = addCheckbox(
		tr::lng_settings_call_accept_calls(),
		{ &st::menuIconCallsReceive },
		authorizations->callsDisabledHereValue() | rpl::map(!_1));
	acceptCalls->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return (toggled == authorizations->callsDisabledHere());
	}) | rpl::start_with_next([=](bool toggled) {
		authorizations->toggleCallsDisabledHere(!toggled);
	}, container->lifetime());

	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddDivider(container);
	Ui::AddSkip(container, st::settingsCheckboxesSkip);
	Ui::AddSubsectionTitle(container, tr::lng_settings_badge_title());

	const auto muted = container->add(object_ptr<Button>(
		container,
		tr::lng_settings_include_muted(),
		st::settingsButtonNoIcon));
	muted->toggleOn(rpl::single(settings.includeMutedCounter()));
	const auto count = container->add(object_ptr<Button>(
		container,
		tr::lng_settings_count_unread(),
		st::settingsButtonNoIcon));
	count->toggleOn(rpl::single(settings.countUnreadMessages()));

	auto nativeText = [&] {
		if (!Platform::Notifications::Supported()
			|| Platform::Notifications::Enforced()) {
			return rpl::producer<QString>();
		} else if (Platform::IsWindows()) {
			return tr::lng_settings_use_windows();
		}
		return tr::lng_settings_use_native_notifications();
	}();
	const auto native = [&]() -> Ui::SettingsButton* {
		if (!nativeText) {
			return nullptr;
		}

		Ui::AddSkip(container, st::settingsCheckboxesSkip);
		Ui::AddDivider(container);
		Ui::AddSkip(container, st::settingsCheckboxesSkip);
		Ui::AddSubsectionTitle(container, tr::lng_settings_native_title());
		return container->add(object_ptr<Button>(
			container,
			std::move(nativeText),
			st::settingsButtonNoIcon
		))->toggleOn(rpl::single(settings.nativeNotifications()));
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

	if (native && advancedSlide && settings.nativeNotifications()) {
		advancedSlide->hide(anim::type::instant);
	}

	using Change = Window::Notifications::ChangeType;
	const auto changed = [=](Change change) {
		Core::App().saveSettingsDelayed();
		Core::App().notifications().notifySettingsChanged(change);
	};
	desktop->toggledChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().desktopNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setDesktopNotify(checked);
		changed(Change::DesktopEnabled);
	}, desktop->lifetime());

	sound->toggledChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().soundNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setSoundNotify(checked);
		changed(Change::SoundEnabled);
	}, sound->lifetime());

	name->checkedChanges(
	) | rpl::map([=](bool checked) {
		if (!checked) {
			preview->setChecked(false);
			return NotifyView::ShowNothing;
		} else if (!preview->checked()) {
			return NotifyView::ShowName;
		}
		return NotifyView::ShowPreview;
	}) | rpl::filter([=](NotifyView value) {
		return (value != Core::App().settings().notifyView());
	}) | rpl::start_with_next([=](NotifyView value) {
		Core::App().settings().setNotifyView(value);
		changed(Change::ViewParams);
	}, name->lifetime());

	preview->checkedChanges(
	) | rpl::map([=](bool checked) {
		if (checked) {
			name->setChecked(true);
			return NotifyView::ShowPreview;
		} else if (name->checked()) {
			return NotifyView::ShowName;
		}
		return NotifyView::ShowNothing;
	}) | rpl::filter([=](NotifyView value) {
		return (value != Core::App().settings().notifyView());
	}) | rpl::start_with_next([=](NotifyView value) {
		Core::App().settings().setNotifyView(value);
		changed(Change::ViewParams);
	}, preview->lifetime());

	flashbounce->toggledChanges(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().flashBounceNotify());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setFlashBounceNotify(checked);
		changed(Change::FlashBounceEnabled);
	}, flashbounce->lifetime());

	muted->toggledChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != Core::App().settings().includeMutedCounter());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setIncludeMutedCounter(checked);
		changed(Change::IncludeMuted);
	}, muted->lifetime());

	count->toggledChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != Core::App().settings().countUnreadMessages());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setCountUnreadMessages(checked);
		changed(Change::CountMessages);
	}, count->lifetime());

	Core::App().notifications().settingsChanged(
	) | rpl::start_with_next([=](Change change) {
		if (change == Change::DesktopEnabled) {
			desktopToggles->fire(Core::App().settings().desktopNotify());
			previewWrap->toggle(
				Core::App().settings().desktopNotify(),
				anim::type::normal);
			previewDivider->toggle(
				!Core::App().settings().desktopNotify(),
				anim::type::normal);
		} else if (change == Change::ViewParams) {
			//
		} else if (change == Change::SoundEnabled) {
			soundAllowed->fire(allowed());
		} else if (change == Change::FlashBounceEnabled) {
			flashbounceToggles->fire(
				Core::App().settings().flashBounceNotify());
		}
	}, desktop->lifetime());

	if (native) {
		native->toggledChanges(
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
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	SetupNotificationsContent(controller, container, std::move(showOther));
}

} // namespace

Notifications::Notifications(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Notifications::title() {
	return tr::lng_settings_section_notify();
}

void Notifications::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupNotifications(controller, content, showOtherMethod());

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
