/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_saved.h"

#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/tabs/info_profile_tab_skeleton.h"
#include "info/saved/info_saved_sublist_inline.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

using SharedMediaType = Storage::SharedMediaType;

class SavedSubController final : public AbstractController {
public:
	SavedSubController(
		not_null<AbstractController*> parent,
		not_null<Data::SavedSublist*> sublist)
	: AbstractController(parent->parentController())
	, _key(sublist) {
	}

	Key key() const override {
		return _key;
	}
	PeerData *migrated() const override {
		return nullptr;
	}
	::Info::Section section() const override {
		return ::Info::Section(::Info::Section::Type::SavedSublists);
	}

private:
	const Key _key;

};

class SavedTabAdapter final : public MediaTabContent {
public:
	explicit SavedTabAdapter(MediaTabContext context)
	: _countPeer(context.sublist
		? context.sublist->sublistPeer()
		: context.peer)
	, _sublist(_countPeer->owner().savedMessages().sublist(_countPeer))
	, _subController(context.controller, _sublist)
	, _host(context.parent) {
		const auto host = _host.data();
		_saved = Saved::MakeInlineSublist(
			host,
			&_subController,
			_sublist,
			[scrollTo = context.scrollToRequest](int top) {
				if (scrollTo) {
					scrollTo(top, -1);
				}
			});
		_skeleton = CreateTabSkeleton(host, SharedMediaType::File);
		_skeleton->show();

		host->paintRequest(
		) | rpl::on_next([this](QRect clip) {
			if (!skeletonShown()) {
				auto p = QPainter(_host.data());
				_saved.paintBackground(p, clip);
			}
		}, host->lifetime());
		host->widthValue(
		) | rpl::on_next([this](int newWidth) {
			_width = newWidth;
			updateSavedGeometry();
		}, host->lifetime());
		host->sizeValue(
		) | rpl::on_next([this](QSize size) {
			_skeleton->setGeometry(QRect(QPoint(), size));
		}, host->lifetime());
		rpl::duplicate(
			_saved.firstSliceLoaded
		) | rpl::on_next([this] {
			if (!_listLoaded) {
				_listLoaded = true;
				_skeleton->hide();
				updateHostHeight();
			}
		}, host->lifetime());
		_saved.list->heightValue(
		) | rpl::on_next([this] {
			updateHostHeight();
		}, host->lifetime());
	}

	not_null<Ui::RpWidget*> widget() override {
		return _host.data();
	}
	TabTopBarBindings topBarBindings() override {
		return {
			.title = tr::lng_media_type_saved(
			) | rpl::map([](const QString &text) {
				return TextWithEntities{ text };
			}),
			.subtitle = SavedSublistCountValue(
				_countPeer
			) | rpl::map([](int count) {
				return TextWithEntities{ (count > 0)
					? tr::lng_profile_saved_messages(
						tr::now,
						lt_count,
						count)
					: QString() };
			}),
			.selectedItems = rpl::duplicate(_saved.selectedItems),
			.selectionAction = crl::guard(
				base::make_weak(_host.data()),
				[this](SelectionAction action) {
					_saved.selectionAction(action);
				}),
		};
	}

	void deactivated() override {
		_saved.selectionAction(SelectionAction::Clear);
	}

	void setVisibleRegion(int top, int bottom) override {
		const auto height = bottom - top;
		if (_viewportHeight != height) {
			_viewportHeight = height;
			updateSavedGeometry();
		}
		_saved.setVisibleRegion(top, bottom);
		_host->update();
	}

private:
	[[nodiscard]] bool skeletonShown() const {
		return !_listLoaded;
	}

	void updateSavedGeometry() {
		if (_width > 0 && _viewportHeight > 0) {
			_saved.updateGeometry(_width, _viewportHeight);
		}
		updateHostHeight();
	}

	void updateHostHeight() {
		const auto height = skeletonShown()
			? st::infoMediaSkeletonMinHeight
			: _saved.list->height();
		if (_host->height() != height) {
			_host->resize(_host->width(), height);
		}
	}

	const not_null<PeerData*> _countPeer;
	const not_null<Data::SavedSublist*> _sublist;
	SavedSubController _subController;
	object_ptr<Ui::RpWidget> _host;
	Saved::InlineSublist _saved;
	object_ptr<Ui::RpWidget> _skeleton = { nullptr };
	int _width = 0;
	int _viewportHeight = 0;
	bool _listLoaded = false;

};

} // namespace

MediaTabDescriptor MakeSavedTabDescriptor(not_null<PeerData*> peer) {
	using namespace rpl::mappers;
	return {
		.id = u"saved"_q,
		.title = tr::lng_media_type_saved(),
		.shown = SavedSublistCountValue(peer) | rpl::map(_1 > 0),
		.factory = [](MediaTabContext context) {
			return std::make_unique<SavedTabAdapter>(std::move(context));
		},
	};
}

} // namespace Info::Profile
