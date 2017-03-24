/*!
 * @file rfcomm_multi_slave.h
 *
 * @brief Constant definitions, eumus, structures and function prototypes.
 */

#ifndef __MAIN_PRIVATE_H
#define __MAIN_PRIVATE_H

#include <connection.h>

/*!
 * @brief Class of Device
 *
 * - Major Service Class (Bits 24 to 13)
 * -- Bit 17 - Networking 
 * -- Bit 13 - Limited Discoverable Mode
 * -- All other bits are 0
 * - Major Device Classes (Bits 12 to 8)
 * -- all set to 1 to indicate 'Uncategorized: (device code not specified)'
 * - Minor Device Class field (bits 7 to 2)
 * -- Set to 1 1 1 1 0 0 - custom for this application
 * - Format (bits 1 and 0)
 * -- Format #1 - bits are 0 0
#define CLASS_OF_DEVICE 0x023FF0
 */
#define CLASS_OF_DEVICE 0x002F00

/*!
 * @brief Limited Discovery Inquiry Access Code (LIAC)
 */
#define LIAC 0x9E8B00

/*!
 * @brief General/Unlimited Unquiry Access Code (GIAC)
 */
#define GIAC 0x9E8B33

/*!
 * @brief Maximum Device Name string size 
 *
 * The device name can be longer but we are only going to cache and display a maximum 
 * ammount of characters - 20 characters + 1 for NULL terminator
 */
#define MAX_OWN_NAME 21

/*! 
 * @brief Maximum connections
 *
 * If Master, can have two slave connections.
 * If slave, can only have one master connection.
 */
#define MAX_CONNECTIONS 2

/*
 * @brief Indicates NO active connection setup.
 */
#define NO_ACTIVE 0xFF

/*!
 * @brief Application task state.
 */
typedef enum {
    STATE_DISCONNECTED,     /*!< Disconnected - default state.*/
    STATE_DISCONNECTING,    /*!< Disconnecting. */
    STATE_CONNECTING,       /*!< Connecting. */
    STATE_PAIRING,          /*!< Pairing. */
    STATE_CONNECTED,        /*!< Connected. */
    STATE_LAST              /*!< This must always be the last enumeration value.*/
} STATE_ENUM_T;

/*!
 * @brief Connection role.
 */
typedef enum {
    ROLE_NONE,          /*! Not yet defined. */
    ROLE_MASTER,
    ROLE_SLAVE
} ROLE_ENUM_T;
/*!
 * @brief Application internal messages, indicating events or state change.
 */
typedef enum {
    MSG_CONNECT_SLAVE,
    MSG_CONNECT_MASTER,
    MSG_SLAVE_CONNECTION_TIMEOUT,
    MSG_DISCONNECT,
    MSG_LAST                /*!< This must always be the last application message. */
} APP_MESSAGES_IDS;


/*!
 * @brief Disconnect message
 */
typedef struct {
    uint16  link_id;
} MSG_DISCONNECT_T;

/*!
 * @brief Connection state information
 */
typedef struct
{
    bdaddr          addr;
    ROLE_ENUM_T     role;       /* Slave or Master */
    STATE_ENUM_T    state;
    Sink            sink;
} CONN_STATE_T;

/*!
 * @brief Main application data structure and state.
 */
typedef struct 
{
    TaskData        task;
    Source          uart_source;
    bool            debug;
    bdaddr          own_addr;
    char            own_name[MAX_OWN_NAME];
    uint16          rfcomm_server_channel;
    uint32          service_record_handle;
    CONN_STATE_T    connection[MAX_CONNECTIONS];
    uint16          conn_count;
    uint16          active;
    ROLE_ENUM_T     role;
} MAIN_APP_T;

extern MAIN_APP_T app;
extern const uint8 *uart_end;

/*!
 * @brief Shortcut macro to save typing.
 *
 * This is very naughty but also makes life easier.
 */
#define ACTIVE app->connection[app->active]

/*!
 * @brief Limited print function that works with the raw UART stream. 
 *
 * Avoids using a buffer and using vsprintf. 
 *
 * The following formates are supported:
 * - %% print %
 * - %B print Bluetooth Device Address
 * - %c print character
 * - %d print signed 16-bit number in decimal
 * - %s print NULL terminated string
 * - %x print unsigned 16-bit number in hex (4-digits)
 * - %X print unsigned 8-bit number in hex (2-digits)
 *
 * @param fmt const char pointer to null terminated string, which can contain formatting.
 * @param ... variable number of arguments that are to be formatted into the string.
 *
 * Returns void.
 */
void print(const char *fmt, ...);

/*!
 * @brief Reads the raw UART source stream and parses commands with their parameters.
 *
 * - Reads the UART source stream.
 * - Parses commands and calls the appropriate command handler to parse parameters for that 
 *   command.
 *
 * @param app The application task structure, used for dispatching messages.
 * @param src The raw UART source stream to read for command line input.
 *
 * @Returns void.
 */
void ui_parser(MAIN_APP_T *app, Source src);

/*!
 * @brief Parses the commands entered on the UART command line (ui_parser), and calls the
 * appropriate function to validate the command parameters.
 *
 * @param app The application task structure.
 * @param cmd The command string parameters.
 *
 * @Returns void.
 */
void command_parse(MAIN_APP_T *app, const uint8 *cmd);


#endif
