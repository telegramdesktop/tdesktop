/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

namespace Ui {
class PlainShadow;
class DynamicImage;
class IconButton;
class PopupMenu;
} // namespace Ui

namespace Dialogs {

enum class ChatSearchTab : uchar {
	MyMessages,
	ThisTopic,
	ThisPeer,
	PublicPosts,
};

enum class ChatSearchPeerTabType : uchar {
	Chat,
	Channel,
	Group,
};

class ChatSearchIn final : public Ui::RpWidget {
public:
	explicit ChatSearchIn(QWidget *parent);
	~ChatSearchIn();

	struct PossibleTab {
		ChatSearchTab tab = {};
		std::shared_ptr<Ui::DynamicImage> icon;
	};
	void apply(
		std::vector<PossibleTab> tabs,
		ChatSearchTab active,
		ChatSearchPeerTabType peerTabType,
		std::shared_ptr<Ui::DynamicImage> fromUserpic,
		QString fromName);

	[[nodiscard]] rpl::producer<> cancelInRequests() const;
	[[nodiscard]] rpl::producer<> cancelFromRequests() const;
	[[nodiscard]] rpl::producer<> changeFromRequests() const;
	[[nodiscard]] rpl::producer<ChatSearchTab> tabChanges() const;

private:
	struct Section {
		std::unique_ptr<Ui::RpWidget> outer;
		std::unique_ptr<Ui::IconButton> cancel;
		std::unique_ptr<Ui::PlainShadow> shadow;
		std::shared_ptr<Ui::DynamicImage> image;
		Ui::Text::String text;
		rpl::event_stream<> clicks;
		rpl::event_stream<> cancelRequests;
		bool subscribed = false;

		void update();
	};

	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void showMenu();

	void updateSection(
		not_null<Section*> section,
		std::shared_ptr<Ui::DynamicImage> image,
		TextWithEntities text);

	Section _in;
	Section _from;
	rpl::variable<ChatSearchTab> _active;

	base::unique_qptr<Ui::PopupMenu> _menu;

	std::vector<PossibleTab> _tabs;
	ChatSearchPeerTabType _peerTabType = ChatSearchPeerTabType::Chat;

};

struct FixedHashtagSearchQuery {
	QString text;
	int cursorPosition = 0;
};
[[nodiscard]] FixedHashtagSearchQuery FixHashtagSearchQuery(
	const QString &query,
	int cursorPosition);

[[nodiscard]] bool IsHashtagSearchQuery(const QString &query);

} // namespace Dialogs
