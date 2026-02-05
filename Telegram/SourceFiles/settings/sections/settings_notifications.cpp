/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_notifications.h"

#include "settings/settings_common_session.h"

#include "api/api_authorizations.h"
#include "api/api_ringtones.h"
#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "boxes/ringtones_box.h"
#include "core/application.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "data/notify/data_peer_notify_volume.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "platform/platform_notifications_manager.h"
#include "platform/platform_specific.h"
#include "settings/settings_builder.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_notifications_common.h"
#include "settings/sections/settings_notifications_type.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/chat_service_checkbox.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/notifications_manager.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QSvgRenderer>

namespace Settings {

using ChangeType = Window::Notifications::ChangeType;

int CurrentNotificationsCount() {
	return std::clamp(
		Core::App().settings().notificationsCount(),
		1,
		kMaxNotificationsCount);
}

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

	NotificationsCount *_owner;
	QPixmap _cache;
	Ui::Animations::Simple _opacity;
	bool _hiding = false;

};

[[nodiscard]] not_null<Ui::SettingsButton*> AddTypeButton(
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
	) | rpl::on_next([=, bg = st.textBgOver] {
		auto p = QPainter(separator);
		p.fillRect(separator->rect(), bg);
	}, separator->lifetime());
	const auto separatorHeight = st.height - 2 * st.toggle.border;
	button->geometryValue(
	) | rpl::on_next([=](const QRect &r) {
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
	) | rpl::on_next([=] {
		auto p = QPainter(checkWidget);
		checkView->paint(p, 0, 0, checkWidget->width());
	}, checkWidget->lifetime());
	toggleButton->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
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
	) | rpl::on_next([=] {
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
						tr::rich),
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
	return button;
}

NotificationsCount::NotificationsCount(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _chosenCorner(Core::App().settings().notificationsCorner())
, _oldCount(CurrentNotificationsCount()) {
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
	auto sampleImage = QImage(
		QSize(width, height) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	sampleImage.setDevicePixelRatio(style::DevicePixelRatio());
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
	_notificationSampleSmall.setDevicePixelRatio(style::DevicePixelRatio());
}

void NotificationsCount::prepareNotificationSampleUserpic() {
	if (_notificationSampleUserpic.isNull()) {
		_notificationSampleUserpic = Ui::PixmapFromImage(
			Window::LogoNoMargin().scaled(
				st::notifyPhotoSize * style::DevicePixelRatio(),
				st::notifyPhotoSize * style::DevicePixelRatio(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation));
		_notificationSampleUserpic.setDevicePixelRatio(
			style::DevicePixelRatio());
	}
}

void NotificationsCount::prepareNotificationSampleLarge() {
	int w = st::notifyWidth, h = st::notifyMinHeight;
	auto sampleImage = QImage(
		w * style::DevicePixelRatio(),
		h * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	sampleImage.setDevicePixelRatio(style::DevicePixelRatio());
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
		const auto r = Window::Notifications::NotificationDisplayRect(
			&_controller->window());
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
	using ThemePtr = std::unique_ptr<Ui::ChatTheme>;
	const auto theme = widget->lifetime().make_state<ThemePtr>(
		Window::Theme::DefaultChatThemeOn(widget->lifetime()));
	widget->widthValue(
	) | rpl::filter(
		_1 >= (st::historyMinimalWidth / 2)
	) | rpl::on_next([=](int width) {
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
	) | rpl::on_next([=](QRect rect) {
		Painter p(widget);
		p.setClipRect(rect);
		Window::SectionWidget::PaintBackground(
			p,
			theme->get(),
			QSize(widget->width(), widget->window()->height()),
			rect);

		view->paint(
			p,
			st::notifyPreviewMargins.left(),
			st::notifyPreviewMargins.top());
	}, widget->lifetime());

	name->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		view->setNameShown(checked);
		widget->update();
	}, name->lifetime());

	preview->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		view->setPreviewShown(checked);
		widget->update();
	}, preview->lifetime());

	return {
		.wrap = wrap,
		.name = name,
		.preview = preview,
	};
}

namespace {

using namespace Builder;

constexpr auto kDefaultDisplayIndex = -1;

using NotifyView = Core::Settings::NotifyView;

void BuildMultiAccountSection(SectionBuilder &builder) {
	if (Core::App().domain().accounts().size() < 2) {
		return;
	}

	builder.addSubsectionTitle({
		.id = u"notifications/multi-account"_q,
		.title = tr::lng_settings_show_from(),
		.keywords = { u"accounts"_q, u"multiple"_q },
	});

	const auto fromAll = builder.addButton({
		.id = u"notifications/accounts"_q,
		.title = tr::lng_settings_notify_all(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(Core::App().settings().notifyFromAll()),
		.keywords = { u"all accounts"_q, u"multiple"_q },
	});

	if (fromAll) {
		fromAll->toggledChanges(
		) | rpl::filter([](bool checked) {
			return (checked != Core::App().settings().notifyFromAll());
		}) | rpl::on_next([=](bool checked) {
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
	}

	builder.addSkip();
	builder.addDividerText(tr::lng_settings_notify_all_about());
	builder.addSkip();
}

void BuildGlobalNotificationsSection(SectionBuilder &builder) {
	builder.addSubsectionTitle({
		.id = u"notifications/global"_q,
		.title = tr::lng_settings_notify_global(),
		.keywords = { u"global"_q, u"desktop"_q, u"sound"_q },
	});

	const auto container = builder.container();
	const auto session = builder.session();
	const auto &settings = Core::App().settings();

	const auto desktopToggles = container
		? container->lifetime().make_state<rpl::event_stream<bool>>()
		: nullptr;
	const auto desktop = builder.addButton({
		.id = u"notifications/desktop"_q,
		.title = tr::lng_settings_desktop_notify(),
		.icon = { &st::menuIconNotifications },
		.toggled = desktopToggles
			? desktopToggles->events_starting_with(settings.desktopNotify())
			: rpl::single(settings.desktopNotify()) | rpl::type_erased,
		.keywords = { u"desktop"_q, u"popup"_q, u"show"_q },
	});

	const auto flashbounceToggles = container
		? container->lifetime().make_state<rpl::event_stream<bool>>()
		: nullptr;
	const auto flashbounce = builder.addButton({
		.id = u"notifications/flash"_q,
		.title = (Platform::IsWindows()
			? tr::lng_settings_alert_windows
			: Platform::IsMac()
			? tr::lng_settings_alert_mac
			: tr::lng_settings_alert_linux)(),
		.icon = { &st::menuIconDockBounce },
		.toggled = flashbounceToggles
			? flashbounceToggles->events_starting_with(settings.flashBounceNotify())
			: rpl::single(settings.flashBounceNotify()) | rpl::type_erased,
		.keywords = { u"flash"_q, u"bounce"_q, u"taskbar"_q },
	});

	const auto soundAllowed = container
		? container->lifetime().make_state<rpl::event_stream<bool>>()
		: nullptr;
	const auto allowed = [=] {
		return Core::App().settings().soundNotify();
	};
	const auto sound = builder.addButton({
		.id = u"notifications/sound"_q,
		.title = tr::lng_settings_sound_allowed(),
		.icon = { &st::menuIconUnmute },
		.toggled = soundAllowed
			? soundAllowed->events_starting_with(allowed())
			: rpl::single(allowed()) | rpl::type_erased,
		.keywords = { u"sound"_q, u"audio"_q, u"mute"_q },
	});

	builder.add([session](const WidgetContext &ctx) {
		Ui::AddRingtonesVolumeSlider(
			ctx.container,
			rpl::single(true),
			tr::lng_settings_master_volume_notifications(),
			Data::VolumeController{
				.volume = []() -> ushort {
					const auto volume
						= Core::App().settings().notificationsVolume();
					return volume ? volume : 100;
				},
				.saveVolume = [=](ushort volume) {
					Core::App().notifications().playSound(
						session,
						0,
						volume / 100.);
					Core::App().settings().setNotificationsVolume(volume);
					Core::App().saveSettingsDelayed();
				}});
		return SectionBuilder::WidgetToAdd{};
	});

	builder.addSkip();

	if (desktop) {
		const auto changed = [=](ChangeType change) {
			Core::App().saveSettingsDelayed();
			Core::App().notifications().notifySettingsChanged(change);
		};

		desktop->toggledChanges(
		) | rpl::filter([](bool checked) {
			return (checked != Core::App().settings().desktopNotify());
		}) | rpl::on_next([=](bool checked) {
			Core::App().settings().setDesktopNotify(checked);
			changed(ChangeType::DesktopEnabled);
		}, desktop->lifetime());

		if (sound) {
			sound->toggledChanges(
			) | rpl::filter([](bool checked) {
				return (checked != Core::App().settings().soundNotify());
			}) | rpl::on_next([=](bool checked) {
				Core::App().settings().setSoundNotify(checked);
				changed(ChangeType::SoundEnabled);
			}, sound->lifetime());
		}

		if (flashbounce) {
			flashbounce->toggledChanges(
			) | rpl::filter([](bool checked) {
				return (checked != Core::App().settings().flashBounceNotify());
			}) | rpl::on_next([=](bool checked) {
				Core::App().settings().setFlashBounceNotify(checked);
				changed(ChangeType::FlashBounceEnabled);
			}, flashbounce->lifetime());
		}

		Core::App().notifications().settingsChanged(
		) | rpl::on_next([=](ChangeType change) {
			if (change == ChangeType::DesktopEnabled) {
				desktopToggles->fire(Core::App().settings().desktopNotify());
			} else if (change == ChangeType::SoundEnabled) {
				soundAllowed->fire(allowed());
			} else if (change == ChangeType::FlashBounceEnabled) {
				flashbounceToggles->fire(
					Core::App().settings().flashBounceNotify());
			}
		}, desktop->lifetime());
	}
}

void BuildNotifyViewSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}

	builder.add([controller](const WidgetContext &ctx) {
		const auto container = ctx.container.get();
		const auto &settings = Core::App().settings();
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

		const auto changed = [=](ChangeType change) {
			Core::App().saveSettingsDelayed();
			Core::App().notifications().notifySettingsChanged(change);
		};

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
		}) | rpl::on_next([=](NotifyView value) {
			Core::App().settings().setNotifyView(value);
			changed(ChangeType::ViewParams);
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
		}) | rpl::on_next([=](NotifyView value) {
			Core::App().settings().setNotifyView(value);
			changed(ChangeType::ViewParams);
		}, preview->lifetime());

		Core::App().notifications().settingsChanged(
		) | rpl::on_next([=](ChangeType change) {
			if (change == ChangeType::DesktopEnabled) {
				previewWrap->toggle(
					Core::App().settings().desktopNotify(),
					anim::type::normal);
				previewDivider->toggle(
					!Core::App().settings().desktopNotify(),
					anim::type::normal);
			}
		}, container->lifetime());

		return SectionBuilder::WidgetToAdd{};
	});
}

void BuildNotifyTypeSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto showOther = builder.showOther();

	builder.addSkip(st::notifyPreviewBottomSkip);
	builder.addSubsectionTitle({
		.id = u"notifications/types"_q,
		.title = tr::lng_settings_notify_title(),
		.keywords = { u"private"_q, u"groups"_q, u"channels"_q },
	});

	if (controller) {
		controller->session().data().notifySettings().loadExceptions();
	}

	builder.add([controller, showOther](const WidgetContext &ctx) {
		const auto privateChats = AddTypeButton(
			ctx.container,
			controller,
			Data::DefaultNotify::User,
			showOther);
		const auto groups = AddTypeButton(
			ctx.container,
			controller,
			Data::DefaultNotify::Group,
			showOther);
		const auto channels = AddTypeButton(
			ctx.container,
			controller,
			Data::DefaultNotify::Broadcast,
			showOther);
		if (ctx.highlights) {
			ctx.highlights->push_back({
				u"notifications/private"_q,
				{ privateChats.get(), { .rippleShape = true } },
			});
			ctx.highlights->push_back({
				u"notifications/groups"_q,
				{ groups.get(), { .rippleShape = true } },
			});
			ctx.highlights->push_back({
				u"notifications/channels"_q,
				{ channels.get(), { .rippleShape = true } },
			});
		}
		return SectionBuilder::WidgetToAdd{};
	}, [] {
		return SearchEntry{
			.id = u"notifications/private"_q,
			.title = tr::lng_notification_private_chats(tr::now),
			.keywords = { u"private"_q, u"chats"_q, u"direct"_q },
			.icon = { &st::menuIconProfile },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"notifications/groups"_q,
			.title = tr::lng_notification_groups(tr::now),
			.keywords = { u"groups"_q, u"chats"_q },
			.icon = { &st::menuIconGroups },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"notifications/channels"_q,
			.title = tr::lng_notification_channels(tr::now),
			.keywords = { u"channels"_q, u"broadcast"_q },
			.icon = { &st::menuIconChannel },
		};
	});
}

void BuildEventNotificationsSection(SectionBuilder &builder) {
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addDivider();
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addSubsectionTitle({
		.id = u"notifications/events"_q,
		.title = tr::lng_settings_events_title(),
		.keywords = { u"events"_q, u"joined"_q, u"pinned"_q },
	});

	const auto session = builder.session();
	const auto &settings = Core::App().settings();

	auto joinSilent = rpl::single(
		session->api().contactSignupSilentCurrent().value_or(false)
	) | rpl::then(session->api().contactSignupSilent());

	const auto joined = builder.addButton({
		.id = u"notifications/events/joined"_q,
		.title = tr::lng_settings_events_joined(),
		.icon = { &st::menuIconInvite },
		.toggled = std::move(joinSilent) | rpl::map([](bool s) { return !s; }),
		.keywords = { u"joined"_q, u"contacts"_q, u"signup"_q },
	});
	if (joined) {
		joined->toggledChanges(
		) | rpl::filter([=](bool enabled) {
			const auto silent = session->api().contactSignupSilentCurrent();
			return (enabled == silent.value_or(false));
		}) | rpl::on_next([=](bool enabled) {
			session->api().saveContactSignupSilent(!enabled);
		}, joined->lifetime());
	}

	const auto pinned = builder.addButton({
		.id = u"notifications/events/pinned"_q,
		.title = tr::lng_settings_events_pinned(),
		.icon = { &st::menuIconPin },
		.toggled = rpl::single(
			settings.notifyAboutPinned()
		) | rpl::then(settings.notifyAboutPinnedChanges()),
		.keywords = { u"pinned"_q, u"message"_q },
	});
	if (pinned) {
		pinned->toggledChanges(
		) | rpl::filter([=](bool notify) {
			return (notify != Core::App().settings().notifyAboutPinned());
		}) | rpl::on_next([=](bool notify) {
			Core::App().settings().setNotifyAboutPinned(notify);
			Core::App().saveSettingsDelayed();
		}, pinned->lifetime());
	}
}

void BuildCallNotificationsSection(SectionBuilder &builder) {
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addDivider();
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addSubsectionTitle({
		.id = u"notifications/calls"_q,
		.title = tr::lng_settings_notifications_calls_title(),
		.keywords = { u"calls"_q, u"incoming"_q, u"receive"_q },
	});

	const auto session = builder.session();
	const auto authorizations = &session->api().authorizations();
	authorizations->reload();

	const auto acceptCalls = builder.addButton({
		.id = u"notifications/calls/accept"_q,
		.title = tr::lng_settings_call_accept_calls(),
		.icon = { &st::menuIconCallsReceive },
		.toggled = authorizations->callsDisabledHereValue()
			| rpl::map([](bool disabled) { return !disabled; }),
		.keywords = { u"calls"_q, u"receive"_q, u"incoming"_q },
	});
	if (acceptCalls) {
		acceptCalls->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled == authorizations->callsDisabledHere());
		}) | rpl::on_next([=](bool toggled) {
			authorizations->toggleCallsDisabledHere(!toggled);
		}, acceptCalls->lifetime());
	}
}

void BuildBadgeCounterSection(SectionBuilder &builder) {
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addDivider();
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addSubsectionTitle({
		.id = u"notifications/badge"_q,
		.title = tr::lng_settings_badge_title(),
		.keywords = { u"badge"_q, u"counter"_q, u"unread"_q },
	});

	const auto session = builder.session();
	const auto &settings = Core::App().settings();

	const auto muted = builder.addButton({
		.id = u"notifications/include-muted-chats"_q,
		.title = tr::lng_settings_include_muted(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings.includeMutedCounter()),
		.keywords = { u"muted"_q, u"badge"_q, u"counter"_q },
	});

	const auto hasFolders = session->data().chatsFilters().has();
	const auto mutedFolders = hasFolders ? builder.addButton({
		.id = u"notifications/badge/muted_folders"_q,
		.title = tr::lng_settings_include_muted_folders(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings.includeMutedCounterFolders()),
		.keywords = { u"muted"_q, u"folders"_q },
	}) : nullptr;

	const auto count = builder.addButton({
		.id = u"notifications/count-unread-messages"_q,
		.title = tr::lng_settings_count_unread(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings.countUnreadMessages()),
		.keywords = { u"unread"_q, u"messages"_q, u"count"_q },
	});

	const auto changed = [=](ChangeType change) {
		Core::App().saveSettingsDelayed();
		Core::App().notifications().notifySettingsChanged(change);
	};

	if (muted) {
		muted->toggledChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != Core::App().settings().includeMutedCounter());
		}) | rpl::on_next([=](bool checked) {
			Core::App().settings().setIncludeMutedCounter(checked);
			changed(ChangeType::IncludeMuted);
		}, muted->lifetime());
	}

	if (mutedFolders) {
		mutedFolders->toggledChanges(
		) | rpl::filter([=](bool checked) {
			return (checked
				!= Core::App().settings().includeMutedCounterFolders());
		}) | rpl::on_next([=](bool checked) {
			Core::App().settings().setIncludeMutedCounterFolders(checked);
			changed(ChangeType::IncludeMuted);
		}, mutedFolders->lifetime());
	}

	if (count) {
		count->toggledChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != Core::App().settings().countUnreadMessages());
		}) | rpl::on_next([=](bool checked) {
			Core::App().settings().setCountUnreadMessages(checked);
			changed(ChangeType::CountMessages);
		}, count->lifetime());
	}
}

void BuildSystemIntegrationAndAdvancedSection(SectionBuilder &builder) {
	const auto controller = builder.controller();

	auto nativeText = [&]() -> rpl::producer<QString> {
		if (!Platform::Notifications::Supported()
			|| Core::App().notifications().nativeEnforced()) {
			return rpl::producer<QString>();
		} else if (Platform::IsWindows()) {
			return tr::lng_settings_use_windows();
		}
		return tr::lng_settings_use_native_notifications();
	}();

	if (nativeText) {
		builder.addSkip(st::settingsCheckboxesSkip);
		builder.addDivider();
		builder.addSkip(st::settingsCheckboxesSkip);
		builder.addSubsectionTitle({
			.id = u"notifications/native"_q,
			.title = tr::lng_settings_native_title(),
			.keywords = { u"native"_q, u"system"_q, u"windows"_q },
		});
	}

	const auto &settings = Core::App().settings();
	const auto native = nativeText ? builder.addButton({
		.id = u"notifications/use-native"_q,
		.title = std::move(nativeText),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings.nativeNotifications()),
		.keywords = { u"native"_q, u"system"_q, u"windows"_q },
	}) : nullptr;

	if (Core::App().notifications().nativeEnforced()) {
		return;
	}
	if (!controller) {
		return;
	}

	builder.add([native, controller](const WidgetContext &ctx) {
		const auto container = ctx.container.get();
		const auto advancedSlide = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto advancedWrap = advancedSlide->entity();

		if (native) {
			native->toggledChanges(
			) | rpl::filter([](bool checked) {
				return (checked != Core::App().settings().nativeNotifications());
			}) | rpl::on_next([=](bool checked) {
				Core::App().settings().setNativeNotifications(checked);
				Core::App().saveSettingsDelayed();
				Core::App().notifications().createManager();
				advancedSlide->toggle(!checked, anim::type::normal);
			}, native->lifetime());
		}

		if (Platform::IsWindows()) {
			const auto skipInFocus = advancedWrap->add(object_ptr<Ui::SettingsButton>(
				advancedWrap,
				tr::lng_settings_skip_in_focus(),
				st::settingsButtonNoIcon
			))->toggleOn(rpl::single(Core::App().settings().skipToastsInFocus()));

			skipInFocus->toggledChanges(
			) | rpl::filter([](bool checked) {
				return (checked != Core::App().settings().skipToastsInFocus());
			}) | rpl::on_next([=](bool checked) {
				Core::App().settings().setSkipToastsInFocus(checked);
				Core::App().saveSettingsDelayed();
				if (checked && Platform::Notifications::SkipToastForCustom()) {
					Core::App().notifications().notifySettingsChanged(
						ChangeType::DesktopEnabled);
				}
			}, skipInFocus->lifetime());
		}

		const auto screens = QGuiApplication::screens();
		if (screens.size() > 1) {
			Ui::AddSkip(advancedWrap, st::settingsCheckboxesSkip);
			Ui::AddDivider(advancedWrap);
			Ui::AddSkip(advancedWrap, st::settingsCheckboxesSkip);
			Ui::AddSubsectionTitle(
				advancedWrap,
				tr::lng_settings_notifications_display());

			const auto currentChecksum
				= Core::App().settings().notificationsDisplayChecksum();
			auto currentIndex = (currentChecksum == 0)
				? kDefaultDisplayIndex
				: 0;
			for (auto i = 0; i < screens.size(); ++i) {
				if (Platform::ScreenNameChecksum(screens[i]) == currentChecksum) {
					currentIndex = i;
					break;
				}
			}

			const auto group = std::make_shared<Ui::RadiobuttonGroup>(
				currentIndex);

			advancedWrap->add(
				object_ptr<Ui::Radiobutton>(
					advancedWrap,
					group,
					kDefaultDisplayIndex,
					tr::lng_settings_notifications_display_default(tr::now),
					st::settingsSendType),
				st::settingsSendTypePadding);

			for (auto i = 0; i < screens.size(); ++i) {
				const auto &screen = screens[i];
				const auto name = Platform::ScreenDisplayLabel(screen);
				const auto geometry = screen->geometry();
				const auto resolution = QString::number(geometry.width())
					+ QChar(0x00D7)
					+ QString::number(geometry.height());
				const auto label = name.isEmpty()
					? QString("Display (%1)").arg(resolution)
					: QString("%1 (%2)").arg(name).arg(resolution);
				advancedWrap->add(
					object_ptr<Ui::Radiobutton>(
						advancedWrap,
						group,
						i,
						label,
						st::settingsSendType),
					st::settingsSendTypePadding);
			}
			group->setChangedCallback([=](int selectedIndex) {
				if (selectedIndex == kDefaultDisplayIndex) {
					Core::App().settings().setNotificationsDisplayChecksum(0);
					Core::App().saveSettings();
					Core::App().notifications().notifySettingsChanged(
						ChangeType::Corner);
				} else {
					const auto screens = QGuiApplication::screens();
					if (selectedIndex >= 0 && selectedIndex < screens.size()) {
						const auto checksum = Platform::ScreenNameChecksum(
							screens[selectedIndex]);
						Core::App().settings().setNotificationsDisplayChecksum(
							checksum);
						Core::App().saveSettings();
						Core::App().notifications().notifySettingsChanged(
							ChangeType::Corner);
					}
				}
			});
		}

		Ui::AddSkip(advancedWrap, st::settingsCheckboxesSkip);
		Ui::AddDivider(advancedWrap);
		Ui::AddSkip(advancedWrap, st::settingsCheckboxesSkip);
		Ui::AddSubsectionTitle(
			advancedWrap,
			tr::lng_settings_notifications_position());
		Ui::AddSkip(advancedWrap, st::settingsCheckboxesSkip);

		const auto position = advancedWrap->add(
			object_ptr<NotificationsCount>(advancedWrap, controller));

		Ui::AddSkip(advancedWrap, st::settingsCheckboxesSkip);
		Ui::AddSubsectionTitle(advancedWrap, tr::lng_settings_notifications_count());

		const auto countSlider = advancedWrap->add(
			object_ptr<Ui::SettingsSlider>(advancedWrap, st::settingsSlider),
			st::settingsBigScalePadding);
		for (int i = 0; i != kMaxNotificationsCount; ++i) {
			countSlider->addSection(QString::number(i + 1));
		}
		countSlider->setActiveSectionFast(CurrentNotificationsCount() - 1);
		countSlider->sectionActivated(
		) | rpl::on_next([=](int section) {
			position->setCount(section + 1);
		}, countSlider->lifetime());
		Ui::AddSkip(advancedWrap, st::settingsCheckboxesSkip);

		if (Core::App().settings().nativeNotifications()) {
			advancedSlide->hide(anim::type::instant);
		}

		Core::App().notifications().settingsChanged(
		) | rpl::on_next([=](ChangeType change) {
			if (change == ChangeType::DesktopEnabled) {
				const auto native = Core::App().settings().nativeNotifications();
				advancedSlide->toggle(!native, anim::type::normal);
			}
		}, advancedSlide->lifetime());

		return SectionBuilder::WidgetToAdd{};
	});
}

void BuildNotificationsSectionContent(SectionBuilder &builder) {
	builder.addSkip(st::settingsPrivacySkip);

	BuildMultiAccountSection(builder);
	BuildGlobalNotificationsSection(builder);
	BuildNotifyViewSection(builder);
	BuildNotifyTypeSection(builder);
	BuildEventNotificationsSection(builder);
	BuildCallNotificationsSection(builder);
	BuildBadgeCounterSection(builder);
	BuildSystemIntegrationAndAdvancedSection(builder);
}

class Notifications : public Section<Notifications> {
public:
	Notifications(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = Notifications::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_section_notify,
	.icon = &st::menuIconNotifications,
}, [](SectionBuilder &builder) {
	BuildNotificationsSectionContent(builder);
});

const SectionBuildMethod kNotificationsSection = kMeta.build;

Notifications::Notifications(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Notifications::title() {
	return tr::lng_settings_section_notify();
}

void Notifications::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kNotificationsSection);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type NotificationsId() {
	return Notifications::Id();
}

} // namespace Settings
