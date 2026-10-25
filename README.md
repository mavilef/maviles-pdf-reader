# Maviles PDF Reader — C++ do zero

Um leitor para Linux com **motor de PDF próprio**, feito em C++17. Sem GTK,
Poppler, MuPDF, Qt, SDL, zlib, FreeType, libjpeg, libX11 ou libxcb.

Os PDFs de referência estão em `~/Downloads`: *A Tour of C++* (**799 páginas**)
e *Effective Modern C++* (**334 páginas**). Ambos foram percorridos e renderizados
a 100% sem erros. No Effective Modern C++, foram também conferidas visualmente
capa, texto, tabela e diagrama. Isso valida esses arquivos; não significa
compatibilidade com todos os PDFs existentes. Os livros e suas fontes não são
distribuídos no projeto.

## Compilar e abrir

Você precisa apenas de um compilador C++17 e Make. O Makefile usa por padrão
o **AMD AOCC já instalado em `/opt/aocc/bin/clang++`**:

```sh
make
./build/maviles-pdf-reader
```

Sem argumentos, o leitor abre o **único arquivo `.pdf` em `~/Downloads`**.
Se houver mais de um, informe o caminho. Para o Effective Modern C++ presente
neste ambiente, use `./build/maviles-pdf-reader ~/Downloads/Effective*.pdf`.
O executável de `build/` incorpora as mudanças; uma instalação anterior de
`mpr` só é atualizada quando você executa `make install` novamente.

Exemplos:

```sh
./build/maviles-pdf-reader "/caminho/meu arquivo.pdf"
./build/maviles-pdf-reader "/caminho/meu arquivo.pdf" --page 114
```

A janela continua aberta se você fechar o terminal que iniciou o leitor.
Para liberar também o prompt do terminal, execute em segundo plano:

```sh
./build/maviles-pdf-reader "/caminho/meu arquivo.pdf" &
```

Feche o leitor pela janela ou pela tecla Q. Essa proteção contra o fechamento
do terminal vale apenas para a leitura normal; os modos de linha de comando
mantêm o comportamento habitual. Encerrar a sessão gráfica ainda fecha a janela.

A janela conversa diretamente com o servidor gráfico por socket UNIX.
Precisa de uma sessão **X11 ou XWayland local**, com `DISPLAY` e a autenticação
normal da sessão (`XAUTHORITY` ou `~/.Xauthority`). Não é preciso instalar
headers ou bibliotecas de X11 para compilar. Esta versão aceita a tela `.0`,
visual TrueColor RGB de 24 bits e pixels de 32 bits. Wayland sem XWayland ainda
não é suportado. O binário usa o runtime padrão de C/C++ do sistema.

Para recompilar do zero: `make clean` e `make`. O Makefile não baixa nada.
O arquivo `.clang-format` é só uma preferência de formatação; a ferramenta
não é necessária para compilar.

Se o AOCC estiver em outro diretório, use `make AOCC=/caminho/aocc`. Também
é possível informar explicitamente `CXX=/caminho/compilador`. Ao trocar de
compilador, execute `make clean` primeiro para não reutilizar objetos antigos.
O build não instala ferramentas ou dependências.

O AOCC usa sua própria `libc++`, já presente em `/opt/aocc/lib`, para evitar
a incompatibilidade de seus headers com os do GCC 16 deste ambiente. O
Makefile grava esse caminho no executável (`RUNPATH`), dispensando alterar
`LD_LIBRARY_PATH`. Ao mover a instalação do AOCC, recompile com o novo caminho.

## Instalar no usuário

```sh
make install
```

Compila se necessário e copia o executável para `~/.local/bin/maviles-pdf-reader`,
com permissão de execução. Também cria `~/.local/bin/mpr` como link simbólico
relativo para o mesmo executável. Cria o diretório se ele não existir e atualiza
o binário e o atalho se já estiverem instalados. Não precisa de `sudo`.

Como `~/.local/bin` está no seu `PATH`, depois basta executar:

```sh
maviles-pdf-reader
# Ou use o nome curto:
mpr
```

Para remover o executável instalado e o atalho `mpr`, preservando o diretório
e os demais arquivos:

```sh
make uninstall
```

`make clean` remove apenas o build local, não o executável instalado.
O binário instalado continua usando o runtime C++ do AOCC em `/opt/aocc/lib`.
Para outro destino, use `PREFIX=/caminho` (instala em `/caminho/bin`) ou
`BINDIR=/caminho/bin`, tanto no install quanto no uninstall. `DESTDIR` permite
testar a instalação em uma pasta temporária, sem alterar o destino real.

## Controles

| Ação | Controle |
| --- | --- |
| Página anterior / próxima | `p` / `n`, Page Up / Page Down ou setas esquerda / direita |
| Próxima página | Espaço |
| Primeira / última página | Home / End |
| Ir para uma página | Digite o número e pressione Enter |
| Corrigir / cancelar número digitado | Backspace / Esc |
| Aumentar / diminuir | Teclas `+` / `-` ou Ctrl + roda do mouse |
| Ajustar à largura | F |
| Rolar verticalmente | `j` / `k` (baixo / cima), roda do mouse ou setas cima / baixo |
| Rolar horizontalmente no zoom | `h` / `l` (esquerda / direita) ou Shift + roda do mouse |
| Sair | Q ou fechar a janela |

A numeração conta todas as páginas do arquivo, incluindo capas. A janela
mostra uma página por vez. Para trocar de arquivo, encerre e execute com o
novo caminho. A interface exibe apenas o documento, sem barras, botões ou
indicadores de página e zoom. A digitação do número da página também não exibe
indicador. O texto do documento usa os contornos das fontes embutidas no próprio PDF.

## Usar sem janela

```sh
# Estrutura básica e quantidade de páginas
./build/maviles-pdf-reader --info

# Renderizar a página 3 em uma imagem PPM (formato simples, RGB sem compressão)
./build/maviles-pdf-reader --render 3 /tmp/pagina.ppm

# Renderizar com escala diferente
./build/maviles-pdf-reader --render 3 /tmp/pagina-grande.ppm --scale 1.5

# Percorrer e renderizar todas as páginas, relatando erros
./build/maviles-pdf-reader --check

# Testar a conexão gráfica, desenhar uma página e fechar
./build/maviles-pdf-reader --smoke
```

Esses comandos também aceitam um caminho de PDF explícito antes das opções.
`--info`, `--render`, `--check` e `--help` não precisam de servidor gráfico.

## Como o código funciona

O caminho de uma página até a tela é:

```text
PDF → xref/ObjStm → streams DEFLATE → operadores da página
                                       ├─ fontes TrueType/CFF → contornos
                                       ├─ imagens JPEG/Flate/fax → pixels
                                       └─ caminhos vetoriais
                                                   ↓
                                          rasterizador próprio
                                                   ↓
                                             bitmap RGB
                                           ↙           ↘
                                      janela X11      arquivo PPM
```

| Arquivo em `src/` | Responsabilidade |
| --- | --- |
| `main.cpp` | Argumentos, descoberta em Downloads e modos sem janela |
| `pdf.cpp` / `pdf.h` | Lexer, objetos, xref clássico/stream, ObjStm, predictors PNG e árvore de páginas |
| `inflate.cpp` / `inflate.h` | zlib/DEFLATE: blocos armazenados, Huffman fixo/dinâmico e Adler-32 |
| `truetype.cpp` / `truetype.h` | Tabelas de fontes, glifos simples/compostos e curvas quadráticas |
| `cff.cpp` / `cff.h` | CFF1, codificações e contornos Type2 com subrotinas e flex |
| `jpeg.cpp` / `jpeg.h` | JPEG baseline, Huffman, IDCT, restart e conversão cinza/RGB/CMYK/YCCK |
| `fax.cpp` / `fax.h` | Decodificação CCITT Group 4 (T.6) |
| `pdf_image.cpp` / `pdf_image.h` | Imagens, paletas Indexed, máscaras e canais de opacidade |
| `render.cpp` / `render.h` | Operadores PDF, estado gráfico, matrizes, fontes e recursos |
| `raster.cpp` / `raster.h` | Preenchimento, traços/pontilhados, recorte por caminhos, composição e antialiasing |
| `x11.cpp` / `x11.h` | Protocolo X11: conexão, autenticação, janela, pixels e eventos |
| `viewer.cpp` / `viewer.h` | Controles, rolagem e cache da página |
| `common.h` | Tipos compartilhados: bytes, ponto, matriz e bitmap |

`Document` é dono dos bytes originais; objetos usam views desses bytes.
Mantenha o documento vivo enquanto o `Renderer` existir. Páginas internas
começam em zero. A interface e a linha de comando começam em 1.

O parser usa os offsets da tabela xref (clássica ou stream), inclusive a cadeia
`/Prev`, e resolve objetos compactados em `/ObjStm`; não procura `obj` dentro
de dados binários. A árvore `/Pages` determina a ordem. O renderizador mantém
a pilha `q`/`Q`, transforma coordenadas PDF em pixels e usa larguras de `/W` ou
`/Widths`. Desenha fontes TrueType `FontFile2` e CFF `FontFile3`/`Type1C`.
Form XObjects usam seus recursos, matriz e BBox; os recortes preservam a regra
nonzero/even-odd. Imagens podem usar paletas, stencil e SMask; há composição
Normal/Multiply e softmask de luminosidade para os casos encontrados no livro.
Os contornos são reutilizados, e a interface reaproveita o bitmap ao rolar.
O antialiasing renderiza com o dobro da resolução e reduz o resultado.

Para evoluir, comece pelo módulo relacionado à mensagem de erro e adicione
um PDF pequeno ao teste. Evite adicionar suporte silencioso que apenas
ignore operadores visuais: um recurso desconhecido deve gerar um erro claro.

## Testes

```sh
make test
```

Os testes usam somente C++ e arquivos temporários. Verificam:

- DEFLATE contra vetores de referência, truncamentos e checksum incorreto;
- escapes, arrays, referências e números no lexer;
- glifos TrueType simples e compostos, além de fontes truncadas;
- JPEG cinza com IDCT e marcadores de restart;
- preenchimento nonzero/even-odd, buracos, recorte e transformação de imagens;
- PDF sintético com duas páginas, herança de recursos, conteúdos em array,
  atualização incremental de xref e comparação de pixels;
- caminhos com espaços/acentos, arquivos inválidos e recursos não suportados;
- xref streams com predictor PNG Up, ObjStm e atualizações incrementais;
- contornos CFF/Type2 com subrotinas, fontes truncadas e referências inválidas;
- fax Group 4, JPEG CMYK/YCCK, paletas Indexed e máscaras de opacidade;
- Form XObjects com matriz, recursos próprios e BBox, recortes triangulares e pontilhados;
- loop da interface com renderização controlada: prévia imediata do zoom, navegação
  durante renderização, N/P, Page Up/Page Down e reutilização do cache.

Validação desta atualização: compilação AMD AOCC, testes sintéticos e renderização
de todas as 334 páginas do Effective Modern C++ e das 799 do A Tour of C++ a 100%.
Os testes sintéticos também passaram com AddressSanitizer e UndefinedBehaviorSanitizer.
LeakSanitizer ficou desativado porque não funciona sob o rastreamento desta sessão.
O executável continua ligado apenas ao runtime C/C++ e ao carregador do sistema.
Na atualização da interface, o teste de abertura X11 (`--smoke`) também passou.
O teste automatizado dos controles usa uma janela simulada e não depende de X11.

## Limites deliberados

Esta base implementa os subconjuntos usados pelos dois PDFs de referência.
Ainda não há xref híbrido, criptografia, CFF CID/ExpertEncoding, Type1 clássico
(PostScript) ou fontes não embutidas. O intérprete Type2 cobre os operadores
geométricos e subrotinas utilizados; operadores aritméticos e `seac` ainda
produzem erro. JPEG progressivo e scans separados, fax Group 3, filtros de
imagem adicionais, máscaras de cor e rotação de página não são suportados.

Transparência permanece parcial: não há composição geral de grupos isolados,
knockout, alpha constante diferente de 1 ou modos além de Normal/Multiply.
As máscaras de luminosidade não aceitam funções de transferência; a conversão
de cores é aproximada, sem gerenciamento ICC. CropBox não é aplicado; a área
exibida é MediaBox. Anotações e links não são interativos. Não há busca, cópia
de texto, seleção, impressão, edição ou persistência da última página.

O desenho é uma aproximação: sem hinting TrueType, curvas subdivididas,
reamostragem simples de imagens e ajuste aproximado dos traços. A renderização da janela ocorre em segundo plano; o zoom reutiliza a imagem
disponível até a nova renderização terminar. Até quatro imagens recentes são
reutilizadas, com orçamento de 64 MiB (uma imagem isolada pode atingir esse limite). Há limites de tamanho: arquivo de 512 MiB,
stream descomprimido de 64 MiB e página final de 16 milhões de pixels.
Os buffers temporários e os caches consomem memória adicional.

## Referências de formato

- [DEFLATE — RFC 1951](https://www.rfc-editor.org/rfc/rfc1951)
- [zlib — RFC 1950](https://www.rfc-editor.org/rfc/rfc1950)
- [TrueType/OpenType: tabela glyf](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf)
- [Protocolo X11](https://xorg.freedesktop.org/archive/X11R7.7/doc/xproto/x11protocol.html)

- [CFF — Adobe Technical Note #5176](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf)
- [Type2 — Adobe Technical Note #5177](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5177.Type2.pdf)
- [ITU T.4 — tabelas de códigos usadas pelo fax](https://www.itu.int/rec/T-REC-T.4)
- [ITU T.6 — fax Group 4](https://www.itu.int/rec/T-REC-T.6)
