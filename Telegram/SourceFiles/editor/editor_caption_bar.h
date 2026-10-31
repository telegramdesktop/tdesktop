/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "editor/photo_editor_common.h"
#include "ui/rp_widget.h"

namespace ChatHelpers {
class Show;
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class AbstractButton;
class EmojiButton;
class IconButton;
class InputField;
class PopupMenu;
} // namespace Ui

namespace Editor {

class PeriodButton;
class RoundRecordButton;
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

	[[nodiscard]] rpl::producer<> submits() const;
	[[nodiscard]] rpl::producer<> cancels() const;

	void setInnerFocus();

	void setRoundRecording(bool recording);
	void setRoundRecordingAmplitude(float64 amplitude);
	void updateRecordButtonGeometry();
	[[nodiscard]] rpl::producer<> roundRequests() const;
	[[nodiscard]] rpl::producer<> roundStopRequests() const;
	[[nodiscard]] rpl::producer<> roundCancelRequests() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void paintRecording(QPainter &p, QRect pill);
	void hideEvent(QHideEvent *e) override;

private:
	void setupField();
	void setupEmoji();
	void setupPeriod();
	void updatePanelGeometry();
	void showPeriodMenu();
	void showPremiumHint(int period);
	void setPeriod(int period);

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Ui::RpWidget*> _panelParent;
	const base::unique_qptr<Ui::InputField> _field;
	const base::unique_qptr<Ui::EmojiButton> _emojiToggle;
	const base::unique_qptr<PeriodButton> _periodButton;
	const base::unique_qptr<Ui::IconButton> _roundButton;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<Ui::PopupMenu> _periodMenu;
	base::unique_qptr<Ui::AbstractButton> _recordCancel;
	base::unique_qptr<RoundRecordButton> _recordButton;
	int _period = 0;
	bool _periodChoice = false;
	bool _recording = false;
	crl::time _recordStarted = 0;
	Ui::Animations::Basic _recordAnimation;

	rpl::event_stream<> _submits;
	rpl::event_stream<> _cancels;
	rpl::event_stream<> _roundStops;
	rpl::event_stream<> _roundCancels;

};

} // namespace Editor
