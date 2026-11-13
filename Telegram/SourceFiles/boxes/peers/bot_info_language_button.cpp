/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/bot_info_language_button.h"

#include "data/data_bot_info_draft.h"
#include "lang/lang_keys.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"

#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"

namespace {

class LanguageButton final : public Ui::AbstractButton {
public:
	LanguageButton(QWidget *parent, const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	const QString _text;
	const int _textWidth = 0;

};

LanguageButton::LanguageButton(QWidget *parent, const QString &text)
: AbstractButton(parent)
, _text(st::botInfoLanguageFont->elided(
	text,
	st::botInfoLanguageTextMaxWidth))
, _textWidth(st::botInfoLanguageFont->width(_text)) {
	const auto &padding = st::botInfoLanguagePadding;
	setAccessibleName(text);
	resize(
		padding.left()
			+ st::botInfoLanguageIcon.width()
			+ st::botInfoLanguageTextSkip
			+ _textWidth
			+ st::botInfoLanguageTextSkip
			+ st::botInfoLanguageArrow.width()
			+ padding.right(),
		st::boxTitleHeight);
}

void LanguageButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto &padding = st::botInfoLanguagePadding;
	const auto &font = st::botInfoLanguageFont;
	const auto &icon = st::botInfoLanguageIcon;
	const auto &arrow = st::botInfoLanguageArrow;
	const auto pill = rect().marginsRemoved(
		{ 0, padding.top(), 0, padding.bottom() });
	const auto radius = pill.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::windowBgRipple : st::windowBgOver);
	p.drawRoundedRect(pill, radius, radius);

	auto left = padding.left();
	icon.paint(
		p,
		left,
		pill.y() + (pill.height() - icon.height()) / 2,
		width());
	left += icon.width() + st::botInfoLanguageTextSkip;
	p.setPen(st::windowBoldFg);
	p.setFont(font);
	p.drawText(
		left,
		pill.y() + (pill.height() - font->height) / 2 + font->ascent,
		_text);
	left += _textWidth + st::botInfoLanguageTextSkip;
	arrow.paint(
		p,
		left,
		pill.y() + (pill.height() - arrow.height()) / 2,
		width());
}

void LanguageButton::onStateChanged(State was, StateChangeSource source) {
	update();
}

[[nodiscard]] auto SortedLanguages(const Data::BotInfoDraft &draft) {
	auto result = std::vector<not_null<const Data::BotInfoLanguage*>>();
	for (const auto &language : draft.languages()) {
		result.push_back(&language);
	}
	ranges::sort(result, [](const auto &a, const auto &b) {
		if (a->fallback() || b->fallback()) {
			return a->fallback() && !b->fallback();
		}
		const auto first = BotInfoLanguageName(a->code);
		const auto second = BotInfoLanguageName(b->code);
		return first.compare(second, Qt::CaseInsensitive) < 0;
	});
	return result;
}

[[nodiscard]] const style::icon *LanguageIcon(
		const Data::BotInfoLanguage &language,
		const QString &current) {
	return (language.code == current)
		? &st::mediaPlayerMenuCheck
		: (language.hasProblem() || !language.error.isEmpty())
		? &st::menuIconInfo
		: !language.staged.empty()
		? &st::menuIconEdit
		: nullptr;
}

} // namespace

QString BotInfoLanguageName(const QString &langCode) {
	if (langCode.isEmpty()) {
		return tr::lng_bot_languages_default(tr::now);
	}
	const auto id = LanguageId::FromName(langCode);
	return (id.twoLetterCode() == langCode)
		? Ui::LanguageName(id)
		: langCode.toUpper();
}

object_ptr<Ui::AbstractButton> CreateBotInfoLanguageButton(
		const QString &text) {
	return object_ptr<LanguageButton>(nullptr, text);
}

void FillBotInfoLanguageMenu(
		not_null<Ui::PopupMenu*> menu,
		BotInfoLanguageMenuArgs &&args) {
	const auto choose = args.choose;
	for (const auto &language : SortedLanguages(*args.draft)) {
		const auto code = language->code;
		menu->addAction(
			BotInfoLanguageName(code),
			[=] { choose(code); },
			LanguageIcon(*language, args.current));
	}
	if (!args.loading.isEmpty()) {
		menu->addAction(args.loading, [] {})->setEnabled(false);
	}
	menu->addSeparator();
	menu->addAction(
		tr::lng_bot_languages_add(tr::now),
		args.add,
		&st::menuIconAdd);
	menu->addAction(
		tr::lng_bot_languages_translate(tr::now),
		args.translate,
		&st::menuIconTranslate);
}
