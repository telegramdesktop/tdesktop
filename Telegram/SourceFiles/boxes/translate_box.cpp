/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/translate_box.h"

#include "api/api_text_entities.h" // Api::EntitiesToMTP / EntitiesFromMTP.
#include "core/application.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "spellcheck/platform/platform_language.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/effects/loading_element.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h" // inviteLinkListItem.
#include "styles/style_layers.h"

#include <QLocale>

namespace Ui {
namespace {

constexpr auto kSkipAtLeastOneDuration = 3 * crl::time(1000);

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
			s.width() + st::defaultEmojiSuggestions.fadeRight.width(),
			s.height());
		_button.moveToRight(0, 0);
	}, lifetime());
	_button.show();
}

void ShowButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto clip = e->rect();

	const auto &icon = st::defaultEmojiSuggestions.fadeRight;
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

	struct State {
		State(not_null<Main::Session*> session) : api(&session->mtp()) {
		}

		MTP::Sender api;
		rpl::variable<LanguageId> to;
	};
	const auto state = box->lifetime().make_state<State>(&peer->session());
	state->to = ChooseTranslateTo(peer->owner().history(peer));

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

	Ui::AddSkip(container);
	// Ui::AddSubsectionTitle(
	// 	container,
	// 	tr::lng_translate_box_original());

	const auto animationsPaused = [] {
		using Which = FlatLabel::WhichAnimationsPaused;
		const auto emoji = On(PowerSaving::kEmojiChat);
		const auto spoiler = On(PowerSaving::kChatSpoiler);
		return emoji
			? (spoiler ? Which::All : Which::CustomEmoji)
			: (spoiler ? Which::Spoiler : Which::None);
	};
	const auto original = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	{
		if (hasCopyRestriction) {
			original->entity()->setContextMenuHook([](auto&&) {
			});
		}
		original->entity()->setAnimationsPausedCallback(animationsPaused);
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
	Ui::AddSkip(container);
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	{
		const auto padding = st::defaultSubsectionTitlePadding;
		const auto subtitle = Ui::AddSubsectionTitle(
			container,
			state->to.value() | rpl::map(LanguageName));

		// Workaround.
		state->to.value() | rpl::start_with_next([=] {
			subtitle->resizeToWidth(container->width()
				- padding.left()
				- padding.right());
		}, subtitle->lifetime());
	}

	const auto translated = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	translated->entity()->setSelectable(!hasCopyRestriction);
	translated->entity()->setAnimationsPausedCallback(animationsPaused);

	constexpr auto kMaxLines = 3;
	container->resizeToWidth(box->width());
	const auto loading = box->addRow(object_ptr<SlideWrap<RpWidget>>(
		box,
		CreateLoadingTextWidget(
			box,
			st::aboutLabel,
			std::min(original->entity()->height() / lineHeight, kMaxLines),
			state->to.value() | rpl::map([=](LanguageId id) {
				return id.locale().textDirection() == Qt::RightToLeft;
			}))));

	const auto showText = [=](TextWithEntities text) {
		const auto label = translated->entity();
		label->setMarkedText(
			text,
			Core::MarkedTextContext{
				.session = &peer->session(),
				.customEmojiRepaint = [=] { label->update(); },
			});
		translated->show(anim::type::instant);
		loading->hide(anim::type::instant);
	};

	const auto send = [=](LanguageId to) {
		loading->show(anim::type::instant);
		translated->hide(anim::type::instant);
		state->api.request(MTPmessages_TranslateText(
			MTP_flags(flags),
			msgId ? peer->input : MTP_inputPeerEmpty(),
			(msgId
				? MTP_vector<MTPint>(1, MTP_int(msgId))
				: MTPVector<MTPint>()),
			(msgId
				? MTPVector<MTPTextWithEntities>()
				: MTP_vector<MTPTextWithEntities>(1, MTP_textWithEntities(
					MTP_string(text.text),
					Api::EntitiesToMTP(
						&peer->session(),
						text.entities,
						Api::ConvertOption::SkipLocal)))),
			MTP_string(to.twoLetterCode())
		)).done([=](const MTPmessages_TranslatedText &result) {
			const auto &data = result.data();
			const auto &list = data.vresult().v;
			if (list.isEmpty()) {
				showText(
					Ui::Text::Italic(tr::lng_translate_box_error(tr::now)));
			} else {
				showText(TextWithEntities{
					.text = qs(list.front().data().vtext()),
					.entities = Api::EntitiesFromMTP(
						&peer->session(),
						list.front().data().ventities().v),
				});
			}
		}).fail([=](const MTP::Error &error) {
			showText(
				Ui::Text::Italic(tr::lng_translate_box_error(tr::now)));
		}).send();
	};
	state->to.value() | rpl::start_with_next(send, box->lifetime());

	box->addLeftButton(tr::lng_settings_language(), [=] {
		if (loading->toggled()) {
			return;
		}
		box->uiShow()->showBox(ChooseTranslateToBox(
			state->to.current(),
			crl::guard(box, [=](LanguageId id) { state->to = id; })));
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
	const auto selected = std::make_shared<std::vector<LanguageId>>(
		Core::App().settings().skipTranslationLanguages());
	const auto weak = std::make_shared<QPointer<BoxContent>>();
	const auto check = [=](LanguageId id) {
		const auto already = ranges::contains(*selected, id);
		if (already) {
			selected->erase(ranges::remove(*selected, id), selected->end());
		} else {
			selected->push_back(id);
		}
		if (already && selected->empty()) {
			if (const auto strong = weak->data()) {
				strong->showToast(
					tr::lng_translate_settings_one(tr::now),
					kSkipAtLeastOneDuration);
			}
			return false;
		}
		return true;
	};
	auto result = Box(ChooseLanguageBox, std::move(title), [=](
			std::vector<LanguageId> &&list) {
		Core::App().settings().setSkipTranslationLanguages(
			std::move(list));
		Core::App().saveSettingsDelayed();
	}, *selected, true, check);
	*weak = result.data();
	return result;
}

object_ptr<BoxContent> ChooseTranslateToBox(
		LanguageId bringUp,
		Fn<void(LanguageId)> callback) {
	auto &settings = Core::App().settings();
	auto selected = std::vector<LanguageId>{
		settings.translateTo(),
	};
	for (const auto &id : settings.skipTranslationLanguages()) {
		if (id != selected.front()) {
			selected.push_back(id);
		}
	}
	if (bringUp && ranges::contains(selected, bringUp)) {
		selected.push_back(bringUp);
	}
	return Box(ChooseLanguageBox, tr::lng_languages(), [=](
			const std::vector<LanguageId> &ids) {
		Expects(!ids.empty());

		const auto id = ids.front();
		Core::App().settings().setTranslateTo(id);
		Core::App().saveSettingsDelayed();
		callback(id);
	}, selected, false, nullptr);
}

LanguageId ChooseTranslateTo(not_null<History*> history) {
	return ChooseTranslateTo(history->translateOfferedFrom());
}

LanguageId ChooseTranslateTo(LanguageId offeredFrom) {
	auto &settings = Core::App().settings();
	return ChooseTranslateTo(
		offeredFrom,
		settings.translateTo(),
		settings.skipTranslationLanguages());
}

LanguageId ChooseTranslateTo(
		not_null<History*> history,
		LanguageId savedTo,
		const std::vector<LanguageId> &skip) {
	return ChooseTranslateTo(history->translateOfferedFrom(), savedTo, skip);
}

LanguageId ChooseTranslateTo(
		LanguageId offeredFrom,
		LanguageId savedTo,
		const std::vector<LanguageId> &skip) {
	return (offeredFrom != savedTo) ? savedTo : skip.front();
}

} // namespace Ui
