// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 1a4e9630
// Mokagram: оформление, настраиваемое в «Кастомизации».
#include "mokagram/appearance/mokagram_appearance.h"
#include "mokagram/lang/mokagram_lang.h"

#include "mokagram/settings/mokagram_settings.h"
#include "mokagram/music/mokagram_motion.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_palette.h"
#include "styles/palette.h"

#include <vector>
#include <rpl/rpl.h>


namespace Mokagram::Appearance {
namespace {

namespace S = Settings;

constexpr auto kBubblesOn = "bubbles.enabled";
constexpr auto kOutColor = "bubbles.out_color";
constexpr auto kInColor = "bubbles.in_color";
constexpr auto kOutText = "bubbles.out_text";
constexpr auto kInText = "bubbles.in_text";

rpl::lifetime Lifetime;
bool Applying = false;

struct Slot {
	const char *name;
	style::color color;
	QColor original;
	QColor lastSet;
};

std::vector<Slot> &Slots() {
	static auto result = std::vector<Slot>{
		{ "msgOutBg", st::msgOutBg },
		{ "msgOutBgSelected", st::msgOutBgSelected },
		{ "msgOutShadow", st::msgOutShadow },
		{ "msgOutShadowSelected", st::msgOutShadowSelected },
		{ "msgInBg", st::msgInBg },
		{ "msgInBgSelected", st::msgInBgSelected },
		{ "msgInShadow", st::msgInShadow },
		{ "msgInShadowSelected", st::msgInShadowSelected },
		{ "historyTextOutFg", st::historyTextOutFg },
		{ "historyTextInFg", st::historyTextInFg },
	};
	return result;
}

[[nodiscard]] QColor Shifted(const QColor &color, int percent) {
	return color.lighter(100 + percent);
}

[[nodiscard]] QColor Shadow(const QColor &color) {
	auto result = color.darker(150);
	result.setAlpha(60);
	return result;
}

void Apply() {
	if (Applying) {
		return;
	}
	Applying = true;
	const auto on = S::Bool(kBubblesOn);
	const auto out = on ? ColorSetting(kOutColor) : QColor();
	const auto in = on ? ColorSetting(kInColor) : QColor();
	const auto outText = on ? ColorSetting(kOutText) : QColor();
	const auto inText = on ? ColorSetting(kInText) : QColor();
	const auto wanted = std::vector<QColor>{
		out.isValid() ? out : QColor(),
		out.isValid() ? Shifted(out, 12) : QColor(),
		out.isValid() ? Shadow(out) : QColor(),
		out.isValid() ? Shadow(Shifted(out, 12)) : QColor(),
		in.isValid() ? in : QColor(),
		in.isValid() ? Shifted(in, 12) : QColor(),
		in.isValid() ? Shadow(in) : QColor(),
		in.isValid() ? Shadow(Shifted(in, 12)) : QColor(),
		outText,
		inText,
	};
	auto &slots = Slots();
	auto changed = false;
	for (auto i = 0; i != int(slots.size()); ++i) {
		auto &slot = slots[i];
		const auto now = QColor(slot.color->c);
		// Цвет не наш: тема его перезаписала, это новый исходный цвет.
		if (!slot.lastSet.isValid() || now != slot.lastSet) {
			slot.original = now;
			slot.lastSet = QColor();
		}
		const auto target = wanted[i].isValid() ? wanted[i] : slot.original;
		if (target != now) {
			style::main_palette::setColor(
				QLatin1String(slot.name),
				uchar(target.red()),
				uchar(target.green()),
				uchar(target.blue()),
				uchar(target.alpha()));
			changed = true;
		}
		slot.lastSet = target;
	}
	if (changed) {
		style::NotifyPaletteChanged();
	}
	Applying = false;
}

} // namespace

QColor ColorSetting(const QString &key) {
	const auto text = S::String(key).trimmed();
	if (text.isEmpty()) {
		return QColor();
	}
	return QColor(text.startsWith('#') ? text : (u"#"_q + text));
}

int SelectionLimit() {
	return S::Bool(u"chats.select_3000"_q) ? 3000 : 100;
}

void RegisterSettings() {
	const auto section = QString::fromUtf8(Mokagram::Lang::Tr("Пузыри сообщений"));
	const auto add = [&](S::Entry entry) {
		entry.section = section;
		entry.category = u"look"_q;
		entry.sectionIcon = u"hide"_q;
		S::Register(std::move(entry));
	};
	add({
		.key = u"bubbles.enabled"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Свои цвета пузырьков")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Выключено: пузырьки рисуются цветами темы.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
		.sectionNote = QString::fromUtf8(
			Mokagram::Lang::Tr("Цвет пишется как #RRGGBB. Пустое поле оставляет цвет темы.")),
	});
	const auto color = [&](const char *key, const char *title) {
		add({
			.key = QString::fromUtf8(key),
			.title = QString::fromUtf8(title),
			.kind = S::Kind::Color,
			.defaultValue = QString(),
			.dependsOn = u"bubbles.enabled"_q,
		});
	};
	color(kOutColor, Mokagram::Lang::Tr("Мои сообщения: фон"));
	color(kOutText, Mokagram::Lang::Tr("Мои сообщения: текст"));
	color(kInColor, Mokagram::Lang::Tr("Чужие сообщения: фон"));
	color(kInText, Mokagram::Lang::Tr("Чужие сообщения: текст"));

	{
		const auto chats = QString::fromUtf8(Mokagram::Lang::Tr("Сообщения"));
		const auto chatEntry = [&](S::Entry entry) {
			entry.section = chats;
			entry.category = u"chats"_q;
			entry.sectionIcon = u"chats"_q;
			S::Register(std::move(entry));
		};
		chatEntry({
			.key = u"chats.select_3000"_q,
			.title = QString::fromUtf8(Mokagram::Lang::Tr("Выделять до 3000 сообщений")),
			.description = QString::fromUtf8(Mokagram::Lang::Tr("Вместо ста. Удаление и пересылка сами делятся на части.")),
			.kind = S::Kind::Bool,
			.defaultValue = true,
		});
		chatEntry({
			.key = u"chats.edited_pencil"_q,
			.title = QString::fromUtf8(Mokagram::Lang::Tr("Короткая надпись «изм.»")),
			.description = QString::fromUtf8(Mokagram::Lang::Tr("У изменённого сообщения возле времени стоит «изм.» вместо «изменено».")),
			.kind = S::Kind::Bool,
			.defaultValue = true,
		});
	}
	const auto player = QString::fromUtf8(Mokagram::Lang::Tr("Плеер"));
	const auto playerEntry = [&](S::Entry entry) {
		entry.section = player;
		entry.category = u"look"_q;
		entry.sectionIcon = u"hide"_q;
		S::Register(std::move(entry));
	};
	playerEntry({
		.key = u"player.presentation"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Вид плеера")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Обычная панель сверху или Dynamic Island над чатом.")),
		.kind = S::Kind::Choice,
		.defaultValue = 1,
		.choices = {
			QString::fromUtf8(Mokagram::Lang::Tr("Обычный плеер")),
			QString::fromUtf8(Mokagram::Lang::Tr("Dynamic Island")),
		},
	});
	playerEntry({
		.key = u"player.cover_colors"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Цвета плеера из обложки")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Фон полноэкранного плеера под цвет обложки и лёгкий оттенок на панели воспроизведения.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
	});
	playerEntry({
		.key = u"player.vinyl"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Обложка в виде диска")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("В верхней панели плеера обложка круглая, как виниловый диск: крутится при воспроизведении и плавно останавливается на паузе.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	});
	playerEntry({
		.key = u"player.autoplay_next"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Автовоспроизведение следующего")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("После трека, голосового или кружка сам включается следующий.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
	});
	playerEntry({
		.key = u"music.smart_stream"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Умный поток музыки")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Подборка «Для вас» играет без конца: очередь сама пополняется под ваш вкус и текущее настроение, без повторов и пропущенного.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
	});
	playerEntry({
		.key = u"music.hint_sticker"_q,
		.title = u"music.hint_sticker"_q,
		.kind = S::Kind::String,
		.defaultValue = QString(),
		.hidden = true,
	});
	playerEntry({
		.key = u"lyrics.enabled"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Текст песни")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Панель со словами справа от чата, текущая строка подсвечивается. Включается кнопкой в панели плеера.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
		.hidden = true,
	});
	playerEntry({
		.key = u"lyrics.plasma"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Оттенок обложки за текстом")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Лёгкий оттенок обложки на фоне текста песни.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
		.dependsOn = u"lyrics.enabled"_q,
	});
	playerEntry({
		.key = u"lyrics.comments"_q,
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Комментарии SoundCloud")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr("Аватарки справа от текущей строки, а без текста песни на полосе прогресса. Наведи, чтобы прочитать; колёсико переключает комментарии в группе.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
	});
	Music::RegisterMotionSettings();
}

void Start() {
	S::Changed(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"bubbles."_q);
	}) | rpl::on_next([] {
		Apply();
	}, Lifetime);
	style::PaletteChanged(
	) | rpl::on_next([] {
		Apply();
	}, Lifetime);
	Apply();
}

} // namespace Mokagram::Appearance
