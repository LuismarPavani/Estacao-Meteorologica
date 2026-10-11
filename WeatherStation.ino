/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

/*
 * WeatherStation ESP8266: estação meteorológica com display OLED, sensor DHT, clima do OpenWeatherMap,
 * página web local, configuração pelo navegador, bateria, LED de status, botões físicos, OTA e envio
 * das leituras para um painel (API REST).
 *
 * MAPA DO ARQUIVO (procure pelos números):
 *   1. Bibliotecas
 *   2. Seleção da placa e pinagem        <- o que muda ao trocar de ESP (display, sensor e botões)
 *   3. Configurações do projeto          <- constantes ajustáveis (tempos, bateria, API, clima...)
 *   4. Tipos
 *   5. Objetos e variáveis de estado
 *   6. Protótipos das funções
 *   7. Tabelas (telas do OLED, idiomas, fusos, abas)
 *   8. Funções: principal, configuração salva, LED, botões, sensores, bateria, clima, display OLED,
 *      tela (tempo ligada e agenda), WiFi, painel (API), servidor web
 */

// ==========================================================================================
//  1. BIBLIOTECAS
// ==========================================================================================
// ---- Base do ESP8266, WiFi e servidor web -------------------------------------------------
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ---- Sistema: OTA, EEPROM (configurações salvas) e Ticker (LED sem travar o loop) ---------
#include <ArduinoOTA.h>                 // atualização de firmware pela rede
#include <EEPROM.h>                     // guarda chave/cidade/idioma na flash
#include <Ticker.h>                     // pisca o LED de status sem travar o loop

// ---- HTTP/HTTPS e JSON (envio ao painel e clima) ------------------------------------------
#include <ESP8266HTTPClient.h>            // envio das leituras ao painel (API)
#include <WiFiClientSecure.h>              // HTTPS
#include <coredecls.h>                  // settimeofday_cb()
#include <ESPHTTPClient.h>
#include <JsonListener.h>

// ---- Portal de configuração do WiFi -------------------------------------------------------
#include <DNSServer.h>
#include <WiFiManager.h>                // https://github.com/tzapu/WiFiManager

// ---- Hora (NTP) ---------------------------------------------------------------------------
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval

// ---- Display OLED -------------------------------------------------------------------------
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "Wire.h"

// ---- Clima (OpenWeatherMap) ---------------------------------------------------------------
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"

// ---- Fontes e imagens do projeto (arquivos .h na mesma pasta) -----------------------------
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"

// ---- Sensor de temperatura e umidade ------------------------------------------------------
#include "DHT.h"

// ==========================================================================================
//  2. SELEÇÃO DA PLACA E PINAGEM
//  Para trocar de ESP, deixe SEM comentário apenas UMA das duas linhas abaixo (PLACA_...).
//  Tudo que muda entre as placas (display, sensor e botões) está neste bloco.
//  
//                            ESP8266 comum (principal)   NodeMCU com OLED integrado
//    OLED SDA                D2  (GPIO4)                 D5  (GPIO14)
//    OLED SCL                D1  (GPIO5)                 D6  (GPIO12)
//    Sensor DHT (dado)       D5  (GPIO14)                D2  (GPIO4)
//    Botão das telas         D7  (GPIO13)                D7  (GPIO13)
//    Botão de reset geral    D6  (GPIO12)                D1  (GPIO5)
//    Bateria                 A0                          A0
//    LED de status           GPIO2 (D4) e GPIO16 (D0)    GPIO2 (D4) e GPIO16 (D0)
//  
//  Botões: contato entre o pino e o GND (usam o pull-up interno). Evite D0, D3, D4 e D8 (pinos de boot/LED).
// ==========================================================================================

#define PLACA_ESP8266_COMUM            // ESP8266 sem OLED integrado + OLED externo  (modelo principal)
//#define PLACA_OLED_INTEGRADO         // NodeMCU com o OLED 0,96" já soldado na placa

#if defined(PLACA_ESP8266_COMUM) && defined(PLACA_OLED_INTEGRADO)
  #error "Escolha apenas UMA placa: comente PLACA_ESP8266_COMUM ou PLACA_OLED_INTEGRADO."
#endif

#if defined(PLACA_ESP8266_COMUM)
  // ---- ESP8266 comum + OLED externo (SSD1306/SSD1315) ----
  #define DISPLAY_SDA      D2           // GPIO4
  #define DISPLAY_SCL      D1           // GPIO5 (se a tela ficar em branco, troque SDA e SCL)
  #define DHTPIN           14           // GPIO14 = D5 (a biblioteca DHT usa o número do GPIO)
  #define DHTTYPE          DHT22        // DHT22 (AM2302); use DHT11 se for o sensor mais simples
  #define PIN_BOTAO_TELA   D7           // GPIO13: clique = próxima tela; segurar = apagar o WiFi
  #define PIN_BOTAO_RESET  D6           // GPIO12: botão de reset geral (-1 desativa)
#elif defined(PLACA_OLED_INTEGRADO)
  // ---- NodeMCU com OLED integrado (SSD1315, compatível com SSD1306) ----
  #define DISPLAY_SDA      D5           // GPIO14
  #define DISPLAY_SCL      D6           // GPIO12 (confirmado na prática; se a tela ficar em branco, troque SDA e SCL)
  #define DHTPIN           4            // GPIO4 = D2
  #define DHTTYPE          DHT22        // troque para DHT11 se esse for o sensor ligado
  #define PIN_BOTAO_TELA   D7           // GPIO13
  #define PIN_BOTAO_RESET  D1           // GPIO5 (o D6 é o SCL do display)
#else
  #error "Nenhuma placa selecionada: descomente PLACA_ESP8266_COMUM ou PLACA_OLED_INTEGRADO."
#endif

// Pinos usados pelo resto do código (vêm da seleção acima; não precisa editar)
const int SDA_PIN = DISPLAY_SDA;
const int SDC_PIN = DISPLAY_SCL;
#define PIN_BATERIA     A0

// ==========================================================================================
//  3. CONFIGURAÇÕES DO PROJETO
//  Valores que você pode ajustar. Vários podem ser mudados também pela página /config (e ficam salvos na flash);
//  os valores daqui são só os padrões de fábrica.
// ==========================================================================================

// ---- Identificação ------------------------------------------------------------------------
#define FIRMWARE_VERSAO "2.7"           // enviado ao painel no campo "firmware"
#define HOSTNAME "ESP8266-OTA-"

// ---- Display OLED e sensor ----------------------------------------------------------------
const int I2C_DISPLAY_ADDRESS = 0x3c;
const unsigned long SENSOR_INTERVAL_MS    = 10UL * 1000UL;        // sensor interno: a cada 10 s

// ---- Tela: botões, tempo ligada e LED de status -------------------------------------------
// Botão das telas: segurar BOTAO_RESET_MS apaga só o WiFi. Botão de reset geral: segurar BOTAO_RESET_GERAL_MS apaga WiFi e ajustes.
const unsigned long BOTAO_DEBOUNCE_MS = 40;
const unsigned long BOTAO_AVISO_MS    = 3000;   // a partir daqui aparece o aviso "Apagando o WiFi..."
const unsigned long BOTAO_RESET_MS    = 8000;   // botão das telas: tempo segurando para apagar só o WiFi
const unsigned long BOTAO_RESET_GERAL_MS = 10000; // botão de reset geral: tempo segurando para apagar tudo
const unsigned long TELA_ACORDAR_NOITE_MS = 10000; // no horário agendado como desligado, o clique liga a tela só por este tempo
const unsigned long TELA_MENSAGEM_MS = 3500;     // quanto tempo uma mensagem (ex.: resultado do envio ao painel) fica no OLED
// LED azul (GPIO2 e GPIO16): pisca rápido = conectando ao WiFi, enviando ou aviso de reset; uma piscada longa a cada 2 s = sem WiFi;
// 1 acendida = sucesso; 3 piscadas curtas = falha. false = LED sempre apagado.
#define LED_STATUS_ATIVO true

// ---- Clima (OpenWeatherMap): padrões; a chave, a cidade e o idioma são configurados em /config (nunca publique a chave) 
String OPEN_WEATHER_MAP_APP_ID = "";
String OPEN_WEATHER_MAP_LOCATION_ID = "3463011";
String OPEN_WEATHER_MAP_LANGUAGE = "pt_br";
const uint8_t MAX_FORECASTS = 3;
const boolean IS_METRIC = true;
const String WDAY_NAMES[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
const String MONTH_NAMES[] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
// Intervalos do clima
const unsigned long UPDATE_INTERVAL_MS = 15UL * 60UL * 1000UL; // clima externo: a cada 15 min
const unsigned long RETRY_INTERVAL_MS  = 60UL * 1000UL;        // nova tentativa se estiver sem WiFi
const unsigned long API_RETRY_MS       = 5UL * 60UL * 1000UL;  // nova tentativa se a API falhar

// ---- Fuso horário padrão (pode ser trocado em /config). Brasília = -3, sem horário de verão 
#define TZ              -3       // (utc+) TZ in hours
#define DST_MN          0        // use 60mn for summer time in some countries
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

// ---- Bateria (A0): divisor de tensão, calibração com multímetro e limites de aviso --------
#define BATERIA_ATIVA_PADRAO false
const float BAT_FATOR = 4.2f;           // (R1 + R2) / R2 do divisor
const float BAT_CALIB = 0.99947619f;    // medido com multímetro (tensão real ÷ tensão lida)
const int   BAT_AMOSTRAS = 16;
#define BAT_AVISO_PCT   20              // abaixo disso: aviso de bateria baixa
#define BAT_CRITICA_PCT 10              // abaixo disso: aviso crítico

// ---- Painel web (API REST): URL e DEVICE_KEY também são configuradas em /config -----------
#define API_URL_PADRAO "https://weatherstation-web.vercel.app/api/readings"   // pode ser trocada em /config
const unsigned long PAINEL_INTERVAL_MS = 15UL * 60UL * 1000UL;   // envio automático ao painel a cada 15 min
const unsigned long PAINEL_RETRY_MS    = 2UL * 60UL * 1000UL;    // se o envio falhar, tenta de novo em 2 min
#define API_TLS_RX_BUF 1024             // buffer de recepção do HTTPS (mesmo valor do WeatherApi.h; aumente se a conexão falhar)

// ---- Histórico interno (144 pontos x 5 min = 12 h, guardado em anel na RAM) ---------------
const uint8_t HIST_SIZE = 144;
const unsigned long HIST_INTERVAL_MS = 5UL * 60UL * 1000UL;

// ---- Configurações salvas na EEPROM -------------------------------------------------------
#define CFG_MAGIC 0x57533031UL          // "WS01"

// ==========================================================================================
//  4. TIPOS
// ==========================================================================================

// Estrutura gravada na EEPROM (campos novos sempre no fim, para não invalidar dados antigos)
struct Config {
  uint32_t magic;
  char apiKey[41];
  char locationId[12];
  char lang[8];
  uint8_t displayLigado;                // 0 = apagado, qualquer outro valor = ligado
  uint8_t agendaAtiva;                  // 1 = agenda ligada
  uint16_t agendaLiga;                  // minutos desde 00:00
  uint16_t agendaDesliga;
  uint8_t fusoOk;                       // 0xA5 = fuso salvo pelo usuário
  int16_t fuso;                         // minutos em relação ao UTC (ex.: -180)
  uint8_t verao;                        // 1 = horário de verão (+1 h)
  uint8_t bateriaOk;                    // 0xA5 = escolha da bateria salva pelo usuário
  uint8_t bateria;                      // 1 = este ESP tem bateria
  uint8_t apiOk;                        // 0xA5 = URL e DEVICE_KEY salvas
  char apiUrl[121];
  char deviceKey[65];
  uint8_t deviceIdOk;                   // 0xA5 = device_id salvo
  char deviceId[33];
  uint8_t telaSeg;                      // segundos de cada tela do OLED (5 a 30)
  uint8_t telaTimeout;                  // tempo de tela ligada em minutos (0 = sempre ligada)
};

// Opção da lista de fusos horários (minutos em relação ao UTC e nome)
struct Fuso { int16_t min; const char* nome; };

// Estados contínuos do LED (rede)
enum { LED_NORMAL = 0, LED_CONECTANDO = 1, LED_SEM_REDE = 2 };

// ==========================================================================================
//  5. OBJETOS E VARIÁVEIS DE ESTADO
// ==========================================================================================

// ---- Objetos: sensor, servidor, display, WiFi e clientes do OpenWeatherMap ----------------
DHT dht(DHTPIN, DHTTYPE);
ESP8266WebServer server(80);
SSD1306Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi   ui( &display );
WiFiManager wifiManager;
OpenWeatherMapCurrent currentWeatherClient;
OpenWeatherMapForecast forecastClient;

// ---- Dados do clima (OpenWeatherMap) ------------------------------------------------------
OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
bool weatherOk = false;                 // true se a última consulta ao OpenWeatherMap trouxe dados
bool readyForWeatherUpdate = false;
unsigned long nextWeatherUpdate = 0;
time_t now;

// ---- Sensor interno e histórico -----------------------------------------------------------
float localTemp = 0;
float localHum = 0;
bool sensorOk = false;                     // true depois da primeira leitura válida
unsigned long nextSensorRead = 0;
int16_t histTemp[HIST_SIZE];            // temperatura x10
uint16_t histHum[HIST_SIZE];            // umidade x10 (1 casa decimal)
uint8_t histHead = 0;
uint8_t histCount = 0;
unsigned long nextHist = 0;

// ---- Rede ---------------------------------------------------------------------------------
unsigned long nextRede = 0;

// ---- Tela: ligada/apagada, tempo de cada tela e tempo de tela ligada ----------------------
bool displayLigado = true;              // false = tela OLED apagada (salvo na EEPROM)
uint8_t telaSeg = 5;                     // segundos de cada tela do OLED (5 a 30, de 5 em 5)
uint8_t telaTimeoutMin = 0;             // tempo de tela ligada: 0 = sempre ligada; 1, 5, 10, 15, 30 ou 60 min
unsigned long telaDesligaEm = 0;        // millis() em que a tela apaga sozinha
bool telaContando = false;              // true = há uma contagem para apagar a tela
bool telaAcordadaNoite = false;         // true = tela acordada pelo botão no horário agendado como desligado

// ---- Mensagem temporária no OLED (ex.: resultado do envio ao painel pelo botão) -----------
String msgTitulo = "";                  // 1ª linha da mensagem
String msgLinha1 = "";                  // 2ª linha
String msgLinha2 = "";                  // 3ª linha (quebra em até 2 linhas se for longa)
unsigned long msgAte = 0;               // millis() em que a mensagem some
bool msgAtiva = false;                  // true enquanto a mensagem está na tela
bool msgLigouTela = false;              // true = a mensagem acendeu a tela; apaga de novo ao terminar

// ---- Agenda da tela (minutos desde 00:00) -------------------------------------------------
bool agendaAtiva = false;
uint16_t agendaLiga = 6 * 60;           // 06:00
uint16_t agendaDesliga = 23 * 60;       // 23:00
int8_t agendaUltimoEstado = -1;         // -1 = ainda não avaliada
unsigned long nextAgenda = 0;

// ---- Fuso horário e horário de verão (minutos em relação ao UTC) --------------------------
int16_t fusoMin = TZ * 60;
bool horarioVerao = (DST_MN != 0);

// ---- Bateria (média móvel da tensão e percentual estimado) --------------------------------
bool bateriaAtiva = BATERIA_ATIVA_PADRAO;   // escolhido em /config
float bateriaV = 0;
int bateriaPct = 0;
unsigned long nextBateria = 0;

// ---- Envio ao painel web (API) ------------------------------------------------------------
String apiUrl = API_URL_PADRAO;
String deviceKey = "";
String apiDeviceId = "";                // device_id enviado ao painel ("" = esp8266-<chip>)
bool apiTentou = false;
bool apiUltimoOk = false;
String apiUltimoMsg = "";
unsigned long apiUltimoMs = 0;
unsigned long nextApi = 20000UL;        // 1º envio 20 s depois de ligar

// ---- LED de status (usadas também pelo Ticker, por isso volatile) -------------------------
volatile uint8_t ledRede = LED_NORMAL;  // estado contínuo da rede
volatile bool ledEnviando = false;      // true durante o envio ao painel
volatile bool ledAviso = false;         // true enquanto o botão é segurado para apagar o WiFi
volatile uint8_t ledPulso = 0;          // 0 = nenhum, 1 = ok, 2 = falha
volatile uint8_t ledPulsoPos = 0;
volatile uint8_t ledPos = 0;
Ticker ledTicker;

// ---- Botões físicos (índice 0 = botão das telas, 1 = botão de reset geral) ----------------
int8_t btnPin[2] = {PIN_BOTAO_TELA, PIN_BOTAO_RESET};   // 0 = botão das telas, 1 = botão de reset geral (opcional)
bool btnEstado[2] = {false, false};     // estado estável (true = pressionado)
bool btnLido[2] = {false, false};       // última leitura bruta
unsigned long btnMudou[2] = {0, 0};     // quando a leitura bruta mudou (debounce)
unsigned long btnDesde[2] = {0, 0};     // quando o botão foi pressionado

// ==========================================================================================
//  6. PROTÓTIPOS DAS FUNÇÕES
//  Declaradas aqui para que a ordem das funções abaixo não importe.
// ==========================================================================================

// ---- CONFIGURAÇÃO SALVA (EEPROM) E FUSO HORÁRIO -------------------------------------------
void carregaConfig();
void salvaConfig();
void limpaConfig();
void aplicaFuso();

// ---- LED DE STATUS ------------------------------------------------------------------------
void ledEscreve(bool aceso);
void ledTick();
void ledInicia();
void ledPulsoOk();
void ledPulsoFalha();

// ---- BOTÕES FÍSICOS -----------------------------------------------------------------------
bool botaoAtualiza(uint8_t i, unsigned long ms);
void proximaTela();
void desenhaAvisoReset(int pct, bool geral);
void resetWiFiPeloBotao();
void resetGeralPeloBotao();
bool trataBotoes(unsigned long ms);
void enviarPeloBotao();

// ---- SENSORES E HISTÓRICO -----------------------------------------------------------------
void varreduraI2C();
void leSensor();
void guardaHistorico();

// ---- BATERIA ------------------------------------------------------------------------------
float lerTensaoBateria();
int bateriaPercentual(float v);
void atualizaBateria();
const char* bateriaSituacao(float v);

// ---- CLIMA (OpenWeatherMap) ---------------------------------------------------------------
void updateData(OLEDDisplay *display);

// ---- DISPLAY OLED: TELAS E ÍCONES ---------------------------------------------------------
void aplicaDisplay();
void drawProgress(OLEDDisplay *display, int percentage, String label);
void mostraMensagemTela(const String& titulo, const String& l1, const String& l2, unsigned long dur);
void desenhaMensagemTela();
bool trataMensagemTela(unsigned long ms);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawClimaInterno(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeatherDetails(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawWiFi(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void drawBateria(OLEDDisplay *display, int x, int y, int pct);
void drawSinalWiFi(OLEDDisplay *display, int x, int y, int pct);
int getRSSIasQuality(int rssi);

// ---- TELA: TEMPO LIGADA E AGENDA ----------------------------------------------------------
void armaTimerTela(unsigned long dur);
unsigned long telaDuracaoPadraoMs();
void verificaTimerTela(unsigned long ms);
int8_t agendaEstadoAtual();
void verificaAgenda();

// ---- WIFI (portal do WiFiManager) ---------------------------------------------------------
void configModeCallback(WiFiManager *myWiFiManager);
void saveConfigCallback();

// ---- PAINEL WEB (API REST) ----------------------------------------------------------------
String idDispositivo();
String montaPayloadApi();
bool enviaParaApiInterno();
bool enviaParaApi();

// ---- SERVIDOR WEB: UTILITÁRIOS DE TEXTO ---------------------------------------------------
String htmlEsc(const String& s);
String jsonEscape(const String& s);
String formataHora(int minutos);
int parseHora(const String& t);
void redirecionaConfig(const char* r);
String fmtBytes(uint32_t b);
String formataUptime(unsigned long s);
String linhaInfo(const char* rotulo, const String& valor);

// ---- SERVIDOR WEB: PÁGINA PRINCIPAL E DADOS EM JSON ---------------------------------------
void paginaInicial();
void paginaJson();
void paginaHistorico();

// ---- SERVIDOR WEB: PÁGINA /config (ABAS E TABELAS) ----------------------------------------
String iconeSinal(int pct);
String iconeBateria(int pct);
String tabelaBateria();
String tabelaWiFi();
String tabelaSistema();
String abaValida(const String& a);
void paginaConfig();

// ---- SERVIDOR WEB: AÇÕES (POST) -----------------------------------------------------------
void handleSalvar();
void handleApi();
void handleEnviar();
void handleTempoLigada();
void handleTempoTela();
void handleBateria();
void handleAgenda();
void handleDisplay();
void handleResetWiFi();
void handleResetGeral();
void handlePortal();

// ==========================================================================================
//  7. TABELAS
// ==========================================================================================

// ---- Telas do OLED (deslizam da direita para a esquerda) e barra inferior -----------------
FrameCallback frames[] = {drawDateTime, drawClimaInterno, drawCurrentWeather, drawCurrentWeatherDetails, drawForecast, drawWiFi};
int numberOfFrames = 6;
OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;

// ---- Idiomas do OpenWeatherMap (lista da página /config) ----------------------------------
static const char* const LANG_CODES[] = {"pt_br", "pt", "en", "es", "fr", "de", "it"};
static const char* const LANG_NAMES[] = {"Português (Brasil)", "Português (Portugal)", "English", "Español", "Français", "Deutsch", "Italiano"};
static const uint8_t LANG_COUNT = 7;

// ---- Fusos horários (minutos em relação ao UTC) da página /config -------------------------
static const Fuso FUSOS[] = {
  {-720, "UTC-12:00"},
  {-660, "UTC-11:00"},
  {-600, "UTC-10:00 (Havaí)"},
  {-540, "UTC-09:00 (Alasca)"},
  {-480, "UTC-08:00 (Califórnia)"},
  {-420, "UTC-07:00 (Colorado)"},
  {-360, "UTC-06:00 (México, Chicago)"},
  {-300, "UTC-05:00 (Acre, Nova York, Colômbia)"},
  {-240, "UTC-04:00 (Amazonas, Mato Grosso, Venezuela)"},
  {-210, "UTC-03:30 (Terra Nova)"},
  {-180, "UTC-03:00 (Brasília, Argentina)"},
  {-120, "UTC-02:00 (Fernando de Noronha)"},
  {-60,  "UTC-01:00 (Açores)"},
  {0,    "UTC+00:00 (Lisboa, Londres)"},
  {60,   "UTC+01:00 (Paris, Berlim)"},
  {120,  "UTC+02:00 (Atenas, Cairo)"},
  {180,  "UTC+03:00 (Moscou, Istambul)"},
  {210,  "UTC+03:30 (Irã)"},
  {240,  "UTC+04:00 (Dubai)"},
  {270,  "UTC+04:30 (Afeganistão)"},
  {300,  "UTC+05:00 (Paquistão)"},
  {330,  "UTC+05:30 (Índia)"},
  {345,  "UTC+05:45 (Nepal)"},
  {360,  "UTC+06:00 (Bangladesh)"},
  {390,  "UTC+06:30 (Mianmar)"},
  {420,  "UTC+07:00 (Bangkok)"},
  {480,  "UTC+08:00 (Pequim, Singapura)"},
  {540,  "UTC+09:00 (Tóquio)"},
  {570,  "UTC+09:30 (Adelaide)"},
  {600,  "UTC+10:00 (Sydney)"},
  {660,  "UTC+11:00"},
  {720,  "UTC+12:00 (Auckland)"},
  {780,  "UTC+13:00"},
  {840,  "UTC+14:00"}
};
static const uint8_t FUSO_COUNT = sizeof(FUSOS) / sizeof(FUSOS[0]);

// ---- Abas da página /config ---------------------------------------------------------------
static const char* const ABAS_ID[]   = {"clima", "tela", "bateria", "wifi", "sistema"};
static const char* const ABAS_NOME[] = {"Clima", "Tela", "Bateria", "WiFi", "Sistema"};
static const uint8_t ABAS_COUNT = 5;

// ---- Página principal (HTML, CSS e JavaScript guardados na flash; os números chegam por fetch de /json e /historico) 
static const char PAGE_INDEX[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Station</title>
<style>
:root{--card:rgba(255,255,255,.09);--mut:#a9c1cf}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:#fff;background:linear-gradient(160deg,#0f2027,#2c5364) fixed}
.wrap{max-width:720px;margin:0 auto;padding:16px}
.card{background:var(--card);border-radius:18px;padding:16px}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin:12px 0}
@media(min-width:560px){.grid.g3{grid-template-columns:repeat(3,1fr)}}
.hero{display:flex;align-items:center;justify-content:space-between;margin-top:0}
.hero .t{font-size:64px;font-weight:300;line-height:1}
.hero .ico{font-size:76px;line-height:1}
.mut{color:var(--mut);font-size:.85rem}
.lbl{color:var(--mut);font-size:.75rem;text-transform:uppercase;letter-spacing:.06em;margin-bottom:6px}
.big{font-size:1.6rem;font-weight:500}
.bar{height:8px;border-radius:4px;background:rgba(255,255,255,.15);overflow:hidden;margin-top:10px}
.bar>i{display:block;height:100%;width:0;background:linear-gradient(90deg,#4fc3f7,#81d4fa);transition:width .6s}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px}
.ring{--p:0;width:96px;height:96px;border-radius:50%;flex:none;display:grid;place-items:center;background:conic-gradient(#4fc3f7 calc(var(--p)*1%),rgba(255,255,255,.15) 0)}
.ring span{width:76px;height:76px;border-radius:50%;background:#17323f;display:grid;place-items:center;font-weight:500}
.fc{display:flex;justify-content:space-around;text-align:center}
.fc .e{font-size:2.2rem;margin:4px 0}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin:0 4px 0 10px}
svg text{fill:#a9c1cf;font-size:10px}
#chart{display:block;margin:0 auto;max-width:520px;touch-action:pan-y;cursor:crosshair}
#chart text{font-size:9px}
a{color:#81d4fa}
footer{text-align:center;margin:8px 0 24px}
.ibtn{display:inline-flex;align-items:center;justify-content:center;width:40px;height:40px;border-radius:12px;background:rgba(255,255,255,.1);border:0;color:#fff;cursor:pointer;padding:0;margin:0;text-decoration:none}
.ibtn:hover{background:rgba(255,255,255,.18)}
</style></head><body><div class="wrap">

<div id="top" style="display:flex;align-items:center;justify-content:space-between;margin-bottom:12px">
  <div style="display:flex;gap:8px">
    <a class="ibtn" href="/config" title="Configurações" aria-label="Configurações"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor"><circle cx="12" cy="12" r="8.6" stroke-width="3.4" stroke-dasharray="3.377 3.377"/><circle cx="12" cy="12" r="6.2" stroke-width="2.4"/><circle cx="12" cy="12" r="2.4" stroke-width="2"/></svg></a>
    <button class="ibtn" id="btnd" onclick="tog()" title="Ligar ou apagar a tela" aria-label="Ligar ou apagar a tela"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/><path id="slash" d="M3 3l18 18" stroke="#ef5350" style="display:none"/></svg></button>
  </div>
  <div style="display:flex;align-items:center;gap:14px">
    <svg id="wf" width="26" height="18" viewBox="0 0 26 18"><title id="wft">WiFi</title><rect id="w1" x="0" y="12" width="5" height="6" rx="1" fill="#ffffff40"/><rect id="w2" x="7" y="8" width="5" height="10" rx="1" fill="#ffffff40"/><rect id="w3" x="14" y="4" width="5" height="14" rx="1" fill="#ffffff40"/><rect id="w4" x="21" y="0" width="5" height="18" rx="1" fill="#ffffff40"/></svg>
    <div id="bat" style="display:none;align-items:center;gap:6px">
      <svg width="34" height="16" viewBox="0 0 34 16"><rect x="1" y="1" width="28" height="14" rx="3.5" fill="none" stroke="#ffffffcc" stroke-width="2"/><rect id="batn" x="4" y="4" width="0" height="8" rx="1.5" fill="#66bb6a"/><rect x="30" y="5" width="3" height="6" rx="1.2" fill="#ffffffcc"/></svg>
      <span id="batp" style="font-size:.9rem;font-weight:500">--%</span>
    </div>
  </div>
</div>
<div class="card" id="bataviso" style="display:none;margin-bottom:12px"></div>
<div class="card" id="erro" style="display:none;background:rgba(255,183,77,.2);margin-bottom:12px">&#9888;&#65039; Não consegui obter o clima externo. Confira a chave da API e o ID da cidade em <a href="/config">Configurações</a>. Chaves novas podem levar um tempo para ativar.</div>

<div class="card hero">
  <div>
    <div class="mut" id="cid">--</div>
    <div class="t" id="tmp">--</div>
    <div id="desc" style="text-transform:capitalize;margin-top:6px">--</div>
    <div class="mut" id="mm"></div>
  </div>
  <div class="ico" id="ico">&#10067;</div>
</div>

<div class="grid g3">
  <div class="card"><div class="lbl">Umidade</div><div class="big" id="hum">--</div><div class="bar"><i id="humb"></i></div></div>
  <div class="card"><div class="lbl">Pressão</div><div class="big" id="pres">--</div><div class="mut">hPa</div></div>
  <div class="card"><div class="lbl">Vento</div>
    <div class="row"><div><div class="big" id="ven">--</div><div class="mut" id="vdir"></div></div>
    <svg width="44" height="44" viewBox="-22 -22 44 44"><circle r="20" fill="none" stroke="#ffffff33" stroke-width="2"/><g id="seta"><path d="M0-15L6 8L0 4L-6 8Z" fill="#81d4fa"/></g></svg></div></div>
  <div class="card"><div class="lbl">Visibilidade</div><div class="big" id="vis">--</div></div>
  <div class="card"><div class="lbl">Nuvens</div><div class="big" id="nuv">--</div><div class="bar"><i id="nuvb"></i></div></div>
  <div class="card"><div class="lbl">Sol</div><div>&#127749; <span id="nas">--</span></div><div>&#127751; <span id="por">--</span></div></div>
</div>

<div class="card row">
  <div><div class="lbl">Clima interno</div><div class="big" style="font-size:2.4rem" id="tin">--</div><div class="mut">Temperatura</div></div>
  <div class="ring" id="ring"><span id="hin">--</span></div>
</div>

<div class="card" style="margin-top:12px"><div class="lbl">Próximos dias</div><div class="fc" id="fc"></div></div>

<div class="card" style="margin-top:12px">
  <div class="lbl">Histórico interno</div>
  <div id="hsel" style="font-size:.95rem;margin-bottom:6px">--</div>
  <svg id="chart" viewBox="0 0 320 170" width="100%"></svg>
  <div class="mut"><span class="dot" style="background:#ffb74d;margin-left:0"></span>Temperatura (°C, eixo esquerdo)<span class="dot" style="background:#4fc3f7"></span>Umidade (%, eixo direito)</div>
  <div class="mut" id="hlab"></div>
</div>

<footer class="mut"><div id="foot">Carregando...</div></footer>
</div>
<script>
const $=id=>document.getElementById(id);
const IC={'01':'☀️','02':'⛅','03':'☁️','04':'☁️','09':'🌧️','10':'🌦️','11':'⛈️','13':'❄️','50':'🌫️'};
function emo(c){if(!c)return'❔';let e=IC[c.slice(0,2)]||'❔';if(c.slice(0,2)==='01'&&c[2]==='n')e='🌙';return e}
const hm=t=>t?new Date(t*1000).toLocaleTimeString('pt-BR',{hour:'2-digit',minute:'2-digit'}):'--';
const dia=t=>new Date(t*1000).toLocaleDateString('pt-BR',{weekday:'short'}).replace('.','').toUpperCase();
const DIRS=['N','NE','L','SE','S','SO','O','NO'];
const f=(v,d)=>v==null?'--':Number(v).toFixed(d);

async function load(){
  try{
    const d=await (await fetch('/json',{cache:'no-store'})).json();
    const e=d.externo,i=d.interno,u=d.unid;
    $('erro').style.display=d.erro?'block':'none';
    const b=d.bateria;
    $('bat').style.display=b?'flex':'none';
    if(b){
      const p=b.pct,c=p<=10?'#ef5350':(p<=20?'#ffb74d':'#66bb6a'),av=$('bataviso');
      $('batn').setAttribute('width',Math.round(22*p/100));$('batn').setAttribute('fill',c);
      $('batp').textContent=p+'%';$('bat').title=b.v+' V';
      if(p<=20){av.style.display='block';av.style.background=p<=10?'rgba(239,83,80,.25)':'rgba(255,183,77,.2)';
        av.textContent=p<=10?'⚠️ Bateria crítica ('+p+'%). A estação pode desligar a qualquer momento.':'⚠️ Bateria baixa ('+p+'%). Conecte ao carregador.';}
      else av.style.display='none';
    }else $('bataviso').style.display='none';
    $('slash').style.display=d.display?'none':'block';
    $('btnd').title=d.display?'Apagar a tela':'Ligar a tela';
    const q=d.rssiPct,nb=q>=75?4:(q>=50?3:(q>=25?2:(q>0?1:0)));
    for(let k=1;k<=4;k++)$('w'+k).setAttribute('fill',k<=nb?'#ffffff':'#ffffff40');
    $('wft').textContent='WiFi '+d.rssi+' dBm ('+q+'%)';
    $('cid').textContent=e.cidade||'--';
    $('tmp').textContent=f(e.temp,0)+u;
    $('desc').textContent=e.desc||'--';
    $('ico').textContent=emo(e.icone);
    $('mm').textContent='Mín '+f(e.min,0)+u+'  ·  Máx '+f(e.max,0)+u;
    $('hum').textContent=e.umid+'%';$('humb').style.width=e.umid+'%';
    $('pres').textContent=e.pressao;
    $('ven').textContent=e.vento+' km/h';
    $('vdir').textContent='de '+DIRS[Math.round(e.ventoDir/45)%8];
    $('seta').setAttribute('transform','rotate('+(e.ventoDir+180)+')');
    $('vis').textContent=f(e.visib,1)+' km';
    $('nuv').textContent=e.nuvens+'%';$('nuvb').style.width=e.nuvens+'%';
    $('nas').textContent=hm(e.nascer);$('por').textContent=hm(e.por);
    $('tin').textContent=i.temp==null?'--':f(i.temp,1)+u;
    $('hin').textContent=i.umid==null?'--':f(i.umid,1)+'%';
    $('ring').style.setProperty('--p',i.umid==null?0:i.umid);
    $('fc').innerHTML=d.previsao.filter(p=>p.dia>0).map(p=>'<div><div class="mut">'+dia(p.dia)+'</div><div class="e">'+emo(p.icone)+'</div><div>'+f(p.temp,0)+u+'</div></div>').join('');
    $('foot').textContent='Atualizado às '+new Date().toLocaleTimeString('pt-BR')+'  ·  WiFi '+d.rssi+' dBm ('+d.rssiPct+'%)';
  }catch(x){$('foot').textContent='Sem resposta da estação (atualizando dados?)...'}
}

const CW=320,CH=170,CL=40,CR=40,CT=10,CB=24;
let HD=null;
const fmtH=ms=>new Date(ms).toLocaleTimeString('pt-BR',{hour:'2-digit',minute:'2-digit'});
const fmtDur=min=>{min=Math.round(min);const h=Math.floor(min/60),m=min%60;return h?(m?h+' h '+m+' min':h+' h'):m+' min'};
function ext(a,span){
  let mn=Math.min(...a),mx=Math.max(...a);
  if(mx-mn<span){const c=(mn+mx)/2;mn=c-span/2;mx=c+span/2}
  else{const p=(mx-mn)*.08;mn-=p;mx+=p}
  return[mn,mx];
}
function draw(sel){
  if(!HD)return;
  const n=HD.t.length,pw=CW-CL-CR,ph=CH-CT-CB;
  const[t0,t1]=ext(HD.t,1),[u0,u1]=ext(HD.u,2);
  const X=k=>CL+k*pw/(n-1);
  const Yt=v=>CT+ph-(v-t0)/(t1-t0)*ph,Yu=v=>CT+ph-(v-u0)/(u1-u0)*ph;
  const tm=k=>fmtH(HD.t0-(n-1-k)*HD.iv);
  let g='';
  for(let i=0;i<3;i++){
    const y=CT+i*ph/2;
    g+='<line x1="'+CL+'" x2="'+(CW-CR)+'" y1="'+y+'" y2="'+y+'" stroke="#ffffff1f"/>';
    g+='<text x="'+(CL-5)+'" y="'+(y+3.5)+'" text-anchor="end" style="fill:#ffb74d">'+(t1-(t1-t0)*i/2).toFixed(1)+'</text>';
    g+='<text x="'+(CW-CR+5)+'" y="'+(y+3.5)+'" style="fill:#4fc3f7">'+(u1-(u1-u0)*i/2).toFixed(0)+'</text>';
  }
  const pts=(a,Y)=>a.map((v,k)=>X(k).toFixed(1)+','+Y(v).toFixed(1)).join(' ');
  g+='<polyline fill="none" stroke="#ffb74d" stroke-width="2" stroke-linejoin="round" points="'+pts(HD.t,Yt)+'"/>';
  g+='<polyline fill="none" stroke="#4fc3f7" stroke-width="2" stroke-linejoin="round" points="'+pts(HD.u,Yu)+'"/>';
  g+='<text x="'+CL+'" y="'+(CH-6)+'">'+tm(0)+'</text>'+
     '<text x="'+(CL+pw/2)+'" y="'+(CH-6)+'" text-anchor="middle">'+tm(Math.round((n-1)/2))+'</text>'+
     '<text x="'+(CW-CR)+'" y="'+(CH-6)+'" text-anchor="end">'+tm(n-1)+'</text>';
  const k=sel==null?n-1:sel;
  g+='<line x1="'+X(k)+'" x2="'+X(k)+'" y1="'+CT+'" y2="'+(CT+ph)+'" stroke="#ffffff66" stroke-dasharray="3 3"/>'+
     '<circle cx="'+X(k)+'" cy="'+Yt(HD.t[k])+'" r="4" fill="#ffb74d"/><circle cx="'+X(k)+'" cy="'+Yu(HD.u[k])+'" r="4" fill="#4fc3f7"/>';
  $('chart').innerHTML=g;
  $('hsel').innerHTML=(sel==null?'Última leitura ('+tm(k)+')':tm(k))+' &nbsp;·&nbsp; <b style="color:#ffb74d">'+HD.t[k].toFixed(1)+'°C</b> &nbsp;·&nbsp; <b style="color:#4fc3f7">'+HD.u[k].toFixed(1)+'%</b>';
}
function pick(e){
  if(!HD)return;
  const r=$('chart').getBoundingClientRect(),n=HD.t.length;
  const x=(e.clientX-r.left)/r.width*CW;
  draw(Math.max(0,Math.min(n-1,Math.round((x-CL)/(CW-CL-CR)*(n-1)))));
}
async function hist(){
  try{
    const d=await (await fetch('/historico',{cache:'no-store'})).json();
    const n=d.temp.length;
    if(!d.ok){HD=null;$('chart').innerHTML='';$('hsel').textContent='';$('hlab').textContent='Sensor DHT22 sem leitura válida. Confira a ligação (dado no pino D5).';return}
    if(n<2){HD=null;$('chart').innerHTML='';$('hsel').textContent='';$('hlab').textContent='Coletando dados... o gráfico aparece em cerca de '+Math.max(1,Math.ceil(d.proxima/60))+' min.';return}
    HD={t:d.temp,u:d.umid,iv:d.intervalo*1000,t0:Date.now()-(d.intervalo-Math.min(d.proxima,d.intervalo))*1000};
    draw();
    const dur=fmtDur((n-1)*d.intervalo/60);
    $('hlab').textContent='Últimas '+dur+' · Temp '+Math.min(...d.temp).toFixed(1)+' a '+Math.max(...d.temp).toFixed(1)+'°C · Umid '+Math.min(...d.umid).toFixed(1)+' a '+Math.max(...d.umid).toFixed(1)+'%';
  }catch(x){}
}
$('chart').addEventListener('pointermove',pick);
$('chart').addEventListener('pointerdown',pick);
$('chart').addEventListener('pointerleave',()=>draw());
async function tog(){try{await fetch('/display',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'estado=t'});load()}catch(x){}}
load();hist();setInterval(load,5000);setInterval(hist,60000);
</script></body></html>)rawliteral";

// ==========================================================================================
//  8. FUNÇÕES
// ==========================================================================================

// ---- PRINCIPAL ----------------------------------------------------------------------------

/**
 * Inicialização (roda uma vez ao ligar): configura LED, botões, display e sensor; conecta ao WiFi
 * (WiFiManager abre o portal se precisar); sincroniza a hora (NTP); inicia OTA e a interface do
 * OLED; faz as primeiras leituras e a primeira consulta de clima; liga o servidor web.
 */
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  EEPROM.begin(sizeof(Config));
  carregaConfig();

  ledInicia();                          // LED de status (apagado até algo acontecer)
  if (PIN_BOTAO_TELA >= 0) pinMode(PIN_BOTAO_TELA, INPUT_PULLUP);
  if (PIN_BOTAO_RESET >= 0) pinMode(PIN_BOTAO_RESET, INPUT_PULLUP);

  delay(50);
  dht.begin();

  // initialize display
  display.init();
  display.clear();
  display.display();
  aplicaDisplay();                      // respeita o estado salvo já na inicialização

  varreduraI2C();                       // lista no Serial o que respondeu no barramento (0x3C = display)

  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);

  display.drawString(64, 3, "Conectando ao WiFi ");
  display.drawXbm(33, 20, 60, 36, WiFi_Logo_bits);
  display.display();

  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);

  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  std::vector<const char *> menu = {"wifi","wifinoscan","info","param","close","sep","erase","restart","exit"};
  wifiManager.setMenu(menu);
  wifiManager.setClass("invert");

  // Se não conectar em 30 s abre o portal; se ninguém configurar em 3 min, reinicia.
  // As credenciais salvas NUNCA são apagadas automaticamente (uma queda do roteador não pode zerar o WiFi).
  wifiManager.setConnectTimeout(30);
  wifiManager.setConfigPortalTimeout(180);
  ledRede = LED_CONECTANDO;             // pisca rápido enquanto conecta (e com o portal aberto)
  if (!wifiManager.autoConnect("ESP_WeatherStation")) {
    Serial.println("Sem WiFi, reiniciando...");
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 20, "Conexão falhou...");
    display.drawString(64, 34, "Reiniciando");
    display.display();
    delay(2000);
    ESP.restart();
  }

  ledRede = LED_NORMAL;                 // conectou
  ledPulsoOk();

  // Get time from network time service
  aplicaFuso();

  // OTA: permite enviar novo firmware pelo WiFi (Arduino IDE > Porta de rede)
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword("2258");       // senha pedida pela IDE no envio por rede
  ArduinoOTA.begin();

  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  static uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));

  ui.setTargetFPS(30);
  ui.setTimePerFrame((uint16_t)(telaSeg * 1000));   // tempo de cada tela (configurável em /config)
  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);
  ui.setIndicatorPosition(BOTTOM);
  ui.setIndicatorDirection(LEFT_RIGHT);
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frames, numberOfFrames);
  ui.setOverlays(overlays, numberOfOverlays);

  // Inital UI takes care of initalising the display too.
  ui.init();
  aplicaDisplay();                      // ui.init() religa a tela; reaplica o estado salvo
  if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());   // tempo de tela ligada (0 = sempre ligada)

  Serial.println("");

  leSensor();
  if (bateriaAtiva) atualizaBateria();
  updateData(&display);

  server.on("/", HTTP_GET, paginaInicial);
  server.on("/json", HTTP_GET, paginaJson);
  server.on("/historico", HTTP_GET, paginaHistorico);
  server.on("/config", HTTP_GET, paginaConfig);
  server.on("/salvar", HTTP_POST, handleSalvar);
  server.on("/display", HTTP_POST, handleDisplay);
  server.on("/agenda", HTTP_POST, handleAgenda);
  server.on("/tempotela", HTTP_POST, handleTempoTela);
  server.on("/tempoligada", HTTP_POST, handleTempoLigada);
  server.on("/bateria", HTTP_POST, handleBateria);
  server.on("/api", HTTP_POST, handleApi);
  server.on("/enviar", HTTP_POST, handleEnviar);
  server.on("/reset-wifi", HTTP_POST, handleResetWiFi);
  server.on("/reset-geral", HTTP_POST, handleResetGeral);
  server.on("/portal", HTTP_POST, handlePortal);
  server.begin();
}

/**
 * Laço principal (roda continuamente): trata os botões e os timers da tela, atende o servidor web
 * e o OTA, lê sensor e bateria, confere a rede e a agenda, atualiza o clima, envia ao painel e
 * redesenha o OLED.
 */
void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  unsigned long ms = millis();

  bool segurandoReset = trataBotoes(ms);   // botão físico: clique = próxima tela; segurar = apagar WiFi
  verificaTimerTela(ms);                   // apaga a tela quando o tempo de tela ligada acabar
  bool mostrandoMsg = trataMensagemTela(ms);   // mensagem temporária no OLED (ex.: resultado do envio)

  // Estado da rede: pisca devagar se o WiFi cair (como em interruptores inteligentes)
  if ((long)(ms - nextRede) >= 0) {
    nextRede = ms + 1000UL;
    bool conectado = (WiFi.status() == WL_CONNECTED);
    if (!conectado && ledRede != LED_SEM_REDE) {
      ledRede = LED_SEM_REDE;
      Serial.println("WiFi caiu");
    } else if (conectado && ledRede == LED_SEM_REDE) {
      ledRede = LED_NORMAL;
      ledPulsoOk();
      Serial.println("WiFi voltou");
    }
  }

  // Sensor interno lido a cada 10 s
  if ((long)(ms - nextSensorRead) >= 0) {
    leSensor();
    nextSensorRead = ms + SENSOR_INTERVAL_MS;
  }

  if (sensorOk && (long)(ms - nextHist) >= 0) {
    guardaHistorico();
    nextHist = ms + HIST_INTERVAL_MS;
  }

  if ((long)(ms - nextApi) >= 0) {
    if (apiUrl.length() > 0 && deviceKey.length() > 0) {
      bool ok = enviaParaApi();
      nextApi = ms + (ok ? PAINEL_INTERVAL_MS : PAINEL_RETRY_MS);
    } else {
      nextApi = ms + 60000UL;           // API não configurada: confere de novo em 1 min
    }
  }

  if (bateriaAtiva && (long)(ms - nextBateria) >= 0) {
    atualizaBateria();
    nextBateria = ms + 30000UL;         // a cada 30 s
  }

  if ((long)(ms - nextAgenda) >= 0) {
    verificaAgenda();
    nextAgenda = ms + 5000UL;
  }

  if ((long)(ms - nextWeatherUpdate) >= 0) {
    readyForWeatherUpdate = true;
  }

  if (readyForWeatherUpdate && (!displayLigado || ui.getUiState()->frameState == FIXED)) {
    updateData(&display);
  }

  if (displayLigado && !segurandoReset && !mostrandoMsg) {
    int remainingTimeBudget = ui.update();

    if (remainingTimeBudget > 0) {
      delay(remainingTimeBudget);
    }
  } else {
    delay(50);                          // tela apagada: não gasta CPU nem I2C desenhando
  }
}

// ---- CONFIGURAÇÃO SALVA (EEPROM) E FUSO HORÁRIO -------------------------------------------

/**
 * Lê da EEPROM as configurações salvas (chave, cidade, idioma, fuso, tela, agenda, bateria, API).
 * Se nada foi salvo ainda, mantém os padrões do código.
 */
void carregaConfig() {
  Config c;
  EEPROM.get(0, c);
  if (c.magic != CFG_MAGIC) return;     // nada salvo ainda: mantém os padrões do código
  c.apiKey[sizeof(c.apiKey) - 1] = 0;
  c.locationId[sizeof(c.locationId) - 1] = 0;
  c.lang[sizeof(c.lang) - 1] = 0;
  OPEN_WEATHER_MAP_APP_ID = String(c.apiKey);
  OPEN_WEATHER_MAP_LOCATION_ID = String(c.locationId);
  OPEN_WEATHER_MAP_LANGUAGE = String(c.lang);
  displayLigado = (c.displayLigado != 0);
  agendaAtiva = (c.agendaAtiva == 1);
  if (c.fusoOk == 0xA5 && c.fuso >= -720 && c.fuso <= 840) {   // só usa se foi salvo de fato
    fusoMin = c.fuso;
    horarioVerao = (c.verao == 1);
  }
  if (c.bateriaOk == 0xA5) bateriaAtiva = (c.bateria == 1);
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
  snprintf(c.lang, sizeof(c.lang), "%s", OPEN_WEATHER_MAP_LANGUAGE.c_str());
  c.displayLigado = displayLigado ? 1 : 0;
  c.agendaAtiva = agendaAtiva ? 1 : 0;
  c.fusoOk = 0xA5;
  c.fuso = fusoMin;
  c.verao = horarioVerao ? 1 : 0;
  c.bateriaOk = 0xA5;
  c.bateria = bateriaAtiva ? 1 : 0;
  c.apiOk = 0xA5;
  snprintf(c.apiUrl, sizeof(c.apiUrl), "%s", apiUrl.c_str());
  snprintf(c.deviceKey, sizeof(c.deviceKey), "%s", deviceKey.c_str());
  c.deviceIdOk = 0xA5;
  snprintf(c.deviceId, sizeof(c.deviceId), "%s", apiDeviceId.c_str());
  c.telaSeg = telaSeg;
  c.telaTimeout = telaTimeoutMin;
  c.agendaLiga = agendaLiga;
  c.agendaDesliga = agendaDesliga;
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
  configTime((int)fusoMin * 60, horarioVerao ? 3600 : 0, "pool.ntp.org");
}

// ---- LED DE STATUS ------------------------------------------------------------------------

/** Acende ou apaga os LEDs azuis da placa (GPIO2 e GPIO16, acesos em nível baixo). */
void ledEscreve(bool aceso) {
  digitalWrite(2, aceso ? LOW : HIGH);  // LED do módulo (GPIO2 = D4), aceso em LOW
  digitalWrite(16, aceso ? LOW : HIGH); // 2º LED de algumas NodeMCU (GPIO16 = D0)
}

/**
 * Executada a cada 100 ms pelo Ticker: decide se o LED fica aceso conforme o padrão atual
 * (conectando, sem WiFi, enviando, aviso de reset ou pulso de sucesso/falha).
 */
void ledTick() {
  bool aceso = false;
  if (LED_STATUS_ATIVO) {
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
  pinMode(16, OUTPUT);
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
  if (!displayLigado) {                                 // tela apagada: o clique só acende
    bool noite = (agendaEstadoAtual() == 0);            // dentro do horário agendado como desligado?
    displayLigado = true;
    aplicaDisplay();
    telaAcordadaNoite = noite;
    armaTimerTela(noite ? TELA_ACORDAR_NOITE_MS : telaDuracaoPadraoMs());   // à noite: poucos segundos
    return;
  }
  ui.nextFrame();
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
  display.displayOn();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  if (geral) {
    display.drawString(64, 0, "RESET GERAL");
    display.drawString(64, 12, "Apaga WiFi e ajustes");
    display.drawString(64, 24, "Solte para cancelar");
    display.drawProgressBar(2, 42, 124, 10, pct);
  } else {
    display.drawString(64, 2, "Apagando o WiFi...");
    display.drawString(64, 14, "Solte para cancelar");
    display.drawProgressBar(2, 32, 124, 10, pct);
  }
  display.display();
}

/** Apaga só a rede WiFi salva e reinicia: na volta, abre o portal 'ESP_WeatherStation'. */
void resetWiFiPeloBotao() {
  display.displayOn();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 14, "WiFi apagado");
  display.drawString(64, 28, "Reiniciando...");
  display.display();
  delay(1200);
  wifiManager.resetSettings();
  ESP.restart();
}

/** Reset geral: apaga os ajustes salvos e o WiFi e reinicia (padrão de fábrica). */
void resetGeralPeloBotao() {
  display.displayOn();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 14, "Reset geral");
  display.drawString(64, 28, "Apagando tudo...");
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
  mostraMensagemTela("ENVIO AO PAINEL", "Enviando...", "", 20000);   // aparece já durante o envio, que pode demorar
  bool ok = enviaParaApi();
  if (ok) nextApi = millis() + PAINEL_INTERVAL_MS;                 // recomeça o ciclo do envio automático
  mostraMensagemTela("ENVIO AO PAINEL", ok ? "Enviado com sucesso" : "Falha no envio", apiUltimoMsg, TELA_MENSAGEM_MS);
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
  Serial.println("Varredura I2C:");
  int achados = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  dispositivo em 0x%02X\n", a);
      achados++;
    }
  }
  if (!achados) Serial.println("  nenhum dispositivo encontrado");
}

/**
 * Lê temperatura e umidade do DHT e ignora leituras inválidas (mantém o último valor bom). Chamada
 * a cada SENSOR_INTERVAL_MS.
 */
void leSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(h) || isnan(t)) {
    Serial.println("Falhou a leitura do sensor DHT22!");
    return;                             // mantém o último valor válido
  }
  localTemp = t;
  localHum = h;
  sensorOk = true;
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

/** Texto descritivo da faixa de tensão da bateria (mostrado na aba Bateria). */
const char* bateriaSituacao(float v) {
  if (v >= 4.15) return "Totalmente carregada";
  if (v >= 4.06) return "Carga alta";
  if (v >= 3.98) return "Carga alta / boa";
  if (v >= 3.89) return "Carga média-alta";
  if (v >= 3.80) return "Carga média";
  if (v >= 3.72) return "Meia carga";
  if (v >= 3.65) return "Carga moderada";
  if (v >= 3.58) return "Carga baixa";
  if (v >= 3.39) return "Nível crítico (recomenda-se recarregar)";
  if (v >= 3.20) return "Muito baixa";
  return "Descarga profunda (risco de danificar a célula)";
}

// ---- CLIMA (OpenWeatherMap) ---------------------------------------------------------------

/**
 * Consulta o OpenWeatherMap (clima atual e previsão) mostrando o progresso no OLED. No boot, se
 * faltar a chave da API, mostra um aviso com o endereço da /config. Define weatherOk e agenda a
 * próxima consulta (ou nova tentativa se falhar).
 */
void updateData(OLEDDisplay *display) {
  readyForWeatherUpdate = false;
  static bool primeiraVez = true;        // 1ª consulta = a do boot: mostra avisos mais longos no OLED
  bool noBoot = primeiraVez;
  primeiraVez = false;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sem WiFi, nova tentativa em 1 min");
    nextWeatherUpdate = millis() + RETRY_INTERVAL_MS;
    return;
  }

  if (OPEN_WEATHER_MAP_APP_ID.length() == 0) {
    Serial.println("Sem chave da API: configure em /config");
    if (noBoot) {                        // no boot, avisa no OLED em vez de deixar a tela preta
      display->clear();
      display->setTextAlignment(TEXT_ALIGN_CENTER);
      display->setFont(ArialMT_Plain_10);
      display->drawString(64, 2, "Sem chave da API");
      display->drawString(64, 18, "Abra no navegador:");
      display->drawString(64, 32, WiFi.localIP().toString() + "/config");
      display->display();
      delay(3500);
    }
    weatherOk = false;
    nextWeatherUpdate = millis() + RETRY_INTERVAL_MS;
    return;
  }

  drawProgress(display, 20, "Atualizando o clima...");
  currentWeather.cityName = "";          // se continuar vazio depois, a consulta falhou
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);

  drawProgress(display, 60, "Atualizando previsões...");
  forecastClient.updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);

  weatherOk = currentWeather.cityName.length() > 0;
  drawProgress(display, 100, weatherOk ? "Pronto..." : "Falha ao obter o clima");
  delay(noBoot ? (weatherOk ? 700 : 2500) : 300);   // no boot a tela de progresso fica visível por mais tempo

  nextWeatherUpdate = millis() + (weatherOk ? UPDATE_INTERVAL_MS : API_RETRY_MS);
}

// ---- DISPLAY OLED: TELAS E ÍCONES ---------------------------------------------------------

/** Liga ou desliga fisicamente o OLED conforme a variável displayLigado. */
void aplicaDisplay() {
  if (displayLigado) display.displayOn();
  else display.displayOff();
}

/** Mostra no OLED uma mensagem com barra de progresso (usada durante a atualização do clima). */
void drawProgress(OLEDDisplay *display, int percentage, String label) {
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
    display.displayOn();
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
    if (msgLigouTela) {
      msgLigouTela = false;
      aplicaDisplay();                  // volta a apagar a tela, se ela estava apagada
    }
    return false;
  }
  return true;
}

/** Tela 1 do OLED: dia da semana, data e hora. */
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[24];

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);

  snprintf_P(buff, sizeof(buff), PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 5 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  snprintf_P(buff, sizeof(buff), PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

/** Tela 2 do OLED: umidade e temperatura internas do sensor, com uma casa decimal. */
void drawClimaInterno(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(22 + x, y, "CLIMA INTERNO");
  display->drawString(0 + x, 12 + y, "Umid");
  display->drawString(66 + x, 12 + y, "Temp");

  // Valores com 1 casa decimal; a unidade vai em fonte pequena logo depois, para caber nas duas colunas
  String hs = sensorOk ? String(localHum, 1) : String("--");
  String ts = sensorOk ? String(localTemp, 1) : String("--");
  display->setFont(ArialMT_Plain_24);
  int wh = display->getStringWidth(hs);
  int wt = display->getStringWidth(ts);
  display->drawString(0 + x, 22 + y, hs);
  display->drawString(66 + x, 22 + y, ts);
  if (sensorOk) {
    display->setFont(ArialMT_Plain_10);
    display->drawString(0 + x + wh + 1, 36 + y, "%");
    display->drawString(66 + x + wt + 1, 36 + y, "°C");
  }
}

/** Tela 3 do OLED: ícone, temperatura, cidade e descrição do clima atual. */
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(60 + x, 1 + y, temp);

  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(60 + x, 26 + y, currentWeather.cityName);

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}

/** Tela 4 do OLED ('PREVISÃO HOJE'): mín, máx, umidade, pressão, visibilidade e vento. */
void drawCurrentWeatherDetails(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(20 + x, y, "PREVISÃO HOJE");

  String unit = IS_METRIC ? "°C" : "°F";

  display->drawString(x, 12 + y, "Min: " + String(currentWeather.tempMin, 1) + unit);
  display->drawString(x, 24 + y, "Max: " + String(currentWeather.tempMax, 1) + unit);
  display->drawString(x, 36 + y, "Hum: " + String(currentWeather.humidity) + "%");

  // A API devolve: pressão em hPa, visibilidade em metros e vento em m/s (metric) ou mph (imperial)
  float wind = IS_METRIC ? currentWeather.windSpeed * 3.6 : currentWeather.windSpeed;
  display->drawString(60 + x, 12 + y, "Pre: " + String(currentWeather.pressure) + "hPa");
  display->drawString(60 + x, 24 + y, "Vis: " + String(currentWeather.visibility / 1000.0, 1) + "km");
  display->drawString(60 + x, 36 + y, "Ven: " + String(wind, 0) + (IS_METRIC ? "km/h" : "mph"));
}

/** Tela 5 do OLED: previsão dos próximos 3 dias (usa drawForecastDetails para cada dia). */
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}

/** Desenha uma coluna da previsão: dia da semana, ícone e temperatura. */
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex) {
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

/** Tela 6 do OLED: bateria, sinal do WiFi (ícone, dBm e %), nome da rede (SSID) e IP. */
void drawWiFi(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char aux[48];
  int32_t rssi = WiFi.RSSI();

  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);

  // Linha 1: bateria
  if (bateriaAtiva) {
    drawBateria(display, x, y + 2, bateriaPct);
    display->drawString(x + 24, y, String(bateriaPct) + "%  " + String(bateriaV, 2) + "V");
  } else {
    display->drawString(x, y, "Bateria: desativada");
  }

  // Linha 2: potência do sinal (ícone + dBm + %)
  if (WiFi.status() == WL_CONNECTED) {
    int pct = getRSSIasQuality(rssi);
    drawSinalWiFi(display, x, 12 + y, pct);
    snprintf(aux, sizeof(aux), "%ddBm  %d%%", (int)rssi, pct);
    display->drawString(x + 24, 12 + y, aux);
  } else {
    drawSinalWiFi(display, x, 12 + y, 0);
    display->drawString(x + 24, 12 + y, "sem WiFi");
  }

  // Linha 3: nome da rede
  snprintf(aux, sizeof(aux), "SSID: %s", WiFi.SSID().c_str());
  display->drawString(x, 24 + y, aux);

  // Linha 4: IP
  snprintf(aux, sizeof(aux), "IP: %s", WiFi.localIP().toString().c_str());
  display->drawString(x, 36 + y, aux);
}

/** Barra inferior fixa em todas as telas: hora à esquerda e temperatura externa à direita. */
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  snprintf_P(buff, sizeof(buff), PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  String temp = String(currentWeather.temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->drawString(128, 54, temp);
  display->drawHorizontalLine(0, 52, 128);
}

/** Desenha o ícone de bateria (20x9 px) com o nível proporcional ao percentual. */
void drawBateria(OLEDDisplay *display, int x, int y, int pct) {
  display->drawRect(x, y, 18, 9);                          // corpo
  display->fillRect(x + 18, y + 2, 2, 5);                  // ponta
  display->fillRect(x + 2, y + 2, (pct * 14) / 100, 5);    // nível
}

/**
 * Desenha o ícone de sinal de WiFi (4 barras crescentes, 15x10 px), igual ao da página web. Barras
 * cheias = força do sinal; as demais ficam só com o contorno.
 */
void drawSinalWiFi(OLEDDisplay *display, int x, int y, int pct) {
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

/** Converte o sinal em dBm para qualidade de 0 a 100%. */
int getRSSIasQuality(int rssi) {
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
    displayLigado = (estado == 1);
    aplicaDisplay();
    telaAcordadaNoite = false;
    if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());   // ao ligar pela agenda, vale o tempo de tela ligada
    else telaContando = false;
  }
}

// ---- WIFI (portal do WiFiManager) ---------------------------------------------------------

/** Chamada pelo WiFiManager ao abrir o portal de configuração: mostra no OLED como se conectar. */
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  display.displayOn();                  // mostra as instruções do portal mesmo com a tela apagada
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 10, "Wifi Manager");
  display.drawString(64, 20, "Conecte ao AP");
  display.drawString(64, 30, myWiFiManager->getConfigPortalSSID());
  display.drawString(64, 40, "Para configurar o Wifi");
  display.display();
}

/** Chamada pelo WiFiManager depois de salvar a rede WiFi: mostra a confirmação no OLED. */
void saveConfigCallback() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 10, "Configuração salva");
  display.display();
}

// ---- PAINEL WEB (API REST) ----------------------------------------------------------------

/** Devolve o device_id enviado ao painel: o salvo em /config ou, se vazio, 'esp8266-' + ID do chip. */
String idDispositivo() {
  if (apiDeviceId.length() > 0) return apiDeviceId;
  String id = String(ESP.getChipId(), HEX);
  id.toLowerCase();
  return "esp8266-" + id;               // padrão, se nenhum ID foi salvo em /config
}

/**
 * Monta o corpo JSON enviado à API do painel. Todos os campos ficam aqui, fáceis de ajustar. O
 * horário não vai no corpo: o servidor registra a hora em que recebe.
 */
String montaPayloadApi() {
  String j;
  j.reserve(360);
  j += "{\"device_id\":\"" + jsonEscape(idDispositivo()) + "\"";
  j += ",\"temperature\":" + String(localTemp, 1);
  j += ",\"humidity\":" + String(localHum, 1);
  j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  j += ",\"rssi\":" + String(WiFi.RSSI());
  String cid = OPEN_WEATHER_MAP_LOCATION_ID;           // código da cidade (OpenWeatherMap), enviado como número
  bool soDigitos = cid.length() > 0;
  for (size_t k = 0; k < cid.length(); k++) {
    if (!isDigit(cid[k])) soDigitos = false;
  }
  j += ",\"city_id\":";
  j += soDigitos ? cid : String("null");
  j += ",\"city_name\":\"" + jsonEscape(currentWeather.cityName) + "\"";
  j += ",\"firmware\":\"" FIRMWARE_VERSAO "\"";
  j += ",\"battery_active\":";
  j += bateriaAtiva ? "true" : "false";
  j += ",\"battery\":" + String(bateriaAtiva ? bateriaPct : 0);
  j += ",\"battery_voltage\":" + String(bateriaAtiva ? bateriaV : 0.0f, 2);
  j += "}";
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

  if (apiUrl.length() == 0 || deviceKey.length() == 0) {
    apiUltimoMsg = "Configure a URL da API e a DEVICE_KEY";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    apiUltimoMsg = "Sem WiFi";
    return false;
  }
  if (!sensorOk) {
    apiUltimoMsg = "Sensor DHT22 sem leitura válida";
    return false;
  }

  bool https = apiUrl.startsWith("https://");
  if (https && ESP.getFreeHeap() < 16000) {
    apiUltimoMsg = "Memória insuficiente para HTTPS (" + String(ESP.getFreeHeap()) + " bytes livres)";
    return false;
  }

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
    apiUltimoMsg = "URL inválida";
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-key", deviceKey);          // DEVICE_KEY configurada em /config

  int codigo = http.POST(corpo);                      // > 0: código HTTP, < 0: erro de conexão
  if (codigo > 0) apiUltimoMsg = "HTTP " + String(codigo);
  else apiUltimoMsg = HTTPClient::errorToString(codigo);
  http.end();

  apiUltimoOk = (codigo >= 200 && codigo < 300);
  Serial.println("API -> " + apiUltimoMsg);
  return apiUltimoOk;
}

/**
 * Envolve o envio com o LED: pisca rápido enquanto envia e, no fim, sinaliza sucesso (1 acendida)
 * ou falha (3 piscadas).
 */
bool enviaParaApi() {
  ledEnviando = true;
  bool ok = enviaParaApiInterno();
  ledEnviando = false;
  if (ok) ledPulsoOk(); else ledPulsoFalha();
  return ok;
}

// ---- SERVIDOR WEB: UTILITÁRIOS DE TEXTO ---------------------------------------------------

/** Escapa caracteres especiais para colocar texto com segurança dentro do HTML. */
String htmlEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t k = 0; k < s.length(); k++) {
    char c = s[k];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
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
  char b[8];
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

/** Redireciona (303) para a aba certa da /config, com o código da mensagem de resultado. */
void redirecionaConfig(const char* r) {
  const char* aba = "clima";
  if (!strcmp(r, "disp") || !strcmp(r, "agenda") || !strcmp(r, "hora") || !strcmp(r, "horaigual") || !strcmp(r, "tempo") || !strcmp(r, "tempo_erro") || !strcmp(r, "tempoligada") || !strcmp(r, "tempoligada_erro")) aba = "tela";
  else if (!strcmp(r, "bat")) aba = "bateria";
  server.sendHeader("Location", String("/config?aba=") + aba + "&r=" + r);
  server.send(303, "text/plain", "");
}

/** Formata uma quantidade de bytes como KB ou MB. */
String fmtBytes(uint32_t b) {
  if (b >= 1048576UL) return String(b / 1048576.0, 1) + " MB";
  return String(b / 1024UL) + " KB";
}

/** Formata segundos como 'Xd Xh Xmin Xs'. */
String formataUptime(unsigned long s) {
  unsigned long d = s / 86400UL;
  unsigned long h = (s % 86400UL) / 3600UL;
  unsigned long m = (s % 3600UL) / 60UL;
  unsigned long seg = s % 60UL;
  String o = "";
  if (d) o += String(d) + "d ";
  o += String(h) + "h " + String(m) + "min " + String(seg) + "s";
  return o;
}

/** Monta uma linha de tabela HTML (rótulo e valor). */
String linhaInfo(const char* rotulo, const String& valor) {
  return "<tr><td>" + String(rotulo) + "</td><td>" + valor + "</td></tr>";
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
  j += "{\"unid\":\"";
  j += IS_METRIC ? "°C" : "°F";
  j += "\",\"erro\":";
  j += weatherOk ? "false" : "true";
  j += ",\"display\":";
  j += displayLigado ? "true" : "false";
  j += ",\"bateria\":";
  if (bateriaAtiva) j += "{\"v\":" + String(bateriaV, 2) + ",\"pct\":" + String(bateriaPct) + "}";
  else j += "null";
  j += ",\"interno\":{";
  if (sensorOk) {
    j += "\"temp\":" + String(localTemp, 1) + ",\"umid\":" + String(localHum, 1);
  } else {
    j += "\"temp\":null,\"umid\":null";
  }
  j += "},\"externo\":{";
  j += "\"cidade\":\"" + jsonEscape(currentWeather.cityName) + "\",";
  j += "\"desc\":\"" + jsonEscape(currentWeather.description) + "\",";
  j += "\"icone\":\"" + jsonEscape(currentWeather.icon) + "\",";
  j += "\"temp\":" + String(currentWeather.temp, 1) + ",";
  j += "\"min\":" + String(currentWeather.tempMin, 1) + ",";
  j += "\"max\":" + String(currentWeather.tempMax, 1) + ",";
  j += "\"umid\":" + String(currentWeather.humidity) + ",";
  j += "\"pressao\":" + String(currentWeather.pressure) + ",";
  j += "\"vento\":" + String(windKmh, 0) + ",";
  j += "\"ventoDir\":" + String(currentWeather.windDeg, 0) + ",";
  j += "\"visib\":" + String(currentWeather.visibility / 1000.0, 1) + ",";
  j += "\"nuvens\":" + String(currentWeather.clouds) + ",";
  j += "\"nascer\":" + String((unsigned long)currentWeather.sunrise) + ",";
  j += "\"por\":" + String((unsigned long)currentWeather.sunset);
  j += "},\"previsao\":[";
  for (uint8_t k = 0; k < MAX_FORECASTS; k++) {
    if (k) j += ",";
    j += "{\"dia\":" + String((unsigned long)forecasts[k].observationTime) + ",";
    j += "\"icone\":\"" + jsonEscape(forecasts[k].icon) + "\",";
    j += "\"temp\":" + String(forecasts[k].temp, 1) + "}";
  }
  j += "],\"rssi\":" + String(WiFi.RSSI()) + ",\"rssiPct\":" + String(getRSSIasQuality(WiFi.RSSI())) + "}";
  server.send(200, "application/json", j);
}

/** GET /historico: histórico interno (do mais antigo para o mais novo) em JSON. */
void paginaHistorico() {
  String j;
  j.reserve(64 + histCount * 12);
  long prox = (long)(nextHist - millis()) / 1000;
  if (prox < 0) prox = 0;
  j += "{\"ok\":";
  j += sensorOk ? "true" : "false";
  j += ",\"proxima\":" + String(prox);
  j += ",\"intervalo\":" + String(HIST_INTERVAL_MS / 1000UL) + ",\"temp\":[";
  uint8_t start = (histCount < HIST_SIZE) ? 0 : histHead;
  for (uint8_t k = 0; k < histCount; k++) {
    if (k) j += ",";
    j += String(histTemp[(start + k) % HIST_SIZE] / 10.0, 1);
  }
  j += "],\"umid\":[";
  for (uint8_t k = 0; k < histCount; k++) {
    if (k) j += ",";
    j += String(histHum[(start + k) % HIST_SIZE] / 10.0, 1);
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// ---- SERVIDOR WEB: PÁGINA /config (ABAS E TABELAS) ----------------------------------------

/** SVG do ícone de sinal de WiFi (4 barras) usado na aba WiFi. */
String iconeSinal(int pct) {
  int n = 0;
  if (pct >= 75) n = 4;
  else if (pct >= 50) n = 3;
  else if (pct >= 25) n = 2;
  else if (pct > 0) n = 1;

  String s = "<svg width=\"34\" height=\"22\" viewBox=\"0 0 34 22\" style=\"vertical-align:middle\">";
  for (int k = 0; k < 4; k++) {
    int h = 6 + k * 5;                  // alturas 6, 11, 16, 21
    s += "<rect x=\"" + String(k * 9) + "\" y=\"" + String(22 - h) + "\" width=\"7\" height=\"" + String(h) +
         "\" rx=\"1.5\" fill=\"" + (k < n ? "#81d4fa" : "#ffffff40") + "\"/>";
  }
  s += "</svg>";
  return s;
}

/** SVG do ícone de bateria estilo celular; a cor muda conforme a carga. */
String iconeBateria(int pct) {
  const char* cor = pct <= BAT_CRITICA_PCT ? "#ef5350" : (pct <= BAT_AVISO_PCT ? "#ffb74d" : "#66bb6a");
  int w = (22 * pct) / 100;
  String s = "<svg width=\"34\" height=\"16\" viewBox=\"0 0 34 16\" style=\"vertical-align:middle\">"
             "<rect x=\"1\" y=\"1\" width=\"28\" height=\"14\" rx=\"3.5\" fill=\"none\" stroke=\"#ffffffcc\" stroke-width=\"2\"/>"
             "<rect x=\"4\" y=\"4\" width=\"" + String(w) + "\" height=\"8\" rx=\"1.5\" fill=\"" + cor + "\"/>"
             "<rect x=\"30\" y=\"5\" width=\"3\" height=\"6\" rx=\"1.2\" fill=\"#ffffffcc\"/></svg>";
  return s;
}

/** Tabela HTML da aba Bateria: carga, tensão, estado e situação. */
String tabelaBateria() {
  String t = "<table>";
  if (!bateriaAtiva) {
    t += linhaInfo("Estado", "Desativada (ative na aba Bateria)");
  } else {
    const char* estado = bateriaPct <= BAT_CRITICA_PCT ? "⚠️ Crítica" : (bateriaPct <= BAT_AVISO_PCT ? "⚠️ Baixa" : "✅ Normal");
    t += linhaInfo("Carga", iconeBateria(bateriaPct) + " " + String(bateriaPct) + "%");
    t += linhaInfo("Tensão", String(bateriaV, 2) + " V");
    t += linhaInfo("Estado", estado);
    t += linhaInfo("Situação", bateriaSituacao(bateriaV));
  }
  t += "</table><div class=\"mut\">Estimativa pela tensão lida no A0. Ajuste BAT_CALIB no código comparando com um multímetro.</div>";
  return t;
}

/** Tabela HTML com os dados da conexão WiFi (SSID, sinal, IP, canal, MAC...). */
String tabelaWiFi() {
  String t;
  t.reserve(1600);
  t = "<table>";

  bool conectado = (WiFi.status() == WL_CONNECTED);
  t += linhaInfo("Estado", conectado ? "✅ Conectado" : "⚠️ Desconectado");

  if (conectado) {
    int rssi = WiFi.RSSI();
    int pct = getRSSIasQuality(rssi);
    const char* nivel = pct >= 75 ? "Excelente" : (pct >= 50 ? "Bom" : (pct >= 25 ? "Regular" : "Fraco"));

    const char* phy = "?";
    switch (WiFi.getPhyMode()) {
      case WIFI_PHY_MODE_11B: phy = "802.11b"; break;
      case WIFI_PHY_MODE_11G: phy = "802.11g"; break;
      case WIFI_PHY_MODE_11N: phy = "802.11n"; break;
      default: break;
    }

    t += linhaInfo("Rede (SSID)", htmlEsc(WiFi.SSID()));
    t += linhaInfo("Sinal", iconeSinal(pct) + " " + String(rssi) + " dBm · " + String(pct) + "% · " + nivel);
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
  t += linhaInfo("Reconexão automática", WiFi.getAutoReconnect() ? "Sim" : "Não");

  const char* sono = "?";
  switch (WiFi.getSleepMode()) {
    case WIFI_NONE_SLEEP: sono = "Desligado"; break;
    case WIFI_LIGHT_SLEEP: sono = "Light sleep"; break;
    case WIFI_MODEM_SLEEP: sono = "Modem sleep"; break;
    default: break;
  }
  t += linhaInfo("Economia de energia", sono);

  t += "</table>";
  return t;
}

/** Tabela HTML com informações do ESP8266 (tempo ligado, memória, flash, firmware, sensor...). */
String tabelaSistema() {
  String t;
  t.reserve(1800);
  t = "<table>";

  t += linhaInfo("Tempo ligado", formataUptime(millis() / 1000UL));
  t += linhaInfo("Último reinício", htmlEsc(ESP.getResetReason()));

  time_t agora = time(nullptr);
  if (agora > 100000) {
    struct tm* ti = localtime(&agora);
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900, ti->tm_hour, ti->tm_min, ti->tm_sec);
    t += linhaInfo("Data e hora (NTP)", buf);
  } else {
    t += linhaInfo("Data e hora (NTP)", "Sem sincronização");
  }

  String chip = String(ESP.getChipId(), HEX);
  chip.toUpperCase();
  String flashId = String(ESP.getFlashChipId(), HEX);
  flashId.toUpperCase();

  t += linhaInfo("Chip ID", chip);
  t += linhaInfo("CPU", String(ESP.getCpuFreqMHz()) + " MHz");
  t += linhaInfo("Flash (chip real)", fmtBytes(ESP.getFlashChipRealSize()));
  t += linhaInfo("Flash (config. da IDE)", fmtBytes(ESP.getFlashChipSize()));
  t += linhaInfo("Velocidade da flash", String(ESP.getFlashChipSpeed() / 1000000UL) + " MHz");
  t += linhaInfo("ID da flash", flashId);
  t += linhaInfo("Programa", fmtBytes(ESP.getSketchSize()) + " usados · " + fmtBytes(ESP.getFreeSketchSpace()) + " livres");
  t += linhaInfo("Memória livre (heap)", String(ESP.getFreeHeap()) + " bytes");
  t += linhaInfo("Maior bloco livre", String(ESP.getMaxFreeBlockSize()) + " bytes");
  t += linhaInfo("Fragmentação do heap", String(ESP.getHeapFragmentation()) + "%");
  t += linhaInfo("Firmware", FIRMWARE_VERSAO);
  t += linhaInfo("Sensor DHT22", sensorOk ? "OK" : "sem leitura válida (confira o dado no pino D5)");
  t += linhaInfo("Core ESP8266", htmlEsc(ESP.getCoreVersion()));
  t += linhaInfo("SDK", htmlEsc(String(ESP.getSdkVersion())));
  t += linhaInfo("Compilado em", String(__DATE__) + " " + String(__TIME__));

  t += "</table>";
  return t;
}

/** Valida o nome da aba pedida em /config (se for inválido, usa 'clima'). */
String abaValida(const String& a) {
  for (uint8_t k = 0; k < ABAS_COUNT; k++) {
    if (a == ABAS_ID[k]) return a;
  }
  return "clima";
}

/**
 * GET /config: monta a página de configuração com abas (Clima, Tela, Bateria, WiFi e Sistema),
 * enviada em partes para economizar RAM.
 */
void paginaConfig() {
  String aba = abaValida(server.arg("aba"));
  String r = server.arg("r");
  String msg = "";
  if (r == "ok") msg = "✅ Configurações salvas. Atualizando o clima...";
  else if (r == "chave") msg = "⚠️ Chave inválida: use só letras e números (20 a 40 caracteres).";
  else if (r == "local") msg = "⚠️ ID da cidade inválido: informe o número (ou cole o endereço da cidade).";
  else if (r == "idioma") msg = "⚠️ Idioma inválido.";
  else if (r == "disp") msg = "✅ Tela atualizada.";
  else if (r == "tempo") msg = "✅ Tempo de cada tela salvo.";
  else if (r == "tempo_erro") msg = "⚠️ Tempo inválido: escolha de 5 a 30 segundos.";
  else if (r == "tempoligada") msg = "✅ Tempo de tela ligada salvo.";
  else if (r == "tempoligada_erro") msg = "⚠️ Tempo de tela ligada inválido.";
  else if (r == "agenda") msg = "✅ Horário salvo.";
  else if (r == "hora") msg = "⚠️ Horário inválido.";
  else if (r == "horaigual") msg = "⚠️ Os horários de ligar e de apagar não podem ser iguais.";
  else if (r == "fuso") msg = "⚠️ Fuso horário inválido.";
  else if (r == "bat") msg = "✅ Configuração da bateria salva.";
  else if (r == "api") msg = "✅ Dados do painel salvos.";
  else if (r == "apiurl") msg = "⚠️ URL inválida: use http:// ou https:// e não deixe espaços.";
  else if (r == "apiid") msg = "⚠️ ID do dispositivo inválido: use até 32 caracteres (letras, números, - _ .).";
  else if (r == "apikey") msg = "⚠️ DEVICE_KEY inválida: use de 8 a 64 caracteres, sem espaços.";
  else if (r == "envio_ok") msg = "✅ Dados enviados ao painel.";
  else if (r == "envio_erro") msg = "⚠️ Falha no envio: " + htmlEsc(apiUltimoMsg);

  // Resposta em partes (chunked): evita montar a página inteira na RAM
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  String h;
  h.reserve(4500);
  h = "<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Configurações</title>"
      "<style>body{font-family:system-ui,sans-serif;max-width:480px;margin:1em auto;padding:0 1em;background:#0f2027;color:#fff}"
      "label{display:block;margin-top:1em;font-weight:600}"
      "input,select{width:100%;padding:.7em;margin-top:.3em;border:0;border-radius:10px;font-size:1rem}"
      "button{padding:.7em 1.2em;margin:.6em 0;border:0;border-radius:10px;font-size:1rem}"
      ".mut{color:#a9c1cf;font-size:.85rem;margin-top:.3em}a{color:#81d4fa}"
      "table{width:100%;border-collapse:collapse;margin:.4em 0}"
      "td{padding:.5em .3em;border-bottom:1px solid #ffffff22;vertical-align:middle;font-size:.9rem;word-break:break-word}"
      "td:first-child{color:#a9c1cf;width:42%}input[type=checkbox]{width:auto;margin-right:.6em}"
      ".back{display:inline-flex;align-items:center;gap:6px;padding:.4em .9em .4em .5em;border-radius:12px;background:#ffffff14;text-decoration:none;color:#fff;font-size:.9rem}"
      "h2 svg{vertical-align:-4px;margin-right:8px}"
      "nav{display:flex;flex-wrap:wrap;gap:6px;margin:.6em 0 1em}"
      "nav a{padding:.45em .9em;border-radius:999px;background:#ffffff14;text-decoration:none;color:#fff;font-size:.9rem}"
      "nav a.on{background:#81d4fa;color:#0f2027;font-weight:600}</style></head><body>"
      "<p><a class=\"back\" href=\"/\"><svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M15 5l-7 7 7 7\"/></svg>Voltar</a></p>"
      "<h2><svg width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\"><circle cx=\"12\" cy=\"12\" r=\"8.6\" stroke-width=\"3.4\" stroke-dasharray=\"3.377 3.377\"/><circle cx=\"12\" cy=\"12\" r=\"6.2\" stroke-width=\"2.4\"/><circle cx=\"12\" cy=\"12\" r=\"2.4\" stroke-width=\"2\"/></svg>Configurações</h2><nav>";
  for (uint8_t k = 0; k < ABAS_COUNT; k++) {
    h += "<a href=\"/config?aba=" + String(ABAS_ID[k]) + "\"";
    if (aba == ABAS_ID[k]) h += " class=\"on\"";
    h += ">" + String(ABAS_NOME[k]) + "</a>";
  }
  h += "</nav>";
  if (msg.length()) h += "<p>" + msg + "</p>";

  if (aba == "clima") {
    // ---------- Aba Clima: chave, cidade, idioma e fuso ----------
    String estado;
    if (weatherOk) estado = "✅ Clima recebido de " + htmlEsc(currentWeather.cityName);
    else estado = "⚠️ Sem dados do clima (confira a chave e o ID da cidade)";

    String masked = "cole a chave aqui";
    if (OPEN_WEATHER_MAP_APP_ID.length() >= 4) {
      masked = "•••••••• " + OPEN_WEATHER_MAP_APP_ID.substring(OPEN_WEATHER_MAP_APP_ID.length() - 4) + " (salva)";
    }

    h += "<h3>Clima (OpenWeatherMap)</h3><p class=\"mut\">" + estado + "</p>"
         "<form method=\"POST\" action=\"/salvar\">"
         "<label>Chave da API</label>"
         "<input name=\"apiKey\" type=\"text\" autocomplete=\"off\" autocapitalize=\"off\" placeholder=\"" + htmlEsc(masked) + "\">"
         "<div class=\"mut\">Deixe em branco para manter a atual. Crie a sua em "
         "<a href=\"https://home.openweathermap.org/api_keys\" target=\"_blank\" rel=\"noopener\">home.openweathermap.org/api_keys</a> "
         "(chaves novas podem levar um tempo para ativar).</div>"
         "<label>ID da cidade</label>"
         "<input name=\"locId\" type=\"text\" inputmode=\"numeric\" value=\"" + htmlEsc(OPEN_WEATHER_MAP_LOCATION_ID) + "\">"
         "<div class=\"mut\">Busque a cidade em <a href=\"https://openweathermap.org/find\" target=\"_blank\" rel=\"noopener\">openweathermap.org/find</a> "
         "e copie o número no fim do endereço (.../city/<b>3463011</b>). Também pode colar o endereço inteiro.</div>"
         "<label>Idioma</label><select name=\"lang\">";
    for (uint8_t k = 0; k < LANG_COUNT; k++) {
      h += "<option value=\"" + String(LANG_CODES[k]) + "\"";
      if (OPEN_WEATHER_MAP_LANGUAGE == LANG_CODES[k]) h += " selected";
      h += ">" + String(LANG_NAMES[k]) + "</option>";
    }
    server.sendContent(h);
    h = "";
    h += "</select>"
         "<label>Fuso horário</label><select name=\"fuso\">";
    for (uint8_t k = 0; k < FUSO_COUNT; k++) {
      h += "<option value=\"" + String(FUSOS[k].min) + "\"";
      if (FUSOS[k].min == fusoMin) h += " selected";
      h += ">" + String(FUSOS[k].nome) + "</option>";
    }
    h += "</select>"
         "<label><input type=\"checkbox\" name=\"verao\" value=\"1\"";
    if (horarioVerao) h += " checked";
    h += "> Horário de verão (+1 h)</label>"
         "<button type=\"submit\">Salvar e atualizar</button></form>";

    // ---------- Painel web (API): URL, DEVICE_KEY e envio imediato ----------
    server.sendContent(h);
    h = "";
    String maskedKey = "cole a DEVICE_KEY aqui";
    if (deviceKey.length() >= 4) {
      maskedKey = "•••••••• " + deviceKey.substring(deviceKey.length() - 4) + " (salva)";
    }
    String cidadeEnviada = currentWeather.cityName.length() ? htmlEsc(currentWeather.cityName) : String("(nome ainda não recebido)");
    cidadeEnviada += " · ID " + htmlEsc(OPEN_WEATHER_MAP_LOCATION_ID);
    String ultimo;
    if (!apiTentou) ultimo = "nenhum envio ainda";
    else ultimo = "há " + formataUptime((millis() - apiUltimoMs) / 1000UL) + " · " + (apiUltimoOk ? "✅ " : "⚠️ ") + htmlEsc(apiUltimoMsg);

    h += "<hr><h3>Painel web (API)</h3>"
         "<form method=\"POST\" action=\"/api\">"
         "<label>ID do dispositivo (device_id)</label>"
         "<input name=\"deviceId\" type=\"text\" autocapitalize=\"off\" value=\"" + htmlEsc(idDispositivo()) + "\">"
         "<div class=\"mut\">Precisa ser igual ao ID cadastrado no painel.</div>"
         "<label>URL da API</label>"
         "<input name=\"apiUrl\" type=\"text\" inputmode=\"url\" autocapitalize=\"off\" placeholder=\"https://seu-app.vercel.app/api/...\" value=\"" + htmlEsc(apiUrl) + "\">"
         "<label>DEVICE_KEY</label>"
         "<input name=\"deviceKey\" type=\"text\" autocomplete=\"off\" autocapitalize=\"off\" placeholder=\"" + htmlEsc(maskedKey) + "\">"
         "<div class=\"mut\">Deixe a chave em branco para manter a atual. Use https:// (a Vercel redireciona o http). "
         "Sem URL, o envio fica desligado.</div>"
         "<button type=\"submit\">Salvar dados do painel</button></form>"
         "<p class=\"mut\">Cidade enviada ao painel: " + cidadeEnviada + "</p>"
         "<p class=\"mut\">Envio automático a cada " + String(PAINEL_INTERVAL_MS / 60000UL) + " min. Último envio: " + ultimo + "</p>"
         "<form method=\"POST\" action=\"/enviar\">"
         "<button type=\"submit\">Enviar dados agora</button></form>";

  } else if (aba == "tela") {
    // ---------- Aba Tela: ligar/apagar e horário automático ----------
    h += "<h3>Tela OLED</h3>";
    h += displayLigado ? "<p class=\"mut\">Estado: ligada</p>" : "<p class=\"mut\">Estado: apagada</p>";
    h += "<form method=\"POST\" action=\"/display\">"
         "<input type=\"hidden\" name=\"estado\" value=\"t\"><input type=\"hidden\" name=\"volta\" value=\"1\">"
         "<button type=\"submit\">";
    h += displayLigado ? "Apagar tela" : "Ligar tela";
    h += "</button></form>";
    h += "<h4>Passagem das telas</h4>"
         "<form method=\"POST\" action=\"/tempotela\">"
         "<label>Tempo de cada tela no display</label><select name=\"seg\">";
    for (uint8_t sgs = 5; sgs <= 30; sgs += 5) {
      h += "<option value=\"" + String(sgs) + "\"";
      if (sgs == telaSeg) h += " selected";
      h += ">" + String(sgs) + " segundos</option>";
    }
    h += "</select><div class=\"mut\">Padrão: 5 s. O botão físico passa para a próxima tela quando você quiser.</div>"
         "<button type=\"submit\">Salvar tempo</button></form>";
    h += "<h4>Tempo de tela ligada</h4>"
         "<form method=\"POST\" action=\"/tempoligada\">"
         "<label>Apagar a tela sozinha depois de</label><select name=\"min\">";
    static const uint8_t OPCOES_MIN[] = {0, 1, 5, 10, 15, 30, 60};
    for (uint8_t k = 0; k < 7; k++) {
      h += "<option value=\"" + String(OPCOES_MIN[k]) + "\"";
      if (OPCOES_MIN[k] == telaTimeoutMin) h += " selected";
      h += ">";
      if (OPCOES_MIN[k] == 0) h += "Sempre ligada";
      else h += String(OPCOES_MIN[k]) + (OPCOES_MIN[k] == 1 ? " minuto" : " minutos");
      h += "</option>";
    }
    h += "</select><div class=\"mut\">Para acender, aperte o botão das telas uma vez; depois ele volta a passar as telas. "
         "No horário agendado como desligado, o clique acende a tela só por 10 s.</div>"
         "<button type=\"submit\">Salvar tempo</button></form>";
    h += "<h4>Horário automático</h4>"
         "<form method=\"POST\" action=\"/agenda\">"
         "<label><input type=\"checkbox\" name=\"ativa\" value=\"1\"";
    if (agendaAtiva) h += " checked";
    h += "> Ligar e apagar a tela sozinha</label>"
         "<label>Ligar às</label><input type=\"time\" name=\"liga\" value=\"" + formataHora(agendaLiga) + "\">"
         "<label>Apagar às</label><input type=\"time\" name=\"desliga\" value=\"" + formataHora(agendaDesliga) + "\">"
         "<div class=\"mut\">O botão manual vale até o próximo horário programado. "
         "Depende da hora da internet (NTP), então só funciona depois de sincronizar.</div>"
         "<button type=\"submit\">Salvar horário</button></form>";

  } else if (aba == "bateria") {
    // ---------- Aba Bateria: ativar/desativar e estado ----------
    h += "<h3>Bateria</h3>"
         "<form method=\"POST\" action=\"/bateria\">"
         "<label><input type=\"checkbox\" name=\"ativa\" value=\"1\"";
    if (bateriaAtiva) h += " checked";
    h += "> Este ESP tem bateria (leitura no A0)</label>"
         "<div class=\"mut\">Desmarcado, a leitura, o ícone e os avisos de bateria ficam desligados. "
         "Marque só se a bateria estiver ligada ao A0 pelo divisor de tensão.</div>"
         "<button type=\"submit\">Salvar</button></form>";
    if (bateriaAtiva) h += tabelaBateria();

  } else if (aba == "wifi") {
    // ---------- Aba WiFi: dados da conexão e ações ----------
    h += "<h3>WiFi</h3>";
    h += tabelaWiFi();
    h += "<form method=\"POST\" action=\"/portal\" onsubmit=\"return confirm('Abrir o portal de configuração? A estação reinicia ao terminar.')\">"
         "<button type=\"submit\">Abrir portal de configuração</button></form>"
         "<form method=\"POST\" action=\"/reset-wifi\" onsubmit=\"return confirm('Apagar a rede WiFi salva e reiniciar?')\">"
         "<button type=\"submit\">Apagar dados de WiFi</button></form>";

  } else {
    // ---------- Aba Sistema: informações do ESP8266 ----------
    h += "<h3>Informações do ESP8266</h3>";
    h += tabelaSistema();
    h += "<hr><h3>Reset geral</h3>"
         "<p class=\"mut\">Apaga o WiFi e <b>todos os ajustes salvos</b> (chave do OpenWeatherMap, cidade, idioma, fuso, agenda, bateria, "
         "tempo das telas e dados do painel) e reinicia no padrão do código. Também pode ser feito segurando o botão de reset geral por 10 s.</p>"
         "<form method=\"POST\" action=\"/reset-geral\" onsubmit=\"return confirm('Apagar o WiFi e TODOS os ajustes e reiniciar?')\">"
         "<button type=\"submit\" style=\"background:#ef5350;color:#fff\">Reset geral</button></form>";
  }

  h += "</body></html>";
  server.sendContent(h);
  server.sendContent("");               // encerra a resposta (chunked)
}

// ---- SERVIDOR WEB: AÇÕES (POST) -----------------------------------------------------------

/** POST /salvar: valida e salva chave, cidade, idioma e fuso do OpenWeatherMap. */
void handleSalvar() {
  String key = server.arg("apiKey");
  key.trim();
  String lang = server.arg("lang");
  String locRaw = server.arg("locId");

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
  if (loc.length() < 1 || loc.length() > 10) { redirecionaConfig("local"); return; }

  bool okLang = false;
  for (uint8_t k = 0; k < LANG_COUNT; k++) {
    if (lang == LANG_CODES[k]) okLang = true;
  }
  if (!okLang) { redirecionaConfig("idioma"); return; }

  // Fuso horário (se o formulário não trouxer o campo, mantém o atual)
  bool trocaFuso = server.hasArg("fuso");
  int fuso = fusoMin;
  if (trocaFuso) {
    fuso = server.arg("fuso").toInt();
    bool okFuso = false;
    for (uint8_t k = 0; k < FUSO_COUNT; k++) {
      if (FUSOS[k].min == fuso) okFuso = true;
    }
    if (!okFuso) { redirecionaConfig("fuso"); return; }
  }

  if (key.length() > 0) OPEN_WEATHER_MAP_APP_ID = key;   // em branco = mantém a atual
  OPEN_WEATHER_MAP_LOCATION_ID = loc;
  OPEN_WEATHER_MAP_LANGUAGE = lang;
  if (trocaFuso) {
    fusoMin = fuso;
    horarioVerao = server.hasArg("verao");
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
  String url = server.arg("apiUrl");
  url.trim();
  String key = server.arg("deviceKey");
  key.trim();
  String id = server.arg("deviceId");
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
  bool ok = enviaParaApi();
  if (ok) nextApi = millis() + PAINEL_INTERVAL_MS;       // recomeça o ciclo do envio automático
  redirecionaConfig(ok ? "envio_ok" : "envio_erro");
}

/**
 * POST /tempoligada: salva o tempo de tela ligada (0 = sempre ligada; ou 1, 5, 10, 15, 30, 60
 * min).
 */
void handleTempoLigada() {
  int mn = server.arg("min").toInt();
  bool valido = server.hasArg("min") && (mn == 0 || mn == 1 || mn == 5 || mn == 10 || mn == 15 || mn == 30 || mn == 60);
  if (!valido) { redirecionaConfig("tempoligada_erro"); return; }
  telaTimeoutMin = mn;
  if (displayLigado && !telaAcordadaNoite) armaTimerTela(telaDuracaoPadraoMs());   // passa a contar a partir de agora
  salvaConfig();
  redirecionaConfig("tempoligada");
}

/** POST /tempotela: salva o tempo de cada tela do OLED (5 a 30 s, de 5 em 5). */
void handleTempoTela() {
  int seg = server.arg("seg").toInt();
  if (seg < 5 || seg > 30 || seg % 5 != 0) { redirecionaConfig("tempo_erro"); return; }
  telaSeg = seg;
  ui.setTimePerFrame((uint16_t)(telaSeg * 1000));
  salvaConfig();
  redirecionaConfig("tempo");
}

/** POST /bateria: liga ou desliga a leitura da bateria (A0) neste ESP. */
void handleBateria() {
  bateriaAtiva = server.hasArg("ativa");
  if (bateriaAtiva) {
    bateriaV = 0;                        // recomeça a média
    atualizaBateria();
    nextBateria = millis() + 30000UL;
  }
  salvaConfig();
  redirecionaConfig("bat");
}

/** POST /agenda: salva o horário automático (ligar e apagar a tela). */
void handleAgenda() {
  bool ativa = server.hasArg("ativa");
  int liga = parseHora(server.arg("liga"));
  int desliga = parseHora(server.arg("desliga"));
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
  String v = server.arg("estado");
  if (v == "1") displayLigado = true;
  else if (v == "0") displayLigado = false;
  else displayLigado = !displayLigado;
  aplicaDisplay();
  telaAcordadaNoite = false;
  if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());
  else telaContando = false;
  salvaConfig();
  if (server.hasArg("volta")) {
    redirecionaConfig("disp");
  } else {
    server.send(200, "application/json", displayLigado ? "{\"display\":true}" : "{\"display\":false}");
  }
}

/** POST /reset-wifi: apaga a rede WiFi salva e reinicia. */
void handleResetWiFi() {
  server.send(200, "text/html", "<meta charset=\"utf-8\"><p>Dados de WiFi apagados. Reiniciando...</p>");
  delay(500);
  wifiManager.resetSettings();
  ESP.restart();
}

/**
 * POST /reset-geral: apaga o WiFi e todos os ajustes salvos e reinicia (igual ao botão físico de
 * reset geral).
 */
void handleResetGeral() {
  server.send(200, "text/html", "<meta charset=\"utf-8\"><p>Reset geral feito. Reiniciando... Conecte-se à rede ESP_WeatherStation para configurar de novo.</p>");
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
  server.send(200, "text/html", "<meta charset=\"utf-8\"><p>Conecte-se à rede ESP_WeatherStation para configurar. A estação reinicia ao terminar.</p>");
  delay(500);
  server.stop();                        // o portal usa a porta 80
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.startConfigPortal("ESP_WeatherStation");
  ESP.restart();
}
