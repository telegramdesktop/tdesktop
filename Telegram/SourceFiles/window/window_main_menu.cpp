/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_main_menu.h"

#include "window/themes/window_theme.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu.h"
#include "ui/special_buttons.h"
#include "ui/empty_userpic.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "support/support_templates.h"
#include "boxes/about_box.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "lang/lang_keys.h"
#include "core/click_handler_types.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"

namespace Window {
namespace {

template <typename Object, typename Other, typename Value>
auto qtSignalProducer(
		Object *object,
		void(Other::*signal)(Value)) {
	using Produced = std::remove_const_t<std::decay_t<Value>>;
	const auto guarded = make_weak(object);
	return rpl::make_producer<Produced>([=](auto consumer) {
		if (!guarded) {
			return rpl::lifetime();
		}
		auto listener = Ui::CreateChild<QObject>(guarded.data());
		QObject::connect(guarded, signal, listener, [=](Value value) {
			consumer.put_next_copy(value);
		});
		const auto weak = make_weak(listener);
		return rpl::lifetime([=] {
			if (weak) {
				delete weak;
			}
		});
	});
}

} // namespace

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
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _menu(this, st::mainMenu)
, _telegram(this, st::mainMenuTelegramLabel)
, _version(this, st::mainMenuVersionLabel) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	auto showSelfChat = [] {
		App::main()->choosePeer(Auth().userPeerId(), ShowAtUnreadMsgId);
	};
	_userpicButton.create(
		this,
		_controller,
		Auth().user(),
		Ui::UserpicButton::Role::Custom,
		st::mainMenuUserpic);
	_userpicButton->setClickedCallback(showSelfChat);
	_userpicButton->show();
	_cloudButton.create(this, st::mainMenuCloudButton);
	_cloudButton->setClickedCallback(showSelfChat);
	_cloudButton->show();

	_nightThemeSwitch.setCallback([this] {
		if (const auto action = *_nightThemeAction) {
			const auto nightMode = Window::Theme::IsNightMode();
			if (action->isChecked() != nightMode) {
				Window::Theme::ToggleNightMode();
				Window::Theme::KeepApplied();
			}
		}
	});

	resize(st::mainMenuWidth, parentWidget()->height());
	_menu->setTriggeredCallback([](QAction *action, int actionTop, Ui::Menu::TriggeredSource source) {
		emit action->triggered();
	});
	refreshMenu();

	_telegram->setRichText(textcmdLink(1, qsl("Telegram Desktop")));
	_telegram->setLink(1, std::make_shared<UrlClickHandler>(qsl("https://desktop.telegram.org")));
	_version->setRichText(textcmdLink(1, lng_settings_current_version(lt_version, currentVersionText())) + QChar(' ') + QChar(8211) + QChar(' ') + textcmdLink(2, lang(lng_menu_about)));
	_version->setLink(1, std::make_shared<UrlClickHandler>(qsl("https://desktop.telegram.org/changelog")));
	_version->setLink(2, std::make_shared<LambdaClickHandler>([] { Ui::show(Box<AboutBox>()); }));

	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserPhoneChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer->isSelf()) {
			updatePhone();
		}
	}));
	subscribe(Global::RefPhoneCallsEnabledChanged(), [this] { refreshMenu(); });
	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.type == Window::Theme::BackgroundUpdate::Type::ApplyingTheme) {
			refreshMenu();
		}
	});
	updatePhone();
	initResetScaleButton();
}

void MainMenu::refreshMenu() {
	_menu->clearActions();
	if (!Auth().supportMode()) {
		_menu->addAction(lang(lng_create_group_title), [] {
			App::wnd()->onShowNewGroup();
		}, &st::mainMenuNewGroup, &st::mainMenuNewGroupOver);
		_menu->addAction(lang(lng_create_channel_title), [] {
			App::wnd()->onShowNewChannel();
		}, &st::mainMenuNewChannel, &st::mainMenuNewChannelOver);
		_menu->addAction(lang(lng_menu_contacts), [] {
			Ui::show(Box<PeerListBox>(std::make_unique<ContactsBoxController>(), [](not_null<PeerListBox*> box) {
				box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
				box->addLeftButton(langFactory(lng_profile_add_contact), [] { App::wnd()->onShowAddContact(); });
			}));
		}, &st::mainMenuContacts, &st::mainMenuContactsOver);
		if (Global::PhoneCallsEnabled()) {
			_menu->addAction(lang(lng_menu_calls), [] {
				Ui::show(Box<PeerListBox>(std::make_unique<Calls::BoxController>(), [](not_null<PeerListBox*> box) {
					box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
				}));
			}, &st::mainMenuCalls, &st::mainMenuCallsOver);
		}
	} else {
		_menu->addAction(lang(lng_profile_add_contact), [] {
			App::wnd()->onShowAddContact();
		}, &st::mainMenuContacts, &st::mainMenuContactsOver);

		const auto fix = std::make_shared<QPointer<QAction>>();
		*fix = _menu->addAction(qsl("Fix chats order"), [=] {
			(*fix)->setChecked(!(*fix)->isChecked());
			Auth().settings().setSupportFixChatsOrder((*fix)->isChecked());
			Local::writeUserSettings();
		}, &st::mainMenuFixOrder, &st::mainMenuFixOrderOver);
		(*fix)->setCheckable(true);
		(*fix)->setChecked(Auth().settings().supportFixChatsOrder());

		_menu->addAction(qsl("Reload templates"), [=] {
			Auth().supportTemplates().reload();
		}, &st::mainMenuReload, &st::mainMenuReloadOver);
	}
	_menu->addAction(lang(lng_menu_settings), [] {
		App::wnd()->showSettings();
	}, &st::mainMenuSettings, &st::mainMenuSettingsOver);

	_nightThemeAction = std::make_shared<QPointer<QAction>>();
	auto action = _menu->addAction(lang(lng_menu_night_mode), [=] {
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

void MainMenu::resizeEvent(QResizeEvent *e) {
	_menu->setForceWidth(width());
	updateControlsGeometry();
}

void MainMenu::updateControlsGeometry() {
	if (_userpicButton) {
		_userpicButton->moveToLeft(st::mainMenuUserpicLeft, st::mainMenuUserpicTop);
	}
	if (_cloudButton) {
		_cloudButton->moveToRight(0, st::mainMenuCoverHeight - _cloudButton->height());
	}
	if (_resetScaleButton) {
		_resetScaleButton->moveToRight(0, 0);
	}
	_menu->moveToLeft(0, st::mainMenuCoverHeight + st::mainMenuSkip);
	_telegram->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuTelegramBottom - _telegram->height());
	_version->moveToLeft(st::mainMenuFooterLeft, height() - st::mainMenuVersionBottom - _version->height());
}

void MainMenu::updatePhone() {
	_phoneText = App::formatPhone(Auth().user()->phone());
	update();
}

void MainMenu::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	auto cover = QRect(0, 0, width(), st::mainMenuCoverHeight).intersected(clip);
	if (!cover.isEmpty()) {
		p.fillRect(cover, st::mainMenuCoverBg);
		p.setPen(st::mainMenuCoverFg);
		p.setFont(st::semiboldFont);
		Auth().user()->nameText.drawLeftElided(
			p,
			st::mainMenuCoverTextLeft,
			st::mainMenuCoverNameTop,
			width() - 2 * st::mainMenuCoverTextLeft,
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
				st::mainMenuCloudBg,
				st::mainMenuCloudFg);
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
		qtSignalProducer(handle, &QWindow::screenChanged)
	) | rpl::map([](QScreen *screen) {
		return rpl::single(
			screen->availableGeometry()
		) | rpl::then(
#ifdef OS_MAC_OLD
			qtSignalProducer(screen, &QScreen::virtualGeometryChanged)
#else // OS_MAC_OLD
			qtSignalProducer(screen, &QScreen::availableGeometryChanged)
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
				cSetConfigScale(kInterfaceScaleDefault);
				Local::writeSettings();
				App::restart();
			});
			_resetScaleButton->show();
			updateControlsGeometry();
		}
	}, lifetime());
}

} // namespace Window
