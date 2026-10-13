#pragma once

#include <QObject>
#include <QVariant>

namespace CCron {

// Modulo de exemplo: mostra a estrutura que qualquer modulo do app segue.
//
//   1. Herda QObject + macro Q_OBJECT.
//   2. Cada funcao exposta ao site e `Q_INVOKABLE` retornando QVariant
//      (ou void). Parametros: tipos concretos ou QVariant*.
//   3. O modulo NAO conhece HTTP/JSON: recebe valores, devolve valores.
//      O Bridge + LocalServer cuidam do transporte.
//   4. Erro de negocio (arquivo nao existe, etc.) = retornar um mapa
//      {ok:false, error:...} — erro de protocolo (modulo/metodo/args
//      invalidos) o Bridge ja trata sozinho.
//
// Para criar o seu: copie este arquivo, renomeie a classe, escreva os
// Q_INVOKABLEs e registre no main com
//   bridge->registerObject("MeuModulo", new MeuModulo(parent));
class FileModule : public QObject {
  Q_OBJECT
public:
  explicit FileModule(QObject *parent = nullptr);

  // {"module":"Arquivos","method":"ping","args":[]} -> "pong"
  Q_INVOKABLE QVariant ping();

  // Ecoa qualquer valor JSON (mostra que QVariant aceita tudo).
  Q_INVOKABLE QVariant eco(const QVariant &valor);

  // Exemplo de parametros tipados (JSON number -> double).
  Q_INVOKABLE QVariant somar(double a, double b);

  // Exemplo real: le um arquivo e devolve {ok, path, size, content}.
  Q_INVOKABLE QVariant lerTexto(const QString &path);
};

} // namespace CCron
