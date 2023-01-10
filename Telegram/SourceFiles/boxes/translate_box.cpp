/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/translate_box.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/data_peer.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "settings/settings_common.h"
#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "spellcheck/platform/platform_language.h"
#endif
#include "ui/effects/loading_element.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h" // inviteLinkListItem.
#include "styles/style_layers.h"
#include "styles/style_settings.h" // settingsSubsectionTitlePadding.

#include <QLocale>

namespace Ui {
namespace {

[[nodiscard]] std::vector<QLocale::Language> Languages() {
	return std::vector<QLocale::Language>{
		QLocale::English,
		QLocale::Afrikaans,
		QLocale::Albanian,
		QLocale::Amharic,
		QLocale::Arabic,
		QLocale::Armenian,
		QLocale::Azerbaijani,
		QLocale::Basque,
		QLocale::Belarusian,
		QLocale::Bosnian,
		QLocale::Bulgarian,
		QLocale::Burmese,
		QLocale::Catalan,
		QLocale::Chinese,
		QLocale::Croatian,
		QLocale::Czech,
		QLocale::Danish,
		QLocale::Dutch,
		QLocale::Esperanto,
		QLocale::Estonian,
		QLocale::Finnish,
		QLocale::French,
		QLocale::Gaelic,
		QLocale::Galician,
		QLocale::Georgian,
		QLocale::German,
		QLocale::Greek,
		QLocale::Gusii,
		QLocale::Hausa,
		QLocale::Hebrew,
		QLocale::Hungarian,
		QLocale::Icelandic,
		QLocale::Igbo,
		QLocale::Indonesian,
		QLocale::Irish,
		QLocale::Italian,
		QLocale::Japanese,
		QLocale::Kazakh,
		QLocale::Kinyarwanda,
		QLocale::Korean,
		QLocale::Kurdish,
		QLocale::Lao,
		QLocale::Latvian,
		QLocale::Lithuanian,
		QLocale::Luxembourgish,
		QLocale::Macedonian,
		QLocale::Malagasy,
		QLocale::Malay,
		QLocale::Maltese,
		QLocale::Maori,
		QLocale::Mongolian,
		QLocale::Nepali,
		QLocale::Pashto,
		QLocale::Persian,
		QLocale::Polish,
		QLocale::Portuguese,
		QLocale::Romanian,
		QLocale::Russian,
		QLocale::Serbian,
		QLocale::Shona,
		QLocale::Sindhi,
		QLocale::Sinhala,
		QLocale::Slovak,
		QLocale::Slovenian,
		QLocale::Somali,
		QLocale::Spanish,
		QLocale::Sundanese,
		QLocale::Swahili,
		QLocale::Swedish,
		QLocale::Tajik,
		QLocale::Tatar,
		QLocale::Teso,
		QLocale::Thai,
		QLocale::Turkish,
		QLocale::Turkmen,
		QLocale::Ukrainian,
		QLocale::Urdu,
		QLocale::Uzbek,
		QLocale::Vietnamese,
		QLocale::Welsh,
		QLocale::WesternFrisian,
		QLocale::Xhosa,
		QLocale::Yiddish,
	};
}

class Row final : public Ui::SettingsButton {
public:
	Row(not_null<Ui::RpWidget*> parent, const QLocale &locale);

	[[nodiscard]] bool filtered(const QString &query) const;
	[[nodiscard]] QLocale locale() const;

	int resizeGetHeight(int newWidth) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::PeerListItem &_st;
	const QLocale _locale;
	const QString _status;
	const QString _titleText;
	Ui::Text::String _title;

};

Row::Row(not_null<Ui::RpWidget*> parent, const QLocale &locale)
: SettingsButton(parent, rpl::never<QString>())
, _st(st::inviteLinkListItem)
, _locale(locale)
, _status(QLocale::languageToString(locale.language()))
, _titleText(LanguageName(locale))
, _title(_st.nameStyle, _titleText) {
}

QLocale Row::locale() const {
	return _locale;
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
	Ui::SettingsButton::paintBg(p, e->rect(), paintOver);
	Ui::SettingsButton::paintRipple(p, 0, 0);
	Ui::SettingsButton::paintToggle(p, width());

	const auto &color = st::windowSubTextFg;
	p.setPen(Qt::NoPen);
	p.setBrush(color);

	const auto left = st::settingsSubsectionTitlePadding.left();
	const auto toggleRect = Ui::SettingsButton::maybeToggleRect();
	const auto right = left
		+ (toggleRect.isEmpty() ? 0 : (width() - toggleRect.x()));

	p.setPen(_st.nameFg);
	_title.drawLeft(
		p,
		left,
		_st.namePosition.y(),
		width() - left - right,
		width() - left - right);

	p.setPen(paintOver ? _st.statusFgOver : _st.statusFg);
	p.setFont(st::contactsStatusFont);
	p.drawTextLeft(
		left,
		_st.statusPosition.y(),
		width() - left - right,
		_status);
}

class ShowButton final : public RpWidget {
public:
	ShowButton(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] rpl::producer<Qt::MouseButton> clicks() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	LinkButton _button;

};

ShowButton::ShowButton(not_null<Ui::RpWidget*> parent)
: RpWidget(parent)
, _button(this, tr::lng_usernames_activate_confirm(tr::now)) {
	_button.sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		resize(
			s.width() + st::emojiSuggestionsFadeRight.width(),
			s.height());
		_button.moveToRight(0, 0);
	}, lifetime());
	_button.show();
}

void ShowButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto clip = e->rect();

	const auto &icon = st::emojiSuggestionsFadeRight;
	const auto fade = QRect(0, 0, icon.width(), height());
	if (fade.intersects(clip)) {
		icon.fill(p, fade);
	}
	const auto fill = clip.intersected(
		{ icon.width(), 0, width() - icon.width(), height() });
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::boxBg);
	}
}

rpl::producer<Qt::MouseButton> ShowButton::clicks() const {
	return _button.clicks();
}

} // namespace

namespace Translate {

std::vector<QLocale> LocalesFromSettings() {
	const auto langs = Core::App().settings().skipTranslationForLanguages();
	if (langs.empty()) {
		return { QLocale(QLocale::English) };
	}
	return ranges::views::all(
		langs
	) | ranges::view::transform([](int langId) {
		const auto lang = QLocale::Language(langId);
		return (lang == QLocale::English)
			? QLocale(Lang::LanguageIdOrDefault(Lang::Id()))
			: (lang == QLocale::C)
			? QLocale(QLocale::English)
			: QLocale(lang);
	}) | ranges::to_vector;
}

} // namespace Translate

using namespace Translate;

QString LanguageName(const QLocale &locale) {
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

void TranslateBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId msgId,
		TextWithEntities text,
		bool hasCopyRestriction) {
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	const auto container = box->verticalLayout();
	const auto defaultId = LocalesFromSettings().front().name().mid(0, 2);

	const auto api = box->lifetime().make_state<MTP::Sender>(
		&peer->session().mtp());
	struct State {
		rpl::event_stream<QLocale> locale;
	};
	const auto state = box->lifetime().make_state<State>();

	text.entities = ranges::views::all(
		text.entities
	) | ranges::views::filter([](const EntityInText &e) {
		return e.type() != EntityType::Spoiler;
	}) | ranges::to<EntitiesInText>();

	if (!IsServerMsgId(msgId)) {
		msgId = 0;
	}

	using Flag = MTPmessages_translateText::Flag;
	const auto flags = msgId
		? (Flag::f_peer | Flag::f_msg_id)
		: !text.text.isEmpty()
		? Flag::f_text
		: Flag(0);

	const auto &stLabel = st::aboutLabel;
	const auto lineHeight = stLabel.style.lineHeight;

	Settings::AddSkip(container);
	// Settings::AddSubsectionTitle(
	// 	container,
	// 	tr::lng_translate_box_original());

	const auto original = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	{
		if (hasCopyRestriction) {
			original->entity()->setContextMenuHook([](auto&&) {
			});
		}
		original->entity()->setMarkedText(
			text,
			Core::MarkedTextContext{
				.session = &peer->session(),
				.customEmojiRepaint = [=] { original->entity()->update(); },
			});
		original->setMinimalHeight(lineHeight);
		original->hide(anim::type::instant);

		const auto show = Ui::CreateChild<FadeWrap<ShowButton>>(
			container.get(),
			object_ptr<ShowButton>(container));
		show->hide(anim::type::instant);
		rpl::combine(
			container->widthValue(),
			original->geometryValue()
		) | rpl::start_with_next([=](int width, const QRect &rect) {
			show->moveToLeft(
				width - show->width() - st::boxRowPadding.right(),
				rect.y() + std::abs(lineHeight - show->height()) / 2);
		}, show->lifetime());
		original->entity()->heightValue(
		) | rpl::filter([](int height) {
			return height > 0;
		}) | rpl::take(1) | rpl::start_with_next([=](int height) {
			if (height > lineHeight) {
				show->show(anim::type::instant);
			}
		}, show->lifetime());
		show->toggleOn(show->entity()->clicks() | rpl::map_to(false));
		original->toggleOn(show->entity()->clicks() | rpl::map_to(true));
	}
	Settings::AddSkip(container);
	Settings::AddSkip(container);
	Settings::AddDivider(container);
	Settings::AddSkip(container);

	{
		const auto padding = st::settingsSubsectionTitlePadding;
		const auto subtitle = Settings::AddSubsectionTitle(
			container,
			state->locale.events() | rpl::map(LanguageName));

		// Workaround.
		state->locale.events(
		) | rpl::start_with_next([=] {
			subtitle->resizeToWidth(container->width()
				- padding.left()
				- padding.right());
		}, subtitle->lifetime());
	}

	const auto translated = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	translated->entity()->setSelectable(!hasCopyRestriction);
	translated->hide(anim::type::instant);

	constexpr auto kMaxLines = 3;
	container->resizeToWidth(box->width());
	const auto loading = box->addRow(object_ptr<SlideWrap<RpWidget>>(
		box,
		CreateLoadingTextWidget(
			box,
			st::aboutLabel,
			std::min(original->entity()->height() / lineHeight, kMaxLines),
			state->locale.events() | rpl::map([=](const QLocale &locale) {
				return locale.textDirection() == Qt::RightToLeft;
			}))));
	loading->show(anim::type::instant);

	const auto showText = [=](const QString &text) {
		translated->entity()->setText(text);
		translated->show(anim::type::instant);
		loading->hide(anim::type::instant);
	};

	const auto send = [=](const QString &toLang) {
		api->request(MTPmessages_TranslateText(
			MTP_flags(flags),
			msgId ? peer->input : MTP_inputPeerEmpty(),
			MTP_int(msgId),
			MTP_string(text.text),
			MTPstring(),
			MTP_string(toLang)
		)).done([=](const MTPmessages_TranslatedText &result) {
			const auto text = result.match([](
					const MTPDmessages_translateNoResult &data) {
				return tr::lng_translate_box_error(tr::now);
			}, [](const MTPDmessages_translateResultText &data) {
				return qs(data.vtext());
			});
			showText(text);
		}).fail([=](const MTP::Error &error) {
			showText(tr::lng_translate_box_error(tr::now));
		}).send();
	};
	send(defaultId);
	state->locale.fire(QLocale(defaultId));

	box->addLeftButton(tr::lng_settings_language(), [=] {
		if (loading->toggled()) {
			return;
		}
		Ui::BoxShow(box).showBox(Box(ChooseLanguageBox, [=](
				std::vector<QLocale> locales) {
			const auto &locale = locales.front();
			state->locale.fire_copy(locale);
			loading->show(anim::type::instant);
			translated->hide(anim::type::instant);
			send(locale.name().mid(0, 2));
		}, std::vector<QLocale>()));
	});
}

void ChooseLanguageBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(std::vector<QLocale>)> callback,
		std::vector<QLocale> toggled) {
	box->setMinHeight(st::boxWidth);
	box->setMaxHeight(st::boxWidth);
	box->setTitle(tr::lng_languages());

	const auto hasToggled = !toggled.empty();

	const auto multiSelect = box->setPinnedToTopContent(
		object_ptr<Ui::MultiSelect>(
			box,
			st::defaultMultiSelect,
			tr::lng_participant_filter()));
	box->setFocusCallback([=] { multiSelect->setInnerFocus(); });

	const auto container = box->verticalLayout();
	const auto langs = [&] {
		auto langs = Languages();
		const auto current = QLocale(
			Lang::LanguageIdOrDefault(Lang::Id())).language();
		if (const auto it = ranges::find(langs, current); it != end(langs)) {
			base::reorder(langs, std::distance(begin(langs), it), 0);
		}
		return langs;
	}();
	auto rows = std::vector<not_null<Ui::SlideWrap<Row>*>>();
	rows.reserve(langs.size());
	for (const auto &lang : langs) {
		const auto locale = QLocale(lang);
		const auto button = container->add(
			object_ptr<Ui::SlideWrap<Row>>(
				container,
				object_ptr<Row>(container, locale)));
		if (hasToggled) {
			button->entity()->toggleOn(
				rpl::single(ranges::contains(toggled, locale)),
				false);
		} else {
			button->entity()->setClickedCallback([=] {
				callback({ locale });
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
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
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

	if (hasToggled) {
		box->addButton(tr::lng_settings_save(), [=] {
			auto result = ranges::views::all(
				rows
			) | ranges::views::filter([](const auto &row) {
				return row->entity()->toggled();
			}) | ranges::views::transform([](const auto &row) {
				return row->entity()->locale();
			}) | ranges::to_vector;
			if (!result.empty()) {
				callback(std::move(result));
			}
			box->closeBox();
		});
	}
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

bool SkipTranslate(TextWithEntities textWithEntities) {
	const auto &text = textWithEntities.text;
	if (text.isEmpty()) {
		return true;
	}
	if (!Core::App().settings().translateButtonEnabled()) {
		return true;
	}
	constexpr auto kFirstChunk = size_t(100);
	auto hasLetters = (text.size() >= kFirstChunk);
	for (auto i = 0; i < kFirstChunk; i++) {
		if (i >= text.size()) {
			break;
		}
		if (text.at(i).isLetter()) {
			hasLetters = true;
			break;
		}
	}
	if (!hasLetters) {
		return true;
	}
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto result = Platform::Language::Recognize(text);
	if (result.unknown) {
		return false;
	}
	return ranges::any_of(LocalesFromSettings(), [&](const QLocale &l) {
		return result.locale.language() == l.language();
	});
#else
    return false;
#endif
}

} // namespace Ui
