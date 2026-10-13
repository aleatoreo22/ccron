#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace CCron {

// Bridge: dispatcher dinamico entre o site (JS) e o C++.
//
// O site envia um envelope JSON e o Bridge resolve tudo em runtime,
// sem switch/if por funcao — equivalente ao reflection do C#:
//
//   { "module": "Arquivos", "method": "lerTexto", "args": ["/tmp/nota.txt"] }
//
// Contrato dos metodos expostos:
//   - a classe do modulo herda QObject e registra a instancia com
//     registerObject() (isso e a allowlist: so o registrado e visivel);
//   - cada funcao exposta e `Q_INVOKABLE` (slots publicos tambem valem)
//     declarada na propria classe do modulo (metodos herdados de QObject
//     como deleteLater nunca sao alcancados);
//   - ate 10 parametros; tipos podem ser concretos (QString, double, ...)
//     ou genericos (QVariant, QVariantMap, QVariantList);
//   - retorno void ou qualquer tipo conversivel para JSON.
//
// Resposta e sempre um envelope: {ok:true, result:...} ou {ok:false, error:...}
class Bridge : public QObject {
  Q_OBJECT
public:
  explicit Bridge(QObject *parent = nullptr);

  // Expõe um modulo sob um nome ("namespace"). Uma instancia pode ser
  // registrada sob varios nomes; o Bridge assume ownership via parent.
  void registerObject(const QString &module, QObject *obj);
  QObject *object(const QString &module) const;
  QStringList modules() const;

  // Chamadas vindas do site. Nunca lanca excecao: todo erro vira envelope.
  QJsonObject invoke(const QString &module, const QString &method,
                     const QJsonArray &args);

  // Descoberta: lista modulos + metodos + tipos de parametro (p/ gerar
  // client tipado ou documentacao automatica no site).
  QJsonArray describe() const;

private:
  QHash<QString, QObject *> m_objects;
};

} // namespace CCron
