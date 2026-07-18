AsyncWiFiMulti
==============

This is an asynchronous replacement for [WiFiMulti](https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/src/WiFiMulti.h).

Lots of code copied from [GuLinux](https://github.com/GuLinux/AsyncWiFiMulti).

## Installation

### PlatformIO

```
https://github.com/WorksOnMyCloud/AsyncWiFiMulti.git#v0.0.0
```
to the `lib_deps` section of your project.

## API

```
    bool addAP(const char* ssid, const char *passphrase = nullptr);
```
Adds an access point to the configuration. Returns `true` if adding was successful, `false` if the AP SSID/passphrase are not valid, or if the AP is already present.

```
    bool start(ms)
```
Starts background connection, ms is delay for retries (default 1000ms), implemented using default callbacks.


### Callbacks signature

```
    using OnConnected = std::function<void(const ApSettings&)>;
    using OnFailure = std::function<void()>;
    using OnDisconnected = std::function<void(const char *ssid, uint8_t disconnectionReason)>;
```

### Callbacks setters

```
    void onConnected(OnConnected callback);
    void onFailure(OnFailure callback);
    void onDisconnected(OnDisconnected callback);
```
