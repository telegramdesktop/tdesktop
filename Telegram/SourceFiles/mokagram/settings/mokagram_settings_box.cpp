// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 277d403b
// Mokagram: окно «Кастомизация», строится из реестра настроек.
// Вид как у настроек iOS: разделы всегда раскрыты, над группой строк серая
// заглавная надпись, под группой сноска. Сверху чипы быстрого перехода.
#include "mokagram/pet/mokagram_pet.h"
#include "mokagram/plugins/mokagram_plugin_store.h"
#include "mokagram/plugins/mokagram_plugin_install.h"
#include "mokagram/settings/mokagram_settings_box.h"
#include "mokagram/ui/mokagram_box_parts.h"
#include "mokagram/misc/mokagram_misc.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/msginfo/mokagram_message_info.h"
#include "ui/power_saving.h"
#include "mokagram/stars/mokagram_stars_stats.h"
#include "mokagram/effects/mokagram_effects.h"
#include "mokagram/quote/mokagram_quote.h"
#include "history/history_inner_widget.h"
#include "history/history_widget.h"

#include <QtWidgets/QApplication>

#include "mokagram/settings/mokagram_settings.h"
#include "mokagram/misc/mokagram_misc.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/layers/generic_box.h"
#include "base/unixtime.h"
#include "wallet/wallet_content.h"
#include "boxes/add_contact_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "mokagram/ghost/mokagram_ghost.h"
#include "history/history.h"
#include "boxes/choose_filter_box.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "data/data_histories.h"
#include "data/data_session.h"

#include "base/call_delayed.h"
#include "mokagram/history/mokagram_history.h"
#include "mokagram/music/mokagram_soundcloud.h"
#include "mokagram/music/mokagram_soundcloud_login.h"
#include "mokagram/music/mokagram_motion.h"
#include "mokagram/music/mokagram_music_ui.h"
#include "data/data_user.h"
#include "dialogs/dialogs_row.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_indexed_list.h"
#include "mtproto/sender.h"
#include "base/flat_map.h"
#include "ui/text/text_custom_emoji.h"
#include "apiwrap.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "api/api_common.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_story.h"
#include "data/data_stories.h"
#include "core/shortcuts.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "mokagram/passcode/mokagram_fake_passcode.h"
#include "mokagram/peek/mokagram_peek.h"
#include "mokagram/tweaks/mokagram_tweaks.h"
#include "mokagram/forward/mokagram_copy_forward.h"
#include "mokagram/stories/mokagram_story_post.h"
#include "data/data_changes.h"
#include "mainwindow.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "window/window_controller.h"
#include <QtCore/QTimer>
#include <QtCore/QFile>
#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "settings.h"
#include "ui/abstract_button.h"
#include "mokagram/commands/mokagram_commands.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text_entity.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "mokagram/misc/mokagram_ping.h"
#include "styles/style_widgets.h"

#include <rpl/event_stream.h>
#include <rpl/filter.h>
#include <rpl/map.h>

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Mokagram::Settings {
namespace {
// Список выбора без значков: ширина по самому длинному пункту, отступы слева и справа равны.
[[nodiscard]] const style::PopupMenu &CompactChoiceMenuStyle() {
	static const auto result = [] {
		auto menu = st::defaultPopupMenu;
		menu.menu.widthMin = style::ConvertScale(96);
		menu.menu.itemRightSkip = 0;
		return menu;
	}();
	return result;
}

// Строка выбора иконки приложения: слева круглая миниатюра варианта.
class AppIconMenuItem final : public Ui::Menu::Action {
public:
	AppIconMenuItem(
		not_null<Ui::Menu::Menu*> menu,
		const style::Menu &st,
		not_null<QAction*> action,
		QImage image)
	: Action(menu, st, action, nullptr, nullptr)
	, _image(std::move(image)) {
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto selected = isSelected();
		paintBackground(p, selected);
		RippleButton::paintRipple(p, 0, 0);
		const auto &menu = st();
		const auto size = style::ConvertScale(26);
		const auto thumb = QRect(
			menu.itemIconPosition.x(),
			(height() - size) / 2,
			size,
			size);
		auto path = QPainterPath();
		path.addEllipse(thumb);
		p.save();
		p.setClipPath(path);
		p.drawImage(thumb, _image);
		p.restore();
		p.setFont(menu.itemStyle.font);
		p.setPen(selected ? menu.itemFgOver : menu.itemFg);
		const auto left = menu.itemPadding.left();
		p.drawText(
			QRect(left, 0, width() - left - menu.itemPadding.right(), height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			action()->text());
	}

	QImage _image;

};



QString PendingQuery;

constexpr auto kPad = 16;
constexpr auto kPlate = 30;
constexpr auto kTextLeft = kPad + kPlate + 12;
constexpr auto kRowPad = 11;
constexpr auto kMinRowHeight = 52;
constexpr auto kResetSize = 32;
constexpr auto kGroupRadius = 22;
constexpr auto kOuterSide = 14;
constexpr auto kExampleHeight = 24;
constexpr auto kBoxWidth = 440;
constexpr auto kChipHeight = 32;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

struct Category {
	const char *id;
	const char *title;
	QColor color;
};

// Порядок разделов в окне и цвет значков.
[[nodiscard]] const std::vector<Category> &Categories() {
	static const auto result = std::vector<Category>{
		{ "privacy", Mokagram::Lang::Tr("Приватность"), QColor(0x34, 0xC7, 0x59) },
		{ "chats", Mokagram::Lang::Tr("Чаты"), QColor(0x0A, 0x84, 0xFF) },
		{ "accounts", Mokagram::Lang::Tr("Аккаунты"), QColor(0xAF, 0x52, 0xDE) },
		{ "look", Mokagram::Lang::Tr("Внешний вид"), QColor(0xFF, 0x37, 0x5F) },
		{ "plugins", Mokagram::Lang::Tr("Плагины"), QColor(0xFF, 0x9F, 0x0A) },
		{ "other", Mokagram::Lang::Tr("Прочее"), QColor(0x8E, 0x8E, 0x93) },
	};
	return result;
}

[[nodiscard]] int CategoryIndex(const QString &id) {
	const auto &all = Categories();
	for (auto i = 0; i != int(all.size()); ++i) {
		if (id == QLatin1String(all[i].id)) {
			return i;
		}
	}
	return int(all.size()) - 1;
}

[[nodiscard]] QString CategoryOf(const Entry &entry) {
	if (!entry.category.isEmpty()) {
		return entry.category;
	}
	return entry.key.startsWith(u"plugin."_q)
		? u"plugins"_q
		: u"other"_q;
}

// Название раздела: у плагинов своё, иначе берём id из ключа.
[[nodiscard]] QString SectionOf(const Entry &entry) {
	if (!entry.section.isEmpty()) {
		return entry.section;
	}
	if (entry.key.startsWith(u"plugin."_q)) {
		return entry.key.section('.', 1, 1);
	}
	return U(Mokagram::Lang::Tr("Прочее"));
}

[[nodiscard]] const style::icon &IconByName(const QString &name) {
	if (name == u"lock"_q) return st::menuIconLock;
	if (name == u"ghost"_q) return st::menuIconStealth;
	if (name == u"hide"_q) return st::menuIconUserHide;
	if (name == u"phone"_q) return st::menuIconPhone;
	if (name == u"username"_q) return st::menuIconUsername;
	if (name == u"link"_q) return st::menuIconLinks;
	if (name == u"id"_q) return st::menuIconInfo;
	if (name == u"person"_q) return st::menuIconPersonal;
	if (name == u"notify"_q) return st::menuIconNotifications;
	if (name == u"copy"_q) return st::menuIconCopy;
	if (name == u"read"_q) return st::menuIconMarkRead;
	if (name == u"online"_q) return st::menuIconWhenOnline;
	if (name == u"typing"_q) return st::menuIconChatBubble;
	if (name == u"stories"_q) return st::menuIconStoriesSavedSection;
	if (name == u"chats"_q) return st::menuIconChats;
	if (name == u"history"_q) return st::menuIconTimer;
	if (name == u"trash"_q) return st::menuIconDelete;
	if (name == u"edit"_q) return st::menuIconEdit;
	if (name == u"filter"_q) return st::menuIconTagFilter;
	if (name == u"block"_q) return st::menuIconBlock;
	if (name == u"storage"_q) return st::menuIconStorage;
	if (name == u"calendar"_q) return st::menuIconSchedule;
	if (name == u"accounts"_q) return st::menuIconAddAccount;
	if (name == u"palette"_q) return st::menuIconPalette;
	if (name == u"plugin"_q) return st::menuIconExperimental;
	if (name == u"bot"_q) return st::menuIconBot;
	if (name == u"update"_q) return st::menuIconDownload;
	return st::menuIconSettings;
}

// Значок настройки из набора iOSTelegram: эмодзи-ключ по ключу настройки.
[[nodiscard]] QString AutoAlt(const QString &key) {
	struct Item {
		const char *part;
		const char *alt;
	};
	static const auto items = std::vector<Item>{
		{ "only_strangers", "👤" },
		{ "sticker_count", "#️⃣" },
		{ "sticker_seconds", "⏳" },
		{ "guard.stickers", "😜" },
		{ "threat_words", "📄" },
		{ "threat_exempt", "✋" },
		{ "guard.threats", "⚠️" },
		{ "mass_count", "#️⃣" },
		{ "mass_minutes", "🕓" },
		{ "mass_leave", "⬅️" },
		{ "mass_add", "➕" },
		{ "ghost.read", "✅" },
		{ "ghost.online", "👁" },
		{ "ghost.typing", "💬" },
		{ "ghost.stories", "🎞" },
		{ "ghost.exceptions", "📌" },
		{ "ghost.schedule_label", "✏️" },
		{ "ghost.schedule_from", "🕓" },
		{ "ghost.schedule_to", "🕓" },
		{ "ghost.sched_", "🗓" },
		{ "ghost.schedule", "🗓" },
		{ "ghost.offline_on_enable", "💤" },
		{ "history.save_deleted", "🗑" },
		{ "history.save_edited", "✏️" },
		{ "history.save_for_bots", "🤖" },
		{ "history.show_ghosts", "🫣" },
		{ "history.dim_deleted", "🔘" },
		{ "history.mark", "📍" },
		{ "history.keep_days", "🗓" },
		{ "history.keep_mb", "📁" },
		{ "history.keep_per_chat", "💬" },
		{ "history.keep_ttl", "⏳" },
		{ "history.sqlite", "📊" },
		{ "history.filters_enabled", "📑" },
		{ "hide_blocked", "🚫" },
		{ "lag.idle", "💤" },
		{ "lag.fps", "📊" },
		{ "lag.autostart", "▶️" },
		{ "lag.background", "🔽" },
		{ "lag.toaster", "⚙️" },
		{ "lag.emoji_cap", "😜" },
		{ "plugins.prefix", "🔧" },
		{ "plugins.dev_reload", "⚙️" },
		{ "plugins.autoupdate", "🔼" },
		{ "lag.sticker_cap", "🎞" },
		{ "lag.enabled", "📈" },
		{ "lag.level", "📊" },
		{ "lag.sensitivity", "📐" },
		{ "lag.cache_mb", "🔽" },
		{ "lag.cache", "📁" },
		{ "lag.calm", "🔕" },
		{ "lag.notify", "🔔" },
		{ "look.sticker_size", "😜" },
		{ "look.gif_size", "🎞" },
		{ "look.round_size", "📹" },
		{ "bubbles.enabled", "🎈" },
		{ "bubbles.out_color", "🖼" },
		{ "bubbles.in_color", "🔵" },
		{ "bubbles.out_text", "📄" },
		{ "bubbles.in_text", "📋" },
		{ "player.cover_colors", "🖼" },
		{ "lyrics.plasma", "💎" },
		{ "lyrics.comments", "💬" },
		{ "player.autoplay_next", "▶️" },
		{ "chats.edited_pencil", "✏️" },
		{ "chats.select_3000", "☑️" },
		{ "misc.ping_dc", "📈" },
		{ "lyrics.enabled", "🎵" },
		{ "time.showSeconds", "🕓" },
		{ "look.fullNumbers", "#️⃣" },
		{ "bots.copyButtonData", "📋" },
		{ "adblock.hideSponsored", "🚫" },
		{ "regdate.show", "🗓" },
		{ "privacy.relativeLastSeen", "🕓" },
		{ "player.vinyl", "🎵" },
		{ "chats.font", "✏️" },
		{ "commands.aliases", "🔗" },
		{ "commands.enabled", "▶️" },
		{ "documents.pdf", "📄" },
		{ "fx.delete.enabled", "🗑" },
		{ "fx.delete.color", "🔵" },
		{ "fx.delete.duration", "⏳" },
		{ "fx.delete.max_particles", "#️⃣" },
		{ "fx.delete.only_own", "👤" },
		{ "fx.delete.size", "📐" },
		{ "fx.delete.speed", "▶️" },
		{ "fx.delete.style", "🎈" },
		{ "fx.startup.enabled", "▶️" },
		{ "ghost.enabled", "🫣" },
		{ "history.save_files_mb", "📁" },
		{ "history.save_files", "📁" },
		{ "inline.currency", "💰" },
		{ "inline.degrees", "📐" },
		{ "inline.enabled", "📋" },
		{ "inline.math", "➕" },
		{ "inline.timezones", "🕓" },
		{ "inline.units", "📊" },
		{ "look.menu_pinned", "📌" },
		{ "look.menu_side", "➡️" },
		{ "look.wallpaper.pick", "🖼" },
		{ "misc.stars_stats", "⭐️" },
		{ "music.hint_sticker", "🙂" },
		{ "music.smart_stream", "🎵" },
		{ "net.fast_download", "🔽" },
		{ "player.audio_filter", "🎙" },
		{ "player.dynamic_background", "🔵" },
		{ "player.expressive_seek", "📈" },
		{ "player.motion_covers", "🎞" },
		{ "player.public_canvas", "🖼" },
		{ "player.spotify_canvas", "🎞" },
		{ "player.spotify_client_id", "🔑" },
		{ "player.spotify_login", "🔓" },
		{ "player.spotify_logout", "↩️" },
		{ "plugins.sources", "🔗" },
		{ "privacy.lock_all", "🛡" },
		{ "quote.bg", "🖼" },
		{ "quote.custom1", "✏️" },
		{ "quote.custom2", "✏️" },
		{ "quote.padding", "📐" },
		{ "quote.quality", "💎" },
		{ "quote.shadow", "🔘" },
		{ "quote.theme", "🔵" },
		{ "quote.watermark", "📍" },
		{ "quote.width", "📐" },
		{ "scam.enabled", "⚠️" },
		{ "streamer.capture", "📹" },
		{ "streamer.copy", "📋" },
		{ "streamer.enabled", "🎙" },
		{ "streamer.id", "#️⃣" },
		{ "streamer.link", "🔗" },
		{ "streamer.name", "👤" },
		{ "streamer.notify", "🔔" },
		{ "streamer.phone", "📞" },
		{ "streamer.username", "🔗" },
		{ "translation.microsoftCredentials", "🔑" },
		{ "translation.microsoftRegion", "📍" },
		{ "translation.provider", "🗣" },
	};
	for (const auto &item : items) {
		if (key.contains(QLatin1String(item.part))) {
			return QString::fromUtf8(item.alt);
		}
	}
	return QString();
}

// Прозрачный фон окна и скруглённая подложка, как в окне «Плагины».
void RoundedNoButtons(not_null<Ui::GenericBox*> box) {
	static const auto clear = style::owned_color(QColor(0, 0, 0, 0));
	static const auto noButtons = [] {
		auto result = st::defaultBox;
		result.buttonPadding = style::margins();
		result.buttonHeight = 0;
		result.bg = clear.color();
		return result;
	}();
	box->setStyle(noButtons);
	box->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(box->rect(), 28, 28);
	});
	box->setWidth(kBoxWidth);
	box->setNoContentMargin(true);
}

// Стили подписей без минимальной ширины: иначе текст обрезается.
[[nodiscard]] const style::FlatLabel &TitleLabelStyle() {
	static const auto result = [] {
		auto value = st::boxLabel;
		value.minWidth = 1;
		return value;
	}();
	return result;
}

[[nodiscard]] const style::FlatLabel &TextLabelStyle() {
	static const auto result = [] {
		auto value = st::boxDividerLabel;
		value.minWidth = 1;
		return value;
	}();
	return result;
}

[[nodiscard]] const style::FlatLabel &HeadLabelStyle() {
	static const auto result = [] {
		auto value = st::boxDividerLabel;
		value.minWidth = 1;
		value.style.font = st::semiboldFont;
		return value;
	}();
	return result;
}

// Круглая кнопка шапки: закрыть или лупа, рисуются вручную.
[[nodiscard]] not_null<Ui::AbstractButton*> MakeRoundButton(
		not_null<QWidget*> parent,
		bool back,
		Fn<void()> callback) {
	const auto button = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	button->resize(36, 36);
	button->setPointerCursor(true);
	button->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(button);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawEllipse(button->rect());
		p.scale(0.75, 0.75);
		auto pen = QPen(st::windowFg->c, 2.2);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		if (back) {
			p.drawLine(QPointF(17, 17), QPointF(31, 31));
			p.drawLine(QPointF(31, 17), QPointF(17, 31));
		} else {
			p.drawEllipse(QPointF(22.5, 22.5), 7.5, 7.5);
			p.drawLine(QPointF(28, 28), QPointF(34, 34));
		}
	}, button->lifetime());
	button->setClickedCallback(std::move(callback));
	return button;
}

// Кнопка сброса с плавным появлением: значок или текстовая ссылка.
struct FadeButton {
	not_null<Ui::AbstractButton*> button;
	Fn<void(bool shown, bool instant)> set;
};

[[nodiscard]] FadeButton MakeResetButton(
		not_null<QWidget*> parent,
		const QString &text,
		Fn<void()> callback) {
	struct State {
		Ui::Animations::Simple animation;
		bool shown = false;
	};
	const auto button = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	const auto state = button->lifetime().make_state<State>();
	if (text.isEmpty()) {
		button->resize(kResetSize, kResetSize);
	} else {
		button->resize(st::normalFont->width(text) + 8, 24);
	}
	button->setPointerCursor(true);
	button->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(button);
		p.setOpacity(state->animation.value(state->shown ? 1. : 0.));
		if (text.isEmpty()) {
			st::menuIconRestore.paintInCenter(p, button->rect());
		} else {
			p.setFont(st::normalFont);
			p.setPen(st::windowActiveTextFg);
			p.drawText(button->rect(), Qt::AlignCenter, text);
		}
	}, button->lifetime());
	button->setClickedCallback(std::move(callback));
	button->hide();
	const auto set = [=](bool shown, bool instant) {
		if (state->shown == shown) {
			return;
		}
		state->shown = shown;
		if (instant) {
			state->animation.stop();
			button->setVisible(shown);
			return;
		}
		button->show();
		state->animation.start([=] {
			button->update();
			if (!state->shown
				&& state->animation.value(0.) <= 0.001) {
				button->hide();
			}
		}, shown ? 0. : 1., shown ? 1. : 0., 220);
	};
	return { .button = button, .set = set };
}

void MakeTransparent(not_null<Ui::RpWidget*> widget) {
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	// Дочерние виджеты, созданные после показа родителя (перестройка
	// страницы по клику), сами не показываются: без этого пропадал весь текст.
	widget->show();
}

// Находит запрос в тексте и выделяет жирным с подчёркиванием.
[[nodiscard]] TextWithEntities Marked(
		const QString &text,
		const QString &query) {
	auto result = TextWithEntities{ text };
	if (query.isEmpty()) {
		return result;
	}
	auto from = 0;
	while (true) {
		const auto at = text.indexOf(query, from, Qt::CaseInsensitive);
		if (at < 0) {
			break;
		}
		result.entities.push_back(EntityInText(
			EntityType::Bold,
			at,
			query.size()));
		result.entities.push_back(EntityInText(
			EntityType::Underline,
			at,
			query.size()));
		from = at + query.size();
	}
	return result;
}

// Пример «было → станет» двумя плашками со стрелкой.
[[nodiscard]] not_null<Ui::RpWidget*> MakeExample(
		not_null<QWidget*> parent,
		const QString &example) {
	const auto widget = Ui::CreateChild<Ui::RpWidget>(parent.get());
	MakeTransparent(widget);
	widget->resize(10, kExampleHeight);
	const auto arrow = QString::fromUtf8("\xE2\x86\x92");
	const auto at = example.indexOf(arrow);
	const auto before = (at >= 0) ? example.left(at).trimmed() : QString();
	const auto after = (at >= 0)
		? example.mid(at + arrow.size()).trimmed()
		: example.trimmed();
	widget->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		const auto &font = st::normalFont;
		const auto total = widget->width();
		const auto gap = before.isEmpty() ? 0 : 26;
		const auto room = std::max(total - gap, 40);
		const auto one = before.isEmpty() ? room : (room / 2);
		auto x = 0;
		const auto pill = [&](
				const QString &text,
				QColor bg,
				QColor fg,
				int limit) {
			const auto shown = font->elided(text, limit - 20);
			const auto width = std::min(font->width(shown) + 20, limit);
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawRoundedRect(
				QRectF(x, 0, width, kExampleHeight),
				kExampleHeight / 2.,
				kExampleHeight / 2.);
			p.setFont(font);
			p.setPen(fg);
			p.drawText(
				QRect(x, 0, width, kExampleHeight),
				Qt::AlignCenter,
				shown);
			x += width;
		};
		if (!before.isEmpty()) {
			auto grey = st::windowSubTextFg->c;
			grey.setAlpha(40);
			pill(before, grey, st::windowSubTextFg->c, one);
			auto pen = QPen(st::windowSubTextFg->c, 1.6);
			pen.setCapStyle(Qt::RoundCap);
			p.setPen(pen);
			const auto cy = kExampleHeight / 2.;
			p.drawLine(QPointF(x + 7, cy), QPointF(x + 19, cy));
			p.drawLine(QPointF(x + 15, cy - 4), QPointF(x + 19, cy));
			p.drawLine(QPointF(x + 15, cy + 4), QPointF(x + 19, cy));
			x += gap;
		}
		auto accent = st::windowBgActive->c;
		accent.setAlpha(46);
		pill(after, accent, st::windowActiveTextFg->c, one);
	});
	return widget;
}

// Значки из набора эмодзи iOSTelegram: ищем документ по обычному эмодзи.
class PackIcons final {
public:
	static PackIcons &Instance() {
		static auto result = PackIcons();
		return result;
	}

	void ensure(not_null<Main::Session*> session) {
		if (_session == session || _requestId) {
			return;
		}
		_session = session;
		// Свой набор mymoka: первый эмодзи служит значком «Настройки mokaGram».
		_ownRequestId = session->api().request(MTPmessages_GetStickerSet(
			MTP_inputStickerSetShortName(MTP_string(u"mymoka"_q)),
			MTP_int(0)
		)).done([=](const MTPmessages_StickerSet &set) {
			_ownRequestId = 0;
			set.match([&](const MTPDmessages_stickerSet &data) {
				if (!data.vdocuments().v.isEmpty()) {
					_own = _session->data().processDocument(
						data.vdocuments().v.front());
					// Четвёртый эмодзи набора: сердце для окна «Поддержать разработку».
					if (data.vdocuments().v.size() > 3) {
						_ownHeart = _session->data().processDocument(
							data.vdocuments().v[3]);
					}
					_updated.fire({});
				}
			}, [](const auto &) {});
		}).fail([=] {
			_ownRequestId = 0;
		}).send();
		_requestId = session->api().request(MTPmessages_GetStickerSet(
			MTP_inputStickerSetShortName(MTP_string(u"iOSTelegram"_q)),
			MTP_int(0)
		)).done([=](const MTPmessages_StickerSet &set) {
			_requestId = 0;
			set.match([&](const MTPDmessages_stickerSet &data) {
				for (const auto &doc : data.vdocuments().v) {
					const auto document = _session->data().processDocument(doc);
					if (const auto sticker = document->sticker()) {
						_docs.emplace(Normalize(sticker->alt), document);
						if (!qEnvironmentVariableIsEmpty("MOKAGRAM_DEBUG_CMD")) {
							fprintf(stderr, "PACK %s\n", sticker->alt.toUtf8().constData());
						}
					}
				}
				_updated.fire({});
			}, [](const auto &) {});
		}).fail([=] {
			_requestId = 0;
		}).send();
	}
	[[nodiscard]] rpl::producer<> updated() const {
		return _updated.events();
	}
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> make(
			const QString &name,
			Fn<void()> repaint,
			int size) {
		if (name.isEmpty()) {
			return nullptr;
		}
		return makeAlt(EmojiFor(name), std::move(repaint), size);
	}
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> makeOwn(
			Fn<void()> repaint,
			int size) {
		if (!_session || !_own) {
			return nullptr;
		}
		return _session->data().customEmojiManager().create(
			_own,
			std::move(repaint),
			Data::CustomEmojiSizeTag::Normal,
			size);
	}
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> makeOwnHeart(
			Fn<void()> repaint,
			int size) {
		if (!_session || !_ownHeart) {
			return nullptr;
		}
		return _session->data().customEmojiManager().create(
			_ownHeart,
			std::move(repaint),
			Data::CustomEmojiSizeTag::Normal,
			size);
	}
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> makeAlt(
			const QString &alt,
			Fn<void()> repaint,
			int size) {
		const auto i = _docs.find(Normalize(alt));
		if (!_session || i == _docs.end()) {
			return nullptr;
		}
		return _session->data().customEmojiManager().create(
			i->second,
			std::move(repaint),
			// Isolated грузит кадры крупнее: значок остаётся чётким после обрезки.
			Data::CustomEmojiSizeTag::Isolated,
			size);
	}

private:
	[[nodiscard]] static QString Normalize(QString value) {
		return value.remove(QChar(0xFE0F)).trimmed();
	}
	[[nodiscard]] static QString EmojiFor(const QString &name) {
		static const auto map = base::flat_map<QString, QString>{
			{ u"lock"_q, QString::fromUtf8("🔑") },
			{ u"ghost"_q, QString::fromUtf8("🫣") },
			{ u"hide"_q, QString::fromUtf8("🛡") },
			{ u"phone"_q, QString::fromUtf8("📞") },
			{ u"username"_q, QString::fromUtf8("👤") },
			{ u"link"_q, QString::fromUtf8("🔗") },
			{ u"id"_q, QString::fromUtf8("#️⃣") },
			{ u"person"_q, QString::fromUtf8("👥") },
			{ u"notify"_q, QString::fromUtf8("🔔") },
			{ u"copy"_q, QString::fromUtf8("📋") },
			{ u"read"_q, QString::fromUtf8("✅") },
			{ u"online"_q, QString::fromUtf8("🔵") },
			{ u"typing"_q, QString::fromUtf8("💬") },
			{ u"stories"_q, QString::fromUtf8("🎞") },
			{ u"chats"_q, QString::fromUtf8("💬") },
			{ u"history"_q, QString::fromUtf8("🕓") },
			{ u"trash"_q, QString::fromUtf8("🗑") },
			{ u"edit"_q, QString::fromUtf8("✏️") },
			{ u"filter"_q, QString::fromUtf8("📑") },
			{ u"block"_q, QString::fromUtf8("🚫") },
			{ u"storage"_q, QString::fromUtf8("📁") },
			{ u"download"_q, QString::fromUtf8("🔽") },
			{ u"update"_q, QString::fromUtf8("🔼") },
			{ u"calendar"_q, QString::fromUtf8("🗓") },
			{ u"accounts"_q, QString::fromUtf8("👥") },
			{ u"palette"_q, QString::fromUtf8("🖼") },
			{ u"plugin"_q, QString::fromUtf8("🔧") },
			{ u"bot"_q, QString::fromUtf8("🤖") },
		};
		const auto i = map.find(name);
		return (i != map.end()) ? i->second : QString::fromUtf8("⚙");
	}

	Main::Session *_session = nullptr;
	mtpRequestId _requestId = 0;
	mtpRequestId _ownRequestId = 0;
	DocumentData *_own = nullptr;
	DocumentData *_ownHeart = nullptr;
	base::flat_map<QString, DocumentData*> _docs;
	rpl::event_stream<> _updated;

};

// Значок строки: эмодзи из набора, пока не загрузился, обычная плашка.
struct PackIcon {
	std::unique_ptr<Ui::Text::CustomEmoji> emoji;
	// Готовая картинка: у эмодзи из пака разные поля вокруг фигуры, поэтому
	// их обрезаем по видимой части и растягиваем до одного размера.
	QImage normalized;
	bool tried = false;
};

constexpr auto kPackEmojiSize = 28;

void PaintPlate(
		QPainter &p,
		const QRect &rect,
		QColor color,
		const style::icon &icon,
		const QString &name,
		const QString &alt,
		PackIcon &pack,
		not_null<Ui::RpWidget*> widget,
		bool plate = true) {
	if (!pack.emoji && !pack.tried) {
		pack.emoji = !alt.isEmpty()
			? PackIcons::Instance().makeAlt(
				alt,
				[=] { widget->update(); },
				kPackEmojiSize)
			: PackIcons::Instance().make(
			name,
			[=] { widget->update(); },
			kPackEmojiSize);
		pack.tried = (pack.emoji != nullptr);
	}
	auto hq = PainterHighQualityEnabler(p);
	if (pack.emoji && pack.normalized.isNull() && pack.emoji->ready()) {
		const auto ratio = style::DevicePixelRatio();
		// Холст с запасом: у некоторых эмодзи фигура выходит за свою клетку.
		constexpr auto kMargin = 8;
		const auto canvas = kPackEmojiSize + 2 * kMargin;
		// Рисуем вдвое плотнее экрана, чтобы обрезка и сжатие не мылили картинку.
		const auto scale = ratio * 2;
		auto image = QImage(
			QSize(canvas, canvas) * scale,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(scale);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			pack.emoji->paint(q, {
				.textColor = QColor(Qt::white),
				.now = crl::now(),
				.position = QPoint(kMargin, kMargin),
			});
		}
		auto bounds = QRect();
		for (auto y = 0; y != image.height(); ++y) {
			const auto line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
			for (auto x = 0; x != image.width(); ++x) {
				if (qAlpha(line[x]) > 24) {
					bounds = bounds.united(QRect(x, y, 1, 1));
				}
			}
		}
		if (qEnvironmentVariableIsSet("MOKAGRAM_DEBUG_CMD")) {
			if (const auto path = Mokagram::Misc::DebugFilePath(
					u"moka_icon_%1.png"_q.arg(
						QString::fromLatin1(alt.toUtf8().toHex())));
				!path.isEmpty()) {
				image.save(path);
			}
		}
		if (bounds.width() > 4 && bounds.height() > 4) {
			// Плашки квадратные: если видимая часть заметно не квадрат, кадр
			// неполный или эмодзи шире клетки, берём всю клетку 28x28.
			const auto skewed = std::max(bounds.width(), bounds.height())
				> std::min(bounds.width(), bounds.height()) * 1.12;
			if (skewed) {
				bounds = QRect(
					kMargin * scale,
					kMargin * scale,
					kPackEmojiSize * scale,
					kPackEmojiSize * scale);
			}
			const auto side = std::max(bounds.width(), bounds.height());
			const auto square = QRect(
				bounds.center().x() - side / 2,
				bounds.center().y() - side / 2,
				side,
				side);
			auto fitted = QImage(
				QSize(kPackEmojiSize, kPackEmojiSize) * ratio,
				image.format());
			fitted.setDevicePixelRatio(ratio);
			fitted.fill(Qt::transparent);
			auto q = QPainter(&fitted);
			q.setRenderHint(QPainter::SmoothPixmapTransform);
			q.drawImage(
				QRect(1, 1, kPackEmojiSize - 2, kPackEmojiSize - 2),
				image,
				square);
			q.end();
			pack.normalized = std::move(fitted);
		}
	}
	if (!pack.normalized.isNull()) {
		p.drawImage(
			QPoint(
				rect.x() + (rect.width() - kPackEmojiSize) / 2,
				rect.y() + (rect.height() - kPackEmojiSize) / 2),
			pack.normalized);
		return;
	}
	if (pack.emoji) {
		pack.emoji->paint(p, {
			.textColor = QColor(Qt::white),
			.now = crl::now(),
			.position = QPoint(
				rect.x() + (rect.width() - kPackEmojiSize) / 2,
				rect.y() + (rect.height() - kPackEmojiSize) / 2),
		});
		return;
	}
	if (!plate) {
		return;
	}
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(rect, 8, 8);
	icon.paintInCenter(p, rect, Qt::white);
}

struct RowState {
	PackIcon pack;
	bool first = false;
	bool changed = false;
	int plateTop = 0;
	int chevronTop = 0;
};

struct RowInfo {
	not_null<Ui::SlideWrap<Ui::RpWidget>*> wrap;
	QString key;
	QString haystack;
	QString dependsOn;
	int section = 0;
	Fn<void(bool)> setFirst;
	Fn<void(const QString &)> highlight;
};

struct SectionInfo {
	QString name;
	QString category;
	not_null<Ui::SlideWrap<Ui::VerticalLayout>*> wrap;
	FadeButton reset;
	std::vector<int> items;
};

struct Group {
	QString name;
	QString category;
	QString note;
	std::vector<Entry> entries;
};

[[nodiscard]] std::vector<Group> BuildGroups() {
	auto result = std::vector<Group>();
	for (const auto &entry : Entries()) {
		if (entry.hidden) {
			continue;
		}
		const auto name = SectionOf(entry);
		auto i = ranges::find(result, name, &Group::name);
		if (i == result.end()) {
			result.push_back({ .name = name, .category = CategoryOf(entry) });
			i = result.end() - 1;
		}
		if (i->note.isEmpty()) {
			i->note = entry.sectionNote;
		}
		i->entries.push_back(entry);
	}
	std::stable_sort(result.begin(), result.end(), [](
			const Group &a,
			const Group &b) {
		return CategoryIndex(a.category) < CategoryIndex(b.category);
	});
	return result;
}

// Выбор цвета: готовые цвета и ввод #RRGGBB. Пустое значение возвращает цвет темы.
void ShowColorBox(const QString &title, const QString &key) {
	const auto window = Core::App().activeWindow();
	if (!window) {
		return;
	}
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		box->setWidth(360);
		const auto content = box->verticalLayout();
		static const auto presets = std::vector<QColor>{
			QColor(0x7A, 0x3D, 0xF0), QColor(0x0A, 0x84, 0xFF),
			QColor(0x34, 0xC7, 0x59), QColor(0xFF, 0x9F, 0x0A),
			QColor(0xFF, 0x45, 0x3A), QColor(0xFF, 0x37, 0x5F),
			QColor(0xBF, 0x5A, 0xF2), QColor(0x64, 0xD2, 0xFF),
			QColor(0x24, 0x3B, 0x2C), QColor(0x1C, 0x1C, 0x1E),
			QColor(0x3A, 0x3A, 0x3C), QColor(0xFF, 0xFF, 0xFF),
		};
		static const auto fieldStyle = [] {
			auto result = st::defaultInputField;
			// Текст по центру поля: высоту задаём сами и считаем поля от высоты шрифта.
			constexpr auto kFieldHeight = 44;
			result.heightMin = kFieldHeight;
			const auto margin = (kFieldHeight - result.style.font->height) / 2;
			result.textMargins = style::margins(0, margin, 0, margin);
			// Подсказка не всплывает над полем (в низком поле её обрезало).
			result.placeholderScale = 0.;
			result.placeholderShift = 0;
			return result;
		}();
		const auto field = content->add(
			object_ptr<Ui::InputField>(
				content,
				fieldStyle,
				rpl::single(U("#RRGGBB")),
				String(key)),
			QMargins(22, 8, 22, 6));
		const auto grid = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(16, 4, 16, 8));
		constexpr auto kDot = 36;
		constexpr auto kGap = 12;
		// Поля вокруг кружков: белое кольцо выбранного цвета выходит за кружок и раньше обрезалось краем.
		constexpr auto kPad = 6;
		grid->resize(grid->width(), 2 * kDot + kGap + 2 * kPad);
		grid->paintOn([=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			const auto current = QColor(String(key));
			const auto perRow = 6;
			for (auto i = 0; i != int(presets.size()); ++i) {
				const auto r = QRectF(
					kPad + (i % perRow) * (kDot + kGap),
					kPad + (i / perRow) * (kDot + kGap),
					kDot,
					kDot);
				p.setPen(QPen(QColor(255, 255, 255, 40), 1.));
				p.setBrush(presets[i]);
				p.drawEllipse(r);
				if (current.isValid() && current == presets[i]) {
					p.setPen(QPen(Qt::white, 2.));
					p.setBrush(Qt::NoBrush);
					p.drawEllipse(r.adjusted(-4, -4, 4, 4));
				}
			}
		});
		grid->setCursor(style::cur_pointer);
		grid->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::MouseButtonRelease;
		}) | rpl::on_next([=](not_null<QEvent*> e) {
			const auto point = static_cast<QMouseEvent*>(e.get())->pos()
				- QPoint(kPad, kPad);
			if (point.x() < 0 || point.y() < 0) {
				return;
			}
			const auto perRow = 6;
			const auto col = point.x() / (kDot + kGap);
			const auto row = point.y() / (kDot + kGap);
			const auto index = row * perRow + col;
			if (col < perRow && index >= 0 && index < int(presets.size())) {
				SetString(key, presets[index].name());
				field->setText(presets[index].name());
				grid->update();
			}
		}, grid->lifetime());
		field->changes() | rpl::on_next([=] {
			const auto text = field->getLastText().trimmed();
			const auto color = QColor(text.startsWith('#') ? text : (u"#"_q + text));
			if (text.isEmpty() || color.isValid()) {
				SetString(key, text.isEmpty() ? QString() : color.name());
				grid->update();
			}
		}, field->lifetime());
		box->addButton(rpl::single(U(Mokagram::Lang::Tr("Готово"))), [=] { box->closeBox(); });
		box->addLeftButton(rpl::single(U(Mokagram::Lang::Tr("По теме"))), [=] {
			SetString(key, QString());
			field->setText(QString());
			grid->update();
		});
	}));
}

// Строка одной настройки. Раскладка зависит от вида значения.
[[nodiscard]] RowInfo AddRow(
		not_null<Ui::VerticalLayout*> card,
		const Entry &entry,
		QColor color,
		int section,
		const QString &sectionName,
		Fn<void()> afterChange) {
	auto holder = object_ptr<Ui::RippleButton>(
		card,
		st::defaultRippleAnimation);
	const auto row = holder.data();
	const auto wrap = card->add(object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		card,
		std::move(holder),
		QMargins()));
	wrap->toggle(true, anim::type::instant);
	const auto view = row->lifetime().make_state<RowState>();

	const auto key = entry.key;
	const auto kind = entry.kind;
	// «alt:эмодзи» у плагинов: значок самого плагина из набора.
	const auto altIcon = entry.icon.startsWith(u"alt:"_q);
	const auto iconName = (entry.icon.isEmpty() || altIcon)
		? entry.sectionIcon
		: entry.icon;
	const auto autoAlt = altIcon
		? entry.icon.mid(4)
		: entry.icon.isEmpty() ? AutoAlt(entry.key) : QString();
	const auto &icon = IconByName(iconName);
	auto description = entry.description;
	if (const auto dot = description.indexOf(QStringLiteral(". ")); dot > 0) {
		description = description.left(dot + 1);
	}
	if (entry.requiresRestart) {
		description += (description.isEmpty() ? QString() : u"\n"_q)
			+ U(Mokagram::Lang::Tr("Начнёт работать после перезапуска клиента."));
	}
	const auto titleText = entry.title;
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		row,
		titleText,
		TitleLabelStyle());
	MakeTransparent(title);
	const auto text = description.isEmpty()
		? nullptr
		: Ui::CreateChild<Ui::FlatLabel>(
			row,
			description,
			TextLabelStyle());
	if (text) {
		MakeTransparent(text);
	}
	const auto example = entry.example.isEmpty()
		? nullptr
		: MakeExample(row, entry.example).get();
	const auto reset = MakeResetButton(row, QString(), [=] {
		Reset(key);
	});

	Ui::AbstractButton *toggle = nullptr;
	Ui::FlatLabel *valueLabel = nullptr;
	Ui::MediaSlider *slider = nullptr;
	Ui::InputField *field = nullptr;
	Ui::RpWidget *swatch = nullptr;
	const auto choiceText = [=](int index) {
		return (index >= 0 && index < int(entry.choices.size()))
			? entry.choices[index]
			: QString();
	};
	auto rippled = true;
	switch (kind) {
	case Kind::Bool:
		toggle = BoxParts::MakeSwitch(row, BoolValue(key));
		row->setClickedCallback([=] {
			SetBool(key, !Bool(key));
			afterChange();
		});
		row->setPointerCursor(true);
		break;
	case Kind::Choice: {
		valueLabel = Ui::CreateChild<Ui::FlatLabel>(
			row,
			choiceText(Int(key)),
			TextLabelStyle());
		MakeTransparent(valueLabel);
		row->setPointerCursor(true);
		const auto menu = row->lifetime().make_state<
			base::unique_qptr<Ui::PopupMenu>>();
		row->setClickedCallback([=] {
			// Без значков слева нет пустого места под них: обычный стиль меню.
			*menu = base::make_unique_q<Ui::PopupMenu>(
				row,
				(key == u"look.app_icon"_q)
					? st::popupMenuWithIcons
					: CompactChoiceMenuStyle());
			for (auto i = 0; i != int(entry.choices.size()); ++i) {
				const auto choose = [=] {
					SetInt(key, i);
					afterChange();
				};
				if (key == u"look.app_icon"_q) {
					const auto raw = (*menu)->menu();
					(*menu)->addAction(base::make_unique_q<AppIconMenuItem>(
						raw,
						raw->st(),
						Ui::Menu::CreateAction(raw, entry.choices[i], Fn<void()>(choose)),
						Mokagram::Misc::AppIconImage(i)));
					continue;
				}
				(*menu)->addAction(entry.choices[i], choose);
			}
			(*menu)->popup(QCursor::pos());
		});
		IntValue(key) | rpl::on_next([=](int value) {
			valueLabel->setText(choiceText(value));
			row->resize(row->width(), row->height());
		}, row->lifetime());
	} break;
	case Kind::Int: {
		rippled = false;
		valueLabel = Ui::CreateChild<Ui::FlatLabel>(
			row,
			QString::number(Int(key)),
			TitleLabelStyle());
		MakeTransparent(valueLabel);
		slider = Ui::CreateChild<Ui::MediaSlider>(
			row,
			st::localStorageLimitSlider);
		slider->setAlwaysDisplayMarker(true);
		const auto range = std::max(entry.maxValue - entry.minValue, 1);
		const auto toValue = [=](float64 progress) {
			return entry.minValue
				+ int(base::SafeRound(progress * range));
		};
		slider->setAdjustCallback([=](float64 progress) {
			return float64(toValue(progress) - entry.minValue) / range;
		});
		slider->setChangeProgressCallback([=](float64 progress) {
			SetInt(key, toValue(progress));
		});
		slider->setChangeFinishedCallback([=](float64 progress) {
			SetInt(key, toValue(progress));
			afterChange();
		});
		IntValue(key) | rpl::on_next([=](int value) {
			valueLabel->setText(QString::number(value));
			slider->setValue(float64(value - entry.minValue) / range);
			row->resize(row->width(), row->height());
		}, row->lifetime());
	} break;
	case Kind::Color: {
		const auto shown = [=](const QString &value) {
			return value.isEmpty() ? U(Mokagram::Lang::Tr("По теме")) : value.toUpper();
		};
		valueLabel = Ui::CreateChild<Ui::FlatLabel>(
			row,
			shown(String(key)),
			TextLabelStyle());
		MakeTransparent(valueLabel);
		swatch = Ui::CreateChild<Ui::RpWidget>(row);
		swatch->resize(22, 22);
		swatch->setAttribute(Qt::WA_TransparentForMouseEvents);
		swatch->paintOn([=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			const auto color = QColor(String(key));
			const auto r = QRectF(1, 1, 20, 20);
			if (color.isValid()) {
				p.setPen(QPen(QColor(255, 255, 255, 60), 1.));
				p.setBrush(color);
			} else {
				auto pen = QPen(QColor(255, 255, 255, 90), 1.5, Qt::DashLine);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
			}
			p.drawEllipse(r);
		});
		row->setPointerCursor(true);
		row->setClickedCallback([=] { ShowColorBox(titleText, key); });
		StringValue(key) | rpl::on_next([=](const QString &value) {
			valueLabel->setText(shown(value));
			swatch->update();
			row->resize(row->width(), row->height());
		}, row->lifetime());
	} break;
	case Kind::String: {
		if (entry.action) {
			valueLabel = Ui::CreateChild<Ui::FlatLabel>(
				row,
				String(key),
				TextLabelStyle());
			MakeTransparent(valueLabel);
			row->setPointerCursor(true);
			row->setClickedCallback([=] { entry.action(); });
			StringValue(key) | rpl::on_next([=](const QString &value) {
				valueLabel->setText(value);
				row->resize(row->width(), row->height());
			}, row->lifetime());
			break;
		}
		rippled = false;
		static const auto fieldStyle = [] {
			auto result = st::defaultInputField;
			result.textMargins = style::margins(0, 7, 0, 7);
			return result;
		}();
		field = Ui::CreateChild<Ui::InputField>(
			row,
			fieldStyle,
			rpl::single(QString()),
			String(key));
		field->changes() | rpl::on_next([=] {
			SetString(key, field->getLastText());
		}, field->lifetime());
		field->submits() | rpl::on_next([=] {
			afterChange();
		}, field->lifetime());
		StringValue(key) | rpl::on_next([=](const QString &value) {
			if (field->getLastText() != value) {
				field->setText(value);
			}
		}, row->lifetime());
	} break;
	}

	const auto relayout = [=] {
		const auto width = row->width();
		if (width <= 0) {
			return;
		}
		const auto left = kTextLeft;
		const auto full = width - left - kPad;
		const auto resetW = view->changed ? kResetSize + 2 : 0;
		const auto chevron = (kind == Kind::Choice || entry.action) ? 14 : 0;
		auto controlW = 0;
		if (toggle) {
			controlW = toggle->width();
		} else if (valueLabel) {
			valueLabel->resizeToNaturalWidth(full / 2);
			controlW = valueLabel->width() + chevron + (swatch ? 30 : 0);
		}
		const auto textW = full
			- controlW
			- resetW
			- ((controlW || resetW) ? 10 : 0);
		title->resizeToWidth(textW);
		auto y = kRowPad;
		title->move(left, y);
		y += title->height();
		if (text) {
			text->resizeToWidth(textW);
			y += 2;
			text->move(left, y);
			y += text->height();
		}
		if (example) {
			y += 8;
			example->resize(full, kExampleHeight);
			example->move(left, y);
			y += kExampleHeight;
		}
		const auto block = y + kRowPad;
		auto height = std::max(kMinRowHeight, block);
		const auto firstHeight = height;
		if (slider) {
			const auto sliderHeight = st::localStorageLimitSlider
				.seekSize.height();
			slider->setGeometry(left, y + 6, full, sliderHeight);
			height = y + 6 + sliderHeight + kRowPad;
		} else if (field) {
			field->resizeToWidth(full);
			field->move(left, y);
			height = y + field->height() + kRowPad;
		}
		view->plateTop = (firstHeight - kPlate) / 2;
		if (slider || field) {
			view->plateTop = std::min(view->plateTop, kRowPad);
		}
		const auto center = (slider || field)
			? (kRowPad + title->height() / 2)
			: (firstHeight / 2);
		auto right = width - kPad;
		if (toggle) {
			toggle->move(right - toggle->width(), center - toggle->height() / 2);
		} else if (valueLabel) {
			valueLabel->move(
				right - chevron - valueLabel->width(),
				center - valueLabel->height() / 2);
			if (swatch) {
				swatch->move(
					valueLabel->x() - 30,
					center - swatch->height() / 2);
			}
			view->chevronTop = center;
		}
		right -= controlW + (controlW ? 8 : 0);
		reset.button->move(
			right - kResetSize + (controlW ? 0 : 6),
			center - kResetSize / 2);
		row->resize(width, height);
		row->update();
	};
	const auto syncReset = [=](bool instant) {
		const auto changed = IsChanged(key);
		if (view->changed != changed) {
			view->changed = changed;
			reset.set(changed, instant);
			relayout();
		}
	};
	row->widthValue() | rpl::on_next(relayout, row->lifetime());
	PackIcons::Instance().updated() | rpl::on_next([=] {
		view->pack = PackIcon();
		row->update();
	}, row->lifetime());
	syncReset(true);
	Changed(
	) | rpl::filter([=](const QString &changed) {
		return changed == key;
	}) | rpl::on_next([=] {
		syncReset(false);
		relayout();
	}, row->lifetime());

	row->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(row);
		if (rippled) {
			// Волна не выходит за скруглённые углы группы.
			const auto origin = row->mapTo(card, QPoint());
			auto path = QPainterPath();
			path.addRoundedRect(
				QRectF(-origin.x(), -origin.y(), card->width(), card->height()),
				kGroupRadius,
				kGroupRadius);
			p.save();
			p.setClipPath(path);
			row->paintRipple(p, 0, 0);
			p.restore();
		}
		PaintPlate(
			p,
			QRect(kPad, view->plateTop, kPlate, kPlate),
			color,
			icon,
			iconName,
			autoAlt,
			view->pack,
			row);
		if (!view->first) {
			auto line = st::windowSubTextFg->c;
			line.setAlpha(40);
			p.fillRect(
				kTextLeft,
				0,
				row->width() - kTextLeft,
				st::lineWidth,
				line);
		}
		if (kind == Kind::Choice || entry.action) {
			auto hq = PainterHighQualityEnabler(p);
			auto pen = QPen(st::windowSubTextFg->c, 1.6);
			pen.setCapStyle(Qt::RoundCap);
			pen.setJoinStyle(Qt::RoundJoin);
			p.setPen(pen);
			const auto x = row->width() - kPad - 4.;
			const auto y = view->chevronTop;
			p.drawLine(QPointF(x - 3, y - 4), QPointF(x, y));
			p.drawLine(QPointF(x, y), QPointF(x - 3, y + 4));
		}
	}, row->lifetime());

	return {
		.wrap = wrap,
		.key = key,
		.haystack = (entry.title + ' ' + description + ' ' + entry.example
			+ ' ' + sectionName + ' ' + entry.key).toLower(),
		.dependsOn = entry.dependsOn,
		.section = section,
		.setFirst = [=](bool first) {
			if (view->first != first) {
				view->first = first;
				row->update();
			}
		},
		.highlight = [=](const QString &query) {
			title->setMarkedText(Marked(titleText, query));
			if (text) {
				text->setMarkedText(Marked(description, query));
			}
			relayout();
		},
	};
}

// Строка действия внизу: экспорт, импорт, сброс всего.
void AddActionRow(
		not_null<Ui::VerticalLayout*> card,
		const QString &titleText,
		const QString &noteText,
		const style::icon &icon,
		QColor color,
		bool first,
		Fn<void()> callback,
		const QString &alt = QString()) {
	const auto row = card->add(object_ptr<Ui::RippleButton>(
		card,
		st::defaultRippleAnimation));
	row->setPointerCursor(true);
	row->setClickedCallback(std::move(callback));
	const auto pack = row->lifetime().make_state<PackIcon>();
	const auto iconName = QString();
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		row,
		titleText,
		TitleLabelStyle());
	MakeTransparent(title);
	const auto note = Ui::CreateChild<Ui::FlatLabel>(
		row,
		noteText,
		TextLabelStyle());
	MakeTransparent(note);
	row->widthValue() | rpl::on_next([=](int width) {
		const auto textW = width - kTextLeft - kPad;
		if (textW <= 0) {
			return;
		}
		title->resizeToWidth(textW);
		note->resizeToWidth(textW);
		const auto block = title->height() + 2 + note->height();
		const auto height = std::max(kMinRowHeight, block + 2 * kRowPad);
		title->move(kTextLeft, (height - block) / 2);
		note->move(kTextLeft, (height - block) / 2 + title->height() + 2);
		row->resize(width, height);
	}, row->lifetime());
	row->paintRequest(
	) | rpl::on_next([=, &icon] {
		auto p = QPainter(row);
		const auto origin = row->mapTo(card, QPoint());
		auto path = QPainterPath();
		path.addRoundedRect(
			QRectF(-origin.x(), -origin.y(), card->width(), card->height()),
			kGroupRadius,
			kGroupRadius);
		p.save();
		p.setClipPath(path);
		row->paintRipple(p, 0, 0);
		p.restore();
		if (!first) {
			auto line = st::windowSubTextFg->c;
			line.setAlpha(40);
			p.fillRect(
				kTextLeft,
				0,
				row->width() - kTextLeft,
				st::lineWidth,
				line);
		}
		PaintPlate(
			p,
			QRect(kPad, (row->height() - kPlate) / 2, kPlate, kPlate),
			color,
			icon,
			iconName,
			alt,
			*pack,
			row);
	}, row->lifetime());
}

void PaintGroupBg(not_null<Ui::RpWidget*> card) {
	card->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(card->rect(), kGroupRadius, kGroupRadius);
	});
}

} // namespace

// После изменения, которое работает только с новым запуском: окно с
// кнопками «Отмена» и «Перезагрузить». Ползунок шлёт много изменений
// подряд, поэтому спрашиваем один раз, когда человек остановился.
void AskForRestart() {
	static auto generation = 0;
	static auto shown = false;
	const auto mine = ++generation;
	QTimer::singleShot(900, [=] {
		if (mine != generation || shown) {
			return;
		}
		const auto window = Core::App().activeWindow();
		if (!window) {
			return;
		}
		shown = true;
		window->show(Ui::MakeConfirmBox({
			.text = TextWithEntities{ U(Mokagram::Lang::Tr("Для применения этого изменения нужно перезагрузить клиент.")) },
			.confirmed = [](Fn<void()> close) {
				shown = false;
				close();
				Core::Restart();
			},
			.cancelled = [](Fn<void()> close) {
				shown = false;
				close();
			},
			.confirmText = U(Mokagram::Lang::Tr("Перезагрузить")),
			.cancelText = U(Mokagram::Lang::Tr("Отмена")),
			.title = rpl::single(U(Mokagram::Lang::Tr("Нужна перезагрузка"))),
		}));
	});
}

// К какой странице настроек mokaGram относится раздел.
[[nodiscard]] QString PageOfGroup(const Group &group) {
	if (group.entries.empty()) {
		return u"look"_q;
	}
	const auto key = group.entries.front().key;
	const auto starts = [&](std::initializer_list<const char*> prefixes) {
		for (const auto prefix : prefixes) {
			if (key.startsWith(QLatin1String(prefix))) {
				return true;
			}
		}
		return false;
	};
	if (starts({ "streamer.", "ghost.", "filters.", "privacy.", "regdate." })) {
		return u"privacy"_q;
	} else if (starts({ "scam.", "guard." })) {
		return u"security"_q;
	} else if (starts({ "history." })) {
		return u"history"_q;
	} else if (starts({ "player.", "lyrics." })) {
		return u"music"_q;
	} else if (starts({ "lag." })) {
		return u"performance"_q;
	} else if (starts({ "misc.ping", "net." })
		|| group.name == U(Mokagram::Lang::Tr("Аккаунты"))) {
		return u"system"_q;
	} else if (starts({ "fx.startup" })) {
		return u"look"_q;
	} else if (starts({ "chats.", "fx.", "inline.", "translation.", "documents.", "time.", "bots.", "adblock." })) {
		return u"chats"_q;
	} else if (starts({ "misc.stars", "quote." })) {
		return u"tools"_q;
	}
	return u"look"_q;
}

[[nodiscard]] QString PageTitle(const QString &page) {
	if (page == u"privacy"_q) return U(Mokagram::Lang::Tr("Приватность"));
	if (page == u"security"_q) return U(Mokagram::Lang::Tr("Безопасность"));
	if (page == u"history"_q) return U(Mokagram::Lang::Tr("Удалёнки"));
	if (page == u"music"_q) return U(Mokagram::Lang::Tr("Музыка"));
	if (page == u"system"_q) return U(Mokagram::Lang::Tr("Система"));
	if (page == u"performance"_q) return U(Mokagram::Lang::Tr("Производительность"));
	if (page == u"chats"_q) return U(Mokagram::Lang::Tr("Чаты"));
	if (page == u"tools"_q) return U(Mokagram::Lang::Tr("Инструменты"));
	return U(Mokagram::Lang::Tr("Кастомизация"));
}

void CustomizeBox(
		not_null<Ui::GenericBox*> box,
		Window::SessionController *controller,
		QString page) {
	RoundedNoButtons(box);
	if (controller) {
		PackIcons::Instance().ensure(&controller->session());
	}
	const auto content = box->verticalLayout();
	const auto weak = base::make_weak(controller);
	const auto toast = [=](const QString &text) {
		if (const auto strong = weak.get()) {
			strong->showToast(text);
		}
	};

	// Аккаунт SoundCloud живёт на странице «Музыка»: строка-кнопка входа и
	// отключение. Статус строки обновляется вместе с аккаунтом.
	if (page == u"music"_q) {
		Mokagram::Music::RegisterSoundCloudSettings();
		auto entries = std::vector<Entry>();
		for (const auto &entry : Entries()) {
			if (entry.key.startsWith(u"music.soundcloud_"_q)) {
				entries.push_back(entry);
			}
		}
		const auto inner = content->add(
			object_ptr<Ui::VerticalLayout>(content),
			QMargins(kOuterSide, 0, kOuterSide, 0));
		const auto head = inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				U(Mokagram::Lang::Tr("SOUNDCLOUD")),
				HeadLabelStyle()),
			QMargins(kPad, 20, kPad, 6));
		MakeTransparent(head);
		const auto card = inner->add(object_ptr<Ui::VerticalLayout>(inner));
		PaintGroupBg(card);
		const auto grey = Categories().back().color;
		auto first = true;
		for (const auto &entry : entries) {
			if (!entry.action) {
				continue;
			}
			const auto action = entry.action;
			AddActionRow(
				card,
				entry.key.endsWith(u"_logout"_q)
					? entry.title
					: (Mokagram::Music::Account::IsLoggedIn()
						? U(Mokagram::Lang::Tr("Аккаунт SoundCloud подключён"))
						: entry.title),
				entry.description,
				entry.key.endsWith(u"_logout"_q)
					? st::menuIconDelete
					: st::menuIconProfile,
				grey,
				first,
				[=] {
					action();
				},
				entry.key.endsWith(u"_logout"_q)
					? QString()
					: QString::fromUtf8("🔗"));
			first = false;
		}
		Mokagram::Music::Account::Changes(
		) | rpl::on_next([=] {
			auto value = QString::fromUtf8(
				Mokagram::Lang::Tr("Подключено"));
			const auto info = Mokagram::Music::Account::User();
			if (!info.username.isEmpty()) {
				value = u"@"_q + info.username;
			}
			SetString(
				u"music.soundcloud_login"_q,
				Mokagram::Music::Account::IsLoggedIn()
					? value
					: U(Mokagram::Lang::Tr("Подключить")));
		}, box->lifetime());
	}

	// Закреплённая шапка: закрыть, название, поиск и ниже чипы разделов.
	const auto pinned = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	const auto header = pinned->add(
		object_ptr<Ui::RpWidget>(pinned),
		QMargins(10, 10, 10, 0));
	header->resize(header->width(), 40);
	const auto close = MakeRoundButton(
		header,
		true,
		[=] { box->closeBox(); });
	const auto searchButton = MakeRoundButton(header, false, nullptr);
	const auto headerTitle = Ui::CreateChild<Ui::FlatLabel>(
		header,
		PageTitle(page),
		st::boxTitle);
	static const auto searchClear = style::owned_color(QColor(0, 0, 0, 0));
	static const auto fieldStyle = [] {
		auto result = st::defaultInputField;
		result.textBg = searchClear.color();
		result.textBgActive = searchClear.color();
		result.textMargins = style::margins(0, 7, 0, 7);
		result.placeholderFgActive = st::windowSubTextFg;
		result.placeholderScale = 0.;
		result.placeholderShift = 0;
		result.placeholderFont = st::normalFont;
		result.border = 0;
		result.borderActive = 0;
		result.heightMin = 36;
		return result;
	}();
	const auto pill = Ui::CreateChild<Ui::RpWidget>(header);
	pill->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(pill->rect(), 18, 18);
	});
	const auto field = Ui::CreateChild<Ui::InputField>(
		header,
		fieldStyle,
		rpl::single(U(Mokagram::Lang::Tr("Найти настройку"))));
	pill->hide();
	field->hide();
	header->widthValue() | rpl::on_next([=](int width) {
		close->move(0, (header->height() - close->height()) / 2);
		searchButton->move(
			width - searchButton->width(),
			(header->height() - searchButton->height()) / 2);
		headerTitle->move(
			(width - headerTitle->width()) / 2,
			(header->height() - headerTitle->height()) / 2);
		const auto left = close->width() + 8;
		const auto pillWidth = width - left - searchButton->width() - 8;
		pill->setGeometry(left, (header->height() - 36) / 2, pillWidth, 36);
		field->resizeToWidth(pillWidth - 28);
		field->move(
			left + 14,
			(header->height() - field->height()) / 2);
	}, header->lifetime());

	const auto chipsWrap = pinned->add(
		object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
			pinned,
			object_ptr<Ui::RpWidget>(pinned),
			QMargins(10, 8, 10, 8)));
	const auto chips = chipsWrap->entity();
	chips->resize(chips->width(), kChipHeight);
	const auto showChips = (page == u"look"_q);
	chipsWrap->toggle(showChips, anim::type::instant);

	struct ChipState {
		Ui::Animations::Simple animation;
		bool active = false;
	};
	struct Chip {
		not_null<Ui::AbstractButton*> button;
		ChipState *state = nullptr;
		QString text;
		int section = 0;
	};
	struct State {
		std::vector<SectionInfo> sections;
		std::vector<RowInfo> rows;
		std::vector<Chip> chips;
		QString query;
		bool built = false;
		Fn<void()> apply;
		Ui::SlideWrap<Ui::FlatLabel> *empty = nullptr;
		Ui::Animations::Simple scroll;
		Ui::Animations::Simple fade;
		// WHY: нажатый чип держим активным до ручной прокрутки:
		// нижние разделы не доезжают до верха, и по позиции выбрался бы другой.
		int forcedChip = -1;
		int forcedTop = 0;
		bool chipScrolling = false;
		Fn<void()> syncChips;
	};
	const auto state = box->lifetime().make_state<State>();

	auto groups = BuildGroups();
	groups.erase(
		std::remove_if(groups.begin(), groups.end(), [&](const Group &group) {
			return PageOfGroup(group) != page;
		}),
		groups.end());
	if (groups.empty()) {
		Ui::AddSkip(content, 14);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				U(Mokagram::Lang::Tr("Пока нет настроек.")),
				st::boxDividerLabel),
			QMargins(24, 16, 24, 8),
			style::al_top);
	}
	for (auto s = 0; s != int(groups.size()); ++s) {
		const auto &group = groups[s];
		const auto name = group.name;
		const auto color = Categories()[CategoryIndex(group.category)].color;
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content),
				QMargins(kOuterSide, 0, kOuterSide, 0)));
		wrap->toggle(true, anim::type::instant);
		const auto inner = wrap->entity();

		// Серая заглавная надпись и «Сбросить» справа.
		const auto head = inner->add(
			object_ptr<Ui::RpWidget>(inner),
			QMargins(0, 16, 0, 0));
		head->resize(head->width(), 28);
		const auto headLabel = Ui::CreateChild<Ui::FlatLabel>(
			head,
			name.toUpper(),
			HeadLabelStyle());
		MakeTransparent(headLabel);
		const auto resetSection = MakeResetButton(
			head,
			U(Mokagram::Lang::Tr("Сбросить")),
			[=] {
				for (const auto &entry : Entries()) {
					if (SectionOf(entry) == name) {
						Reset(entry.key);
					}
				}
			});
		head->widthValue() | rpl::on_next([=](int width) {
			headLabel->resizeToNaturalWidth(width - 120);
			headLabel->move(
				kPad,
				head->height() - headLabel->height() - 4);
			resetSection.button->move(
				width - kPad - resetSection.button->width(),
				head->height() - resetSection.button->height() - 2);
		}, head->lifetime());

		const auto card = inner->add(
			object_ptr<Ui::VerticalLayout>(inner));
		PaintGroupBg(card);
		state->sections.push_back({
			.name = name,
			.category = group.category,
			.wrap = wrap,
			.reset = resetSection,
		});
		for (const auto &entry : group.entries) {
			const auto restart = entry.requiresRestart;
			state->rows.push_back(AddRow(
				card,
				entry,
				color,
				s,
				name,
				[=] {
					if (restart) {
						AskForRestart();
					}
				}));
			state->sections[s].items.push_back(int(state->rows.size()) - 1);
		}
		if (false && !group.note.isEmpty()) {
			inner->add(
				object_ptr<Ui::FlatLabel>(
					inner,
					group.note,
					TextLabelStyle()),
				QMargins(kPad, 8, kPad, 0));
		}
	}

	state->empty = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				U(Mokagram::Lang::Tr("Ничего не найдено")),
				TextLabelStyle()),
			QMargins(24, 24, 24, 8)));
	state->empty->toggle(false, anim::type::instant);

	// Нижний раздел: экспорт, импорт и сброс всего.
	const auto actionsWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content),
			QMargins(kOuterSide, 0, kOuterSide, 0)));
	// Экспорт, импорт и сброс живут на странице «Система».
	actionsWrap->toggle(page == u"system"_q, anim::type::instant);
	{
		const auto inner = actionsWrap->entity();
		const auto head = inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				U(Mokagram::Lang::Tr("ДАННЫЕ")),
				HeadLabelStyle()),
			QMargins(kPad, 20, kPad, 6));
		MakeTransparent(head);
		const auto actions = inner->add(
			object_ptr<Ui::VerticalLayout>(inner));
		PaintGroupBg(actions);
		const auto grey = Categories().back().color;
		AddActionRow(
			actions,
			U(Mokagram::Lang::Tr("Сохранить настройки в файл")),
			U(Mokagram::Lang::Tr("Все ваши переключатели одним файлом, чтобы перенести на другой компьютер.")),
			st::menuIconExport,
			grey,
			true,
			[=] {
				const auto initial = QDir::homePath() + u"/moka.mokaset"_q;
				FileDialog::GetWritePath(
					box.get(),
					U(Mokagram::Lang::Tr("Экспорт настроек")),
					U(Mokagram::Lang::Tr("Набор настроек mokaGram (*.mokaset)")),
					initial,
					[=](QString &&path) {
						auto file = QFile(path);
						if (file.open(QIODevice::WriteOnly)
							&& file.write(Export()) >= 0) {
							toast(U(Mokagram::Lang::Tr("Настройки сохранены в файл")));
						} else {
							toast(U(Mokagram::Lang::Tr("Не удалось записать файл")));
						}
					});
			},
			QString::fromUtf8("📤"));
		AddActionRow(
			actions,
			U(Mokagram::Lang::Tr("Загрузить настройки из файла")),
			U(Mokagram::Lang::Tr("Перед применением покажем, сколько значений изменится.")),
			st::menuIconDownload,
			grey,
			false,
			[=] {
				FileDialog::GetOpenPath(
					box.get(),
					U(Mokagram::Lang::Tr("Импорт настроек")),
					U(Mokagram::Lang::Tr("Набор настроек mokaGram (*.mokaset)")),
					[=](FileDialog::OpenResult &&result) {
						auto data = result.remoteContent;
						if (data.isEmpty() && !result.paths.isEmpty()) {
							auto file = QFile(result.paths.front());
							if (file.open(QIODevice::ReadOnly)) {
								data = file.readAll();
							}
						}
						auto summary = ImportSummary();
						auto error = QString();
						if (data.isEmpty()) {
							toast(U(Mokagram::Lang::Tr("Не удалось прочитать файл")));
							return;
						} else if (!PreviewImport(data, &summary, &error)) {
							toast(error);
							return;
						}
						auto text = U(Mokagram::Lang::Tr("Будет изменено настроек: "))
							+ QString::number(summary.changed) + '.';
						if (summary.unknown) {
							text += U(Mokagram::Lang::Tr("\nНеизвестных (для плагинов, которых нет): "))
								+ QString::number(summary.unknown)
								+ U(Mokagram::Lang::Tr(", они сохранятся."));
						}
						if (summary.invalid) {
							text += U(Mokagram::Lang::Tr("\nПропущено неверных значений: "))
								+ QString::number(summary.invalid) + '.';
						}
						const auto strong = weak.get();
						if (!strong) {
							return;
						}
						strong->show(Box([=](
								not_null<Ui::GenericBox*> confirm) {
							Ui::ConfirmBox(confirm, {
								.text = text,
								.confirmed = [=](Fn<void()> closeConfirm) {
									auto failure = QString();
									if (Import(data, &failure)) {
										toast(U(Mokagram::Lang::Tr("Настройки применены")));
									} else {
										toast(failure);
									}
									closeConfirm();
								},
								.confirmText = U(Mokagram::Lang::Tr("Применить")),
								.title = U(Mokagram::Lang::Tr("Импорт настроек")),
							});
						}));
					});
			},
			QString::fromUtf8("🔽"));
		AddActionRow(
			actions,
			U(Mokagram::Lang::Tr("Вернуть всё как было")),
			U(Mokagram::Lang::Tr("Все настройки станут такими, какими были после установки.")),
			st::menuIconRestoreAttention,
			QColor(0xFF, 0x45, 0x3A),
			false,
			[=] {
				const auto strong = weak.get();
				if (!strong) {
					return;
				}
				strong->show(Box([=](not_null<Ui::GenericBox*> confirm) {
					Ui::ConfirmBox(confirm, {
						.text = U(Mokagram::Lang::Tr("Вернуть все настройки к значениям по умолчанию?")),
						.confirmed = [=](Fn<void()> closeConfirm) {
							ResetAll();
							closeConfirm();
						},
						.confirmText = U(Mokagram::Lang::Tr("Сбросить")),
					});
				}));
			},
			QString::fromUtf8("↩️"));
	}
	Ui::AddSkip(content, 22);

	// Показ и скрытие строк и разделов по поиску, зависимостям и сбросам.
	state->apply = [=] {
		const auto query = state->query;
		const auto kindOfAnim = state->built
			? anim::type::normal
			: anim::type::instant;
		auto any = false;
		auto shown = std::vector<bool>(state->sections.size(), false);
		auto firstDone = std::vector<bool>(state->sections.size(), false);
		for (const auto &row : state->rows) {
			const auto match = query.isEmpty()
				|| row.haystack.contains(query);
			const auto depends = !query.isEmpty()
				|| row.dependsOn.isEmpty()
				|| Bool(row.dependsOn);
			const auto visible = match && depends;
			row.wrap->toggle(visible, kindOfAnim);
			if (visible) {
				row.setFirst(!firstDone[row.section]);
				firstDone[row.section] = true;
			} else {
				row.setFirst(false);
			}
			if (match) {
				shown[row.section] = true;
			}
			row.highlight(query);
		}
		for (auto s = 0; s != int(state->sections.size()); ++s) {
			auto &section = state->sections[s];
			const auto show = query.isEmpty() || shown[s];
			any = any || show;
			section.wrap->toggle(show, kindOfAnim);
			auto changed = false;
			for (const auto index : section.items) {
				changed = changed || IsChanged(state->rows[index].key);
			}
			section.reset.set(changed, !state->built);
		}
		state->empty->entity()->setText(query.isEmpty()
			? U(Mokagram::Lang::Tr("Ничего не найдено"))
			: U(Mokagram::Lang::Tr("Ничего не найдено по запросу «")) + query + U("»"));
		state->empty->toggle(
			!any && !state->sections.empty(),
			kindOfAnim);
	};
	Changed() | rpl::on_next([=](const QString &) {
		state->apply();
	}, box->lifetime());
	state->apply();
	state->built = true;

	// Чипы: быстрый переход к разделам с прокруткой.
	auto seen = std::vector<QString>();
	for (auto s = 0; s != int(state->sections.size()); ++s) {
		const auto &category = state->sections[s].category;
		if (ranges::contains(seen, category)) {
			continue;
		}
		seen.push_back(category);
		const auto text = U(Categories()[CategoryIndex(category)].title);
		const auto index = int(state->chips.size());
		const auto button = Ui::CreateChild<Ui::AbstractButton>(chips);
		const auto chip = button->lifetime().make_state<ChipState>();
		button->setPointerCursor(true);
		button->resize(40, kChipHeight);
		button->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(button);
			auto hq = PainterHighQualityEnabler(p);
			const auto t = chip->animation.value(chip->active ? 1. : 0.);
			p.setPen(Qt::NoPen);
			p.setBrush(anim::color(st::windowBg->c, st::windowBgActive->c, t));
			p.drawRoundedRect(
				button->rect(),
				kChipHeight / 2.,
				kChipHeight / 2.);
			p.setFont(st::normalFont);
			p.setPen(anim::color(st::windowFg->c, st::windowFgActive->c, t));
			p.drawText(button->rect(), Qt::AlignCenter, text);
		}, button->lifetime());
		button->setClickedCallback([=] {
			if (!state->query.isEmpty()) {
				return;
			}
			const auto top = std::max(
				state->sections[s].wrap->y() + 4,
				0);
			state->forcedChip = index;
			state->forcedTop = box->scrollTop();
			state->syncChips();
			state->scroll.start([=](float64 value) {
				state->chipScrolling = true;
				box->scrollToY(int(base::SafeRound(value)));
				state->chipScrolling = false;
				state->forcedTop = box->scrollTop();
			}, box->scrollTop(), top, 340, anim::easeOutCubic);
		});
		state->chips.push_back({
			.button = button,
			.state = chip,
			.text = text,
			.section = s,
		});
	}
	chips->widthValue() | rpl::on_next([=](int width) {
		if (state->chips.empty()) {
			return;
		}
		const auto count = int(state->chips.size());
		auto sum = 0;
		for (const auto &chip : state->chips) {
			sum += st::normalFont->width(chip.text);
		}
		const auto gaps = 6 * (count - 1);
		const auto pad = std::clamp((width - gaps - sum) / (2 * count), 4, 14);
		auto x = 0;
		for (const auto &chip : state->chips) {
			const auto w = st::normalFont->width(chip.text) + 2 * pad;
			chip.button->setGeometry(x, 0, w, kChipHeight);
			x += w + 6;
		}
	}, chips->lifetime());
	state->syncChips = [=] {
		if (state->chips.empty()) {
			return;
		}
		if (state->forcedChip >= 0
			&& !state->chipScrolling
			&& box->scrollTop() != state->forcedTop) {
			state->forcedChip = -1;
		}
		const auto top = box->scrollTop() + 24;
		auto current = 0;
		for (auto i = 0; i != int(state->chips.size()); ++i) {
			if (state->sections[state->chips[i].section].wrap->y() <= top) {
				current = i;
			}
		}
		if (box->scrollTop() + box->scrollHeight()
			>= content->height() - 2) {
			current = int(state->chips.size()) - 1;
		}
		if (state->forcedChip >= 0) {
			current = state->forcedChip;
		}
		for (auto i = 0; i != int(state->chips.size()); ++i) {
			const auto chip = state->chips[i].state;
			const auto active = (i == current);
			if (chip->active != active) {
				chip->active = active;
				chip->animation.start(
					[button = state->chips[i].button] { button->update(); },
					active ? 0. : 1.,
					active ? 1. : 0.,
					220);
			}
		}
	};
	if (!state->chips.empty()) {
		state->chips.front().state->active = true;
	}
	box->scrolls() | rpl::on_next([=] {
		state->syncChips();
	}, box->lifetime());
	content->heightValue() | rpl::on_next([=] {
		state->syncChips();
	}, box->lifetime());

	// Поиск: поле вместо заголовка, чипы прячутся.
	const auto searching = std::make_shared<bool>(false);
	const auto applyFilter = [=] {
		state->query = *searching
			? field->getLastText().trimmed().toLower()
			: QString();
		state->apply();
	};
	field->changes() | rpl::on_next(applyFilter, field->lifetime());
	searchButton->setClickedCallback([=] {
		*searching = !*searching;
		headerTitle->setVisible(!*searching);
		pill->setVisible(*searching);
		field->setVisible(*searching);
		field->raise();
		chipsWrap->toggle(showChips && !*searching, anim::type::normal);
		if (*searching) {
			field->setFocusFast();
		} else {
			field->setText(QString());
		}
		applyFilter();
	});

	if (!PendingQuery.isEmpty()) {
		const auto query = base::take(PendingQuery);
		*searching = true;
		headerTitle->hide();
		pill->show();
		field->show();
		field->raise();
		chipsWrap->toggle(false, anim::type::instant);
		// Текст ставим уже после показа окна, иначе подсказка поля не гаснет.
		crl::on_main(box, [=] {
			field->setFocusFast();
			field->setText(query);
			applyFilter();
		});
	}

	// Плавное появление окна: закрывающая плёнка постепенно тает.
	const auto cover = Ui::CreateChild<Ui::RpWidget>(box.get());
	MakeTransparent(cover);
	cover->paintOn([=](QPainter &p) {
		const auto t = state->fade.value(0.);
		if (t <= 0.001) {
			return;
		}
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(t);
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(cover->rect(), 28, 28);
	});
	box->sizeValue() | rpl::on_next([=](QSize size) {
		cover->setGeometry(QRect(QPoint(), size));
		cover->raise();
	}, cover->lifetime());
	state->fade.start([=] {
		cover->update();
		if (state->fade.value(0.) <= 0.001) {
			cover->hide();
		}
	}, 1., 0., 380, anim::easeOutCubic);
}


namespace {

// Строка результата поиска по настройкам: название и «Раздел · группа».
class SettingsSearchRow final : public Ui::AbstractButton {
public:
	SettingsSearchRow(QWidget *parent, QString title, QString note)
	: AbstractButton(parent)
	, _title(std::move(title))
	, _note(std::move(note)) {
		setPointerCursor(true);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return style::ConvertScale(56);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		if (isOver()) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(rect().adjusted(s(6), s(2), -s(6), -s(2)), s(16), s(16));
		}
		const auto left = s(22);
		const auto available = width() - left - s(22);
		auto titleFont = st::semiboldFont->f;
		titleFont.setPixelSize(s(14));
		p.setFont(titleFont);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(left, s(8), available, s(20)),
			Qt::AlignVCenter | Qt::AlignLeft,
			QFontMetrics(titleFont).elidedText(_title, Qt::ElideRight, available));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, s(29), available, s(18)),
			Qt::AlignVCenter | Qt::AlignLeft,
			QFontMetrics(st::normalFont->f).elidedText(_note, Qt::ElideRight, available));
	}

private:
	QString _title;
	QString _note;

};

} // namespace

void ShowSettingsSearch(Window::SessionController *controller) {
	if (!controller) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		RoundedNoButtons(box);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto content = box->verticalLayout();

		const auto pinned = box->setPinnedToTopContent(
			object_ptr<Ui::VerticalLayout>(box));
		const auto header = pinned->add(
			object_ptr<Ui::RpWidget>(pinned),
			QMargins(10, 10, 10, 0));
		header->resize(header->width(), 40);
		const auto close = MakeRoundButton(
			header,
			true,
			[=] { box->closeBox(); });
		const auto title = Ui::CreateChild<Ui::FlatLabel>(
			header,
			U(Mokagram::Lang::Tr("Поиск настроек")),
			st::boxTitle);
		header->widthValue() | rpl::on_next([=](int width) {
			close->move(0, (header->height() - close->height()) / 2);
			title->move(
				(width - title->width()) / 2,
				(header->height() - title->height()) / 2);
		}, header->lifetime());

		// Поле в карточке, как в остальных окнах клиента.
		const auto fieldCard = pinned->add(
			object_ptr<Ui::VerticalLayout>(pinned),
			QMargins(s(14), s(10), s(14), s(8)));
		fieldCard->paintOn([=](QPainter &p) {
			BoxParts::PaintCard(p, fieldCard->rect());
		});
		const auto field = fieldCard->add(
			object_ptr<Ui::InputField>(
				fieldCard,
				BoxParts::CardField(st::defaultInputField),
				rpl::single(U(Mokagram::Lang::Tr("Название настройки или плагина")))),
			QMargins());

		const auto results = content->add(
			object_ptr<Ui::VerticalLayout>(content),
			QMargins(s(14), 0, s(14), s(14)));
		const auto rebuild = [=] {
			results->clear();
			const auto query = field->getLastText().trimmed().toLower();
			const auto words = query.split(u' ', Qt::SkipEmptyParts);
			if (words.isEmpty()) {
				return;
			}
			struct Hit {
				QString title;
				QString note;
				QString page;
			};
			auto hits = std::vector<Hit>();
			for (const auto &group : BuildGroups()) {
				const auto page = PageOfGroup(group);
				const auto pageTitle = PageTitle(page);
				for (const auto &entry : group.entries) {
					const auto haystack = (entry.title + u' ' + entry.description
						+ u' ' + group.name + u' ' + pageTitle).toLower();
					auto all = true;
					for (const auto &word : words) {
						all = all && haystack.contains(word);
					}
					if (all) {
						hits.push_back({
							entry.title,
							pageTitle + QString::fromUtf8(" · ") + group.name,
							page });
					}
				}
			}
			if (hits.empty()) {
				const auto empty = results->add(
					object_ptr<Ui::FlatLabel>(
						results,
						U(Mokagram::Lang::Tr("Ничего не нашлось.")),
						st::boxDividerLabel),
					QMargins(s(16), s(12), s(16), s(12)));
				Q_UNUSED(empty);
				return;
			}
			const auto card = results->add(object_ptr<Ui::VerticalLayout>(results));
			card->paintOn([=](QPainter &p) {
				BoxParts::PaintCard(p, card->rect());
			});
			Ui::AddSkip(card, s(4));
			const auto shown = std::min(int(hits.size()), 30);
			for (auto i = 0; i != shown; ++i) {
				const auto hit = hits[i];
				const auto row = card->add(object_ptr<SettingsSearchRow>(
					card,
					hit.title,
					hit.note));
				row->setClickedCallback([=] {
					const auto page = hit.page;
					const auto name = hit.title;
					box->closeBox();
					PendingQuery = name;
					ShowSettingsPage(controller, page);
				});
			}
			Ui::AddSkip(card, s(4));
		};
		field->changes() | rpl::on_next(rebuild, field->lifetime());
		box->setFocusCallback([=] { field->setFocusFast(); });
	}));
}

void ShowCustomizeSearch(
		Window::SessionController *controller,
		const QString &query) {
	PendingQuery = query;
	ShowSettingsPage(controller, u"look"_q);
}

void ShowCustomizeBox(Window::SessionController *controller) {
	ShowSettingsPage(controller, u"look"_q);
}

void ShowSettingsPage(
		Window::SessionController *controller,
		const QString &page) {
	if (controller) {
		controller->show(Box(CustomizeBox, controller, page));
	}
}

namespace {

struct CommandInfo {
	const char *name;
	const char *note;
	const style::icon *icon;
	QColor color;
	const char *alt;
};

void CommandsPage(
		not_null<Ui::GenericBox*> box,
		Window::SessionController *controller) {
	RoundedNoButtons(box);
	const auto content = box->verticalLayout();
	const auto toast = [weak = base::make_weak(controller)](const QString &text) {
		if (const auto strong = weak.get()) {
			strong->showToast(text);
		}
	};

	const auto pinned = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	const auto header = pinned->add(
		object_ptr<Ui::RpWidget>(pinned),
		QMargins(10, 10, 10, 8));
	header->resize(header->width(), 40);
	const auto close = MakeRoundButton(
		header,
		true,
		[=] { box->closeBox(); });
	const auto headerTitle = Ui::CreateChild<Ui::FlatLabel>(
		header,
		U(Mokagram::Lang::Tr("Быстрые команды")),
		st::boxTitle);
	header->widthValue() | rpl::on_next([=](int width) {
		close->move(0, (header->height() - close->height()) / 2);
		headerTitle->move(
			(width - headerTitle->width()) / 2,
			(header->height() - headerTitle->height()) / 2);
	}, header->lifetime());

	const auto body = content->add(object_ptr<Ui::VerticalLayout>(content));
	const auto build = std::make_shared<Fn<void()>>();
	const auto addHead = [=](const QString &text) {
		const auto head = body->add(
			object_ptr<Ui::RpWidget>(body),
			QMargins(kOuterSide, 16, kOuterSide, 0));
		head->resize(head->width(), 28);
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			head,
			text.toUpper(),
			HeadLabelStyle());
		MakeTransparent(label);
		label->move(kPad, 6);
	};
	const auto addCard = [=] {
		const auto wrap = body->add(
			object_ptr<Ui::VerticalLayout>(body),
			QMargins(kOuterSide, 0, kOuterSide, 0));
		PaintGroupBg(wrap);
		return wrap;
	};
	const auto note = [=](const QString &text) {
		const auto label = body->add(
			object_ptr<Ui::FlatLabel>(
				body,
				text,
				TextLabelStyle()),
			QMargins(kOuterSide + kPad, 6, kOuterSide + kPad, 0));
		Q_UNUSED(label);
	};

	*build = [=] {
		body->clear();
		const auto green = QColor(0x34, 0xC7, 0x59);
		const auto blue = QColor(0x0A, 0x84, 0xFF);
		const auto red = QColor(0xFF, 0x45, 0x3A);
		const auto orange = QColor(0xFF, 0x9F, 0x0A);
		const auto purple = QColor(0xAF, 0x52, 0xDE);
		const auto gray = QColor(0x8E, 0x8E, 0x93);

		// Включение.
		const auto enabled = Settings::Bool(u"commands.enabled"_q);
		{
			const auto card = addCard();
			AddActionRow(
				card,
				enabled ? U(Mokagram::Lang::Tr("Команды включены")) : U(Mokagram::Lang::Tr("Команды выключены")),
				U(Mokagram::Lang::Tr("Нажми, чтобы переключить. Команда срабатывает, только если в поле ввода больше ничего нет.")),
				st::menuIconChats,
				enabled ? green : gray,
				true,
				[=] {
					Settings::SetBool(u"commands.enabled"_q, !enabled);
					(*build)();
				},
				enabled ? QString::fromUtf8("✅") : QString::fromUtf8("🚫"));
		}

		addHead(U(Mokagram::Lang::Tr("Команды")));
		const auto commands = std::vector<CommandInfo>{
			{ ".mute [30m|2h|1d]", Mokagram::Lang::Tr("Заглушить чат. Ответом в группе: запретить человеку писать"), &st::menuIconMute, orange, "🔕" },
			{ ".unmute", Mokagram::Lang::Tr("Включить звук чата или снять запрет писать"), &st::menuIconUnmute, orange, "🔔" },
			{ ".ban", Mokagram::Lang::Tr("Исключить человека (ответом в группе) или заблокировать в личке"), &st::menuIconBlock, red, "🚫" },
			{ ".unban", Mokagram::Lang::Tr("Снять бан или разблокировать человека"), &st::menuIconBlock, green, "✅" },
			{ ".kick", Mokagram::Lang::Tr("Исключить без вечного бана, можно вернуться (ответом)"), &st::menuIconRemove, red, "⬅️" },
			{ ".del", Mokagram::Lang::Tr("Удалить сообщение, на которое ответил"), &st::menuIconDelete, red, "🗑" },
			{ ".del N", Mokagram::Lang::Tr("Удалить свои последние N сообщений (ответом: N сообщений человека)"), &st::menuIconDelete, red, "🗑" },
			{ ".del all", Mokagram::Lang::Tr("Ответом: удалить все сообщения человека. Без ответа: все твои загруженные"), &st::menuIconDelete, red, "🗑" },
			{ ".pin", Mokagram::Lang::Tr("Закрепить сообщение (ответом)"), &st::menuIconPin, blue, "📌" },
			{ ".unpin", Mokagram::Lang::Tr("Открепить сообщение (ответом)"), &st::menuIconUnpin, blue, "📍" },
			{ ".id", Mokagram::Lang::Tr("Скопировать ID автора, чата и сообщения и ссылку на сообщение"), &st::menuIconInfo, purple, "ℹ️" },
			{ ".tr en", Mokagram::Lang::Tr("Перевести и сразу отправить. Ответом на сообщение или .tr en текст"), &st::menuIconTranslate, blue, "🗣" },
			{ ".calc 2+2*3", Mokagram::Lang::Tr("Посчитать: в поле останется «2+2*3 = 8»"), &st::menuIconEdit, orange, "#️⃣" },
			{ ".archive", Mokagram::Lang::Tr("Отправить чат в архив (.unarchive вернёт)"), &st::menuIconShowInFolder, gray, "📁" },
			{ ".dl", Mokagram::Lang::Tr("Скачать файл или фото из сообщения (ответом) в загрузки"), &st::menuIconDownload, green, "🔽" },
			{ ".tag", Mokagram::Lang::Tr("Тег у собеседника: виден в списке чатов и под именем (.tag клиент, пусто = убрать)"), &st::menuIconTagFilter, blue, "📑" },
			{ ".snip", Mokagram::Lang::Tr("Свои сниппеты: .snip привет Здравствуйте!, дальше пишешь ;привет, и при отправке подставится текст (.snip list, .snip del имя)"), &st::menuIconEdit, blue, "📋" },
			{ ".note", Mokagram::Lang::Tr("Личная заметка о человеке: видна только тебе в его профиле"), &st::menuIconEdit, blue, "✏️" },
			{ ".save", Mokagram::Lang::Tr("Переслать сообщение (ответом) в «Избранное»"), &st::menuIconSavedMessages, purple, "📁" },
		};
		{
			const auto card = addCard();
			for (auto i = 0; i != int(commands.size()); ++i) {
				const auto &c = commands[i];
				const auto name = QString::fromUtf8(c.name);
				AddActionRow(
					card,
					name,
					U(c.note),
					*c.icon,
					c.color,
					(i == 0),
					[=] {
						QGuiApplication::clipboard()->setText(
							name.section(' ', 0, 0));
						toast(U(Mokagram::Lang::Tr("Скопировано: ")) + name.section(' ', 0, 0));
					},
					QString::fromUtf8(c.alt));
			}
		}
		note(U(Mokagram::Lang::Tr("Нажми на команду, чтобы скопировать её.")));

		addHead(U(Mokagram::Lang::Tr("Алиасы")));
		{
			const auto card = addCard();
			const auto aliases = Mokagram::Commands::Aliases();
			auto first = true;
			for (const auto &[alias, command] : aliases) {
				AddActionRow(
					card,
					U(".") + alias,
					U(Mokagram::Lang::Tr("то же, что .")) + command + U(Mokagram::Lang::Tr(". Нажми, чтобы удалить.")),
					st::menuIconRemove,
					gray,
					first,
					[=] {
						auto list = Mokagram::Commands::Aliases();
						list.erase(
							std::remove_if(
								list.begin(),
								list.end(),
								[&](const auto &pair) {
									return pair.first == alias;
								}),
							list.end());
						Mokagram::Commands::SetAliases(list);
						(*build)();
					},
					QString::fromUtf8("🗑"));
				first = false;
			}
			AddActionRow(
				card,
				U(Mokagram::Lang::Tr("Добавить алиас")),
				U(Mokagram::Lang::Tr("Своё короткое имя для команды, например rm для .del")),
				st::menuIconAdd,
				blue,
				first,
				[=] {
					controller->show(Box([=](not_null<Ui::GenericBox*> inner) {
						inner->setTitle(rpl::single(U(Mokagram::Lang::Tr("Новый алиас"))));
						inner->setWidth(360);
						const auto layout = inner->verticalLayout();
						const auto nameField = layout->add(
							object_ptr<Ui::InputField>(
								layout,
								st::defaultInputField,
								rpl::single(U(Mokagram::Lang::Tr("Имя, например rm"))),
								QString()),
							QMargins(22, 10, 22, 4));
						const auto commandField = layout->add(
							object_ptr<Ui::InputField>(
								layout,
								st::defaultInputField,
								rpl::single(U(Mokagram::Lang::Tr("Команда: "))
									+ Mokagram::Commands::CommandNames().join(u", "_q)),
								QString()),
							QMargins(22, 4, 22, 10));
						inner->setFocusCallback([=] {
							nameField->setFocusFast();
						});
						inner->addButton(rpl::single(U(Mokagram::Lang::Tr("Добавить"))), [=] {
							auto alias = nameField->getLastText().trimmed().toLower();
							auto command = commandField->getLastText().trimmed().toLower();
							while (alias.startsWith('.')) {
								alias = alias.mid(1);
							}
							while (command.startsWith('.')) {
								command = command.mid(1);
							}
							if (alias.isEmpty()
								|| alias.contains(QRegularExpression(u"[^\\p{L}\\d_]"_q))
								|| !Mokagram::Commands::CommandNames().contains(command)) {
								toast(U(Mokagram::Lang::Tr("Нужны имя из букв и цифр и существующая команда")));
								return;
							}
							auto list = Mokagram::Commands::Aliases();
							list.emplace_back(alias, command);
							Mokagram::Commands::SetAliases(list);
							inner->closeBox();
							(*build)();
						});
						inner->addButton(rpl::single(U(Mokagram::Lang::Tr("Отмена"))), [=] {
							inner->closeBox();
						});
					}));
				},
				QString::fromUtf8("➕"));
		}
		Ui::AddSkip(body, 16);
	};
	(*build)();
}

} // namespace

void ShowCommandsPage(Window::SessionController *controller) {
	if (controller) {
		controller->show(Box(CommandsPage, controller));
	}
}

void StartDebugCommands() {
	if (qEnvironmentVariableIsEmpty("MOKAGRAM_DEBUG_CMD")) {
		return;
	}
	const auto timer = new QTimer(qApp);
	QObject::connect(timer, &QTimer::timeout, [] {
		const auto path = Mokagram::Misc::DebugFilePath(u"mokagram_cmd"_q);
		if (path.isEmpty()) {
			return;
		}
		// Подложенный symlink не читаем и не удаляем по ссылке: снимаем саму
		// ссылку (unlink не ходит по symlink) и создаём на её месте обычный файл.
		if (QFileInfo(path).isSymLink()) {
			QFile::remove(path);
			QFile recreate(path);
			if (recreate.open(QIODevice::WriteOnly)) {
				recreate.close();
			}
			return;
		}
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return;
		}
		const auto cmd = QString::fromUtf8(file.readAll()).trimmed();
		file.close();
		QFile::remove(path);
		const auto window = Core::App().activePrimaryWindow();
		// Экран блокировки: сеанса ещё нет, работают только ввод и снимок.
		if (window && Core::App().passcodeLocked()) {
			if (cmd.startsWith(u"winshot:"_q)) {
				window->widget()->grab().save(cmd.mid(8));
			} else if (cmd.startsWith(u"type:"_q)) {
				if (const auto w = QApplication::focusWidget()) {
					for (const auto ch : cmd.mid(5)) {
						auto press = QKeyEvent(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
						QCoreApplication::sendEvent(w, &press);
					}
				}
			} else if (cmd == u"enter"_q) {
				if (const auto w = QApplication::focusWidget()) {
					auto press = QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(QChar('\r')));
					QCoreApplication::sendEvent(w, &press);
				}
			}
			return;
		}
		const auto controller = window ? window->sessionController() : nullptr;
		if (!controller) {
			return;
		}
		if (Mokagram::Pet::DebugCommand(cmd, &controller->session())) {
			return;
		}
		if (cmd == u"customize"_q) {
			ShowCustomizeBox(controller);
		} else if (cmd == u"profile"_q) {
			controller->showPeerInfo(controller->session().userPeerId());
		} else if (cmd.startsWith(u"profile:"_q)) {
			controller->showPeerInfo(PeerId(cmd.mid(8).toULongLong()));
		} else if (cmd.startsWith(u"chat:"_q)) {
			controller->showPeerHistory(
				PeerId(cmd.mid(5).toULongLong()),
				Window::SectionShow::Way::Forward);
		} else if (cmd.startsWith(u"sendtext:"_q)) {
			// Только тестовый чат mokaChat: отправка через тот же путь, что и из поля ввода.
			const auto history = controller->activeChatCurrent().history();
			if (history && history->peer->id.value == 562954397233167ULL) {
				auto message = Api::MessageToSend(Api::SendAction(history));
				message.textWithTags.text = cmd.mid(9);
				history->session().api().sendMessage(std::move(message));
			}
		} else if (cmd == u"fakereact"_q) {
			// Проверка цепочки события on_new_reaction в тестовом чате: настоящую реакцию может поставить только другой человек.
			const auto history = controller->activeChatCurrent().history();
			if (history && history->peer->id.value == 562954397233167ULL) {
				if (const auto item = history->lastMessage()) {
					history->session().changes().messageUpdated(
						item,
						Data::MessageUpdate::Flag::NewUnreadReaction);
				}
			}
		} else if (cmd == u"welcome"_q) {
			Mokagram::Plugins::ShowWelcome(controller);
		} else if (cmd.startsWith(u"colorbox:"_q)) {
			ShowColorBox(u"Мои сообщения: фон"_q, cmd.mid(9));
		} else if (cmd == u"pluginsbox"_q) {
			controller->show(Box(Mokagram::Plugins::PluginsBox, controller));
		} else if (cmd == u"sclogin"_q) {
			Mokagram::Music::ShowSoundCloudLogin(nullptr);
		} else if (cmd == u"pluginstore"_q) {
			controller->show(Box(
				Mokagram::Plugins::StoreBox,
				controller,
				Fn<void()>([] {})));
		} else if (cmd == u"quit"_q) {
			Core::Quit();
		} else if (cmd == u"history"_q) {
			History::ShowChatsBox(controller);
		} else if (cmd == u"deleted"_q) {
			History::ShowHistoryBox(controller, 0, History::Kind::Deleted);
		} else if (cmd == u"filters"_q) {
			History::ShowFiltersBox(controller);
		} else if (cmd == u"settings"_q) {
			controller->showSettings();
		} else if (cmd == u"editpeer"_q) {
			if (const auto peer = controller->activeChatCurrent().peer()) {
				controller->show(Box<EditPeerInfoBox>(controller, peer));
			}
		} else if (cmd == u"sendmoney"_q) {
			if (const auto peer = controller->activeChatCurrent().peer()) {
				if (const auto user = peer->asUser()) {
					Wallet::ShowSendToUser(controller->uiShow(), user);
				}
			}
		} else if (cmd == u"colors"_q) {
			const auto show = [](const char *name, const style::color &c) {
				fprintf(stderr, "COLOR %s %d %d %d %d\n", name, c->c.red(), c->c.green(), c->c.blue(), c->c.alpha());
			};
			show("windowBg", st::windowBg);
			show("dialogsBg", st::dialogsBg);
			show("boxBg", st::boxBg);
			show("dialogsBgOver", st::dialogsBgOver);
			show("windowBgOver", st::windowBgOver);
			show("searchedBarBg", st::searchedBarBg);
			show("filterInputInactiveBg", st::filterInputInactiveBg);
			show("layerBg", st::layerBg);
		} else if (cmd == u"search"_q) {
			controller->searchMessages(QString(), Dialogs::Key());
		} else if (cmd == u"datetime"_q) {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				Ui::ChooseDateTimeBox(box, {
					.title = rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Выбрать время"))),
					.submit = rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Запланировать"))),
					.done = [](TimeId) {},
					.min = [] { return base::unixtime::now(); },
					.time = base::unixtime::now() + 3600,
					.max = [] { return base::unixtime::now() + 86400 * 30; },
					.description = rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Описание"))),
				});
			}));
		} else if (cmd == u"newgroup"_q) {
			controller->show(Box<GroupInfoBox>(
				controller,
				GroupInfoBox::Type::Group));
		} else if (cmd == u"newchannel"_q) {
			controller->show(Box<GroupInfoBox>(
				controller,
				GroupInfoBox::Type::Channel));
		} else if (cmd.startsWith(u"at:"_q)) {
			const auto parts = cmd.mid(3).split(',');
			auto w = (QWidget*)controller->widget();
			auto pt = QPoint(parts[0].toInt(), parts[1].toInt());
			while (w) {
				fprintf(stderr, "AT %s %dx%d @%d,%d\n", w->metaObject()->className(), w->width(), w->height(), w->x(), w->y());
				const auto child = w->childAt(pt);
				if (!child) break;
				pt = child->mapFrom(w, pt);
				w = child;
			}
			{
				auto shown = 0;
				for (const auto child : w->children()) {
					if (const auto cw = qobject_cast<QWidget*>(child)) {
						fprintf(stderr, "CH %s %dx%d @%d,%d vis=%d\n", cw->metaObject()->className(), cw->width(), cw->height(), cw->x(), cw->y(), cw->isVisible() ? 1 : 0);
						if (++shown >= 6) break;
					}
				}
			}
			for (auto up = w; up; up = up->parentWidget()) {
				fprintf(stderr, "UP %s %dx%d @%d,%d\n", up->metaObject()->className(), up->width(), up->height(), up->x(), up->y());
			}
		} else if (cmd.startsWith(u"type:"_q)) {
			if (const auto w = QApplication::focusWidget()) {
				for (const auto ch : cmd.mid(5)) {
					auto press = QKeyEvent(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
					QCoreApplication::sendEvent(w, &press);
					auto release = QKeyEvent(QEvent::KeyRelease, 0, Qt::NoModifier, QString(ch));
					QCoreApplication::sendEvent(w, &release);
				}
			}
		} else if (cmd.startsWith(u"rclick:"_q)) {
			const auto parts = cmd.mid(7).split(',');
			auto w = (QWidget*)controller->widget();
			auto pt = QPoint(parts[0].toInt(), parts[1].toInt());
			while (const auto child = w->childAt(pt)) {
				pt = child->mapFrom(w, pt);
				w = child;
			}
			auto e = QContextMenuEvent(QContextMenuEvent::Mouse, pt, w->mapToGlobal(pt));
			QCoreApplication::sendEvent(w, &e);
		} else if (cmd.startsWith(u"click:"_q)) {
			const auto parts = cmd.mid(6).split(',');
			const auto mods = (parts.size() > 2)
				? Qt::KeyboardModifiers(Qt::ControlModifier)
				: Qt::KeyboardModifiers(Qt::NoModifier);
			auto w = (QWidget*)controller->widget();
			auto pt = QPoint(parts[0].toInt(), parts[1].toInt());
			const auto global = w->mapToGlobal(pt);
			for (const auto top : QApplication::topLevelWidgets()) {
				if (top != w->window() && top->isVisible() && top->geometry().contains(global)) {
					w = top;
					pt = top->mapFromGlobal(global);
					break;
				}
			}
			while (const auto child = w->childAt(pt)) {
				pt = child->mapFrom(w, pt);
				w = child;
			}
			{
				auto enter = QEnterEvent(QPointF(pt), w->mapToGlobal(QPointF(pt)), w->mapToGlobal(QPointF(pt)));
				QCoreApplication::sendEvent(w, &enter);
				auto move = QMouseEvent(QEvent::MouseMove, QPointF(pt), w->mapToGlobal(QPointF(pt)), Qt::NoButton, Qt::NoButton, mods);
				QCoreApplication::sendEvent(w, &move);
			}
			for (const auto type : { QEvent::MouseButtonPress, QEvent::MouseButtonRelease }) {
				auto e = QMouseEvent(type, QPointF(pt), w->mapToGlobal(QPointF(pt)), Qt::LeftButton, type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, mods);
				QCoreApplication::sendEvent(w, &e);
			}
		} else if (cmd.startsWith(u"url:"_q)) {
			Core::App().openLocalUrl(cmd.mid(4), {});
		} else if (cmd == u"menu"_q) {
			controller->widget()->showMainMenu();
		} else if (cmd == u"psave"_q) {
			PowerSaving::SetForceAll(true);
		} else if (cmd == u"psaveoff"_q) {
			PowerSaving::SetForceAll(false);
		} else if (cmd == u"msginfo"_q) {
			if (const auto history = controller->activeChatCurrent().history()) {
				if (const auto item = history->lastMessage()) {
					Mokagram::MessageInfo::Show(controller, item);
				}
			}
		} else if (cmd == u"copyauthor"_q) {
			if (const auto history = controller->activeChatCurrent().history()) {
				if (const auto item = history->lastMessage()) {
					Misc::CopyWithAuthor(item);
				}
			}
		} else if (cmd.startsWith(u"addfolder"_q)) {
			if (const auto history = controller->activeChatCurrent().history()) {
				const auto &list = history->owner().chatsFilters().list();
				const auto want = cmd.mid(9).toInt();
				for (const auto &f : list) {
					LOG(("MOKAFOLDER id=%1 contains=%2 inlist=%3").arg(f.id()).arg(f.contains(history)).arg(history->inChatList()));
					if (f.id() && f.id() == want) {
						ChooseFilterValidator(history).add(f.id());
					}
				}
			}
		} else if (cmd == u"clearfield"_q) {
			if (const auto w = QApplication::focusWidget()) {
				if (const auto field = dynamic_cast<Ui::InputField*>(
						w->parentWidget())) {
					field->setText(QString());
				}
			}
		} else if (cmd == u"enter"_q) {
			if (const auto w = QApplication::focusWidget()) {
				auto press = QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(QChar('\r')));
				QCoreApplication::sendEvent(w, &press);
			}
		} else if (cmd == u"dellast"_q) {
			if (const auto history = controller->activeChatCurrent().history()) {
				if (const auto item = history->lastMessage(); item && history->peer->isSelf() && item->out() && item->originalText().text.contains(u"test fx hello"_q)) {
					history->session().data().histories().deleteMessages({ item->fullId() }, true);
					history->session().data().histories().sendPendingReadInbox(history);
				}
			}
		} else if (cmd.startsWith(u"fxtest"_q)) {
			// Проигрывает эффект на нижних сообщениях, ничего не удаляя.
			if (cmd.size() > 7) {
				Settings::SetInt(u"fx.delete.style"_q, cmd.mid(7).toInt());
			}
			QWidget *inner = nullptr, *widget = nullptr;
			for (const auto child : controller->widget()->findChildren<QWidget*>()) {
				if (dynamic_cast<HistoryInner*>(child)) inner = child;
				if (dynamic_cast<HistoryWidget*>(child)) widget = child;
			}
			if (inner && widget) {
				const auto top = inner->height() - 110;
				Effects::PlayDelete(widget, inner, QRect(0, top, inner->width(), 100), true);
			}
		} else if (cmd == u"quote"_q) {
			if (const auto history = controller->activeChatCurrent().history()) {
				if (const auto last = history->lastMessage()) {
					auto ids = std::vector<FullMsgId>();
					for (auto i = 0; i != 3; ++i) {
						ids.push_back(FullMsgId(history->peer->id, last->id - i));
					}
					Quote::ShowQuoteBox(controller, std::move(ids));
				}
			}
		} else if (cmd.startsWith(u"setint:"_q)) {
			const auto parts = cmd.mid(7).split('=');
			if (parts.size() == 2) {
				Mokagram::Settings::SetInt(parts[0], parts[1].toInt());
			}
		} else if (cmd.startsWith(u"wheel:"_q)) {
			const auto parts = cmd.mid(6).split(',');
			auto w = (QWidget*)controller->widget();
			auto pt = QPoint(parts[0].toInt(), parts[1].toInt());
			while (const auto child = w->childAt(pt)) {
				pt = child->mapFrom(w, pt);
				w = child;
			}
			auto e = QWheelEvent(
				QPointF(pt),
				QPointF(w->mapToGlobal(pt)),
				QPoint(),
				QPoint(0, parts[2].toInt()),
				Qt::NoButton,
				Qt::NoModifier,
				Qt::NoScrollPhase,
				false);
			QCoreApplication::sendEvent(w, &e);
		} else if (cmd.startsWith(u"mdrag:"_q)) {
			const auto parts = cmd.mid(6).split(',');
			auto w = (QWidget*)controller->widget();
			auto pt1 = QPoint(parts[0].toInt(), parts[1].toInt());
			auto pt2 = QPoint(parts[2].toInt(), parts[3].toInt());
			auto pos = pt1;
			while (const auto child = w->childAt(pos)) {
				pos = child->mapFrom(w, pos);
				w = child;
			}
			const auto global1 = w->mapToGlobal(pos);
			const auto delta = pt2 - pt1;
			const auto send = [&](QEvent::Type type, QPoint p, Qt::MouseButton b, Qt::MouseButtons bs) {
				auto e = QMouseEvent(type, QPointF(p), QPointF(w->mapToGlobal(p)), b, bs, Qt::NoModifier);
				QCoreApplication::sendEvent(w, &e);
			};
			send(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
			send(QEvent::MouseMove, pos + delta / 2, Qt::NoButton, Qt::LeftButton);
			send(QEvent::MouseMove, pos + delta, Qt::NoButton, Qt::LeftButton);
			send(QEvent::MouseButtonRelease, pos + delta, Qt::LeftButton, Qt::NoButton);
			Q_UNUSED(global1);
		} else if (cmd == u"starsstats"_q) {
			Stars::ShowSpendingStatsBox();
		} else if (cmd == u"ping"_q) {
			Mokagram::Misc::ShowPingBox();
		} else if (cmd.startsWith(u"winshot:"_q)) {
			controller->widget()->window()->grab().save(cmd.mid(8));
		} else if (cmd.startsWith(u"fullshot:"_q)) {
			Mokagram::Music::DebugGrabFullscreen(cmd.mid(9));
		} else if (cmd == u"fullplayer"_q) {
			Mokagram::Music::ShowFullscreenPlayer(controller);
		} else if (cmd == u"player"_q) {
			Mokagram::Music::ShowPlayerBox(controller);
		} else if (cmd.startsWith(u"musictab:"_q)) {
			Mokagram::Music::DebugTab(cmd.mid(9).toInt());
		} else if (cmd.startsWith(u"musiclike:"_q)) {
			Mokagram::Music::DebugLike(cmd.mid(10).toInt());
		} else if (cmd.startsWith(u"musicplay:"_q)) {
			Mokagram::Music::DebugPlay(cmd.mid(10).toInt());
		} else if (cmd.startsWith(u"musicsave:"_q)) {
			Mokagram::Music::DebugSaveTrack(cmd.mid(10).toInt());
		} else if (cmd.startsWith(u"music"_q)) {
			Mokagram::Music::ShowMusicBox(controller, cmd.mid(6));
		} else if (cmd.startsWith(u"scsearch:"_q)) {
			Mokagram::Music::Search(cmd.mid(9), 0, [](Mokagram::Music::SearchResult result) {
				LOG(("[music] search: %1 tracks, error '%2', next %3").arg(result.tracks.size()).arg(result.error).arg(result.nextOffset));
				if (result.tracks.empty()) {
					return;
				}
				const auto track = result.tracks.front();
				LOG(("[music] first: '%1' by '%2', %3 ms, %4 hls=%5").arg(track.title).arg(track.artist).arg(track.durationMs).arg(track.extension).arg(track.hls));
				Mokagram::Music::Download(track, [](const QString &path, const QString &error) {
					LOG(("[music] download: path '%1' error '%2'").arg(path).arg(error));
				});
			});
		} else if (cmd.startsWith(u"deletemine"_q)) {
			auto peer = (PeerData*)controller->session().user();
			if (cmd.size() > 11) {
				auto index = cmd.mid(11).toInt();
				for (const auto &row : controller->session().data().chatsList()->indexed()->all()) {
					if (const auto history = row->history()) {
						if (!index--) {
							peer = history->peer;
							break;
						}
					}
				}
			}
			Mokagram::History::ShowDeleteMyMessages(controller, peer);
		} else if (cmd == u"readnow"_q) {
			if (const auto history = controller->activeChatCurrent().history()) {
				LOG(("MOKAREAD before unread=%1").arg(history->unreadCount()));
				Ghost::ReadNow(history);
				LOG(("MOKAREAD after unread=%1").arg(history->unreadCount()));
			}
		} else if (cmd.startsWith(u"passcode:set:"_q)) {
			// Только если код-пароля ещё нет: чужой не меняем.
			auto &local = Core::App().domain().local();
			if (!local.hasLocalPasscode()) {
				const auto result = local.setPasscode(
					cmd.mid(13).toUtf8(),
					Storage::PasscodeVerification());
				if (result == Storage::SetPasscodeResult::Success) {
					Core::App().localPasscodeChanged();
				}
			}
		} else if (cmd.startsWith(u"passcode:off:"_q)) {
			auto &local = Core::App().domain().local();
			const auto passcode = cmd.mid(13).toUtf8();
			if (const auto token = local.verifyPasscode(passcode)) {
				Mokagram::FakePasscode::RealPasscodeChanged(QByteArray());
				const auto result = local.setPasscode(QByteArray(), *token);
				if (result == Storage::SetPasscodeResult::Success) {
					Core::App().localPasscodeChanged();
				}
			}
		} else if (cmd == u"lock"_q) {
			if (Core::App().domain().local().hasLocalPasscode()) {
				Core::App().lockByPasscode();
			}
		} else if (cmd.startsWith(u"storybox:"_q)) {
			Mokagram::Stories::DebugShowCompose(controller, cmd.mid(9));
		} else if (cmd.startsWith(u"setbool:"_q)) {
			const auto parts = cmd.mid(8).split('=');
			if (parts.size() == 2) {
				Mokagram::Settings::SetBool(parts[0], parts[1] == u"1"_q);
			}
		} else if (cmd.startsWith(u"copyfwdtest:"_q)) {
			// Последнее сообщение чата копией в «Избранное».
			const auto peerId = PeerId(cmd.mid(12).toULongLong());
			auto &owner = controller->session().data();
			if (const auto history = owner.historyLoaded(peerId)) {
				if (const auto item = history->lastMessage()) {
					Mokagram::Forward::CopyForward(
						&controller->session(),
						{ not_null<HistoryItem*>(item) },
						Api::SendAction(owner.history(
							controller->session().user())),
						false);
				}
			}
		} else if (cmd.startsWith(u"localpin:"_q)) {
			if (const auto peer = controller->session().data().peerLoaded(
					PeerId(cmd.mid(9).toULongLong()))) {
				Mokagram::Tweaks::ToggleLocalPin(peer);
			}
		} else if (cmd.startsWith(u"stripfile:"_q)) {
			const auto parts = cmd.mid(10).split(',');
			if (parts.size() == 2) {
				auto in = QFile(parts[0]);
				if (in.open(QIODevice::ReadOnly)) {
					const auto cleaned = Mokagram::Tweaks::StripMetadata(
						in.readAll());
					auto out = QFile(parts[1]);
					if (out.open(QIODevice::WriteOnly)) {
						out.write(cleaned);
					}
				}
			}
		} else if (cmd == u"protected"_q) {
			// Только список защищённых чатов в stderr, без действий.
			const auto raw = Mokagram::Forward::RawScope();
			auto &owner = controller->session().data();
			owner.enumerateBroadcasts([&](not_null<ChannelData*> channel) {
				if (!channel->allowsForwarding()) {
					fprintf(stderr, "PROTECTED %llu %s\n",
						(unsigned long long)channel->id.value,
						channel->name().toUtf8().constData());
				}
			});
			owner.enumerateGroups([&](not_null<PeerData*> group) {
				if (!group->allowsForwarding()) {
					fprintf(stderr, "PROTECTED %llu %s\n",
						(unsigned long long)group->id.value,
						group->name().toUtf8().constData());
				}
			});
			fflush(stderr);
		} else if (cmd == u"storydeltest"_q) {
			// Удаляет только свои истории с тестовой подписью.
			auto &stories = controller->session().data().stories();
			const auto self = controller->session().user()->id;
			auto ids = std::vector<FullStoryId>();
			if (const auto source = stories.source(self)) {
				for (const auto &entry : source->ids) {
					const auto id = FullStoryId{ self, entry.id };
					if (const auto story = stories.lookup(id)) {
						if ((*story)->caption().text
							== u"mokagram test story"_q) {
							ids.push_back(id);
						}
					}
				}
			}
			if (!ids.empty()) {
				stories.deleteList(ids);
			}
			controller->showToast(u"test stories: "_q
				+ QString::number(ids.size()));
		} else if (cmd == u"stash"_q) {
			Shortcuts::Launch(Shortcuts::Command::StashMessage);
		} else if (cmd.startsWith(u"peek:"_q)) {
			const auto userId = UserId(cmd.mid(5).toULongLong());
			if (const auto user = controller->session().data().userLoaded(userId)) {
				Mokagram::Peek::Start(user);
			}
		} else if (cmd.startsWith(u"action:"_q)) {
			// Действие одной строки настроек по ключу.
			for (const auto &entry : Entries()) {
				if (entry.key == cmd.mid(7) && entry.action) {
					entry.action();
				}
			}
		} else if (cmd == u"schedule"_q) {
			for (const auto &entry : Entries()) {
				if (entry.action) {
					entry.action();
				}
			}
		} else if (cmd.startsWith(u"wheel:"_q)) {
			const auto parts = cmd.mid(6).split(',');
			auto w = (QWidget*)controller->widget();
			auto pt = QPoint(parts[0].toInt(), parts[1].toInt());
			while (const auto child = w->childAt(pt)) {
				pt = child->mapFrom(w, pt);
				w = child;
			}
			auto e = QWheelEvent(
				QPointF(pt),
				QPointF(w->mapToGlobal(pt)),
				QPoint(),
				QPoint(0, parts[2].toInt()),
				Qt::NoButton,
				Qt::NoModifier,
				Qt::NoScrollPhase,
				false);
			for (auto t = w; t; t = t->parentWidget()) {
				e.setAccepted(false);
				QCoreApplication::sendEvent(t, &e);
				if (e.isAccepted()) {
					break;
				}
			}
		} else if (cmd.startsWith(u"hover:"_q)) {
			const auto parts = cmd.mid(6).split(',');
			auto w = (QWidget*)controller->widget();
			auto pt = QPoint(parts[0].toInt(), parts[1].toInt());
			const auto global = w->mapToGlobal(pt);
			for (const auto top : QApplication::topLevelWidgets()) {
				if (top != w->window() && top->isVisible() && top->geometry().contains(global)) {
					w = top;
					pt = top->mapFromGlobal(global);
					break;
				}
			}
			while (const auto child = w->childAt(pt)) {
				pt = child->mapFrom(w, pt);
				w = child;
			}
			fprintf(stderr, "HOVER %s\n", w->metaObject()->className());
			auto enter = QEnterEvent(QPointF(pt), QPointF(w->mapToGlobal(pt)), QPointF(w->mapToGlobal(pt)));
			QCoreApplication::sendEvent(w, &enter);
			auto move = QMouseEvent(QEvent::MouseMove, QPointF(pt), QPointF(w->mapToGlobal(pt)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
			QCoreApplication::sendEvent(w, &move);
		} else if (cmd == u"clip"_q) {
			LOG(("MOKACLIP %1").arg(QGuiApplication::clipboard()->text()));
		} else if (cmd == u"close"_q) {
			controller->hideLayer();
		}
	});
	timer->start(400);
}


const style::icon &OwnRowIcon() {
	static const style::icon result = st::menuIconPalette;
	return result;
}

namespace {
[[nodiscard]] const base::flat_map<const void*, QString> &RowIconMap() {
	static const auto map = base::flat_map<const void*, QString>{
		{ &st::menuIconProfile, QString::fromUtf8("👤") },
		{ &st::menuIconPhone, QString::fromUtf8("📞") },
		{ &st::menuIconUsername, QString::fromUtf8("🔗") },
		{ &st::settingsIconChatAutomation, QString::fromUtf8("🤖") },
		{ &st::menuIconNotifications, QString::fromUtf8("🔔") },
		{ &st::menuIconLock, QString::fromUtf8("🔑") },
		{ &st::menuIconChatBubble, QString::fromUtf8("💬") },
		{ &st::menuIconShowInFolder, QString::fromUtf8("📁") },
		{ &st::menuIconManage, QString::fromUtf8("⚙️") },
		{ &st::menuIconReport, QString::fromUtf8("❗️") },
		{ &st::menuIconUnmute, QString::fromUtf8("🎵") },
		{ &st::menuIconBotCommands, QString::fromUtf8("🔧") },
		{ &st::menuIconPalette, QString::fromUtf8("🖼") },
		{ &st::menuIconPowerUsage, QString::fromUtf8("💤") },
		{ &st::menuIconTranslate, QString::fromUtf8("🗣") },
		{ &st::menuIconTon, QString::fromUtf8("💰") },
		{ &st::menuIconShop, QString::fromUtf8("📈") },
		{ &st::menuIconGiftPremium, QString::fromUtf8("🎁") },
		{ &st::menuIconFaq, QString::fromUtf8("❔") },
		{ &st::menuIconEmojiObjects, QString::fromUtf8("ℹ️") },
		{ &st::menuIconShowInChat, QString::fromUtf8("👁") },
		{ &st::menuIconDiscussion, QString::fromUtf8("💬") },
		{ &st::menuIconCopy, QString::fromUtf8("📋") },
		{ &st::menuIconStickers, QString::fromUtf8("🙂") },
		{ &st::menuIconPermissions, QString::fromUtf8("🛡") },
		{ &st::menuIconDeleteAttention, QString::fromUtf8("🗑") },
		{ &st::menuIconChannel, QString::fromUtf8("📢") },
		{ &st::menuIconNetwork, QString::fromUtf8("🔗") },
		{ &st::menuIconGroups, QString::fromUtf8("👥") },
		{ &st::menuIconGroupReactions, QString::fromUtf8("💟") },
		{ &st::menuIconEmoji, QString::fromUtf8("😜") },
		{ &st::menuIconEdit, QString::fromUtf8("✏️") },
		{ &st::menuIconChangeColors, QString::fromUtf8("🎈") },
		{ &st::menuIconTTL, QString::fromUtf8("⏳") },
		{ &st::menuIconShortcut, QString::fromUtf8("▶️") },
		{ &st::menuIconShare, QString::fromUtf8("📤") },
		{ &st::menuIconReplace, QString::fromUtf8("↩️") },
		{ &st::menuIconRecoveryEmail, QString::fromUtf8("📑") },
		{ &st::menuIconPremium, QString::fromUtf8("⭐️") },
		{ &st::menuIconPin, QString::fromUtf8("📌") },
		{ &st::menuIconPhone, QString::fromUtf8("📞") },
		{ &st::menuIconNightMode, QString::fromUtf8("💤") },
		{ &st::menuIconLink, QString::fromUtf8("🔗") },
		{ &st::menuIconLeaveAttention, QString::fromUtf8("⛔️") },
		{ &st::menuIconIpAddress, QString::fromUtf8("📍") },
		{ &st::menuIconFont, QString::fromUtf8("✏️") },
		{ &st::menuIconDevices, QString::fromUtf8("🤳") },
		{ &st::menuIconDelete, QString::fromUtf8("🗑") },
		{ &st::menuIconBlock, QString::fromUtf8("🚫") },
		{ &st::menuIconWinHello, QString::fromUtf8("🔑") },
		{ &st::menuIconUsername, QString::fromUtf8("👤") },
		{ &st::menuIconUnpin, QString::fromUtf8("📌") },
		{ &st::menuIconTouchID, QString::fromUtf8("🔑") },
		{ &st::menuIconTopics, QString::fromUtf8("📑") },
		{ &st::menuIconTimer, QString::fromUtf8("⏳") },
		{ &st::menuIconTagSell, QString::fromUtf8("💰") },
		{ &st::menuIconTagRemove, QString::fromUtf8("❌") },
		{ &st::menuIconSystemPwd, QString::fromUtf8("🔓") },
		{ &st::menuIconStorage, QString::fromUtf8("📁") },
		{ &st::menuIconStealth, QString::fromUtf8("🫣") },
		{ &st::menuIconStats, QString::fromUtf8("📊") },
		{ &st::menuIconStartStream, QString::fromUtf8("🎙") },
		{ &st::menuIconSoundOn, QString::fromUtf8("🎵") },
		{ &st::menuIconSettings, QString::fromUtf8("⚙️") },
		{ &st::menuIconQrCode, QString::fromUtf8("📐") },
		{ &st::menuIconPhoto, QString::fromUtf8("🖼") },
		{ &st::menuIconNftWear, QString::fromUtf8("👑") },
		{ &st::menuIconNftTakeOff, QString::fromUtf8("👑") },
		{ &st::menuIconNewWindow, QString::fromUtf8("➕") },
		{ &st::menuIconMarkUnread, QString::fromUtf8("🔴") },
		{ &st::menuIconInviteSettings, QString::fromUtf8("🔗") },
		{ &st::menuIconInvite, QString::fromUtf8("➕") },
		{ &st::menuIconInfo, QString::fromUtf8("ℹ️") },
		{ &st::menuIconImportTheme, QString::fromUtf8("🖼") },
		{ &st::menuIconFaq, QString::fromUtf8("❔") },
		{ &st::menuIconExport, QString::fromUtf8("📤") },
		{ &st::menuIconExperimental, QString::fromUtf8("🔧") },
		{ &st::menuIconEarn, QString::fromUtf8("💰") },
		{ &st::menuIconDownload, QString::fromUtf8("🔽") },
		{ &st::menuIconDockBounce, QString::fromUtf8("🎈") },
		{ &st::menuIconCreatePoll, QString::fromUtf8("📊") },
		{ &st::menuIconCraft, QString::fromUtf8("🔧") },
		{ &st::menuIconCancel, QString::fromUtf8("❌") },
		{ &st::menuIconCallsReceive, QString::fromUtf8("📞") },
		{ &st::menuIconBlockSettings, QString::fromUtf8("🚫") },
		{ &st::menuIconArchive, QString::fromUtf8("📁") },
		{ &st::menuIconAppleWatch, QString::fromUtf8("🔘") },
		{ &st::menuIconAddress, QString::fromUtf8("📍") },
		{ &st::menuIconAddAccount, QString::fromUtf8("➕") },
		{ &st::menuIcon2SV, QString::fromUtf8("🔑") },
		{ &st::menuIconAbove, QString::fromUtf8("🔼") },
		{ &st::menuIconAdd, QString::fromUtf8("➕") },
		{ &st::menuIconAddToFolder, QString::fromUtf8("📁") },
		{ &st::menuIconAdmin, QString::fromUtf8("👮") },
		{ &st::menuIconAntispam, QString::fromUtf8("🛡") },
		{ &st::menuIconArchiveOpen, QString::fromUtf8("📁") },
		{ &st::menuIconArticle, QString::fromUtf8("📄") },
		{ &st::menuIconAsMessages, QString::fromUtf8("💬") },
		{ &st::menuIconAsTopics, QString::fromUtf8("📑") },
		{ &st::menuIconAuctionDrop, QString::fromUtf8("📈") },
		{ &st::menuIconBelow, QString::fromUtf8("🔽") },
		{ &st::menuIconBlockAttention, QString::fromUtf8("🚫") },
		{ &st::menuIconBoosts, QString::fromUtf8("💎") },
		{ &st::menuIconBot, QString::fromUtf8("🤖") },
		{ &st::menuIconCancelAttention, QString::fromUtf8("❌") },
		{ &st::menuIconCancelFee, QString::fromUtf8("❌") },
		{ &st::menuIconCaptionHide, QString::fromUtf8("🫣") },
		{ &st::menuIconCaptionShow, QString::fromUtf8("👁") },
		{ &st::menuIconChangeOrder, QString::fromUtf8("🔽") },
		{ &st::menuIconChatDiscuss, QString::fromUtf8("💬") },
		{ &st::menuIconChats, QString::fromUtf8("💬") },
		{ &st::menuIconClear, QString::fromUtf8("🗑") },
		{ &st::menuIconCollapse, QString::fromUtf8("🔼") },
		{ &st::menuIconCommunity, QString::fromUtf8("👥") },
		{ &st::menuIconCommunityRemoveAttention, QString::fromUtf8("⛔️") },
		{ &st::menuIconCopyright, QString::fromUtf8("📄") },
		{ &st::menuIconCraftChance, QString::fromUtf8("🎲") },
		{ &st::menuIconCraftTraits, QString::fromUtf8("🔧") },
		{ &st::menuIconCreateTodoList, QString::fromUtf8("☑️") },
		{ &st::menuIconCustomize, QString::fromUtf8("🎈") },
		{ &st::menuIconDisableAttention, QString::fromUtf8("⛔️") },
		{ &st::menuIconDownloadOff, QString::fromUtf8("⛔️") },
		{ &st::menuIconDraw, QString::fromUtf8("✏️") },
		{ &st::menuIconDrugs, QString::fromUtf8("⚠️") },
		{ &st::menuIconEnlarge, QString::fromUtf8("➕") },
		{ &st::menuIconExpand, QString::fromUtf8("🔽") },
		{ &st::menuIconExportTheme, QString::fromUtf8("📤") },
		{ &st::menuIconFactcheck, QString::fromUtf8("✅") },
		{ &st::menuIconFake, QString::fromUtf8("⚠️") },
		{ &st::menuIconFave, QString::fromUtf8("⭐️") },
		{ &st::menuIconFile, QString::fromUtf8("📄") },
		{ &st::menuIconForward, QString::fromUtf8("➡️") },
		{ &st::menuIconFromMainMenu, QString::fromUtf8("⬅️") },
		{ &st::menuIconGif, QString::fromUtf8("🎞") },
		{ &st::menuIconGroupLog, QString::fromUtf8("📋") },
		{ &st::menuIconGroupsHide, QString::fromUtf8("🫣") },
		{ &st::menuIconHourglass, QString::fromUtf8("⏳") },
		{ &st::menuIconKeysLegend, QString::fromUtf8("🔑") },
		{ &st::menuIconLeave, QString::fromUtf8("⛔️") },
		{ &st::menuIconLike, QString::fromUtf8("💟") },
		{ &st::menuIconLinks, QString::fromUtf8("🔗") },
		{ &st::menuIconMarkRead, QString::fromUtf8("✅") },
		{ &st::menuIconMute, QString::fromUtf8("🔕") },
		{ &st::menuIconMuteFor, QString::fromUtf8("🔕") },
		{ &st::menuIconMuteForAny, QString::fromUtf8("🔕") },
		{ &st::menuIconOrderDate, QString::fromUtf8("🗓") },
		{ &st::menuIconOrderNumber, QString::fromUtf8("#️⃣") },
		{ &st::menuIconOrderPrice, QString::fromUtf8("💰") },
		{ &st::menuIconPayment, QString::fromUtf8("💰") },
		{ &st::menuIconPendingRequests, QString::fromUtf8("⏳") },
		{ &st::menuIconPersonal, QString::fromUtf8("👤") },
		{ &st::menuIconPhotoSet, QString::fromUtf8("🖼") },
		{ &st::menuIconPhotoSuggest, QString::fromUtf8("🖼") },
		{ &st::menuIconPorn, QString::fromUtf8("⚠️") },
		{ &st::menuIconPromote, QString::fromUtf8("📈") },
		{ &st::menuIconQualityHigh, QString::fromUtf8("📹") },
		{ &st::menuIconRatingGifts, QString::fromUtf8("🎁") },
		{ &st::menuIconRatingRefund, QString::fromUtf8("↩️") },
		{ &st::menuIconRatingUsers, QString::fromUtf8("👥") },
		{ &st::menuIconReactions, QString::fromUtf8("💟") },
		{ &st::menuIconRemove, QString::fromUtf8("❌") },
		{ &st::menuIconRemoveAttention, QString::fromUtf8("❌") },
		{ &st::menuIconRemovedUsers, QString::fromUtf8("🚫") },
		{ &st::menuIconReorder, QString::fromUtf8("🔽") },
		{ &st::menuIconReply, QString::fromUtf8("↩️") },
		{ &st::menuIconReport, QString::fromUtf8("⚠️") },
		{ &st::menuIconReportAntiSpam, QString::fromUtf8("🛡") },
		{ &st::menuIconReportAttention, QString::fromUtf8("⚠️") },
		{ &st::menuIconReschedule, QString::fromUtf8("🗓") },
		{ &st::menuIconRestartBot, QString::fromUtf8("🤖") },
		{ &st::menuIconRestore, QString::fromUtf8("↩️") },
		{ &st::menuIconRestoreAttention, QString::fromUtf8("↩️") },
		{ &st::menuIconRetractVote, QString::fromUtf8("↩️") },
		{ &st::menuIconSavedMessages, QString::fromUtf8("⭐️") },
		{ &st::menuIconSaveImage, QString::fromUtf8("🔽") },
		{ &st::menuIconSchedule, QString::fromUtf8("🗓") },
		{ &st::menuIconSearch, QString::fromUtf8("👁") },
		{ &st::menuIconSelect, QString::fromUtf8("✔️") },
		{ &st::menuIconSend, QString::fromUtf8("➡️") },
		{ &st::menuIconShareOff, QString::fromUtf8("🚫") },
		{ &st::menuIconShareOn, QString::fromUtf8("📤") },
		{ &st::menuIconSharing, QString::fromUtf8("📤") },
		{ &st::menuIconShowAll, QString::fromUtf8("👁") },
		{ &st::menuIconShrink, QString::fromUtf8("🔼") },
		{ &st::menuIconSigned, QString::fromUtf8("✏️") },
		{ &st::menuIconSilent, QString::fromUtf8("🔕") },
		{ &st::menuIconSoundAdd, QString::fromUtf8("🎵") },
		{ &st::menuIconSoundOff, QString::fromUtf8("🔕") },
		{ &st::menuIconSoundSelect, QString::fromUtf8("🎵") },
		{ &st::menuIconSpoiler, QString::fromUtf8("🫣") },
		{ &st::menuIconStar, QString::fromUtf8("⭐️") },
		{ &st::menuIconStarRefLink, QString::fromUtf8("🔗") },
		{ &st::menuIconStarRefShare, QString::fromUtf8("📤") },
		{ &st::menuIconStarsCarryover, QString::fromUtf8("⭐️") },
		{ &st::menuIconStarsRefund, QString::fromUtf8("↩️") },
		{ &st::menuIconStartStreamWith, QString::fromUtf8("🎙") },
		{ &st::menuIconStealthLocked, QString::fromUtf8("🫣") },
		{ &st::menuIconStickerAdd, QString::fromUtf8("🙂") },
		{ &st::menuIconStickerCreate, QString::fromUtf8("🙂") },
		{ &st::menuIconStoriesArchive, QString::fromUtf8("📁") },
		{ &st::menuIconStoriesArchiveSection, QString::fromUtf8("📁") },
		{ &st::menuIconStoriesSave, QString::fromUtf8("🔽") },
		{ &st::menuIconStoriesSavedSection, QString::fromUtf8("⭐️") },
		{ &st::menuIconTableSubmenuDelete, QString::fromUtf8("🗑") },
		{ &st::menuIconTagAdd, QString::fromUtf8("➕") },
		{ &st::menuIconTagEdit, QString::fromUtf8("✏️") },
		{ &st::menuIconTagFilter, QString::fromUtf8("📑") },
		{ &st::menuIconTagRemoveAttention, QString::fromUtf8("❌") },
		{ &st::menuIconTagRename, QString::fromUtf8("✏️") },
		{ &st::menuIconToMainMenu, QString::fromUtf8("➡️") },
		{ &st::menuIconTradable, QString::fromUtf8("📈") },
		{ &st::menuIconTransparent, QString::fromUtf8("🔘") },
		{ &st::menuIconTTLAny, QString::fromUtf8("⏳") },
		{ &st::menuIconUnarchive, QString::fromUtf8("📁") },
		{ &st::menuIconUnblock, QString::fromUtf8("✅") },
		{ &st::menuIconUnfave, QString::fromUtf8("❌") },
		{ &st::menuIconUnique, QString::fromUtf8("💎") },
		{ &st::menuIconUniqueProfile, QString::fromUtf8("💎") },
		{ &st::menuIconUserHide, QString::fromUtf8("🫣") },
		{ &st::menuIconUserHideDisabled, QString::fromUtf8("👁") },
		{ &st::menuIconUserShow, QString::fromUtf8("👁") },
		{ &st::menuIconVideoChat, QString::fromUtf8("📹") },
		{ &st::menuIconViewReplies, QString::fromUtf8("💬") },
		{ &st::menuIconViolence, QString::fromUtf8("⚠️") },
		{ &st::menuIconWelcomeMessage, QString::fromUtf8("👋") },
		{ &st::menuIconWhenOnline, QString::fromUtf8("🔵") },
		{ &st::menuIconZoomIn, QString::fromUtf8("➕") },
		{ &st::menuIconZoomOut, QString::fromUtf8("🔽") },
	};
	return map;
}
} // namespace

bool HasRowIcon(const void *icon) {
	// Ждём набор только первые секунды после запуска: нет сети, значит рисуем старые.
	static const auto started = crl::now();
	return (RowIconMap().contains(icon) || icon == &OwnRowIcon())
		&& (crl::now() - started < 8000);
}

void PreloadRowIcons(not_null<Main::Session*> session) {
	PackIcons::Instance().ensure(session);
}

std::unique_ptr<Ui::Text::CustomEmoji> RowIconEmoji(
		const void *icon,
		Fn<void()> repaint,
		int size) {
	const auto &map = RowIconMap();
	const auto i = map.find(icon);
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (icon == &OwnRowIcon() && controller) {
		auto &pack = PackIcons::Instance();
		pack.ensure(&controller->session());
		return pack.makeOwn(std::move(repaint), size);
	}
	if (i == map.end() || !controller) {
		return nullptr;
	}
	auto &pack = PackIcons::Instance();
	pack.ensure(&controller->session());
	return pack.makeAlt(i->second, std::move(repaint), size);
}

std::unique_ptr<Ui::Text::CustomEmoji> AltIconEmoji(
		const QString &alt,
		Fn<void()> repaint,
		int size) {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return nullptr;
	}
	auto &pack = PackIcons::Instance();
	pack.ensure(&controller->session());
	return pack.makeAlt(alt, std::move(repaint), size);
}

rpl::producer<> RowIconsUpdated() {
	return PackIcons::Instance().updated();
}

bool PaintPackIcon(
		QPainter &p,
		const QRect &rect,
		const QString &alt,
		std::shared_ptr<void> &state,
		not_null<Ui::RpWidget*> widget) {
	if (!state) {
		state = std::make_shared<PackIcon>();
	}
	const auto pack = static_cast<PackIcon*>(state.get());
	PaintPlate(p, rect, QColor(), st::menuIconSettings, QString(), alt, *pack, widget, false);
	return pack->emoji || !pack->normalized.isNull();
}

void ShowSupportBox(Window::SessionController *controller) {
	if (!controller) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		RoundedNoButtons(box);
		box->setWidth(380);
		PackIcons::Instance().ensure(&controller->session());
		const auto content = box->verticalLayout();

		constexpr auto kHead = 64;
		const auto head = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(0, 14, 0, 0));
		head->resize(head->width(), kHead);
		constexpr auto kHeart = 64;
		const auto heart = std::make_shared<
			std::unique_ptr<Ui::Text::CustomEmoji>>();
		const auto makeHeart = [=] {
			if (!*heart) {
				*heart = PackIcons::Instance().makeOwnHeart(
					[=] { head->update(); },
					kHeart);
			}
		};
		PackIcons::Instance().updated(
		) | rpl::on_next([=] {
			makeHeart();
			head->update();
		}, head->lifetime());
		head->paintOn([=](QPainter &p) {
			makeHeart();
			if (*heart && (*heart)->ready()) {
				(*heart)->paint(p, {
					.textColor = QColor(0xFF, 0x3B, 0x5C),
					.now = crl::now(),
					.position = QPoint(
						(head->width() - kHeart) / 2,
						(head->height() - kHeart) / 2),
				});
			}
		});

		const auto title = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(U(Mokagram::Lang::Tr("Поддержать разработку"))),
				st::boxTitle),
			QMargins(22, 6, 22, 4),
			style::al_top);
		Q_UNUSED(title);
		const auto addItem = [&](
				const QString &alt,
				const QString &name,
				const QString &text) {
			const auto row = content->add(
				object_ptr<Ui::RpWidget>(content),
				QMargins(22, 16, 22, 0));
			const auto head = Ui::CreateChild<Ui::FlatLabel>(
				row,
				name,
				TitleLabelStyle());
			const auto body = Ui::CreateChild<Ui::FlatLabel>(
				row,
				text,
				TextLabelStyle());
			const auto state = std::make_shared<std::shared_ptr<void>>();
			constexpr auto kIcon = 26;
			constexpr auto kLeft = 42;
			row->widthValue() | rpl::on_next([=](int width) {
				const auto inner = std::max(width - kLeft, 1);
				head->resizeToWidth(inner);
				body->resizeToWidth(inner);
				head->moveToLeft(kLeft, 0);
				body->moveToLeft(kLeft, head->height() + 2);
				const auto height = head->height() + 2 + body->height();
				if (row->height() != height) {
					row->resize(width, height);
				}
			}, row->lifetime());
			row->paintOn([=](QPainter &p) {
				PaintPackIcon(p, QRect(0, 1, kIcon, kIcon), alt, *state, row);
			});
		};
		addItem(
			U("💰"),
			U(Mokagram::Lang::Tr("Выберите уровень")),
			U(Mokagram::Lang::Tr("На 1 уровне выдаётся простой бейджик, на 2 выбор из 4 бейджиков, на 3 полная кастомизация.")));
		addItem(
			U("📤"),
			U(Mokagram::Lang::Tr("Оплатите в боте")),
			U(Mokagram::Lang::Tr("Оплатите счёт в боте")));
		addItem(
			U("🎁"),
			U(Mokagram::Lang::Tr("Получите значок")),
			U(Mokagram::Lang::Tr("Он появится в вашем профиле.")));
		const auto close = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(22, 20, 22, 18));
		close->resize(close->width(), 44);
		close->setCursor(style::cur_pointer);
		close->paintOn([=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(close->rect(), 24, 24);
			p.setPen(st::windowFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				close->rect(),
				Qt::AlignCenter,
				U(Mokagram::Lang::Tr("Закрыть")));
		});
		close->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::MouseButtonRelease;
		}) | rpl::on_next([=] {
			box->closeBox();
		}, close->lifetime());
	}));
}

} // namespace Mokagram::Settings
