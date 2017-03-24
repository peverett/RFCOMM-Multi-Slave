/*!
 * @file command.c
 * 
 * @brief Command handling for RFCOMM Multi Slave application.
 */

#include <util.h>
#include <stdlib.h>
#include <panic.h>
#include <sink.h>
#include <string.h>
#include <vm.h>

#include "rfcomm_multi_slave.h"

/* C defines blank as space, \f, \n, \r, and \t, but space is enough for now */
#define isblank(c)      (c == ' ')

#define PARAMS()        (params < uart_end)

#ifdef ENABLE_HELP
#   define COMMAND_HELP(msg)                     \
    if (!params) {                               \
        print(msg);                              \
        return TRUE;                             \
    }
#   define COMMAND_HELP_MULTI(msg, isLast)       \
    if (!params) {                               \
        print(msg);                              \
    if (isLast) return TRUE;                     \
    }
#else
#   define COMMAND_HELP(msg)
#   define COMMAND_HELP_MULTI(msg, isLast)
#endif

/*************************************************************************
NAME    
    cmdcmp
    
DESCRIPTION
    Check if a string matches a command.

    Upper case part of the command is mandatory, lower case optional.

RETURNS

*/
static int cmdcmp(const uint8 *s, const uint8 **endp, const char *cmd)
{
    /* skip leading whitespace */
    while (s < uart_end && isblank(*s)) s++;

    if (endp) {
        *endp = s;
    }
    
    for (; s < uart_end; s++, cmd++)
    {
        if (*s != *cmd)
        {
            if (isblank(*s)) break;
            if ((*s | 0x20) != (*cmd | 0x20)) return *s - *cmd;
        }
    }

    /* check that we are in lowercase (optional) part of cmd */
    if (!*cmd || (*cmd & 0x20))
    {
        if (endp) {
            while (s < uart_end && isblank(*s)) s++; /* skip spaces */
            *endp = s;
        }
        
        return 0;
    }
    
    return -1;
}

/*************************************************************************
NAME    
    ch_to_u8
    
DESCRIPTION
    Convert a character into a number.

RETURNS

*/
static bool ch_to_u8(const uint8 ch, uint8 *num)
{
    if (ch >= '0' && ch <= '9')
        *num = ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        *num = ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F')
        *num = ch - 'A' + 10;
    else return FALSE;

    return TRUE;
}

/*************************************************************************
NAME    
    cmd_parse_num
    
DESCRIPTION
    Parse uint16 from input stream.

RETURNS

*/
static bool cmd_parse_num(const uint8 *s,
                          const uint8 **endp,
                          uint16 *num)
{
    bool rc = TRUE;
    uint8 i;
    
    while (s < uart_end && isblank(*s)) s++;

    if (s == uart_end) return FALSE;
    
    if (s + 2 < uart_end &&
        s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X'))
    {
        *num = 0;
        
        for (s += 2; s < uart_end; s++)
        {
            if (ch_to_u8(*s, &i))
                *num = (*num << 4) | i;
            else
                break;
        }
    }
    else
    {
        const uint8 *sp = s;
        s = UtilGetNumber(s, uart_end, num);

        if (!s)
        {
            s = sp;
            rc = FALSE;
        }
    }
    
    if (endp)
    {
        *endp = s;
    }

    return rc;
}

#if 0
/*************************************************************************
NAME    
    cmd_parse_bdaddr
    
DESCRIPTION
    Parse Typed Bluetooth address from input stream.

RETURNS

*/

static bool cmd_parse_bdaddr(const uint8 *s,
                             const uint8 **endp,
                             bdaddr      *addr)
{
    uint16 i;
    uint8 num;
    bool rc = TRUE;

    while (s < uart_end && isblank(*s)) s++;

    if (s + 2 < uart_end &&
        s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X'))
    {
        s += 2;

        memset(addr, 0, sizeof(bdaddr));
        
        for (i = 0; i < 12 && s < uart_end; i++, s++)
        {
            if (!ch_to_u8(*s, &num))
            {
                rc = FALSE;
                break;
            }
            
            if (i < 4)
            {
                addr->nap = (addr->nap << 4) | num;
            }
            else if (i < 6)
            {
                addr->uap = (addr->uap << 4) | num;
            }
            else
            {
                addr->lap = (addr->lap << 4) | num;                
            }
        }

    }
    else
    {
        rc = FALSE;
    }

    if (endp)
    {
        *endp = s;
    }
    
    return rc;
}
#endif


/*************************************************************************
NAME    
    cmd_parse_value
    
DESCRIPTION
    Parse generic data from input stream.

NOTE
    Data can be formatted either as "string", 0xhexnum, num, or any
    combination of those. All numbers are maximum 16 bits.

RETURNS

*/
static bool cmd_parse_value(const uint8 *s,
                            const uint8 **endp,
                            uint16 *plen,
                            uint8 **prc)
{
    bool in_str = FALSE;
    uint16 len = 0;
    uint16 num;
    uint8 *rc = NULL;

    rc = PanicUnlessMalloc(uart_end - s);

    /* the way we parse data means that stream length is always greater than
     * the resulting string length. */
    while (s < uart_end)
    {
        /* start/end of string */
        if (*s == '"') in_str = !in_str;

        /* string */
        else if (in_str) rc[len++] = *s;

        /* blank */
        else if (isblank(*s))
        {
            /* skip blanks */
            do s++; while (s < uart_end && isblank(*s));
            continue; /* do not advance s at the end */
        }

        /* number */
        else if (cmd_parse_num(s, &s, &num)) rc[len++] = num;

        /* failure */
        else {
            /* break the loop claiming we're in the middle of a string as
             * that will be counted as an error. */
            in_str = TRUE;
            break;
        }

        /* jump to next character */
        s++;
    }

    /* in case of error clean up */
    if (in_str || !len)
    {
        free(rc);
        rc = NULL;
        len = 0;
    }
    
    *plen = len;
    *prc = rc;
    if (endp) *endp = s;

    /* in_str means that we encountered an error */
    return !in_str;
}

/*!
 * @brief Outputs the current application state.
 * 
 * Can be executed in any application state.
 *
 * @param app The application state.
 * @param params Command parmeters.
 *
 * @returns Always returns true, as params are ignored.
 */
static bool cmd_state(MAIN_APP_T *app, const uint8 *params)
{
    uint16 i;
    
    COMMAND_HELP(
            "help state\r\n"
            );
    
    print("%s (%B)", app->own_name, &app->own_addr);
    switch(app->role)
    {
        case ROLE_NONE: 
            print("\r\n");
            break;
        case ROLE_MASTER:
            print(" is master\r\n");
            break;
        case ROLE_SLAVE:
            print(" is slave\r\n");
            break;
    }
    
    for (i=0; i<MAX_CONNECTIONS; i++)
    {
        print("%d: %B, ", 
              i,
              &app->connection[i].addr
              );
        
        switch(app->connection[i].role)
        {
            case ROLE_NONE:
                print("None, ");
                break;
            case ROLE_MASTER:
                print("Master, ");
                break;
            case ROLE_SLAVE:
                print("Slave, ");
                break;
        }
                
        switch(app->connection[i].state)
        {
            case STATE_DISCONNECTED:
                print("Disconnected");
                break;
            case STATE_DISCONNECTING:
                print("Disconnecting");
                break;
            case STATE_CONNECTING:
                print("Connecting");
                break;
            case STATE_CONNECTED:
                print("Connected");
                break;
            case STATE_PAIRING:
                print("Pairing");
                break;
            default:
                print("Unknown (%d)", app->connection[i].state);
                break;
        }
        print("\r\n");
    }
    
    return TRUE;
}

/*!
 * @brief Start an RFCOMM connection, either as master or a slave.
 *
 * @param app The application state.
 * @param params 'master' or 'slave'
 *
 * @returns Always returns true, as params are ignored.
 */
static bool cmd_connect(MAIN_APP_T *app, const uint8 *params)
{
    bool master = FALSE;
    
    COMMAND_HELP(
            "help connect {master|slave}\r\n"
            );
    
    if (!PARAMS())
        return FALSE;
    else if ( cmdcmp(params, &params, "Master") == 0 )
        master = TRUE;
    else if ( cmdcmp(params, &params, "Slave") == 0 )
        master = FALSE;
    else
        return FALSE;
    
    if (master)
    {
        /* A master can have up to two slave connections. */
        if (app->role == ROLE_SLAVE)
        {
            print("ERROR: Already connected as slave.\r\n");
        }
        else if (app->role == ROLE_MASTER && app->conn_count == MAX_CONNECTIONS)
        {
            print("ERROR: Already have %d slave connections.", MAX_CONNECTIONS);
        }
        else /* Either master with only 1 connection or role is NONE. */
        {
            print("Connecting as Master.\r\n");
            MessageSend(&app->task, MSG_CONNECT_MASTER, 0);
        }
    }
    else /* Slave */
    {
        if (app->role == ROLE_NONE)
        {
            print("Connecting as Slave.\r\n");
            MessageSend(&app->task, MSG_CONNECT_SLAVE, 0);
        }
        else 
        {
            print(
                "ERROR: Already connected as %s.\r\n", 
                (app->role == ROLE_SLAVE) ? "slave" : "master"
                );
        }
    }
    return TRUE;
}

/*!
 * @brief Indicate current debug mode, or turn debug on or off.
 *
 * @param app The application state.
 * @param params 'on' or 'off' or no param to report state.
 *
 * @returns Always returns true, as params are ignored.
 */
static bool cmd_debug(MAIN_APP_T *app, const uint8 *params)
{
    COMMAND_HELP(
            "help debug [on|off]\r\n"
            );
    
       
    if ( cmdcmp(params, &params, "ON") == 0 )
        app->debug = TRUE;
    else if ( cmdcmp(params, &params, "OFF") == 0 )
        app->debug = FALSE;
    else if (PARAMS())
        return FALSE;
    
    print("Debug mode: %s\r\n", (app->debug) ? "On" : "Off");
    return TRUE;
}
    
/*!
 * @brief Disconnect RFCOMM link
 *
 * @param app The application state.
 * @param params RFCOMM link id, a number starting 0 to MAX_CONECTION-1.
 *
 * @returns Always returns true, as params are ignored.
 */
static bool cmd_disconnect(MAIN_APP_T *app, const uint8 *params)
{
    uint16 link_id;
    MSG_DISCONNECT_T *msg;
    
    COMMAND_HELP(
            "help disconnect [link_id]\r\n"
            );
    
    if (!PARAMS()) 
        link_id = 0xFFFF;
    else if (!cmd_parse_num(params, &params, &link_id)) 
        return FALSE;
    
    if (app->conn_count == 0)
    {
        print("ERROR: No links to disconnect.\r\n");
        return TRUE;
    }
  
    if (link_id == 0xFFFF)
    {
        int i;
        
        for (i=0; i<MAX_CONNECTIONS; i++) 
        {
            if (app->connection[i].state == STATE_CONNECTED) 
            {
                msg = PanicUnlessNew(MSG_DISCONNECT_T);
                msg->link_id = i;
                MessageSend(&app->task, MSG_DISCONNECT, msg);
            }
        }
    }
    else if (link_id < MAX_CONNECTIONS) 
    {
        if (app->connection[link_id].state == STATE_CONNECTED)
        {
            msg = PanicUnlessNew(MSG_DISCONNECT_T);
            msg->link_id = link_id;
            MessageSend(&app->task, MSG_DISCONNECT, msg);
        }
        else
        {
            print("ERROR: Link %d is not connected.\r\n", link_id);
        }
    }
    else
    {
        print("ERROR: Link id %d is out of range 0..%d\r\n", link_id, MAX_CONNECTIONS-1);
    }
    return TRUE;

}


/*!
 * @brief Disconnect RFCOMM link
 *
 * @param app The application state.
 * @param params RFCOMM link id, a number starting 0 to MAX_CONECTION-1.
 *
 * @returns Always returns true, as params are ignored.
 */
static bool cmd_tx(MAIN_APP_T *app, const uint8 *params)
{
    uint16 link_id;
    uint8  *s;
    uint16 len;
    
    COMMAND_HELP(
            "help tx [link_id] \"string to send\"\r\n"
            );
    
    if (!cmd_parse_num(params, &params, &link_id)) 
        return FALSE;
    
    if (!cmd_parse_value(params, &params, &len, &s))
        return FALSE;
    
    if (app->conn_count == 0)
    {
        print("ERROR: No connections.\r\n");
    }
    else if (link_id < MAX_CONNECTIONS) 
    {
        if (app->connection[link_id].state == STATE_CONNECTED)
        {
            uint16 offs;
            uint8 *data;
    
            while ( (SinkSlack(app->connection[link_id].sink)) < len )  
            {
                SinkFlush(app->connection[link_id].sink, 1);
            }
    
            if (
                (offs = SinkClaim(app->connection[link_id].sink, len)) != 0xffff &&
                (data = SinkMap(app->connection[link_id].sink))
                )
            {
                memmove(data + offs, s, len);
                SinkFlush(app->connection[link_id].sink, len);
            }  
            else
            {
                if (app->debug) print("DBG: Tx SinkClaim or SinkMap failed!\r\n");
            }
        }
        else
        {
            print("ERROR: Link %d is not connected.\r\n", link_id);
        }
    }
    else
    {
        print("ERROR: Link id %d is out of range 0..%d\r\n", link_id, MAX_CONNECTIONS-1);
    }

    free(s);
    return TRUE;
   
}
/*************************************************************************

NAME    
    command_parse
    
DESCRIPTION
    Parse a command line and run correct handler.

RETURNS

*/
void command_parse(MAIN_APP_T *app, const uint8 *cmd)
{
    const uint8 *params = NULL;
    const uint8 **pparams = &params;
    bool ok = TRUE;
    
    /* skip blanks */
    while (cmd < uart_end && isblank(*cmd)) cmd++;

    if (cmd < uart_end)
    {
#ifdef ENABLE_HELP
        if (!cmdcmp(cmd, &cmd, "Help"))
        {
            /* help for a particular command */
            if (cmd < uart_end)
            {
                /* pparams == NULL means that the handler prints out help */
                pparams = NULL;

                /* NOTE: COMMAND_HELP(help_message) macro needs to be present
                 * in the beginning of every command handler. */
            }
            
            /* list all commands */
            else
            {
                print("help State       Get current state\r\n");
                print("help Connect     Start a master or slave connection\r\n");
                print("help DEbug       With debug on, extra event data is output\r\n");
                print("Help Disconnect  Disconnect a link.\r\n");
                print("help TX          Send data on a specific link.\r\n");

                return;
            }
        }
#endif /* ENABLE_HELP */
        
        if (!cmdcmp(cmd, pparams, "State"))
            ok = cmd_state(app, params);

        else if (!cmdcmp(cmd, pparams, "Connect"))
            ok = cmd_connect(app, params);
        
        else if (!cmdcmp(cmd, pparams, "DEbug"))
            ok = cmd_debug(app, params);

        else if (!cmdcmp(cmd, pparams, "Disconnect"))
            ok = cmd_disconnect(app, params);
        
        else if (!cmdcmp(cmd, pparams, "TX"))
            ok = cmd_tx(app, params);

        else
            print("ERROR: Unknown command.\r\n");
        
        if (!ok)
            print("ERROR: Invalid command parameters.\r\n");
    }
}




/* End-of-File */
