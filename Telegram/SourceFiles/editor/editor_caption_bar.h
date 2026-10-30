/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/photo_editor_common.h"
#include "ui/rp_widget.h"

namespace ChatHelpers {
class Show;
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class EmojiButton;
class IconButton;
class InputField;
class PopupMenu;
} // namespace Ui

namespace Editor {

class AudioTimeline;
class PeriodButton;
struct StoryOptions;

class CaptionBar final : public Ui::RpWidget {
public:
	CaptionBar(
		not_null<QWidget*> parent,
		not_null<Ui::RpWidget*> panelParent,
		const StoryOptions &options);
	~CaptionBar();

	[[nodiscard]] TextWithTags caption() const;
	[[nodiscard]] int period() const;
	[[nodiscard]] StoryAudio audio() const;
	void chooseAudio();
	[[nodiscard]] rpl::producer<> audioChanges() const;

	[[nodiscard]] rpl::producer<> submits() const;
	[[nodiscard]] rpl::producer<> cancels() const;

	void setInnerFocus();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void hideEvent(QHideEvent *e) override;

private:
	void setupField();
	void setupEmoji();
	void setupPeriod();
	void refreshAudioTimeline();
	void updatePanelGeometry();
	void showPeriodMenu();
	void showPremiumHint(int period);
	void setPeriod(int period);

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Ui::RpWidget*> _panelParent;
	const base::unique_qptr<Ui::InputField> _field;
	const base::unique_qptr<Ui::EmojiButton> _emojiToggle;
	const base::unique_qptr<PeriodButton> _periodButton;
	const base::unique_qptr<Ui::IconButton> _musicButton;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<Ui::PopupMenu> _periodMenu;
	base::unique_qptr<AudioTimeline> _audioTimeline;
	int _period = 0;
	bool _musicChoice = false;
	StoryAudio _audio;

	rpl::event_stream<> _submits;
	rpl::event_stream<> _cancels;
	rpl::event_stream<> _audioChanges;

};

} // namespace Editor
