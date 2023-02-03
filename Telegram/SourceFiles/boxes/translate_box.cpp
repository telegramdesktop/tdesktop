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
#include "spellcheck/platform/platform_language.h"
#include "ui/boxes/choose_language_box.h"
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

void TranslateBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId msgId,
		TextWithEntities text,
		bool hasCopyRestriction) {
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	const auto container = box->verticalLayout();

	auto id = Core::App().settings().translateToValue();
	const auto api = box->lifetime().make_state<MTP::Sender>(
		&peer->session().mtp());

	text.entities = ranges::views::all(
		text.entities
	) | ranges::views::filter([](const EntityInText &e) {
		return e.type() != EntityType::Spoiler;
	}) | ranges::to<EntitiesInText>();

	if (!IsServerMsgId(msgId)) {
		msgId = 0;
	}

	using Flag = MTPmessages_TranslateText::Flag;
	const auto flags = msgId
		? (Flag::f_peer | Flag::f_id)
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
			rpl::duplicate(id) | rpl::map(LanguageName));

		// Workaround.
		rpl::duplicate(id) | rpl::start_with_next([=] {
			subtitle->resizeToWidth(container->width()
				- padding.left()
				- padding.right());
		}, subtitle->lifetime());
	}

	const auto translated = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	translated->entity()->setSelectable(!hasCopyRestriction);

	constexpr auto kMaxLines = 3;
	container->resizeToWidth(box->width());
	const auto loading = box->addRow(object_ptr<SlideWrap<RpWidget>>(
		box,
		CreateLoadingTextWidget(
			box,
			st::aboutLabel,
			std::min(original->entity()->height() / lineHeight, kMaxLines),
			rpl::duplicate(id) | rpl::map([=](LanguageId id) {
				return id.locale().textDirection() == Qt::RightToLeft;
			}))));

	const auto showText = [=](const QString &text) {
		translated->entity()->setText(text);
		translated->show(anim::type::instant);
		loading->hide(anim::type::instant);
	};

	const auto send = [=](LanguageId to) {
		loading->show(anim::type::instant);
		translated->hide(anim::type::instant);
		api->request(MTPmessages_TranslateText(
			MTP_flags(flags),
			msgId ? peer->input : MTP_inputPeerEmpty(),
			(msgId
				? MTP_vector<MTPint>(1, MTP_int(msgId))
				: MTPVector<MTPint>()),
			(msgId
				? MTPVector<MTPTextWithEntities>()
				: MTP_vector<MTPTextWithEntities>(1, MTP_textWithEntities(
					MTP_string(text.text),
					MTP_vector<MTPMessageEntity>()))),
			MTP_string(to.locale().name().mid(0, 2))
		)).done([=](const MTPmessages_TranslatedText &result) {
			const auto &data = result.data();
			const auto &list = data.vresult().v;
			showText(list.isEmpty()
				? tr::lng_translate_box_error(tr::now)
				: qs(list.front().data().vtext()));
		}).fail([=](const MTP::Error &error) {
			showText(tr::lng_translate_box_error(tr::now));
		}).send();
	};
	std::move(id) | rpl::start_with_next(send, box->lifetime());

	box->addLeftButton(tr::lng_settings_language(), [=] {
		if (loading->toggled()) {
			return;
		}
		Ui::BoxShow(box).showBox(ChooseTranslateToBox());
	});
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
	const auto skip = Core::App().settings().skipTranslationLanguages();
	return result.known() && ranges::contains(skip, result);
#else
    return false;
#endif
}

object_ptr<BoxContent> EditSkipTranslationLanguages() {
	auto title = tr::lng_translate_settings_choose();
	return Box(ChooseLanguageBox, std::move(title), [=](
			std::vector<LanguageId> &&list) {
		Core::App().settings().setSkipTranslationLanguages(
			std::move(list));
		Core::App().saveSettingsDelayed();
	}, Core::App().settings().skipTranslationLanguages(), true);
}

object_ptr<BoxContent> ChooseTranslateToBox() {
	const auto selected = std::vector<LanguageId>{
		Core::App().settings().translateTo(),
	};
	return Box(ChooseLanguageBox, tr::lng_languages(), [=](
			const std::vector<LanguageId> &ids) {
		Expects(!ids.empty());

		Core::App().settings().setTranslateTo(ids.front());
		Core::App().saveSettingsDelayed();
	}, selected, false);
}

} // namespace Ui
