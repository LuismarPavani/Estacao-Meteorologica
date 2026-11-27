/*
 * SERVIDOR WEB: PÁGINA /config (SEÇÕES E TABELAS)
 *
 * Menu, peças comuns e uma função 'cfg...' por seção da /config.
 * ESP32: quase igual.
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- SERVIDOR WEB: PÁGINA /config (SEÇÕES E TABELAS) ----------------------------------------

/** SVG do ícone de sinal de WiFi (4 barras) usado na seção WiFi. */
String iconeSinal(int pct) {
  int n = 0;
  if (pct >= 75) n = 4;
  else if (pct >= 50) n = 3;
  else if (pct >= 25) n = 2;
  else if (pct > 0) n = 1;

  String s = F("<svg width=\"34\" height=\"22\" viewBox=\"0 0 34 22\" style=\"vertical-align:middle\">");
  for (int k = 0; k < 4; k++) {
    int h = 6 + k * 5;                  // alturas 6, 11, 16, 21
    s += F("<rect x=\"") + String(k * 9) + F("\" y=\"") + String(22 - h) + F("\" width=\"7\" height=\"") + String(h) +
         F("\" rx=\"1.5\" fill=\"") + (k < n ? F("#81d4fa") : F("#ffffff40")) + F("\"/>");
  }
  s += F("</svg>");
  return s;
}

/** SVG do ícone de bateria estilo celular; a cor muda conforme a carga. */
String iconeBateria(int pct) {
  const char* cor = pct <= BAT_CRITICA_PCT ? "#ef5350" : (pct <= BAT_AVISO_PCT ? "#ffb74d" : "#66bb6a");
  int w = (22 * pct) / 100;
  String s = F("<svg width=\"34\" height=\"16\" viewBox=\"0 0 34 16\" style=\"vertical-align:middle\">"
             "<rect x=\"1\" y=\"1\" width=\"28\" height=\"14\" rx=\"3.5\" fill=\"none\" stroke=\"#ffffffcc\" stroke-width=\"2\"/>"
             "<rect x=\"4\" y=\"4\" width=\"") + String(w) + F("\" height=\"8\" rx=\"1.5\" fill=\"") + cor + F("\"/>"
             "<rect x=\"30\" y=\"5\" width=\"3\" height=\"6\" rx=\"1.2\" fill=\"#ffffffcc\"/></svg>");
  return s;
}

/** Cartão da seção Bateria e energia: carga, tensão, estado e situação. */
String tabelaBateria() {
  String t = F("<div class=\"grp\">");
  if (energiaAtiva && PIN_ENERGIA >= 0) t += linhaInfo("Alimentação", energiaExterna ? F("🔌 Energia externa (5 V conectada)") : F("🔋 Bateria (sem energia externa)"));
  if (!bateriaAtiva) {
    t += linhaInfo("Estado", F("Desativada (ative acima)"));
  } else {
    const char* estado = bateriaPct <= BAT_CRITICA_PCT ? "⚠️ Crítica" : (bateriaPct <= BAT_AVISO_PCT ? "⚠️ Baixa" : "✅ Normal");
    t += linhaInfo("Carga", iconeBateria(bateriaPct) + F(" ") + String(bateriaPct) + F("%"));
    t += linhaInfo("Tensão", String(bateriaV, 2) + F(" V"));
    t += linhaInfo("Estado", estado);
    t += linhaInfo("Situação", bateriaSituacao(bateriaV));
  }
  t += F("</div><div class=\"note\">Estimativa pela tensão lida no A0. Ajuste BAT_CALIB no código comparando com um multímetro.</div>");
  return t;
}

/** Tabela HTML com os dados da conexão WiFi (SSID, sinal, IP, canal, MAC...). */
String tabelaWiFi() {
  String t;
  t.reserve(1600);
  t = F("<div class=\"grp\">");

  bool conectado = (WiFi.status() == WL_CONNECTED);
  t += linhaInfo("Estado", conectado ? F("✅ Conectado") : F("⚠️ Desconectado"));

  if (conectado) {
    int rssi = WiFi.RSSI();
    int pct = qualidadeSinal(rssi);
    const char* nivel = pct >= 75 ? "Excelente" : (pct >= 50 ? "Bom" : (pct >= 25 ? "Regular" : "Fraco"));

    const char* phy = "?";
    switch (WiFi.getPhyMode()) {
      case WIFI_PHY_MODE_11B: phy = "802.11b"; break;
      case WIFI_PHY_MODE_11G: phy = "802.11g"; break;
      case WIFI_PHY_MODE_11N: phy = "802.11n"; break;
      default: break;
    }

    t += linhaInfo("Rede (SSID)", htmlEsc(WiFi.SSID()));
    t += linhaInfo("Sinal", iconeSinal(pct) + F(" ") + String(rssi) + F(" dBm · ") + String(pct) + F("% · ") + nivel);
    t += linhaInfo("BSSID (roteador)", WiFi.BSSIDstr());
    t += linhaInfo("Canal", String(WiFi.channel()));
    t += linhaInfo("Padrão WiFi", phy);
    t += linhaInfo("IP", WiFi.localIP().toString());
    t += linhaInfo("Máscara", WiFi.subnetMask().toString());
    t += linhaInfo("Gateway", WiFi.gatewayIP().toString());
    t += linhaInfo("DNS", WiFi.dnsIP().toString());
  }

  String hn = String(WiFi.hostname());
  t += linhaInfo("Hostname", htmlEsc(hn));
  t += linhaInfo("MAC (estação)", WiFi.macAddress());
  t += linhaInfo("MAC (AP)", WiFi.softAPmacAddress());
  t += linhaInfo("Reconexão automática", WiFi.getAutoReconnect() ? F("Sim") : F("Não"));

  const char* sono = "?";
  switch (WiFi.getSleepMode()) {
    case WIFI_NONE_SLEEP: sono = "Desligado"; break;
    case WIFI_LIGHT_SLEEP: sono = "Light sleep"; break;
    case WIFI_MODEM_SLEEP: sono = "Modem sleep"; break;
    default: break;
  }
  t += linhaInfo("Economia de energia", sono);

  t += F("</div>");
  return t;
}

/** Uma barra de progresso da seção Sistema: pct = preenchimento (0-100), nivel 'o' ok, 'a' atenção, 'r' ruim. */
String barraPct(uint8_t pct, char nivel, const String& texto) {
  if (pct > 100) pct = 100;
  String s = F("<span class=\"pb ");
  s += (nivel == 'r') ? F("ru") : (nivel == 'a') ? F("at") : F("ok");
  s += F("\" style=\"--p:");
  s += String(pct);
  s += F("%\"><span class=\"t\">");
  s += texto;
  s += F("</span><span class=\"f\">");
  s += texto;
  s += F("</span></span>");
  return s;
}

/** Linha com rótulo, texto pequeno embaixo dele e, à direita, um valor (texto com class 'val' ou uma barra). */
String linhaSub(const __FlashStringHelper* rotulo, const String& sub, const String& valor) {
  String s = F("<div class=\"it\"><span class=\"tx\">");
  s += rotulo;
  if (sub.length()) { s += F("<small>"); s += sub; s += F("</small>"); }
  s += F("</span>");
  if (valor.startsWith(F("<span class=\"pb"))) {
    s += valor;                         // barra de progresso
  } else {
    s += F("<span class=\"val\">");
    s += valor;
    s += F("</span>");
  }
  s += F("</div>");
  return s;
}

/** Bytes em KB com uma casa ('25.9 KB'), como nas outras linhas da página. */
String kb1(uint32_t bytes) {
  return String(bytes / 1024.0f, 1) + F(" KB");
}

/**
 * Linhas da seção Sistema que mudam com o tempo, numeradas (id "d0" a "d6"): 0 tempo ligado, 1 data e
 * hora, 2 sensor, 3 programa, 4 memória livre, 5 maior bloco, 6 fragmentação. As mesmas funções montam
 * a página e o /sistema.json (a página troca cada linha pelo id, a cada 5 s).
 */
String linhaViva(uint8_t k) {
  String s;
  if (k == 0) {
    s = linhaInfo("Tempo ligado", formataUptime(millis() / 1000UL));
  } else if (k == 1) {
    time_t agora = time(nullptr);
    if (agora > 100000) {
      struct tm* ti = localtime(&agora);
      char buf[32];   // com os casts o compilador sabe o tamanho máximo (evita -Wformat-truncation)
      snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d", (uint8_t)ti->tm_mday, (uint8_t)(ti->tm_mon + 1), (uint16_t)(ti->tm_year + 1900),
               (uint8_t)ti->tm_hour, (uint8_t)ti->tm_min, (uint8_t)ti->tm_sec);
      s = linhaInfo("Data e hora", String(F("✅ ")) + buf);
    } else {
      s = linhaInfo("Data e hora", F("⚠️ sem sincronização"));
    }
  } else if (k == 2) {
    s = linhaInfo("Sensor " SENSOR_NOME, sensorUltimaOk ? String(F("✅ OK")) : F("⚠️ sem leitura válida (confira o dado no pino ") + nomePino(DHTPIN) + F(")"));
  } else if (k == 3) {
    uint32_t prog = ESP.getSketchSize(), progLivre = ESP.getFreeSketchSpace(), progTot = prog + progLivre;
    uint8_t p = progTot ? (uint8_t)((uint64_t)prog * 100 / progTot) : 0;
    s = linhaSub(F("Programa"), fmtBytes(prog) + F(" de ") + fmtBytes(progTot) + F(" · ") + fmtBytes(progLivre) + F(" livres"),
                 barraPct(p, p <= 70 ? 'o' : (p <= 85 ? 'a' : 'r'), String(p) + F("% usado")));
  } else if (k == 4) {
    uint32_t livre = ESP.getFreeHeap();
    uint32_t ref = heapBoot > livre ? heapBoot : livre;                 // nunca mais de 100% livre
    uint8_t p = ref ? (uint8_t)((uint64_t)livre * 100 / ref) : 0;
    s = linhaSub(F("Memória livre (heap)"), kb1(livre) + F(" livres de ~") + kb1(ref),
                 barraPct(p, p >= 35 ? 'o' : (p >= 25 ? 'a' : 'r'), String(p) + F("% livre")));
  } else if (k == 5) {
    uint32_t bloco = ESP.getMaxFreeBlockSize();
    uint32_t x10 = (uint32_t)((uint64_t)bloco * 10 / HTTPS_MIN_BLOCO);  // bloco ÷ o que o HTTPS precisa, x10
    uint8_t p = x10 >= 20 ? 100 : (uint8_t)(x10 * 5);                   // cheia a partir de 2×; 1× = metade
    String xs = String(x10 / 10) + ',' + String(x10 % 10) + F("×");
    s = linhaSub(F("Maior bloco livre"), kb1(bloco) + F(" · o HTTPS precisa de ") + kb1(HTTPS_MIN_BLOCO),
                 barraPct(p, x10 >= 20 ? 'o' : (x10 >= 10 ? 'a' : 'r'), xs + (x10 < 10 ? F(": HTTPS adiado") : F(" o necessário"))));
  } else {
    uint8_t frag = ESP.getHeapFragmentation();
    s = linhaSub(F("Fragmentação do heap"), F("até 25% ok · até 50% atenção · acima, ruim"),
                 barraPct(frag, frag <= 25 ? 'o' : (frag <= 50 ? 'a' : 'r'), String(frag) + F("%")));
  }
  String id = F("<div class=\"it\" id=\"d");
  id += String(k);
  id += '"';
  s.replace(String(F("<div class=\"it\"")), id);   // cada linha tem um só <div class="it">
  return s;
}

/**
 * GET /sistema.json (com login): as linhas que mudam da seção Sistema, já em HTML, e a hora da
 * resposta. ~1,5 KB, enviado em partes. A memória é lida antes de montar o resto (mede o ESP, não a resposta).
 */
void paginaSistemaJson() {
  if (!exigeLogin()) return;
  static const uint8_t ORDEM[] = {4, 5, 6, 3, 0, 1, 2};   // memória primeiro
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String j = F("{\"l\":{");
  for (uint8_t n = 0; n < sizeof(ORDEM); n++) {
    if (n) j += ',';
    j += F("\"d");
    j += String(ORDEM[n]);
    j += F("\":\"");
    j += jsonEscape(linhaViva(ORDEM[n]));
    j += '"';
    server.sendContent(j);
    j = "";
  }
  time_t agora = time(nullptr);
  char b[16] = "";
  if (agora > 100000) {
    struct tm* ti = localtime(&agora);
    snprintf(b, sizeof(b), "%02d:%02d:%02d", (uint8_t)ti->tm_hour, (uint8_t)ti->tm_min, (uint8_t)ti->tm_sec);
  }
  j = F("},\"h\":\"");
  j += b;
  j += F("\"}");
  server.sendContent(j);
  server.sendContent("");
}

/**
 * Seção Sistema em 4 grupos (Funcionamento, Memória, Hardware, Software), enviada grupo a grupo para
 * não montar a página inteira na RAM. Memória com barras coloridas pelo nível; ✅/⚠️ no reinício, na
 * hora, no sensor e na flash. Tudo dentro de #diag, que o botão 'Copiar diagnóstico' lê. As linhas que
 * mudam (linhaViva) são atualizadas pela página a cada 5 s, por /sistema.json.
 */
void enviaTabelaSistema() {
  server.sendContent_P(SIS_CSS);
  String t;
  t.reserve(1200);

  // ---- Funcionamento ----
  uint32_t motivo = ESP.getResetInfoPtr()->reason;   // travou = watchdog de hardware, exceção ou watchdog de software
  bool travou = (motivo == REASON_WDT_RST || motivo == REASON_EXCEPTION_RST || motivo == REASON_SOFT_WDT_RST);
  t = F("<div id=\"diag\"><div class=\"cap\">Funcionamento</div><div class=\"grp\">");
  t += linhaViva(0);
  t += linhaInfo("Último reinício", (travou ? F("⚠️ ") : F("✅ ")) + htmlEsc(ESP.getResetReason()));
  t += linhaViva(1);
  t += linhaInfo("Servidores NTP", NTP_SERVIDOR_1 "<br>" NTP_SERVIDOR_2 "<br>" NTP_SERVIDOR_3);
  t += linhaViva(2);
  if (coordOk) {
    String la = String(cidadeLat, 4), lo = String(cidadeLon, 4);
    t += linhaInfo("Coordenadas da cidade", F("<a href=\"https://www.openstreetmap.org/?mlat=") + la + F("&amp;mlon=") + lo +
                   F("#map=12/") + la + '/' + lo + F("\" target=\"_blank\" rel=\"noopener\">") + la + F(", ") + lo + F(" ↗</a>"));
  } else {
    t += linhaInfo("Coordenadas da cidade", F("ainda não recebidas"));
  }
  t += F("</div>");
  server.sendContent(t);

  // ---- Memória (barras) ----
  t = F("<div class=\"cap\">Memória</div><div class=\"grp\">");
  t += linhaViva(3);
  t += linhaViva(4);
  t += linhaViva(5);
  t += linhaViva(6);
  t += F("</div><div class=\"bts\"><button class=\"mini\" type=\"button\" id=\"bp\" onclick=\"pausar()\">⏸ Pausar</button>"
         "<span class=\"note\" id=\"sst\" style=\"margin:0\">Atualiza sozinho a cada 5 s.</span></div>");
  server.sendContent(t);

  // ---- Hardware ----
  String chip = String(ESP.getChipId(), HEX);
  chip.toUpperCase();
  String flashId = String(ESP.getFlashChipId(), HEX);
  flashId.toUpperCase();
  uint32_t fReal = ESP.getFlashChipRealSize(), fIde = ESP.getFlashChipSize();
  String mhz = String(ESP.getFlashChipSpeed() / 1000000UL) + F(" MHz");
  t = F("<div class=\"cap\">Hardware</div><div class=\"grp\">");
  t += linhaInfo("Chip ID", chip);
  t += linhaInfo("CPU", String(ESP.getCpuFreqMHz()) + F(" MHz"));
  if (fReal == fIde) t += linhaSub(F("Flash"), F("chip e configuração da IDE iguais"), F("✅ ") + fmtBytes(fReal) + F(" · ") + mhz);
  else t += linhaSub(F("Flash"), F("a IDE está configurada para outro tamanho · ") + mhz,
                     F("⚠️ chip ") + fmtBytes(fReal) + F(" · IDE ") + fmtBytes(fIde));
  t += linhaInfo("ID da flash", flashId);
  t += F("</div>");
  server.sendContent(t);

  // ---- Software ----
  t = F("<div class=\"cap\">Software</div><div class=\"grp\">");
  t += linhaInfo("Firmware", FIRMWARE_VERSAO);
  t += linhaInfo("Core ESP8266", htmlEsc(ESP.getCoreVersion()));
  t += linhaInfo("SDK", htmlEsc(String(ESP.getSdkVersion())));
  t += linhaInfo("Compilado em", String(__DATE__) + F(" ") + String(__TIME__));
  t += F("</div></div>");                // fecha o grupo e o #diag
  server.sendContent(t);
  server.sendContent_P(SIS_JS);         // botão 'Copiar diagnóstico'
}

// ---- /config: peças comuns ---------------------------------------------------------------

/** Tempo curto para os resumos ('menos de 1 min', '12 min', '3 h 5 min', '2 d 4 h'). */
String tempoCurto(unsigned long s) {
  if (s < 60UL) return F("menos de 1 min");
  if (s < 3600UL) return String(s / 60UL) + F(" min");
  if (s < 86400UL) return String(s / 3600UL) + F(" h ") + String((s % 3600UL) / 60UL) + F(" min");
  return String(s / 86400UL) + F(" d ") + String((s % 86400UL) / 3600UL) + F(" h");
}

/** Texto da mensagem de resultado (código 'r' que volta depois de salvar). erro = true para avisos. */
String msgConfig(const String& r, bool& erro) {
  String m = F("");
  if (r.equals(F("ok"))) m = F("✅ Configurações salvas. Atualizando o clima...");
  else if (r.equals(F("chave"))) m = F("⚠️ Chave inválida: use só letras e números (20 a 40 caracteres).");
  else if (r.equals(F("local"))) m = F("⚠️ ID da cidade inválido: informe o número (ou cole o endereço da cidade).");
  else if (r.equals(F("local_vazio"))) m = F("⚠️ Informe a cidade: busque pelo nome, preencha a latitude e a longitude, ou informe o ID.");
  else if (r.equals(F("coord"))) m = F("⚠️ Coordenadas inválidas: preencha as duas (latitude de -90 a 90 e longitude de -180 a 180).");
  else if (r.equals(F("idioma"))) m = F("⚠️ Idioma inválido.");
  else if (r.equals(F("disp"))) m = F("✅ Tela atualizada.");
  else if (r.equals(F("tempo"))) m = F("✅ Tempo de cada tela salvo.");
  else if (r.equals(F("tempo_erro"))) m = F("⚠️ Tempo inválido: escolha de 5 a 30 segundos.");
  else if (r.equals(F("tempoligada"))) m = F("✅ Tempo de tela ligada salvo.");
  else if (r.equals(F("tempoligada_erro"))) m = F("⚠️ Tempo de tela ligada inválido.");
  else if (r.equals(F("led"))) m = F("✅ LED de status salvo.");
  else if (r.equals(F("brilho"))) m = F("✅ Brilho salvo.");
  else if (r.equals(F("brilho_erro"))) m = F("⚠️ Brilho inválido: escolha de 5% a 100%.");
  else if (r.equals(F("telas"))) m = F("✅ Telas do display salvas.");
  else if (r.equals(F("telas_erro"))) m = F("⚠️ Marque pelo menos uma tela.");
  else if (r.equals(F("agenda"))) m = F("✅ Horário automático salvo.");
  else if (r.equals(F("hora"))) m = F("⚠️ Horário inválido.");
  else if (r.equals(F("horaigual"))) m = F("⚠️ Os horários de ligar e de apagar não podem ser iguais.");
  else if (r.equals(F("fuso"))) m = F("⚠️ Fuso horário inválido.");
  else if (r.equals(F("fuso_ok"))) m = F("✅ Fuso horário salvo.");
  else if (r.equals(F("bat"))) m = F("✅ Bateria e energia salvas.");
  else if (r.equals(F("eco"))) m = F("✅ Economia na bateria salva.");
  else if (r.equals(F("api"))) m = F("✅ Dados do painel salvos.");
  else if (r.equals(F("apiurl"))) m = F("⚠️ URL inválida: use http:// ou https:// e não deixe espaços.");
  else if (r.equals(F("apiid"))) m = F("⚠️ ID do dispositivo inválido: use até 32 caracteres (letras, números, - _ .).");
  else if (r.equals(F("apikey"))) m = F("⚠️ DEVICE_KEY inválida: use de 8 a 64 caracteres, sem espaços.");
  else if (r.equals(F("envio_ok"))) m = F("✅ Dados enviados ao painel.");
  else if (r.equals(F("envio_erro"))) m = String(F("⚠️ Falha no envio: ")) + htmlEsc(apiUltimoMsg);
  else if (r.equals(F("log_limpo"))) m = F("✅ Registro apagado.");
  erro = m.startsWith("⚠️");
  return m;
}

/** Resumo do estado de uma seção, mostrado embaixo do nome no menu. */
String secaoResumo(uint8_t k) {
  String id = FPSTR(SECOES[k].id);
  if (id.equals(F("clima"))) {
    if (OPEN_WEATHER_MAP_APP_ID.length() == 0) return F("⚠️ falta a chave da API");
    if (weatherOk) return htmlEsc(currentWeather.cityName) + F(" · chave salva");
    return F("⚠️ sem dados do clima");
  }
  if (id.equals(F("display"))) {
    return String(displayLigado ? F("Ligado") : F("Apagado")) + F(" · brilho ") + String(brilhoAgora()) + F("% · ") +
           String(numTelasAtivas) + (numTelasAtivas == 1 ? F(" tela") : F(" telas"));
  }
  if (id.equals(F("hora"))) {
    String fuso = F("UTC");
    for (uint8_t f = 0; f < FUSO_COUNT; f++) if (FUSOS[f].min == fusoMin) fuso = String(FPSTR(FUSOS[f].nome)).substring(0, 9);
    if (horarioVerao) fuso += F(" +verão");
    return fuso + (horaOk() ? F(" · sincronizada") : F(" · sem sincronizar"));
  }
  if (id.equals(F("bateria"))) {
    String s = bateriaAtiva ? String(bateriaPct) + F("% · ") + String(bateriaV, 2) + F(" V") : String(F("bateria desativada"));
    if (energiaAtiva && PIN_ENERGIA >= 0) s += energiaExterna ? F(" · na tomada") : F(" · na bateria");
    return s;
  }
  if (id.equals(F("painel"))) {
    if (apiUrl.length() == 0) return F("envio desligado");
    if (!apiTentou) return F("nenhum envio ainda");
    return F("último envio há ") + tempoCurto((millis() - apiUltimoMs) / 1000UL) + (apiUltimoOk ? F(" ✅") : F(" ⚠️"));
  }
  if (id.equals(F("wifi"))) {
    if (WiFi.status() != WL_CONNECTED) return F("⚠️ desconectado");
    return htmlEsc(WiFi.SSID()) + F(" · sinal ") + String(qualidadeSinal(WiFi.RSSI())) + F("%");   // dBm fica na seção WiFi
  }
  if (id.equals(F("led"))) {
    if (!ledStatusAtivo) return F("desligado");
    return ledSegueAgenda ? F("ligado · segue o horário da tela") : F("ligado");
  }
#if LOG_WEB
  if (id.equals(F("log"))) {
    if (logCount == 0) return F("nenhum evento ainda");
    return String(logCount) + (logCount == 1 ? F(" evento") : F(" eventos")) + F(" · último há ") +
           tempoCurto((millis() - logUltimoMs) / 1000UL);
  }
#endif
  return F("v" FIRMWARE_VERSAO " · ligado há ") + tempoCurto(millis() / 1000UL);   // sistema
}

/**
 * Começa uma página da /config: cabeçalhos, estilo (da flash), link de voltar, título e a mensagem
 * de resultado. A página vai em partes (chunked): nenhuma página inteira fica na RAM.
 */
void cfgCabecalho(const String& titulo, const String& voltaHref, const String& voltaTexto, const String& r) {
  server.sendHeader(F("Cache-Control"), F("no-store"));   // página protegida por login: o navegador não guarda cópia
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(F("<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
                     "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>") + titulo + F("</title>"));
  server.sendContent_P(CFG_CSS);
  String h = F("</head><body><div class=\"wrap\"><a class=\"vol\" href=\"") + voltaHref + F("\"><b>‹</b>") + voltaTexto + F("</a>"
             "<h1>") + titulo + F("</h1>");
  bool erro = false;
  String m = msgConfig(r, erro);
  if (m.length()) h += String(F("<div class=\"msg")) + (erro ? F(" err") : F("")) + F("\">") + m + F("</div>");
  server.sendContent(h);
}

/** Termina a página: envia o que falta em 'h', fecha o HTML e encerra a resposta. */
void cfgRodape(String& h) {
  h += F("</div></body></html>");
  server.sendContent(h);
  server.sendContent("");               // encerra a resposta (chunked)
}

/** Linha com chave liga/desliga. autoEnvia = salva ao tocar (sem botão). */
String itemSwitch(const char* nome, const String& rotulo, bool ligado, bool autoEnvia, const String& sub) {
  String s = F("<label class=\"it\"><span class=\"tx\">") + rotulo;
  if (sub.length()) s += F("<small class=\"w\">") + sub + F("</small>");
  s += F("</span><input type=\"checkbox\" class=\"sw\" name=\"") + String(nome) + F("\" value=\"1\"");
  if (ligado) s += F(" checked");
  if (autoEnvia) s += F(" onchange=\"this.form.submit()\"");
  s += F("></label>");
  return s;
}

/** Abre uma linha com lista de opções (feche com '</select></label>'). autoEnvia = salva ao escolher. */
String itemSelect(const String& rotulo, const char* nome, bool autoEnvia) {
  String s = F("<label class=\"it\"><span class=\"tx\">") + rotulo + F("</span><select name=\"") + String(nome) + F("\"");
  if (autoEnvia) s += F(" onchange=\"this.form.submit()\"");
  s += F(">");
  return s;
}

/** Uma opção de lista. */
String opcao(const String& valor, const String& texto, bool sel) {
  return F("<option value=\"") + valor + F("\"") + (sel ? F(" selected") : F("")) + F(">") + texto + F("</option>");
}

// ---- /config: menu ------------------------------------------------------------------------

/** Menu da /config: uma linha por seção, com ícone, nome e resumo do estado, em 3 grupos. */
void cfgMenu(const String& r) {
  cfgCabecalho(F("Configurações"), F("/"), F("Início"), r);
  String h;
  h.reserve(2200);
  h = F("<div class=\"grp\">");
  for (uint8_t k = 0; k < SECOES_TOTAL; k++) {
    if (k == 3 || k == 6) h += F("</div><div class=\"grp\">");   // novo grupo
    h += F("<a class=\"it\" href=\"/config?s=") + String(FPSTR(SECOES[k].id)) + F("\">"
         "<span class=\"ic\" style=\"background:") + String(FPSTR(SECOES[k].cor)) + F("\">") + String(FPSTR(SECOES[k].icone)) + F("</span>"
         "<span class=\"tx\">") + String(FPSTR(SECOES[k].nome)) + F("<small>") + secaoResumo(k) + F("</small></span>"
         "<span class=\"chev\">›</span></a>");
  }
  h += F("</div>");
  h += F("<div class=\"grp\"><a class=\"it\" href=\"/sobre?de=config\"><span class=\"ic\" style=\"background:#4fc3f7\">ℹ️</span>"
         "<span class=\"tx\">Sobre<small>versão, manual e links do projeto</small></span><span class=\"chev\">›</span></a></div>");
  if (loginAtivo()) h += F("<form method=\"POST\" action=\"/sair\"><button class=\"btn sec\" type=\"submit\">Sair (só neste navegador)</button></form>");
  h += F("<div class=\"pe\">Estação Meteorológica ESP8266 · v" FIRMWARE_VERSAO "</div>");
  cfgRodape(h);
}

// ---- /config: Cidade e clima --------------------------------------------------------------

/** Seção 'Cidade e clima': chave da API, cidade (busca pelo nome, latitude/longitude, ID) e idioma. */
void cfgClima(const String& r) {
  cfgCabecalho(F("Cidade e clima"), F("/config"), F("Configurações"), r);
  String masked = F("cole a chave aqui");
  if (OPEN_WEATHER_MAP_APP_ID.length() >= 4) {
    masked = F("•••••••• ") + OPEN_WEATHER_MAP_APP_ID.substring(OPEN_WEATHER_MAP_APP_ID.length() - 4) + F(" (salva)");
  }
  String h;
  h.reserve(3600);
  h = F("<div class=\"cap\">Estado</div><div class=\"grp\">") +
      linhaInfo("Clima", weatherOk ? F("✅ ") + htmlEsc(currentWeather.cityName) : String(F("⚠️ sem dados"))) +
      linhaInfo("Coordenadas", coordOk ? String(cidadeLat, 4) + F(", ") + String(cidadeLon, 4) : String(F("ainda não recebidas"))) +
      F("</div>"
      "<form method=\"POST\" action=\"/salvar\">"
      "<div class=\"cap\">Chave da API (OpenWeatherMap)</div><div class=\"grp\"><div class=\"fld\">"
      "<input name=\"apiKey\" type=\"text\" autocomplete=\"off\" autocapitalize=\"off\" placeholder=\"") + htmlEsc(masked) + F("\">"
      "</div></div>"
      "<div class=\"note\">Deixe em branco para manter a atual. Crie a sua em "
      "<a href=\"https://home.openweathermap.org/api_keys\" target=\"_blank\" rel=\"noopener\">home.openweathermap.org/api_keys</a> "
      "(chaves novas podem levar um tempo para ativar).</div>"
      "<div class=\"cap\">Cidade</div><div class=\"grp\"><div class=\"fld\">"
      "<span class=\"lb\">Buscar pelo nome</span>"
      "<div style=\"display:flex;gap:8px\"><input id=\"bq\" type=\"text\" autocomplete=\"off\" placeholder=\"Ex.: Campinas, BR\" style=\"flex:1\" "
      "onkeydown=\"if(event.key==='Enter'){event.preventDefault();busca()}\">"
      "<button type=\"button\" class=\"mini\" onclick=\"busca()\">Buscar</button></div>"
      "<div id=\"br\" class=\"res\">Escolha um resultado para preencher a latitude e a longitude.</div></div>"
      "<label class=\"it\"><span class=\"tx\">Latitude</span><input class=\"num\" name=\"lat\" id=\"lat\" type=\"text\" inputmode=\"decimal\" "
      "placeholder=\"-22.9056\" value=\"") + (coordOk ? String(cidadeLat, 4) : String("")) + F("\"></label>"
      "<label class=\"it\"><span class=\"tx\">Longitude</span><input class=\"num\" name=\"lon\" id=\"lon\" type=\"text\" inputmode=\"decimal\" "
      "placeholder=\"-47.0608\" value=\"") + (coordOk ? String(cidadeLon, 4) : String("")) + F("\"></label>"
      "<label class=\"it\"><span class=\"tx\">ID da cidade<small>opcional</small></span><input class=\"num\" name=\"locId\" id=\"loc\" "
      "type=\"text\" inputmode=\"numeric\" value=\"") + htmlEsc(OPEN_WEATHER_MAP_LOCATION_ID) + F("\"></label>"
      "</div>");
  h += F("<div class=\"note\">Com a latitude e a longitude, valem elas; o ID, se houver, só vai para o painel (city_id). "
      "Só com o ID, a estação descobre as coordenadas na 1ª consulta. Também pode colar \"latitude, longitude\" (como o Google Maps copia) "
      "no campo da latitude. A busca usa a chave já salva. ID: "
      "<a href=\"https://openweathermap.org/find\" target=\"_blank\" rel=\"noopener\">openweathermap.org/find</a> (número no fim do endereço).</div>");
  server.sendContent(h);
  h = F("<div class=\"cap\">Idioma das descrições</div><div class=\"grp\">") + itemSelect(F("Idioma"), "lang", false);
  for (uint8_t k = 0; k < LANG_COUNT; k++) h += opcao(LANG_CODES[k], FPSTR(LANG_NAMES[k]), OPEN_WEATHER_MAP_LANGUAGE == LANG_CODES[k]);
  h += F("</select></label></div>"
       "<button class=\"btn\" type=\"submit\">Salvar e atualizar</button></form>"
       "<script>"
       "async function busca(){const q=document.getElementById('bq').value.trim(),r=document.getElementById('br');"
       "if(q.length<2){r.textContent='Digite o nome da cidade.';return}r.textContent='Buscando...';"
       "try{const d=await(await fetch('/buscar-cidade?q='+encodeURIComponent(q),{cache:'no-store'})).json();"
       "if(!d.ok){r.textContent='\\u26a0\\ufe0f '+d.erro;return}"
       "if(!d.cidades.length){r.textContent='Nenhuma cidade encontrada. Tente \"Cidade, BR\" ou \"Cidade, UF, BR\".';return}"
       "r.textContent='';d.cidades.forEach(c=>{const b=document.createElement('button');b.type='button';"
       "const t=c.nome+(c.estado?', '+c.estado:'')+' - '+c.pais;"
       "b.textContent=t+'  ('+c.lat.toFixed(4)+', '+c.lon.toFixed(4)+')';"
       "b.onclick=()=>{document.getElementById('lat').value=c.lat.toFixed(4);document.getElementById('lon').value=c.lon.toFixed(4);"
       "document.getElementById('loc').value='';r.textContent='\\u2705 '+t+'. Agora toque em \"Salvar e atualizar\".'};"
       "r.appendChild(b)})}catch(e){r.textContent='\\u26a0\\ufe0f Sem resposta da estação.'}}"
       "</script>");
  cfgRodape(h);
}

// ---- /config: Display ---------------------------------------------------------------------

/**
 * Seção 'Display': tela ligada, telas do display, passagem e tempo ligada, brilho e horário
 * automático. Listas e chaves salvam ao tocar; listas longas abrem na própria página (›).
 */
void cfgDisplay(const String& r) {
  cfgCabecalho(F("Display"), F("/config"), F("Configurações"), r);
  String h;
  h.reserve(3000);
  // Tela ligada (a chave alterna) e o brilho de agora
  h = F("<div class=\"cap\">Tela</div><div class=\"grp\">"
      "<form method=\"POST\" action=\"/display\"><input type=\"hidden\" name=\"estado\" value=\"t\"><input type=\"hidden\" name=\"volta\" value=\"1\">") +
      itemSwitch("x", F("Tela ligada"), displayLigado, true) + F("</form>") +
      linhaInfo("Brilho agora", String(brilhoAgora()) + F("%") + (noiteAgora() ? F(" (noturno)") : F(""))) + F("</div>");

  // Telas do display (abre na própria página)
  h += F("<div class=\"cap\">Telas e horário</div>"
       "<details class=\"grp\"");
  if (r.equals(F("telas")) || r.equals(F("telas_erro"))) h += F(" open");
  h += F("><summary class=\"it\"><span class=\"tx\">Telas do display</span><span class=\"val\">") + String(numTelasAtivas) + F(" de ") +
       String(TELAS_TOTAL) + F("</span><span class=\"chev\">›</span></summary><form method=\"POST\" action=\"/telas\">");
  for (uint8_t k = 0; k < TELAS_TOTAL; k++) {
    h += itemSwitch((F("t") + String(k)).c_str(), FPSTR(TELAS_NOME[k]), telasMask & (1 << k), false,
                    k == TELA_WIFI ? String(F("mostra o IP no display")) : String(""));
  }
  h += F("<div class=\"fld\"><button class=\"btn\" type=\"submit\">Salvar telas</button></div></form></details>");
  server.sendContent(h);

  // Horário automático (abre na própria página)
  h = F("<details class=\"grp\"");
  if (r.equals(F("agenda")) || r.equals(F("hora")) || r.equals(F("horaigual"))) h += F(" open");
  h += F("><summary class=\"it\"><span class=\"tx\">Horário automático</span><span class=\"val\">") +
       (agendaAtiva ? formataHora(agendaLiga) + F("–") + formataHora(agendaDesliga) : String(F("desligado"))) +
       F("</span><span class=\"chev\">›</span></summary><form method=\"POST\" action=\"/agenda\">") +
       itemSwitch("ativa", F("Ligar e apagar sozinha"), agendaAtiva, false) +
       F("<label class=\"it\"><span class=\"tx\">Ligar às</span><input type=\"time\" name=\"liga\" value=\"") + formataHora(agendaLiga) + F("\"></label>"
       "<label class=\"it\"><span class=\"tx\">Apagar às</span><input type=\"time\" name=\"desliga\" value=\"") + formataHora(agendaDesliga) + F("\"></label>"
       "<div class=\"fld\"><button class=\"btn\" type=\"submit\">Salvar horário</button></div></form></details>");
  h += F("<div class=\"note\">Elas passam na ordem da lista; com uma só, ela fica fixa. Sem a tela \"WiFi e bateria\", o IP aparece só ao conectar. "
       "O botão manual da tela vale até o próximo horário programado (precisa da hora da internet).</div>");

  // Passagem das telas e tempo de tela ligada (salvam ao escolher)
  h += F("<div class=\"cap\">Passagem</div><div class=\"grp\"><form method=\"POST\" action=\"/tempotela\">") +
       itemSelect(F("Tempo de cada tela"), "seg", true);
  for (uint8_t sgs = 5; sgs <= 30; sgs += 5) h += opcao(String(sgs), String(sgs) + F(" s"), sgs == telaSeg);
  h += F("</select></label></form><form method=\"POST\" action=\"/tempoligada\">") + itemSelect(F("Apagar sozinha após"), "min", true);
  static const uint8_t OPCOES_MIN[] = {0, 1, 5, 10, 15, 30, 60};
  for (uint8_t k = 0; k < 7; k++) {
    h += opcao(String(OPCOES_MIN[k]), OPCOES_MIN[k] == 0 ? String(F("Nunca")) : String(OPCOES_MIN[k]) + F(" min"), OPCOES_MIN[k] == telaTimeoutMin);
  }
  h += F("</select></label></form></div>"
       "<div class=\"note\">Com a tela apagada, um clique no botão das telas acende; no horário de apagar, acende só por 10 s.</div>");
  server.sendContent(h);

  // Brilho (salva ao escolher)
  h = F("<div class=\"cap\">Brilho</div><div class=\"grp\"><form method=\"POST\" action=\"/brilho\">") + itemSelect(F("Dia"), "dia", true);
  for (uint8_t p = 100; p >= 5; p -= 5) h += opcao(String(p), String(p) + F("%"), p == brilhoPct);
  h += F("</select></label>") + itemSelect(F("Noite"), "noite", true);
  for (uint8_t p = 100; p >= 5; p -= 5) h += opcao(String(p), String(p) + F("%"), p == brilhoNoitePct);
  h += F("</select></label>") + itemSelect(F("A noite começa"), "fonte", true) +
       opcao(F("0"), F("No horário automático"), !noitePeloSol) + opcao(F("1"), F("No pôr do sol"), noitePeloSol) +
       F("</select></label>") + itemSelect(F("No horário de apagar"), "modo", true) +
       opcao(F("0"), F("Apagar a tela"), !noiteDiminui) + opcao(F("1"), F("Manter acesa"), noiteDiminui) +
       F("</select></label></form></div>"
       "<div class=\"note\">Pôr do sol: o brilho noturno vale do pôr ao nascer do sol (");
  h += astroOk ? F("hoje ") + horaCurta(solPoe) + F(" a ") + horaCurta(solNasce) : String(F("disponível depois de receber o clima"));
  h += F("). Horário automático: vale no horário de apagar. Acender a tela à noite usa sempre o brilho noturno.</div>");
  cfgRodape(h);
}

// ---- /config: Data e hora -----------------------------------------------------------------

/** Seção 'Data e hora': hora atual, fuso e horário de verão (salvam ao escolher) e servidores NTP. */
void cfgHora(const String& r) {
  cfgCabecalho(F("Data e hora"), F("/config"), F("Configurações"), r);
  String agora = F("sem sincronização");
  if (horaOk()) {
    time_t t = time(nullptr);
    struct tm* ti = localtime(&t);
    char b[32];   // com os casts o compilador sabe o tamanho máximo (evita -Wformat-truncation)
    snprintf(b, sizeof(b), "%02d/%02d/%04d %02d:%02d", (uint8_t)ti->tm_mday, (uint8_t)(ti->tm_mon + 1), (uint16_t)(ti->tm_year + 1900),
             (uint8_t)ti->tm_hour, (uint8_t)ti->tm_min);
    agora = b;
  }
  String h;
  h.reserve(2600);
  h = F("<div class=\"grp\">") + linhaInfo("Agora", agora) + F("</div>"
      "<div class=\"cap\">Fuso horário</div><div class=\"grp\"><form method=\"POST\" action=\"/hora\">") + itemSelect(F("Fuso"), "fuso", true);
  for (uint8_t k = 0; k < FUSO_COUNT; k++) h += opcao(String(FUSOS[k].min), FPSTR(FUSOS[k].nome), FUSOS[k].min == fusoMin);
  h += F("</select></label>") + itemSwitch("verao", F("Horário de verão"), horarioVerao, true, F("adianta 1 hora")) + F("</form></div>"
       "<div class=\"cap\">Servidores da hora (NTP)</div><div class=\"grp\">") +
       linhaInfo("1º", NTP_SERVIDOR_1) + linhaInfo("2º", NTP_SERVIDOR_2) + linhaInfo("3º", NTP_SERVIDOR_3) + F("</div>"
       "<div class=\"note\">Os servidores são definidos no EstacaoConfig.h e usados na ordem, se um não responder.</div>");
  cfgRodape(h);
}

// ---- /config: Bateria e energia -----------------------------------------------------------

/** Seção 'Bateria e energia': ativar a leitura da bateria e o sensor da UPS (salvam ao tocar) e o estado. */
void cfgBateria(const String& r) {
  cfgCabecalho(F("Bateria e energia"), F("/config"), F("Configurações"), r);
  String h;
  h.reserve(2800);
  h = F("<div class=\"grp\"><form method=\"POST\" action=\"/bateria\">") +
      itemSwitch("ativa", F("Bateria no A0"), bateriaAtiva, true, F("este ESP tem bateria ligada ao A0 pelo divisor")) +
      itemSwitch("energia", F("Placa UPS"), energiaAtiva, true, F("sensor de energia externa (5 V) no pino de energia")) +
      F("</form></div>");
  // Economia na bateria (light sleep do WiFi): só faz sentido com a placa UPS, que avisa quando falta energia
  h += F("<div class=\"cap\">Economia na bateria</div>");
  if (energiaAtiva && PIN_ENERGIA >= 0) {
    h += F("<div class=\"grp\"><form method=\"POST\" action=\"/economia\">") +
         itemSwitch("eco", F("Economia na bateria"), ecoBateria, true, F("sem energia externa, o WiFi dorme entre os acessos")) +
         itemSelect(F("Envio ao painel na bateria"), "int", true);
    static const uint8_t OPCOES_ECO[] = {15, 20, 30, 45, 60};
    for (uint8_t k = 0; k < 5; k++) h += opcao(String(OPCOES_ECO[k]), F("a cada ") + String(OPCOES_ECO[k]) + F(" min"), OPCOES_ECO[k] == ecoIntervaloMin);
    h += F("</select></label></form>");
    h += linhaInfo("Agora", !ecoBateria ? String(F("Desligada")) : naBateria() ? String(F("🔋 Ativa (na bateria)")) : String(F("🔌 Em espera (na tomada)")));
    h += F("</div>");
    h += F("<div class=\"note\">Na bateria, o ESP gasta bem menos com o WiFi parado. Abrir esta página ou apertar um botão "
           "deixa o ESP acordado por ");
    h += String(ECO_ACORDADO_MS / 60000UL);
    h += F(" min: só o primeiro acesso demora um pouco mais. Na tomada, nada muda.</div>");
  } else {
    h += F("<div class=\"note\">Ative a placa UPS acima: a economia usa o sensor de energia para saber quando o ESP está na bateria.</div>");
  }
  if (bateriaAtiva || energiaAtiva) h += F("<div class=\"cap\">Estado</div>") + tabelaBateria();
  else h += F("<div class=\"note\">Ative só o que este ESP tem ligado (veja as ligações no README).</div>");
  cfgRodape(h);
}

// ---- /config: Painel web ------------------------------------------------------------------

/** Seção 'Painel web': último envio, dados do painel (ID, URL, DEVICE_KEY) e 'Enviar dados agora'. */
void cfgPainel(const String& r) {
  cfgCabecalho(F("Painel web"), F("/config"), F("Configurações"), r);
  String maskedKey = F("cole a DEVICE_KEY aqui");
  if (deviceKey.length() >= 4) maskedKey = F("•••••••• ") + deviceKey.substring(deviceKey.length() - 4) + F(" (salva)");
  String ultimo = !apiTentou ? String(F("nenhum ainda"))
                : F("há ") + tempoCurto((millis() - apiUltimoMs) / 1000UL) + (apiUltimoOk ? F(" · ✅ ") : F(" · ⚠️ ")) + htmlEsc(apiUltimoMsg);
  String cidade = currentWeather.cityName.length() ? htmlEsc(currentWeather.cityName) : String(F("(ainda não recebida)"));
  cidade += OPEN_WEATHER_MAP_LOCATION_ID.length() ? F(" · ID ") + htmlEsc(OPEN_WEATHER_MAP_LOCATION_ID) : String(F(" · sem ID (null)"));
  String h;
  h.reserve(2600);
  h = F("<div class=\"cap\">Envio</div><div class=\"grp\">") +
      linhaInfo("Último envio", ultimo) +
      linhaInfo("Automático", apiUrl.length() ? F("a cada ") + String(intervaloEnvioMs() / 60000UL) + F(" min") + (intervaloEnvioMs() != PAINEL_INTERVAL_MS ? F(" (economia na bateria)") : F("")) : String(F("desligado (sem URL)"))) +
      linhaInfo("Cidade enviada", cidade) + F("</div>"
      "<form method=\"POST\" action=\"/enviar\"><button class=\"btn sec\" type=\"submit\">Enviar dados agora</button></form>"
      "<form method=\"POST\" action=\"/api\"><div class=\"cap\">Dados do painel</div><div class=\"grp\">"
      "<div class=\"fld\"><span class=\"lb\">ID do dispositivo (device_id)</span>"
      "<input name=\"deviceId\" type=\"text\" autocapitalize=\"off\" value=\"") + htmlEsc(idDispositivo()) + F("\"></div>"
      "<div class=\"fld\"><span class=\"lb\">URL da API</span>"
      "<input name=\"apiUrl\" type=\"text\" inputmode=\"url\" autocapitalize=\"off\" placeholder=\"https://seu-app.vercel.app/api/...\" value=\"") +
      htmlEsc(apiUrl) + F("\"></div>"
      "<div class=\"fld\"><span class=\"lb\">DEVICE_KEY</span>"
      "<input name=\"deviceKey\" type=\"text\" autocomplete=\"off\" autocapitalize=\"off\" placeholder=\"") + htmlEsc(maskedKey) + F("\"></div>"
      "</div>");
  h += F("<div class=\"note\">O ID precisa ser igual ao cadastrado no painel. Deixe a chave em branco para manter a atual. "
      "Use https:// (a Vercel redireciona o http). Sem URL, o envio fica desligado.</div>"
      "<button class=\"btn\" type=\"submit\">Salvar dados do painel</button></form>");
  cfgRodape(h);
}

// ---- /config: WiFi ------------------------------------------------------------------------

/** Seção 'WiFi': dados da conexão, abrir o portal e apagar a rede salva. */
void cfgWiFi(const String& r) {
  cfgCabecalho(F("WiFi"), F("/config"), F("Configurações"), r);
  String h = tabelaWiFi();
  h += F("<form method=\"POST\" action=\"/portal\" onsubmit=\"return confirm('Abrir o portal de configuração? A estação reinicia ao terminar.')\">"
       "<button class=\"btn sec\" type=\"submit\">Abrir portal de configuração</button></form>"
       "<form method=\"POST\" action=\"/reset-wifi\" onsubmit=\"return confirm('Apagar a rede WiFi salva e reiniciar?')\">"
       "<button class=\"btn red\" type=\"submit\">Apagar dados de WiFi</button></form>"
       "<div class=\"note\" style=\"margin-top:-8px\">O portal cria a rede " PORTAL_SSID " (com a senha PORTAL_SENHA do EstacaoConfig.h).</div>");
  cfgRodape(h);
}

// ---- /config: LED de status ---------------------------------------------------------------

/** Seção 'LED de status': ligar/desligar e seguir o horário automático da tela (salvam ao tocar). */
void cfgLed(const String& r) {
  cfgCabecalho(F("LED de status"), F("/config"), F("Configurações"), r);
  String h = F("<div class=\"grp\"><form method=\"POST\" action=\"/led\">") +
             itemSwitch("ativo", F("Piscar para indicar o estado"), ledStatusAtivo, true,
                        F("rápido ao conectar e ao enviar, devagar sem WiFi; 1 acendida = sucesso, 3 piscadas = falha")) +
             itemSwitch("agenda", F("Seguir o horário da tela"), ledSegueAgenda, true,
                        F("apagado no horário de tela desligada (precisa do horário automático)")) +
             F("</form></div>");
  cfgRodape(h);
}

// ---- /config: Sistema ---------------------------------------------------------------------

/** Seção 'Sistema': informações do ESP8266, login (desconectar todos) e reset geral. */
void cfgSistema(const String& r) {
  cfgCabecalho(F("Sistema"), F("/config"), F("Configurações"), r);
  enviaTabelaSistema();
  String h;
  if (loginAtivo()) {
    h += F("<div class=\"cap\">Login</div>"
         "<form method=\"POST\" action=\"/sair-todos\" onsubmit=\"return confirm('Desconectar todos os navegadores? Todos vão precisar digitar a senha de novo.')\">"
         "<button class=\"btn sec\" type=\"submit\">Desconectar todos os navegadores</button></form>"
         "<div class=\"note\" style=\"margin-top:-8px\">Cada navegador fica conectado por ") + String(LOGIN_DIAS) + F(" dias. "
         "Use se perdeu um celular ou entrou num computador de outra pessoa.</div>");
  }
  if (h.length()) server.sendContent(h);   // vazio encerraria a resposta (chunked)
  // Backup: exportar baixa o JSON; importar lê o arquivo no navegador, confere e envia no campo 'cfg'
  static const char BACKUP_HTML[] PROGMEM =
    "<div class=\"cap\">Backup das configurações</div>"
    "<form method=\"GET\" action=\"/exportar\"><button class=\"btn sec\" type=\"submit\">Exportar configurações</button></form>"
    "<form method=\"POST\" action=\"/importar\" id=\"fimp\"><input type=\"hidden\" name=\"cfg\" id=\"cfg\">"
    "<input type=\"file\" id=\"arq\" accept=\".json,application/json\" style=\"display:none\" onchange=\"importa(this)\">"
    "<button class=\"btn sec\" type=\"button\" onclick=\"document.getElementById('arq').click()\">Importar configurações</button></form>"
    "<div class=\"note\" style=\"margin-top:-8px\">O arquivo leva todos os ajustes, inclusive a chave do OpenWeatherMap e a DEVICE_KEY: "
    "guarde-o com cuidado. Não leva o WiFi, as senhas do EstacaoConfig.h nem o ID do dispositivo. Ao importar, este ESP mantém o ID dele "
    "e reinicia.</div>"
    "<script>function importa(i){var f=i.files[0];i.value='';if(!f)return;"
    "if(f.size>4096){alert('Arquivo grande demais para um backup da estação.');return;}"
    "var r=new FileReader();r.onload=function(){var j;try{j=JSON.parse(r.result)}catch(e){alert('Arquivo inválido: não é um JSON.');return;}"
    "if(!j||j.formato!==1){alert('Este arquivo não é um backup da estação.');return;}"
    "if(confirm('Importar as configurações do ESP '+(j.chip||'?')+' (firmware '+(j.firmware||'?')+')? Este ESP mantém o ID dele e reinicia.')){"
    "document.getElementById('cfg').value=r.result;document.getElementById('fimp').submit();}};r.readAsText(f);}</script>";
  server.sendContent_P(BACKUP_HTML);
  h = F("");
  h += F("<div class=\"cap\">Reiniciar</div>"
       "<form method=\"POST\" action=\"/reiniciar\" onsubmit=\"return confirm('Reiniciar o ESP agora?')\">"
       "<button class=\"btn sec\" type=\"submit\">Reiniciar o ESP</button></form>"
       "<div class=\"note\" style=\"margin-top:-8px\">Só reinicia a placa: o WiFi e os ajustes continuam salvos. "
       "A página volta sozinha em uns 15 s.</div>"
       "<div class=\"cap\">Reset geral</div>"
       "<form method=\"POST\" action=\"/reset-geral\" onsubmit=\"return confirm('Apagar o WiFi e TODOS os ajustes e reiniciar?')\">"
       "<button class=\"btn red\" type=\"submit\">Reset geral</button></form>"
       "<div class=\"note\" style=\"margin-top:-8px\">Apaga o WiFi e todos os ajustes salvos e reinicia no padrão do código. "
       "Também pode ser feito segurando o botão de reset geral por 10 s.</div>");
  cfgRodape(h);
}

/** GET /config: o menu, ou a seção pedida em ?s= (cada uma é uma página pequena, enviada em partes). */
void paginaConfig() {
  if (!exigeLogin()) return;            // sem login: vai para /login
  String s = server.arg(F("s"));
  String r = server.arg(F("r"));
  if (s.equals(F("clima"))) cfgClima(r);
  else if (s.equals(F("display"))) cfgDisplay(r);
  else if (s.equals(F("hora"))) cfgHora(r);
  else if (s.equals(F("bateria"))) cfgBateria(r);
  else if (s.equals(F("painel"))) cfgPainel(r);
  else if (s.equals(F("wifi"))) cfgWiFi(r);
  else if (s.equals(F("led"))) cfgLed(r);
  else if (s.equals(F("sistema"))) cfgSistema(r);
#if LOG_WEB
  else if (s.equals(F("log"))) cfgLog(r);
#endif
  else cfgMenu(r);
}
