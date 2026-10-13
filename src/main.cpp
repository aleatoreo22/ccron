#include "bridge.h"
#include "ccron.h"
#include "filemodule.h"
#include "localserver.h"
#include "qnamespace.h"
#include <QApplication>
#include <QDebug>
#include <QUrl>
#include <QtWebView>

int main(int argc, char *argv[]) {
  QString sitePath =
      qEnvironmentVariable("CCRON_DIST", QStringLiteral("./frontend/dist/"));

  QString viteUrl = qEnvironmentVariable(
      "CCRON_VITE_URL", QStringLiteral("http://127.0.0.1:5173/"));

  bool dev = qEnvironmentVariableIsSet("CCRON_DEV");

  for (int i = 1; i < argc; ++i) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == QStringLiteral("--dev")) {
      dev = true;
    } else if (a == QStringLiteral("--dist") && i + 1 < argc) {
      sitePath = QString::fromLocal8Bit(argv[++i]);
    } else if (a == QStringLiteral("--vite-url") && i + 1 < argc) {
      viteUrl = QString::fromLocal8Bit(argv[++i]);
    } else if (a == QStringLiteral("--port") && i + 1 < argc) {
      qputenv("CCRON_PORT", argv[++i]);
    }
  }

  QCoreApplication::setOrganizationName("QtExamples");
  QApplication app(argc, argv);
  QtWebView::initialize();

  auto *win = new CCron::Window(sitePath);

  win->server()->bridge()->registerObject(QStringLiteral("Arquivos"),
                                          new CCron::FileModule(win));

  auto width = 900;
  auto height = 560;
  win->setMaximumHeight(height);
  win->setMinimumHeight(height);
  win->setMaximumWidth(width);
  win->setMinimumWidth(width);

  // Dev e prod usam o mesmo caminho: a view e configurada na Window e o
  // Show() e unico. Em dev a view e o Vite (HMR); a API continua no
  // LocalServer desta mesma Window (win->apiUrl()).
  if (dev)
    win->setViewUrl(QUrl(viteUrl));
  qInfo() << "ccron: view=" << win->viewUrl().toString()
          << "api=" << win->apiUrl().toString() << "port=" << win->port();
  win->Show();

  return app.exec();
}
