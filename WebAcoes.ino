/*
 * SERVIDOR WEB: BUSCA DE CIDADE (GET) E AÇÕES (POST)
 *
 * Rotas 'handle...' que mudam ajustes, reiniciam ou abrem o portal.
 * ESP32: quase igual.
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- SERVIDOR WEB: BUSCA DE CIDADE (GET) ------------------------------------------------

/**
 * GET /buscar-cidade?q=Campinas,BR: busca a cidade pelo nome na API de geocodificação do
 * OpenWeatherMap (gratuita, mesma chave) e devolve até 5 resultados com as coordenadas, em JSON,
 * para a seção Cidade e clima preencher a latitude e a longitude. Precisa estar logado.
 */
void handleBuscaCidade() {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  if (!logado()) { server.send(401, "application/json", PSTR("{\"ok\":false,\"erro\":\"Faça o login de novo (recarregue a página).\"}")); return; }
  String q = server.arg(F("q"));
  q.trim();
  if (q.length() < 2 || q.length() > 60) { server.send(200, "application/json", PSTR("{\"ok\":false,\"erro\":\"Digite de 2 a 60 caracteres.\"}")); return; }
  if (OPEN_WEATHER_MAP_APP_ID.length() == 0) { server.send(200, "application/json", PSTR("{\"ok\":false,\"erro\":\"Salve primeiro a chave da API (campo acima).\"}")); return; }
  if (WiFi.status() != WL_CONNECTED) { server.send(200, "application/json", PSTR("{\"ok\":false,\"erro\":\"Sem WiFi.\"}")); return; }

  WiFiClient cliente;
  HTTPClient http;
  http.setTimeout(8000);
  http.useHTTP10(true);                 // resposta sem 'chunked': o leitor recebe o JSON puro
  String url = F("http://api.openweathermap.org/geo/1.0/direct?q=") + urlEncode(q) + F("&limit=") + String(BuscaCidade::MAX) +
               F("&appid=") + OPEN_WEATHER_MAP_APP_ID;
  if (!http.begin(cliente, url)) { server.send(200, "application/json", PSTR("{\"ok\":false,\"erro\":\"Falha ao preparar a busca.\"}")); return; }
  int codigo = http.GET();

  BuscaCidade busca;
  if (codigo > 0) {
    JsonStreamingParser parser;
    parser.setListener(&busca);
    WiFiClient* s = http.getStreamPtr();
    unsigned long t0 = millis();
    while ((http.connected() || s->available()) && millis() - t0 < 8000UL) {
      while (s->available()) parser.parse((char)s->read());
      delay(1);                         // deixa o WiFi trabalhar enquanto chegam os dados
    }
  }
  http.end();

  String j;
  if (codigo != 200) {
    String e = (codigo == 401) ? String(F("Chave da API inválida ou ainda não ativada."))
             : (codigo > 0) ? F("HTTP ") + String(codigo) + (busca.erro.length() ? F(": ") + busca.erro : String(""))
             : HTTPClient::errorToString(codigo);
    j = F("{\"ok\":false,\"erro\":\"") + jsonEscape(e) + F("\"}");
  } else {
    j = F("{\"ok\":true,\"cidades\":[");
    for (uint8_t k = 0; k < busca.n; k++) {
      BuscaCidade::Resultado& c = busca.r[k];
      if (k) j += F(",");
      j += F("{\"nome\":\"") + jsonEscape(c.nomePt.length() ? c.nomePt : c.nome) + F("\",\"estado\":\"") + jsonEscape(c.estado) +
           F("\",\"pais\":\"") + jsonEscape(c.pais) + F("\",\"lat\":") + String(c.lat, 4) + F(",\"lon\":") + String(c.lon, 4) + F("}");
    }
    j += F("]}");
  }
  server.send(200, "application/json", j);
}

// ---- SERVIDOR WEB: AÇÕES (POST) -----------------------------------------------------------

/** POST /salvar: valida e salva chave, cidade (coordenadas e/ou ID), idioma e fuso do OpenWeatherMap. */
void handleSalvar() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  String key = server.arg(F("apiKey"));
  key.trim();
  String lang = server.arg(F("lang"));
  String locRaw = server.arg(F("locId"));

  // ID da cidade: usa a última sequência de dígitos (aceita colar o endereço inteiro)
  String loc = "";
  int e = (int)locRaw.length() - 1;
  while (e >= 0 && !isDigit(locRaw[e])) e--;
  int b = e;
  while (b >= 0 && isDigit(locRaw[b])) b--;
  if (e >= 0) loc = locRaw.substring(b + 1, e + 1);

  if (key.length() > 0) {
    bool okKey = key.length() >= 20 && key.length() <= 40;
    for (size_t k = 0; k < key.length(); k++) {
      if (!isAlphaNumeric(key[k])) okKey = false;
    }
    if (!okKey) { redirecionaConfig("chave"); return; }
  }
  if (loc.length() > 10) { redirecionaConfig("local"); return; }

  // Coordenadas: opcionais. Aceita colar 'lat, lon' (como o Google Maps copia) no campo da latitude.
  String latRaw = server.arg(F("lat"));
  String lonRaw = server.arg(F("lon"));
  latRaw.trim();
  lonRaw.trim();
  int virgula = latRaw.indexOf(',');
  if (virgula >= 0 && lonRaw.length() == 0) {
    lonRaw = latRaw.substring(virgula + 1);
    latRaw = latRaw.substring(0, virgula);
    latRaw.trim();
    lonRaw.trim();
  }
  float lat = 0, lon = 0;
  int8_t okLat = parseCoord(latRaw, lat, -90, 90);       // 0 = vazio, 1 = ok, -1 = inválido
  int8_t okLon = parseCoord(lonRaw, lon, -180, 180);
  if (okLat < 0 || okLon < 0 || (okLat == 0) != (okLon == 0)) { redirecionaConfig("coord"); return; }
  bool temCoord = (okLat == 1 && okLon == 1);
  if (!temCoord && loc.length() == 0) { redirecionaConfig("local_vazio"); return; }   // precisa de um dos dois

  bool okLang = false;
  for (uint8_t k = 0; k < LANG_COUNT; k++) {
    if (lang == LANG_CODES[k]) okLang = true;
  }
  if (!okLang) { redirecionaConfig("idioma"); return; }

  // Fuso horário (se o formulário não trouxer o campo, mantém o atual)
  bool trocaFuso = server.hasArg(F("fuso"));
  int fuso = fusoMin;
  if (trocaFuso) {
    fuso = server.arg(F("fuso")).toInt();
    bool okFuso = false;
    for (uint8_t k = 0; k < FUSO_COUNT; k++) {
      if (FUSOS[k].min == fuso) okFuso = true;
    }
    if (!okFuso) { redirecionaConfig("fuso"); return; }
  }

  if (key.length() > 0) OPEN_WEATHER_MAP_APP_ID = key;   // em branco = mantém a atual
  // Regra: coordenadas preenchidas valem (com ou sem ID); só o ID = o ESP descobre as coordenadas na 1ª consulta
  bool mudouCoord = temCoord ? (!coordOk || fabsf(lat - cidadeLat) > 0.00005f || fabsf(lon - cidadeLon) > 0.00005f)
                             : coordOk;  // apagou as coordenadas: volta a descobrir pelo ID
  if (loc != OPEN_WEATHER_MAP_LOCATION_ID || mudouCoord) {   // outro lugar: os dados antigos não valem mais
    climaRecebido = false;
    prox24Ok = false;
    astroOk = false;
    nextAstro = 0;
  }
  if (temCoord) {
    coordOk = true;
    cidadeLat = lat;
    cidadeLon = lon;
  } else if (loc != OPEN_WEATHER_MAP_LOCATION_ID || mudouCoord) {
    coordOk = false;                     // a próxima consulta é pelo ID e traz as coordenadas novas
  }
  OPEN_WEATHER_MAP_LOCATION_ID = loc;
  OPEN_WEATHER_MAP_LANGUAGE = lang;
  if (trocaFuso) {
    fusoMin = fuso;
    horarioVerao = server.hasArg(F("verao"));
    aplicaFuso();                        // reinicia o NTP com o novo fuso
    agendaUltimoEstado = -1;             // a agenda da tela reavalia com a nova hora
  }
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  salvaConfig();

  nextWeatherUpdate = millis();          // o loop() atualiza o clima em seguida
  redirecionaConfig("ok");
}

/**
 * POST /api: salva ID do dispositivo, URL da API e DEVICE_KEY. URL em branco desliga o envio;
 * chave em branco mantém a atual; ID em branco volta ao padrão.
 */
void handleApi() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  String url = server.arg(F("apiUrl"));
  url.trim();
  String key = server.arg(F("deviceKey"));
  key.trim();
  String id = server.arg(F("deviceId"));
  id.trim();

  if (url.length() > 0) {
    bool ok = (url.startsWith("http://") || url.startsWith("https://")) && url.length() <= 120;
    for (size_t k = 0; k < url.length(); k++) {
      if (url[k] < 33 || url[k] > 126) ok = false;    // sem espaços nem caracteres de controle
    }
    if (!ok) { redirecionaConfig("apiurl"); return; }
  }
  if (key.length() > 0) {
    bool ok = key.length() >= 8 && key.length() <= 64;
    for (size_t k = 0; k < key.length(); k++) {
      char c = key[k];
      if (c < 33 || c > 126 || c == '"' || c == '\\') ok = false;
    }
    if (!ok) { redirecionaConfig("apikey"); return; }
  }

  if (id.length() > 0) {
    bool ok = id.length() <= 32;
    for (size_t k = 0; k < id.length(); k++) {
      char c = id[k];
      if (!(isAlphaNumeric(c) || c == '-' || c == '_' || c == '.')) ok = false;
    }
    if (!ok) { redirecionaConfig("apiid"); return; }
  }

  apiUrl = url;
  apiDeviceId = id;
  if (key.length() > 0) deviceKey = key;
  salvaConfig();
  nextApi = millis() + 3000UL;           // primeiro envio logo depois de salvar
  redirecionaConfig("api");
}

/** POST /enviar: botão 'Enviar dados agora', sem esperar o próximo envio automático. */
void handleEnviar() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  bool ok = enviaParaApi();
  if (ok) nextApi = millis() + intervaloEnvioMs();       // recomeça o ciclo do envio automático
  redirecionaConfig(ok ? "envio_ok" : "envio_erro");
}

/**
 * POST /tempoligada: salva o tempo de tela ligada (0 = sempre ligada; ou 1, 5, 10, 15, 30, 60
 * min).
 */
void handleTempoLigada() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  int mn = server.arg(F("min")).toInt();
  bool valido = server.hasArg(F("min")) && (mn == 0 || mn == 1 || mn == 5 || mn == 10 || mn == 15 || mn == 30 || mn == 60);
  if (!valido) { redirecionaConfig("tempoligada_erro"); return; }
  telaTimeoutMin = mn;
  if (displayLigado && !telaAcordadaNoite) armaTimerTela(telaDuracaoPadraoMs());   // passa a contar a partir de agora
  salvaConfig();
  redirecionaConfig("tempoligada");
}

/** POST /tempotela: salva o tempo de cada tela do OLED (5 a 30 s, de 5 em 5). */
void handleTempoTela() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  int seg = server.arg(F("seg")).toInt();
  if (seg < 5 || seg > 30 || seg % 5 != 0) { redirecionaConfig("tempo_erro"); return; }
  telaSeg = seg;
  ui.setTimePerFrame((uint16_t)(telaSeg * 1000));
  salvaConfig();
  redirecionaConfig("tempo");
}

/**
 * POST /led: liga ou desliga o LED azul de status e define se ele segue o horário automático da
 * tela (apagado no horário de tela desligada).
 */
void handleLed() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  ledStatusAtivo = server.hasArg(F("ativo"));
  ledSegueAgenda = server.hasArg(F("agenda"));
  ledSilencio = ledSegueAgenda && agendaEstadoAtual() == 0;     // vale já, sem esperar o próximo ciclo do loop
  salvaConfig();
  redirecionaConfig("led");
}

/**
 * POST /bateria: liga ou desliga, neste ESP, a leitura da bateria (A0) e o sensor de energia
 * externa da placa UPS.
 */
void handleBateria() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  bateriaAtiva = server.hasArg(F("ativa"));
  if (bateriaAtiva) {
    bateriaV = 0;                        // recomeça a média
    atualizaBateria();
    nextBateria = millis() + 30000UL;
  }
  energiaAtiva = server.hasArg(F("energia"));
  if (energiaAtiva) energiaLeAgora();    // já mostra o estado atual da energia
  salvaConfig();
  redirecionaConfig("bat");
}

/**
 * POST /economia: liga ou desliga a economia na bateria (light sleep do WiFi sem energia externa) e
 * escolhe o intervalo do envio ao painel na bateria. Vale na hora.
 */
void handleEconomia() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  ecoBateria = server.hasArg(F("eco"));
  int m = server.arg(F("int")).toInt();
  if (ecoIntervaloValido(m)) ecoIntervaloMin = m;
  salvaConfig();
  redirecionaConfig("eco");
}

/**
 * POST /brilho: salva o brilho de dia, o brilho noturno e o que fazer à noite (apagar a tela ou só
 * diminuir o brilho). Vale na hora.
 */
void handleBrilho() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  int dia = server.arg(F("dia")).toInt();
  int noite = server.arg(F("noite")).toInt();
  if (!brilhoValido(dia) || !brilhoValido(noite)) { redirecionaConfig("brilho_erro"); return; }
  brilhoPct = dia;
  brilhoNoitePct = noite;
  noiteDiminui = (server.arg(F("modo")) == "1");
  noitePeloSol = (server.arg(F("fonte")) == "1");
  salvaConfig();
  if (agendaEstadoAtual() == 0) {       // no horário de tela desligada, reavalia a agenda já: apaga ou mantém conforme o novo modo
    agendaUltimoEstado = -1;
    verificaAgenda();
  }
  if (displayLigado) aplicaDisplay();   // aplica o novo brilho agora (se a tela estiver acesa)
  redirecionaConfig("brilho");
}

/** POST /hora: salva o fuso horário e o horário de verão (seção 'Data e hora'). Vale na hora. */
void handleHora() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  if (!server.hasArg(F("fuso"))) { redirecionaConfig("fuso"); return; }
  int fuso = server.arg(F("fuso")).toInt();
  bool okFuso = false;
  for (uint8_t k = 0; k < FUSO_COUNT; k++) {
    if (FUSOS[k].min == fuso) okFuso = true;
  }
  if (!okFuso) { redirecionaConfig("fuso"); return; }
  fusoMin = fuso;
  horarioVerao = server.hasArg(F("verao"));
  aplicaFuso();                         // reinicia o NTP com o novo fuso
  agendaUltimoEstado = -1;              // a agenda da tela reavalia com a nova hora
  salvaConfig();
  redirecionaConfig("fuso_ok");
}

/** POST /telas: salva quais telas aparecem no OLED (caixas t0 a t6). Pelo menos uma. Vale na hora. */
void handleTelas() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  uint8_t mask = 0;
  for (uint8_t k = 0; k < TELAS_TOTAL; k++) {
    if (server.hasArg(F("t") + String(k))) mask |= (1 << k);
  }
  if (mask == 0) { redirecionaConfig("telas_erro"); return; }
  telasMask = mask;
  aplicaTelas();                        // volta para a 1ª tela marcada
  salvaConfig();
  redirecionaConfig("telas");
}

/** POST /agenda: salva o horário automático (ligar e apagar a tela). */
void handleAgenda() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  bool ativa = server.hasArg(F("ativa"));
  int liga = parseHora(server.arg(F("liga")));
  int desliga = parseHora(server.arg(F("desliga")));
  if (liga < 0 || desliga < 0) { redirecionaConfig("hora"); return; }
  if (ativa && liga == desliga) { redirecionaConfig("horaigual"); return; }

  agendaAtiva = ativa;
  agendaLiga = liga;
  agendaDesliga = desliga;
  agendaUltimoEstado = -1;               // reavalia já: a tela passa a seguir a agenda agora
  salvaConfig();
  nextAgenda = millis();
  redirecionaConfig("agenda");
}

/** POST /display: liga/apaga a tela (estado = 1, 0 ou t para alternar). */
void handleDisplay() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  String v = server.arg(F("estado"));
  if (v.equals(F("1"))) displayLigado = true;
  else if (v.equals(F("0"))) displayLigado = false;
  else displayLigado = !displayLigado;
  displayLigadoSalvo = displayLigado;   // escolha manual: é ela que vale no próximo boot
  logEvento(displayLigado ? F("Tela ligada pela página") : F("Tela apagada pela página"));
  aplicaDisplay();
  telaAcordadaNoite = false;
  if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());
  else telaContando = false;
  salvaConfig();
  if (server.hasArg(F("volta"))) {
    redirecionaConfig("disp");
  } else {
    server.send(200, "application/json", displayLigado ? "{\"display\":true}" : "{\"display\":false}");
  }
}

/** POST /reset-wifi: apaga a rede WiFi salva e reinicia. */
void handleResetWiFi() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  server.send(200, "text/html", PSTR("<meta charset=\"utf-8\"><p>Dados de WiFi apagados. Reiniciando...</p>"));
  delay(500);
  wifiManager.resetSettings();
  ESP.restart();
}

/**
 * POST /reiniciar: só reinicia o ESP (WiFi e ajustes continuam salvos). A resposta recarrega a
 * seção Sistema depois de 15 s, quando o ESP já voltou à rede.
 */
void handleReiniciar() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  server.send(200, "text/html", PSTR("<meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"15;url=/config?s=sistema\">"
                                     "<body style=\"font-family:system-ui,sans-serif;background:#07101e;color:#fff;padding:24px\">"
                                     "<p>Reiniciando o ESP... A página volta sozinha em uns 15 s.</p></body>"));
  delay(500);
  ESP.restart();
}

/**
 * POST /reset-geral: apaga o WiFi e todos os ajustes salvos e reinicia (igual ao botão físico de
 * reset geral).
 */
void handleResetGeral() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  server.send(200, "text/html", PSTR("<meta charset=\"utf-8\"><p>Reset geral feito. Reiniciando... Conecte-se à rede " PORTAL_SSID " para configurar de novo.</p>"));
  delay(500);
  limpaConfig();
  wifiManager.resetSettings();
  ESP.restart();
}

/**
 * POST /portal: abre o portal de configuração do WiFiManager (o servidor web para enquanto o
 * portal está aberto).
 */
void handlePortal() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  server.send(200, "text/html", PSTR("<meta charset=\"utf-8\"><p>Conecte-se à rede " PORTAL_SSID " para configurar. A estação reinicia ao terminar.</p>"));
  delay(500);
  server.stop();                        // o portal usa a porta 80
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.startConfigPortal(PORTAL_SSID, PORTAL_SENHA);
  ESP.restart();
}
