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
#include "ui/chat/chat_theme.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "ui/empty_userpic.h"
#include "dialogs/ui/dialogs_layout.h"
#include "base/call_delayed.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "support/support_templates.h"
#include "settings/settings_common.h"
#include "base/qt_signal_producer.h"
#include "boxes/about_box.h"
#include "ui/boxes/confirm_box.h"
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
#include "styles/style_window.h"
#include "styles/style_widgets.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h" // infoTopBarMenu
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace {

constexpr auto kMinDiffIntensity = 0.25;

[[nodiscard]] float64 IntensityOfColor(QColor color) {
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
		|| !background->gradientForFill().isNull()
		|| background->paper().isPattern()
		|| Data::IsLegacy1DefaultWallPaper(background->paper());
}

[[nodiscard]] bool IsAltShift(Qt::KeyboardModifiers modifiers) {
	return (modifiers & Qt::ShiftModifier) && (modifiers & Qt::AltModifier);
}

void ShowCallsBox(not_null<Window::SessionController*> window) {
	auto controller = std::make_unique<Calls::BoxController>(window);
	const auto initBox = [
		window,
		controller = controller.get()
	](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		using MenuPointer = base::unique_qptr<Ui::PopupMenu>;
		const auto menu = std::make_shared<MenuPointer>();
		const auto menuButton = box->addTopButton(st::infoTopBarMenu);
		menuButton->setClickedCallback([=] {
			*menu = base::make_unique_q<Ui::PopupMenu>(
				menuButton,
				st::popupMenuWithIcons);
			const auto showSettings = [=] {
				window->showSettings(
					Settings::Type::Calls,
					Window::SectionShow(anim::type::instant));
			};
			const auto clearAll = crl::guard(box, [=] {
				box->getDelegate()->show(Box(Calls::ClearCallsBox, window));
			});
			(*menu)->addAction(
				tr::lng_settings_section_call_settings(tr::now),
				showSettings,
				&st::menuIconSettings);
			if (controller->delegate()->peerListFullRowsCount() > 0) {
				(*menu)->addAction(
					tr::lng_call_box_clear_all(tr::now),
					clearAll,
					&st::menuIconDelete);
			}
			(*menu)->popup(QCursor::pos());
			return true;
		});
	};
	window->show(Box<PeerListBox>(std::move(controller), initBox));
}

[[nodiscard]] std::vector<not_null<Main::Account*>> OrderedAccounts() {
	const auto order = Core::App().settings().accountsOrder();
	auto accounts = ranges::views::all(
		Core::App().domain().accounts()
	) | ranges::views::transform([](const Main::Domain::AccountWithIndex &a) {
		return not_null{ a.account.get() };
	}) | ranges::to_vector;
	ranges::stable_sort(accounts, [&](
			not_null<Main::Account*> a,
			not_null<Main::Account*> b) {
		const auto aIt = a->sessionExists()
			? ranges::find(order, a->session().uniqueId())
			: end(order);
		const auto bIt = b->sessionExists()
			? ranges::find(order, b->session().uniqueId())
			: end(order);
		return aIt < bIt;
	});
	return accounts;
}

} // namespace

namespace Window {

struct UnreadBadge {
	int count = 0;
	bool muted = false;
};

void AddUnreadBadge(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<UnreadBadge> value) {
	struct State {
		State(QWidget *parent) : widget(parent) {
			widget.setAttribute(Qt::WA_TransparentForMouseEvents);
		}

		Ui::RpWidget widget;
		Dialogs::Ui::UnreadBadgeStyle st;
		int count = 0;
		QString string;
	};
	const auto state = button->lifetime().make_state<State>(button);

	std::move(
		value
	) | rpl::start_with_next([=](UnreadBadge badge) {
		state->st.muted = badge.muted;
		state->count = badge.count;
		if (!badge.count) {
			state->widget.hide();
			return;
		}
		state->string = (state->count > 99)
			? "99+"
			: QString::number(state->count);
		state->widget.resize(CountUnreadBadgeSize(state->string, state->st));
		if (state->widget.isHidden()) {
			state->widget.show();
		}
	}, state->widget.lifetime());

	state->widget.paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(&state->widget);
		Dialogs::Ui::PaintUnreadBadge(
			p,
			state->string,
			state->widget.width(),
			0,
			state->st);
	}, state->widget.lifetime());

	rpl::combine(
		button->sizeValue(),
		state->widget.sizeValue(),
		state->widget.shownValue()
	) | rpl::start_with_next([=](QSize outer, QSize inner, bool shown) {
		auto padding = button->st().padding;
		if (shown) {
			state->widget.moveToRight(
				padding.right(),
				(outer.height() - inner.height()) / 2,
				outer.width());
			padding.setRight(
				padding.right() + inner.width() + button->st().font->spacew);
		}
		button->setPaddingOverride(padding);
	}, state->widget.lifetime());
}

[[nodiscard]] object_ptr<Ui::SettingsButton> MakeAccountButton(
		QWidget *parent,
		not_null<Main::Account*> account,
		Fn<void()> callback) {
	const auto active = (account == &Core::App().activeAccount());
	const auto session = &account->session();
	const auto user = session->user();

	auto text = rpl::single(
		user->name
	) | rpl::then(session->changes().realtimeNameUpdates(
		user
	) | rpl::map([=] {
		return user->name;
	}));
	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		std::move(text),
		st::mainMenuAddAccountButton);
	const auto raw = result.data();

	struct State {
		State(QWidget *parent) : userpic(parent) {
			userpic.setAttribute(Qt::WA_TransparentForMouseEvents);
		}

		Ui::RpWidget userpic;
		std::shared_ptr<Data::CloudImageView> view;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = raw->lifetime().make_state<State>(raw);

	if (!active) {
		AddUnreadBadge(raw, rpl::single(
			rpl::empty_value()
		) | rpl::then(
			session->data().unreadBadgeChanges()
		) | rpl::map([=] {
			auto &owner = session->data();
			return UnreadBadge{
				owner.unreadBadge(),
				owner.unreadBadgeMuted(),
			};
		}));
	}

	const auto userpicSkip = 2 * st::mainMenuAccountLine + st::lineWidth;
	const auto userpicSize = st::mainMenuAccountSize
		+ userpicSkip * 2;
	raw->heightValue(
	) | rpl::start_with_next([=](int height) {
		const auto left = st::mainMenuAddAccountButton.iconLeft
			+ (st::mainMenuAddAccount.width() - userpicSize) / 2;
		const auto top = (height - userpicSize) / 2;
		state->userpic.setGeometry(left, top, userpicSize, userpicSize);
	}, state->userpic.lifetime());

	state->userpic.paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(&state->userpic);
		const auto size = st::mainMenuAccountSize;
		const auto line = st::mainMenuAccountLine;
		const auto skip = 2 * line + st::lineWidth;
		const auto full = size + skip * 2;
		user->paintUserpicLeft(p, state->view, skip, skip, full, size);
		if (active) {
			const auto shift = st::lineWidth + (line * 0.5);
			const auto diameter = full - 2 * shift;
			const auto rect = QRectF(shift, shift, diameter, diameter);
			auto hq = PainterHighQualityEnabler(p);
			auto pen = st::settingsIconBg4->p; // The same as '+' in add.
			pen.setWidthF(line);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(rect);
		}
	}, state->userpic.lifetime());

	raw->setAcceptBoth(true);
	raw->clicks(
	) | rpl::start_with_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			callback();
			return;
		} else if (which != Qt::RightButton) {
			return;
		}
		const auto addAction = [&](
				const QString &text,
				Fn<void()> callback,
				const style::icon *icon) {
			return state->menu->addAction(
				text,
				crl::guard(raw, std::move(callback)),
				icon);
		};
		if (!state->menu && IsAltShift(raw->clickModifiers())) {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				raw,
				st::popupMenuWithIcons);
			MenuAddMarkAsReadAllChatsAction(&session->data(), addAction);
			state->menu->popup(QCursor::pos());
			return;
		}
		if (&session->account() == &Core::App().activeAccount()
			|| state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			raw,
			st::popupMenuWithIcons);
		addAction(tr::lng_menu_activate(tr::now), [=] {
			Core::App().domain().activate(&session->account());
		}, &st::menuIconProfile);
		addAction(tr::lng_settings_logout(tr::now), [=] {
			const auto callback = [=](Fn<void()> &&close) {
				close();
				Core::App().logoutWithChecks(&session->account());
			};
			Ui::show(Box<Ui::ConfirmBox>(
				tr::lng_sure_logout(tr::now),
				tr::lng_settings_logout(tr::now),
				st::attentionBoxButton,
				crl::guard(session, callback)));
		}, &st::menuIconLeave);
		state->menu->popup(QCursor::pos());
	}, raw->lifetime());

	return result;
}

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
	Dialogs::Ui::UnreadBadgeStyle st;

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

	Dialogs::Ui::UnreadBadgeStyle st;
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
	object_ptr<Ui::VerticalLayout>(_inner.get()),
	{ 0, st::mainMenuSkip, 0, 0 }))
, _footer(_inner->add(object_ptr<Ui::RpWidget>(_inner.get())))
, _telegram(
	Ui::CreateChild<Ui::FlatLabel>(_footer.get(), st::mainMenuTelegramLabel))
, _version(
	Ui::CreateChild<Ui::FlatLabel>(
		_footer.get(),
		st::mainMenuVersionLabel)) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	setupUserpicButton();
	setupAccountsToggle();
	setupAccounts();
	setupArchive();
	setupMenu();

	_nightThemeSwitch.setCallback([this] {
		Expects(_nightThemeToggle != nullptr);

		const auto nightMode = Window::Theme::IsNightMode();
		if (_nightThemeToggle->toggled() != nightMode) {
			Window::Theme::ToggleNightMode();
			Window::Theme::KeepApplied();
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
	refreshBackground();

	_telegram->setMarkedText(Ui::Text::Link(
		qsl("Telegram Desktop"),
		qsl("https://desktop.telegram.org")));
	_telegram->setLinksTrusted();
	_version->setMarkedText(
		Ui::Text::Link(
			tr::lng_settings_current_version(
				tr::now,
				lt_version,
				currentVersionText()),
			1) // Link 1.
		.append(QChar(' '))
		.append(QChar(8211))
		.append(QChar(' '))
		.append(Ui::Text::Link(tr::lng_menu_about(tr::now), 2))); // Link 2.
	_version->setLink(
		1,
		std::make_shared<UrlClickHandler>(Core::App().changelogLink()));
	_version->setLink(
		2,
		std::make_shared<LambdaClickHandler>([=] {
			controller->show(Box<AboutBox>());
		}));

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

	using Window::Theme::BackgroundUpdate;
	Window::Theme::Background()->updates(
	) | rpl::start_with_next([=](const BackgroundUpdate &update) {
		if (update.type == BackgroundUpdate::Type::ApplyingTheme) {
			_nightThemeSwitches.fire(Window::Theme::IsNightMode());
		}
		if (update.type == BackgroundUpdate::Type::New) {
			refreshBackground();
		}
	}, lifetime());
	updatePhone();
	initResetScaleButton();
}

void MainMenu::setupArchive() {
	using namespace Settings;

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

	const auto wrap = _menu->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_menu,
			object_ptr<Ui::VerticalLayout>(_menu)));
	const auto inner = wrap->entity();
	wrap->toggle(checkArchive(), anim::type::instant);

	const auto button = AddButton(
		inner,
		tr::lng_archived_name(),
		st::mainMenuButton,
		{ &st::settingsIconArchive, kIconGray });
	inner->add(
		object_ptr<Ui::PlainShadow>(inner),
		{ 0, st::mainMenuSkip, 0, st::mainMenuSkip });
	button->setAcceptBoth(true);
	button->clicks(
	) | rpl::start_with_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			showArchive();
			return;
		} else if (which != Qt::RightButton) {
			return;
		}
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		const auto addAction = [&](
				const QString &text,
				Fn<void()> callback,
				const style::icon *icon) {
			return _contextMenu->addAction(
				text,
				std::move(callback),
				icon);
		};

		const auto hide = [=] {
			controller->session().settings().setArchiveInMainMenu(false);
			controller->session().saveSettingsDelayed();
			Ui::hideSettingsAndLayer();
		};
		addAction(
			tr::lng_context_archive_to_list(tr::now),
			std::move(hide),
			&st::menuIconFromMainMenu);

		MenuAddMarkAsReadChatListAction(
			[f = folder()] { return f->chatsList(); },
			addAction);

		_contextMenu->popup(QCursor::pos());
	}, button->lifetime());

	controller->session().data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return folder && (folder->id() == Data::Folder::kId);
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		const auto isArchiveVisible = checkArchive();
		wrap->toggle(isArchiveVisible, anim::type::normal);
		if (!isArchiveVisible) {
			_contextMenu = nullptr;
		}
		update();
	}, lifetime());
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
	const auto inner = _accounts->entity()->insert(
		1, // After skip with the fixed height.
		object_ptr<Ui::VerticalLayout>(_accounts.get()));

	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(inner);
	_reorder->updates(
	) | rpl::start_with_next([=](Ui::VerticalLayoutReorder::Single data) {
		using State = Ui::VerticalLayoutReorder::State;
		if (data.state == State::Started) {
			++_reordering;
		} else {
			Ui::PostponeCall(inner, [=] {
				--_reordering;
			});
			if (data.state == State::Applied) {
				std::vector<uint64> order;
				order.reserve(inner->count());
				for (auto i = 0; i < inner->count(); i++) {
					for (const auto &[account, button] : _watched) {
						if (button.get() == inner->widgetAt(i)) {
							order.push_back(account->session().uniqueId());
						}
					}
				}
				Core::App().settings().setAccountsOrder(order);
				Core::App().saveSettings();
			}
		}
	}, inner->lifetime());

	for (const auto &account : OrderedAccounts()) {
		auto i = _watched.find(account);
		Assert(i != _watched.end());

		auto &button = i->second;
		if (!account->sessionExists()) {
			button = nullptr;
		} else if (!button) {
			button.reset(inner->add(MakeAccountButton(inner, account, [=] {
				if (_reordering) {
					return;
				}
				if (account == &Core::App().domain().active()) {
					closeLayer();
					return;
				}
				auto activate = [=, guard = _accountSwitchGuard.make_guard()]{
					if (guard) {
						_reorder->finishReordering();
						Core::App().domain().maybeActivate(account);
					}
				};
				base::call_delayed(
					st::defaultRippleAnimation.hideDuration,
					account,
					std::move(activate));
			})));
		}
	}
	inner->resizeToWidth(_accounts->width());

	_addAccount->toggle(
		(inner->count() < Main::Domain::kMaxAccounts),
		anim::type::instant);

	_reorder->start();
}

not_null<Ui::SlideWrap<Ui::SettingsButton>*> MainMenu::setupAddAccount(
		not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto result = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container.get(),
			CreateButton(
				container.get(),
				tr::lng_menu_add_account(),
				st::mainMenuAddAccountButton,
				{
					&st::mainMenuAddAccount,
					kIconLightBlue,
					IconType::Round,
				})))->setDuration(0);
	const auto button = result->entity();

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

void MainMenu::setupMenu() {
	using namespace Settings;

	const auto controller = _controller;
	const auto addAction = [&](
			rpl::producer<QString> text,
			IconDescriptor &&descriptor) {
		return AddButton(
			_menu,
			std::move(text),
			st::mainMenuButton,
			std::move(descriptor));
	};
	if (!_controller->session().supportMode()) {
		addAction(
			tr::lng_create_group_title(),
			{ &st::settingsIconNewGroup, kIconLightBlue }
		)->setClickedCallback([=] {
			controller->showNewGroup();
		});
		addAction(
			tr::lng_create_channel_title(),
			{ &st::settingsIconNewChannel, kIconLightOrange }
		)->setClickedCallback([=] {
			controller->showNewChannel();
		});
		addAction(
			tr::lng_menu_contacts(),
			{ &st::settingsIconContacts, kIconRed }
		)->setClickedCallback([=] {
			controller->show(PrepareContactsBox(controller));
		});
		addAction(
			tr::lng_menu_calls(),
			{ &st::settingsIconCalls, kIconGreen }
		)->setClickedCallback([=] {
			ShowCallsBox(controller);
		});
		addAction(
			tr::lng_saved_messages(),
			{ &st::settingsIconSavedMessages, kIconLightBlue }
		)->setClickedCallback([=] {
			controller->content()->choosePeer(
				controller->session().userPeerId(),
				ShowAtUnreadMsgId);
		});
	} else {
		addAction(
			tr::lng_profile_add_contact(),
			{ &st::settingsIconContacts, kIconRed }
		)->setClickedCallback([=] {
			controller->showAddContact();
		});
		addAction(
			rpl::single(u"Fix chats order"_q),
			{ &st::settingsIconKey, kIconGreen }
		)->toggleOn(rpl::single(
			_controller->session().settings().supportFixChatsOrder()
		))->toggledChanges(
		) | rpl::start_with_next([=](bool fix) {
			_controller->session().settings().setSupportFixChatsOrder(fix);
			_controller->session().saveSettings();
		}, _menu->lifetime());
		addAction(
			rpl::single(u"Reload templates"_q),
			{ &st::settingsIconReload, kIconLightBlue }
		)->setClickedCallback([=] {
			_controller->session().supportTemplates().reload();
		});
	}
	addAction(
		tr::lng_menu_settings(),
		{ &st::settingsIconSettings, kIconPurple }
	)->setClickedCallback([=] {
		controller->showSettings();
	});

	_nightThemeToggle = addAction(
		tr::lng_menu_night_mode(),
		{ &st::settingsIconNight, kIconDarkBlue }
	)->toggleOn(_nightThemeSwitches.events_starting_with(
		Window::Theme::IsNightMode()
	));
	_nightThemeToggle->toggledChanges(
	) | rpl::filter([=](bool night) {
		return (night != Window::Theme::IsNightMode());
	}) | rpl::start_with_next([=](bool night) {
		if (Window::Theme::Background()->editingTheme()) {
			_nightThemeSwitches.fire(!night);
			controller->show(Box<Ui::InformBox>(
				tr::lng_theme_editor_cant_change_theme(tr::now)));
			return;
		}
		const auto weak = MakeWeak(this);
		const auto toggle = [=] {
			if (!weak) {
				Window::Theme::ToggleNightMode();
				Window::Theme::KeepApplied();
			} else {
				_nightThemeSwitch.callOnce(st::mainMenu.itemToggle.duration);
			}
		};
		Window::Theme::ToggleNightModeWithConfirmation(
			&_controller->window(),
			toggle);
	}, _nightThemeToggle->lifetime());

	Core::App().settings().systemDarkModeValue(
	) | rpl::start_with_next([=](std::optional<bool> darkMode) {
		const auto darkModeEnabled
			= Core::App().settings().systemDarkModeEnabled();
		if (darkModeEnabled && darkMode.has_value()) {
			_nightThemeSwitches.fire_copy(*darkMode);
		}
	}, _nightThemeToggle->lifetime());

	updatePhone();
}

void MainMenu::refreshBackground() {
	if (IsFilledCover()) {
		return;
	}
	const auto fill = QSize(st::mainMenuWidth, st::mainMenuCoverHeight);
	const auto intensityText = IntensityOfColor(st::mainMenuCoverFg->c);
	const auto background = Window::Theme::Background();
	const auto &prepared = background->prepared();

	const auto rects = Ui::ComputeChatBackgroundRects(
		fill,
		prepared.size());

	auto backgroundImage = QImage(
		fill * cIntRetinaFactor(),
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

	// Background image.
	p.drawImage(rects.to, prepared, rects.from);

	// Cut off the part of the background that is under text.
	const QRect underText(
		st::mainMenuCoverNameLeft,
		st::mainMenuCoverNameTop,
		std::max(
			st::semiboldFont->width(
				_controller->session().user()->nameText().toString()),
			st::normalFont->width(_phoneText)),
		st::semiboldFont->height * 2);
	if (IsShadowShown(backgroundImage, underText, intensityText)) {
		drawShadow(p);
	}
	_background = backgroundImage;
}

void MainMenu::resizeEvent(QResizeEvent *e) {
	_inner->resizeToWidth(width());
	updateControlsGeometry();
}

void MainMenu::updateControlsGeometry() {
	_userpicButton->moveToLeft(
		st::mainMenuUserpicLeft,
		st::mainMenuUserpicTop);
	if (_resetScaleButton) {
		_resetScaleButton->moveToRight(0, 0);
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
	_phoneText = Ui::FormatPhone(_controller->session().user()->phone());
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
			- st::mainMenuCoverNameLeft
			- _toggleAccounts->rightSkip();

		if (isFill) {
			p.fillRect(cover, st::mainMenuCoverBg);
		}
		p.setPen(st::mainMenuCoverFg);
		p.setFont(st::semiboldFont);
		_controller->session().user()->nameText().drawLeftElided(
			p,
			st::mainMenuCoverNameLeft,
			st::mainMenuCoverNameTop,
			widthText,
			width());
		p.setFont(st::normalFont);
		p.drawTextLeft(
			st::mainMenuCoverStatusLeft,
			st::mainMenuCoverStatusTop,
			width(),
			_phoneText);
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
			base::qt_signal_producer(screen, &QScreen::availableGeometryChanged)
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
				Core::Restart();
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
