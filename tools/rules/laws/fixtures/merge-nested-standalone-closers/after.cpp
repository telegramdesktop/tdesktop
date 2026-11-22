int inner(int value);
void sink(int value);
void run() {
	sink(
		inner(
			1));
}
