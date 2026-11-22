struct Item {
	[[nodiscard]] int value() const;
};

int Item::value() const {
	return 1;
}
