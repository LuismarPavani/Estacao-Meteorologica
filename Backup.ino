/*
 * BACKUP DAS CONFIGURAÇÕES (exportar e importar)
 *
 * /exportar (backupJson) e /importar (Importacao e importa...).
 * ESP32: igual.
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- BACKUP DAS CONFIGURAÇÕES (exportar e importar) -----------------------------------------

/**
 * Monta o arquivo de backup: todos os ajustes da /config, inclusive a chave do OpenWeatherMap e a
 * DEVICE_KEY, menos o ID do dispositivo (cada ESP tem o seu). JSON de um nível só, com nomes em
 * português, para ser lido por este firmware e pelos próximos (o porte para ESP32 inclusive).
 * Não entram o WiFi (fica com o WiFiManager), as senhas do EstacaoConfig.h nem o sal do login.
 */
String backupJson() {
  String j;
  j.reserve(1100);
  auto txt = [&](const __FlashStringHelper* nome, const String& v) { j += F(",\""); j += nome; j += F("\":\""); j += jsonEscape(v); j += '"'; };
  auto num = [&](const __FlashStringHelper* nome, long v) { j += F(",\""); j += nome; j += F("\":"); j += String(v); };
  auto sim = [&](const __FlashStringHelper* nome, bool v) { j += F(",\""); j += nome; j += F("\":"); j += v ? F("true") : F("false"); };
  j = F("{\"formato\":1,\"firmware\":\"" FIRMWARE_VERSAO "\"");
  txt(F("chip"), String(ESP.getChipId(), HEX));
  // Cidade e clima
  txt(F("owmChave"), OPEN_WEATHER_MAP_APP_ID);
  txt(F("cidadeId"), OPEN_WEATHER_MAP_LOCATION_ID);
  if (coordOk) { j += F(",\"lat\":"); j += String(cidadeLat, 5); j += F(",\"lon\":"); j += String(cidadeLon, 5); }
  txt(F("idioma"), OPEN_WEATHER_MAP_LANGUAGE);
  // Data e hora
  num(F("fuso"), fusoMin);
  sim(F("verao"), horarioVerao);
  // Display
  sim(F("telaLigada"), displayLigadoSalvo);
  num(F("telas"), telasMask);
  num(F("telaSeg"), telaSeg);
  num(F("telaMin"), telaTimeoutMin);
  num(F("brilho"), brilhoPct);
  num(F("brilhoNoite"), brilhoNoitePct);
  sim(F("noiteDiminui"), noiteDiminui);
  sim(F("noitePeloSol"), noitePeloSol);
  sim(F("agenda"), agendaAtiva);
  txt(F("agendaLiga"), formataHora(agendaLiga));
  txt(F("agendaDesliga"), formataHora(agendaDesliga));
  // Bateria e energia
  sim(F("bateria"), bateriaAtiva);
  sim(F("energia"), energiaAtiva);
  sim(F("eco"), ecoBateria);
  num(F("ecoIntervalo"), ecoIntervaloMin);
  // LED de status
  sim(F("led"), ledStatusAtivo);
  sim(F("ledAgenda"), ledSegueAgenda);
  // Painel web (o ID do dispositivo fica de fora de propósito)
  txt(F("painelUrl"), apiUrl);
  txt(F("painelChave"), deviceKey);
  j += '}';
  return j;
}

/** GET /exportar: baixa o arquivo de backup (estacao-<chip>.json). Precisa estar logado (tem chaves). */
void handleExportar() {
  if (!exigeLogin()) return;            // sem login: vai para /login
  String nome = F("estacao-") + String(ESP.getChipId(), HEX) + F(".json");
  server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"") + nome + F("\""));
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(200, "application/json", backupJson());
}

/** true para true/1, false para false/0; devolve false se o texto não é nenhum dos dois. */
bool leSimNao(const String& s, bool& out) {
  if (s.equals(F("true")) || s.equals(F("1"))) { out = true; return true; }
  if (s.equals(F("false")) || s.equals(F("0"))) { out = false; return true; }
  return false;
}

/** Número inteiro (com sinal opcional) em 'out'; false se o texto não é um inteiro. */
bool leInteiro(const String& s, long& out) {
  if (s.length() == 0 || s.length() > 7) return false;
  for (size_t k = 0; k < s.length(); k++) {
    if (!(isDigit(s[k]) || (k == 0 && s[k] == '-'))) return false;
  }
  out = s.toInt();
  return true;
}

/** Lê o campo 'nome' do arquivo em 's' (sem espaços nas pontas); false se ele não veio. */
bool Importacao::pega(const __FlashStringHelper* nome) {
  int i = b->busca(nome);
  if (i < 0) return false;
  s = b->v[i];
  s.trim();
  return true;
}

/** Anota um campo com valor inválido (o ESP mantém o valor atual dele). */
void Importacao::ignora(const __FlashStringHelper* nome) {
  if (ignorados.length()) ignorados += F(", ");
  ignorados += nome;
}

/** Liga/desliga: aplica em 'var' se o campo veio como true/false; senão, ignora. */
void Importacao::simNao(const __FlashStringHelper* nome, bool& var) {
  if (!pega(nome)) return;
  bool v;
  if (leSimNao(s, v)) { var = v; aplicados++; } else ignora(nome);
}

/** Importa a chave do OpenWeatherMap (em branco = mantém a atual), a cidade e o idioma. */
void importaClima(Importacao& imp) {
  String& s = imp.s;
  if (imp.pega(F("owmChave")) && s.length()) {
    bool ok = s.length() >= 20 && s.length() <= 40;
    for (size_t k = 0; k < s.length(); k++) if (!isAlphaNumeric(s[k])) ok = false;
    if (ok) { OPEN_WEATHER_MAP_APP_ID = s; imp.aplicados++; } else imp.ignora(F("owmChave"));
  }

  // Cidade: ID e/ou coordenadas, com as regras da /config (precisa de um dos dois)
  bool temId = imp.pega(F("cidadeId"));
  String id = temId ? s : String("");
  String latS = imp.pega(F("lat")) ? s : String("");
  String lonS = imp.pega(F("lon")) ? s : String("");
  if (temId || latS.length() || lonS.length()) {
    bool idOk = id.length() <= 10;
    for (size_t k = 0; k < id.length(); k++) if (!isDigit(id[k])) idOk = false;
    float lat = 0, lon = 0;
    int8_t okLat = parseCoord(latS, lat, -90, 90);
    int8_t okLon = parseCoord(lonS, lon, -180, 180);
    bool coordValida = okLat == 1 && okLon == 1;
    bool coordErrada = okLat < 0 || okLon < 0 || (okLat == 0) != (okLon == 0);
    if (!idOk || coordErrada || (id.length() == 0 && !coordValida)) imp.ignora(F("cidade"));
    else {
      OPEN_WEATHER_MAP_LOCATION_ID = id;
      coordOk = coordValida;             // sem coordenadas: o ESP descobre pelo ID na 1ª consulta
      if (coordValida) { cidadeLat = lat; cidadeLon = lon; }
      imp.aplicados++;
    }
  }

  if (imp.pega(F("idioma"))) {
    bool ok = false;
    for (uint8_t k = 0; k < LANG_COUNT; k++) if (s.equals(LANG_CODES[k])) ok = true;
    if (ok) { OPEN_WEATHER_MAP_LANGUAGE = s; imp.aplicados++; } else imp.ignora(F("idioma"));
  }
}

/** Importa o fuso horário (só os da lista da /config) e o horário de verão. */
void importaHora(Importacao& imp) {
  long n;
  if (imp.pega(F("fuso"))) {
    bool ok = false;
    if (leInteiro(imp.s, n)) for (uint8_t k = 0; k < FUSO_COUNT; k++) if (FUSOS[k].min == n) ok = true;
    if (ok) { fusoMin = n; imp.aplicados++; } else imp.ignora(F("fuso"));
  }
  imp.simNao(F("verao"), horarioVerao);
}

/** Importa os ajustes do display: tela ligada, telas, tempos, brilho, noite e horário automático. */
void importaDisplay(Importacao& imp) {
  String& s = imp.s;
  long n;
  imp.simNao(F("telaLigada"), displayLigadoSalvo);
  displayLigado = displayLigadoSalvo;
  if (imp.pega(F("telas"))) {
    if (leInteiro(s, n) && n >= 1 && n <= 0x7F) { telasMask = n; imp.aplicados++; } else imp.ignora(F("telas"));
  }
  if (imp.pega(F("telaSeg"))) {
    if (leInteiro(s, n) && n >= 5 && n <= 30 && n % 5 == 0) { telaSeg = n; imp.aplicados++; } else imp.ignora(F("telaSeg"));
  }
  if (imp.pega(F("telaMin"))) {
    if (leInteiro(s, n) && (n == 0 || n == 1 || n == 5 || n == 10 || n == 15 || n == 30 || n == 60)) { telaTimeoutMin = n; imp.aplicados++; }
    else imp.ignora(F("telaMin"));
  }
  if (imp.pega(F("brilho"))) {
    if (leInteiro(s, n) && brilhoValido(n)) { brilhoPct = n; imp.aplicados++; } else imp.ignora(F("brilho"));
  }
  if (imp.pega(F("brilhoNoite"))) {
    if (leInteiro(s, n) && brilhoValido(n)) { brilhoNoitePct = n; imp.aplicados++; } else imp.ignora(F("brilhoNoite"));
  }
  imp.simNao(F("noiteDiminui"), noiteDiminui);
  imp.simNao(F("noitePeloSol"), noitePeloSol);

  // Horário automático: liga/desliga antes dos horários (com a agenda ligada, eles não podem ser iguais)
  imp.simNao(F("agenda"), agendaAtiva);
  bool temLiga = imp.pega(F("agendaLiga"));
  int liga = temLiga ? parseHora(s) : -1;
  bool temDesliga = imp.pega(F("agendaDesliga"));
  int desliga = temDesliga ? parseHora(s) : -1;
  if (temLiga || temDesliga) {
    if (liga >= 0 && desliga >= 0 && !(agendaAtiva && liga == desliga)) { agendaLiga = liga; agendaDesliga = desliga; imp.aplicados++; }
    else imp.ignora(F("agenda (horários)"));
  }
}

/** Importa bateria, sensor de energia, economia na bateria e LED de status. */
void importaEnergia(Importacao& imp) {
  long n;
  imp.simNao(F("bateria"), bateriaAtiva);
  imp.simNao(F("energia"), energiaAtiva);
  imp.simNao(F("eco"), ecoBateria);
  if (imp.pega(F("ecoIntervalo"))) {
    if (leInteiro(imp.s, n) && ecoIntervaloValido(n)) { ecoIntervaloMin = n; imp.aplicados++; } else imp.ignora(F("ecoIntervalo"));
  }

  bool led = ledStatusAtivo;             // ledStatusAtivo é volatile (usado no Ticker): passa por uma cópia
  imp.simNao(F("led"), led);
  ledStatusAtivo = led;
  imp.simNao(F("ledAgenda"), ledSegueAgenda);
}

/**
 * Importa a URL do painel (em branco desliga o envio, como na /config) e a DEVICE_KEY (em branco
 * mantém a atual). O ID do dispositivo nunca vem do arquivo: apiDeviceId continua o deste ESP.
 */
void importaPainel(Importacao& imp) {
  String& s = imp.s;
  if (imp.pega(F("painelUrl"))) {
    bool ok = s.length() == 0 || ((s.startsWith("http://") || s.startsWith("https://")) && s.length() <= 120);
    for (size_t k = 0; k < s.length(); k++) if (s[k] < 33 || s[k] > 126) ok = false;
    if (ok) { apiUrl = s; imp.aplicados++; } else imp.ignora(F("painelUrl"));
  }
  if (imp.pega(F("painelChave")) && s.length()) {
    bool ok = s.length() >= 8 && s.length() <= 64;
    for (size_t k = 0; k < s.length(); k++) { char c = s[k]; if (c < 33 || c > 126 || c == '"' || c == '\\') ok = false; }
    if (ok) { deviceKey = s; imp.aplicados++; } else imp.ignora(F("painelChave"));
  }
}

/**
 * POST /importar: recebe o arquivo de backup (campo 'cfg', lido pelo navegador) e aplica cada ajuste
 * com as mesmas regras da /config. Campo ausente = mantém o atual; inválido = ignorado (e listado na
 * resposta). O ID do dispositivo nunca é importado: este ESP mantém o dele (ou o padrão esp8266-<chip>).
 * Chaves em branco no arquivo também mantêm as atuais. No fim, grava tudo e reinicia.
 */
void handleImportar() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)

  Importacao imp;
  imp.b = new LeitorBackup();            // ~1 KB: no heap, não na pilha
  {
    String corpo = server.arg(F("cfg"));
    JsonStreamingParser parser;
    parser.setListener(imp.b);
    for (size_t k = 0; k < corpo.length(); k++) parser.parse(corpo[k]);
  }

  int fi = imp.b->busca(F("formato"));
  if (fi < 0 || imp.b->v[fi].toInt() != 1) {
    delete imp.b;
    server.send_P(400, PSTR("text/html; charset=utf-8"),
                PSTR("<meta charset=\"utf-8\"><body style=\"font-family:system-ui,sans-serif;background:#07101e;color:#fff;padding:24px\">"
                "<p>⚠️ Arquivo inválido: não é um backup da estação (ou é de uma versão mais nova). Nada foi alterado.</p>"
                "<p><a style=\"color:#81d4fa\" href=\"/config?s=sistema\">‹ Voltar</a></p></body>"));
    return;
  }

  importaClima(imp);
  importaHora(imp);
  importaDisplay(imp);
  importaEnergia(imp);
  importaPainel(imp);
  delete imp.b;

  salvaConfig();
  String h;
  h.reserve(700);
  h = F("<meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"15;url=/config\">"
        "<body style=\"font-family:system-ui,sans-serif;background:#07101e;color:#fff;padding:24px;line-height:1.5\">"
        "<p>✅ Configurações importadas: ") + String(imp.aplicados) + (imp.aplicados == 1 ? F(" ajuste aplicado.") : F(" ajustes aplicados.")) + F("</p>");
  if (imp.ignorados.length()) h += F("<p>⚠️ Ignorados (valor inválido, o atual foi mantido): ") + htmlEsc(imp.ignorados) + F("</p>");
  h += F("<p>O ID do dispositivo deste ESP foi mantido. Reiniciando... A página volta sozinha em uns 15 s.</p></body>");
  server.send(200, "text/html; charset=utf-8", h);
  Serial.println(F("Backup importado: ") + String(imp.aplicados) + F(" ajustes; ignorados: ") + imp.ignorados);
  delay(500);
  ESP.restart();
}
