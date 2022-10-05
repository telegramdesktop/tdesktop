/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_forum_topic_box.h"

#include "ui/widgets/input_fields.h"
#include "ui/abstract_button.h"
#include "ui/color_int_conversion.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "base/random.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/view/history_view_replies_section.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "window/window_session_controller.h"
#include "settings/settings_common.h"
#include "apiwrap.h"
#include "styles/style_dialogs.h"

namespace {

[[nodiscard]] int EditIconSize() {
	const auto tag = Data::CustomEmojiManager::SizeTag::Large;
	return Data::FrameSizeFromTag(tag) / style::DevicePixelRatio();
}

[[nodiscard]] rpl::producer<DocumentId> EditIconButton(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		int32 colorId,
		DocumentId iconId) {
	using namespace Info::Profile;
	struct State {
		rpl::variable<DocumentId> iconId;
		EmojiStatusPanel panel;
		std::unique_ptr<Ui::Text::CustomEmoji> chosen;
		QImage empty;
	};
	const auto tag = Data::CustomEmojiManager::SizeTag::Large;
	const auto size = EditIconSize();
	const auto result = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	const auto state = result->lifetime().make_state<State>();
	state->empty = Data::ForumTopicIconBackground(
		colorId,
		st::largeForumTopicIcon.size);
	state->iconId.value(
	) | rpl::start_with_next([=](DocumentId iconId) {
		const auto owner = &controller->session().data();
		state->chosen = iconId
			? owner->customEmojiManager().create(
				iconId,
				[=] { result->update(); },
				tag)
			: nullptr;
		result->update();
	}, result->lifetime());
	state->iconId = iconId;
	state->panel.setChooseFilter([=](DocumentId) {
		return true;
	});
	state->panel.setChooseCallback([=](DocumentId id) {
		state->iconId = id;
	});
	result->resize(size, size);
	result->paintRequest(
	) | rpl::filter([=] {
		return !state->panel.paintBadgeFrame(result);
	}) | rpl::start_with_next([=](QRect clip) {
		auto args = Ui::Text::CustomEmoji::Context{
			.preview = st::windowBgOver->c,
			.now = crl::now(),
			.paused = controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer),
		};
		auto p = QPainter(result);
		if (state->chosen) {
			state->chosen->paint(p, args);
		} else {
			const auto skip = (size - st::largeForumTopicIcon.size) / 2;
			p.drawImage(skip, skip, state->empty);
		}
	}, result->lifetime());
	result->setClickedCallback([=] {
		state->panel.show(controller, result, tag);
	});
	return state->iconId.value();
}

} // namespace

void NewForumTopicBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<History*> forum) {
	EditForumTopicBox(box, controller, forum, MsgId(0));
}

void EditForumTopicBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<History*> forum,
		MsgId rootId) {
	const auto creating = !rootId;
	const auto topic = (!creating && forum->peer->forum())
		? forum->peer->forum()->topicFor(rootId)
		: nullptr;
	// #TODO forum lang
	box->setTitle(rpl::single(creating ? u"New topic"_q : u"Edit topic"_q));

	struct State {
		int32 colorId = 0;
		DocumentId iconId = 0;
		mtpRequestId requestId = 0;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto &colors = Data::ForumTopicColorIds();
	state->iconId = topic ? topic->iconId() : 0;
	state->colorId = topic
		? topic->colorId()
		: colors[base::RandomIndex(colors.size())];
	// #TODO forum lang and design
	Settings::AddSubsectionTitle(
		box->verticalLayout(),
		rpl::single(u"Topic Icon"_q));
	const auto badgeWrap = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, EditIconSize()));
	EditIconButton(
		controller,
		badgeWrap,
		state->colorId,
		state->iconId
	) | rpl::start_with_next([=](DocumentId id) {
		state->iconId = id;
	}, box->lifetime());

	const auto title = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Topic Title"_q),
			topic ? topic->title() : QString())); // #TODO forum lang
	box->setFocusCallback([=] {
		title->setFocusFast();
	});

	const auto requestId = std::make_shared<mtpRequestId>();
	const auto create = [=] {
		const auto channel = forum->peer->asChannel();
		if (!channel || !channel->isForum()) {
			box->closeBox();
			return;
		} else if (title->getLastText().trimmed().isEmpty()) {
			title->showError();
			return;
		}
		controller->showSection(
			std::make_shared<HistoryView::RepliesMemento>(
				forum,
				channel->forum()->reserveCreatingId(
					title->getLastText().trimmed(),
					state->colorId,
					state->iconId)),
			Window::SectionShow::Way::ClearStack);
	};

	const auto save = [=] {
		const auto parent = forum->peer->forum();
		const auto topic = parent
			? parent->topicFor(rootId)
			: nullptr;
		if (!topic) {
			box->closeBox();
			return;
		} else if (state->requestId > 0) {
			return;
		} else if (title->getLastText().trimmed().isEmpty()) {
			title->showError();
			return;
		} else if (parent->creating(rootId)) {
			topic->applyTitle(title->getLastText().trimmed());
			topic->applyColorId(state->colorId);
			topic->applyIconId(state->iconId);
		} else {
			using Flag = MTPchannels_EditForumTopic::Flag;
			const auto api = &forum->session().api();
			state->requestId = api->request(MTPchannels_EditForumTopic(
				MTP_flags(Flag::f_title | Flag::f_icon_emoji_id),
				topic->channel()->inputChannel,
				MTP_int(rootId),
				MTP_string(title->getLastText().trimmed()),
				MTP_long(state->iconId)
			)).done([=](const MTPUpdates &result) {
				api->applyUpdates(result);
				box->closeBox();
			}).fail([=](const MTP::Error &error) {
				if (error.type() == u"TOPIC_NOT_MODIFIED") {
					box->closeBox();
				} else {
					state->requestId = -1;
				}
			}).send();
		}
	};

	if (creating) {
		box->addButton(tr::lng_create_group_create(), create);
	} else {
		box->addButton(tr::lng_settings_save(), save);
	}
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}
