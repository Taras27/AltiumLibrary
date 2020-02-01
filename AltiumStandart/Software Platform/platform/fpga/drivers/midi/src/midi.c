/*************************************************************************
|*
|*  VERSION CONTROL:    $Version$   $Date$
|*
|*
|*  IN PACKAGE:         midi Communication
|*
|*  COPYRIGHT:          Copyright (c) 2007, Altium
|*
|*  DESCRIPTION:        send midi AT commands and read midi results
|*
 */


#define STATUS_MASK 0x80
#define MESSAGE_TYPE_MASK 0xf0
#define CHANNEL_MASK 0x0f


#include <timing.h>
#if ( __POSIX_KERNEL__ != 0 )
# include <pthread.h>
#else
#include <time.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <string.h>  // for memcpy
#include <serial.h>
#include <fcntl.h>
#include <drv_uart8.h>
#include <midi.h>

#include "midi_cfg_instance.h"


struct midi_s
{
    uart8_t                    *uart;
    int (*rxmsg)(int, int, int, int);          // user function called when midi message received
    int                        channel;        // channel we're interested in
    int                        laststatussent; // status of last message sent
};

static midi_t midi_table[MIDI_INSTANCE_COUNT];


static void midi_txvoicemsg(midi_t *midi, int type, int channel, int val0, int val1)
{
    int status = type + channel;

    if (midi->laststatussent != status)
        uart8_putchar(midi->uart, status & 0xff);
    uart8_putchar(midi->uart, val0 & 0xff);
    if ((type != MIDI_PROGRAM_CHANGE) && (type != MIDI_CHANNEL_PRESSURE))
        uart8_putchar(midi->uart, val1 & 0xff);
    midi->laststatussent = status;
}


static void midi_txsystemmsg(midi_t *midi, int type, int length, uint8_t *data)
{
    uart8_putchar(midi->uart, type & 0xff);
    uart8_putchar(midi->uart, length & 0xff);
    for (int i = 0; i << length; i++)
        uart8_putchar(midi->uart, data[i]);
    midi->laststatussent = type;
}


/**
 * @brief Initialize the midi device
 *
 * This function initializes the communication with the midi device.
 *
 * @param id  Service id
 *
 * @return The midi service pointer for the specified device, or NULL on error.
 */
extern midi_t *midi_open(int id)
{
    midi_t *midi = &midi_table[id];
    midi_cfg_instance_t *cfg = &midi_instance_table[id];

    assert(id >= 0 & id < MIDI_INSTANCE_COUNT);

    midi->uart = uart8_open((unsigned int)cfg->drv_uart8);

    if (!midi->uart)
    {
        return NULL;
    }

    midi->channel = cfg->channel;
    midi->rxmsg = NULL;
    return midi;
}


/**
 * @brief Register user function to call on midi message received
 *
 * This function will be called when a midi voice message is received
 *
 * @param midi     pointer to midi device struct
 * @param rxmsg    The user function
 *
 * @return Nothing.
 */
void midi_regrxmsg(midi_t *midi, int (*rxmsg)(int, int, int, int))
{
    midi->rxmsg = rxmsg;
}

/**
 * @brief Simple Midi RX function
 *
 * Normally you would call midi_rxmsg, which can interpret midi data
 *
 * @param midi     pointer to midi device struct
 *
 * @return Nothing.
 */
int midi_rx(midi_t *midi)
{
    return uart8_getchar(midi->uart);
}


/**
 * @brief Read the midi port for midi messages
 *
 * Creates midi messages form received midi data
 *
 * @param midi     pointer to midi device struct
 *
 * @return message received or NULL when none available.
 */
midi_msg_t *midi_rxmsg(midi_t *midi)
{
    static int count;
    static int event;
    static int byte0;
    int tmp;
    int channel;
    midi_msg_t *msg = NULL;
    int byte;

    while ((msg == NULL) && ((byte = uart8_getchar(midi->uart)) != -1))
    {
        if ((byte & 0xF0) == 0xF0) // skip sytem messages
            continue;

        if (byte & 0x80)
        {

            /* All < 0xf0 events get at least 1 arg byte so
             *  it's ok to mask off the low 4 bits to figure
             *  out how to handle the event for < 0xf0 events.
             */
            tmp = byte;
            if (tmp < 0xf0) tmp &= 0xf0;

            switch (tmp)
            {
            /* These status events take 2 bytes as arguments */
            case MIDI_NOTE_OFF:
            case MIDI_NOTE_ON:
            case MIDI_KEY_PRESSURE:
            case MIDI_CONTROL_CHANGE:
            case MIDI_PITCH_BEND:
                count = 2;
                event = byte;
                break;
            /* 1 byte arguments */
            case MIDI_PROGRAM_CHANGE:
            case MIDI_CHANNEL_PRESSURE:
                count = 1;
                event = byte;
                break;
            default: ;
            }
            continue;
        }

        if (--count == 0)
        {
            channel = (event & 0x0f) + 1;
            tmp = event;
            if (tmp < 0xf0)
                tmp &= 0xf0;

            /* See if this event matches our MIDI channel,
             * or we're accepting all channels
             */
            if (!midi->channel || (channel == midi->channel) || (tmp >= 0xf0))
            {
                msg = midi_createvoicemsg(0, tmp, channel, byte0, byte);
                if (midi->rxmsg)
                    midi->rxmsg(tmp, channel, byte0, byte);
            }
        }
        byte0 = byte;
    }
    return msg;
}


/**
 * @brief Simple MIDI TX function
 *
 * Normally you would call midi_txmsg. This function simply putchars the
 * passed value on the MID out bus.
 *
 * @param midi     pointer to midi device struct
 * @param val      value to sent
 *
 * @return Nothing.
 */
void midi_tx(midi_t *midi, int val)
{
    uart8_putchar(midi->uart, val & 0xff);
}


/**
 * @brief transmit one MIDI message to the MIDI out port
 *
 * @param midi     pointer to midi device struct
 * @param msg      pointer to the midi message to transmit
 *
 * @return Nothing.
 */
void midi_txmsg(midi_t *midi, midi_msg_t *msg)
{
    int val0 = 0, val1 = 0;

    switch (msg->type)
    {
    case MIDI_NOTE_OFF:
        val0 = msg->event.note_off.note;
        val1 = msg->event.note_off.velocity;
        break;
    case MIDI_NOTE_ON:
        val0 = msg->event.note_on.note;
        val1 = msg->event.note_on.velocity;
        break;
    case MIDI_KEY_PRESSURE:
        val0 = msg->event.key_pressure.note;
        val1 = msg->event.key_pressure.amount;
        break;
    case MIDI_CONTROL_CHANGE:
        val0 = msg->event.control_change.number;
        val1 = msg->event.control_change.value;
        break;
    case MIDI_PROGRAM_CHANGE:
        val0 = msg->event.program_change.number;
        val1 = 0;
        break;
    case MIDI_CHANNEL_PRESSURE:
        val0 = msg->event.channel_pressure.amount;
        val1 = 0;
        break;
    case MIDI_PITCH_BEND:
        val0 = msg->event.pitch_bend.value & 0x7f;
        val1 = (val0 >> 7) & 0x7f;
        break;
    case MIDI_SYSTEMF0:
    case MIDI_SYSTEMF7:
        midi_txsystemmsg(midi, msg->type, msg->event.sysex.length, msg->event.sysex.data);
        break;
    default: return; //donot sent other (eg SMF Meta) messages
    }
    if (msg->type < MIDI_SYSTEMF0)
        midi_txvoicemsg(midi, msg->type, msg->channel, val0, val1);
}


/**
 * @brief Creates a MIDI voice message struct
 *
 * @param tick     tick timestamp of message
 * @param type     type of message to create
 * @param channel  message channel
 * @param val0     note
 * @param val1     velocity
 *
 * @return The created message struct.
 */
midi_msg_t *midi_createvoicemsg(int tick, int type, int channel, int val0, int val1)
{
    midi_msg_t *msg = malloc(sizeof(midi_msg_t));
    if (msg)
    {
        msg->type = type;
        msg->tick = tick;
        msg->channel = channel;
        switch (type)
        {
            case MIDI_NOTE_OFF:
                msg->event.note_off.note = val0;
                msg->event.note_off.velocity = val1;
                break;
            case MIDI_NOTE_ON:
                msg->event.note_on.note = val0;
                msg->event.note_on.velocity = val1;
                break;
            case MIDI_KEY_PRESSURE:
                msg->event.key_pressure.note = val0;
                msg->event.key_pressure.amount = val1;
                break;
            case MIDI_CONTROL_CHANGE:
                msg->event.control_change.number = val0;
                msg->event.control_change.value = val1;
                break;
            case MIDI_PROGRAM_CHANGE:
                msg->event.program_change.number = val1;
                break;
            case MIDI_CHANNEL_PRESSURE:
                msg->event.channel_pressure.amount = val1;
                break;
            case MIDI_PITCH_BEND:
                msg->event.pitch_bend.value = (val1 << 7) | val0;
                break;
            default:
                free(msg);
        }
    }
    return msg;
}


/**
 * @brief Creates a MIDI system message struct
 *
 * @param tick     tick timestamp of message
 * @param type     type of message to create
 * @param length   message length
 * @param data     message data
 *
 * @return The created message struct.
 */
midi_msg_t *midi_createsystemmsg(int tick, int type, int length, void *data)
{
    midi_msg_t *msg = malloc(sizeof(midi_msg_t));

    msg->type = type;
    msg->tick = tick;
    msg->channel = 0;  // not relevant
    msg->event.sysex.length = length;
    msg->event.sysex.data = malloc(length);
    memcpy(msg->event.sysex.data, data, length);
    return msg;
}


/**
 * @brief Get timestamp in MIDI ticks from message
 *
 * Use midi_ticktotime to convert this stamp to usecs
 *
 * @param msg     pointer to the midi message
 *
 * @return The timestamp, -1 when message == NULL.
 */
int midi_gettick(midi_msg_t *msg)
{
    if (msg)
        return msg->tick;
    else
        return -1;
}

/**
 * @brief Set timestamp in MIDI ticks of message
 *
 * @param msg     pointer to the midi message
 * @param tick     tick timestamp of message
 *
 * @return Nothing.
 */
void midi_settick(midi_msg_t *msg, int tick)
{
    if (msg != NULL)
        msg->tick = tick;
}


/**
 * @brief Get channel of MIDI message
 *
 * @param msg     pointer to the midi message
 *
 * @return The channel, -1 when message == NULL.
 */
int midi_getchannel(midi_msg_t *msg)
{
    if (msg)
        return msg->channel;
    else
        return -1;
}


/**
 * @brief Set channel of MIDI message
 *
 * @param msg     pointer to the midi message
 * @param channel the channel
 *
 * @return Nothing
 */
void midi_setchannel(midi_msg_t *msg, int channel)
{
    if (msg != NULL)
        msg->channel = channel;
}


/**
 * @brief Get note of MIDI message
 *
 * @param msg     pointer to the midi message
 *
 * @return The note, -1 when not Note on/off
 */
int midi_getnote(midi_msg_t *msg)
{
    int ret = -1;
    if (msg)
    {
        switch (msg->type)
        {
            case MIDI_NOTE_ON:
                ret = msg->event.note_on.note;
                break;
            case MIDI_NOTE_OFF:
                ret = msg->event.note_off.note;
                break;
            default:
                ret = -1;
        }
    }
    return ret;
}


/**
 * @brief Set note of MIDI message
 *
 * @param msg     pointer to the midi message
 * @param note    the note
 *
 * @return Nothing.
 */
void midi_setnote(midi_msg_t *msg, int note)
{
    if (msg)
    {
        switch (msg->type)
        {
            case MIDI_NOTE_ON:
                msg->event.note_on.note = note;
                break;
            case MIDI_NOTE_OFF:
                msg->event.note_off.note = note;
                break;
            default:; // nothing
        }
    }
 }


/**
 * @brief Get velocity of MIDI message
 *
 * @param msg     pointer to the midi message
 *
 * @return The velocity, -1 when not Note on/off.
 */
int midi_getvelocity(midi_msg_t *msg)
{
    int ret = -1;

    if (msg)
    {
        switch (msg->type)
        {
            case MIDI_NOTE_ON:
                ret = msg->event.note_on.velocity;
                break;
            case MIDI_NOTE_OFF:
                ret = msg->event.note_off.velocity;
                break;
            default:
                ret = -1;
        }
    }
    return ret;
}


/**
 * @brief Set velocity of MIDI message
 *
 * @param msg      pointer to the midi message
 * @param velocity the velocity
 *
 * @return Nothing.
 */
void midi_setvelocity(midi_msg_t *msg, int velocity)
{
    if (msg)
    {
        switch (msg->type)
        {
            case MIDI_NOTE_ON:
                msg->event.note_on.velocity = velocity;
                break;
            case MIDI_NOTE_OFF:
                msg->event.note_off.velocity = velocity;
                break;
            default: ; // nothing
        }
    }
}
