/*!
 * @file ui.c
 * 
 * @brief  User interface (UART) handling.
 */

#include <stdio.h>
#include <stdarg.h>
#include <source.h>
#include <sink.h>
#include <stream.h>
#include <string.h>

#include "rfcomm_multi_slave.h"

const uint8 *uart_end = NULL;
static const char *hex = "0123456789abcdef";

/*************************************************************************
NAME    
    uart_copy
    
DESCRIPTION
    Copy a string into the UART sink.

RETURNS
    Number of bytes copied
*/
static uint16 uart_copy(const char *s, uint16 len)
{
    uint16 offs;
    uint8 *data;
    
    while ( (SinkSlack(StreamUartSink())) < len )  
    {
        SinkFlush(StreamUartSink(), 1);
    }
    
    if ((offs = SinkClaim(StreamUartSink(), len)) != 0xffff &&
        (data = SinkMap(StreamUartSink())))
    {
        memmove(data + offs, s, len);
        return len;
    }
    return 0;
}

/*************************************************************************
NAME    
    u8_to_uart
    
DESCRIPTION
    Convert uint8 to a string and copy it into the UART sink

RETURNS

*/
static void u8_to_uart(uint8 num)
{
    char buf[2];

    buf[0] = hex[(num >>  4) & 0xf];
    buf[1] = hex[(num >>  0) & 0xf];

    uart_copy(buf, 2);
}

/*************************************************************************
NAME    
    u16_to_uart
    
DESCRIPTION
    Convert uint16 to a hex string and copy it into the UART sink

RETURNS

*/
static void u16_to_uart(uint16 num)
{
    char buf[4];

    buf[0] = hex[(num >> 12) & 0xf];
    buf[1] = hex[(num >>  8) & 0xf];
    buf[2] = hex[(num >>  4) & 0xf];
    buf[3] = hex[(num >>  0) & 0xf];

    uart_copy(buf, 4);
}

/*************************************************************************
NAME    
    i16_to_uart
    
DESCRIPTION
    Convert int16 to a decimal string and copy it into the UART sink

RETURNS

*/
static void i16_to_uart(int16 num)
{
    char buf[6]; /* maximum length for a signed 16 bit decimal */
    char *p = &buf[6];
    bool neg;

    if (num < 0)
    {
        neg = TRUE;
        num = ~num + 1;
    }
    else
    {
        neg = FALSE;
    }
    
    do
    {
        *(--p) = '0' + (num % 10);
        num /= 10;
    } while (num);

    if (neg)
    {
        *(--p) = '-';
    }

    uart_copy(p, 6 - (p - buf));
}

/*************************************************************************
NAME    
    passkey_to_uart
    
DESCRIPTION
    Convert 6-digit passkey to a decimal string and copy it into the UART
    sink

RETURNS

*/
static void passkey_to_uart(uint32 num)
{
    char buf[6];
    char *p = &buf[6];

    memset(buf, '0', 6);    
    
    do
    {
        *(--p) = '0' + (num % 10);
        num /= 10;
    } while (num);

    uart_copy(buf, 6);
}

/*************************************************************************
NAME    
    addrt_to_uart
    
DESCRIPTION
    Copy a typed Bluetooth address into the UART sink.

RETURNS
    Number of bytes copied
*/
static void addr_to_uart(bdaddr *addr)
{
    char buf[14];

    buf[ 0] = '0';
    buf[ 1] = 'x';
    buf[ 2] = hex[(addr->nap >> 12) & 0xf];
    buf[ 3] = hex[(addr->nap >>  8) & 0xf];
    buf[ 4] = hex[(addr->nap >>  4) & 0xf];
    buf[ 5] = hex[(addr->nap >>  0) & 0xf];

    buf[ 6] = hex[(addr->uap >>  4) & 0xf];
    buf[ 7] = hex[(addr->uap >>  0) & 0xf];

    buf[ 8] = hex[(addr->lap >> 20) & 0xf];
    buf[ 9] = hex[(addr->lap >> 16) & 0xf];
    buf[10] = hex[(addr->lap >> 12) & 0xf];
    buf[11] = hex[(addr->lap >>  8) & 0xf];
    buf[12] = hex[(addr->lap >>  4) & 0xf];
    buf[13] = hex[(addr->lap >>  0) & 0xf];

    /* only print type if it's not public */
    uart_copy(buf, 14);
}

/*************************************************************************
NAME    
    print
    
DESCRIPTION
    Simple formatting print command outputting to UART.

    This is to avoid having buffer for vsprintf.

    Following formatters are supported:

    %% print %
    %B print typed_bdaddr
    %c print character
    %d print signed 16-bit number in decimal
    %s print null terminated string
    %U print UUID
    %x print unsigned 16-bit number in hex (4 digits)
    %X print unsigned 8-bit number in hex (2 digits)

RETURNS
    
*/
void print(const char *fmt, ...)
{
    va_list ap;
    const char *str;
    void *p;

    va_start(ap, fmt);
    str = fmt;
    
    while (*fmt)
    {
        if (*(fmt++) == '%')
        {
            uart_copy(str, fmt - str - 1);
            
            switch (*(fmt++))
            {
                case '%':
                    str = fmt - 1;
                    continue;

                case 'B':
                    p = va_arg(ap, void *);
                    addr_to_uart(p);
                    break;

                case 'c':
                    p = (void*)va_arg(ap, unsigned int);
                    uart_copy((char*)&p, 1);
                    break;

                case 'd':
                    i16_to_uart(va_arg(ap, signed int));
                    break;
                    
                case 's':
                    p = va_arg(ap, char *);
                    uart_copy(p, strlen(p));
                    break;

                case 'P':
                    passkey_to_uart(va_arg(ap, unsigned long));
                    break;
                    
                case 'x':
                    u16_to_uart(va_arg(ap, unsigned int));
                    break;
                    
                case 'X':
                    u8_to_uart(va_arg(ap, unsigned int));
                    break;
            }

            str = fmt;
        }
    }

    uart_copy(str, fmt - str);

    /* flush the sink */
    SinkFlush(StreamUartSink(), SinkClaim(StreamUartSink(), 0));
}

#if 0
/*************************************************************************
NAME    
    fancy_print
    
DESCRIPTION
    Print binary data in human readable format.

RETURNS

*/
void fancy_print(const uint8 *p, uint16 len)
{
    enum { none, num, ch } printmode = none;
    uint16 i;

    for (i = 0; i < len; i++)
    {
        char *delim;
            
        if (p[i] > 0x1f && p[i] < 0x7f)
        {
            switch (printmode)
            {
                case none:
                    delim = "\"";
                    break;
                    
                case num:
                    delim = " \"";
                    break;
                    
                case ch:
                default:
                    delim = "";
            }
            print("%s%c", delim, p[i]);
            printmode = ch;
        }
        else
        {
            switch (printmode)
            {
                case none:
                    delim = "";
                    break;

                case num:
                    delim = " ";
                    break;

                case ch:
                default:
                    delim = "\" ";
            }
                
            print("%s%X", delim, p[i]);
            printmode = num;
        }
    }
    if (printmode == ch) print("\"");
}
#endif 

/*************************************************************************
NAME    
    ui_parser
    
DESCRIPTION
    Handle reading UART source and dispatch command handlers.

RETURNS

*/
void ui_parser(MAIN_APP_T *app, Source src)
{
    static uint16 pos = 0;
    const uint8 *data;
    uint16 len;
    uint16 i;
    bool cmd = FALSE;

    if (!(data = SourceMap(src))) return;

    while ((len = SourceSize(src)) > pos)
    {
        /* search for line ending */
        for (i = pos; i < len; i++)
        {
            if (data[i] == '\r' || data[i] == '\n')
            {
                cmd = TRUE;
                break;
            }
        }

        /* echo */
        uart_copy((char*)&data[pos], i - pos);

        SinkFlush(StreamUartSink(), SinkClaim(StreamUartSink(), 0));
        pos = i;

        /* check for command */
        if (cmd)
        {
            print("\r\n");

            /* set end mark */
            uart_end = data + pos;

            /* run command parser */
            command_parse(app, data);
            
            if (pos + 1 < len && data[pos + 1] == '\n')
                SourceDrop(src, pos + 2); /* drop \r\n */
            else
                SourceDrop(src, pos + 1); /* drop \r */
                
            cmd = pos = 0;
        }
    }
}

/* End-of-File */
