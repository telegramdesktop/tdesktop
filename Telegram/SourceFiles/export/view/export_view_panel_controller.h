/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_controller.h"
#include "export/view/export_view_content.h"
#include "base/unique_qptr.h"
#include "base/timer.h"

namespace Ui {
class SeparatePanel;
class BoxContent;
} // namespace Ui

namespace Export {

struct Environment;

namespace View {

Environment PrepareEnvironment();
QPointer<Ui::BoxContent> SuggestStart();
void ClearSuggestStart();
bool IsDefaultPath(const QString &path);
void ResolveSettings(Settings &settings);

class Panel;

class PanelController {
public:
	PanelController(not_null<Controller*> process);
	~PanelController();

	void activatePanel();
	void stopWithConfirmation(FnMut<void()> callback = nullptr);

	rpl::producer<> stopRequests() const;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	auto progressState() const {
		return ContentFromState(_settings.get(), _process->state());
	}

private:
	void fillParams(const PasswordCheckState &state);
	void stopExport();
	void createPanel();
	void updateState(State &&state);
	void showSettings();
	void showProgress();
	void showError(const ApiErrorState &error);
	void showError(const OutputErrorState &error);
	void showError(const QString &text);
	void showCriticalError(const QString &text);

	void saveSettings() const;

	not_null<Controller*> _process;
	std::unique_ptr<Settings> _settings;
	base::Timer _saveSettingsTimer;

	base::unique_qptr<Ui::SeparatePanel> _panel;

	State _state;
	QPointer<Ui::BoxContent> _confirmStopBox;
	rpl::event_stream<rpl::producer<>> _panelCloseEvents;
	bool _stopRequested = false;
	rpl::lifetime _lifetime;

};

} // namespace View
} // namespace Export
