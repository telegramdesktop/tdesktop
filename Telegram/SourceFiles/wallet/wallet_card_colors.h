/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "mtproto/sender.h"

#include "styles/style_widgets.h"

class DocumentData;

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace Wallet {

struct CollectibleRecord;

[[nodiscard]] std::vector<QString> CardColorCollections(
	not_null<Main::Session*> session);
[[nodiscard]] bool CardColorAllowed(
	const CollectibleRecord &record,
	const std::vector<QString> &collections);

class CardColorToast final : public base::has_weak_ptr {
public:
	explicit CardColorToast(std::shared_ptr<Main::SessionShow> show);
	~CardColorToast();

	void show(
		const std::vector<QString> &collections,
		const std::vector<CollectibleRecord> &held);

private:
	struct Metadata {
		QString title;
		uint64 giftId = 0;
		not_null<DocumentData*> sticker;
	};

	void cancelResolution();
	void resolve(const QString &slug);
	void remember(const QString &slug, const Data::UniqueGift *unique);
	void complete(const QString &slug, const Data::UniqueGift *unique);
	void showResolved();
	void openResale(uint64 giftId, const QString &title);
	void dropToast();
	void cleanup();

	const std::shared_ptr<Main::SessionShow> _show;
	style::complex_color _linkFg;
	style::Toast _st;
	MTP::Sender _api;
	base::flat_map<QString, Metadata> _metadata;
	base::flat_map<QString, mtpRequestId> _requests;
	std::vector<QString> _collections;
	base::Timer _timeout;
	base::weak_ptr<Ui::Toast::Instance> _toast;
	rpl::lifetime _resaleLifetime;
	rpl::lifetime _lifetime;
	bool _pending = false;

};

} // namespace Wallet
