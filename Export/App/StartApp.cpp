#include <PandaRuntime/PandaRuntime.hpp>

int startApp(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    return Panda::Runtime::run();
}
