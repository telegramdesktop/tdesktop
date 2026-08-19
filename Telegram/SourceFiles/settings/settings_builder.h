/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_user_privacy.h"
#include "base/object_ptr.h"
#include "settings/settings_common.h"
#include "settings/settings_type.h"

#include <variant>
#include <vector>

class EditPrivacyController;

namespace Ui {
class RpWidget;
class VerticalLayout;
class SettingsButton;
class Checkbox;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct SettingsButton;
} // namespace style

namespace st {
extern const int &boxRadius;
} // namespace st

namespace Ui {
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace tr {
template <typename ...Tags>
struct phrase;
} // namespace tr

namespace Settings::Builder {

class SectionBuilder;

enum class SearchEntryCheckIcon {
	None,
	Checked,
	Unchecked,
};
struct SearchEntry {
	QString id;
	QString title;
	QStringList keywords;
	Type section;
	IconDescriptor icon;
	SearchEntryCheckIcon checkIcon = SearchEntryCheckIcon::None;
	QString deeplink;

	explicit operator bool() const {
		return !id.isEmpty();
	}
};

using SearchEntriesIndexer = Fn<std::vector<SearchEntry>(
	not_null<::Main::Session*> session)>;

struct SearchIndexerEntry {
	Type sectionId;
	SearchEntriesIndexer indexer;
};

struct SectionMeta {
	Type id;
	Type parentId;
	not_null<const tr::phrase<>*> title;
	not_null<const style::icon*> icon;
};

class SearchRegistry {
public:
	static SearchRegistry &Instance();

	void add(
		not_null<const SectionMeta*> meta,
		SearchEntriesIndexer indexer);

	[[nodiscard]] std::vector<SearchEntry> collectAll(
		not_null<::Main::Session*> session) const;

	[[nodiscard]] QString sectionTitle(Type sectionId) const;
	[[nodiscard]] QString sectionPath(
		Type sectionId,
		bool parentsOnly = false) const;

private:
	std::vector<SearchIndexerEntry> _indexers;
	base::flat_map<Type, const SectionMeta*> _sections;

};


class BuildHelper {
public:
	BuildHelper(SectionMeta &&meta, FnMut<void(SectionBuilder&)> method);

	const SectionBuildMethod build;

	[[nodiscard]] std::vector<SearchEntry> index(
		not_null<::Main::Session*> session) const;

private:
	const SectionMeta _meta;
	mutable FnMut<void(SectionBuilder &)> _method;

};

struct WidgetContext {
	not_null<Ui::VerticalLayout*> container;
	not_null<Window::SessionController*> controller;
	Fn<void(Type)> showOther;
	Fn<bool()> isPaused;
	HighlightRegistry *highlights = nullptr;
};

struct SearchContext {
	Type sectionId;
	not_null<::Main::Session*> session;
	not_null<std::vector<SearchEntry>*> entries;
};

using BuildContext = std::variant<WidgetContext, SearchContext>;

class SectionBuilder {
public:
	explicit SectionBuilder(BuildContext context);

	void add(FnMut<void(const BuildContext &ctx)> method);

	using ToggledScopePtr = not_null<Ui::SlideWrap<Ui::VerticalLayout>*>;
	Ui::VerticalLayout *scope(
		FnMut<void()> method,
		rpl::producer<bool> shown = nullptr,
		FnMut<void(ToggledScopePtr)> hook = nullptr);

	struct WidgetToAdd {
		object_ptr<Ui::RpWidget> widget = { nullptr };
		QMargins margin;
		style::align align = style::al_left;
		HighlightArgs highlight;

		explicit operator bool() const {
			return widget != nullptr;
		}
	};
	Ui::RpWidget *add(
		FnMut<WidgetToAdd(const WidgetContext &ctx)> widget,
		FnMut<SearchEntry()> search = nullptr);

	struct ControlArgs {
		Fn<object_ptr<Ui::RpWidget>(not_null<Ui::VerticalLayout*>)> factory;
		QString id;
		rpl::producer<QString> title;
		style::margins margin;
		style::align align = style::al_left;
		HighlightArgs highlight;
		rpl::producer<bool> shown;

		QStringList keywords;
		IconDescriptor searchIcon;
		SearchEntryCheckIcon searchCheckIcon = SearchEntryCheckIcon::None;
	};
	Ui::RpWidget *addControl(ControlArgs &&args);

	struct ButtonArgs {
		QString id;
		rpl::producer<QString> title;
		const style::SettingsButton *st = nullptr;
		IconDescriptor icon;
		Ui::VerticalLayout *container = nullptr;
		rpl::producer<QString> label;
		rpl::producer<bool> toggled;
		Fn<void()> onClick;
		QStringList keywords;
		HighlightArgs highlight;
		rpl::producer<bool> shown;
	};
	Ui::SettingsButton *addButton(ButtonArgs &&args);

	struct SectionArgs {
		//QString id; // Sections should register themselves in search.
		rpl::producer<QString> title;
		Type targetSection;
		IconDescriptor icon;
		QStringList keywords;
	};
	Ui::SettingsButton *addSectionButton(SectionArgs &&args);

	struct PremiumButtonArgs {
		QString id;
		rpl::producer<QString> title;
		rpl::producer<QString> label;
		bool credits = false;
		Fn<void()> onClick;
		QStringList keywords;
	};
	Ui::SettingsButton *addPremiumButton(PremiumButtonArgs &&args);

	struct PrivacyButtonArgs {
		QString id;
		rpl::producer<QString> title;
		Api::UserPrivacy::Key key;
		Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory;
		bool premium = false;
		QStringList keywords;
	};
	Ui::SettingsButton *addPrivacyButton(PrivacyButtonArgs &&args);

	struct CheckboxArgs {
		QString id;
		rpl::producer<QString> title;
		bool checked = false;
		QStringList keywords;
		HighlightArgs highlight = { .radius = st::boxRadius };
		rpl::producer<bool> shown;
	};
	Ui::Checkbox *addCheckbox(CheckboxArgs &&args);

	struct SubsectionTitleArgs {
		QString id;
		rpl::producer<QString> title;
		QStringList keywords;
	};
	void addSubsectionTitle(SubsectionTitleArgs &&args);
	void addSubsectionTitle(rpl::producer<QString> text);
	void addDivider();
	void addDividerText(rpl::producer<QString> text);
	void addSkip();
	void addSkip(int height);

	[[nodiscard]] Ui::VerticalLayout *container() const;
	[[nodiscard]] Window::SessionController *controller() const;
	[[nodiscard]] not_null<::Main::Session*> session() const;
	[[nodiscard]] Fn<void(Type)> showOther() const;
	[[nodiscard]] HighlightRegistry *highlights() const;

private:
	void registerHighlight(
		QString id,
		QWidget *widget,
		HighlightArgs &&args);

	BuildContext _context;

};

} // namespace Settings::Builder
