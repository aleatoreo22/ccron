#include "bridge.h"

#include <QJsonValue>
#include <QMetaMethod>
#include <QMetaType>
#include <QVariant>
#include <utility>

namespace CCron {
namespace {

// Maximo suportado por QMetaMethod::invoke.
constexpr int kMaxArgs = 10;

// Converte o QJsonArray do envelope em QVariantList.
// QJsonValue::toVariant() ja mapeia: object->QVariantMap,
// array->QVariantList, string->QString, bool->bool,
// number->double (int vira double; a conversao p/ o tipo declarado
// do parametro acontece na etapa seguinte).
QVariantList jsonToVariantList(const QJsonArray &args) {
  QVariantList out;
  out.reserve(args.size());
  for (const QJsonValue &v : args)
    out << v.toVariant();
  return out;
}

// "Spread" do C++: QMetaMethod::invoke so existe na forma
// (val0..val9 por valor), entao um pack com index_sequence expande o
// array na chamada — equivalente ao invoke(obj, ret, ...gargs) do JS.
// Os defaults (name==nullptr) marcam "sem argumento".
template <std::size_t... I>
bool tryInvoke(const QMetaMethod &method, QObject *obj,
               QGenericReturnArgument retArg, QGenericArgument *gargs,
               std::index_sequence<I...>) {
  return method.invoke(obj, Qt::DirectConnection, retArg, gargs[I]...);
}

// Tenta invocar UM candidato do meta-object com os args convertidos
// para os tipos declarados dos parametros. Retorna true se invocou
// (mesmo que o metodo em si represente um erro de negocio: isso vai
// no valor de retorno, nao aqui).
bool tryInvoke(QObject *obj, const QMetaMethod &method,
               const QVariantList &args, QVariant &ret, QString &error) {
  if (method.parameterCount() != args.size()) {
    error = QStringLiteral("esperava %1 argumento(s), recebeu %2")
                .arg(method.parameterCount())
                .arg(args.size());
    return false;
  }

  // Converte cada arg para o tipo declarado do parametro.
  // A lista `conv` precisa ficar viva durante o invoke (os
  // QGenericArgument apontam para dentro dela).
  QVariantList conv;
  QList<QMetaType> decl;
  conv.reserve(args.size());
  decl.reserve(args.size());
  for (int i = 0; i < args.size(); ++i) {
    const QMetaType declared = method.parameterMetaType(i);
    // QVariant aceita qualquer valor JSON: sem conversao.
    if (declared.id() == QMetaType::QVariant) {
      conv << args[i];
      decl << declared;
      continue;
    }
    QVariant v = args[i];
    if (v.metaType() != declared && !v.convert(declared)) {
      error = QStringLiteral("argumento %1 nao converte para %2")
                  .arg(i)
                  .arg(QString::fromUtf8(declared.name()));
      return false;
    }
    conv << std::move(v);
    decl << declared;
  }

  QGenericArgument gargs[kMaxArgs];
  for (int i = 0; i < conv.size(); ++i) {
    if (decl[i].id() == QMetaType::QVariant)
      // Parametro QVariant: o metacall espera o ENDERECO do QVariant
      // (constData() apontaria para o valor interno — tipo errado).
      gargs[i] = QGenericArgument("QVariant", &conv[i]);
    else
      gargs[i] = QGenericArgument(decl[i].name(), conv[i].constData());
  }

  QGenericReturnArgument retArg;
  const QMetaType retType = method.returnMetaType();
  if (retType.isValid() && retType.id() != QMetaType::Void) {
    if (retType.id() == QMetaType::QVariant) {
      // Retorno QVariant: o metacall escreve DIRETO no objeto (mesmo
      // que Q_RETURN_ARG(QVariant, v)); um QVariant-aninhado via
      // ret.data() nao seria desenvelopado depois.
      ret = QVariant();
      retArg = QGenericReturnArgument("QVariant", &ret);
    } else {
      ret = QVariant(retType); // valor default do tipo; invoke preenche
      retArg = QGenericReturnArgument(retType.name(), ret.data());
    }
  } else {
    ret = QVariant(); // metodo void -> result null no envelope
  }

  if (!tryInvoke(method, obj, retArg, gargs,
                 std::make_index_sequence<kMaxArgs>{})) {
    error = QStringLiteral("falha interna ao invocar o metodo");
    return false;
  }
  return true;
}

QJsonObject okEnvelope(const QVariant &result) {
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("result"), QJsonValue::fromVariant(result)}};
}

QJsonObject errEnvelope(const QString &error) {
  return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

} // namespace

Bridge::Bridge(QObject *parent) : QObject(parent) {}

void Bridge::registerObject(const QString &module, QObject *obj) {
  if (!obj)
    return;
  if (obj->parent() == nullptr)
    obj->setParent(this);
  m_objects.insert(module, obj);
}

QObject *Bridge::object(const QString &module) const {
  return m_objects.value(module, nullptr);
}

QStringList Bridge::modules() const { return m_objects.keys(); }

QJsonObject Bridge::invoke(const QString &module, const QString &method,
                           const QJsonArray &args) {

  QObject *obj = m_objects.value(module, nullptr);
  if (!obj)
    return errEnvelope(QStringLiteral("modulo desconhecido: %1").arg(module));

  if (method.isEmpty())
    return errEnvelope(QStringLiteral("method vazio"));

  if (args.size() > kMaxArgs)
    return errEnvelope(QStringLiteral("maximo de %1 argumentos").arg(kMaxArgs));

  const QVariantList argv = jsonToVariantList(args);
  const QMetaObject *mo = obj->metaObject();
  const QByteArray wanted = method.toUtf8();

  // So metodos declarados na propria classe do modulo (methodOffset()
  // pula os herdados: deleteLater e cia. nunca sao alcancaveis).
  // Aceita Q_INVOKABLE (Method) e slots publicos (Slot).
  QString lastError;
  bool methodFound = false;
  for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
    const QMetaMethod m = mo->method(i);

    if (m.access() != QMetaMethod::Public)
      continue;

    if (m.methodType() != QMetaMethod::Method &&
        m.methodType() != QMetaMethod::Slot)
      continue;

    if (m.name() != wanted)
      continue;

    methodFound = true;
    QVariant ret;
    QString error;
    if (tryInvoke(obj, m, argv, ret, error))
      return okEnvelope(ret);
    lastError = error; // tenta a proxima sobrecarga, se houver
  }

  if (!methodFound)
    return errEnvelope(
        QStringLiteral("metodo desconhecido: %1.%2").arg(module, method));
  return errEnvelope(
      QStringLiteral("%1.%2: %3").arg(module, method, lastError));
}

QJsonArray Bridge::describe() const {
  QJsonArray out;
  for (auto it = m_objects.constBegin(); it != m_objects.constEnd(); ++it) {
    QJsonArray methods;
    const QMetaObject *mo = it.value()->metaObject();
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
      const QMetaMethod m = mo->method(i);
      if (m.access() != QMetaMethod::Public)
        continue;
      if (m.methodType() != QMetaMethod::Method &&
          m.methodType() != QMetaMethod::Slot)
        continue;
      QJsonArray params;
      for (int p = 0; p < m.parameterCount(); ++p)
        params << QString::fromUtf8(m.parameterMetaType(p).name());
      methods << QJsonObject{
          {QStringLiteral("name"), QString::fromUtf8(m.name())},
          {QStringLiteral("params"), params},
          {QStringLiteral("returns"),
           QString::fromUtf8(m.returnMetaType().name())}};
    }
    out << QJsonObject{{QStringLiteral("module"), it.key()},
                       {QStringLiteral("methods"), methods}};
  }
  return out;
}

} // namespace CCron
