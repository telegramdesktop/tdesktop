auto caption(bool populated) {
	return populated ? label() : TextWithEntities();
}

auto fallback() {
	return TextWithEntities{};
}
