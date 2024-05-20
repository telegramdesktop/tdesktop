/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/chat_search_tabs.h"

#include "lang/lang_keys.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/shadow.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] QString TabLabel(
		ChatSearchTab tab,
		ChatSearchPeerTabType type = {}) {
	switch (tab) {
	case ChatSearchTab::MyMessages:
		return tr::lng_search_tab_my_messages(tr::now);
	case ChatSearchTab::ThisTopic:
		return tr::lng_search_tab_this_topic(tr::now);
	case ChatSearchTab::ThisPeer:
		switch (type) {
		case ChatSearchPeerTabType::Chat:
			return tr::lng_search_tab_this_chat(tr::now);
		case ChatSearchPeerTabType::Channel:
			return tr::lng_search_tab_this_channel(tr::now);
		case ChatSearchPeerTabType::Group:
			return tr::lng_search_tab_this_group(tr::now);
		}
		Unexpected("Type in Dialogs::TabLabel.");
	case ChatSearchTab::PublicPosts:
		return tr::lng_search_tab_public_posts(tr::now);
	}
	Unexpected("Tab in Dialogs::TabLabel.");
}

} // namespace

TextWithEntities DefaultShortLabel(ChatSearchTab tab) {
	// Return them in QString::fromUtf8 format.
	switch (tab) {
	case ChatSearchTab::MyMessages:
		return { QString::fromUtf8("\xf0\x9f\x93\xa8") };
	case ChatSearchTab::PublicPosts:
		return { QString::fromUtf8("\xf0\x9f\x8c\x8e") };
	}
	Unexpected("Tab in Dialogs::DefaultShortLabel.");
}

FixedHashtagSearchQuery FixHashtagSearchQuery(
		const QString &query,
		int cursorPosition) {
	const auto trimmed = query.trimmed();
	const auto hash = trimmed.isEmpty()
		? query.size()
		: query.indexOf(trimmed);
	const auto start = std::min(cursorPosition, hash);
	auto result = query.mid(0, start);
	for (const auto &ch : query.mid(start)) {
		if (ch.isSpace()) {
			if (cursorPosition > result.size()) {
				--cursorPosition;
			}
			continue;
		} else if (result.size() == start) {
			result += '#';
			if (ch != '#') {
				++cursorPosition;
			}
		}
		if (ch != '#') {
			result += ch;
		}
	}
	if (result.size() == start) {
		result += '#';
		++cursorPosition;
	}
	return { result, cursorPosition };
}

bool IsHashtagSearchQuery(const QString &query) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty() || trimmed[0] != '#') {
		return false;
	}
	for (const auto &ch : trimmed) {
		if (ch.isSpace()) {
			return false;
		}
	}
	return true;
}

ChatSearchTabs::ChatSearchTabs(QWidget *parent, ChatSearchTab active)
: RpWidget(parent)
, _tabs(std::make_unique<Ui::SettingsSlider>(this, st::dialogsSearchTabs))
, _shadow(std::make_unique<Ui::PlainShadow>(this))
, _active(active) {
	for (const auto tab : {
		ChatSearchTab::ThisTopic,
		ChatSearchTab::ThisPeer,
		ChatSearchTab::MyMessages,
		ChatSearchTab::PublicPosts,
	}) {
		_list.push_back({ tab, TabLabel(tab) });
	}
	_tabs->move(st::dialogsSearchTabsPadding, 0);
	_tabs->sectionActivated(
	) | rpl::start_with_next([=](int index) {
		for (const auto &tab : _list) {
			if (tab.shortLabel.empty()) {
				continue;
			} else if (!index) {
				_active = tab.value;
				return;
			}
			--index;
		}
	}, lifetime());
}

ChatSearchTabs::~ChatSearchTabs() = default;

void ChatSearchTabs::setTabShortLabels(
		std::vector<ShortLabel> labels,
		ChatSearchTab active,
		ChatSearchPeerTabType peerTabType) {
	for (const auto &label : labels) {
		const auto i = ranges::find(_list, label.tab, &Tab::value);
		Assert(i != end(_list));
		i->shortLabel = std::move(label.label);
		if (i->value == ChatSearchTab::ThisPeer) {
			i->label = TabLabel(label.tab, peerTabType);
		}
	}
	refreshTabs(active);
}

rpl::producer<ChatSearchTab> ChatSearchTabs::tabChanges() const {
	return _active.changes();
}

void ChatSearchTabs::refreshTabs(ChatSearchTab active) {
	auto index = 0;
	auto labels = std::vector<QString>();
	for (const auto &tab : _list) {
		if (tab.value == active) {
			index = int(labels.size());
			Assert(!tab.shortLabel.empty());
			labels.push_back(tab.label);
		} else if (!tab.shortLabel.empty()) {
			labels.push_back(tab.label);
		}
	}
	_tabs->setSections(labels);
	_tabs->setActiveSectionFast(index);
	resizeToWidth(width());
}

int ChatSearchTabs::resizeGetHeight(int newWidth) {
	_tabs->resizeToWidth(newWidth);
	_shadow->setGeometry(
		0,
		_tabs->y() + _tabs->height() - st::lineWidth,
		newWidth,
		st::lineWidth);
	return _tabs->height();
}

void ChatSearchTabs::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(e->rect(), st::dialogsBg);
}

} // namespace Dialogs