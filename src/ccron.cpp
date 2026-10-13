#include "ccron.h"
#include "localserver.h"
#include "qlogging.h"

#include <QLoggingCategory>
#include <QVBoxLayout>
#include <QWebView>
#include <QWebViewLoadingInfo>

namespace CCron {
namespace {

void applyDefaultSettings(QWebView *webview) {
  auto *s = webview->settings();
  s->setAttribute(QWebViewSettings::WebAttribute::JavaScriptEnabled, true);
  s->setAttribute(QWebViewSettings::WebAttribute::LocalStorageEnabled, true);
}

} // namespace

Window::Window(const QString &applicationPath, const QString &baseFile,
               QWidget *parent)
    : QWidget(parent), m_applicationPath(applicationPath) {
  m_server = new LocalServer(applicationPath, this);
  if (!m_server->start())
    qWarning()
        << "ccron: servidor local nao iniciou, view vai dar erro de conexao";

  // view padrao = http://127.0.0.1:<porta>/<baseFile> (prod).
  // setViewUrl() troca p/ o Vite em dev; a API continua no m_server.
  QString base = baseFile;
  if (base.isEmpty())
    base = QStringLiteral("index.html");
  m_viewUrl = m_server->baseUrl().resolved(QUrl(base));

  // QWebView e QWindow: embute no QWidget via container.
  m_webview = new QWebView();
  applyDefaultSettings(m_webview);

  connect(m_webview, &QWebView::loadingChanged, m_webview,
          [](const QWebViewLoadingInfo &info) {
            qDebug() << "ccron: webview status=" << (int)info.status()
                     << "url=" << info.url().toString()
                     << "error=" << info.errorString();
          });
  connect(m_webview, &QWebView::titleChanged, m_webview,
          [](const QString &title) { qDebug() << "ccron: title=" << title; });
  connect(m_webview, &QWebView::urlChanged, m_webview,
          [](const QUrl &url) { qDebug() << "ccron: urlChanged=" << url; });

  QWidget *container = QWidget::createWindowContainer(m_webview, this);
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->addWidget(container);
  setLayout(m_layout);
}

quint16 Window::port() const { return m_server ? m_server->port() : 0; }

QUrl Window::apiUrl() const { return m_server ? m_server->baseUrl() : QUrl(); }

void Window::setViewUrl(const QUrl &url) {
  if (!url.isValid() || url.isEmpty()) {
    qWarning() << "ccron: setViewUrl ignorado, url invalida:" << url.toString();
    return;
  }
  m_viewUrl = url;
}

void Window::Show(const QUrl &url) {
  QUrl final = url.isEmpty() ? m_viewUrl : m_viewUrl.resolved(url);
  qDebug() << "ccron: view=" << final.toString();
  if (final.authority() != m_viewUrl.authority()) {
    qWarning() << "ccron: Show rejeitado, fora da view:" << final.toString()
               << "view:" << m_viewUrl.toString();
    return;
  }

  m_webview->setUrl(final);
  QWidget::show();
}

} // namespace CCron
