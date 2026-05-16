# Marvel's Spider-Man 2 RX 580 Fix

Fix experimental para abrir e jogar Marvel's Spider-Man 2 em placas AMD Radeon RX 580 / RX 580 2048SP.

Criado e testado por Joao Lucas.
Canal: https://www.youtube.com/@MEGADROIDGAMESS

## O que esse fix faz?

Este fix coloca uma `d3d12.dll` proxy na pasta do jogo.

Ela faz quatro coisas principais:

1. Faz o jogo criar o DirectX 12 em Feature Level 12_0.
2. Corrige algumas falhas de pipeline que fazem o jogo travar ou fechar.
3. Remove o aviso inicial de GPU nao compativel.
4. Usa um modo mais compativel com a RX 580 / RX 580 2048SP.

## Aviso importante

Este fix e experimental.

Ele nao transforma a RX 580 em uma placa oficialmente suportada. Algumas partes do jogo podem ter queda de desempenho, bugs visuais, travamentos ou fases/cenas que precisam de ajustes futuros.

Use por sua conta e risco.

## Como instalar

1. Baixe este repositorio.
2. Abra a pasta `release`.
3. Copie o arquivo `d3d12.dll`.
4. Cole dentro da pasta onde fica o `Spider-Man2.exe`.

Exemplo de pasta do jogo:

```text
E:\SteamLibrary\steamapps\common\Marvel's Spider-Man 2
```

A pasta correta precisa ter estes arquivos:

```text
Spider-Man2.exe
steam_api64.dll
cache.pso
```

Depois de colar a `d3d12.dll`, abra o jogo normalmente pela Steam.

## Passo extra recomendado

Se o jogo abrir mas ficar travando, com tela preta, ou fechar durante o loading, renomeie o arquivo `cache.pso` da pasta do jogo.

Renomeie de:

```text
cache.pso
```

para:

```text
cache.pso.disabled
```

Isso forca o jogo a reconstruir parte do cache de shaders. No teste da RX 580 2048SP, isso ajudou bastante.

## Como remover o fix

Para remover:

1. Feche o jogo.
2. Apague o arquivo `d3d12.dll` que voce colocou na pasta do jogo.
3. Se voce renomeou `cache.pso`, volte o nome dele para `cache.pso`.

Pronto, o jogo volta ao estado original.

## Erros conhecidos

### O jogo ainda fala que a GPU nao e compativel

Use a DLL mais recente da pasta `release`. A versao atual remove esse aviso automaticamente.

### O jogo fecha no loading

Tente renomear `cache.pso` para `cache.pso.disabled`.

### O jogo abre, mas fica pesado

A RX 580 nao e oficialmente suportada. Use tudo no baixo, resolucao menor e FSR se estiver disponivel.

## Arquivos do projeto

```text
release\d3d12.dll
```

DLL pronta para usar.

```text
src\
```

Codigo-fonte do proxy D3D12.

```text
scripts\build_spider_proxy.ps1
```

Script para compilar usando Visual Studio Build Tools com MSVC.

## Creditos

Fix por Joao Lucas.

YouTube:
https://www.youtube.com/@MEGADROIDGAMESS
