/*
 * PLACA: LED, BOTÕES, SENSOR, BATERIA, ENERGIA, ECONOMIA E WIFI
 *
 * Tudo o que depende do hardware do ESP8266, menos a tela.
 * ESP32: reescrever boa parte (ADC da bateria, light sleep, motivo do reinício, WiFi).
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- LED DE STATUS ------------------------------------------------------------------------

/**
 * Acende ou apaga os LEDs azuis da placa (GPIO2 e, se o D0 não for o sensor de energia, também o
 * GPIO16; acesos em nível baixo).
 */
void ledEscreve(bool aceso) {
  digitalWrite(2, aceso ? LOW : HIGH);  // LED do módulo (GPIO2 = D4), aceso em LOW
  if (PIN_ENERGIA != 16) digitalWrite(16, aceso ? LOW : HIGH);   // 2º LED de algumas NodeMCU (GPIO16 = D0); não usado se o D0 for o sensor de energia
}

/**
 * Executada a cada 100 ms pelo Ticker: decide se o LED fica aceso conforme o padrão atual
 * (conectando, sem WiFi, enviando, aviso de reset ou pulso de sucesso/falha). Fica sempre apagado
 * se o LED de status estiver desligado em /config ou silenciado pela agenda.
 */
void ledTick() {
  bool aceso = false;
  if (ledStatusAtivo && !ledSilencio) {
    if (ledPulso) {
      // cada posição = 100 ms: ok = 300 ms aceso; falha = 3 piscadas curtas
      uint16_t bits = (ledPulso == 1) ? 0b0000000111 : 0b0000010101;
      aceso = (bits >> ledPulsoPos) & 1;
      if (++ledPulsoPos >= 10) { ledPulso = 0; ledPulsoPos = 0; }
    } else {
      if (ledEnviando || ledAviso || ledRede == LED_CONECTANDO) aceso = (ledPos % 2) == 0;   // pisca rápido (100/100 ms)
      else if (ledRede == LED_SEM_REDE) aceso = (ledPos % 20) < 5;                // 500 ms aceso a cada 2 s
      if (++ledPos >= 40) ledPos = 0;
    }
  }
  ledEscreve(aceso);
}

/** Configura os pinos do LED, apaga o LED e inicia o Ticker que faz os padrões de piscada. */
void ledInicia() {
  pinMode(2, OUTPUT);
  if (PIN_ENERGIA != 16) pinMode(16, OUTPUT);
  ledEscreve(false);
  ledTicker.attach_ms(100, ledTick);
}

/** Dispara uma acendida de 0,3 s (sinal de sucesso). */
void ledPulsoOk()    { ledPulsoPos = 0; ledPulso = 1; }

/** Dispara 3 piscadas curtas (sinal de falha). */
void ledPulsoFalha() { ledPulsoPos = 0; ledPulso = 2; }

// ---- BOTÕES FÍSICOS -----------------------------------------------------------------------

/**
 * Lê o botão i com debounce (filtra a trepidação). Devolve true quando o estado estável mudou
 * (apertou ou soltou).
 */
bool botaoAtualiza(uint8_t i, unsigned long ms) {
  bool lido = (digitalRead(btnPin[i]) == LOW);          // ligado ao GND: LOW = pressionado
  if (lido != btnLido[i]) {
    btnLido[i] = lido;
    btnMudou[i] = ms;
  }
  if (lido != btnEstado[i] && (ms - btnMudou[i]) >= BOTAO_DEBOUNCE_MS) {
    btnEstado[i] = lido;
    if (lido) btnDesde[i] = ms;
    return true;
  }
  return false;
}

/**
 * Ação do clique no botão das telas. Com a tela apagada, só acende (10 s à noite, ou o tempo de
 * tela ligada); com a tela acesa, passa para a próxima tela e renova o tempo.
 */
void proximaTela() {
  if (!displayLigado) {                                 // tela apagada: o clique só acende (com o brilho noturno, se for noite)
    bool noite = agendaEstadoAtual() == 0 && !noiteDiminui;   // horário agendado como desligado? (no modo 'diminuir' a agenda não apaga)
    displayLigado = true;
    aplicaDisplay();
    telaAcordadaNoite = noite;
    armaTimerTela(noite ? TELA_ACORDAR_NOITE_MS : telaDuracaoPadraoMs());   // à noite: poucos segundos
    return;
  }
  if (numTelasAtivas > 1) ui.nextFrame();               // com uma tela só, não há para onde passar
  // clique com a tela ligada: renova o tempo (10 s à noite; o tempo configurado de dia)
  if (telaAcordadaNoite) armaTimerTela(TELA_ACORDAR_NOITE_MS);
  else if (telaContando) armaTimerTela(telaDuracaoPadraoMs());
}

/**
 * Mostra no OLED o aviso com barra de progresso enquanto um botão é segurado (apagar WiFi ou reset
 * geral).
 */
void desenhaAvisoReset(int pct, bool geral) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  ligaTela();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  if (geral) {
    display.drawString(64, 0, F("RESET GERAL"));
    display.drawString(64, 12, F("Apaga WiFi e ajustes"));
    display.drawString(64, 24, F("Solte para cancelar"));
    display.drawProgressBar(2, 42, 124, 10, pct);
  } else {
    display.drawString(64, 2, F("Apagando o WiFi..."));
    display.drawString(64, 14, F("Solte para cancelar"));
    display.drawProgressBar(2, 32, 124, 10, pct);
  }
  display.display();
}

/** Apaga só a rede WiFi salva e reinicia: na volta, abre o portal (rede PORTAL_SSID). */
void resetWiFiPeloBotao() {
  ligaTela();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 14, F("WiFi apagado"));
  display.drawString(64, 28, F("Reiniciando..."));
  display.display();
  delay(1200);
  wifiManager.resetSettings();
  ESP.restart();
}

/** Reset geral: apaga os ajustes salvos e o WiFi e reinicia (padrão de fábrica). */
void resetGeralPeloBotao() {
  ligaTela();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 14, F("Reset geral"));
  display.drawString(64, 28, F("Apagando tudo..."));
  display.display();
  delay(1200);
  limpaConfig();
  wifiManager.resetSettings();
  ESP.restart();
}

/**
 * Clique no botão de reset geral: envia uma leitura ao painel agora (como o botão 'Enviar dados
 * agora' da /config) e mostra no OLED 'Enviando...' e depois o resultado com o código HTTP ou o
 * erro.
 */
void enviarPeloBotao() {
  msgTipo = MSG_ENVIO_ANDAMENTO;
  mostraMensagemTela("", "", "", 20000);   // acende a tela (se apagada) e segura o OLED durante o envio, que pode demorar
  envioNaTela = true;                      // enviaParaApiInterno() desenha as etapas (preparando, conectando)
  bool ok = enviaParaApi();
  envioNaTela = false;
  if (ok) nextApi = millis() + intervaloEnvioMs();                 // recomeça o ciclo do envio automático
  msgTipo = ok ? MSG_ENVIO_OK : MSG_ENVIO_FALHA;                   // tela de resultado com ícone, código HTTP e tempo
  mostraMensagemTela("", "", "", TELA_MENSAGEM_MS);
}

/**
 * Chamada a cada volta do loop(): lê os dois botões. Clique no botão das telas = próxima tela;
 * clique no botão de reset geral = envia os dados ao painel. Segurar executa os resets (com aviso
 * na tela). Devolve true enquanto um aviso de reset está na tela.
 */
bool trataBotoes(unsigned long ms) {
  bool aviso = false;
  for (uint8_t i = 0; i < 2; i++) {
    if (btnPin[i] < 0) continue;                        // botão não configurado
    bool mudou = botaoAtualiza(i, ms);

    // Botão 0 (telas): segurar apaga só o WiFi. Botão 1 (reset geral): segurar apaga WiFi e ajustes.
    unsigned long limite = (i == 0) ? BOTAO_RESET_MS : BOTAO_RESET_GERAL_MS;
    if (btnEstado[i]) {
      unsigned long seg = ms - btnDesde[i];
      if (seg >= limite) {                              // reinicia: não volta
        if (i == 0) resetWiFiPeloBotao();
        else resetGeralPeloBotao();
      }
      if (seg >= BOTAO_AVISO_MS) {
        desenhaAvisoReset((int)((seg - BOTAO_AVISO_MS) * 100UL / (limite - BOTAO_AVISO_MS)), i == 1);
        aviso = true;
      }
    }

    if (mudou && btnEstado[i]) ecoAcorda(ECO_ACORDADO_MS);   // apertou: fica acordado (economia na bateria)
    if (mudou && !btnEstado[i] && ms - btnDesde[i] < BOTAO_AVISO_MS) {   // soltou depois de um clique curto (segurar e soltar cancela)
      if (i == 0) proximaTela();                        // botão das telas: próxima tela
      else enviarPeloBotao();                           // botão de reset geral: envia os dados ao painel
    }
  }
  ledAviso = aviso;                                     // LED pisca rápido durante o aviso
  static bool avisoAnterior = false;
  if (avisoAnterior && !aviso && !displayLigado) aplicaDisplay();   // o aviso ligou a tela: apaga de novo ao cancelar
  avisoAnterior = aviso;
  return aviso;
}

// ---- SENSORES E HISTÓRICO -----------------------------------------------------------------

/**
 * Lista no Serial os endereços que responderam no I2C (diagnóstico da ligação do display;
 * esperado: 0x3C).
 */
void varreduraI2C() {
  Serial.println(F("Varredura I2C:"));
  int achados = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf_P(PSTR("  dispositivo em 0x%02X\n"), a);
      achados++;
    }
  }
  if (!achados) Serial.println(F("  nenhum dispositivo encontrado"));
}

/** Nome do pino na placa a partir do número do GPIO (ex.: 14 -> 'D5 (GPIO14)'), para as mensagens. */
String nomePino(int gpio) {
  switch (gpio) {
    case 16: return F("D0 (GPIO16)");
    case 5:  return F("D1 (GPIO5)");
    case 4:  return F("D2 (GPIO4)");
    case 0:  return F("D3 (GPIO0)");
    case 2:  return F("D4 (GPIO2)");
    case 14: return F("D5 (GPIO14)");
    case 12: return F("D6 (GPIO12)");
    case 13: return F("D7 (GPIO13)");
    case 15: return F("D8 (GPIO15)");
    default: return F("GPIO") + String(gpio);
  }
}

/**
 * Lê temperatura e umidade do DHT e ignora leituras inválidas (mantém o último valor bom).
 * Registra se a última tentativa deu certo (sensorUltimaOk). Chamada a cada SENSOR_INTERVAL_MS.
 */
void leSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(h) || isnan(t)) {
    Serial.println(F("Falhou a leitura do sensor " SENSOR_NOME "!"));
    sensorUltimaOk = false;             // o painel é avisado pelo campo sensor_ok
    return;                             // mantém o último valor válido
  }
  localTemp = t;
  localHum = h;
  sensorOk = true;
  sensorUltimaOk = true;
}

/**
 * Guarda a leitura atual (temperatura e umidade x10) no histórico circular da RAM. Chamada a cada
 * HIST_INTERVAL_MS.
 */
void guardaHistorico() {
  histTemp[histHead] = (int16_t)roundf(localTemp * 10);
  histHum[histHead] = (uint16_t)roundf(localHum * 10);
  histHead = (histHead + 1) % HIST_SIZE;
  if (histCount < HIST_SIZE) histCount++;
}

// ---- BATERIA ------------------------------------------------------------------------------

/** Lê o pino A0 (média de várias amostras) e converte para volts com BAT_FATOR e BAT_CALIB. */
float lerTensaoBateria() {
  long soma = 0;
  for (int k = 0; k < BAT_AMOSTRAS; k++) {
    soma += analogRead(PIN_BATERIA);
    delay(2);
  }
  float adc = soma / (float)BAT_AMOSTRAS;          // 0..1023 corresponde a 0..1 V no chip
  return (adc / 1023.0f) * BAT_FATOR * BAT_CALIB;
}

/**
 * Converte a tensão (V) em percentual por interpolação linear numa curva de Li-ion ajustada com
 * multímetro.
 */
int bateriaPercentual(float v) {
  // Curva ajustada comparando com o multímetro (tensão em V -> carga em %)
  static const float   V[] = {4.20, 4.15, 4.10, 4.015, 3.93, 3.84, 3.755, 3.68, 3.61, 3.48, 3.29, 3.00};
  static const uint8_t P[] = { 100,   95,   90,    80,   70,   60,    50,   40,   30,   20,   10,    0};
  const int N = sizeof(V) / sizeof(V[0]);
  if (v >= V[0]) return 100;
  if (v <= V[N - 1]) return 0;
  for (int k = 0; k < N - 1; k++) {
    if (v <= V[k] && v > V[k + 1]) {
      return P[k + 1] + (int)((v - V[k + 1]) * (P[k] - P[k + 1]) / (V[k] - V[k + 1]));
    }
  }
  return 0;
}

/**
 * Lê a bateria, aplica média móvel (90% do valor antigo, 10% do novo) e atualiza tensão e
 * percentual.
 */
void atualizaBateria() {
  float v = lerTensaoBateria();
  bateriaV = (bateriaV == 0) ? v : (bateriaV * 0.9f + v * 0.1f);   // média móvel: evita saltos
  bateriaPct = bateriaPercentual(bateriaV);
}

/** Texto descritivo da faixa de tensão da bateria (mostrado na seção Bateria e energia). */
const __FlashStringHelper* bateriaSituacao(float v) {
  if (v >= 4.15) return F("Totalmente carregada");
  if (v >= 4.06) return F("Carga alta");
  if (v >= 3.98) return F("Carga alta / boa");
  if (v >= 3.89) return F("Carga média-alta");
  if (v >= 3.80) return F("Carga média");
  if (v >= 3.72) return F("Meia carga");
  if (v >= 3.65) return F("Carga moderada");
  if (v >= 3.58) return F("Carga baixa");
  if (v >= 3.39) return F("Nível crítico (recomenda-se recarregar)");
  if (v >= 3.20) return F("Muito baixa");
  return F("Descarga profunda (risco de danificar a célula)");
}

// ---- ENERGIA EXTERNA (placa UPS) ----------------------------------------------------------

/**
 * Lê o pino do sensor de energia agora (sem filtro) e atualiza energiaExterna. Usada ao ligar e ao
 * ativar o sensor em /config.
 */
void energiaLeAgora() {
  if (PIN_ENERGIA < 0) return;
  bool presente = (digitalRead(PIN_ENERGIA) == (ENERGIA_ATIVA_EM_ALTO ? HIGH : LOW));
  energiaExterna = presente;
  energiaUltima = presente;
  energiaContagem = 3;                  // já considera estável
}

/**
 * Chamada a cada volta do loop(): lê o pino a cada ENERGIA_INTERVAL_MS e só muda o estado depois
 * de 3 leituras iguais seguidas (filtro de ruído). energiaExterna = true quando há 5 V na entrada
 * da UPS.
 */
void atualizaEnergia(unsigned long ms) {
  if (!energiaAtiva || PIN_ENERGIA < 0) return;
  if ((long)(ms - nextEnergia) < 0) return;
  nextEnergia = ms + ENERGIA_INTERVAL_MS;
  bool presente = (digitalRead(PIN_ENERGIA) == (ENERGIA_ATIVA_EM_ALTO ? HIGH : LOW));
  if (presente == energiaUltima) {
    if (energiaContagem < 255) energiaContagem++;
  } else {
    energiaUltima = presente;
    energiaContagem = 0;
  }
  if (energiaContagem >= 3 && presente != energiaExterna) {
    energiaExterna = presente;
    Serial.println(presente ? F("Energia externa conectada (5 V)") : F("Sem energia externa: rodando na bateria"));
  }
}

// ---- ECONOMIA NA BATERIA (light sleep do WiFi) --------------------------------------------

/** true = a placa UPS está ativa e não há energia externa (o ESP está rodando na bateria). */
bool naBateria() {
  return energiaAtiva && PIN_ENERGIA >= 0 && !energiaExterna;
}

/** Intervalos aceitos para o envio ao painel na bateria (minutos). */
bool ecoIntervaloValido(int m) {
  return m == 15 || m == 20 || m == 30 || m == 45 || m == 60;
}

/** Intervalo do envio automático ao painel: o normal ou, na bateria com a economia ligada, o da economia. */
unsigned long intervaloEnvioMs() {
  return (ecoBateria && naBateria()) ? (unsigned long)ecoIntervaloMin * 60000UL : PAINEL_INTERVAL_MS;
}

/**
 * Mantém o ESP acordado (WiFi sem light sleep) por 'dur' ms a partir de agora. Chamada a cada
 * acesso à página, clique num botão, envio ao painel, consulta do clima e OTA. Se já estava
 * dormindo, acorda na hora.
 */
void ecoAcorda(unsigned long dur) {
  unsigned long ate = millis() + dur;
  if ((long)(ate - ecoAcordadoAte) > 0) ecoAcordadoAte = ate;
  if (ecoDormindo) {
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
    ecoDormindo = false;
    Serial.println(F("Economia: acordado"));
  }
}

/**
 * Chamada a cada volta do loop(): liga o light sleep quando está na bateria, com a economia ligada,
 * com WiFi conectado e sem nenhum acesso recente; volta ao modo normal (modem sleep) caso contrário.
 */
void atualizaEco(unsigned long ms) {
  bool dormir = ecoBateria && naBateria() && WiFi.status() == WL_CONNECTED && (long)(ms - ecoAcordadoAte) >= 0;
  if (dormir == ecoDormindo) return;
  WiFi.setSleepMode(dormir ? WIFI_LIGHT_SLEEP : WIFI_MODEM_SLEEP);
  ecoDormindo = dormir;
  Serial.println(dormir ? F("Economia: light sleep (na bateria)") : F("Economia: modo normal"));
}

// ---- WIFI (portal do WiFiManager) ---------------------------------------------------------

/**
 * Inicia o OTA (uma vez só). Precisa de WiFi conectado: é chamada no boot, se conectou, ou pelo
 * loop() quando o WiFi conecta depois de um boot sem rede.
 */
void iniciaOta() {
  if (otaIniciado || WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.begin();
  otaIniciado = true;
  Serial.println(F("OTA pronto"));
}

/** Chamada pelo WiFiManager ao abrir o portal de configuração: mostra no OLED como se conectar. */
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println(F("Portal de configuração do WiFi aberto"));
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  ligaTela();                             // mostra as instruções mesmo com a tela apagada

  // Rede, senha (PORTAL_SENHA) e endereço do portal. A senha aparece porque o portal só abre
  // sem WiFi, e quem lê o OLED está ao lado da estação.
  String senha = F("Senha: ");
  senha += PORTAL_SENHA;
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 0, F("CONFIGURAR O WIFI"));
  display.drawString(64, 12, F("Conecte-se à rede:"));
  display.drawString(64, 24, myWiFiManager->getConfigPortalSSID());
  if (display.getStringWidth(senha) <= 128) display.drawString(64, 36, senha);
  else display.drawString(64, 36, String(PORTAL_SENHA));   // senha longa: sem o "Senha:" na frente
  display.drawString(64, 50, F("e abra ") + WiFi.softAPIP().toString());
  display.display();
}

/** Chamada pelo WiFiManager depois de salvar a rede WiFi: mostra a confirmação no OLED. */
void saveConfigCallback() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 10, F("Configuração salva"));
  display.display();
}
