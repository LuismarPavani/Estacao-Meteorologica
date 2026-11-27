/*
 * SERVIDOR WEB: UTILITÁRIOS, LOGIN DA /config E PÁGINA PRINCIPAL
 *
 * Textos e HTML, CSRF, login por cookie, página inicial, /json e /historico.
 * ESP32: quase igual (WebServer no lugar de ESP8266WebServer, SHA-256 pelo mbedTLS).
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- SERVIDOR WEB: UTILITÁRIOS DE TEXTO ---------------------------------------------------

/** Escapa caracteres especiais para colocar texto com segurança dentro do HTML. */
String htmlEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t k = 0; k < s.length(); k++) {
    char c = s[k];
    if (c == '&') o += F("&amp;");
    else if (c == '<') o += F("&lt;");
    else if (c == '>') o += F("&gt;");
    else if (c == '"') o += F("&quot;");
    else o += c;
  }
  return o;
}

/** Escapa aspas e barras para colocar texto dentro de uma string JSON. */
String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (size_t k = 0; k < s.length(); k++) {
    char c = s[k];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { o += ' '; }
    else { o += c; }
  }
  return o;
}

/** Converte minutos desde 00:00 para o formato 'HH:MM'. */
String formataHora(int minutos) {
  char b[16];   // folga para o compilador (int com sinal), evita -Wformat-truncation
  snprintf(b, sizeof(b), "%02d:%02d", minutos / 60, minutos % 60);
  return String(b);
}

/** Converte 'HH:MM' em minutos desde 00:00 (ou -1 se for inválido). */
int parseHora(const String& t) {
  int c = t.indexOf(':');
  if (c < 1 || !isDigit(t[0])) return -1;
  int h = t.substring(0, c).toInt();
  int m = t.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

/** Tira a porta e passa para minúsculas ('192.168.0.50:80' -> '192.168.0.50'). */
String semPorta(String h) {
  int p = h.indexOf(':');
  if (p >= 0) h = h.substring(0, p);
  h.toLowerCase();
  return h;
}

/** Devolve 'host:porta' de um endereço 'http://host:porta/caminho' (ou '' se não for um endereço). */
String hostDaUrl(const String& url) {
  int i = url.indexOf("://");
  if (i < 0) return "";
  String r = url.substring(i + 3);
  int b = r.indexOf('/');
  if (b >= 0) r = r.substring(0, b);
  return r;
}

/**
 * O endereço digitado no navegador (cabeçalho Host) é deste ESP? Aceita o IP e o nome na rede
 * (ex.: estacaometeorologica-abc123, estacaometeorologica-abc123.local). Barra o ataque de DNS rebinding, em que um
 * site usa um domínio próprio apontando para o IP do ESP.
 */
bool hostEhDesteEsp(const String& host) {
  if (host.length() == 0) return true;  // sem Host: não é um navegador (que sempre envia)
  if (host == WiFi.localIP().toString()) return true;
  String nome = String(WiFi.hostname());
  nome.toLowerCase();
  return host == nome || host.startsWith(nome + F("."));
}

/**
 * Proteção contra CSRF: chamada no início de cada POST. Recusa (403) pedidos feitos por outro site
 * aberto no navegador, que poderia apagar ajustes ou trocar a URL do painel para roubar a
 * DEVICE_KEY. Aceita só quando o Origin (ou, na falta dele, o Referer) é a própria página do ESP.
 * Pedidos sem Origin e sem Referer (curl, scripts) são aceitos: não vêm de um navegador.
 */
bool requisicaoSegura() {
  String host = server.hostHeader();
  bool ok = hostEhDesteEsp(semPorta(host));
  if (ok) {
    String origem = server.header(F("Origin"));
    String ref = server.header(F("Referer"));
    if (origem.length() > 0) ok = origem.startsWith("http://") && hostDaUrl(origem).equalsIgnoreCase(host);   // 'null' também é recusado
    else if (ref.length() > 0) ok = ref.startsWith("http://") && hostDaUrl(ref).equalsIgnoreCase(host);
  }
  if (!ok) {
    Serial.println(F("POST recusado (veio de outro site): ") + server.uri());
    server.send(403, "text/html; charset=utf-8",
                PSTR("<meta charset=\"utf-8\"><p>Pedido recusado: ele não veio da página desta estação. "
                "Abra a página pelo endereço do ESP e tente de novo.</p>"));
  }
  return ok;
}

/** Redireciona (303) para a seção certa da /config, com o código da mensagem de resultado. */
void redirecionaConfig(const char* r) {
  // Cada código de resultado volta para a seção do formulário que o gerou
  static const char DISPLAY_COD[] PROGMEM = "|disp|tempo|tempo_erro|tempoligada|tempoligada_erro|brilho|brilho_erro|telas|telas_erro|agenda|hora|horaigual|";
  static const char PAINEL_COD[] PROGMEM = "|api|apiurl|apiid|apikey|envio_ok|envio_erro|";
  static const char HORA_COD[] PROGMEM = "|fuso|fuso_ok|";
  static const char BATERIA_COD[] PROGMEM = "|bat|eco|";
  String chave = String('|') + r + '|';
  const __FlashStringHelper* secao = F("clima");   // ok, chave, local, local_vazio, coord, idioma
  if (String(FPSTR(DISPLAY_COD)).indexOf(chave) >= 0) secao = F("display");
  else if (String(FPSTR(PAINEL_COD)).indexOf(chave) >= 0) secao = F("painel");
  else if (String(FPSTR(HORA_COD)).indexOf(chave) >= 0) secao = F("hora");
  else if (String(FPSTR(BATERIA_COD)).indexOf(chave) >= 0) secao = F("bateria");
  else if (!strcmp_P(r, PSTR("led"))) secao = F("led");
  else if (!strcmp_P(r, PSTR("log_limpo"))) secao = F("log");
  logAjuste(r);                         // registro de eventos: "Ajuste: Brilho salvo." etc.
  server.sendHeader(F("Location"), String(F("/config?s=")) + secao + F("&r=") + r);
  server.send(303, "text/plain", "");
}

/** Formata uma quantidade de bytes como KB ou MB. */
String fmtBytes(uint32_t b) {
  if (b >= 1048576UL) return String(b / 1048576.0, 1) + F(" MB");
  return String(b / 1024UL) + F(" KB");
}

/** Tempo ligado: '12min 5s' (menos de 1 h), '5h 32min' (menos de 1 dia) ou '2 d 3 h'. */
String formataUptime(unsigned long s) {
  unsigned long d = s / 86400UL;
  unsigned long h = (s % 86400UL) / 3600UL;
  unsigned long m = (s % 3600UL) / 60UL;
  if (d) return String(d) + F(" d ") + String(h) + F(" h");
  if (h) return String(h) + F("h ") + String(m) + F("min");
  return String(m) + F("min ") + String(s % 60UL) + F("s");
}

/** Monta uma linha de informação da /config (rótulo à esquerda, valor à direita, como nos Ajustes do iPhone). */
String linhaInfoP(const __FlashStringHelper* rotulo, const String& valor) {
  return F("<div class=\"it\"><span class=\"tx\">") + String(rotulo) + F("</span><span class=\"val\">") + valor + F("</span></div>");
}

/** Codifica um texto para ir numa URL (espaços, acentos e símbolos viram %XX; UTF-8 byte a byte). */
String urlEncode(const String& s) {
  String o;
  o.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t k = 0; k < s.length(); k++) {
    uint8_t c = (uint8_t)s[k];
    if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

/**
 * Lê uma coordenada em graus decimais ('-22.9056' ou '-22,9056') e confere a faixa. Devolve 0 se o
 * texto está vazio, 1 se é válida (valor em 'out') e -1 se é inválida.
 */
int8_t parseCoord(String s, float& out, float minimo, float maximo) {
  s.trim();
  if (s.length() == 0) return 0;
  s.replace(',', '.');
  bool temDigito = false;
  for (size_t k = 0; k < s.length(); k++) {
    char c = s[k];
    if (isDigit(c)) temDigito = true;
    else if (!(c == '.' || (k == 0 && (c == '-' || c == '+')))) return -1;
  }
  if (!temDigito || s.length() > 14) return -1;
  out = s.toFloat();
  return (out >= minimo && out <= maximo) ? 1 : -1;
}

// ---- SERVIDOR WEB: LOGIN DA /config -------------------------------------------------------
// A senha (CONFIG_SENHA) é pedida uma vez por navegador. Depois do login, o navegador guarda um
// cookie com SHA-256(sal + senha) por LOGIN_DIAS dias. O sal é aleatório e fica na EEPROM, então
// o login sobrevive a reinícios; trocar a senha ou o sal ("Desconectar todos") invalida os cookies.
// A página principal, /json, /historico e o botão de ligar/apagar a tela continuam sem senha.

/** true se há senha configurada (CONFIG_SENHA não vazia). */
bool loginAtivo() {
  return strlen(CONFIG_SENHA) > 0;
}

/** Valor do cookie de login: SHA-256(sal + CONFIG_SENHA) em hexadecimal (64 caracteres). */
String tokenSessao() {
  br_sha256_context ctx;
  uint8_t out[32];
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, sessaoSal, sizeof(sessaoSal));
  br_sha256_update(&ctx, CONFIG_SENHA, strlen(CONFIG_SENHA));
  br_sha256_out(&ctx, out);
  char hex[65];
  for (uint8_t k = 0; k < 32; k++) snprintf(hex + k * 2, 3, "%02x", out[k]);
  return String(hex);
}

/** Compara dois textos sempre no mesmo tempo (não revela, pela demora, quantos caracteres batem). */
bool comparaSeguro(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t dif = 0;
  for (size_t k = 0; k < a.length(); k++) dif |= (uint8_t)(a[k] ^ b[k]);
  return dif == 0;
}

/** Grava só o sal do login na EEPROM (sem mexer nos outros ajustes nem marcá-los como salvos). */
void gravaSalSessao() {
  EEPROM.put(offsetof(Config, sessaoOk), (uint8_t)0xA5);
  EEPROM.put(offsetof(Config, sessaoSal), sessaoSal);
  EEPROM.commit();
}

/** Sorteia um sal novo (gerador do chip, aleatório de verdade com o WiFi ligado) e grava. */
void geraSalSessao() {
  for (uint8_t k = 0; k < sizeof(sessaoSal); k += 4) {
    uint32_t r = ESP.random();
    memcpy(sessaoSal + k, &r, 4);
  }
  sessaoSalOk = true;
  gravaSalSessao();
}

/** true se o navegador mandou o cookie de login válido (ou se o login está desligado). */
bool logado() {
  if (!loginAtivo()) return true;
  if (!sessaoSalOk) return false;       // ninguém fez login ainda
  String c = server.header(F("Cookie"));
  int i = c.indexOf("ws_sessao=");
  if (i < 0) return false;
  String v = c.substring(i + 10);
  int f = v.indexOf(';');
  if (f >= 0) v = v.substring(0, f);
  v.trim();
  return comparaSeguro(v, tokenSessao());
}

/** Chamada no início das páginas e ações protegidas: sem login, manda para /login e devolve false. */
bool exigeLogin() {
  if (logado()) return true;
  server.sendHeader(F("Location"), F("/login"));
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(303, "text/plain", "");
  return false;
}

/** GET /login: página com o campo de senha (se já estiver logado, vai direto para a /config). */
void paginaLogin() {
  if (logado()) {
    server.sendHeader(F("Location"), F("/config"));
    server.send(303, "text/plain", "");
    return;
  }
  String r = server.arg(F("r"));
  String msg = "";
  if (r.equals(F("erro"))) msg = F("⚠️ Senha incorreta.");
  else if (r.equals(F("bloq"))) msg = F("⚠️ Muitas tentativas erradas. Aguarde 1 minuto e tente de novo.");
  else if (r.equals(F("sair"))) msg = F("✅ Você saiu da configuração neste navegador.");
  else if (r.equals(F("todos"))) msg = F("✅ Todos os navegadores foram desconectados.");

  String h;
  h.reserve(1800);
  h = F("<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Entrar - Configurações</title>"
      "<style>html{min-height:100%;background:radial-gradient(1100px 460px at 85% -8%,rgba(56,209,255,.16),transparent),"
      "linear-gradient(#0b182c,#07101e) fixed}"
      "body{font-family:system-ui,sans-serif;max-width:420px;margin:2em auto;padding:0 1em;color:#fff}"
      "label{display:block;margin-top:1em;font-weight:600}"
      "input{width:100%;box-sizing:border-box;padding:.7em;margin-top:.3em;border:0;border-radius:10px;font-size:1rem}"
      "button{padding:.7em 1.2em;margin:1em 0;border:0;border-radius:10px;font-size:1rem;background:#81d4fa;color:#0f2027;font-weight:600}"
      ".mut{color:#a9c1cf;font-size:.85rem}a{color:#81d4fa}</style></head><body>"
      "<p><a href=\"/\">&larr; Voltar</a></p><h2>Configurações</h2>");
  if (msg.length()) h += F("<p>") + msg + F("</p>");
  h += F("<form method=\"POST\" action=\"/login\">"
       "<input type=\"text\" name=\"u\" value=\"admin\" autocomplete=\"username\" style=\"display:none\">"   // ajuda o gerenciador de senhas
       "<label>Senha</label>"
       "<input name=\"senha\" type=\"password\" autocomplete=\"current-password\" autofocus required>"
       "<button type=\"submit\">Entrar</button></form>"
       "<p class=\"mut\">Este navegador fica conectado por ") + String(LOGIN_DIAS) + F(" dias. "
       "Esqueceu a senha? Ela é a CONFIG_SENHA do EstacaoConfig.h usado para gravar o firmware.</p>"
       "</body></html>");
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(200, "text/html; charset=utf-8", h);
}

/** POST /login: confere a senha e grava o cookie por LOGIN_DIAS dias. 5 erros seguidos = 1 min de espera. */
void handleLogin() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  unsigned long ms = millis();
  if (loginFalhas >= 5 && (long)(ms - loginLiberaEm) < 0) {
    server.sendHeader(F("Location"), F("/login?r=bloq"));
    server.send(303, "text/plain", "");
    return;
  }
  if (!loginAtivo() || !comparaSeguro(server.arg(F("senha")), String(CONFIG_SENHA))) {
    if (loginFalhas < 255) loginFalhas++;
    if (loginFalhas >= 5) loginLiberaEm = ms + 60000UL;
    Serial.println(F("Login da /config: senha incorreta"));
    logEvento(F("Login: senha errada (") + server.client().remoteIP().toString() + F(")"));
    server.sendHeader(F("Location"), F("/login?r=erro"));
    server.send(303, "text/plain", "");
    return;
  }
  loginFalhas = 0;
  logEvento(F("Login na /config (") + server.client().remoteIP().toString() + F(")"));
  if (!sessaoSalOk) geraSalSessao();    // 1º login: sorteia o sal (com o WiFi ligado)
  server.sendHeader(F("Set-Cookie"), F("ws_sessao=") + tokenSessao() + F("; Max-Age=") + String((unsigned long)LOGIN_DIAS * 86400UL) +
                                  F("; Path=/; HttpOnly; SameSite=Lax"));
  server.sendHeader(F("Location"), F("/config"));
  server.send(303, "text/plain", "");
}

/** POST /sair: sai da configuração só neste navegador (apaga o cookie). */
void handleSair() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  server.sendHeader(F("Set-Cookie"), F("ws_sessao=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax"));
  server.sendHeader(F("Location"), F("/login?r=sair"));
  server.send(303, "text/plain", "");
}

/** POST /sair-todos: troca o sal, o que desconecta todos os navegadores (inclusive este). */
void handleSairTodos() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;            // precisa estar logado (senha da /config)
  geraSalSessao();
  server.sendHeader(F("Set-Cookie"), F("ws_sessao=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax"));
  server.sendHeader(F("Location"), F("/login?r=todos"));
  server.send(303, "text/plain", "");
}

// ---- SERVIDOR WEB: PÁGINA PRINCIPAL E DADOS EM JSON ---------------------------------------

/** GET /: envia a página principal (HTML fixo guardado na flash; os números chegam por fetch). */
void paginaInicial() {
  server.send_P(200, "text/html; charset=utf-8", PAGE_INDEX);
}

/** GET /json: dados atuais (interno, externo, previsão, WiFi, bateria e estado da tela) em JSON. */
void paginaJson() {
  float windKmh = IS_METRIC ? currentWeather.windSpeed * 3.6 : currentWeather.windSpeed * 1.609;
  String j;
  j.reserve(800);
  j += F("{\"unid\":\"");
  j += IS_METRIC ? F("°C") : F("°F");
  j += F("\",\"erro\":");
  j += weatherOk ? F("false") : F("true");
  j += F(",\"display\":");
  j += displayLigado ? F("true") : F("false");
  j += F(",\"brilho\":") + String(brilhoAgora());           // % de brilho que vale agora (dia ou noturno)
  j += F(",\"brilhoNoite\":");
  j += noiteAgora() ? F("true") : F("false");
  j += F(",\"bateria\":");
  if (bateriaAtiva) j += F("{\"v\":") + String(bateriaV, 2) + F(",\"pct\":") + String(bateriaPct) + F("}");
  else j += F("null");
  j += F(",\"energia\":");                 // true = energia externa (5 V) conectada; false = bateria; null = sem sensor
  if (energiaAtiva && PIN_ENERGIA >= 0) j += energiaExterna ? F("true") : F("false");
  else j += F("null");
  j += F(",\"interno\":{");
  if (sensorOk) {
    j += F("\"temp\":") + String(localTemp, 1) + F(",\"umid\":") + String(localHum, 1);
  } else {
    j += F("\"temp\":null,\"umid\":null");
  }
  j += F("},\"externo\":{");
  j += F("\"cidade\":\"") + jsonEscape(currentWeather.cityName) + F("\",");
  j += F("\"desc\":\"") + jsonEscape(currentWeather.description) + F("\",");
  j += F("\"icone\":\"") + jsonEscape(currentWeather.icon) + F("\",");
  j += F("\"temp\":") + String(currentWeather.temp, 1) + F(",");
  j += F("\"min\":") + String(currentWeather.tempMin, 1) + F(",");
  j += F("\"max\":") + String(currentWeather.tempMax, 1) + F(",");
  j += F("\"umid\":") + String(currentWeather.humidity) + F(",");
  j += F("\"pressao\":") + String(currentWeather.pressure) + F(",");
  j += F("\"vento\":") + String(windKmh, 0) + F(",");
  j += F("\"ventoDir\":") + String(currentWeather.windDeg, 0) + F(",");
  j += F("\"visib\":") + String(currentWeather.visibility / 1000.0, 1) + F(",");
  j += F("\"nuvens\":") + String(currentWeather.clouds) + F(",");
  j += F("\"nascer\":") + String((unsigned long)currentWeather.sunrise) + F(",");
  j += F("\"por\":") + String((unsigned long)currentWeather.sunset);
  j += F("},\"previsao\":[");
  for (uint8_t k = 0; k < MAX_FORECASTS; k++) {
    if (k) j += F(",");
    j += F("{\"dia\":") + String((unsigned long)forecasts[k].observationTime) + F(",");
    j += F("\"icone\":\"") + jsonEscape(forecasts[k].icon) + F("\",");
    j += F("\"temp\":") + String(forecasts[k].temp, 1) + F("}");
  }
  j += F("],\"prox24\":");                  // próximas 24 h (null até a 1ª previsão)
  if (prox24Ok) {
    j += F("{\"min\":") + String(prox24Min, 1) + F(",\"max\":") + String(prox24Max, 1) +
         F(",\"chuva\":") + String(prox24Chuva, 1) + F(",\"sens\":") + String(prox24Sens, 1) + F("}");
  } else j += F("null");
  j += F(",\"lua\":");                      // fase da Lua (null até a hora sincronizar); nascer/pôr = 0 sem coordenadas
  if (luaOk) {
    j += F("{\"fase\":") + String(luaFase) + F(",\"nome\":\"") + String(nomeFaseLua(luaFase)) + F("\",\"ilum\":") +
         String((int)(luaIlum * 100 + 0.5f)) + F(",\"nasce\":") + String(astroOk ? (unsigned long)luaNasce : 0UL) +
         F(",\"poe\":") + String(astroOk ? (unsigned long)luaPoe : 0UL) + F("}");
  } else j += F("null");
  j += F(",\"rssi\":") + String(WiFi.RSSI()) + F(",\"rssiPct\":") + String(qualidadeSinal(WiFi.RSSI())) + F("}");
  server.send(200, "application/json", j);
}

/** GET /historico: histórico interno (do mais antigo para o mais novo) em JSON. */
void paginaHistorico() {
  String j;
  j.reserve(64 + histCount * 12);
  long prox = (long)(nextHist - millis()) / 1000;
  if (prox < 0) prox = 0;
  j += F("{\"ok\":");
  j += sensorOk ? F("true") : F("false");
  j += F(",\"sensor\":\"" SENSOR_NOME "\",\"pino\":\"") + nomePino(DHTPIN) + F("\"");   // usados na mensagem de erro da página
  j += F(",\"proxima\":") + String(prox);
  j += F(",\"intervalo\":") + String(HIST_INTERVAL_MS / 1000UL) + F(",\"temp\":[");
  uint8_t start = (histCount < HIST_SIZE) ? 0 : histHead;
  for (uint8_t k = 0; k < histCount; k++) {
    if (k) j += F(",");
    j += String(histTemp[(start + k) % HIST_SIZE] / 10.0, 1);
  }
  j += F("],\"umid\":[");
  for (uint8_t k = 0; k < histCount; k++) {
    if (k) j += F(",");
    j += String(histHum[(start + k) % HIST_SIZE] / 10.0, 1);
  }
  j += F("]}");
  server.send(200, "application/json", j);
}

// ---- SERVIDOR WEB: PÁGINA /sobre ------------------------------------------------------------

/**
 * GET /sobre: versão, placa e sensor deste aparelho, um resumo do projeto e os links do GitHub e do
 * painel. Pública como a página principal (não mostra nada sensível). O texto fixo vem da flash.
 * O "‹ voltar" leva de volta para onde a pessoa estava: ?de=config volta à /config; sem ele, ao início.
 */
void paginaSobre() {
  if (server.arg(F("de")).equals(F("config"))) cfgCabecalho(F("Sobre"), F("/config"), F("Configurações"), String());
  else cfgCabecalho(F("Sobre"), F("/"), F("Início"), String());
#if defined(PLACA_OLED_INTEGRADO)
  const __FlashStringHelper* placa = F("NodeMCU com OLED integrado");
#else
  const __FlashStringHelper* placa = F("ESP8266 com OLED externo");
#endif
  String h = F("<div class=\"cap\">Este aparelho</div><div class=\"grp\">");
  h += linhaInfo("Firmware", F("v" FIRMWARE_VERSAO));
  h += linhaInfo("Placa", placa);
  h += linhaInfo("Sensor", F(SENSOR_NOME));
  h += linhaInfo("Ligado há", tempoCurto(millis() / 1000UL));
  h += F("</div>");
  server.sendContent(h);
  server.sendContent_P(SOBRE_HTML);
  h = F("<div class=\"pe\">Estação Meteorológica ESP8266 · v" FIRMWARE_VERSAO "</div>");
  cfgRodape(h);
}