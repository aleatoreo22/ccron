#pragma once

#include <QHash>
#include <QObject>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

namespace CCron {

class Bridge;

// Servidor HTTP estatico embutido (somente 127.0.0.1, porta efemera).
// Serve a pasta rootDir com fallback SPA (-> index.html), MIME por extensao
// e cache em RAM. Sem dependencias alem de QtNetwork: funciona igual no
// WebEngine (desktop) e no QtWebView (mobile).
// Rotas de API (bridge JS <-> C++):
//   POST /api/invoke   {module, method, args} -> {ok, result|error}
//   GET  /api/modulos  descoberta: modulos + metodos + assinaturas
// As conexoes usam "Connection: close" (uma resposta por conexao).
class LocalServer : public QObject {
public:
  explicit LocalServer(const QString &rootDir, QObject *parent = nullptr);

  bool start();
  QUrl baseUrl() const;
  quint16 port() const { return m_port; }
  QString rootDir() const { return m_rootDir; }
  void setPreferredPort(quint16 port) { m_preferredPort = port; }

  // Dono do Bridge (criado no construtor). Registre os modulos via
  // server->bridge()->registerObject(...).
  Bridge *bridge() const { return m_bridge; }

private:
  void onNewConnection();
  void onReadyRead(QTcpSocket *sock);
  void answer(QTcpSocket *sock, const QByteArray &headers,
              const QByteArray &body);

  QString m_rootDir;
  QTcpServer *m_server = nullptr;
  Bridge *m_bridge = nullptr;
  quint16 m_port = 0;
  quint16 m_preferredPort = 0;
  QHash<QTcpSocket *, QByteArray> m_pending;
};

} // namespace CCron
