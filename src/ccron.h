#pragma once

#include "qboxlayout.h"
#include "qwidget.h"
#include <QUrl>
#include <QWidget>

class QWebView;

namespace CCron {

class LocalServer;

// Janela do framework: levanta um servidor HTTP local servindo
// applicationPath (SPA: Svelte, React, Vue, ...) e abre num QtWebView.
// A mesma URL funciona no desktop e no mobile (backend nativo do OS).
//
// A Window tem 2 URLs com papeis distintos:
//   apiUrl  = raiz do LocalServer (controle da API: /api/invoke, /api/modulos)
//   viewUrl = o que a WebView carrega (controle do webview)
// Em prod as duas apontam p/ o mesmo servidor:
//   api  = http://127.0.0.1:<porta>/
//   view = http://127.0.0.1:<porta>/index.html
// Em dev a view aponta p/ o Vite (HMR) e a API continua no LocalServer,
// entao o ambiente fica proximo do prod (mesmo Show(), mesma API):
//   api  = http://127.0.0.1:<portaC++>/
//   view = http://127.0.0.1:5173/
// Uso:
//   auto *win = new CCron::Window(path);   // prod direto
//   win->setViewUrl(QUrl("http://127.0.0.1:5173/")); // dev: antes do Show()
//   win->resize(1280, 800);
//   win->Show();                            // abre a viewUrl
//   win->Show(QUrl("rota"));                // viewUrl.resolved("rota")
// Host/porta fora da view exige outra Window.
class Window : public QWidget {
public:
  explicit Window(const QString &applicationPath,
                  const QString &baseFile = QStringLiteral("index.html"),
                  QWidget *parent = nullptr);

  // using QWidget::setWindowFlags;
  // using QWidget::show;
  void Show(const QUrl &url = QUrl());

  // Troca o que a WebView carrega (dev: URL do Vite). A API nao muda.
  void setViewUrl(const QUrl &url);
  QUrl viewUrl() const { return m_viewUrl; }
  QUrl apiUrl() const;
  QUrl baseUrl() const { return m_viewUrl; }

  QWebView *view() const { return m_webview; }
  QVBoxLayout *layout() const { return m_layout; }
  LocalServer *server() const { return m_server; }
  QString applicationPath() const { return m_applicationPath; }
  quint16 port() const;

private:
  QString m_applicationPath;
  QUrl m_viewUrl;
  LocalServer *m_server = nullptr;
  QWebView *m_webview = nullptr;
  QVBoxLayout *m_layout = nullptr;
};

} // namespace CCron
