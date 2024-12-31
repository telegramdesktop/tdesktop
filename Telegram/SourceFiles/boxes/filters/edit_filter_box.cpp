/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_box.h"

#include "apiwrap.h"
#include "base/event_filter.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/filters/edit_filter_chats_preview.h"
#include "boxes/filters/edit_filter_links.h"
#include "boxes/premium_limits_box.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "info/userpic/info_userpic_color_circle_button.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/chat/chats_filter_tag.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/animations.h"
#include "ui/effects/panel_animation.h"
#include "ui/empty_userpic.h"
#include "ui/filter_icon_panel.h"
#include "ui/filter_icons.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"
#include "styles/style_chat.h"
#include "styles/style_info_userpic_builder.h"

namespace {

using namespace Settings;

constexpr auto kMaxFilterTitleLength = 12;

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;
using ExceptionPeersRef = const base::flat_set<not_null<History*>> &;
using ExceptionPeersGetter = ExceptionPeersRef(Data::ChatFilter::*)() const;

struct NameEditing {
	not_null<Ui::InputField*> field;
	bool custom = false;
	bool settingDefault = false;
};

not_null<FilterChatsPreview*> SetupChatsPreview(
		not_null<Ui::VerticalLayout*> content,
		not_null<rpl::variable<Data::ChatFilter>*> data,
		Fn<void(const Data::ChatFilter&)> updateDefaultTitle,
		Flags flags,
		ExceptionPeersGetter peers) {
	const auto rules = data->current();
	const auto preview = content->add(object_ptr<FilterChatsPreview>(
		content,
		rules.flags() & flags,
		(rules.*peers)()));

	preview->flagRemoved(
	) | rpl::start_with_next([=](Flag flag) {
		const auto rules = data->current();
		auto computed = Data::ChatFilter(
			rules.id(),
			rules.title(),
			rules.iconEmoji(),
			rules.colorIndex(),
			(rules.flags() & ~flag),
			rules.always(),
			rules.pinned(),
			rules.never());
		updateDefaultTitle(computed);
		*data = std::move(computed);
	}, preview->lifetime());

	preview->peerRemoved(
	) | rpl::start_with_next([=](not_null<History*> history) {
		const auto rules = data->current();
		auto always = rules.always();
		auto pinned = rules.pinned();
		auto never = rules.never();
		always.remove(history);
		pinned.erase(ranges::remove(pinned, history), end(pinned));
		never.remove(history);
		auto computed = Data::ChatFilter(
			rules.id(),
			rules.title(),
			rules.iconEmoji(),
			rules.colorIndex(),
			rules.flags(),
			std::move(always),
			std::move(pinned),
			std::move(never));
		updateDefaultTitle(computed);
		*data = std::move(computed);
	}, preview->lifetime());

	return preview;
}

void EditExceptions(
		not_null<Window::SessionController*> window,
		not_null<QObject*> context,
		Flags options,
		not_null<rpl::variable<Data::ChatFilter>*> data,
		Fn<void(const Data::ChatFilter&)> updateDefaultTitle,
		Fn<void()> refresh) {
	const auto include = (options & Flag::Contacts) != Flags(0);
	const auto rules = data->current();
	const auto session = &window->session();
	const auto limit = Data::PremiumLimits(
		session
	).dialogFiltersChatsCurrent();
	const auto showLimitReached = [=] {
		window->show(Box(FilterChatsLimitBox, session, limit, include));
	};
	auto controller = std::make_unique<EditFilterChatsListController>(
		session,
		(include
			? tr::lng_filters_include_title()
			: tr::lng_filters_exclude_title()),
		options,
		rules.flags() & options,
		include ? rules.always() : rules.never(),
		limit,
		showLimitReached);
	const auto rawController = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setCloseByOutsideClick(false);
		box->addButton(tr::lng_settings_save(), crl::guard(context, [=] {
			const auto peers = box->collectSelectedRows();
			const auto rules = data->current();
			auto &&histories = ranges::views::all(
				peers
			) | ranges::views::transform([=](not_null<PeerData*> peer) {
				return window->session().data().history(peer);
			});
			auto changed = base::flat_set<not_null<History*>>{
				histories.begin(),
				histories.end()
			};
			auto removeFrom = include ? rules.never() : rules.always();
			for (const auto &history : changed) {
				removeFrom.remove(history);
			}
			auto pinned = rules.pinned();
			pinned.erase(ranges::remove_if(pinned, [&](
					not_null<History*> history) {
				const auto contains = changed.contains(history);
				return include ? !contains : contains;
			}), end(pinned));
			auto computed = Data::ChatFilter(
				rules.id(),
				rules.title(),
				rules.iconEmoji(),
				rules.colorIndex(),
				((rules.flags() & ~options)
					| rawController->chosenOptions()),
				include ? std::move(changed) : std::move(removeFrom),
				std::move(pinned),
				include ? std::move(removeFrom) : std::move(changed));
			updateDefaultTitle(computed);
			*data = computed;
			refresh();
			box->closeBox();
		}));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	window->window().show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

void CreateIconSelector(
		not_null<QWidget*> outer,
		not_null<QWidget*> box,
		not_null<QWidget*> parent,
		not_null<Ui::InputField*> input,
		not_null<rpl::variable<Data::ChatFilter>*> data) {
	const auto rules = data->current();
	const auto toggle = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	toggle->resize(st::windowFilterIconToggleSize);

	const auto type = toggle->lifetime().make_state<Ui::FilterIcon>();
	data->value(
	) | rpl::map([=](const Data::ChatFilter &filter) {
		return Ui::ComputeFilterIcon(filter);
	}) | rpl::start_with_next([=](Ui::FilterIcon icon) {
		*type = icon;
		toggle->update();
	}, toggle->lifetime());

	input->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		const auto left = geometry.x() + geometry.width() - toggle->width();
		const auto position = st::windowFilterIconTogglePosition;
		toggle->move(
			left - position.x(),
			geometry.y() + position.y());
	}, toggle->lifetime());

	toggle->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(toggle);
		const auto icons = Ui::LookupFilterIcon(*type);
		icons.normal->paintInCenter(
			p,
			toggle->rect(),
			st::dialogsUnreadBgMuted->c);
	}, toggle->lifetime());

	const auto panel = toggle->lifetime().make_state<Ui::FilterIconPanel>(
		outer);
	toggle->installEventFilter(panel);
	toggle->addClickHandler([=] {
		panel->toggleAnimated();
	});
	panel->chosen(
	) | rpl::filter([=](Ui::FilterIcon icon) {
		return icon != Ui::ComputeFilterIcon(data->current());
	}) | rpl::start_with_next([=](Ui::FilterIcon icon) {
		panel->hideAnimated();
		const auto rules = data->current();
		*data = Data::ChatFilter(
			rules.id(),
			rules.title(),
			Ui::LookupFilterIcon(icon).emoji,
			rules.colorIndex(),
			rules.flags(),
			rules.always(),
			rules.pinned(),
			rules.never());
	}, panel->lifetime());

	const auto updatePanelGeometry = [=] {
		const auto global = toggle->mapToGlobal({
			toggle->width(),
			toggle->height()
		});
		const auto local = outer->mapFromGlobal(global);
		const auto position = st::windwoFilterIconPanelPosition;
		const auto padding = panel->innerPadding();
		panel->move(
			local.x() - panel->width() + position.x() + padding.right(),
			local.y() + position.y() - padding.top());
	};

	const auto filterForGeometry = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Move || type == QEvent::Resize) {
			// updatePanelGeometry uses not only container geometry, but
			// also container children geometries that will be updated later.
			crl::on_main(panel, [=] { updatePanelGeometry(); });
		}
		return base::EventFilterResult::Continue;
	};

	const auto installFilterForGeometry = [&](not_null<QWidget*> target) {
		panel->lifetime().make_state<base::unique_qptr<QObject>>(
			base::install_event_filter(target, filterForGeometry));
	};
	installFilterForGeometry(outer);
	installFilterForGeometry(box);
}

[[nodiscard]] QString DefaultTitle(const Data::ChatFilter &filter) {
	using Icon = Ui::FilterIcon;
	const auto icon = Ui::ComputeDefaultFilterIcon(filter);
	switch (icon) {
	case Icon::Private:
		return (filter.flags() & Data::ChatFilter::Flag::NonContacts)
			? tr::lng_filters_name_people(tr::now)
			: tr::lng_filters_include_contacts(tr::now);
	case Icon::Groups:
		return tr::lng_filters_include_groups(tr::now);
	case Icon::Channels:
		return tr::lng_filters_include_channels(tr::now);
	case Icon::Bots:
		return tr::lng_filters_include_bots(tr::now);
	case Icon::Unread:
		return tr::lng_filters_name_unread(tr::now);
	case Icon::Unmuted:
		return tr::lng_filters_name_unmuted(tr::now);
	}
	return QString();
}

not_null<Ui::SettingsButton*> AddToggledButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<bool> shown,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	const auto toggled = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateButtonWithIcon(
				container,
				std::move(text),
				st,
				std::move(descriptor)))
	)->toggleOn(std::move(shown), anim::type::instant)->setDuration(0);
	return toggled->entity();
}

[[nodiscard]] QString TrimDefaultTitle(const QString &title) {
	return (title.size() <= kMaxFilterTitleLength) ? title : QString();
}

} // namespace

void EditFilterBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		Fn<void(const Data::ChatFilter &)> doneCallback,
		Fn<void(
			const Data::ChatFilter &data,
			Fn<void(Data::ChatFilter)> next)> saveAnd) {
	using namespace rpl::mappers;
	constexpr auto kColorsCount = 8;
	constexpr auto kNoTag = kColorsCount - 1;

	struct State {
		rpl::variable<Data::ChatFilter> rules;
		rpl::variable<std::vector<Data::ChatFilterLink>> links;
		rpl::variable<bool> hasLinks;
		rpl::variable<bool> chatlist;
		rpl::variable<bool> creating;
		rpl::variable<TextWithEntities> title;
		rpl::variable<bool> staticTitle;
		rpl::variable<int> colorIndex;
	};
	const auto owner = &window->session().data();
	const auto state = box->lifetime().make_state<State>(State{
		.rules = filter,
		.chatlist = filter.chatlist(),
		.creating = filter.title().empty(),
		.title = filter.titleText(),
		.staticTitle = filter.staticTitle(),
	});
	state->colorIndex = filter.colorIndex().value_or(kNoTag);
	state->links = owner->chatsFilters().chatlistLinks(filter.id()),
	state->hasLinks = state->links.value() | rpl::map([=](const auto &v) {
		return !v.empty();
	});
	state->hasLinks.value() | rpl::filter(
		_1
	) | rpl::start_with_next([=] {
		state->chatlist = true;
	}, box->lifetime());

	const auto data = &state->rules;

	owner->chatsFilters().isChatlistChanged(
	) | rpl::filter([=](FilterId id) {
		return (id == data->current().id());
	}) | rpl::start_with_next([=](FilterId id) {
		const auto filters = &owner->chatsFilters();
		const auto &list = filters->list();
		const auto i = ranges::find(list, id, &Data::ChatFilter::id);
		if (i == end(list)) {
			return;
		}
		*data = data->current().withChatlist(i->chatlist(), i->hasMyLinks());
		if (!i->chatlist() && !state->hasLinks.current()) {
			state->chatlist = false;
		}
	}, box->lifetime());

	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::conditional(
		state->creating.value(),
		tr::lng_filters_new(),
		tr::lng_filters_edit()));
	box->setCloseByOutsideClick(false);

	const auto session = &window->session();
	Data::AmPremiumValue(
		session
	) | rpl::start_with_next([=] {
		box->closeBox();
	}, box->lifetime());

	const auto content = box->verticalLayout();
	const auto current = state->title.current();
	const auto name = content->add(
		object_ptr<Ui::InputField>(
			box,
			st::windowFilterNameInput,
			Ui::InputField::Mode::SingleLine,
			tr::lng_filters_new_name()),
		st::markdownLinkFieldPadding);
	InitMessageFieldHandlers(window, name, ChatHelpers::PauseReason::Layer);
	name->setTextWithTags({
		current.text,
		TextUtilities::ConvertEntitiesToTextTags(current.entities),
	}, Ui::InputField::HistoryAction::Clear);
	name->setMaxLength(kMaxFilterTitleLength);

	const auto nameEditing = box->lifetime().make_state<NameEditing>(
		NameEditing{ name });

	const auto staticTitle = Ui::CreateChild<Ui::LinkButton>(
		name,
		QString());
	staticTitle->setClickedCallback([=] {
		state->staticTitle = !state->staticTitle.current();
	});
	state->staticTitle.value() | rpl::start_with_next([=](bool value) {
		staticTitle->setText(value
			? tr::lng_filters_enable_animations(tr::now)
			: tr::lng_filters_disable_animations(tr::now));
		const auto paused = [=] {
			using namespace Window;
			return window->isGifPausedAtLeastFor(GifPauseReason::Layer);
		};
		name->setCustomTextContext([=](Fn<void()> repaint) {
			return std::any(Core::MarkedTextContext{
				.session = session,
				.customEmojiRepaint = std::move(repaint),
				.customEmojiLoopLimit = value ? -1 : 0,
			});
		}, [paused] {
			return On(PowerSaving::kEmojiChat) || paused();
		}, [paused] {
			return On(PowerSaving::kChatSpoiler) || paused();
		});
		name->update();
	}, staticTitle->lifetime());

	rpl::combine(
		staticTitle->widthValue(),
		name->widthValue()
	) | rpl::start_with_next([=](int inner, int outer) {
		staticTitle->moveToRight(
			st::windowFilterStaticTitlePosition.x(),
			st::windowFilterStaticTitlePosition.y(),
			outer);
	}, staticTitle->lifetime());

	state->creating.value(
	) | rpl::filter(!_1) | rpl::start_with_next([=] {
		nameEditing->custom = true;
	}, box->lifetime());

	name->changes(
	) | rpl::start_with_next([=] {
		if (!nameEditing->settingDefault) {
			nameEditing->custom = true;
		}
		auto entered = name->getTextWithTags();
		state->title = TextWithEntities{
			std::move(entered.text),
			TextUtilities::ConvertTextTagsToEntities(entered.tags),
		};
	}, name->lifetime());

	const auto updateDefaultTitle = [=](const Data::ChatFilter &filter) {
		if (nameEditing->custom) {
			return;
		}
		const auto title = TrimDefaultTitle(DefaultTitle(filter));
		if (nameEditing->field->getLastText() != title) {
			nameEditing->settingDefault = true;
			nameEditing->field->setText(title);
			nameEditing->settingDefault = false;
		}
	};

	state->title.value(
	) | rpl::start_with_next([=](const TextWithEntities &value) {
		staticTitle->setVisible(!value.entities.isEmpty());
	}, staticTitle->lifetime());

	const auto outer = box->getDelegate()->outerContainer();
	CreateIconSelector(
		outer,
		box,
		content,
		name,
		data);

	constexpr auto kTypes = Flag::Contacts
		| Flag::NonContacts
		| Flag::Groups
		| Flag::Channels
		| Flag::Bots;
	constexpr auto kExcludeTypes = Flag::NoMuted
		| Flag::NoArchived
		| Flag::NoRead;

	box->setFocusCallback([=] {
		name->setFocusFast();
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_filters_include());

	const auto includeAdd = AddButtonWithIcon(
		content,
		tr::lng_filters_add_chats(),
		st::settingsButtonActive,
		{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive });

	const auto include = SetupChatsPreview(
		content,
		data,
		updateDefaultTitle,
		kTypes,
		&Data::ChatFilter::always);

	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_filters_include_about());
	Ui::AddSkip(content);

	auto excludeWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content))
	)->setDuration(0);
	excludeWrap->toggleOn(state->chatlist.value() | rpl::map(!_1));
	const auto excludeInner = excludeWrap->entity();

	Ui::AddSubsectionTitle(excludeInner, tr::lng_filters_exclude());

	const auto excludeAdd = AddButtonWithIcon(
		excludeInner,
		tr::lng_filters_remove_chats(),
		st::settingsButtonActive,
		{ &st::settingsIconRemove, IconType::Round, &st::windowBgActive });

	const auto exclude = SetupChatsPreview(
		excludeInner,
		data,
		updateDefaultTitle,
		kExcludeTypes,
		&Data::ChatFilter::never);

	Ui::AddSkip(excludeInner);
	Ui::AddDividerText(excludeInner, tr::lng_filters_exclude_about());
	Ui::AddSkip(excludeInner);

	{
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));
		const auto colors = wrap->entity();
		const auto session = &window->session();

		wrap->toggleOn(
			rpl::combine(
				session->premiumPossibleValue(),
				session->data().chatsFilters().tagsEnabledValue(),
				Data::AmPremiumValue(session)
			) | rpl::map([=] (bool possible, bool tagsEnabled, bool premium) {
				return possible && (tagsEnabled || !premium);
			}),
			anim::type::instant);

		const auto isPremium = session->premium();
		const auto title = Ui::AddSubsectionTitle(
			colors,
			tr::lng_filters_tag_color_subtitle());
		const auto preview = Ui::CreateChild<Ui::RpWidget>(colors);
		title->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			const auto h = st::normalFont->height;
			preview->setGeometry(
				colors->x(),
				r.y() + (r.height() - h) / 2 + st::lineWidth,
				colors->width(),
				h);
		}, preview->lifetime());

		struct TagState {
			Ui::Animations::Simple animation;
			Ui::ChatsFilterTagContext context;
			QImage frame;
			float64 alpha = 1.;
		};
		const auto tag = preview->lifetime().make_state<TagState>();
		tag->context.textContext = Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [] {},
		};
		preview->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(preview);
			p.setOpacity(tag->alpha);
			const auto size = tag->frame.size() / style::DevicePixelRatio();
			const auto rect = QRect(
				preview->width() - size.width() - st::boxRowPadding.right(),
				(st::normalFont->height - size.height()) / 2,
				size.width(),
				size.height());
			p.drawImage(rect.topLeft(), tag->frame);
			if (p.opacity() < 1) {
				p.setOpacity(1. - p.opacity());
				p.setFont(st::normalFont);
				p.setPen(st::windowSubTextFg);
				p.drawText(
					preview->rect() - st::boxRowPadding,
					tr::lng_filters_tag_color_no(tr::now),
					style::al_right);
			}
		}, preview->lifetime());

		const auto side = st::userpicBuilderEmojiAccentColorSize;
		const auto line = colors->add(
			Ui::CreateSkipWidget(colors, side),
			st::boxRowPadding);
		auto buttons = std::vector<not_null<UserpicBuilder::CircleButton*>>();
		const auto palette = [](int i) {
			return Ui::EmptyUserpic::UserpicColor(i).color2;
		};
		const auto upperTitle = [=] {
			auto value = state->title.current();
			value.text = value.text.toUpper();
			return value;
		};
		state->title.changes(
		) | rpl::start_with_next([=] {
			tag->context.color = palette(state->colorIndex.current())->c;
			tag->frame = Ui::ChatsFilterTag(
				upperTitle(),
				tag->context);
			preview->update();
		}, preview->lifetime());
		for (auto i = 0; i < kColorsCount; ++i) {
			const auto button = Ui::CreateChild<UserpicBuilder::CircleButton>(
				line);
			button->resize(side, side);
			const auto progress = isPremium
				? (state->colorIndex.current() == i)
				: (i == kNoTag);
			button->setSelectedProgress(progress);
			const auto color = palette(i);
			button->setBrush(color);
			if (progress == 1) {
				tag->context.color = color->c;
				tag->frame = Ui::ChatsFilterTag(
					upperTitle(),
					tag->context);
				if (i == kNoTag) {
					tag->alpha = 0.;
				}
			}
			buttons.push_back(button);
		}
		for (auto i = 0; i < kColorsCount; ++i) {
			const auto &button = buttons[i];
			button->setClickedCallback([=] {
				const auto was = state->colorIndex.current();
				const auto now = i;
				if (was != now) {
					const auto c1 = palette(was);
					const auto c2 = palette(now);
					const auto a1 = (was == kNoTag) ? 0. : 1.;
					const auto a2 = (now == kNoTag) ? 0. : 1.;
					tag->animation.stop();
					tag->animation.start([=](float64 progress) {
						if (was >= 0) {
							buttons[was]->setSelectedProgress(1. - progress);
						}
						buttons[now]->setSelectedProgress(progress);
						tag->context.color = anim::color(c1, c2, progress);
						tag->frame = Ui::ChatsFilterTag(
							upperTitle(),
							tag->context);
						tag->alpha = anim::interpolateF(a1, a2, progress);
						preview->update();
					}, 0., 1., st::universalDuration);
				}
				state->colorIndex = now;
			});
			if (!session->premium()) {
				button->setClickedCallback([w = window] {
					ShowPremiumPreviewToBuy(w, PremiumFeature::FilterTags);
				});
			}
		}
		line->sizeValue() | rpl::start_with_next([=](const QSize &size) {
			const auto totalWidth = buttons.size() * side;
			const auto spacing = (size.width() - totalWidth)
				/ (buttons.size() - 1);
			for (auto i = 0; i < kColorsCount; ++i) {
				const auto &button = buttons[i];
				button->moveToLeft(i * (side + spacing), 0);
			}
		}, line->lifetime());

		{
			const auto last = buttons.back();
			const auto icon = Ui::CreateChild<Ui::RpWidget>(last);
			icon->resize(side, side);
			icon->paintRequest() | rpl::start_with_next([=] {
				auto p = QPainter(icon);
				(session->premium()
					? st::windowFilterSmallRemove.icon
					: st::historySendDisabledIcon).paintInCenter(
						p,
						QRectF(icon->rect()),
						st::historyPeerUserpicFg->c);
			}, icon->lifetime());
			icon->setAttribute(Qt::WA_TransparentForMouseEvents);
			last->setBrush(st::historyPeerArchiveUserpicBg);
		}

		Ui::AddSkip(colors);
		Ui::AddSkip(colors);
		Ui::AddDividerText(colors, tr::lng_filters_tag_color_about());
		Ui::AddSkip(colors);
	}

	const auto collect = [=]() -> std::optional<Data::ChatFilter> {
		auto title = state->title.current();
		const auto staticTitle = !title.entities.isEmpty()
			&& state->staticTitle.current();
		const auto rules = data->current();
		if (title.empty()) {
			name->showError();
			box->scrollToY(0);
			return {};
		} else if (!(rules.flags() & kTypes) && rules.always().empty()) {
			window->window().showToast(tr::lng_filters_empty(tr::now));
			return {};
		} else if ((rules.flags() == (kTypes | Flag::NoArchived))
			&& rules.always().empty()
			&& rules.never().empty()) {
			window->window().showToast(tr::lng_filters_default(tr::now));
			return {};
		}
		const auto rawColorIndex = state->colorIndex.current();
		const auto colorIndex = (rawColorIndex >= kNoTag
			? std::nullopt
			: std::make_optional(rawColorIndex));
		return rules.withTitle(
			{ std::move(title), staticTitle }
		).withColorIndex(colorIndex);
	};

	Ui::AddSubsectionTitle(
		content,
		rpl::conditional(
			state->hasLinks.value(),
			tr::lng_filters_link_has(),
			tr::lng_filters_link()));

	state->hasLinks.changes() | rpl::start_with_next([=] {
		content->resizeToWidth(content->widthNoMargins());
	}, content->lifetime());

	if (filter.chatlist()) {
		window->session().data().chatsFilters().reloadChatlistLinks(
			filter.id());
	}

	const auto createLink = AddToggledButton(
		content,
		state->hasLinks.value() | rpl::map(!rpl::mappers::_1),
		tr::lng_filters_link_create(),
		st::settingsButtonActive,
		{ &st::settingsFolderShareIcon, IconType::Simple });
	const auto addLink = AddToggledButton(
		content,
		state->hasLinks.value(),
		tr::lng_group_invite_add(),
		st::settingsButtonActive,
		{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive });

	SetupFilterLinks(
		content,
		window,
		state->links.value(),
		[=] { return collect().value_or(Data::ChatFilter()); });

	rpl::merge(
		createLink->clicks(),
		addLink->clicks()
	) | rpl::filter(
		(rpl::mappers::_1 == Qt::LeftButton)
	) | rpl::start_with_next([=](Qt::MouseButton button) {
		const auto result = collect();
		if (!result || !GoodForExportFilterLink(window, *result)) {
			return;
		}
		const auto shared = CollectFilterLinkChats(*result);
		if (shared.empty()) {
			window->show(ShowLinkBox(window, *result, {}));
			return;
		}
		saveAnd(*result, crl::guard(box, [=](Data::ChatFilter updated) {
			state->creating = false;

			// Comparison of ChatFilter-s don't take id into account!
			data->force_assign(updated);
			const auto id = updated.id();
			state->links = owner->chatsFilters().chatlistLinks(id);
			ExportFilterLink(id, shared, crl::guard(box, [=](
					Data::ChatFilterLink link) {
				Expects(link.id == id);

				*data = data->current().withChatlist(true, true);
				window->show(ShowLinkBox(window, updated, link));
			}), crl::guard(box, [=](QString error) {
				const auto session = &window->session();
				if (error == u"CHATLISTS_TOO_MUCH"_q) {
					window->show(Box(ShareableFiltersLimitBox, session));
				} else if (error == u"INVITES_TOO_MUCH"_q) {
					window->show(Box(FilterLinksLimitBox, session));
				} else if (error == u"CHANNELS_TOO_MUCH"_q) {
					window->show(Box(ChannelsLimitBox, session));
				} else if (error == u"USER_CHANNELS_TOO_MUCH"_q) {
					window->showToast(
						{ tr::lng_filters_link_group_admin_error(tr::now) });
				} else {
					window->show(ShowLinkBox(window, updated, { .id = id }));
				}
			}));
		}));
	}, createLink->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::conditional(
			state->hasLinks.value(),
			tr::lng_filters_link_about_many(),
			tr::lng_filters_link_about()));

	const auto show = box->uiShow();
	const auto refreshPreviews = [=] {
		include->updateData(
			data->current().flags() & kTypes,
			data->current().always());
		exclude->updateData(
			data->current().flags() & kExcludeTypes,
			data->current().never());
	};
	includeAdd->setClickedCallback([=] {
		EditExceptions(
			window,
			box,
			kTypes | (state->chatlist.current() ? Flag::Chatlist : Flag()),
			data,
			updateDefaultTitle,
			refreshPreviews);
	});
	excludeAdd->setClickedCallback([=] {
		EditExceptions(
			window,
			box,
			kExcludeTypes,
			data,
			updateDefaultTitle,
			refreshPreviews);
	});

	const auto save = [=] {
		if (const auto result = collect()) {
			box->closeBox();
			doneCallback(*result);
		}
	};

	box->addButton(rpl::conditional(
		state->creating.value(),
		tr::lng_filters_create_button(),
		tr::lng_settings_save()
	), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void EditExistingFilter(
		not_null<Window::SessionController*> window,
		FilterId id) {
	Expects(id != 0);

	const auto session = &window->session();
	const auto &list = session->data().chatsFilters().list();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	if (i == end(list)) {
		return;
	}
	const auto doneCallback = [=](const Data::ChatFilter &result) {
		Expects(id == result.id());

		const auto tl = result.tl();
		session->data().chatsFilters().apply(MTP_updateDialogFilter(
			MTP_flags(MTPDupdateDialogFilter::Flag::f_filter),
			MTP_int(id),
			tl));
		session->api().request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
			MTP_int(id),
			tl
		)).send();
	};
	const auto saveAnd = [=](
			const Data::ChatFilter &data,
			Fn<void(Data::ChatFilter)> next) {
		doneCallback(data);
		next(data);
	};
	window->window().show(Box(
		EditFilterBox,
		window,
		*i,
		crl::guard(session, doneCallback),
		crl::guard(session, saveAnd)));
}
