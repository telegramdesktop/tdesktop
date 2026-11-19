/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_main_menu.h"
#include "mokagram/stories/mokagram_story_post.h"
#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/settings/mokagram_settings.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "styles/style_info.h"
#include "styles/style_dialogs.h"
#include "mokagram/plugins/mokagram_plugin_install.h"
#include "mokagram/ui/mokagram_box_parts.h"
#include "mokagram/history/mokagram_history.h"
#include "mokagram/music/mokagram_music_ui.h"
#include "mokagram/feed/mokagram_feed.h"
#include <QtGui/QBitmap>

#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "boxes/about_box.h"
#include "boxes/premium_limits_box.h"
#include "core/update_channel.h"
#include "boxes/contacts_box.h"
#include "boxes/premium_preview_box.h"
#include "calls/group/calls_group_common.h"
#include "calls/calls_box_controller.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_changes.h"
#include "data/data_document_media.h"
#include "data/data_folder.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_badge.h"
#include "settings/settings_common.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/profile/info_profile_icon.h"
#include "info/stories/info_stories_widget.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/mtproto_config.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_information.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "support/support_templates.h"
#include "tde2e/tde2e_api.h"
#include "tde2e/tde2e_integration.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/swipe_handler.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/snowflakes.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/unread_badge_paint.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/layers/generic_box.h"
#include "base/algorithm.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "wallet/wallet_panel.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_main_menu_helpers.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"
#include "styles/style_window.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"
#include "styles/style_window_main_menu.h"
#include "mokagram/streamer/mokagram_streamer.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

#include <array>

namespace Main {
class Session;
} // namespace Main

namespace Mokagram::Misc {
[[nodiscard]] bool PrivacyLocked(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<bool> PrivacyLockedValue(not_null<Main::Session*> session);
void SetPrivacyLocked(not_null<Main::Session*> session, bool locked);
void ShowPrivacyLockBox();
} // namespace Mokagram::Misc

namespace Window {
namespace {

// Mokagram: кнопка закрепления меню у края окна (значок боковой панели).
// side: 0 слева, 1 справа, 2 сверху, 3 снизу.
class DockButton final : public Ui::AbstractButton {
public:
	DockButton(QWidget *parent, int side, int size)
	: AbstractButton(parent)
	, _side(side) {
		setPointerCursor(true);
		resize(size, size);
		Mokagram::Settings::Changed() | rpl::on_next([=](const QString &key) {
			if (key == u"look.menu_pinned"_q || key == u"look.menu_side"_q) {
				update();
			}
		}, lifetime());
		setClickedCallback([=] {
			namespace S = Mokagram::Settings;
			if (active()) {
				S::SetBool(u"look.menu_pinned"_q, false);
			} else {
				S::SetInt(u"look.menu_side"_q, _side);
				S::SetBool(u"look.menu_pinned"_q, true);
			}
		});
	}

private:
	[[nodiscard]] bool active() const {
		namespace S = Mokagram::Settings;
		return S::Bool(u"look.menu_pinned"_q)
			&& (S::Int(u"look.menu_side"_q) == _side);
	}
	void onStateChanged(State was, StateChangeSource source) override {
		const auto now = isOver();
		if (now != bool(was & StateFlag::Over)) {
			_hover.start(
				[=] { update(); },
				now ? 0. : 1.,
				now ? 1. : 0.,
				140,
				anim::easeOutCubic);
		}
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto over = _hover.value(isOver() ? 1. : 0.);
		const auto on = active();
		if (over > 0. || on) {
			auto bg = st::windowActiveTextFg->c;
			bg.setAlphaF(on ? 0.18 : 0.10 * over);
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		}
		const auto color = on
			? st::windowActiveTextFg->c
			: anim::color(st::menuIconFg->c, st::menuIconFgOver->c, over);
		const auto &icon = st::topBarInfo.icon;
		const auto x = (width() - icon.width()) / 2;
		const auto y = (height() - icon.height()) / 2;
		// Та же иконка боковой панели, повёрнутая к нужному краю.
		p.translate(width() / 2., height() / 2.);
		switch (_side) {
		case 0: p.scale(-1., 1.); break;
		case 2: p.rotate(-90.); break;
		case 3: p.rotate(90.); break;
		default: break;
		}
		p.translate(-width() / 2., -height() / 2.);
		icon.paint(p, QPoint(x, y), width(), color);
	}

	const int _side = 0;
	Ui::Animations::Simple _hover;

};

// Mokagram: какие значки показаны в панели и в каком порядке.
// Имя значка это его подпись, списки хранятся через перевод строки.
[[nodiscard]] QStringList BarList(const QString &key) {
	const auto value = Mokagram::Settings::String(key);
	return value.isEmpty() ? QStringList() : value.split(QChar('\n'));
}

void SaveBarList(const QString &key, const QStringList &list) {
	Mokagram::Settings::SetString(key, list.join(QChar('\n')));
}

struct BarItem {
	QString id;
	QString text;
	const style::icon *icon = nullptr;
};

// Имя значка панели не зависит от языка: по значку пункта меню.
[[nodiscard]] QString BarIconName(const style::icon *icon) {
	const auto names = std::vector<std::pair<const style::icon*, QString>>{
		{ &st::menuIconProfile, u"profile"_q },
		{ &st::menuIconGroups, u"groups"_q },
		{ &st::menuIconChannel, u"channel"_q },
		{ &st::menuIconSavedMessages, u"saved"_q },
		{ &st::menuIconBotCommands, u"plugins"_q },
		{ &st::menuIconSoundOn, u"music"_q },
		{ &st::menuIconTopics, u"feed"_q },
		{ &st::menuIconAdd, u"story"_q },
		{ &st::menuIconPin, u"pin"_q },
		{ &st::menuIconRestore, u"restore"_q },
		{ &st::menuIconSettings, u"settings"_q },
		{ &st::menuIconLock, u"lock"_q },
		{ &st::menuIconNightMode, u"night"_q },
	};
	for (const auto &[pointer, name] : names) {
		if (pointer == icon) {
			return name;
		}
	}
	return QString();
}

// Кнопка-значок одного пункта меню в горизонтальной панели.
class BarButton final : public Ui::AbstractButton {
public:
	BarButton(
		QWidget *parent,
		const style::icon *icon,
		const QString &tip,
		Fn<bool()> toggled)
	: AbstractButton(parent)
	, _icon(icon)
	, _toggled(std::move(toggled)) {
		setPointerCursor(true);
		setAcceptBoth();
		setToolTip(tip);
		resize(style::ConvertScale(44), style::ConvertScale(44));
	}
	void refresh() {
		update();
	}
	void setId(const QString &id) {
		_id = id;
	}
	[[nodiscard]] const QString &id() const {
		return _id;
	}
	// Появление: 0 прозрачна и мала, 1 на месте.
	void setReveal(double value) {
		if (_reveal != value) {
			_reveal = value;
			update();
		}
	}

private:
	void onStateChanged(State was, StateChangeSource source) override {
		const auto now = isOver();
		if (now != bool(was & StateFlag::Over)) {
			_hover.start(
				[=] { update(); },
				now ? 0. : 1.,
				now ? 1. : 0.,
				140,
				anim::easeOutCubic);
		}
		const auto down = isDown();
		if (down != bool(was & StateFlag::Down)) {
			_press.start(
				[=] { update(); },
				down ? 0. : 1.,
				down ? 1. : 0.,
				down ? 90 : 160,
				anim::easeOutCubic);
		}
	}
	void paintEvent(QPaintEvent *e) override {
		if (_reveal <= 0.) {
			return;
		}
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto over = _hover.value(isOver() ? 1. : 0.);
		const auto pressed = _press.value(isDown() ? 1. : 0.);
		const auto on = _toggled && _toggled();
		if (on != _wasOn) {
			_wasOn = on;
			_toggle.start(
				[=] { update(); },
				on ? 0. : 1.,
				on ? 1. : 0.,
				220,
				anim::easeOutCubic);
		}
		const auto lit = _toggle.value(on ? 1. : 0.);
		p.setOpacity(_reveal);
		// Всплывает из меньшего размера, при нажатии слегка проседает.
		const auto scale = (0.6 + 0.4 * _reveal) * (1. - 0.08 * pressed);
		p.translate(width() / 2., height() / 2.);
		p.scale(scale, scale);
		p.translate(-width() / 2., -height() / 2.);
		if (over > 0. || lit > 0.) {
			// Подсветка включённого пункта плавно загорается и гаснет.
			auto bg = anim::color(
				st::windowFg->c,
				st::windowActiveTextFg->c,
				lit);
			bg.setAlphaF(0.08 * over * (1. - lit) + 0.20 * lit);
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		}
		if (!_icon) {
			return;
		}
		const auto color = anim::color(
			anim::color(st::menuIconFg->c, st::menuIconFgOver->c, over),
			st::windowActiveTextFg->c,
			lit);
		_icon->paint(
			p,
			QPoint((width() - _icon->width()) / 2, (height() - _icon->height()) / 2),
			width(),
			color);
	}

	const style::icon *_icon = nullptr;
	Fn<bool()> _toggled;
	double _reveal = 0.;
	QString _id;
	bool _wasOn = false;
	Ui::Animations::Simple _hover;
	Ui::Animations::Simple _press;
	Ui::Animations::Simple _toggle;

};

// Строка списка аккаунтов: аватарка слева, активный аккаунт жирным шрифтом.
class AccountMenuItem final : public Ui::Menu::Action {
public:
	AccountMenuItem(
		not_null<Ui::Menu::Menu*> menu,
		const style::Menu &st,
		not_null<QAction*> action,
		not_null<UserData*> user,
		bool active)
	: Action(menu, st, action, nullptr, nullptr)
	, _user(user)
	, _active(active) {
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		const auto selected = isSelected();
		paintBackground(p, selected);
		RippleButton::paintRipple(p, 0, 0);
		const auto &menu = st();
		const auto size = style::ConvertScale(24);
		_user->paintUserpic(
			p,
			_view,
			menu.itemIconPosition.x(),
			(height() - size) / 2,
			size,
			true);
		p.setFont(_active ? st::semiboldFont : menu.itemStyle.font);
		p.setPen(selected ? menu.itemFgOver : menu.itemFg);
		const auto left = menu.itemPadding.left();
		const auto available = width() - left - menu.itemPadding.right();
		p.drawText(
			QRect(left, 0, available, height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			p.fontMetrics().elidedText(
				_user->name(),
				Qt::ElideRight,
				available));
	}

	const not_null<UserData*> _user;
	Ui::PeerUserpicView _view;
	const bool _active;

};

// Круглая аватарка текущего аккаунта слева на панели: открывает список аккаунтов.
class AccountBarButton final : public Ui::AbstractButton {
public:
	AccountBarButton(QWidget *parent, not_null<Main::Session*> session)
	: AbstractButton(parent)
	, _user(session->user()) {
		setPointerCursor(true);
		resize(style::ConvertScale(44), style::ConvertScale(44));
		session->changes().peerUpdates(
			_user,
			Data::PeerUpdate::Flag::Photo
		) | rpl::on_next([=] {
			update();
		}, lifetime());
	}
	void setReveal(double value) {
		if (_reveal != value) {
			_reveal = value;
			update();
		}
	}

private:
	void onStateChanged(State was, StateChangeSource source) override {
		const auto now = isOver();
		if (now != bool(was & StateFlag::Over)) {
			_hover.start(
				[=] { update(); },
				now ? 0. : 1.,
				now ? 1. : 0.,
				140,
				anim::easeOutCubic);
		}
	}
	void paintEvent(QPaintEvent *e) override {
		if (_reveal <= 0.) {
			return;
		}
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto over = _hover.value(isOver() ? 1. : 0.);
		p.setOpacity(_reveal);
		const auto scale = 0.6 + 0.4 * _reveal;
		p.translate(width() / 2., height() / 2.);
		p.scale(scale, scale);
		p.translate(-width() / 2., -height() / 2.);
		if (over > 0.) {
			auto bg = st::windowFg->c;
			bg.setAlphaF(0.08 * over);
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		}
		const auto size = style::ConvertScale(32);
		_user->paintUserpic(
			p,
			_view,
			(width() - size) / 2,
			(height() - size) / 2,
			size,
			true);
	}

	const not_null<UserData*> _user;
	Ui::PeerUserpicView _view;
	double _reveal = 0.;
	Ui::Animations::Simple _hover;

};

constexpr auto kDockPillDuration = crl::time(220);

// Mokagram: общая пилюля для кнопок закрепления меню.
class DockPill final : public Ui::RpWidget {
public:
	explicit DockPill(QWidget *parent) : RpWidget(parent) {
		for (auto i = 0; i != int(kSides.size()); ++i) {
			_buttons[i] = Ui::CreateChild<DockButton>(this, kSides[i], ButtonSize());
			_buttons[i]->show();
		}
		resize(FullWidth(), FullHeight());
	}

	[[nodiscard]] static int FullWidth() {
		return int(kSides.size()) * (ButtonSize() + Padding()) + Padding();
	}

	[[nodiscard]] static int FullHeight() {
		return ButtonSize() + 2 * Padding();
	}

	void setInlineGeometry(int right, int top, double progress) {
		const auto shown = int(base::SafeRound(FullWidth() * progress));
		setGeometry(right - shown, top, shown, FullHeight());
		setVisible(shown > 0);
	}

private:
	static constexpr auto kSides = std::array{ 3, 2, 1, 0 };
	[[nodiscard]] static int ButtonSize() {
		return style::ConvertScale(32);
	}
	[[nodiscard]] static int Padding() {
		return style::ConvertScale(4);
	}
	void resizeEvent(QResizeEvent *e) override {
		auto x = width() - FullWidth() + Padding();
		for (const auto button : _buttons) {
			button->move(x, Padding());
			x += button->width() + Padding();
		}
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = std::min(width(), height()) / 2.;
		p.drawRoundedRect(rect(), radius, radius);
	}

	std::array<DockButton*, kSides.size()> _buttons = {};
};

[[nodiscard]] int MainMenuDockPillTop() {
	return st::mainMenuCoverHeight - style::ConvertScale(6);
}

[[nodiscard]] int MainMenuCoverHeight() {
	return MainMenuDockPillTop() + DockPill::FullHeight() + style::ConvertScale(8);
}

// Mokagram: ручка перетаскивания строки в окне панели.
class BarGrip final : public Ui::RpWidget {
public:
	explicit BarGrip(not_null<Ui::RpWidget*> parent) : RpWidget(parent) {
		setCursor(style::cur_sizeall);
		resize(style::ConvertScale(32), style::ConvertScale(32));
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		st::usernamesReorderIcon.paintInCenter(p, rect());
	}
	void mousePressEvent(QMouseEvent *e) override {
	}

};

// Строка значка в окне панели: значок, подпись, переключатель, ручка.
class BarRow final : public Ui::RpWidget {
public:
	BarRow(
		QWidget *parent,
		const BarItem &item,
		bool shown,
		Fn<void()> changed)
	: RpWidget(parent)
	, _item(item)
	, _shown(shown) {
		_switch = Mokagram::BoxParts::MakeSwitch(this, shown, [=](bool on) {
			_shown = on;
			update();
			changed();
		});
		_grip = Ui::CreateChild<BarGrip>(this);
	}

	[[nodiscard]] const QString &id() const {
		return _item.id;
	}
	[[nodiscard]] bool shown() const {
		return _shown;
	}
	[[nodiscard]] Ui::RpWidget *grip() const {
		return _grip;
	}

private:
	int resizeGetHeight(int newWidth) override {
		return style::ConvertScale(52);
	}
	void resizeEvent(QResizeEvent *e) override {
		const auto side = style::ConvertScale(12);
		_grip->move(
			width() - side - _grip->width(),
			(height() - _grip->height()) / 2);
		_switch->move(
			_grip->x() - style::ConvertScale(8) - _switch->width(),
			(height() - _switch->height()) / 2);
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setOpacity(_shown ? 1. : 0.45);
		const auto side = style::ConvertScale(18);
		const auto icon = _item.icon;
		if (icon) {
			icon->paint(
				p,
				QPoint(side, (height() - icon->height()) / 2),
				width(),
				st::menuIconFg->c);
		}
		const auto left = side
			+ (icon ? icon->width() : 0)
			+ style::ConvertScale(14);
		const auto available = _switch->x() - style::ConvertScale(8) - left;
		p.setPen(st::windowFg);
		p.setFont(st::normalFont);
		p.drawText(
			QRect(left, 0, available, height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::normalFont->elided(_item.text, available));
	}

	const BarItem _item;
	bool _shown = true;
	QPointer<Ui::AbstractButton> _switch;
	QPointer<BarGrip> _grip;

};

// Окно по ПКМ на панели: сторона закрепления, показ и порядок значков.
void ShowBarBox(
		not_null<SessionController*> controller,
		std::vector<BarItem> items) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		namespace BP = Mokagram::BoxParts;
		BP::RoundLayer(box);
		box->setStyle(BP::PillButtonsBox());
		box->setTitle(rpl::single(QString::fromUtf8(
			Mokagram::Lang::Tr("Панель"))));
		const auto content = box->verticalLayout();
		const auto outer = BP::kOuterSide;

		Ui::AddSubsectionTitle(
			content,
			rpl::single(QString::fromUtf8(
				Mokagram::Lang::Tr("Расположение"))));
		const auto place = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(outer, 0, outer, 0));
		place->resize(place->width(), style::ConvertScale(64));
		place->paintRequest(
		) | rpl::on_next([=](QRect clip) {
			auto p = QPainter(place);
			BP::PaintCard(p, place->rect());
		}, place->lifetime());
		auto docks = std::vector<DockButton*>();
		for (const auto side : { 0, 1, 2, 3 }) {
			docks.push_back(Ui::CreateChild<DockButton>(place, side, 40));
		}
		place->sizeValue(
		) | rpl::on_next([=](QSize size) {
			const auto gap = style::ConvertScale(12);
			const auto one = docks.front()->width();
			const auto total = int(docks.size()) * one
				+ (int(docks.size()) - 1) * gap;
			auto x = (size.width() - total) / 2;
			for (const auto dock : docks) {
				dock->move(x, (size.height() - dock->height()) / 2);
				x += one + gap;
			}
		}, place->lifetime());

		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(
			content,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Значки"))));
		const auto list = content->add(
			object_ptr<Ui::VerticalLayout>(content),
			QMargins(outer, 0, outer, 0));
		list->paintRequest(
		) | rpl::on_next([=](QRect clip) {
			auto p = QPainter(list);
			BP::PaintCard(p, list->rect());
		}, list->lifetime());

		struct Rows {
			std::vector<BarRow*> rows;
			std::unique_ptr<Ui::VerticalLayoutReorder> reorder;
		};
		const auto state = list->lifetime().make_state<Rows>();
		const auto save = [=] {
			auto order = QStringList();
			auto hidden = QStringList();
			for (const auto row : state->rows) {
				order.push_back(row->id());
				if (!row->shown()) {
					hidden.push_back(row->id());
				}
			}
			SaveBarList(u"look.bar_order"_q, order);
			SaveBarList(u"look.bar_hidden"_q, hidden);
		};
		const auto hidden = BarList(u"look.bar_hidden"_q);
		const auto order = BarList(u"look.bar_order"_q);
		const auto position = [&](const BarItem &item) {
			const auto index = order.indexOf(item.id);
			return (index < 0) ? int(order.size()) : index;
		};
		auto sorted = items;
		std::stable_sort(sorted.begin(), sorted.end(), [&](
				const BarItem &a,
				const BarItem &b) {
			return position(a) < position(b);
		});
		for (const auto &item : sorted) {
			state->rows.push_back(list->add(object_ptr<BarRow>(
				list,
				item,
				!hidden.contains(item.id),
				save)));
		}
		state->reorder = std::make_unique<Ui::VerticalLayoutReorder>(list);
		state->reorder->setMouseEventProxy([=](int i) {
			return state->rows[i]->grip();
		});
		state->reorder->start();
		state->reorder->updates(
		) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
			using Step = Ui::VerticalLayoutReorder::State;
			if (data.state == Step::Applied) {
				base::reorder(
					state->rows,
					data.oldPosition,
					data.newPosition);
				save();
			}
		}, list->lifetime());

		Ui::AddSkip(content);

		box->addButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Готово"))),
			[=] { box->closeBox(); });
		box->addLeftButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Сбросить"))),
			[=] {
				SaveBarList(u"look.bar_order"_q, QStringList());
				SaveBarList(u"look.bar_hidden"_q, QStringList());
				box->closeBox();
			});
	}));
}

// Горизонтальная панель меню у верхнего или нижнего края области чата.
// Её высота в раскладке задаётся свойством "moka-progress" (0..1), оно анимируется.
class DockBar final : public Ui::RpWidget {
public:
	DockBar(QWidget *parent, int side)
	: RpWidget(parent)
	, _side(side) {
		setProperty("moka-progress", 0.);
		setProperty("moka-side", side);
		Mokagram::Settings::Changed(
		) | rpl::filter([](const QString &key) {
			return key == u"look.menu_pinned"_q || key == u"look.menu_side"_q;
		}) | rpl::on_next([=] {
			namespace S = Mokagram::Settings;
			if (!(S::Bool(u"look.menu_pinned"_q)
				&& S::Int(u"look.menu_side"_q) == _side)) {
				close();
			}
		}, lifetime());
		Mokagram::Settings::Changed(
		) | rpl::filter([](const QString &key) {
			return key == u"look.bar_hidden"_q || key == u"look.bar_order"_q;
		}) | rpl::on_next([=] {
			layoutChildren(_docksAnim.value(_docksOpen ? 1. : 0.));
			update();
		}, lifetime());
		_collapse.setCallback([=] { setDocksOpen(false); });
		_slide.start(
			[=] { slideStep(); },
			0.,
			1.,
			300,
			anim::easeOutCubic);
	}

	~DockBar() {
		if (_pill && _pill->parentWidget() != this) {
			_pill->hide();
			_pill->deleteLater();
		}
	}

	void add(not_null<BarButton*> button) {
		_buttons.push_back(QPointer<BarButton>(button.get()));
	}
	void setBurger(not_null<BarButton*> button) {
		_burger = button.get();
	}
	void setAccount(not_null<AccountBarButton*> button) {
		_account = button.get();
	}
	void toggleDocks() {
		setDocksOpen(!_docksOpen);
	}
	// ПКМ по пустому месту панели.
	void setContextCallback(Fn<void()> callback) {
		_context = std::move(callback);
	}

private:
	// Значки в порядке из настроек, скрытые не входят.
	[[nodiscard]] std::vector<QPointer<BarButton>> ordered() const {
		const auto hidden = BarList(u"look.bar_hidden"_q);
		const auto order = BarList(u"look.bar_order"_q);
		auto result = std::vector<QPointer<BarButton>>();
		for (const auto &button : _buttons) {
			if (button && !hidden.contains(button->id())) {
				result.push_back(button);
			}
		}
		const auto position = [&](const QPointer<BarButton> &button) {
			const auto index = order.indexOf(button->id());
			return (index < 0) ? int(order.size()) : index;
		};
		std::stable_sort(result.begin(), result.end(), [&](
				const QPointer<BarButton> &a,
				const QPointer<BarButton> &b) {
			return position(a) < position(b);
		});
		return result;
	}
	void setDocksOpen(bool open) {
		if (_docksOpen == open) {
			return;
		}
		_docksOpen = open;
		if (open && !_pill) {
			_pill = Ui::CreateChild<DockPill>(this);
		}
		_docksAnim.start(
			[=] { docksStep(); },
			open ? 0. : 1.,
			open ? 1. : 0.,
			kDockPillDuration,
			anim::easeOutCubic);
		if (open) {
			_collapse.callOnce(5000);
		} else {
			_collapse.cancel();
		}
	}
	void docksStep() {
		layoutChildren(_docksAnim.value(_docksOpen ? 1. : 0.));
		update();
	}
	// Закрепление выключено или панель переехала: съезжает и удаляется.
	void close() {
		if (_closing) {
			return;
		}
		_closing = true;
		if (_pill) {
			_pill->hide();
		}
		setObjectName(u"moka-bar-closing"_q);
		_slide.start(
			[=] { slideStep(); },
			_slide.value(1.),
			0.,
			220,
			anim::easeOutCubic);
	}
	void slideStep() {
		const auto value = _slide.value(_closing ? 0. : 1.);
		setProperty("moka-progress", value);
		const auto count = int(_buttons.size());
		for (auto i = 0; i != count; ++i) {
			if (const auto button = _buttons[i]) {
				// Иконки всплывают по очереди слева направо.
				const auto delay = (count > 1) ? (0.45 * i / (count - 1)) : 0.;
				button->setReveal(std::clamp((value - delay) / 0.55, 0., 1.));
			}
		}
		if (_burger) {
			_burger->setReveal(value);
		}
		if (_account) {
			_account->setReveal(value);
		}
		if (const auto parent = parentWidget()) {
			auto event = QResizeEvent(parent->size(), parent->size());
			QCoreApplication::sendEvent(parent, &event);
		}
		update();
		if (_closing && !_slide.animating()) {
			deleteLater();
		}
	}
	void layoutPill(double value) {
		if (!_pill || !_burger) {
			return;
		} else if (value <= 0. || _closing) {
			_pill->hide();
			return;
		}
		const auto parent = _dropdown
			? parentWidget()
			: static_cast<QWidget*>(this);
		if (_pill->parentWidget() != parent) {
			_pill->setParent(parent);
		}
		const auto skip = style::ConvertScale(4);
		const auto fullWidth = DockPill::FullWidth();
		const auto fullHeight = DockPill::FullHeight();
		if (_dropdown) {
			const auto shown = int(base::SafeRound(fullHeight * value));
			const auto right = x() + _burger->x() + _burger->width();
			const auto top = (_side == 3)
				? y() - shown - skip
				: y() + height() + skip;
			_pill->setGeometry(right - fullWidth, top, fullWidth, shown);
			_pill->setVisible(shown > 0);
		} else {
			_pill->setInlineGeometry(
				_burger->x() - skip,
				(height() - fullHeight) / 2,
				value);
		}
		_pill->raise();
	}
	void layoutChildren(double docksOpen) {
		const auto skip = style::ConvertScale(4);
		const auto margin = skip * 3;
		if (_account) {
			_account->move(margin, (height() - _account->height()) / 2);
		}
		auto burgerLeft = width() - margin;
		if (_burger) {
			burgerLeft -= _burger->width();
			_burger->move(burgerLeft, (height() - _burger->height()) / 2);
		}

		// Пункты живут строго между аватаркой и бургером. Не влезают: сначала
		// сжимаем кнопки до квадратов поменьше, потом прячем лишние с конца.
		const auto full = style::ConvertScale(44);
		const auto least = style::ConvertScale(30);
		const auto from = (_account ? _account->x() + _account->width() : margin) + skip;
		const auto to = burgerLeft - skip;
		const auto span = std::max(to - from, 0);
		const auto list = ordered();
		for (const auto &button : _buttons) {
			if (button && std::find(list.begin(), list.end(), button) == list.end()) {
				button->hide();
			}
		}
		const auto total = int(list.size());
		const auto fullItemsWidth = total * full + std::max(total - 1, 0) * skip;
		const auto pillSpace = DockPill::FullWidth() + skip;
		_dropdown = span - fullItemsWidth < pillSpace;
		const auto itemsSpan = (_pill && !_dropdown)
			? span - int(base::SafeRound(pillSpace * docksOpen))
			: span;
		auto size = full;
		auto gap = skip;
		auto capacity = total;
		if (total > 0 && total * full + (total - 1) * skip > span) {
			size = std::max((span - (total - 1) * skip) / total, least);
			if (total * size + (total - 1) * skip > span) {
				gap = 0;
				size = least;
				capacity = std::max(span / size, 0);
			}
		}
		const auto shown = std::min(total, capacity);
		const auto itemsWidth = shown * size + std::max(shown - 1, 0) * gap;
		auto x = std::clamp(
			(width() - itemsWidth) / 2,
			from,
			std::max(from + itemsSpan - itemsWidth, from));
		auto index = 0;
		for (const auto &button : list) {
			if (!button) {
				continue;
			}
			const auto visible = (index++ < shown);
			button->setVisible(visible);
			if (!visible) {
				continue;
			}
			button->resize(size, size);
			button->move(x, (height() - size) / 2);
			x += size + gap;
		}
		layoutPill(docksOpen);
	}
	void moveEvent(QMoveEvent *e) override {
		if (_dropdown) {
			layoutPill(_docksAnim.value(_docksOpen ? 1. : 0.));
		}
	}
	void resizeEvent(QResizeEvent *e) override {
		layoutChildren(_docksAnim.value(_docksOpen ? 1. : 0.));
	}
	void mousePressEvent(QMouseEvent *e) override {
		_rightPressed = (e->button() == Qt::RightButton);
		e->accept();
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() == Qt::RightButton
			&& _rightPressed
			&& rect().contains(e->pos())
			&& _context) {
			_context();
		}
		_rightPressed = false;
	}
	void paintEvent(QPaintEvent *e) override {
		// Прямая плашка без скруглений.
		auto p = QPainter(this);
		p.fillRect(e->rect(), st::mainMenuBg);
	}

	const int _side = 2;
	bool _closing = false;
	bool _docksOpen = false;
	bool _dropdown = false;
	bool _rightPressed = false;
	Fn<void()> _context;
	QPointer<DockPill> _pill;
	Ui::Animations::Simple _slide;
	Ui::Animations::Simple _docksAnim;
	base::Timer _collapse;
	QPointer<BarButton> _burger;
	QPointer<AccountBarButton> _account;
	std::vector<QPointer<BarButton>> _buttons;

};

constexpr auto kPlayStatusLimit = 2;

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

[[nodiscard]] rpl::producer<TextWithEntities> SetStatusLabel(
		not_null<Main::Session*> session) {
	const auto self = session->user();
	return session->changes().peerFlagsValue(
		self,
		Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::map([=] {
		return !!self->emojiStatusId();
	}) | rpl::distinct_until_changed() | rpl::map([](bool has) {
		const auto makeLink = [](const QString &text) {
			return tr::link(text);
		};
		return (has
			? tr::lng_menu_change_status
			: tr::lng_menu_set_status)(makeLink);
	}) | rpl::flatten_latest();
}

} // namespace

class MainMenu::ToggleAccountsButton final : public Ui::AbstractButton {
public:
	ToggleAccountsButton(QWidget *parent, not_null<Main::Account*> current);

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

	const not_null<Main::Account*> _current;
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

MainMenu::ToggleAccountsButton::ToggleAccountsButton(
	QWidget *parent,
	not_null<Main::Account*> current)
: AbstractButton(parent)
, _current(current) {
	rpl::single(rpl::empty) | rpl::then(
		Core::App().unreadBadgeChanges()
	) | rpl::on_next([=] {
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
	}) | rpl::on_next([=](bool value) {
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
	const auto state = OtherAccountsUnreadStateCurrent(_current);
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
	[] {
		// Mokagram: аватарка в шапке крупнее.
		static const auto result = [] {
			auto copy = st::mainMenuUserpic;
			copy.size = QSize(64, 64);
			copy.photoSize = 64;
			return copy;
		}();
		return std::cref(result);
	}().get())
, _toggleAccounts(this, &controller->session().account())
, _setEmojiStatus(this, SetStatusLabel(&controller->session()))
, _emojiStatusPanel(std::make_unique<Info::Profile::EmojiStatusPanel>())
, _badge(std::make_unique<Info::Profile::Badge>(
	this,
	st::settingsInfoPeerBadge,
	&controller->session(),
	Info::Profile::BadgeContentForPeer(controller->session().user()),
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
, _version(AddVersionLabel(_footer)) {
	setProperty("moka-main-menu", true);

	// Режим стримера: имя в шапке рисуется без rpl, сбрасываем кэш текста.
	Mokagram::Streamer::Changed(
	) | rpl::on_next([=] {
		_nameVersion = 0;
		update();
	}, lifetime());

	setupUserpicButton();
	setupAccountsToggle();
	setupSetEmojiStatus();
	// Mokagram: положения панели собраны в одну строку под статусом.
	const auto dockPill = Ui::CreateChild<DockPill>(this);
	const auto dockToggle = Ui::CreateChild<Ui::IconButton>(
		this,
		st::dialogsMenuToggle);
	const auto dockLabel = QString::fromUtf8(
		Mokagram::Lang::Tr("Куда закрепить панель"));
	dockToggle->setToolTip(dockLabel);
	dockToggle->setAccessibleName(dockLabel);
	struct DockState {
		Ui::Animations::Simple slide;
		bool open = false;
	};
	const auto dockState = dockPill->lifetime().make_state<DockState>();
	const auto dockGap = style::ConvertScale(8);
	const auto layoutDockPill = [=](double progress) {
		dockPill->setInlineGeometry(
			dockToggle->x() - dockGap,
			MainMenuDockPillTop(),
			progress);
	};
	dockToggle->setClickedCallback([=] {
		const auto from = dockState->slide.value(dockState->open ? 1. : 0.);
		dockState->open = !dockState->open;
		dockState->slide.start(
			layoutDockPill,
			from,
			dockState->open ? 1. : 0.,
			kDockPillDuration,
			anim::easeOutCubic);
	});
	widthValue() | rpl::on_next([=](int width) {
		dockToggle->move(
			(width + DockPill::FullWidth()) / 2 + dockGap,
			MainMenuDockPillTop()
				+ (DockPill::FullHeight() - dockToggle->height()) / 2);
		layoutDockPill(dockState->slide.value(dockState->open ? 1. : 0.));
	}, dockPill->lifetime());
	dockToggle->show();
	rpl::merge(
		_userpicButton->sizeValue() | rpl::to_empty,
		_setEmojiStatus->sizeValue() | rpl::to_empty
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, lifetime());
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

	// Mokagram: ссылки на канал и форум над названием клиента.
	const auto links = Ui::CreateChild<Ui::FlatLabel>(
		_footer.get(),
		st::mainMenuVersionLabel);
	links->setMarkedText(
		tr::link(
			QString::fromUtf8(Mokagram::Lang::Tr("Канал")),
			u"https://t.me/mokagram"_q)
		.append(QChar(' '))
		.append(QChar(8211))
		.append(QChar(' '))
		.append(tr::link(
			QString::fromUtf8(Mokagram::Lang::Tr("Форум")),
			u"https://t.me/mokaforum"_q)));
	links->setLinksTrusted();

	_footer->heightValue(
	) | rpl::on_next([=] {
		links->moveToLeft(st::mainMenuFooterLeft, _footer->height() - st::mainMenuTelegramBottom - _telegram->height() - links->height() - st::lineWidth * 6);
		_telegram->moveToLeft(st::mainMenuFooterLeft, _footer->height() - st::mainMenuTelegramBottom - _telegram->height());
		_version->moveToLeft(st::mainMenuFooterLeft, _footer->height() - st::mainMenuVersionBottom - _version->height());
	}, _footer->lifetime());

	rpl::combine(
		heightValue(),
		_inner->heightValue()
	) | rpl::on_next([=] {
		updateInnerControlsGeometry();
	}, _inner->lifetime());

	parentResized();

	_telegram->setMarkedText(tr::link(
		u"mokaGram Desktop"_q,
		u"https://github.com/mokagram/mokagram"_q));
	_telegram->setLinksTrusted();
	// The canary version is too long for the "Version {version}" form.
	_version->setMarkedText(
		tr::link(
			Core::BuildIsCanary
				? currentVersionShortText()
				: tr::lng_settings_current_version(
					tr::now,
					lt_version,
					u"Beta"_q),
			1) // Link 1.
		.append(QChar(' '))
		.append(QChar(8211))
		.append(QChar(' '))
		.append(tr::link(tr::lng_menu_about(tr::now), 2))); // Link 2.
	_version->setLink(
		1,
		std::make_shared<UrlClickHandler>(Core::App().changelogLink()));
	_version->setLink(
		2,
		std::make_shared<LambdaClickHandler>([=] {
			controller->show(Box(AboutBox));
		}));

	rpl::combine(
		_toggleAccounts->rightSkipValue(),
		rpl::single(rpl::empty) | rpl::then(_badge->updated())
	) | rpl::on_next([=] {
		moveBadge();
	}, lifetime());
	_badge->setPremiumClickCallback([=] {
		chooseEmojiStatus();
	});

	_controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
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
			) | rpl::on_next([=](bool shown) {
				snow->setPaused(!shown);
			}, snowRaw->lifetime());
			snowRaw->paintRequest(
			) | rpl::on_next([=](const QRect &r) {
				auto p = Painter(snowRaw);
				p.fillRect(r, st::mainMenuBg);
				drawName(p);
				snow->paint(p, snowRaw->rect());
			}, snowRaw->lifetime());
			widthValue(
			) | rpl::on_next([=](int width) {
				snowRaw->setGeometry(0, 0, width, st::mainMenuCoverHeight);
			}, snowRaw->lifetime());
			snowRaw->show();
			snowRaw->lower();
			snowRaw->setAttribute(Qt::WA_TransparentForMouseEvents);
			snowLifetime->add([=] { base::unique_qptr{ snowRaw }; });
		};
		Window::Theme::IsNightModeValue(
		) | rpl::on_next([=](bool isNightMode) {
			snowLifetime->destroy();
			if (isNightMode) {
				rebuild();
			}
		}, lifetime());
	}

	setupSwipe();
}

MainMenu::~MainMenu() = default;

void MainMenu::moveBadge() {
	if (!_badge->widget()) {
		return;
	}
	const auto nameWidth = std::min(
		_name.maxWidth(),
		width() - 2 * st::mainMenuCoverNameLeft
			- 2 * (st::semiboldFont->spacew + _badge->widget()->width()));
	const auto left = (width() + nameWidth) / 2
		+ st::semiboldFont->spacew;
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
	const auto showArchive = [=](Qt::KeyboardModifiers modifiers) {
		if (const auto f = folder()) {
			if (modifiers & Qt::ControlModifier) {
				controller->showInNewWindow(Window::SeparateId(
					Window::SeparateType::Archive,
					&controller->session()));
			} else {
				controller->openFolder(f);
			}
			controller->window().hideSettingsAndLayer();
			controller->removeLayerBlackout();
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

	static const auto archiveStyle = [] {
		auto result = st::mainMenuButton;
		result.padding = style::margins(
			result.padding.left() + 6,
			result.padding.top() + 3,
			result.padding.right(),
			result.padding.bottom() + 3);
		result.iconLeft += 4;
		result.textBg = st::mainMenuBg;
		return result;
	}();
	const auto button = inner->add(
		CreateButtonWithIcon(
			inner,
			tr::lng_archived_name(),
			archiveStyle,
			{ &st::menuIconArchiveOpen }),
		QMargins(10, 1, 10, 1));
	Mokagram::BoxParts::MakePill(button, [] { return st::mainMenuBg->c; });
	button->setAcceptBoth(true);
	button->clicks(
	) | rpl::on_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			showArchive(button->clickModifiers());
			return;
		} else if (which != Qt::RightButton) {
			return;
		}
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuExpandedSeparator);
		Window::FillDialogsEntryMenu(
			_controller,
			Dialogs::EntryState{
				.key = folder(),
				.section = Dialogs::EntryState::Section::ContextMenu,
			},
			Ui::Menu::CreateAddActionCallback(_contextMenu));
		_contextMenu->popup(QCursor::pos());
	}, button->lifetime());

	const auto now = folder();
	auto folderValue = now
		? (rpl::single(now) | rpl::type_erased)
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
	) | rpl::on_next([=] {
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
	) | rpl::on_next([=] {
		closeLayer();
	}, inner->lifetime());

	_accounts->toggleOn(Core::App().settings().mainMenuAccountsShownValue());
	_accounts->finishAnimating();

	_shadow->toggle(false, anim::type::instant);
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

not_null<Ui::RpWidget*> MainMenu::createDockBar(QWidget *parent, int side) {
	const auto bar = Ui::CreateChild<DockBar>(parent, side);
	for (const auto &entry : _barEntries) {
		if (!entry.button) {
			continue;
		}
		const auto source = entry.button;
		const auto toggle = entry.toggle;
		const auto button = Ui::CreateChild<BarButton>(
			bar,
			entry.icon,
			entry.text,
			toggle
				? Fn<bool()>([=] { return source && source->toggled(); })
				: Fn<bool()>());
		button->setId(entry.id);
		button->show();
		const auto alt = entry.alt;
		button->clicks(
		) | rpl::on_next([=](Qt::MouseButton which) {
			if (which == Qt::RightButton) {
				if (alt) {
					alt();
				}
			} else if (which == Qt::LeftButton && source) {
				source->clicked(Qt::NoModifier, Qt::LeftButton);
			}
		}, button->lifetime());
		if (toggle) {
			source->toggledChanges(
			) | rpl::on_next([=](bool) {
				button->refresh();
			}, button->lifetime());
		}
		bar->add(button);
	}
	const auto burger = Ui::CreateChild<BarButton>(
		bar,
		&st::dialogsMenuToggle.icon,
		QString::fromUtf8(Mokagram::Lang::Tr("Куда закрепить панель")),
		Fn<bool()>());
	burger->show();
	burger->clicks(
	) | rpl::on_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			bar->toggleDocks();
		}
	}, burger->lifetime());
	bar->setBurger(burger);
	bar->setContextCallback([=] {
		auto items = std::vector<BarItem>();
		for (const auto &entry : _barEntries) {
			if (entry.button) {
				items.push_back({ entry.id, entry.text, entry.icon });
			}
		}
		ShowBarBox(_controller, std::move(items));
	});
	const auto account = Ui::CreateChild<AccountBarButton>(
		bar,
		&_controller->session());
	account->show();
	const auto menu = account->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	account->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			account,
			st::popupMenuWithIcons);
		auto &domain = Core::App().domain();
		const auto current = &_controller->session().account();
		for (const auto &other : domain.orderedAccounts()) {
			if (!other->sessionExists()) {
				continue;
			}
			const auto user = other->session().user();
			const auto raw = (*menu)->menu();
			const auto action = Ui::Menu::CreateAction(
				raw,
				user->name(),
				[=] {
					if (other != current) {
						Core::App().domain().maybeActivate(other);
					}
				});
			(*menu)->addAction(base::make_unique_q<AccountMenuItem>(
				raw,
				raw->st(),
				action,
				user,
				other == current));
		}
		(*menu)->addAction(
			tr::lng_menu_add_account(tr::now),
			[=] {
				auto &domain = _controller->session().domain();
				if (domain.accounts().size() >= domain.maxAccounts()) {
					_controller->show(
						Box(AccountsLimitBox, &_controller->session()));
				} else {
					_controller->window().preventOrInvoke([=] {
						Core::App().setActivePrimaryWindow(
							&_controller->window());
						_controller->session().domain().addActivated(
							MTP::Environment::Production);
					});
				}
			},
			&st::menuIconAddAccount);
		(*menu)->popup(account->mapToGlobal(
			QPoint(0, account->height())));
	});
	bar->setAccount(account);
	bar->show();
	return bar;
}

void MainMenu::showFinished() {
	_showFinished = true;

	_controller->checkHighlightControl(
		u"main-menu/emoji-status"_q,
		_setEmojiStatus,
		Settings::SubsectionTitleHighlight());
	_controller->checkHighlightControl(
		u"main-menu/night-mode"_q,
		_nightThemeToggle);
}

void MainMenu::setupMenu() {
	using namespace Settings;

	const auto controller = _controller;
	// Mokagram: пункты меню отдельными пилюлями с одинаковыми отступами.
	static const auto rowStyle = [] {
		auto result = st::mainMenuButton;
		result.padding = style::margins(
			result.padding.left() + 6,
			result.padding.top() + 3,
			result.padding.right(),
			result.padding.bottom() + 3);
		result.iconLeft += 4;
		result.textBg = st::mainMenuBg;
		return result;
	}();
	const auto pillMargins = QMargins(10, 1, 10, 1);
	const auto menuBg = [] { return st::mainMenuBg->c; };
	const auto addAction = [&](
			rpl::producer<QString> text,
			IconDescriptor &&descriptor) {
		auto entry = BarEntry();
		entry.icon = descriptor.icon;
		rpl::duplicate(text) | rpl::take(1) | rpl::on_next([&](QString value) {
			entry.text = std::move(value);
		}, lifetime());
		{
			auto id = BarIconName(entry.icon);
			if (id.isEmpty()) {
				id = u"t:"_q + entry.text;
			}
			const auto base = id;
			auto n = 1;
			const auto taken = [&](const QString &check) {
				return std::any_of(
					_barEntries.begin(),
					_barEntries.end(),
					[&](const BarEntry &e) { return e.id == check; });
			};
			while (taken(id)) {
				id = base + u"#"_q + QString::number(++n);
			}
			entry.id = id;
		}
		const auto button = _menu->add(
			CreateButtonWithIcon(
				_menu,
				std::move(text),
				rowStyle,
				std::move(descriptor)),
			pillMargins);
		Mokagram::BoxParts::MakePill(button, menuBg);
		entry.button = button;
		_barEntries.push_back(std::move(entry));
		return button;
	};
	if (!_controller->session().supportMode()) {
		addAction(
			tr::lng_menu_my_profile(),
			{ &st::menuIconProfile }
		)->setClickedCallback([=] {
			controller->showSection(
				Info::Stories::MakeMyProfile(controller->session().user()));
		});

		SetupMenuBots(_menu, controller);

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

		addAction(
			tr::lng_saved_messages(),
			{ &st::menuIconSavedMessages }
		)->setClickedCallback([=] {
			controller->showPeerHistory(controller->session().user());
		});
		addAction(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Плагины"))),
			{ &st::menuIconBotCommands }
		)->setClickedCallback([=] {
			controller->show(Box(Mokagram::Plugins::PluginsBox, controller));
		});
		const auto music = addAction(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Музыка"))),
			{ &st::menuIconSoundOn });
		music->setClickedCallback([=] {
			Mokagram::Music::ShowMusicBox(controller);
		});
		// Mokagram: отдельный переход на главную полноэкранного плеера.
		static const auto homeStyle = [] {
			auto result = Mokagram::BoxParts::PillLight();
			result.width = -style::ConvertScale(24);
			result.height = style::ConvertScale(32);
			result.textTop = (result.height - result.style.font->height) / 2;
			return result;
		}();
		const auto home = Ui::CreateChild<Ui::RoundButton>(
			music,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Главная"))),
			homeStyle);
		home->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		home->setPointerCursor(true);
		home->setFocusPolicy(Qt::TabFocus);
		home->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr(
			"Открыть главную полноэкранного плеера")));
		home->events() | rpl::on_next([=](not_null<QEvent*> event) {
			if (event->type() == QEvent::FocusIn) {
				home->setPenOverride(QPen(
					st::windowActiveTextFg->c,
					style::ConvertScale(2)));
			} else if (event->type() == QEvent::FocusOut) {
				home->setPenOverride(std::nullopt);
			}
		}, home->lifetime());
		const auto inset = style::ConvertScale(12);
		auto musicPadding = music->st().padding;
		musicPadding.setRight(home->width() + 2 * inset);
		music->setPaddingOverride(musicPadding);
		music->sizeValue() | rpl::on_next([=](QSize size) {
			home->moveToRight(inset, (size.height() - home->height()) / 2);
		}, home->lifetime());
		home->setClickedCallback([=] {
			crl::on_main(controller, [=] {
				controller->window().hideSettingsAndLayer(anim::type::instant);
				Mokagram::Music::ShowFullscreenPlayerHome(controller);
			});
		});
		addAction(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Лента"))),
			{ &st::menuIconTopics }
		)->setClickedCallback([=] {
			Mokagram::Feed::Show(controller);
		});
		addAction(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Опубликовать историю"))),
			{ &st::menuIconAdd }
		)->setClickedCallback([=] {
			Mokagram::Stories::StartPosting(
				controller,
				controller->session().user());
		});
		for (auto &item : Mokagram::Plugins::DrawerMenuItems()) {
			addAction(
				rpl::single(item.text),
				{ item.icon }
			)->setClickedCallback(std::move(item.handler));
		}
		const auto session = &controller->session();
		const auto wallet = _menu->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
				_menu,
				CreateButtonWithIcon(
					_menu,
					tr::lng_wallet_menu(),
					rowStyle,
					{ &st::walletMenuIcon }),
				pillMargins));
		Mokagram::BoxParts::MakePill(wallet->entity(), menuBg);
		Ui::NewBadge::AddAfterButtonText(
			wallet->entity(),
			tr::lng_wallet_menu(),
			rowStyle);
		wallet->toggleOn(session->appConfig().value(
		) | rpl::map([=] {
			return session->appConfig().walletAvailable();
		}) | rpl::distinct_until_changed());
		wallet->finishAnimating();
		wallet->entity()->setClickedCallback([=] {
			controller->window().hideSettingsAndLayer();
			Wallet::ShowWallet(session);
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
		) | rpl::on_next([=](bool fix) {
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

	{
		// Mokagram: «Приватный режим» одним тумблером; правый клик выбирает, что скрывать.
		const auto session = &_controller->session();
		const auto lock = addAction(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Приватный режим"))),
			{ &st::menuIconLock }
		)->toggleOn(Mokagram::Misc::PrivacyLockedValue(session));
		_barEntries.back().toggle = true;
		_barEntries.back().alt = [] { Mokagram::Misc::ShowPrivacyLockBox(); };
		lock->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr(
			"Скрывает номер, время захода, приглашения и p2p-звонки. Правый клик: выбрать, что скрывать.")));
		lock->toggledChanges(
		) | rpl::filter([=](bool on) {
			return on != Mokagram::Misc::PrivacyLocked(session);
		}) | rpl::on_next([=](bool on) {
			Mokagram::Misc::SetPrivacyLocked(session, on);
		}, lock->lifetime());
		lock->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::MouseButtonRelease
				&& static_cast<QMouseEvent*>(e.get())->button() == Qt::RightButton;
		}) | rpl::on_next([](not_null<QEvent*>) {
			Mokagram::Misc::ShowPrivacyLockBox();
		}, lock->lifetime());
	}

	_nightThemeToggle = addAction(
		tr::lng_menu_night_mode(),
		{ &st::menuIconNightMode }
	)->toggleOn(_nightThemeSwitches.events_starting_with(
		Window::Theme::IsNightMode()
	));
	_barEntries.back().toggle = true;
	_nightThemeToggle->toggledChanges(
	) | rpl::filter([=](bool night) {
		return (night != Window::Theme::IsNightMode());
	}) | rpl::on_next([=](bool night) {
		if (Window::Theme::Background()->editingTheme()) {
			_nightThemeSwitches.fire(!night);
			controller->show(Ui::MakeInformBox(
				tr::lng_theme_editor_cant_change_theme()));
			return;
		}
		const auto weak = base::make_weak(this);
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
	) | rpl::on_next([=](std::optional<bool> darkMode) {
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
		(width() - 64) / 2,
		st::mainMenuUserpicTop - 6);
	if (_resetScaleButton) {
		_resetScaleButton->moveToRight(0, 0);
	}
	_setEmojiStatus->moveToLeft(
		std::max(
			(width() - _setEmojiStatus->naturalWidth()) / 2,
			st::mainMenuCoverStatusLeft),
		st::mainMenuCoverStatusTop,
		width());
	_toggleAccounts->setGeometry(
		0,
		st::mainMenuCoverNameTop,
		width(),
		MainMenuDockPillTop() - st::mainMenuCoverNameTop);
	// Allow cover shadow over the scrolled content.
	const auto top = MainMenuCoverHeight() - st::lineWidth;
	_scroll->setGeometry(0, top, width(), height() - top);
	updateInnerControlsGeometry();
}

void MainMenu::updateInnerControlsGeometry() {
	const auto contentHeight = _accounts->height()
		+ _shadow->height()
		+ st::mainMenuSkip
		+ _menu->height();
	const auto available = height() - MainMenuCoverHeight() - contentHeight;
	const auto footerHeight = std::max(
		available,
		st::mainMenuFooterHeightMin);
	if (_footer->height() != footerHeight) {
		_footer->resize(_footer->width(), footerHeight);
	}
}

void MainMenu::chooseEmojiStatus() {
	if (_controller->showFrozenError()) {
		return;
	} else if (const auto widget = _badge->widget()) {
		setupEmojiStatusDismiss();
		_emojiStatusPanel->show(_controller, widget, _badge->sizeTag());
	} else {
		ShowPremiumPreviewBox(_controller, PremiumFeature::EmojiStatus);
	}
}

void MainMenu::setupEmojiStatusDismiss() {
	if (_emojiStatusDismissSetup) {
		return;
	}
	_emojiStatusDismissSetup = true;

	base::install_event_filter(this, parentWidget(), [=](
			not_null<QEvent*> e) {
		if (e->type() != QEvent::MouseButtonPress
			|| !_emojiStatusPanel->shown()) {
			return base::EventFilterResult::Continue;
		}
		_emojiStatusPanel->hideAnimated();
		return base::EventFilterResult::Cancel;
	});
}

bool MainMenu::eventHook(QEvent *event) {
	const auto type = event->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		QGuiApplication::sendEvent(_inner, event);
	}
	return RpWidget::eventHook(event);
}

void MainMenu::hideEvent(QHideEvent *e) {
	_emojiStatusPanel->hideFast();
}

void MainMenu::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	const auto cover = QRect(0, 0, width(), st::mainMenuCoverHeight);

	// Mokagram: углы меню у внутреннего края скруглены, как у профиля.
	constexpr auto kRadius = 32;
	const auto shape = (Mokagram::Settings::Int(u"look.menu_side"_q) == 1)
		? QRectF(0, 0, width() + kRadius, height())
		: QRectF(-kRadius, 0, width() + kRadius, height());
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	{
		// Углы закрашены тем, что под меню, с тем же затемнением слоя.
		auto corners = QPainterPath();
		corners.addRect(QRectF(0, 0, width(), height()));
		auto body = QPainterPath();
		body.addRoundedRect(shape, kRadius, kRadius);
		corners = corners.subtracted(body);
		p.setBrush(property("moka-pinned").toBool()
			? st::windowBg
			: st::layerBg);
		p.drawPath(corners);
	}
	p.setBrush(st::mainMenuBg);
	p.drawRoundedRect(shape, kRadius, kRadius);
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
			Mokagram::Streamer::Active(Mokagram::Streamer::Field::Name)
				? Mokagram::Streamer::Mask(
					Mokagram::Streamer::Field::Name,
					user->name())
				: user->name(),
			Ui::NameTextOptions());
		moveBadge();
	}
	const auto badgeWidth = _badge->widget()
		? (st::semiboldFont->spacew + _badge->widget()->width())
		: 0;
	const auto nameWidth = std::min(
		_name.maxWidth(),
		widthText - 2 * badgeWidth);
	p.setFont(st::semiboldFont);
	p.setPen(st::windowBoldFg);
	_name.drawLeftElided(
		p,
		(width() - nameWidth) / 2,
		st::mainMenuCoverNameTop,
		nameWidth,
		width());
}

void MainMenu::initResetScaleButton() {
	_controller->widget()->screenValue(
	) | rpl::map([](not_null<QScreen*> screen) {
		return rpl::single(
			screen->availableGeometry()
		) | rpl::then(
			base::qt_signal_producer(
				screen.get(),
				&QScreen::availableGeometryChanged
			)
		);
	}) | rpl::flatten_latest(
	) | rpl::map([](QRect available) {
		return (available.width() >= st::windowMinWidth)
			&& (available.height() >= st::windowMinHeight);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool good) {
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

OthersUnreadState OtherAccountsUnreadStateCurrent(
		not_null<Main::Account*> current) {
	const auto &domain = Core::App().domain();
	auto counter = 0;
	auto allMuted = true;
	for (const auto &[index, account] : domain.accounts()) {
		if (account.get() == current) {
			continue;
		} else if (const auto session = account->maybeSession()) {
			counter += session->data().unreadWithMentionsBadge();
			if (!session->data().unreadWithMentionsBadgeMuted()) {
				allMuted = false;
			}
		}
	}
	return {
		.count = counter,
		.allMuted = allMuted,
	};
}

rpl::producer<OthersUnreadState> OtherAccountsUnreadState(
		not_null<Main::Account*> current) {
	return rpl::single(rpl::empty) | rpl::then(
		Core::App().unreadBadgeChanges()
	) | rpl::map([=] {
		return OtherAccountsUnreadStateCurrent(current);
	});
}

base::EventFilterResult MainMenu::redirectToInnerChecked(not_null<QEvent*> e) {
	if (_insideEventRedirect) {
		return base::EventFilterResult::Continue;
	}
	const auto weak = base::make_weak(this);
	_insideEventRedirect = true;
	QGuiApplication::sendEvent(_inner, e);
	if (weak) {
		_insideEventRedirect = false;
	}
	return base::EventFilterResult::Cancel;
}

void MainMenu::setupSwipe() {
	const auto outer = _controller->widget()->body();
	base::install_event_filter(this, outer, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::TouchBegin
			|| type == QEvent::TouchUpdate
			|| type == QEvent::TouchEnd
			|| type == QEvent::TouchCancel) {
			return redirectToInnerChecked(e);
		} else if (type == QEvent::Wheel) {
			const auto w = static_cast<QWheelEvent*>(e.get());
			const auto d = Ui::ScrollDeltaF(w);
			if (std::abs(d.x()) > std::abs(d.y())) {
				return redirectToInnerChecked(e);
			}
		}
		return base::EventFilterResult::Continue;
	});
	const auto handles = outer->testAttribute(Qt::WA_AcceptTouchEvents);
	if (!handles) {
		outer->setAttribute(Qt::WA_AcceptTouchEvents);
		lifetime().add([=] {
			outer->setAttribute(Qt::WA_AcceptTouchEvents, false);
		});
	}

	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation < 0) {
			if (!_swipeBackData.callback) {
				_swipeBackData = Ui::Controls::SetupSwipeBack(
					this,
					[=]() -> std::pair<QColor, QColor> {
						return {
							st::historyForwardChooseBg->c,
							st::historyForwardChooseFg->c,
						};
					});
			}
			_swipeBackData.callback(data);
			return;
		} else if (_swipeBackData.lifetime) {
			_swipeBackData = {};
		}
	};

	auto init = [=](Ui::Controls::SwipeHandlerInitData data) {
		if (data.direction != Qt::LeftToRight) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		if (_emojiStatusPanel && _emojiStatusPanel->hasFocus()) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
			closeLayer();
		});
	};

	Ui::Controls::SetupSwipeHandler({
		.widget = _inner,
		.scroll = _scroll.data(),
		.update = std::move(update),
		.init = std::move(init),
	});
}

} // namespace Window
