/*
This file is part of Ionlib.  Copyright (C) 2018  Tim Sweet

Ionlib is free software : you can redistribute it and / or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Ionlib is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Ionlib.If not, see <http://www.gnu.org/licenses/>.
*/

#include "EasyAPConfig.h"
#include "EEPROM.h"
#include "Checksum.h"
#include <string.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#define LOG_LEVEL_DEBUG
#include "log.h"

#define CHECKSUM_LEN (2)
#define CFG_LEN (MAX_AP_NAME_LEN+MAX_AP_PASSWORD_LEN+CHECKSUM_LEN)
#define NUM_AP_CONNECT_ATTEMPTS (60) //number of times to check if we are connected
#define AP_CONNECT_INTERVAL (500) //how many milliseconds between checks if we are connected

static String g_ap_name_buf;
static String g_ap_password_buf;
static bool g_bufs_ready = false;
static ESP8266WebServer* g_server = NULL;

static String prepareHtmlPage()
{
    String htmlPage =
        String("<html><form>SSID:<input name=\"s\"/>PSK:<input name=\"p\"/><input type=\"submit\"/></html>");
    return htmlPage;
}
static uint16_t ComputeAPChecksum(const char apName[MAX_AP_NAME_LEN], const char apPass[MAX_AP_PASSWORD_LEN])
{
    char buf[MAX_AP_NAME_LEN + MAX_AP_PASSWORD_LEN];
    (void)memcpy(buf, apName, MAX_AP_NAME_LEN);
    (void)memcpy(buf + MAX_AP_NAME_LEN, apPass, MAX_AP_PASSWORD_LEN);
    return ion::FletcherChecksum(buf, MAX_AP_NAME_LEN + MAX_AP_PASSWORD_LEN);
}
static bool ReadConfig(uint16_t startAddr, char apName[MAX_AP_NAME_LEN], char apPass[MAX_AP_PASSWORD_LEN], uint16_t* checksum)
{
    uint16_t cfg_checksum;
    char *cfg_checksum_bytes = (char*)&cfg_checksum;
    uint16_t cursor = startAddr;
    EEPROM.begin(startAddr + CFG_LEN);
    //Read AP name from EEPROM
    for (uint32_t index = 0; index < MAX_AP_NAME_LEN; ++index)
    {
        apName[index] = EEPROM.read(cursor);
        cursor++;
    }
    //Read AP password from EEPROM
    for (uint32_t index = 0; index < MAX_AP_PASSWORD_LEN; ++index)
    {
        apPass[index] = EEPROM.read(cursor);
        cursor++;
    }
    //Read checksum from EEPROM
    for (uint32_t index = 0; index < CHECKSUM_LEN; ++index)
    {
        cfg_checksum_bytes[index] = EEPROM.read(cursor);
        cursor++;
    }
    EEPROM.end();
    LOGDEBUG("Read ap %s", apName);
    LOGDEBUG("Read pass %s", apPass);
    LOGDEBUG("Read checksum: %u", cfg_checksum);
    *checksum = ComputeAPChecksum(apName, apPass);
    return cfg_checksum == *checksum;
}
static bool WriteConfig(uint16_t startAddr, const char apName[MAX_AP_NAME_LEN], const char apPass[MAX_AP_PASSWORD_LEN], uint16_t checksum)
{
    uint16_t cursor = startAddr;
    EEPROM.begin(startAddr + CFG_LEN);
    for (uint16_t byte_idx = 0; byte_idx < MAX_AP_NAME_LEN; ++byte_idx)
    {
        EEPROM.write(cursor, apName[byte_idx]);
        cursor++;
    }
    for (uint16_t byte_idx = 0; byte_idx < MAX_AP_PASSWORD_LEN; ++byte_idx)
    {
        EEPROM.write(cursor, apPass[byte_idx]);
        cursor++;
    }
    char* checksum_bytes = (char*)&checksum;
    EEPROM.write(cursor, checksum_bytes[0]);
    cursor++;
    EEPROM.write(cursor, checksum_bytes[1]);
    cursor++;
    bool commit_success = EEPROM.commit();
    EEPROM.end();
    //Read back the config to verify it was stored correctly
    char readback_ap_name[MAX_AP_NAME_LEN];
    char readback_ap_pass[MAX_AP_PASSWORD_LEN];
    uint16_t readback_checksum;
    bool config_good = ReadConfig(startAddr, readback_ap_name, readback_ap_pass, &readback_checksum);
    return commit_success && config_good && readback_checksum == checksum;
}

EasyAPConfig::EasyAPConfig(uint16_t configStartAddr)
{
    char cfg_ap_name[MAX_AP_NAME_LEN];
    char cfg_ap_pass[MAX_AP_PASSWORD_LEN];
    uint16_t checksum;
    cfgStartAddr_ = configStartAddr;
    //Try to load the config from EEPROM
    bool config_valid = ReadConfig(cfgStartAddr_, cfg_ap_name, cfg_ap_pass, &checksum);
    if (config_valid)
    {
        //AP and password are good
        (void)memcpy(apName_, cfg_ap_name, sizeof(apName_));
        (void)memcpy(apPassword_, cfg_ap_pass, sizeof(cfg_ap_pass));
    } else
    {
        (void)memset(apName_, 0, sizeof(apName_));
        (void)memset(cfg_ap_pass, 0, sizeof(cfg_ap_pass));
    }
}
void handleGetCredentials()
{
    LOGDEBUG("In cred handler");
    if (g_server->hasArg("s"))
    {
        g_ap_name_buf = g_server->arg("s");
        if (g_server->hasArg("p"))
        {
            g_ap_password_buf = g_server->arg("p");
            g_bufs_ready = true;
        }
    }
    if (!g_bufs_ready)
    {
        g_server->send(200, "text/html", prepareHtmlPage());
    }
}
void EasyAPConfig::Connect(const char* setupAPName, uint32_t timeout)
{
    unsigned long start_t = millis();
    LOGDEBUG("Startup EasyAPConfig::Connect");
    while (WiFi.status() != WL_CONNECTED && (millis() - start_t < timeout))
    {
        LOGINFO("WiFi is not connected");
        if (apName_[0] != 0)
        {
            LOGINFO("Connecting to configured AP");
            //We have a valid configuration, try to use it
            WiFi.begin(apName_, apPassword_);
            LOGDEBUG("Client IP: %s", WiFi.localIP().toString().c_str());
            //Wait until successfully connected or a timeout occurs
            for (uint32_t connect_checks = 0; connect_checks < NUM_AP_CONNECT_ATTEMPTS; ++connect_checks)
            {
                if (WiFi.status() == WL_CONNECTED)
                {
                    break;
                }
                delay(AP_CONNECT_INTERVAL);
            }
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            LOGINFO("Creating cfg AP");
            g_bufs_ready = false;
            //Create an access point to get credentials
            bool status = WiFi.softAPConfig(IPAddress(192, 168, 0, 1), IPAddress(192, 168, 0, 1), IPAddress(255, 255, 255, 0));
            if (!status)
            {
                LOGWARN("Failed to config softAP");
                continue;
            }
            status = WiFi.softAP(setupAPName);
            if (!status)
            {
                LOGWARN("Failed to setup softAP");
                continue;
            }
            LOGDEBUG("AP IP: %s", WiFi.softAPIP().toString().c_str());
            LOGDEBUG("Opening cfg web server");
            //Setup the webserver
            if (g_server == NULL)
            {
                g_server = new ESP8266WebServer(82);
            }
            g_server->onNotFound(handleGetCredentials);
            g_server->begin();
            bool keep_waiting = (millis() - start_t < timeout) || (wifi_softap_get_station_num() > 0);
            while (!g_bufs_ready && keep_waiting)
            {
                g_server->handleClient();
                LOGDEBUG("Waiting for credentials");
                delay(500);
                ESP.wdtFeed();
            }
            if (g_bufs_ready)
            {
                strcpy(apName_, g_ap_name_buf.c_str());
                strcpy(apPassword_, g_ap_password_buf.c_str());
                //Compute checksum
                uint16_t checksum = ComputeAPChecksum(apName_, apPassword_);
                LOGDEBUG("Writing config: ap: %s, pass: %s, checksum: %hu", apName_, apPassword_, checksum);
                bool write_success = WriteConfig(cfgStartAddr_, apName_, apPassword_, checksum);
                LOGINFO("AP info write %s", write_success ? "success" : "failed");
                WiFi.disconnect();
            } else
            {
                LOGDEBUG("Exiting due to timeout");
            }
        }
    }
}