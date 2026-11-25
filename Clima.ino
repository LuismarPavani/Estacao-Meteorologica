/*
 * CLIMA (OpenWeatherMap)
 *
 * atualizaClima e calculaPrevisao (próximas 24 h).
 * ESP32: igual.
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- CLIMA (OpenWeatherMap) ---------------------------------------------------------------

/**
 * Consulta o OpenWeatherMap (clima atual e previsão) mostrando o progresso no OLED. No boot, se
 * faltar a chave da API, mostra um aviso com o endereço da /config. Define weatherOk e agenda a
 * próxima consulta (ou nova tentativa se falhar).
 */
void atualizaClima(OLEDDisplay *display) {
  ecoAcorda(30000UL);                    // consulta do clima sem light sleep
  readyForWeatherUpdate = false;
  static bool primeiraVez = true;        // 1ª consulta = a do boot: mostra avisos mais longos no OLED
  bool noBoot = primeiraVez;
  primeiraVez = false;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Sem WiFi, nova tentativa em 1 min"));
    nextWeatherUpdate = millis() + RETRY_INTERVAL_MS;
    return;
  }

  if (OPEN_WEATHER_MAP_APP_ID.length() == 0) {
    Serial.println(F("Sem chave da API: configure em /config"));
    if (noBoot) {                        // no boot, avisa no OLED em vez de deixar a tela preta
      display->clear();
      display->setTextAlignment(TEXT_ALIGN_CENTER);
      display->setFont(ArialMT_Plain_10);
      display->drawString(64, 2, F("Sem chave da API"));
      display->drawString(64, 18, F("Abra no navegador:"));
      display->drawString(64, 32, WiFi.localIP().toString() + F("/config"));
      display->display();
      delay(3500);
    }
    weatherOk = false;
    nextWeatherUpdate = millis() + RETRY_INTERVAL_MS;
    return;
  }

  if (!coordOk && OPEN_WEATHER_MAP_LOCATION_ID.length() == 0) {   // sem coordenadas e sem ID: não há o que consultar
    Serial.println(F("Sem cidade: informe em /config → Clima"));
    weatherOk = false;
    nextWeatherUpdate = millis() + RETRY_INTERVAL_MS;
    return;
  }

  desenhaProgresso(display, 20, F("Atualizando o clima..."));
  String cidadeAnterior = currentWeather.cityName;
  currentWeather.cityName = "";          // se continuar vazio depois, a consulta falhou
  // Por coordenadas quando já são conhecidas (a consulta por ID é obsoleta); na 1ª vez, pelo ID da cidade
  if (coordOk) currentWeatherClient.updateCurrent(&currentWeather, OPEN_WEATHER_MAP_APP_ID, cidadeLat, cidadeLon);
  else currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);

  weatherOk = currentWeather.cityName.length() > 0;
  if (weatherOk && !coordOk && (currentWeather.lat != 0 || currentWeather.lon != 0)) {
    coordOk = true;                      // guarda as coordenadas da cidade: próximas consultas e Sol/Lua usam elas
    cidadeLat = currentWeather.lat;
    cidadeLon = currentWeather.lon;
    salvaConfig();
    nextAstro = 0;                       // já calcula o sol e a Lua
    Serial.printf_P(PSTR("Coordenadas da cidade: %.4f, %.4f\n"), cidadeLat, cidadeLon);
  }

  desenhaProgresso(display, 60, F("Atualizando previsões..."));
  // Todos os blocos de 3 h (até 4 dias), numa consulta só; a memória é liberada logo depois
  OpenWeatherMapForecastData* blocos = new (std::nothrow) OpenWeatherMapForecastData[MAX_BLOCOS_PREVISAO]();
  if (blocos) {
    uint8_t n;
    if (coordOk) n = forecastClient.updateForecasts(blocos, OPEN_WEATHER_MAP_APP_ID, cidadeLat, cidadeLon, MAX_BLOCOS_PREVISAO);
    else n = forecastClient.updateForecastsById(blocos, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_BLOCOS_PREVISAO);
    if (n > 0) calculaPrevisao(blocos, n);   // falhou: mantém a última previsão boa
    delete[] blocos;
  } else {
    Serial.println(F("Previsão: memória insuficiente para os blocos"));
  }

  if (weatherOk) climaRecebido = true;
  else if (climaRecebido) currentWeather.cityName = cidadeAnterior;   // falhou: o OLED segue com os últimos dados bons
  desenhaProgresso(display, 100, weatherOk ? F("Pronto...") : F("Falha ao obter o clima"));
  delay(noBoot ? (weatherOk ? 700 : 2500) : 300);   // no boot a tela de progresso fica visível por mais tempo

  nextWeatherUpdate = millis() + (weatherOk ? UPDATE_INTERVAL_MS : API_RETRY_MS);
}

/**
 * Com os blocos de 3 h da previsão, calcula as próximas 24 h (mínima, máxima, chuva somada e
 * sensação térmica) e escolhe, para cada um dos 3 dias seguintes, o bloco mais perto do meio-dia
 * local (vai para forecasts[], usado na tela 'Próximos dias' e na página).
 */
void calculaPrevisao(OpenWeatherMapForecastData* b, uint8_t n) {
  // Próximas 24 h: os blocos até 21 h depois do primeiro (8 blocos de 3 h)
  time_t inicio = b[0].observationTime;
  prox24Min = b[0].tempMin;
  prox24Max = b[0].tempMax;
  prox24Chuva = 0;
  for (uint8_t k = 0; k < n && b[k].observationTime <= inicio + 21UL * 3600UL; k++) {
    if (b[k].tempMin < prox24Min) prox24Min = b[k].tempMin;
    if (b[k].temp < prox24Min) prox24Min = b[k].temp;
    if (b[k].tempMax > prox24Max) prox24Max = b[k].tempMax;
    if (b[k].temp > prox24Max) prox24Max = b[k].temp;
    prox24Chuva += b[k].rain;           // mm em cada bloco de 3 h (0 se não houver chuva)
  }
  prox24Sens = weatherOk ? currentWeather.feelsLike : b[0].feelsLike;   // sensação de agora (ou do próximo bloco)
  prox24Ok = true;

  // Próximos 3 dias: bloco mais perto das 12:00 locais de amanhã, depois de amanhã e do dia seguinte
  time_t ref = horaOk() ? time(nullptr) : inicio;
  struct tm meiaNoite = *localtime(&ref);
  meiaNoite.tm_hour = 0; meiaNoite.tm_min = 0; meiaNoite.tm_sec = 0;
  time_t hoje = mktime(&meiaNoite);     // 00:00 local de hoje
  for (uint8_t d = 0; d < MAX_FORECASTS; d++) {
    time_t alvo = hoje + (time_t)(d + 1) * 86400 + 12 * 3600;
    int8_t melhor = -1;
    long menorDif = 3L * 3600L;         // aceita até 3 h de distância do meio-dia
    for (uint8_t k = 0; k < n; k++) {
      long dif = labs((long)b[k].observationTime - (long)alvo);
      if (dif < menorDif) { menorDif = dif; melhor = k; }
    }
    if (melhor >= 0) forecasts[d] = b[melhor];
    else forecasts[d] = OpenWeatherMapForecastData();   // dia sem dados: a tela mostra '--'
  }
}
