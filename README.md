# ccron

Uma alternativa ao [Electron](https://www.electronjs.org/): em vez de empacotar um
navegador inteiro em cima do Node, o **ccron** usa **Qt (C++)** para abrir uma
janela nativa com uma `QtWebView` embutida e um servidor HTTP local que serve o
frontend web.

Foi uma **experiência / prova de conceito** para ver o que dá pra fazer com Qt e
chegar em algo **mais simples e mais performático** que o fluxo Electron.

> Não é um projeto de produção — é um estudo.

## Como funciona

- A janela (`CCron::Window`) sobe um servidor HTTP local (`LocalServer`, só em
  `127.0.0.1`, sem dependências além de `QtNetwork`) que serve a SPA (Svelte/Vite).
- A mesma `WebView` roda em desktop e mobile (backend nativo do sistema).
- Um **bridge** JS ↔ C++ expõe módulos nativos:
  - `POST /api/invoke` — `{module, method, args}` → `{ok, result|error}`
  - `GET /api/modulos` — descoberta de módulos, métodos e assinaturas
- Módulos nativos são `QObject` com métodos `Q_INVOKABLE`. O exemplo incluído é o
  módulo `Arquivos` (`ping`, `eco`, `somar`, `lerTexto`).

Ou seja: o frontend fica em HTML/CSS/JS como no Electron, mas o "backend" é C++
nativo em vez de Node.

## Requisitos

- Qt 6 (`Qt6Widgets`, `Qt6WebView`, `Qt6Network`) + `pkg-config`
- `g++` com C++17
- `make`
- [Bun](https://bun.sh/) (usado pelo `make dev`) — ou npm/pnpm só para o frontend

## Como rodar

### 1. Buildar o frontend

```sh
cd frontend
bun install
bun run build
cd ..
```

Isso gera o `frontend/dist/`, que é o que o servidor local serve.

### 2. Compilar e rodar o app

```sh
make        # compila o binário myapp
make prod   # roda servindo o frontend/dist/
```

Também existe `make run`, que é igual ao `prod` sem os logs extras.

### Modo desenvolvimento (hot reload)

```sh
make dev        # sobe o Vite (HMR) + o app apontando pra ele
make dev-logs   # acompanha o log do Vite em outra aba
```

No modo dev o frontend roda no Vite (com HMR) e a API continua no servidor C++,
o que deixa o ambiente bem próximo do de produção.

### Outros comandos

```sh
make clean      # remove build/ e o binário
make compiledb  # gera compile_commands.json (para clangd/IDE)
```

## Estrutura

```
src/            C++ / Qt
  main.cpp      entrada: monta a Window e registra os módulos
  ccron.*       Window: servidor local + WebView
  localserver.* servidor HTTP estático + rotas /api/*
  bridge.*      dispatcher JS <-> C++ via meta-object (Q_INVOKABLE)
  filemodule.*  módulo de exemplo ("Arquivos")
frontend/       SPA (Svelte + Vite)
```
