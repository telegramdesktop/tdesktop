/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_search.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_faq_suggestions.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/search_field_controller.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

struct SearchResultItem {
	int index = 0;
	int matchCount = 0;
};

[[nodiscard]] QStringList PrepareEntryWords(const Builder::SearchEntry &entry) {
	auto combined = entry.title;
	for (const auto &keyword : entry.keywords) {
		combined += ' ' + keyword;
	}
	return TextUtilities::PrepareSearchWords(combined);
}

[[nodiscard]] int CalculateDepth(
		Type sectionId,
		const Builder::SearchRegistry &registry) {
	const auto path = registry.sectionPath(sectionId);
	if (path.isEmpty()) {
		return 0;
	}
	return path.count(u" > "_q) + 1;
}

void SetupCheckIcon(
		not_null<Ui::SettingsButton*> button,
		Builder::SearchEntryCheckIcon checkIcon,
		const style::SettingsButton &st) {
	struct CheckWidget {
		CheckWidget(QWidget *parent, bool checked)
		: widget(parent)
		, view(st::defaultCheck, checked) {
			view.finishAnimating();
		}
		Ui::RpWidget widget;
		Ui::CheckView view;
	};
	const auto checked = (checkIcon == Builder::SearchEntryCheckIcon::Checked);
	const auto check = button->lifetime().make_state<CheckWidget>(
		button,
		checked);
	check->widget.setAttribute(Qt::WA_TransparentForMouseEvents);
	check->widget.resize(check->view.getSize());
	check->widget.show();

	button->sizeValue(
	) | rpl::on_next([=, left = st.iconLeft](QSize size) {
		check->widget.moveToLeft(
			left,
			(size.height() - check->widget.height()) / 2,
			size.width());
	}, check->widget.lifetime());

	check->widget.paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(&check->widget);
		check->view.paint(p, 0, 0, check->widget.width());
		p.setOpacity(0.5);
		p.fillRect(clip, st::boxBg);
	}, check->widget.lifetime());
}

[[nodiscard]] not_null<Ui::SettingsButton*> CreateSearchResultButtonRaw(
		not_null<QWidget*> parent,
		const QString &title,
		const QString &subtitle,
		const style::SettingsButton &st,
		IconDescriptor &&icon,
		Builder::SearchEntryCheckIcon checkIcon) {
	auto buttonObj = CreateButtonWithIcon(
		parent,
		rpl::single(title),
		st,
		std::move(icon));
	const auto button = buttonObj.release();
	if (checkIcon != Builder::SearchEntryCheckIcon::None) {
		SetupCheckIcon(button, checkIcon, st);
	}
	const auto details = Ui::CreateChild<Ui::FlatLabel>(
		button,
		subtitle,
		st::settingsSearchResultDetails);
	details->show();
	details->moveToLeft(
		st.padding.left(),
		st.padding.top() + st.height - details->height());
	details->setAttribute(Qt::WA_TransparentForMouseEvents);
	return button;
}

} // namespace

Search::Search(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Search::title() {
	return tr::lng_dlg_filter();
}

void Search::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	}
}

base::weak_qptr<Ui::RpWidget> Search::createPinnedToTop(
		not_null<QWidget*> parent) {
	_searchController = std::make_unique<Ui::SearchFieldController>("");
	auto rowView = _searchController->createRowView(
		parent,
		st::infoLayerMediaSearch);
	_searchField = rowView.field;

	const auto searchContainer = Ui::CreateChild<Ui::FixedHeightWidget>(
		parent.get(),
		st::infoLayerMediaSearch.height);
	const auto wrap = rowView.wrap.release();
	wrap->setParent(searchContainer);
	wrap->show();

	searchContainer->widthValue(
	) | rpl::on_next([=](int width) {
		wrap->resizeToWidth(width);
		wrap->moveToLeft(0, 0);
	}, searchContainer->lifetime());

	_searchController->queryChanges() | rpl::on_next([=](QString &&query) {
		if (_stepData) {
			*_stepData = SearchSectionState{ query };
		}
		rebuildResults(std::move(query));
	}, searchContainer->lifetime());

	if (!_pendingQuery.isEmpty()) {
		_searchController->setQuery(base::take(_pendingQuery));
	}

	return base::make_weak(not_null<Ui::RpWidget*>{ searchContainer });
}

void Search::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	_list = content->add(object_ptr<Ui::VerticalLayout>(content));

	setupCustomizations();
	buildIndex();
	rebuildResults(QString());

	controller()->session().faqSuggestions().loadedValue(
	) | rpl::filter([](bool loaded) {
		return loaded;
	}) | rpl::take(1) | rpl::on_next([=] {
		for (auto i = _faqStartIndex; i < int(_entries.size()); ++i) {
			const auto it = _buttonCache.find(i);
			if (it != _buttonCache.end()) {
				delete it->second;
				_buttonCache.erase(it);
			}
		}
		buildIndex();
		const auto query = _searchController
			? _searchController->query()
			: QString();
		rebuildResults(query);
	}, lifetime());

	Ui::ResizeFitChild(this, content);
}

void Search::setupCustomizations() {
	const auto isPaused = Window::PausedIn(
		controller(),
		Window::GifPauseReason::Layer);
	const auto add = [&](const QString &id, ResultCustomization value) {
		_customizations[id] = std::move(value);
	};

	add(u"main/credits"_q, {
		.hook = [=](not_null<Ui::SettingsButton*> b) {
			AddPremiumStar(b, true, isPaused);
		},
		.st = &st::settingsSearchResult,
	});
	add(u"main/premium"_q, {
		.hook = [=](not_null<Ui::SettingsButton*> b) {
			AddPremiumStar(b, false, isPaused);
		},
		.st = &st::settingsSearchResult,
	});
}

void Search::buildIndex() {
	_entries.clear();
	_firstLetterIndex.clear();

	const auto &registry = Builder::SearchRegistry::Instance();
	const auto rawEntries = registry.collectAll(&controller()->session());

	_entries.reserve(rawEntries.size());
	for (const auto &entry : rawEntries) {
		auto indexed = IndexedEntry{
			.entry = entry,
			.terms = PrepareEntryWords(entry),
			.depth = CalculateDepth(entry.section, registry),
		};
		_entries.push_back(std::move(indexed));
	}

	_faqStartIndex = int(_entries.size());

	const auto &faq = controller()->session().faqSuggestions();
	for (const auto &faqEntry : faq.entries()) {
		auto entry = Builder::SearchEntry{
			.title = faqEntry.title,
		};
		auto indexed = IndexedEntry{
			.entry = std::move(entry),
			.terms = TextUtilities::PrepareSearchWords(faqEntry.title),
			.depth = 1000,
			.faqUrl = faqEntry.url,
			.faqSection = faqEntry.section,
		};
		_entries.push_back(std::move(indexed));
	}

	for (auto i = 0; i < int(_entries.size()); ++i) {
		for (const auto &term : _entries[i].terms) {
			if (!term.isEmpty()) {
				_firstLetterIndex[term[0]].insert(i);
			}
		}
	}
}

void Search::rebuildResults(const QString &query) {
	for (auto i = 0, count = _list->count(); i != count; ++i) {
		_list->widgetAt(i)->hide();
	}
	_list->clear();

	const auto queryWords = TextUtilities::PrepareSearchWords(query);

	if (queryWords.isEmpty()) {
		rebuildFaqResults();
		return;
	}

	auto results = std::vector<SearchResultItem>();
	{
		auto toFilter = (const base::flat_set<int>*)nullptr;
		for (const auto &word : queryWords) {
			if (word.isEmpty()) {
				continue;
			}
			const auto it = _firstLetterIndex.find(word[0]);
			if (it == _firstLetterIndex.end() || it->second.empty()) {
				toFilter = nullptr;
				break;
			} else if (!toFilter || it->second.size() < toFilter->size()) {
				toFilter = &it->second;
			}
		}

		if (toFilter) {
			for (const auto entryIndex : *toFilter) {
				const auto &indexed = _entries[entryIndex];
				auto matched = 0;
				for (const auto &queryWord : queryWords) {
					for (const auto &term : indexed.terms) {
						if (term.startsWith(queryWord)) {
							++matched;
							break;
						}
					}
				}
				if (matched > 0) {
					results.push_back({
						.index = entryIndex,
						.matchCount = matched,
					});
				}
			}

			ranges::sort(results, [&](const auto &a, const auto &b) {
				if (a.matchCount != b.matchCount) {
					return a.matchCount > b.matchCount;
				}
				const auto &entryA = _entries[a.index];
				const auto &entryB = _entries[b.index];
				if (entryA.depth != entryB.depth) {
					return entryA.depth < entryB.depth;
				}
				return entryA.entry.title < entryB.entry.title;
			});
		}
	}

	if (results.empty() && !queryWords.isEmpty()) {
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				tr::lng_search_tab_no_results(),
				st::settingsSearchNoResults),
			st::settingsSearchNoResultsPadding);
	} else {
		const auto showOther = showOtherMethod();
		const auto &registry = Builder::SearchRegistry::Instance();
		const auto faqSubtitle = tr::lng_settings_faq_subtitle(tr::now);
		const auto weak = base::make_weak(controller());

		for (const auto &result : results) {
			const auto entryIndex = result.index;
			const auto &indexed = _entries[entryIndex];
			const auto &entry = indexed.entry;
			const auto isFaq = !indexed.faqUrl.isEmpty();

			const auto cached = _buttonCache.find(entryIndex);
			if (cached != _buttonCache.end()) {
				const auto button = cached->second;
				button->show();
				_list->add(
					object_ptr<Ui::SettingsButton>::fromRaw(button));
				continue;
			}

			auto subtitle = QString();
			if (isFaq) {
				subtitle = faqSubtitle + u" > "_q + indexed.faqSection;
			} else {
				const auto parentsOnly = entry.id.isEmpty();
				subtitle = registry.sectionPath(entry.section, parentsOnly);
			}
			const auto hasIcon = entry.icon.icon != nullptr;
			const auto hasCheckIcon = !hasIcon
				&& (entry.checkIcon != Builder::SearchEntryCheckIcon::None);

			const auto it = _customizations.find(entry.id);
			const auto custom = (it != _customizations.end())
				? &it->second
				: nullptr;

			const auto &st = custom && custom->st
				? *custom->st
				: (hasIcon || hasCheckIcon)
				? st::settingsSearchResult
				: st::settingsSearchResultNoIcon;

			const auto button = CreateSearchResultButtonRaw(
				this,
				entry.title,
				subtitle,
				st,
				IconDescriptor{ entry.icon.icon },
				(hasCheckIcon
					? entry.checkIcon
					: Builder::SearchEntryCheckIcon::None));

			if (custom && custom->hook) {
				custom->hook(button);
			}

			if (isFaq) {
				const auto url = indexed.faqUrl;
				button->addClickHandler([=] {
					UrlClickHandler::Open(
						url,
						QVariant::fromValue(ClickHandlerContext{
							.sessionWindow = weak,
						}));
				});
			} else {
				const auto targetSection = entry.section;
				const auto controlId = entry.id;
				const auto deeplink = entry.deeplink;
				button->addClickHandler([=] {
					if (!deeplink.isEmpty()) {
						Core::App().openLocalUrl(
							deeplink,
							QVariant::fromValue(ClickHandlerContext{
								.sessionWindow = base::make_weak(controller()),
							}));
					} else {
						controller()->setHighlightControlId(controlId);
						showOther(targetSection);
					}
				});
			}

			_buttonCache.emplace(entryIndex, button);
			_list->add(object_ptr<Ui::SettingsButton>::fromRaw(button));
		}
	}

	_list->resizeToWidth(_list->width());
}

void Search::setStepDataReference(std::any &data) {
	_stepData = &data;
	if (_stepData->has_value()) {
		const auto state = std::any_cast<SearchSectionState>(_stepData);
		if (state && !state->query.isEmpty()) {
			if (_searchController) {
				_searchController->setQuery(state->query);
			} else {
				_pendingQuery = state->query;
			}
		}
	}
}

void Search::rebuildFaqResults() {
	if (_faqStartIndex >= int(_entries.size())) {
		return;
	}

	const auto faqSubtitle = tr::lng_settings_faq_subtitle(tr::now);
	const auto weak = base::make_weak(controller());

	for (auto i = _faqStartIndex; i < int(_entries.size()); ++i) {
		const auto &indexed = _entries[i];

		const auto cached = _buttonCache.find(i);
		if (cached != _buttonCache.end()) {
			const auto button = cached->second;
			button->show();
			_list->add(object_ptr<Ui::SettingsButton>::fromRaw(button));
			continue;
		}

		const auto subtitle = faqSubtitle + u" > "_q + indexed.faqSection;
		const auto button = CreateSearchResultButtonRaw(
			this,
			indexed.entry.title,
			subtitle,
			st::settingsSearchResultNoIcon,
			IconDescriptor{},
			Builder::SearchEntryCheckIcon::None);

		const auto url = indexed.faqUrl;
		button->addClickHandler([=] {
			UrlClickHandler::Open(
				url,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = weak,
				}));
		});

		_buttonCache.emplace(i, button);
		_list->add(object_ptr<Ui::SettingsButton>::fromRaw(button));
	}

	_list->resizeToWidth(_list->width());
}

} // namespace Settings
