/*****************************************************************************\
|*
|*  VERSION CONTROL:    $Version$   $Date$
|*
|*  IN PACKAGE:         PDU SMS Coder/Decoder
|*
|*  COPYRIGHT:          Copyright (c) 2007, Altium
|*
|*  DESCRIPTION:        handling of PDU encoded SMS messages
|*
 */
#ifndef _SMS_H_
#define _SMS_H_

#include "smsg_type.h"

extern void sms_pdu_decode(char * pdu, smsg_t * sms_message);
extern void sms_pdu_encode(char * message, char * dest_nr, char * pdu);

#endif
