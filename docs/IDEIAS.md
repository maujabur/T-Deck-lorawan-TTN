# Ideias futuras — painel web (docs/index.html)

Notas de design para evoluções do mapa. Nada aqui está implementado; são
decisões a tomar quando formos mexer.

## Painel genérico para qualquer canal ThingSpeak

**Motivação.** Hoje o painel é específico do range test: a cor dos pontos vem
fixa de `field1` (RSSI ida), com faixas em dBm, e os popups/legenda têm rótulos
hardcoded. Para servir a outros canais (ex.: temperatura, umidade), o painel
precisaria escolher qual campo colore os pontos e como mapear o valor para cor.

**O que está amarrado ao caso atual (range test):**
- Cor dos pontos: `rssiColor(field1)` com faixas fixas -70 / -100 dBm.
- Popups: rótulos fixos (RSSI ida/volta, SNR, seq/acks, waypoint = field7).
- Legenda: descreve RSSI.
- Waypoint: convenção de usar `field7` como rótulo de marca.
- (Lat/lon via `location=true` já é genérico — mantém.)

**Decisões de design a tomar:**

1. **Seleção do campo de cor.** Dropdown "Colorir por: field1..field8",
   preenchido com os nomes reais dos campos. O `feeds.json` já traz
   `channel.field1`, `channel.field2`, ... com os rótulos definidos pelo dono
   do canal — usar esses nomes em vez de "field1".

2. **Mapa de valor -> cor.** Duas abordagens:
   - *Auto-escala* (padrão sugerido): min/max do campo nos dados carregados,
     gradiente contínuo. Zero config, funciona para qualquer grandeza.
   - *Faixas configuráveis*: usuário define limiares e cores. Mais poderoso,
     mais UI. Deixar como opção avançada.

3. **Popup genérico.** Listar os campos presentes usando `channel.fieldN` como
   rótulo, em vez dos textos fixos atuais.

4. **Perfis / compatibilidade.** Manter um "perfil range test" (faixas de RSSI
   e rótulos atuais) como preset, para não perder o que já funciona, e um
   "perfil genérico" (auto-escala) para outros canais.

5. **Waypoint.** `field7` como marca é convenção nossa. No genérico, virar
   configurável ("campo de marca") ou tratar como só mais um campo.

**Esboço de implementação (quando for a hora):**
- Novo seletor de campo no painel + leitura de `data.channel.fieldN` para os
  rótulos.
- Generalizar `rssiColor()` para `valueColor(value, min, max)` (auto-escala) e
  manter `rssiColor()` só no preset range test.
- `popupFor()` monta linhas a partir dos campos presentes + seus nomes.
