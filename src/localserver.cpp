#include "localserver.h"
#include "bridge.h"
#include "qlogging.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>

namespace CCron {
namespace {

// realPath -> {mtime, bytes}: evita reler o bundle do disco a cada request
QHash<QString, QPair<QDateTime, QByteArray>> &cache() {
  static QHash<QString, QPair<QDateTime, QByteArray>> s;
  return s;
}

QByteArray mimeForPath(const QString &path) {
  if (path.endsWith(QStringLiteral(".html")))
    return "text/html; charset=utf-8";
  if (path.endsWith(QStringLiteral(".js")) ||
      path.endsWith(QStringLiteral(".mjs")))
    return "application/javascript; charset=utf-8";
  if (path.endsWith(QStringLiteral(".css")))
    return "text/css; charset=utf-8";
  if (path.endsWith(QStringLiteral(".json")) ||
      path.endsWith(QStringLiteral(".map")))
    return "application/json";
  if (path.endsWith(QStringLiteral(".png")))
    return "image/png";
  if (path.endsWith(QStringLiteral(".jpg")) ||
      path.endsWith(QStringLiteral(".jpeg")))
    return "image/jpeg";
  if (path.endsWith(QStringLiteral(".gif")))
    return "image/gif";
  if (path.endsWith(QStringLiteral(".webp")))
    return "image/webp";
  if (path.endsWith(QStringLiteral(".avif")))
    return "image/avif";
  if (path.endsWith(QStringLiteral(".svg")))
    return "image/svg+xml";
  if (path.endsWith(QStringLiteral(".ico")))
    return "image/x-icon";
  if (path.endsWith(QStringLiteral(".woff")))
    return "font/woff";
  if (path.endsWith(QStringLiteral(".woff2")))
    return "font/woff2";
  if (path.endsWith(QStringLiteral(".ttf")))
    return "font/ttf";
  if (path.endsWith(QStringLiteral(".otf")))
    return "font/otf";
  if (path.endsWith(QStringLiteral(".wasm")))
    return "application/wasm";
  if (path.endsWith(QStringLiteral(".mp4")))
    return "video/mp4";
  if (path.endsWith(QStringLiteral(".webm")))
    return "video/webm";
  if (path.endsWith(QStringLiteral(".txt")))
    return "text/plain; charset=utf-8";
  if (path.endsWith(QStringLiteral(".xml")))
    return "application/xml";
  return "application/octet-stream";
}

// true se localFile esta dentro de root (resolve symlinks e "..").
bool isInsideRoot(const QString &root, const QString &localFile) {
  QString canonRoot = QDir(root).canonicalPath();
  if (canonRoot.isEmpty())
    canonRoot = QDir(root).absolutePath();
  const QString canonFile = QFileInfo(localFile).canonicalFilePath();
  if (canonFile.isEmpty() || canonRoot.isEmpty())
    return false;
  return canonFile == canonRoot || canonFile.startsWith(canonRoot + '/');
}

// "<root>/<rel>" direto, ou busca recursiva pelo nome em qualquer subpasta
// (Vite/Svelte costumam aninhar assets em _app/, assets/, ...).
QString searchFileRealPath(const QString &root, const QString &requestPath) {
  QString rel = requestPath;
  if (rel.startsWith(QStringLiteral("/")))
    rel.remove(0, 1);
  if (rel.isEmpty())
    rel = QStringLiteral("index.html");

  const QString direct = QDir(root).filePath(rel);
  if (QFileInfo::exists(direct) && isInsideRoot(root, direct))
    return direct;

  const QString name = QFileInfo(rel).fileName();
  QDirIterator it(root, {name}, QDir::Files | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString found = it.next();
    if (isInsideRoot(root, found))
      return found;
  }
  return {};
}

QByteArray readCached(const QString &realPath) {
  const QDateTime mtime = QFileInfo(realPath).lastModified();
  if (const auto it = cache().constFind(realPath);
      it != cache().constEnd() && it->first == mtime)
    return it->second;

  QFile f(realPath);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "ccron: achou mas nao abriu:" << realPath;
    return {};
  }
  const QByteArray data = f.readAll();
  cache().insert(realPath, {mtime, data});
  return data;
}

struct Served {
  int status = 404;
  QByteArray mime = "text/plain; charset=utf-8";
  QByteArray body = "Not Found";
};

// Resolve o que servir para um request do bundle:
// arquivo real -> 200+mime; rota SPA (sem extensao) -> index.html;
// asset inexistente -> 404 de verdade, nao mascara chunk quebrado.
Served serve(const QString &root, const QString &requestPath) {

  if (requestPath.contains(QStringLiteral("..")))
    return {};
  const QString realPath = searchFileRealPath(root, requestPath);

  if (!realPath.isEmpty())
    return {200, mimeForPath(realPath), readCached(realPath)};

  // SPA fallback: "/rota", "/" ou dir -> index.html (client-side routing).
  const QString last = QFileInfo(requestPath).fileName();
  if (requestPath.endsWith('/') || requestPath == QStringLiteral("/") ||
      !last.contains('.')) {
    const QString index =
        searchFileRealPath(root, QStringLiteral("index.html"));
    if (!index.isEmpty())
      return {200, "text/html; charset=utf-8", readCached(index)};
  }
  return {};
}

QByteArray reasonFor(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 500:
    return "Internal Server Error";
  default:
    return "Error";
  }
}

// POST /api/invoke: envelope {module, method, args} -> {ok, result|error}.
// Erro de protocolo (JSON invalido) = HTTP 400; erro de dispatch
// (modulo/metodo/args) = HTTP 200 com {ok:false} — o fetch do site
// trata tudo no corpo, sem precisar ramificar por status.
Served apiInvoke(Bridge *bridge, const QByteArray &body) {
  QJsonParseError perr{};
  const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);

  if (perr.error != QJsonParseError::NoError || !doc.isObject())
    return {400, "application/json",
            QJsonDocument(QJsonObject{{QStringLiteral("ok"), false},
                                      {QStringLiteral("error"),
                                       QStringLiteral("JSON invalido: %1")
                                           .arg(perr.errorString())}})
                .toJson(QJsonDocument::Compact)};

  const QJsonObject req = doc.object();
  const QJsonObject res =
      bridge->invoke(req.value(QStringLiteral("module")).toString(),
                     req.value(QStringLiteral("method")).toString(),
                     req.value(QStringLiteral("args")).toArray());
  return {200, "application/json",
          QJsonDocument(res).toJson(QJsonDocument::Compact)};
}

} // namespace

LocalServer::LocalServer(const QString &rootDir, QObject *parent)
    : QObject(parent), m_rootDir(QDir(rootDir).absolutePath()) {
  m_bridge = new Bridge(this);
}

bool LocalServer::start() {
  m_server = new QTcpServer(this);
  // Porta fixa via setPreferredPort() ou env CCRON_PORT (dev: o proxy do
  // Vite precisa saber para onde mandar /api). 0 = efemera (prod).
  quint16 want = m_preferredPort;
  if (want == 0) {
    bool ok = false;
    const int env = qEnvironmentVariableIntValue("CCRON_PORT", &ok);
    if (ok && env > 0 && env < 65536)
      want = quint16(env);
  }
  const bool listening =
      want == 0 ? m_server->listen(QHostAddress::LocalHost, 0)
                : (m_server->listen(QHostAddress::LocalHost, want) ||
                   m_server->listen(QHostAddress::LocalHost, 0));
  if (!listening) {
    qWarning() << "ccron: nao abriu porta local:" << m_server->errorString();
    return false;
  }
  m_port = m_server->serverPort();
  connect(m_server, &QTcpServer::newConnection, this,
          [this]() { onNewConnection(); });
  return true;
}

QUrl LocalServer::baseUrl() const {
  QUrl u;
  u.setScheme(QStringLiteral("http"));
  u.setHost(QStringLiteral("127.0.0.1"));
  u.setPort(m_port);
  u.setPath(QStringLiteral("/"));
  return u;
}

void LocalServer::onNewConnection() {
  while (QTcpSocket *sock = m_server->nextPendingConnection()) {
    m_pending.insert(sock, {});
    connect(sock, &QTcpSocket::readyRead, this,
            [this, sock]() { onReadyRead(sock); });
    connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
      m_pending.remove(sock);
      sock->deleteLater();
    });
  }
}

void LocalServer::onReadyRead(QTcpSocket *sock) {
  QByteArray &buf = m_pending[sock];
  buf += sock->readAll();
  const int hend = buf.indexOf("\r\n\r\n");
  if (hend < 0)
    return; // cabecalho ainda incompleto: espera mais dados

  // POST (ex.: /api/invoke) traz corpo JSON: espera ele chegar por
  // completo antes de responder, usando o Content-Length.
  int contentLength = 0;
  const QByteArray headers = buf.left(hend);
  const int clPos = headers.toLower().indexOf("content-length:");
  if (clPos >= 0) {
    const int eol = headers.indexOf("\r\n", clPos);
    contentLength = headers.mid(clPos + 15, eol - clPos - 15).trimmed().toInt();
    if (contentLength < 0)
      contentLength = 0;
  }
  if (buf.size() < hend + 4 + contentLength)
    return; // corpo ainda incompleto: espera mais dados

  answer(sock, headers, buf.mid(hend + 4, contentLength));
  sock->disconnectFromHost(); // Connection: close, uma resposta por conexao
}

void LocalServer::answer(QTcpSocket *sock, const QByteArray &headers,
                         const QByteArray &body) {
  // CORS liberado de proposito: em dev o site roda no Vite (:5173) e a
  // API continua aqui. Mesmo em prod nao ha risco: so ouvimos 127.0.0.1.
  auto writeHead = [sock](int status, const QByteArray &mime, int size) {
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " +
                      reasonFor(status) + "\r\n";
    head += "Content-Type: " + mime + "\r\n";
    head += "Content-Length: " + QByteArray::number(size) + "\r\n";
    head += "Access-Control-Allow-Origin: *\r\n";
    head += "Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS\r\n";
    head += "Access-Control-Allow-Headers: Content-Type\r\n";
    head += "Connection: close\r\n\r\n";
    sock->write(head);
  };
  auto writeServed = [&](const Served &s, bool get) {
    writeHead(s.status, s.mime, s.body.size());
    if (get)
      sock->write(s.body);
  };

  // Linha de request: "METODO alvo VERSAO"
  const int eol = headers.indexOf("\r\n");
  const QList<QByteArray> parts = headers.left(eol).split(' ');
  if (parts.size() < 2) {
    writeHead(400, "text/plain; charset=utf-8", 0);
    return;
  }
  const QByteArray method = parts[0].toUpper();
  if (method == "OPTIONS") { // preflight do browser em dev (Vite -> API)
    writeHead(204, "text/plain; charset=utf-8", 0);
    return;
  }
  if (method != "GET" && method != "HEAD" && method != "POST") {
    writeHead(405, "text/plain; charset=utf-8", 0);
    return;
  }
  const bool get = (method != "HEAD"); // GET e POST recebem corpo

  // Alvo: origin-form ("/x?y") ou absolute-form ("http://h/x").
  QString target = QString::fromUtf8(parts[1]);
  QString path;
  if (target.startsWith(QStringLiteral("http://")) ||
      target.startsWith(QStringLiteral("https://")))
    path = QUrl(target).path();
  else
    path = target.split('?').first();
  path = QUrl::fromPercentEncoding(path.toUtf8());
  if (path.isEmpty())
    path = QStringLiteral("/");

  // API JS <-> C++ (Bridge). POST com envelope JSON; GET de descoberta.
  if (path == QStringLiteral("/api/invoke")) {
    if (method != "POST") {
      writeHead(405, "text/plain; charset=utf-8", 0);
      return;
    }
    writeServed(apiInvoke(m_bridge, body), true);
    return;
  }
  if (path == QStringLiteral("/api/modulos")) {
    const QByteArray json =
        QJsonDocument(m_bridge->describe()).toJson(QJsonDocument::Compact);
    writeServed({200, "application/json", json}, get);
    return;
  }
  if (method == "POST") { // POST fora da API nao existe
    writeHead(404, "text/plain; charset=utf-8", 0);
    return;
  }

  writeServed(serve(m_rootDir, path), get);
}

} // namespace CCron
