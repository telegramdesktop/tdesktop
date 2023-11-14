/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_reactions.h"

#include "base/event_filter.h"
#include "boxes/reactions_settings_box.h" // AddReactionAnimatedIcon
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/controls/emoji_button.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

#include <QtWidgets/QTextEdit>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocumentFragment>

namespace {

[[nodiscard]] QString AllowOnlyCustomEmojiProcessor(QStringView mimeTag) {
	auto all = TextUtilities::SplitTags(mimeTag);
	for (auto i = all.begin(); i != all.end();) {
		if (Ui::InputField::IsCustomEmojiLink(*i)) {
			++i;
		} else {
			i = all.erase(i);
		}
	}
	return TextUtilities::JoinTag(all);
}

[[nodiscard]] bool AllowOnlyCustomEmojiMimeDataHook(
		not_null<const QMimeData*> data,
		Ui::InputField::MimeAction action) {
	if (action == Ui::InputField::MimeAction::Check) {
		const auto textMime = TextUtilities::TagsTextMimeType();
		const auto tagsMime = TextUtilities::TagsMimeType();
		if (!data->hasFormat(textMime) || !data->hasFormat(tagsMime)) {
			return false;
		}
		auto text = QString::fromUtf8(data->data(textMime));
		auto tags = TextUtilities::DeserializeTags(
			data->data(tagsMime),
			text.size());
		auto checkedTill = 0;
		ranges::sort(tags, ranges::less(), &TextWithTags::Tag::offset);
		for (const auto &tag : tags) {
			if (tag.offset != checkedTill
				|| AllowOnlyCustomEmojiProcessor(tag.id) != tag.id) {
				return false;
			}
			checkedTill += tag.length;
		}
		return true;
	} else if (action == Ui::InputField::MimeAction::Insert) {
		return false;
	}
	Unexpected("Action in MimeData hook.");
}

struct UniqueCustomEmojiContext {
	base::flat_set<uint64> ids;
};

[[nodiscard]] bool RemoveNonCustomEmojiFragment(
		not_null<QTextDocument*> document,
		UniqueCustomEmojiContext &context) {
	context.ids.clear();
	auto removeFrom = 0;
	auto removeTill = 0;
	auto block = document->begin();
	for (auto j = block.begin(); !j.atEnd(); ++j) {
		const auto fragment = j.fragment();
		Assert(fragment.isValid());

		removeTill = removeFrom = fragment.position();
		const auto format = fragment.charFormat();
		if (format.objectType() != Ui::InputField::kCustomEmojiFormat) {
			removeTill += fragment.length();
			break;
		}
		const auto id = format.property(Ui::InputField::kCustomEmojiId);
		if (!context.ids.emplace(id.toULongLong()).second) {
			removeTill += fragment.length();
			break;
		}
	}
	while (removeTill == removeFrom) {
		block = block.next();
		if (block == document->end()) {
			return false;
		}
		removeTill = block.position();
	}
	Ui::PrepareFormattingOptimization(document);
	auto cursor = QTextCursor(document);
	cursor.setPosition(removeFrom);
	cursor.setPosition(removeTill, QTextCursor::KeepAnchor);
	cursor.removeSelectedText();
	return true;
}

bool RemoveNonCustomEmoji(
		not_null<QTextDocument*> document,
		UniqueCustomEmojiContext &context) {
	if (!RemoveNonCustomEmojiFragment(document, context)) {
		return false;
	}
	while (RemoveNonCustomEmojiFragment(document, context)) {
	}
	return true;
}

void SetupOnlyCustomEmojiField(not_null<Ui::InputField*> field) {
	field->setTagMimeProcessor(AllowOnlyCustomEmojiProcessor);
	field->setMimeDataHook(AllowOnlyCustomEmojiMimeDataHook);

	struct State {
		bool processing = false;
		bool pending = false;
	};
	const auto state = field->lifetime().make_state<State>();

	field->changes(
	) | rpl::start_with_next([=] {
		state->pending = true;
		if (state->processing) {
			return;
		}
		auto context = UniqueCustomEmojiContext();
		auto changed = false;
		state->processing = true;
		while (state->pending) {
			state->pending = false;
			const auto document = field->rawTextEdit()->document();
			const auto pageSize = document->pageSize();
			QTextCursor(document).joinPreviousEditBlock();
			if (RemoveNonCustomEmoji(document, context)) {
				changed = true;
			}
			state->processing = false;
			QTextCursor(document).endEditBlock();
			if (document->pageSize() != pageSize) {
				document->setPageSize(pageSize);
			}
		}
		if (changed) {
			field->forceProcessContentsChanges();
		}
	}, field->lifetime());
}

struct ReactionsSelectorArgs {
	not_null<QWidget*> outer;
	not_null<Window::SessionController*> controller;
	rpl::producer<QString> title;
	std::vector<Data::Reaction> list;
	std::vector<Data::ReactionId> selected;
	Fn<void(std::vector<Data::ReactionId>)> callback;
	rpl::producer<> focusRequests;
};

object_ptr<Ui::RpWidget> AddReactionsSelector(
		not_null<Ui::RpWidget*> parent,
		ReactionsSelectorArgs &&args) {
	using namespace ChatHelpers;

	auto result = object_ptr<Ui::InputField>(
		parent,
		st::manageGroupReactionsField,
		Ui::InputField::Mode::MultiLine,
		std::move(args.title));
	const auto raw = result.data();

	const auto customEmojiPaused = [controller = args.controller] {
		return controller->isGifPausedAtLeastFor(PauseReason::Layer);
	};
	raw->setCustomEmojiFactory(
		args.controller->session().data().customEmojiManager().factory(),
		std::move(customEmojiPaused));

	SetupOnlyCustomEmojiField(raw);

	std::move(args.focusRequests) | rpl::start_with_next([=] {
		raw->setFocusFast();
	}, raw->lifetime());

	const auto toggle = Ui::CreateChild<Ui::EmojiButton>(
		parent.get(),
		st::boxAttachEmoji);

	const auto panel = Ui::CreateChild<TabbedPanel>(
		args.outer.get(),
		args.controller,
		object_ptr<TabbedSelector>(
			nullptr,
			args.controller->uiShow(),
			Window::GifPauseReason::Layer,
			TabbedSelector::Mode::EmojiOnly));
	panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	panel->hide();
	panel->selector()->customEmojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		Data::InsertCustomEmoji(raw, data.document);
	}, panel->lifetime());

	const auto updateEmojiPanelGeometry = [=] {
		const auto parent = panel->parentWidget();
		const auto global = toggle->mapToGlobal({ 0, 0 });
		const auto local = parent->mapFromGlobal(global);
		panel->moveBottomRight(
			local.y(),
			local.x() + toggle->width() * 3);
	};
	const auto scheduleUpdateEmojiPanelGeometry = [=] {
		// updateEmojiPanelGeometry uses not only container geometry, but
		// also container children geometries that will be updated later.
		crl::on_main(raw, updateEmojiPanelGeometry);
	};
	const auto filterCallback = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Move || type == QEvent::Resize) {
			scheduleUpdateEmojiPanelGeometry();
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(args.outer, filterCallback);

	toggle->installEventFilter(panel);
	toggle->addClickHandler([=] {
		panel->toggleAnimated();
	});

	raw->geometryValue() | rpl::start_with_next([=](QRect geometry) {
		toggle->move(
			geometry.x() + geometry.width() - toggle->width(),
			geometry.y() + geometry.height() - toggle->height());
		updateEmojiPanelGeometry();
	}, toggle->lifetime());

	return result;
}

} // namespace

void EditAllowedReactionsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		bool isGroup,
		const std::vector<Data::Reaction> &list,
		const Data::AllowedReactions &allowed,
		Fn<void(const Data::AllowedReactions &)> callback) {
	using namespace Data;
	using namespace rpl::mappers;

	const auto iconHeight = st::editPeerReactionsPreview;
	box->setTitle(tr::lng_manage_peer_reactions());

	enum class Option {
		All,
		Some,
		None,
	};
	struct State {
		base::flat_map<ReactionId, not_null<Ui::SettingsButton*>> toggles;
		rpl::variable<Option> option; // For groups.
		rpl::variable<bool> anyToggled; // For channels.
		rpl::event_stream<bool> forceToggleAll; // For channels.
		rpl::event_stream<> focusRequests;
	};
	const auto optionInitial = (allowed.type != AllowedReactionsType::Some)
		? Option::All
		: allowed.some.empty()
		? Option::None
		: Option::Some;
	const auto state = box->lifetime().make_state<State>(State{
		.option = optionInitial,
		.anyToggled = (optionInitial != Option::None),
	});

	const auto collect = [=] {
		auto result = AllowedReactions();
		if (!isGroup || state->option.current() == Option::Some) {
			result.some.reserve(state->toggles.size());
			for (const auto &[id, button] : state->toggles) {
				if (button->toggled()) {
					result.some.push_back(id);
				}
			}
		}
		result.type = isGroup
			? (state->option.current() != Option::All
				? AllowedReactionsType::Some
				: AllowedReactionsType::All)
			: (result.some.size() == state->toggles.size())
			? AllowedReactionsType::Default
			: AllowedReactionsType::Some;
		return result;
	};

	const auto container = box->verticalLayout();

	const auto enabled = isGroup
		? nullptr
		: container->add(object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_manage_peer_reactions_enable(),
			st::manageGroupButton.button));
	if (enabled && !list.empty()) {
		AddReactionAnimatedIcon(
			enabled,
			enabled->sizeValue(
			) | rpl::map([=](const QSize &size) {
				return QPoint(
					st::manageGroupButton.iconPosition.x(),
					(size.height() - iconHeight) / 2);
			}),
			iconHeight,
			list.front(),
			rpl::never<>(),
			rpl::never<>(),
			&enabled->lifetime());
	}
	if (enabled) {
		enabled->toggleOn(state->anyToggled.value());
		enabled->toggledChanges(
		) | rpl::filter([=](bool value) {
			return (value != state->anyToggled.current());
		}) | rpl::start_to_stream(state->forceToggleAll, enabled->lifetime());
	}
	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>(
		state->option.current());
	group->setChangedCallback([=](Option value) {
		state->option = value;
	});
	const auto addOption = [&](Option option, const QString &text) {
		if (!isGroup) {
			return;
		}
		container->add(
			object_ptr<Ui::Radioenum<Option>>(
				container,
				group,
				option,
				text,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	addOption(Option::All, tr::lng_manage_peer_reactions_all(tr::now));
	addOption(Option::Some, tr::lng_manage_peer_reactions_some(tr::now));
	addOption(Option::None, tr::lng_manage_peer_reactions_none(tr::now));

	const auto about = [](Option option) {
		switch (option) {
		case Option::All: return tr::lng_manage_peer_reactions_all_about();
		case Option::Some: return tr::lng_manage_peer_reactions_some_about();
		case Option::None: return tr::lng_manage_peer_reactions_none_about();
		}
		Unexpected("Option value in EditAllowedReactionsBox.");
	};
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		(isGroup
			? (state->option.value()
				| rpl::map(about)
				| rpl::flatten_latest())
			: tr::lng_manage_peer_reactions_about_channel()));

	const auto wrap = enabled ? nullptr : container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	if (wrap) {
		wrap->toggleOn(state->option.value() | rpl::map(_1 == Option::Some));
		wrap->finishAnimating();
	}
	const auto reactions = wrap ? wrap->entity() : container.get();

	Ui::AddSkip(reactions);

	const auto like = QString::fromUtf8("\xf0\x9f\x91\x8d");
	const auto dislike = QString::fromUtf8("\xf0\x9f\x91\x8e");
	auto selected = allowed.some;
	if (selected.empty()) {
		selected.push_back(Data::ReactionId(like));
		selected.push_back(Data::ReactionId(dislike));
	}
	const auto changed = [=](std::vector<Data::ReactionId> chosen) {
	};
	reactions->add(AddReactionsSelector(reactions, {
		.outer = box->getDelegate()->outerContainer(),
		.controller = navigation->parentController(),
		.title = (enabled
			? tr::lng_manage_peer_reactions_available()
			: tr::lng_manage_peer_reactions_some_title()),
		.list = list,
		.selected = std::move(selected),
		.callback = changed,
		.focusRequests = state->focusRequests.events(),
	}), st::boxRowPadding);

	box->setFocusCallback([=] {
		if (!wrap || state->option.current() == Option::Some) {
			state->focusRequests.fire({});
		}
	});

	box->addButton(tr::lng_settings_save(), [=] {
		const auto result = collect();
		box->closeBox();
		callback(result);
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void SaveAllowedReactions(
		not_null<PeerData*> peer,
		const Data::AllowedReactions &allowed) {
	auto ids = allowed.some | ranges::views::transform(
		Data::ReactionToMTP
	) | ranges::to<QVector<MTPReaction>>;

	using Type = Data::AllowedReactionsType;
	const auto updated = (allowed.type != Type::Some)
		? MTP_chatReactionsAll(MTP_flags((allowed.type == Type::Default)
			? MTPDchatReactionsAll::Flag(0)
			: MTPDchatReactionsAll::Flag::f_allow_custom))
		: allowed.some.empty()
		? MTP_chatReactionsNone()
		: MTP_chatReactionsSome(MTP_vector<MTPReaction>(ids));
	peer->session().api().request(MTPmessages_SetChatAvailableReactions(
		peer->input,
		updated
	)).done([=](const MTPUpdates &result) {
		peer->session().api().applyUpdates(result);
		if (const auto chat = peer->asChat()) {
			chat->setAllowedReactions(Data::Parse(updated));
		} else if (const auto channel = peer->asChannel()) {
			channel->setAllowedReactions(Data::Parse(updated));
		} else {
			Unexpected("Invalid peer type in SaveAllowedReactions.");
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"REACTION_INVALID"_q) {
			peer->updateFullForced();
			peer->owner().reactions().refreshDefault();
		}
	}).send();
}
