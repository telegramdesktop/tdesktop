/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/choose_language_box.h"

#include "lang/lang_keys.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "base/debug_log.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

const auto kLanguageNamePrefix = "cloud_lng_language_";
const auto kTranslateToPrefix = "cloud_lng_translate_to_";

[[nodiscard]] std::vector<LanguageId> TranslationLanguagesList() {
	// If adding some languages here you need to check that it is
	// supported on the server. Right now server supports those:
	//
	// 'af', 'sq', 'am', 'ar', 'hy', 'az', 'eu', 'be', 'bn', 'bs', 'bg',
	// 'ca', 'ceb', 'zh-CN', 'zh', 'zh-TW', 'co', 'hr', 'cs', 'da', 'nl',
	// 'en', 'eo', 'et', 'fi', 'fr', 'fy', 'gl', 'ka', 'de', 'el', 'gu',
	// 'ht', 'ha', 'haw', 'he', 'iw', 'hi', 'hmn', 'hu', 'is', 'ig', 'id',
	// 'ga', 'it', 'ja', 'jv', 'kn', 'kk', 'km', 'rw', 'ko', 'ku', 'ky',
	// 'lo', 'la', 'lv', 'lt', 'lb', 'mk', 'mg', 'ms', 'ml', 'mt', 'mi',
	// 'mr', 'mn', 'my', 'ne', 'no', 'ny', 'or', 'ps', 'fa', 'pl', 'pt',
	// 'pa', 'ro', 'ru', 'sm', 'gd', 'sr', 'st', 'sn', 'sd', 'si', 'sk',
	// 'sl', 'so', 'es', 'su', 'sw', 'sv', 'tl', 'tg', 'ta', 'tt', 'te',
	// 'th', 'tr', 'tk', 'uk', 'ur', 'ug', 'uz', 'vi', 'cy', 'xh', 'yi',
	// 'yo', 'zu',
	return {
		{ QLocale::English },
		{ QLocale::Arabic },
		{ QLocale::Belarusian },
		{ QLocale::Catalan },
		{ QLocale::Chinese },
		{ QLocale::Dutch },
		{ QLocale::French },
		{ QLocale::German },
		{ QLocale::Indonesian },
		{ QLocale::Italian },
		{ QLocale::Japanese },
		{ QLocale::Korean },
		{ QLocale::Polish },
		{ QLocale::Portuguese },
		{ QLocale::Russian },
		{ QLocale::Spanish },
		{ QLocale::Ukrainian },

		{ QLocale::Afrikaans },
		{ QLocale::Albanian },
		{ QLocale::Amharic },
		{ QLocale::Armenian },
		{ QLocale::Azerbaijani },
		{ QLocale::Basque },
		{ QLocale::Bosnian },
		{ QLocale::Bulgarian },
		{ QLocale::Burmese },
		{ QLocale::Croatian },
		{ QLocale::Czech },
		{ QLocale::Danish },
		{ QLocale::Esperanto },
		{ QLocale::Estonian },
		{ QLocale::Finnish },
		{ QLocale::Gaelic },
		{ QLocale::Galician },
		{ QLocale::Georgian },
		{ QLocale::Greek },
		{ QLocale::Gusii },
		{ QLocale::Hausa },
		{ QLocale::Hebrew },
		{ QLocale::Hungarian },
		{ QLocale::Icelandic },
		{ QLocale::Igbo },
		{ QLocale::Irish },
		{ QLocale::Kazakh },
		{ QLocale::Kinyarwanda },
		{ QLocale::Kurdish },
		{ QLocale::Lao },
		{ QLocale::Latvian },
		{ QLocale::Lithuanian },
		{ QLocale::Luxembourgish },
		{ QLocale::Macedonian },
		{ QLocale::Malagasy },
		{ QLocale::Malay },
		{ QLocale::Maltese },
		{ QLocale::Maori },
		{ QLocale::Mongolian },
		{ QLocale::Nepali },
		{ QLocale::Pashto },
		{ QLocale::Persian },
		{ QLocale::Romanian },
		{ QLocale::Serbian },
		{ QLocale::Shona },
		{ QLocale::Sindhi },
		{ QLocale::Sinhala },
		{ QLocale::Slovak },
		{ QLocale::Slovenian },
		{ QLocale::Somali },
		{ QLocale::Sundanese },
		{ QLocale::Swahili },
		{ QLocale::Swedish },
		{ QLocale::Tajik },
		{ QLocale::Tatar },
		{ QLocale::Teso },
		{ QLocale::Thai },
		{ QLocale::Turkish },
		{ QLocale::Turkmen },
		{ QLocale::Urdu },
		{ QLocale::Uzbek },
		{ QLocale::Vietnamese },
		{ QLocale::Welsh },
		{ QLocale::WesternFrisian },
		{ QLocale::Xhosa },
		{ QLocale::Yiddish },
	};
}

class Row final : public SettingsButton {
public:
	Row(not_null<RpWidget*> parent, LanguageId id);

	[[nodiscard]] bool filtered(const QString &query) const;
	[[nodiscard]] LanguageId id() const;

	int resizeGetHeight(int newWidth) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::PeerListItem &_st;
	const LanguageId _id;
	const QString _status;
	const QString _titleText;
	Text::String _title;

};

Row::Row(not_null<RpWidget*> parent, LanguageId id)
: SettingsButton(parent, rpl::never<QString>())
, _st(st::inviteLinkListItem)
, _id(id)
, _status(LanguageName(id))
, _titleText(LanguageNameNative(id))
, _title(_st.nameStyle, _titleText) {
}

LanguageId Row::id() const {
	return _id;
}

bool Row::filtered(const QString &query) const {
	return _status.startsWith(query, Qt::CaseInsensitive)
		|| _titleText.startsWith(query, Qt::CaseInsensitive);
}

int Row::resizeGetHeight(int newWidth) {
	return _st.height;
}

void Row::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto paintOver = (isOver() || isDown()) && !isDisabled();
	SettingsButton::paintBg(p, e->rect(), paintOver);
	SettingsButton::paintRipple(p, 0, 0);
	SettingsButton::paintToggle(p, width());

	const auto &color = st::windowSubTextFg;
	p.setPen(Qt::NoPen);
	p.setBrush(color);

	const auto left = st::defaultSubsectionTitlePadding.left();
	const auto toggleRect = SettingsButton::maybeToggleRect();
	const auto right = left
		+ (toggleRect.isEmpty() ? 0 : (width() - toggleRect.x()));

	const auto availableWidth = std::min(
		_title.maxWidth(),
		width() - left - right);
	p.setPen(_st.nameFg);
	_title.drawLeft(
		p,
		left,
		_st.namePosition.y(),
		availableWidth,
		width() - left - right);

	p.setPen(paintOver ? _st.statusFgOver : _st.statusFg);
	p.setFont(st::contactsStatusFont);
	p.drawTextLeft(
		left,
		_st.statusPosition.y(),
		width() - left - right,
		_status);
}

} // namespace

QString LanguageNameTranslated(const QString &twoLetterCode) {
	return Lang::GetNonDefaultValue(
		kLanguageNamePrefix + twoLetterCode.toUtf8());
}

QString LanguageNameLocal(LanguageId id) {
	return QLocale::languageToString(id.language());
}

QString LanguageName(LanguageId id) {
	const auto translated = LanguageNameTranslated(id.twoLetterCode());
	return translated.isEmpty() ? LanguageNameLocal(id) : translated;
}

QString LanguageNameNative(LanguageId id) {
	const auto locale = id.locale();
	if (locale.language() == QLocale::English
			&& (locale.country() == QLocale::UnitedStates
				|| locale.country() == QLocale::AnyCountry)) {
		return u"English"_q;
	} else if (locale.language() == QLocale::Spanish) {
		return QString::fromUtf8("\x45\x73\x70\x61\xc3\xb1\x6f\x6c");
	} else {
		const auto name = locale.nativeLanguageName();
		return name.left(1).toUpper() + name.mid(1);
	}
}

rpl::producer<QString> TranslateBarTo(LanguageId id) {
	const auto translated = Lang::GetNonDefaultValue(
		kTranslateToPrefix + id.twoLetterCode().toUtf8());
	return (translated.isEmpty()
		? tr::lng_translate_bar_to_other
		: tr::lng_translate_bar_to)(
			lt_name,
			rpl::single(translated.isEmpty()
				? LanguageNameLocal(id)
				: translated));
}

QString TranslateMenuDont(tr::now_t, LanguageId id) {
	const auto translated = Lang::GetNonDefaultValue(
		kTranslateToPrefix + id.twoLetterCode().toUtf8());
	return (translated.isEmpty()
		? tr::lng_translate_menu_dont_other
		: tr::lng_translate_menu_dont)(
			tr::now,
			lt_name,
			translated.isEmpty() ? LanguageNameLocal(id) : translated);
}

void ChooseLanguageBox(
		not_null<GenericBox*> box,
		rpl::producer<QString> title,
		Fn<void(std::vector<LanguageId>)> callback,
		std::vector<LanguageId> selected,
		bool multiselect,
		Fn<bool(LanguageId)> toggleCheck) {
	box->setMinHeight(st::boxWidth);
	box->setMaxHeight(st::boxWidth);
	box->setTitle(std::move(title));

	const auto multiSelect = box->setPinnedToTopContent(
		object_ptr<MultiSelect>(
			box,
			st::defaultMultiSelect,
			tr::lng_participant_filter()));
	box->setFocusCallback([=] { multiSelect->setInnerFocus(); });

	const auto container = box->verticalLayout();
	const auto langs = [&] {
		auto list = TranslationLanguagesList();
		for (const auto id : list) {
			LOG(("cloud_lng_language_%1").arg(id.twoLetterCode()));
		}
		const auto current = LanguageId{ QLocale(
			Lang::LanguageIdOrDefault(Lang::Id())).language() };
		if (const auto i = ranges::find(list, current); i != end(list)) {
			base::reorder(list, std::distance(begin(list), i), 0);
		}
		ranges::stable_partition(list, [&](LanguageId id) {
			return ranges::contains(selected, id);
		});
		return list;
	}();
	struct ToggleOne {
		LanguageId id;
		bool selected = false;
	};
	struct State {
		rpl::event_stream<ToggleOne> toggles;
	};
	const auto state = box->lifetime().make_state<State>();
	auto rows = std::vector<not_null<SlideWrap<Row>*>>();
	rows.reserve(langs.size());
	for (const auto &id : langs) {
		const auto button = container->add(
			object_ptr<SlideWrap<Row>>(
				container,
				object_ptr<Row>(container, id)));
		if (multiselect) {
			button->entity()->toggleOn(rpl::single(
				ranges::contains(selected, id)
			) | rpl::then(state->toggles.events(
			) | rpl::filter([=](ToggleOne one) {
				return one.id == id;
			}) | rpl::map([=](ToggleOne one) {
				return one.selected;
			})));

			button->entity()->toggledChanges(
			) | rpl::start_with_next([=](bool value) {
				if (toggleCheck && !toggleCheck(id)) {
					state->toggles.fire({ .id = id, .selected = !value });
				}
			}, button->lifetime());
		} else {
			button->entity()->setClickedCallback([=] {
				callback({ id });
				box->closeBox();
			});
		}
		rows.push_back(button);
	}

	multiSelect->setQueryChangedCallback([=](const QString &query) {
		for (const auto &row : rows) {
			const auto toggled = row->entity()->filtered(query);
			if (toggled != row->toggled()) {
				row->toggle(toggled, anim::type::instant);
			}
		}
	});

	{
		const auto label = CreateChild<FlatLabel>(
			box.get(),
			tr::lng_languages_none(),
			st::membersAbout);
		box->verticalLayout()->geometryValue(
		) | rpl::start_with_next([=](const QRect &geometry) {
			const auto shown = (geometry.height() <= 0);
			label->setVisible(shown);
			if (shown) {
				label->moveToLeft(
					(geometry.width() - label->width()) / 2,
					geometry.y() + st::membersAbout.style.font->height * 4);
				label->stackUnder(box->verticalLayout());
			}
		}, label->lifetime());
	}

	if (multiselect) {
		box->addButton(tr::lng_settings_save(), [=] {
			auto result = ranges::views::all(
				rows
			) | ranges::views::filter([](const auto &row) {
				return row->entity()->toggled();
			}) | ranges::views::transform([](const auto &row) {
				return row->entity()->id();
			}) | ranges::to_vector;
			if (!result.empty()) {
				callback(std::move(result));
			}
			box->closeBox();
		});
	}
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Ui
