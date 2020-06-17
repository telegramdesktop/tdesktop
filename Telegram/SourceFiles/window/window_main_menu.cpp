/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_main_menu.h"

#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "ui/empty_userpic.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "support/support_templates.h"
#include "settings/settings_common.h"
#include "base/qt_signal_producer.h"
#include "boxes/about_box.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "lang/lang_keys.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "mainwidget.h"
#include "app.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace {

constexpr auto kMinDiffIntensity = 0.25;

float64 IntensityOfColor(QColor color) {
	return (0.299 * color.red()
			+ 0.587 * color.green()
			+ 0.114 * color.blue()) / 255.0;
}

bool IsShadowShown(const QImage &img, const QRect r, float64 intensityText) {
	for (auto x = r.x(); x < r.x() + r.width(); x++) {
		for (auto y = r.y(); y < r.y() + r.height(); y++) {
			const auto intensity = IntensityOfColor(QColor(img.pixel(x, y)));
			if ((std::abs(intensity - intensityText)) < kMinDiffIntensity) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

namespace Window {

class MainMenu::ResetScaleButton : public Ui::AbstractButton {
public:
	ResetScaleButton(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

	static constexpr auto kText = "100%";

};

MainMenu::ResetScaleButton::ResetScaleButton(QWidget *parent)
: AbstractButton(parent) {
	const auto margin = st::mainMenuCloudButton.height
		- st::mainMenuCloudSize;
	const auto textWidth = st::mainMenuResetScaleFont->width(kText);
	const auto innerWidth = st::mainMenuResetScaleLeft
		+ textWidth
		+ st::mainMenuResetScaleRight;
	const auto width = margin + innerWidth;
	resize(width, st::mainMenuCloudButton.height);
}

void MainMenu::ResetScaleButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto innerHeight = st::mainMenuCloudSize;
	const auto radius = innerHeight / 2;
	const auto margin = st::mainMenuCloudButton.height
		- st::mainMenuCloudSize;
	const auto textWidth = st::mainMenuResetScaleFont->width(kText);
	const auto innerWidth = st::mainMenuResetScaleLeft
		+ textWidth
		+ st::mainMenuResetScaleRight;
	const auto left = margin / 2;
	const auto top = margin / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(st::mainMenuCloudBg);
	p.drawRoundedRect(left, top, innerWidth, innerHeight, radius, radius);

	st::settingsIconInterfaceScale.paint(
		p,
		left + st::mainMenuResetScaleIconLeft,
		top + ((innerHeight - st::settingsIconInterfaceScale.height()) / 2),
		width(),
		st::mainMenuCloudFg->c);

	p.setFont(st::mainMenuResetScaleFont);
	p.setPen(st::mainMenuCloudFg);
	p.drawText(
		left + st::mainMenuResetScaleLeft,
		top + st::mainMenuResetScaleTop + st::mainMenuResetScaleFont->ascent,
		kText);
}

MainMenu::MainMenu(
	QWidget *parent,
	not_null<SessionController*> controller)
: LayerWidget(parent)
, _controller(controller)
, _menu(this, st::mainMenu)
, _telegram(this, st::mainMenuTelegramLabel)
, _version(this, st::mainMenuVersionLabel) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	const auto showSelfChat = [=] {
		controller->content()->choosePeer(
			_controller->session().userPeerId(),
			ShowAtUnreadMsgId);
	};
	const auto showArchive = [=] {
		const auto folder = _controller->session().data().folderLoaded(
			Data::Folder::kId);
		if (folder) {
			controller->openFolder(folder);
			Ui::hideSettingsAndLayer();
		}
	};
	const auto checkArchive = [=] {
		const auto folder = _controller->session().data().folderLoaded(
			Data::Folder::kId);
		return folder
			&& !folder->chatsList()->empty()
			&& _controller->session().settings().archiveInMainMenu();
	};
	_userpicButton.create(
		this,
		_controller,
		_controller->session().user(),
		Ui::UserpicButton::Role::Custom,
		st::mainMenuUserpic);
	_userpicButton->setClickedCallback(showSelfChat);
	_userpicButton->show();
	_cloudButton.create(this, st::mainMenuCloudButton);
	_cloudButton->setClickedCallback(showSelfChat);
	_cloudButton->show();

	_archiveButton.create(this, st::mainMenuCloudButton);
	_archiveButton->setHidden(!checkArchive());
	_archiveButton->setAcceptBoth(true);
	_archiveButton->clicks(
	) | rpl::start_with_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			showArchive();
			return;
		} else if (which != Qt::RightButton) {
			return;
		}
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(this);
		_contextMenu->addAction(
			tr::lng_context_archive_to_list(tr::now), [=] {
			_controller->session().settings().setArchiveInMainMenu(false);
			_controller->session().saveSettingsDelayed();
			Ui::hideSettingsAndLayer();
		});
		_contextMenu->popup(QCursor::pos());
	}, _archiveButton->lifetime());

	_nightThemeSwitch.setCallback([this] {
		if (const auto action = *_nightThemeAction) {
			const auto nightMode = Window::Theme::IsNightMode();
			if (action->isChecked() != nightMode) {
				Window::Theme::ToggleNightMode();
				Window::Theme::KeepApplied();
			}
		}
	});

	parentResized();
	_menu->setTriggeredCallback([](QAction *action, int actionTop, Ui::Menu::TriggeredSource source) {
		emit action->triggered();
	});
	refreshMenu();
	refreshBackground();

	_telegram->setMarkedText(Ui::Text::Link(
		qsl("Telegram Desktop"),
		qsl("https://desktop.telegram.org")));
	_telegram->setLinksTrusted();
	_version->setRichText(textcmdLink(1, tr::lng_settings_current_version(tr::now, lt_version, currentVersionText())) + QChar(' ') + QChar(8211) + QChar(' ') + textcmdLink(2, tr::lng_menu_about(tr::now)));
	_version->setLink(1, std::make_shared<UrlClickHandler>(qsl("https://desktop.telegram.org/changelog")));
	_version->setLink(2, std::make_shared<LambdaClickHandler>([] { Ui::show(Box<AboutBox>()); }));

	subscribe(_controller->session().downloaderTaskFinished(), [=] { update(); });

	_controller->session().changes().peerUpdates(
		_controller->session().user(),
		Data::PeerUpdate::Flag::PhoneNumber
	) | rpl::start_with_next([=] {
		updatePhone();
	}, lifetime());

	_controller->session().serverConfig().phoneCallsEnabled.changes(
	) | rpl::start_with_next([=] {
		refreshMenu();
	}, lifetime());

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.type == Window::Theme::BackgroundUpdate::Type::ApplyingTheme) {
			refreshMenu();
		}
		if (update.type == Window::Theme::BackgroundUpdate::Type::New) {
			refreshBackground();
		}
	});
	_controller->session().data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return folder && (folder->id() == Data::Folder::kId);
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		_archiveButton->setHidden(!checkArchive());
		update();
	}, lifetime());
	updatePhone();
	initResetScaleButton();
}

void MainMenu::parentResized() {
	resize(st::mainMenuWidth, parentWidget()->height());
}

void MainMenu::refreshMenu() {
	_menu->clearActions();
	if (!_controller->session().supportMode()) {
		const auto controller = _controller;
		_menu->addAction(tr::lng_create_group_title(tr::now), [] {
			App::wnd()->onShowNewGroup();
		}, &st::mainMenuNewGroup, &st::mainMenuNewGroupOver);
		_menu->addAction(tr::lng_create_channel_title(tr::now), [] {
			App::wnd()->onShowNewChannel();
		}, &st::mainMenuNewChannel, &st::mainMenuNewChannelOver);
		_menu->addAction(tr::lng_menu_contacts(tr::now), [=] {
			Ui::show(Box<PeerListBox>(std::make_unique<ContactsBoxController>(controller), [](not_null<PeerListBox*> box) {
				box->addButton(tr::lng_close(), [box] { box->closeBox(); });
				box->addLeftButton(tr::lng_profile_add_contact(), [] { App::wnd()->onShowAddContact(); });
			}));
		}, &st::mainMenuContacts, &st::mainMenuContactsOver);
		if (_controller->session().serverConfig().phoneCallsEnabled.current()) {
			_menu->addAction(tr::lng_menu_calls(tr::now), [=] {
				Ui::show(Box<PeerListBox>(std::make_unique<Calls::BoxController>(controller), [](not_null<PeerListBox*> box) {
					box->addButton(tr::lng_close(), [=] {
						box->closeBox();
					});
					box->addTopButton(st::callSettingsButton, [=] {
						App::wnd()->sessionController()->showSettings(
							Settings::Type::Calls,
							Window::SectionShow(anim::type::instant));
					});
				}));
			}, &st::mainMenuCalls, &st::mainMenuCallsOver);
		}
	} else {
		_menu->addAction(tr::lng_profile_add_contact(tr::now), [] {
			App::wnd()->onShowAddContact();
		}, &st::mainMenuContacts, &st::mainMenuContactsOver);

		const auto fix = std::make_shared<QPointer<QAction>>();
		*fix = _menu->addAction(qsl("Fix chats order"), [=] {
			(*fix)->setChecked(!(*fix)->isChecked());
			_controller->session().settings().setSupportFixChatsOrder(
				(*fix)->isChecked());
			_controller->session().local().writeSettings();
		}, &st::mainMenuFixOrder, &st::mainMenuFixOrderOver);
		(*fix)->setCheckable(true);
		(*fix)->setChecked(
			_controller->session().settings().supportFixChatsOrder());

		_menu->addAction(qsl("Reload templates"), [=] {
			_controller->session().supportTemplates().reload();
		}, &st::mainMenuReload, &st::mainMenuReloadOver);
	}
	_menu->addAction(tr::lng_menu_settings(tr::now), [] {
		App::wnd()->showSettings();
	}, &st::mainMenuSettings, &st::mainMenuSettingsOver);

	_nightThemeAction = std::make_shared<QPointer<QAction>>();
	auto action = _menu->addAction(tr::lng_menu_night_mode(tr::now), [=] {
		if (auto action = *_nightThemeAction) {
			action->setChecked(!action->isChecked());
			_nightThemeSwitch.callOnce(st::mainMenu.itemToggle.duration);
		}
	}, &st::mainMenuNightMode, &st::mainMenuNightModeOver);
	*_nightThemeAction = action;
	action->setCheckable(true);
	action->setChecked(Window::Theme::IsNightMode());
	_menu->finishAnimating();

	updatePhone();
}

void MainMenu::refreshBackground() {
	const auto fill = QRect(0, 0, width(), st::mainMenuCoverHeight);
	const auto intensityText = IntensityOfColor(st::mainMenuCoverFg->c);
	QImage backgroundImage(
		st::mainMenuWidth * cIntRetinaFactor(),
		st::mainMenuCoverHeight * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	QPainter p(&backgroundImage);

	const auto drawShadow = [](QPainter &p) {
		st::mainMenuShadow.paint(
			p,
			0,
			st::mainMenuCoverHeight - st::mainMenuShadow.height(),
			st::mainMenuWidth,
			IntensityOfColor(st::mainMenuCoverFg->c) < 0.5
				? Qt::white
				: Qt::black);
	};

	// Solid color.
	if (const auto color = Window::Theme::Background()->colorForFill()) {
		const auto intensity = IntensityOfColor(*color);
		p.fillRect(fill, *color);
		if (std::abs(intensity - intensityText) < kMinDiffIntensity) {
			drawShadow(p);
		}
		_background = backgroundImage;
		return;
	}

	// Background image.
	const auto &pixmap = Window::Theme::Background()->pixmap();
	QRect to, from;
	Window::Theme::ComputeBackgroundRects(fill, pixmap.size(), to, from);

	// Cut off the part of the background that is under text.
	const QRect underText(
		st::mainMenuCoverTextLeft,
		st::mainMenuCoverNameTop,
		std::max(
			st::semiboldFont->width(
				_controller->session().user()->nameText().toString()),
			st::normalFont->width(_phoneText)),
		st::semiboldFont->height * 2);

	p.drawPixmap(to, pixmap, from);
	if (IsShadowShown(backgroundImage, underText, intensityText)) {
		drawShadow(p);
	}
	_background = backgroundImage;
}

void MainMenu::resizeEvent(QResizeEvent *e) {
	_menu->setForceWidth(width());
	updateControlsGeometry();
}

void MainMenu::updateControlsGeometry() {
	if (_userpicButton) {
		_userpicButton->moveToLeft(st::mainMenuUserpicLeft, st::mainMenuUserpicTop);
	}
	if (_cloudButton) {
		const auto offset = st::mainMenuCloudSize / 4;
		const auto y = st::mainMenuCoverHeight
			- _cloudButton->height()
			- offset;
		_cloudButton->moveToRight(offset, y);
		if (_archiveButton) {
			_archiveButton->moveToRight(
				offset,
				y - _cloudButton->height());
		}
	}
	if (_resetScaleButton) {
		_resetScaleButton->moveToRight(0, 0);
	}
	_menu->moveToLeft(0, st::mainMenuCoverHeight + st::mainMenuSkip);
	_telegram->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuTelegramBottom - _telegram->height());
	_version->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuVersionBottom - _version->height());
}

void MainMenu::updatePhone() {
	_phoneText = App::formatPhone(_controller->session().user()->phone());
	update();
}

void MainMenu::paintEvent(QPaintEvent *e) {
	Painter p(this);
	const auto clip = e->rect();
	const auto cover = QRect(0, 0, width(), st::mainMenuCoverHeight)
		.intersected(e->rect());

	const auto background = Window::Theme::Background();
	const auto isFill = background->tile()
		|| background->colorForFill().has_value()
		|| background->isMonoColorImage()
		|| background->paper().isPattern()
		|| Data::IsLegacy1DefaultWallPaper(background->paper());

	if (!isFill && !_background.isNull()) {
		PainterHighQualityEnabler hq(p);
		p.drawImage(0, 0, _background);
	}

	if (!cover.isEmpty()) {
		const auto widthText = _cloudButton
			? _cloudButton->x() - st::mainMenuCloudSize
			: width() - 2 * st::mainMenuCoverTextLeft;

		if (isFill) {
			p.fillRect(cover, st::mainMenuCoverBg);
		}
		p.setPen(st::mainMenuCoverFg);
		p.setFont(st::semiboldFont);
		_controller->session().user()->nameText().drawLeftElided(
			p,
			st::mainMenuCoverTextLeft,
			st::mainMenuCoverNameTop,
			widthText,
			width());
		p.setFont(st::normalFont);
		p.drawTextLeft(st::mainMenuCoverTextLeft, st::mainMenuCoverStatusTop, width(), _phoneText);
		if (_cloudButton) {
			Ui::EmptyUserpic::PaintSavedMessages(
				p,
				_cloudButton->x() + (_cloudButton->width() - st::mainMenuCloudSize) / 2,
				_cloudButton->y() + (_cloudButton->height() - st::mainMenuCloudSize) / 2,
				width(),
				st::mainMenuCloudSize,
				isFill ? st::mainMenuCloudBg : st::msgServiceBg,
				isFill ? st::mainMenuCloudFg : st::msgServiceFg);
		}

		// Draw Archive button.
		if (!_archiveButton->isHidden()) {
			const auto folder = _controller->session().data().folderLoaded(
				Data::Folder::kId);
			if (folder) {
				folder->paintUserpic(
					p,
					_archiveButton->x() + (_archiveButton->width() - st::mainMenuCloudSize) / 2,
					_archiveButton->y() + (_archiveButton->height() - st::mainMenuCloudSize) / 2,
					st::mainMenuCloudSize,
					isFill ? st::mainMenuCloudBg : st::msgServiceBg,
					isFill ? st::mainMenuCloudFg : st::msgServiceFg);
			}
		}
	}
	auto other = QRect(0, st::mainMenuCoverHeight, width(), height() - st::mainMenuCoverHeight).intersected(clip);
	if (!other.isEmpty()) {
		p.fillRect(other, st::mainMenuBg);
	}
}

void MainMenu::initResetScaleButton() {
	if (!window() || !window()->windowHandle()) {
		return;
	}
	const auto handle = window()->windowHandle();
	rpl::single(
		handle->screen()
	) | rpl::then(
		base::qt_signal_producer(handle, &QWindow::screenChanged)
	) | rpl::filter([](QScreen *screen) {
		return screen != nullptr;
	}) | rpl::map([](QScreen * screen) {
		return rpl::single(
			screen->availableGeometry()
		) | rpl::then(
#ifdef OS_MAC_OLD
			base::qt_signal_producer(screen, &QScreen::virtualGeometryChanged)
#else // OS_MAC_OLD
			base::qt_signal_producer(screen, &QScreen::availableGeometryChanged)
#endif // OS_MAC_OLD
		);
	}) | rpl::flatten_latest(
	) | rpl::map([](QRect available) {
		return (available.width() >= st::windowMinWidth)
			&& (available.height() >= st::windowMinHeight);
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool good) {
		if (good) {
			_resetScaleButton.destroy();
		} else {
			_resetScaleButton.create(this);
			_resetScaleButton->addClickHandler([] {
				cSetConfigScale(style::kScaleDefault);
				Local::writeSettings();
				App::restart();
			});
			_resetScaleButton->show();
			updateControlsGeometry();
		}
	}, lifetime());
}

} // namespace Window
