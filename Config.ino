/*
 * CONFIGURAÇÃO SALVA (EEPROM) E FUSO HORÁRIO
 *
 * carregaConfig, salvaConfig, limpaConfig e aplicaFuso.
 * ESP32: igual (a EEPROM do core do ESP32 tem a mesma interface).
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- CONFIGURAÇÃO SALVA (EEPROM) E FUSO HORÁRIO -------------------------------------------

/**
 * Lê da EEPROM as configurações salvas (chave, cidade, idioma, fuso, tela, agenda, bateria, API).
 * Se nada foi salvo ainda, mantém os padrões do código.
 */
void carregaConfig() {
  Config c;
  EEPROM.get(0, c);
  if (c.sessaoOk == 0xA5) {             // sal do login: gravado à parte, vale mesmo sem o resto salvo
    memcpy(sessaoSal, c.sessaoSal, sizeof(sessaoSal));
    sessaoSalOk = true;
  }
  if (c.magic != CFG_MAGIC) return;     // nada salvo ainda: mantém os padrões do código
  c.apiKey[sizeof(c.apiKey) - 1] = 0;
  c.locationId[sizeof(c.locationId) - 1] = 0;
  c.lang[sizeof(c.lang) - 1] = 0;
  OPEN_WEATHER_MAP_APP_ID = String(c.apiKey);
  OPEN_WEATHER_MAP_LOCATION_ID = String(c.locationId);
  OPEN_WEATHER_MAP_LANGUAGE = String(c.lang);
  displayLigadoSalvo = (c.displayLigado != 0);
  displayLigado = displayLigadoSalvo;
  agendaAtiva = (c.agendaAtiva == 1);
  if (c.fusoOk == 0xA5 && c.fuso >= -720 && c.fuso <= 840) {   // só usa se foi salvo de fato
    fusoMin = c.fuso;
    horarioVerao = (c.verao == 1);
  }
  if (c.bateriaOk == 0xA5) bateriaAtiva = (c.bateria == 1);
  if (c.energiaOk == 0xA5) energiaAtiva = (c.energia == 1);
  if (c.ledOk == 0xA5) {
    ledStatusAtivo = (c.led & 1) != 0;
    ledSegueAgenda = (c.led & 2) != 0;
  }
  if (c.apiOk == 0xA5) {
    c.apiUrl[sizeof(c.apiUrl) - 1] = 0;
    c.deviceKey[sizeof(c.deviceKey) - 1] = 0;
    apiUrl = String(c.apiUrl);
    deviceKey = String(c.deviceKey);
  }
  if (c.deviceIdOk == 0xA5) {
    c.deviceId[sizeof(c.deviceId) - 1] = 0;
    apiDeviceId = String(c.deviceId);
  }
  if (c.telaSeg >= 5 && c.telaSeg <= 30 && c.telaSeg % 5 == 0) telaSeg = c.telaSeg;   // valor inválido: mantém o padrão
  if (c.telaTimeout == 0 || c.telaTimeout == 1 || c.telaTimeout == 5 || c.telaTimeout == 10 ||
      c.telaTimeout == 15 || c.telaTimeout == 30 || c.telaTimeout == 60) telaTimeoutMin = c.telaTimeout;
  if (c.brilhoOk == 0xA5) {             // firmware antigo não tinha brilho: mantém os padrões
    if (brilhoValido(c.brilho)) brilhoPct = c.brilho;
    if (brilhoValido(c.brilhoNoite)) brilhoNoitePct = c.brilhoNoite;
    noiteDiminui = (c.noiteModo == 1);
  }
  if (c.telasOk == 0xA5 && (c.telas & 0x7F) != 0) telasMask = c.telas & 0x7F;   // nenhuma tela marcada: mantém o padrão
  if (c.noiteSolOk == 0xA5) noitePeloSol = (c.noiteSol == 1);
  if (c.coordOk == 0xA5 && c.lat >= -90 && c.lat <= 90 && c.lon >= -180 && c.lon <= 180) {
    coordOk = true;
    cidadeLat = c.lat;
    cidadeLon = c.lon;
  }
  if (c.ecoOk == 0xA5) {
    ecoBateria = (c.eco == 1);
    if (ecoIntervaloValido(c.ecoIntervalo)) ecoIntervaloMin = c.ecoIntervalo;
  }
  if (c.agendaLiga < 1440 && c.agendaDesliga < 1440) {   // dados antigos/vazios são ignorados
    agendaLiga = c.agendaLiga;
    agendaDesliga = c.agendaDesliga;
  }
}

/** Grava na EEPROM todas as configurações atuais. É chamada sempre que algo é alterado pela página. */
void salvaConfig() {
  Config c;
  memset(&c, 0, sizeof(c));
  c.magic = CFG_MAGIC;
  snprintf(c.apiKey, sizeof(c.apiKey), "%s", OPEN_WEATHER_MAP_APP_ID.c_str());
  snprintf(c.locationId, sizeof(c.locationId), "%s", OPEN_WEATHER_MAP_LOCATION_ID.c_str());
  OPEN_WEATHER_MAP_LANGUAGE.toCharArray(c.lang, sizeof(c.lang));   // corta no tamanho do campo e sempre termina com '\0'
  // Grava a escolha manual, não o estado de agora: se a tela apagou pelo tempo de tela ligada ou pela
  // agenda, salvar outro ajuste não pode fazer o próximo boot começar com a tela apagada.
  c.displayLigado = displayLigadoSalvo ? 1 : 0;
  c.agendaAtiva = agendaAtiva ? 1 : 0;
  c.fusoOk = 0xA5;
  c.fuso = fusoMin;
  c.verao = horarioVerao ? 1 : 0;
  c.bateriaOk = 0xA5;
  c.bateria = bateriaAtiva ? 1 : 0;
  c.energiaOk = 0xA5;
  c.energia = energiaAtiva ? 1 : 0;
  c.ledOk = 0xA5;
  c.led = (ledStatusAtivo ? 1 : 0) | (ledSegueAgenda ? 2 : 0);
  c.apiOk = 0xA5;
  snprintf(c.apiUrl, sizeof(c.apiUrl), "%s", apiUrl.c_str());
  snprintf(c.deviceKey, sizeof(c.deviceKey), "%s", deviceKey.c_str());
  c.deviceIdOk = 0xA5;
  snprintf(c.deviceId, sizeof(c.deviceId), "%s", apiDeviceId.c_str());
  c.telaSeg = telaSeg;
  c.telaTimeout = telaTimeoutMin;
  c.agendaLiga = agendaLiga;
  c.agendaDesliga = agendaDesliga;
  c.brilhoOk = 0xA5;
  c.brilho = brilhoPct;
  c.brilhoNoite = brilhoNoitePct;
  c.noiteModo = noiteDiminui ? 1 : 0;
  c.sessaoOk = sessaoSalOk ? 0xA5 : 0;
  memcpy(c.sessaoSal, sessaoSal, sizeof(sessaoSal));
  c.telasOk = 0xA5;
  c.telas = telasMask;
  c.noiteSolOk = 0xA5;
  c.noiteSol = noitePeloSol ? 1 : 0;
  c.coordOk = coordOk ? 0xA5 : 0;
  c.lat = cidadeLat;
  c.lon = cidadeLon;
  c.ecoOk = 0xA5;
  c.eco = ecoBateria ? 1 : 0;
  c.ecoIntervalo = ecoIntervaloMin;
  EEPROM.put(0, c);
  EEPROM.commit();
}

/** Apaga a área de configurações da EEPROM: no próximo boot tudo volta ao padrão do código. */
void limpaConfig() {
  for (size_t k = 0; k < sizeof(Config); k++) EEPROM.write(k, 0xFF);
  EEPROM.commit();
}

/**
 * (Re)configura o NTP com o fuso e o horário de verão escolhidos. Pode ser chamada a qualquer
 * momento.
 */
void aplicaFuso() {
  configTime((int)fusoMin * 60, horarioVerao ? 3600 : 0, NTP_SERVIDOR_1, NTP_SERVIDOR_2, NTP_SERVIDOR_3);   // 3 servidores (EstacaoConfig.h)
  nextAstro = 0;                         // recalcula o sol e a Lua com a hora nova
}
