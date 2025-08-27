/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct SponsoredMessageDetails;
struct SponsoredReportAction;
} // namespace Data

namespace Ui {
class RpWidget;
namespace Menu {
struct MenuCallback;
} // namespace Menu
} // namespace Ui

class HistoryItem;

namespace Menu {

enum class SponsoredPhrases {
	Channel,
	Bot,
	Search,
};

struct SponsoredMenuSettings {
	bool dark = false;
	bool skipAbout = false;
	bool skipInfo = false;
};

void FillSponsored(
	const Ui::Menu::MenuCallback &addAction,
	std::shared_ptr<ChatHelpers::Show> show,
	SponsoredPhrases phrases,
	const Data::SponsoredMessageDetails &details,
	Data::SponsoredReportAction report,
	SponsoredMenuSettings settings = {});

void FillSponsored(
	const Ui::Menu::MenuCallback &addAction,
	std::shared_ptr<ChatHelpers::Show> show,
	const FullMsgId &fullId,
	SponsoredMenuSettings settings = {});

void ShowSponsored(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	const FullMsgId &fullId);

void ShowSponsoredAbout(
	std::shared_ptr<ChatHelpers::Show> show,
	const FullMsgId &fullId);

} // namespace Menu
