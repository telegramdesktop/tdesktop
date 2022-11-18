/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/translate_box.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_peer.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "settings/settings_common.h"
#include "spellcheck/platform/platform_language.h"
#include "ui/effects/loading_element.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h" // settingsSubsectionTitlePadding.

#include <QLocale>

namespace Ui {
namespace {

[[nodiscard]] std::vector<QLocale::Language> Languages() {
	return std::vector<QLocale::Language>{
		QLocale::English,
		QLocale::Albanian,
		QLocale::Armenian,
		QLocale::Bulgarian,
		QLocale::Catalan,
		QLocale::Chinese,
		QLocale::Croatian,
		QLocale::Czech,
		QLocale::Danish,
		QLocale::Dutch,
		QLocale::Estonian,
		QLocale::French,
		QLocale::German,
		QLocale::Greek,
		QLocale::Hebrew,
		QLocale::Hindi,
		QLocale::Hungarian,
		QLocale::Indonesian,
		QLocale::Italian,
		QLocale::Japanese,
		QLocale::Korean,
		QLocale::Latvian,
		QLocale::Lithuanian,
		QLocale::Persian,
		QLocale::Polish,
		QLocale::Portuguese,
		QLocale::Romanian,
		QLocale::Russian,
		QLocale::Serbian,
		QLocale::Slovak,
		QLocale::Slovenian,
		QLocale::Spanish,
		QLocale::Swedish,
		QLocale::Tajik,
		QLocale::Tamil,
		QLocale::Turkish,
		QLocale::Ukrainian,
		QLocale::Vietnamese,
		QLocale::Welsh,
	};
}

class ShowButton : public RpWidget {
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

QString LanguageName(const QLocale &locale) {
	if (locale.language() == QLocale::English
			&& (locale.country() == QLocale::UnitedStates
				|| locale.country() == QLocale::AnyCountry)) {
		return u"English"_q;
	} else {
		const auto name = locale.nativeLanguageName();
		return name.left(1).toUpper() + name.mid(1);
	}
}

void TranslateBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId msgId,
		TextWithEntities text) {
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	const auto container = box->verticalLayout();
	const auto defaultId = QLocale(
		Core::App().settings().skipTranslationForLanguage()).name().mid(0, 2);

	const auto api = box->lifetime().make_state<MTP::Sender>(
		&peer->session().mtp());
	struct State {
		rpl::event_stream<QLocale> locale;
	};
	const auto state = box->lifetime().make_state<State>();

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
		original->entity()->setMarkedText(text);
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
	translated->entity()->setSelectable(true);
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
		Ui::BoxShow(box).showBox(Box(ChooseLanguageBox, [=](QLocale locale) {
			state->locale.fire_copy(locale);
			loading->show(anim::type::instant);
			translated->hide(anim::type::instant);
			send(locale.name().mid(0, 2));
		}));
	});

}

void ChooseLanguageBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(QLocale)> callback) {
	box->setTitle(tr::lng_languages());
	for (const auto &lang : Languages()) {
		const auto locale = QLocale(lang);
		const auto button = Settings::AddButton(
			box->verticalLayout(),
			rpl::single(LanguageName(locale)),
			st::defaultSettingsButton);
		button->setClickedCallback([=] {
			callback(locale);
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
	auto hasLetters = false;
	constexpr auto kFirstChunk = 100;
	for (auto i = 0; i < kFirstChunk; i++) {
		if (i >= text.size()) {
			hasLetters = true; // Rest characters are unknown.
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
	const auto result = Platform::Language::Recognize(text);
	if (result.unknown) {
		return false;
	}
	const auto skip = Core::App().settings().skipTranslationForLanguage();
	return (result.locale.language() == skip);
}

} // namespace Ui
