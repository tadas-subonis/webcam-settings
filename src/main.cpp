#include "main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Webcam Settings"));
    QCoreApplication::setApplicationVersion(QStringLiteral(WEBCAM_SETTINGS_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Tadas"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cross-platform physical webcam settings."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save an in-application screenshot after startup, then exit."),
        QStringLiteral("path"));
    parser.addOption(screenshotOption);
    parser.process(application);

    MainWindow window;
    window.show();

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        QTimer::singleShot(3000, &window, [&application, &window, path] {
            const bool saved = window.grab().save(path);
            QTextStream(saved ? stdout : stderr)
                << (saved ? "Saved screenshot to " : "Could not save screenshot to ") << path << '\n';
            application.exit(saved ? 0 : 2);
        });
    }
    return application.exec();
}
