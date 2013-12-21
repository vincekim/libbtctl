/*
 *  Android Bluetooth Control tool
 *
 *  Copyright (C) 2013 João Paulo Rechi Vita
 *  Copyright (C) 2013 Vince Kim
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>
#include <hardware/bt_gatt_client.h>
#include <hardware/hardware.h>
#include <cutils/str_parms.h>



#define LOG_TAG "libbtctl"
#include <cutils/log.h>
#include "util.h"
#include "libbtctl.h"
#include "list.h"


#define DEV_LIST_SIZE 10
#define ERROR(fmt, ...)     ALOGE("%s: " fmt,__FUNCTION__, ## __VA_ARGS__)

#define ASSERTC(cond, msg, val) if (!(cond)) {ERROR("### ASSERT : %s line %d %s (%d) ###", __FILE__, __LINE__, msg, val);}

#define CLIENT_CBACK(P_CB, P_CBACK, ...)\
    if (P_CB && P_CB->P_CBACK) {            \
        P_CB->P_CBACK(__VA_ARGS__);         \
    }                                       \
    else {                                  \
        ASSERTC(0, "Callback is NULL", 0);  \
    }


static pid_t mypid;
static void (* wake_up_by)();
/* Arbitrary UUID used to identify this application with the GATT library. The
 * Android JAVA framework
 * (frameworks/base/core/java/android/bluetooth/BluetoothAdapter.java,
 * frameworks/base/core/java/android/bluetooth/BluetoothGatt.java and
 * frameworks/base/core/java/android/bluetooth/BluetoothGattServer.java) uses
 * the method randomUUID()
 */
static bt_uuid_t app_uuid = {
    .uu = { 0x1b, 0x1c, 0xb9, 0x2e, 0x0d, 0x2e, 0x4c, 0x45, \
        0xbb, 0xb9, 0xf4, 0x1b, 0x46, 0x39, 0x23, 0x36}
};


static libbtctl_ctx_t btctl_ctx;
static List connection_list;


static void get_characteristic_cb(int conn_id, int status, btgatt_srvc_id_t *srvc_id,
                                  btgatt_char_id_t *char_id, int char_prop);


p_btctl_bt_device_prop_t alloc_dev_prop(p_btctl_bt_device_prop_t p_data)
{
    p_btctl_bt_device_prop_t p_dev_prop =  NULL;

    p_dev_prop = (p_btctl_bt_device_prop_t)calloc(1, sizeof(btctl_bt_device_prop_t));

    if (p_data->bd_name.name) {
        strncpy((char *)p_dev_prop->bd_name.name, (char *)p_data->bd_name.name, sizeof(p_dev_prop->bd_name.name));      
    }

    memcpy(p_dev_prop->bd_addr.address, p_data->bd_addr.address, sizeof(p_dev_prop->bd_addr.address));
    p_dev_prop->bd_class = p_data->bd_class;
    p_dev_prop->bd_type = p_data->bd_type;

    if (p_data->bd_alias) {
        p_dev_prop->bd_alias = strdup(p_data->bd_alias);
    }
    p_dev_prop->bd_rssi = p_data->bd_rssi;
    return p_dev_prop;

}


/* increase device list */
static void realloc_bt_device_list()
{
    int i = 0, max = btctl_ctx.dicovered_device_list.max;
    max = max * 2;

    btctl_ctx.dicovered_device_list.max = max;
    btctl_ctx.dicovered_device_list.p_bt_device_list = realloc(btctl_ctx.dicovered_device_list.p_bt_device_list, sizeof(p_btctl_bt_device_prop_t) * max);      

}

inline int bt_device_list_empty()
{

    return(btctl_ctx.dicovered_device_list.count ? 0 : 1);

}


static void dealloc_dev_prop(p_btctl_bt_device_prop_t p_data)
{

    if (p_data->bd_alias) {
        free(p_data->bd_alias);
    }
    free(p_data);

}

static void enque_prop_data(p_btctl_bt_device_prop_t p_data)
{
    if (btctl_ctx.dicovered_device_list.count == btctl_ctx.dicovered_device_list.max)
        realloc_bt_device_list();
    btctl_ctx.dicovered_device_list.p_bt_device_list[btctl_ctx.dicovered_device_list.count++] = alloc_dev_prop(p_data);   
}


static uint32_t free_bt_device_list()
{
    uint32_t i = 0, count = btctl_ctx.dicovered_device_list.count;
    p_btctl_bt_device_prop_t *dev_list;


    for (dev_list = btctl_ctx.dicovered_device_list.p_bt_device_list, i = 0; i < count; i++) {
        dealloc_dev_prop(dev_list[i]);

    }

    free(btctl_ctx.dicovered_device_list.p_bt_device_list);
    return count;

}

static void init_bt_device_list(uint32_t count)
{
    int i = 0, max;
    btctl_ctx.dicovered_device_list.count = 0;

    max = (count > DEV_LIST_SIZE)? count : DEV_LIST_SIZE;
    btctl_ctx.dicovered_device_list.max = max;

    btctl_ctx.dicovered_device_list.p_bt_device_list = malloc(sizeof(p_btctl_bt_device_prop_t) * max);

}






static void print_bt_device_prop(p_btctl_bt_device_prop_t p_data)
{
    char addr_str[BT_ADDRESS_STR_LEN];
    ALOGD("  name: %s\n", p_data->bd_name.name);
    ALOGD("  addr: %s\n", ba2str((uint8_t *) p_data->bd_addr.address,
                                 addr_str));
    ALOGD("  class: 0x%x\n",  p_data->bd_class );
    switch ( p_data->bd_type ) {
    case BT_DEVICE_DEVTYPE_BREDR:
        ALOGD("  type: BR/EDR only\n");
        break;
    case BT_DEVICE_DEVTYPE_BLE:
        ALOGD("  type: LE only\n");
        break;
    case BT_DEVICE_DEVTYPE_DUAL:
        ALOGD("  type: DUAL MODE\n");
        break;
    }

    ALOGD("  alias: %s\n", p_data->bd_alias);
    ALOGD("  rssi: %i\n", p_data->bd_rssi);


}






static int find_svc(pConnection connection , btgatt_srvc_id_t *svc) {
    uint8_t i;

    for (i = 0; i < connection->svcs_size; i++)
        if (connection->svcs[i].svc_id.is_primary == svc->is_primary &&
            connection->svcs[i].svc_id.id.inst_id == svc->id.inst_id &&
            !memcmp(&connection->svcs[i].svc_id.id.uuid, &svc->id.uuid,
                    sizeof(bt_uuid_t)))
            return i;
    return -1;
}

static int find_char(service_info_t *svc_info, btgatt_char_id_t *ch) {
    uint8_t i;

    for (i = 0; i < svc_info->char_count; i++) {
        btgatt_char_id_t *char_id = &svc_info->chars_buf[i].char_id;

        if (char_id->inst_id == ch->inst_id &&
            !memcmp(&char_id->uuid, &ch->uuid, sizeof(bt_uuid_t)))
            return i;
    }

    return -1;
}

/* Clean blanks until a non-blank is found */
static void line_skip_blanks(char **line) {
    while (**line == ' ')
        (*line)++;
}

/* Parses a sub-string out of a string */
static void line_get_str(char **line, char *str) {
    line_skip_blanks(line);

    while (**line != 0 && **line != ' ') {
        *str = **line;
        (*line)++;
        str++;
    }

    *str = 0;
}




static void catcher(int signum) {

}

/* Called every time the adapter state changes */
static void adapter_state_change_cb(bt_state_t state) {

    btctl_ctx.adapter_state = state;
    ALOGD("\nAdapter state changed: %i\n", state);

    if (state ==  BT_STATE_ON) {
        /* Register as a GATT client with the stack
         *
     * This has to be done here because it is the first available point we're
     * sure the GATT interface is initialized and ready to be used, since
     * there is callback for gattiface->init().
         */
        bt_status_t status = btctl_ctx.gattiface->client->register_client(&app_uuid);
        usleep(100000);
        if (status != BT_STATUS_SUCCESS)
            ALOGD("Failed to register as a GATT client, status: %d\n",
                  status);
    }
    wake_up_by = &adapter_state_change_cb;
    kill(mypid, SIGCONT);
    ALOGD("\n adapter_state_change_cb -- signal sent\n");


}

/* Enables the Bluetooth adapter */
bt_status_t btctl_enable() {
    bt_status_t status;
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = catcher;
    sigaction(SIGCONT, &sigact, NULL);

    if (btctl_ctx.adapter_state == BT_STATE_ON) {
        //ALOGD("Bluetooth is already enabled\n");
        return BT_STATUS_NOT_READY;
    }

    status = btctl_ctx.btiface->enable();

    while (1 ) {
        pause();
        if (wake_up_by == adapter_state_change_cb)
            break;

    }
    wake_up_by = 0;
    return status;
}

/* Disables the Bluetooth adapter */
int btctl_disable() {
    bt_status_t result;
    int status;
    int ret = 0;

    if (btctl_ctx.adapter_state == BT_STATE_OFF) {
        ALOGD("Bluetooth is already disabled\n");
        return BTCTL_BT_STATE_OFF;
    }

    result = btctl_ctx.gattiface->client->unregister_client(btctl_ctx.client_if);
    usleep(100000);
    if (result != BT_STATUS_SUCCESS) {
        ALOGD("Failed to unregister client, error: %u\n", result);
        ret |= BTCTL_ERR_UNREG_CLIENT;
    }

    status = btctl_ctx.btiface->disable();
    if (status != BT_STATUS_SUCCESS) {
        ALOGD("Failed to disable Bluetooth\n");
        ret |= BTCTL_ERR_DISABLE_IFACE;
    }

    return ret;
}

static void adapter_properties_cb(bt_status_t status, int num_properties,
                                  bt_property_t *properties) {
    char addr_str[BT_ADDRESS_STR_LEN];
    int i;

    if (status != BT_STATUS_SUCCESS) {
        ALOGD("Failed to get adapter properties, error: %i\n", status);
        return;
    }

    ALOGD("\nAdapter properties\n");




    while (num_properties--) {
        bt_property_t prop = properties[num_properties];

        switch (prop.type) {
        case BT_PROPERTY_BDNAME:
            ALOGD("  Name: %s\n", (const char *) prop.val);
            break;

        case BT_PROPERTY_BDADDR:
            ALOGD("  Address: %s\n", ba2str((uint8_t *) prop.val,
                                            addr_str));
            break;

        case BT_PROPERTY_CLASS_OF_DEVICE:
            ALOGD("  Class of Device: 0x%x\n",
                  ((uint32_t *) prop.val)[0]);
            break;

        case BT_PROPERTY_TYPE_OF_DEVICE:
            switch (((bt_device_type_t *) prop.val)[0]) {
            case BT_DEVICE_DEVTYPE_BREDR:
                ALOGD("  Device Type: BR/EDR only\n");
                break;
            case BT_DEVICE_DEVTYPE_BLE:
                ALOGD("  Device Type: LE only\n");
                break;
            case BT_DEVICE_DEVTYPE_DUAL:
                ALOGD("  Device Type: DUAL MODE\n");
                break;
            }
            break;

        case BT_PROPERTY_ADAPTER_BONDED_DEVICES:
            i = prop.len / sizeof(bt_bdaddr_t);
            ALOGD("  Bonded devices: %u\n", i);
            while (i-- > 0) {
                uint8_t *addr = ((bt_bdaddr_t *) prop.val)[i].address;
                ALOGD("    Address: %s\n", ba2str(addr, addr_str));
            }
            break;

        default:
            /* Other properties not handled */
            break;
        }
    }


}




static void remote_device_properties_cb(bt_status_t status, bt_bdaddr_t *bd_addr,
                                        int num_properties, bt_property_t *properties)
{

    ALOGD("---remote_device_properties_cb---\n");

}
static void device_found_cb(int num_properties, bt_property_t *properties) {
    char addr_str[BT_ADDRESS_STR_LEN];
    btctl_bt_device_prop_t prop_data;

    int num_properties_arg = num_properties;
    bt_property_t *properties_arg = properties;

    memset(&prop_data, 0, sizeof(btctl_bt_device_prop_t));
    ALOGD("\nDevice found\n");


    while (num_properties--) {
        bt_property_t prop = properties[num_properties];

        switch (prop.type) {
        case BT_PROPERTY_BDNAME:
            ALOGD("  name: %s\n", (const char *) prop.val);
            ALOGD("\nsizeof prop.val name: %d   sizeof prop_data.bd_name.name %d\n", sizeof(prop.val),  sizeof(prop_data.bd_name.name));
            strncpy((char *)prop_data.bd_name.name , (const char *)prop.val, sizeof(prop_data.bd_name.name));
            break;

        case BT_PROPERTY_BDADDR:
            ALOGD("  addr: %s\n", ba2str((uint8_t *) prop.val,
                    addr_str));
            memcpy(prop_data.bd_addr.address, prop.val, sizeof(prop_data.bd_addr.address));
            break;

        case BT_PROPERTY_CLASS_OF_DEVICE:
            ALOGD("  class: 0x%x\n", ((uint32_t *) prop.val)[0]);
            prop_data.bd_class = ((uint32_t *) prop.val)[0];
            break;

        case BT_PROPERTY_TYPE_OF_DEVICE:
            prop_data.bd_type = ((bt_device_type_t *) prop.val)[0];
            break;

        case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
            ALOGD("  alias: %s\n", (const char *) prop.val);
            prop_data.bd_alias = prop.val;
            break;

        case BT_PROPERTY_REMOTE_RSSI:
            ALOGD("  rssi: %i\n", ((uint8_t *) prop.val)[0]);
            prop_data.bd_rssi = ((uint8_t *) prop.val)[0];
            break;

        case BT_PROPERTY_REMOTE_VERSION_INFO:
            ALOGD("  version info:\n");
            ALOGD("    version: %d\n",
                  ((bt_remote_version_t *) prop.val)->version);
            ALOGD("    subversion: %d\n",
                  ((bt_remote_version_t *) prop.val)->sub_ver);
            ALOGD("    manufacturer: %d\n",
                  ((bt_remote_version_t *) prop.val)->manufacturer);

            break;

        default:
            ALOGD("  Unknown property type:%i len:%i val:%p\n",
                  prop.type, prop.len, prop.val);
            break;
        }
    }
    enque_prop_data(&prop_data);

    CLIENT_CBACK(btctl_ctx.bt_callbacks, device_found_cb,
                 num_properties_arg, properties_arg);

}



static void discovery_state_changed_cb(bt_discovery_state_t state) {
    btctl_ctx.discovery_state = state;
    ALOGD("\nDiscovery state changed: %i\n", state);




    /* discovery stopped */
    if (state == BT_DISCOVERY_STOPPED ) {
        /* continue if stopped */
        wake_up_by = &discovery_state_changed_cb;
        kill(mypid, SIGCONT);     
        ALOGD("\nDiscovery state change CB -- signal sent\n");
    }
}








static void do_ssp_reply(const bt_bdaddr_t *bd_addr, bt_ssp_variant_t variant,
                         uint8_t accept, uint32_t passkey) {
    bt_status_t status = btctl_ctx.btiface->ssp_reply(bd_addr, variant, accept,
                                                      passkey);

    if (status != BT_STATUS_SUCCESS) {
        ALOGD("SSP Reply error: %u\n", status);
        return;
    }
}

static void pin_request_cb(bt_bdaddr_t *remote_bd_addr, bt_bdname_t *bd_name,
                           uint32_t cod) {

    /* ask user which PIN code is showed at remote device */
    memcpy(&btctl_ctx.r_bd_addr, remote_bd_addr, sizeof(btctl_ctx.r_bd_addr));

}

static void ssp_request_cb(bt_bdaddr_t *remote_bd_addr, bt_bdname_t *bd_name,
                           uint32_t cod, bt_ssp_variant_t pairing_variant,
                           uint32_t pass_key) {

    if (pairing_variant == BT_SSP_VARIANT_CONSENT) {
        /* we need to ask to user if he wants to bond */
        memcpy(&btctl_ctx.r_bd_addr, remote_bd_addr, sizeof(btctl_ctx.r_bd_addr));

    } else {
        char addr_str[BT_ADDRESS_STR_LEN];
        const char *action = "Enter";

        if (pairing_variant == BT_SSP_VARIANT_PASSKEY_CONFIRMATION) {
            action = "Confirm";
            do_ssp_reply(remote_bd_addr, pairing_variant, true, pass_key);
        }

        ALOGD("Remote addr: %s\n",
              ba2str(remote_bd_addr->address, addr_str));
        ALOGD("%s passkey on peer device: %d\n", action, pass_key);
    }
}
static void acl_state_changed_cb(bt_status_t status, bt_bdaddr_t *remote_bd_addr,
                                 bt_acl_state_t state) {
    ALOGD("-----acl_state_changed_cb  \n");
    //wake_up_by = acl_state_changed_cb ;
    // kill(mypid, SIGCONT);
}



static void bond_state_changed_cb(bt_status_t status, bt_bdaddr_t *bda,
                                  bt_bond_state_t state) {
    char addr_str[BT_ADDRESS_STR_LEN];
    char state_str[32] = {0};

    if (status != BT_STATUS_SUCCESS) {
        ALOGD("Failed to change bond state, status: %d\n", status);
        return;
    }

    switch (state) {
    case BT_BOND_STATE_NONE:
        strcpy(state_str, "BT_BOND_STATE_NONE");
        break;

    case BT_BOND_STATE_BONDING:
        strcpy(state_str, "BT_BOND_STATE_BONDING");
        break;

    case BT_BOND_STATE_BONDED:
        strcpy(state_str, "BT_BOND_STATE_BONDED");
        break;

    default:
        sprintf(state_str, "Unknown (%d)", state);
        break;
    }

    ALOGD("Bond state changed for device %s: %s\n",
          ba2str(bda->address, addr_str), state_str);
}




static void register_client_cb(int status, int client_if,
                               bt_uuid_t *app_uuid) {

    if (status == BT_STATUS_SUCCESS) {
        ALOGD("Registered!, client_if: %d\n", client_if);

        btctl_ctx.client_if = client_if;
        btctl_ctx.client_registered = true;


    }


    CLIENT_CBACK(btctl_ctx.client, register_client_cb,
                 status, client_if, app_uuid);


}

static void scan_result_cb(bt_bdaddr_t *bda, int rssi, uint8_t *adv_data) {

    CLIENT_CBACK(btctl_ctx.client, scan_result_cb,
                 bda, rssi, adv_data);

}

static void open_cb(int conn_id, int status, int client_if,
                    bt_bdaddr_t *bda) {

    char addr_str[BT_ADDRESS_STR_LEN];
    pConnection conn;

    if (status == 0) {
        ALOGD("Connected to device %s, conn_id: %d, client_if: %d\n",
              ba2str(bda->address, addr_str), conn_id, client_if);
        conn = (pConnection)malloc(sizeof(Connection));
        conn->conn_id = conn_id;
        conn->svcs_size = 0;
        memcpy(&conn->bd_addr, bda, sizeof(conn->bd_addr));
        ListAddTail(&connection_list, conn);

    }

    CLIENT_CBACK(btctl_ctx.client, open_cb,
                 conn_id, status, client_if, bda);


    wake_up_by = &open_cb;
    kill(mypid, SIGCONT);

}





static void close_cb(int conn_id, int status, int client_if,
                     bt_bdaddr_t *bda) {
    char addr_str[BT_ADDRESS_STR_LEN];
    ALOGD("Disconnected from device conn_id: %d, addr: %s, client_if: %d, "
          "status: %d\n", conn_id,ba2str(bda->address, addr_str),
          client_if, status);

    ListRemoveConnection(&connection_list,conn_id);
    CLIENT_CBACK(btctl_ctx.client, close_cb,
                 conn_id, status, client_if, bda);


}

/* called when search has finished */
static void search_complete_cb(int conn_id, int status) {

    ALOGD("Search complete, status: %u\n", status);

    CLIENT_CBACK(btctl_ctx.client, search_complete_cb,
                 conn_id, status);
    /* contiue at btctl_get_service*/
    kill(mypid, SIGCONT);

}

/* called for each search result */
static void search_result_cb(int conn_id, btgatt_srvc_id_t *srvc_id) {
    char uuid_str[UUID128_STR_LEN] = {0};
    pConnection connection;
    bt_uuid_t uuid;


    connection = ListFindConnectionByID(&connection_list, conn_id);

    if (connection != NULL) {



        if (connection->svcs_size < MAX_SVCS_SIZE) {
            /* srvc_id value is replaced each time, so we need to copy it */
            memcpy(&connection->svcs[connection->svcs_size].svc_id, srvc_id, sizeof(btgatt_srvc_id_t));
            connection->svcs_size++;
        }

        ALOGD("ID:%i %s UUID: %s instance:%i\n", connection->svcs_size - 1,
              srvc_id->is_primary ? "Primary" : "Secondary",
              uuid2str(&srvc_id->id.uuid, uuid_str), srvc_id->id.inst_id);


    }

    CLIENT_CBACK(btctl_ctx.client, search_result_cb,
                 conn_id, srvc_id);


}



static void get_characteristic_cb(int conn_id, int status, btgatt_srvc_id_t *srvc_id,
                                  btgatt_char_id_t *char_id, int char_prop) {
    bt_status_t ret;
    char uuid_str[UUID128_STR_LEN] = {0};
    int svc_id;
    service_info_t *svc_info;
    pConnection connection;



    if (status != 0) {
        if (status == 0x85) { /* it's not really an error, just finished */
            ALOGD("List characteristics finished\n");
            goto exit;
        }

        ALOGD("List characteristics finished, status: %i %s\n", status,
              atterror2str(status));
        goto exit;
    }

    connection = ListFindConnectionByID(&connection_list, conn_id);
    if (connection == NULL)
        goto exit;

    svc_id = find_svc(connection, srvc_id);

    if (svc_id < 0) {
        ALOGD("Received invalid characteristic (service inexistent)\n");
        goto exit;
    }
    svc_info = &connection->svcs[svc_id];

    ALOGD("ID:%i UUID: %s instance:%i properties:0x%x\n",
          svc_info->char_count, uuid2str(&char_id->uuid, uuid_str),
          char_id->inst_id, char_prop);

    if (svc_info->char_count == svc_info->chars_buf_size) {
        int i;

        svc_info->chars_buf_size += MAX_CHARS_SIZE;
        ALOGD("realloc\n");
        svc_info->chars_buf = realloc(svc_info->chars_buf, sizeof(char_info_t) *
                                      svc_info->chars_buf_size);

        for (i = svc_info->char_count; i < svc_info->chars_buf_size; i++) {
            svc_info->chars_buf[i].descrs = NULL;
            svc_info->chars_buf[i].descr_count = 0;
        }
    }

    /* copy characteristic data */
    memcpy(&svc_info->chars_buf[svc_info->char_count].char_id, char_id,
           sizeof(btgatt_char_id_t));

    svc_info->chars_buf[svc_info->char_count].descr_count = 0;

    svc_info->char_count++;

    CLIENT_CBACK(btctl_ctx.client,get_characteristic_cb,
                 conn_id, status, srvc_id, char_id, char_prop);
    /* get next characteristic */
    ret = btctl_ctx.gattiface->client->get_characteristic(conn_id, srvc_id, char_id);

    if (ret != BT_STATUS_SUCCESS) {
        ALOGD("Failed to list characteristics\n");

    }

    usleep(100000);
    return;


    exit:
    CLIENT_CBACK(btctl_ctx.client,get_characteristic_cb,
                 conn_id, status, srvc_id, char_id, char_prop);

    wake_up_by = &get_characteristic_cb;
    kill(mypid, SIGCONT);


}



static void get_descriptor_cb(int conn_id, int status, btgatt_srvc_id_t *srvc_id,
                              btgatt_char_id_t *char_id, bt_uuid_t *descr_id) {
    bt_status_t ret;
    char uuid_str[UUID128_STR_LEN] = {0};
    int svc_id, ch_id;
    service_info_t *svc_info = NULL;
    char_info_t *char_info = NULL;
    pConnection connection;

    if (status != 0) {
        if (status == 0x85) { /* it's not really an error, just finished */
            ALOGD("List characteristics descriptors finished\n");
            goto exit;
        }

        ALOGD("List characteristic descriptors finished, status: %i %s\n",
              status, atterror2str(status));
        goto exit;
    }

    connection = ListFindConnectionByID(&connection_list, conn_id);
    if ( connection == NULL) {
        ALOGD("Invalid connection\n");
        goto exit;

    }

    svc_id = find_svc(connection, srvc_id);
    if (svc_id < 0) {
        ALOGD("Received invalid descriptor (service inexistent)\n");
        goto exit;
    }
    svc_info = &connection->svcs[svc_id];


    ch_id = find_char(svc_info, char_id);
    if (ch_id < 0) {
        ALOGD("Received invalid descriptor (characteristic inexistent)\n");
        goto exit;
    }
    char_info = &svc_info->chars_buf[ch_id];

    ALOGD("ID:%i UUID: %s\n", char_info->descr_count,
          uuid2str(descr_id, uuid_str));

    if (char_info->descr_count == 255) {
        ALOGD("Max descriptors overflow error\n");
        goto exit;
    }

    char_info->descr_count++;
    char_info->descrs = realloc(char_info->descrs, char_info->descr_count *
                                sizeof(char_info->descrs[0]));

    /* copy descriptor data */
    memcpy(&char_info->descrs[char_info->descr_count - 1], descr_id,
           sizeof(*descr_id));

    CLIENT_CBACK(btctl_ctx.client, get_descriptor_cb,
                 conn_id, status, srvc_id, char_id, descr_id);


    /* get next descriptor */
    ret = btctl_ctx.gattiface->client->get_descriptor(conn_id, srvc_id, char_id,
                                                      descr_id);
    if (ret != BT_STATUS_SUCCESS) {
        ALOGD("Failed to list descriptors\n");

    }


    wake_up_by = &get_descriptor_cb;
    kill(mypid, SIGCONT);
    return;

    exit:
    CLIENT_CBACK(btctl_ctx.client, get_descriptor_cb,
                 conn_id, status, srvc_id, char_id, descr_id);
    wake_up_by = &get_descriptor_cb;
    kill(mypid, SIGCONT);


}

static void get_included_service_cb(int conn_id, int status, btgatt_srvc_id_t *srvc_id,
                                    btgatt_srvc_id_t *incl_srvc_id) {
    bt_status_t ret;

    if (status == 0) {

        ret = btctl_ctx.gattiface->client->get_included_service(conn_id, srvc_id,
                                                                incl_srvc_id);
        usleep(100000);
        if (ret != BT_STATUS_SUCCESS) {
            ALOGD("Failed to list included services\n");
            return;
        }
    } else
        ALOGD("Included finished, status: %i\n", status);

    CLIENT_CBACK(btctl_ctx.client,get_included_service_cb,
                 conn_id, status, srvc_id, incl_srvc_id);
}

static void register_for_notification_cb(int conn_id, int registered, int status,
                                         btgatt_srvc_id_t *srvc_id,
                                         btgatt_char_id_t *char_id) {


    CLIENT_CBACK(btctl_ctx.client, register_for_notification_cb,
                 conn_id, registered, status, srvc_id, char_id);
}


static void notify_cb(int conn_id, btgatt_notify_params_t *p_data) {




    CLIENT_CBACK(btctl_ctx.client, notify_cb,
                 conn_id, p_data);

}


static void read_characteristic_cb(int conn_id, int status,
                                   btgatt_read_params_t *p_data) {


    CLIENT_CBACK(btctl_ctx.client, read_characteristic_cb,
                 conn_id, status, p_data);
    

}

static void write_characteristic_cb(int conn_id, int status,
                                    btgatt_write_params_t *p_data) {
    CLIENT_CBACK(btctl_ctx.client, write_characteristic_cb,
                 conn_id, status, p_data);
    
    wake_up_by = &write_characteristic_cb;
    kill(mypid, SIGCONT);

}


static void read_descriptor_cb(int conn_id, int status, btgatt_read_params_t *p_data) {





    CLIENT_CBACK(btctl_ctx.client, read_descriptor_cb,
                 conn_id, status, p_data);
}


static void write_descriptor_cb(int conn_id, int status,
                                btgatt_write_params_t *p_data) {

    CLIENT_CBACK(btctl_ctx.client, write_descriptor_cb,
                 conn_id, status, p_data);
    wake_up_by = &write_descriptor_cb;
    kill(mypid, SIGCONT);

}

static void read_remote_rssi_cb(int client_if, bt_bdaddr_t *bda, int rssi,
                                int status) {
    char addr_str[BT_ADDRESS_STR_LEN];

    CLIENT_CBACK(btctl_ctx.client, read_remote_rssi_cb,
                 client_if, bda ,  rssi,  status);


    ALOGE("Address: %s RSSI: %i\n", ba2str(bda->address, addr_str), rssi);
}



bt_status_t btctl_discovery_start_blocked()
{
    bt_status_t status;
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = catcher;
    sigaction(SIGCONT, &sigact, NULL);

    mypid = getpid();
    if (btctl_ctx.adapter_state != BT_STATE_ON) {
        ALOGD("Unable to start discovery: Adapter is down\n");
        return BT_STATUS_NOT_READY;
    }
    if (btctl_ctx.discovery_state == BT_DISCOVERY_STARTED) {
        ALOGD("Discovery is already running\n");
        return BT_STATUS_BUSY;
    }

    if (! bt_device_list_empty() )
        init_bt_device_list (free_bt_device_list());

    status = btctl_ctx.btiface->start_discovery();   

    /* wait until discovery_state_changed_cb sends a signal */
    pause();
    ALOGD("Wake up btctl_discovery_start_blocked\n");
    while ( wake_up_by != &discovery_state_changed_cb) {
        ALOGD("Sleep btctl_discovery_start_blocked\n");
        pause();
    }

    wake_up_by = 0;



    return status;
}

bt_status_t btctl_discovery_start_unblocked()
{
    bt_status_t status;
    if (btctl_ctx.adapter_state != BT_STATE_ON) {
        ALOGD("Unable to start discovery: Adapter is down\n");
        return BT_STATUS_NOT_READY;
    }
    if (btctl_ctx.discovery_state == BT_DISCOVERY_STARTED) {
        ALOGD("Discovery is already running\n");
        return BT_STATUS_BUSY;
    }

    if (! bt_device_list_empty() )
        init_bt_device_list (free_bt_device_list());

    status = btctl_ctx.btiface->start_discovery();        
    return status;
}

bt_status_t btctl_discovery_stop()
{
    bt_status_t status;

    if (btctl_ctx.discovery_state == BT_DISCOVERY_STOPPED) {

        return BT_STATUS_DONE;
    }

    status = btctl_ctx.btiface->cancel_discovery();
    return status;
}


void btctl_print_discovered_devices()
{
    int i = 0,count = btctl_ctx.dicovered_device_list.count;

    p_btctl_bt_device_prop_t *dev_list;

    for (dev_list = btctl_ctx.dicovered_device_list.p_bt_device_list, i = 0; i < count; i++) {
        ALOGD("\nDevice found\n");
        print_bt_device_prop( dev_list[i] );

    }
}



bt_status_t btctl_connect(bt_bdaddr_t *bdaddr) {
    bt_status_t status;
    int ret;
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = catcher;
    sigaction(SIGCONT, &sigact, NULL);

    if (btctl_ctx.gattiface == NULL) {
        ALOGD("Unable to BLE connect: GATT interface not available\n");
        return BT_STATUS_FAIL;
    }

    if (btctl_ctx.adapter_state != BT_STATE_ON) {
        ALOGD("Unable to connect: Adapter is down\n");
        return BT_STATUS_NOT_READY;
    }

    if (btctl_ctx.client_registered == false) {
        ALOGD("Unable to connect: We're not registered as GATT client\n");
        return BT_STATUS_FAIL;
    }


    status = btctl_ctx.gattiface->client->connect(btctl_ctx.client_if, bdaddr , true);


    pause();
    ALOGD("Wake up btctl_connect wake_up_by %p\n", wake_up_by);

    while ( wake_up_by != &open_cb) {
        ALOGD("Sleep btctl_connect\n");
        pause();
    }
    wake_up_by = 0;
    sleep(3);

    if (status != BT_STATUS_SUCCESS) {
        ALOGD("Failed to connect, status: %d\n", status);
        return BT_STATUS_FAIL;
    }

    return BT_STATUS_SUCCESS;
}


bt_status_t btctl_disconnect(pConnection connection ) {
    bt_status_t status;
    char addr_str[BT_ADDRESS_STR_LEN];
    
    if (connection->conn_id <= 0) {
        ALOGE("Device not connected\n");
        return BT_STATUS_NOT_READY;
    }

    status = btctl_ctx.gattiface->client->disconnect(btctl_ctx.client_if,  &connection->bd_addr,
                                             connection->conn_id);
    if (status != BT_STATUS_SUCCESS) {
        ALOGE("Failed to disconnect, status: %d\n", status);
        return BT_STATUS_FAIL; 
    }
    return BT_STATUS_SUCCESS;
}


bt_status_t btctl_get_service(int conn_id, bt_uuid_t *p_uuid) {
    bt_status_t status;
    pConnection connection;

    ALOGD("btctl_get_service\n");
    if (conn_id <= 0) {
        ALOGD("Not connected\n");
        return BT_STATUS_FAIL;
    }

    if (btctl_ctx.gattiface == NULL) {
        ALOGD("Unable to BLE search-svc: GATT interface not avaiable\n");
        return BT_STATUS_NOT_READY;
    }

    ListClearSvcCacheConnID(&connection_list, conn_id);


    status = btctl_ctx.gattiface->client->search_service(conn_id, p_uuid);
    pause();
    if (status != BT_STATUS_SUCCESS) {
        ALOGD("Failed to search services\n");

    }
    usleep(1000000);
    return status;
}



bt_status_t btctl_get_characteristic(pConnection connection, int id)
{
    bt_status_t status;
    service_info_t *svc_info;
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = catcher;
    sigaction(SIGCONT, &sigact, NULL);



    if (id < 0 || id >= connection->svcs_size) {
        ALOGE("Invalid serviceID: %i need to be between 0 and %i\n", id,
              connection->svcs_size - 1);
        return BT_STATUS_FAIL;
    }

    if (connection->svcs[id].chars_buf == NULL) {
        int i;
        connection->svcs[id].chars_buf_size = MAX_CHARS_SIZE;
        connection->svcs[id].chars_buf = malloc(sizeof(char_info_t) *
                                                connection->svcs[id].chars_buf_size);

        for (i = connection->svcs[id].char_count; i < connection->svcs[id].chars_buf_size; i++) {
            connection->svcs[id].chars_buf[i].descrs = NULL;
            connection->svcs[id].chars_buf[i].descr_count = 0;
        }
    } else if (connection->svcs[id].char_count > 0)
        connection->svcs[id].char_count = 0;

    status = btctl_ctx.gattiface->client->get_characteristic(connection->conn_id,
                                                             &connection->svcs[id].svc_id, NULL);

    pause();
    while ( wake_up_by != &get_characteristic_cb) {
        ALOGD("Sleep btctl_connect\n");
        pause();
    }
    wake_up_by = 0;
    
    
    if (status != BT_STATUS_SUCCESS) {
        ALOGE("Failed to list characteristics\n");
        return status;
    }

    return status;
}


/* return index , negative is invalid */
int btctl_search_service_uuid_and_get_index(pConnection connection, bt_uuid_t *uuid)
{
    int i;
    btgatt_srvc_id_t *p_srvc_id;
    // bt_uuid_t uuid;
    char uuid_str[UUID128_STR_LEN] = {0};

    if (connection == NULL) {
        ALOGE("Invalid connection\n");
        return -1;

    }

    for ( i = 0; i < connection->svcs_size; i++) {
        p_srvc_id =  &connection->svcs[i].svc_id;
        if ( ! btctl_util_uuidcmp(uuid, &p_srvc_id->id.uuid) ) {

            ALOGE("----->found _uuid %s, conn id: %d, svc index %d\n",uuid2str(uuid,  uuid_str), connection->conn_id, i);
            return i;
        }

    }
    ALOGE("Can't find the UUID %s from conn ID %d \n",  uuid2str(uuid,  uuid_str), connection->conn_id); 
    return -1;

}


/* return index , negative is invalid */
int btctl_search_characteristic_uuid_and_get_index(pConnection connection, int svc_id, bt_uuid_t *uuid)
{
    int i;
    btgatt_srvc_id_t *p_srvc_id;
    service_info_t *svc_info;
    char uuid_str[UUID128_STR_LEN] = {0};

    if (connection == NULL) {
        ALOGE("Invalid connection\n");
        return -1;

    }

    svc_info = &connection->svcs[svc_id];
    if (svc_info == NULL) {
        ALOGE("Invalid svc_info\n");
        return -1;
    }

    for ( i = 0; i < svc_info->char_count; i++) {
        ALOGE("ID:%i UUID: %s instance:%i n",
              svc_info->char_count, uuid2str(&svc_info->chars_buf[i].char_id.uuid, uuid_str),
              svc_info->chars_buf[i].char_id.inst_id);

        if ( ! btctl_util_uuidcmp(uuid, &svc_info->chars_buf[i].char_id.uuid)) {

            ALOGE("----->found _uuid %s, conn id: %d, char index %d\n",uuid2str(uuid,  uuid_str), connection->conn_id, i);
            return i;
        }

    }
    ALOGE("Can't find the UUID %s from conn ID %d \n",  uuid2str(uuid,  uuid_str), connection->conn_id); 
    return -1;
}

int btctl_search_svc_n_char_uuid_and_get_index(pConnection connection, int *service_index, int *char_index, bt_uuid_t *svc_uuid,bt_uuid_t *char_uuid )
{

    *service_index = -1;
    *char_index = -1;


    *service_index =  btctl_search_service_uuid_and_get_index(connection, svc_uuid);

    if ( *service_index < 0) {

        return -1;
    }
    btctl_get_characteristic(connection, *service_index);

    *char_index = btctl_search_characteristic_uuid_and_get_index(connection, *service_index, char_uuid);  

    if ( *char_index < 0) {

        return -1;
    }
    return 0;

}



void btctl_get_descriptor(pConnection connection, int svc_id, int char_id) {
    bt_status_t status;
    service_info_t *svc_info;
    char_info_t *char_info;

    if (svc_id < 0 || svc_id >= connection->svcs_size) {
        ALOGE("Invalid serviceID: %i need to be between 0 and %i\n", svc_id,
              connection->svcs_size - 1);
        return;
    }

    svc_info = &connection->svcs[svc_id];
    if (char_id < 0 || char_id >= svc_info->char_count) {
        ALOGE("Invalid characteristicID, try to run characteristics "
              "command.\n");
        return;
    }

    char_info = &svc_info->chars_buf[char_id];
    char_info->descr_count = 0;
    /* get first descriptor */
    status = btctl_ctx.gattiface->client->get_descriptor(connection->conn_id, &svc_info->svc_id,
                                                         &char_info->char_id, NULL);

    while (1 ) {
        pause();
        if (wake_up_by == get_descriptor_cb)
            break;

    }
    wake_up_by = 0;

    if (status != BT_STATUS_SUCCESS) {
        ALOGE("Failed to list characteristic descriptors\n");

    }

}

void btctl_reg_notification(pConnection connection, int svc_id, int char_id) {
    bt_status_t status;
    service_info_t *svc_info;
    char_info_t *char_info;

    if (svc_id < 0 || svc_id >= connection->svcs_size) {
        ALOGE("Invalid serviceID: %i need to be between 0 and %i\n", svc_id,
              connection->svcs_size - 1);
        return;
    }

    svc_info = &connection->svcs[svc_id];
    if (char_id < 0 || char_id >= svc_info->char_count) {
        ALOGE("Invalid characteristicID, try to run characteristics "
              "command\n");
        return;
    }

    char_info = &svc_info->chars_buf[char_id];
    status = btctl_ctx.gattiface->client->register_for_notification(btctl_ctx.client_if,
                                                                    &connection->bd_addr,
                                                                    &svc_info->svc_id,
                                                                    &char_info->char_id);
    if (status != BT_STATUS_SUCCESS)
        ALOGE("Failed to register for characteristic "
              "notification/indication\n");

    usleep(100000);
}


void btctl_write_cmd_char(pConnection connection, int svc_id, int char_id,  int auth, char *new_value, int new_value_len)
{
    bt_status_t status;
    service_info_t *svc_info;
    char_info_t *char_info;
    char *saveptr = NULL, *tok;
    int params = 0;
    int write_type = 1;

    svc_info = &connection->svcs[svc_id];

    char_info = &svc_info->chars_buf[char_id];

    status = btctl_ctx.gattiface->client->write_characteristic(connection->conn_id,
                                                               &svc_info->svc_id,
                                                               &char_info->char_id,
                                                               write_type,
                                                               new_value_len,
                                                               auth, new_value);
    if (status != BT_STATUS_SUCCESS) {
        ALOGE("Failed to write characteristic\n");

    }
    usleep(100000);

}

void btctl_write_req_char(pConnection connection, int svc_id, int char_id,  int auth, char *new_value, int new_value_len)
{
    bt_status_t status;
    service_info_t *svc_info;
    char_info_t *char_info;
    char *saveptr = NULL, *tok;
    int params = 0;
    int write_type = 2;

    svc_info = &connection->svcs[svc_id];

    char_info = &svc_info->chars_buf[char_id];

    status = btctl_ctx.gattiface->client->write_characteristic(connection->conn_id,
                                                               &svc_info->svc_id,
                                                               &char_info->char_id,
                                                               write_type,
                                                               new_value_len,
                                                               auth, new_value);
    if (status != BT_STATUS_SUCCESS) {
        ALOGE("Failed to write characteristic\n");

    }
    
    pause();
    ALOGD("Wake up btctl_connect wake_up_by %p\n", wake_up_by);

    while ( wake_up_by != &write_characteristic_cb) {
        ALOGD("Sleep btctl_connect\n");
        pause();
    }
    wake_up_by = 0;
}

void btctl_write_cmd_descriptor(pConnection connection, int svc_id, int char_id,  int desc_id , int auth, char *new_value, int new_value_len )
{
    bt_status_t status;
    service_info_t *svc_info;
    char_info_t *char_info;
    bt_uuid_t *descr_uuid;
    char *saveptr = NULL, *tok = NULL;
    int params = 0;
    int write_type = 1;/* Write No Request */


    svc_info = &connection->svcs[svc_id];

    char_info = &svc_info->chars_buf[char_id];

    descr_uuid = &char_info->descrs[desc_id];

    ALOGE("new_value_len %d\n", new_value_len);
    status = btctl_ctx.gattiface->client->write_descriptor(connection->conn_id, &svc_info->svc_id,
                                                           &char_info->char_id,
                                                           descr_uuid,
                                                           write_type ,
                                                           new_value_len, auth,
                                                           new_value);


    if (status != BT_STATUS_SUCCESS) {
        ALOGE("Failed to write characteristic\n");

    }
    usleep(100000);
}

void btctl_write_req_descriptor(pConnection connection, int svc_id, int char_id,  int desc_id , int auth, char *new_value, int new_value_len )
{
    bt_status_t status;
    service_info_t *svc_info;
    char_info_t *char_info;
    bt_uuid_t *descr_uuid;
    char *saveptr = NULL, *tok = NULL;
    int params = 0;
    int write_type = 2;/* Write Request */


    svc_info = &connection->svcs[svc_id];

    char_info = &svc_info->chars_buf[char_id];

    descr_uuid = &char_info->descrs[desc_id];

    ALOGE("new_value_len %d\n", new_value_len);
    status = btctl_ctx.gattiface->client->write_descriptor(connection->conn_id, &svc_info->svc_id,
                                                           &char_info->char_id,
                                                           descr_uuid,
                                                           write_type ,
                                                           new_value_len, auth,
                                                           new_value);


    if (status != BT_STATUS_SUCCESS) {
        ALOGE("Failed to write characteristic\n");

    }
    
    pause();
    ALOGD("Wake up btctl_connect wake_up_by %p\n", wake_up_by);
    while ( wake_up_by != &write_descriptor_cb) {
        ALOGD("Sleep btctl_connect\n");
        pause();
    }
    wake_up_by = 0;

}

pConnection btctl_list_get_head_connection () {
    pNode node = ListGetHead(&connection_list);
    return( (node == NULL) ? NULL : node->data);
}

pConnection btctl_list_get_tail_connection () {
    pNode node = ListGetTail(&connection_list);
    return( (node == NULL) ? NULL : node->data);
}

pConnection btctl_list_get_next_connection (pConnection connection) {
    pNode node = ListGetNext(connection->node);
    return( (node == NULL) ? NULL : node->data);
}

pConnection btctl_list_find_connection_by_connid(int conn_id) {

    return( ListFindConnectionByID(&connection_list, conn_id));
}

void btctl_list_print_all_connection() {
    ListPrintAllConnection(&connection_list);
}

/* return 0 if both uuid are same */
int btctl_util_uuidcmp(bt_uuid_t *uuid_dst, bt_uuid_t *uuid_src) {

    return( memcmp(uuid_dst->uu, uuid_src->uu, 16 ) );

}

/* GATT client callbacks */
static const btgatt_client_callbacks_t btctl_gattccbs = {
    register_client_cb, /* Called after client is registered */
    scan_result_cb, /* called every time an advertising report is seen */
    open_cb, /* called every time a connection attempt finishes */
    close_cb, /* called every time a connection attempt finishes */
    search_complete_cb, /* search_complete_callback */
    search_result_cb, /* search_result_callback */
    get_characteristic_cb, /* get_characteristic_callback */
    get_descriptor_cb, /* get_descriptor_callback */
    get_included_service_cb, /* get_included_service_callback */
    register_for_notification_cb, /* register_for_notification_callback */
    notify_cb, /* notify_callback */
    read_characteristic_cb, /* read_characteristic_callback */
    write_characteristic_cb, /* write_characteristic_callback */
    read_descriptor_cb, /* read_descriptor_callback */
    write_descriptor_cb, /* write_descriptor_callback */
    NULL, /* execute_write_callback */
    read_remote_rssi_cb, /* read_remote_rssi_callback */
};

/* GATT interface callbacks */
static btgatt_callbacks_t gattcbs = {
    sizeof(btgatt_callbacks_t),
    //&gattccbs,/* btgatt_client_callbacks_t */
    NULL,/* btgatt_client_callbacks_t */
    NULL  /* btgatt_server_callbacks_t */
};

/* This callback is used by the thread that handles Bluetooth interface (btif)
 * to send events for its users. At the moment there are two events defined:
 *
 *     ASSOCIATE_JVM: sinalizes the btif handler thread is ready;
 *     DISASSOCIATE_JVM: sinalizes the btif handler thread is about to exit.
 *
 * This events are used by the JNI to know when the btif handler thread should
 * be associated or dessociated with the JVM
 */
static void thread_event_cb(bt_cb_thread_evt event) {
    ALOGD("\nBluetooth interface %s\n",
          event == ASSOCIATE_JVM ? "ready" : "finished");
    if (event == ASSOCIATE_JVM) {
        btctl_ctx.btiface_initialized = 1;

        btctl_ctx.gattiface = btctl_ctx.btiface->get_profile_interface(BT_PROFILE_GATT_ID);
        if (btctl_ctx.gattiface != NULL) {
            bt_status_t status = btctl_ctx.gattiface->init(&gattcbs);
            if (status != BT_STATUS_SUCCESS) {
                ALOGD("Failed to initialize Bluetooth GATT interface, "
                      "status: %d\n", status);
                btctl_ctx.gattiface = NULL;
            } else
                btctl_ctx.gattiface_initialized = 1;
        } else
            ALOGD("Failed to get Bluetooth GATT Interface\n");
    } else
        btctl_ctx.btiface_initialized = 0;
}



/* Bluetooth interface callbacks */
static bt_callbacks_t btcbs = {
    sizeof(bt_callbacks_t),
    adapter_state_change_cb, /* Called every time the adapter state changes */
    adapter_properties_cb, /* adapter_properties_callback */
    remote_device_properties_cb, /* remote_device_properties_callback */
    device_found_cb, /* Called for every device found */
    discovery_state_changed_cb, /* Called every time the discovery state changes */
    pin_request_cb, /* pin_request_callback */
    ssp_request_cb, /* ssp_request_callback */
    bond_state_changed_cb, /* bond_state_changed_callback */
    acl_state_changed_cb, /* acl_state_changed_callback */
    thread_event_cb, /* Called when the JVM is associated / dissociated */
    NULL, /* dut_mode_recv_callback */
    NULL, /* le_test_mode_callback */
};


/* Initialize the Bluetooth stack */
p_libbtctl_ctx_t btctl_init(const btgatt_client_callbacks_t *gatt_callbacks , bt_callbacks_t *bt_callbacks) {
    int status;
    hw_module_t *module;
    hw_device_t *hwdev;
    bluetooth_device_t *btdev;

    btctl_ctx.btiface_initialized = 0;
    btctl_ctx.quit = 0;
    btctl_ctx.adapter_state = BT_STATE_OFF; /* The adapter is OFF in the beginning */


    //gattcbs.client = gatt_callbacks;
    btctl_ctx.client = gatt_callbacks;
    gattcbs.client = &btctl_gattccbs;

    btctl_ctx.bt_callbacks= bt_callbacks;
    ListInit(&connection_list);

    /* Get the Bluetooth module from libhardware */
    status = hw_get_module(BT_STACK_MODULE_ID, (hw_module_t const**) &module);
    if (status < 0) {
        errno = status;
        err(1, "Failed to get the Bluetooth module");
    }
    ALOGD("Bluetooth stack infomation:\n");
    ALOGD("    id = %s\n", module->id);
    ALOGD("    name = %s\n", module->name);
    ALOGD("    author = %s\n", module->author);
    ALOGD("    HAL API version = %d\n", module->hal_api_version);

    /* Get the Bluetooth hardware device */
    status = module->methods->open(module, BT_STACK_MODULE_ID, &hwdev);
    if (status < 0) {
        errno = status;
        err(2, "Failed to get the Bluetooth hardware device");
    }
    ALOGD("Bluetooth device infomation:\n");
    ALOGD("    API version = %d\n", hwdev->version);

    /* Get the Bluetooth interface */
    btdev = (bluetooth_device_t *) hwdev;
    btctl_ctx.btiface = btdev->get_bluetooth_interface();
    if (btctl_ctx.btiface == NULL)
        err(3, "Failed to get the Bluetooth interface");

    /* Init the Bluetooth interface, setting a callback for each operation */
    status = btctl_ctx.btiface->init(&btcbs);
    if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
        err(4, "Failed to initialize the Bluetooth interface");

    init_bt_device_list(DEV_LIST_SIZE);

    return (&btctl_ctx);
}

