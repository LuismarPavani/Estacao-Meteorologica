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
 * Estação Meteorológica ESP8266: estação com display OLED, sensor DHT, clima do OpenWeatherMap,
 * página web local, configuração pelo navegador, bateria, LED de status, botões físicos, OTA e envio
 * das leituras para um painel (API REST).
 *
 * MAPA DO ARQUIVO (procure pelos números):
 *   1. Bibliotecas
 *   2. Seleção da placa e pinagem        <- em EstacaoConfig.h (junto com as senhas e chaves)
 *   3. Configurações do projeto          <- em EstacaoConfig.h
 *   4. Tipos
 *   5. Objetos e variáveis de estado
 *   6. Protótipos das funções
 *   7. Tabelas (telas do OLED, idiomas, fusos, seções da /config e a página principal)
 *   8. Funções principais: setup, loop e etapas do boot
 *
 * ABAS (as outras funções; a IDE junta todas a este arquivo antes de compilar):
 *   Config.ino     configuração salva (EEPROM) e fuso horário
 *   Placa.ino      LED, botões, sensor, bateria, energia externa, economia e WiFi   (muda no ESP32)
 *   Clima.ino      clima do OpenWeatherMap e próximas 24 h
 *   Tela.ino       display OLED: telas, ícones, brilho, tempo ligada e agenda       (troca no ESP32)
 *   Painel.ino     envio ao painel web (API REST)
 *   Web.ino        servidor web: utilitários de texto, login e página principal
 *   WebConfig.ino  página /config (seções e tabelas)
 *   WebAcoes.ino   busca de cidade (GET) e ações (POST)
 *   Backup.ino     backup das configurações (exportar e importar)
 *   Registro.ino   registro de eventos (/config → Registro, LOG_WEB no EstacaoConfig.h)
 *
 * Globais, tipos, tabelas e protótipos ficam neste arquivo, para valer em todas as abas.
 *
 * Nomes: funções e variáveis em português; 'desenha...' desenha no OLED, 'handle...' atende uma rota
 * do servidor web, 'pagina...' monta uma página e 'cfg...' monta uma seção da /config.
 */

// ==========================================================================================
//  1. BIBLIOTECAS
// ==========================================================================================
// Regra: <...> = core do ESP8266, C/C++ e bibliotecas instaladas; "..." = só arquivos desta pasta.
// Ordem: core e sistema, rede, bibliotecas instaladas (por finalidade) e, por último, os do projeto.

// ---- Core do ESP8266 e sistema --------------------------------------------------------------
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>                 // atualização de firmware pela rede
#include <EEPROM.h>                     // guarda os ajustes da /config na flash
#include <Ticker.h>                     // pisca o LED de status sem travar o loop
#include <Wire.h>                       // I2C do display OLED
#include <coredecls.h>                  // settimeofday_cb(): aviso de hora sincronizada (NTP)
#include <time.h>                       // time(), localtime()
#include <sys/time.h>                   // struct timeval
#include <new>                          // new (std::nothrow): blocos da previsão sem travar se faltar memória

// ---- Rede: HTTP, HTTPS e portal do WiFi -----------------------------------------------------
#include <ESP8266HTTPClient.h>          // envio das leituras ao painel (API)
#include <WiFiClientSecure.h>           // HTTPS
#include <bearssl/bearssl.h>            // SHA-256 do login da /config (o BearSSL vem no core)
#include <DNSServer.h>                  // usado pelo portal do WiFiManager
#include <WiFiManager.h>                // portal de configuração do WiFi (tzapu)

// ---- JSON lido aos poucos (busca de cidades e backup) ---------------------------------------
#include <JsonListener.h>
#include <JsonStreamingParser.h>

// ---- Display OLED (ThingPulse OLED SSD1306) -------------------------------------------------
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>

// ---- Clima, Sol e Lua (ThingPulse ESP8266 Weather Station) ----------------------------------
#include <ESPHTTPClient.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>
#include <Astronomy.h>                  // fase e iluminação da Lua (só precisa da data)
#include <SunMoonCalc.h>                // nascer/pôr do sol e da Lua e altura do sol (precisa das coordenadas)

// ---- Sensor de temperatura e umidade (Adafruit DHT) -----------------------------------------
#include <DHT.h>

// ---- Fontes e imagens do projeto (arquivos .h nesta pasta) ----------------------------------
#include "EstacaoFontes.h"
#include "EstacaoImagens.h"

// ==========================================================================================
//  2. SELEÇÃO DA PLACA E PINAGEM  e  3. CONFIGURAÇÕES DO PROJETO
//  Ficam no arquivo EstacaoConfig.h (mesma pasta), junto com as senhas e chaves.
//  Edite só aquele arquivo: aqui não há nada para ajustar.
// ==========================================================================================
#include "EstacaoConfig.h"

// Modelo do sensor para a biblioteca (DHTTYPE) e nome nas mensagens (página, Sistema e Serial),
// a partir do SENSOR_TIPO do EstacaoConfig.h. É um número porque DHT11/DHT22 são constantes de C++,
// que o #if do pré-processador não enxerga.
#if SENSOR_TIPO == 11
  #define DHTTYPE DHT11
  #define SENSOR_NOME "DHT11"
#elif SENSOR_TIPO == 12
  #define DHTTYPE DHT12
  #define SENSOR_NOME "DHT12"
#elif SENSOR_TIPO == 21
  #define DHTTYPE DHT21
  #define SENSOR_NOME "DHT21"
#elif SENSOR_TIPO == 22
  #define DHTTYPE DHT22
  #define SENSOR_NOME "DHT22"
#else
  #error "SENSOR_TIPO invalido: use 11, 12, 21 ou 22"
#endif

// Registro de eventos: padrões para um EstacaoConfig.h antigo, sem as linhas LOG_*
#ifndef LOG_WEB
  #define LOG_WEB 1
#endif
#ifndef LOG_BYTES
  #define LOG_BYTES 1024
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
  uint8_t ecoOk;                        // 0xA5 = escolha da economia na bateria salva
  uint8_t eco;                          // 1 = economia na bateria ligada
  uint8_t ecoIntervalo;                 // envio ao painel na bateria, em minutos
};

// Leitor da resposta da busca de cidades (API de geocodificação do OpenWeatherMap), lida aos poucos
// para não guardar o JSON inteiro na RAM (cada cidade traz dezenas de nomes em outras línguas).
// Formato: [{"name":"Campinas","local_names":{"pt":"Campinas",...},"lat":-22.9,"lon":-47.06,"country":"BR","state":"São Paulo"}, ...]
// Em erro vem um objeto: {"cod":401,"message":"Invalid API key ..."}
class BuscaCidade final : public JsonListener {   // final: apagada pelo próprio tipo (sem aviso de destrutor virtual)
  public:
    static const uint8_t MAX = 5;
    struct Resultado { String nome; String nomePt; String estado; String pais; float lat = 0; float lon = 0; };
    Resultado r[MAX];
    uint8_t n = 0;
    String erro = "";
    void whitespace(char /*c*/) override {}
    void startDocument() override {}
    void endDocument() override {}
    void startArray() override {}
    void endArray() override {}
    void key(String k) override { chave = k; }
    void startObject() override {
      nivel++;
      if (nivel == 2) emNomesLocais = (chave.equals(F("local_names")));
    }
    void endObject() override {
      if (nivel == 1 && n < MAX && r[n].nome.length()) n++;   // terminou uma cidade
      if (nivel == 2) emNomesLocais = false;
      nivel--;
    }
    void value(String v) override {
      if (n >= MAX) return;
      if (nivel == 1) {
        if (chave.equals(F("name"))) r[n].nome = v;
        else if (chave.equals(F("lat"))) r[n].lat = v.toFloat();
        else if (chave.equals(F("lon"))) r[n].lon = v.toFloat();
        else if (chave.equals(F("country"))) r[n].pais = v;
        else if (chave.equals(F("state"))) r[n].estado = v;
        else if (chave.equals(F("message"))) erro = v;
      } else if (nivel == 2 && emNomesLocais && chave.equals(F("pt"))) {
        r[n].nomePt = v;                  // nome em português, quando a API tem
      }
    }
  private:
    String chave = "";
    int nivel = 0;
    bool emNomesLocais = false;
};

// Leitor do arquivo de backup da /config (JSON de um nível só: {"formato":1,"brilho":80,...}).
// Guarda os pares chave/valor do primeiro nível; objetos e listas internos são ignorados.
class LeitorBackup final : public JsonListener {  // final: o delete do handleImportar não precisa de destrutor virtual
  public:
    static const uint8_t MAX = 40;      // o backup tem ~32 campos; sobra para versões futuras
    String k[MAX];
    String v[MAX];
    uint8_t n = 0;
    void whitespace(char /*c*/) override {}
    void startDocument() override {}
    void endDocument() override {}
    void startArray() override { nivel++; }
    void endArray() override { nivel--; }
    void startObject() override { nivel++; }
    void endObject() override { nivel--; }
    void key(String s) override { chave = s; }
    void value(String s) override { if (nivel == 1 && n < MAX) { k[n] = chave; v[n] = s; n++; } }
    /** Índice da chave no arquivo, ou -1 se ela não veio. */
    int busca(const __FlashStringHelper* nome) const {   // nome na flash: F("brilho")
      for (uint8_t i = 0; i < n; i++) if (strcmp_P(k[i].c_str(), (PGM_P)nome) == 0) return i;
      return -1;
    }
  private:
    String chave = "";
    int nivel = 0;
};

// Estado de uma importação de backup: o arquivo lido, quantos ajustes foram aplicados e quais foram
// ignorados. Passado para as funções importa...() (uma por grupo de ajustes da /config).
struct Importacao {
  LeitorBackup* b = nullptr;            // campos do arquivo
  uint8_t aplicados = 0;
  String ignorados = "";                // nomes dos campos com valor inválido, separados por vírgula
  String s = "";                        // valor do último campo lido por pega()
  bool pega(const __FlashStringHelper* nome);
  void ignora(const __FlashStringHelper* nome);
  void simNao(const __FlashStringHelper* nome, bool& var);
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
bool displayLigado = true;              // false = tela OLED apagada agora (pela escolha, pelo tempo de tela ligada ou pela agenda)
bool displayLigadoSalvo = true;         // escolha manual (botão da página e backup): é ela que vai para a EEPROM e vale no boot
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
bool ecoBateria = ECO_BATERIA_PADRAO;   // economia na bateria ligada? (escolhida em /config → Bateria e energia)
uint8_t ecoIntervaloMin = ECO_INTERVALO_PADRAO;   // envio ao painel na bateria, em minutos
bool ecoDormindo = false;               // true = WiFi em light sleep agora
unsigned long ecoAcordadoAte = 0;       // millis() até quando fica acordado (acesso à página, clique, envio, OTA)
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

// ---- Memória livre no começo do boot (referência do "de ~X KB" na seção Sistema) ----------
// O ESP8266 não informa o tamanho total do heap; o livre no início do setup() é o mais perto disso.
uint32_t heapBoot = 0;

// ---- Registro de eventos (/config → Registro): anel fixo na RAM, perdido ao reiniciar -------
#if LOG_WEB
struct LogEntrada {
  int32_t quando;                       // > 0: hora da internet (epoch); <= 0: -(segundos desde que ligou) - 1
  char txt[44];                         // até 43 bytes de texto (UTF-8, cortado sem quebrar acentos)
};
static const uint8_t LOG_LINHAS = LOG_BYTES / sizeof(LogEntrada);   // 1024 bytes = 21 eventos
static_assert(LOG_LINHAS >= 4 && LOG_BYTES / sizeof(LogEntrada) <= 255, "LOG_BYTES: use de 192 a 12000");
LogEntrada logBuf[LOG_LINHAS];
uint8_t logHead = 0;                    // próxima posição a gravar
uint8_t logCount = 0;                   // eventos guardados (até LOG_LINHAS)
uint32_t logTotal = 0;                  // eventos desde que ligou (inclusive os que já saíram do anel)
unsigned long logUltimoMs = 0;          // millis() do último evento
#else
#define logEvento(x) do {} while (0)    // registro desligado: as chamadas somem na compilação
#endif

// ==========================================================================================
//  6. PROTÓTIPOS DAS FUNÇÕES
//  Declaradas aqui para que a ordem das funções abaixo não importe.
// ==========================================================================================

// ---- PRINCIPAL: ETAPAS DO BOOT (chamadas pelo setup) ----------------------------------------
void iniciaPinosESensor();
void iniciaDisplayBoot();
void conectaWiFiBoot(const String& hostname);
void configuraOta(const String& hostname);
void mostraResultadoWiFiBoot();
void iniciaInterfaceOled();
void iniciaServidorWeb();

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
const __FlashStringHelper* bateriaSituacao(float v);

// ---- ENERGIA EXTERNA (placa UPS) ----------------------------------------------------------
void energiaLeAgora();
void atualizaEnergia(unsigned long ms);

// ---- CLIMA (OpenWeatherMap) ---------------------------------------------------------------
void atualizaClima(OLEDDisplay *display);

// ---- DISPLAY OLED: TELAS E ÍCONES ---------------------------------------------------------
void aplicaDisplay();
void desenhaAbertura();
bool brilhoValido(int v);
bool noiteAgora();
uint8_t brilhoAgora();
void ligaTela();
bool naBateria();
bool ecoIntervaloValido(int m);
unsigned long intervaloEnvioMs();
void ecoAcorda(unsigned long dur);
void atualizaEco(unsigned long ms);
void desenhaProgresso(OLEDDisplay *display, int percentage, String label);
void mostraMensagemTela(const String& titulo, const String& l1, const String& l2, unsigned long dur);
void desenhaMensagemTela();
bool trataMensagemTela(unsigned long ms);
void desenhaCabecalho(const String& titulo);
void desenhaIconeNuvemEnvio(int cx, int cy);
void desenhaIconeOk(int cx, int cy);
void desenhaIconeFalha(int cx, int cy);
String cabeNaLargura(const String& s, int largura);
void desenhaEnvioEtapa(int pct, const String& texto);
void desenhaResultadoEnvio();
bool horaOk();
void desenhaSemClima(OLEDDisplay *display, int16_t x, int16_t y, const String& titulo);
void desenhaDataHora(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaClimaInterno(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaClimaAtual(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaClimaDetalhes(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaPrevisao(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaPrevisaoDia(OLEDDisplay *display, int x, int y, int dayIndex);
void desenhaTelaWiFi(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaBarraInferior(OLEDDisplay *display, OLEDDisplayUiState* state);
void desenhaIndicadores(OLEDDisplay *display, OLEDDisplayUiState* state);
void desenhaTelaSolLua(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void desenhaLua(OLEDDisplay *display, int cx, int cy, int r, uint8_t fase, float ilum, bool sul);
void desenhaSeta(OLEDDisplay *display, int x, int y, bool cima);
void desenhaSolPequeno(OLEDDisplay *display, int cx, int cy);
void desenhaLuaPequena(OLEDDisplay *display, int cx, int cy);
String horaCurta(time_t t);
const __FlashStringHelper* nomeFaseLua(uint8_t fase);
void atualizaAstro();
uint8_t contaTelas(uint8_t mask);
void aplicaTelas();
void desenhaConectando(uint8_t passo);
void desenhaIconeWiFiGrande(int x, int y, int pct);
void desenhaTelaIp();
void calculaPrevisao(OpenWeatherMapForecastData* b, uint8_t n);
void desenhaIconeBateria(OLEDDisplay *display, int x, int y, int pct);
void desenhaIconeSinal(OLEDDisplay *display, int x, int y, int pct);
void desenhaIconeTomada(OLEDDisplay *display, int x, int y, bool ligada);
int qualidadeSinal(int rssi);

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
String urlEncode(const String& s);
int8_t parseCoord(String s, float& out, float minimo, float maximo);
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
void paginaSobre();

// ---- SERVIDOR WEB: PÁGINA /config (SEÇÕES E TABELAS) ----------------------------------------
String iconeSinal(int pct);
String iconeBateria(int pct);
String tabelaBateria();
String tabelaWiFi();
void enviaTabelaSistema();
String linhaViva(uint8_t k);
void paginaSistemaJson();
String barraPct(uint8_t pct, char nivel, const String& texto);
String linhaSub(const __FlashStringHelper* rotulo, const String& sub, const String& valor);
String kb1(uint32_t bytes);
String tempoCurto(unsigned long s);
String msgConfig(const String& r, bool& erro);
String secaoResumo(uint8_t k);
void cfgCabecalho(const String& titulo, const String& voltaHref, const String& voltaTexto, const String& r);
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

// ---- SERVIDOR WEB: BUSCA DE CIDADE (GET) ------------------------------------------------
void handleBuscaCidade();

// ---- SERVIDOR WEB: AÇÕES (POST) -----------------------------------------------------------
void handleSalvar();
void handleHora();
void handleApi();
void handleEnviar();
void handleTempoLigada();
void handleTempoTela();
void handleBateria();
void handleEconomia();
void handleLed();
void handleBrilho();
void handleTelas();
void handleAgenda();
void handleDisplay();
void handleResetWiFi();
void handleResetGeral();
void handleReiniciar();
void handlePortal();

// ---- BACKUP DAS CONFIGURAÇÕES (exportar e importar) -----------------------------------------
String backupJson();
void handleExportar();
bool leSimNao(const String& s, bool& out);
bool leInteiro(const String& s, long& out);
void importaClima(Importacao& imp);
void importaHora(Importacao& imp);
void importaDisplay(Importacao& imp);
void importaEnergia(Importacao& imp);
void importaPainel(Importacao& imp);
void handleImportar();

// ---- REGISTRO DE EVENTOS (/config → Registro) ---------------------------------------------
#if LOG_WEB
void logEvento(const String& msg);
String logHora(int32_t quando);
char logNivel(const char* txt);
void paginaLogJson();
void handleLogLimpar();
void cfgLog(const String& r);
void logAjuste(const char* r);
#else
#define logAjuste(x) do {} while (0)
#endif

// ==========================================================================================
//  7. TABELAS
// ==========================================================================================

// ---- Telas do OLED (deslizam da direita para a esquerda) e barra inferior -----------------
// Todas as telas, na ordem em que aparecem. Em /config → Tela escolhe-se quais entram (telasMask, bit k = TELAS[k]).
FrameCallback TELAS[] = {desenhaDataHora, desenhaClimaInterno, desenhaClimaAtual, desenhaClimaDetalhes, desenhaPrevisao, desenhaTelaSolLua, desenhaTelaWiFi};
static const char TELA_NOME_0[] PROGMEM = "Data e hora";
static const char TELA_NOME_1[] PROGMEM = "Clima interno";
static const char TELA_NOME_2[] PROGMEM = "Clima atual";
static const char TELA_NOME_3[] PROGMEM = "Próximas 24 h";
static const char TELA_NOME_4[] PROGMEM = "Próximos dias";
static const char TELA_NOME_5[] PROGMEM = "Sol e Lua";
static const char TELA_NOME_6[] PROGMEM = "WiFi e bateria";
static const char* const TELAS_NOME[] = {TELA_NOME_0, TELA_NOME_1, TELA_NOME_2, TELA_NOME_3, TELA_NOME_4, TELA_NOME_5, TELA_NOME_6};   // textos na flash: use FPSTR(TELAS_NOME[k])
const uint8_t TELAS_TOTAL = 7;
const uint8_t TELA_WIFI = 6;            // índice da tela 'WiFi e bateria' (a que mostra o IP)
FrameCallback framesAtivos[7];          // só as telas marcadas (montado por aplicaTelas())
uint8_t numTelasAtivas = 0;
OverlayCallback overlays[] = { desenhaBarraInferior };
int numberOfOverlays = 1;

// ---- Idiomas do OpenWeatherMap (lista da página /config) ----------------------------------
static const char* const LANG_CODES[] = {"pt_br", "pt", "en", "es", "fr", "de", "it"};
static const char LANG_NOME_0[] PROGMEM = "Português (Brasil)";
static const char LANG_NOME_1[] PROGMEM = "Português (Portugal)";
static const char LANG_NOME_2[] PROGMEM = "English";
static const char LANG_NOME_3[] PROGMEM = "Español";
static const char LANG_NOME_4[] PROGMEM = "Français";
static const char LANG_NOME_5[] PROGMEM = "Deutsch";
static const char LANG_NOME_6[] PROGMEM = "Italiano";
static const char* const LANG_NAMES[] = {LANG_NOME_0, LANG_NOME_1, LANG_NOME_2, LANG_NOME_3, LANG_NOME_4, LANG_NOME_5, LANG_NOME_6};   // textos na flash: use FPSTR(LANG_NAMES[k])
static const uint8_t LANG_COUNT = 7;

// ---- Fusos horários (minutos em relação ao UTC) da página /config -------------------------
static const char FUSO_NOME_0[] PROGMEM = "UTC-12:00";
static const char FUSO_NOME_1[] PROGMEM = "UTC-11:00";
static const char FUSO_NOME_2[] PROGMEM = "UTC-10:00 (Havaí)";
static const char FUSO_NOME_3[] PROGMEM = "UTC-09:00 (Alasca)";
static const char FUSO_NOME_4[] PROGMEM = "UTC-08:00 (Califórnia)";
static const char FUSO_NOME_5[] PROGMEM = "UTC-07:00 (Colorado)";
static const char FUSO_NOME_6[] PROGMEM = "UTC-06:00 (México, Chicago)";
static const char FUSO_NOME_7[] PROGMEM = "UTC-05:00 (Acre, Nova York, Colômbia)";
static const char FUSO_NOME_8[] PROGMEM = "UTC-04:00 (Amazonas, Mato Grosso, Venezuela)";
static const char FUSO_NOME_9[] PROGMEM = "UTC-03:30 (Terra Nova)";
static const char FUSO_NOME_10[] PROGMEM = "UTC-03:00 (Brasília, Argentina)";
static const char FUSO_NOME_11[] PROGMEM = "UTC-02:00 (Fernando de Noronha)";
static const char FUSO_NOME_12[] PROGMEM = "UTC-01:00 (Açores)";
static const char FUSO_NOME_13[] PROGMEM = "UTC+00:00 (Lisboa, Londres)";
static const char FUSO_NOME_14[] PROGMEM = "UTC+01:00 (Paris, Berlim)";
static const char FUSO_NOME_15[] PROGMEM = "UTC+02:00 (Atenas, Cairo)";
static const char FUSO_NOME_16[] PROGMEM = "UTC+03:00 (Moscou, Istambul)";
static const char FUSO_NOME_17[] PROGMEM = "UTC+03:30 (Irã)";
static const char FUSO_NOME_18[] PROGMEM = "UTC+04:00 (Dubai)";
static const char FUSO_NOME_19[] PROGMEM = "UTC+04:30 (Afeganistão)";
static const char FUSO_NOME_20[] PROGMEM = "UTC+05:00 (Paquistão)";
static const char FUSO_NOME_21[] PROGMEM = "UTC+05:30 (Índia)";
static const char FUSO_NOME_22[] PROGMEM = "UTC+05:45 (Nepal)";
static const char FUSO_NOME_23[] PROGMEM = "UTC+06:00 (Bangladesh)";
static const char FUSO_NOME_24[] PROGMEM = "UTC+06:30 (Mianmar)";
static const char FUSO_NOME_25[] PROGMEM = "UTC+07:00 (Bangkok)";
static const char FUSO_NOME_26[] PROGMEM = "UTC+08:00 (Pequim, Singapura)";
static const char FUSO_NOME_27[] PROGMEM = "UTC+09:00 (Tóquio)";
static const char FUSO_NOME_28[] PROGMEM = "UTC+09:30 (Adelaide)";
static const char FUSO_NOME_29[] PROGMEM = "UTC+10:00 (Sydney)";
static const char FUSO_NOME_30[] PROGMEM = "UTC+11:00";
static const char FUSO_NOME_31[] PROGMEM = "UTC+12:00 (Auckland)";
static const char FUSO_NOME_32[] PROGMEM = "UTC+13:00";
static const char FUSO_NOME_33[] PROGMEM = "UTC+14:00";
static const Fuso FUSOS[] = {   // nome na flash: use FPSTR(FUSOS[k].nome)
  {-720, FUSO_NOME_0},
  {-660, FUSO_NOME_1},
  {-600, FUSO_NOME_2},
  {-540, FUSO_NOME_3},
  {-480, FUSO_NOME_4},
  {-420, FUSO_NOME_5},
  {-360, FUSO_NOME_6},
  {-300, FUSO_NOME_7},
  {-240, FUSO_NOME_8},
  {-210, FUSO_NOME_9},
  {-180, FUSO_NOME_10},
  {-120, FUSO_NOME_11},
  {-60, FUSO_NOME_12},
  {0, FUSO_NOME_13},
  {60, FUSO_NOME_14},
  {120, FUSO_NOME_15},
  {180, FUSO_NOME_16},
  {210, FUSO_NOME_17},
  {240, FUSO_NOME_18},
  {270, FUSO_NOME_19},
  {300, FUSO_NOME_20},
  {330, FUSO_NOME_21},
  {345, FUSO_NOME_22},
  {360, FUSO_NOME_23},
  {390, FUSO_NOME_24},
  {420, FUSO_NOME_25},
  {480, FUSO_NOME_26},
  {540, FUSO_NOME_27},
  {570, FUSO_NOME_28},
  {600, FUSO_NOME_29},
  {660, FUSO_NOME_30},
  {720, FUSO_NOME_31},
  {780, FUSO_NOME_32},
  {840, FUSO_NOME_33}
};
static const uint8_t FUSO_COUNT = sizeof(FUSOS) / sizeof(FUSOS[0]);

// ---- Seções da página /config (menu no estilo dos Ajustes do iPhone) ----------------------
// Menu com uma linha por seção (ícone, nome e resumo do estado); cada seção abre em /config?s=<id>.
// O menu separa as seções em grupos: depois da 3ª e da 6ª começa um grupo novo.
struct SecaoCfg { const char* id; const char* nome; const char* icone; const char* cor; };
static const char SEC_ID_0[] PROGMEM = "clima";
static const char SEC_NOME_0[] PROGMEM = "Cidade e clima";
static const char SEC_ICONE_0[] PROGMEM = "⛅";
static const char SEC_COR_0[] PROGMEM = "#4fc3f7";
static const char SEC_ID_1[] PROGMEM = "display";
static const char SEC_NOME_1[] PROGMEM = "Display";
static const char SEC_ICONE_1[] PROGMEM = "🖥️";
static const char SEC_COR_1[] PROGMEM = "#ffb74d";
static const char SEC_ID_2[] PROGMEM = "hora";
static const char SEC_NOME_2[] PROGMEM = "Data e hora";
static const char SEC_ICONE_2[] PROGMEM = "🕒";
static const char SEC_COR_2[] PROGMEM = "#9fa8da";
static const char SEC_ID_3[] PROGMEM = "bateria";
static const char SEC_NOME_3[] PROGMEM = "Bateria e energia";
static const char SEC_ICONE_3[] PROGMEM = "🔋";
static const char SEC_COR_3[] PROGMEM = "#66bb6a";
static const char SEC_ID_4[] PROGMEM = "painel";
static const char SEC_NOME_4[] PROGMEM = "Painel web";
static const char SEC_ICONE_4[] PROGMEM = "📡";
static const char SEC_COR_4[] PROGMEM = "#ce93d8";
static const char SEC_ID_5[] PROGMEM = "wifi";
static const char SEC_NOME_5[] PROGMEM = "WiFi";
static const char SEC_ICONE_5[] PROGMEM = "📶";
static const char SEC_COR_5[] PROGMEM = "#81d4fa";
static const char SEC_ID_6[] PROGMEM = "led";
static const char SEC_NOME_6[] PROGMEM = "LED de status";
static const char SEC_ICONE_6[] PROGMEM = "💡";
static const char SEC_COR_6[] PROGMEM = "#90a4ae";
static const char SEC_ID_7[] PROGMEM = "sistema";
static const char SEC_NOME_7[] PROGMEM = "Sistema";
static const char SEC_ICONE_7[] PROGMEM = "⚙️";
static const char SEC_COR_7[] PROGMEM = "#78909c";
#if LOG_WEB
static const char SEC_ID_8[] PROGMEM = "log";
static const char SEC_NOME_8[] PROGMEM = "Registro";
static const char SEC_ICONE_8[] PROGMEM = "🧾";
static const char SEC_COR_8[] PROGMEM = "#4db6ac";
#endif
static const SecaoCfg SECOES[] = {   // textos na flash: use FPSTR(SECOES[k].campo)
  {SEC_ID_0, SEC_NOME_0, SEC_ICONE_0, SEC_COR_0},
  {SEC_ID_1, SEC_NOME_1, SEC_ICONE_1, SEC_COR_1},
  {SEC_ID_2, SEC_NOME_2, SEC_ICONE_2, SEC_COR_2},
  {SEC_ID_3, SEC_NOME_3, SEC_ICONE_3, SEC_COR_3},
  {SEC_ID_4, SEC_NOME_4, SEC_ICONE_4, SEC_COR_4},
  {SEC_ID_5, SEC_NOME_5, SEC_ICONE_5, SEC_COR_5},
  {SEC_ID_6, SEC_NOME_6, SEC_ICONE_6, SEC_COR_6},
  {SEC_ID_7, SEC_NOME_7, SEC_ICONE_7, SEC_COR_7},
#if LOG_WEB
  {SEC_ID_8, SEC_NOME_8, SEC_ICONE_8, SEC_COR_8},   // último grupo, junto de LED e Sistema
#endif
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
.vol{display:inline-flex;align-items:center;gap:6px;color:#fff;background:#ffffff14;text-decoration:none;font-size:.9rem;font-weight:500;
  padding:7px 15px 7px 11px;border-radius:999px;margin:2px 0 6px}
.vol:hover{background:#ffffff24}
.vol b{color:#81d4fa;font-size:1.15rem;line-height:1;font-weight:600}
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

// ---- /config → Registro: lista de eventos que se atualiza sozinha (dados de /log.json) ------
#if LOG_WEB
static const char LOG_HTML[] PROGMEM = R"rawliteral(
<style>.con{display:grid;grid-template-columns:auto auto 1fr;gap:3px 10px;align-items:baseline;background:#04080e;
border:1px solid #ffffff1f;border-radius:12px;padding:12px 14px;margin:0 0 20px;max-height:62vh;overflow:auto;
font:.78rem/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
.ln{display:contents}.con .hr{color:#6f8396;white-space:nowrap}.con .tx{color:#dfe7ee;word-break:break-word}
.nv{font-weight:700;letter-spacing:.03em}.nv.o{color:#66bb6a}.nv.i{color:#90a4ae}.nv.a{color:#ffb74d}.nv.e{color:#ef5350}
.con .vz{grid-column:1/-1;color:#6f8396}
.bts{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 14px}.bts form{margin:0}
.mini.sec{background:#ffffff1a;color:#fff}.mini.red{background:#ef5350;color:#fff}</style>
<div class="bts"><button class="mini" type="button" id="bp" onclick="pausar()">⏸ Pausar</button>
<button class="mini sec" type="button" onclick="baixar()">⬇ Baixar .txt</button>
<form method="POST" action="/log/limpar" onsubmit="return confirm('Apagar o registro?')"><button class="mini red" type="submit">🗑 Limpar</button></form></div>
)rawliteral";
// Depois da lista (montada pelo ESP em cfgLog): avisos e o script que atualiza a cada 2 s.
// ATENÇÃO: no script, as funções são 'const nome=()=>{...}'. Uma linha que comece com
// 'function nome(){' (sem parâmetros) é lida como C++ pela IDE do Arduino, que gera um protótipo
// e o enfia, com uma linha '#line', DENTRO deste texto: o script quebra no navegador.
static const char LOG_HTML_FIM[] PROGMEM = R"rawliteral(
<div class="note" id="lgst">Atualiza sozinho a cada 2 s.</div>
<div class="note">Guarda só os eventos (ligar, WiFi, clima, envio ao painel, energia, bateria, sensor, login e ajustes), os mais novos em cima. Fica na memória: ao reiniciar, começa vazio. O monitor serial pela USB continua com todas as mensagens.</div>
<script>
let pausa=false,dados=[];const L=document.getElementById('lg'),E=document.getElementById('lgst');
const esc=s=>s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const N={O:['o','OK'],I:['i','INFO'],A:['a','AVISO'],E:['e','ERRO']};
const nv=x=>N[x[1]]||N.I;
const linha=x=>'<div class="ln"><span class="hr">'+esc(x[0])+'</span><span class="nv '+nv(x)[0]+'">'+nv(x)[1]+'</span><span class="tx">'+esc(x[2])+'</span></div>';
const mostra=()=>{L.innerHTML=dados.length?dados.map(linha).join(''):'<div class="ln"><span class="vz">Nenhum evento ainda.</span></div>'};
let ocupado=false;
const busca=async()=>{if(pausa||ocupado)return;ocupado=true;const ac=new AbortController(),to=setTimeout(()=>ac.abort(),6000);
try{const r=await fetch('/log.json',{cache:'no-store',signal:ac.signal});if(!r.ok)throw new Error('HTTP '+r.status);
const tx=await r.text();let j;try{j=JSON.parse(tx)}catch(e){throw new Error(tx.indexOf('<')>=0?'login expirou: recarregue a página':'resposta inválida ('+tx.length+' bytes)')}
dados=j.l||[];mostra();E.textContent=j.total+(j.total==1?' evento':' eventos')+' desde que ligou · mostrando '+dados.length+' de até '+j.max}
catch(e){E.textContent='⚠️ Não consegui ler o registro: '+(e.name=='AbortError'?'o ESP não respondeu em 6 s':e.message)}
finally{clearTimeout(to);ocupado=false}};
const pausar=()=>{pausa=!pausa;document.getElementById('bp').textContent=pausa?'▶ Continuar':'⏸ Pausar';if(!pausa)busca()};
const baixar=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([dados.map(x=>x[0].padEnd(15)+nv(x)[1].padEnd(6)+x[2]).join('\r\n')+'\r\n'],{type:'text/plain;charset=utf-8'}));a.download='registro-estacao.txt';a.click()};
busca();setInterval(busca,2000);
</script>
)rawliteral";
#endif

// ---- /config → Sistema: barras de progresso (texto no meio, invertido sobre a parte preenchida) --
// Cor pelo nível: .ok verde, .at amarelo, .ru vermelho. O preenchimento vem em style="--p:NN%".
static const char SIS_CSS[] PROGMEM = R"rawliteral(<style>
.pb{position:relative;flex:none;width:58%;max-width:230px;height:22px;border-radius:5px;overflow:hidden;
background:#ffffff14;border:1px solid #ffffff2e;font-size:.8rem;font-weight:600;font-variant-numeric:tabular-nums}
.pb .t,.pb .f{position:absolute;inset:0;display:grid;place-items:center;white-space:nowrap}
.pb .t{color:#dfe7ee}.pb .f{clip-path:inset(0 calc(100% - var(--p)) 0 0)}
.pb.ok .f{background:linear-gradient(#5cc46a,#3d9a4b);color:#fff}
.pb.at .f{background:linear-gradient(#ffc061,#e89b2c);color:#1c1300}
.pb.ru .f{background:linear-gradient(#f2645f,#c93a37);color:#fff}
.copiado{background:#66bb6a!important;color:#fff!important}
.bts{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:-8px 0 20px}.mini.sec{background:#ffffff1a;color:#fff}</style>)rawliteral";

// Botão 'Copiar diagnóstico': junta o texto de cada linha de #diag, por grupo. Em http o navegador
// não libera navigator.clipboard, então usa textarea + execCommand. Funções como 'const x=()=>{}'
// (uma linha começando com 'function x(){' vira protótipo C++ inserido aqui pela IDE).
static const char SIS_JS[] PROGMEM = R"rawliteral(
<button class="btn sec" id="bc" type="button" onclick="copiar()">📋 Copiar diagnóstico</button>
<div class="note" style="margin-top:-8px">Copia os valores acima como texto, para colar numa mensagem quando algo der errado.</div>
<script>
const copiar=()=>{const L=['Estação Meteorológica · diagnóstico'];
document.querySelectorAll('#diag > .cap, #diag > .grp').forEach(e=>{
if(e.classList.contains('cap')){L.push('','['+e.textContent+']');return}
e.querySelectorAll('.it').forEach(it=>{const tx=it.querySelector('.tx');const k=tx.firstChild.textContent.trim();
const sb=tx.querySelector('small');const vv=it.querySelector('.val')||it.querySelector('.pb .t');
const v=vv?vv.innerText.replace(/\s*\n\s*/g,', ').replace(' ↗',''):'';L.push(k+': '+v+(sb?' ('+sb.textContent+')':''))})});
const s=L.join('\n'),b=document.getElementById('bc');
const ok=()=>{b.textContent='✅ Copiado';b.classList.add('copiado');setTimeout(()=>{b.textContent='📋 Copiar diagnóstico';b.classList.remove('copiado')},2000)};
if(navigator.clipboard&&window.isSecureContext){navigator.clipboard.writeText(s).then(ok);return}
const a=document.createElement('textarea');a.value=s;a.style.position='fixed';a.style.opacity='0';document.body.appendChild(a);
a.select();try{document.execCommand('copy');ok()}catch(e){prompt('Copie o texto:',s)}a.remove()};
let pausa=false,ocupado=false;const S=document.getElementById('sst');
const busca=async()=>{if(pausa||ocupado||document.hidden)return;ocupado=true;const ac=new AbortController(),to=setTimeout(()=>ac.abort(),4000);
try{const r=await fetch('/sistema.json',{cache:'no-store',signal:ac.signal});if(!r.ok)throw new Error('HTTP '+r.status);
const tx=await r.text();let j;try{j=JSON.parse(tx)}catch(e){throw new Error(tx.indexOf('<')>=0?'login expirou: recarregue a página':'resposta inválida')}
for(const id in j.l){const el=document.getElementById(id);if(el)el.outerHTML=j.l[id]}
S.textContent=j.h?'Atualizado às '+j.h:'Atualizado agora'}
catch(e){S.textContent='⚠️ Sem atualizar: '+(e.name=='AbortError'?'o ESP não respondeu':e.message)}
finally{clearTimeout(to);ocupado=false}};
const pausar=()=>{pausa=!pausa;document.getElementById('bp').textContent=pausa?'▶ Continuar':'⏸ Pausar';
document.getElementById('bp').className=pausa?'mini sec':'mini';if(!pausa)busca()};
setInterval(busca,5000);
</script>
)rawliteral";

// ---- Página /sobre (texto fixo na flash; a parte "Este aparelho" é montada em paginaSobre) -----
// Resumo do projeto e links. O manual completo é o README no GitHub (sempre atualizado).
static const char SOBRE_HTML[] PROGMEM = R"rawliteral(
<div class="grp"><div class="fld" style="line-height:1.5">
Estação meteorológica doméstica com <b>ESP8266</b>: display OLED, sensor de temperatura e umidade
interna, clima e previsão do OpenWeatherMap, página web própria, configuração pelo navegador,
bateria com placa UPS e envio das leituras para um painel web.
</div></div>
<div class="cap">Recursos</div>
<div class="grp"><div class="fld" style="line-height:1.6">
⛅ Clima agora, próximas 24 h e próximos dias<br>
🌙 Sol e Lua calculados no próprio ESP<br>
🌡️ Sensor interno com histórico e gráfico<br>
🖥️ Telas do OLED escolhidas, brilho e agenda noturna<br>
🔋 Bateria, energia externa e economia de energia<br>
📡 Envio das leituras para o painel web<br>
🔒 /config com senha, OTA e backup das configurações
</div></div>
<div class="cap">Links</div>
<div class="grp">
<a class="it" href="https://github.com/LuismarPavani/Estacao-Meteorologica#readme" target="_blank" rel="noopener"><span class="ic" style="background:#4fc3f7">📖</span><span class="tx">Manual completo<small>README do firmware no GitHub</small></span><span class="chev">›</span></a>
<a class="it" href="https://github.com/LuismarPavani/Estacao-Meteorologica" target="_blank" rel="noopener"><span class="ic" style="background:#78909c">💾</span><span class="tx">Código do firmware<small>github.com/LuismarPavani/Estacao-Meteorologica</small></span><span class="chev">›</span></a>
<a class="it" href="https://weatherstation-web.vercel.app/" target="_blank" rel="noopener"><span class="ic" style="background:#ce93d8">📈</span><span class="tx">Painel web<small>weatherstation-web.vercel.app</small></span><span class="chev">›</span></a>
<a class="it" href="https://github.com/LuismarPavani/Estacao-Meteorologica-web" target="_blank" rel="noopener"><span class="ic" style="background:#9fa8da">🧩</span><span class="tx">Código do painel<small>github.com/LuismarPavani/Estacao-Meteorologica-web</small></span><span class="chev">›</span></a>
<a class="it" href="https://github.com/LuismarPavani" target="_blank" rel="noopener"><span class="ic" style="background:#66bb6a">👤</span><span class="tx">Luismar Pavani<small>perfil no GitHub</small></span><span class="chev">›</span></a>
</div>
<div class="cap">Créditos</div>
<div class="grp"><div class="fld" style="line-height:1.5;color:#a9c1cf;font-size:.9rem">
Baseado no exemplo <b>WeatherStation</b> da ThingPulse (Daniel Eichhorn), bastante ampliado.
Bibliotecas: ThingPulse ESP8266 Weather Station e OLED SSD1306, WiFiManager, Adafruit DHT,
JSON Streaming Parser e o core ESP8266 para Arduino. Licença MIT.
</div></div>
<div class="note" style="margin-top:-8px">Os links abrem na internet: o celular ou computador precisa ter acesso a ela.</div>
)rawliteral";

// ---- Página principal (HTML, CSS e JavaScript guardados na flash; os números chegam por fetch de /json e /historico) 
static const char PAGE_INDEX[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Estação Meteorológica</title>
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

<footer class="mut"><div id="foot">Carregando...</div><div style="margin-top:6px"><a href="/sobre">Sobre a estação</a></div></footer>
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
//  8. FUNÇÕES PRINCIPAIS (as outras estão nas abas: veja o mapa no início do arquivo)
// ==========================================================================================

// ---- PRINCIPAL ----------------------------------------------------------------------------

/**
 * Inicialização (roda uma vez ao ligar), em etapas: configuração salva, pinos e sensor, display e
 * abertura, WiFi (ou portal), hora (NTP), OTA, interface do OLED, primeiras leituras e servidor web.
 */
void setup() {
  heapBoot = ESP.getFreeHeap();         // antes de WiFi, servidor e clima ocuparem memória
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  EEPROM.begin(sizeof(Config));
  carregaConfig();
  logEvento(F("Ligado (v" FIRMWARE_VERSAO "): ") + ESP.getResetReason());
  displayLigado = true;                 // o boot sempre aparece (abertura, WiFi e IP): mostra que deu tudo certo.
                                        // A escolha salva (displayLigadoSalvo) vale no fim do setup().

  iniciaPinosESensor();                 // LED, botões, sensor de energia e DHT
  iniciaDisplayBoot();                  // display, varredura I2C e tela de abertura

  String hostname(HOSTNAME);            // nome na rede e na porta OTA: prefixo + ID do chip
  hostname += String(ESP.getChipId(), HEX);
  conectaWiFiBoot(hostname);            // rede salva (ou portal do WiFiManager, sem rede salva)
  aplicaFuso();                         // hora pela internet (sincroniza sozinha quando houver rede)
  configuraOta(hostname);
  mostraResultadoWiFiBoot();            // tela 'WiFi conectado' com o IP, ou 'Sem WiFi'
  iniciaInterfaceOled();                // clientes do OpenWeatherMap e telas que deslizam

  Serial.println(F(""));
  leSensor();
  if (bateriaAtiva) atualizaBateria();
  atualizaClima(&display);
  iniciaServidorWeb();

  if (!displayLigadoSalvo) {            // tela apagada pela página: apaga agora, depois de mostrar o boot
    displayLigado = false;
    telaContando = false;
    aplicaDisplay();
  }
}

/** Boot: LED de status, botões, sensor de energia da placa UPS e sensor DHT. */
void iniciaPinosESensor() {
  ledInicia();                          // LED de status (apagado até algo acontecer)
  if (PIN_BOTAO_TELA >= 0) pinMode(PIN_BOTAO_TELA, INPUT_PULLUP);
  if (PIN_BOTAO_RESET >= 0) pinMode(PIN_BOTAO_RESET, INPUT_PULLUP);
  if (PIN_ENERGIA >= 0) {               // sensor de energia da UPS (divisor de tensão no pino)
    pinMode(PIN_ENERGIA, (PIN_ENERGIA == 16) ? INPUT_PULLDOWN_16 : INPUT);
    if (energiaAtiva) energiaLeAgora();
  }
  delay(50);
  dht.begin();
}

/** Boot: inicia o display, lista o barramento I2C no Serial e mostra a abertura e 'Conectando ao WiFi'. */
void iniciaDisplayBoot() {
  display.init();
  display.clear();
  display.display();
  aplicaDisplay();                      // respeita o estado salvo já na inicialização

  varreduraI2C();                       // lista no Serial o que respondeu no barramento (0x3C = display)

  desenhaAbertura();                    // logo, nome e versão
  if (displayLigado) delay(ABERTURA_MS);   // com a tela apagada não atrasa o boot

  //display.flipScreenVertically();
  desenhaConectando(0);                 // 'Conectando ao WiFi' com o ícone e os 3 pontos animados
}

/**
 * Boot: conecta ao WiFi. Com rede salva, tenta por WIFI_BOOT_TIMEOUT_MS e, se não der, segue SEM WiFi
 * (sensor, display, botões e histórico continuam; o ESP reconecta sozinho quando o roteador voltar).
 * Assim uma queda de energia que derrube o roteador não deixa a estação reiniciando em loop.
 * Sem rede salva (1ª vez ou depois de apagar o WiFi): abre o portal; se ninguém configurar em 3 min,
 * reinicia. As credenciais salvas NUNCA são apagadas automaticamente.
 */
void conectaWiFiBoot(const String& hostname) {
  WiFi.hostname(hostname);

  // Descomente para testar o portal do WiFiManager (apaga a rede salva a cada boot)
  //wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  std::vector<const char *> menu = {"wifi","wifinoscan","info","param","close","sep","erase","restart","exit"};
  wifiManager.setMenu(menu);
  wifiManager.setClass("invert");

  wifiManager.setConnectTimeout(30);
  wifiManager.setConfigPortalTimeout(180);
  ledRede = LED_CONECTANDO;             // pisca rápido enquanto conecta (e com o portal aberto)
  if (WiFi.SSID().length() > 0) {       // há uma rede salva na flash
    Serial.println(F("Conectando a ") + WiFi.SSID() + F("..."));
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
}

/**
 * Boot: prepara o OTA (enviar firmware pelo WiFi: Arduino IDE > Porta de rede). Ele só inicia com
 * rede: iniciaOta(), chamada aqui ou pelo loop quando o WiFi conectar depois do boot.
 */
void configuraOta(const String& hostname) {
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(OTA_SENHA);   // senha definida em EstacaoConfig.h
  ArduinoOTA.onStart([]() {
    ecoAcorda(10UL * 60UL * 1000UL);    // gravação pelo WiFi sem light sleep (economia na bateria)
    logEvento(F("OTA: gravando firmware novo"));
  });
  ArduinoOTA.onError([](ota_error_t erro) { (void)erro; logEvento(F("OTA: falhou (erro ") + String((int)erro) + F(")")); });
}

/** Boot: com WiFi, inicia o OTA e mostra a tela 'WiFi conectado'; sem WiFi, avisa que segue sem rede. */
void mostraResultadoWiFiBoot() {
  if (WiFi.status() == WL_CONNECTED) {
    ledRede = LED_NORMAL;               // conectou
    ledPulsoOk();
    logEvento(F("WiFi: ") + WiFi.SSID() + F(" · ") + WiFi.localIP().toString());
    iniciaOta();
    if (displayLigado) {                // tela 'WiFi conectado': rede, sinal e IP (útil se a tela do WiFi estiver desmarcada)
      desenhaTelaIp();
      delay(IP_TELA_MS);
    }
    ipMostrado = true;
  } else {
    ledRede = LED_SEM_REDE;             // segue sem WiFi: piscada longa a cada 2 s até conectar
    Serial.println(F("Sem WiFi: seguindo sem rede; reconecta quando o roteador voltar"));
    logEvento(F("WiFi: sem rede no boot"));
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 8, F("Sem WiFi"));
    display.drawString(64, 22, F("Seguindo sem rede"));
    display.drawString(64, 36, F("(reconecta sozinho)"));
    display.display();
    delay(2000);
  }
}

/** Boot: clientes do OpenWeatherMap (unidade e idioma) e a interface de telas que deslizam no OLED. */
void iniciaInterfaceOled() {
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

  // A ui.init() também inicia o display.
  ui.init();
  brilhoAplicado = 0xFF;                // ui.init() reinicia o display e volta o contraste ao padrão: reenvia o brilho
  aplicaDisplay();                      // ui.init() religa a tela; reaplica o estado salvo (e o brilho)
  if (displayLigado) armaTimerTela(telaDuracaoPadraoMs());   // tempo de tela ligada (0 = sempre ligada)
}

/** Boot: registra as rotas do servidor web (página, /config, ações e backup) e o liga. */
void iniciaServidorWeb() {
  server.on(F("/"), HTTP_GET, paginaInicial);
  server.on(F("/json"), HTTP_GET, paginaJson);
  server.on(F("/historico"), HTTP_GET, paginaHistorico);
  server.on(F("/sobre"), HTTP_GET, paginaSobre);
  server.on(F("/sistema.json"), HTTP_GET, paginaSistemaJson);
#if LOG_WEB
  server.on(F("/log.json"), HTTP_GET, paginaLogJson);
  server.on(F("/log/limpar"), HTTP_POST, handleLogLimpar);
#endif
  server.on(F("/config"), HTTP_GET, paginaConfig);
  server.on(F("/salvar"), HTTP_POST, handleSalvar);
  server.on(F("/display"), HTTP_POST, handleDisplay);
  server.on(F("/agenda"), HTTP_POST, handleAgenda);
  server.on(F("/tempotela"), HTTP_POST, handleTempoTela);
  server.on(F("/tempoligada"), HTTP_POST, handleTempoLigada);
  server.on(F("/bateria"), HTTP_POST, handleBateria);
  server.on(F("/economia"), HTTP_POST, handleEconomia);
  server.on(F("/led"), HTTP_POST, handleLed);
  server.on(F("/brilho"), HTTP_POST, handleBrilho);
  server.on(F("/telas"), HTTP_POST, handleTelas);
  server.on(F("/hora"), HTTP_POST, handleHora);
  server.on(F("/buscar-cidade"), HTTP_GET, handleBuscaCidade);
  server.on(F("/api"), HTTP_POST, handleApi);
  server.on(F("/enviar"), HTTP_POST, handleEnviar);
  server.on(F("/reset-wifi"), HTTP_POST, handleResetWiFi);
  server.on(F("/reset-geral"), HTTP_POST, handleResetGeral);
  server.on(F("/reiniciar"), HTTP_POST, handleReiniciar);
  server.on(F("/portal"), HTTP_POST, handlePortal);
  server.on(F("/exportar"), HTTP_GET, handleExportar);
  server.on(F("/importar"), HTTP_POST, handleImportar);
  server.on(F("/login"), HTTP_GET, paginaLogin);
  server.on(F("/login"), HTTP_POST, handleLogin);
  server.on(F("/sair"), HTTP_POST, handleSair);
  server.on(F("/sair-todos"), HTTP_POST, handleSairTodos);
  // Origin/Referer: requisicaoSegura() (proteção contra CSRF); Cookie: logado() (login da /config)
  server.collectHeaders("Origin", "Referer", "Cookie");   // forma do core 3.x (lista de nomes)
  // Economia na bateria: qualquer acesso à página acorda o ESP e o mantém acordado por ECO_ACORDADO_MS
  server.addHook([](const String&, const String&, WiFiClient*, ESP8266WebServer::ContentTypeFunction) {
    ecoAcorda(ECO_ACORDADO_MS);
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });
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
  atualizaEco(ms);                         // na bateria: light sleep do WiFi quando ninguém está usando

  // Estado da rede: pisca devagar se o WiFi cair (como em interruptores inteligentes)
  if ((long)(ms - nextRede) >= 0) {
    nextRede = ms + 1000UL;
    ledSilencio = ledSegueAgenda && agendaEstadoAtual() == 0;   // à noite (tela desligada pela agenda) o LED fica apagado
    bool conectado = (WiFi.status() == WL_CONNECTED);
    if (!conectado && ledRede != LED_SEM_REDE) {
      ledRede = LED_SEM_REDE;
      Serial.println(F("WiFi caiu"));
      logEvento(F("WiFi caiu"));
    } else if (conectado && ledRede == LED_SEM_REDE) {
      ledRede = LED_NORMAL;
      ledPulsoOk();
      Serial.println(F("WiFi voltou"));
      logEvento(F("WiFi voltou: ") + WiFi.SSID() + F(", ") + String(WiFi.RSSI()) + F(" dBm"));
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
      nextApi = ms + (ok ? intervaloEnvioMs() : PAINEL_RETRY_MS);
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
    atualizaClima(&display);
  }

  if (displayLigado && !segurandoReset && !mostrandoMsg) {
    int remainingTimeBudget = ui.update();

    if (remainingTimeBudget > 0) {
      delay(remainingTimeBudget);
    }
  } else {
    delay(ecoDormindo ? ECO_LOOP_MS : 50);   // tela apagada: não gasta CPU nem I2C desenhando (dormindo: pausa maior, mais economia)
  }
}
