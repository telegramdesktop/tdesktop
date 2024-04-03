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
#include "window/window_controller.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/snowflakes.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/unread_badge_paint.h"
#include "inline_bots/bot_attach_web_view.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "support/support_templates.h"
#include "settings/settings_advanced.h"
#include "settings/settings_calls.h"
#include "settings/settings_information.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/stories/info_stories_widget.h"
#include "info/info_memento.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "boxes/about_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/premium_preview_box.h"
#include "calls/calls_box_controller.h"
#include "lang/lang_keys.h"
#include "core/click_handler_types.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtproto_config.h"
#include "data/data_chat.h"
#include "data/data_document_media.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_stories.h"
#include "mainwidget.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_window.h"
#include "styles/style_settings.h"
#include "styles/style_info.h" // infoTopBarMenu
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Window {
namespace {

constexpr auto kPlayStatusLimit = 2;

class VersionLabel final
	: public Ui::FlatLabel
	, public Ui::AbstractTooltipShower {
public:
	using Ui::FlatLabel::FlatLabel;

	void clickHandlerActiveChanged(
			const ClickHandlerPtr &action,
			bool active) override {
		update();
		if (active && action && !action->dragText().isEmpty()) {
			Ui::Tooltip::Show(1000, this);
		} else {
			Ui::Tooltip::Hide();
		}
	}

	QString tooltipText() const override {
		return u"Build date: %1."_q.arg(__DATE__);
	}

	QPoint tooltipPos() const override {
		return QCursor::pos();
	}

	bool tooltipWindowActive() const override {
		return Ui::AppInFocus() && Ui::InFocusChain(window());
	}

};

not_null<Ui::SettingsButton*> AddMyChannelsBox(
		not_null<Ui::SettingsButton*> button,
		not_null<SessionController*> controller,
		bool chats) {
	button->setAcceptBoth(true);

	const auto myChannelsBox = [=](not_null<Ui::GenericBox*> box) {
		box->setTitle(chats
			? tr::lng_notification_groups()
			: tr::lng_notification_channels());

		const auto st = box->lifetime().make_state<style::UserpicButton>(
			st::defaultUserpicButton);
		st->photoSize = st::defaultPeerListItem.photoSize;
		st->size = QSize(st->photoSize, st->photoSize);

		class Button final : public Ui::SettingsButton {
		public:
			using Ui::SettingsButton::SettingsButton;

			void setPeer(not_null<PeerData*> p) {
				const auto c = p->asChannel();
				const auto g = p->asChat();
				_text.setText(st::defaultPeerListItem.nameStyle, p->name());
				const auto count = c ? c->membersCount() : g->count;
				_status.setText(
					st::defaultTextStyle,
					!p->username().isEmpty()
						? ('@' + p->username())
						: count
						? tr::lng_chat_status_subscribers(
							tr::now,
							lt_count,
							count)
						: QString());
			}

			int resizeGetHeight(int) override {
				return st::defaultPeerListItem.height;
			}

			void paintEvent(QPaintEvent *e) override {
				Ui::SettingsButton::paintEvent(e);
				auto p = Painter(this);
				const auto &st = st::defaultPeerListItem;
				const auto availableWidth = width()
					- st::boxRowPadding.right()
					- st.namePosition.x();
				p.setPen(st.nameFg);
				auto context = Ui::Text::PaintContext{
					.position = st.namePosition,
					.outerWidth = availableWidth,
					.availableWidth = availableWidth,
					.elisionLines = 1,
				};
				_text.draw(p, context);
				p.setPen(st.statusFg);
				context.position = st.statusPosition;
				_status.draw(p, context);
			}

		private:
			Ui::Text::String _text;
			Ui::Text::String _status;

		};

		const auto add = [&](not_null<PeerData*> peer) {
			const auto row = box->addRow(
				object_ptr<Button>(box, rpl::single(QString())),
				{});
			row->setPeer(peer);
			row->setClickedCallback([=] {
				controller->showPeerHistory(peer);
			});
			using Button = Ui::UserpicButton;
			const auto userpic = Ui::CreateChild<Button>(row, peer, *st);
			userpic->move(st::defaultPeerListItem.photoPosition);
			userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		};

		const auto &data = controller->session().data();
		if (chats) {
			auto ids = std::vector<PeerId>();
			data.enumerateGroups([&](not_null<PeerData*> peer) {
				peer = peer->migrateToOrMe();
				const auto c = peer->asChannel();
				const auto g = peer->asChat();
				if (ranges::contains(ids, peer->id)) {
					return;
				}
				if ((c && c->amCreator()) || (g && g->amCreator())) {
					ids.push_back(peer->id);
					add(peer);
				}
			});
		} else {
			data.enumerateBroadcasts([&](not_null<ChannelData*> channel) {
				if (channel->amCreator()) {
					add(channel);
				}
			});
		}
	};

	using Menu = base::unique_qptr<Ui::PopupMenu>;
	const auto menu = button->lifetime().make_state<Menu>();
	button->addClickHandler([=](Qt::MouseButton which) {
		if (which != Qt::RightButton) {
			return;
		}

		(*menu) = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::defaultPopupMenu);
		(*menu)->addAction(
			(chats ? tr::lng_menu_my_groups : tr::lng_menu_my_channels)(
				tr::now),
			[=] { controller->uiShow()->showBox(Box(myChannelsBox)); },
			nullptr);
		(*menu)->popup(QCursor::pos());
	});

	return button;
}

[[nodiscard]] bool CanCheckSpecialEvent() {
	static const auto result = [] {
		const auto now = QDate::currentDate();
		return (now.month() == 12) || (now.month() == 1 && now.day() == 1);
	}();
	return result;
}

[[nodiscard]] bool CheckSpecialEvent() {
	const auto now = QDate::currentDate();
	return (now.month() == 12 && now.day() >= 24)
		|| (now.month() == 1 && now.day() == 1);
}

void ShowCallsBox(not_null<Window::SessionController*> window) {
	struct State {
		State(not_null<Window::SessionController*> window)
		: callsController(window)
		, groupCallsController(window) {
		}
		Calls::BoxController callsController;
		PeerListContentDelegateSimple callsDelegate;

		Calls::GroupCalls::ListController groupCallsController;
		PeerListContentDelegateSimple groupCallsDelegate;

		base::unique_qptr<Ui::PopupMenu> menu;
	};

	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto state = box->lifetime().make_state<State>(window);

		const auto groupCalls = box->addRow(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				box,
				object_ptr<Ui::VerticalLayout>(box)),
			{});
		groupCalls->hide(anim::type::instant);
		groupCalls->toggleOn(state->groupCallsController.shownValue());

		Ui::AddSubsectionTitle(
			groupCalls->entity(),
			tr::lng_call_box_groupcalls_subtitle());
		state->groupCallsDelegate.setContent(groupCalls->entity()->add(
			object_ptr<PeerListContent>(box, &state->groupCallsController),
			{}));
		state->groupCallsController.setDelegate(&state->groupCallsDelegate);
		Ui::AddSkip(groupCalls->entity());
		Ui::AddDivider(groupCalls->entity());
		Ui::AddSkip(groupCalls->entity());

		const auto content = box->addRow(
			object_ptr<PeerListContent>(box, &state->callsController),
			{});
		state->callsDelegate.setContent(content);
		state->callsController.setDelegate(&state->callsDelegate);

		box->setWidth(state->callsController.contentWidth());
		state->callsController.boxHeightValue(
		) | rpl::start_with_next([=](int height) {
			box->setMinHeight(height);
		}, box->lifetime());
		box->setTitle(tr::lng_call_box_title());
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		const auto menuButton = box->addTopButton(st::infoTopBarMenu);
		menuButton->setClickedCallback([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				menuButton,
				st::popupMenuWithIcons);
			const auto showSettings = [=] {
				window->showSettings(
					Settings::Calls::Id(),
					Window::SectionShow(anim::type::instant));
			};
			const auto clearAll = crl::guard(box, [=] {
				box->uiShow()->showBox(Box(Calls::ClearCallsBox, window));
			});
			state->menu->addAction(
				tr::lng_settings_section_call_settings(tr::now),
				showSettings,
				&st::menuIconSettings);
			if (state->callsDelegate.peerListFullRowsCount() > 0) {
				state->menu->addAction(
					tr::lng_call_box_clear_all(tr::now),
					clearAll,
					&st::menuIconDelete);
			}
			state->menu->popup(QCursor::pos());
			return true;
		});
	}));
}

[[nodiscard]] rpl::producer<TextWithEntities> SetStatusLabel(
		not_null<Main::Session*> session) {
	const auto self = session->user();
	return session->changes().peerFlagsValue(
		self,
		Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::map([=] {
		return (self->emojiStatusId() != 0);
	}) | rpl::distinct_until_changed() | rpl::map([](bool has) {
		const auto makeLink = [](const QString &text) {
			return Ui::Text::Link(text);
		};
		return (has
			? tr::lng_menu_change_status
			: tr::lng_menu_set_status)(makeLink);
	}) | rpl::flatten_latest();
}

void SetupMenuBots(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto wrap = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto bots = &controller->session().attachWebView();
	const auto iconLoadLifetime = wrap->lifetime().make_state<
		rpl::lifetime
	>();

	rpl::single(
		rpl::empty
	) | rpl::then(
		bots->attachBotsUpdates()
	) | rpl::start_with_next([=] {
		const auto width = container->widthNoMargins();
		wrap->clear();
		for (const auto &bot : bots->attachBots()) {
			const auto user = bot.user;
			if (!bot.inMainMenu || !bot.media) {
				continue;
			} else if (const auto media = bot.media; !media->loaded()) {
				if (!*iconLoadLifetime) {
					auto &session = user->session();
					*iconLoadLifetime = session.downloaderTaskFinished(
					) | rpl::start_with_next([=] {
						if (media->loaded()) {
							iconLoadLifetime->destroy();
							bots->notifyBotIconLoaded();
						}
					});
				}
				continue;
			}
			const auto button = wrap->add(object_ptr<Ui::SettingsButton>(
				wrap,
				rpl::single(bot.name),
				st::mainMenuButton));
			const auto menu = button->lifetime().make_state<
				base::unique_qptr<Ui::PopupMenu>
			>();
			const auto icon = Ui::CreateChild<InlineBots::MenuBotIcon>(
				button,
				bot.media);
			button->heightValue(
			) | rpl::start_with_next([=](int height) {
				icon->move(
					st::mainMenuButton.iconLeft,
					(height - icon->height()) / 2);
			}, button->lifetime());
			const auto weak = Ui::MakeWeak(container);
			button->setAcceptBoth(true);
			button->clicks(
			) | rpl::start_with_next([=](Qt::MouseButton which) {
				if (which == Qt::LeftButton) {
					bots->requestSimple(controller, user, {
						.fromMainMenu = true,
					});
					if (weak) {
						controller->window().hideSettingsAndLayer();
					}
				} else {
					(*menu) = nullptr;
					(*menu) = base::make_unique_q<Ui::PopupMenu>(
						button,
						st::popupMenuWithIcons);
					(*menu)->addAction(
						tr::lng_bot_remove_from_menu(tr::now),
						[=] { bots->removeFromMenu(user); },
						&st::menuIconDelete);
					(*menu)->popup(QCursor::pos());
				}
			}, button->lifetime());

			if (bots->showMainMenuNewBadge(bot)) {
				Ui::NewBadge::AddToRight(button);
			}
		}
		wrap->resizeToWidth(width);
	}, wrap->lifetime());
}

} // namespace

class MainMenu::ToggleAccountsButton final : public Ui::AbstractButton {
public:
	explicit ToggleAccountsButton(QWidget *parent);

	[[nodiscard]] int rightSkip() const {
		return _rightSkip.current();
	}
	[[nodiscard]] rpl::producer<int> rightSkipValue() const {
		return _rightSkip.value();
	}

private:
	void paintEvent(QPaintEvent *e) override;
	void paintUnreadBadge(Painter &p);

	void validateUnreadBadge();
	[[nodiscard]] QString computeUnreadBadge() const;

	rpl::variable<int> _rightSkip = 0;
	Ui::Animations::Simple _toggledAnimation;
	bool _toggled = false;

	QString _unreadBadge;
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
	rpl::single(rpl::empty) | rpl::then(
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

	const auto path = Ui::ToggleUpDownArrowPath(
		0. + width() - st::mainMenuTogglePosition.x(),
		0. + height() - st::mainMenuTogglePosition.y(),
		st::mainMenuToggleSize,
		st::mainMenuToggleFourStrokes,
		_toggledAnimation.value(_toggled ? 1. : 0.));

	auto hq = PainterHighQualityEnabler(p);
	p.fillPath(path, st::windowSubTextFg);

	paintUnreadBadge(p);
}

void MainMenu::ToggleAccountsButton::paintUnreadBadge(Painter &p) {
	const auto progress = 1. - _toggledAnimation.value(_toggled ? 1. : 0.);
	if (!progress) {
		return;
	}
	validateUnreadBadge();
	if (_unreadBadge.isEmpty()) {
		return;
	}

	auto st = Settings::Badge::Style();
	const auto right = width()
		- st::mainMenuTogglePosition.x()
		- st::mainMenuToggleSize * 3;
	const auto top = height()
		- st::mainMenuTogglePosition.y()
		- st::mainMenuBadgeSize / 2;
	p.setOpacity(progress);
	Ui::PaintUnreadBadge(p, _unreadBadge, right, top, st);
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

	auto skip = base;
	if (!_unreadBadge.isEmpty()) {
		const auto st = Settings::Badge::Style();
		skip += 2 * st::mainMenuToggleSize
			+ Ui::CountUnreadBadgeSize(_unreadBadge, st).width();
	}
	_rightSkip = skip;
}

QString MainMenu::ToggleAccountsButton::computeUnreadBadge() const {
	const auto state = OtherAccountsUnreadStateCurrent();
	return state.allMuted
		? QString()
		: (state.count > 0)
		? Lang::FormatCountToShort(state.count).string
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
	_controller->session().user(),
	st::mainMenuUserpic)
, _toggleAccounts(this)
, _setEmojiStatus(this, SetStatusLabel(&controller->session()))
, _emojiStatusPanel(std::make_unique<Info::Profile::EmojiStatusPanel>())
, _badge(std::make_unique<Info::Profile::Badge>(
	this,
	st::settingsInfoPeerBadge,
	controller->session().user(),
	_emojiStatusPanel.get(),
	[=] { return controller->isGifPausedAtLeastFor(GifPauseReason::Layer); },
	kPlayStatusLimit,
	Info::Profile::BadgeType::Premium))
, _scroll(this, st::defaultSolidScroll)
, _inner(_scroll->setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(_scroll.data())))
, _topShadowSkip(_inner->add(
	object_ptr<Ui::FixedHeightWidget>(_inner.get(), st::lineWidth)))
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
, _version((Platform::IsMacStoreBuild() || Platform::IsWindowsStoreBuild())
	? Ui::CreateChild<Ui::FlatLabel>(
		_footer.get(),
		st::mainMenuVersionLabel)
	: Ui::CreateChild<VersionLabel>(
		_footer.get(),
		st::mainMenuVersionLabel)) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	setupUserpicButton();
	setupAccountsToggle();
	setupSetEmojiStatus();
	setupAccounts();
	setupArchive();
	setupMenu();

	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(this);
	widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto line = st::lineWidth;
		shadow->setGeometry(0, st::mainMenuCoverHeight - line, width, line);
	}, shadow->lifetime());

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

	_telegram->setMarkedText(Ui::Text::Link(
		u"Telegram Desktop"_q,
		u"https://desktop.telegram.org"_q));
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

	rpl::combine(
		_toggleAccounts->rightSkipValue(),
		rpl::single(rpl::empty) | rpl::then(_badge->updated())
	) | rpl::start_with_next([=] {
		moveBadge();
	}, lifetime());
	_badge->setPremiumClickCallback([=] {
		chooseEmojiStatus();
	});

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	initResetScaleButton();

	if (CanCheckSpecialEvent() && CheckSpecialEvent()) {
		const auto snowLifetime = lifetime().make_state<rpl::lifetime>();
		const auto rebuild = [=] {
			const auto snowRaw = Ui::CreateChild<Ui::RpWidget>(this);
			const auto snow = snowLifetime->make_state<Ui::Snowflakes>(
				[=](const QRect &r) { snowRaw->update(r); });
			snow->setBrush(QColor(230, 230, 230));
			_showFinished.value(
			) | rpl::start_with_next([=](bool shown) {
				snow->setPaused(!shown);
			}, snowRaw->lifetime());
			snowRaw->paintRequest(
			) | rpl::start_with_next([=](const QRect &r) {
				auto p = Painter(snowRaw);
				p.fillRect(r, st::mainMenuBg);
				drawName(p);
				snow->paint(p, snowRaw->rect());
			}, snowRaw->lifetime());
			widthValue(
			) | rpl::start_with_next([=](int width) {
				snowRaw->setGeometry(0, 0, width, st::mainMenuCoverHeight);
			}, snowRaw->lifetime());
			snowRaw->show();
			snowRaw->lower();
			snowRaw->setAttribute(Qt::WA_TransparentForMouseEvents);
			snowLifetime->add([=] { base::unique_qptr{ snowRaw }; });
		};
		Window::Theme::IsNightModeValue(
		) | rpl::start_with_next([=](bool isNightMode) {
			snowLifetime->destroy();
			if (isNightMode) {
				rebuild();
			}
		}, lifetime());
	}
}

MainMenu::~MainMenu() = default;

void MainMenu::moveBadge() {
	if (!_badge->widget()) {
		return;
	}
	const auto available = width()
		- st::mainMenuCoverNameLeft
		- _toggleAccounts->rightSkip()
		- _badge->widget()->width();
	const auto left = st::mainMenuCoverNameLeft
		+ std::min(_name.maxWidth() + st::semiboldFont->spacew, available);
	_badge->move(
		left,
		st::mainMenuCoverNameTop,
		st::mainMenuCoverNameTop + st::semiboldFont->height);
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
			controller->window().hideSettingsAndLayer();
		}
	};
	const auto checkArchive = [=] {
		const auto f = folder();
		return f
			&& (!f->chatsList()->empty() || f->storiesCount() > 0)
			&& controller->session().settings().archiveInMainMenu();
	};

	const auto wrap = _menu->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_menu,
			object_ptr<Ui::VerticalLayout>(_menu)));
	const auto inner = wrap->entity();
	wrap->toggle(checkArchive(), anim::type::instant);

	const auto button = AddButtonWithIcon(
		inner,
		tr::lng_archived_name(),
		st::mainMenuButton,
		{ &st::menuIconArchiveOpen });
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
			st::popupMenuExpandedSeparator);
		const auto addAction = PeerMenuCallback([&](
				PeerMenuCallback::Args a) {
			return _contextMenu->addAction(
				a.text,
				std::move(a.handler),
				a.icon);
		});

		const auto hide = [=] {
			controller->session().settings().setArchiveInMainMenu(false);
			controller->session().saveSettingsDelayed();
			controller->window().hideSettingsAndLayer();
		};
		addAction(
			tr::lng_context_archive_to_list(tr::now),
			std::move(hide),
			&st::menuIconFromMainMenu);

		MenuAddMarkAsReadChatListAction(
			controller,
			[f = folder()] { return f->chatsList(); },
			addAction);

		_contextMenu->addSeparator();
		Settings::PreloadArchiveSettings(&controller->session());
		addAction(tr::lng_context_archive_settings(tr::now), [=] {
			controller->show(Box(Settings::ArchiveSettingsBox, controller));
		}, &st::menuIconManage);

		_contextMenu->popup(QCursor::pos());
	}, button->lifetime());

	const auto now = folder();
	auto folderValue = now
		? (rpl::single(now) | rpl::type_erased())
		: controller->session().data().chatsListChanges(
		) | rpl::filter([](Data::Folder *folder) {
			return folder && (folder->id() == Data::Folder::kId);
		}) | rpl::take(1);

	using namespace Settings;
	Badge::AddUnread(button, rpl::single(rpl::empty) | rpl::then(std::move(
		folderValue
	) | rpl::map([=](not_null<Data::Folder*> folder) {
		return folder->owner().chatsList(folder)->unreadStateChanges();
	}) | rpl::flatten_latest() | rpl::to_empty) | rpl::map([=] {
		const auto loaded = folder();
		const auto state = loaded
			? loaded->chatListBadgesState()
			: Dialogs::BadgesState();
		return Badge::UnreadBadge{ state.unreadCounter, true };
	}));

	rpl::merge(
		controller->session().data().chatsListChanges(
		) | rpl::filter([](Data::Folder *folder) {
			return folder && (folder->id() == Data::Folder::kId);
		}) | rpl::to_empty,
		controller->session().data().stories().sourcesChanged(
			Data::StorySourcesList::Hidden
		)
	) | rpl::start_with_next([=] {
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
	auto events = Settings::SetupAccounts(inner, _controller);
	inner->add(object_ptr<Ui::FixedHeightWidget>(inner, st::mainMenuSkip));

	std::move(
		events.closeRequests
	) | rpl::start_with_next([=] {
		closeLayer();
	}, inner->lifetime());

	_accounts->toggleOn(Core::App().settings().mainMenuAccountsShownValue());
	_accounts->finishAnimating();

	_shadow->setDuration(0)->toggleOn(_accounts->shownValue());
}

void MainMenu::setupAccountsToggle() {
	_toggleAccounts->show();
	_toggleAccounts->setAcceptBoth();
	_toggleAccounts->addClickHandler([=](Qt::MouseButton button) {
		if (button == Qt::LeftButton) {
			toggleAccounts();
		}
	});
}

void MainMenu::setupSetEmojiStatus() {
	_setEmojiStatus->overrideLinkClickHandler([=] {
		chooseEmojiStatus();
	});
}

void MainMenu::parentResized() {
	resize(st::mainMenuWidth, parentWidget()->height());
}

void MainMenu::showFinished() {
	_showFinished = true;
}

void MainMenu::setupMenu() {
	using namespace Settings;

	const auto controller = _controller;
	const auto addAction = [&](
			rpl::producer<QString> text,
			IconDescriptor &&descriptor) {
		return AddButtonWithIcon(
			_menu,
			std::move(text),
			st::mainMenuButton,
			std::move(descriptor));
	};
	if (!_controller->session().supportMode()) {
		AddMyChannelsBox(addAction(
			tr::lng_create_group_title(),
			{ &st::menuIconGroups }
		), controller, true)->addClickHandler([=](Qt::MouseButton which) {
			if (which == Qt::LeftButton) {
				controller->showNewGroup();
			}
		});

		AddMyChannelsBox(addAction(
			tr::lng_create_channel_title(),
			{ &st::menuIconChannel }
		), controller, false)->addClickHandler([=](Qt::MouseButton which) {
			if (which == Qt::LeftButton) {
				controller->showNewChannel();
			}
		});

		const auto wrap = _menu->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
				_menu,
				CreateButtonWithIcon(
					_menu,
					tr::lng_menu_my_stories(),
					st::mainMenuButton,
					IconDescriptor{ &st::menuIconStoriesSavedSection })));
		const auto selfId = controller->session().userPeerId();
		const auto stories = &controller->session().data().stories();
		if (stories->archiveCount(selfId) > 0) {
			wrap->toggle(true, anim::type::instant);
		} else {
			wrap->toggle(false, anim::type::instant);
			if (!stories->archiveCountKnown(selfId)) {
				stories->archiveLoadMore(selfId);
				wrap->toggleOn(stories->archiveChanged(
				) | rpl::filter(
					rpl::mappers::_1 == selfId
				) | rpl::map([=] {
					return stories->archiveCount(selfId) > 0;
				}) | rpl::filter(rpl::mappers::_1) | rpl::take(1));
			}
		}
		wrap->entity()->setClickedCallback([=] {
			controller->showSection(
				Info::Stories::Make(controller->session().user()));
		});

		SetupMenuBots(_menu, controller);

		addAction(
			tr::lng_menu_contacts(),
			{ &st::menuIconProfile }
		)->setClickedCallback([=] {
			controller->show(PrepareContactsBox(controller));
		});
		addAction(
			tr::lng_menu_calls(),
			{ &st::menuIconPhone }
		)->setClickedCallback([=] {
			ShowCallsBox(controller);
		});
		addAction(
			tr::lng_saved_messages(),
			{ &st::menuIconSavedMessages }
		)->setClickedCallback([=] {
			controller->showPeerHistory(controller->session().user());
		});
	} else {
		addAction(
			tr::lng_profile_add_contact(),
			{ &st::menuIconProfile }
		)->setClickedCallback([=] {
			controller->showAddContact();
		});
		addAction(
			rpl::single(u"Fix chats order"_q),
			{ &st::menuIconPin }
		)->toggleOn(rpl::single(
			_controller->session().settings().supportFixChatsOrder()
		))->toggledChanges(
		) | rpl::start_with_next([=](bool fix) {
			_controller->session().settings().setSupportFixChatsOrder(fix);
			_controller->session().saveSettings();
		}, _menu->lifetime());
		addAction(
			rpl::single(u"Reload templates"_q),
			{ &st::menuIconRestore }
		)->setClickedCallback([=] {
			_controller->session().supportTemplates().reload();
		});
	}
	addAction(
		tr::lng_menu_settings(),
		{ &st::menuIconSettings }
	)->setClickedCallback([=] {
		controller->showSettings();
	});

	_nightThemeToggle = addAction(
		tr::lng_menu_night_mode(),
		{ &st::menuIconNightMode }
	)->toggleOn(_nightThemeSwitches.events_starting_with(
		Window::Theme::IsNightMode()
	));
	_nightThemeToggle->toggledChanges(
	) | rpl::filter([=](bool night) {
		return (night != Window::Theme::IsNightMode());
	}) | rpl::start_with_next([=](bool night) {
		if (Window::Theme::Background()->editingTheme()) {
			_nightThemeSwitches.fire(!night);
			controller->show(Ui::MakeInformBox(
				tr::lng_theme_editor_cant_change_theme()));
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
	_setEmojiStatus->moveToLeft(
		st::mainMenuCoverStatusLeft,
		st::mainMenuCoverStatusTop,
		width());
	_toggleAccounts->setGeometry(
		0,
		st::mainMenuCoverNameTop,
		width(),
		st::mainMenuCoverHeight - st::mainMenuCoverNameTop);
	// Allow cover shadow over the scrolled content.
	const auto top = st::mainMenuCoverHeight - st::lineWidth;
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
		st::mainMenuFooterHeightMin);
	if (_footer->height() != footerHeight) {
		_footer->resize(_footer->width(), footerHeight);
	}
}

void MainMenu::chooseEmojiStatus() {
	if (const auto widget = _badge->widget()) {
		_emojiStatusPanel->show(_controller, widget, _badge->sizeTag());
	} else {
		ShowPremiumPreviewBox(_controller, PremiumFeature::EmojiStatus);
	}
}

void MainMenu::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	const auto cover = QRect(0, 0, width(), st::mainMenuCoverHeight);

	p.fillRect(clip, st::mainMenuBg);
	if (cover.intersects(clip)) {
		drawName(p);
	}
}

void MainMenu::drawName(Painter &p) {
	const auto widthText = width()
		- st::mainMenuCoverNameLeft
		- _toggleAccounts->rightSkip();

	const auto user = _controller->session().user();
	if (_nameVersion < user->nameVersion()) {
		_nameVersion = user->nameVersion();
		_name.setText(
			st::semiboldTextStyle,
			user->name(),
			Ui::NameTextOptions());
		moveBadge();
	}
	p.setFont(st::semiboldFont);
	p.setPen(st::windowBoldFg);
	_name.drawLeftElided(
		p,
		st::mainMenuCoverNameLeft,
		st::mainMenuCoverNameTop,
		(widthText
			- (_badge->widget()
				? (st::semiboldFont->spacew + _badge->widget()->width())
				: 0)),
		width());
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
	auto &domain = Core::App().domain();
	const auto active = &domain.active();
	auto counter = 0;
	auto allMuted = true;
	for (const auto &[index, account] : domain.accounts()) {
		if (account.get() == active) {
			continue;
		} else if (const auto session = account->maybeSession()) {
			counter += session->data().unreadBadge();
			if (!session->data().unreadBadgeMuted()) {
				allMuted = false;
			}
		}
	}
	return {
		.count = counter,
		.allMuted = allMuted,
	};
}

rpl::producer<OthersUnreadState> OtherAccountsUnreadState() {
	return rpl::single(rpl::empty) | rpl::then(
		Core::App().unreadBadgeChanges()
	) | rpl::map(OtherAccountsUnreadStateCurrent);
}

} // namespace Window
