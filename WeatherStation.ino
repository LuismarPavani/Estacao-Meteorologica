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
 *   2. Seleção da placa e pinagem        <- em WeatherStationConfig.h (junto com as senhas e chaves)
 *   3. Configurações do projeto          <- em WeatherStationConfig.h
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
#include <bearssl/bearssl.h>              // SHA-256 do login da /config (BearSSL já vem no core do ESP8266)
#include <coredecls.h>                  // settimeofday_cb()
#include <ESPHTTPClient.h>
#include <JsonListener.h>
#include <JsonStreamingParser.h>           // lê o JSON aos poucos (busca de cidades)

// ---- Portal de configuração do WiFi -------------------------------------------------------
#include <DNSServer.h>
#include <WiFiManager.h>                // https://github.com/tzapu/WiFiManager

// ---- Hora (NTP) ---------------------------------------------------------------------------
#include <time.h>                       // time() ctime()
#include <new>                          // new (std::nothrow): blocos da previsão sem travar se faltar memória
#include <sys/time.h>                   // struct timeval

// ---- Display OLED -------------------------------------------------------------------------
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "Wire.h"

// ---- Clima (OpenWeatherMap) ---------------------------------------------------------------
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"

// ---- Sol e Lua, calculados no próprio ESP (vêm na biblioteca ESP8266 Weather Station) ------
#include "Astronomy.h"                  // fase e iluminação da Lua (só precisa da data)
#include "SunMoonCalc.h"                // nascer/pôr do sol e da Lua e altura do sol (precisa das coordenadas)

// ---- Fontes e imagens do projeto (arquivos .h na mesma pasta) -----------------------------
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"

// ---- Sensor de temperatura e umidade ------------------------------------------------------
#include "DHT.h"

// ==========================================================================================
//  2. SELEÇÃO DA PLACA E PINAGEM  e  3. CONFIGURAÇÕES DO PROJETO
//  Ficam no arquivo WeatherStationConfig.h (mesma pasta), junto com as senhas e chaves.
//  Edite só aquele arquivo: aqui não há nada para ajustar.
// ==========================================================================================
#include "WeatherStationConfig.h"

// Nome do sensor nas mensagens (página, seção Sistema e Serial), conforme o DHTTYPE escolhido acima
#if DHTTYPE == DHT11
  #define SENSOR_NOME "DHT11"
#elif DHTTYPE == DHT12
  #define SENSOR_NOME "DHT12"
#elif DHTTYPE == DHT21
  #define SENSOR_NOME "DHT21"
#else
  #define SENSOR_NOME "DHT22"
#endif

// ==========================================================================================
//  4. TIPOS
// ==========================================================================================

// Marca da área de configurações na EEPROM (interna: mudar apaga os ajustes salvos)
#define CFG_MAGIC 0x57533031UL          // "WS01"

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
  uint8_t energiaOk;                    // 0xA5 = escolha do sensor de energia salva
  uint8_t energia;                      // 1 = este ESP tem o sensor de energia (placa UPS)
  uint8_t ledOk;                        // 0xA5 = escolhas do LED salvas
  uint8_t led;                          // bit 0 = LED de status ligado; bit 1 = seguir o horário automático da tela
  uint8_t brilhoOk;                     // 0xA5 = brilho salvo
  uint8_t brilho;                       // brilho de dia, em % (5 a 100, de 5 em 5)
  uint8_t brilhoNoite;                  // brilho no horário noturno da agenda, em % (5 a 100)
  uint8_t noiteModo;                    // 0 = apagar a tela à noite; 1 = só diminuir o brilho
  uint8_t sessaoOk;                     // 0xA5 = sal do login salvo (gravado à parte: vale mesmo sem o resto salvo)
  uint8_t sessaoSal[16];                // sal aleatório do login: trocá-lo desconecta todos os navegadores
  uint8_t telasOk;                      // 0xA5 = escolha das telas salva
  uint8_t telas;                        // bit k = tela k do OLED aparece (ver TELAS[])
  uint8_t noiteSolOk;                   // 0xA5 = escolha de como a noite é definida salva
  uint8_t noiteSol;                     // 1 = brilho noturno do pôr ao nascer do sol; 0 = pela agenda
  uint8_t coordOk;                      // 0xA5 = coordenadas da cidade salvas (vêm do OpenWeatherMap)
  float lat;                            // latitude da cidade
  float lon;                            // longitude da cidade
};

// Leitor da resposta da busca de cidades (API de geocodificação do OpenWeatherMap), lida aos poucos
// para não guardar o JSON inteiro na RAM (cada cidade traz dezenas de nomes em outras línguas).
// Formato: [{"name":"Campinas","local_names":{"pt":"Campinas",...},"lat":-22.9,"lon":-47.06,"country":"BR","state":"São Paulo"}, ...]
// Em erro vem um objeto: {"cod":401,"message":"Invalid API key ..."}
class BuscaCidade : public JsonListener {
  public:
    static const uint8_t MAX = 5;
    struct Resultado { String nome; String nomePt; String estado; String pais; float lat = 0; float lon = 0; };
    Resultado r[MAX];
    uint8_t n = 0;
    String erro = "";
    void whitespace(char c) override {}
    void startDocument() override {}
    void endDocument() override {}
    void startArray() override {}
    void endArray() override {}
    void key(String k) override { chave = k; }
    void startObject() override {
      nivel++;
      if (nivel == 2) emNomesLocais = (chave == "local_names");
    }
    void endObject() override {
      if (nivel == 1 && n < MAX && r[n].nome.length()) n++;   // terminou uma cidade
      if (nivel == 2) emNomesLocais = false;
      nivel--;
    }
    void value(String v) override {
      if (n >= MAX) return;
      if (nivel == 1) {
        if (chave == "name") r[n].nome = v;
        else if (chave == "lat") r[n].lat = v.toFloat();
        else if (chave == "lon") r[n].lon = v.toFloat();
        else if (chave == "country") r[n].pais = v;
        else if (chave == "state") r[n].estado = v;
        else if (chave == "message") erro = v;
      } else if (nivel == 2 && emNomesLocais && chave == "pt") {
        r[n].nomePt = v;                  // nome em português, quando a API tem
      }
    }
  private:
    String chave = "";
    int nivel = 0;
    bool emNomesLocais = false;
};

// Opção da lista de fusos horários (minutos em relação ao UTC e nome)
struct Fuso { int16_t min; const char* nome; };

// Estados contínuos do LED (rede)
enum { LED_NORMAL = 0, LED_CONECTANDO = 1, LED_SEM_REDE = 2 };

// Tipos de mensagem temporária no OLED
enum { MSG_TEXTO = 0, MSG_ENVIO_ANDAMENTO = 1, MSG_ENVIO_OK = 2, MSG_ENVIO_FALHA = 3, MSG_IP = 4 };

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
bool climaRecebido = false;             // true depois da 1ª consulta com dados (até lá o OLED mostra '--')

// ---- Coordenadas da cidade (vêm na resposta do OpenWeatherMap e ficam salvas na EEPROM) ---
// Com elas, o clima é consultado por latitude/longitude (a consulta por ID é obsoleta) e o Sol/Lua
// são calculados. Antes da 1ª resposta, a consulta usa o ID da cidade.
bool coordOk = false;
float cidadeLat = 0;
float cidadeLon = 0;

// ---- Próximas 24 h (calculadas com os blocos de 3 h da previsão) --------------------------
const uint8_t MAX_BLOCOS_PREVISAO = 32;   // 4 dias de blocos de 3 h: cobre as 24 h e o meio-dia dos 3 dias seguintes
bool prox24Ok = false;
float prox24Min = 0;                    // mínima das próximas 24 h
float prox24Max = 0;                    // máxima das próximas 24 h
float prox24Chuva = 0;                  // chuva prevista nas próximas 24 h, em mm
float prox24Sens = 0;                   // sensação térmica do próximo bloco

// ---- Sol e Lua (recalculados a cada minuto) -----------------------------------------------
bool astroOk = false;                   // true = horários do sol/Lua calculados (precisa da hora e das coordenadas)
time_t solNasce = 0, solPoe = 0, luaNasce = 0, luaPoe = 0;
float solAltura = 0;                    // altura do sol em graus (negativa = abaixo do horizonte)
uint8_t luaFase = 0;                    // 0 = nova ... 4 = cheia ... 7
float luaIlum = 0;                      // fração iluminada da Lua (0 a 1)
bool luaOk = false;                     // true = fase da Lua calculada (precisa só da hora)
unsigned long nextAstro = 0;
bool readyForWeatherUpdate = false;
unsigned long nextWeatherUpdate = 0;
time_t now;

// ---- Sensor interno e histórico -----------------------------------------------------------
float localTemp = 0;
float localHum = 0;
bool sensorOk = false;                     // true depois da primeira leitura válida
bool sensorUltimaOk = false;                // true se a última tentativa de leitura deu certo (vai ao painel como sensor_ok)
unsigned long nextSensorRead = 0;
int16_t histTemp[HIST_SIZE];            // temperatura x10
uint16_t histHum[HIST_SIZE];            // umidade x10 (1 casa decimal)
uint8_t histHead = 0;
uint8_t histCount = 0;
unsigned long nextHist = 0;

// ---- Rede ---------------------------------------------------------------------------------
unsigned long nextRede = 0;
bool otaIniciado = false;               // true depois de ArduinoOTA.begin() (só com WiFi conectado)

// ---- Login da /config (cookie lembrado por LOGIN_DIAS dias) -------------------------------
uint8_t sessaoSal[16];                  // sal aleatório: o cookie é SHA-256(sal + CONFIG_SENHA)
bool sessaoSalOk = false;               // false = ainda sem sal (é gerado no 1º login, com o WiFi ligado)
uint8_t loginFalhas = 0;                // senhas erradas seguidas
unsigned long loginLiberaEm = 0;        // após 5 erros, novas tentativas só depois deste millis()

// ---- Tela: ligada/apagada, tempo de cada tela e tempo de tela ligada ----------------------
bool displayLigado = true;              // false = tela OLED apagada (salvo na EEPROM)
uint8_t telaSeg = 5;                     // segundos de cada tela do OLED (5 a 30, de 5 em 5)
uint8_t telaTimeoutMin = 0;             // tempo de tela ligada: 0 = sempre ligada; 1, 5, 10, 15, 30 ou 60 min
unsigned long telaDesligaEm = 0;        // millis() em que a tela apaga sozinha
bool telaContando = false;              // true = há uma contagem para apagar a tela
bool telaAcordadaNoite = false;         // true = tela acordada pelo botão no horário agendado como desligado

// ---- Brilho do OLED -----------------------------------------------------------------------
uint8_t brilhoPct = BRILHO_PADRAO;             // brilho de dia, em % (5 a 100, de 5 em 5)
uint8_t brilhoNoitePct = BRILHO_NOITE_PADRAO;  // brilho no horário de tela desligada da agenda
bool noiteDiminui = NOITE_DIMINUI_PADRAO;      // true = à noite a tela só diminui o brilho; false = apaga
uint8_t brilhoAplicado = 0xFF;                 // último % enviado ao display (0xFF = enviar de novo)
bool noitePeloSol = NOITE_PELO_SOL_PADRAO;     // true = noite do pôr ao nascer do sol; false = pela agenda

// ---- Telas do OLED escolhidas em /config ---------------------------------------------------
uint8_t telasMask = TELAS_PADRAO & 0x7F;       // bit k = tela k aparece (ver TELAS[])
bool ipMostrado = false;                       // a tela 'WiFi conectado' já apareceu neste boot

// ---- Mensagem temporária no OLED (ex.: resultado do envio ao painel pelo botão) -----------
String msgTitulo = "";                  // 1ª linha da mensagem
String msgLinha1 = "";                  // 2ª linha
String msgLinha2 = "";                  // 3ª linha (quebra em até 2 linhas se for longa)
uint8_t msgTipo = 0;                    // 0 = texto simples; MSG_ENVIO_* = telas do envio ao painel pelo botão
bool envioNaTela = false;               // true durante o envio pelo botão: mostra as etapas no OLED
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

// ---- Energia externa (placa UPS) ----------------------------------------------------------
bool energiaAtiva = ENERGIA_ATIVA_PADRAO;   // este ESP tem o sensor de energia? (escolhido em /config)
bool energiaExterna = false;            // true = há 5 V na entrada (energia externa); false = rodando na bateria
bool energiaUltima = false;             // última leitura bruta do pino (para o filtro)
uint8_t energiaContagem = 0;            // quantas leituras iguais seguidas
unsigned long nextEnergia = 0;          // próxima leitura do pino

// ---- Envio ao painel web (API) ------------------------------------------------------------
String apiUrl = API_URL_PADRAO;
String deviceKey = DEVICE_KEY_PADRAO;
String apiDeviceId = "";                // device_id enviado ao painel ("" = esp8266-<chip>)
bool apiTentou = false;
bool apiUltimoOk = false;
String apiUltimoMsg = "";
unsigned long apiUltimoMs = 0;
unsigned long apiUltimoDurMs = 0;       // quanto tempo levou o último envio (mostrado no OLED)
unsigned long nextApi = 20000UL;        // 1º envio 20 s depois de ligar

// ---- LED de status (usadas também pelo Ticker, por isso volatile) -------------------------
volatile uint8_t ledRede = LED_NORMAL;  // estado contínuo da rede
volatile bool ledEnviando = false;      // true durante o envio ao painel
volatile bool ledAviso = false;         // true enquanto o botão é segurado para apagar o WiFi
volatile bool ledStatusAtivo = LED_STATUS_ATIVO;   // LED de status ligado? (escolhido em /config → Tela)
bool ledSegueAgenda = false;            // true = LED apagado no horário agendado como tela desligada (escolhido em /config)
volatile bool ledSilencio = false;      // true = LED silenciado agora pela agenda (atualizado a cada 1 s no loop)
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
String nomePino(int gpio);
void leSensor();
void guardaHistorico();

// ---- BATERIA ------------------------------------------------------------------------------
float lerTensaoBateria();
int bateriaPercentual(float v);
void atualizaBateria();
const char* bateriaSituacao(float v);

// ---- ENERGIA EXTERNA (placa UPS) ----------------------------------------------------------
void energiaLeAgora();
void atualizaEnergia(unsigned long ms);

// ---- CLIMA (OpenWeatherMap) ---------------------------------------------------------------
void updateData(OLEDDisplay *display);

// ---- DISPLAY OLED: TELAS E ÍCONES ---------------------------------------------------------
void aplicaDisplay();
void desenhaAbertura();
bool brilhoValido(int v);
bool noiteAgora();
uint8_t brilhoAgora();
void ligaTela();
void drawProgress(OLEDDisplay *display, int percentage, String label);
void mostraMensagemTela(const String& titulo, const String& l1, const String& l2, unsigned long dur);
void desenhaMensagemTela();
bool trataMensagemTela(unsigned long ms);
void desenhaCabecalho(const char* titulo);
void desenhaIconeNuvemEnvio(int cx, int cy);
void desenhaIconeOk(int cx, int cy);
void desenhaIconeFalha(int cx, int cy);
String cabeNaLargura(const String& s, int largura);
void desenhaEnvioEtapa(int pct, const char* texto);
void desenhaResultadoEnvio();
bool horaOk();
void desenhaSemClima(OLEDDisplay *display, int16_t x, int16_t y, const char* titulo);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawClimaInterno(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeatherDetails(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawWiFi(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void desenhaIndicadores(OLEDDisplay *display, OLEDDisplayUiState* state);
void drawSolLua(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaLua(OLEDDisplay *display, int cx, int cy, int r, uint8_t fase, float ilum, bool sul);
void desenhaSeta(OLEDDisplay *display, int x, int y, bool cima);
void desenhaSolPequeno(OLEDDisplay *display, int cx, int cy);
void desenhaLuaPequena(OLEDDisplay *display, int cx, int cy);
String horaCurta(time_t t);
const char* nomeFaseLua(uint8_t fase);
void atualizaAstro();
uint8_t contaTelas(uint8_t mask);
void aplicaTelas();
void desenhaConectando(uint8_t passo);
void desenhaIconeWiFiGrande(int x, int y, int pct);
void desenhaTelaIp();
void calculaPrevisao(OpenWeatherMapForecastData* b, uint8_t n);
void drawBateria(OLEDDisplay *display, int x, int y, int pct);
void drawSinalWiFi(OLEDDisplay *display, int x, int y, int pct);
void drawTomada(OLEDDisplay *display, int x, int y, bool ligada);
int getRSSIasQuality(int rssi);

// ---- TELA: TEMPO LIGADA E AGENDA ----------------------------------------------------------
void armaTimerTela(unsigned long dur);
unsigned long telaDuracaoPadraoMs();
void verificaTimerTela(unsigned long ms);
int8_t agendaEstadoAtual();
void verificaAgenda();

// ---- WIFI (portal do WiFiManager) E OTA ---------------------------------------------------
void iniciaOta();
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
String semPorta(String h);
String hostDaUrl(const String& url);
bool hostEhDesteEsp(const String& host);
bool requisicaoSegura();
void redirecionaConfig(const char* r);
String fmtBytes(uint32_t b);
String formataUptime(unsigned long s);
String linhaInfoP(const __FlashStringHelper* rotulo, const String& valor);
// Os rótulos das linhas de informação (dezenas de textos fixos) ficam na flash, não na RAM
#define linhaInfo(rotulo, valor) linhaInfoP(F(rotulo), (valor))

// ---- SERVIDOR WEB: LOGIN DA /config -------------------------------------------------------
bool loginAtivo();
String tokenSessao();
bool comparaSeguro(const String& a, const String& b);
void gravaSalSessao();
void geraSalSessao();
bool logado();
bool exigeLogin();
void paginaLogin();
void handleLogin();
void handleSair();
void handleSairTodos();

// ---- SERVIDOR WEB: PÁGINA PRINCIPAL E DADOS EM JSON ---------------------------------------
void paginaInicial();
void paginaJson();
void paginaHistorico();

// ---- SERVIDOR WEB: PÁGINA /config (SEÇÕES E TABELAS) ----------------------------------------
String iconeSinal(int pct);
String iconeBateria(int pct);
String tabelaBateria();
String tabelaWiFi();
String tabelaSistema();
String tempoCurto(unsigned long s);
String msgConfig(const String& r, bool& erro);
String secaoResumo(uint8_t k);
void cfgCabecalho(const String& titulo, const char* voltaHref, const char* voltaTexto, const String& r);
void cfgRodape(String& h);
String itemSwitch(const char* nome, const String& rotulo, bool ligado, bool autoEnvia, const String& sub = "");
String itemSelect(const String& rotulo, const char* nome, bool autoEnvia);
String opcao(const String& valor, const String& texto, bool sel);
void cfgMenu(const String& r);
void cfgClima(const String& r);
void cfgDisplay(const String& r);
void cfgHora(const String& r);
void cfgBateria(const String& r);
void cfgPainel(const String& r);
void cfgWiFi(const String& r);
void cfgLed(const String& r);
void cfgSistema(const String& r);
void paginaConfig();
void handleHora();

// ---- SERVIDOR WEB: AÇÕES (POST) -----------------------------------------------------------
void handleSalvar();
void handleApi();
void handleEnviar();
void handleTempoLigada();
void handleTempoTela();
void handleBateria();
void handleLed();
void handleBrilho();
void handleTelas();
void handleBuscaCidade();
int8_t parseCoord(String s, float& out, float minimo, float maximo);
String urlEncode(const String& s);
void handleAgenda();
void handleDisplay();
void handleResetWiFi();
void handleResetGeral();
void handlePortal();

// ==========================================================================================
//  7. TABELAS
// ==========================================================================================

// ---- Telas do OLED (deslizam da direita para a esquerda) e barra inferior -----------------
// Todas as telas, na ordem em que aparecem. Em /config → Tela escolhe-se quais entram (telasMask, bit k = TELAS[k]).
FrameCallback TELAS[] = {drawDateTime, drawClimaInterno, drawCurrentWeather, drawCurrentWeatherDetails, drawForecast, drawSolLua, drawWiFi};
static const char* const TELAS_NOME[] = {"Data e hora", "Clima interno", "Clima atual", "Próximas 24 h", "Próximos dias", "Sol e Lua", "WiFi e bateria"};
const uint8_t TELAS_TOTAL = 7;
const uint8_t TELA_WIFI = 6;            // índice da tela 'WiFi e bateria' (a que mostra o IP)
FrameCallback framesAtivos[7];          // só as telas marcadas (montado por aplicaTelas())
uint8_t numTelasAtivas = 0;
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

// ---- Seções da página /config (menu no estilo dos Ajustes do iPhone) ----------------------
// Menu com uma linha por seção (ícone, nome e resumo do estado); cada seção abre em /config?s=<id>.
// O menu separa as seções em grupos: depois da 3ª e da 6ª começa um grupo novo.
struct SecaoCfg { const char* id; const char* nome; const char* icone; const char* cor; };
static const SecaoCfg SECOES[] = {
  {"clima",   "Cidade e clima",    "⛅",  "#4fc3f7"},
  {"display", "Display",           "🖥️", "#ffb74d"},
  {"hora",    "Data e hora",       "🕒",  "#9fa8da"},
  {"bateria", "Bateria e energia", "🔋",  "#66bb6a"},
  {"painel",  "Painel web",        "📡",  "#ce93d8"},
  {"wifi",    "WiFi",              "📶",  "#81d4fa"},
  {"led",     "LED de status",     "💡",  "#90a4ae"},
  {"sistema", "Sistema",           "⚙️", "#78909c"},
};
static const uint8_t SECOES_TOTAL = sizeof(SECOES) / sizeof(SECOES[0]);

// Estilo da /config, guardado na flash e enviado uma vez por página (cartões, linhas, chaves liga/desliga)
static const char CFG_CSS[] PROGMEM = R"rawliteral(<style>
*{box-sizing:border-box}
body{margin:0;min-height:100vh;color:#fff;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-text-size-adjust:100%;
  background:radial-gradient(1100px 460px at 85% -8%,rgba(56,209,255,.16),transparent),
  linear-gradient(#0b182c,#07101e) fixed}
.wrap{max-width:540px;margin:0 auto;padding:14px 14px 32px}
a{color:#81d4fa}
.vol{display:inline-block;color:#81d4fa;text-decoration:none;font-size:.95rem;padding:4px 0}
h1{font-size:1.9rem;font-weight:700;margin:.15em 0 .7em}
.cap{color:#a9c1cf;font-size:.72rem;letter-spacing:.07em;text-transform:uppercase;margin:0 6px 6px}
.grp{background:#ffffff12;border-radius:14px;overflow:hidden;margin:0 0 20px}
.it,.fld{border-top:1px solid #ffffff1a;margin-top:-1px}
.it{display:flex;align-items:center;gap:12px;padding:10px 14px;min-height:50px;color:#fff;text-decoration:none}
.fld{padding:11px 14px}
.ic{width:30px;height:30px;border-radius:8px;display:grid;place-items:center;font-size:17px;flex:none}
.tx{flex:1;min-width:0}
.tx small{display:block;color:#a9c1cf;font-size:.78rem;margin-top:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tx small.w{white-space:normal}
.val{color:#a9c1cf;text-align:right;max-width:62%;word-break:break-word;font-size:.92rem}
.chev{color:#ffffff66;font-size:1.4rem;line-height:1;transition:transform .2s}
summary{list-style:none;cursor:pointer}summary::-webkit-details-marker{display:none}
details[open] summary .chev{transform:rotate(90deg)}
select,input{font:inherit;color:#fff;background:#ffffff14;border:0;border-radius:9px;padding:.5em .65em}
select{max-width:58%}option{color:#000}
.it input.num{width:46%;text-align:right}
.fld input{width:100%}
.lb{display:block;color:#a9c1cf;font-size:.8rem;margin-bottom:5px}
input.sw{appearance:none;-webkit-appearance:none;width:50px;height:30px;border-radius:15px;background:#ffffff33;position:relative;flex:none;margin:0;padding:0;cursor:pointer;transition:background .2s}
input.sw::after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#fff;transition:left .2s}
input.sw:checked{background:#66bb6a}input.sw:checked::after{left:23px}
.note{color:#a9c1cf;font-size:.8rem;margin:-12px 8px 20px;line-height:1.35}
.btn{display:block;width:100%;padding:.8em;border:0;border-radius:12px;background:#81d4fa;color:#0f2027;font:inherit;font-weight:600;cursor:pointer;margin:0 0 20px}
.btn.sec{background:#ffffff1a;color:#fff}.btn.red{background:#ef5350;color:#fff}
.fld .btn{margin:0}
.mini{border:0;border-radius:9px;background:#81d4fa;color:#0f2027;font:inherit;font-weight:600;padding:.5em .9em;cursor:pointer}
.res{color:#a9c1cf;font-size:.82rem;margin-top:6px}
.res button{display:block;width:100%;text-align:left;background:#ffffff14;color:#fff;border:0;border-radius:9px;padding:.6em .7em;margin-top:6px;font:inherit;cursor:pointer}
.msg{border-radius:12px;padding:11px 14px;margin:0 0 18px;background:#66bb6a33}.msg.err{background:#ffb74d33}
.pe{text-align:center;color:#ffffff55;font-size:.75rem;margin-top:8px}
</style>)rawliteral";

// ---- Página principal (HTML, CSS e JavaScript guardados na flash; os números chegam por fetch de /json e /historico) 
static const char PAGE_INDEX[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Station</title>
<style>
:root{--card:rgba(255,255,255,.09);--mut:#a9c1cf}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:#fff;
  background:radial-gradient(1100px 460px at 85% -8%,rgba(56,209,255,.16),transparent),
  linear-gradient(#0b182c,#07101e) fixed}
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
.ring span{width:76px;height:76px;border-radius:50%;background:#0d1b30;display:grid;place-items:center;font-weight:500}
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
    <span id="br" style="display:inline-flex;align-items:center;gap:4px;color:#ffd54f" title="Brilho da tela"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg><span id="brp" style="font-size:.9rem;font-weight:500;color:#fff">--%</span></span>
    <svg id="wf" width="26" height="18" viewBox="0 0 26 18"><title id="wft">WiFi</title><rect id="w1" x="0" y="12" width="5" height="6" rx="1" fill="#ffffff40"/><rect id="w2" x="7" y="8" width="5" height="10" rx="1" fill="#ffffff40"/><rect id="w3" x="14" y="4" width="5" height="14" rx="1" fill="#ffffff40"/><rect id="w4" x="21" y="0" width="5" height="18" rx="1" fill="#ffffff40"/></svg>
    <span id="pw" style="display:none;color:#a9c1cf" title="Energia"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 2v5M15 2v5"/><path d="M6 7h12v4a6 6 0 0 1-12 0z"/><path d="M12 17v5"/><path id="pwslash" d="M3 3l18 18" stroke="#ef5350" style="display:none"/></svg></span>
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
  <div class="card"><div class="lbl">Sensação</div><div class="big" id="sens">--</div></div>
  <div class="card"><div class="lbl">Chuva (24 h)</div><div class="big" id="chv">--</div><div class="mut">previsão</div></div>
  <div class="card"><div class="lbl">Lua</div><div class="row"><div><div id="luan">--</div><div class="mut" id="luai"></div></div><div style="font-size:2rem" id="luae"></div></div></div>
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
    const b=d.bateria,en=d.energia;
    $('pw').style.display=en==null?'none':'inline-flex';
    if(en!=null){$('pw').style.color=en?'#66bb6a':'#a9c1cf';$('pwslash').style.display=en?'none':'block';$('pw').title=en?'Energia externa conectada (5 V)':'Sem energia externa: rodando na bateria';}
    $('bat').style.display=b?'flex':'none';
    if(b){
      const p=b.pct,c=p<=10?'#ef5350':(p<=20?'#ffb74d':'#66bb6a'),av=$('bataviso');
      $('batn').setAttribute('width',Math.round(22*p/100));$('batn').setAttribute('fill',c);
      $('batp').textContent=p+'%';$('bat').title=b.v+' V';
      if(p<=20&&en!==true){av.style.display='block';av.style.background=p<=10?'rgba(239,83,80,.25)':'rgba(255,183,77,.2)';
        av.textContent=p<=10?'⚠️ Bateria crítica ('+p+'%). A estação pode desligar a qualquer momento.':'⚠️ Bateria baixa ('+p+'%). Conecte ao carregador.';}
      else av.style.display='none';
    }else $('bataviso').style.display='none';
    $('slash').style.display=d.display?'none':'block';
    $('btnd').title=d.display?'Apagar a tela':'Ligar a tela';
    if(d.brilho!=null){const br=$('br');$('brp').textContent=d.brilho+'%';
      br.style.color=d.brilhoNoite?'#9fa8da':'#ffd54f';br.style.opacity=d.display?1:.45;
      br.title='Brilho da tela: '+d.brilho+'%'+(d.brilhoNoite?' (noturno)':'')+(d.display?'':' · tela apagada');}
    const q=d.rssiPct,nb=q>=75?4:(q>=50?3:(q>=25?2:(q>0?1:0)));
    for(let k=1;k<=4;k++)$('w'+k).setAttribute('fill',k<=nb?'#ffffff':'#ffffff40');
    $('wft').textContent='WiFi '+d.rssi+' dBm ('+q+'%)';
    $('cid').textContent=e.cidade||'--';
    $('tmp').textContent=f(e.temp,0)+u;
    $('desc').textContent=e.desc||'--';
    $('ico').textContent=emo(e.icone);
    const p24=d.prox24;
    $('mm').textContent=p24?'Próximas 24 h: mín '+f(p24.min,0)+u+'  ·  máx '+f(p24.max,0)+u:'';
    $('sens').textContent=p24?f(p24.sens,0)+u:'--';
    $('chv').textContent=p24?f(p24.chuva,1)+' mm':'--';
    const L=d.lua;
    $('luan').textContent=L?L.nome:'--';
    $('luai').textContent=L?L.ilum+'% iluminada'+(L.nasce?' · nasce '+hm(L.nasce)+', põe '+hm(L.poe):''):'';
    $('luae').textContent=L?['🌑','🌒','🌓','🌔','🌕','🌖','🌗','🌘'][L.fase&7]:'';
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
    if(!d.ok){HD=null;$('chart').innerHTML='';$('hsel').textContent='';$('hlab').textContent='Sensor '+d.sensor+' sem leitura válida. Confira a ligação (dado no pino '+d.pino+').';return}
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
  if (PIN_ENERGIA >= 0) {               // sensor de energia da UPS (divisor de tensão no pino)
    pinMode(PIN_ENERGIA, (PIN_ENERGIA == 16) ? INPUT_PULLDOWN_16 : INPUT);
    if (energiaAtiva) energiaLeAgora();
  }

  delay(50);
  dht.begin();

  // initialize display
  display.init();
  display.clear();
  display.display();
  aplicaDisplay();                      // respeita o estado salvo já na inicialização

  varreduraI2C();                       // lista no Serial o que respondeu no barramento (0x3C = display)

  desenhaAbertura();                    // logo, nome e versão
  if (displayLigado) delay(ABERTURA_MS);   // com a tela apagada não atrasa o boot

  //display.flipScreenVertically();
  desenhaConectando(0);                 // 'Conectando ao WiFi' com o ícone e os 3 pontos animados

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

  // Com rede salva: tenta conectar por WIFI_BOOT_TIMEOUT_MS e, se não der, segue SEM WiFi (sensor, display,
  // botões e histórico continuam; o ESP reconecta sozinho quando o roteador voltar). Assim uma queda de
  // energia que derrube o roteador não deixa a estação reiniciando em loop.
  // Sem rede salva (1ª vez ou depois de apagar o WiFi): abre o portal; se ninguém configurar em 3 min, reinicia.
  // As credenciais salvas NUNCA são apagadas automaticamente (uma queda do roteador não pode zerar o WiFi).
  wifiManager.setConnectTimeout(30);
  wifiManager.setConfigPortalTimeout(180);
  ledRede = LED_CONECTANDO;             // pisca rápido enquanto conecta (e com o portal aberto)
  if (WiFi.SSID().length() > 0) {       // há uma rede salva na flash
    Serial.println("Conectando a " + WiFi.SSID() + "...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);        // o próprio ESP fica tentando reconectar
    WiFi.begin();                       // usa a rede salva
    unsigned long t0 = millis();
    uint8_t passo = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_BOOT_TIMEOUT_MS) {
      delay(100);
      if (++passo % 4 == 0) desenhaConectando(passo / 4);   // os pontos andam a cada 400 ms
    }
  } else if (!wifiManager.autoConnect(PORTAL_SSID, PORTAL_SENHA)) {   // sem rede salva: portal (protegido por senha)
    Serial.println(F("Portal sem configuração, reiniciando..."));
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 20, F("WiFi não configurado"));
    display.drawString(64, 34, F("Reiniciando"));
    display.display();
    delay(2000);
    ESP.restart();
  }

  // Get time from network time service (sincroniza sozinho quando houver rede)
  aplicaFuso();

  // OTA: permite enviar novo firmware pelo WiFi (Arduino IDE > Porta de rede). Só inicia com rede
  // (iniciaOta(), também chamada pelo loop quando o WiFi conecta depois do boot).
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(OTA_SENHA);   // senha definida em WeatherStationConfig.h

  if (WiFi.status() == WL_CONNECTED) {
    ledRede = LED_NORMAL;               // conectou
    ledPulsoOk();
    iniciaOta();
    if (displayLigado) {                // tela 'WiFi conectado': rede, sinal e IP (útil se a tela do WiFi estiver desmarcada)
      desenhaTelaIp();
      delay(IP_TELA_MS);
    }
    ipMostrado = true;
  } else {
    ledRede = LED_SEM_REDE;             // segue sem WiFi: piscada longa a cada 2 s até conectar
    Serial.println(F("Sem WiFi: seguindo sem rede; reconecta quando o roteador voltar"));
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 8, F("Sem WiFi"));
    display.drawString(64, 22, F("Seguindo sem rede"));
    display.drawString(64, 36, F("(reconecta sozinho)"));
    display.display();
    delay(2000);
  }

  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  // Sem setAllowedHours: a previsão traz todos os blocos de 3 h (calculaPrevisao() monta as 24 h e os próximos dias)

  ui.setTargetFPS(30);
  ui.setTimePerFrame((uint16_t)(telaSeg * 1000));   // tempo de cada tela (configurável em /config)
  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);
  ui.setIndicatorPosition(BOTTOM);
  ui.setIndicatorDirection(LEFT_RIGHT);
  ui.disableAllIndicators();            // os indicadores (compactos) são desenhados pela barra inferior: desenhaIndicadores()
  ui.setFrameAnimation(SLIDE_LEFT);
  aplicaTelas();                        // só as telas marcadas em /config
  ui.setOverlays(overlays, numberOfOverlays);

  // Inital UI takes care of initalising the display too.
  ui.init();
  brilhoAplicado = 0xFF;                // ui.init() reinicia o display e volta o contraste ao padrão: reenvia o brilho
  aplicaDisplay();                      // ui.init() religa a tela; reaplica o estado salvo (e o brilho)
  if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());   // tempo de tela ligada (0 = sempre ligada)

  Serial.println(F(""));

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
  server.on("/led", HTTP_POST, handleLed);
  server.on("/brilho", HTTP_POST, handleBrilho);
  server.on("/telas", HTTP_POST, handleTelas);
  server.on("/hora", HTTP_POST, handleHora);
  server.on("/buscar-cidade", HTTP_GET, handleBuscaCidade);
  server.on("/api", HTTP_POST, handleApi);
  server.on("/enviar", HTTP_POST, handleEnviar);
  server.on("/reset-wifi", HTTP_POST, handleResetWiFi);
  server.on("/reset-geral", HTTP_POST, handleResetGeral);
  server.on("/portal", HTTP_POST, handlePortal);
  server.on("/login", HTTP_GET, paginaLogin);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/sair", HTTP_POST, handleSair);
  server.on("/sair-todos", HTTP_POST, handleSairTodos);
  // Origin/Referer: requisicaoSegura() (proteção contra CSRF); Cookie: logado() (login da /config)
  server.collectHeaders("Origin", "Referer", "Cookie");   // forma do core 3.x (lista de nomes)
  server.begin();
}

/**
 * Laço principal (roda continuamente): trata os botões e os timers da tela, atende o servidor web
 * e o OTA, lê sensor e bateria, confere a rede e a agenda, atualiza o clima, envia ao painel e
 * redesenha o OLED.
 */
void loop() {
  server.handleClient();
  if (otaIniciado) ArduinoOTA.handle();

  unsigned long ms = millis();

  bool segurandoReset = trataBotoes(ms);   // botão físico: clique = próxima tela; segurar = apagar WiFi
  verificaTimerTela(ms);                   // apaga a tela quando o tempo de tela ligada acabar
  bool mostrandoMsg = trataMensagemTela(ms);   // mensagem temporária no OLED (ex.: resultado do envio)
  atualizaEnergia(ms);                     // sensor de energia da UPS: energia externa conectada ou bateria

  // Estado da rede: pisca devagar se o WiFi cair (como em interruptores inteligentes)
  if ((long)(ms - nextRede) >= 0) {
    nextRede = ms + 1000UL;
    ledSilencio = ledSegueAgenda && agendaEstadoAtual() == 0;   // à noite (tela desligada pela agenda) o LED fica apagado
    bool conectado = (WiFi.status() == WL_CONNECTED);
    if (!conectado && ledRede != LED_SEM_REDE) {
      ledRede = LED_SEM_REDE;
      Serial.println(F("WiFi caiu"));
    } else if (conectado && ledRede == LED_SEM_REDE) {
      ledRede = LED_NORMAL;
      ledPulsoOk();
      Serial.println(F("WiFi voltou"));
      iniciaOta();                      // se o boot foi sem rede, o OTA começa agora
      if (!weatherOk) nextWeatherUpdate = ms;   // sem clima ainda: atualiza já, sem esperar a nova tentativa
      if (!ipMostrado && displayLigado && !msgAtiva && !segurandoReset) {   // boot sem rede: mostra o IP na 1ª conexão
        msgTipo = MSG_IP;
        mostraMensagemTela("", "", "", IP_TELA_MS);
        mostrandoMsg = true;            // nesta volta do loop o OLED não redesenha as telas por cima
      }
      ipMostrado = true;
    }
    // Brilho: troca para o noturno (ou volta ao de dia) na hora certa, pela agenda ou pelo pôr do sol
    if (displayLigado && !msgAtiva && !segurandoReset && brilhoAgora() != brilhoAplicado) ligaTela();
  }

  // Sol e Lua: recalculados a cada minuto (contas locais, sem internet)
  if ((long)(ms - nextAstro) >= 0) {
    atualizaAstro();
    nextAstro = ms + 60000UL;
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
  displayLigado = (c.displayLigado != 0);
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
  configTime((int)fusoMin * 60, horarioVerao ? 3600 : 0, NTP_SERVIDOR_1, NTP_SERVIDOR_2, NTP_SERVIDOR_3);   // 3 servidores (WeatherStationConfig.h)
  nextAstro = 0;                         // recalcula o sol e a Lua com a hora nova
}

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
  if (ok) nextApi = millis() + PAINEL_INTERVAL_MS;                 // recomeça o ciclo do envio automático
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
    case 16: return "D0 (GPIO16)";
    case 5:  return "D1 (GPIO5)";
    case 4:  return "D2 (GPIO4)";
    case 0:  return "D3 (GPIO0)";
    case 2:  return "D4 (GPIO2)";
    case 14: return "D5 (GPIO14)";
    case 12: return "D6 (GPIO12)";
    case 13: return "D7 (GPIO13)";
    case 15: return "D8 (GPIO15)";
    default: return "GPIO" + String(gpio);
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
    Serial.println("Falhou a leitura do sensor " SENSOR_NOME "!");
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
    Serial.println(presente ? "Energia externa conectada (5 V)" : "Sem energia externa: rodando na bateria");
  }
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
      display->drawString(64, 32, WiFi.localIP().toString() + "/config");
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

  drawProgress(display, 20, "Atualizando o clima...");
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

  drawProgress(display, 60, "Atualizando previsões...");
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
  drawProgress(display, 100, weatherOk ? "Pronto..." : "Falha ao obter o clima");
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

// ---- DISPLAY OLED: TELAS E ÍCONES ---------------------------------------------------------

/** Liga (com o brilho certo para agora) ou desliga fisicamente o OLED conforme a variável displayLigado. */
void aplicaDisplay() {
  if (displayLigado) ligaTela();
  else display.displayOff();
}

/** Percentual de brilho aceito: 5 a 100, de 5 em 5. */
bool brilhoValido(int v) {
  return v >= 5 && v <= 100 && v % 5 == 0;
}

/**
 * true quando vale o brilho noturno. Pelo sol: do pôr ao nascer (sol abaixo do horizonte; sem
 * coordenadas ou sem hora ainda: false). Pela agenda: no horário de tela desligada (sem agenda: false).
 */
bool noiteAgora() {
  if (noitePeloSol) return astroOk && solAltura < -0.833f;   // -0,833° = mesma referência do nascer/pôr calculados
  return agendaEstadoAtual() == 0;
}

/**
 * Brilho que vale agora, em %: o noturno no horário de tela desligada da agenda (mesmo se a tela
 * foi acesa pelo botão), o de dia no resto do tempo.
 */
uint8_t brilhoAgora() {
  return noiteAgora() ? brilhoNoitePct : brilhoPct;
}

/**
 * Acende o OLED já com o brilho de agora. Use no lugar de display.displayOn(). Só reenvia o brilho
 * quando ele muda, porque setBrightness() da biblioteca também liga a tela (por isso o brilho
 * nunca é mandado com a tela apagada; o display guarda o valor enquanto está apagado).
 */
void ligaTela() {
  uint8_t pct = brilhoAgora();
  if (pct != brilhoAplicado) {
    display.setBrightness((uint8_t)((pct * 255U + 50U) / 100U));   // 0-255; ajusta contraste, precharge e VCOMH
    brilhoAplicado = pct;
  } else {
    display.displayOn();
  }
}

/** Tela de abertura do boot: logo (sol atrás da nuvem), nome do projeto e versão do firmware. */
void desenhaAbertura() {
  display.clear();
  display.drawXbm(2, 12, Logo_width, Logo_height, Logo_bits);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(56, 6, F("Weather"));
  display.drawString(56, 23, F("Station"));
  display.setFont(ArialMT_Plain_10);
  display.drawString(56, 46, "ESP8266  v" FIRMWARE_VERSAO);
  display.display();
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
  if (msgTipo == MSG_IP) { desenhaTelaIp(); return; }
  if (msgTipo == MSG_ENVIO_ANDAMENTO) { desenhaEnvioEtapa(5, "Iniciando..."); return; }
  if (msgTipo == MSG_ENVIO_OK || msgTipo == MSG_ENVIO_FALHA) { desenhaResultadoEnvio(); return; }
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

// ---- Telas do envio ao painel pelo botão ----

/** Faixa de título invertida (fundo aceso, texto apagado) no topo do OLED, 13 px de altura. */
void desenhaCabecalho(const char* titulo) {
  display.setColor(WHITE);
  display.fillRect(0, 0, 128, 13);
  display.setColor(BLACK);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, titulo);
  display.setColor(WHITE);
}

/** Ícone de nuvem cheia com seta para cima (envio), centrado em (cx, cy), cerca de 26x18 px. */
void desenhaIconeNuvemEnvio(int cx, int cy) {
  display.setColor(WHITE);
  display.fillCircle(cx - 7, cy + 2, 6);
  display.fillCircle(cx + 1, cy - 2, 8);
  display.fillCircle(cx + 8, cy + 3, 5);
  display.fillRect(cx - 7, cy + 2, 15, 7);
  display.setColor(BLACK);              // seta recortada dentro da nuvem
  display.fillRect(cx, cy - 2, 2, 9);
  display.drawLine(cx - 3, cy + 1, cx, cy - 3);
  display.drawLine(cx - 3, cy + 2, cx, cy - 2);
  display.drawLine(cx + 4, cy + 1, cx + 1, cy - 3);
  display.drawLine(cx + 4, cy + 2, cx + 1, cy - 2);
  display.setColor(WHITE);
}

/** Ícone de sucesso: círculo cheio com um 'visto' recortado, raio 11, centrado em (cx, cy). */
void desenhaIconeOk(int cx, int cy) {
  display.setColor(WHITE);
  display.fillCircle(cx, cy, 11);
  display.setColor(BLACK);
  for (int k = 0; k < 2; k++) {         // traço com 2 px de espessura
    display.drawLine(cx - 6, cy + k, cx - 2, cy + 4 + k);
    display.drawLine(cx - 2, cy + 4 + k, cx + 6, cy - 4 + k);
  }
  display.setColor(WHITE);
}

/** Ícone de falha: anel com um 'X' dentro, raio 11, centrado em (cx, cy). */
void desenhaIconeFalha(int cx, int cy) {
  display.setColor(WHITE);
  display.drawCircle(cx, cy, 11);
  display.drawCircle(cx, cy, 10);
  for (int k = 0; k < 2; k++) {
    display.drawLine(cx - 5 + k, cy - 5, cx + 5 + k, cy + 5);
    display.drawLine(cx + 5 + k, cy - 5, cx - 5 + k, cy + 5);
  }
}

/** Corta o texto para caber em 'largura' px na fonte atual. */
String cabeNaLargura(const String& s, int largura) {
  int corte = s.length();
  while (corte > 0 && display.getStringWidth(s.substring(0, corte)) > largura) corte--;
  return s.substring(0, corte);
}

/**
 * Tela durante o envio pelo botão: título, nuvem com seta, a etapa atual e uma barra de progresso.
 * Chamada em cada etapa (o envio trava o loop, então a tela é desenhada na hora).
 */
void desenhaEnvioEtapa(int pct, const char* texto) {
  display.clear();
  desenhaCabecalho("PAINEL WEB");
  desenhaIconeNuvemEnvio(16, 30);
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(34, 15, F("Enviando"));
  display.setFont(ArialMT_Plain_10);
  display.drawString(34, 33, cabeNaLargura(String(texto), 94));
  display.drawProgressBar(2, 51, 124, 10, pct);
  display.display();
}

/**
 * Tela de resultado do envio pelo botão. Sucesso: visto, 'Enviado!', código HTTP, tempo e a leitura
 * enviada. Falha: X, 'Falhou', o erro (em até 2 linhas) e quando será a nova tentativa.
 */
void desenhaResultadoEnvio() {
  bool ok = (msgTipo == MSG_ENVIO_OK);
  display.clear();
  desenhaCabecalho("PAINEL WEB");
  if (ok) desenhaIconeOk(14, 38);
  else desenhaIconeFalha(14, 38);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(32, 14, ok ? "Enviado!" : "Falhou");
  display.setFont(ArialMT_Plain_10);

  // tempo do envio com 1 casa e vírgula (ex.: 1,2 s); 0 = falhou antes de conectar
  String dur = "";
  if (apiUltimoDurMs > 0) {
    dur = String(apiUltimoDurMs / 1000UL) + "," + String((apiUltimoDurMs % 1000UL) / 100UL) + " s";
  }

  if (ok) {
    display.drawString(32, 32, apiUltimoMsg + "  " + dur);
    String leitura = sensorOk ? String(localTemp, 1) + "°C  " + String(localHum, 0) + "%" : String("sensor sem leitura");
    display.drawString(32, 44, cabeNaLargura(leitura, 96));
  } else {
    String erro = apiUltimoMsg;
    String l1 = cabeNaLargura(erro, 96);
    String l2 = cabeNaLargura(erro.substring(l1.length()), 96);
    display.drawString(32, 32, l1);
    if (l2.length()) display.drawString(32, 44, l2);
    else display.drawString(32, 44, "Nova tentativa: " + String(PAINEL_RETRY_MS / 60000UL) + " min");
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
    ligaTela();                         // com o brilho de agora (noturno à noite)
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
    msgTipo = MSG_TEXTO;                // a próxima mensagem volta a ser texto simples, salvo indicação
    if (msgLigouTela) {
      msgLigouTela = false;
      aplicaDisplay();                  // volta a apagar a tela, se ela estava apagada
    }
    return false;
  }
  return true;
}

/** true depois que a hora da internet (NTP) sincronizou. Antes disso o relógio marca 1970. */
bool horaOk() {
  return time(nullptr) > 100000;
}

/**
 * Desenha, numa tela de clima, o aviso de que ainda não há dados (com o motivo): sem WiFi, sem
 * chave da API ou aguardando a consulta.
 */
void desenhaSemClima(OLEDDisplay *display, int16_t x, int16_t y, const char* titulo) {
  const char* motivo = (WiFi.status() != WL_CONNECTED) ? "Sem WiFi"
                     : (OPEN_WEATHER_MAP_APP_ID.length() == 0) ? "Falta a chave da API"
                     : "Aguardando dados...";
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, y, titulo);
  display->setFont(ArialMT_Plain_24);
  display->drawString(64 + x, 12 + y, F("--"));
  display->setFont(ArialMT_Plain_10);
  display->drawString(64 + x, 38 + y, motivo);
}

/** Tela 1 do OLED: dia da semana, data e hora ('--' até a hora da internet sincronizar). */
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[24];

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);

  if (!horaOk()) {                      // ainda sem NTP (ex.: boot sem WiFi): não mostra 01/01/1970
    display->drawString(64 + x, 5 + y, F("Aguardando a hora"));
    display->setFont(ArialMT_Plain_24);
    display->drawString(64 + x, 15 + y, F("--:--:--"));
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    return;
  }

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
  display->drawString(22 + x, y, F("CLIMA INTERNO"));
  display->drawString(0 + x, 12 + y, F("Umid"));
  display->drawString(66 + x, 12 + y, F("Temp"));

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
    display->drawString(0 + x + wh + 1, 36 + y, F("%"));
    display->drawString(66 + x + wt + 1, 36 + y, F("°C"));
  }
}

/** Tela 3 do OLED: ícone, temperatura, cidade e descrição do clima atual. */
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  if (!climaRecebido) { desenhaSemClima(display, x, y, "CLIMA ATUAL"); return; }
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

/**
 * Tela 4 do OLED ('PRÓXIMAS 24 H'): mínima e máxima das próximas 24 h, umidade, sensação térmica,
 * chuva prevista nas 24 h e vento. Pressão e visibilidade ficam na página web.
 */
void drawCurrentWeatherDetails(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  if (!climaRecebido) { desenhaSemClima(display, x, y, "PRÓXIMAS 24 H"); return; }
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, y, F("PRÓXIMAS 24 H"));
  display->setTextAlignment(TEXT_ALIGN_LEFT);

  String unit = IS_METRIC ? "°C" : "°F";
  // Mín/máx: das próximas 24 h (previsão); sem previsão ainda, '--'
  String mn = prox24Ok ? String(prox24Min, 1) + unit : String("--");
  String mx = prox24Ok ? String(prox24Max, 1) + unit : String("--");
  display->drawString(x, 12 + y, "Min: " + mn);
  display->drawString(x, 24 + y, "Max: " + mx);
  display->drawString(x, 36 + y, "Umid: " + String(currentWeather.humidity) + "%");

  // A API devolve o vento em m/s (metric) ou mph (imperial); a chuva em mm
  float wind = IS_METRIC ? currentWeather.windSpeed * 3.6 : currentWeather.windSpeed;
  // chuva com 1 casa até 9,9 mm e sem casa a partir de 10 mm, para caber na coluna
  String chuva = prox24Ok ? String(prox24Chuva, prox24Chuva < 9.95f ? 1 : 0) + "mm" : String("--");
  display->drawString(60 + x, 12 + y, "Sens: " + String(prox24Ok ? prox24Sens : currentWeather.feelsLike, 0) + unit);
  display->drawString(60 + x, 24 + y, "Chuva: " + chuva);
  display->drawString(60 + x, 36 + y, "Ven: " + String(wind, 0) + (IS_METRIC ? "km/h" : "mph"));
}

/** Tela 5 do OLED: previsão dos próximos 3 dias (usa drawForecastDetails para cada dia). */
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  if (!climaRecebido) { desenhaSemClima(display, x, y, "PRÓXIMOS DIAS"); return; }
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
  if (observationTimestamp == 0) {      // este dia da previsão não veio: '--' em vez de 'QUI 0°C'
    display->drawString(x + 20, y, F("--"));
    display->drawString(x + 20, y + 34, F("--"));
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    return;
  }
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * Tela 6 do OLED: bateria (com a tomada indicando energia externa), sinal do WiFi (ícone, dBm e
 * %), nome da rede (SSID) e IP.
 */
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
    display->drawString(x, y, F("Bateria: desativada"));
  }
  if (energiaAtiva && PIN_ENERGIA >= 0) drawTomada(display, x + 112, y, energiaExterna);   // tomada: energia externa conectada ou não

  // Linha 2: potência do sinal (ícone + dBm + %)
  if (WiFi.status() == WL_CONNECTED) {
    int pct = getRSSIasQuality(rssi);
    drawSinalWiFi(display, x, 12 + y, pct);
    snprintf(aux, sizeof(aux), "%ddBm  %d%%", (int)rssi, pct);
    display->drawString(x + 24, 12 + y, aux);
  } else {
    drawSinalWiFi(display, x, 12 + y, 0);
    display->drawString(x + 24, 12 + y, F("sem WiFi"));
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
  if (horaOk()) snprintf_P(buff, sizeof(buff), PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);
  else snprintf_P(buff, sizeof(buff), PSTR("--:--"));                   // ainda sem hora da internet

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  String temp = (climaRecebido ? String(currentWeather.temp, 0) : String("--")) + (IS_METRIC ? "°C" : "°F");   // '--°C' até chegar o clima
  display->drawString(128, 54, temp);
  display->drawHorizontalLine(0, 52, 128);
  desenhaIndicadores(display, state);
}

/**
 * Indicadores das telas na barra inferior, compactos (no lugar dos da biblioteca, que usam 12 px
 * por tela e encostariam na hora e na temperatura): um ponto a cada 7 px, centralizados; a tela
 * atual é um ponto cheio maior. Com uma tela só, não desenha nada.
 */
void desenhaIndicadores(OLEDDisplay *display, OLEDDisplayUiState* state) {
  if (numTelasAtivas <= 1) return;
  const int passo = 7, yc = 58;
  int x0 = 64 - ((numTelasAtivas - 1) * passo) / 2;
  display->setColor(WHITE);
  for (uint8_t k = 0; k < numTelasAtivas; k++) {
    int cx = x0 + k * passo;
    if (k == state->currentFrame) display->fillCircle(cx, yc, 2);   // tela atual: 5 px cheio
    else display->drawCircle(cx, yc, 1);                            // outras: anel de 3 px
  }
}

// ---- Tela 'Sol e Lua' -----------------------------------------------------------------------

/** Hora local 'HH:MM' de um instante (ou '--:--' se ainda não houver o valor). */
String horaCurta(time_t t) {
  if (t <= 0) return "--:--";
  struct tm* ti = localtime(&t);
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", ti->tm_hour, ti->tm_min);
  return String(b);
}

/** Nome da fase da Lua (0 = nova, 4 = cheia), como devolvido por Astronomy::calculateMoonData. */
const char* nomeFaseLua(uint8_t fase) {
  static const char* const NOMES[] = {"Lua nova", "Crescente", "Quarto crescente", "Crescente gibosa",
                                      "Lua cheia", "Minguante gibosa", "Quarto minguante", "Minguante"};
  return NOMES[fase & 7];
}

/**
 * Recalcula a fase e a iluminação da Lua (só precisa da hora) e, com as coordenadas da cidade, o
 * nascer/pôr do sol e da Lua e a altura do sol. Contas locais, sem internet; chamada a cada minuto.
 */
void atualizaAstro() {
  if (!horaOk()) { luaOk = false; astroOk = false; return; }
  time_t agora = time(nullptr);
  Astronomy astronomia;
  Astronomy::MoonData md = astronomia.calculateMoonData(agora);
  luaFase = md.phase;
  luaIlum = md.illumination;
  luaOk = true;
  if (!coordOk) { astroOk = false; return; }      // sem coordenadas ainda: os horários ficam '--:--'
  SunMoonCalc calc(agora, cidadeLat, cidadeLon);  // 'agora' em UTC; lat/lon em graus
  SunMoonCalc::Result r = calc.calculateSunAndMoonData();
  solNasce = r.sun.rise;
  solPoe = r.sun.set;
  luaNasce = r.moon.rise;
  luaPoe = r.moon.set;
  solAltura = r.sun.elevation;
  astroOk = true;
}

/** Seta pequena (5x7 px) para cima (nascer) ou para baixo (pôr), alinhada a uma linha de texto em y. */
void desenhaSeta(OLEDDisplay *display, int x, int y, bool cima) {
  display->drawVerticalLine(x + 2, y + 3, 7);
  if (cima) {
    display->drawLine(x, y + 5, x + 2, y + 3);
    display->drawLine(x + 4, y + 5, x + 2, y + 3);
  } else {
    display->drawLine(x, y + 7, x + 2, y + 9);
    display->drawLine(x + 4, y + 7, x + 2, y + 9);
  }
}

/** Sol pequeno (11x11 px): disco com 8 raios curtos. */
void desenhaSolPequeno(OLEDDisplay *display, int cx, int cy) {
  display->fillCircle(cx, cy, 2);
  display->drawVerticalLine(cx, cy - 5, 2);
  display->drawVerticalLine(cx, cy + 4, 2);
  display->drawHorizontalLine(cx - 5, cy, 2);
  display->drawHorizontalLine(cx + 4, cy, 2);
  display->setPixel(cx - 3, cy - 3); display->setPixel(cx + 3, cy - 3);
  display->setPixel(cx - 3, cy + 3); display->setPixel(cx + 3, cy + 3);
}

/** Lua pequena (9x9 px) em forma de crescente. */
void desenhaLuaPequena(OLEDDisplay *display, int cx, int cy) {
  display->fillCircle(cx, cy, 4);
  display->setColor(BLACK);
  display->fillCircle(cx + 2, cy - 1, 3);
  display->setColor(WHITE);
}

/**
 * Desenha a Lua com a parte iluminada da fase atual: linha a linha, acende do terminador até a
 * borda. Crescente = iluminada à direita no hemisfério norte e à esquerda no sul (invertido).
 */
void desenhaLua(OLEDDisplay *display, int cx, int cy, int r, uint8_t fase, float ilum, bool sul) {
  bool direita = (fase <= 4);           // 1 a 3 = crescente (0 e 4: nova/cheia, o lado não importa)
  if (sul) direita = !direita;
  for (int dy = -r; dy <= r; dy++) {
    float w = sqrtf((float)(r * r - dy * dy));
    int borda = (int)(w + 0.5f);
    int term = (int)lroundf(w * (1.0f - 2.0f * ilum));   // terminador: de +w (nova) a -w (cheia)
    if (borda - term <= 0) continue;
    if (direita) display->drawHorizontalLine(cx + term, cy + dy, borda - term + 1);
    else display->drawHorizontalLine(cx - borda, cy + dy, borda - term + 1);
  }
  display->drawCircle(cx, cy, r);       // contorno: a Lua nova também aparece
}

/**
 * Tela 'Sol e Lua': nascer e pôr do sol e da Lua (com as coordenadas da cidade) e a Lua desenhada
 * com a fase e a porcentagem iluminada (só com a data).
 */
void drawSolLua(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);

  // Linha 1: sol (nascer e pôr)   Linha 2: Lua (nascer e pôr)
  desenhaSolPequeno(display, 5 + x, 6 + y);
  desenhaSeta(display, 14 + x, y, true);
  display->drawString(21 + x, y, astroOk ? horaCurta(solNasce) : String("--:--"));
  desenhaSeta(display, 54 + x, y, false);
  display->drawString(61 + x, y, astroOk ? horaCurta(solPoe) : String("--:--"));

  desenhaLuaPequena(display, 5 + x, 18 + y);
  desenhaSeta(display, 14 + x, 12 + y, true);
  display->drawString(21 + x, 12 + y, astroOk ? horaCurta(luaNasce) : String("--:--"));
  desenhaSeta(display, 54 + x, 12 + y, false);
  display->drawString(61 + x, 12 + y, astroOk ? horaCurta(luaPoe) : String("--:--"));

  // Embaixo: a Lua com a fase, o nome e a iluminação
  bool sul = coordOk ? (cidadeLat < 0) : true;   // sem coordenadas: hemisfério sul (padrão do projeto, Brasil)
  if (luaOk) desenhaLua(display, 15 + x, 38 + y, 12, luaFase, luaIlum, sul);
  else display->drawCircle(15 + x, 38 + y, 12);
  display->drawString(34 + x, 26 + y, luaOk ? String(nomeFaseLua(luaFase)) : String("Aguardando a hora"));
  display->drawString(34 + x, 38 + y, luaOk ? "Iluminada: " + String((int)(luaIlum * 100 + 0.5f)) + "%" : String("--"));
}

// ---- Escolha das telas ---------------------------------------------------------------------

/** Quantas telas estão marcadas na máscara. */
uint8_t contaTelas(uint8_t mask) {
  uint8_t n = 0;
  for (uint8_t k = 0; k < TELAS_TOTAL; k++) if (mask & (1 << k)) n++;
  return n;
}

/**
 * Monta a lista das telas marcadas e entrega ao OLED (volta para a 1ª). Com uma tela só, ela fica
 * fixa: sem passagem automática.
 */
void aplicaTelas() {
  numTelasAtivas = 0;
  for (uint8_t k = 0; k < TELAS_TOTAL; k++) {
    if (telasMask & (1 << k)) framesAtivos[numTelasAtivas++] = TELAS[k];
  }
  if (numTelasAtivas == 0) { framesAtivos[0] = TELAS[0]; numTelasAtivas = 1; }   // segurança: nunca fica sem tela
  ui.setFrames(framesAtivos, numTelasAtivas);
  if (numTelasAtivas > 1) ui.enableAutoTransition();
  else ui.disableAutoTransition();
}

// ---- Telas do boot: conectando e WiFi conectado ---------------------------------------------

/** 'Conectando ao WiFi' com o ícone e 3 pontos que se alternam (passo = qual ponto está aceso). */
void desenhaConectando(uint8_t passo) {
  display.clear();
  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, F("Conectando ao WiFi"));
  display.drawXbm(34, 13, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
  for (uint8_t k = 0; k < 3; k++) {
    int cx = 52 + k * 12;
    if (k == passo % 3) display.fillCircle(cx, 57, 3);
    else display.fillCircle(cx, 57, 1);
  }
  display.display();
}

/** Ícone grande de sinal do WiFi (4 barras, 22x17 px): barras cheias conforme a qualidade. */
void desenhaIconeWiFiGrande(int x, int y, int pct) {
  int n = 0;
  if (pct >= 75) n = 4;
  else if (pct >= 50) n = 3;
  else if (pct >= 25) n = 2;
  else if (pct > 0) n = 1;
  for (int k = 0; k < 4; k++) {
    int h = 5 + k * 4;                  // alturas 5, 9, 13 e 17 px
    int bx = x + k * 6;                 // barras de 4 px com 2 px de espaço
    int by = y + 17 - h;                // alinhadas pela base
    if (k < n) display.fillRect(bx, by, 4, h);
    else display.drawRect(bx, by, 4, h);
  }
}

/**
 * Tela 'WIFI CONECTADO' (logo depois de conectar): faixa de título, ícone de sinal, nome da rede,
 * sinal em dBm e %, e o IP em destaque, para abrir a página no navegador.
 */
void desenhaTelaIp() {
  int rssi = WiFi.RSSI();
  int pct = getRSSIasQuality(rssi);
  display.clear();
  desenhaCabecalho("WIFI CONECTADO");
  desenhaIconeWiFiGrande(4, 15, pct);

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(32, 13, cabeNaLargura(WiFi.SSID(), 94));
  display.drawString(32, 23, String(rssi) + " dBm  " + String(pct) + "%");

  String ip = WiFi.localIP().toString();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  if (display.getStringWidth(ip) > 126) display.setFont(ArialMT_Plain_10);   // IP muito longo: fonte menor
  display.drawString(64, 34, ip);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 52, F("abra no navegador"));
  display.display();
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

/**
 * Desenha o ícone de tomada no OLED (12x10 px): cheio = energia externa conectada; só contorno e
 * riscado = sem energia externa (rodando na bateria).
 */
void drawTomada(OLEDDisplay *display, int x, int y, bool ligada) {
  display->fillRect(x + 3, y, 1, 3);            // pino esquerdo
  display->fillRect(x + 8, y, 1, 3);            // pino direito
  if (ligada) display->fillRect(x + 1, y + 3, 10, 4);   // corpo cheio
  else display->drawRect(x + 1, y + 3, 10, 4);          // corpo vazio
  display->fillRect(x + 5, y + 7, 2, 3);        // cabo
  if (!ligada) display->drawLine(x, y + 9, x + 11, y);  // risco: sem energia
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
    if (estado == 1) displayLigado = true;              // horário de ligar: acende com o brilho de dia
    else if (!noiteDiminui) displayLigado = false;      // horário de apagar, modo 'apagar'
    // modo 'diminuir' à noite: mantém a tela como está; aplicaDisplay() só troca para o brilho noturno
    aplicaDisplay();
    telaAcordadaNoite = false;
    if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());   // ao ligar pela agenda, vale o tempo de tela ligada
    else telaContando = false;
  }
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
  Serial.println(F("Entered config mode"));
  ligaTela();                  // mostra as instruções do portal mesmo com a tela apagada
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 10, F("Wifi Manager"));
  display.drawString(64, 20, F("Conecte ao AP"));
  display.drawString(64, 30, myWiFiManager->getConfigPortalSSID());
  display.drawString(64, 40, F("Para configurar o Wifi"));
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

// ---- PAINEL WEB (API REST) ----------------------------------------------------------------

/** Devolve o device_id enviado ao painel: o salvo em /config ou, se vazio, 'esp8266-' + ID do chip. */
String idDispositivo() {
  if (apiDeviceId.length() > 0) return apiDeviceId;
  String id = String(ESP.getChipId(), HEX);
  id.toLowerCase();
  return "esp8266-" + id;               // padrão, se nenhum ID foi salvo em /config
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
  j += "{\"device_id\":\"" + jsonEscape(idDispositivo()) + "\"";
  j += ",\"temperature\":" + String(localTemp, 1);
  j += ",\"humidity\":" + String(localHum, 1);
  j += F(",\"city_id\":");
  j += soDigitos ? cid : String("null");
  j += ",\"city_name\":\"" + jsonEscape(currentWeather.cityName) + "\"";
  // Coordenadas da cidade (null até serem conhecidas): o painel consulta o clima por elas e,
  // sem city_id (cidade escolhida pela busca ou por latitude/longitude), identifica a cidade por elas
  j += F(",\"lat\":");
  j += coordOk ? String(cidadeLat, 4) : String("null");
  j += F(",\"lon\":");
  j += coordOk ? String(cidadeLon, 4) : String("null");
  j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  j += ",\"rssi\":" + String(rssi);
  j += ",\"firmware\":\"" FIRMWARE_VERSAO "\"";
  // ---- bateria ----
  j += F(",\"battery_active\":");
  j += bateriaAtiva ? "true" : "false";
  j += ",\"battery\":" + String(bateriaAtiva ? bateriaPct : 0);
  j += ",\"battery_voltage\":" + String(bateriaAtiva ? bateriaV : 0.0f, 2);
  // ---- energia, sensor e saúde do ESP ----
  j += F(",\"external_power\":");                 // true = energia externa (5 V); false = bateria; null = ESP sem o sensor
  if (energiaAtiva && PIN_ENERGIA >= 0) j += energiaExterna ? "true" : "false";
  else j += F("null");
  j += F(",\"sensor_ok\":");                      // false = a última leitura do sensor falhou (temperature/humidity repetem o último valor)
  j += sensorUltimaOk ? "true" : "false";
  j += ",\"uptime_s\":" + String(millis() / 1000UL);
  j += ",\"reset_reason\":\"" + jsonEscape(ESP.getResetReason()) + "\"";
  j += ",\"free_heap\":" + String(ESP.getFreeHeap());
  j += ",\"chip_id\":\"" + chip + "\"";
  j += ",\"send_interval_s\":" + String(PAINEL_INTERVAL_MS / 1000UL);
  j += F(",\"display_on\":");
  j += displayLigado ? "true" : "false";
  j += ",\"rssi_pct\":" + String(getRSSIasQuality(rssi));
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
    apiUltimoMsg = "Memória insuficiente para HTTPS (" + String(ESP.getFreeHeap()) + " bytes livres, maior bloco " +
                   String(ESP.getMaxFreeBlockSize()) + ")";
    return false;
  }

  if (envioNaTela) desenhaEnvioEtapa(25, "Preparando dados");
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

  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-key", deviceKey);          // DEVICE_KEY configurada em /config

  if (envioNaTela) desenhaEnvioEtapa(60, https ? "Conectando (HTTPS)" : "Conectando");
  unsigned long t0 = millis();
  int codigo = http.POST(corpo);                      // > 0: código HTTP, < 0: erro de conexão
  apiUltimoDurMs = millis() - t0;
  if (envioNaTela) desenhaEnvioEtapa(100, "Resposta recebida");
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
 * (ex.: weatherstationesp8266-abc123, weatherstationesp8266-abc123.local). Barra o ataque de DNS rebinding, em que um
 * site usa um domínio próprio apontando para o IP do ESP.
 */
bool hostEhDesteEsp(const String& host) {
  if (host.length() == 0) return true;  // sem Host: não é um navegador (que sempre envia)
  if (host == WiFi.localIP().toString()) return true;
  String nome = String(WiFi.hostname());
  nome.toLowerCase();
  return host == nome || host.startsWith(nome + ".");
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
    String origem = server.header("Origin");
    String ref = server.header("Referer");
    if (origem.length() > 0) ok = origem.startsWith("http://") && hostDaUrl(origem).equalsIgnoreCase(host);   // 'null' também é recusado
    else if (ref.length() > 0) ok = ref.startsWith("http://") && hostDaUrl(ref).equalsIgnoreCase(host);
  }
  if (!ok) {
    Serial.println("POST recusado (veio de outro site): " + server.uri());
    server.send(403, "text/html; charset=utf-8",
                "<meta charset=\"utf-8\"><p>Pedido recusado: ele não veio da página desta estação. "
                "Abra a página pelo endereço do ESP e tente de novo.</p>");
  }
  return ok;
}

/** Redireciona (303) para a seção certa da /config, com o código da mensagem de resultado. */
void redirecionaConfig(const char* r) {
  // Cada código de resultado volta para a seção do formulário que o gerou
  static const char* const DISPLAY_COD[] = {"disp", "tempo", "tempo_erro", "tempoligada", "tempoligada_erro", "brilho",
                                            "brilho_erro", "telas", "telas_erro", "agenda", "hora", "horaigual"};
  static const char* const PAINEL_COD[] = {"api", "apiurl", "apiid", "apikey", "envio_ok", "envio_erro"};
  const char* secao = "clima";          // ok, chave, local, local_vazio, coord, idioma
  for (const char* c : DISPLAY_COD) if (!strcmp(r, c)) secao = "display";
  for (const char* c : PAINEL_COD) if (!strcmp(r, c)) secao = "painel";
  if (!strcmp(r, "fuso") || !strcmp(r, "fuso_ok")) secao = "hora";
  else if (!strcmp(r, "bat")) secao = "bateria";
  else if (!strcmp(r, "led")) secao = "led";
  server.sendHeader("Location", String("/config?s=") + secao + "&r=" + r);
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

/** Monta uma linha de informação da /config (rótulo à esquerda, valor à direita, como nos Ajustes do iPhone). */
String linhaInfoP(const __FlashStringHelper* rotulo, const String& valor) {
  return "<div class=\"it\"><span class=\"tx\">" + String(rotulo) + "</span><span class=\"val\">" + valor + "</span></div>";
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
  String c = server.header("Cookie");
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
  server.sendHeader("Location", "/login");
  server.sendHeader("Cache-Control", "no-store");
  server.send(303, "text/plain", "");
  return false;
}

/** GET /login: página com o campo de senha (se já estiver logado, vai direto para a /config). */
void paginaLogin() {
  if (logado()) {
    server.sendHeader("Location", "/config");
    server.send(303, "text/plain", "");
    return;
  }
  String r = server.arg("r");
  String msg = "";
  if (r == "erro") msg = "⚠️ Senha incorreta.";
  else if (r == "bloq") msg = "⚠️ Muitas tentativas erradas. Aguarde 1 minuto e tente de novo.";
  else if (r == "sair") msg = "✅ Você saiu da configuração neste navegador.";
  else if (r == "todos") msg = "✅ Todos os navegadores foram desconectados.";

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
  if (msg.length()) h += "<p>" + msg + "</p>";
  h += "<form method=\"POST\" action=\"/login\">"
       "<input type=\"text\" name=\"u\" value=\"admin\" autocomplete=\"username\" style=\"display:none\">"   // ajuda o gerenciador de senhas
       "<label>Senha</label>"
       "<input name=\"senha\" type=\"password\" autocomplete=\"current-password\" autofocus required>"
       "<button type=\"submit\">Entrar</button></form>"
       "<p class=\"mut\">Este navegador fica conectado por " + String(LOGIN_DIAS) + " dias. "
       "Esqueceu a senha? Ela é a CONFIG_SENHA do WeatherStationConfig.h usado para gravar o firmware.</p>"
       "</body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", h);
}

/** POST /login: confere a senha e grava o cookie por LOGIN_DIAS dias. 5 erros seguidos = 1 min de espera. */
void handleLogin() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  unsigned long ms = millis();
  if (loginFalhas >= 5 && (long)(ms - loginLiberaEm) < 0) {
    server.sendHeader("Location", "/login?r=bloq");
    server.send(303, "text/plain", "");
    return;
  }
  if (!loginAtivo() || !comparaSeguro(server.arg("senha"), String(CONFIG_SENHA))) {
    if (loginFalhas < 255) loginFalhas++;
    if (loginFalhas >= 5) loginLiberaEm = ms + 60000UL;
    Serial.println(F("Login da /config: senha incorreta"));
    server.sendHeader("Location", "/login?r=erro");
    server.send(303, "text/plain", "");
    return;
  }
  loginFalhas = 0;
  if (!sessaoSalOk) geraSalSessao();    // 1º login: sorteia o sal (com o WiFi ligado)
  server.sendHeader("Set-Cookie", "ws_sessao=" + tokenSessao() + "; Max-Age=" + String((unsigned long)LOGIN_DIAS * 86400UL) +
                                  "; Path=/; HttpOnly; SameSite=Lax");
  server.sendHeader("Location", "/config");
  server.send(303, "text/plain", "");
}

/** POST /sair: sai da configuração só neste navegador (apaga o cookie). */
void handleSair() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  server.sendHeader("Set-Cookie", "ws_sessao=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax");
  server.sendHeader("Location", "/login?r=sair");
  server.send(303, "text/plain", "");
}

/** POST /sair-todos: troca o sal, o que desconecta todos os navegadores (inclusive este). */
void handleSairTodos() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;            // precisa estar logado (senha da /config)
  geraSalSessao();
  server.sendHeader("Set-Cookie", "ws_sessao=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax");
  server.sendHeader("Location", "/login?r=todos");
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
  j += IS_METRIC ? "°C" : "°F";
  j += F("\",\"erro\":");
  j += weatherOk ? "false" : "true";
  j += F(",\"display\":");
  j += displayLigado ? "true" : "false";
  j += ",\"brilho\":" + String(brilhoAgora());           // % de brilho que vale agora (dia ou noturno)
  j += F(",\"brilhoNoite\":");
  j += noiteAgora() ? "true" : "false";
  j += F(",\"bateria\":");
  if (bateriaAtiva) j += "{\"v\":" + String(bateriaV, 2) + ",\"pct\":" + String(bateriaPct) + "}";
  else j += F("null");
  j += F(",\"energia\":");                 // true = energia externa (5 V) conectada; false = bateria; null = sem sensor
  if (energiaAtiva && PIN_ENERGIA >= 0) j += energiaExterna ? "true" : "false";
  else j += F("null");
  j += F(",\"interno\":{");
  if (sensorOk) {
    j += "\"temp\":" + String(localTemp, 1) + ",\"umid\":" + String(localHum, 1);
  } else {
    j += F("\"temp\":null,\"umid\":null");
  }
  j += F("},\"externo\":{");
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
  j += F("},\"previsao\":[");
  for (uint8_t k = 0; k < MAX_FORECASTS; k++) {
    if (k) j += F(",");
    j += "{\"dia\":" + String((unsigned long)forecasts[k].observationTime) + ",";
    j += "\"icone\":\"" + jsonEscape(forecasts[k].icon) + "\",";
    j += "\"temp\":" + String(forecasts[k].temp, 1) + "}";
  }
  j += F("],\"prox24\":");                  // próximas 24 h (null até a 1ª previsão)
  if (prox24Ok) {
    j += "{\"min\":" + String(prox24Min, 1) + ",\"max\":" + String(prox24Max, 1) +
         ",\"chuva\":" + String(prox24Chuva, 1) + ",\"sens\":" + String(prox24Sens, 1) + "}";
  } else j += F("null");
  j += F(",\"lua\":");                      // fase da Lua (null até a hora sincronizar); nascer/pôr = 0 sem coordenadas
  if (luaOk) {
    j += "{\"fase\":" + String(luaFase) + ",\"nome\":\"" + String(nomeFaseLua(luaFase)) + "\",\"ilum\":" +
         String((int)(luaIlum * 100 + 0.5f)) + ",\"nasce\":" + String(astroOk ? (unsigned long)luaNasce : 0UL) +
         ",\"poe\":" + String(astroOk ? (unsigned long)luaPoe : 0UL) + "}";
  } else j += F("null");
  j += ",\"rssi\":" + String(WiFi.RSSI()) + ",\"rssiPct\":" + String(getRSSIasQuality(WiFi.RSSI())) + "}";
  server.send(200, "application/json", j);
}

/** GET /historico: histórico interno (do mais antigo para o mais novo) em JSON. */
void paginaHistorico() {
  String j;
  j.reserve(64 + histCount * 12);
  long prox = (long)(nextHist - millis()) / 1000;
  if (prox < 0) prox = 0;
  j += F("{\"ok\":");
  j += sensorOk ? "true" : "false";
  j += ",\"sensor\":\"" SENSOR_NOME "\",\"pino\":\"" + nomePino(DHTPIN) + "\"";   // usados na mensagem de erro da página
  j += ",\"proxima\":" + String(prox);
  j += ",\"intervalo\":" + String(HIST_INTERVAL_MS / 1000UL) + ",\"temp\":[";
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

// ---- SERVIDOR WEB: PÁGINA /config (SEÇÕES E TABELAS) ----------------------------------------

/** SVG do ícone de sinal de WiFi (4 barras) usado na seção WiFi. */
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

/** Cartão da seção Bateria e energia: carga, tensão, estado e situação. */
String tabelaBateria() {
  String t = F("<div class=\"grp\">");
  if (energiaAtiva && PIN_ENERGIA >= 0) t += linhaInfo("Alimentação", energiaExterna ? "🔌 Energia externa (5 V conectada)" : "🔋 Bateria (sem energia externa)");
  if (!bateriaAtiva) {
    t += linhaInfo("Estado", "Desativada (ative acima)");
  } else {
    const char* estado = bateriaPct <= BAT_CRITICA_PCT ? "⚠️ Crítica" : (bateriaPct <= BAT_AVISO_PCT ? "⚠️ Baixa" : "✅ Normal");
    t += linhaInfo("Carga", iconeBateria(bateriaPct) + " " + String(bateriaPct) + "%");
    t += linhaInfo("Tensão", String(bateriaV, 2) + " V");
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

  t += F("</div>");
  return t;
}

/** Tabela HTML com informações do ESP8266 (tempo ligado, memória, flash, firmware, sensor...). */
String tabelaSistema() {
  String t;
  t.reserve(1800);
  t = F("<div class=\"grp\">");

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
  t += linhaInfo("Servidores NTP", NTP_SERVIDOR_1 "<br>" NTP_SERVIDOR_2 "<br>" NTP_SERVIDOR_3);
  t += linhaInfo("Coordenadas da cidade", coordOk ? String(cidadeLat, 4) + ", " + String(cidadeLon, 4) : String("ainda não recebidas"));

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
  t += linhaInfo("Sensor " SENSOR_NOME, sensorUltimaOk ? String("OK") : "sem leitura válida (confira o dado no pino " + nomePino(DHTPIN) + ")");
  t += linhaInfo("Core ESP8266", htmlEsc(ESP.getCoreVersion()));
  t += linhaInfo("SDK", htmlEsc(String(ESP.getSdkVersion())));
  t += linhaInfo("Compilado em", String(__DATE__) + " " + String(__TIME__));

  t += F("</div>");
  return t;
}

// ---- /config: peças comuns ---------------------------------------------------------------

/** Tempo curto para os resumos ('menos de 1 min', '12 min', '3 h 5 min', '2 d 4 h'). */
String tempoCurto(unsigned long s) {
  if (s < 60UL) return "menos de 1 min";
  if (s < 3600UL) return String(s / 60UL) + " min";
  if (s < 86400UL) return String(s / 3600UL) + " h " + String((s % 3600UL) / 60UL) + " min";
  return String(s / 86400UL) + " d " + String((s % 86400UL) / 3600UL) + " h";
}

/** Texto da mensagem de resultado (código 'r' que volta depois de salvar). erro = true para avisos. */
String msgConfig(const String& r, bool& erro) {
  String m = F("");
  if (r == "ok") m = F("✅ Configurações salvas. Atualizando o clima...");
  else if (r == "chave") m = F("⚠️ Chave inválida: use só letras e números (20 a 40 caracteres).");
  else if (r == "local") m = F("⚠️ ID da cidade inválido: informe o número (ou cole o endereço da cidade).");
  else if (r == "local_vazio") m = F("⚠️ Informe a cidade: busque pelo nome, preencha a latitude e a longitude, ou informe o ID.");
  else if (r == "coord") m = F("⚠️ Coordenadas inválidas: preencha as duas (latitude de -90 a 90 e longitude de -180 a 180).");
  else if (r == "idioma") m = F("⚠️ Idioma inválido.");
  else if (r == "disp") m = F("✅ Tela atualizada.");
  else if (r == "tempo") m = F("✅ Tempo de cada tela salvo.");
  else if (r == "tempo_erro") m = F("⚠️ Tempo inválido: escolha de 5 a 30 segundos.");
  else if (r == "tempoligada") m = F("✅ Tempo de tela ligada salvo.");
  else if (r == "tempoligada_erro") m = F("⚠️ Tempo de tela ligada inválido.");
  else if (r == "led") m = F("✅ LED de status salvo.");
  else if (r == "brilho") m = F("✅ Brilho salvo.");
  else if (r == "brilho_erro") m = F("⚠️ Brilho inválido: escolha de 5% a 100%.");
  else if (r == "telas") m = F("✅ Telas do display salvas.");
  else if (r == "telas_erro") m = F("⚠️ Marque pelo menos uma tela.");
  else if (r == "agenda") m = F("✅ Horário automático salvo.");
  else if (r == "hora") m = F("⚠️ Horário inválido.");
  else if (r == "horaigual") m = F("⚠️ Os horários de ligar e de apagar não podem ser iguais.");
  else if (r == "fuso") m = F("⚠️ Fuso horário inválido.");
  else if (r == "fuso_ok") m = F("✅ Fuso horário salvo.");
  else if (r == "bat") m = F("✅ Bateria e energia salvas.");
  else if (r == "api") m = F("✅ Dados do painel salvos.");
  else if (r == "apiurl") m = F("⚠️ URL inválida: use http:// ou https:// e não deixe espaços.");
  else if (r == "apiid") m = F("⚠️ ID do dispositivo inválido: use até 32 caracteres (letras, números, - _ .).");
  else if (r == "apikey") m = F("⚠️ DEVICE_KEY inválida: use de 8 a 64 caracteres, sem espaços.");
  else if (r == "envio_ok") m = F("✅ Dados enviados ao painel.");
  else if (r == "envio_erro") m = String(F("⚠️ Falha no envio: ")) + htmlEsc(apiUltimoMsg);
  erro = m.startsWith("⚠️");
  return m;
}

/** Resumo do estado de uma seção, mostrado embaixo do nome no menu. */
String secaoResumo(uint8_t k) {
  String id = SECOES[k].id;
  if (id == "clima") {
    if (OPEN_WEATHER_MAP_APP_ID.length() == 0) return "⚠️ falta a chave da API";
    if (weatherOk) return htmlEsc(currentWeather.cityName) + " · chave salva";
    return "⚠️ sem dados do clima";
  }
  if (id == "display") {
    return String(displayLigado ? "Ligado" : "Apagado") + " · brilho " + String(brilhoAgora()) + "% · " +
           String(numTelasAtivas) + (numTelasAtivas == 1 ? " tela" : " telas");
  }
  if (id == "hora") {
    String fuso = "UTC";
    for (uint8_t f = 0; f < FUSO_COUNT; f++) if (FUSOS[f].min == fusoMin) fuso = String(FUSOS[f].nome).substring(0, 9);
    if (horarioVerao) fuso += " +verão";
    return fuso + (horaOk() ? " · sincronizada" : " · sem sincronizar");
  }
  if (id == "bateria") {
    String s = bateriaAtiva ? String(bateriaPct) + "% · " + String(bateriaV, 2) + " V" : String("bateria desativada");
    if (energiaAtiva && PIN_ENERGIA >= 0) s += energiaExterna ? " · na tomada" : " · na bateria";
    return s;
  }
  if (id == "painel") {
    if (apiUrl.length() == 0) return "envio desligado";
    if (!apiTentou) return "nenhum envio ainda";
    return "último envio há " + tempoCurto((millis() - apiUltimoMs) / 1000UL) + (apiUltimoOk ? " ✅" : " ⚠️");
  }
  if (id == "wifi") {
    if (WiFi.status() != WL_CONNECTED) return "⚠️ desconectado";
    return htmlEsc(WiFi.SSID()) + " · " + String(WiFi.RSSI()) + " dBm";
  }
  if (id == "led") {
    if (!ledStatusAtivo) return "desligado";
    return ledSegueAgenda ? "ligado · segue o horário da tela" : "ligado";
  }
  return "v" FIRMWARE_VERSAO " · ligado há " + tempoCurto(millis() / 1000UL);   // sistema
}

/**
 * Começa uma página da /config: cabeçalhos, estilo (da flash), link de voltar, título e a mensagem
 * de resultado. A página vai em partes (chunked): nenhuma página inteira fica na RAM.
 */
void cfgCabecalho(const String& titulo, const char* voltaHref, const char* voltaTexto, const String& r) {
  server.sendHeader("Cache-Control", "no-store");   // página protegida por login: o navegador não guarda cópia
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent("<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
                     "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + titulo + "</title>");
  server.sendContent_P(CFG_CSS);
  String h = "</head><body><div class=\"wrap\"><a class=\"vol\" href=\"" + String(voltaHref) + "\">‹ " + String(voltaTexto) + "</a>"
             "<h1>" + titulo + "</h1>";
  bool erro = false;
  String m = msgConfig(r, erro);
  if (m.length()) h += String("<div class=\"msg") + (erro ? " err" : "") + "\">" + m + "</div>";
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
  String s = "<label class=\"it\"><span class=\"tx\">" + rotulo;
  if (sub.length()) s += "<small class=\"w\">" + sub + "</small>";
  s += "</span><input type=\"checkbox\" class=\"sw\" name=\"" + String(nome) + "\" value=\"1\"";
  if (ligado) s += " checked";
  if (autoEnvia) s += " onchange=\"this.form.submit()\"";
  s += "></label>";
  return s;
}

/** Abre uma linha com lista de opções (feche com '</select></label>'). autoEnvia = salva ao escolher. */
String itemSelect(const String& rotulo, const char* nome, bool autoEnvia) {
  String s = "<label class=\"it\"><span class=\"tx\">" + rotulo + "</span><select name=\"" + String(nome) + "\"";
  if (autoEnvia) s += " onchange=\"this.form.submit()\"";
  s += ">";
  return s;
}

/** Uma opção de lista. */
String opcao(const String& valor, const String& texto, bool sel) {
  return "<option value=\"" + valor + "\"" + (sel ? " selected" : "") + ">" + texto + "</option>";
}

// ---- /config: menu ------------------------------------------------------------------------

/** Menu da /config: uma linha por seção, com ícone, nome e resumo do estado, em 3 grupos. */
void cfgMenu(const String& r) {
  cfgCabecalho("Configurações", "/", "Início", r);
  String h;
  h.reserve(2200);
  h = F("<div class=\"grp\">");
  for (uint8_t k = 0; k < SECOES_TOTAL; k++) {
    if (k == 3 || k == 6) h += F("</div><div class=\"grp\">");   // novo grupo
    h += "<a class=\"it\" href=\"/config?s=" + String(SECOES[k].id) + "\">"
         "<span class=\"ic\" style=\"background:" + String(SECOES[k].cor) + "\">" + String(SECOES[k].icone) + "</span>"
         "<span class=\"tx\">" + String(SECOES[k].nome) + "<small>" + secaoResumo(k) + "</small></span>"
         "<span class=\"chev\">›</span></a>";
  }
  h += F("</div>");
  if (loginAtivo()) h += F("<form method=\"POST\" action=\"/sair\"><button class=\"btn sec\" type=\"submit\">Sair (só neste navegador)</button></form>");
  h += "<div class=\"pe\">WeatherStation ESP8266 · v" FIRMWARE_VERSAO "</div>";
  cfgRodape(h);
}

// ---- /config: Cidade e clima --------------------------------------------------------------

/** Seção 'Cidade e clima': chave da API, cidade (busca pelo nome, latitude/longitude, ID) e idioma. */
void cfgClima(const String& r) {
  cfgCabecalho("Cidade e clima", "/config", "Configurações", r);
  String masked = "cole a chave aqui";
  if (OPEN_WEATHER_MAP_APP_ID.length() >= 4) {
    masked = "•••••••• " + OPEN_WEATHER_MAP_APP_ID.substring(OPEN_WEATHER_MAP_APP_ID.length() - 4) + " (salva)";
  }
  String h;
  h.reserve(3600);
  h = "<div class=\"cap\">Estado</div><div class=\"grp\">" +
      linhaInfo("Clima", weatherOk ? "✅ " + htmlEsc(currentWeather.cityName) : String("⚠️ sem dados")) +
      linhaInfo("Coordenadas", coordOk ? String(cidadeLat, 4) + ", " + String(cidadeLon, 4) : String("ainda não recebidas")) +
      "</div>"
      "<form method=\"POST\" action=\"/salvar\">"
      "<div class=\"cap\">Chave da API (OpenWeatherMap)</div><div class=\"grp\"><div class=\"fld\">"
      "<input name=\"apiKey\" type=\"text\" autocomplete=\"off\" autocapitalize=\"off\" placeholder=\"" + htmlEsc(masked) + "\">"
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
      "placeholder=\"-22.9056\" value=\"" + (coordOk ? String(cidadeLat, 4) : String("")) + "\"></label>"
      "<label class=\"it\"><span class=\"tx\">Longitude</span><input class=\"num\" name=\"lon\" id=\"lon\" type=\"text\" inputmode=\"decimal\" "
      "placeholder=\"-47.0608\" value=\"" + (coordOk ? String(cidadeLon, 4) : String("")) + "\"></label>"
      "<label class=\"it\"><span class=\"tx\">ID da cidade<small>opcional</small></span><input class=\"num\" name=\"locId\" id=\"loc\" "
      "type=\"text\" inputmode=\"numeric\" value=\"" + htmlEsc(OPEN_WEATHER_MAP_LOCATION_ID) + "\"></label>"
      "</div>";
  h += F("<div class=\"note\">Com a latitude e a longitude, valem elas; o ID, se houver, só vai para o painel (city_id). "
      "Só com o ID, a estação descobre as coordenadas na 1ª consulta. Também pode colar \"latitude, longitude\" (como o Google Maps copia) "
      "no campo da latitude. A busca usa a chave já salva. ID: "
      "<a href=\"https://openweathermap.org/find\" target=\"_blank\" rel=\"noopener\">openweathermap.org/find</a> (número no fim do endereço).</div>");
  server.sendContent(h);
  h = "<div class=\"cap\">Idioma das descrições</div><div class=\"grp\">" + itemSelect("Idioma", "lang", false);
  for (uint8_t k = 0; k < LANG_COUNT; k++) h += opcao(LANG_CODES[k], LANG_NAMES[k], OPEN_WEATHER_MAP_LANGUAGE == LANG_CODES[k]);
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
  cfgCabecalho("Display", "/config", "Configurações", r);
  String h;
  h.reserve(3000);
  // Tela ligada (a chave alterna) e o brilho de agora
  h = "<div class=\"cap\">Tela</div><div class=\"grp\">"
      "<form method=\"POST\" action=\"/display\"><input type=\"hidden\" name=\"estado\" value=\"t\"><input type=\"hidden\" name=\"volta\" value=\"1\">" +
      itemSwitch("x", "Tela ligada", displayLigado, true) + "</form>" +
      linhaInfo("Brilho agora", String(brilhoAgora()) + "%" + (noiteAgora() ? " (noturno)" : "")) + "</div>";

  // Telas do display (abre na própria página)
  h += F("<div class=\"cap\">Telas e horário</div>"
       "<details class=\"grp\"");
  if (r == "telas" || r == "telas_erro") h += F(" open");
  h += "><summary class=\"it\"><span class=\"tx\">Telas do display</span><span class=\"val\">" + String(numTelasAtivas) + " de " +
       String(TELAS_TOTAL) + "</span><span class=\"chev\">›</span></summary><form method=\"POST\" action=\"/telas\">";
  for (uint8_t k = 0; k < TELAS_TOTAL; k++) {
    h += itemSwitch(("t" + String(k)).c_str(), TELAS_NOME[k], telasMask & (1 << k), false,
                    k == TELA_WIFI ? String("mostra o IP no display") : String(""));
  }
  h += F("<div class=\"fld\"><button class=\"btn\" type=\"submit\">Salvar telas</button></div></form></details>");
  server.sendContent(h);

  // Horário automático (abre na própria página)
  h = F("<details class=\"grp\"");
  if (r == "agenda" || r == "hora" || r == "horaigual") h += F(" open");
  h += "><summary class=\"it\"><span class=\"tx\">Horário automático</span><span class=\"val\">" +
       (agendaAtiva ? formataHora(agendaLiga) + "–" + formataHora(agendaDesliga) : String("desligado")) +
       "</span><span class=\"chev\">›</span></summary><form method=\"POST\" action=\"/agenda\">" +
       itemSwitch("ativa", "Ligar e apagar sozinha", agendaAtiva, false) +
       "<label class=\"it\"><span class=\"tx\">Ligar às</span><input type=\"time\" name=\"liga\" value=\"" + formataHora(agendaLiga) + "\"></label>"
       "<label class=\"it\"><span class=\"tx\">Apagar às</span><input type=\"time\" name=\"desliga\" value=\"" + formataHora(agendaDesliga) + "\"></label>"
       "<div class=\"fld\"><button class=\"btn\" type=\"submit\">Salvar horário</button></div></form></details>";
  h += F("<div class=\"note\">Elas passam na ordem da lista; com uma só, ela fica fixa. Sem a tela \"WiFi e bateria\", o IP aparece só ao conectar. "
       "O botão manual da tela vale até o próximo horário programado (precisa da hora da internet).</div>");

  // Passagem das telas e tempo de tela ligada (salvam ao escolher)
  h += "<div class=\"cap\">Passagem</div><div class=\"grp\"><form method=\"POST\" action=\"/tempotela\">" +
       itemSelect("Tempo de cada tela", "seg", true);
  for (uint8_t sgs = 5; sgs <= 30; sgs += 5) h += opcao(String(sgs), String(sgs) + " s", sgs == telaSeg);
  h += "</select></label></form><form method=\"POST\" action=\"/tempoligada\">" + itemSelect("Apagar sozinha após", "min", true);
  static const uint8_t OPCOES_MIN[] = {0, 1, 5, 10, 15, 30, 60};
  for (uint8_t k = 0; k < 7; k++) {
    h += opcao(String(OPCOES_MIN[k]), OPCOES_MIN[k] == 0 ? String("Nunca") : String(OPCOES_MIN[k]) + " min", OPCOES_MIN[k] == telaTimeoutMin);
  }
  h += F("</select></label></form></div>"
       "<div class=\"note\">Com a tela apagada, um clique no botão das telas acende; no horário de apagar, acende só por 10 s.</div>");
  server.sendContent(h);

  // Brilho (salva ao escolher)
  h = "<div class=\"cap\">Brilho</div><div class=\"grp\"><form method=\"POST\" action=\"/brilho\">" + itemSelect("Dia", "dia", true);
  for (uint8_t p = 100; p >= 5; p -= 5) h += opcao(String(p), String(p) + "%", p == brilhoPct);
  h += "</select></label>" + itemSelect("Noite", "noite", true);
  for (uint8_t p = 100; p >= 5; p -= 5) h += opcao(String(p), String(p) + "%", p == brilhoNoitePct);
  h += "</select></label>" + itemSelect("A noite começa", "fonte", true) +
       opcao("0", "No horário automático", !noitePeloSol) + opcao("1", "No pôr do sol", noitePeloSol) +
       "</select></label>" + itemSelect("No horário de apagar", "modo", true) +
       opcao("0", "Apagar a tela", !noiteDiminui) + opcao("1", "Manter acesa", noiteDiminui) +
       "</select></label></form></div>"
       "<div class=\"note\">Pôr do sol: o brilho noturno vale do pôr ao nascer do sol (";
  h += astroOk ? "hoje " + horaCurta(solPoe) + " a " + horaCurta(solNasce) : String("disponível depois de receber o clima");
  h += F("). Horário automático: vale no horário de apagar. Acender a tela à noite usa sempre o brilho noturno.</div>");
  cfgRodape(h);
}

// ---- /config: Data e hora -----------------------------------------------------------------

/** Seção 'Data e hora': hora atual, fuso e horário de verão (salvam ao escolher) e servidores NTP. */
void cfgHora(const String& r) {
  cfgCabecalho("Data e hora", "/config", "Configurações", r);
  String agora = "sem sincronização";
  if (horaOk()) {
    time_t t = time(nullptr);
    struct tm* ti = localtime(&t);
    char b[24];
    snprintf(b, sizeof(b), "%02d/%02d/%04d %02d:%02d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900, ti->tm_hour, ti->tm_min);
    agora = b;
  }
  String h;
  h.reserve(2600);
  h = "<div class=\"grp\">" + linhaInfo("Agora", agora) + "</div>"
      "<div class=\"cap\">Fuso horário</div><div class=\"grp\"><form method=\"POST\" action=\"/hora\">" + itemSelect("Fuso", "fuso", true);
  for (uint8_t k = 0; k < FUSO_COUNT; k++) h += opcao(String(FUSOS[k].min), FUSOS[k].nome, FUSOS[k].min == fusoMin);
  h += "</select></label>" + itemSwitch("verao", "Horário de verão", horarioVerao, true, "adianta 1 hora") + "</form></div>"
       "<div class=\"cap\">Servidores da hora (NTP)</div><div class=\"grp\">" +
       linhaInfo("1º", NTP_SERVIDOR_1) + linhaInfo("2º", NTP_SERVIDOR_2) + linhaInfo("3º", NTP_SERVIDOR_3) + "</div>"
       "<div class=\"note\">Os servidores são definidos no WeatherStationConfig.h e usados na ordem, se um não responder.</div>";
  cfgRodape(h);
}

// ---- /config: Bateria e energia -----------------------------------------------------------

/** Seção 'Bateria e energia': ativar a leitura da bateria e o sensor da UPS (salvam ao tocar) e o estado. */
void cfgBateria(const String& r) {
  cfgCabecalho("Bateria e energia", "/config", "Configurações", r);
  String h;
  h.reserve(1800);
  h = "<div class=\"grp\"><form method=\"POST\" action=\"/bateria\">" +
      itemSwitch("ativa", "Bateria no A0", bateriaAtiva, true, "este ESP tem bateria ligada ao A0 pelo divisor") +
      itemSwitch("energia", "Placa UPS", energiaAtiva, true, "sensor de energia externa (5 V) no pino de energia") +
      "</form></div>";
  if (bateriaAtiva || energiaAtiva) h += "<div class=\"cap\">Estado</div>" + tabelaBateria();
  else h += F("<div class=\"note\">Ative só o que este ESP tem ligado (veja as ligações no README).</div>");
  cfgRodape(h);
}

// ---- /config: Painel web ------------------------------------------------------------------

/** Seção 'Painel web': último envio, dados do painel (ID, URL, DEVICE_KEY) e 'Enviar dados agora'. */
void cfgPainel(const String& r) {
  cfgCabecalho("Painel web", "/config", "Configurações", r);
  String maskedKey = "cole a DEVICE_KEY aqui";
  if (deviceKey.length() >= 4) maskedKey = "•••••••• " + deviceKey.substring(deviceKey.length() - 4) + " (salva)";
  String ultimo = !apiTentou ? String("nenhum ainda")
                : "há " + tempoCurto((millis() - apiUltimoMs) / 1000UL) + (apiUltimoOk ? " · ✅ " : " · ⚠️ ") + htmlEsc(apiUltimoMsg);
  String cidade = currentWeather.cityName.length() ? htmlEsc(currentWeather.cityName) : String("(ainda não recebida)");
  cidade += OPEN_WEATHER_MAP_LOCATION_ID.length() ? " · ID " + htmlEsc(OPEN_WEATHER_MAP_LOCATION_ID) : String(" · sem ID (null)");
  String h;
  h.reserve(2600);
  h = "<div class=\"cap\">Envio</div><div class=\"grp\">" +
      linhaInfo("Último envio", ultimo) +
      linhaInfo("Automático", apiUrl.length() ? "a cada " + String(PAINEL_INTERVAL_MS / 60000UL) + " min" : String("desligado (sem URL)")) +
      linhaInfo("Cidade enviada", cidade) + "</div>"
      "<form method=\"POST\" action=\"/enviar\"><button class=\"btn sec\" type=\"submit\">Enviar dados agora</button></form>"
      "<form method=\"POST\" action=\"/api\"><div class=\"cap\">Dados do painel</div><div class=\"grp\">"
      "<div class=\"fld\"><span class=\"lb\">ID do dispositivo (device_id)</span>"
      "<input name=\"deviceId\" type=\"text\" autocapitalize=\"off\" value=\"" + htmlEsc(idDispositivo()) + "\"></div>"
      "<div class=\"fld\"><span class=\"lb\">URL da API</span>"
      "<input name=\"apiUrl\" type=\"text\" inputmode=\"url\" autocapitalize=\"off\" placeholder=\"https://seu-app.vercel.app/api/...\" value=\"" +
      htmlEsc(apiUrl) + "\"></div>"
      "<div class=\"fld\"><span class=\"lb\">DEVICE_KEY</span>"
      "<input name=\"deviceKey\" type=\"text\" autocomplete=\"off\" autocapitalize=\"off\" placeholder=\"" + htmlEsc(maskedKey) + "\"></div>"
      "</div>";
  h += F("<div class=\"note\">O ID precisa ser igual ao cadastrado no painel. Deixe a chave em branco para manter a atual. "
      "Use https:// (a Vercel redireciona o http). Sem URL, o envio fica desligado.</div>"
      "<button class=\"btn\" type=\"submit\">Salvar dados do painel</button></form>");
  cfgRodape(h);
}

// ---- /config: WiFi ------------------------------------------------------------------------

/** Seção 'WiFi': dados da conexão, abrir o portal e apagar a rede salva. */
void cfgWiFi(const String& r) {
  cfgCabecalho("WiFi", "/config", "Configurações", r);
  String h = tabelaWiFi();
  h += "<form method=\"POST\" action=\"/portal\" onsubmit=\"return confirm('Abrir o portal de configuração? A estação reinicia ao terminar.')\">"
       "<button class=\"btn sec\" type=\"submit\">Abrir portal de configuração</button></form>"
       "<form method=\"POST\" action=\"/reset-wifi\" onsubmit=\"return confirm('Apagar a rede WiFi salva e reiniciar?')\">"
       "<button class=\"btn red\" type=\"submit\">Apagar dados de WiFi</button></form>"
       "<div class=\"note\" style=\"margin-top:-8px\">O portal cria a rede " PORTAL_SSID " (com a senha PORTAL_SENHA do WeatherStationConfig.h).</div>";
  cfgRodape(h);
}

// ---- /config: LED de status ---------------------------------------------------------------

/** Seção 'LED de status': ligar/desligar e seguir o horário automático da tela (salvam ao tocar). */
void cfgLed(const String& r) {
  cfgCabecalho("LED de status", "/config", "Configurações", r);
  String h = "<div class=\"grp\"><form method=\"POST\" action=\"/led\">" +
             itemSwitch("ativo", "Piscar para indicar o estado", ledStatusAtivo, true,
                        "rápido ao conectar e ao enviar, devagar sem WiFi; 1 acendida = sucesso, 3 piscadas = falha") +
             itemSwitch("agenda", "Seguir o horário da tela", ledSegueAgenda, true,
                        "apagado no horário de tela desligada (precisa do horário automático)") +
             "</form></div>";
  cfgRodape(h);
}

// ---- /config: Sistema ---------------------------------------------------------------------

/** Seção 'Sistema': informações do ESP8266, login (desconectar todos) e reset geral. */
void cfgSistema(const String& r) {
  cfgCabecalho("Sistema", "/config", "Configurações", r);
  String h = tabelaSistema();
  server.sendContent(h);
  h = F("");
  if (loginAtivo()) {
    h += "<div class=\"cap\">Login</div>"
         "<form method=\"POST\" action=\"/sair-todos\" onsubmit=\"return confirm('Desconectar todos os navegadores? Todos vão precisar digitar a senha de novo.')\">"
         "<button class=\"btn sec\" type=\"submit\">Desconectar todos os navegadores</button></form>"
         "<div class=\"note\" style=\"margin-top:-8px\">Cada navegador fica conectado por " + String(LOGIN_DIAS) + " dias. "
         "Use se perdeu um celular ou entrou num computador de outra pessoa.</div>";
  }
  h += F("<div class=\"cap\">Reset geral</div>"
       "<form method=\"POST\" action=\"/reset-geral\" onsubmit=\"return confirm('Apagar o WiFi e TODOS os ajustes e reiniciar?')\">"
       "<button class=\"btn red\" type=\"submit\">Reset geral</button></form>"
       "<div class=\"note\" style=\"margin-top:-8px\">Apaga o WiFi e todos os ajustes salvos e reinicia no padrão do código. "
       "Também pode ser feito segurando o botão de reset geral por 10 s.</div>");
  cfgRodape(h);
}

/** GET /config: o menu, ou a seção pedida em ?s= (cada uma é uma página pequena, enviada em partes). */
void paginaConfig() {
  if (!exigeLogin()) return;            // sem login: vai para /login
  String s = server.arg("s");
  String r = server.arg("r");
  if (s == "clima") cfgClima(r);
  else if (s == "display") cfgDisplay(r);
  else if (s == "hora") cfgHora(r);
  else if (s == "bateria") cfgBateria(r);
  else if (s == "painel") cfgPainel(r);
  else if (s == "wifi") cfgWiFi(r);
  else if (s == "led") cfgLed(r);
  else if (s == "sistema") cfgSistema(r);
  else cfgMenu(r);
}

// ---- SERVIDOR WEB: AÇÕES (POST) -----------------------------------------------------------

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
 * GET /buscar-cidade?q=Campinas,BR: busca a cidade pelo nome na API de geocodificação do
 * OpenWeatherMap (gratuita, mesma chave) e devolve até 5 resultados com as coordenadas, em JSON,
 * para a seção Cidade e clima preencher a latitude e a longitude. Precisa estar logado.
 */
void handleBuscaCidade() {
  server.sendHeader("Cache-Control", "no-store");
  if (!logado()) { server.send(401, "application/json", "{\"ok\":false,\"erro\":\"Faça o login de novo (recarregue a página).\"}"); return; }
  String q = server.arg("q");
  q.trim();
  if (q.length() < 2 || q.length() > 60) { server.send(200, "application/json", "{\"ok\":false,\"erro\":\"Digite de 2 a 60 caracteres.\"}"); return; }
  if (OPEN_WEATHER_MAP_APP_ID.length() == 0) { server.send(200, "application/json", "{\"ok\":false,\"erro\":\"Salve primeiro a chave da API (campo acima).\"}"); return; }
  if (WiFi.status() != WL_CONNECTED) { server.send(200, "application/json", "{\"ok\":false,\"erro\":\"Sem WiFi.\"}"); return; }

  WiFiClient cliente;
  HTTPClient http;
  http.setTimeout(8000);
  http.useHTTP10(true);                 // resposta sem 'chunked': o leitor recebe o JSON puro
  String url = "http://api.openweathermap.org/geo/1.0/direct?q=" + urlEncode(q) + "&limit=" + String(BuscaCidade::MAX) +
               "&appid=" + OPEN_WEATHER_MAP_APP_ID;
  if (!http.begin(cliente, url)) { server.send(200, "application/json", "{\"ok\":false,\"erro\":\"Falha ao preparar a busca.\"}"); return; }
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
    String e = (codigo == 401) ? String("Chave da API inválida ou ainda não ativada.")
             : (codigo > 0) ? "HTTP " + String(codigo) + (busca.erro.length() ? ": " + busca.erro : String(""))
             : HTTPClient::errorToString(codigo);
    j = "{\"ok\":false,\"erro\":\"" + jsonEscape(e) + "\"}";
  } else {
    j = F("{\"ok\":true,\"cidades\":[");
    for (uint8_t k = 0; k < busca.n; k++) {
      BuscaCidade::Resultado& c = busca.r[k];
      if (k) j += F(",");
      j += "{\"nome\":\"" + jsonEscape(c.nomePt.length() ? c.nomePt : c.nome) + "\",\"estado\":\"" + jsonEscape(c.estado) +
           "\",\"pais\":\"" + jsonEscape(c.pais) + "\",\"lat\":" + String(c.lat, 4) + ",\"lon\":" + String(c.lon, 4) + "}";
    }
    j += F("]}");
  }
  server.send(200, "application/json", j);
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

/** POST /salvar: valida e salva chave, cidade (coordenadas e/ou ID), idioma e fuso do OpenWeatherMap. */
void handleSalvar() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
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
  if (loc.length() > 10) { redirecionaConfig("local"); return; }

  // Coordenadas: opcionais. Aceita colar 'lat, lon' (como o Google Maps copia) no campo da latitude.
  String latRaw = server.arg("lat");
  String lonRaw = server.arg("lon");
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
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
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
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  bool ok = enviaParaApi();
  if (ok) nextApi = millis() + PAINEL_INTERVAL_MS;       // recomeça o ciclo do envio automático
  redirecionaConfig(ok ? "envio_ok" : "envio_erro");
}

/**
 * POST /tempoligada: salva o tempo de tela ligada (0 = sempre ligada; ou 1, 5, 10, 15, 30, 60
 * min).
 */
void handleTempoLigada() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
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
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  int seg = server.arg("seg").toInt();
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
  ledStatusAtivo = server.hasArg("ativo");
  ledSegueAgenda = server.hasArg("agenda");
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
  bateriaAtiva = server.hasArg("ativa");
  if (bateriaAtiva) {
    bateriaV = 0;                        // recomeça a média
    atualizaBateria();
    nextBateria = millis() + 30000UL;
  }
  energiaAtiva = server.hasArg("energia");
  if (energiaAtiva) energiaLeAgora();    // já mostra o estado atual da energia
  salvaConfig();
  redirecionaConfig("bat");
}

/**
 * POST /brilho: salva o brilho de dia, o brilho noturno e o que fazer à noite (apagar a tela ou só
 * diminuir o brilho). Vale na hora.
 */
void handleBrilho() {
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  int dia = server.arg("dia").toInt();
  int noite = server.arg("noite").toInt();
  if (!brilhoValido(dia) || !brilhoValido(noite)) { redirecionaConfig("brilho_erro"); return; }
  brilhoPct = dia;
  brilhoNoitePct = noite;
  noiteDiminui = (server.arg("modo") == "1");
  noitePeloSol = (server.arg("fonte") == "1");
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
  if (!server.hasArg("fuso")) { redirecionaConfig("fuso"); return; }
  int fuso = server.arg("fuso").toInt();
  bool okFuso = false;
  for (uint8_t k = 0; k < FUSO_COUNT; k++) {
    if (FUSOS[k].min == fuso) okFuso = true;
  }
  if (!okFuso) { redirecionaConfig("fuso"); return; }
  fusoMin = fuso;
  horarioVerao = server.hasArg("verao");
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
    if (server.hasArg("t" + String(k))) mask |= (1 << k);
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
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
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
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
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
  if (!requisicaoSegura()) return;      // recusa POST vindo de outro site (CSRF)
  if (!exigeLogin()) return;           // precisa estar logado (senha da /config)
  server.send(200, "text/html", "<meta charset=\"utf-8\"><p>Reset geral feito. Reiniciando... Conecte-se à rede " PORTAL_SSID " para configurar de novo.</p>");
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
  server.send(200, "text/html", "<meta charset=\"utf-8\"><p>Conecte-se à rede " PORTAL_SSID " para configurar. A estação reinicia ao terminar.</p>");
  delay(500);
  server.stop();                        // o portal usa a porta 80
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.startConfigPortal(PORTAL_SSID, PORTAL_SENHA);
  ESP.restart();
}
