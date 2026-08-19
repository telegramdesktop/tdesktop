/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_builder.h"

#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "api/api_user_privacy.h"
#include "boxes/edit_privacy_box.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/sections/settings_privacy_security.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings::Builder {
namespace {

[[nodiscard]] QString ResolveTitle(rpl::producer<QString> title) {
	auto result = QString();
	auto lifetime = rpl::lifetime();
	std::move(title).start(
		[&](QString value) { result = std::move(value); },
		[](auto&&) {},
		[] {},
		lifetime);
	return result;
}

} // namespace

BuildHelper::BuildHelper(
	SectionMeta &&meta,
	FnMut<void(SectionBuilder&)> method)
: build([=](
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Fn<void(Type)> showOther,
		rpl::producer<> showFinished) {
	auto &lifetime = container->lifetime();
	const auto highlights = lifetime.make_state<HighlightRegistry>();
	const auto isPaused = Window::PausedIn(
		controller,
		Window::GifPauseReason::Layer);
	auto builder = SectionBuilder(WidgetContext{
		.container = static_cast<Ui::VerticalLayout*>(
			container->add(object_ptr<Ui::OverrideMargins>(
				container,
				object_ptr<Ui::VerticalLayout>(container)))->entity()),
		.controller = controller,
		.showOther = std::move(showOther),
		.isPaused = isPaused,
		.highlights = highlights,
	});
	_method(builder);

	std::move(showFinished) | rpl::on_next([=] {
		for (const auto &[id, entry] : *highlights) {
			if (entry.widget) {
				controller->checkHighlightControl(
					id,
					entry.widget,
					base::duplicate(entry.args));
			}
		}
	}, lifetime);
})
, _meta(std::move(meta))
, _method(std::move(method)) {
	Expects(_method != nullptr);

	SearchRegistry::Instance().add(
		&_meta,
		[=](not_null<::Main::Session*> session) { return index(session); });
}

SearchRegistry &SearchRegistry::Instance() {
	static SearchRegistry instance;
	return instance;
}

void SearchRegistry::add(
		not_null<const SectionMeta*> meta,
		SearchEntriesIndexer indexer) {
	_sections[meta->id] = meta;
	_indexers.push_back({ meta->id, std::move(indexer) });
}

std::vector<SearchEntry> SearchRegistry::collectAll(
		not_null<Main::Session*> session) const {
	auto result = std::vector<SearchEntry>();
	for (const auto &[sectionId, meta] : _sections) {
		if (meta->parentId) {
			result.push_back({
				.title = (*meta->title)(tr::now),
				.section = sectionId,
				.icon = { meta->icon },
			});
		}
	}
	for (const auto &entry : _indexers) {
		auto entries = entry.indexer(session);
		result.insert(result.end(), entries.begin(), entries.end());
	}
	return result;
}

QString SearchRegistry::sectionTitle(Type sectionId) const {
	const auto it = _sections.find(sectionId);
	return (it != _sections.end()) ? (*it->second->title)(tr::now) : QString();
}

QString SearchRegistry::sectionPath(Type sectionId, bool parentsOnly) const {
	auto parts = QStringList();
	auto current = sectionId;
	while (current) {
		if (const auto title = sectionTitle(current); !title.isEmpty()) {
			parts.prepend(title);
		}
		const auto it = _sections.find(current);
		current = (it != _sections.end()) ? it->second->parentId : nullptr;
	}
	if (parts.size() > 1 && parentsOnly) {
		parts.pop_back();
	}
	return parts.join(u" > "_q);
}

std::vector<SearchEntry> BuildHelper::index(
		not_null<Main::Session*> session) const {
	auto entries = std::vector<SearchEntry>();
	auto builder = SectionBuilder(SearchContext{
		.sectionId = _meta.id,
		.session = session,
		.entries = &entries,
	});
	_method(builder);
	for (auto &entry : entries) {
		if (!entry.section) {
			entry.section = _meta.id;
		}
	}
	return entries;
}

SectionBuilder::SectionBuilder(BuildContext context)
: _context(std::move(context)) {
}

void SectionBuilder::add(FnMut<void(const BuildContext &ctx)> method) {
	Expects(method != nullptr);

	method(_context);
}

Ui::VerticalLayout *SectionBuilder::scope(
		FnMut<void()> method,
		rpl::producer<bool> shown,
		FnMut<void(ToggledScopePtr)> hook) {
	auto result = (Ui::VerticalLayout*)nullptr;
	v::match(_context, [&](WidgetContext &wctx) {
		const auto outer = wctx.container;
		const auto toggled = (shown || hook);
		const auto wrap = toggled
			? outer->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					outer,
					object_ptr<Ui::VerticalLayout>(outer)))
			: nullptr;
		const auto inner = wrap
			? wrap->entity()
			: outer->add(object_ptr<Ui::VerticalLayout>(outer));
		wctx.container = inner;
		method();
		if (shown) {
			wrap->toggleOn(std::move(shown));
			wrap->finishAnimating();
		}
		if (hook) {
			hook(wrap);
		}
		wctx.container = outer;
		result = inner;
	}, [&](const SearchContext &sctx) {
		method();
	});
	return result;
}

Ui::RpWidget *SectionBuilder::add(
		FnMut<WidgetToAdd(const WidgetContext &ctx)> widget,
		FnMut<SearchEntry()> search) {
	auto result = (Ui::RpWidget*)nullptr;
	add([&](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			if (auto w = widget ? widget(wctx) : WidgetToAdd()) {
				result = w.widget.data();
				wctx.container->add(std::move(w.widget), w.margin, w.align);

				if (auto entry = search ? search() : SearchEntry()) {
					if (wctx.highlights) {
						wctx.highlights->push_back({
							std::move(entry.id),
							{ result, std::move(w.highlight) },
						});
					}
				}
			}
		}, [&](const SearchContext &sctx) {
			if (auto entry = search ? search() : SearchEntry()) {
				entry.section = sctx.sectionId;
				sctx.entries->push_back(std::move(entry));
			}
		});
	});
	return result;
}

Ui::RpWidget *SectionBuilder::addControl(ControlArgs &&args) {
	if (auto shown = base::take(args.shown)) {
		auto result = (Ui::RpWidget*)nullptr;
		scope([&] {
			result = addControl(std::move(args));
		}, std::move(shown));
		return result;
	}
	return add([&](const WidgetContext &ctx) {
		return WidgetToAdd{
			.widget = args.factory ? args.factory(ctx.container) : nullptr,
			.margin = args.margin,
			.align = args.align,
			.highlight = std::move(args.highlight),
		};
	}, [&]() mutable {
		return SearchEntry{
			.id = std::move(args.id),
			.title = ResolveTitle(std::move(args.title)),
			.keywords = std::move(args.keywords),
			.icon = std::move(args.searchIcon),
			.checkIcon = args.searchCheckIcon,
		};
	});
}

Ui::SettingsButton *SectionBuilder::addButton(ButtonArgs &&args) {
	const auto &st = args.st ? *args.st : st::settingsButton;
	auto iconForSearch = IconDescriptor{ args.icon.icon };
	const auto factory = [&](not_null<Ui::VerticalLayout*> container) {
		auto button = CreateButtonWithIcon(
			args.container ? args.container : container.get(),
			rpl::duplicate(args.title),
			st,
			std::move(args.icon));
		if (button && args.onClick) {
			button->addClickHandler(std::move(args.onClick));
		}
		if (args.label) {
			CreateRightLabel(
				button.data(),
				std::move(args.label),
				st,
				rpl::duplicate(args.title));
		}
		if (args.toggled) {
			button->toggleOn(std::move(args.toggled));
		}
		return button;
	};
	return static_cast<Ui::SettingsButton*>(addControl({
		.factory = factory,
		.id = std::move(args.id),
		.title = rpl::duplicate(args.title),
		.highlight = std::move(args.highlight),
		.shown = std::move(args.shown),

		.keywords = std::move(args.keywords),
		.searchIcon = std::move(iconForSearch),
	}));
}

Ui::SettingsButton *SectionBuilder::addSectionButton(SectionArgs &&args) {
	const auto wctx = std::get_if<WidgetContext>(&_context);
	const auto showOther = wctx ? wctx->showOther : nullptr;
	const auto target = args.targetSection;
	return addButton({
		.title = std::move(args.title),
		.icon = std::move(args.icon),
		.onClick = [=] { showOther(target); },
		.keywords = std::move(args.keywords),
	});
}

void SectionBuilder::addDivider() {
	v::match(_context, [&](const WidgetContext &ctx) {
		Ui::AddDivider(ctx.container);
	}, [](const SearchContext &) {
	});
}

void SectionBuilder::addSkip() {
	v::match(_context, [&](const WidgetContext &ctx) {
		Ui::AddSkip(ctx.container);
	}, [](const SearchContext &) {
	});
}

void SectionBuilder::addSkip(int height) {
	v::match(_context, [&](const WidgetContext &ctx) {
		Ui::AddSkip(ctx.container, height);
	}, [](const SearchContext &) {
	});
}

void SectionBuilder::addDividerText(rpl::producer<QString> text) {
	v::match(_context, [&](const WidgetContext &ctx) {
		Ui::AddDividerText(ctx.container, std::move(text));
	}, [](const SearchContext &) {
	});
}

Ui::SettingsButton *SectionBuilder::addPremiumButton(PremiumButtonArgs &&args) {
	const auto result = addButton({
		.id = std::move(args.id),
		.title = std::move(args.title),
		.label = std::move(args.label),
		.onClick = std::move(args.onClick),
		.keywords = std::move(args.keywords),
	});
	if (result) {
		AddPremiumStar(
			result,
			args.credits,
			v::get<WidgetContext>(_context).isPaused);
	}
	return result;
}

Ui::SettingsButton *SectionBuilder::addPrivacyButton(PrivacyButtonArgs &&args) {
	const auto controller = this->controller();
	const auto session = this->session();

	const auto button = addButton({
		.id = args.id,
		.title = rpl::duplicate(args.title),
		.st = &st::settingsButtonNoIcon,
		.label = PrivacyButtonLabel(session, args.key),
		.keywords = args.keywords,
	});
	if (button) {
		const auto id = args.id;
		const auto premium = args.premium;
		const auto shower = Ui::CreateChild<rpl::lifetime>(button);
		const auto factory = args.controllerFactory;
		button->addClickHandler([=, key = args.key] {
			*shower = session->api().userPrivacy().value(
				key
			) | rpl::take(
				1
			) | rpl::on_next(crl::guard(controller, [=](
					const Api::UserPrivacy::Rule &value) {
				controller->show(Box<EditPrivacyBox>(
					controller,
					factory(),
					value));
			}));
		});
		if (premium) {
			AddPrivacyPremiumStar(
				button,
				session,
				std::move(args.title),
				st::settingsButtonNoIcon.padding);
		}
	}
	return button;
}

Ui::Checkbox *SectionBuilder::addCheckbox(CheckboxArgs &&args) {
	const auto factory = [&](not_null<Ui::VerticalLayout*> container) {
		return object_ptr<Ui::Checkbox>(
			container,
			ResolveTitle(rpl::duplicate(args.title)),
			args.checked,
			st::settingsCheckbox);
	};
	return static_cast<Ui::Checkbox*>(addControl({
		.factory = factory,
		.id = std::move(args.id),
		.title = rpl::duplicate(args.title),
		.margin = st::settingsCheckboxPadding,
		.highlight = std::move(args.highlight),
		.shown = std::move(args.shown),
		.keywords = std::move(args.keywords),
		.searchCheckIcon = (args.checked
			? SearchEntryCheckIcon::Checked
			: SearchEntryCheckIcon::Unchecked),
	}));
}

void SectionBuilder::addSubsectionTitle(SubsectionTitleArgs &&args) {
	v::match(_context, [&](const WidgetContext &ctx) {
		const auto title = AddSubsectionTitle(
			ctx.container,
			rpl::duplicate(args.title));
		if (!args.id.isEmpty() && ctx.highlights) {
			ctx.highlights->push_back({
				args.id,
				{ title.get(), SubsectionTitleHighlight() },
			});
		}
	}, [&](const SearchContext &ctx) {
		if (!args.id.isEmpty()) {
			ctx.entries->push_back({
				.id = std::move(args.id),
				.title = ResolveTitle(std::move(args.title)),
				.keywords = std::move(args.keywords),
				.section = ctx.sectionId,
			});
		}
	});
}

void SectionBuilder::addSubsectionTitle(rpl::producer<QString> text) {
	v::match(_context, [&](const WidgetContext &ctx) {
		AddSubsectionTitle(ctx.container, std::move(text));
	}, [](const SearchContext &) {
	});
}

Ui::VerticalLayout *SectionBuilder::container() const {
	return v::match(_context, [](const WidgetContext &ctx) {
		return ctx.container.get();
	}, [](const SearchContext &) -> Ui::VerticalLayout* {
		return nullptr;
	});
}

Window::SessionController *SectionBuilder::controller() const {
	return v::match(_context, [](const WidgetContext &ctx) {
		return ctx.controller.get();
	}, [](const SearchContext &) -> Window::SessionController* {
		return nullptr;
	});
}

not_null<Main::Session*> SectionBuilder::session() const {
	return v::match(_context, [](const WidgetContext &ctx) {
		return &ctx.controller->session();
	}, [](const SearchContext &ctx) {
		return ctx.session.get();
	});
}

Fn<void(Type)> SectionBuilder::showOther() const {
	return v::match(_context, [](const WidgetContext &ctx) {
		return ctx.showOther;
	}, [](const SearchContext &) -> Fn<void(Type)> {
		return nullptr;
	});
}

HighlightRegistry *SectionBuilder::highlights() const {
	return v::match(_context, [](const WidgetContext &ctx) {
		return ctx.highlights;
	}, [](const SearchContext &) -> HighlightRegistry* {
		return nullptr;
	});
}

void SectionBuilder::registerHighlight(
		QString id,
		QWidget *widget,
		HighlightArgs &&args) {
	v::match(_context, [&](const WidgetContext &ctx) {
		if (ctx.highlights && widget) {
			ctx.highlights->push_back({
				std::move(id),
				{ widget, std::move(args) },
			});
		}
	}, [](const SearchContext &) {
	});
}

} // namespace Settings::Builder
