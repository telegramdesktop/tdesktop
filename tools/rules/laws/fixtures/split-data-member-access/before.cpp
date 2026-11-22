struct Value {
	int number = 0;
	int amount() const;
};
int field(Value value) {
	return value
		.number;
}
int method(Value value) {
	return value
		.amount();
}
