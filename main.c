/*!
 * @file main.c
 *
 * @brief This is the main application module. It contains the message and state handlers for
 * messages received from firmware.
 */

#include <connection.h>
#include <stream.h>
#include <sink.h>
#include <source.h>
#include <panic.h>
#include <string.h>
#include <stdlib.h>
#include <vm.h>
#include <ps.h>
#include <sdp_parse.h>
#include <bdaddr.h>

#include "rfcomm_multi_slave.h"

/*!
 * @brief Salutation message output to UART on program start.
 */
#define SALUTATION "\r\nRFCOMM multi-slave messaging application\r\nVersion 1.0\r\n"

/*!
 * @brief The application state.
 */
MAIN_APP_T app;

/*!
 * @brief RFCOMM Service Record - used when in slave mode.
 *
 * The application's service record can be read by other devices during an Inquiry
 * IF we are visable (Paging) e.g. if a 'connect slave' command is used, this will
 * start that procedure.
 *
 * This application uses the Service Class Name UUID 0x1201 for 'GenericNetworking' - 
 * this has no specific profile or protocol specified for its use.
 */
static uint8 rfcomm_slave_sr [] =
{
    0x09, 0x00, 0x01,   /* ServiceClassIDList(0x0001) */
    0x35, 0x03,         /* DataElSeq 3 bytes */
    0x19, 0xFF, 0xF0,   /* UUID 0xFFF0 for Echo Service */
    0x09, 0x00, 0x04,   /* ProtocolDescriptorList(0x0004) */
    0x35, 0x0c,         /* DataElSeq 12 bytes */
    0x35, 0x03,         /* DataElSeq 3 bytes */
    0x19, 0x01, 0x00,   /* UUID L2CAP(0x0100) */
    0x35, 0x05,         /* DataElSeq 5 bytes */
    0x19, 0x00, 0x03,   /* UUID RFCOMM(0x0003) */
    0x08, 0x00,         /* uint8 0x00 <- Service Channel - to be over-written*/
    0x09, 0x00, 0x06,   /* LanguageBaseAttributeIDList(0x0006) */
    0x35, 0x09,         /* DataElSeq 9 bytes */
    0x09, 0x65, 0x6e,   /* uint16 0x656e */
    0x09, 0x00, 0x6a,   /* uint16 0x006a */
    0x09, 0x01, 0x00,   /* uint16 0x0100 */
    0x09, 0x01, 0x00,   /* ServiceName(0x0100) = "RFCOMM Echo" */
    0x25, 0x0b,         /* String length 11 */
    'R','F','C','O','M', 'M', ' ', 'E', 'c', 'h', 'o'
};

/* RFCOMM Echo service search request */
static const uint8 RfcommMultiServiceRequest [] =
{
    0x35, 0x05,                     /* type = DataElSeq, 5 bytes in DataElSeq */
    0x1a, 0x00, 0x00, 0xFF, 0xF0    /* 4 byte UUID 0x0000FFF0 for RFCOMM Echo */
};

/* Protocol search request */
static const uint8 ProtocolAttributeRequest [] =
{
    0x35, 0x03,         /* type = DataElSeq, 3 bytes in DataElSeq */
    0x09, 0x00, 0x04    /* 2 byte UINT attrID ProtocolDescriptorList */    
};

/*!
 * @brief Given a sink id, return the link id (index into app->connections) for that sink.
 *
 * @param app The application state.
 * @param m The CL_INIT_CFM message pointer.
 *
 * @returns link_id or NO_ACTIVE (0xFF)
 */
static uint16 LinkFromSink(Sink sink) 
{
    int i;
    for (i=0; i<MAX_CONNECTIONS; i++)
        if (sink == app.connection[i].sink)
            return i;
    return NO_ACTIVE;    
}

/*!
 * @brief Handled CL_INIT_CFM from Connection library in response to ConnectionInit().
 *
 * Ask the Firmware to read our own Bluetooth Device Address.
 *
 * @param app The application state.
 * @param m The CL_INIT_CFM message pointer.
 *
 * @returns void.
 */
static void cl_init_cfm(MAIN_APP_T *app, const CL_INIT_CFM_T *m)
{
    if (app->debug) print("DBG: cl_init_cfm\r\n");

    if (m->status != success)
    {
        print("FATAL ERROR: Connection libary failed to initialise.\r\n");
        Panic();
    }
    
    /* Read our own Bluetooth device address. */
    ConnectionReadLocalAddr(&app->task);
}

/*!
 * @brief Handled CL_DM_LOCAL_BD_ADDR_CFM from Connection library in response to 
 * ConnectionReadLocalAddr().
 *
 * Cache our own Bluetooth Device Address
 * Ask Firmware to read our own Device Name.
 *
 * @param app The application state.
 * @param m The CL_DM_LOCAL_BD_ADDR_CFM message pointer.
 *
 * @returns void.
 */
static void cl_dm_local_bd_addr_cfm(MAIN_APP_T *app, const CL_DM_LOCAL_BD_ADDR_CFM_T *m)
{    
    if (app->debug) print("DBG: cl_dm_local_bd_addr_cfm\r\n");
    
    if (m->status != success)
    {
        print("FATAL ERROR: Failed to read our own Bluetooth Device Address.\r\n");
        Panic();
    }
    
    /* Cache our own Bluetooth Device Address. */
    memmove(&app->own_addr, &m->bd_addr, sizeof(bdaddr));
    
    /* Ask Firmware to read our own Device Name */
    ConnectionReadLocalName(&app->task);
}

/*!
 * @brief Handled CL_DM_LOCAL_BD_ADDR_CFM from Connection library in response to 
 * ConnectionReadLocalAddr().
 *
 * Cache our own Device Name.
 * Set our Class Of Device.
 * Ask Firmware for an RFCOMM server channel for incoming slave connections.
 *
 * @param app The application state.
 * @param m The CL_DM_LOCAL_BD_ADDR_CFM message pointer.
 *
 * @returns void.
 */
static void cl_dm_local_name_complete(MAIN_APP_T *app, const CL_DM_LOCAL_NAME_COMPLETE_T *m)
{
    uint16 copy_size;
    
    if (app->debug) print("DBG: cl_dm_local_name_complete\r\n");
    
    if (m->status != success)
    {
        print("FATAL ERROR: Failed to read our own Device Name.\r\n");
        Panic();
    }
    
    /* Cache our own name, either for the max size of our cache storage or the returned
     * name size, whichever is smaller
     */
    copy_size = (m->size_local_name < MAX_OWN_NAME) ? m->size_local_name : MAX_OWN_NAME;
    memmove(&app->own_name, &m->local_name, copy_size);
    
    /* Ensure NULL string termination */
    app->own_name[copy_size] = '\0';    
    
    /* Write class of device to the FW - this will be used during Paging/Inquiry. */
    ConnectionWriteClassOfDevice(CLASS_OF_DEVICE);
    
    /* Get an RFCOMM server channel for incoming connections. */
    ConnectionRfcommAllocateChannel(&app->task, 0);
}

/*!
 * @brief Handled CL_RFCOMM_REGISTER_CFM from Connection library in response to the 
 * ConnectionRfcommAllocateChannel() function called in cl_init_cfm().
 *
 * Updates the app's rfcomm_slave_sr Service Record with the allocated server channel
 * ID and then register the Service Record with the SDP protocol in the FW.
 * 
 * @param app The application state.
 * @param m The CL_RFCOMM_REGISTER_CFM message pointer.
 *
 * @returns void.
 */
static void cl_rfcomm_register_cfm(MAIN_APP_T *app, const CL_RFCOMM_REGISTER_CFM_T *m) 
{
    if (app->debug) print("DBG: cl_rfcomm_register_cfm\r\n");
    
    if (m->status != success)
    {
        print("ERROR: Failed to register RFCOMM server channel!\r\n");
        Panic();
    }
    
    if (!SdpParseInsertRfcommServerChannel(
             sizeof(rfcomm_slave_sr), 
             rfcomm_slave_sr,
             m->server_channel
             )
        )
    {
        print("ERROR: Could not update RFCOMM Service record!\r\n");
        Panic();
    }  
    
    /* Cache this for later when setting up security and the SDP Service Record.*/
    app->rfcomm_server_channel = m->server_channel; 
    
    /* Set up security for incoming connections - Secure Simple Pairing */
    ConnectionSmRegisterIncomingService( 
        protocol_rfcomm,
        app->rfcomm_server_channel,
        sec4_in_level_1 
        );

    /* Turn off security for SDP browsing. */
    ConnectionSmSetSdpSecurityIn((bool) TRUE);   

    print("Ready.\r\n");
}

/*!
 * @brief Start the process to accept an incoming RFCOMM connection. 
 *
 * If there is not incoming connection completed within 30-seconds, then go back
 * to the ready (idle) state.
 *
 * The first steps are:
 * - Register an SDP record for the RFCOMM MULTI service.
 * - Start paging so that the device is discoverable for connection.
 * 
 * @param app The application state.
 *
 * @returns void.
 */
static void connect_slave(MAIN_APP_T *app) 
{
    uint8 *service_record = NULL;
    uint16 service_record_size = (uint16)sizeof(rfcomm_slave_sr); 
    
    if (app->debug) print("DBG: connect_slave\r\n");
     
    /* A slave can only have one connection, to its master. */
    app->active = 0;
    
    /* This device is a Slave and the connection is, hopefully, a master.*/
    app->role = ROLE_SLAVE;
    ACTIVE.role = ROLE_MASTER;
    ACTIVE.state = STATE_CONNECTING;
    
    /* Allocate memory for a copy of the service record, which will be sent to the FW
     * to register it for SDP.
     */
    service_record = (uint8 *)PanicUnlessMalloc(service_record_size);
    
    /* memmove is more efficient than memcpy on Bluecore chips */
    memmove(service_record, rfcomm_slave_sr, service_record_size);
    
    /* Register the service record with SDP in the FW. The FW will free the memory
     * for service_record, when the operation is complete.
     */
    ConnectionRegisterServiceRecord( 
            &app->task,
            service_record_size,
            service_record
            );
}

/*!
 * @brief Handled CL_SDP_REGISTER_CFM from Connection library in response to the 
 * ConnectRegisterServiceRecord() function called in cl_rfcomm_register_cfm().
 *
 * Updates the app's rfcomm_slave_sr Service Record with the allocated server channel
 * ID and then register the Service Record with the SDP protocol in the FW.
 * 
 * @param app The application state.
 * @param m The CL_SDP_REGISTER_CFM message pointer.
 *
 * @returns void.
 */
static void cl_sdp_register_cfm(MAIN_APP_T *app, const CL_SDP_REGISTER_CFM_T *m) 
{
    if (app->debug) print("DBG: cl_sdp_register_cfm\r\n");
    
    if (m->status != success)
    {
        print("ERROR: Failed to register SDP Service Record!\r\n");
        Panic();
    }

    /* Cache the service record handle for later when we want to unregister the
     * service.
     */
    app->service_record_handle = m->service_handle;
    
    /* Make this device discoverable. */
    ConnectionWriteScanEnable(hci_scan_enable_inq_and_page);
     
    /* Send a message to be delivered in 30-seconds. This is the timeout for incomming
     * connections.
     */
    MessageSendLater(&app->task, MSG_SLAVE_CONNECTION_TIMEOUT, 0, 30000);
}

/*!
 * @brief For an active connection that is conneting or disconnecting, reset its state.
 * 
 * @param app The application state.
 *
 * @returns void.
 */
static void reset_active_connection(MAIN_APP_T *app) 
{
    if (app->debug) print("DBG: reset_active_connection\r\n");
    
    if (app->active == NO_ACTIVE) 
    {
        if (app->debug) print ("DBG: No active connection!\r\n");
        return;
    }
    
    ACTIVE.state = STATE_DISCONNECTED;
    BdaddrSetZero(&ACTIVE.addr);
    ACTIVE.role = ROLE_NONE;
    ACTIVE.sink = 0;
    app->active = NO_ACTIVE;
    
    if (app->conn_count > 0)
        app->conn_count -= 1;
    else
        if (app->debug) print("DBG: conn_count is already 0!\r\n");
            
    if (app->connection[0].state == STATE_DISCONNECTED &&
        app->connection[1].state == STATE_DISCONNECTED)
        {
            app->role = ROLE_NONE;
        }
    }

/*!
 * @brief Stop any potentional slave connection.
 *
 * Unregister the SDP record and make the device undiscoverable again.
 * 
 * @param app The application state.
 *
 * @returns void.
 */
static void stop_slave_connection(MAIN_APP_T *app) 
{
    if (app->debug) print("DBG: stop_slave_connection\r\n");
    /* Make this device undiscoverable. */
    ConnectionWriteScanEnable(hci_scan_enable_off);
    
    ConnectionUnregisterServiceRecord(&app->task, app->service_record_handle);
}    

/*!
 * @brief Handled CL_SDP_UNREGISTER_CFM from Connection library in response to the 
 * ConnectUnregisterServiceRecord() function called in stop_slave_connection().
 * 
 * @param app The application state.
 * @param m The CL_SDP_REGISTER_CFM message pointer.
 *
 * @returns void.
 */
static void cl_sdp_unregister_cfm(MAIN_APP_T *app, const CL_SDP_UNREGISTER_CFM_T *m) 
{
    if (app->debug) print("DBG: cl_sdp_unregister_cfm\r\n");
    
    /* There can be a 'PENDING' message before success. */
    if (m->status == success) 
    {
         print("Ready.\r\n");
    }
}

/*!
 * @brief Handled CL_RFCOMM_CONNECT_IND, which should only be dealt with when expecting
 * an incoming connection.
 * 
 * @param app The application state.
 * @param m The CL_RFCOMM_CONNECT_IND message pointer.
 *
 * @returns void.
 */
static void cl_rfcomm_connect_ind(MAIN_APP_T *app, const CL_RFCOMM_CONNECT_IND_T *m) 
{
    if (app->debug) print("DBG: cl_rfcomm_connect_ind\r\n");  
    
    if (app->role == ROLE_SLAVE 
        && app->active != NO_ACTIVE 
        && app->connection[app->active].state == STATE_CONNECTING)
    {
        print("Slave connection %d started.\r\n", app->active);
        
        /* Cancel the timeout message, we have a connection. */
        MessageCancelAll(&app->task, MSG_SLAVE_CONNECTION_TIMEOUT);
 
        memmove(&app->connection[app->active].addr, &m->bd_addr, sizeof(bdaddr));
        app->connection[app->active].sink = m->sink;
        ConnectionRfcommConnectResponse(
                &app->task,
                (bool) TRUE,                     /* Accept the connection */
                m->sink,
                app->rfcomm_server_channel,
                0);                              /* Default config */
    }
}

/*!
 * @brief Handled CL_RFCOMM_SERVER_CONNECT_CFM, which should only be dealt when and RFCOMM
 * slave connection is ongoing.
 * 
 * @param app The application state.
 * @param m The CL_RFCOMM_CONNECT_IND message pointer.
 *
 * @returns void.
 */
static void cl_rfcomm_server_connect_cfm(
        MAIN_APP_T *app, 
        const CL_RFCOMM_SERVER_CONNECT_CFM_T *m
        )
{
    if (app->debug) print("DBG: cl_rfcomm_server_connect_cfm\r\n");  

    if (app->role == ROLE_SLAVE 
        && app->active != NO_ACTIVE 
        && app->connection[app->active].state == STATE_CONNECTING)
    {
        if (m->status == success) 
        {
            print("Slave connection %d complete.\r\n", app->active);

            PanicNull(m->sink);         /* Shouldn't happen. */
            ACTIVE.sink = m->sink;
            ACTIVE.state = STATE_CONNECTED;
            app->conn_count += 1;
            app->active = NO_ACTIVE;
            
            /* Now the connection is established, stop paging and take down the 
             * SDP service record.
             */
            stop_slave_connection(app);
        }
        else
        {
            print("ERROR: Slave connection %d failed.\r\n", app->active);
            reset_active_connection(app);
        }
    }
    else 
    {
        print("ERROR: Unexpected RFCOMM Server Cfm\r\n");
        Panic();
    }
}

/*!
 * @brief Start the process to seeking an RFCOMM slave and connecting to it. 
 *
 * If there is not incoming connection completed within 30-seconds inquiry will stop.
 *
 * Inquire for a devices that match our class of device. Try and connect to them!
 * 
 * @param app The application state.
 *
 * @returns void.
 */
static void connect_master(MAIN_APP_T *app) 
{
    int i;
    
    if (app->debug) print("DBG: connect_master\r\n");
    
    /* The master can have up to two slave connections. */
    for (i=0; i<MAX_CONNECTIONS; i++) 
    {
        if (app->connection[i].role == ROLE_NONE)
        {
            app->active = i;
            break;
        }
    } 
       
    /* This shouldn't happen. */    
    if (app->active == NO_ACTIVE)
    {
        print("DBG: app->active not set - something went wrong!");
        Panic();
    }
    
    /* We are the master and the active connection is to a slave. */
    app->role = ROLE_MASTER;
    ACTIVE.state = STATE_CONNECTING;
    ACTIVE.role = ROLE_SLAVE;
    
    /* Inquire to look for devices in inquiry scan mode. Look for one at a time.*/
    ConnectionInquire(
            &app->task, 
            GIAC,     /* LimitedInqury Access */
            1,        /* Maximum no. of responses */
            24,       /* Timeout after ~30-seconds */
            CLASS_OF_DEVICE
            );
}

/*!
 * @brief Process an inquiry result for a outgoing Slave connection. 
 *
 * Registers an outgoing service for the connection to the slave.
 * 
 * @param app The application state.
 * @param m The CL_DM_INQUIRE_RESULT message pointer.
 *
 * @returns void.
 */
static void cl_dm_inquire_result(MAIN_APP_T *app, const CL_DM_INQUIRE_RESULT_T *m) 
{
    if (app->debug) print("DBG: cl_dm_inquire_result\r\n"); 
    
    /* 'inquiry_status_result' indicates we got a hit. Cache the address until inquiry is
     * complete. Since we asked for only 1-result, this should be it!
     */
    if (m->status == inquiry_status_result)
    {
        app->connection[app->active].addr = m->bd_addr;
    }
    else /* inquiry_status_ready - inquiry process is complete. */
    {
        if (BdaddrIsZero(&app->connection[app->active].addr))
        {
            print("No slave devices found.\r\n");
            reset_active_connection(app);
            print("Ready.\r\n");
        }
        else
        {
            ConnectionSmRegisterOutgoingService(
                    &app->task,
                    &app->connection[app->active].addr,
                    protocol_rfcomm,
                    0,                  /* Suggested server channel */
                    sec4_out_level_1
                    );
        }
    }
}

/*!
 * @brief Process the Register Outgoing Service Confirmation 
 *
 * This indicates we have opened an RFCOMM channel as a client. Now perform an SDP
 * Search on the server's Service Record to make sure it has the RFCOMM Multi service
 * we want.
 * 
 * @param app The application state.
 * @param m The CL_SM_REGISTER_OUTGOING_SERVICE_CFM message pointer.
 *
 * @returns void.
 */
static void cl_sm_register_outgoing_service_cfm(
        MAIN_APP_T *app, 
        const CL_SM_REGISTER_OUTGOING_SERVICE_CFM_T *m
        )
{
    if (app->debug) print("DBG: cl_sm_register_outgoing_service_cfm\r\n");
    
    app->rfcomm_server_channel = m->security_channel;
    
    ConnectionSdpServiceSearchAttributeRequest(
            &app->task,
            &app->connection[app->active].addr,
            0x40,                       /* Max attributes to return. */
            sizeof(RfcommMultiServiceRequest),
            RfcommMultiServiceRequest,
            sizeof(ProtocolAttributeRequest),
            ProtocolAttributeRequest
            );
}

/*!
 * @brief Process the SDP Search Attribute Confirmations 
 *
 * We found the SDP service we are looking for, now parse it for the RFCOMM channel and
 * then request a connection to that RFCOMM channel.
 * 
 * @param app The application state.
 * @param m The CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM message pointer.
 *
 * @returns void.
 */
static void cl_sdp_service_search_attribute_cfm(
        MAIN_APP_T *app,
        const CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM_T *m
        )
{
    if (app->debug) print("DBG: cl_sdp_service_search_attribute_cfm\r\n");
    
    if (m->status == success)
    {
        uint8* rfcomm_channels;	
        uint8 size_rfcomm_channels = 1;
        uint8 channels_found = 0;

        rfcomm_channels = PanicUnlessMalloc(size_rfcomm_channels * sizeof(uint8)) ;
                
        /* See if the received data contains an rfcomm channel. */
        if ( SdpParseGetMultipleRfcommServerChannels(
                            m->size_attributes, 
                            (uint8*)m->attributes, 
                            size_rfcomm_channels, 
                            &rfcomm_channels, 
                            &channels_found)
            )
        {
            /* An RFCOMM channel was found, proceed with connection */
            ConnectionRfcommConnectRequest(
                            &app->task,
                            &app->connection[app->active].addr,
                            app->rfcomm_server_channel,
                            rfcomm_channels[0],
                            0                   /* default payload size */
                            );  
        }
        else
        {
            print("Couldn't get an RFCOMM channel from Service Record Attributes\r\n");
            reset_active_connection(app);
            print("Ready.\r\n");
        }
        free(rfcomm_channels);
    }
    else
    {
        print("SDP Service Search for Attributes failed.\r\n");
        reset_active_connection(app);
        print("Ready.\r\n");
    }
}

/*!
 * @brief Process RFCOMM Client Connect Confirmation
 *
 * Finally, indicates the RFCOMM connection is complete, or has failed.
 *
 * @param app The application state.
 * @param m The CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM message pointer.
 *
 * @returns void.
 */
static void cl_rfcomm_client_connect_cfm(
        MAIN_APP_T *app,
        const CL_RFCOMM_CLIENT_CONNECT_CFM_T *m
        )
{
    if (app->debug) print("DBG: cl_rfcomm_client_connect_cfm\r\n");
    
    if (m->status == rfcomm_connect_pending)
    {
        /* We can disconnect at any point now. */
        ACTIVE.sink = m->sink;      
    }        
    else if (m->status == success)
    {
        print("Master connection complete.\r\n");

        PanicNull(m->sink);         /* Shouldn't happen. */
        ACTIVE.sink = m->sink; 
        ACTIVE.state = STATE_CONNECTED;
        app->conn_count += 1;
        app->active = NO_ACTIVE;    /* No longer connecting. */
        print("Ready.\r\n");        /* TO DO: move this. */
    }
    else
    {
        print("RFCOMM connection failed.\r\n");
        reset_active_connection(app);
    }
}

/*!
 * @brief Process Disconnect message from the application.
 *
 * Calls ConnectionRfcommDisconnect
 *
 * @param app The application state.
 * @param m The MSG_DISCONNECT message pointer.
 *
 * @returns void.
 */
static void disconnect(MAIN_APP_T *app, const MSG_DISCONNECT_T *m)
{
    if (app->debug) print("DBG: disconnect %d\r\n", m->link_id);
    
    /* Between the command being issues and actually requesting, the link could 
     * already have gone. So always, check.
     */
    if (app->connection[m->link_id].state == STATE_CONNECTED)
    {
        app->active = m->link_id;
        ACTIVE.state = STATE_DISCONNECTING;
        print("Disconnecting link %d\r\n", m->link_id);
        
        ConnectionRfcommDisconnectRequest(
                &app->task, 
                ACTIVE.sink
                );
        app->active = NO_ACTIVE;
    }
    else
    {
        print("ERROR: Link %d is not in the connected state.\r\n", m->link_id);
    }
}
    
/*!
 * @brief Process CL_RFCOMM_DISCONNECT_CFM from Connection library
 *
 * Mark the connection as disconnected and reset its state.
 *
 * @param app The application state.
 * @param m The CL_RFCOMM_DISCONNECT_CFM message pointer.
 *
 * @returns void.
 */
static void cl_rfcomm_disconnect_cfm(
        MAIN_APP_T *app, 
        const CL_RFCOMM_DISCONNECT_CFM_T *m)
{
    if (app->debug) print("DBG: cl_rfcomm_disconnect_cfm 0x%x\r\n", m->status);
    
    /* Find the sink, find the link to disconnect. */
    app->active = LinkFromSink(m->sink);
    
    print("Disconnected link %d\r\n", app->active);
    reset_active_connection(app);        
}

/*!
 * @brief Process CL_RFCOMM_DISCONNECT_IND from Connection library, indicating the remote
 *        device is disconnecting the link.
 *
 * Mark the connection as disconnecting and call ConnectionRfcommDisconnectResponse() 
 * to acknowledge the disconnection. It's not over until the ACL is closed.
 *
 * @param app The application state.
 * @param m The CL_RFCOMM_DISCONNECT_IND_T message pointer.
 *
 * @returns void.
 */
static void cl_rfcomm_disconnect_ind(
        MAIN_APP_T *app, 
        const CL_RFCOMM_DISCONNECT_IND_T *m)
{
    int link_id;
    if (app->debug) print("DBG: cl_rfcomm_disconnect_ind 0x%x\r\n", m->status);
    
    /* Go through all the RFCOMM connections to find a matching sink. */
    for (link_id=0; link_id<MAX_CONNECTIONS; link_id++) 
    {
        if (
            app->connection[link_id].state == STATE_CONNECTED &&
            m->sink == app->connection[link_id].sink
            )
        {
            app->active = link_id;
            break;
        }
    }
    
    if (link_id <MAX_CONNECTIONS)
    {
        print("Remote has disconnected link %d\r\n", link_id);
        ConnectionRfcommDisconnectResponse(m->sink);
        reset_active_connection(app);        
    }
}

/*!
 * @brief Handled CL_DM_ACL_OPENED_IND from Connection library.
 *
 * For an incoming master connection, this could be the first time we get the 
 * Master's BD addr, so Cache it for Pairing procedures later.
 * 
 * @param app The application state.
 * @param m The CL_DM_ACL_OPENED_IND message pointer.
 *
 * @returns void.
 */
static void cl_dm_acl_opened_ind(MAIN_APP_T *app, const CL_DM_ACL_OPENED_IND_T *m)
{
    if (app->debug) 
    {
        print("DBG: cl_dm_acl_closed_ind\r\n");
        print("     bdaddr:   %B\r\n", &m->bd_addr); 
        print("     incoming: %s\r\n", (m->incoming) ? "yes" : "no");
        print("     status:   0x%x\r\n", m->status); 
    }
    
    if (ACTIVE.role == ROLE_MASTER && BdaddrIsZero(&ACTIVE.addr)) 
    {
        ACTIVE.addr = m->bd_addr;
    }
}

/*!
 * @brief Handled CL_DM_ACL_CLOSED_IND from Connection library.
 *
 * Handle DM_ACL_CLOSED_IND, especially for a DISCONNECTING link.
 * 
 * @param app The application state.
 * @param m The CL_DM_ACL_CLOSED_IND message pointer.
 *
 * @returns void.
 */
static void cl_dm_acl_closed_ind(MAIN_APP_T *app, const CL_DM_ACL_CLOSED_IND_T *m)
{
    if (app->debug) 
    {
        print("DBG: cl_dm_acl_closed_ind\r\n");
        print("     bdaddr:   %B\r\n", &m->bd_addr); 
        print("     status:   0x%x\r\n", m->status); 
    }
    
    /* ACL can open and close during pairing, so only deal with connection that
     * is actively disconnecting.
     *
     * In this case the master or slave have actively started the disconnection and
     * either RFCOMM_DISCONNECT_IND has been received all the 'disconnect' command from
     * the ui.
     */
    if (app->active != NO_ACTIVE && ACTIVE.state == STATE_DISCONNECTING)
    {        
        if (BdaddrIsSame(&m->bd_addr, &ACTIVE.addr))
        {
            print("Link %d disconnected\r\n", app->active);
            reset_active_connection(app);            
        }
    }  
    /* This could be link loss. In which case the ACL Closed is either before or after 
       the disconnect indication.
     */
    else if (m->status == hci_error_conn_timeout)
    {
    }
}

/*!
 * @brief Handled CL_SM_REMOTE_IO_CAPABILITY_IND from Connection library.
 *
 * This is the start of the pairing/bonding process. This application uses the
 * 'Just Works' association model - there is no Man In The Middle protection 
 * (Authentication), but this is still much more secure than the old fixed PIN code
 * approach prior to BT 2.0.
 * 
 * @param app The application state./
 * @param m The CL_SM_REMOTE_IO_CAPABILITY_IND message pointer.
 *
 * @returns void.
 */
static void cl_sm_remote_io_capability_ind(
                    MAIN_APP_T *app,
                    const CL_SM_REMOTE_IO_CAPABILITY_IND_T *m
                    )
{
    if (app->debug) 
    {
        print("DBG: cl_sm_remote_io_capability_ind\r\n");
        print("     Auth:    %d\r\n", m->authentication_requirements);
        print("     I/O Cap: %d\r\n", m->io_capability);
        print("     BD Addr: %B\r\n", &m->bd_addr);
        
        print("     Active : %B\r\n", &ACTIVE.addr);
    }
    
    /* TO DO: This should be the same device that is opening an RFCOMM channel. */
}            



/*!
 * @brief Handled CL_SM_IO_CAPABILITY_REQ_IND from Connection library.
 *
 * This application uses the 'Just Works' association model - there is no 
 * Man In The Middle (MITM) protection (Authentication).
 * 
 * @param app The application state.
 * @param m The CL_SM_IO_CAPABILITY_REQ_IND message pointer.
 *
 * @returns void.
 */
static void cl_sm_io_capability_req_ind(MAIN_APP_T *app)
{
    if (app->debug) print("DBG: cl_sm_io_capability_req_ind\r\n");

    ConnectionSmIoCapabilityResponse(
            &ACTIVE.addr,                           /*  Active connection. */ 
            cl_sm_io_cap_no_input_no_output,        /* 'Just Works' */
            FALSE,                                  /* Force MITM - no. */
            TRUE,                                   /* Bonding - Yes. */
            FALSE,                                  /* Out of Band Data - No. */
            0,
            0);
}

/*!
 * @brief Handled CL_SM_AUTHORISE_IND from Connection library.
 *
 * Automatically authorise the incomming connection if it is an incoming
 * RFCOMM connection to the RFCOMM server channel.
 * 
 * @param app The application state.
 * @param m The CL_SM_IO_CAPABILITY_REQ_IND message pointer.
 *
 * @returns void.
 */
static void cl_sm_authorise_ind(MAIN_APP_T *app, const CL_SM_AUTHORISE_IND_T *m)
{
    if (app->debug) 
    {
            print("DBG: cl_sm_authorise_ind\r\n");
            print("     protocol_id: %d\r\n", m->protocol_id);
            print("     channel:     %d\r\n", m->channel);
            print("     incoming:    %d\r\n", m->incoming);
    }
            
    ConnectionSmAuthoriseResponse(
            &m->bd_addr,
            m->protocol_id,
            m->channel,
            m->incoming,
            (bool)TRUE
            );
}

/*!
 * @brief Handled MESSAGE_MORE_DATA from Firmware
 *
 * Identify the source ID e.g. UART or RFCOMM stream. Uart stream -> ui parser
 * RFCOMM stream -> Rx message.
 * 
 * @param app The application state.
 * @param m The MESSAGE_MORE_DATA message pointer.
 *
 * @returns void.
 */
static void message_more_data(MAIN_APP_T *app, const MessageMoreData *m)
{
    /* No debug or it messes up the UI */
    if (m->source == app->uart_source) 
    {
        ui_parser(app, m->source);
    }
    else
    {
        uint16 i;
        for (i=0; i<MAX_CONNECTIONS; i++)
        {
            if (
                app->connection[i].state == STATE_CONNECTED &&
                m->source == StreamSourceFromSink(app->connection[i].sink)
                )
            {
                const uint8 *data = SourceMap(m->source);
                uint16 len = SourceSize(m->source);
                    
                /* If there is data in the source, copy it and make it a NULL terminated
                 * string.
                 */
                if (len)
                {
                    uint8 *string = PanicUnlessMalloc((len+1) * sizeof(uint8));
                    
                    memmove(string, data, len);
                    string[len] = '\0';
                    print("Rx %d \"%s\"\r\n", i, string);
                    
                    /* clean up */
                    free(string); 
                    SourceDrop(m->source, len);
                }
            }
        }
    }
}

/*!
 * @brief Message handler for messages from the connection libary OR application itself.
 *
 * @param task Task handler pointer.
 * @param id The unique message id.
 * @param msg Pointer to the received message.
 *
 * @Returns void
 */
static void message_handler(Task task, MessageId id, Message msg)
{
    MAIN_APP_T *app = (MAIN_APP_T *)task;
    
    switch( id ) 
    {
        case CL_INIT_CFM:
            cl_init_cfm(app, (CL_INIT_CFM_T *)msg);
            break;
            
        case CL_DM_LOCAL_BD_ADDR_CFM:
            cl_dm_local_bd_addr_cfm(app, (CL_DM_LOCAL_BD_ADDR_CFM_T *)msg);
            break;
            
        case CL_DM_LOCAL_NAME_COMPLETE:
            cl_dm_local_name_complete(app, (CL_DM_LOCAL_NAME_COMPLETE_T *)msg);
            break;
            
        case CL_RFCOMM_REGISTER_CFM:
            cl_rfcomm_register_cfm(app, (CL_RFCOMM_REGISTER_CFM_T *)msg);
            break;
            
        case CL_SDP_REGISTER_CFM:
            cl_sdp_register_cfm(app, (CL_SDP_REGISTER_CFM_T *)msg);
            break;
            
        case CL_SDP_UNREGISTER_CFM:
            cl_sdp_unregister_cfm(app, (CL_SDP_UNREGISTER_CFM_T *)msg);
            break;
            
        case CL_SM_REMOTE_IO_CAPABILITY_IND:
            cl_sm_remote_io_capability_ind(app, (CL_SM_REMOTE_IO_CAPABILITY_IND_T *)msg);
            break;
            
        case CL_SM_IO_CAPABILITY_REQ_IND:
            cl_sm_io_capability_req_ind(app);
            break;
                         
        case CL_SM_AUTHORISE_IND:
            cl_sm_authorise_ind(app, (CL_SM_AUTHORISE_IND_T *) msg);
            break;
         
        case CL_RFCOMM_CONNECT_IND:
            cl_rfcomm_connect_ind(app, (CL_RFCOMM_CONNECT_IND_T *)msg);
            break;
            
        case CL_RFCOMM_SERVER_CONNECT_CFM:
            cl_rfcomm_server_connect_cfm(app, (CL_RFCOMM_SERVER_CONNECT_CFM_T *)msg);
            break;   
            
        case CL_DM_INQUIRE_RESULT:
            cl_dm_inquire_result(app, (CL_DM_INQUIRE_RESULT_T *)msg);
            break;
            
        case CL_SM_REGISTER_OUTGOING_SERVICE_CFM:
           cl_sm_register_outgoing_service_cfm(
                   app, 
                   (CL_SM_REGISTER_OUTGOING_SERVICE_CFM_T *)msg
                   );
           break;
           
        case CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM:
           cl_sdp_service_search_attribute_cfm(
                   app,
                   (CL_SDP_SERVICE_SEARCH_ATTRIBUTE_CFM_T *)msg
                   );
           break;
           
        case CL_RFCOMM_CLIENT_CONNECT_CFM:
           cl_rfcomm_client_connect_cfm(
                   app,
                   (CL_RFCOMM_CLIENT_CONNECT_CFM_T *)msg
                   );
           break;    
           
        case CL_DM_ACL_OPENED_IND:
            cl_dm_acl_opened_ind(app, (CL_DM_ACL_OPENED_IND_T *)msg);
            break;
            
        case CL_RFCOMM_DISCONNECT_CFM:
            cl_rfcomm_disconnect_cfm(app, (CL_RFCOMM_DISCONNECT_CFM_T *)msg);
            break;
            
        case CL_RFCOMM_DISCONNECT_IND:
            cl_rfcomm_disconnect_ind(app, (CL_RFCOMM_DISCONNECT_IND_T *)msg);
            break;

        case CL_DM_ACL_CLOSED_IND:
            cl_dm_acl_closed_ind(app, (CL_DM_ACL_CLOSED_IND_T *)msg);
            break;
            
        /* 
         * System messages for streams
         */   
        case MESSAGE_MORE_DATA:
            message_more_data(app, (MessageMoreData *)msg);
            break;   
            
        /* 
         * Application specific messages 
         */
        case MSG_CONNECT_SLAVE:                 /* UI Command: connect slave */
           connect_slave(app);
           break;
           
        case MSG_SLAVE_CONNECTION_TIMEOUT:
           print("Slave connection timed out.\r\n");
           stop_slave_connection(app);
           reset_active_connection(app);
           break;
           
        case MSG_CONNECT_MASTER:
           connect_master(app);
           break;

        case MSG_DISCONNECT:
           disconnect(app, (MSG_DISCONNECT_T *)msg);
           break;
           
        /* 
         * The following messages are not handled but can be useful when debugging. 
         */
            
        case CL_SM_AUTHENTICATE_CFM:
            if (app->debug)
            {
                const CL_SM_AUTHENTICATE_CFM_T *m = (CL_SM_AUTHENTICATE_CFM_T *)msg;
                print("DBG: CL_SM_AUTHENTICATE_CFM\r\n");
                print("     bdaddr: %B\r\n", &m->bd_addr); 
                print("     status: 0x%x\r\n", m->status); 
                print("     key type: %d\r\n", m->key_type);
                print("     bonded:   %s\r\n", (m->bonded) ? "yes" : "no");
            }
            break;
            
        case CL_RFCOMM_CONTROL_IND:
            if (app->debug)
            {
                const CL_RFCOMM_CONTROL_IND_T *m = (CL_RFCOMM_CONTROL_IND_T *)msg;
                print("DBG: CL_RFCOMM_CONTROL_IND\r\n");
                print("     sink:         0x%x\r\n", m->sink);
                print("     break_signal: %d\r\n", m->break_signal);
                print("     modem_signal: %d\r\n", m->modem_signal);
            }
            break;
            
        case CL_RFCOMM_LINE_STATUS_IND:
            if (app->debug)
            {
                const CL_RFCOMM_LINE_STATUS_IND_T *m = (CL_RFCOMM_LINE_STATUS_IND_T *)msg;
                print("DBG: CL_RFCOMM_LINE_STATUS_IND\r\n");
                print("     sink:         0x%x\r\n", m->sink);
                print("     error:        %s\r\n", (m->error) ? "yes" : "no");
                print("     status_error: 0x%x\r\n", m->line_status);
            }
            break;

        case MESSAGE_SOURCE_EMPTY:
            if (app->debug)
            {
                MessageSourceEmpty *m = (MessageSourceEmpty *)msg;
                print("DBG: MESSAGE_SOURCE_EMPTY 0x%x\r\n", m->source);
            }
            break;
            
        case MESSAGE_MORE_SPACE:
            if (app->debug)
            {
                MessageMoreSpace *m = (MessageMoreSpace *)msg;
                print("DBG: MESSAGE_MORE_SPACE 0x%x\r\n", m->sink);
            }
            break;
            
        case CL_SM_ENCRYPTION_KEY_REFRESH_IND:
            if (app->debug) print("DBG: CL_SM_ENCRYPTION_KEY_REFRESH_IND\r\n");
            break;

        case CL_SM_ENCRYPTION_CHANGE_IND:
            if (app->debug) print("DBG: CL_SM_ENCRYPTION_CHANGE_IND\r\n");
            break;
            
        default:
            print("ERROR: Unhandled message id 0x%x\r\n", id);
            break;
    }
}

/*!
 * @brief Main function
 *
 * Initalises the MAIN_APP_T state structure and initiates the connection library before
 * entering the Firmware message loop, from which it should never return.
 *
 * @returns Interger, but this should never happen.
 */
int main(void)
{
    print(SALUTATION);
    
    app.task.handler = message_handler;
    app.debug = FALSE;
    app.active = NO_ACTIVE;
    app.conn_count = 0;
    app.role = ROLE_NONE;
    
    {  /* Intialise the connections list */
        uint16 i;
        for (i=0; i<MAX_CONNECTIONS; i++) 
        {
            memset(&app.connection[i], 0, sizeof(CONN_STATE_T));
        }
    }
    
    MessageSinkTask(StreamUartSink(), (Task)&app);
    SinkConfigure(StreamUartSink(), VM_SINK_MESSAGES, VM_MESSAGES_NONE);
    app.uart_source = StreamSourceFromSink(StreamUartSink());
 
    ConnectionInit((Task)&app);
    
    print("Intialising.\r\n");
    MessageLoop();    

    /* Never gets here. */    
    return 0;
}
