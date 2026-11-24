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
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/painter.h"

#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"

namespace {

struct LanguageLabel {
	QString text;
	int width = 0;
};

class LanguageButton final : public Ui::RippleButton {
public:
	LanguageButton(QWidget *parent, rpl::producer<QString> text);

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void setText(const QString &text);
	void updateWidth();
	[[nodiscard]] int textWidth() const;
	[[nodiscard]] QRect pill() const;

	LanguageLabel _label;
	LanguageLabel _previous;
	Ui::Animations::Simple _change;

};

LanguageButton::LanguageButton(QWidget *parent, rpl::producer<QString> text)
: RippleButton(parent, st::defaultRippleAnimation) {
	std::move(text) | rpl::on_next([=](const QString &text) {
		setText(text);
	}, lifetime());
}

void LanguageButton::setText(const QString &text) {
	const auto &font = st::botInfoLanguageFont;
	const auto elided = font->elided(text, st::botInfoLanguageTextMaxWidth);
	if (_label.text == elided) {
		return;
	}
	setAccessibleName(text);
	_previous = std::exchange(_label, { elided, font->width(elided) });
	if (_previous.text.isEmpty()) {
		_change.stop();
	} else {
		_change.start(
			[=] { updateWidth(); },
			0.,
			1.,
			st::slideWrapDuration,
			anim::easeOutCirc);
	}
	updateWidth();
}

void LanguageButton::updateWidth() {
	const auto &padding = st::botInfoLanguagePadding;
	const auto right = x() + width();
	const auto was = width();
	resize(
		padding.left()
			+ st::botInfoLanguageIcon.width()
			+ st::botInfoLanguageTextSkip
			+ textWidth()
			+ st::botInfoLanguageTextSkip
			+ st::botInfoLanguageArrow.width()
			+ padding.right(),
		st::boxTitleHeight);
	if (was && (was != width())) {
		move(right - width(), y());
	}
	update();
}

int LanguageButton::textWidth() const {
	return anim::interpolate(
		_previous.width,
		_label.width,
		_change.value(1.));
}

QRect LanguageButton::pill() const {
	const auto &padding = st::botInfoLanguagePadding;
	return rect().marginsRemoved(
		{ 0, padding.top(), 0, padding.bottom() });
}

void LanguageButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto &padding = st::botInfoLanguagePadding;
	const auto &font = st::botInfoLanguageFont;
	const auto &icon = st::botInfoLanguageIcon;
	const auto &arrow = st::botInfoLanguageArrow;
	const auto pill = LanguageButton::pill();
	const auto radius = pill.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(pill, radius, radius);
	paintRipple(p, pill.topLeft());

	auto left = padding.left();
	icon.paint(
		p,
		left,
		pill.y() + (pill.height() - icon.height()) / 2,
		width());
	left += icon.width() + st::botInfoLanguageTextSkip;
	const auto progress = _change.value(1.);
	const auto top = pill.y()
		+ (pill.height() - font->height) / 2
		+ font->ascent;
	p.setPen(st::windowBoldFg);
	p.setFont(font);
	if (progress < 1.) {
		p.setOpacity(1. - progress);
		p.drawText(left, top, _previous.text);
		p.setOpacity(progress);
	}
	p.drawText(left, top, _label.text);
	p.setOpacity(1.);
	left += textWidth() + st::botInfoLanguageTextSkip;
	arrow.paint(
		p,
		left,
		pill.y() + (pill.height() - arrow.height()) / 2,
		width());
}

QImage LanguageButton::prepareRippleMask() const {
	const auto size = pill().size();
	return Ui::RippleAnimation::RoundRectMask(size, size.height() / 2);
}

QPoint LanguageButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - pill().topLeft();
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

struct LanguageEntry {
	QString code;
	const style::icon *icon = nullptr;

	friend inline bool operator==(
		const LanguageEntry &,
		const LanguageEntry &) = default;
};

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
		rpl::producer<QString> text) {
	return object_ptr<LanguageButton>(nullptr, std::move(text));
}

void FillBotInfoLanguageMenu(
		not_null<Ui::PopupMenu*> menu,
		BotInfoLanguageMenuArgs &&args) {
	struct State {
		std::vector<LanguageEntry> entries;
		rpl::variable<QString> loading;
		QAction *loadingAction = nullptr;
		Ui::Animations::Simple resize;
	};
	const auto draft = args.draft;
	const auto current = args.current;
	const auto choose = args.choose;
	const auto add = args.add;
	const auto translate = args.translate;
	const auto state = menu->lifetime().make_state<State>();
	state->loading = std::move(args.loading);

	const auto entries = [=] {
		auto result = std::vector<LanguageEntry>();
		for (const auto &language : SortedLanguages(*draft)) {
			result.push_back({
				.code = language->code,
				.icon = LanguageIcon(*language, current),
			});
		}
		return result;
	};
	const auto rebuild = [=] {
		menu->clearActions();
		state->entries = entries();
		for (const auto &entry : state->entries) {
			const auto code = entry.code;
			menu->addAction(
				BotInfoLanguageName(code),
				[=] { choose(code); },
				entry.icon);
		}
		const auto loading = state->loading.current();
		state->loadingAction = loading.isEmpty()
			? nullptr
			: menu->addAction(loading, [] {}).get();
		if (state->loadingAction) {
			state->loadingAction->setEnabled(false);
		}
		menu->addSeparator();
		menu->addAction(
			tr::lng_bot_languages_translate(tr::now),
			translate,
			&st::menuIconTranslate);
		menu->addAction(
			tr::lng_bot_languages_add(tr::now),
			add,
			&st::menuIconAdd);
	};
	rebuild();

	const auto keepRight = [=](int right) {
		menu->move(right - menu->width(), menu->y());
	};
	const auto rebuildAnimated = [=] {
		const auto inner = menu->menu();
		const auto from = inner->width();
		const auto right = menu->x() + menu->width();
		state->resize.stop();
		menu->setForceWidth(0);
		rebuild();
		const auto to = inner->width();
		if (from == to) {
			keepRight(right);
			return;
		}
		const auto apply = [=] {
			const auto progress = state->resize.value(1.);
			if (state->resize.animating()) {
				menu->setForceWidth(anim::interpolate(from, to, progress));
			} else {
				menu->setForceWidth(0);
				rebuild();
			}
			keepRight(right);
		};
		menu->setForceWidth(from);
		keepRight(right);
		state->resize.start(
			apply,
			0.,
			1.,
			st::slideWrapDuration,
			anim::easeOutCirc);
	};
	const auto refresh = [=] {
		const auto loading = state->loading.current();
		const auto shown = (state->loadingAction != nullptr);
		if (state->resize.animating()) {
			return;
		} else if ((shown == loading.isEmpty())
			|| (state->entries != entries())) {
			rebuildAnimated();
		} else if (shown) {
			const auto right = menu->x() + menu->width();
			state->loadingAction->setText(loading);
			keepRight(right);
		}
	};
	rpl::merge(
		draft->updates(),
		state->loading.changes() | rpl::to_empty
	) | rpl::on_next([=] {
		crl::on_main(menu, refresh);
	}, menu->lifetime());
}
