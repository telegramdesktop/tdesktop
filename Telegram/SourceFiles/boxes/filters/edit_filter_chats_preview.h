/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_chat_filters.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

class History;

namespace Ui {
class IconButton;
} // namespace Ui

class FilterChatsPreview final : public Ui::RpWidget {
public:
	using Flag = Data::ChatFilter::Flag;
	using Flags = Data::ChatFilter::Flags;

	FilterChatsPreview(
		not_null<QWidget*> parent,
		Flags flags,
		const base::flat_set<not_null<History*>> &peers);

	[[nodiscard]] rpl::producer<Flag> flagRemoved() const;
	[[nodiscard]] rpl::producer<not_null<History*>> peerRemoved() const;

	void updateData(
		Flags flags,
		const base::flat_set<not_null<History*>> &peers);

	int resizeGetHeight(int newWidth) override;

private:
	using Button = base::unique_qptr<Ui::IconButton>;
	struct FlagButton {
		Flag flag = Flag();
		Button button;
	};
	struct PeerButton {
		not_null<History*> history;
		Ui::PeerUserpicView userpic;
		Ui::Text::String name;
		Button button;
	};

	void paintEvent(QPaintEvent *e) override;

	void refresh();
	void removeFlag(Flag flag);
	void removePeer(not_null<History*> history);

	std::vector<FlagButton> _removeFlag;
	std::vector<PeerButton> _removePeer;

	rpl::event_stream<Flag> _flagRemoved;
	rpl::event_stream<not_null<History*>> _peerRemoved;

};
