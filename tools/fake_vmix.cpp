// Dev helper: a fake vMix Web API on http://localhost:8088 (or the port given as the first argument) so the
// vMix mode of Atem Color AI can be demoed without vMix. Prints every function call. Not installed.
#include "../tests/support/FakeVmixServer.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const quint16 port = argc > 1 ? quint16(QString::fromLocal8Bit(argv[1]).toUShort()) : quint16(8088);
    const bool exposeCc = argc > 2 && QString::fromLocal8Bit(argv[2]) == QLatin1String("--expose-cc");
    FakeVmixServer server(port, exposeCc);
    if (server.startAndWait() == 0) {
        std::fprintf(stderr, "fake vMix: cannot listen on port %u\n", unsigned(port));
        return 1;
    }
    std::printf("fake vMix listening on http://localhost:%u/api\n", unsigned(server.boundPort()));
    std::fflush(stdout);
    int shown = 0;
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, [&] {
        const QStringList c = server.calls();
        for (; shown < c.size(); ++shown)
            std::printf("call: %s\n", qPrintable(c.at(shown)));
        std::fflush(stdout);
    });
    t.start(200);
    return app.exec();
}
