/*************************************************************************
|*
|*  VERSION CONTROL:    $Version$   $Date$
|*
|*
|*  IN PACKAGE:         MODEM Communication
|*
|*  COPYRIGHT:          Copyright (c) 2007, Altium
|*
|*  DESCRIPTION:        send modem AT commands and read modem results
|*
 */

#define MODEM_SMALL_MSG     512
#define INT2STR_BUFFER_SIZE 20

#include <timing.h>
#if ( __POSIX_KERNEL__ != 0 )
# include <pthread.h>
#else
#include <time.h>
#endif

#ifdef MODEM_DEBUG
#include <stdio.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <serial.h>
#include <fcntl.h>
#include <modem.h>

#include "modem_cfg_instance.h"

struct modem_drv_s
{
    void         *device;                // transceiver device
    posix_devctl_serial_impl_t serial;
    bool        dle_filter;             // filter <DLE> chars received from modem
    bool        blocking;               //
    uint32_t    speed;                  // bits/s
    uint32_t    delay;                  // modem processing time
    int32_t     kind;                   // modem kind
    bool        isRinging;              // Incoming call
    bool        isHangup;               // Phone is hang up
};

// device data
static modem_t modem_table[MODEM_INSTANCE_COUNT];

/**
 * @brief delay for the given amount of microseconds.
 *
 * @param usec        microseconds to wait
 *
 * @return Nothing.
 */
static void modem_sleep(unsigned long usec)
{
#if ( __POSIX_KERNEL__ != 0 )
    struct timespec ts = {0, usec * 1000};

    nanosleep(&ts, NULL);
#else
    clock_t delay;

    if (usec > 0)
    {
        delay = clock() + usec * (freq_hz()/1000000);
        while ( delay > clock() );
    }
#endif
}

/**
 * @brief Convert an integer into a char
 *
 * @param  value    integer to be converted into a string
 * @param  result   the resulting string
 *
 * @return Nothing.
 */
static void modem_int_to_str(int value, char * result)
{
    int i=0;
    int j=0;
    char s[INT2STR_BUFFER_SIZE];

    do
    {
        s[i++] = (char)(value % 10 + 48);
        value -= value % 10;
    } while((value /= 10) > 0);

    for (j=0; j < i; j++)
        result[i - 1 - j] = s[j];
    result[j] = 0;
}

/**
 * @brief Initialize the modem device
 *
 * This function initializes the communication with the modem device.
 *
 * @param id  Service id
 *
 * @return The modem service pointer for the specified device, or NULL on error.
 */
extern modem_t *modem_open(int id)
{
    modem_t *modem = &modem_table[id];
    modem_cfg_instance_t *cfg = &modem_instance_table[id];
    int fd;
    int serial_id;
    const char *name;

    assert(id >= 0 & id < MODEM_INSTANCE_COUNT);

    serial_id = modem_instance_table[id].serial;
    name = serial_instance_table[serial_id].name;

    fd = open(name, O_RDWR);

    posix_devctl(fd, DEVCTL_SERIAL_IMPL, &modem->serial, sizeof(posix_devctl_serial_impl_t), NULL);
    modem->speed      = modem->serial.f_get_baudrate(modem->serial.device);
    modem->delay      = cfg->delay;
    modem->dle_filter = cfg->filter_dle;
    modem->kind       = cfg->kind;
    modem->isRinging  = false;
    modem->isHangup   = false;

    if (fd >= 0)
    {
        return modem;
    }
    return NULL;
}


/*******************************************************************************
 *
 *
 *
 *  Generic MODEM commands
 *
 *
 *
 ******************************************************************************/
/**
 * @brief Read multiple characters from the modem
 *
 * Read the modem and wait until the expected response was received or size
 * number of characters where read or a timeout has occured . Put this to be
 * expected response in buf. When the* function exits buf contains the actual
 * modem response
 * Note: that some commands (eg. AT+IPR=PINCODE) take a long time before you
 * actually get a reponse. When using such commands wait before calling this
 * routine. Also note ther're commands resulting in two answers (eg. ATDT -->
 * OK and later CONNECT)
 *
 * @param modem     pointer to modem device struct
 * @param buf       expected reponse on entry and if size > 0 the
 *                  actual modem response on exit
 * @param size      maximum Number of characters to read and put in buf.
 *
 * @return The number of characters read.
 */
int modem_read(modem_t *modem, char *buf, size_t size)
{
    uint32_t timeout;
    // convert from bits/sec to chars/sec
    uint32_t charspeed = 1000 * 1000 * 10 / modem->speed;
    uint32_t timeoutcounter = 0;
    char     rsp[MODEM_SMALL_MSG] = "";
    int      ch;
    int      count = 0;
    int      ret = 0;
    int      i;

    // wait maximum timeout usec on modem response
    timeout = modem->delay + charspeed * size;
    do    // get answer, as long as data is received or timeout has occured
    {
        modem_sleep(charspeed);
        timeoutcounter += charspeed;
        ch = modem->serial.f_getchar(modem->serial.device);
        if (ch != -1)
        {
            if (!modem->dle_filter || (ch != '\x10'))
            {
                 rsp[count++] = (uint8_t)ch;
                 rsp[count] = 0;      // append a string termination character
            }
        }

        if (modem->kind == MODEM_KIND_MODEM_TELIT)
        {
            if (strstr(rsp, "ERROR")) { return 0; }
        }

        if (strstr(rsp, "RING"))
        {
            modem->isRinging = true;
        }

        if (strstr(rsp, "NO CARRIER"))
            modem->isHangup = true;

       if (size == 0)
       {
         if ((buf[0] && strstr(rsp, buf)))
         {
             for (i = 0; i<size; i++)
                 buf[i] = rsp[i];
             if (i)
                 buf[i] = 0;
             ret = count;
             break;
         }
       }
       else
       {
         if ((buf[0] && strstr(rsp, buf)) || ((count == (size - 1))))
         {
             for (i = 0; i<(size - 1); i++)
                 buf[i] = rsp[i];
             if (i)
                 buf[i] = 0;
             ret = count;
             break;
         }
       }

    }
    while (timeoutcounter < timeout);    // repeat until timout
#ifdef MODEM_DEBUG
    printf(" answer after %lums\n", timeoutcounter / 1000);
#endif
    return ret;
}

/**
 * @brief write a string to communications port
 *
 * @param modem     pointer to modem device struct
 * @param cmd       string to send to serial comm.
 *
 * @return number of bytes actually sent
 */
int modem_write(modem_t *modem, const char *cmd)
{
    int i;
    int size = strlen(cmd);
#ifdef MODEM_DEBUG
    printf("%s", cmd);
#endif
    while(modem->serial.f_getchar(modem->serial.device) != -1);
    for (i = 0; i < size; i++)
    {
        modem->serial.f_putchar(modem->serial.device, (unsigned char)*cmd++);
    }
    return i;
}


/**
 * @brief Send a modem initialisation string
 *
 * @param modem     pointer to modem device struct
 *
 * @return Zero if the modem was not initialized, non-zero otherwise
 */
int modem_send_init(modem_t *modem)
{
    if (modem->kind == MODEM_KIND_MODEM_TELIT ) // Telit
    {
        modem_write(modem, "AT\r");
        return modem_read(modem, "OK", 0)? 1 : 0;
    }
    else // Hayes
    {
        modem_write(modem, "AT&D0\\Q3M0E0\r");
        return modem_read(modem, "OK", 0)? 1 : 0;
    }
}


/**
 * @brief Hangup the modem
 *
 * @param modem     pointer to modem device struct
 *
 * @return Zero if the modem was not hangup, non-zero otherwise
 */
int modem_hangup(modem_t *modem)
{
    int ret;

    if (modem->kind == MODEM_KIND_MODEM_TELIT )
    {
        modem_write(modem, "ATH\r");
        ret = modem_read(modem, "OK", 0) ? 1 : 0;
    }
    else
    {
        modem_write(modem, "AT\r");
        if(!modem_read(modem, "OK", 0))
        {
            modem_write(modem, "\x10\x03");  // <DLE><ETX> goto command state from data state
            if (!modem_read(modem, "VCON", 0))
            {
                modem_sleep(1500000);
                modem_write(modem, "+++");   // goto online command mode from online data mode
            }
        }
        modem_write(modem, "ATH0\r");
        ret = modem_read(modem, "OK", 0)? 1 : 0;
    }

    modem->isRinging = false;
    if (ret)
        modem->isHangup = true;

    return ret;
}


/**
 * @brief Set modem in autoanswer mode
 *
 * @param modem     pointer to modem device struct
 *
 * @return   Zero if set to autoanswer has failed, non-zero otherwise
 */
int modem_set_autoanswer(modem_t *modem)
{
    modem_write(modem, "ATS0=1\r");
    return modem_read(modem, "OK", 0);
}


/*******************************************************************************
 *
 *
 *
 *  GSM MODEM specific commands
 *
 *
 *
 *
 ******************************************************************************/
/**
 * @brief Set/check GSM PIN code
 *
 * @param modem        pointer to modem device struct
 * @param pin_code     string containing the pincode
 *
 * @return   Zero if setting the pin failed, One on success
 */
int modem_enter_pin(modem_t *modem, const char *pin_code)
{
    char rsp[MODEM_SMALL_MSG] = "OK";
    int ret = 0;

    // check if PIN is needed
    modem_write(modem, "AT+CPIN?\r");
    if (modem_read(modem, rsp, MODEM_SMALL_MSG))
    {
        if (strstr(rsp, "+CPIN: SIM PIN") && pin_code)
        {
            // yes, we do need PIN
            modem_write(modem, "AT+CPIN=");
            modem_write(modem, pin_code);
            modem_write(modem, "\r");
            modem_sleep(10000000);
            modem_read(modem, "OK", 0);
            // refresh CPIN query to check if we passed the pin...
            modem_write(modem, "AT+CPIN?\r");
            strcpy(rsp, "OK");
            modem_read(modem, rsp, MODEM_SMALL_MSG);
        }
        if (strstr(rsp, "+CPIN: READY"))
        {
            ret = 1;
        }
    }
    return ret;
}


/**
 * @brief Check GSM-network connection
 *
 * Checks if GSM-network connection is OK (AT+CREG). Reply is like :
 * AT+REG: <n>, <stat>[, ....] stat should be 1 (checked in)
 *
 * @param modem     pointer to modem device struct
 *
 * @return 1 if network was found, 0 otherwise
 */
int modem_check_network(modem_t *modem)
{
    int i = 0;
    int ret = 0;
    char rsp[MODEM_SMALL_MSG] = "OK";

    // check if connection to GSM network is ok
    modem_write(modem, "AT+CREG?\r");
    modem_read(modem, rsp, MODEM_SMALL_MSG);
    // get the <stat> parameter from the response
    if (strstr(rsp, "+CREG:") != NULL)
    {
        while ((rsp[i] != ',') && (i < strlen(rsp)))
        {
            i++;
        }
        if (rsp[i] == ',')
        {
            i++;
            if( rsp[i] == '1' || rsp[i] == '5')
            {
                ret = 1;
            }
        }
    }
    return ret;
}


/**
 * @brief Get the gsm reception level and convert value on a scale 0-5
 *
 * @param modem     pointer to modem device struct
 *
 * @return 0 - 5 of reception level (0 = very bad reception or n.a.)
 */
int modem_get_signal_level(modem_t *modem)
{
    int ret = 0;
    int i, level;
    char rsp[MODEM_SMALL_MSG] = "OK";
    char *buf;

    // check GSM reception level
    modem_write(modem, "AT+CSQ\r");
    modem_read(modem, rsp, MODEM_SMALL_MSG);
    buf = strstr(rsp, "+CSQ:");
    if (buf != NULL)
    {
        strcpy(rsp, buf + 6);
        i=0;
        while (i < (int)strlen(rsp))
        {
            if (rsp[i] == ',')
            {
                rsp[i] = 0;
            }
            i++;
        }
        level = atoi(rsp);
        ret = 0;                 // -113 dBm or less
        if (level > 0 ) ret = 1; // -111 dBm
        if (level > 1 ) ret = 2; //  -109
        if (level > 20) ret = 3; //      ...
        if (level > 27) ret = 4; //         -53 dBm
        if (level > 30) ret = 5; // -51 dBm or more
        if (level > 31) ret = 0; // level unknown
    }
    return ret;
}


/*******************************************************************************
 *
 *
 *
 *  GSM MODEM specific SMS commands
 *
 *
 *
 ******************************************************************************/
/**
 * @brief Get SMS from modem
 *
 * Get a received message (AT+CMGR=); return the message location from the first message
 * found. the SMS message is returned in message
 *
 * @param modem       pointer to modem device struct
 * @param message     the sms message retreived from the modem
 *
 * @return the message storage location of the message, zero if no message found
 */
int modem_get_sms(modem_t *modem, char *message)
{
    int message_idx;
    int max_msg_idx;
    char *message_bgn;
    char *message_end;

    // get number of messages stored
    max_msg_idx = modem_get_max_sms(modem);


    for (message_idx = max_msg_idx; message_idx > 0; message_idx--)
    {
        modem_write(modem, "AT+CMGR=");
        modem_int_to_str(message_idx, message);
        modem_write(modem, message);
        modem_write(modem, "\r");

        strcpy(message, "OK");
        modem_read(modem, message, MODEM_SMALL_MSG);
        // check if this message was stored in "received message" (either read or unread) memory
        if ( strstr(message, "OK") && !strstr(message, "+CMGR: 0,,0") &&
             (strstr(message, " 1,") || strstr(message, " 0,")) )
        {
            // strip the |+CMGR: 0, , 0||OK| part of the message
            message_bgn = strstr(message+1, "\r");
            message_end = strstr(message_bgn + 1, "\r");
            message_end[0] = 0;
            if (strlen(message_bgn) > 2)
            {
                strcpy(message, message_bgn + 2);
                break;
            }
        }
    }
    return message_idx;
}


/**
 * @brief Send a pdu encoded SMS message.
 *
 * @param modem     pointer to modem device struct
 * @param pdu       pdu encoded message to send
 *
 * @return   Zero if sending the SMS failed, non-zero on success.
 */
int modem_send_sms(modem_t *modem, char *pdu)
{
    int ret = 0;
    int length;
    char buf[MODEM_SMALL_MSG];

    // pdu msg includes CTRL-Z and SMSC address octets: donot count them in length
    length = (strlen(pdu) - 3) / 2;
    modem_int_to_str(length, buf);
    modem_write(modem, "AT+CMGS=");
    modem_write(modem, buf);
    modem_write(modem, "\r");
    if (modem_read(modem, ">", 0))
    {
        modem_write(modem, pdu);
        // send actual message
        modem_sleep(10000000);
        ret = modem_read(modem, "OK", 0);
    }
    return ret;
}


/**
 * @brief Get the maximum number of messages from modem
 *
 * @param modem     pointer to modem device struct
 *
 * @return number of max. messages (or default 25 on error)
 */
int modem_get_max_sms(modem_t *modem)
{
    char rsp[MODEM_SMALL_MSG] = "OK";
    unsigned int pos;
    char * number;
    static int msg_nr      = 20;
    static int passed_once = 0;

    if (passed_once==0)
    {
        modem_write(modem, "AT+CPMS?\r");
        // reply like +CPMS: <mem1>, used1, total1, <mem2>, used2, total2, <mem3>, used3, total3 OK
        if (modem_read(modem, rsp, MODEM_SMALL_MSG))
        {
            // find total entries for mem1
            pos = 0;
            number = rsp;
            while ((number[pos] != ',') && (pos < strlen(number)))
            {
                pos++;
            }
            if (pos < strlen(number))
            {
                pos++;  // skip this comma !

                while ((number[pos] != ',') && (pos < strlen(number)))
                {
                    pos++;
                }
                if (pos < strlen(number))
                {
                    number = number + pos + 1;
                    pos = 0;
                    while ((number[pos] != ',') && (pos < strlen(number)))
                    {
                        pos++;
                    }
                    if (pos < strlen(number))
                    {
                        number[pos] = 0;
                        msg_nr = atoi(number);
                        // check for sanity
                        if (msg_nr > 0)
                        {
                            passed_once = 1;
                        }
                    }
                }
            }
        }
    }
    return msg_nr;
}


/**
 * @brief Try setting PDU mode and check for "OK"
 *
 * @param modem     pointer to modem device struct
 *
 * @return 1 if PDU mode is supported, 0 otherwise
 */
int modem_set_pdu_mode(modem_t *modem)
{
    // check if PDU mode is supported
    modem_write(modem, "AT+CMGF=0\r");
    return modem_read(modem, "OK", 0) ? 1 : 0;
}


/**
 * @brief Delete specified message
 *
 * @param modem     pointer to modem device struct
 * @param index     modem memory location of message to be deleted
 *
 * @return 1 on succes, 0 otherwise
 */
int modem_remove_sms_message(modem_t *modem, int index)
{
    char buf[MODEM_SMALL_MSG];

    modem_int_to_str(index, buf);
    modem_write(modem, "AT+CMGD=");
    modem_write(modem, buf);
    modem_write(modem, "\r");
    return modem_read(modem, "OK", 0)? 1 : 0;
}



/*******************************************************************************
 *
 *
 *
 *  VOICE MODEM specific commands
 *
 *
 *
 *
 ******************************************************************************/
/**
 * @brief Set the modem in voice mode
 *
 * @param modem     pointer to modem device struct
 *
 * @return Zero if setting to voice mode failed, non-zero on success.
 */
int modem_set_voice_mode(modem_t *modem)
{
    if (modem->kind == MODEM_KIND_MODEM_TELIT)
        return 0;

    modem_write(modem, "AT#CLS=8\r");
    return modem_read(modem, "OK", 0)? 1 : 0;
}


/**
 * @brief Send a wavefile to modem to transmit to caller
 *
 * This function only is only tested on a modem using a rockwell chipset.
 * The wav file must be in rockwell-4 format.
 *
 * @param *modem   pointer to modem device struct
 * @param *wav     pointer to begin of a buffer containing a wav file
 * @param size     size of the wav buffer
 *
 * @return   Zero if sending the wave file failed, non-zero on success.
 */
int modem_send_wave(modem_t *modem, unsigned char *wav, int size)
{
    int i;
    int ret = 0;

    if (modem->kind == MODEM_KIND_MODEM_TELIT)
        return ret;

    modem_write(modem, "AT#VTX\r");
    if(modem_read(modem, "CONNECT", 0))
    {
        for (i=0; i<size; i++)
            modem->serial.f_putchar(modem->serial.device, wav[i]);
        modem_write(modem, "\x10\x03");  //send <DLE><ETX>
        ret = 1;
    }
    return ret;
}


/**
 * @brief Remove DLE - Data Link Escape 0x10 chars from buf
 *
 * @param buf          a string (0 terminated)
 *
 * @return Nothing.
 */
void modem_filter_dle(char *buf)
{
    int size = strlen(buf);
    int i;
    int j = 0;

    for (i = 0; i < size + 1; i++)
    {
        if ( buf[i] != '\x10')
        {
            buf[j] = buf[i];
            j++;
        }
    }
    buf[j] = 0;
}

/**
 * @brief turn modem echo off
 *
 * @param *modem     pointer to modem device struct
 *
 * @return Nothing.
 */
int modem_echo_off(modem_t *modem)
{
    modem_write(modem, "ATE0\r");
    return modem_read(modem, "OK", 0) ? 1 : 0;
}

/**
 * @brief turn modem echo on
 *
 * @param *modem     pointer to modem device struct
 *
 * @return Nothing.
 */
int modem_echo_on(modem_t *modem)
{
    modem_write(modem, "ATE1\r");
    return modem_read(modem, "OK", 0) ? 1 : 0;
}

/**
 * @brief answer phone call
 *
 * @param *modem     pointer to modem device struct
 *
 * @return Nothing.
 */
int modem_answer(modem_t *modem)
{
    int ret;
    modem_write(modem, "ATA\r");
    ret = modem_read(modem, "OK", 0) ? 1 : 0;
    if (ret)
        modem->isRinging = false;
    return ret;
}

/**
 * @brief place phone call
 *
 * @param *modem     pointer to modem device struct
 * @param *number    pointer to phone number
 *
 * @return Nothing.
 */
int modem_place_call(modem_t *modem, const char *number)
{
    modem_write(modem, "ATD ");
    modem_write(modem, number);
    if (modem->kind == MODEM_KIND_MODEM_TELIT)
        modem_write(modem, ";");
    modem_write(modem, "\r");

    return modem_read(modem, "OK", 0) ? 1 : 0;
}

/**
 * @brief check if phone is ringing
 *
 * @param *modem     pointer to modem device struct
 *
 * @return Nothing.
 */
int modem_is_ringing(modem_t *modem)
{
    if (modem->isRinging)
    {
        return 1;
    }
    else
    {
        return modem_read(modem, "RING", 0) ? 1 : 0;
    }
}


/**
 * @brief check if phone is on the hook
 *
 * @param *modem     pointer to modem device struct
 *
 * @return Nothing.
 */
int modem_is_hangup(modem_t *modem)
{
    if (modem->isHangup)
    {
        return 1;
    }
    else
    {
        return modem_read(modem, "NO CARRIER", 0) ? 1 : 0;
    }
}

