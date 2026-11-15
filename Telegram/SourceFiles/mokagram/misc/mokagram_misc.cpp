// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: e8fae5a2
// Mokagram: мелочи (лимит аккаунтов, стикер из фото, владелец пака, название клиента).
#include "mokagram/misc/mokagram_misc.h"
#include "mokagram/lang/mokagram_lang.h"
#include "lang/translate_external_provider.h"
#include "lang/lang_keys.h"
#include "mokagram/misc/mokagram_datereg.h"
#include "mokagram/misc/mokagram_ping.h"
#include "mokagram/stars/mokagram_stars_stats.h"

#include "api/api_stickers_creator.h"
#include "base/unixtime.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_text.h"
#include "ui/text/text_utilities.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "history/history.h"
#include "api/api_blocked_peers.h"
#include "api/api_user_privacy.h"
#include "apiwrap.h"
#include "base/flat_map.h"
#include "base/timer.h"
#include "main/main_domain.h"
#include "window/main_window.h"
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include "ui/power_saving.h"
#include "storage/cache/storage_cache_database.h"
#include "base/call_delayed.h"
#include <deque>
#include <map>
#include <set>
#include <QtGui/QGuiApplication>
#include "ui/unread_badge.h"
#include "core/application.h"
#include "base/flat_set.h"
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "mokagram/settings/mokagram_settings.h"
#include "mokagram/settings/mokagram_settings_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <vector>
#include <QtGui/QClipboard>


namespace Mokagram::Guard {
void RegisterSettings();
} // namespace Mokagram::Guard

namespace Mokagram::Misc {
namespace {

std::set<uint64> &SponsoredBlockedPeers() {
	static auto result = std::set<uint64>();
	return result;
}

void RegisterSponsoredEntry() {
	const auto blocked = int(SponsoredBlockedPeers().size());
	Settings::Register({
		.key = u"adblock.hideSponsored"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Реклама")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Скрывать спонсорские сообщения")),
		.description = Mokagram::Lang::Tr(
			"Не запрашиваем и не показываем рекламу в каналах. Скрыто за сессию: ")
			+ QString::number(blocked),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.category = u"chats"_q,
		.sectionIcon = u"block"_q,
	});
}

} // namespace


QString DebugFilePath(const QString &name) {
	const auto dir = QStandardPaths::writableLocation(
		QStandardPaths::RuntimeLocation);
	return (dir.isEmpty() || !QFileInfo(dir).isDir())
		? QString()
		: dir + '/' + name;
}
namespace {

#include "mokagram/misc/mokagram_app_icons.inc"

constexpr auto kAppIconKey = "look.app_icon";

rpl::lifetime AppIconLifetime;

constexpr auto kMaxAccountsKey = "misc.max_accounts";

// Приватный режим: пачка правил приватности одним тумблером и одним окном.
struct LockRow {
	Api::UserPrivacy::Key key;
	Api::UserPrivacy::Option target;
	const char *title;
	bool preset;
};

[[nodiscard]] const std::vector<LockRow> &LockRows() {
	using K = Api::UserPrivacy::Key;
	using O = Api::UserPrivacy::Option;
	static const auto rows = std::vector<LockRow>{
		{ K::PhoneNumber, O::Nobody, Mokagram::Lang::Tr("Скрыть номер телефона от всех"), true },
		{ K::AddedByPhone, O::Contacts, Mokagram::Lang::Tr("Не находить меня по номеру телефона"), true },
		{ K::LastSeen, O::Nobody, Mokagram::Lang::Tr("Скрыть время захода"), true },
		{ K::Invites, O::Nobody, Mokagram::Lang::Tr("Запретить добавлять меня в группы и каналы"), true },
		{ K::CallsPeer2Peer, O::Nobody, Mokagram::Lang::Tr("Звонки только через серверы Telegram (без p2p)"), true },
		{ K::Calls, O::Contacts, Mokagram::Lang::Tr("Звонить мне могут только контакты"), true },
		{ K::ProfilePhoto, O::Contacts, Mokagram::Lang::Tr("Фото профиля только контактам"), true },
		{ K::About, O::Contacts, Mokagram::Lang::Tr("Описание профиля только контактам"), true },
		{ K::Birthday, O::Contacts, Mokagram::Lang::Tr("День рождения только контактам"), true },
		{ K::Forwards, O::Nobody, Mokagram::Lang::Tr("Скрывать ссылку на меня при пересылке"), true },
		{ K::Voices, O::Contacts, Mokagram::Lang::Tr("Голосовые и кружки только от контактов"), false },
	};
	return rows;
}

constexpr auto kLockBackupKey = "privacy.lock_backup";

// Прежние значения хранятся отдельно для каждого аккаунта: {"<id аккаунта>": {"<правило>": опция}}.
[[nodiscard]] QString LockAccountKey(not_null<Main::Session*> session) {
	return QString::number(session->userId().bare);
}

[[nodiscard]] QJsonObject LockBackupRoot() {
	return QJsonDocument::fromJson(
		Settings::String(QString::fromLatin1(kLockBackupKey)).toUtf8()).object();
}

[[nodiscard]] QJsonObject LockBackup(not_null<Main::Session*> session) {
	return LockBackupRoot().value(LockAccountKey(session)).toObject();
}

void SaveLockBackup(
		not_null<Main::Session*> session,
		const QJsonObject &object) {
	auto root = LockBackupRoot();
	if (object.isEmpty()) {
		root.remove(LockAccountKey(session));
	} else {
		root.insert(LockAccountKey(session), object);
	}
	Settings::SetString(
		QString::fromLatin1(kLockBackupKey),
		root.isEmpty()
			? QString()
			: QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

// Применяет опцию к одному правилу: старое значение запоминается один раз,
// исключения («всегда» и «никогда») не трогаются.
void ApplyLock(
		not_null<Main::Session*> session,
		Api::UserPrivacy::Key key,
		Api::UserPrivacy::Option target,
		bool remember) {
	auto &privacy = session->api().userPrivacy();
	privacy.reload(key);
	privacy.value(key) | rpl::take(1) | rpl::on_next([=](Api::UserPrivacy::Rule rule) {
		if (remember) {
			auto backup = LockBackup(session);
			const auto id = QString::number(int(key));
			if (!backup.contains(id)) {
				backup.insert(id, int(rule.option));
				SaveLockBackup(session, backup);
			}
		}
		if (rule.option == target) {
			return;
		}
		rule.option = target;
		session->api().userPrivacy().save(key, rule);
	}, session->lifetime());
}

// Что закрывать: индексы строк через запятую, пусто значит набор по умолчанию.
constexpr auto kLockSelectionKey = "privacy.lock_selection";

[[nodiscard]] std::vector<bool> LockSelection() {
	const auto &rows = LockRows();
	auto result = std::vector<bool>(rows.size(), false);
	const auto saved = Settings::String(QString::fromLatin1(kLockSelectionKey));
	if (saved.isEmpty()) {
		for (auto i = 0; i != int(rows.size()); ++i) {
			result[i] = rows[i].preset;
		}
		return result;
	}
	for (const auto &part : saved.split(',', Qt::SkipEmptyParts)) {
		const auto index = part.toInt();
		if (index >= 0 && index < int(result.size())) {
			result[index] = true;
		}
	}
	return result;
}

void SaveLockSelection(const std::vector<bool> &selection) {
	auto parts = QStringList();
	for (auto i = 0; i != int(selection.size()); ++i) {
		if (selection[i]) {
			parts.push_back(QString::number(i));
		}
	}
	// Пустой набор хранится как «-1», чтобы не путать его с набором по умолчанию.
	Settings::SetString(
		QString::fromLatin1(kLockSelectionKey),
		parts.isEmpty() ? u"-1"_q : parts.join(','));
}

} // namespace

bool PrivacyLocked(not_null<Main::Session*> session) {
	return !LockBackup(session).isEmpty();
}

rpl::producer<bool> PrivacyLockedValue(not_null<Main::Session*> session) {
	return Settings::StringValue(QString::fromLatin1(kLockBackupKey))
		| rpl::map([=](const QString &) { return PrivacyLocked(session); })
		| rpl::distinct_until_changed();
}

// Закрывает выбранное (locked) или возвращает запомненные значения.
void SetPrivacyLocked(not_null<Main::Session*> session, bool locked) {
	if (locked) {
		const auto selection = LockSelection();
		for (auto i = 0; i != int(selection.size()); ++i) {
			if (selection[i]) {
				const auto &row = LockRows()[i];
				ApplyLock(session, row.key, row.target, true);
			}
		}
		return;
	}
	const auto backup = LockBackup(session);
	for (auto i = backup.begin(); i != backup.end(); ++i) {
		ApplyLock(
			session,
			Api::UserPrivacy::Key(i.key().toInt()),
			Api::UserPrivacy::Option(i.value().toInt()),
			false);
	}
	SaveLockBackup(session, QJsonObject());
}

void ShowPrivacyLockBox() {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return;
	}
	const auto controller = window->sessionController();
	if (!controller) {
		return;
	}
	const auto session = &controller->session();
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8(
			Mokagram::Lang::Tr("Приватный режим"))));
		box->setWidth(460);
		const auto content = box->verticalLayout();
		Ui::AddSkip(content);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				Mokagram::Lang::Tr(
					"Отметьте, что скрыть. Исключения «всегда разрешать» и «никогда не разрешать» не меняются. Прежние значения запомнятся, их можно вернуть."),
				st::boxLabel),
			QMargins(24, 0, 24, 8));
		auto checks = std::make_shared<std::vector<QPointer<Ui::Checkbox>>>();
		const auto selection = LockSelection();
		for (auto i = 0; i != int(LockRows().size()); ++i) {
			checks->push_back(content->add(
				object_ptr<Ui::Checkbox>(
					content,
					QString::fromUtf8(Mokagram::Lang::Tr(LockRows()[i].title)),
					selection[i],
					st::defaultBoxCheckbox),
				QMargins(24, 4, 24, 4)));
		}
		Ui::AddSkip(content);
		box->addButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Включить выбранное"))),
			[=] {
				auto count = 0;
				auto chosen = std::vector<bool>(LockRows().size(), false);
				for (auto i = 0; i != int(LockRows().size()); ++i) {
					const auto check = (*checks)[i];
					chosen[i] = check && check->checked();
					count += chosen[i] ? 1 : 0;
				}
				SaveLockSelection(chosen);
				SetPrivacyLocked(session, true);
				box->closeBox();
				if (count) {
					controller->showToast(QString::fromUtf8(
						Mokagram::Lang::Tr("Скрыто: ")) + QString::number(count));
				}
			});
		if (!LockBackup(session).isEmpty()) {
			box->addLeftButton(
				rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Вернуть как было"))),
				[=] {
					SetPrivacyLocked(session, false);
					box->closeBox();
					controller->showToast(QString::fromUtf8(
						Mokagram::Lang::Tr("Прежние значения возвращены")));
				});
		}
		box->addButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Отмена"))),
			[=] { box->closeBox(); });
	}));
}

QIcon CustomAppIcon() {
	const auto index = Settings::Int(QString::fromUtf8(kAppIconKey));
	if (index <= 0 || index > 6) {
		return QIcon();
	}
	auto pixmap = QPixmap();
	pixmap.loadFromData(
		kAppIcons[index - 1],
		uint(kAppIconSizes[index - 1]),
		"PNG");
	return pixmap.isNull() ? QIcon() : QIcon(pixmap);
}

QImage AppIconImage(int index) {
	if (index <= 0 || index > 6) {
		return Window::Logo();
	}
	auto image = QImage();
	image.loadFromData(
		kAppIcons[index - 1],
		uint(kAppIconSizes[index - 1]),
		"PNG");
	return image;
}

void ApplyAppIcon() {
	const auto icon = CustomAppIcon();
	if (icon.isNull()) {
		return;
	}
	QGuiApplication::setWindowIcon(icon);
	for (const auto widget : QApplication::topLevelWidgets()) {
		widget->setWindowIcon(icon);
	}
}

void AddBotButtonCopyActions(
		not_null<Ui::PopupMenu*> menu,
		const std::shared_ptr<ClickHandler> &link) {
	if (!Settings::Bool(u"bots.copyButtonData"_q)) {
		return;
	}
	const auto handler = std::dynamic_pointer_cast<ReplyMarkupClickHandler>(
		link);
	const auto button = handler ? handler->getButton() : nullptr;
	if (!button) {
		return;
	}
	using Type = HistoryMessageMarkupButton::Type;
	const auto add = [&](const char *title, QString text) {
		menu->addAction(
			QString::fromUtf8(Mokagram::Lang::Tr(title)),
			[text = std::move(text)] {
				QGuiApplication::clipboard()->setText(text);
			},
			&st::menuIconCopy);
	};
	const auto data = QString::fromUtf8(button->data);
	switch (button->type) {
	case Type::Callback:
	case Type::CallbackWithPassword:
		add(Mokagram::Lang::Tr("Копировать callback data"), data);
		break;
	case Type::WebView:
	case Type::SimpleWebView:
		add(Mokagram::Lang::Tr("Копировать url"), data);
		break;
	case Type::SwitchInline:
	case Type::SwitchInlineSame:
		add(Mokagram::Lang::Tr("Копировать inline query"), data);
		break;
	default:
		break;
	}
	add(Mokagram::Lang::Tr("Копировать текст кнопки"), button->text);
}

bool SponsoredHidden() {
	return Settings::Bool(u"adblock.hideSponsored"_q);
}

void NoteSponsoredBlocked(not_null<PeerData*> peer) {
	if (SponsoredBlockedPeers().insert(peer->id.value).second) {
		RegisterSponsoredEntry();
	}
}

QString FormatMessageTime(const QTime &time) {
	return Settings::Bool(u"time.showSeconds"_q)
		? QLocale().toString(time, u"HH:mm:ss"_q)
		: QLocale().toString(time, QLocale::ShortFormat);
}

void RegisterSettings() {
	RegisterSponsoredEntry();
	Settings::Register({
		.key = u"misc.welcome_shown"_q,
		.title = u"welcome"_q,
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.hidden = true,
	});
	Settings::Register({
		.key = QString::fromUtf8(kMaxAccountsKey),
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Аккаунты")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сколько аккаунтов можно добавить")),
		.description = QString::fromUtf8(
			Mokagram::Lang::Tr("Предел аккаунтов в одном клиенте. Если поставить больше 6, официальный Telegram не прочтёт эти данные.")),
		.kind = Settings::Kind::Int,
		.defaultValue = 10,
		.minValue = 3,
		.maxValue = 50,
		.category = u"accounts"_q,
		.sectionIcon = u"accounts"_q,
		.sectionNote = QString::fromUtf8(
			Mokagram::Lang::Tr("Переключаться между аккаунтами можно в меню слева вверху. Здесь настраивается только их предельное число.")),
	});
	Settings::Register({
		.key = u"chats.font"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Шрифт сообщений")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Каким шрифтом отправлять")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr(
			"Всё, что вы пишете, уходит выбранным начертанием: напишете «привет», уйдёт жирным.")),
		.kind = Settings::Kind::Choice,
		.defaultValue = 0,
		.choices = {
			QString::fromUtf8(Mokagram::Lang::Tr("Обычный")),
			QString::fromUtf8(Mokagram::Lang::Tr("Жирный")),
			QString::fromUtf8(Mokagram::Lang::Tr("Курсив")),
			QString::fromUtf8(Mokagram::Lang::Tr("Жирный курсив")),
			QString::fromUtf8(Mokagram::Lang::Tr("Подчёркнутый")),
			QString::fromUtf8(Mokagram::Lang::Tr("Зачёркнутый")),
			QString::fromUtf8(Mokagram::Lang::Tr("Моноширинный")),
			QString::fromUtf8(Mokagram::Lang::Tr("Скрытый (спойлер)")),
			QString::fromUtf8(Mokagram::Lang::Tr("Цитата")),
			QString::fromUtf8(Mokagram::Lang::Tr("𝐁𝐨𝐥𝐝 (жирный)")),
			QString::fromUtf8(Mokagram::Lang::Tr("𝓢𝓬𝓻𝓲𝓹𝓽 (рукописный)")),
			QString::fromUtf8(Mokagram::Lang::Tr("𝔊𝔬𝔱𝔥𝔦𝔠 (готический)")),
			QString::fromUtf8(Mokagram::Lang::Tr("Ⓒⓘⓡⓒⓛⓔⓢ (в кружках)")),
			QString::fromUtf8(Mokagram::Lang::Tr("Ｗｉｄｅ (широкий)")),
			QString::fromUtf8(Mokagram::Lang::Tr("ꜱᴍᴀʟʟ ᴄᴀᴘꜱ (малые прописные)")),
		},
		.category = u"chats"_q,
		.sectionIcon = u"edit"_q,
		.sectionNote = QString::fromUtf8(Mokagram::Lang::Tr(
			"Начертания с 𝐁𝐨𝐥𝐝 и ниже меняют только латиницу, кириллица остаётся обычной. Команды плагинов, ссылки, @имена и текст, где вы сами выделили жирный или курсив, не меняются.")),
	});
	const auto look = QString::fromUtf8(Mokagram::Lang::Tr("Размеры в чате"));
	const auto size = [&](const char *key, const char *title, int def, int lo, int hi) {
		Settings::Register({
			.key = QString::fromUtf8(key),
			.section = look,
			.title = QString::fromUtf8(title),
			.description = QString::fromUtf8(Mokagram::Lang::Tr("В пикселях. Применяется после перезапуска.")),
			.kind = Settings::Kind::Int,
			.defaultValue = def,
			.minValue = lo,
			.maxValue = hi,
			.requiresRestart = true,
			.category = u"look"_q,
			.sectionIcon = u"hide"_q,
		});
	};
	size("look.sticker_size", Mokagram::Lang::Tr("Размер стикеров"), 224, 80, 512);
	size("look.gif_size", Mokagram::Lang::Tr("Размер GIF"), 320, 120, 640);
	size("look.round_size", Mokagram::Lang::Tr("Размер кружков"), 240, 120, 480);
	Settings::Register({
		.key = u"misc.ping_dc"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Сеть")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Пинг дата-центров")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Показывает, за сколько отвечает каждый дата-центр Telegram.")),
		.kind = Settings::Kind::String,
		.defaultValue = QString::fromUtf8(Mokagram::Lang::Tr("Проверить")),
		.category = u"other"_q,
		.sectionIcon = u"storage"_q,
		.action = [] { ShowPingBox(); },
	});
	Settings::Register({
		.key = u"plugins.prefix"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Команды плагинов")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Префикс команд")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("С чего начинаются команды плагинов в поле ввода: например .help или !help. Один или два знака.")),
		.kind = Settings::Kind::String,
		.defaultValue = QString::fromUtf8("."),
		.category = u"plugins"_q,
	});
	Settings::Register({
		.key = u"plugins.autoupdate"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Команды плагинов")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Обновлять плагины из магазина")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Плагины из магазина обновляются сами раз в 12 часов, если новая версия не просит новых прав. Если просит, придёт подсказка открыть магазин.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = true,
		.category = u"plugins"_q,
	});
	Settings::Register({
		.key = u"plugins.dev_reload"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Команды плагинов")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Режим разработчика")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Плагин перезагружается сам, когда вы сохраняете его файл, без подтверждения прав. Включайте только для плагинов, которые пишете сами.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.category = u"plugins"_q,
	});
	Settings::Register({
		.key = u"misc.stars_stats"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Профиль")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Траты звёзд")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Сколько звёзд потрачено: по месяцам, категориям и получателям.")),
		.kind = Settings::Kind::String,
		.defaultValue = QString::fromUtf8(Mokagram::Lang::Tr("Открыть")),
		.category = u"other"_q,
		.sectionIcon = u"person"_q,
		.action = [] { Stars::ShowSpendingStatsBox(); },
	});
	Settings::Register({
		.key = u"bots.copyButtonData"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Боты")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Копирование данных кнопок")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Правый клик по кнопке бота: скопировать callback data, url, inline query или текст кнопки.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = true,
		.category = u"chats"_q,
		.sectionIcon = u"bot"_q,
	});
	Settings::Register({
		.key = u"regdate.show"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Дата регистрации")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Показывать дату регистрации")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Примерный месяц и год по айди аккаунта: в профиле и в информации о сообщении.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.category = u"other"_q,
		.sectionIcon = u"calendar"_q,
	});
	Settings::Register({
		.key = u"time.showSeconds"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Время и числа")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Секунды во времени сообщений")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Под сообщением время вида 12:34:56 вместо 12:34.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.requiresRestart = true,
		.category = u"chats"_q,
		.sectionIcon = u"chats"_q,
	});
	Settings::Register({
		.key = u"look.fullNumbers"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Время и числа")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Полные числа вместо сокращённых")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Просмотры, реакции и счётчики показываются как 12 457, а не 12.4K.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.requiresRestart = true,
		.category = u"look"_q,
		.sectionIcon = u"palette"_q,
	});
	Settings::Register({
		.key = u"privacy.relativeLastSeen"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Статус «в сети»")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Точное время вместо «был(а) недавно»")),
		.description = Mokagram::Lang::Tr("Если Telegram скрывает время, но мы видели человека в сети или за действием, пишем «был(а) 5 минут назад». Строка обновляется сама."),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.category = u"privacy"_q,
		.sectionIcon = u"online"_q,
	});
	Guard::RegisterSettings();
	Settings::Register({
		.key = u"documents.pdf"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Документы")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Встроенный просмотрщик PDF")),
		.kind = Settings::Kind::Bool,
		.defaultValue = true,
		.category = u"chats"_q,
		.hidden = true,
	});
	Settings::Register({
		.key = u"plugins.sources"_q,
		.kind = Settings::Kind::String,
		.defaultValue = QString(),
		.hidden = true,
	});
	const auto inlineEntry = [](Settings::Entry entry) {
		entry.section = QString::fromUtf8(Mokagram::Lang::Tr("Результаты в поле ввода"));
		entry.category = u"chats"_q;
		entry.sectionIcon = u"chats"_q;
		Settings::Register(std::move(entry));
	};
	inlineEntry({ .key = u"inline.enabled"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Результаты в поле ввода")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Нажмите на подсказку или Tab, чтобы вставить результат.")),
		.kind = Settings::Kind::Bool, .defaultValue = true });
	inlineEntry({ .key = u"inline.math"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Математические выражения")),
		.kind = Settings::Kind::Bool, .defaultValue = true,
		.dependsOn = u"inline.enabled"_q });
	inlineEntry({ .key = u"inline.degrees"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Углы в градусах")),
		.kind = Settings::Kind::Bool, .defaultValue = false,
		.dependsOn = u"inline.math"_q });
	inlineEntry({ .key = u"inline.units"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Перевод единиц измерения")),
		.description = u"10 km to mi · 32 F to C · 1024 KiB to MiB"_q,
		.kind = Settings::Kind::Bool, .defaultValue = true,
		.dependsOn = u"inline.enabled"_q });
	inlineEntry({ .key = u"inline.timezones"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Часовые пояса")),
		.description = u"14:30 Europe/London to Asia/Novosibirsk"_q,
		.kind = Settings::Kind::Bool, .defaultValue = true,
		.dependsOn = u"inline.enabled"_q });
	inlineEntry({ .key = u"inline.currency"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Курсы валют")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Загружает таблицу курсов Coinbase. Текст сообщения не передаётся. Пример: 100 USD to RUB.")),
		.kind = Settings::Kind::Bool, .defaultValue = false,
		.dependsOn = u"inline.enabled"_q });
	Settings::Register({
		.key = u"translation.provider"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Перевод сообщений")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сервис перевода")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Google, Microsoft и Яндекс получают текст для перевода. Форматирование сохраняется.")),
		.kind = Settings::Kind::Choice,
		.defaultValue = 0,
		.choices = { u"Telegram"_q, u"Google"_q, u"Microsoft"_q, QString::fromUtf8(Mokagram::Lang::Tr("Яндекс")) },
		.requiresRestart = true,
		.category = u"chats"_q,
		.hidden = true,
	});
	Settings::Register({
		.key = u"translation.microsoftCredentials"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Перевод сообщений")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Ключ Microsoft Translator")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Для Microsoft нужен ключ Azure Translator. Ключ хранится только в памяти.")),
		.kind = Settings::Kind::String,
		.defaultValue = QString(),
		.category = u"chats"_q,
		.hidden = true,
		.action = Ui::ConfigureMicrosoftTranslation,
	});
	Settings::Register({
		.key = u"translation.microsoftRegion"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Перевод сообщений")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Регион Microsoft Translator")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Регион ресурса Azure, если требуется.")),
		.kind = Settings::Kind::String,
		.defaultValue = QString(),
		.category = u"chats"_q,
		.hidden = true,
	});
	Settings::Register({
		.key = u"look.menu_side"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Боковое меню")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сторона меню")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr(
			"Откуда выезжает меню. Сверху и снизу это панель, она работает при закреплении.")),
		.kind = Settings::Kind::Choice,
		.defaultValue = 0,
		.choices = {
			QString::fromUtf8(Mokagram::Lang::Tr("Слева")),
			QString::fromUtf8(Mokagram::Lang::Tr("Справа")),
			QString::fromUtf8(Mokagram::Lang::Tr("Сверху")),
			QString::fromUtf8(Mokagram::Lang::Tr("Снизу")),
		},
		.category = u"look"_q,
		.sectionIcon = u"palette"_q,
	});
	// Какие значки показывать в закреплённой панели и в каком порядке
	// (имена через перевод строки). Правится окном по ПКМ на панели.
	Settings::Register({
		.key = u"look.bar_hidden"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Боковое меню")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Скрытые значки панели")),
		.kind = Settings::Kind::String,
		.defaultValue = QString(),
		.category = u"look"_q,
		.hidden = true,
	});
	Settings::Register({
		.key = u"look.bar_order"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Боковое меню")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Порядок значков панели")),
		.kind = Settings::Kind::String,
		.defaultValue = QString(),
		.category = u"look"_q,
		.hidden = true,
	});
	Settings::Register({
		.key = u"privacy.lock_all"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Приватный режим")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Приватный режим")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr(
			"Номер, время захода, приглашения в группы и звонки p2p меняются одним нажатием. Прежние значения можно вернуть.")),
		.kind = Settings::Kind::String,
		.defaultValue = QString::fromUtf8(Mokagram::Lang::Tr("Открыть")),
		.category = u"privacy"_q,
		.sectionIcon = u"lock"_q,
		.action = [] { ShowPrivacyLockBox(); },
	});
	Settings::Register({
		.key = QString::fromLatin1(kLockSelectionKey),
		.title = u"lock_selection"_q,
		.kind = Settings::Kind::String,
		.defaultValue = QString(),
		.hidden = true,
	});
	Settings::Register({
		.key = u"look.menu_pinned"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Боковое меню")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Закрепить меню")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr(
			"Меню остаётся на экране у выбранного края и не закрывается.")),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.category = u"look"_q,
		.sectionIcon = u"palette"_q,
	});
	Settings::Register({
		.key = QString::fromLatin1(kLockBackupKey),
		.title = u"lock_backup"_q,
		.kind = Settings::Kind::String,
		.defaultValue = QString(),
		.hidden = true,
	});
	Settings::Register({
		.key = QString::fromUtf8(kAppIconKey),
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Иконка приложения")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Иконка")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Значок mokaGram в панели задач и в окне. Применяется сразу.")),
		.kind = Settings::Kind::Choice,
		.defaultValue = 0,
		.choices = {
			QString::fromUtf8(Mokagram::Lang::Tr("Стандартная")),
			QString::fromUtf8(Mokagram::Lang::Tr("Классика")),
			QString::fromUtf8(Mokagram::Lang::Tr("Заливка")),
			QString::fromUtf8(Mokagram::Lang::Tr("Контур")),
			QString::fromUtf8(Mokagram::Lang::Tr("Чёрная")),
			QString::fromUtf8(Mokagram::Lang::Tr("Белая")),
			QString::fromUtf8(Mokagram::Lang::Tr("Стикер")),
		},
		.category = u"look"_q,
		.sectionIcon = u"palette"_q,
	});
	Settings::IntValue(QString::fromUtf8(kAppIconKey)) | rpl::on_next([] {
		ApplyAppIcon();
	}, AppIconLifetime);
	Settings::Register({
		.key = u"net.fast_download"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Загрузка файлов")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Быстрая загрузка")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Больше кусков файла запрашивается одновременно. Работает на новых загрузках.")),
		.kind = Settings::Kind::Choice,
		.defaultValue = 0,
		.choices = {
			QString::fromUtf8(Mokagram::Lang::Tr("Выключено")),
			QString::fromUtf8(Mokagram::Lang::Tr("Быстрая")),
			QString::fromUtf8(Mokagram::Lang::Tr("Максимум")),
		},
		.category = u"other"_q,
		.sectionIcon = u"download"_q,
	});
}

void CopyWithAuthor(not_null<HistoryItem*> item) {
	const auto when = base::unixtime::parse(item->date());
	const auto from = item->from();
	auto line = TextForMimeData();
	line.append(QString::fromUtf8("[")
		+ QLocale().toString(when, u"dd.MM.yyyy HH:mm"_q)
		+ QString::fromUtf8("] ")
		+ (from ? from->name() : QString())
		+ QString::fromUtf8(": "));
	line.append(HistoryItemText(item));
	TextUtilities::SetClipboardText(line);
}

uint64 OwnerIdFromStickerSetId(uint64 setId) {
	auto owner = setId >> 32;
	if (((setId >> 16) & 0xFF) == 0x3F) {
		owner |= 0x80000000ULL;
	}
	if ((setId >> 24) & 0xFF) {
		owner += 0x100000000ULL;
	}
	return owner;
}

void ShowStickerSetOwner(
		std::shared_ptr<ChatHelpers::Show> show,
		uint64 setId) {
	const auto owner = OwnerIdFromStickerSetId(setId);
	if (!owner || owner > 0xFFFFFFFFFFULL) {
		show->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Владельца определить не удалось.")));
		return;
	}
	const auto session = &show->session();
	const auto peerId = peerFromUser(UserId(owner));
	if (const auto peer = session->data().peerLoaded(peerId)) {
		if (const auto window = show->resolveWindow()) {
			window->showPeerInfo(peer);
			return;
		}
	}
	const auto idText = QString::number(owner);
	show->showBox(Ui::MakeConfirmBox({
		.text = QString::fromUtf8(
			Mokagram::Lang::Tr("Вероятный владелец пака: ID "))
			+ idText
			+ QString::fromUtf8(
				Mokagram::Lang::Tr(". Его профиля нет в кэше, открыть напрямую нельзя. Ссылка tg://user?id="))
			+ idText
			+ QString::fromUtf8(Mokagram::Lang::Tr(" может сработать, если вы с ним знакомы.")),
		.confirmed = [=](Fn<void()> close) {
			QGuiApplication::clipboard()->setText(idText);
			show->showToast(QString::fromUtf8(Mokagram::Lang::Tr("ID скопирован")));
			close();
		},
		.confirmText = QString::fromUtf8(Mokagram::Lang::Tr("Копировать ID")),
		.cancelText = QString::fromUtf8(Mokagram::Lang::Tr("Закрыть")),
	}));
}

QByteArray BrandRewrite(const QByteArray &value) {
	// Идемпотентно: «mokaGram» не содержит «Telegram». Названия
	// «Telegram Stars» и «Telegram Business» остаются как есть.
	static const auto from = QByteArrayLiteral("Telegram");
	static const auto to = QByteArrayLiteral("mokaGram");
	static const auto keep = std::vector<QByteArray>{
		QByteArrayLiteral(" stars"),
		QByteArrayLiteral(" business"),
		QByteArrayLiteral(" premium"),
		QString::fromUtf8(Mokagram::Lang::Tr(" бизнес")).toUtf8(),
		QString::fromUtf8(Mokagram::Lang::Tr(" для бизнес")).toUtf8(),
		QString::fromUtf8(Mokagram::Lang::Tr(" звёзд")).toUtf8(),
		QString::fromUtf8(Mokagram::Lang::Tr(" звезд")).toUtf8(),
	};
	if (!value.contains(from)) {
		return value;
	}
	// Ссылки не трогаем: t.me/TelegramTips не должен стать t.me/mokaGramTips.
	if (value.startsWith("http://")
		|| value.startsWith("https://")
		|| value.startsWith("tg://")) {
		return value;
	}
	auto result = QByteArray();
	result.reserve(value.size());
	auto i = 0;
	while (true) {
		const auto at = value.indexOf(from, i);
		if (at < 0) {
			result.append(value.mid(i));
			break;
		}
		result.append(value.mid(i, at - i));
		const auto after = at + from.size();
		auto protectedName = false;
		for (const auto &tail : keep) {
			const auto rest = value.mid(after, tail.size() + 4);
			if (QString::fromUtf8(rest).toLower().toUtf8().startsWith(tail)) {
				protectedName = true;
				break;
			}
		}
		result.append(protectedName ? from : to);
		i = after;
	}
	return result;
}


namespace {

struct DcCache {
	base::flat_map<uint64, int> values;
	bool loaded = false;
	bool saveQueued = false;
};

[[nodiscard]] DcCache &UserDcCache() {
	static auto result = DcCache();
	return result;
}

[[nodiscard]] QString DcCachePath() {
	return cWorkingDir() + u"tdata/moka_dc_cache.json"_q;
}

void LoadDcCache() {
	auto &cache = UserDcCache();
	if (cache.loaded) {
		return;
	}
	cache.loaded = true;
	auto file = QFile(DcCachePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = object.begin(); i != object.end(); ++i) {
		cache.values.emplace(i.key().toULongLong(), i.value().toInt());
	}
}

void SaveDcCache() {
	auto &cache = UserDcCache();
	if (cache.saveQueued) {
		return;
	}
	cache.saveQueued = true;
	// Пишем не на каждое изменение, а раз в несколько секунд.
	base::call_delayed(3000, qApp, [] {
		auto &cache = UserDcCache();
		cache.saveQueued = false;
		auto object = QJsonObject();
		for (const auto &[id, dc] : cache.values) {
			object.insert(QString::number(id), dc);
		}
		auto file = QFile(DcCachePath());
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		}
	});
}

} // namespace

void RememberUserDc(uint64 userId, int dcId) {
	if (!userId || dcId < 1 || dcId > 5) {
		return;
	}
	LoadDcCache();
	auto &values = UserDcCache().values;
	const auto i = values.find(userId);
	if (i != values.end() && i->second == dcId) {
		return;
	}
	values[userId] = dcId;
	SaveDcCache();
}

int RememberedUserDc(uint64 userId) {
	LoadDcCache();
	const auto &values = UserDcCache().values;
	const auto i = values.find(userId);
	return (i != values.end()) ? i->second : 0;
}

} // namespace Mokagram::Misc

namespace Mokagram::Bot {

void Ask(const QString &query, Fn<void(Results)> done, Fn<void()> fail) {
	constexpr auto kBotId = 8687512401LL;
	const auto failed = [fail] {
		if (fail) {
			fail();
		}
	};
	const auto account = Core::App().domain().maybeLastOrSomeAuthedAccount();
	const auto session = account ? account->maybeSession() : nullptr;
	if (!session) {
		failed();
		return;
	}
	const auto ask = [=](not_null<UserData*> bot) {
		session->api().request(MTPmessages_GetInlineBotResults(
			MTP_flags(0),
			bot->inputUser(),
			MTP_inputPeerSelf(),
			MTPInputGeoPoint(),
			MTP_string(query),
			MTP_string()
		)).done([=](const MTPmessages_BotResults &result) {
			auto out = Results();
			result.match([&](const MTPDmessages_botResults &data) {
				for (const auto &item : data.vresults().v) {
					item.match([&](const MTPDbotInlineResult &r) {
						out.push_back({
							qs(r.vid()),
							r.vtitle() ? qs(*r.vtitle()) : QString(),
							r.vdescription() ? qs(*r.vdescription()) : QString(),
						});
					}, [](const auto &) {});
				}
			});
			if (done) {
				done(std::move(out));
			}
		}).fail([=] {
			failed();
		}).send();
	};
	if (const auto bot = session->data().userLoaded(UserId(kBotId))) {
		ask(bot);
		return;
	}
	session->api().request(MTPcontacts_Search(
		MTP_flags(0),
		MTP_string(u"mokagent"_q),
		MTP_int(5)
	)).done([=](const MTPcontacts_Found &result) {
		result.match([&](const MTPDcontacts_found &data) {
			session->data().processUsers(data.vusers());
		});
		if (const auto bot = session->data().userLoaded(UserId(kBotId))) {
			ask(bot);
		} else {
			failed();
		}
	}).fail([=] {
		failed();
	}).send();
}

} // namespace Mokagram::Bot

namespace Mokagram::Badges {
namespace {

// Свой вид и подпись (уровни 2 и 3): значок и текст под профилем.
struct Custom {
	DocumentId icon = 0;
	QString text;
};

struct State {
	std::map<quint64, Custom> custom;
	base::flat_set<quint64> users;
	base::flat_set<quint64> remote;
	DocumentId icon = 5402618655601109041ULL;
	bool loaded = false;
	bool fresh = false; // список уже получен от бота в этом запуске
	bool inFlight = false;
};

// Список выданных галочек лежит в репозитории, ветка badges, файл badges.json;
// его обновляет бот командой /publish. Только https: plain HTTP с фиксированным
// IP позволял MITM в той же сети назначить «верификацию» любому аккаунту.
constexpr const char *kBadgesUrls[] = {
	"https://raw.githubusercontent.com/mokagram/mokagram/badges/badges.json",
};

[[nodiscard]] State &Data() {
	static auto result = State();
	return result;
}

void Load() {
	auto &state = Data();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	// По умолчанию галочка у владельца клиента; список можно подменить файлом
	// tdata/moka_badges.json: {"emoji_id": "...", "users": [id, ...]}.
	state.users.emplace(6612638154ULL);
	// Последний полученный список: значки видны сразу при запуске, до ответа бота.
	auto cache = QFile(cWorkingDir() + u"tdata/moka_badges_cache.json"_q);
	if (cache.open(QIODevice::ReadOnly)) {
		const auto object = QJsonDocument::fromJson(cache.readAll()).object();
		const auto icon = object.value(u"emoji_id"_q).toVariant().toString().toULongLong();
		if (icon) {
			state.icon = icon;
		}
		for (const auto &value : object.value(u"users"_q).toArray()) {
			if (const auto id = value.toVariant().toULongLong()) {
				state.remote.emplace(id);
			}
		}
		for (const auto &value : object.value(u"custom"_q).toArray()) {
			const auto row = value.toArray();
			if (row.size() == 3) {
				state.custom[row[0].toVariant().toULongLong()] = Custom{
					.icon = row[1].toVariant().toString().toULongLong(),
					.text = row[2].toString().left(40),
				};
			}
		}
	}
	auto file = QFile(cWorkingDir() + u"tdata/moka_badges.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		return;
	}
	const auto object = document.object();
	const auto icon = object.value(u"emoji_id"_q).toVariant().toString().toULongLong();
	if (icon) {
		state.icon = icon;
	}
	for (const auto &value : object.value(u"users"_q).toArray()) {
		if (const auto id = value.toVariant().toULongLong()) {
			state.users.emplace(id);
		}
	}
}

} // namespace

namespace {

// Команда клиента по юзернейму (в нижнем регистре) и подпись под профилем.
// Подпись на русском и на английском: язык берётся из языка интерфейса.
struct TeamEntry {
	const char *name;
	const char *ru;
	const char *en;
	bool blue;
};
constexpr TeamEntry kTeam[] = {
	{ "litteraliy", "Разработчик mokaGram", "Maintainer mokaGram", true },
	{ "xdshka", "Разработчик mokaGram", "Maintainer mokaGram", true },
};
constexpr auto kOwnerId = 6612638154ULL;

[[nodiscard]] bool Russian() {
	return ::Lang::Id().startsWith(u"ru"_q);
}

constexpr auto kBlueIcon = 5397879498492389616ULL;

[[nodiscard]] bool BlueIcon(not_null<UserData*> user) {
	if (peerToUser(user->id).bare == kOwnerId) {
		return true;
	}
	for (const auto &username : user->usernames()) {
		for (const auto &entry : kTeam) {
			if (entry.blue && (username.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0)) {
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] const char *TeamRole(not_null<UserData*> user) {
	if (peerToUser(user->id).bare == kOwnerId) {
		return Russian()
			? Mokagram::Lang::Tr("Разработчик и владелец mokaGram")
			: "Developer and owner of mokaGram";
	}
	for (const auto &username : user->usernames()) {
		for (const auto &entry : kTeam) {
			if (username.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0) {
				return Russian() ? entry.ru : entry.en;
			}
		}
	}
	return nullptr;
}

} // namespace

bool IsOwner(UserData *user) {
	return user && (peerToUser(user->id).bare == kOwnerId);
}

bool HasProfileVerification(UserData *user) {
	return user && TeamRole(user);
}

Ui::BotVerifyDetails Synthetic(not_null<UserData*> user) {
	Load();
	const auto &state = Data();
	const auto id = peerToUser(user->id).bare;
	const auto role = TeamRole(user);
	if (!role && !state.users.contains(id) && !state.remote.contains(id)) {
		return {};
	}
	const auto found = state.custom.find(id);
	const auto custom = (found != state.custom.end()) ? &found->second : nullptr;
	return Ui::BotVerifyDetails{
		.botId = UserId(0),
		.iconId = BlueIcon(user)
			? kBlueIcon
			: (custom && custom->icon ? custom->icon : state.icon),
		.description = TextWithEntities{
			(!role && custom && !custom->text.isEmpty())
				? custom->text
				: QString::fromUtf8(role ? role : (state.remote.contains(id) ? Mokagram::Lang::Tr("Поддерживает mokaGram") : Mokagram::Lang::Tr("Разработчик mokaGram"))),
		},
	};
}

void SaveCache() {
	const auto &state = Data();
	auto users = QJsonArray();
	for (const auto id : state.remote) {
		users.append(QString::number(id));
	}
	auto customArray = QJsonArray();
	for (const auto &[id, value] : state.custom) {
		customArray.append(QJsonArray{
			QString::number(id),
			QString::number(value.icon),
			value.text,
		});
	}
	auto file = QFile(cWorkingDir() + u"tdata/moka_badges_cache.json"_q);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"emoji_id"_q, QString::number(state.icon) },
			{ u"users"_q, users },
			{ u"custom"_q, customArray },
		}).toJson(QJsonDocument::Compact));
	}
}

// Список выданных галочек пришёл (из бота или из файла): обновляем значки у уже загруженных людей.
void ApplyRemote(
		const std::vector<quint64> &ids,
		quint64 icon,
		std::map<quint64, Custom> custom) {
	auto &state = Data();
	state.fresh = true;
	state.inFlight = false;
	const auto old = base::take(state.remote);
	auto changedIds = base::flat_set<quint64>();
	for (const auto &[id, value] : state.custom) {
		changedIds.emplace(id);
	}
	state.custom = std::move(custom);
	for (const auto &[id, value] : state.custom) {
		changedIds.emplace(id);
	}
	if (icon) {
		state.icon = icon;
	}
	for (const auto id : ids) {
		state.remote.emplace(id);
	}
	SaveCache();
	auto all = old;
	for (const auto id : state.remote) {
		all.emplace(id);
	}
	for (const auto id : changedIds) {
		all.emplace(id);
	}
	const auto account = Core::App().domain().maybeLastOrSomeAuthedAccount();
	if (const auto session = account ? account->maybeSession() : nullptr) {
		for (const auto id : all) {
			if (const auto user = session->data().userLoaded(UserId(id))) {
				user->setBotVerifyDetails(Synthetic(user));
			}
		}
	}
}

// Основной путь: inline-запросы «badges:N» к @mokagent, страницами по 44 человека.
struct PagesState {
	std::vector<quint64> ids;
	std::map<quint64, Custom> custom;
	quint64 icon = 0;
};

void RefreshPage(int page, std::shared_ptr<PagesState> state, Fn<void()> fail) {
	Mokagram::Bot::Ask(u"badges:%1"_q.arg(page), [=](Mokagram::Bot::Results results) {
		auto count = 0;
		for (const auto &result : results) {
			if (result.id == u"e"_q) {
				state->icon = result.title.toULongLong();
			} else if (result.id.startsWith('b')) {
				++count;
				const auto user = result.id.mid(1).toULongLong();
				if (!user) {
					continue;
				}
				state->ids.push_back(user);
				const auto emoji = result.title.toULongLong();
				const auto text = result.description.left(40);
				if (emoji || !text.isEmpty()) {
					state->custom[user] = Custom{ .icon = emoji, .text = text };
				}
			}
		}
		if (count >= 44 && page < 200) {
			RefreshPage(page + 1, state, fail);
			return;
		}
		ApplyRemote(state->ids, state->icon, std::move(state->custom));
	}, fail);
}

void RefreshInline(Fn<void()> fail) {
	RefreshPage(0, std::make_shared<PagesState>(), fail);
}

void RefreshApi(Fn<void()> fail);

void Refresh(int index = 0) {
	Load();
	if (index == 0) {
		auto &state = Data();
		if (state.inFlight) {
			return;
		}
		state.inFlight = true;
		// Запрос мог потеряться: через 15 с разрешаем новый.
		QTimer::singleShot(15000, [] { Data().inFlight = false; });
		RefreshApi([] {
			RefreshInline([] {
				Data().inFlight = false;
				Refresh(1);
			});
		});
		return;
	}
	index -= 1;
	if (index >= int(std::size(kBadgesUrls))) {
		return;
	}
	static auto manager = new QNetworkAccessManager();
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kBadgesUrls[index])));
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
	request.setTransferTimeout(8000);
	const auto reply = manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto document = (reply->error() == QNetworkReply::NoError)
			? QJsonDocument::fromJson(reply->readAll())
			: QJsonDocument();
		if (!document.isObject()) {
			// Сервер недоступен: пробуем следующий источник.
			Refresh(index + 2);
			return;
		}
		const auto object = document.object();
		auto ids = std::vector<quint64>();
		for (const auto &value : object.value(u"users"_q).toArray()) {
			if (const auto id = value.toVariant().toULongLong()) {
				ids.push_back(id);
			}
		}
		ApplyRemote(ids, object.value(u"emoji_id"_q).toVariant().toString().toULongLong(), Data().custom);
	});
}

constexpr auto kApiBadgesUrl = "https://api.moka.wtf/v1/badges";
constexpr auto kApiEventsUrl = "https://api.moka.wtf/v1/events";

[[nodiscard]] QNetworkAccessManager *Manager() {
	static auto result = new QNetworkAccessManager();
	return result;
}

// Главный путь: JSON с api.moka.wtf (см. https://api.moka.wtf/docs/).
void RefreshApi(Fn<void()> fail) {
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kApiBadgesUrl)));
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
	request.setTransferTimeout(8000);
	const auto reply = Manager()->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto document = (reply->error() == QNetworkReply::NoError)
			? QJsonDocument::fromJson(reply->readAll())
			: QJsonDocument();
		if (!document.isObject() || !document.object().value(u"badges"_q).isArray()) {
			if (fail) {
				fail();
			}
			return;
		}
		const auto object = document.object();
		auto ids = std::vector<quint64>();
		auto custom = std::map<quint64, Custom>();
		for (const auto &value : object.value(u"badges"_q).toArray()) {
			const auto row = value.toObject();
			const auto user = row.value(u"userId"_q).toVariant().toULongLong();
			if (!user) {
				continue;
			}
			ids.push_back(user);
			const auto emoji = row.value(u"emojiId"_q).toVariant().toString().toULongLong();
			const auto text = row.value(u"signature"_q).toString().left(64);
			if (emoji || !text.isEmpty()) {
				custom[user] = Custom{ .icon = emoji, .text = text };
			}
		}
		ApplyRemote(
			ids,
			object.value(u"defaultEmojiId"_q).toVariant().toString().toULongLong(),
			std::move(custom));
	});
}

// Одно изменение из потока событий: не перезагружаем весь список.
void ApplyOne(quint64 user, const QJsonValue &badge) {
	auto &state = Data();
	if (badge.isObject()) {
		const auto row = badge.toObject();
		state.remote.emplace(user);
		const auto emoji = row.value(u"emojiId"_q).toVariant().toString().toULongLong();
		const auto text = row.value(u"signature"_q).toString().left(64);
		if (emoji || !text.isEmpty()) {
			state.custom[user] = Custom{ .icon = emoji, .text = text };
		} else {
			state.custom.erase(user);
		}
	} else {
		state.remote.erase(user);
		state.custom.erase(user);
	}
	SaveCache();
	const auto account = Core::App().domain().maybeLastOrSomeAuthedAccount();
	if (const auto session = account ? account->maybeSession() : nullptr) {
		if (const auto loaded = session->data().userLoaded(UserId(user))) {
			loaded->setBotVerifyDetails(Synthetic(loaded));
		}
	}
}

// Постоянное соединение (SSE): галочки меняются у всех сразу после выдачи или снятия.
struct EventStream {
	QNetworkReply *reply = nullptr;
	QByteArray buffer;
	qint64 lastId = 0;
	int failures = 0;
	bool connected = false;
	qint64 lastDataMs = 0;
};

[[nodiscard]] EventStream &Events() {
	static auto result = EventStream();
	return result;
}

void HandleEvent(const QString &name, const QByteArray &data) {
	const auto object = QJsonDocument::fromJson(data).object();
	if (name == u"hello"_q) {
		if (object.value(u"resync"_q).toBool()) {
			Refresh(0);
		}
		return;
	}
	if (name != u"badge"_q) {
		return;
	}
	const auto user = object.value(u"userId"_q).toVariant().toULongLong();
	if (user) {
		ApplyOne(user, object.value(u"badge"_q));
	}
}

void ParseEvents() {
	auto &stream = Events();
	for (;;) {
		const auto end = stream.buffer.indexOf("\n\n");
		if (end < 0) {
			return;
		}
		const auto block = stream.buffer.left(end);
		stream.buffer.remove(0, end + 2);
		auto name = QString();
		auto data = QByteArray();
		for (const auto &line : block.split('\n')) {
			if (line.startsWith("event:")) {
				name = QString::fromUtf8(line.mid(6).trimmed());
			} else if (line.startsWith("data:")) {
				data += line.mid(5).trimmed();
			} else if (line.startsWith("id:")) {
				stream.lastId = std::max(stream.lastId, line.mid(3).trimmed().toLongLong());
			}
		}
		if (!name.isEmpty()) {
			HandleEvent(name, data);
		}
	}
}

void ConnectEvents() {
	auto &stream = Events();
	if (stream.reply) {
		return;
	}
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kApiEventsUrl)));
	request.setRawHeader("Accept", "text/event-stream");
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
	if (stream.lastId) {
		request.setRawHeader("Last-Event-ID", QByteArray::number(stream.lastId));
	}
	stream.buffer.clear();
	stream.lastDataMs = QDateTime::currentMSecsSinceEpoch();
	const auto reply = Manager()->get(request);
	stream.reply = reply;
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		auto &stream = Events();
		stream.connected = true;
		stream.failures = 0;
		stream.lastDataMs = QDateTime::currentMSecsSinceEpoch();
		stream.buffer += reply->readAll().replace("\r", "");
		ParseEvents();
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		auto &stream = Events();
		reply->deleteLater();
		stream.reply = nullptr;
		stream.connected = false;
		stream.failures = std::min(stream.failures + 1, 10);
		QTimer::singleShot(std::min(3000 * stream.failures, 30000), [] {
			ConnectEvents();
		});
	});
}

void Start() {
	// Сначала кеш с диска (значки видны сразу), потом запрос к боту, как только появится сессия.
	Load();
	static auto lifetime = rpl::lifetime();
	Core::App().domain().activeSessionValue(
	) | rpl::filter([](Main::Session *session) {
		return session != nullptr;
	}) | rpl::on_next([](Main::Session *) {
		Refresh(0);
	}, lifetime);
	// Вернулись в окно: свежий список сразу, не ждём минуту.
	QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [](Qt::ApplicationState state) {
		if (state == Qt::ApplicationActive) {
			Refresh(0);
		}
	});
	ConnectEvents();
	static auto timer = new QTimer();
	timer->setTimerType(Qt::CoarseTimer);
	timer->setInterval(3000);
	QObject::connect(timer, &QTimer::timeout, [] {
		static auto ticks = 0;
		auto &stream = Events();
		// Сервер шлёт ping раз в 20 с: тишина дольше минуты значит мёртвое соединение (сон, смена сети).
		if (stream.reply
			&& stream.connected
			&& QDateTime::currentMSecsSinceEpoch() - stream.lastDataMs > 60000) {
			stream.reply->abort();
		}
		ConnectEvents();
		// Пока список не получен, повторяем каждые 3 с; дальше раз в минуту,
		// а при живом потоке событий раз в 5 минут, только для подстраховки.
		const auto period = stream.connected ? 100 : 20;
		if (!Data().fresh || ++ticks >= period) {
			ticks = 0;
			Refresh(0);
		}
	});
	timer->start();
}

void Apply(not_null<UserData*> user) {
	auto details = Synthetic(user);
	if (details && !user->botVerifyDetails()) {
		user->setBotVerifyDetails(std::move(details));
	}
}

} // namespace Mokagram::Badges

namespace Mokagram::Guard {
namespace {

namespace S = ::Mokagram::Settings;

[[nodiscard]] QString SpamSection() {
	return QString::fromUtf8(Mokagram::Lang::Tr("Защита от спама"));
}
[[nodiscard]] QString LagSection() {
	return QString::fromUtf8(Mokagram::Lang::Tr("Анти-лаг"));
}

constexpr auto kDefaultThreats =
	"убью, прибью, найду тебя, найду и убью, сдохни, зарежу, взорву, "
	"выебу тебя, i will kill you, i'll kill you, kill yourself, kys";

struct StickerLog {
	std::deque<crl::time> times;
};

struct MassAddLog {
	std::deque<crl::time> times;
	base::flat_set<quint64> seen;
};

[[nodiscard]] bool IsStranger(not_null<UserData*> user) {
	return !user->isSelf()
		&& !user->isBot()
		&& !user->isContact()
		&& !user->isVerified()
		&& peerToUser(user->id).bare != 777000;
}

void Toast(const QString &text) {
	if (const auto window = Core::App().activePrimaryWindow()) {
		if (const auto controller = window->sessionController()) {
			controller->showToast(text);
		}
	}
}

void Block(not_null<UserData*> user, const QString &why) {
	user->session().api().blockedPeers().block(user);
	Toast(QString::fromUtf8(Mokagram::Lang::Tr("Заблокировал ")) + user->name() + QString::fromUtf8(": ") + why);
}

[[nodiscard]] QStringList ExemptList() {
	auto result = QStringList();
	for (auto part : S::String(u"guard.threat_exempt"_q).split(
			QRegularExpression(u"[,\\n;\\s]+"_q), Qt::SkipEmptyParts)) {
		result.push_back(part.trimmed().toLower().remove(u'@'));
	}
	return result;
}

[[nodiscard]] QStringList Threats() {
	auto result = QStringList();
	const auto raw = S::String(u"guard.threat_words"_q);
	for (auto part : raw.split(QRegularExpression(u"[,\\n;]"_q), Qt::SkipEmptyParts)) {
		part = part.trimmed().toLower();
		if (!part.isEmpty()) {
			result.push_back(part);
		}
	}
	return result;
}

void OnNewMessage(not_null<HistoryItem*> item) {
	if (item->out() || item->isService() || item->isEmpty()) {
		return;
	}
	const auto user = item->history()->peer->asUser();
	if (!user || user->isSelf() || user->isBot() || user->isVerified()) {
		return;
	}
	const auto strangersOnly = S::Bool(u"guard.only_strangers"_q);
	if (strangersOnly && !IsStranger(user)) {
		return;
	}
	if (S::Bool(u"guard.stickers"_q)) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		if (document && document->sticker()) {
			static auto logs = base::flat_map<quint64, StickerLog>();
			auto &log = logs[peerToUser(user->id).bare];
			const auto now = crl::now();
			const auto window = crl::time(S::Int(u"guard.sticker_seconds"_q)) * 1000;
			log.times.push_back(now);
			while (!log.times.empty() && now - log.times.front() > window) {
				log.times.pop_front();
			}
			if (int(log.times.size()) >= S::Int(u"guard.sticker_count"_q)) {
				logs.erase(peerToUser(user->id).bare);
				Block(user, QString::fromUtf8(Mokagram::Lang::Tr("спам стикерами")));
				return;
			}
		}
	}
	if (S::Bool(u"guard.threats"_q) && !ThreatExempt(user)) {
		const auto text = item->originalText().text.toLower();
		if (!text.isEmpty()) {
			for (const auto &word : Threats()) {
				if (text.contains(word)) {
					Block(user, QString::fromUtf8(Mokagram::Lang::Tr("угрозы")));
					return;
				}
			}
		}
	}
}

void WatchMassAdd(not_null<Main::Session*> session) {
	static const auto started = crl::now();
	static auto log = MassAddLog();
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::ChannelAmIn
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		const auto channel = update.peer->asChannel();
		if (!channel || !channel->amIn() || channel->amCreator()) {
			return;
		}
		const auto id = peerToChannel(channel->id).bare;
		if (!log.seen.emplace(id).second) {
			return;
		}
		// Первые минуты после запуска чаты просто подгружаются.
		if (crl::now() - started < 90 * 1000 || !S::Bool(u"guard.mass_add"_q)) {
			return;
		}
		const auto channelId = channel->id;
		base::call_delayed(5000, session, [=] {
			const auto strong = session->data().channelLoaded(peerToChannel(channelId));
			if (!strong || !strong->amIn() || strong->inviter.bare <= 0) {
				return;
			}
			const auto inviter = session->data().userLoaded(strong->inviter);
			if (!inviter || !IsStranger(inviter)) {
				return;
			}
			const auto now = crl::now();
			const auto window = crl::time(S::Int(u"guard.mass_minutes"_q)) * 60 * 1000;
			log.times.push_back(now);
			while (!log.times.empty() && now - log.times.front() > window) {
				log.times.pop_front();
			}
			if (S::Bool(u"guard.mass_leave"_q)) {
				session->api().leaveChannel(strong);
			}
			if (int(log.times.size()) >= S::Int(u"guard.mass_count"_q)) {
				log.times.clear();
				auto rule = Api::UserPrivacy::Rule();
				rule.option = Api::UserPrivacy::Option::Contacts;
				session->api().userPrivacy().save(
					Api::UserPrivacy::Key::Invites,
					rule);
				Toast(QString::fromUtf8(Mokagram::Lang::Tr("Много добавлений в чаты подряд: теперь добавлять вас могут только контакты")));
			}
		});
	}, session->lifetime());
}

} // namespace

bool ThreatExempt(not_null<PeerData*> peer) {
	const auto list = ExemptList();
	if (list.contains(QString::number(peerToUser(peer->id).bare))) {
		return true;
	}
	if (const auto user = peer->asUser()) {
		const auto name = user->username().toLower();
		return !name.isEmpty() && list.contains(name);
	}
	return false;
}

void ToggleThreatExempt(not_null<PeerData*> peer) {
	auto list = ExemptList();
	const auto id = QString::number(peerToUser(peer->id).bare);
	if (ThreatExempt(peer)) {
		list.removeAll(id);
		if (const auto user = peer->asUser()) {
			list.removeAll(user->username().toLower());
		}
	} else {
		list.push_back(id);
	}
	S::SetString(u"guard.threat_exempt"_q, list.join(u", "_q));
}

void RegisterSettings() {
	const auto add = [&](S::Entry entry) {
		entry.section = SpamSection();
		entry.category = u"privacy"_q;
		entry.sectionIcon = u"block"_q;
		entry.sectionNote = QString::fromUtf8(
			Mokagram::Lang::Tr("Работает в личных сообщениях. Заблокированных можно вернуть в настройках Telegram, раздел «Заблокированные»."));
		S::Register(std::move(entry));
	};
	add({
		.key = u"guard.only_strangers"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Только для тех, кого нет в контактах")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Знакомых и ботов защита не трогает.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
	});
	add({
		.key = u"guard.stickers"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Блокировать за спам стикерами")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Если человек присылает слишком много стикеров подряд.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	});
	add({
		.key = u"guard.sticker_count"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сколько стикеров ведёт к блокировке")),
		.kind = S::Kind::Int,
		.defaultValue = 5,
		.minValue = 2,
		.maxValue = 50,
		.dependsOn = u"guard.stickers"_q,
	});
	add({
		.key = u"guard.sticker_seconds"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("За сколько секунд")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Окно, в котором считаются стикеры.")),
		.kind = S::Kind::Int,
		.defaultValue = 10,
		.minValue = 1,
		.maxValue = 120,
		.dependsOn = u"guard.stickers"_q,
	});
	add({
		.key = u"guard.threats"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Блокировать за угрозы")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Если в сообщении есть слово или фраза из списка ниже.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	});
	add({
		.key = u"guard.threat_words"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Список угроз")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Через запятую, регистр не важен.")),
		.kind = S::Kind::String,
		.defaultValue = QString::fromUtf8(kDefaultThreats),
		.dependsOn = u"guard.threats"_q,
	});
	add({
		.key = u"guard.threat_exempt"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("На кого список угроз не действует")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Им можно писать что угодно. Добавляется в меню чата («Угрозы: не проверять»), здесь можно править: айди или @юзернеймы через запятую.")),
		.kind = S::Kind::String,
		.defaultValue = QString(),
		.dependsOn = u"guard.threats"_q,
	});
	add({
		.key = u"guard.mass_add"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Защита от массового добавления в чаты")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Если вас подряд добавляют в чаты незнакомые, добавлять смогут только контакты.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	});
	add({
		.key = u"guard.mass_count"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сколько добавлений считать массовыми")),
		.kind = S::Kind::Int,
		.defaultValue = 3,
		.minValue = 2,
		.maxValue = 20,
		.dependsOn = u"guard.mass_add"_q,
	});
	add({
		.key = u"guard.mass_minutes"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("За сколько минут")),
		.kind = S::Kind::Int,
		.defaultValue = 10,
		.minValue = 1,
		.maxValue = 180,
		.dependsOn = u"guard.mass_add"_q,
	});
	add({
		.key = u"guard.mass_leave"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сразу выходить из таких чатов")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Только там, куда вас добавил не контакт.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
		.dependsOn = u"guard.mass_add"_q,
	});

	const auto lag = [&](S::Entry entry) {
		entry.section = LagSection();
		entry.category = u"look"_q;
		entry.sectionIcon = u"hide"_q;
		entry.sectionNote = QString::fromUtf8(
			Mokagram::Lang::Tr("Клиент следит за подвисаниями интерфейса. Если их много, он на время отключает тяжёлые анимации, а когда всё спокойно, возвращает как было."));
		S::Register(std::move(entry));
	};
	lag({
		.key = u"lag.enabled"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Анти-лаг")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Автоматически включает экономный режим при подвисаниях.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
	});
	lag({
		.key = u"lag.idle"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Экономия в фоне")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Когда окно не в фокусе, анимации, эмодзи и эффекты приостанавливаются. Сообщения, плагины и уведомления работают как обычно.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
	});
	lag({
		.key = u"lag.fps"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Счётчик кадров")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Плавающий оверлей с текущим fps и 1% low за последние 5 секунд. Нужен для замеров.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	});
	lag({
		.key = u"lag.emoji_cap"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сколько эмодзи играет одновременно")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Остальные премиум-эмодзи замирают на кадре. 0 значит без ограничения.")),
		.kind = S::Kind::Int,
		.defaultValue = 12,
		.minValue = 0,
		.maxValue = 200,
	});
	lag({
		.key = u"lag.sticker_cap"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сколько стикеров играет одновременно")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Остальные анимированные стикеры замирают на кадре. 0 значит без ограничения.")),
		.kind = S::Kind::Int,
		.defaultValue = 3,
		.minValue = 0,
		.maxValue = 50,
	});
	S::Register({
		.key = u"lag.autostart"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Запуск и фон")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Запускать вместе с системой")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Нужно, чтобы расписания и команды плагинов работали сразу после включения компьютера.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
		.category = u"look"_q,
		.sectionIcon = u"storage"_q,
	});
	S::Register({
		.key = u"lag.background"_q,
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Запуск и фон")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Крестик убирает окно в фон")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Клиент продолжает работать со свёрнутым окном: плагины, расписания и уведомления не останавливаются. Если выключить, крестик закрывает клиент.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
		.category = u"look"_q,
		.sectionIcon = u"storage"_q,
	});
	lag({
		.key = u"lag.toaster"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Режим тостера")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Максимальная экономия всегда: без анимаций и эффектов. Для слабых компьютеров и ноутбуков на батарее.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	});
	lag({
		.key = u"lag.level"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Что отключать при лагах")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Мягко: фон чата, эффекты, спойлеры. Средне: ещё стикеры и эмодзи. Жёстко: всё.")),
		.kind = S::Kind::Choice,
		.defaultValue = 1,
		.choices = {
			QString::fromUtf8(Mokagram::Lang::Tr("Мягко")),
			QString::fromUtf8(Mokagram::Lang::Tr("Средне")),
			QString::fromUtf8(Mokagram::Lang::Tr("Жёстко")),
		},
		.dependsOn = u"lag.enabled"_q,
	});
	lag({
		.key = u"lag.sensitivity"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Сколько подвисаний за 10 секунд считать лагом")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Меньше число: реагирует раньше.")),
		.kind = S::Kind::Int,
		.defaultValue = 8,
		.minValue = 3,
		.maxValue = 40,
		.dependsOn = u"lag.enabled"_q,
	});
	lag({
		.key = u"lag.calm"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Через сколько секунд тишины вернуть всё")),
		.kind = S::Kind::Int,
		.defaultValue = 45,
		.minValue = 10,
		.maxValue = 600,
		.dependsOn = u"lag.enabled"_q,
	});
	lag({
		.key = u"lag.cache"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Чистить кэш при лагах")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Только если он больше предела ниже. Не чаще раза в полчаса.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
		.dependsOn = u"lag.enabled"_q,
	});
	lag({
		.key = u"lag.cache_mb"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Предел кэша, МБ")),
		.kind = S::Kind::Int,
		.defaultValue = 2048,
		.minValue = 256,
		.maxValue = 20480,
		.dependsOn = u"lag.cache"_q,
	});
	lag({
		.key = u"lag.notify"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Говорить, когда режим включается")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
		.dependsOn = u"lag.enabled"_q,
	});
}

namespace {

struct LagState {
	crl::time last = 0;
	crl::time lastStall = 0;
	std::deque<crl::time> stalls;
	bool active = false;
	PowerSaving::Flags saved;
	crl::time lastCacheClear = 0;
};

void LagTick(LagState &state) {
	const auto now = crl::now();
	const auto delta = (state.last > 0) ? (now - state.last - 200) : 0;
	state.last = now;
	if (!S::Bool(u"lag.enabled"_q)) {
		if (state.active) {
			PowerSaving::Set(state.saved);
			state.active = false;
		}
		state.stalls.clear();
		return;
	}
	// Сон, свёрнутое окно и скачки времени подвисаниями не считаем.
	const auto counted = QGuiApplication::applicationState() == Qt::ApplicationActive
		&& delta > 120 && delta < 3000;
	if (counted) {
		state.lastStall = now;
		state.stalls.push_back(now);
	}
	while (!state.stalls.empty() && now - state.stalls.front() > 10000) {
		state.stalls.pop_front();
	}
	if (!state.active && int(state.stalls.size()) >= S::Int(u"lag.sensitivity"_q)) {
		const auto level = S::Int(u"lag.level"_q);
		auto flags = PowerSaving::Flags(0);
		using F = PowerSaving::Flag;
		flags |= F::kChatBackground | F::kChatSpoiler | F::kChatEffects | F::kEmojiStatus;
		if (level >= 1) {
			flags |= F::kStickersChat | F::kEmojiChat | F::kEmojiReactions
				| F::kStickersPanel | F::kEmojiPanel;
		}
		if (level >= 2) {
			flags = PowerSaving::Flags(PowerSaving::kAll);
		}
		state.saved = PowerSaving::Current();
		state.active = true;
		PowerSaving::Set(state.saved | flags);
		if (S::Bool(u"lag.notify"_q)) {
			Toast(QString::fromUtf8(Mokagram::Lang::Tr("Анти-лаг: включил экономный режим")));
		}
		// Mokagram: disk cache eviction adds downloads and I/O after a UI stall.
#ifndef Q_OS_MAC
		if (S::Bool(u"lag.cache"_q)
			&& (now - state.lastCacheClear > 30 * 60 * 1000)) {
			if (const auto window = Core::App().activePrimaryWindow()) {
				if (const auto controller = window->sessionController()) {
					const auto session = &controller->session();
					const auto limit = int64(S::Int(u"lag.cache_mb"_q)) * 1024 * 1024;
					rpl::combine(
						session->data().cache().statsOnMain(),
						session->data().cacheBigFile().statsOnMain()
					) | rpl::take(1) | rpl::on_next([=](
							Storage::Cache::Database::Stats &&a,
							Storage::Cache::Database::Stats &&b) {
						if (a.full.totalSize + b.full.totalSize > limit) {
							session->data().cacheBigFile().clear();
							session->data().cache().clear();
						}
					}, session->lifetime());
					state.lastCacheClear = now;
				}
			}
		}
#endif // Q_OS_MAC
	} else if (state.active
		&& now - state.lastStall > crl::time(S::Int(u"lag.calm"_q)) * 1000) {
		PowerSaving::Set(state.saved);
		state.active = false;
		state.stalls.clear();
	}
}

} // namespace

void Start() {
	// Лаг-монитор: опоздание таймера на главном потоке = подвисание интерфейса.
	static auto state = LagState();
	static auto timer = new QTimer();
	// Грубый таймер: Qt склеивает пробуждения, процессор спит дольше.
	timer->setTimerType(Qt::CoarseTimer);
	timer->setInterval(200);
	QObject::connect(timer, &QTimer::timeout, [] { LagTick(state); });
	timer->start();
	// Пока окно в фоне, подвисания никто не видит: таймер спит и не будит процессор.
	QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [](
			Qt::ApplicationState application) {
		if (application == Qt::ApplicationActive) {
			if (!timer->isActive()) {
				// После сна не считаем паузу подвисанием.
				state.last = 0;
				timer->start();
			}
		} else {
			timer->stop();
		}
	});

	static auto lifetime = rpl::lifetime();
	Core::App().domain().activeSessionValue(
	) | rpl::filter([](Main::Session *session) {
		return session != nullptr;
	}) | rpl::on_next([](Main::Session *session) {
		session->changes().realtimeMessageUpdates(
			Data::MessageUpdate::Flag::NewAdded
		) | rpl::on_next([](const Data::MessageUpdate &update) {
			OnNewMessage(update.item);
		}, session->lifetime());
		WatchMassAdd(session);
		::Mokagram::Settings::PreloadRowIcons(session);
	}, lifetime);
}

} // namespace Mokagram::Guard
