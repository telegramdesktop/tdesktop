/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "boxes/peers/peer_short_info_box.h"

struct PreparedShortInfoUserpic;

namespace style {
struct ShortInfoCover;
} // namespace style

namespace Calls {

namespace Group {
struct MuteRequest;
struct VolumeRequest;
struct ParticipantState;
} // namespace Group

class CoverItem final : public Ui::Menu::ItemBase {
public:
	CoverItem(
		not_null<RpWidget*> parent,
		const style::Menu &stMenu,
		const style::ShortInfoCover &st,
		rpl::producer<QString> name,
		rpl::producer<QString> status,
		PreparedShortInfoUserpic userpic);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;

	const PeerShortInfoCover _cover;
	const not_null<QAction*> _dummyAction;
	const style::ShortInfoCover &_st;

};

class AboutItem final : public Ui::Menu::ItemBase {
public:
	AboutItem(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		TextWithEntities &&about);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;

	const style::Menu &_st;
	const base::unique_qptr<Ui::FlatLabel> _text;
	const not_null<QAction*> _dummyAction;

};

} // namespace Calls
