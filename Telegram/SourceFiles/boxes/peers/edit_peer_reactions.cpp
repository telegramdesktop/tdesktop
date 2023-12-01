/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_reactions.h"

#include "base/event_filter.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "ui/boxes/boost_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"

#include <QtWidgets/QTextEdit>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocumentFragment>

namespace {

constexpr auto kDisabledEmojiOpacity = 0.4;

struct UniqueCustomEmojiContext {
	std::vector<DocumentId> ids;
	Fn<bool(DocumentId)> applyHardLimit;
	int hardLimit = 0;
	int hardLimitChecked = 0;
	bool hardLimitHit = false;
};

class MaybeDisabledEmoji final : public Ui::Text::CustomEmoji {
public:
	MaybeDisabledEmoji(
		std::unique_ptr<CustomEmoji> wrapped,
		Fn<bool()> enabled);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const std::unique_ptr<Ui::Text::CustomEmoji> _wrapped;
	const Fn<bool()> _enabled;

};

MaybeDisabledEmoji::MaybeDisabledEmoji(
	std::unique_ptr<CustomEmoji> wrapped,
	Fn<bool()> enabled)
: _wrapped(std::move(wrapped))
, _enabled(std::move(enabled)) {
}

int MaybeDisabledEmoji::width() {
	return _wrapped->width();
}

QString MaybeDisabledEmoji::entityData() {
	return _wrapped->entityData();
}

void MaybeDisabledEmoji::paint(QPainter &p, const Context &context) {
	const auto disabled = !_enabled();
	const auto was = disabled ? p.opacity() : 1.;
	if (disabled) {
		p.setOpacity(kDisabledEmojiOpacity);
	}
	_wrapped->paint(p, context);
	if (disabled) {
		p.setOpacity(was);
	}
}

void MaybeDisabledEmoji::unload() {
	_wrapped->unload();
}

bool MaybeDisabledEmoji::ready() {
	return _wrapped->ready();
}

bool MaybeDisabledEmoji::readyInDefaultState() {
	return _wrapped->readyInDefaultState();
}

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

[[nodiscard]] std::vector<Data::ReactionId> DefaultSelected() {
	const auto like = QString::fromUtf8("\xf0\x9f\x91\x8d");
	const auto dislike = QString::fromUtf8("\xf0\x9f\x91\x8e");
	return { Data::ReactionId{ like }, Data::ReactionId{ dislike } };
}

[[nodiscard]] bool RemoveNonCustomEmojiFragment(
		not_null<QTextDocument*> document,
		UniqueCustomEmojiContext &context) {
	context.ids.clear();
	context.hardLimitChecked = 0;
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
		const auto documentId = id.toULongLong();
		const auto applyHardLimit = context.applyHardLimit(documentId);
		if (ranges::contains(context.ids, documentId)) {
			removeTill += fragment.length();
			break;
		} else if (applyHardLimit
			&& context.hardLimitChecked >= context.hardLimit) {
			context.hardLimitHit = true;
			removeTill += fragment.length();
			break;
		}
		context.ids.push_back(documentId);
		if (applyHardLimit) {
			++context.hardLimitChecked;
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

void SetupOnlyCustomEmojiField(
		not_null<Ui::InputField*> field,
		Fn<void(std::vector<DocumentId>, bool)> callback,
		Fn<bool(DocumentId)> applyHardLimit,
		int customHardLimit) {
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
		auto context = UniqueCustomEmojiContext{
			.applyHardLimit = applyHardLimit,
			.hardLimit = customHardLimit,
		};
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
		callback(context.ids, context.hardLimitHit);
		if (changed) {
			field->forceProcessContentsChanges();
		}
	}, field->lifetime());
}

[[nodiscard]] TextWithTags ComposeEmojiList(
		not_null<Data::Reactions*> reactions,
		const std::vector<Data::ReactionId> &list) {
	auto result = TextWithTags();
	const auto size = [&] {
		return int(result.text.size());
	};
	auto added = base::flat_set<Data::ReactionId>();
	const auto &all = reactions->list(Data::Reactions::Type::All);
	const auto add = [&](Data::ReactionId id) {
		if (!added.emplace(id).second) {
			return;
		}
		auto unifiedId = id.custom();
		const auto offset = size();
		if (unifiedId) {
			result.text.append('@');
		} else {
			result.text.append(id.emoji());
			const auto i = ranges::find(all, id, &Data::Reaction::id);
			if (i == end(all)) {
				return;
			}
			unifiedId = i->selectAnimation->id;
		}
		const auto data = Data::SerializeCustomEmojiId(unifiedId);
		const auto tag = Ui::InputField::CustomEmojiLink(data);
		result.tags.append({ offset, size() - offset, tag });
	};
	for (const auto &id : list) {
		add(id);
	}
	return result;
}

enum class ReactionsSelectorState {
	Active,
	Disabled,
	Hidden,
};

struct ReactionsSelectorArgs {
	not_null<QWidget*> outer;
	not_null<Window::SessionController*> controller;
	rpl::producer<QString> title;
	std::vector<Data::Reaction> list;
	std::vector<Data::ReactionId> selected;
	Fn<void(std::vector<Data::ReactionId>, bool)> callback;
	rpl::producer<ReactionsSelectorState> stateValue;
	int customAllowed = 0;
	int customHardLimit = 0;
	bool all = false;
};

object_ptr<Ui::RpWidget> AddReactionsSelector(
		not_null<Ui::RpWidget*> parent,
		ReactionsSelectorArgs &&args) {
	using namespace ChatHelpers;
	using HistoryView::Reactions::UnifiedFactoryOwner;

	auto result = object_ptr<Ui::InputField>(
		parent,
		st::manageGroupReactionsField,
		Ui::InputField::Mode::MultiLine,
		std::move(args.title));
	const auto raw = result.data();
	const auto session = &args.controller->session();
	const auto owner = &session->data();
	const auto reactions = &owner->reactions();
	const auto customAllowed = args.customAllowed;

	struct State {
		std::unique_ptr<Ui::RpWidget> overlay;
		std::unique_ptr<UnifiedFactoryOwner> unifiedFactoryOwner;
		UnifiedFactoryOwner::RecentFactory factory;
		base::flat_set<DocumentId> allowed;
		rpl::lifetime focusLifetime;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->unifiedFactoryOwner = std::make_unique<UnifiedFactoryOwner>(
		session,
		reactions->list(Data::Reactions::Type::Active));
	state->factory = state->unifiedFactoryOwner->factory();

	const auto customEmojiPaused = [controller = args.controller] {
		return controller->isGifPausedAtLeastFor(PauseReason::Layer);
	};
	raw->setCustomEmojiFactory([=](QStringView data, Fn<void()> update)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		const auto id = Data::ParseCustomEmojiData(data);
		auto result = owner->customEmojiManager().create(
			data,
			std::move(update));
		if (state->unifiedFactoryOwner->lookupReactionId(id).custom()) {
			return std::make_unique<MaybeDisabledEmoji>(
				std::move(result),
				[=] { return state->allowed.contains(id); });
		}
		using namespace Ui::Text;
		return std::make_unique<FirstFrameEmoji>(std::move(result));
	}, std::move(customEmojiPaused));

	const auto callback = args.callback;
	const auto isCustom = [=](DocumentId id) {
		return state->unifiedFactoryOwner->lookupReactionId(id).custom();
	};
	SetupOnlyCustomEmojiField(raw, [=](
			std::vector<DocumentId> ids,
			bool hardLimitHit) {
		auto allowed = base::flat_set<DocumentId>();
		auto reactions = std::vector<Data::ReactionId>();
		reactions.reserve(ids.size());
		allowed.reserve(std::min(customAllowed, int(ids.size())));
		const auto owner = state->unifiedFactoryOwner.get();
		for (const auto id : ids) {
			const auto reactionId = owner->lookupReactionId(id);
			if (reactionId.custom() && allowed.size() < customAllowed) {
				allowed.emplace(id);
			}
			reactions.push_back(reactionId);
		}
		if (state->allowed != allowed) {
			state->allowed = std::move(allowed);
			raw->rawTextEdit()->update();
		}
		callback(std::move(reactions), hardLimitHit);
	}, isCustom, args.customHardLimit);
	raw->setTextWithTags(ComposeEmojiList(reactions, args.selected));

	using SelectorState = ReactionsSelectorState;
	std::move(
		args.stateValue
	) | rpl::start_with_next([=](SelectorState value) {
		switch (value) {
		case SelectorState::Active:
			state->overlay = nullptr;
			state->focusLifetime.destroy();
			if (raw->empty()) {
				raw->setTextWithTags(
					ComposeEmojiList(reactions, DefaultSelected()));
			}
			raw->setDisabled(false);
			raw->setFocusFast();
			break;
		case SelectorState::Disabled:
			state->overlay = std::make_unique<Ui::RpWidget>(parent);
			state->overlay->show();
			raw->geometryValue() | rpl::start_with_next([=](QRect rect) {
				state->overlay->setGeometry(rect);
			}, state->overlay->lifetime());
			state->overlay->paintRequest() | rpl::start_with_next([=](QRect clip) {
				auto color = st::boxBg->c;
				color.setAlphaF(0.5);
				QPainter(state->overlay.get()).fillRect(
					clip,
					color);
			}, state->overlay->lifetime());
			[[fallthrough]];
		case SelectorState::Hidden:
			if (Ui::InFocusChain(raw)) {
				raw->parentWidget()->setFocus();
			}
			raw->setDisabled(true);
			raw->focusedChanges(
			) | rpl::start_with_next([=](bool focused) {
				if (focused) {
					raw->parentWidget()->setFocus();
				}
			}, state->focusLifetime);
			break;
		}
	}, raw->lifetime());

	const auto toggle = Ui::CreateChild<Ui::IconButton>(
		parent.get(),
		st::manageGroupReactions);

	const auto panel = Ui::CreateChild<TabbedPanel>(
		args.outer.get(),
		args.controller,
		object_ptr<TabbedSelector>(
			nullptr,
			args.controller->uiShow(),
			Window::GifPauseReason::Layer,
			(args.all
				? TabbedSelector::Mode::FullReactions
				: TabbedSelector::Mode::RecentReactions)));
	panel->selector()->provideRecentEmoji(
		state->unifiedFactoryOwner->unifiedIdsList());
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
	for (auto widget = (QWidget*)raw
		; widget && widget != args.outer
		; widget = widget->parentWidget()) {
		base::install_event_filter(raw, widget, filterCallback);
	}
	base::install_event_filter(raw, args.outer, filterCallback);
	scheduleUpdateEmojiPanelGeometry();

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

void AddReactionsText(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionNavigation*> navigation,
		int allowedCustomReactions,
		rpl::producer<int> customCountValue,
		Fn<void(int required)> askForBoosts) {
	auto ownedInner = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = ownedInner.data();
	const auto count = inner->lifetime().make_state<rpl::variable<int>>(
		std::move(customCountValue));

	container->add(
		object_ptr<Ui::DividerLabel>(
			container,
			std::move(ownedInner),
			st::defaultBoxDividerLabelPadding),
		QMargins(0, st::manageGroupReactionsTextSkip, 0, 0));
	const auto label = inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_manage_peer_reactions_own(
				lt_link,
				tr::lng_manage_peer_reactions_own_link(
				) | Ui::Text::ToLink(),
				Ui::Text::WithEntities),
			st::boxDividerLabel));
	const auto weak = base::make_weak(navigation);
	label->setClickHandlerFilter([=](const auto &...) {
		if (const auto strong = weak.get()) {
			strong->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = u"stickers"_q,
				.resolveType = Window::ResolveType::Mention,
			});
		}
		return false;
	});
	auto countString = count->value() | rpl::map([](int count) {
		return TextWithEntities{ QString::number(count) };
	});
	auto needs = rpl::combine(
		tr::lng_manage_peer_reactions_level(
			lt_count,
			count->value() | tr::to_count(),
			lt_same_count,
			std::move(countString),
			Ui::Text::RichLangValue),
		tr::lng_manage_peer_reactions_boost(
			lt_link,
			tr::lng_manage_peer_reactions_boost_link() | Ui::Text::ToLink(),
			Ui::Text::RichLangValue)
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		a.append(' ').append(std::move(b));
		return std::move(a);
	});
	const auto wrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				std::move(needs),
				st::boxDividerLabel),
			QMargins{ 0, st::normalFont->height, 0, 0 }));
	wrap->toggleOn(count->value() | rpl::map(
		rpl::mappers::_1 > allowedCustomReactions
	));
	wrap->finishAnimating();

	wrap->entity()->setClickHandlerFilter([=](const auto &...) {
		askForBoosts(count->current());
		return false;
	});
}

} // namespace

void EditAllowedReactionsBox(
		not_null<Ui::GenericBox*> box,
		EditAllowedReactionsArgs &&args) {
	using namespace Data;
	using namespace rpl::mappers;

	box->setTitle(tr::lng_manage_peer_reactions());
	box->setWidth(st::boxWideWidth);

	enum class Option {
		All,
		Some,
		None,
	};
	using SelectorState = ReactionsSelectorState;
	struct State {
		rpl::variable<Option> option; // For groups.
		rpl::variable<SelectorState> selectorState;
		std::vector<Data::ReactionId> selected;
		rpl::variable<int> customCount;
	};
	const auto allowed = args.allowed;
	const auto optionInitial = (allowed.type != AllowedReactionsType::Some)
		? Option::All
		: allowed.some.empty()
		? Option::None
		: Option::Some;
	const auto state = box->lifetime().make_state<State>(State{
		.option = optionInitial,
	});

	const auto container = box->verticalLayout();
	const auto isGroup = args.isGroup;
	const auto enabled = isGroup
		? nullptr
		: container->add(object_ptr<Ui::SettingsButton>(
			container.get(),
			tr::lng_manage_peer_reactions_enable(),
			st::manageGroupNoIconButton.button));
	if (enabled) {
		enabled->toggleOn(rpl::single(optionInitial != Option::None));
		enabled->toggledValue(
		) | rpl::start_with_next([=](bool value) {
			state->selectorState = value
				? SelectorState::Active
				: SelectorState::Disabled;
		}, enabled->lifetime());
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
		wrap->toggleOn(state->option.value(
		) | rpl::map(_1 == Option::Some) | rpl::before_next([=](bool some) {
			if (!some) {
				state->selectorState = SelectorState::Hidden;
			}
		}) | rpl::after_next([=](bool some) {
			if (some) {
				state->selectorState = SelectorState::Active;
			}
		}));
		wrap->finishAnimating();
	}
	const auto reactions = wrap ? wrap->entity() : container.get();

	Ui::AddSkip(reactions);

	const auto all = args.list;
	auto selected = (allowed.type != AllowedReactionsType::Some)
		? (all
			| ranges::views::transform(&Data::Reaction::id)
			| ranges::to_vector)
		: allowed.some;
	const auto changed = [=](
		std::vector<Data::ReactionId> chosen,
		bool hardLimitHit) {
		state->selected = std::move(chosen);
		state->customCount = ranges::count_if(
			state->selected,
			&Data::ReactionId::custom);
		if (hardLimitHit) {
			box->uiShow()->showToast(
				tr::lng_manage_peer_reactions_limit(tr::now));
		}
	};
	changed(selected.empty() ? DefaultSelected() : std::move(selected), {});
	reactions->add(AddReactionsSelector(reactions, {
		.outer = box->getDelegate()->outerContainer(),
		.controller = args.navigation->parentController(),
		.title = (enabled
			? tr::lng_manage_peer_reactions_available()
			: tr::lng_manage_peer_reactions_some_title()),
		.list = all,
		.selected = state->selected,
		.callback = changed,
		.stateValue = state->selectorState.value(),
		.customAllowed = args.allowedCustomReactions,
		.customHardLimit = args.customReactionsHardLimit,
		.all = !args.isGroup,
	}), st::boxRowPadding);

	box->setFocusCallback([=] {
		if (!wrap || state->option.current() == Option::Some) {
			state->selectorState.force_assign(SelectorState::Active);
		}
	});

	if (!isGroup) {
		AddReactionsText(
			container,
			args.navigation,
			args.allowedCustomReactions,
			state->customCount.value(),
			args.askForBoosts);
	}
	const auto collect = [=] {
		auto result = AllowedReactions();
		if (isGroup
			? (state->option.current() == Option::Some)
			: (enabled->toggled())) {
			result.some = state->selected;
		}
		auto some = result.some;
		auto simple = all | ranges::views::transform(
			&Data::Reaction::id
		) | ranges::to_vector;
		ranges::sort(some);
		ranges::sort(simple);
		result.type = isGroup
			? (state->option.current() != Option::All
				? AllowedReactionsType::Some
				: AllowedReactionsType::All)
			: (some == simple)
			? AllowedReactionsType::Default
			: AllowedReactionsType::Some;
		return result;
	};

	box->addButton(tr::lng_settings_save(), [=] {
		const auto result = collect();
		if (!isGroup) {
			const auto custom = ranges::count_if(
				result.some,
				&Data::ReactionId::custom);
			if (custom > args.allowedCustomReactions) {
				args.askForBoosts(custom);
				return;
			}
		}
		box->closeBox();
		args.save(result);
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
