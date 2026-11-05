#include "lunaris/State.h"
#include <iostream>
#include <string>

int Add(int a, int b) {
    return a + b;
}

int main() {
    lunaris::State lua;
    // 1. register C++-function:
    {
        lua["Add"] = Add;

        // 2. Define Luau functions and call them:
        auto script = lua.load_script(R"(
            function multiply(a:number, b:number): number
                return a * b
            end
            
            function doSomething()
                return Add(multiply(2, 1), 3)
            end)",
                                      "test.luau");
        if (!script) {
            std::cout << "Failed to load Script" << std::endl;
        }

        auto result = script.value().execute();

        if (!result) {
            auto error = result.error();
            std::cout << error.message() << std::endl;
        }

        auto resultValue = lua["multiply"].call_as<int>(6, 7);

        if (resultValue.value() != 42) {
            std::cout << "Multiplication failed" << std::endl;
        }

        auto doSomething = lua.get_function("doSomething");

        if (!doSomething) {
            std::cout << "Missing Global or not a functioin" << std::endl
                      << doSomething.error().message() << std::endl;
        }

        Function &fn = doSomething.value();

        Result<int> returnValue = fn.call_as<int>();

        if (!returnValue) {
            std::cout << returnValue.error().message() << std::endl;
        } else {
            if (returnValue.value() != 5) {
                std::cout << "Calling doSomething gave the wrong return Value"
                          << std::endl;
            }
        }
    }
    // 3. Wrong Type for return and arguments:

    // Wrong Type for return Value:
    {
        Result<std::string> result = lua["multiply"].call_as<std::string>(6, 7);

        if (!result) {
            std::cout << result.message() << std::endl;
        }
    }

    // Wrong Type for Arguments:
    {
        Result<int> result = lua["multiply"].call_as<int>("lol", "lol");

        if (!result) {
            std::cout << result.message() << std::endl;
        }
    }

    // Wrong Type for both return value and Arguments:
    {
        Result<bool> result = lua["multiply"].call_as<bool>(true, "lol");

        if (!result) {
            std::cout << result.message() << std::endl;
        }
    }
}