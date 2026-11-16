// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: c562bb5c
#include "mokagram/scam/mokagram_scam.h"
#include "mokagram/lang/mokagram_lang.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "data/data_peer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_reply_markup.h"
#include "mokagram/settings/mokagram_settings.h"
#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/layers/generic_box.h"
#include "window/window_controller.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

namespace Mokagram::Scam {
namespace {

namespace S = Mokagram::Settings;

constexpr auto kTelegramServiceId = 777000;
constexpr auto kMarkChar = QChar(0x2063);

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] bool ContainsAny(
		const QString &text,
		std::initializer_list<const char*> needles) {
	for (const auto needle : needles) {
		if (text.contains(U(needle), Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

} // namespace

void RegisterSettings() {
	S::Register({
		.key = u"scam.enabled"_q,
		.section = U(Mokagram::Lang::Tr("Защита от скама")),
		.title = U(Mokagram::Lang::Tr("Предупреждать о скаме")),
		.description = U(Mokagram::Lang::Tr("Помечает фейковые офферы, покупателей юзернеймов, гарант-боты и ссылки, спрятанные за текстом (псевдо-Fragment, мини-аппы). Настоящий оффер выглядит как карточка подарка по центру чата.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
		.category = u"privacy"_q,
		.sectionIcon = u"block"_q,
	});
}

namespace {

bool BypassLinkGuard = false;

[[nodiscard]] bool OfferText(const QString &text) {
	return ContainsAny(text, {
		"предложение действует", "offer is valid",
	}) || (ContainsAny(text, { "предлагает вам", "offers you" })
		&& ContainsAny(text, {
			"за подарок", "за коллекционный", "for your gift",
			"for your collectible", " gram",
		}));
}

// «Do you sell @name»: покупатель юзернейма или подарка, который потом
// пришлёт ссылку на поддельный Fragment.
[[nodiscard]] bool BuyerBait(const QString &text) {
	static const auto phrase = QRegularExpression(
		u"(do you (still )?sell|are you (still )?selling|selling your|"
		"want to buy your|wanna buy your|interested in (buying|your)|"
		"buy your|продаёшь|продаешь|продаете|продаёте|куплю (твой|ваш)|"
		"выкуплю|интересует (твой|ваш)|"
		"(willing|open|ready|happy) (to|for) sell|"
		"sell (your |the |this )?user ?name|"
		"what (amount|price) would you (accept|take)|"
		"how much (do you want|would you (take|accept))|"
		"(offer|price) for your|your user ?name|"
		"готов продать|готовы продать|сколько хочешь за|за сколько продашь)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto subject = QRegularExpression(
		u"(@\\w{4,}|user ?name|nft|gift|юзернейм|юзер|подарок|нфт)"_q,
		QRegularExpression::CaseInsensitiveOption);
	// Большинство сообщений не содержит объекта сделки: проверяем его первым.
	return subject.match(text).hasMatch() && phrase.match(text).hasMatch();
}

// Гарант-боты: настоящие площадки не торгуют через гарантов в Telegram.
[[nodiscard]] bool GarantMention(const QString &text) {
	// Каждая ветка выражения содержит одно из этих слов.
	if (!ContainsAny(text, { "garant", "гарант", "escrow", "guarant" })) {
		return false;
	}
	static const auto re = QRegularExpression(
		u"(@\\w*(garant|гарант|escrow|guarant)\\w*|"
		"t\\.me/\\w*(garant|escrow|guarant)\\w*|"
		"(playerok|funpay|plati|fanpay|player0k)\\W{0,3}(гарант|garant|escrow))"_q,
		QRegularExpression::CaseInsensitiveOption);
	return re.match(text).hasMatch();
}

// «Профиль заблокирован, пройдите верификацию»: пугалка с кнопкой на
// фишинговый мини-апп, обычно через inline-бота.
[[nodiscard]] bool AlarmText(const QString &text) {
	static const auto re = QRegularExpression(
		u"(profile (has been|is|was) (blocked|restricted|frozen|suspended|limited)|"
		"account (has been|is|was) (blocked|restricted|frozen|suspended|limited)|"
		"unlock (your )?(profile|account|wallet)|"
		"(profile|account|wallet|identity) verification|"
		"verif(y|ication) (to|in order to) (confirm|unlock|restore|continue)|"
		"account will be (deleted|removed|closed|banned)|"
		"assets? will be (burned|lost|frozen|deleted)|"
		"deposit error|pending limits|"
		"(профиль|аккаунт) (был |была |будет )?(заблокирован|ограничен|заморожен|удал)|"
		"разблокир\\w+ (профиль|аккаунт|кошел)|"
		"пройдите верификацию|подтвердите (личность|профиль)|"
		"активы будут (сожжены|утеряны|заморожены))"_q,
		QRegularExpression::CaseInsensitiveOption);
	// Одной фразы мало: в обычной переписке тоже бывает «верификация».
	auto count = 0;
	auto i = re.globalMatch(text);
	while (i.hasNext()) {
		i.next();
		if (++count >= 2) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString HostOf(const QString &url) {
	auto host = QUrl::fromUserInput(url).host().toLower();
	if (host.startsWith(u"www."_q)) {
		host = host.mid(4);
	}
	return host;
}

[[nodiscard]] bool IsTelegramHost(const QString &host) {
	return host == u"t.me"_q
		|| host == u"telegram.me"_q
		|| host == u"telegram.dog"_q;
}

[[nodiscard]] bool IsMiniAppLink(const QString &url) {
	if (url.startsWith(u"tg://"_q, Qt::CaseInsensitive)) {
		return url.contains(u"startapp"_q, Qt::CaseInsensitive)
			|| url.contains(u"appname"_q, Qt::CaseInsensitive)
			|| url.contains(u"startattach"_q, Qt::CaseInsensitive);
	}
	const auto parsed = QUrl::fromUserInput(url);
	if (!IsTelegramHost(parsed.host().toLower())) {
		return false;
	}
	const auto path = parsed.path().split('/', Qt::SkipEmptyParts);
	return (path.size() >= 2 && path[0] != u"c"_q && path[0] != u"s"_q
		&& path[0] != u"addstickers"_q && path[0] != u"addemoji"_q
		&& path[0] != u"joinchat"_q && path[0] != u"nft"_q
		&& path[0] != u"m"_q && path[0] != u"addlist"_q
		&& !path[1].at(0).isDigit())
		|| parsed.query().contains(u"startapp"_q, Qt::CaseInsensitive);
}

// Причина, почему ссылка с таким текстом подозрительна. Пусто, если всё ок.
[[nodiscard]] QString LinkReason(const QString &shown, const QString &url) {
	const auto host = HostOf(url);
	const auto text = shown.trimmed();
	// Текст ссылки выдаёт себя за другой сайт.
	static const auto looksLikeUrl = QRegularExpression(
		u"^(https?://)?([\\w-]+\\.)+[a-z]{2,}(/\\S*)?$"_q,
		QRegularExpression::CaseInsensitiveOption);
	if (!text.isEmpty() && looksLikeUrl.match(text).hasMatch()) {
		const auto shownHost = HostOf(text);
		if (!shownHost.isEmpty()
			&& host != shownHost
			&& !host.endsWith(u"."_q + shownHost)
			&& !shownHost.endsWith(u"."_q + host)) {
			return U(Mokagram::Lang::Tr("Текст ссылки показывает ")) + shownHost
				+ U(Mokagram::Lang::Tr(", а ведёт она на "))
				+ (host.isEmpty() ? url : host) + U(".");
		}
	}
	if (ContainsAny(text, { "fragment", "getgems", "portals", "tonkeeper",
			"playerok", "funpay" })
		&& !text.isEmpty()
		&& !(host.endsWith(u"fragment.com"_q)
			|| host.endsWith(u"getgems.io"_q)
			|| host.endsWith(u"tonkeeper.com"_q)
			|| host.endsWith(u"playerok.com"_q)
			|| host.endsWith(u"funpay.com"_q))) {
		return U(Mokagram::Lang::Tr("Ссылка выдаёт себя за известный сервис, но ведёт на "))
			+ (host.isEmpty() ? url : host) + U(".");
	}
	if (IsMiniAppLink(url) && !text.isEmpty() && text != url) {
		return U(Mokagram::Lang::Tr("Ссылка спрятана за текстом и открывает мини-апп. Так фальшивые «Fragment» и «гаранты» просят номер телефона."));
	}
	if (GarantMention(url)) {
		return U(Mokagram::Lang::Tr("Это гарант-бот. Настоящие площадки не проводят сделки через гарантов в Telegram, такие боты забирают деньги и подарки."));
	}
	return QString();
}

[[nodiscard]] bool HasBadLink(const TextWithEntities &text) {
	for (const auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomUrl) {
			continue;
		}
		const auto shown = text.text.mid(entity.offset(), entity.length());
		if (!LinkReason(shown, entity.data()).isEmpty()) {
			return true;
		}
	}
	return false;
}

} // namespace

void MarkText(
		not_null<const HistoryItem*> item,
		TextWithEntities &text) {
	const auto testing = qEnvironmentVariableIsSet("MOKAGRAM_SCAM_TEST");
	if (!S::Bool(u"scam.enabled"_q)
		|| text.text.isEmpty()
		|| item->isService()
		|| (item->out() && !testing)
		|| item->isPost()) {
		return;
	}
	const auto from = item->from();
	if (from->isVerified()
		|| from->id.value == kTelegramServiceId
		|| (from->isUser() && from->asUser()->isBot())) {
		return;
	}
	const auto user = from->asUser();
	const auto stranger = testing || !user || !user->isContact();
	const auto suspicious = OfferText(text.text)
		|| AlarmText(text.text)
		|| HasBadLink(text)
		|| (stranger && (BuyerBait(text.text) || GarantMention(text.text)));
	if (!suspicious) {
		return;
	}
	text = TextWithEntities{ QString(kMarkChar) + u"\n"_q }.append(std::move(text));
}

bool GuardLink(
		const QString &url,
		const QVariant &context,
		Fn<void()> again) {
	if (!S::Bool(u"scam.enabled"_q) || BypassLinkGuard) {
		return false;
	}
	auto shown = QString();
	const auto my = context.value<ClickHandlerContext>();
	if (const auto window = my.sessionWindow.get()) {
		if (const auto item = window->session().data().message(my.itemId)) {
			const auto from = item->from();
			if (item->out()
				|| from->isVerified()
				|| (from->isUser() && from->asUser()->isBot())) {
				return false;
			}
			const auto &text = item->originalText();
			const auto offset = HasMark(text.text) ? 2 : 0;
			Q_UNUSED(offset);
			for (const auto &entity : text.entities) {
				if (entity.type() == EntityType::CustomUrl
					&& entity.data() == url) {
					shown = text.text.mid(entity.offset(), entity.length());
					break;
				}
			}
		}
	}
	const auto reason = LinkReason(shown, url);
	if (reason.isEmpty()) {
		return false;
	}
	const auto window = Core::App().activeWindow();
	if (!window) {
		return false;
	}
	window->show(Ui::MakeConfirmBox({
		.text = TextWithEntities{ reason + U(Mokagram::Lang::Tr("\n\nНе вводи номер телефона и не подтверждай ничего на открывшейся странице. Сделки с юзернеймами и подарками идут через Fragment, адрес которого ты вводишь сам.")) },
		.confirmed = [=](Fn<void()> close) {
			close();
			BypassLinkGuard = true;
			again();
			BypassLinkGuard = false;
		},
		.confirmText = U(Mokagram::Lang::Tr("Всё равно открыть")),
		.cancelText = U(Mokagram::Lang::Tr("Не открывать")),
		.title = rpl::single(U(Mokagram::Lang::Tr("Подозрительная ссылка"))),
	}));
	return true;
}

bool IsFakeOffer(not_null<const HistoryItem*> item) {
	if (!S::Bool(u"scam.enabled"_q)
		|| item->isService()
		|| item->out()
		|| item->isPost()) {
		return false;
	}
	const auto from = item->from();
	if (from->isVerified() || from->id.value == kTelegramServiceId) {
		return false;
	}
	const auto text = item->originalText().text;
	// Живой оффер Telegram показывает служебная плашка. Обычное сообщение
	// с такой же фразой и таймером это подделка.
	const auto offerText = ContainsAny(text, {
		"предложение действует", "offer is valid",
	}) || (ContainsAny(text, { "предлагает вам", "offers you" })
		&& ContainsAny(text, {
			"за подарок", "за коллекционный", "for your gift",
			"for your collectible", " gram",
		}));
	if (offerText) {
		return true;
	}
	const auto markup = item->inlineReplyMarkup();
	if (!markup) {
		return false;
	}
	using Type = HistoryMessageMarkupButton::Type;
	auto hasLink = false;
	auto actionLabel = false;
	for (const auto &row : markup->data.rows) {
		for (const auto &button : row) {
			if (button.type == Type::Url
				|| button.type == Type::Auth
				|| button.type == Type::WebView
				|| button.type == Type::SimpleWebView) {
				hasLink = true;
				actionLabel = actionLabel || ContainsAny(button.text, {
					"принять", "отклонить", "подтвердить", "забрать",
					"получить", "accept", "decline", "confirm", "claim",
					"reject",
				});
			}
		}
	}
	const auto impersonates = ContainsAny(from->name(), {
		"telegram", "телеграм", "gift", "подарк",
	});
	return hasLink && actionLabel && impersonates;
}

bool HasMark(const QString &text) {
	return !text.isEmpty() && text[0] == kMarkChar;
}

void PaintBanner(QPainter &p, QRect rect) {
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	const auto red = QColor(0xFF, 0x5C, 0x5C);
	rect.setHeight(rect.height() - 2);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xFF, 0x3B, 0x30, 46));
	p.drawRoundedRect(rect, 7, 7);
	p.setBrush(red);
	p.drawRoundedRect(QRect(rect.x(), rect.y(), 3, rect.height()), 1.5, 1.5);
	p.setPen(red);
	p.setFont(st::semiboldFont);
	p.drawText(
		rect.adjusted(11, 0, -6, 0),
		Qt::AlignVCenter | Qt::AlignLeft,
		st::semiboldFont->elided(
			QString::fromUtf8(Mokagram::Lang::Tr("\xE2\x9A\xA0 Похоже на скам")),
			rect.width() - 17));
	p.restore();
}

void ShowWarning(not_null<const HistoryItem*> item, Fn<void()> proceed) {
	const auto window = Core::App().activeWindow();
	if (!window) {
		return;
	}
	window->show(Ui::MakeConfirmBox({
		.text = TextWithEntities{ U(Mokagram::Lang::Tr(
			"Это похоже на фейковый оффер. "
			"Настоящий оффер выглядит как карточка подарка по центру чата, "
			"а не как обычное сообщение человека с кнопками. "
			"Такие кнопки могут вести на сайты, которые крадут аккаунт и подарки. "
			"Не заходи в аккаунт и не подтверждай ничего на открывшейся странице.")) },
		.confirmed = [=](Fn<void()> close) {
			close();
			proceed();
		},
		.confirmText = U(Mokagram::Lang::Tr("Всё равно открыть")),
		.cancelText = U(Mokagram::Lang::Tr("Не открывать")),
		.title = rpl::single(U(Mokagram::Lang::Tr("Возможный скам"))),
	}));
}

} // namespace Mokagram::Scam
