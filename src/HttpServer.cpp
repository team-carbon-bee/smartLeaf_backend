#include <SPIFFS.h>

#ifdef ESP32
#include <ESPmDNS.h>
#include "tools/WiFiManager.h"
#else // ESP8266
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#endif

// You can update by 'curl -F "image=@firmware.bin" MDNS_Name_of_ESP.local/'

#include "ConfigurationProvider.h"
#include "AnimationFactory.h"
#include "Animator.h"
#include "HttpServer.h"
#include "tools/Logger.h"
#include "Constants.h"

#include "web_autogen.h"

/********************************************************/
/******************** Public Method *********************/
/********************************************************/

HttpServer::HttpServer()
    : _webServer(80), _httpUpdater(true)
{
}

HttpServer::~HttpServer()
{
}

void HttpServer::setup(void)
{
    MDNS.begin(Configuration.parameters().hostname.c_str());
    MDNS.addService("http", "tcp", 80);

    // Enable global CORS
    _webServer.enableCORS();

    _webServer.on("/reboot", HTTP_PUT, [&]() {
        _webServer.send(200, "text/plain", "ESP reboot now !");
        delay(200);
        ESP.restart();
    });

    _webServer.on("/wifimanager", [&]() {
        _webServer.send(200, "text/plain", "Reset settings of WifiManager, the card reboot now in AP mode");
        delay(200);
        WiFiManager wifiManager;
        wifiManager.startConfigPortal();
        delay(200);
        ESP.restart();
    });

    _webServer.on("/resetConfiguration", [&]() {
        _webServer.send(200, "text/plain", "Resetting all parameters, you must reboot for apply it !");
        delay(200);
        Configuration.createDefaultConfiguration();
        Configuration.saveToFlash();
    });

    _webServer.on("/setConfiguration", HTTP_PUT, [&]() {
        setConfig();
    });

    _webServer.on("/animations", [&]() {
        getAnimationList();
    });

    _webServer.on("/informations", [&]() {
        getInformations();
    });

    _webServer.on("/powerOff", HTTP_PUT, [&]() {
        powerOff();
    });

    _webServer.on("/powerOn", HTTP_PUT, [&]() {
        powerOn();
    });

    _webServer.on("/previewAnimation", HTTP_PUT, [&]() {
        previewAnimation();
    });

    _webServer.onNotFound([&]() {
        handleFileRead(_webServer.uri());
    });

    _httpUpdater.setup(&_webServer, String("/update"));
    _webServer.begin();
}

void HttpServer::handle(void)
{
    _webServer.handleClient();
}

void HttpServer::setConfig()
{
    if (_webServer.hasArg("plain") == false)
    {
        Log.println("Error, no body received !");
        _webServer.send(400, "text/plain", "Body not received");
    }
    else
    {
        Log.println("Received new configuration !");
        Log.println(_webServer.arg("plain"));
        if (Configuration.load(_webServer.arg("plain").c_str()))
        {
            if (Configuration.saveToFlash())
            {
                _webServer.send(200, "application/json", "{\"result\":true}");
            }
            else
            {
                _webServer.send(400, "application/json", "{\"result\":false}");
            }
        }
        else
        {
            Log.println("Error, parsing JSON !");
            _webServer.send(400, "text/plain", "Error during parsing JSON");
        }
    }
}

void HttpServer::getAnimationList()
{
    if (GlobalAnimationFactory.animations().moveToStart())
    {
        DynamicJsonDocument doc(3 * 1024);
        // JsonObject animationsList = doc.createNestedObject("animations");
        do
        {
            auto animation = GlobalAnimationFactory.animations().getCurrent();
            // JsonObject currentAnimation = animationsList.createNestedObject(String(animation->id()));
            JsonObject currentAnimation = doc.createNestedObject(String(animation->id()));
            currentAnimation["name"] = animation->name();
            currentAnimation["canFinish"] = animation->canFinish();
        } while (GlobalAnimationFactory.animations().next());

        String s;
        serializeJson(doc, s);
        _webServer.send(200, "application/json", s);
    }
    else
    {
        _webServer.send(400, "text/plain", "Body not received");
    }
}

void HttpServer::getInformations()
{
    DynamicJsonDocument doc(3 * 1024);
    doc["buildDate"] = __DATE__ " " __TIME__;
    doc["version"] = Constants::ApplicationVersion;
    String s;
    serializeJson(doc, s);
    _webServer.send(200, "application/json", s);
}

void HttpServer::powerOn()
{
    GlobalAnimator.enabled(true);
    _webServer.send(200, "application/json", "{\"result\":true}");
}

void HttpServer::powerOff()
{
    GlobalAnimator.enabled(false);
    _webServer.send(200, "application/json", "{\"result\":true}");
}

void HttpServer::previewAnimation()
{

    uint8_t id = HTTPServer.webServer().arg("id").toInt();
    GlobalAnimator.previewAnimation(id);
    _webServer.send(200, "application/json", "{\"result\":true}");
}

String HttpServer::getContentType(String filename)
{
    if (_webServer.hasArg("download"))
        return "application/octet-stream";
    else if (filename.endsWith(".htm"))
        return "text/html";
    else if (filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".css"))
        return "text/css";
    else if (filename.endsWith(".js"))
        return "application/javascript";
    else if (filename.endsWith(".png"))
        return "image/png";
    else if (filename.endsWith(".gif"))
        return "image/gif";
    else if (filename.endsWith(".jpg"))
        return "image/jpeg";
    else if (filename.endsWith(".ico"))
        return "image/x-icon";
    else if (filename.endsWith(".xml"))
        return "text/xml";
    else if (filename.endsWith(".pdf"))
        return "application/x-pdf";
    else if (filename.endsWith(".zip"))
        return "application/x-zip";
    else if (filename.endsWith(".gz"))
        return "application/x-gzip";
    else if (filename.endsWith(".json"))
        return "application/json";
    return "text/plain";
}

// send the right file to the client (if it exists)
bool HttpServer::handleFileRead(String path)
{
    // Send cors and exit properly when method OPTIONS
    if (_webServer.method() == HTTP_OPTIONS)
    {
        _webServer.send(204);
        return true;
    }

    if (path.endsWith("/"))
    {
        path += "index.html"; // If a folder is requested, send the index file
    }

    Log.println("handleFileRead: " + path);

    // Check if file is on flash and send it
    if (checkAndSendFile(path, _webServer))
        return true;

    String contentType = HTTPServer.getContentType(path); // Get the MIME type
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
    {                                       // If the file exists, either as a compressed archive, or normal
        if (SPIFFS.exists(pathWithGz))      // If there's a compressed version available
            path += ".gz";                  // Use the compressed verion
        File file = SPIFFS.open(path, "r"); // Open the file
        _webServer.streamFile(file, contentType); // Send it to the client
        file.close();                                         // Close the file again
        Log.println(String("\tSent file: ") + path);
        return true;
    }
    else{
        Log.println(String("\tFile Not Found: ") + path); // If the file doesn't exist, return false
        _webServer.send(404, "text/plain", "File Not Found !");
        return false;
    }
}

#ifdef ESP32
WebServer &HttpServer::webServer()
#else // ESP8266
ESP8266WebServer &HttpServer::webServer()
#endif
{
    return _webServer;
}

/********************************************************/
/******************** Private Method ********************/
/********************************************************/

void HttpServer::sendJson(const uint16_t code, JsonDocument &doc)
{
    WiFiClient client = HTTPServer.webServer().client();

    // Write Header
    client.print(F("HTTP/1.0 "));
    client.print(code);
    client.println(F(" OK"));
    client.println(F("Content-Type: application/json"));
    client.println(F("Access-Control-Allow-Origin: *"));
    client.print(F("Content-Length: "));
    client.println(measureJson(doc));
    client.println(F("Connection: close"));
    client.println();

    // Write JSON document
    serializeJson(doc, client);
}

#if !defined(NO_GLOBAL_INSTANCES)
HttpServer HTTPServer;
#endif
