/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_main_menu.h"

#include "window/themes/window_theme.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "ui/empty_userpic.h"
#include "dialogs/dialogs_layout.h"
#include "base/call_delayed.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "support/support_templates.h"
#include "settings/settings_common.h"
#include "base/qt_signal_producer.h"
#include "boxes/about_box.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "lang/lang_keys.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_config.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "mainwidget.h"
#include "app.h"
#include "styles/style_window.h"
#include "styles/style_widgets.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace {

constexpr auto kMinDiffIntensity = 0.25;

[[nodicard]] float64 IntensityOfColor(QColor color) {
	return (0.299 * color.red()
			+ 0.587 * color.green()
			+ 0.114 * color.blue()) / 255.0;
}

[[nodiscard]] bool IsShadowShown(const QImage &img, const QRect r, float64 intensityText) {
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

[[nodiscard]] bool IsFilledCover() {
	const auto background = Window::Theme::Background();
	return background->tile()
		|| background->colorForFill().has_value()
		|| background->isMonoColorImage()
		|| background->paper().isPattern()
		|| Data::IsLegacy1DefaultWallPaper(background->paper());
}

[[nodiscard]] bool IsAltShift(Qt::KeyboardModifiers modifiers) {
	return (modifiers & Qt::ShiftModifier) && (modifiers & Qt::AltModifier);
}

} // namespace

namespace Window {

class MainMenu::AccountButton final
	: public Ui::RippleButton
	, public base::Subscriber {
public:
	AccountButton(QWidget *parent, not_null<Main::Account*> account);

private:
	void paintEvent(QPaintEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void paintUserpic(Painter &p);

	const not_null<Main::Session*> _session;
	const style::Menu &_st;
	std::shared_ptr<Data::CloudImageView> _userpicView;
	InMemoryKey _userpicKey = {};
	QImage _userpicCache;
	base::unique_qptr<Ui::PopupMenu> _menu;

	Dialogs::Layout::UnreadBadgeStyle _unreadSt;
	int _unreadBadge = 0;
	bool _unreadBadgeMuted = true;

};

class MainMenu::ToggleAccountsButton final : public Ui::AbstractButton {
public:
	explicit ToggleAccountsButton(QWidget *parent);

	[[nodiscard]] int rightSkip() const {
		return _rightSkip.current();
	}

private:
	void paintEvent(QPaintEvent *e) override;
	void paintUnreadBadge(QPainter &p);

	void validateUnreadBadge();
	[[nodiscard]] QString computeUnreadBadge() const;

	rpl::variable<int> _rightSkip;
	Ui::Animations::Simple _toggledAnimation;
	bool _toggled = false;

	QString _unreadBadge;
	int _unreadBadgeWidth = 0;
	bool _unreadBadgeStale = false;

};

class MainMenu::ResetScaleButton final : public Ui::AbstractButton {
public:
	ResetScaleButton(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

	static constexpr auto kText = "100%";

};

MainMenu::AccountButton::AccountButton(
	QWidget *parent,
	not_null<Main::Account*> account)
: RippleButton(parent, st::defaultRippleAnimation)
, _session(&account->session())
, _st(st::mainMenu){
	const auto height = _st.itemPadding.top()
		+ _st.itemStyle.font->height
		+ _st.itemPadding.bottom();
	resize(width(), height);

	subscribe(Window::Theme::Background(), [=](
			const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			_userpicKey = {};
		}
	});

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		_session->data().unreadBadgeChanges()
	) | rpl::start_with_next([=] {
		_unreadBadge = _session->data().unreadBadge();
		_unreadBadgeMuted = _session->data().unreadBadgeMuted();
		update();
	}, lifetime());
}

void MainMenu::AccountButton::paintUserpic(Painter &p) {
	const auto size = st::mainMenuAccountSize;
	const auto iconSize = height() - 2 * _st.itemIconPosition.y();
	const auto shift = (size - iconSize) / 2;
	const auto x = _st.itemIconPosition.x() - shift;
	const auto y = (height() - size) / 2;

	const auto check = (&_session->account()
		== &Core::App().domain().active());
	const auto user = _session->user();
	if (!check) {
		user->paintUserpicLeft(p, _userpicView, x, y, width(), size);
		return;
	}
	const auto added = st::mainMenuAccountCheck.size;
	const auto cacheSize = QSize(size + added, size + added)
		* cIntRetinaFactor();
	const auto key = user->userpicUniqueKey(_userpicView);
	if (_userpicKey != key) {
		_userpicKey = key;
		if (_userpicCache.size() != cacheSize) {
			_userpicCache = QImage(cacheSize, QImage::Format_ARGB32_Premultiplied);
			_userpicCache.setDevicePixelRatio(cRetinaFactor());
		}
		_userpicCache.fill(Qt::transparent);

		auto q = Painter(&_userpicCache);
		user->paintUserpicLeft(q, _userpicView, 0, 0, width(), size);

		const auto iconDiameter = st::mainMenuAccountCheck.size;
		const auto iconLeft = size + st::mainMenuAccountCheckPosition.x() - iconDiameter;
		const auto iconTop = size + st::mainMenuAccountCheckPosition.y() - iconDiameter;
		const auto iconEllipse = QRect(iconLeft, iconTop, iconDiameter, iconDiameter);
		auto iconBorderPen = QPen(Qt::transparent);
		const auto line = st::mainMenuAccountCheckLine;
		iconBorderPen.setWidth(line);

		PainterHighQualityEnabler hq(q);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setPen(iconBorderPen);
		q.setBrush(st::dialogsUnreadBg);
		q.drawEllipse(iconEllipse);

		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		st::mainMenuAccountCheck.check.paintInCenter(q, iconEllipse);
	}
	p.drawImage(x, y, _userpicCache);
}

void MainMenu::AccountButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto over = isOver();
	p.fillRect(rect(), over ? _st.itemBgOver : _st.itemBg);
	paintRipple(p, 0, 0);

	paintUserpic(p);

	auto available = width() - _st.itemPadding.left();
	if (_unreadBadge
		&& (&_session->account() != &Core::App().activeAccount())) {
		_unreadSt.muted = _unreadBadgeMuted;
		const auto string = (_unreadBadge > 99)
			? "99+"
			: QString::number(_unreadBadge);
		auto unreadWidth = 0;
		const auto skip = _st.itemPadding.right()
			- st::mainMenu.itemToggleShift;
		const auto unreadRight = width() - skip;
		const auto unreadTop = (height() - _unreadSt.size) / 2;
		Dialogs::Layout::paintUnreadCount(
			p,
			string,
			unreadRight,
			unreadTop,
			_unreadSt,
			&unreadWidth);
		available -= unreadWidth + skip + st::mainMenu.itemStyle.font->spacew;
	} else {
		available -= _st.itemPadding.right();
	}

	p.setPen(over ? _st.itemFgOver : _st.itemFg);
	_session->user()->nameText().drawElided(
		p,
		_st.itemPadding.left(),
		_st.itemPadding.top(),
		available);
}

void MainMenu::AccountButton::contextMenuEvent(QContextMenuEvent *e) {
	if (!_menu && IsAltShift(e->modifiers())) {
		_menu = base::make_unique_q<Ui::PopupMenu>(this);
		const auto addAction = [&](const QString &text, Fn<void()> callback) {
			return _menu->addAction(
				text,
				crl::guard(this, std::move(callback)));
		};
		MenuAddMarkAsReadAllChatsAction(&_session->data(), addAction);
		_menu->popup(QCursor::pos());
		return;
	}
	if (&_session->account() == &Core::App().activeAccount() || _menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this);
	_menu->addAction(tr::lng_menu_activate(tr::now), crl::guard(this, [=] {
		Core::App().domain().activate(&_session->account());
	}));
	_menu->addAction(tr::lng_settings_logout(tr::now), crl::guard(this, [=] {
		const auto session = _session;
		const auto callback = [=](Fn<void()> &&close) {
			close();
			Core::App().logout(&session->account());
		};
		Ui::show(Box<ConfirmBox>(
			tr::lng_sure_logout(tr::now),
			tr::lng_settings_logout(tr::now),
			st::attentionBoxButton,
			crl::guard(session, callback)));
	}));
	_menu->popup(QCursor::pos());
}

MainMenu::ToggleAccountsButton::ToggleAccountsButton(QWidget *parent)
: AbstractButton(parent) {
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		Core::App().unreadBadgeChanges()
	) | rpl::start_with_next([=] {
		_unreadBadgeStale = true;
		if (!_toggled) {
			validateUnreadBadge();
			update();
		}
	}, lifetime());

	auto &settings = Core::App().settings();
	if (Core::App().domain().accounts().size() < 2
		&& settings.mainMenuAccountsShown()) {
		settings.setMainMenuAccountsShown(false);
	}
	settings.mainMenuAccountsShownValue(
	) | rpl::filter([=](bool value) {
		return (_toggled != value);
	}) | rpl::start_with_next([=](bool value) {
		_toggled = value;
		_toggledAnimation.start(
			[=] { update(); },
			_toggled ? 0. : 1.,
			_toggled ? 1. : 0.,
			st::slideWrapDuration);
		validateUnreadBadge();
	}, lifetime());
	_toggledAnimation.stop();
}

void MainMenu::ToggleAccountsButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto toggled = _toggledAnimation.value(_toggled ? 1. : 0.);
	const auto x = 0. + width() - st::mainMenuTogglePosition.x();
	const auto y = 0. + height() - st::mainMenuTogglePosition.y();
	const auto size = st::mainMenuToggleSize;
	const auto size2 = size / 2.;
	const auto sqrt2 = sqrt(2.);
	const auto stroke = (st::mainMenuToggleFourStrokes / 4.) / sqrt2;
	const auto left = x - size;
	const auto right = x + size;
	const auto bottom = y + size2;
	const auto top = y - size2;
	constexpr auto kPointCount = 6;
	std::array<QPointF, kPointCount> points = { {
		{ left - stroke, bottom - stroke },
		{ x, bottom - stroke - size - stroke },
		{ right + stroke, bottom - stroke },
		{ right - stroke, bottom + stroke },
		{ x, bottom + stroke - size + stroke },
		{ left + stroke, bottom + stroke }
	} };
	const auto alpha = (toggled - 1.) * M_PI;
	const auto cosalpha = cos(alpha);
	const auto sinalpha = sin(alpha);
	for (auto &point : points) {
		auto px = point.x() - x;
		auto py = point.y() - y;
		point.setX(x + px * cosalpha - py * sinalpha);
		point.setY(y + py * cosalpha + px * sinalpha);
	}
	QPainterPath path;
	path.moveTo(points[0]);
	for (int i = 1; i != kPointCount; ++i) {
		path.lineTo(points[i]);
	}
	path.lineTo(points[0]);

	auto hq = PainterHighQualityEnabler(p);
	p.fillPath(path, st::mainMenuCoverFg);

	paintUnreadBadge(p);
}

void MainMenu::ToggleAccountsButton::paintUnreadBadge(QPainter &p) {
	const auto progress = 1. - _toggledAnimation.value(_toggled ? 1. : 0.);
	if (!progress) {
		return;
	}
	validateUnreadBadge();
	if (_unreadBadge.isEmpty()) {
		return;
	}
	Dialogs::Layout::UnreadBadgeStyle st;

	const auto right = width() - st::mainMenuTogglePosition.x() - st::mainMenuToggleSize * 2;
	const auto top = height() - st::mainMenuTogglePosition.y() - st::mainMenuToggleSize;
	const auto width = _unreadBadgeWidth;
	const auto rectHeight = st.size;
	const auto rectWidth = std::max(width + 2 * st.padding, rectHeight);
	const auto left = right - rectWidth;
	const auto textLeft = left + (rectWidth - width) / 2;
	const auto textTop = top + (st.textTop ? st.textTop : (rectHeight - st.font->height) / 2);

	const auto isFill = IsFilledCover();

	auto hq = PainterHighQualityEnabler(p);
	auto brush = (isFill ? st::mainMenuCloudBg : st::msgServiceBg)->c;
	brush.setAlphaF(progress * brush.alphaF());
	p.setBrush(brush);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(left, top, rectWidth, rectHeight, rectHeight / 2, rectHeight / 2);

	p.setFont(st.font);
	auto pen = (isFill ? st::mainMenuCloudFg : st::msgServiceFg)->c;
	pen.setAlphaF(progress * pen.alphaF());
	p.setPen(pen);
	p.drawText(textLeft, textTop + st.font->ascent, _unreadBadge);
}

void MainMenu::ToggleAccountsButton::validateUnreadBadge() {
	const auto base = st::mainMenuTogglePosition.x()
		+ 2 * st::mainMenuToggleSize;
	if (_toggled) {
		_rightSkip = base;
		return;
	} else if (!_unreadBadgeStale) {
		return;
	}
	_unreadBadge = computeUnreadBadge();

	Dialogs::Layout::UnreadBadgeStyle st;
	_unreadBadgeWidth = st.font->width(_unreadBadge);
	const auto rectHeight = st.size;
	const auto rectWidth = std::max(
		_unreadBadgeWidth + 2 * st.padding,
		rectHeight);
	_rightSkip = base + rectWidth + st::mainMenuToggleSize;
}

QString MainMenu::ToggleAccountsButton::computeUnreadBadge() const {
	const auto state = OtherAccountsUnreadStateCurrent();
	return state.allMuted
		? QString()
		: (state.count > 99)
		? u"99+"_q
		: (state.count > 0)
		? QString::number(state.count)
		: QString();
}

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
, _userpicButton(
	this,
	_controller,
	_controller->session().user(),
	Ui::UserpicButton::Role::Custom,
	st::mainMenuUserpic)
, _toggleAccounts(this)
, _archiveButton(this, st::mainMenuCloudButton)
, _cloudButton(this, st::mainMenuCloudButton)
, _scroll(this, st::defaultSolidScroll)
, _inner(_scroll->setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(_scroll.data())))
, _accounts(_inner->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
	_inner.get(),
	object_ptr<Ui::VerticalLayout>(_inner.get()))))
, _shadow(_inner->add(object_ptr<Ui::SlideWrap<Ui::PlainShadow>>(
	_inner.get(),
	object_ptr<Ui::PlainShadow>(_inner.get()))))
, _menu(_inner->add(
	object_ptr<Ui::Menu>(_inner.get(), st::mainMenu),
	{ 0, st::mainMenuSkip, 0, 0 }))
, _footer(_inner->add(object_ptr<Ui::RpWidget>(_inner.get())))
, _telegram(
	Ui::CreateChild<Ui::FlatLabel>(_footer.get(), st::mainMenuTelegramLabel))
, _version(
	Ui::CreateChild<Ui::FlatLabel>(
		_footer.get(),
		st::mainMenuVersionLabel)) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	setupArchiveButton();
	setupCloudButton();
	setupUserpicButton();
	setupAccountsToggle();
	setupAccounts();

	_nightThemeSwitch.setCallback([this] {
		if (const auto action = *_nightThemeAction) {
			const auto nightMode = Window::Theme::IsNightMode();
			if (action->isChecked() != nightMode) {
				Window::Theme::ToggleNightMode();
				Window::Theme::KeepApplied();
			}
		}
	});

	_footer->heightValue(
	) | rpl::start_with_next([=] {
		_telegram->moveToLeft(st::mainMenuFooterLeft, _footer->height() - st::mainMenuTelegramBottom - _telegram->height());
		_version->moveToLeft(st::mainMenuFooterLeft, _footer->height() - st::mainMenuVersionBottom - _version->height());
	}, _footer->lifetime());

	rpl::combine(
		heightValue(),
		_inner->heightValue()
	) | rpl::start_with_next([=] {
		updateInnerControlsGeometry();
	}, _inner->lifetime());

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

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

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
			if (const auto action = *_nightThemeAction) {
				const auto nightMode = Window::Theme::IsNightMode();
				if (action->isChecked() != nightMode) {
					action->setChecked(nightMode);
					_menu->finishAnimating();
				}
			}
		}
		if (update.type == Window::Theme::BackgroundUpdate::Type::New) {
			refreshBackground();
		}
	});
	updatePhone();
	initResetScaleButton();
}

void MainMenu::setupArchiveButton() {
	const auto controller = _controller;
	const auto folder = [=] {
		return controller->session().data().folderLoaded(Data::Folder::kId);
	};
	const auto showArchive = [=] {
		if (const auto f = folder()) {
			controller->openFolder(f);
			Ui::hideSettingsAndLayer();
		}
	};
	const auto checkArchive = [=] {
		const auto f = folder();
		return f
			&& !f->chatsList()->empty()
			&& controller->session().settings().archiveInMainMenu();
	};
	_archiveButton->setVisible(checkArchive());
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
		const auto addAction = [&](const QString &text, Fn<void()> callback) {
			return _contextMenu->addAction(text, std::move(callback));
		};

		const auto hide = [=] {
			controller->session().settings().setArchiveInMainMenu(false);
			controller->session().saveSettingsDelayed();
			Ui::hideSettingsAndLayer();
		};
		addAction(tr::lng_context_archive_to_list(tr::now), std::move(hide));

		MenuAddMarkAsReadChatListAction(
			[f = folder()] { return f->chatsList(); },
			addAction);

		_contextMenu->popup(QCursor::pos());
	}, _archiveButton->lifetime());

	controller->session().data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return folder && (folder->id() == Data::Folder::kId);
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		const auto isArchiveVisible = checkArchive();
		_archiveButton->setVisible(isArchiveVisible);
		if (!isArchiveVisible) {
			_contextMenu = nullptr;
		}
		update();
	}, lifetime());
}

void MainMenu::setupCloudButton() {
	_cloudButton->setClickedCallback([=] {
		_controller->content()->choosePeer(
			_controller->session().userPeerId(),
			ShowAtUnreadMsgId);
	});
	_cloudButton->show();
}

void MainMenu::setupUserpicButton() {
	_userpicButton->setClickedCallback([=] { toggleAccounts(); });
	_userpicButton->show();
}

void MainMenu::toggleAccounts() {
	auto &settings = Core::App().settings();
	const auto shown = !settings.mainMenuAccountsShown();
	settings.setMainMenuAccountsShown(shown);
	Core::App().saveSettingsDelayed();
}

void MainMenu::setupAccounts() {
	const auto inner = _accounts->entity();

	inner->add(object_ptr<Ui::FixedHeightWidget>(inner, st::mainMenuSkip));
	_addAccount = setupAddAccount(inner);
	inner->add(object_ptr<Ui::FixedHeightWidget>(inner, st::mainMenuSkip));

	rpl::single(
		rpl::empty_value()
	) | rpl::then(Core::App().domain().accountsChanges(
	)) | rpl::start_with_next([=] {
		const auto &list = Core::App().domain().accounts();
		const auto exists = [&](not_null<Main::Account*> account) {
			for (const auto &[index, existing] : list) {
				if (account == existing.get()) {
					return true;
				}
			}
			return false;
		};
		for (auto i = _watched.begin(); i != _watched.end();) {
			if (!exists(i->first)) {
				i = _watched.erase(i);
			} else {
				++i;
			}
		}
		for (const auto &[index, account] : list) {
			if (_watched.emplace(account.get()).second) {
				account->sessionChanges(
				) | rpl::start_with_next([=](Main::Session *session) {
					rebuildAccounts();
				}, lifetime());
			}
		}
		rebuildAccounts();
	}, lifetime());

	_accounts->toggleOn(Core::App().settings().mainMenuAccountsShownValue());
	_accounts->finishAnimating();

	_shadow->setDuration(0)->toggleOn(_accounts->shownValue());
}

void MainMenu::rebuildAccounts() {
	const auto inner = _accounts->entity();

	auto count = 0;
	for (const auto &[index, pointer] : Core::App().domain().accounts()) {
		const auto account = pointer.get();
		auto i = _watched.find(account);
		Assert(i != _watched.end());

		auto &button = i->second;
		if (!account->sessionExists()) {
			button = nullptr;
		} else if (!button) {
			button.reset(inner->insert(
				++count,
				object_ptr<AccountButton>(inner, account)));
			button->setClickedCallback([=] {
				if (account == &Core::App().domain().active()) {
					closeLayer();
					return;
				}
				auto activate = [=, guard = _accountSwitchGuard.make_guard()]{
					if (guard) {
						Core::App().domain().maybeActivate(account);
					}
				};
				base::call_delayed(
					st::defaultRippleAnimation.hideDuration,
					account,
					std::move(activate));
			});
		} else {
			++count;
		}
	}
	inner->resizeToWidth(_accounts->width());

	_addAccount->toggle(
		(count < Main::Domain::kMaxAccounts),
		anim::type::instant);
}

not_null<Ui::SlideWrap<Ui::RippleButton>*> MainMenu::setupAddAccount(
		not_null<Ui::VerticalLayout*> container) {
	const auto result = container->add(
		object_ptr<Ui::SlideWrap<Ui::RippleButton>>(
			container.get(),
			object_ptr<Ui::RippleButton>(
				container.get(),
				st::defaultRippleAnimation)))->setDuration(0);
	const auto st = &st::mainMenu;
	const auto height = st->itemPadding.top()
		+ st->itemStyle.font->height
		+ st->itemPadding.bottom();
	const auto button = result->entity();
	button->resize(button->width(), height);

	button->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(button);
		const auto over = button->isOver();
		p.fillRect(button->rect(), over ? st->itemBgOver : st->itemBg);
		button->paintRipple(p, 0, 0);
		const auto &icon = over
			? st::mainMenuAddAccountOver
			: st::mainMenuAddAccount;
		icon.paint(p, st->itemIconPosition, width());
		p.setPen(over ? st->itemFgOver : st->itemFg);
		p.setFont(st->itemStyle.font);
		p.drawTextLeft(
			st->itemPadding.left(),
			st->itemPadding.top(),
			width(),
			tr::lng_menu_add_account(tr::now));
	}, button->lifetime());

	const auto add = [=](MTP::Environment environment) {
		Core::App().preventOrInvoke([=] {
			Core::App().domain().addActivated(environment);
		});
	};

	button->setAcceptBoth(true);
	button->clicks(
	) | rpl::start_with_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			add(MTP::Environment::Production);
			return;
		} else if (which != Qt::RightButton
			|| !IsAltShift(button->clickModifiers())) {
			return;
		}
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(this);
		_contextMenu->addAction("Production Server", [=] {
			add(MTP::Environment::Production);
		});
		_contextMenu->addAction("Test Server", [=] {
			add(MTP::Environment::Test);
		});
		_contextMenu->popup(QCursor::pos());
	}, button->lifetime());

	return result;
}

void MainMenu::setupAccountsToggle() {
	_toggleAccounts->show();
	_toggleAccounts->setClickedCallback([=] { toggleAccounts(); });
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
			Ui::show(PrepareContactsBox(controller));
		}, &st::mainMenuContacts, &st::mainMenuContactsOver);
		if (_controller->session().serverConfig().phoneCallsEnabled.current()) {
			_menu->addAction(tr::lng_menu_calls(tr::now), [=] {
				Ui::show(Box<PeerListBox>(std::make_unique<Calls::BoxController>(controller), [=](not_null<PeerListBox*> box) {
					box->addButton(tr::lng_close(), [=] {
						box->closeBox();
					});
					box->addTopButton(st::callSettingsButton, [=] {
						controller->showSettings(
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
			_controller->session().saveSettings();
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
		if (Window::Theme::Background()->editingTheme()) {
			Ui::show(Box<InformBox>(
				tr::lng_theme_editor_cant_change_theme(tr::now)));
			return;
		}
		const auto weak = MakeWeak(this);
		const auto toggle = [=] {
			if (!weak) {
				Window::Theme::ToggleNightMode();
				Window::Theme::KeepApplied();
			} else if (auto action = *_nightThemeAction) {
				action->setChecked(!action->isChecked());
				_nightThemeSwitch.callOnce(st::mainMenu.itemToggle.duration);
			}
		};
		Window::Theme::ToggleNightModeWithConfirmation(
			&_controller->window(),
			toggle);
	}, &st::mainMenuNightMode, &st::mainMenuNightModeOver);
	*_nightThemeAction = action;
	action->setCheckable(true);
	action->setChecked(Window::Theme::IsNightMode());
	Core::App().settings().systemDarkModeValue(
	) | rpl::start_with_next([=](std::optional<bool> darkMode) {
		const auto darkModeEnabled = Core::App().settings().systemDarkModeEnabled();
		if (darkModeEnabled && darkMode.has_value()) {
			action->setChecked(*darkMode);
		}
	}, lifetime());
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
	_inner->resizeToWidth(width());
	updateControlsGeometry();
}

void MainMenu::updateControlsGeometry() {
	_userpicButton->moveToLeft(
		st::mainMenuUserpicLeft,
		st::mainMenuUserpicTop);
	if (_resetScaleButton) {
		_resetScaleButton->moveToRight(0, 0);
		_cloudButton->moveToRight(_resetScaleButton->width(), 0);
		_archiveButton->moveToRight(
			_resetScaleButton->width() + _cloudButton->width(),
			0);
	} else {
		const auto right = st::mainMenuTogglePosition.x()
			- (_cloudButton->width() / 2);
		const auto top = st::mainMenuUserpicTop
			- (_cloudButton->height() - st::mainMenuCloudSize) / 2;
		_cloudButton->moveToRight(right, top);
		_archiveButton->moveToRight(right + _cloudButton->width(), top);
	}
	_toggleAccounts->setGeometry(
		0,
		st::mainMenuCoverNameTop,
		width(),
		st::mainMenuCoverHeight - st::mainMenuCoverNameTop);
	const auto top = st::mainMenuCoverHeight;
	_scroll->setGeometry(0, top, width(), height() - top);
	updateInnerControlsGeometry();
}

void MainMenu::updateInnerControlsGeometry() {
	const auto contentHeight = _accounts->height()
		+ _shadow->height()
		+ st::mainMenuSkip
		+ _menu->height();
	const auto available = height() - st::mainMenuCoverHeight - contentHeight;
	const auto footerHeight = std::max(
		available,
		st::mainMenuTelegramBottom + _telegram->height() + st::mainMenuSkip);
	if (_footer->height() != footerHeight) {
		_footer->resize(_footer->width(), footerHeight);
	}
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

	const auto isFill = IsFilledCover();
	if (!isFill && !_background.isNull()) {
		PainterHighQualityEnabler hq(p);
		p.drawImage(0, 0, _background);
	}

	if (!cover.isEmpty()) {
		const auto widthText = width()
			- st::mainMenuCoverTextLeft
			- _toggleAccounts->rightSkip();

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

		// Draw Saved Messages button.
		if (!_cloudButton->isHidden()) {
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

OthersUnreadState OtherAccountsUnreadStateCurrent() {
	auto &app = Core::App();
	const auto active = &app.activeAccount();
	auto allMuted = true;
	for (const auto &[index, account] : app.domain().accounts()) {
		if (account.get() == active) {
			continue;
		} else if (const auto session = account->maybeSession()) {
			if (!session->data().unreadBadgeMuted()) {
				allMuted = false;
				break;
			}
		}
	}
	return {
		.count = (app.unreadBadge() - active->session().data().unreadBadge()),
		.allMuted = allMuted,
	};
}

rpl::producer<OthersUnreadState> OtherAccountsUnreadState() {
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		Core::App().unreadBadgeChanges()
	) | rpl::map(OtherAccountsUnreadStateCurrent);
}


} // namespace Window
