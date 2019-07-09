/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "reporters/catch_reporter_compact.hpp"
#include <QFile>

int (*TestForkedMethod)()/* = nullptr*/;

namespace base {
namespace assertion {

// For Assert() / Expects() / Ensures() / Unexpected() to work.
void log(const char *message, const char *file, int line) {
	std::cout << message << " (" << file << ":" << line << ")" << std::endl;
}

} // namespace assertion
} // namespace base

namespace Catch {

	struct MinimalReporter : CompactReporter {
		MinimalReporter( ReporterConfig const& _config )
        : CompactReporter( _config )
        {}

        virtual void testRunEnded( TestRunStats const& _testRunStats ) {
            printTotals( _testRunStats.totals );
        }

    private:
        // Colour, message variants:
        // - white: No tests ran.
        // -   red: Failed [both/all] N test cases, failed [both/all] M assertions.
        // - white: Passed [both/all] N test cases (no assertions).
        // -   red: Failed N tests cases, failed M assertions.
        // - green: Passed [both/all] N tests cases with M assertions.

        std::string bothOrAll( std::size_t count ) const {
            return count == 1 ? std::string() : count == 2 ? "both " : "all " ;
        }

        void printTotals( const Totals& totals ) const {
            if( totals.testCases.total() == 0 ) {
            }
            else if( totals.testCases.failed == totals.testCases.total() ) {
                Colour colour( Colour::ResultError );
                const std::string qualify_assertions_failed =
                    totals.assertions.failed == totals.assertions.total() ?
                        bothOrAll( totals.assertions.failed ) : std::string();
                stream <<
                    "Failed " << bothOrAll( totals.testCases.failed )
                              << pluralise( totals.testCases.failed, "test case"  ) << ", "
                    "failed " << qualify_assertions_failed <<
                                 pluralise( totals.assertions.failed, "assertion" ) << '.';
            }
            else if( totals.assertions.total() == 0 ) {
                stream <<
                    "Passed " << bothOrAll( totals.testCases.total() )
                              << pluralise( totals.testCases.total(), "test case" )
                              << " (no assertions).";
            }
            else if( totals.assertions.failed ) {
                Colour colour( Colour::ResultError );
                stream <<
                    "Failed " << pluralise( totals.testCases.failed, "test case"  ) << ", "
                    "failed " << pluralise( totals.assertions.failed, "assertion" ) << '.';
            }
            else {
            }
        }
    };

    INTERNAL_CATCH_REGISTER_REPORTER( "minimal", MinimalReporter )

} // end namespace Catch

int main(int argc, const char *argv[]) {
	auto touchFile = QString();
	for (auto i = 0; i != argc; ++i) {
		if (argv[i] == QString("--touch") && i + 1 != argc) {
			touchFile = QFile::decodeName(argv[++i]);
		} else if (argv[i] == QString("--forked") && TestForkedMethod) {
			return TestForkedMethod();
		}
	}
	const char *catch_argv[] = {
		argv[0],
		touchFile.isEmpty() ? "-b" : "-r",
		touchFile.isEmpty() ? "-b" : "minimal" };
	constexpr auto catch_argc = sizeof(catch_argv) / sizeof(catch_argv[0]);
	auto result = Catch::Session().run(catch_argc, catch_argv);
	if (result == 0 && !touchFile.isEmpty()) {
		QFile(touchFile).open(QIODevice::WriteOnly);
	}
	return (result < 0xff ? result : 0xff);
}

