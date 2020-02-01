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
/**
 * @file
 * Service enabling you to communicate with Modems using hayes AT commands
 *
 * The API supports Hayes AT commands in three major areas:<BR>
 * -# General commands like dialing a number disconnecting the line etc.<BR>
 * -# GSM commands like SMS handling, setting a pin code etc.<BR>
 * -# Voice modem commands like sending a wave file etc.<BR>
 *
 * Functions ( sms_pdu_encode() and sms_pdu_decode() ) to decode and 
 * encode pdu encoded sms messages are delivered through the sms.h library.
 */
#ifndef _MODEM_H_
#define _MODEM_H_

#ifdef  __cplusplus
extern "C" {
#endif

#define MODEM_SMS_SIZE    512

#include <stdint.h>
#include <stdbool.h>
//#include "sms.h"

typedef struct modem_drv_s modem_t;

/* generic modem handling: */
extern modem_t *modem_open(int id);
extern int modem_read (modem_t *modem, char *rsp, size_t size);
extern int modem_write(modem_t *modem, const char *cmd);

/* generic modem AT commands: */
//extern int modem_send_init(const modem_t *modem, const char *init);
extern int modem_send_init     (modem_t *modem);
extern int modem_hangup        (modem_t *modem);
extern int modem_set_autoanswer(modem_t *modem);
extern int modem_echo_off      (modem_t *modem);
extern int modem_echo_on       (modem_t *modem);
extern int modem_answer        (modem_t *modem);
extern int modem_place_call    (modem_t *modem, const char *number);
extern int modem_is_ringing    (modem_t *modem);
extern int modem_is_hangup     (modem_t *modem);

/* GSM modem specific modem AT commands: */
extern int modem_enter_pin       (modem_t *modem, const char *pin_code);
extern int modem_check_network   (modem_t *modem);
extern int modem_get_signal_level(modem_t *modem);

/* GSM modem specific modem SMS AT commands: */
extern int modem_get_sms           (modem_t *modem, char *message);
extern int modem_send_sms          (modem_t *modem, char *pdu);
extern int modem_get_max_sms       (modem_t *modem);
extern int modem_set_pdu_mode      (modem_t *modem);
extern int modem_remove_sms_message(modem_t *modem, int index);

/* Voice modem (Rockwell) specific AT commands: */
extern int  modem_set_voice_mode(modem_t *modem);
extern int  modem_send_wave     (modem_t *modem, unsigned char *wav, int size);
extern void modem_filter_dle(char * buf);


#ifdef  __cplusplus
}
#endif

#endif
