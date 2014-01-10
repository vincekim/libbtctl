#ifndef __LIBBTCTL_H
#define __LIBBTCTL_H

#define MAX_LINE_SIZE 64
#define MAX_SVCS_SIZE 128
#define MAX_CHARS_SIZE 8

/* AD types */
#define AD_FLAGS              0x01
#define AD_UUID16_SOME        0x02
#define AD_UUID16_ALL         0x03
#define AD_UUID128_SOME       0x06
#define AD_UUID128_ALL        0x07
#define AD_NAME_SHORT         0x08
#define AD_NAME_COMPLETE      0x09
#define AD_TX_POWER           0x0a
#define AD_SLAVE_CONN_INT     0x12
#define AD_SOLICIT_UUID16     0x14
#define AD_SOLICIT_UUID128    0x15
#define AD_SERVICE_DATA       0x16
#define AD_PUBLIC_ADDRESS     0x17
#define AD_RANDOM_ADDRESS     0x18
#define AD_GAP_APPEARANCE     0x19
#define AD_ADV_INTERVAL       0x1a
#define AD_MANUFACTURER_DATA  0xff

#define BTCTL_BT_STATE_ON         1<<0
#define BTCTL_BT_STATE_OFF        1<<1
#define BTCTL_ERR_BT_ENABLE       1<<2
#define BTCTL_ERR_UNREG_CLIENT    1<<3
#define BTCTL_ERR_DISABLE_IFACE   1<<4

typedef struct char_info {
    btgatt_char_id_t char_id;
    bt_uuid_t *descrs;
    uint8_t descr_count;
} char_info_t;

typedef struct service_info {
    btgatt_srvc_id_t svc_id;
    char_info_t *chars_buf;
    uint8_t chars_buf_size;
    uint8_t char_count;
} service_info_t;

typedef struct btctl_bt_device_prop {
    bt_bdname_t bd_name;
    bt_bdaddr_t bd_addr;
    uint32_t bd_class;
    bt_device_type_t bd_type;
    char *bd_alias;
    uint8_t bd_rssi;    
} btctl_bt_device_prop_t, *p_btctl_dev_prop_t;

typedef struct btctl_bt_device_list_hdr {
    p_btctl_dev_prop_t *p_bt_device_list;
    uint32_t count;
    uint32_t max;
} btctl_bt_device_list_hdr_t;

typedef struct Connection_tag {
    int conn_id;
    bt_bdaddr_t bd_addr;
    int connection_status_flag;
    /* When searching for services, we receive at search_result_cb a pointer
     * for btgatt_srvc_id_t. But its value is replaced each time. So one option
     * is to store these values and show a simpler ID to user.
     *
     * This static list limits the number of services that we can store, but it
     * is simpler than using linked list.
     */
    service_info_t svcs[MAX_SVCS_SIZE];
    int svcs_size;
    void *node;   
} Connection, *pConnection;


/* Data that have to be acessable by the callbacks */
typedef struct libbtctl_ctx {
    const bt_interface_t *btiface;
    uint8_t btiface_initialized;
    const btgatt_interface_t *gattiface;
    uint8_t gattiface_initialized;
    uint8_t quit;
    bt_state_t adapter_state; /* The adapter is always OFF in the beginning */
    bt_discovery_state_t discovery_state;
    uint8_t scan_state;
    bool client_registered;
    int client_if;
    bt_bdaddr_t r_bd_addr; /* remote address when pairing */
    btctl_bt_device_list_hdr_t dicovered_device_list;
    btgatt_client_callbacks_t *client;
    bt_callbacks_t *bt_callbacks;
} libbtctl_ctx_t, *p_libbtctl_ctx_t;


/* 'bt_status_t' defined at <repo root>/hardware/libhardware/include/hardware/bluetooth.h */

/****************************
 * 
 * typedef enum {
 * BT_STATUS_SUCCESS,
 * BT_STATUS_FAIL,
 * BT_STATUS_NOT_READY,
 * BT_STATUS_NOMEM,
 * BT_STATUS_BUSY,
 * BT_STATUS_DONE,        
 * BT_STATUS_UNSUPPORTED,
 * BT_STATUS_PARM_INVALID,
 * BT_STATUS_UNHANDLED,
 * BT_STATUS_AUTH_FAILURE,
 * BT_STATUS_RMT_DEV_DOWN
 * } bt_status_t;
 * 
 *****************************/





p_libbtctl_ctx_t btctl_init(const btgatt_client_callbacks_t *gatt_callbacks , bt_callbacks_t *bt_callbacks);

bt_status_t btctl_enable();
int btctl_disable();

/* 
 * synchronous discovery of bluetooth and BLE devices
 * It times out by bluedroid stack.
 * device_found_cb() called for each discovery event.
 */
bt_status_t btctl_discovery_start_blocked();

/* 
 * starts asynchronous discovery of bluetooth and BLE devices
 * This function returns right away, btctl_discovery_stop() stops the discovery.
 * device_found_cb() called for each discovery event.
 */
bt_status_t btctl_discovery_start_unblocked();
bt_status_t btctl_discovery_stop();

/*
 * After device descovery, an array is filled up with pointer of device property structure.
 * btctl_get_discovered_dev_array() returns the pointer to the array.
 * example: 
 * p_btctl_dev_prop_t *dev_list = btctl_get_discovered_dev_array; 
 * if (dev_list[0]->bd_type == BT_DEVICE_DEVTYPE_BLE)
 *      printf("type: BT LE\n");
 */
p_btctl_dev_prop_t* btctl_get_discovered_dev_array();
int btctl_get_count_discovered_dev_array();

/*
 * Below functions returns members of property of discovered device
 */
char * btctl_get_dev_name_from_dev_prop(p_btctl_dev_prop_t p_data);
bt_bdaddr_t * btctl_get_bdaddr_from_dev_prop(p_btctl_dev_prop_t p_data);
uint8_t * btctl_get_address_from_dev_prop(p_btctl_dev_prop_t p_data);
uint32_t btctl_get_bd_class_from_dev_prop(p_btctl_dev_prop_t p_data);
bt_device_type_t btctl_get_bd_type_from_dev_prop(p_btctl_dev_prop_t p_data);
char * btctl_get_bd_alias_from_dev_prop(p_btctl_dev_prop_t p_data);
uint8_t btctl_get_bd_rssi_from_dev_prop(p_btctl_dev_prop_t p_data);


void btctl_print_discovered_devices();

/* 
 * connect BLE device 
 */
bt_status_t btctl_connect(bt_bdaddr_t *bdaddr);

bt_status_t btctl_disconnect(pConnection connection );

/*
 * return conn_id for given connection, 
 * return 0 for invalid connection 
 */
int btctl_get_conn_id(pConnection connection );
/* 
 * get supported GATT services from BLE device
 * UUIDs are stored internal table with associtated index.
 */
bt_status_t btctl_get_service(pConnection connection, bt_uuid_t *p_uuid);

/* 
 * Get supported Characteristics for given service index from BLE device 
 * UUIDs are stored internal table with associtated index.
 */
bt_status_t btctl_get_characteristic(pConnection connection, int service_index);

/* 
 * Search given Service UUID from services table which is filled by btctl_get_service()
 * then, if it finds the matching UUID, returns index.
 * It returns -1 when it failed to find matching UUID from table.
 */
int btctl_search_service_uuid_and_get_index(pConnection connection, bt_uuid_t *uuid);

/* 
 * Search given Characteristic UUID from characteristic table which is filled by btctl_get_characteristic()
 * then, if it finds the matching UUID, returns index.
 * It returns -1 when it failed to find matching UUID from table.
 */
int btctl_search_characteristic_uuid_and_get_index(pConnection connection, int svc_id, bt_uuid_t *uuid);

/* Search both Service and Characteristic UUID at single shot. *service_index and *char_index
 * points the indexs for matching UUIDs.
 It returns -1 when it failed to find matching UUIDs from both service and characteristic tables.
 */
int btctl_search_svc_n_char_uuid_and_get_index(pConnection connection, int *service_index, int *char_index, bt_uuid_t *svc_uuid,bt_uuid_t *char_uuid );

/* 
 * Get supported Descriptor for given service index ( for Service UUID) and characteristic index (for characteristic UUID)
 * from BLE device 
 * UUIDs are stored internal table with associtated index.
 */
bt_status_t btctl_get_descriptor(pConnection connection, int svc_id, int char_id);

/* 
 * Register notification for service and characteristic
 */
bt_status_t btctl_reg_notification(pConnection connection, int svc_id, int char_id);

/* 
 * UnRegister notification for service and characteristic
 */
bt_status_t btctl_unreg_notification(pConnection connection, int svc_id, int char_id);

/*
 * Below four functions writes vlaues at given service and characteristic on device.
 * write_descriptor_cb() and write_characteristic_cb() call are called 
 * for btctl_write_req_descriptor() and write_req_char().
 * 
 * 
 * Continuous issuing btctl_write_req_char() and btctl_write_req_descriptor() without any delay in between quickly 
 * fills up the stack's command queue with pending commands. 
 * 
 * Too much pending events in queue causes that CBs are not invoked properly. 
 * If this happens, library waits 7 seconds for bt stack to recover. 
 * 
 */

bt_status_t btctl_write_cmd_char(pConnection connection, int svc_id, int char_id,  int auth, char *values, int values_len);
bt_status_t btctl_write_req_char(pConnection connection, int svc_id, int char_id,  int auth, char *values, int values_len);
bt_status_t btctl_write_cmd_descriptor(pConnection connection, int svc_id, int char_id,  int desc_id , int auth, char *values, int values_len );
bt_status_t btctl_write_req_descriptor(pConnection connection, int svc_id, int char_id,  int desc_id , int auth, char *values, int values_len );

/*
 * internal linked list to maintain connected BLE device.
 */
pConnection btctl_list_get_head_connection ();
pConnection btctl_list_get_tail_connection ();
pConnection btctl_list_get_next_connection (pConnection connection);
pConnection btctl_list_find_connection_by_connid(int conn_id);
int btctl_list_get_total_connection_count();
void btctl_list_print_all_connected_dev();

/* return 0 if both UUIDs are same */
int btctl_util_uuidcmp(bt_uuid_t *uuid_dst, bt_uuid_t *uuid_src);

/* return 0 if both bd_addr are same */
int btctl_util_bt_bdaddr_cmp(bt_bdaddr_t *bt_addr_dst, bt_bdaddr_t *bt_addr_src);

#endif /* __LIBBTCTL_H */
