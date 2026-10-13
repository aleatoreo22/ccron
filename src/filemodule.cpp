#include "filemodule.h"

#include <QFile>
#include <QFileInfo>

namespace CCron {

FileModule::FileModule(QObject *parent) : QObject(parent) {}

QVariant FileModule::ping() { return QStringLiteral("pong"); }

QVariant FileModule::eco(const QVariant &valor) { return valor; }

QVariant FileModule::somar(double a, double b) { return a + b; }

QVariant FileModule::lerTexto(const QString &path) {
  QFileInfo info(path);
  if (!info.exists() || !info.isFile())
    return QVariantMap{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"),
                        QStringLiteral("arquivo nao encontrado: %1").arg(path)}};
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QVariantMap{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"),
                        QStringLiteral("nao foi possivel abrir: %1").arg(path)}};
  const QString content = QString::fromUtf8(f.readAll());
  return QVariantMap{{QStringLiteral("ok"), true},
                     {QStringLiteral("path"), info.absoluteFilePath()},
                     {QStringLiteral("size"), content.size()},
                     {QStringLiteral("content"), content}};
}

} // namespace CCron
