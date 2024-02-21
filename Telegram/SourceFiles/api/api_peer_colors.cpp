/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_peer_colors.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "ui/chat/chat_style.h"

namespace Api {
namespace {

constexpr auto kRequestEach = 3600 * crl::time(1000);

} // namespace

PeerColors::PeerColors(not_null<ApiWrap*> api)
: _api(&api->instance())
, _timer([=] { request(); }) {
	request();
	_timer.callEach(kRequestEach);
}

PeerColors::~PeerColors() = default;

void PeerColors::request() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPhelp_GetPeerColors(
		MTP_int(_hash)
	)).done([=](const MTPhelp_PeerColors &result) {
		_requestId = 0;
		result.match([&](const MTPDhelp_peerColors &data) {
			_hash = data.vhash().v;
			apply(data);
		}, [](const MTPDhelp_peerColorsNotModified &) {
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

std::vector<uint8> PeerColors::suggested() const {
	return _suggested.current();
}

rpl::producer<std::vector<uint8>> PeerColors::suggestedValue() const {
	return _suggested.value();
}

auto PeerColors::indicesValue() const
-> rpl::producer<Ui::ColorIndicesCompressed> {
	return rpl::single(
		indicesCurrent()
	) | rpl::then(_colorIndicesChanged.events() | rpl::map([=] {
		return indicesCurrent();
	}));
}

Ui::ColorIndicesCompressed PeerColors::indicesCurrent() const {
	return _colorIndicesCurrent
		? *_colorIndicesCurrent
		: Ui::ColorIndicesCompressed();
}

const base::flat_map<uint8, int> &PeerColors::requiredLevelsGroup() const {
	return _requiredLevelsGroup;
}

const base::flat_map<uint8, int> &PeerColors::requiredLevelsChannel() const {
	return _requiredLevelsChannel;
}

int PeerColors::requiredGroupLevelFor(PeerId channel, uint8 index) const {
	if (Data::DecideColorIndex(channel) == index) {
		return 0;
	} else if (const auto i = _requiredLevelsGroup.find(index)
		; i != end(_requiredLevelsGroup)) {
		return i->second;
	}
	return 1;
}

int PeerColors::requiredChannelLevelFor(PeerId channel, uint8 index) const {
	if (Data::DecideColorIndex(channel) == index) {
		return 0;
	} else if (const auto i = _requiredLevelsChannel.find(index)
		; i != end(_requiredLevelsChannel)) {
		return i->second;
	}
	return 1;
}

void PeerColors::apply(const MTPDhelp_peerColors &data) {
	auto suggested = std::vector<uint8>();
	auto colors = std::make_shared<
		std::array<Ui::ColorIndexData, Ui::kColorIndexCount>>();

	using ParsedColor = std::array<uint32, Ui::kColorPatternsCount>;
	const auto parseColors = [](const MTPhelp_PeerColorSet &set) {
		return set.match([&](const MTPDhelp_peerColorSet &data) {
			auto result = ParsedColor();
			const auto &list = data.vcolors().v;
			if (list.empty() || list.size() > Ui::kColorPatternsCount) {
				LOG(("API Error: Bad count for PeerColorSet.colors: %1"
					).arg(list.size()));
				return ParsedColor();
			}
			auto fill = result.data();
			for (const auto &color : list) {
				*fill++ = (uint32(1) << 24) | uint32(color.v);
			}
			return result;
		}, [](const MTPDhelp_peerColorProfileSet &) {
			LOG(("API Error: peerColorProfileSet in colors result!"));
			return ParsedColor();
		});
	};

	const auto &list = data.vcolors().v;
	_requiredLevelsGroup.clear();
	_requiredLevelsChannel.clear();
	suggested.reserve(list.size());
	for (const auto &color : list) {
		const auto &data = color.data();
		const auto colorIndexBare = data.vcolor_id().v;
		if (colorIndexBare < 0 || colorIndexBare >= Ui::kColorIndexCount) {
			LOG(("API Error: Bad color index: %1").arg(colorIndexBare));
			continue;
		}
		const auto colorIndex = uint8(colorIndexBare);
		if (const auto min = data.vgroup_min_level()) {
			_requiredLevelsGroup[colorIndex] = min->v;
		}
		if (const auto min = data.vchannel_min_level()) {
			_requiredLevelsChannel[colorIndex] = min->v;
		}
		if (!data.is_hidden()) {
			suggested.push_back(colorIndex);
		}
		if (const auto light = data.vcolors()) {
			auto &fields = (*colors)[colorIndex];
			fields.light = parseColors(*light);
			if (const auto dark = data.vdark_colors()) {
				fields.dark = parseColors(*dark);
			} else {
				fields.dark = fields.light;
			}
		}
	}

	if (!_colorIndicesCurrent) {
		_colorIndicesCurrent = std::make_unique<Ui::ColorIndicesCompressed>(
			Ui::ColorIndicesCompressed{ std::move(colors) });
		_colorIndicesChanged.fire({});
	} else if (*_colorIndicesCurrent->colors != *colors) {
		_colorIndicesCurrent->colors = std::move(colors);
		_colorIndicesChanged.fire({});
	}
	_suggested = std::move(suggested);
}

} // namespace Api
