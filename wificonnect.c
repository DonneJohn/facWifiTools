/*
**
** Copyright 2006, The Android Open Source Project 
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at ** 
**     http://www.apache.org/licenses/LICENSE-2.0 ** 
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#include <stdlib.h>
#include "string.h"
#include "hardware_legacy/wifi.h"

#define LOG_TAG "FacWifiTools"
#include "cutils/log.h"
#include "cutils/properties.h"
#include <netutils/ifc.h>
#include <netutils/dhcp.h>

static const char DRIVER_PROP_NAME[]    = "wlan.driver.status";


static void usage(void)
{
    fprintf(stderr,"usage: facwifitools ssid  (If no ssid, do nothing!)\n");
    fprintf(stderr,"\tfor example: facwifitools ssid \"scty-5\"\n");
    exit(1);
}
static int doCommand(const char *cmd, char *replybuf, int replybuflen) {
    size_t reply_len = replybuflen - 1;

    if (wifi_command(cmd, replybuf, &reply_len) != 0)
        return -1;
    else {
        // Strip off trailing newline
        if (reply_len > 0 && replybuf[reply_len-1] == '\n')
            replybuf[reply_len-1] = '\0';
        else
            replybuf[reply_len] = '\0';
        return 0;
    }
}

static int doIntCommand(const char *cmd) {
    char reply[256];
    if (doCommand(cmd, reply, sizeof(reply)) != 0) {
        return -1;
    } else {
        return atoi(reply);
    }
}

static int doBooleanCommand(const char *cmd, const char *expect) {
    char reply[256];
    if (doCommand(cmd, reply, sizeof(reply)) != 0) {
        return 0;
    } else {
        return (strcmp(reply, expect) == 0);
    }
}

// Send a command to the supplicant, and return the reply as a String 
static char* doStringCommand(const char *cmd) {
    static char reply[4096];
    if (doCommand(cmd, reply, sizeof(reply)) != 0) {
        return NULL;
    } else {
        return reply;
    }
}

int get_driver_status() {
    char driver_status[PROPERTY_VALUE_MAX];
    if (property_get(DRIVER_PROP_NAME, driver_status, NULL)) {
        ALOGE("driverstatus %s", driver_status);
        if (strcmp(driver_status, "ok") == 0) {
            return 0;
        }
    }
    return -1;
}

int init_stage() {
    // load the wifi driver: insmod .ko
    int ret = get_driver_status();
    ALOGE("is_wifi_driver_loaded %d", ret);
    if (ret < 0) {
        ret = wifi_load_driver();
    }

    if(ret < 0) {
        ALOGE("Failed to load Wi-Fi driver. %s",strerror(errno));
        return -1;
    }
    // start wpa_supplicant
    ret =  wifi_start_supplicant(-1);
    if(ret < 0) {
        ALOGE("Failed to start supplicant daemon. %s",strerror(errno));
        return -1;
    }
    ret = wifi_connect_to_supplicant();
    if(ret < 0) {
        ALOGE("Failed to connect supplicant daemon. %s",strerror(errno));
        return -1;
    }
    char ifname[256];
    property_get("wifi.interface", ifname ,"eth0");
    ret = ifc_enable(ifname);
    if(ret < 0) {
        ALOGE("Failed to enable wifi interface %s. %s", ifname ,strerror(errno));
        return -1;
    }
    return 0;
}

int scan_stage(){
        // XXX we don't need to really scan the wifi
        return 0;
}

#define SSID_NAME "ssid"
#define KEY_MGMT "key_mgmt"
#define MODE "NONE"
int config_stage(const char *ssid){
    // Add a network config to supplicant mode
    const char *needle = "\"";
    char ssidstr[256];
    if(!strstr(ssid, needle)) {
        snprintf(ssidstr, sizeof(ssidstr), "%s%s%s", needle, ssid, needle);
    }
    ALOGD("new ssid is: %s.", ssidstr);
    int networkId = doIntCommand("IFNAME=wlan0 ADD_NETWORK");
    // Add a new network id
    if(networkId < 0) {
        ALOGE("Failed to add a network configuration. %s",strerror(errno));
        return -1;
    }
    ALOGD("Add a network %d", networkId);

    // set the ssid of the destination wifi adhoc
    char cmdstr[256];
    snprintf(cmdstr, sizeof(cmdstr), "IFNAME=wlan0 SET_NETWORK %d %s %s",networkId, SSID_NAME, ssidstr);
    if(!doBooleanCommand(cmdstr,"OK")) {
        ALOGE("Failed to set network %d configuration ssid. %s ssid is:%s", networkId, strerror(errno), ssid);
        return -1;
    }
    snprintf(cmdstr, sizeof(cmdstr), "IFNAME=wlan0 SET_NETWORK %d %s %s", networkId, KEY_MGMT ,MODE);
    if(!doBooleanCommand(cmdstr,"OK")) {
        ALOGE("Failed to set network %d configuration key_mgmr. %s", networkId, strerror(errno));
        return -1;
    }

    return networkId;
}


#define CONNECTED "CTRL-EVENT-CONNECTED"
#define DISCONNECTED "CTRL-EVENT-DISCONNECTED"
int connect_stage(int networkId) {
    char cmdstr[256];
    // enable the network
    snprintf(cmdstr, sizeof(cmdstr), "IFNAME=wlan0 SELECT_NETWORK %d",networkId);
    if(!doBooleanCommand(cmdstr,"OK")) {
        ALOGE("Failed to select network %d. %s", networkId, strerror(errno));
        return -1;
    }

    // wait for connect 
    char buf[256];
    while(1) {
        int nread = wifi_wait_for_event(buf, sizeof(buf));
        if(nread > 0) {
            ALOGE("receive buf:\n %s\n",buf);
            if (strstr(buf,CONNECTED) != NULL) {
                break;
            } else if (strstr(buf, DISCONNECTED) != NULL ) {
                doIntCommand("IFNAME=wlan0 RECONNECT");
            }
            // XXX danger of not going out of the loop!!!
        }
        continue;
    }
    return 0;
}


int dhcp_stage(){
    int result;
    int ipaddr, gateway, mask, dns1, dns2, server;
    int lease;

    char ifname[256];
    char mDns1Name[256];
    char mDns2Name[256];
    property_get("wifi.interface", ifname ,"eth0");
    result = dhcp_release_lease(ifname);
    if (result < 0) {
        printf("false\n");
        ALOGE("Failed to release dhcp");
        return -1;
    }
    snprintf(mDns1Name, sizeof(mDns1Name), "net.%s.dns1",ifname);
    snprintf(mDns2Name, sizeof(mDns2Name), "net.%s.dns2",ifname);

    result = do_dhcp_request(&ipaddr, &gateway, &mask, &dns1, &dns2, &server, &lease);
    if(result != 0) {
        ALOGE("Failed to dhcp on interface %s. %s", ifname, strerror(errno));
        return -1;
    }

    struct in_addr dns_struct1, dns_struct2, ip_struct;
    dns_struct1.s_addr = dns1;
    dns_struct2.s_addr = dns2;
    ip_struct.s_addr = ipaddr;
    printf("%s\n", inet_ntoa(ip_struct));
    ALOGE("get wifi ip: %s\n", inet_ntoa(ip_struct));
    property_set(mDns1Name,inet_ntoa(dns_struct1));
    property_set(mDns2Name,inet_ntoa(dns_struct2));
    return 0;
}

int main(int argc, char *argv[])
{
    if(argc != 3) usage();
    int ret = strcmp(argv[1], "ssid");
    if (ret != 0) usage();

    ret = init_stage();
    if(ret < 0) {
        ALOGE("Failed init stage. %s",strerror(errno));
        exit(-1);
    }
    ALOGD("Finished init stage.");

    ret = config_stage(argv[2]);
    if(ret < 0) {
        ALOGE("Failed config stage. %s",strerror(errno));
        exit(-1);
    }
    ALOGD("Finished config stage.");

    ret = connect_stage(ret);
    if(ret < 0) {
        ALOGE("Failed connect stage. %s",strerror(errno));
        exit(-1); 
    }
    ALOGD("Finished connect stage.");

    ret = dhcp_stage();
    if(ret < 0) {
        ALOGE("Failed dhcp stage. %s",strerror(errno));
        exit(-1);
    }
    ALOGD("Finished dhcp stage.");
    return 0;
}
