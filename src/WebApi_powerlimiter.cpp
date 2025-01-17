// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Thomas Basler and others
 */
#include "WebApi_powerlimiter.h"
#include "VeDirectFrameHandler.h"
#include "ArduinoJson.h"
#include "AsyncJson.h"
#include "Configuration.h"
#include "MqttHandleHass.h"
#include "MqttHandleVedirectHass.h"
#include "MqttSettings.h"
#include "PowerMeter.h"
#include "PowerLimiter.h"
#include "WebApi.h"
#include "helper.h"
#include "WebApi_errors.h"

void WebApiPowerLimiterClass::init(AsyncWebServer* server)
{
    using std::placeholders::_1;

    _server = server;

    _server->on("/api/powerlimiter/status", HTTP_GET, std::bind(&WebApiPowerLimiterClass::onStatus, this, _1));
    _server->on("/api/powerlimiter/config", HTTP_GET, std::bind(&WebApiPowerLimiterClass::onAdminGet, this, _1));
    _server->on("/api/powerlimiter/config", HTTP_POST, std::bind(&WebApiPowerLimiterClass::onAdminPost, this, _1));
}

void WebApiPowerLimiterClass::loop()
{
}

void WebApiPowerLimiterClass::onStatus(AsyncWebServerRequest* request)
{
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    const CONFIG_T& config = Configuration.get();

    root[F("enabled")] = config.PowerLimiter_Enabled;
    root[F("solar_passtrough_enabled")] = config.PowerLimiter_SolarPassTroughEnabled;
    root[F("battery_drain_strategy")] = config.PowerLimiter_BatteryDrainStategy;
    root[F("is_inverter_behind_powermeter")] = config.PowerLimiter_IsInverterBehindPowerMeter;
    root[F("inverter_id")] = config.PowerLimiter_InverterId;
    root[F("inverter_channel_id")] = config.PowerLimiter_InverterChannelId;
    root[F("target_power_consumption")] = config.PowerLimiter_TargetPowerConsumption;
    root[F("target_power_consumption_hysteresis")] = config.PowerLimiter_TargetPowerConsumptionHysteresis;
    root[F("lower_power_limit")] = config.PowerLimiter_LowerPowerLimit;
    root[F("upper_power_limit")] = config.PowerLimiter_UpperPowerLimit;
    root[F("battery_soc_start_threshold")] = config.PowerLimiter_BatterySocStartThreshold;
    root[F("battery_soc_stop_threshold")] = config.PowerLimiter_BatterySocStopThreshold;
    root[F("voltage_start_threshold")] = static_cast<int>(config.PowerLimiter_VoltageStartThreshold * 100 +0.5) / 100.0;
    root[F("voltage_stop_threshold")] = static_cast<int>(config.PowerLimiter_VoltageStopThreshold * 100 +0.5) / 100.0;;
    root[F("voltage_load_correction_factor")] = config.PowerLimiter_VoltageLoadCorrectionFactor;

    response->setLength();
    request->send(response);
}

void WebApiPowerLimiterClass::onAdminGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    this->onStatus(request);
}

void WebApiPowerLimiterClass::onAdminPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonObject retMsg = response->getRoot();
    retMsg[F("type")] = F("warning");

    if (!request->hasParam("data", true)) {
        retMsg[F("message")] = F("No values found!");
        response->setLength();
        request->send(response);
        return;
    }

    String json = request->getParam("data", true)->value();

    if (json.length() > 1024) {
        retMsg[F("message")] = F("Data too large!");
        response->setLength();
        request->send(response);
        return;
    }

    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, json);

    if (error) {
        retMsg[F("message")] = F("Failed to parse data!");
        response->setLength();
        request->send(response);
        return;
    }

    if (!(root.containsKey("enabled") 
        	&& root.containsKey("lower_power_limit")
            && root.containsKey("inverter_id")
            && root.containsKey("inverter_channel_id")
            && root.containsKey("target_power_consumption")
            && root.containsKey("target_power_consumption_hysteresis")
        )) {
        retMsg[F("message")] = F("Values are missing!");
        retMsg[F("code")] = WebApiError::GenericValueMissing;
        response->setLength();
        request->send(response);
        return;
    }


    CONFIG_T& config = Configuration.get();
    config.PowerLimiter_Enabled = root[F("enabled")].as<bool>();
    config.PowerLimiter_SolarPassTroughEnabled = root[F("solar_passtrough_enabled")].as<bool>();
    config.PowerLimiter_BatteryDrainStategy= root[F("battery_drain_strategy")].as<uint8_t>();
    config.PowerLimiter_IsInverterBehindPowerMeter = root[F("is_inverter_behind_powermeter")].as<bool>();
    config.PowerLimiter_InverterId = root[F("inverter_id")].as<uint8_t>();
    config.PowerLimiter_InverterChannelId = root[F("inverter_channel_id")].as<uint8_t>();
    config.PowerLimiter_TargetPowerConsumption = root[F("target_power_consumption")].as<uint32_t>();
    config.PowerLimiter_TargetPowerConsumptionHysteresis = root[F("target_power_consumption_hysteresis")].as<uint32_t>();
    config.PowerLimiter_LowerPowerLimit = root[F("lower_power_limit")].as<uint32_t>();
    config.PowerLimiter_UpperPowerLimit = root[F("upper_power_limit")].as<uint32_t>();
    config.PowerLimiter_BatterySocStartThreshold = root[F("battery_soc_start_threshold")].as<float>();
    config.PowerLimiter_BatterySocStopThreshold = root[F("battery_soc_stop_threshold")].as<float>();
    config.PowerLimiter_VoltageStartThreshold = root[F("voltage_start_threshold")].as<float>();
    config.PowerLimiter_VoltageStartThreshold = static_cast<int>(config.PowerLimiter_VoltageStartThreshold * 100) / 100.0;
    config.PowerLimiter_VoltageStopThreshold = root[F("voltage_stop_threshold")].as<float>();
    config.PowerLimiter_VoltageStopThreshold = static_cast<int>(config.PowerLimiter_VoltageStopThreshold * 100) / 100.0;
    config.PowerLimiter_VoltageLoadCorrectionFactor = root[F("voltage_load_correction_factor")].as<float>();
    Configuration.write();

    retMsg[F("type")] = F("success");
    retMsg[F("message")] = F("Settings saved!");

    response->setLength();
    request->send(response);
}
