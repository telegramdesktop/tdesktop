/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/peer_qr_colors_panel.h"
#include "ui/effects/peer_qr_gift_field.h"
#include "ui/rp_widget.h"

namespace Ui {

class ElasticScroll;
class PeerQrPanel;

class PeerQrGiftsPanel final : public RpWidget {
public:
	PeerQrGiftsPanel(
		not_null<PeerQrPanel*> panel,
		rpl::producer<QString> emptyAbout);
	~PeerQrGiftsPanel();

	[[nodiscard]] int openHeight(int width) const;

	void setSource(PeerQrGiftSource source);
	void setColors(const PeerQrColors &colors);
	void setGifts(std::vector<PeerQrSwatch> gifts, bool complete);
	void setBrowseShown(bool shown);
	void setSelected(std::optional<PeerQrThemeKey> key, anim::type animated);
	void scrollToSelected();

	[[nodiscard]] rpl::producer<PeerQrSwatch> picks() const;
	[[nodiscard]] rpl::producer<> loadMoreRequests() const;
	[[nodiscard]] rpl::producer<> browseRequests() const;
	[[nodiscard]] rpl::producer<> openHeightChanges() const;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;

	const not_null<ElasticScroll*> _scroll;
	const not_null<Inner*> _inner;
	rpl::event_stream<PeerQrSwatch> _picks;
	rpl::event_stream<> _loadMore;
	rpl::event_stream<> _browse;
	rpl::event_stream<> _openHeightChanges;

};

} // namespace Ui
