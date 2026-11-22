struct QBuffer {
	explicit QBuffer(int *) {}
};

void run(int &result) {
	auto value = QBuffer(&result);
}
