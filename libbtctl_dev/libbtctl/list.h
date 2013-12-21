#ifndef __LIBBTCTL_LIST_H
#define __LIBBTCTL_LIST_H

#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>
#include <hardware/bt_gatt_client.h>
#include <hardware/hardware.h>


#if 0
typedef struct Connection_tag {
    int conn_id;
    bt_bdaddr_t bd_addr;
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
#endif


typedef struct Node_tag {
    struct Node_tag *next;
    Connection    *data;
} Node, *pNode;



typedef struct List_tag {
    pNode head;
    pNode tail;
    int count;

} List, *pList;

void ListInit(pList list)
{

    list->count = 0;
    list->head = NULL;
    list->tail = NULL;


}

void ListAddHead(pList list, pConnection data)
{
    pNode new_head;

    new_head = (pNode)malloc(sizeof(Node));
    data->node = (pNode)new_head;
    new_head->data = data;
    new_head->next = list->head;
    list->head = new_head;
    if (list->count == 0)
        list->tail = new_head;
    list->count++;
}

void ListAddTail(pList list, pConnection data)
{
    pNode new_tail;

    new_tail = (pNode)malloc(sizeof(Node));
    data->node = (pNode)new_tail;
    new_tail->data = data;
    new_tail->next = NULL;

    if (list->tail)
        list->tail->next = new_tail;

    list->tail = new_tail;

    if (list->count == 0) {
        list->head = new_tail;

    }

    list->count++;
}

pNode ListGetHead(pList list)
{
    if (list)
        return list->head;

    return NULL;
}

pNode ListGetTail(pList list)
{
    if (list)
        return list->tail;

    return NULL;
}

pNode ListGetNext(pNode node)
{
    if (node)
        return node->next;

    return NULL;
}

void ListPrintAllConnection(pList list)
{
    pNode head = list->head;
    char addr_str[BT_ADDRESS_STR_LEN];
    printf("\n----\n");
    while (head) {
        printf("Conn ID %d, bd_addr %s \n", head->data->conn_id,  ba2str(head->data->bd_addr.address,
                                                                         addr_str) ) ;
        head = head->next;
    }


    printf("\n----\n");


}


void ListClearSvcCacheAll(pList list)
{
    pNode head = list->head;
    pConnection data;


    while (head) {
        uint8_t i;
        data = head->data;

        memset(data->svcs, 0 , sizeof(service_info_t) * MAX_SVCS_SIZE);
        data->svcs_size = 0;

        head = head->next;
    }

}

void ListClearSvcCacheNode(pNode node)
{

    pConnection data;


    if (node) {
        uint8_t i;
        data = node->data;
        memset(data->svcs, 0 , sizeof(service_info_t) * MAX_SVCS_SIZE);
        data->svcs_size = 0;
    }

}

void ListClearSvcCacheConnection(pConnection connection)
{


    if (connection) {
        memset(connection->svcs, 0 , sizeof(service_info_t) * MAX_SVCS_SIZE);
        connection->svcs_size = 0;
    }
}

pConnection ListFindConnectionByID(pList list, int conn_id)
{
    pNode head = list->head;

    while (head) {
        if (conn_id == head->data->conn_id) {
            return(head->data);
        }
        head = head->next;
    }

    return NULL;

}


void ListClearSvcCacheConnID(pList list, int conn_id)
{

    pConnection connection = ListFindConnectionByID(list, conn_id);

    if (connection) {
        memset(connection->svcs, 0 , sizeof(service_info_t) * MAX_SVCS_SIZE);
        connection->svcs_size = 0;
    }
}

int is_same_data(pConnection src, pConnection target)
{

    if ( src->conn_id == target->conn_id ) {
        return true;
    }
    return false;
}

pNode RemoveNode(pNode head)
{
    pNode node;
    node = head->next;
    free(head);
    return node;
}


int ListRemoveConnection(pList list, int conn_id)
{
    pNode head = list->head;
    pNode prev = NULL;
    pNode next;

    if (list == NULL)
        return false;

    while (head) {
        if (conn_id == head->data->conn_id) {

            next = RemoveNode(head);
            if ( next == NULL)
                list->tail = prev;

            if ( prev ) {
                prev->next = next;
            } else {
                list->head  = next;
            }

            list->count--;  
            return true;
        } else {
            prev = head;
            head = head->next;
        }
    }
    return false;
}

void ListRemoveAll(pList list)
{
    pNode head = list->head;
    pNode old_head;

    while (head) {
        old_head = head;
        head = head->next;
        free(old_head);

    }

    list->count = 0;
    list->head = NULL;
    list->tail = NULL;
}
#endif /* __LIBBTCTL_LIST_H */