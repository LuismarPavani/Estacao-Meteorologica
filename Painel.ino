/*
 * PAINEL WEB (API REST)
 *
 * idDispositivo, montaPayloadApi e envio das leituras ao painel.
 * ESP32: quase igual (ID do chip e cliente HTTPS mudam de nome).
 *
 * Aba do sketch EstacaoMeteorologica: a IDE do Arduino junta esta aba ao EstacaoMeteorologica.ino
 * antes de compilar. Globais, tipos, tabelas e protótipos ficam na aba principal.
 */

// ---- PAINEL WEB (API REST) ----------------------------------------------------------------

/** Devolve o device_id enviado ao painel: o salvo em /config ou, se vazio, 'esp8266-' + ID do chip. */
String idDispositivo() {
  if (apiDeviceId.length() > 0) return apiDeviceId;
  String id = String(ESP.getChipId(), HEX);
  id.toLowerCase();
  return F("esp8266-") + id;               // padrão, se nenhum ID foi salvo em /config
}

/**
 * Monta o corpo JSON enviado à API do painel (22 campos: leitura, rede, cidade e coordenadas, bateria, energia
 * externa e saúde do ESP). Todos os campos ficam aqui, fáceis de ajustar. O horário não vai no
 * corpo: o servidor registra a hora em que recebe.
 */
String montaPayloadApi() {
  int rssi = WiFi.RSSI();
  String chip = String(ESP.getChipId(), HEX);
  chip.toUpperCase();
  String cid = OPEN_WEATHER_MAP_LOCATION_ID;           // código da cidade (OpenWeatherMap), enviado como número
  bool soDigitos = cid.length() > 0;
  for (size_t k = 0; k < cid.length(); k++) {
    if (!isDigit(cid[k])) soDigitos = false;
  }

  String j;
  j.reserve(800);
  // ---- campos principais ----
  j += F("{\"device_id\":\"") + jsonEscape(idDispositivo()) + F("\"");
  j += F(",\"temperature\":") + String(localTemp, 1);
  j += F(",\"humidity\":") + String(localHum, 1);
  j += F(",\"city_id\":");
  j += soDigitos ? cid : String(F("null"));
  j += F(",\"city_name\":\"") + jsonEscape(currentWeather.cityName) + F("\"");
  // Coordenadas da cidade (null até serem conhecidas): o painel consulta o clima por elas e,
  // sem city_id (cidade escolhida pela busca ou por latitude/longitude), identifica a cidade por elas
  j += F(",\"lat\":");
  j += coordOk ? String(cidadeLat, 4) : String(F("null"));
  j += F(",\"lon\":");
  j += coordOk ? String(cidadeLon, 4) : String(F("null"));
  j += F(",\"ip\":\"") + WiFi.localIP().toString() + F("\"");
  j += F(",\"rssi\":") + String(rssi);
  j += F(",\"firmware\":\"" FIRMWARE_VERSAO "\"");
  // ---- bateria ----
  j += F(",\"battery_active\":");
  j += bateriaAtiva ? F("true") : F("false");
  j += F(",\"battery\":") + String(bateriaAtiva ? bateriaPct : 0);
  j += F(",\"battery_voltage\":") + String(bateriaAtiva ? bateriaV : 0.0f, 2);
  // ---- energia, sensor e saúde do ESP ----
  j += F(",\"external_power\":");                 // true = energia externa (5 V); false = bateria; null = ESP sem o sensor
  if (energiaAtiva && PIN_ENERGIA >= 0) j += energiaExterna ? F("true") : F("false");
  else j += F("null");
  j += F(",\"sensor_ok\":");                      // false = a última leitura do sensor falhou (temperature/humidity repetem o último valor)
  j += sensorUltimaOk ? F("true") : F("false");
  j += F(",\"uptime_s\":") + String(millis() / 1000UL);
  j += F(",\"reset_reason\":\"") + jsonEscape(ESP.getResetReason()) + F("\"");
  j += F(",\"free_heap\":") + String(ESP.getFreeHeap());
  j += F(",\"chip_id\":\"") + chip + F("\"");
  j += F(",\"send_interval_s\":") + String(intervaloEnvioMs() / 1000UL);
  j += F(",\"display_on\":");
  j += displayLigado ? F("true") : F("false");
  j += F(",\"rssi_pct\":") + String(qualidadeSinal(rssi));
  j += F("}");
  return j;
}

/**
 * Faz o POST (HTTP ou HTTPS) da leitura para a API e guarda o resultado em apiUltimoMsg. Devolve
 * true se o servidor respondeu 2xx.
 */
bool enviaParaApiInterno() {
  apiTentou = true;
  apiUltimoMs = millis();
  apiUltimoOk = false;
  apiUltimoDurMs = 0;                   // 0 = não chegou a conectar (falha antes do envio)

  if (apiUrl.length() == 0 || deviceKey.length() == 0) {
    apiUltimoMsg = F("Configure a URL da API e a DEVICE_KEY");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    apiUltimoMsg = F("Sem WiFi");
    return false;
  }

  bool https = apiUrl.startsWith("https://");
  // O HTTPS (BearSSL, buffers de 1024/512) usa ~11-12 KB no total; o maior pedaço é a pilha
  // de ~6,2 KB. Por isso confere o total livre e o maior bloco contínuo (HTTPS_MIN_*).
  if (https && (ESP.getFreeHeap() < HTTPS_MIN_LIVRE || ESP.getMaxFreeBlockSize() < HTTPS_MIN_BLOCO)) {
    apiUltimoMsg = F("Memória insuficiente para HTTPS (") + String(ESP.getFreeHeap()) + F(" bytes livres, maior bloco ") +
                   String(ESP.getMaxFreeBlockSize()) + F(")");
    return false;
  }

  if (envioNaTela) desenhaEnvioEtapa(25, F("Preparando dados"));
  String corpo = montaPayloadApi();

  WiFiClient clientPlain;
  WiFiClientSecure clientTls;
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);

  bool iniciou;
  if (https) {
    clientTls.setInsecure();                          // não valida o certificado do servidor
    clientTls.setBufferSizes(API_TLS_RX_BUF, 512);    // economiza RAM no HTTPS
    iniciou = http.begin(clientTls, apiUrl);
  } else {
    iniciou = http.begin(clientPlain, apiUrl);
  }
  if (!iniciou) {
    apiUltimoMsg = F("URL inválida");
    return false;
  }

  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("x-device-key"), deviceKey);          // DEVICE_KEY configurada em /config

  if (envioNaTela) desenhaEnvioEtapa(60, https ? F("Conexão segura") : F("Conectando"));
  unsigned long t0 = millis();
  int codigo = http.POST(corpo);                      // > 0: código HTTP, < 0: erro de conexão
  apiUltimoDurMs = millis() - t0;
  if (envioNaTela) desenhaEnvioEtapa(100, F("Resposta recebida"));
  if (codigo > 0) apiUltimoMsg = F("HTTP ") + String(codigo);
  else apiUltimoMsg = HTTPClient::errorToString(codigo);
  http.end();

  apiUltimoOk = (codigo >= 200 && codigo < 300);
  Serial.println(F("API -> ") + apiUltimoMsg);
  return apiUltimoOk;
}

/**
 * Envolve o envio com o LED: pisca rápido enquanto envia e, no fim, sinaliza sucesso (1 acendida)
 * ou falha (3 piscadas).
 */
bool enviaParaApi() {
  ecoAcorda(30000UL);                   // HTTPS sem light sleep: conecta mais rápido e não perde pacotes
  ledEnviando = true;
  bool ok = enviaParaApiInterno();
  ledEnviando = false;
  String ev = F("Painel: ") + apiUltimoMsg;   // ex.: "Painel: HTTP 200 (1.6 s)" ou o motivo da falha
  if (apiUltimoDurMs > 0) ev += F(" (") + String(apiUltimoDurMs / 1000.0f, 1) + F(" s)");
  logEvento(ev);
  if (ok) ledPulsoOk(); else ledPulsoFalha();
  return ok;
}
