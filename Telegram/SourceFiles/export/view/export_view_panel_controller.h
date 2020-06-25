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

namespace Main {
class Session;
} // namespace Main

namespace Export {
namespace View {

QPointer<Ui::BoxContent> SuggestStart(not_null<Main::Session*> session);
void ClearSuggestStart(not_null<Main::Session*> session);
bool IsDefaultPath(not_null<Main::Session*> session, const QString &path);
void ResolveSettings(not_null<Main::Session*> session, Settings &settings);

class Panel;

class PanelController {
public:
	PanelController(
		not_null<Main::Session*> session,
		not_null<Controller*> process);
	~PanelController();

	[[nodiscard]] Main::Session &session() const {
		return *_session;
	}

	void activatePanel();
	void stopWithConfirmation(FnMut<void()> callback = nullptr);

	[[nodiscard]] rpl::producer<> stopRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
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

	const not_null<Main::Session*> _session;
	const not_null<Controller*> _process;
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
