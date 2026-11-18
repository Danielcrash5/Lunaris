// Shows how to run a script and handle a compile error with safeScript().
// The program checks its results and exits with a non-zero value if they do
// not match the documented behavior, so CI notices.
#include <lunaris/state.h>

#include <iostream>
#include <string>

int main() {
    lunaris::State state;

    //! [run script]
    const lunaris::ScriptResult result =
        state.script("return 2 + 3, 'hello'", "example");
    //! [run script]

    if (result.size() != 2 || result.get<double>(0) != 5.0 ||
        result.get<std::string>(1) != "hello") {
        std::cerr << "Unexpected script results\n";
        return 1;
    }

    //! [safe script]
    const auto failed = state.safeScript("return )", "broken_example");
    //! [safe script]

    if (failed || failed.error().kind() != lunaris::ErrorKind::compile) {
        std::cerr << "Expected safeScript() to return a compile error\n";
        return 1;
    }

    std::cout << "Script returned two values; safeScript captured a compile "
                 "error.\n";
    return 0;
}
