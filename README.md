# PS4 USB Link Fixer

Homebrew para PS4 desbloqueado (GoldHEN) focado nos links simbólicos criados por **Itemzflow > Move to USB**.

## Objetivo
O Itemzflow copia os PKGs para `/mnt/usbN/itemzflow/apps/<TITLE_ID>/` e substitui o arquivo interno por um symlink. Quando as unidades trocam de índice USB, o symlink pode apontar para o HD errado.

Este projeto usa os mesmos nomes de arquivo observados no código do Itemzflow, mas é independente.

## Segurança da versão inicial
- Primeiro verifica quais caminhos existem e lê os destinos de links.
- Nunca move, reinstala, formata ou regrava arquivos .pkg.
- Nunca modifica arquivo regular ou link com destino não reconhecido.
- Reparo será liberado **somente após validar o diagnóstico num PS4 real**.
- A análise não presume que ambos os HDs estejam danificados.
- Logs para auditoria sob `/data/USBLinkFixer/` (se permitido).

## Fontes
- [Itemzflow - implementação de Move to USB](https://github.com/LightningMods/Itemzflow/blob/main/itemzflow/source/GLES2_itemzflow.cpp)
- [Itemzflow - função usbpath](https://github.com/LightningMods/Itemzflow/blob/main/itemzflow/source/utils.cpp)

## Versões
- v0.3: interface e scanner read-only, lista de CUSA e vínculos, relatório.
- v0.4 planejada: correção transacional com confirmação e restauração; executar jogo pela API do PS4.

## Desenvolvimento
Compilação por OpenOrbis SDK 0.5.4, usando `make` ou GitHub Actions. O pacote precisa ser testado no PS4 real antes que seja considerado compatível.
