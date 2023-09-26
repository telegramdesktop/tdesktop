/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/userpic_view.h"

namespace Data {
enum class StoryPrivacy : uchar;
} // namespace Data

namespace Ui {
class RpWidget;
class FlatLabel;
class IconButton;
class AbstractButton;
class UserpicButton;
class ImportantTooltip;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Media::Stories {

class Controller;
enum class PauseState;

struct HeaderData {
	not_null<PeerData*> peer;
	TimeId date = 0;
	int fullIndex = 0;
	int fullCount = 0;
	Data::StoryPrivacy privacy = {};
	bool edited = false;
	bool video = false;
	bool silent = false;

	friend inline auto operator<=>(HeaderData, HeaderData) = default;
	friend inline bool operator==(HeaderData, HeaderData) = default;
};

class Header final {
public:
	explicit Header(not_null<Controller*> controller);
	~Header();

	void updatePauseState();
	void updateVolumeIcon();

	void show(HeaderData data);
	void raise();

	[[nodiscard]] bool ignoreWindowMove(QPoint position) const;
	[[nodiscard]] rpl::producer<bool> tooltipShownValue() const;

private:
	enum class Tooltip {
		None,
		SilentVideo,
		Privacy,
	};

	void updateDateText();
	void applyPauseState();
	void createPlayPause();
	void createVolumeToggle();
	void rebuildVolumeControls(
		not_null<Ui::RpWidget*> dropdown,
		bool horizontal);
	void toggleTooltip(Tooltip type, bool show);
	void updateTooltipGeometry();

	const not_null<Controller*> _controller;

	PauseState _pauseState = {};

	std::unique_ptr<Ui::RpWidget> _widget;
	std::unique_ptr<Ui::AbstractButton> _info;
	std::unique_ptr<Ui::UserpicButton> _userpic;
	std::unique_ptr<Ui::FlatLabel> _name;
	std::unique_ptr<Ui::FlatLabel> _counter;
	std::unique_ptr<Ui::FlatLabel> _date;
	rpl::event_stream<> _dateUpdated;
	std::unique_ptr<Ui::RpWidget> _playPause;
	std::unique_ptr<Ui::RpWidget> _volumeToggle;
	std::unique_ptr<Ui::FadeWrap<Ui::RpWidget>> _volume;
	rpl::variable<const style::icon*> _volumeIcon;
	std::unique_ptr<Ui::RpWidget> _privacy;
	QRect _privacyBadgeGeometry;
	std::optional<HeaderData> _data;
	std::unique_ptr<Ui::ImportantTooltip> _tooltip;
	rpl::variable<bool> _tooltipShown = false;
	QRect _contentGeometry;
	Tooltip _tooltipType = {};
	base::Timer _dateUpdateTimer;
	bool _ignoreWindowMove = false;
	bool _privacyBadgeOver = false;

};

} // namespace Media::Stories
