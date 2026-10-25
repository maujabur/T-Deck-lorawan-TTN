# src/common

Código compartilhado entre os firmwares deste projeto (T-Deck scanner e
Heltec gateway). Compilado por TODOS os environments via `build_src_filter`.

Candidatos naturais a morar aqui:
- Credenciais/identificadores comuns (joinEUI, devEUI, appKey)
- Parâmetros de rádio comuns (frequência base AU915, SF, largura de banda)
- Helpers utilitários independentes de placa

Mantenha aqui apenas código SEM dependência de pinos específicos de placa.
