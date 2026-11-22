struct Item {
	[[nodiscard]] int value() const;
};

[[nodiscard]] int Item::value() const {
	return 1;
}
