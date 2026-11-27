/*
 * EstacaoConfig.example.h: MODELO do arquivo de senhas e configurações.
 *
 * COMO USAR:
 *   1. Copie este arquivo para EstacaoConfig.h, na mesma pasta.
 *   2. No EstacaoConfig.h, troque as senhas (OTA_SENHA, PORTAL_SENHA e CONFIG_SENHA) e, se precisar,
 *      escolha a placa na seção 2.
 *   3. Compile e grave. O EstacaoMeteorologica.ino inclui só o EstacaoConfig.h; este modelo
 *      não é usado na compilação.
 *
 * Este modelo pode ser publicado: não tem senhas reais. O EstacaoConfig.h, sim, tem.
 * Não o publique (se usar Git, coloque-o no .gitignore).
 *
 * Ao mudar o EstacaoConfig.h (novas constantes), atualize também este modelo, com
 * valores de exemplo no lugar das senhas.
 *
 * MAPA DO ARQUIVO:
 *   SENHAS E CHAVES                      <- OTA, portal do WiFi, OpenWeatherMap e painel
 *   2. Seleção da placa e pinagem        <- o que muda ao trocar de ESP (display, sensor e botões)
 *   3. Configurações do projeto          <- constantes ajustáveis (tempos, bateria, API, clima...)
 * (os números 2 e 3 seguem o mapa do EstacaoMeteorologica.ino)
 */

#pragma once

// ==========================================================================================
//  SENHAS E CHAVES
//  Troque os valores antes de gravar. Os itens terminados em _PADRAO são só o padrão de fábrica:
//  podem ser trocados em /config, e o que for salvo lá vale mais que o daqui.
// ==========================================================================================

// ---- OTA: senha pedida pela Arduino IDE ao enviar firmware pela rede ----------------------
#define OTA_SENHA          "troque-esta-senha"

// ---- Portal de configuração do WiFi (rede que o ESP cria quando não há WiFi salvo) --------
// A senha protege a rede do portal (WPA2) e precisa ter de 8 a 63 caracteres.
#define PORTAL_SSID        "EstacaoMeteorologica"
#define PORTAL_SENHA       "troque-esta-senha"

// ---- Senha da página de configurações (/config) -------------------------------------------
// Pedida uma vez em cada navegador; o login fica lembrado por LOGIN_DIAS dias (mesmo fechando o
// navegador ou reiniciando o ESP). Trocar a senha desconecta todos os navegadores.
// "" = sem senha (a /config fica aberta para qualquer um na rede; não recomendado).
#define CONFIG_SENHA       "troque-esta-senha"
#define LOGIN_DIAS         30

// ---- OpenWeatherMap: chave da API ("" = informar em /config → Clima) ----------------------
#define OWM_APP_ID_PADRAO  ""

// ---- Painel web (API REST): DEVICE_KEY ("" = informar em /config → Clima → Painel web) ----
#define DEVICE_KEY_PADRAO  ""

// Impede gravar com uma senha de portal que o WiFi recusaria (o portal abriria sem senha)
static_assert(sizeof(PORTAL_SENHA) - 1 >= 8 && sizeof(PORTAL_SENHA) - 1 <= 63,
              "PORTAL_SENHA precisa ter de 8 a 63 caracteres (WPA2).");

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
//    Sensor de energia (UPS) D0  (GPIO16)                D0  (GPIO16)   <- 5 V da entrada da UPS, por divisor 10k/20k
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
  #define SENSOR_TIPO      22           // 22 = DHT22 (AM2302); use 11 se for o DHT11 (mais simples); também aceita 12 ou 21
  #define PIN_BOTAO_TELA   D7           // GPIO13: clique = próxima tela; segurar = apagar o WiFi
  #define PIN_BOTAO_RESET  D6           // GPIO12: botão de reset geral (-1 desativa)
  #define PIN_ENERGIA      D0           // GPIO16: sensor de energia da placa UPS (-1 desativa); com o D0 aqui, o 2º LED do D0 não é usado
#elif defined(PLACA_OLED_INTEGRADO)
  // ---- NodeMCU com OLED integrado (SSD1315, compatível com SSD1306) ----
  #define DISPLAY_SDA      D5           // GPIO14
  #define DISPLAY_SCL      D6           // GPIO12 (confirmado na prática; se a tela ficar em branco, troque SDA e SCL)
  #define DHTPIN           4            // GPIO4 = D2
  #define SENSOR_TIPO      22           // troque para 11 (DHT11) se esse for o sensor ligado
  #define PIN_BOTAO_TELA   D7           // GPIO13
  #define PIN_BOTAO_RESET  D1           // GPIO5 (o D6 é o SCL do display)
  #define PIN_ENERGIA      D0           // GPIO16: sensor de energia da placa UPS (-1 desativa); com o D0 aqui, o 2º LED do D0 não é usado
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
#define FIRMWARE_VERSAO "3.11.1"           // enviado ao painel no campo "firmware"
#define HOSTNAME "EstacaoMeteorologica-"   // nome na rede e na porta OTA: este prefixo + ID do chip (ex.: EstacaoMeteorologica-a1b2c3). Só letras, números e hífen; até 26 caracteres

// ---- Hora da internet (NTP): até 3 servidores, usados na ordem se um não responder --------
#define NTP_SERVIDOR_1  "a.st1.ntp.br"      // NTP.br (servidor oficial brasileiro)
#define NTP_SERVIDOR_2  "pool.ntp.org"
#define NTP_SERVIDOR_3  "time.google.com"

// ---- WiFi no boot -------------------------------------------------------------------------
// Com rede salva, espera a conexão por até este tempo; depois segue sem WiFi e reconecta sozinho.
const unsigned long WIFI_BOOT_TIMEOUT_MS = 30UL * 1000UL;

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
const unsigned long ABERTURA_MS = 2500;          // quanto tempo a tela de abertura (logo, nome e versão) fica no boot
const unsigned long TELA_MENSAGEM_MS = 3500;     // quanto tempo uma mensagem (ex.: resultado do envio ao painel) fica no OLED
// Brilho do OLED (5 a 100%, de 5 em 5). São só os padrões de fábrica: mudam em /config → Tela.
// O brilho noturno vale no horário de tela desligada do horário automático (agenda), inclusive quando a tela
// é acesa pelo botão nesse horário. NOITE_DIMINUI_PADRAO: false = à noite a tela apaga; true = só diminui o brilho.
#define BRILHO_PADRAO          100
#define BRILHO_NOITE_PADRAO    20
#define NOITE_DIMINUI_PADRAO   false
// NOITE_PELO_SOL_PADRAO: true = o brilho noturno vale do pôr ao nascer do sol (calculado com as coordenadas da
// cidade); false = vale no horário de tela desligada do horário automático (agenda).
#define NOITE_PELO_SOL_PADRAO  false
// Telas do OLED que aparecem (padrão de fábrica; muda em /config → Tela). Some os números das telas:
//   1 = Data e hora, 2 = Clima interno, 4 = Clima atual, 8 = Próximas 24 h, 16 = Próximos dias,
//   32 = Sol e Lua, 64 = WiFi e bateria.   127 = todas.
#define TELAS_PADRAO           127
const unsigned long IP_TELA_MS = 4000;           // tela "WiFi conectado" (rede, sinal e IP) logo depois de conectar
// LED azul (GPIO2 e GPIO16): pisca rápido = conectando ao WiFi, enviando ou aviso de reset; uma piscada longa a cada 2 s = sem WiFi;
// 1 acendida = sucesso; 3 piscadas curtas = falha. false = LED apagado. Este é só o padrão de fábrica: liga/desliga em /config → Tela.
#define LED_STATUS_ATIVO true

// ---- Clima (OpenWeatherMap): padrões; a chave, a cidade e o idioma são configurados em /config (nunca publique a chave) 
String OPEN_WEATHER_MAP_APP_ID = OWM_APP_ID_PADRAO;   // chave padrão: veja SENHAS E CHAVES no topo
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
#define DST_MN          0        // 60 = horário de verão (em minutos), usado em alguns países
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

// ---- Bateria (A0): divisor de tensão, calibração com multímetro e limites de aviso --------
#define BATERIA_ATIVA_PADRAO false
const float BAT_FATOR = 4.2f;           // (R1 + R2) / R2 do divisor
const float BAT_CALIB = 0.99947619f;    // medido com multímetro (tensão real ÷ tensão lida)
const int   BAT_AMOSTRAS = 16;
#define BAT_AVISO_PCT   20              // abaixo disso: aviso de bateria baixa
#define BAT_CRITICA_PCT 10              // abaixo disso: aviso crítico

// ---- Energia externa (placa UPS): o pino lê, por um divisor de tensão, se há 5 V na ENTRADA da placa 
// Ligação: entrada de 5 V da UPS (IN+, do carregador/USB) -> resistor 10 kΩ -> PIN_ENERGIA, e PIN_ENERGIA -> resistor 20 kΩ -> GND.
// NUNCA ligue os 5 V direto no pino (o ESP8266 aceita no máximo 3,3 V) e não use a SAÍDA da UPS: ela tem 5 V também na bateria.
// Ativar/desativar por ESP: aba Bateria em /config. ENERGIA_ATIVA_EM_ALTO: true = com 5 V o pino lê HIGH; false = lê LOW.
#define ENERGIA_ATIVA_PADRAO    false   // padrão de fábrica (muda em /config)
#define ENERGIA_ATIVA_EM_ALTO   true
const unsigned long ENERGIA_INTERVAL_MS = 200;   // leitura do pino (muda de estado após 3 leituras iguais)

// ---- Painel web (API REST): URL e DEVICE_KEY também são configuradas em /config -----------
#define API_URL_PADRAO ""               // sem URL o envio ao painel fica desligado; informe em /config → Clima → Painel web
const unsigned long PAINEL_INTERVAL_MS = 15UL * 60UL * 1000UL;   // envio automático ao painel a cada 15 min
const unsigned long PAINEL_RETRY_MS    = 2UL * 60UL * 1000UL;    // se o envio falhar, tenta de novo em 2 min
// Economia na bateria (precisa da placa UPS ativa em /config → Bateria): sem energia externa o WiFi
// entra em light sleep (~1-3 mA no lugar de ~15-20 mA) e o envio ao painel fica mais espaçado.
// Qualquer acesso à página ou clique num botão mantém o ESP acordado por ECO_ACORDADO_MS.
#define ECO_BATERIA_PADRAO      false   // padrão de fábrica (muda em /config → Bateria e energia)
#define ECO_INTERVALO_PADRAO    30      // envio ao painel na bateria, em minutos (15, 20, 30, 45 ou 60)
const unsigned long ECO_ACORDADO_MS = 2UL * 60UL * 1000UL;   // acordado depois de um acesso ou clique
const unsigned long ECO_LOOP_MS     = 100;   // pausa do loop com a tela apagada e o ESP dormindo
// Memória mínima para tentar o envio por HTTPS (abaixo disso o envio é adiado, sem travar o ESP)
#define HTTPS_MIN_LIVRE 12500           // bytes livres no total
#define HTTPS_MIN_BLOCO 7000            // maior bloco contínuo livre
#define API_TLS_RX_BUF 1024             // buffer de recepção do HTTPS (mesmo valor do WeatherApi.h; aumente se a conexão falhar)

// ---- Histórico interno (144 pontos x 5 min = 12 h, guardado em anel na RAM) ---------------
const uint8_t HIST_SIZE = 144;
const unsigned long HIST_INTERVAL_MS = 5UL * 60UL * 1000UL;

// ---- Registro de eventos (/config → Registro): últimos eventos na RAM (somem ao reiniciar) --
#define LOG_WEB   1                     // 1 = liga o registro; 0 = desliga (não ocupa RAM nem flash)
#define LOG_BYTES 1024                  // RAM do registro: 1024 = ~21 eventos; 2048 = ~42 (cada KB a mais sai do heap livre)
