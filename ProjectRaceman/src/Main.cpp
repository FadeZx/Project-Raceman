#include "Application.h"

int main() {
    using namespace raceman;

    ApplicationConfig config;
    Application app(config);

    app.Run();

    return 0;
}
