/*
 * REGISTRO DE EVENTOS (/config → Registro)
 *
 * Guarda na RAM os últimos eventos (ligar, WiFi, clima, envio ao painel, energia, bateria, sensor,
 * login e ajustes), num anel fixo de LOG_BYTES (EstacaoConfig.h). Ao reiniciar, começa vazio.
 * ESP32: igual.
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

#if LOG_WEB

/**
 * Registra um evento: guarda a hora (da internet, ou o tempo desde que ligou) e até 43 bytes do texto.
 * Quando o anel enche, o mais antigo sai. Não escreve no Serial (cada evento já tem a sua mensagem lá).
 */
void logEvento(const String& msg) {
  LogEntrada& e = logBuf[logHead];
  time_t agora = time(nullptr);
  e.quando = (agora > 100000) ? (int32_t)agora : -(int32_t)(millis() / 1000UL) - 1;
  size_t n = msg.length();
  if (n > sizeof(e.txt) - 1) {
    n = sizeof(e.txt) - 1;
    while (n > 0 && ((uint8_t)msg[n] & 0xC0) == 0x80) n--;   // não corta uma letra acentuada no meio
  }
  memcpy(e.txt, msg.c_str(), n);
  e.txt[n] = 0;
  logHead = (logHead + 1) % LOG_LINHAS;
  if (logCount < LOG_LINHAS) logCount++;
  logTotal++;
  logUltimoMs = millis();
}

/** Registra o resultado de um ajuste da /config (o texto da mensagem verde ou laranja, sem o ícone). */
void logAjuste(const char* r) {
  if (!strncmp_P(r, PSTR("envio"), 5)) return;   // o envio ao painel já tem o seu próprio evento
  bool erro = false;
  String m = msgConfig(String(r), erro);
  if (m.length() == 0) return;
  int sp = m.indexOf(' ');
  if (sp > 0 && sp <= 7 && (uint8_t)m[0] >= 0xE2) m.remove(0, sp + 1);   // tira o ✅ / ⚠️ do começo
  logEvento((erro ? F("Recusado: ") : F("Ajuste: ")) + m);
}

/**
 * Nível de um evento pelo texto (as mensagens são todas deste firmware): 'E' = erro, 'A' = aviso,
 * 'O' = ok, 'I' = informação. Mostrado como etiqueta colorida na página e no arquivo baixado.
 */
char logNivel(const char* txt) {
  if (!strncmp_P(txt, PSTR("Painel: "), 8)) return strncmp_P(txt + 8, PSTR("HTTP 2"), 6) ? 'E' : 'O';
  if (!strncmp_P(txt, PSTR("Ligado"), 6))   // reinício depois de travar (Exception ou Watchdog)
    return (strstr_P(txt, PSTR("Exception")) || strstr_P(txt, PSTR("Watchdog"))) ? 'A' : 'I';
  if (!strncmp_P(txt, PSTR("Recusado"), 8) || strstr_P(txt, PSTR("falhou"))) return 'E';
  if (strstr_P(txt, PSTR("rodando na bateria")) || strstr_P(txt, PSTR("baixa")) ||
      strstr_P(txt, PSTR("falta ")) || strstr_P(txt, PSTR("sem rede"))) return 'A';
  if (strstr_P(txt, PSTR("falha")) || strstr_P(txt, PSTR("caiu")) || strstr_P(txt, PSTR("errada")) ||
      strstr_P(txt, PSTR("Recusado")) || strstr_P(txt, PSTR("crítica"))) return 'E';
  if (strstr_P(txt, PSTR("voltou")) || strstr_P(txt, PSTR("Login na")) || strstr_P(txt, PSTR("Ajuste:")) ||
      strstr_P(txt, PSTR("normal"))) return 'O';
  return 'I';
}

/** Hora de um evento: 'HH:MM:SS' (hoje), 'DD/MM HH:MM:SS' (outro dia) ou '+HH:MM:SS' desde que ligou. */
String logHora(int32_t quando) {
  char b[24];
  if (quando > 0) {
    time_t t = quando;
    struct tm ev = *localtime(&t);
    time_t agora = time(nullptr);
    struct tm hoje = *localtime(&agora);
    if (ev.tm_yday == hoje.tm_yday && ev.tm_year == hoje.tm_year) {
      snprintf(b, sizeof(b), "%02d:%02d:%02d", (uint8_t)ev.tm_hour, (uint8_t)ev.tm_min, (uint8_t)ev.tm_sec);
    } else {
      snprintf(b, sizeof(b), "%02d/%02d %02d:%02d:%02d", (uint8_t)ev.tm_mday, (uint8_t)(ev.tm_mon + 1),
               (uint8_t)ev.tm_hour, (uint8_t)ev.tm_min, (uint8_t)ev.tm_sec);
    }
  } else {
    uint32_t s = (uint32_t)(-(quando + 1));      // segundos desde que ligou (antes da hora da internet)
    snprintf(b, sizeof(b), "+%02lu:%02d:%02d", (unsigned long)(s / 3600UL), (uint8_t)(s / 60UL % 60UL), (uint8_t)(s % 60UL));
  }
  return String(b);
}

/**
 * GET /log.json (com login): {"total":N,"max":21,"l":[["14:32:05","O","texto"],...]}, do mais novo para
 * o mais antigo. Enviado em partes, sem montar o JSON inteiro na RAM.
 */
void paginaLogJson() {
  if (!exigeLogin()) return;
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String j = F("{\"total\":");
  j += String(logTotal);
  j += F(",\"max\":");
  j += String(LOG_LINHAS);
  j += F(",\"l\":[");
  for (uint8_t k = 0; k < logCount; k++) {
    const LogEntrada& e = logBuf[(logHead + LOG_LINHAS - 1 - k) % LOG_LINHAS];
    if (k) j += ',';
    j += F("[\"");
    j += logHora(e.quando);
    j += F("\",\"");
    j += logNivel(e.txt);
    j += F("\",\"");
    j += jsonEscape(String(e.txt));
    j += F("\"]");
    if (j.length() > 400) {             // manda em pedaços pequenos
      server.sendContent(j);
      j = "";
    }
  }
  j += F("]}");
  server.sendContent(j);
  server.sendContent("");               // encerra a resposta (chunked)
}

/** POST /log/limpar (com login): esvazia o registro e volta para a página dele. */
void handleLogLimpar() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;
  logHead = 0;
  logCount = 0;
  redirecionaConfig("log_limpo");       // o 1º evento depois de limpar é o próprio "Registro apagado."
}

/** Seção 'Registro' da /config: botões, lista e o script que busca /log.json a cada 2 s. */
void cfgLog(const String& r) {
  cfgCabecalho(F("Registro"), F("/config"), F("Configurações"), r);
  server.sendContent_P(LOG_HTML);
  // A lista já vem pronta do ESP (aparece mesmo se o script não rodar); o script só a atualiza
  String h = F("<div class=\"con\" id=\"lg\">");
  if (logCount == 0) h += F("<div class=\"ln\"><span class=\"vz\">Nenhum evento ainda.</span></div>");
  for (uint8_t k = 0; k < logCount; k++) {
    const LogEntrada& e = logBuf[(logHead + LOG_LINHAS - 1 - k) % LOG_LINHAS];
    char n = logNivel(e.txt);
    h += F("<div class=\"ln\"><span class=\"hr\">");
    h += logHora(e.quando);
    h += F("</span><span class=\"nv ");
    h += (n == 'E') ? F("e\">ERRO") : (n == 'A') ? F("a\">AVISO") : (n == 'O') ? F("o\">OK") : F("i\">INFO");
    h += F("</span><span class=\"tx\">");
    h += htmlEsc(String(e.txt));
    h += F("</span></div>");
    if (h.length() > 500) {
      server.sendContent(h);
      h = "";
    }
  }
  h += F("</div>");
  server.sendContent(h);
  server.sendContent_P(LOG_HTML_FIM);
  h = "";
  cfgRodape(h);
}

#endif
