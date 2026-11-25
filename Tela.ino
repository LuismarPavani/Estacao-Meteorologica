/*
 * DISPLAY OLED: TELAS, ÍCONES, BRILHO, TEMPO LIGADA E AGENDA
 *
 * Tudo o que desenha no OLED ('desenha...') e controla a tela.
 * ESP32: trocar a aba inteira (TFT colorida, outra biblioteca).
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- DISPLAY OLED: TELAS E ÍCONES ---------------------------------------------------------

/** Liga (com o brilho certo para agora) ou desliga fisicamente o OLED conforme a variável displayLigado. */
void aplicaDisplay() {
  if (displayLigado) ligaTela();
  else display.displayOff();
}

/** Percentual de brilho aceito: 5 a 100, de 5 em 5. */
bool brilhoValido(int v) {
  return v >= 5 && v <= 100 && v % 5 == 0;
}

/**
 * true quando vale o brilho noturno. Pelo sol: do pôr ao nascer (sol abaixo do horizonte; sem
 * coordenadas ou sem hora ainda: false). Pela agenda: no horário de tela desligada (sem agenda: false).
 */
bool noiteAgora() {
  if (noitePeloSol) return astroOk && solAltura < -0.833f;   // -0,833° = mesma referência do nascer/pôr calculados
  return agendaEstadoAtual() == 0;
}

/**
 * Brilho que vale agora, em %: o noturno no horário de tela desligada da agenda (mesmo se a tela
 * foi acesa pelo botão), o de dia no resto do tempo.
 */
uint8_t brilhoAgora() {
  return noiteAgora() ? brilhoNoitePct : brilhoPct;
}

/**
 * Acende o OLED já com o brilho de agora. Use no lugar de display.displayOn(). Só reenvia o brilho
 * quando ele muda, porque setBrightness() da biblioteca também liga a tela (por isso o brilho
 * nunca é mandado com a tela apagada; o display guarda o valor enquanto está apagado).
 */
void ligaTela() {
  uint8_t pct = brilhoAgora();
  if (pct != brilhoAplicado) {
    display.setBrightness((uint8_t)((pct * 255U + 50U) / 100U));   // 0-255; ajusta contraste, precharge e VCOMH
    brilhoAplicado = pct;
  } else {
    display.displayOn();
  }
}

/** Tela de abertura do boot: logo (sol atrás da nuvem), nome do projeto e versão do firmware. */
void desenhaAbertura() {
  display.clear();
  display.drawXbm(2, 12, Logo_width, Logo_height, Logo_bits);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(56, 8, F("Estação"));
  display.setFont(ArialMT_Plain_10);     // "Meteorológica" em 16 passaria da tela (~104 px); em 10 tem ~65 px
  display.drawString(56, 27, F("Meteorológica"));
  // Placa e versão em duas linhas: "ESP8266  v3.6.x" numa linha só tem ~80 px e passa dos 72 px
  // que sobram à direita do logo (o OLED corta o fim, e a versão aparecia só como "3.6").
  display.drawString(56, 40, F("ESP8266"));
  display.drawString(56, 51, "v" FIRMWARE_VERSAO);
  display.display();
}

/** Mostra no OLED uma mensagem com barra de progresso (usada durante a atualização do clima). */
void desenhaProgresso(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

/**
 * Desenha a mensagem temporária (título, linha 1 e linha 2) no OLED. A linha 2 quebra em até 2
 * linhas se for longa.
 */
void desenhaMensagemTela() {
  if (msgTipo == MSG_IP) { desenhaTelaIp(); return; }
  if (msgTipo == MSG_ENVIO_ANDAMENTO) { desenhaEnvioEtapa(5, F("Iniciando...")); return; }
  if (msgTipo == MSG_ENVIO_OK || msgTipo == MSG_ENVIO_FALHA) { desenhaResultadoEnvio(); return; }
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 0, msgTitulo);
  display.drawString(64, 14, msgLinha1);
  String resto = msgLinha2;
  int y = 28;
  for (int k = 0; k < 2 && resto.length() > 0; k++) {
    int corte = resto.length();
    while (corte > 1 && display.getStringWidth(resto.substring(0, corte)) > 124) corte--;   // cabe na largura da tela
    display.drawString(64, y, resto.substring(0, corte));
    resto = resto.substring(corte);
    y += 12;
  }
  display.display();
}

// ---- Telas do envio ao painel pelo botão ----

/** Faixa de título invertida (fundo aceso, texto apagado) no topo do OLED, 13 px de altura. */
void desenhaCabecalho(const String& titulo) {
  display.setColor(WHITE);
  display.fillRect(0, 0, 128, 13);
  display.setColor(BLACK);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, titulo);
  display.setColor(WHITE);
}

/** Ícone de nuvem cheia com seta para cima (envio), centrado em (cx, cy), cerca de 26x18 px. */
void desenhaIconeNuvemEnvio(int cx, int cy) {
  display.setColor(WHITE);
  display.fillCircle(cx - 7, cy + 2, 6);
  display.fillCircle(cx + 1, cy - 2, 8);
  display.fillCircle(cx + 8, cy + 3, 5);
  display.fillRect(cx - 7, cy + 2, 15, 7);
  display.setColor(BLACK);              // seta recortada dentro da nuvem
  display.fillRect(cx, cy - 2, 2, 9);
  display.drawLine(cx - 3, cy + 1, cx, cy - 3);
  display.drawLine(cx - 3, cy + 2, cx, cy - 2);
  display.drawLine(cx + 4, cy + 1, cx + 1, cy - 3);
  display.drawLine(cx + 4, cy + 2, cx + 1, cy - 2);
  display.setColor(WHITE);
}

/** Ícone de sucesso: círculo cheio com um 'visto' recortado, raio 11, centrado em (cx, cy). */
void desenhaIconeOk(int cx, int cy) {
  display.setColor(WHITE);
  display.fillCircle(cx, cy, 11);
  display.setColor(BLACK);
  for (int k = 0; k < 2; k++) {         // traço com 2 px de espessura
    display.drawLine(cx - 6, cy + k, cx - 2, cy + 4 + k);
    display.drawLine(cx - 2, cy + 4 + k, cx + 6, cy - 4 + k);
  }
  display.setColor(WHITE);
}

/** Ícone de falha: anel com um 'X' dentro, raio 11, centrado em (cx, cy). */
void desenhaIconeFalha(int cx, int cy) {
  display.setColor(WHITE);
  display.drawCircle(cx, cy, 11);
  display.drawCircle(cx, cy, 10);
  for (int k = 0; k < 2; k++) {
    display.drawLine(cx - 5 + k, cy - 5, cx + 5 + k, cy + 5);
    display.drawLine(cx + 5 + k, cy - 5, cx - 5 + k, cy + 5);
  }
}

/** Corta o texto para caber em 'largura' px na fonte atual. */
String cabeNaLargura(const String& s, int largura) {
  int corte = s.length();
  while (corte > 0 && display.getStringWidth(s.substring(0, corte)) > largura) corte--;
  return s.substring(0, corte);
}

/**
 * Tela durante o envio pelo botão: título, nuvem com seta, a etapa atual e uma barra de progresso.
 * Chamada em cada etapa (o envio trava o loop, então a tela é desenhada na hora).
 */
void desenhaEnvioEtapa(int pct, const String& texto) {
  display.clear();
  desenhaCabecalho(F("PAINEL WEB"));
  desenhaIconeNuvemEnvio(16, 30);
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(34, 15, F("Enviando"));
  display.setFont(ArialMT_Plain_10);
  display.drawString(34, 33, cabeNaLargura(texto, 94));
  display.drawProgressBar(2, 51, 124, 10, pct);
  display.display();
}

/**
 * Tela de resultado do envio pelo botão. Sucesso: visto, 'Enviado!', código HTTP, tempo e a leitura
 * enviada. Falha: X, 'Falhou', o erro (em até 2 linhas) e quando será a nova tentativa.
 */
void desenhaResultadoEnvio() {
  bool ok = (msgTipo == MSG_ENVIO_OK);
  display.clear();
  desenhaCabecalho(F("PAINEL WEB"));
  if (ok) desenhaIconeOk(14, 38);
  else desenhaIconeFalha(14, 38);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(32, 14, ok ? F("Enviado!") : F("Falhou"));
  display.setFont(ArialMT_Plain_10);

  // tempo do envio com 1 casa e vírgula (ex.: 1,2 s); 0 = falhou antes de conectar
  String dur = "";
  if (apiUltimoDurMs > 0) {
    dur = String(apiUltimoDurMs / 1000UL) + F(",") + String((apiUltimoDurMs % 1000UL) / 100UL) + F(" s");
  }

  if (ok) {
    display.drawString(32, 32, apiUltimoMsg + F("  ") + dur);
    String leitura = sensorOk ? String(localTemp, 1) + F("°C  ") + String(localHum, 0) + F("%") : String(F("sensor sem leitura"));
    display.drawString(32, 44, cabeNaLargura(leitura, 96));
  } else {
    String erro = apiUltimoMsg;
    String l1 = cabeNaLargura(erro, 96);
    String l2 = cabeNaLargura(erro.substring(l1.length()), 96);
    display.drawString(32, 32, l1);
    if (l2.length()) display.drawString(32, 44, l2);
    else display.drawString(32, 44, F("Nova tentativa: ") + String(PAINEL_RETRY_MS / 60000UL) + F(" min"));
  }
  display.display();
}

/**
 * Mostra uma mensagem de 3 linhas no OLED por 'dur' ms, por cima das telas. Desenha na hora (mesmo
 * durante um envio demorado) e acende a tela se estiver apagada; ao terminar, a tela volta ao
 * estado anterior.
 */
void mostraMensagemTela(const String& titulo, const String& l1, const String& l2, unsigned long dur) {
  msgTitulo = titulo;
  msgLinha1 = l1;
  msgLinha2 = l2;
  msgAte = millis() + dur;
  if (!msgAtiva && !displayLigado) {
    msgLigouTela = true;                // a tela estava apagada: acende só para a mensagem
    ligaTela();                         // com o brilho de agora (noturno à noite)
  }
  msgAtiva = true;
  desenhaMensagemTela();
}

/**
 * Chamada pelo loop(): mantém a mensagem na tela até o tempo acabar. Devolve true enquanto ela
 * está ativa (o loop não redesenha as telas nesse período).
 */
bool trataMensagemTela(unsigned long ms) {
  if (!msgAtiva) return false;
  if ((long)(ms - msgAte) >= 0) {
    msgAtiva = false;
    msgTipo = MSG_TEXTO;                // a próxima mensagem volta a ser texto simples, salvo indicação
    if (msgLigouTela) {
      msgLigouTela = false;
      aplicaDisplay();                  // volta a apagar a tela, se ela estava apagada
    }
    return false;
  }
  return true;
}

/** true depois que a hora da internet (NTP) sincronizou. Antes disso o relógio marca 1970. */
bool horaOk() {
  return time(nullptr) > 100000;
}

/**
 * Desenha, numa tela de clima, o aviso de que ainda não há dados (com o motivo): sem WiFi, sem
 * chave da API ou aguardando a consulta.
 */
void desenhaSemClima(OLEDDisplay *display, int16_t x, int16_t y, const String& titulo) {
  const __FlashStringHelper* motivo = (WiFi.status() != WL_CONNECTED) ? F("Sem WiFi")
                     : (OPEN_WEATHER_MAP_APP_ID.length() == 0) ? F("Falta a chave da API")
                     : F("Aguardando dados...");
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, y, titulo);
  display->setFont(ArialMT_Plain_24);
  display->drawString(64 + x, 12 + y, F("--"));
  display->setFont(ArialMT_Plain_10);
  display->drawString(64 + x, 38 + y, motivo);
}

/** Tela 1 do OLED: dia da semana, data e hora ('--' até a hora da internet sincronizar). */
void desenhaDataHora(OLEDDisplay *display, OLEDDisplayUiState* /*state*/, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[24];

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);

  if (!horaOk()) {                      // ainda sem NTP (ex.: boot sem WiFi): não mostra 01/01/1970
    display->drawString(64 + x, 5 + y, F("Aguardando a hora"));
    display->setFont(ArialMT_Plain_24);
    display->drawString(64 + x, 15 + y, F("--:--:--"));
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    return;
  }

  snprintf_P(buff, sizeof(buff), PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 5 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  snprintf_P(buff, sizeof(buff), PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

/** Tela 2 do OLED: umidade e temperatura internas do sensor, com uma casa decimal. */
void desenhaClimaInterno(OLEDDisplay *display, OLEDDisplayUiState* /*state*/, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(22 + x, y, F("CLIMA INTERNO"));
  display->drawString(0 + x, 12 + y, F("Umid"));
  display->drawString(66 + x, 12 + y, F("Temp"));

  // Valores com 1 casa decimal; a unidade vai em fonte pequena logo depois, para caber nas duas colunas
  String hs = sensorOk ? String(localHum, 1) : String(F("--"));
  String ts = sensorOk ? String(localTemp, 1) : String(F("--"));
  display->setFont(ArialMT_Plain_24);
  int wh = display->getStringWidth(hs);
  int wt = display->getStringWidth(ts);
  display->drawString(0 + x, 22 + y, hs);
  display->drawString(66 + x, 22 + y, ts);
  if (sensorOk) {
    display->setFont(ArialMT_Plain_10);
    display->drawString(0 + x + wh + 1, 36 + y, F("%"));
    display->drawString(66 + x + wt + 1, 36 + y, F("°C"));
  }
}

/** Tela 3 do OLED: ícone, temperatura, cidade e descrição do clima atual. */
void desenhaClimaAtual(OLEDDisplay *display, OLEDDisplayUiState* /*state*/, int16_t x, int16_t y) {
  if (!climaRecebido) { desenhaSemClima(display, x, y, F("CLIMA ATUAL")); return; }
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? F("°C") : F("°F"));
  display->drawString(60 + x, 1 + y, temp);

  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(60 + x, 26 + y, currentWeather.cityName);

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}

/**
 * Tela 4 do OLED ('PRÓXIMAS 24 H'): mínima e máxima das próximas 24 h, umidade, sensação térmica,
 * chuva prevista nas 24 h e vento. Pressão e visibilidade ficam na página web.
 */
void desenhaClimaDetalhes(OLEDDisplay *display, OLEDDisplayUiState* /*state*/, int16_t x, int16_t y) {
  if (!climaRecebido) { desenhaSemClima(display, x, y, F("PRÓXIMAS 24 H")); return; }
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, y, F("PRÓXIMAS 24 H"));
  display->setTextAlignment(TEXT_ALIGN_LEFT);

  String unit = IS_METRIC ? F("°C") : F("°F");
  // Mín/máx: das próximas 24 h (previsão); sem previsão ainda, '--'
  String mn = prox24Ok ? String(prox24Min, 1) + unit : String(F("--"));
  String mx = prox24Ok ? String(prox24Max, 1) + unit : String(F("--"));
  display->drawString(x, 12 + y, F("Min: ") + mn);
  display->drawString(x, 24 + y, F("Max: ") + mx);
  display->drawString(x, 36 + y, F("Umid: ") + String(currentWeather.humidity) + F("%"));

  // A API devolve o vento em m/s (metric) ou mph (imperial); a chuva em mm
  float wind = IS_METRIC ? currentWeather.windSpeed * 3.6 : currentWeather.windSpeed;
  // chuva com 1 casa até 9,9 mm e sem casa a partir de 10 mm, para caber na coluna
  String chuva = prox24Ok ? String(prox24Chuva, prox24Chuva < 9.95f ? 1 : 0) + F("mm") : String(F("--"));
  display->drawString(60 + x, 12 + y, F("Sens: ") + String(prox24Ok ? prox24Sens : currentWeather.feelsLike, 0) + unit);
  display->drawString(60 + x, 24 + y, F("Chuva: ") + chuva);
  display->drawString(60 + x, 36 + y, F("Ven: ") + String(wind, 0) + (IS_METRIC ? F("km/h") : F("mph")));
}

/** Tela 5 do OLED: previsão dos próximos 3 dias (usa desenhaPrevisaoDia para cada dia). */
void desenhaPrevisao(OLEDDisplay *display, OLEDDisplayUiState* /*state*/, int16_t x, int16_t y) {
  if (!climaRecebido) { desenhaSemClima(display, x, y, F("PRÓXIMOS DIAS")); return; }
  desenhaPrevisaoDia(display, x, y, 0);
  desenhaPrevisaoDia(display, x + 44, y, 1);
  desenhaPrevisaoDia(display, x + 88, y, 2);
}

/** Desenha uma coluna da previsão: dia da semana, ícone e temperatura. */
void desenhaPrevisaoDia(OLEDDisplay *display, int x, int y, int dayIndex) {
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  if (observationTimestamp == 0) {      // este dia da previsão não veio: '--' em vez de 'QUI 0°C'
    display->drawString(x + 20, y, F("--"));
    display->drawString(x + 20, y + 34, F("--"));
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    return;
  }
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? F("°C") : F("°F"));
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * Tela 6 do OLED: bateria (com a tomada indicando energia externa), sinal do WiFi (ícone, dBm e
 * %), nome da rede (SSID) e IP.
 */
void desenhaTelaWiFi(OLEDDisplay *display, OLEDDisplayUiState* /*state*/, int16_t x, int16_t y) {
  char aux[48];
  int32_t rssi = WiFi.RSSI();

  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);

  // Linha 1: bateria
  if (bateriaAtiva) {
    desenhaIconeBateria(display, x, y + 2, bateriaPct);
    display->drawString(x + 24, y, String(bateriaPct) + F("%  ") + String(bateriaV, 2) + F("V"));
  } else {
    display->drawString(x, y, F("Bateria: desativada"));
  }
  if (energiaAtiva && PIN_ENERGIA >= 0) desenhaIconeTomada(display, x + 112, y, energiaExterna);   // tomada: energia externa conectada ou não

  // Linha 2: potência do sinal (ícone + dBm + %)
  if (WiFi.status() == WL_CONNECTED) {
    int pct = qualidadeSinal(rssi);
    desenhaIconeSinal(display, x, 12 + y, pct);
    snprintf(aux, sizeof(aux), "%ddBm  %d%%", (int)rssi, pct);
    display->drawString(x + 24, 12 + y, aux);
  } else {
    desenhaIconeSinal(display, x, 12 + y, 0);
    display->drawString(x + 24, 12 + y, F("sem WiFi"));
  }

  // Linha 3: nome da rede
  snprintf(aux, sizeof(aux), "SSID: %s", WiFi.SSID().c_str());
  display->drawString(x, 24 + y, aux);

  // Linha 4: IP
  snprintf(aux, sizeof(aux), "IP: %s", WiFi.localIP().toString().c_str());
  display->drawString(x, 36 + y, aux);
}

/** Barra inferior fixa em todas as telas: hora à esquerda e temperatura externa à direita. */
void desenhaBarraInferior(OLEDDisplay *display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  if (horaOk()) snprintf_P(buff, sizeof(buff), PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);
  else snprintf_P(buff, sizeof(buff), PSTR("--:--"));                   // ainda sem hora da internet

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  String temp = (climaRecebido ? String(currentWeather.temp, 0) : String(F("--"))) + (IS_METRIC ? F("°C") : F("°F"));   // '--°C' até chegar o clima
  display->drawString(128, 54, temp);
  display->drawHorizontalLine(0, 52, 128);
  desenhaIndicadores(display, state);
}

/**
 * Indicadores das telas na barra inferior, compactos (no lugar dos da biblioteca, que usam 12 px
 * por tela e encostariam na hora e na temperatura): um ponto a cada 7 px, centralizados; a tela
 * atual é um ponto cheio maior. Com uma tela só, não desenha nada.
 */
void desenhaIndicadores(OLEDDisplay *display, OLEDDisplayUiState* state) {
  if (numTelasAtivas <= 1) return;
  const int passo = 7, yc = 58;
  int x0 = 64 - ((numTelasAtivas - 1) * passo) / 2;
  display->setColor(WHITE);
  for (uint8_t k = 0; k < numTelasAtivas; k++) {
    int cx = x0 + k * passo;
    if (k == state->currentFrame) display->fillCircle(cx, yc, 2);   // tela atual: 5 px cheio
    else display->drawCircle(cx, yc, 1);                            // outras: anel de 3 px
  }
}

// ---- Tela 'Sol e Lua' -----------------------------------------------------------------------

/** Hora local 'HH:MM' de um instante (ou '--:--' se ainda não houver o valor). */
String horaCurta(time_t t) {
  if (t <= 0) return F("--:--");
  struct tm* ti = localtime(&t);
  char b[16];   // folga para o compilador (int com sinal), evita -Wformat-truncation
  snprintf(b, sizeof(b), "%02d:%02d", ti->tm_hour, ti->tm_min);
  return String(b);
}

/** Nome da fase da Lua (0 = nova, 4 = cheia), como devolvido por Astronomy::calculateMoonData. */
const __FlashStringHelper* nomeFaseLua(uint8_t fase) {
  static const char N0[] PROGMEM = "Lua nova";
  static const char N1[] PROGMEM = "Crescente";
  static const char N2[] PROGMEM = "Quarto crescente";
  static const char N3[] PROGMEM = "Crescente gibosa";
  static const char N4[] PROGMEM = "Lua cheia";
  static const char N5[] PROGMEM = "Minguante gibosa";
  static const char N6[] PROGMEM = "Quarto minguante";
  static const char N7[] PROGMEM = "Minguante";
  static const char* const NOMES[] = {N0, N1, N2, N3, N4, N5, N6, N7};
  return FPSTR(NOMES[fase & 7]);
}

/**
 * Recalcula a fase e a iluminação da Lua (só precisa da hora) e, com as coordenadas da cidade, o
 * nascer/pôr do sol e da Lua e a altura do sol. Contas locais, sem internet; chamada a cada minuto.
 */
void atualizaAstro() {
  if (!horaOk()) { luaOk = false; astroOk = false; return; }
  time_t agora = time(nullptr);
  Astronomy astronomia;
  Astronomy::MoonData md = astronomia.calculateMoonData(agora);
  luaFase = md.phase;
  luaIlum = md.illumination;
  luaOk = true;
  if (!coordOk) { astroOk = false; return; }      // sem coordenadas ainda: os horários ficam '--:--'
  SunMoonCalc calc(agora, cidadeLat, cidadeLon);  // 'agora' em UTC; lat/lon em graus
  SunMoonCalc::Result r = calc.calculateSunAndMoonData();
  solNasce = r.sun.rise;
  solPoe = r.sun.set;
  luaNasce = r.moon.rise;
  luaPoe = r.moon.set;
  solAltura = r.sun.elevation;
  astroOk = true;
}

/** Seta pequena (5x7 px) para cima (nascer) ou para baixo (pôr), alinhada a uma linha de texto em y. */
void desenhaSeta(OLEDDisplay *display, int x, int y, bool cima) {
  display->drawVerticalLine(x + 2, y + 3, 7);
  if (cima) {
    display->drawLine(x, y + 5, x + 2, y + 3);
    display->drawLine(x + 4, y + 5, x + 2, y + 3);
  } else {
    display->drawLine(x, y + 7, x + 2, y + 9);
    display->drawLine(x + 4, y + 7, x + 2, y + 9);
  }
}

/** Sol pequeno (11x11 px): disco com 8 raios curtos. */
void desenhaSolPequeno(OLEDDisplay *display, int cx, int cy) {
  display->fillCircle(cx, cy, 2);
  display->drawVerticalLine(cx, cy - 5, 2);
  display->drawVerticalLine(cx, cy + 4, 2);
  display->drawHorizontalLine(cx - 5, cy, 2);
  display->drawHorizontalLine(cx + 4, cy, 2);
  display->setPixel(cx - 3, cy - 3); display->setPixel(cx + 3, cy - 3);
  display->setPixel(cx - 3, cy + 3); display->setPixel(cx + 3, cy + 3);
}

/** Lua pequena (9x9 px) em forma de crescente. */
void desenhaLuaPequena(OLEDDisplay *display, int cx, int cy) {
  display->fillCircle(cx, cy, 4);
  display->setColor(BLACK);
  display->fillCircle(cx + 2, cy - 1, 3);
  display->setColor(WHITE);
}

/**
 * Desenha a Lua com a parte iluminada da fase atual: linha a linha, acende do terminador até a
 * borda. Crescente = iluminada à direita no hemisfério norte e à esquerda no sul (invertido).
 */
void desenhaLua(OLEDDisplay *display, int cx, int cy, int r, uint8_t fase, float ilum, bool sul) {
  bool direita = (fase <= 4);           // 1 a 3 = crescente (0 e 4: nova/cheia, o lado não importa)
  if (sul) direita = !direita;
  for (int dy = -r; dy <= r; dy++) {
    float w = sqrtf((float)(r * r - dy * dy));
    int borda = (int)(w + 0.5f);
    int term = (int)lroundf(w * (1.0f - 2.0f * ilum));   // terminador: de +w (nova) a -w (cheia)
    if (borda - term <= 0) continue;
    if (direita) display->drawHorizontalLine(cx + term, cy + dy, borda - term + 1);
    else display->drawHorizontalLine(cx - borda, cy + dy, borda - term + 1);
  }
  display->drawCircle(cx, cy, r);       // contorno: a Lua nova também aparece
}

/**
 * Tela 'Sol e Lua': nascer e pôr do sol e da Lua (com as coordenadas da cidade) e a Lua desenhada
 * com a fase e a porcentagem iluminada (só com a data).
 */
void desenhaTelaSolLua(OLEDDisplay *display, OLEDDisplayUiState* /*state*/, int16_t x, int16_t y) {
  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);

  // Linha 1: sol (nascer e pôr)   Linha 2: Lua (nascer e pôr)
  desenhaSolPequeno(display, 5 + x, 6 + y);
  desenhaSeta(display, 14 + x, y, true);
  display->drawString(21 + x, y, astroOk ? horaCurta(solNasce) : String(F("--:--")));
  desenhaSeta(display, 54 + x, y, false);
  display->drawString(61 + x, y, astroOk ? horaCurta(solPoe) : String(F("--:--")));

  desenhaLuaPequena(display, 5 + x, 18 + y);
  desenhaSeta(display, 14 + x, 12 + y, true);
  display->drawString(21 + x, 12 + y, astroOk ? horaCurta(luaNasce) : String(F("--:--")));
  desenhaSeta(display, 54 + x, 12 + y, false);
  display->drawString(61 + x, 12 + y, astroOk ? horaCurta(luaPoe) : String(F("--:--")));

  // Embaixo: a Lua com a fase, o nome e a iluminação
  bool sul = coordOk ? (cidadeLat < 0) : true;   // sem coordenadas: hemisfério sul (padrão do projeto, Brasil)
  if (luaOk) desenhaLua(display, 15 + x, 38 + y, 12, luaFase, luaIlum, sul);
  else display->drawCircle(15 + x, 38 + y, 12);
  display->drawString(34 + x, 26 + y, luaOk ? String(nomeFaseLua(luaFase)) : String(F("Aguardando a hora")));
  display->drawString(34 + x, 38 + y, luaOk ? F("Iluminada: ") + String((int)(luaIlum * 100 + 0.5f)) + F("%") : String(F("--")));
}

// ---- Escolha das telas ---------------------------------------------------------------------

/** Quantas telas estão marcadas na máscara. */
uint8_t contaTelas(uint8_t mask) {
  uint8_t n = 0;
  for (uint8_t k = 0; k < TELAS_TOTAL; k++) if (mask & (1 << k)) n++;
  return n;
}

/**
 * Monta a lista das telas marcadas e entrega ao OLED (volta para a 1ª). Com uma tela só, ela fica
 * fixa: sem passagem automática.
 */
void aplicaTelas() {
  numTelasAtivas = 0;
  for (uint8_t k = 0; k < TELAS_TOTAL; k++) {
    if (telasMask & (1 << k)) framesAtivos[numTelasAtivas++] = TELAS[k];
  }
  if (numTelasAtivas == 0) { framesAtivos[0] = TELAS[0]; numTelasAtivas = 1; }   // segurança: nunca fica sem tela
  ui.setFrames(framesAtivos, numTelasAtivas);
  if (numTelasAtivas > 1) ui.enableAutoTransition();
  else ui.disableAutoTransition();
}

// ---- Telas do boot: conectando e WiFi conectado ---------------------------------------------

/** 'Conectando ao WiFi' com o ícone e 3 pontos que se alternam (passo = qual ponto está aceso). */
void desenhaConectando(uint8_t passo) {
  display.clear();
  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, F("Conectando ao WiFi"));
  display.drawXbm(34, 13, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
  for (uint8_t k = 0; k < 3; k++) {
    int cx = 52 + k * 12;
    if (k == passo % 3) display.fillCircle(cx, 57, 3);
    else display.fillCircle(cx, 57, 1);
  }
  display.display();
}

/** Ícone grande de sinal do WiFi (4 barras, 22x17 px): barras cheias conforme a qualidade. */
void desenhaIconeWiFiGrande(int x, int y, int pct) {
  int n = 0;
  if (pct >= 75) n = 4;
  else if (pct >= 50) n = 3;
  else if (pct >= 25) n = 2;
  else if (pct > 0) n = 1;
  for (int k = 0; k < 4; k++) {
    int h = 5 + k * 4;                  // alturas 5, 9, 13 e 17 px
    int bx = x + k * 6;                 // barras de 4 px com 2 px de espaço
    int by = y + 17 - h;                // alinhadas pela base
    if (k < n) display.fillRect(bx, by, 4, h);
    else display.drawRect(bx, by, 4, h);
  }
}

/**
 * Tela 'WIFI CONECTADO' (logo depois de conectar): faixa de título, ícone de sinal, nome da rede,
 * sinal em dBm e %, e o IP em destaque, para abrir a página no navegador.
 */
void desenhaTelaIp() {
  int rssi = WiFi.RSSI();
  int pct = qualidadeSinal(rssi);
  display.clear();
  desenhaCabecalho(F("WIFI CONECTADO"));
  desenhaIconeWiFiGrande(4, 15, pct);

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(32, 13, cabeNaLargura(WiFi.SSID(), 94));
  display.drawString(32, 23, String(rssi) + F(" dBm  ") + String(pct) + F("%"));

  String ip = WiFi.localIP().toString();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  if (display.getStringWidth(ip) > 126) display.setFont(ArialMT_Plain_10);   // IP muito longo: fonte menor
  display.drawString(64, 34, ip);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 52, F("abra no navegador"));
  display.display();
}

// ---- Ícones da barra inferior (bateria, sinal do WiFi e tomada) ----------------------------

/** Desenha o ícone de bateria (20x9 px) com o nível proporcional ao percentual. */
void desenhaIconeBateria(OLEDDisplay *display, int x, int y, int pct) {
  display->drawRect(x, y, 18, 9);                          // corpo
  display->fillRect(x + 18, y + 2, 2, 5);                  // ponta
  display->fillRect(x + 2, y + 2, (pct * 14) / 100, 5);    // nível
}

/**
 * Desenha o ícone de sinal de WiFi (4 barras crescentes, 15x10 px), igual ao da página web. Barras
 * cheias = força do sinal; as demais ficam só com o contorno.
 */
void desenhaIconeSinal(OLEDDisplay *display, int x, int y, int pct) {
  int n = 0;
  if (pct >= 75) n = 4;
  else if (pct >= 50) n = 3;
  else if (pct >= 25) n = 2;
  else if (pct > 0) n = 1;
  for (int k = 0; k < 4; k++) {
    int h = 3 + k * 2;                  // alturas 3, 5, 7 e 9 px
    int bx = x + k * 4;                 // barras de 3 px com 1 px de espaço
    int by = y + 10 - h;                // alinhadas pela base
    if (k < n) display->fillRect(bx, by, 3, h);
    else display->drawRect(bx, by, 3, h);
  }
}

/**
 * Desenha o ícone de tomada no OLED (12x10 px): cheio = energia externa conectada; só contorno e
 * riscado = sem energia externa (rodando na bateria).
 */
void desenhaIconeTomada(OLEDDisplay *display, int x, int y, bool ligada) {
  display->fillRect(x + 3, y, 1, 3);            // pino esquerdo
  display->fillRect(x + 8, y, 1, 3);            // pino direito
  if (ligada) display->fillRect(x + 1, y + 3, 10, 4);   // corpo cheio
  else display->drawRect(x + 1, y + 3, 10, 4);          // corpo vazio
  display->fillRect(x + 5, y + 7, 2, 3);        // cabo
  if (!ligada) display->drawLine(x, y + 9, x + 11, y);  // risco: sem energia
}

/** Converte o sinal em dBm para qualidade de 0 a 100%. */
int qualidadeSinal(int rssi) {
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
}

// ---- TELA: TEMPO LIGADA E AGENDA ----------------------------------------------------------

/**
 * Programa o apagar automático da tela daqui a 'dur' ms (dur = 0 cancela a contagem: tela sempre
 * ligada).
 */
void armaTimerTela(unsigned long dur) {
  if (dur == 0) {
    telaContando = false;
    return;
  }
  telaDesligaEm = millis() + dur;
  telaContando = true;
}

/** Tempo de tela ligada escolhido em /config, em ms (0 = sempre ligada). */
unsigned long telaDuracaoPadraoMs() {
  return (unsigned long)telaTimeoutMin * 60000UL;
}

/**
 * Chamada pelo loop(): apaga a tela quando a contagem do tempo de tela ligada termina (espera uma
 * mensagem temporária acabar, se houver).
 */
void verificaTimerTela(unsigned long ms) {
  if (telaContando && displayLigado && !msgAtiva && (long)(ms - telaDesligaEm) >= 0) {
    telaContando = false;
    telaAcordadaNoite = false;
    displayLigado = false;              // não salva na EEPROM (evita desgaste); o botão acende de novo
    aplicaDisplay();
  }
}

/**
 * Estado da agenda agora: 1 = horário de tela ligada, 0 = horário de tela desligada, -1 = sem
 * agenda ou sem hora da internet.
 */
int8_t agendaEstadoAtual() {
  if (!agendaAtiva || agendaLiga == agendaDesliga) return -1;
  time_t agora = time(nullptr);
  if (agora < 100000) return -1;         // ainda sem hora da internet
  struct tm* ti = localtime(&agora);
  int m = ti->tm_hour * 60 + ti->tm_min;
  bool ligada = (agendaLiga < agendaDesliga) ? (m >= agendaLiga && m < agendaDesliga)
                                             : (m >= agendaLiga || m < agendaDesliga);   // janela que cruza a meia-noite
  return ligada ? 1 : 0;
}

/**
 * Aplica a agenda: só age quando a janela muda (liga/apaga nos horários). Assim o botão manual
 * vale até o próximo horário programado.
 */
void verificaAgenda() {
  int8_t estado = agendaEstadoAtual();
  if (estado < 0) return;
  if (estado != agendaUltimoEstado) {
    agendaUltimoEstado = estado;
    if (estado == 1) displayLigado = true;              // horário de ligar: acende com o brilho de dia
    else if (!noiteDiminui) displayLigado = false;      // horário de apagar, modo 'apagar'
    // modo 'diminuir' à noite: mantém a tela como está; aplicaDisplay() só troca para o brilho noturno
    aplicaDisplay();
    telaAcordadaNoite = false;
    if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());   // ao ligar pela agenda, vale o tempo de tela ligada
    else telaContando = false;
  }
}
