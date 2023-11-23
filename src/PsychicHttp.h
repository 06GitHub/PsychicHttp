#ifndef PsychicHttp_h
#define PsychicHttp_h

#define CORE_DEBUG_LEVEL ARDUHAL_LOG_LEVEL_WARN
#define PH_TAG "http"

#define MAX_COOKIE_SIZE 256
#define FILE_CHUNK_SIZE 8*1024
#define MAX_UPLOAD_SIZE   (200*1024) // 200 KB
#define MAX_UPLOAD_SIZE_STR "200KB"

//#define ENABLE_KEEPALIVE
#define ENABLE_SERVE_STATIC

#include <ArduinoTrace.h>
#include <esp_https_server.h>
#include <http_status.h>
#include <map>
#include <list>
#include <libb64/cencode.h>
#include "esp_random.h"
#include "MD5Builder.h"
#include <UrlEncode.h>
#include "FS.h"

#ifdef ENABLE_KEEPALIVE
  #include <keep_alive.h>
#endif

typedef std::map<String, String> SessionData;

struct HTTPHeader {
  char * field;
  char * value;
};

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
    char *data;
};

enum HTTPAuthMethod { BASIC_AUTH, DIGEST_AUTH };

class PsychicHttpServer;
class PsychicHttpServerRequest;
class PsychicHttpServerResponse;
class PsychicHttpWebSocketRequest;
class PsychicHttpWebSocketConnection;
class PsychicStaticFileHandler;

class PsychicHttpServerRequest {
  friend PsychicHttpServer;

  protected:
    PsychicHttpServer *_server;
    http_method _method;
    String _body;
    SessionData *_session;

    void loadBody();

  public:
    PsychicHttpServerRequest(PsychicHttpServer *server, httpd_req_t *req);
    virtual ~PsychicHttpServerRequest();

    httpd_req_t *_req;

    virtual bool isUpload() { return false; }
    virtual bool isWebSocket() { return false; }

    String headers(const char *name);
    String header(const char *name);
    bool hasHeader(const char *name);

    static void freeSession(void *ctx);
    bool hasSessionKey(String key);
    String getSessionKey(String key);
    void setSessionKey(String key, String value);

    bool hasCookie(const char * key);
    String getCookie(const char * key);

    http_method method();
    String methodStr();
    String uri();
    String host();
    String contentType();
    size_t contentLength();
    String body();

    String queryString();
    bool hasParam(const char *key);
    esp_err_t getParam(const char *name, char *value);
    String getParam(const char *name);

    String _extractParam(String& authReq, const String& param, const char delimit);
    String _getRandomHexString();
    bool authenticate(const char * username, const char * password);
    esp_err_t requestAuthentication(HTTPAuthMethod mode, const char* realm, const String& authFailMsg);

    esp_err_t redirect(const char *url);
    esp_err_t reply(int code);
    esp_err_t reply(const char *content);
    esp_err_t reply(int code, const char *content);
    esp_err_t reply(int code, const char *contentType, const char *content="");
};

class PsychicHttpServerResponse
{
  protected:
    PsychicHttpServerRequest *_request;
    int64_t _contentLength;
    char _status[60];
    const char * body;

    std::list<HTTPHeader> headers;

  public:
    PsychicHttpServerResponse(PsychicHttpServerRequest *request);
    virtual ~PsychicHttpServerResponse();

    void setCode(int code);

    void setContentType(const char *contentType);
    void setContentLength(int64_t contentLength) { _contentLength = contentLength; }
    int64_t getContentLength(int64_t contentLength) { return _contentLength; }

    void addHeader(const char *field, const char *value);
    void setCookie(const char *key, const char *value, unsigned long max_age = 60*60*24*30);

    void setContent(const char *content);
    void setContent(const uint8_t *content, size_t len);

    const char * getContent();
    size_t getContentLength();

    esp_err_t send();
};

typedef std::function<esp_err_t(PsychicHttpServerRequest *request)> PsychicHttpRequestHandler;
//typedef std::function<size_t(PsychicHttpServerRequest *request, int ev, MongooseString filename, uint64_t index, uint8_t *data, size_t len)> PsychicHttpUploadHandler;
typedef std::function<esp_err_t(PsychicHttpWebSocketRequest *connection)> PsychicHttpWebSocketRequestHandler;
typedef std::function<esp_err_t(PsychicHttpWebSocketRequest *connection, httpd_ws_frame *frame)> PsychicHttpWebSocketFrameHandler;
typedef std::function<esp_err_t(httpd_handle_t hd, int sockfd)> PsychicHttpOpenHandler;
typedef std::function<esp_err_t(httpd_handle_t hd, int sockfd)> PsychicHttpCloseHandler;

class PsychicHttpServerEndpoint
{
  friend PsychicHttpServer;

  private:
    PsychicHttpServer *server;
    http_method method;

    PsychicHttpRequestHandler request;
    //PsychicHttpUploadHandler upload;
    PsychicHttpWebSocketRequestHandler wsConnect;
    PsychicHttpWebSocketFrameHandler wsFrame;

  public:
    PsychicHttpServerEndpoint();
    PsychicHttpServerEndpoint(PsychicHttpServer *server, http_method method);

    PsychicHttpServerEndpoint *onRequest(PsychicHttpRequestHandler handler);
    // PsychicHttpServerEndpoint *onUpload(PsychicHttpUploadHandler handler);
    PsychicHttpServerEndpoint *onConnect(PsychicHttpWebSocketRequestHandler handler);
    PsychicHttpServerEndpoint *onFrame(PsychicHttpWebSocketFrameHandler handler);

    static esp_err_t requestHandler(httpd_req_t *req);
    static esp_err_t websocketHandler(httpd_req_t *req);
};

class PsychicHttpWebSocketRequest : public PsychicHttpServerRequest
{
  friend PsychicHttpServer;

  public:
    PsychicHttpWebSocketRequest(PsychicHttpServer *server, httpd_req_t *req);
    virtual ~PsychicHttpWebSocketRequest();

    PsychicHttpWebSocketConnection *connection;

    esp_err_t reply(httpd_ws_frame_t * ws_pkt);
    esp_err_t reply(httpd_ws_type_t op, const void *data, size_t len);
    esp_err_t reply(const char *buf);
};

class PsychicHttpWebSocketConnection
{
  friend PsychicHttpServer;

  protected:
    httpd_handle_t _server;
    int _fd;

  public:
    PsychicHttpWebSocketConnection(httpd_handle_t server, int fd);

    esp_err_t queueMessage(httpd_ws_frame_t * ws_pkt);
    esp_err_t queueMessage(httpd_ws_type_t op, const void *data, size_t len);
    esp_err_t queueMessage(const char *buf);
    static void queueMessageCallback(void *arg);

    int id() { return this->_fd; }
};

class PsychicHttpServer
{
  protected:
    bool use_ssl = false;
    std::list<PsychicHttpServerEndpoint *> endpoints;

  public:
    PsychicHttpServer();
    ~PsychicHttpServer();

    httpd_handle_t server;
    httpd_config_t config;
    httpd_ssl_config_t ssl_config;

    #ifdef ENABLE_KEEPALIVE
      wss_keep_alive_config_t keep_alive_config;
      wss_keep_alive_t keep_alive;
    #endif
    
    PsychicHttpServerEndpoint defaultEndpoint;
    PsychicHttpOpenHandler openHandler;
    PsychicHttpCloseHandler closeHandler;

    static void destroy(void *ctx);

    void listen(uint16_t port);
    void listen(uint16_t port, const char *cert, const char *private_key);
    bool start();
    void stop();

    PsychicHttpServerEndpoint *on(const char* uri);
    PsychicHttpServerEndpoint *on(const char* uri, http_method method);
    PsychicHttpServerEndpoint *on(const char* uri, PsychicHttpRequestHandler onRequest);
    PsychicHttpServerEndpoint *on(const char* uri, http_method method, PsychicHttpRequestHandler onRequest);

    PsychicHttpServerEndpoint *websocket(const char* uri);

    void onNotFound(PsychicHttpRequestHandler fn);
    static esp_err_t notFoundHandler(httpd_req_t *req, httpd_err_code_t err);
    static esp_err_t defaultNotFoundHandler(PsychicHttpServerRequest *request);

    void onOpen(PsychicHttpOpenHandler handler);
    void onClose(PsychicHttpCloseHandler handler);
    static esp_err_t openCallback(httpd_handle_t hd, int sockfd);
    static void closeCallback(httpd_handle_t hd, int sockfd);

    #ifdef ENABLE_SERVE_STATIC
      PsychicStaticFileHandler *staticHandler;
      PsychicStaticFileHandler& serveStatic(const char* uri, fs::FS& fs, const char* path, const char* cache_control = NULL);
    #endif

    void sendAll(httpd_ws_frame_t * ws_pkt);
    void sendAll(httpd_ws_type_t op, const void *data, size_t len);
    void sendAll(const char *buf);
};

#ifdef ENABLE_SERVE_STATIC
  class PsychicStaticFileHandler {
    using File = fs::File;
    using FS = fs::FS;
    private:
      bool _getFile(PsychicHttpServerRequest *request);
      bool _fileExists(const String& path);
      uint8_t _countBits(const uint8_t value) const;
    protected:
      FS _fs;
      File _file;
      String _filename;
      String _uri;
      String _path;
      String _default_file;
      String _cache_control;
      String _last_modified;
      bool _isDir;
      bool _gzipFirst;
      uint8_t _gzipStats;
    public:
      PsychicStaticFileHandler(const char* uri, FS& fs, const char* path, const char* cache_control);
      bool canHandle(PsychicHttpServerRequest *request);
      esp_err_t handleRequest(PsychicHttpServerRequest *request);
      PsychicStaticFileHandler& setIsDir(bool isDir);
      PsychicStaticFileHandler& setDefaultFile(const char* filename);
      PsychicStaticFileHandler& setCacheControl(const char* cache_control);
      PsychicStaticFileHandler& setLastModified(const char* last_modified);
      PsychicStaticFileHandler& setLastModified(struct tm* last_modified);
    #ifdef ESP8266
      PsychicStaticFileHandler& setLastModified(time_t last_modified);
      PsychicStaticFileHandler& setLastModified(); //sets to current time. Make sure sntp is runing and time is updated
    #endif
      //PsychicStaticFileHandler& setTemplateProcessor(AwsTemplateProcessor newCallback) {_callback = newCallback; return *this;}
  };

class PsychicHttpFileResponse: public PsychicHttpServerResponse
{
  using File = fs::File;
  using FS = fs::FS;
  private:
    File _content;
    String _path;
    bool _sendContentLength;
    bool _chunked;
    String _contentType;
    void _setContentType(const String& path);
  public:
    PsychicHttpFileResponse(PsychicHttpServerRequest *request, FS &fs, const String& path, const String& contentType=String(), bool download=false);
    PsychicHttpFileResponse(PsychicHttpServerRequest *request, File content, const String& path, const String& contentType=String(), bool download=false);
    ~PsychicHttpFileResponse();
    esp_err_t send();
};

#endif // ENABLE_SERVE_STATIC
#endif /* PsychicHttp_h */