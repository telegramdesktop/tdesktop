/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

class HistoryItem;
struct HistoryMessageMarkupButton;

namespace Main {
class Session;
} // namespace Main

class UrlAuthBox : public Ui::BoxContent {
public:
	static void Activate(
		not_null<const HistoryItem*> message,
		int row,
		int column);
	static void Activate(
		not_null<Main::Session*> session,
		const QString &url,
		QVariant context);

protected:
	void prepare() override;

private:
	static void Request(
		const MTPDurlAuthResultRequest &request,
		not_null<const HistoryItem*> message,
		int row,
		int column);
	static void Request(
		const MTPDurlAuthResultRequest &request,
		not_null<Main::Session*> session,
		const QString &url,
		QVariant context);

	enum class Result {
		None,
		Auth,
		AuthAndAllowWrite,
	};

public:
	UrlAuthBox(
		QWidget*,
		not_null<Main::Session*> session,
		const QString &url,
		const QString &domain,
		UserData *bot,
		Fn<void(Result)> callback);

private:
	not_null<Ui::RpWidget*> setupContent(
		not_null<Main::Session*> session,
		const QString &url,
		const QString &domain,
		UserData *bot,
		Fn<void(Result)> callback);

	Fn<void()> _callback;
	not_null<Ui::RpWidget*> _content;

};
